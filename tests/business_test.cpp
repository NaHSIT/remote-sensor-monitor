#include "business/AlarmManager.h"
#include "business/DataProcessor.h"
#include <cassert>
#include <limits>

namespace {
SensorData sample() {
    SensorData data;
    data.deviceId = "sensor-001";
    data.temperature = 25.0;
    data.humidity = 50.0;
    data.pressure = 100.0;
    data.vibration = 0.2;
    return data;
}
}

int main() {
    DataProcessor processor({}, 2);
    const SensorData first = sample();
    assert(processor.process(first).has_value());
    auto second = first;
    second.temperature = 27.0;
    const auto averaged = processor.process(second);
    assert(averaged.has_value() && averaged->temperature == 26.0); // 最近两个样本的滑动平均。
    auto invalid = first;
    invalid.deviceId.clear();
    assert(!processor.process(invalid).has_value());
    invalid = first;
    invalid.temperature = std::numeric_limits<double>::quiet_NaN();
    assert(!processor.process(invalid).has_value());

    AlarmThresholds limits;
    limits.temperature = {0.0, 30.0};
    AlarmManager alarms(limits);
    auto hot = first;
    hot.temperature = 31.0;
    assert(alarms.evaluate(hot).size() == 1); // 正常 -> 越界
    assert(alarms.evaluate(hot).empty());    // 越界期间不重复报警
    hot.temperature = 29.0;
    assert(alarms.evaluate(hot).size() == 1); // 越界 -> 恢复
    assert(!alarms.isAlarmActive(hot.deviceId, MeasurementType::Temperature));
    return 0;
}
