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

namespace net
{

    /// 会话状态枚举（通知上层连接状态变化）
    enum class SessionStatus
    {
        Connected,        // 已连接
        Disconnected,     // 已断开
        HeartbeatTimeout, // 心跳超时
        Reconnecting,     // 正在重连
        ReconnectFailed   // 重连失败（达到最大次数）
    };

    /// 设备会话：绑定一个 TcpClient + 一个 ProtocolParser
    /// 对外提供：连接、断开、数据回调、心跳、重连
    class SensorSession
    {
    public:
        /// 会话配置
        struct Config
        {
            std::string host;               ///< 服务器 IP 地址
            uint16_t port = 0;              ///< 服务器端口
            uint16_t deviceId = 0;          ///< 本设备 ID
            int heartbeatIntervalMs = 5000; ///< 心跳发送间隔（毫秒）
            int heartbeatTimeoutMs = 10000; ///< 心跳响应超时（毫秒）
            int reconnectIntervalMs = 3000; ///< 断线重连间隔（毫秒）
            int maxReconnectRetries = 5;    ///< 最大重连次数（0 = 无限）
        };

        /// 回调类型
        using FrameCallback = std::function<void(const common::Frame &)>;
        using ErrorCallback = std::function<void(const std::string &reason)>;
        using StatusCallback = std::function<void(SessionStatus status)>;

        /// 构造：接收共享的 io_context 和会话配置
        explicit SensorSession(boost::asio::io_context &io, const Config &config);
        ~SensorSession();

        // 禁止拷贝
        SensorSession(const SensorSession &) = delete;
        SensorSession &operator=(const SensorSession &) = delete;

        // 连接服务器
        void connect();
        // 断开连接
        void disconnect();

        // 设置帧回调（收到 CRC 校验通过的合法帧时触发，心跳回复帧除外）
        void setFrameCallback(FrameCallback cb);
        // 设置错误回调（解析失败等异常通知）
        void setErrorCallback(ErrorCallback cb);
        // 设置状态回调（连接/断开/重连等状态变化通知）
        void setStatusCallback(StatusCallback cb);

        // 当前是否处于活跃状态（已调用 connect 且未 disconnect）
        bool isActive() const { return active_; }
        // 获取设备 ID
        uint16_t deviceId() const { return config_.deviceId; }

    private:
        // ==================== 数据链路回调 ====================
        // TcpClient 收到数据 → 转换类型 → 喂给 ProtocolParser
        void onDataReceived(const std::string &addr, uint16_t port,
                            const std::vector<char> &data);
        // ProtocolParser 解析出合法帧 → 区分心跳回复和数据帧
        void onFrameParsed(const common::Frame &frame);
        // TcpClient 连接断开回调
        void onDisconnected();
        // ProtocolParser 解析错误回调
        void onParseError(const std::string &reason);

        // ==================== 心跳 ====================
        // 启动心跳循环（递归定时器）
        void startHeartbeatLoop();
        // 发送一帧心跳并启动超时检测
        void sendHeartbeat();
        // 心跳超时处理：判定断线 → 触发重连
        void onHeartbeatTimeout(const boost::system::error_code &ec);

        // ==================== 重连 ====================
        // 启动重连流程
        void startReconnect();
        // 重连定时器到期 → 执行重连
        void onReconnectTimer(const boost::system::error_code &ec);

        // ==================== 工具 ====================
        // 构建心跳帧（10 字节）
        std::vector<char> buildHeartbeatFrame() const;

        // ==================== 成员变量 ====================
        boost::asio::io_context &io_;                      ///< 共享 io_context
        Config config_;                                    ///< 会话配置
        std::unique_ptr<TcpClient> client_;                ///< TCP 客户端
        std::unique_ptr<protocol::ProtocolParser> parser_; ///< 协议解析器

        boost::asio::steady_timer heartbeatTimer_; ///< 心跳间隔定时器
        boost::asio::steady_timer reconnectTimer_; ///< 重连延迟定时器
        bool active_ = false;                      ///< 会话是否活跃
        int retryCount_ = 0;                       ///< 当前重连次数

        // 用户回调
        FrameCallback frameCallback_;
        ErrorCallback errorCallback_;
        StatusCallback statusCallback_;
    };

} // namespace net

#endif // SENSOR_SESSION_H
