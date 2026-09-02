// 模块：网络通信；作用：多设备会话管理器实现
#include "DeviceManager.h"

namespace net {
    DeviceManager::DeviceManager(boost::asio::io_context& io)
    : io_(io)
    {
    }

    bool DeviceManager::addDevice(const SensorSession::Config& config) {
    std::lock_guard<std::mutex> lock(mutex_);

    // deviceId 已存在，不重复添加
    if (sessions_.count(config.deviceId)) {
        return false;
    }

    // 创建 Session 并连接
    auto session = std::make_unique<SensorSession>(io_, config);
    session->connect();

    sessions_[config.deviceId] = std::move(session);
    return true;
}

bool DeviceManager::removeDevice(uint16_t deviceId) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = sessions_.find(deviceId);
    if (it == sessions_.end()) {
        return false;
    }

    // 先断开连接，再销毁
    it->second->disconnect();
    sessions_.erase(it);
    return true;
}

SensorSession* DeviceManager::getSession(uint16_t deviceId) const {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = sessions_.find(deviceId);
    if (it == sessions_.end()) {
        return nullptr;
    }
    return it->second.get();
}

std::vector<uint16_t> DeviceManager::getDeviceIds() const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<uint16_t> ids;
    ids.reserve(sessions_.size());
    for (const auto& pair : sessions_) {
        ids.push_back(pair.first);
    }
    return ids;
}

size_t DeviceManager::deviceCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return sessions_.size();
}

void DeviceManager::stopAll() {
    std::lock_guard<std::mutex> lock(mutex_);

    for (auto& pair : sessions_) {
        pair.second->disconnect();
    }
    sessions_.clear();
}

}