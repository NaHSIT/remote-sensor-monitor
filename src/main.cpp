#include "business/AlarmManager.h"
#include "business/DataProcessor.h"
#include <iostream>

// 当前入口先提供一个无 UI 的核心流水线示例，方便验证成员 B 的模块。
// 成员 C 接入 Qt 后，可复用 sensor_core，不需要重新实现业务规则。
int main() {
    DataProcessor processor;
    AlarmManager alarms;
    alarms.setCallback([](const AlarmEvent& event) {
        std::cout << (event.active ? "alarm: " : "recovered: ") << event.deviceId << '\n';
    });
    SensorData sample;
    sample.deviceId = "demo-sensor";
    sample.temperature = 25.0;
    sample.humidity = 50.0;
    sample.pressure = 100.0;
    sample.vibration = 0.1;
    if (const auto processed = processor.process(sample)) alarms.evaluate(*processed);
    return 0;
}
