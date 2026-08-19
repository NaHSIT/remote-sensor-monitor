#pragma once
#include "../common/SensorData.h"
#include <cstddef>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

struct DataValidationRules {
    // 每个测量值允许的最小值和最大值。
    // 超出范围通常意味着传感器故障、报文损坏或单位配置错误。
    double minimumTemperature = -80.0;
    double maximumTemperature = 85.0;
    double minimumHumidity = 0.0;
    double maximumHumidity = 100.0;
    double minimumPressure = 30.0;
    double maximumPressure = 120.0;
    double minimumVibration = 0.0;
    double maximumVibration = 1'000.0;
};

// 数据处理器：先验证数据，再按设备做简单的滑动平均。
class DataProcessor {
public:
    // smoothingWindow 表示平均时最多参考多少个样本。
    // 传入 0 时会自动修正为 1，表示不做平滑。
    explicit DataProcessor(DataValidationRules rules = {}, std::size_t smoothingWindow = 1);

    // 处理一个样本。
    // 返回空 optional 表示样本不合法；否则返回处理后的数据。
    std::optional<SensorData> process(const SensorData& input);

    // 修改/读取校验规则。修改规则会清空已有的平均值缓存。
    void setValidationRules(DataValidationRules rules);
    DataValidationRules validationRules() const;

    // 修改平滑窗口大小。修改后会清空历史缓存，避免新旧规则混用。
    void setSmoothingWindow(std::size_t windowSize);

    // 清空缓存：deviceId 为空时清空全部设备，否则只清空指定设备。
    void reset(const std::string& deviceId = {});

private:
    // 每个设备独立保存一段最近样本，避免不同设备的数据互相影响。
    struct RunningAverage {
        std::deque<SensorData> samples;
    };

    // 检查设备 ID、NaN/无穷大以及数值范围。
    bool isValid(const SensorData& data) const;
    // std::isfinite 用来排除 NaN 和正负无穷大。
    static bool isFinite(double value);

    // 处理器可能被多个采集线程调用，所以所有共享状态都由 mutex_ 保护。
    mutable std::mutex mutex_;
    DataValidationRules rules_;
    std::size_t smoothingWindow_;
    std::unordered_map<std::string, RunningAverage> averages_;
};
