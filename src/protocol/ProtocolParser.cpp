// 模块：协议解析；作用：把网络字节流解析为传感器数据；编写内容：补充帧头、长度、字段和粘包分包处理。
// 模块：协议解析
// 作用：从 TCP 字节流中解析完整协议帧，处理粘包/分包

#include "ProtocolParser.h"

namespace protocol {

void ProtocolParser::feed(const uint8_t* data, size_t len)
{
    if (data == nullptr || len == 0) return;

    // 1. 新数据追加到缓冲区尾部
    buffer_.insert(buffer_.end(), data, data + len);

    // 2. 循环拆帧，直到缓冲区数据不够为止
    while (true) {
        // ---- 检查1：缓冲区够不够读帧长字段（4 字节）？ ----
        if (buffer_.size() < 4) return;  // 不够，等下一批数据

        // ---- 检查2：帧长合不合法？ ----
        uint32_t frameLen = common::ByteOrder::readUint32LE(buffer_.data());

        if (frameLen < MIN_FRAME_SIZE || frameLen > MAX_FRAME_SIZE) {
            // 帧长不合法，可能是随机字节凑出的假帧长
            // 丢 1 字节，重新对齐，继续找真正的帧起始位置
            if (errorCallback_) {
                errorCallback_("非法帧长: " + std::to_string(frameLen));
            }
            buffer_.erase(buffer_.begin());
            continue;  // 重新循环，在新的位置尝试
        }

        // ---- 检查3：缓冲区够不够一整帧？ ----
        if (buffer_.size() < frameLen) return;  // 分包场景，等更多数据

        // ---- 到这里：缓冲区有一整帧 ----
        common::Frame frame = parseFrame(buffer_.data(), frameLen);

        // 从缓冲区移除已处理的帧数据
        buffer_.erase(buffer_.begin(), buffer_.begin() + frameLen);

        // CRC 校验
        if (frame.isValid()) {
            if (frameCallback_) {
                frameCallback_(frame);
            }
        } else {
            // CRC 失败，丢弃该帧（TCP 可靠传输，数据本身就是坏的）
            if (errorCallback_) {
                errorCallback_("CRC 校验失败");
            }
        }
        // 继续循环，处理可能的粘包（缓冲区里可能还有下一帧）
    }
}

void ProtocolParser::feed(const std::vector<uint8_t>& data)
{
    feed(data.data(), data.size());
}

void ProtocolParser::reset()
{
    buffer_.clear();
}

// ============================================================================
// 帧解析
// ============================================================================

common::Frame ProtocolParser::parseFrame(const uint8_t* frameData, uint32_t frameLen)
{
    common::Frame frame;

    // 1. 帧长
    frame.frameLength = frameLen;

    // 2. 设备 ID（偏移 4，长度 2，小端序）
    frame.deviceId = common::ByteOrder::readUint16LE(
        frameData + common::FrameDef::DEVICE_ID_OFFSET);

    // 3. 载荷数据（偏移 6，长度 = frameLen - 8）
    size_t payloadLen = frameLen - common::FrameDef::MIN_FRAME_SIZE;
    frame.payload.assign(
        frameData + common::FrameDef::PAYLOAD_OFFSET,
        frameData + common::FrameDef::PAYLOAD_OFFSET + payloadLen);

    // 4. 收到的 CRC（帧末尾 2 字节）
    frame.crcReceived = common::ByteOrder::readUint16LE(
        frameData + frameLen - common::FrameDef::CRC_SIZE);

    // 5. 计算 CRC（校验范围：deviceId + payload）
    size_t crcDataLen = frameLen - common::FrameDef::FRAME_LENGTH_SIZE
                              - common::FrameDef::CRC_SIZE;
    frame.crcCalculated = common::CRC16::calculate(
        frameData + common::FrameDef::DEVICE_ID_OFFSET, crcDataLen);

    return frame;
}

} // namespace protocol
