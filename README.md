# Remote Sensor Monitor

基于 C++、Qt 和 Boost.Asio 开发的远程传感器数据采集、处理、存储与实时可视化系统。

## Tech Stack

- C++17/20
- Qt6
- Boost.Asio
- CMake
- SQLite
- QCustomPlot / Qt Charts

## 成员 B 核心模块构建

成员 B 的业务和存储代码被组织为不依赖 Qt 的 `sensor_core` 静态/共享库，
便于先独立联调，再由 UI 层链接。机器安装 CMake、C++17 编译器和 SQLite3
开发包后执行：

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

核心链路为 `DataProcessor::process()` → `AlarmManager::evaluate()` →
`DatabaseManager::save()` → `CsvExporter::exportToFile()`。处理器会过滤空设备号、
NaN/无穷大和越界值；告警器只在状态切换时通知；数据库使用参数化 SQL 和批量事务；
CSV 导出遵循 RFC 4180 的引号转义规则，并写入 UTF-8 BOM 方便 Excel 打开。

## Project Structure

### Root Files

- `CMakeLists.txt`：项目构建配置，定义编译标准、依赖库、源文件、测试和资源
- `README.md`：项目说明文档，介绍项目目标、技术栈和目录结构
- `LICENSE`：项目许可证文件，说明代码的使用和授权条件
- `.gitignore`：Git 忽略规则，排除构建产物、临时文件和本地配置

### `docs/`

- `design.md`：项目总体技术设计，说明系统架构、模块职责、数据流和设计约束
- `protocol.md`：通信协议说明，记录数据帧格式、字段定义、字节序和校验规则
- `screenshots/`：项目运行截图目录，用于保存界面和运行效果图片

### `config/`

- `config.example.ini`：运行配置模板，预留服务器地址、端口、设备和告警等参数

### `src/`

- `main.cpp`：程序入口，负责初始化 Qt 应用并组装网络、业务、存储和界面模块

#### `src/net/`

- `TcpClient.h`：声明 TCP 客户端的连接、发送、接收和错误处理接口
- `TcpClient.cpp`：实现 TCP 客户端的网络连接、数据收发和连接关闭逻辑
- `SensorSession.h`：声明单个传感器设备会话的状态和管理接口
- `SensorSession.cpp`：实现设备会话、心跳检测、断线重连和数据接收流程

#### `src/protocol/`

- `ProtocolParser.h`：声明通信协议解析器和解析结果接口
- `ProtocolParser.cpp`：实现数据帧解析、长度判断、粘包分包处理和数据转换
- `CRC.h`：声明 CRC 计算和校验接口
- `CRC.cpp`：实现通信数据的 CRC 校验算法

#### `src/business/`

- `DataProcessor.h`：声明传感器数据处理器的输入、输出和处理接口
- `DataProcessor.cpp`：实现数据过滤、异常值处理和业务数据转换
- `AlarmManager.h`：声明告警规则、告警状态和通知接口
- `AlarmManager.cpp`：实现阈值判断、告警产生、恢复和状态管理

#### `src/storage/`

- `DatabaseManager.h`：声明数据库初始化、数据保存和历史查询接口
- `DatabaseManager.cpp`：实现 SQLite 建表、读写、查询和批量保存逻辑
- `CsvExporter.cpp`：实现传感器数据或查询结果的 CSV 文件导出功能

#### `src/ui/`

- `MainWindow.h`：声明 Qt 主窗口、界面状态和信号槽接口
- `MainWindow.cpp`：实现设备状态、实时数据、曲线、告警和历史查询交互
- `MainWindow.ui`：保存 Qt Designer 主窗口布局、控件和初始属性

#### `src/common/`

- `SensorData.h`：定义网络、业务、存储和 UI 之间共享的传感器数据结构
- `ConfigManager.h`：声明配置文件读取、校验、查询和保存接口
- `Logger.h`：声明统一日志记录接口、日志级别和输出配置

### `tests/`

- `protocol_test.cpp`：测试协议解析、CRC 校验、半包、粘包和错误帧处理
- `business_test.cpp`：测试数据处理、异常值判断、告警阈值和状态变化
- `storage_test.cpp`：测试数据库建表、数据读写、历史查询和 CSV 导出

### `resources/`

- 项目资源目录，预留 Qt 图片、图标、样式表和其他资源文件
