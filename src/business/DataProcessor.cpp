#include "DataProcessor.h"
#include <algorithm>
#include <cmath>
#include <utility>

namespace {
DataValidationRules normaliseRules(DataValidationRules rules) {
    // 配置文件中的数值可能被误填。无效区间回退到安全默认值，避免
    // 所有数据都被误判，或 NaN/无穷大绕过范围检查。
    const DataValidationRules defaults{};
    const auto valid = [](double minimum, double maximum) {
        return std::isfinite(minimum) && std::isfinite(maximum) && minimum <= maximum;
    };
    if (!valid(rules.minimumTemperature, rules.maximumTemperature)) {
        rules.minimumTemperature = defaults.minimumTemperature;
        rules.maximumTemperature = defaults.maximumTemperature;
    }
    if (!valid(rules.minimumHumidity, rules.maximumHumidity)) {
        rules.minimumHumidity = defaults.minimumHumidity;
        rules.maximumHumidity = defaults.maximumHumidity;
    }
    if (!valid(rules.minimumPressure, rules.maximumPressure)) {
        rules.minimumPressure = defaults.minimumPressure;
        rules.maximumPressure = defaults.maximumPressure;
    }
    if (!valid(rules.minimumVibration, rules.maximumVibration)) {
        rules.minimumVibration = defaults.minimumVibration;
        rules.maximumVibration = defaults.maximumVibration;
    }
    return rules;
}
} // namespace

DataProcessor::DataProcessor(DataValidationRules rules, std::size_t smoothingWindow)
    // std::max 保证窗口至少为 1，避免后续出现除以 0 的问题。
    : rules_(normaliseRules(std::move(rules))), smoothingWindow_(std::max<std::size_t>(1, smoothingWindow)) {}

std::optional<SensorData> DataProcessor::process(const SensorData& input) {
    // lock_guard 构造时加锁，离开函数/作用域时自动解锁。
    std::lock_guard<std::mutex> lock(mutex_);
    if (!isValid(input)) {
        // optional 用空值表达“没有可用结果”，比返回一个特殊数值更安全。
        return std::nullopt;
    }

    SensorData output = input;
    if (smoothingWindow_ == 1) {
        // 窗口为 1 时直接返回原始数据，不需要创建缓存。
        return output;
    }

    // unordered_map 会按 deviceId 找到该设备自己的历史样本。
    RunningAverage& average = averages_[input.deviceId];
    average.samples.push_back(input);
    if (average.samples.size() > smoothingWindow_) {
        // 超过窗口后删除最旧的样本，形成“滑动窗口”。
        average.samples.pop_front();
    }

    // 使用 long double 累加，避免多个较大但合法的 double 相加时中间溢出。
    long double temperatureSum = 0.0L;
    long double humiditySum = 0.0L;
    long double pressureSum = 0.0L;
    long double vibrationSum = 0.0L;
    // 先求和，最后除以样本数量得到平均值。
    for (const SensorData& sample : average.samples) {
        temperatureSum += static_cast<long double>(sample.temperature);
        humiditySum += static_cast<long double>(sample.humidity);
        pressureSum += static_cast<long double>(sample.pressure);
        vibrationSum += static_cast<long double>(sample.vibration);
    }
    const double divisor = static_cast<double>(average.samples.size());
    output.temperature = static_cast<double>(temperatureSum / divisor);
    output.humidity = static_cast<double>(humiditySum / divisor);
    output.pressure = static_cast<double>(pressureSum / divisor);
    output.vibration = static_cast<double>(vibrationSum / divisor);
    return output;
}

void DataProcessor::setValidationRules(DataValidationRules rules) {
    std::lock_guard<std::mutex> lock(mutex_);
    rules_ = normaliseRules(std::move(rules));
    // 规则改变后，旧缓存可能来自不同条件，因此一起清空。
    averages_.clear();
}

DataValidationRules DataProcessor::validationRules() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return rules_;
}

void DataProcessor::setSmoothingWindow(std::size_t windowSize) {
    std::lock_guard<std::mutex> lock(mutex_);
    smoothingWindow_ = std::max<std::size_t>(1, windowSize);
    averages_.clear();
}

void DataProcessor::reset(const std::string& deviceId) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (deviceId.empty()) {
        averages_.clear();
    } else {
        averages_.erase(deviceId);
    }
}

bool DataProcessor::isValid(const SensorData& data) const {
    // 这里使用“全部条件都满足”判断数据合法。
    return !data.deviceId.empty() && isFinite(data.temperature) && isFinite(data.humidity) &&
           isFinite(data.pressure) && isFinite(data.vibration) &&
           data.temperature >= rules_.minimumTemperature && data.temperature <= rules_.maximumTemperature &&
           data.humidity >= rules_.minimumHumidity && data.humidity <= rules_.maximumHumidity &&
           data.pressure >= rules_.minimumPressure && data.pressure <= rules_.maximumPressure &&
           data.vibration >= rules_.minimumVibration && data.vibration <= rules_.maximumVibration;
}

bool DataProcessor::isFinite(double value) {
    return std::isfinite(value);
}
