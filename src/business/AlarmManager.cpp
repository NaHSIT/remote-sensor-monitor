#include "AlarmManager.h"

#include <iterator>
#include <cmath>
#include <utility>

namespace {
bool validLimit(const AlarmLimit& limit) {
    return std::isfinite(limit.minimum) && std::isfinite(limit.maximum) && limit.minimum <= limit.maximum;
}

AlarmThresholds normaliseThresholds(AlarmThresholds thresholds) {
    // 无效阈值会令告警永远触发/永远不触发，因此回退到默认配置。
    const AlarmThresholds defaults{};
    if (!validLimit(thresholds.temperature)) thresholds.temperature = defaults.temperature;
    if (!validLimit(thresholds.humidity)) thresholds.humidity = defaults.humidity;
    if (!validLimit(thresholds.pressure)) thresholds.pressure = defaults.pressure;
    if (!validLimit(thresholds.vibration)) thresholds.vibration = defaults.vibration;
    return thresholds;
}
} // namespace

AlarmManager::AlarmManager(AlarmThresholds thresholds)
    : thresholds_(normaliseThresholds(std::move(thresholds))) {}

std::vector<AlarmEvent> AlarmManager::evaluate(const SensorData& data) {
    AlarmCallback callback;
    std::vector<AlarmEvent> events;
    {
        // 只在计算状态时持锁，避免用户回调执行时间过长阻塞其他设备。
        std::lock_guard<std::mutex> lock(mutex_);
        events = evaluateLocked(data);
        callback = callback_;
    }
    if (callback) {
        // 回调放在锁外执行，回调内部可以安全地调用管理器的其他接口。
        for (const AlarmEvent& event : events) {
            callback(event);
        }
    }
    return events;
}

void AlarmManager::setThresholds(AlarmThresholds thresholds) {
    std::lock_guard<std::mutex> lock(mutex_);
    thresholds_ = normaliseThresholds(std::move(thresholds));
}

AlarmThresholds AlarmManager::thresholds() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return thresholds_;
}

bool AlarmManager::isAlarmActive(const std::string& deviceId, MeasurementType measurement) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = activeStates_.find({deviceId, measurement});
    return it != activeStates_.end() && it->second;
}

void AlarmManager::clearDevice(const std::string& deviceId) {
    std::lock_guard<std::mutex> lock(mutex_);
    // erase 返回下一个迭代器，因此可以在遍历 map 时安全删除元素。
    for (auto it = activeStates_.begin(); it != activeStates_.end();) {
        it = it->first.first == deviceId ? activeStates_.erase(it) : std::next(it);
    }
}

void AlarmManager::setCallback(AlarmCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    callback_ = std::move(callback);
}

std::vector<AlarmEvent> AlarmManager::evaluateLocked(const SensorData& data) {
    std::vector<AlarmEvent> events;
    // AlarmManager 通常接收 DataProcessor 的结果，但这里仍做最后一道防线：
    // 非有限值不能参与阈值比较，否则 NaN 会被误当作“恢复正常”。
    if (data.deviceId.empty() || !std::isfinite(data.temperature) || !std::isfinite(data.humidity) ||
        !std::isfinite(data.pressure) || !std::isfinite(data.vibration)) {
        return events;
    }
    // 四类测量值分别判断；一个样本可能同时产生多个告警事件。
    evaluateValue(events, data, MeasurementType::Temperature, data.temperature, thresholds_.temperature);
    evaluateValue(events, data, MeasurementType::Humidity, data.humidity, thresholds_.humidity);
    evaluateValue(events, data, MeasurementType::Pressure, data.pressure, thresholds_.pressure);
    evaluateValue(events, data, MeasurementType::Vibration, data.vibration, thresholds_.vibration);
    return events;
}

void AlarmManager::evaluateValue(std::vector<AlarmEvent>& events, const SensorData& data,
                                 MeasurementType measurement, double value, const AlarmLimit& limit) {
    // 只要低于下限或高于上限，就认为当前告警处于 active 状态。
    const bool isActive = value < limit.minimum || value > limit.maximum;
    const AlarmKey key{data.deviceId, measurement};
    // map 下标访问会为新设备创建 false 初始状态。
    const bool wasActive = activeStates_[key];
    if (wasActive == isActive) {
        return;
    }
    activeStates_[key] = isActive;
    // 记录状态切换，而不是每次采样都重复报警。
    events.push_back({data.deviceId, measurement, value, limit, isActive, data.timestamp});
}
