// 模块：网络通信；作用：管理单个传感器设备会话；编写内容：补充设备连接、心跳、重连和数据接收流程。

#include "SensorSession.h"
#include <cstdio>

// 日志宏
#ifndef SESSION_LOG_INFO
#define SESSION_LOG_INFO(fmt, ...) printf("[SensorSession][INFO]  " fmt "\n", ##__VA_ARGS__)
#endif
#ifndef SESSION_LOG_WARN
#define SESSION_LOG_WARN(fmt, ...) printf("[SensorSession][WARN]  " fmt "\n", ##__VA_ARGS__)
#endif
#ifndef SESSION_LOG_ERROR
#define SESSION_LOG_ERROR(fmt, ...) printf("[SensorSession][ERROR] " fmt "\n", ##__VA_ARGS__)
#endif

namespace net
{
    // 构造与析构
    SensorSession::SensorSession(boost::asio::io_context &io, const Config &config)
        : io_(io), config_(config), heartbeatTimer_(io), reconnectTimer_(io)
    {
        SESSION_LOG_INFO("创建会话: %s:%u deviceId=%u",
                         config.host.c_str(), config.port, config.deviceId);

        active_ = false;
        retryCount_ = 0;

        // 创建 TcpClient（共享 io_context）
        client_ = std::make_unique<TcpClient>(io);

        // 创建 ProtocolParser
        parser_ = std::make_unique<protocol::ProtocolParser>();

        // ---- 绑定 TcpClient 回调 ----
        // 连接成功 → 重置重连计数 + 启动心跳
        client_->setConnectCallback(
            [this]()
            {
                SESSION_LOG_INFO("连接成功 deviceId=%u", config_.deviceId);
                retryCount_ = 0;
                startHeartbeatLoop();
                if (statusCallback_)
                    statusCallback_(SessionStatus::Connected);
            });

        // 收到数据 → 喂给 ProtocolParser
        client_->setDataCallback(
            [this](const std::string &addr, uint16_t port,
                   const std::vector<char> &data)
            {
                onDataReceived(addr, port, data);
            });

        // 连接断开 → 记录日志
        client_->setDisconnectCallback(
            [this]()
            { onDisconnected(); });

        // 网络错误 → 转发给上层
        client_->setErrorCallback(
            [this](const boost::system::error_code &ec, const std::string &msg)
            {
                SESSION_LOG_ERROR("网络错误: %s", msg.c_str());
                if (errorCallback_)
                    errorCallback_(msg);
            });

        // ---- 绑定 ProtocolParser 回调 ----
        // 合法帧 → 区分心跳回复和数据帧
        parser_->setFrameCallback(
            [this](const common::Frame &frame)
            { onFrameParsed(frame); });

        // 解析错误 → 转发给上层
        parser_->setErrorCallback(
            [this](const std::string &reason)
            { onParseError(reason); });

        // ---- 关闭 TcpClient 的自动重连（由 SensorSession 统一管理重连）----
        client_->setAutoReconnect(false);
    }

    SensorSession::~SensorSession()
    {
        active_ = false;
        heartbeatTimer_.cancel();
        reconnectTimer_.cancel();
        if (client_)
            client_->disconnect();
    }

    // 连接服务器
    void SensorSession::connect() {
        active_=true;
        retryCount_=0;

        SESSION_LOG_INFO("正在连接 %s:%u ...",config_.host.c_str(),config_.port);
        client_->connect(config_.host,config_.port);
    }

    // 断开连接
    void SensorSession::disconnect()
    {
        if (!active_)
            return;
        active_ = false;

        SESSION_LOG_INFO("断开连接 deviceId=%u", config_.deviceId);

        // 停掉所有定时器
        heartbeatTimer_.cancel();
        reconnectTimer_.cancel();

        // 断开 TCP 连接
        if (client_)
            client_->disconnect();

        // 通知上层状态变化
        if (statusCallback_)
            statusCallback_(SessionStatus::Disconnected);
    }

