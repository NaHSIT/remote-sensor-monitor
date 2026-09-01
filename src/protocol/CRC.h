// 模块：协议解析；作用：声明 CRC 计算和校验接口；编写内容：补充 CRC 类型、参数和公共函数声明。
// 算法：预计算256项查找表，运行时O(n)查表
// 多项式：0x8005(位反转形式0xA001),初始值 0xFFFF

#ifndef CRC_H
#define CRC_H

#include <cstdint>
#include <cstddef>
#include <array>
#include <vector>

namespace common
{
    // 编译期生成 256 项 CRC 查找表
    // 每一项 = 把下标值（0~255）经过 8 次逐位 CRC 运算的结果
    static constexpr std::array<uint16_t, 256> generateCrcTable()
    {
        std::array<uint16_t, 256> table{};
        for (uint32_t i = 0; i < 256; ++i)
        {
            uint16_t crc = static_cast<uint16_t>(i);
            for (int j = 0; j < 8; ++j)
            {
                if (crc & 0x0001)
                {
                    crc = (crc >> 1) ^ 0xA001;
                }
                else
                {
                    crc >>= 1;
                }
            }
            table[i] = crc;
        }
        return table;
    }
    class CRC16
    {
    public:
        // 计算buffer的CRC16校验值
        static uint16_t calculate(const uint8_t *data, size_t length);

        // vector重载
        static uint16_t calculate(const std::vector<uint8_t> &data);

    private:
        // 编译器常量查表，零运行时开销
        static constexpr std::array<uint16_t, 256> table_ = generateCrcTable();
    };
}

#endif