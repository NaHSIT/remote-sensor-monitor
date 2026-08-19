// 模块：网络通信；作用：实现 TCP 客户端的连接、收发和关闭；编写内容：补充 Boost.Asio 异步网络逻辑。

#include "TcpClient.h"
#include <iostream>
#include <sstream>

namespace net
{
    // 构造函数
    TcpClient::TcpClient(boost::asio::io_context &io)
        : io_(io),
          socket_(io),
          // 创建strand 保证线程安全
          strand_(io),
          // 初始化重连定时器
          reconnectTimer_(io),
          // 初始化超连定时器
          connectTimer_(io)
    {
        NET_LOG_INFO("TcpClient 已创建");
    }

    TcpClient::~TcpClient()
    {
        disconnect();
        NET_LOG_INFO("TcpClient 已销毁");
    }
}