    // ============================================================================
    // 回调设置
    // ============================================================================

    // 设置帧回调（收到 CRC 校验通过的合法帧时触发，心跳回复帧除外）
    void SensorSession::setFrameCallback(FrameCallback cb)
    {
        frameCallback_ = std::move(cb);
    }

    void SensorSession::setErrorCallback(ErrorCallback cb)
    {
        errorCallback_ = std::move(cb);
    }

    void SensorSession::setStatusCallback(StatusCallback cb)
    {
        statusCallback_ = std::move(cb);
    }

    // ============================================================================
    // 数据链路
    // ============================================================================

    void SensorSession::onDataReceived(const std::string & /*addr*/,
                                       uint16_t /*port*/,
                                       const std::vector<char> &data)
    {
        if (!active_ || data.empty())
            return;

        // char 和 uint8_t 内存布局相同，直接 reinterpret_cast（零拷贝）
        parser_->feed(reinterpret_cast<const uint8_t *>(data.data()), data.size());
    }

    void SensorSession::onFrameParsed(const common::Frame &frame)
    {
        if (!active_)
            return;

        // 检查是否为心跳回复（payload = 0x00 0x01）
        if (frame.payload.size() == 2 &&
            frame.payload[0] == 0x00 && frame.payload[1] == 0x01)
        {
            SESSION_LOG_INFO("收到心跳回复 deviceId=%u", frame.deviceId);
            // 心跳回复：重置超时定时器（不需要重启心跳循环，它自己在跑）
            heartbeatTimer_.cancel();
            heartbeatTimer_.expires_after(
                std::chrono::milliseconds(config_.heartbeatTimeoutMs));
            heartbeatTimer_.async_wait(
                [this](const boost::system::error_code &ec)
                {
                    onHeartbeatTimeout(ec);
                });
            return; // 心跳回复帧不传给上层
        }

        // 数据帧：转发给上层
        if (frameCallback_)
            frameCallback_(frame);
    }

    void SensorSession::onDisconnected()
    {
        SESSION_LOG_WARN("连接断开 deviceId=%u", config_.deviceId);
        // 不做自动重连——由心跳超时检测触发重连
        // 如果用户主动 disconnect()，active_ 已经是 false，不会走重连
    }

    void SensorSession::onParseError(const std::string &reason)
    {
        SESSION_LOG_WARN("协议解析错误: %s", reason.c_str());
        if (errorCallback_)
            errorCallback_(reason);
    }

    // ============================================================================
    // 心跳
    // ============================================================================

    void SensorSession::startHeartbeatLoop()
    {
        if (!active_)
            return;

        SESSION_LOG_INFO("启动心跳: 间隔=%dms 超时=%dms",
                         config_.heartbeatIntervalMs, config_.heartbeatTimeoutMs);

        // 第一个心跳在 heartbeatIntervalMs 后发送
        heartbeatTimer_.expires_after(
            std::chrono::milliseconds(config_.heartbeatIntervalMs));
        heartbeatTimer_.async_wait(
            [this](const boost::system::error_code &ec)
            {
                if (ec)
                    return; // cancelled（disconnect 或切换到超时检测）
                if (!active_)
                    return;
                sendHeartbeat();
            });
    }

    void SensorSession::sendHeartbeat()
    {
        if (!active_)
            return;

        // 构建心跳帧并发送
        std::vector<char> heartbeat = buildHeartbeatFrame();
        client_->asyncWrite(heartbeat.data(), heartbeat.size());

        SESSION_LOG_INFO("发送心跳 deviceId=%u", config_.deviceId);

        // 发送后启动超时检测：heartbeatTimeoutMs 内没收到回复 → 判定断线
        heartbeatTimer_.cancel();
        heartbeatTimer_.expires_after(
            std::chrono::milliseconds(config_.heartbeatTimeoutMs));
        heartbeatTimer_.async_wait(
            [this](const boost::system::error_code &ec)
            {
                onHeartbeatTimeout(ec);
            });
    }

