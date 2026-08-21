// 模块：网络通信；作用：实现 TCP 客户端的连接、收发和关闭；编写内容：补充 Boost.Asio 异步网络逻辑。

#include "TcpClient.h"
#include <iostream>
#include <sstream>

namespace net
{
    // 构造函数
    TcpClient::TcpClient(boost::asio::io_context &io)
        : io_(io),
          socket_(io),
          // 创建strand 保证线程安全
          strand_(io),
          // 初始化重连定时器
          reconnectTimer_(io),
          // 初始化超连定时器
          connectTimer_(io)
    {
        NET_LOG_INFO("TcpClient 已创建");
    }

    TcpClient::~TcpClient()
    {
        closeSocket();
        NET_LOG_INFO("TcpClient 已销毁");
    }

    /// 异步连接服务器
    /// host IP地址或域名
    /// port 端口号
    void TcpClient::connect(const std::string &host, uint16_t port)
    {
        // 检查当前状态
        if (state_ == ConnectState::Connected)
        {
            NET_LOG_WARN("已连接了%s:%u,请先断开连接", remoteHost_.c_str(), remotePort_);
            disconnect();
        }

        if (state_ == ConnectState::Connecting)
        {
            NET_LOG_WARN("当前正在连接...");
            return;
        }

        if (state_ == ConnectState::Reconnecting)
        {
            NET_LOG_WARN("当前正在重连...");
            return;
        }

        // 保存连接信息
        remoteHost_ = host;
        remotePort_ = port;
        retryCount_ = 0; // 重置连接计数

        // 更新状态
        state_ = ConnectState::Connecting;
        NET_LOG_INFO("正在连接%s:%u...", host.c_str(), port);

        tcp::resolver resolver(io_);
        boost::system::error_code resolveEc;
        auto endpoints = resolver.resolve(host, std::to_string(port), resolveEc);

        if (resolveEc)
        {
            NET_LOG_INFO("解析地址失败:%s", resolveEc.message().c_str());
            handleError(resolveEc, "解析地址失败");
            state_ = ConnectState::Disconnected;
            return;
        }

        // 设置超时连接
        connectTimer_.expires_after(std::chrono::milliseconds(CONNECT_TIMEOUT_MS));
        connectTimer_.async_wait(boost::asio::bind_executor(strand_, [this](const boost::system::error_code &ec)
                                                            {
            //超时回溯
            if(!ec&&state_==ConnectState::Connecting){
                NET_LOG_ERROR("连接超时（%d ms）", CONNECT_TIMEOUT_MS);

                                           // 关闭 socket
                                           closeSocket();

                                           // 创建超时错误码
                                           boost::system::error_code timeoutEc =
                                               boost::asio::error::timed_out;

                                           // 处理错误
                                           handleError(timeoutEc, "连接超时");

                                           // 更新状态
                                           state_ = ConnectState::Disconnected;

                                           // 尝试重连
                                           startReconnect();
            } }));

        // 异步连接
        boost::asio::async_connect(
            socket_,
            endpoints,
            boost::asio::bind_executor(strand_,
                                       [this](const boost::system::error_code &ec,
                                              const tcp::endpoint &endpoint)
                                       {
                                           handleConnect(ec);
                                       }));
    }

    // 异步连接
    void TcpClient::handleConnect(const boost::system::error_code &ec)
    {
        // 取消连接超时定时器
        connectTimer_.cancel();

        if (!ec)
        {
            // 连接成功
            state_ = ConnectState::Connected;
            retryCount_ = 0;

            NET_LOG_INFO("连接成功");

            if (connectCallback_)
            {
                connectCallback_();
            }

            doRead();
        }
        else
        {
            // 连接失败
            NET_LOG_ERROR("连接失败: %s", ec.message().c_str());

            closeSocket();
            state_ = ConnectState::Disconnected;

            if (errorCallback_)
            {
                errorCallback_(ec, "连接失败");
            }

            startReconnect();
        }
    }

    // 发起异步读取
    void TcpClient::doRead()
    {
        // 检查连接状态
        if (!isConnected())
        {
            NET_LOG_WARN("未连接，无法读取数据");
            return;
        }

        // 发起异步读取
        socket_.async_read_some(
            boost::asio::buffer(readBuffer_, READ_BUFFER_SIZE),
            boost::asio::bind_executor(strand_,
                                       [this](const boost::system::error_code &ec, size_t bytesTransferred)
                                       {
                                           handleRead(ec, bytesTransferred);
                                       }));
    }

