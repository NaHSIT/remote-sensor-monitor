// 模块：公共数据模型；作用：定义网络、业务、存储和 UI 之间共享的传感器数据；编写内容：补充设备 ID、测量值、时间戳和状态字段。

#ifndef SENSORDATA_H
#define SENSORDATA_H

#include <cstdint>
#include <cstddef>
#include <vector>
#include <string>
#include <chrono>

namespace common
{
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

    // 解析后的协议帧
    struct Frame
    {
        uint32_t frameLength = 0;
        uint16_t deviceId = 0;
        std::vector<uint8_t> payload;
        uint16_t crcReceived = 0;
        uint16_t crcCalculated = 0;

        bool isValid() const { return crcReceived == crcCalculated; }
    };

    //CRC16 校验
    
class CRC16 {
public:
    static uint16_t calculate(const uint8_t* data, size_t length) {
        uint16_t crc = 0xFFFF;
        for (size_t i = 0; i < length; ++i) {
            crc ^= static_cast<uint16_t>(data[i]);
            for (int j = 0; j < 8; ++j) {
                if (crc & 0x0001) {
                    crc = (crc >> 1) ^ 0xA001;
                } else {
                    crc >>= 1;
                }
            }
        }
        return crc;
    }

    static uint16_t calculate(const std::vector<uint8_t>& data) {
        return calculate(data.data(), data.size());
    }
};

//字节序工具
namespace ByteOrder {

    inline uint16_t readUint16LE(const uint8_t* data) {
        return static_cast<uint16_t>(data[0])
             | (static_cast<uint16_t>(data[1]) << 8);
    }

    inline uint32_t readUint32LE(const uint8_t* data) {
        return static_cast<uint32_t>(data[0])
             | (static_cast<uint32_t>(data[1]) << 8)
             | (static_cast<uint32_t>(data[2]) << 16)
             | (static_cast<uint32_t>(data[3]) << 24);
    }

    inline void writeUint16LE(uint8_t* buf, uint16_t value) {
        buf[0] = static_cast<uint8_t>(value & 0xFF);
        buf[1] = static_cast<uint8_t>((value >> 8) & 0xFF);
    }

    inline void writeUint32LE(uint8_t* buf, uint32_t value) {
        buf[0] = static_cast<uint8_t>(value & 0xFF);
        buf[1] = static_cast<uint8_t>((value >> 8) & 0xFF);
        buf[2] = static_cast<uint8_t>((value >> 16) & 0xFF);
        buf[3] = static_cast<uint8_t>((value >> 24) & 0xFF);
    }

}

}

#endif