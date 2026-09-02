// 模块：网络通信；作用：多设备会话管理器
// 管理多个SensorSesion 实例，支持按 deviceID 添加/移除/查询
// 不做独立复杂类，只做 map 包装 + 线程安全

#ifndef DEVICE_MANAGER_H
#define DEVICE_MANAGER_H

#include <memory>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <cstdint>
#include <boost/asio.hpp>
#include "SensorSession.h"

namespace net {

    class DeviceManager {
        public:
        explicit DeviceManager(boost::asio::io_context& io);
        ~DeviceManager()=default;

        // 禁止拷贝
        DeviceManager(const DeviceManager&)            = delete;
    DeviceManager& operator=(const DeviceManager&) = delete;

    // 添加设备 （创建 SensorSession 并连接）
    bool addDevice(const SensorSession::Config& config);

    // 移除设备 （断开连接并销毁）
    bool removeDevice(uint16_t deviceID);

    // 获取某个设备的Session
    SensorSession* getSension(uint16_t deviceID) const;

    // 获取所有在线设备ID列表
    std::vector<uint16_t> getDeviceIDs() const;

    // 获取当前设备数量
    size_t deviceCount() const;

    // 断开所有设备
    void stopALL();

    private:
    boost::asio::io_context& io_;
    mutable std::mutex mutex_;
    std::unordered_map<uint16_t,std::unique_ptr<SensorSession>> sessions_;
    };
}

#endif