    // 处理读取结果
    void TcpClient::handleRead(const boost::system::error_code &ec, size_t bytesTransferred)
    {
        // 读取成功 操作成功ec为空
        if (!ec)
        {
            if (bytesTransferred > 0)
            {
                // 1.1 将数据添加到接收累积缓冲区（用于粘包分包处理）
                recvBuffer_.insert(recvBuffer_.end(),
                                   readBuffer_.begin(),
                                   readBuffer_.begin() + bytesTransferred);

                // 1.2 触发数据回调
                if (dataCallback_)
                {
                    // 创建本次读取的数据副本
                    std::vector<char> data(readBuffer_.begin(),
                                           readBuffer_.begin() + bytesTransferred);

                    // 调用回调，传递数据
                    dataCallback_(remoteHost_, remotePort_, data);
                }

                // 1.3 打印日志
                NET_LOG_INFO("收到 %zu 字节数据", bytesTransferred);
            }

            // 1.4 继续读取下一批数据
            doRead();
        }
        // ==================== 2. 对方正常关闭连接 ====================
        else if (ec == boost::asio::error::eof)
        {
            NET_LOG_INFO("服务器正常关闭了连接");

            // 触发断开回调
            if (disconnectCallback_)
            {
                disconnectCallback_();
            }

            // 清理资源
            closeSocket();
            state_ = ConnectState::Disconnected;

            // 尝试重连
            startReconnect();
        }
        // ==================== 3. 操作被取消（主动断开） ====================
        else if (ec == boost::asio::error::operation_aborted)
        {
            NET_LOG_INFO("读取操作被取消");
            // 主动断开，不需要重连
        }
        // ==================== 4. 其他错误 ====================
        else
        {
            NET_LOG_ERROR("读取错误: %s (错误码: %d)", ec.message().c_str(), ec.value());

            // 触发错误回调
            handleError(ec, "读取数据失败");

            // 清理资源
            closeSocket();
            state_ = ConnectState::Disconnected;

            // 尝试重连
            startReconnect();
        }
    }

    // 数据发送 参数（vector）
    void TcpClient::asyncWrite(const std::vector<char> &data)
    {
        asyncWrite(data.data(), data.size());
    }

    // 数据发送 参数（指针，长度）
    void TcpClient::asyncWrite(const char *data, size_t len)
    {
        // 参数检查
        if (data == nullptr || len <= 0)
        {
            NET_LOG_WARN("数据发送为空");
            return;
        }

        // 连接状态检查
        if (!isConnected())
        {
            NET_LOG_ERROR("未连接，无法发送数据");
            return;
        }

        // 将数据加入到发送队列
        std::vector<char> buffer(data, data + len);
        writeQueue_.push_back(std::move(buffer));
        NET_LOG_INFO("数据已加入到发送队列，当前队列大小为%zu", writeQueue_.size());

        if (!isWriting_)
        {
            doWrite();
        }
    }

    // 执行写操作
    void TcpClient::doWrite()
    {
        if (writeQueue_.empty())
        {
            isWriting_ = false;
            return;
        }

        if (!isConnected())
        {
            NET_LOG_ERROR("未连接，无法执行操作");
            writeQueue_.clear();
            isWriting_ = false;
            return;
        }

        isWriting_ = true;
        auto &data = writeQueue_.front();
        // 发起异步写入
        boost::asio::async_write(
            socket_,
            boost::asio::buffer(data),
            boost::asio::bind_executor(strand_,
                                       [this](const boost::system::error_code &ec, size_t bytesTransferred)
                                       {
                                           handleWrite(ec, bytesTransferred);
                                       }));
    }

    // 处理写入结果
    void TcpClient::handleWrite(const boost::system::error_code &ec, size_t bytesTransfeered)
    {
        // 写入成功
        if (!ec)
        {
            NET_LOG_INFO("成功发送%zu字节", bytesTransfeered);
            writeQueue_.pop_front();
            if (!writeQueue_.empty())
            {
                doWrite();
            }
            else
            {
                isWriting_ = false;
            }
        }
        else if (ec == boost::asio::error::operation_aborted)
        {
            NET_LOG_INFO("写操作被取消");
            writeQueue_.clear();
            isWriting_ = false;
        }
        else
        {
            // 写入失败
            NET_LOG_ERROR("写入失败:%s(错误码:%d)", ec.message().c_str(), ec.value());
            writeQueue_.clear();
            isWriting_ = false;
            handleError(ec, "发送数据失败");
            closeSocket();
            state_ = ConnectState::Disconnected;
            startReconnect();
        }
    }

