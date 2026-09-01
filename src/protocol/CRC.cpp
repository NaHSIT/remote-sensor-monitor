// 模块：协议解析；作用：实现通信数据的 CRC 校验；编写内容：补充 CRC 算法和校验计算逻辑。
#include "CRC.h"

namespace common {

uint16_t CRC16::calculate(const uint8_t* data, size_t length) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < length; ++i) {
        // (crc 低 8 位) XOR 当前字节 → 查表下标
        // crc 右移 8 位 XOR 表中值 → 新的 crc
        crc = (crc >> 8) ^ table_[(crc ^ data[i]) & 0xFF];
    }
    return crc;
}

uint16_t CRC16::calculate(const std::vector<uint8_t>& data) {
    return calculate(data.data(), data.size());
}

}