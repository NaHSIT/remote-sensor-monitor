// 模块：网络通信；作用：管理单个传感器设备会话；编写内容：补充设备连接、心跳、重连和数据接收流程。

#include "SensorSession.h"
#include <cstdio>

//日志宏
#ifndef SESSION_LOG_INFO
#define SESSION_LOG_INFO(fmt, ...)  printf("[SensorSession][INFO]  " fmt "\n", ##__VA_ARGS__)
#endif
#ifndef SESSION_LOG_WARN
#define SESSION_LOG_WARN(fmt, ...)  printf("[SensorSession][WARN]  " fmt "\n", ##__VA_ARGS__)
#endif
#ifndef SESSION_LOG_ERROR
#define SESSION_LOG_ERROR(fmt, ...) printf("[SensorSession][ERROR] " fmt "\n", ##__VA_ARGS__)
#endif

namespace net{
    //构造与析构
    SensorSession::SensorSession(boost::asio::io_context& io, const Config& config)
    : io_(io)
    , config_(config)
    , heartbeatTimer_(io)
    , reconnectTimer_(io)
{
    SESSION_LOG_INFO("创建会话: %s:%u deviceId=%u",
                     config.host.c_str(), config.port, config.deviceId);

    active_     = false;
    retryCount_ = 0;

    // 创建 TcpClient（共享 io_context）
    client_ = std::make_unique<TcpClient>(io);

    // 创建 ProtocolParser
    parser_ = std::make_unique<protocol::ProtocolParser>();

    // ---- 绑定 TcpClient 回调 ----
    // 连接成功 → 重置重连计数 + 启动心跳
    client_->setConnectCallback(
        [this]() {
            SESSION_LOG_INFO("连接成功 deviceId=%u", config_.deviceId);
            retryCount_ = 0;
            startHeartbeatLoop();
            if (statusCallback_) statusCallback_(SessionStatus::Connected);
        });

    // 收到数据 → 喂给 ProtocolParser
    client_->setDataCallback(
        [this](const std::string& addr, uint16_t port,
               const std::vector<char>& data) {
            onDataReceived(addr, port, data);
        });

    // 连接断开 → 记录日志
    client_->setDisconnectCallback(
        [this]() { onDisconnected(); });

    // 网络错误 → 转发给上层
    client_->setErrorCallback(
        [this](const boost::system::error_code& ec, const std::string& msg) {
            SESSION_LOG_ERROR("网络错误: %s", msg.c_str());
            if (errorCallback_) errorCallback_(msg);
        });

    // ---- 绑定 ProtocolParser 回调 ----
    // 合法帧 → 区分心跳回复和数据帧
    parser_->setFrameCallback(
        [this](const common::Frame& frame) { onFrameParsed(frame); });

    // 解析错误 → 转发给上层
    parser_->setErrorCallback(
        [this](const std::string& reason) { onParseError(reason); });

    // ---- 关闭 TcpClient 的自动重连（由 SensorSession 统一管理重连）----
    client_->setAutoReconnect(false);
}

SensorSession::~SensorSession() {
    active_ = false;
    heartbeatTimer_.cancel();
    reconnectTimer_.cancel();
    if (client_) client_->disconnect();
}
}