    // 关闭socket
    void TcpClient::closeSocket()
    {
        if (socket_.is_open())
        {
            boost::system::error_code ec;
            socket_.shutdown(tcp::socket::shutdown_both, ec);
            if (ec)
            {
                NET_LOG_ERROR("shutdown失败:%s", ec.message().c_str());
            }
            ec.clear();
            socket_.close(ec);
            if (ec)
            {
                NET_LOG_ERROR("closeSocket失败:%s", ec.message().c_str());
            }
        }
    }

    // 断开连接
    void TcpClient::disconnect()
    {
        boost::system::error_code ec;
        reconnectTimer_.cancel(ec);
        ec.clear();
        connectTimer_.cancel(ec);

        closeSocket();

        writeQueue_.clear();
        isWriting_ = false;
        recvBuffer_.clear();

        state_ = ConnectState::Disconnected;
        autoReconnect_ = false;
        retryCount_ = 0;

        NET_LOG_INFO("已断开连接");
    }

    // 开始重连
    void TcpClient::startReconnect()
    {
        if (!autoReconnect_)
        {
            NET_LOG_INFO("未启用重连机制");
            return;
        }

        if (maxRetryCount_ > 0 && retryCount_ >= maxRetryCount_)
        {
            NET_LOG_ERROR("已达到最大重连次数，此次重连无法进行");
            state_ = ConnectState::Disconnected;
            return;
        }

        retryCount_++;
        state_ = ConnectState::Reconnecting;
        NET_LOG_INFO("将在%d毫秒后进行第%d次重连", reconnectIntervalMs_, retryCount_);

        // 设置重连定时器
        reconnectTimer_.expires_after(std::chrono::milliseconds(reconnectIntervalMs_));
        reconnectTimer_.async_wait(boost::asio::bind_executor(strand_, [this](const boost::system::error_code &ec)
                                                              { handleReconnectTimer(ec); }));
    }

    // 处理重连定时器超时
    void TcpClient::handleReconnectTimer(const boost::system::error_code &ec)
    {
        if (ec == boost::asio::error::operation_aborted)
        {
            NET_LOG_INFO("定时器被取消");
            return;
        }

        if (ec)
        {
            NET_LOG_ERROR("重连定时器错误:%s", ec.message().c_str());
            return;
        }

        if (remoteHost_.empty() || remotePort_ == 0)
        {
            NET_LOG_ERROR("信息缺失，无法重连");
            state_ = ConnectState::Disconnected;
            return;
        }

        closeSocket();

        state_ = ConnectState::Connecting;
        NET_LOG_INFO("尝试重新连接%s%u...", remoteHost_.c_str(), remotePort_);

        // 重新解析地址
        tcp::resolver resolver(io_);
        boost::system::error_code resolveEc;
        auto endpoints = resolver.resolve(remoteHost_, std::to_string(remotePort_), resolveEc);

        if (resolveEc)
        {
            NET_LOG_ERROR("重连时解析地址失败: %s", resolveEc.message().c_str());
            handleError(resolveEc, "重连时解析地址失败");
            state_ = ConnectState::Disconnected;
            return;
        }

        // 设置超时连接定时器
        connectTimer_.expires_after(std::chrono::milliseconds(CONNECT_TIMEOUT_MS));
        connectTimer_.async_wait(
            boost::asio::bind_executor(strand_,
                                       [this](const boost::system::error_code &timerEc)
                                       {
                                           if (!timerEc && state_ == ConnectState::Connecting)
                                           {
                                               NET_LOG_ERROR("重连超时（%d ms）", CONNECT_TIMEOUT_MS);
                                               closeSocket();
                                               state_ = ConnectState::Disconnected;
                                               handleError(boost::asio::error::timed_out, "重连超时");
                                               startReconnect(); // 继续下一次重连
                                           }
                                       }));

        // 发起异步连接
        boost::asio::async_connect(
            socket_,
            endpoints,
            boost::asio::bind_executor(strand_,
                                       [this](const boost::system::error_code &connectEc,
                                              const tcp::endpoint &endpoint)
                                       {
                                           handleConnect(connectEc);
                                       }));
    }
}