// 模块：网络通信
// 作用：声明 TCP 客户端接口和状态
// 编写内容：连接、发送、接收、错误处理、自动重连、心跳保活、线程安全

#ifndef TCPCLIENT_H
#define TCPCLIENT_H

#include <functional>
#include <vector>
#include <string>
#include <array>
#include <deque>
#include <cstdint>
#include <memory>

#include <boost/asio.hpp>
#include <boost/asio/steady_timer.hpp>

// 日志（后续接入spdlog时替换）
// #include <spdlog/spdlog.h>
#define NET_LOG_INFO(fmt, ...) /* TODO: 替换为spdlog */ printf("[TcpClient][INFO] " fmt "\n", ##__VA_ARGS__)
#define NET_LOG_WARN(fmt, ...) /* TODO: 替换为spdlog */ printf("[TcpClient][WARN] " fmt "\n", ##__VA_ARGS__)
#define NET_LOG_ERROR(fmt, ...) /* TODO: 替换为spdlog */ printf("[TcpClient][ERROR] " fmt "\n", ##__VA_ARGS__)

namespace net
{

    using boost::asio::ip::tcp;

    /// 连接状态枚举
    enum class ConnectState
    {
        Disconnected, // 未连接
        Connecting,   // 连接中
        Connected,    // 已连接
        Reconnecting  // 重连中
    };

    class TcpClient
    {
    public:
        // ==================== 回调类型定义 ====================

        /// 收到数据回调（远端地址, 远端端口, 数据内容）
        using DataCallback = std::function<void(const std::string &remoteAddr,
                                                uint16_t remotePort,
                                                const std::vector<char> &data)>;

        /// 连接成功回调
        using ConnectCallback = std::function<void()>;

        /// 连接断开回调
        using DisconnectCallback = std::function<void()>;

        /// 错误回调（错误码, 错误描述）
        using ErrorCallback = std::function<void(const boost::system::error_code &ec,
                                                 const std::string &errorMsg)>;

        // ==================== 构造与析构 ====================

        /// 构造TCP客户端
        /// io Boost.Asio的io_context引用，由上层统一管理
        explicit TcpClient(boost::asio::io_context &io);

        ~TcpClient();

        // 禁止拷贝
        TcpClient(const TcpClient &) = delete;
        TcpClient &operator=(const TcpClient &) = delete;

        // 允许移动
        TcpClient(TcpClient &&) noexcept = default;
        TcpClient &operator=(TcpClient &&) noexcept = default;

        // ==================== 连接管理 ====================

        /// 异步连接服务器
        /// host IP地址或域名
        /// port 端口号
        void connect(const std::string &host, uint16_t port);

        /// 断开连接（优雅关闭）
        void disconnect();

        /// 设置自动重连
        /// enable 是否启用自动重连
        /// intervalMs 重连间隔（毫秒）
        /// maxRetries 最大重连次数，0表示无限重连
        void setAutoReconnect(bool enable, int intervalMs = 3000, int maxRetries = 5);

        /// 获取当前连接状态
        ConnectState getState() const { return state_; }

        /// 是否已连接
        bool isConnected() const { return state_ == ConnectState::Connected; }

        // ==================== 数据收发 ====================

        /// 异步发送数据（vector版本）
        void asyncWrite(const std::vector<char> &data);

        /// 异步发送数据（指针+长度版本）
        void asyncWrite(const char *data, size_t len);

        // ==================== 回调设置 ====================

        /// 设置数据接收回调
        void setDataCallback(DataCallback cb)
        {
            dataCallback_ = std::move(cb);
        }

        /// 设置连接成功回调
        void setConnectCallback(ConnectCallback cb)
        {
            connectCallback_ = std::move(cb);
        }

        /// 设置连接断开回调
        void setDisconnectCallback(DisconnectCallback cb)
        {
            disconnectCallback_ = std::move(cb);
        }

        /// 设置错误回调
        void setErrorCallback(ErrorCallback cb)
        {
            errorCallback_ = std::move(cb);
        }

        // ==================== 信息查询 ====================

        /// 获取远端IP地址
        std::string remoteAddress() const;

        /// 获取远端端口号
        uint16_t remotePort() const;

        /// 获取本地端口号
        uint16_t localPort() const;

    private:
        // ==================== 内部方法 ====================

        /// 发起异步读取
        void doRead();

        /// 处理连接结果
        void handleConnect(const boost::system::error_code &ec);

        /// 处理读取结果
        void handleRead(const boost::system::error_code &ec, size_t bytesTransferred);

        /// 处理写入结果
        void handleWrite(const boost::system::error_code &ec, size_t bytesTransferred);

        /// 执行实际的写操作（从队列中取出数据发送）
        void doWrite();

        /// 统一错误处理
        void handleError(const boost::system::error_code &ec, const std::string &context);

        /// 开始重连流程
        void startReconnect();

        /// 处理重连定时器超时
        void handleReconnectTimer(const boost::system::error_code &ec);

        /// 关闭socket并清理资源
        void closeSocket();

        // ==================== 核心成员 ====================

        /// Boost.Asio io_context引用
        boost::asio::io_context &io_;

        /// TCP socket
        tcp::socket socket_;

        /// strand，保证回调在同一线程执行（线程安全）
        boost::asio::io_context::strand strand_;

        // ==================== 连接状态 ====================

        /// 当前连接状态
        ConnectState state_ = ConnectState::Disconnected;

        /// 远端地址
        std::string remoteHost_;

        /// 远端端口
        uint16_t remotePort_ = 0;

        // ==================== 读写缓冲 ====================

        /// 读取缓冲区
        static constexpr size_t READ_BUFFER_SIZE = 8192;
        std::array<char, READ_BUFFER_SIZE> readBuffer_;

        /// 接收累积缓冲区（用于粘包分包处理）
        std::vector<char> recvBuffer_;

        /// 发送数据队列（防止并发async_write）
        std::deque<std::vector<char>> writeQueue_;

        /// 是否正在写入中
        bool isWriting_ = false;

        // ==================== 回调 ====================

        DataCallback dataCallback_;
        ConnectCallback connectCallback_;
        DisconnectCallback disconnectCallback_;
        ErrorCallback errorCallback_;

        // ==================== 重连机制 ====================

        /// 是否启用自动重连
        bool autoReconnect_ = false;

        /// 重连间隔（毫秒）
        int reconnectIntervalMs_ = 3000;

        /// 最大重连次数（0=无限重连）
        int maxRetryCount_ = 5;

        /// 当前已重连次数
        int retryCount_ = 0;

        /// 重连定时器
        boost::asio::steady_timer reconnectTimer_;

        // ==================== 连接超时 ====================

        /// 连接超时定时器
        boost::asio::steady_timer connectTimer_;

        /// 连接超时时间（毫秒）
        static constexpr int CONNECT_TIMEOUT_MS = 5000;
    };

} // namespace net

#endif // TCPCLIENT_H
