// 模块：公共数据模型；作用：定义网络、业务、存储和 UI 之间共享的传感器数据；编写内容：补充设备 ID、测量值、时间戳和状态字段。

#ifndef SENSORDATA_H
#define SENSORDATA_H

#include <cstdint>
#include <cstddef>
#include <vector>
#include <string>
#include <chrono>

#include "../protocol/CRC.h"

namespace common
{
    // 格式约定 将协议中每个字段占几个字节、在哪个位置全部定义成带名字的常量
    struct FrameDef
    {
        static constexpr size_t FRAME_LENGTH_SIZE = 4;
        static constexpr size_t DEVICE_ID_SIZE = 2;
        static constexpr size_t CRC_SIZE = 2;

        static constexpr size_t FRAME_LENGTH_OFFSET = 0;
        static constexpr size_t DEVICE_ID_OFFSET = FRAME_LENGTH_SIZE;                           // = 4
        static constexpr size_t PAYLOAD_OFFSET = DEVICE_ID_OFFSET + DEVICE_ID_SIZE;             // = 6
        static constexpr size_t MIN_FRAME_SIZE = FRAME_LENGTH_SIZE + DEVICE_ID_SIZE + CRC_SIZE; // = 8
    };

    // 解析结果
    // 解析器从字节流里拆出一帧后，把各个字段填进这个结构体，传给上层
    struct Frame
    {
        uint32_t frameLength = 0;
        uint16_t deviceId = 0;
        std::vector<uint8_t> payload;
        uint16_t crcReceived = 0;//从帧里读到的CRC
        uint16_t crcCalculated = 0;//计算得到的CRC

        bool isValid() const { return crcReceived == crcCalculated; }
    };

    // 字节序工具
    // 把网络字节流里的1/2/4个字节拼成一个整数（读），或者把一个整数拆成字节塞进缓冲区（写）
    namespace ByteOrder
    {

        inline uint16_t readUint16LE(const uint8_t *data)
        {
            return static_cast<uint16_t>(data[0]) | (static_cast<uint16_t>(data[1]) << 8);
        }

        inline uint32_t readUint32LE(const uint8_t *data)
        {
            return static_cast<uint32_t>(data[0]) | (static_cast<uint32_t>(data[1]) << 8) | (static_cast<uint32_t>(data[2]) << 16) | (static_cast<uint32_t>(data[3]) << 24);
        }

        inline void writeUint16LE(uint8_t *buf, uint16_t value)
        {
            buf[0] = static_cast<uint8_t>(value & 0xFF);
            buf[1] = static_cast<uint8_t>((value >> 8) & 0xFF);
        }

        inline void writeUint32LE(uint8_t *buf, uint32_t value)
        {
            buf[0] = static_cast<uint8_t>(value & 0xFF);
            buf[1] = static_cast<uint8_t>((value >> 8) & 0xFF);
            buf[2] = static_cast<uint8_t>((value >> 16) & 0xFF);
            buf[3] = static_cast<uint8_t>((value >> 24) & 0xFF);
        }

    }

}

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

#endif



