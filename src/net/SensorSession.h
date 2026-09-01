// 模块：网络通信；作用：设备会话管理器
// 封装 TcpClient + ProtocolParser，实现数据解析、心跳保活和断线重连
// 每个 SensorSession 对应一个传感器设备的完整通信生命周期
#ifndef SENSOR_SESSION_H
#define SENSOR_SESSION_H

#include <memory>
#include <string>
#include <functional>
#include <cstdint>
#include <boost/asio.hpp>
#include <boost/asio/steady_timer.hpp>
#include "../net/TcpClient.h"
#include "../protocol/ProtocolParser.h"

namespace net {

/// 会话状态枚举（通知上层连接状态变化）
enum class SessionStatus {
    Connected,        // 已连接
    Disconnected,     // 已断开
    HeartbeatTimeout, // 心跳超时
    Reconnecting,     // 正在重连
    ReconnectFailed   // 重连失败（达到最大次数）
};

/// 设备会话：绑定一个 TcpClient + 一个 ProtocolParser
/// 对外提供：连接、断开、数据回调、心跳、重连
class SensorSession {
public:
    /// 会话配置
    struct Config {
        std::string host;                     ///< 服务器 IP 地址
        uint16_t    port            = 0;      ///< 服务器端口
        uint16_t    deviceId        = 0;      ///< 本设备 ID
        int  heartbeatIntervalMs  = 5000;     ///< 心跳发送间隔（毫秒）
        int  heartbeatTimeoutMs   = 10000;    ///< 心跳响应超时（毫秒）
        int  reconnectIntervalMs  = 3000;     ///< 断线重连间隔（毫秒）
        int  maxReconnectRetries  = 5;        ///< 最大重连次数（0 = 无限）
    };

    /// 回调类型
    using FrameCallback   = std::function<void(const common::Frame&)>;
    using ErrorCallback   = std::function<void(const std::string& reason)>;
    using StatusCallback  = std::function<void(SessionStatus status)>;

    /// 构造：接收共享的 io_context 和会话配置
    explicit SensorSession(boost::asio::io_context& io, const Config& config);
    ~SensorSession();

    // 禁止拷贝
    SensorSession(const SensorSession&)            = delete;
    SensorSession& operator=(const SensorSession&) = delete;

    // 用户回调
    FrameCallback   frameCallback_;
    ErrorCallback   errorCallback_;
    StatusCallback  statusCallback_;
};

} // namespace net

#endif // SENSOR_SESSION_H