    void SensorSession::onHeartbeatTimeout(const boost::system::error_code &ec)
    {
        if (ec)
            return; // cancelled = 收到了心跳回复，正常
        if (!active_)
            return;

        SESSION_LOG_WARN("心跳超时! deviceId=%u retry=%d/%d",
                         config_.deviceId, retryCount_, config_.maxReconnectRetries);

        // 通知上层心跳超时
        if (statusCallback_)
            statusCallback_(SessionStatus::HeartbeatTimeout);

        // 触发重连
        startReconnect();
    }

    // ============================================================================
    // 重连
    // ============================================================================

    void SensorSession::startReconnect()
    {
        // 检查是否超过最大重连次数（0 = 无限重连）
        if (config_.maxReconnectRetries > 0 &&
            retryCount_ >= config_.maxReconnectRetries)
        {
            SESSION_LOG_ERROR("重连失败: 已达最大次数 %d", config_.maxReconnectRetries);
            active_ = false;
            if (statusCallback_)
                statusCallback_(SessionStatus::ReconnectFailed);
            return;
        }

        retryCount_++;
        SESSION_LOG_INFO("计划重连: %dms 后第 %d 次尝试",
                         config_.reconnectIntervalMs, retryCount_);

        if (statusCallback_)
            statusCallback_(SessionStatus::Reconnecting);

        // 断开当前连接
        client_->disconnect();

        // 延迟后重连
        reconnectTimer_.cancel();
        reconnectTimer_.expires_after(
            std::chrono::milliseconds(config_.reconnectIntervalMs));
        reconnectTimer_.async_wait(
            [this](const boost::system::error_code &ec)
            {
                onReconnectTimer(ec);
            });
    }

    void SensorSession::onReconnectTimer(const boost::system::error_code &ec)
    {
        if (ec || !active_)
            return;

        SESSION_LOG_INFO("执行重连: %s:%u 第 %d 次",
                         config_.host.c_str(), config_.port, retryCount_);

        // 重新发起连接
        // 连接成功后，需要由上层或 TcpClient 的 connectCallback 重启心跳循环
        // 这里简单处理：直接 connect，假设连接成功后会触发后续流程
        client_->connect(config_.host, config_.port);
    }

    // ============================================================================
    // 心跳帧构建
    // ============================================================================

    std::vector<char> SensorSession::buildHeartbeatFrame() const
    {
        // 帧格式（小端序）：
        // [帧长:4B] [deviceId:2B] [心跳标识:2B = 0x0001] [CRC:2B]
        // 总长 = 4 + 2 + 2 + 2 = 10 字节

        const uint32_t frameLen = 10; // 固定 10 字节
        std::vector<char> frame(frameLen);
        uint8_t *buf = reinterpret_cast<uint8_t *>(frame.data());

        // 1. 帧长（4 字节，小端序）
        common::ByteOrder::writeUint32LE(buf, frameLen);

        // 2. 设备 ID（2 字节，小端序）
        common::ByteOrder::writeUint16LE(buf + common::FrameDef::DEVICE_ID_OFFSET,
                                         config_.deviceId);

        // 3. 心跳载荷标识（2 字节）：0x0001 表示心跳
        buf[common::FrameDef::PAYLOAD_OFFSET] = 0x00;
        buf[common::FrameDef::PAYLOAD_OFFSET + 1] = 0x01;

        // 4. CRC16 校验（校验范围：deviceId + payload，共 4 字节）
        uint16_t crc = common::CRC16::calculate(
            buf + common::FrameDef::DEVICE_ID_OFFSET,
            common::FrameDef::DEVICE_ID_SIZE + 2); // deviceId(2) + heartbeat_payload(2)
        common::ByteOrder::writeUint16LE(
            buf + frameLen - common::FrameDef::CRC_SIZE, crc);

        return frame;
    }
}