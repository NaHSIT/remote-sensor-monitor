# 模块：工程构建；作用：统一配置项目、依赖和编译目标；编写内容：补充 Qt、Boost、SQLite、测试及资源配置。
cmake_minimum_required(VERSION 3.16)
project(remote_sensor_monitor LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(SQLite3 REQUIRED)
find_package(Boost REQUIRED COMPONENTS system)

# 核心库：协议层（成员A）+ 会话层（成员A）+ 业务/存储层（成员B/C）
add_library(sensor_core
    # 协议层 - 成员A
    src/protocol/CRC.cpp
    src/protocol/ProtocolParser.cpp
    # 会话层 - 成员A
    src/net/SensorSession.cpp
    src/net/DeviceManager.cpp
    # 业务层 - 成员B
    src/business/DataProcessor.cpp
    src/business/AlarmManager.cpp
    # 存储层 - 成员B/C
    src/storage/DatabaseManager.cpp
    src/storage/CsvExporter.cpp
)

target_include_directories(sensor_core PUBLIC src)
target_link_libraries(sensor_core PUBLIC Boost::system)

if(TARGET SQLite::SQLite3)
    target_link_libraries(sensor_core PUBLIC SQLite::SQLite3)
else()
    target_include_directories(sensor_core PUBLIC ${SQLite3_INCLUDE_DIRS})
    target_link_libraries(sensor_core PUBLIC ${SQLite3_LIBRARIES})
endif()

add_executable(sensor-monitor src/main.cpp)
target_link_libraries(sensor-monitor PRIVATE sensor_core)

include(CTest)
if(BUILD_TESTING)
    add_executable(business_test tests/business_test.cpp)
    target_link_libraries(business_test PRIVATE sensor_core)
    add_test(NAME business_test COMMAND business_test)

    add_executable(storage_test tests/storage_test.cpp)
    target_link_libraries(storage_test PRIVATE sensor_core)
    add_test(NAME storage_test COMMAND storage_test)
endif()