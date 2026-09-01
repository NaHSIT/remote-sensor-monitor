// 模块：协议解析；作用：声明协议解析器接口；编写内容：补充数据帧解析、校验和解析结果接口。
// 将TcpClient收到的原始字节流拆成一帧一帧完整的经过校验的Frame结构体,处理粘包分包
//frameLength:4bytes,deviceID:2bytes,payload:可变长度,CRC:2bytes

#ifndef PROTOCOL_PARSER_H
#define PROTOCOL_PARSER_H

#include <cstdint>
#include <cstddef>
#include <vector>
#include <functional>

#include "../common/SensorData.h"

namespace protocol
{
    class ProtocolParser
    {
    public:
        // 帧解析完成回调（CRC校验通过的合法帧）
        using FrameCallback = std::function<void(const common::Frame &frame)>;

        // 解析错误回调（非法帧被丢弃时通知）
        using ErrorCallback = std::function<void(const std::string &reason)>;

        // 构造与析构
        ProtocolParser() = default;
        ~ProtocolParser() = default;

        //禁止拷贝 const保证回调里不能改parser给的帧
        ProtocolParser(const ProtocolParser&) = delete;
    ProtocolParser& operator=(const ProtocolParser&) = delete;

    //回调设置
    void setFrameCallback(FrameCallback cb) { frameCallback_ = std::move(cb); }
    void setErrorCallback(ErrorCallback cb) { errorCallback_ = std::move(cb); }

    //喂入数据 TcpClient每次收到数据后调用此方法
    void feed(const uint8_t* data, size_t len);

    //喂入数据 vector重载
    void feed(const std::vector<uint8_t>& data);

    //清空缓冲区
    void reset();

    //获取当前缓冲区大小
    size_t bufferSize() const { return buffer_.size(); }

    private:
    static constexpr size_t MIN_FRAME_SIZE = 8;     // 最小帧 = 4 + 2 + 2
    static constexpr size_t MAX_FRAME_SIZE = 4096;  // 单帧最大长度

    // 从字节数据中解析出 Frame 结构体
    common::Frame parseFrame(const uint8_t* frameData, uint32_t frameLen);

    //成员变量
    std::vector<uint8_t> buffer_;       // 累积缓冲区
    FrameCallback frameCallback_;       // 帧解析完成回调
    ErrorCallback errorCallback_;       // 解析错误回调
    };
}

#endif