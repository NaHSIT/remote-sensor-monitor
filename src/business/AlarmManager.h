#pragma once

#include "../common/SensorData.h"

#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>

// 告警对应的测量类型。使用 enum class 可避免普通整数枚举污染全局命名空间。
enum class MeasurementType { Temperature, Humidity, Pressure, Vibration };

struct AlarmLimit {
    double minimum = 0.0; // 允许的下限。
    double maximum = 0.0; // 允许的上限。
};

struct AlarmThresholds {
    // 默认阈值；实际项目可以从配置文件读取后再通过 setThresholds 修改。
    AlarmLimit temperature{-20.0, 60.0};
    AlarmLimit humidity{10.0, 90.0};
    AlarmLimit pressure{80.0, 120.0};
    AlarmLimit vibration{0.0, 10.0};
};

struct AlarmEvent {
    std::string deviceId; // 发生变化的设备。
    MeasurementType measurement = MeasurementType::Temperature;
    double value = 0.0; // 触发告警时的实际值。
    AlarmLimit limit;
    bool active = false; // true 表示产生告警，false 表示告警恢复。
    std::chrono::system_clock::time_point timestamp;
};

class AlarmManager {
public:
    using AlarmCallback = std::function<void(const AlarmEvent&)>;

    // 创建告警管理器，并设置四种测量值的上下限。
    explicit AlarmManager(AlarmThresholds thresholds = {});

    // 检查一个样本。只有状态发生变化时才返回事件：
    // 正常 -> 越界是“产生告警”，越界 -> 正常是“恢复告警”。
    std::vector<AlarmEvent> evaluate(const SensorData& data);
    void setThresholds(AlarmThresholds thresholds);
    AlarmThresholds thresholds() const;
    bool isAlarmActive(const std::string& deviceId, MeasurementType measurement) const;
    // 设备断开或重新初始化时，清空该设备的所有告警状态。
    void clearDevice(const std::string& deviceId);
    // 注册通知函数。每次产生/恢复事件时都会调用它。
    void setCallback(AlarmCallback callback);

private:
    // 一个设备 + 一种测量值，唯一确定一个告警状态。
    using AlarmKey = std::pair<std::string, MeasurementType>;

    std::vector<AlarmEvent> evaluateLocked(const SensorData& data);
    void evaluateValue(std::vector<AlarmEvent>& events, const SensorData& data,
                       MeasurementType measurement, double value, const AlarmLimit& limit);

    mutable std::mutex mutex_; // 保护阈值、状态表和回调函数。
    AlarmThresholds thresholds_;
    std::map<AlarmKey, bool> activeStates_;
    AlarmCallback callback_;
};
