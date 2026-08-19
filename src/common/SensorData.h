#pragma once
#include <chrono>
#include <string>

// 统一的传感器数据结构。
//
// 协议层把网络报文解析成这个结构，业务层负责检查/处理它，
// 存储层负责把它写进数据库，UI 层则可以直接读取它来显示。
struct SensorData {
    std::string deviceId; // 设备编号，不能为空。
    double temperature = 0.0; // 温度，单位：摄氏度。
    double humidity = 0.0;    // 湿度，单位：百分比（0~100）。
    double pressure = 0.0;    // 压力，单位：kPa。
    double vibration = 0.0;  // 振动值，单位：mm/s。
    // system_clock 适合表示现实世界的时间，例如采样发生的时刻。
    std::chrono::system_clock::time_point timestamp = std::chrono::system_clock::now();
};
