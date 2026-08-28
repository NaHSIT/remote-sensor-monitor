# 模块：工程构建；作用：统一配置项目、依赖和编译目标；编写内容：补充 Qt、Boost、SQLite、测试及资源配置。
cmake_minimum_required(VERSION 3.16)
project(remote_sensor_monitor LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(SQLite3 REQUIRED)

# 成员 B 的核心代码不依赖 Qt，先单独编译成库；Qt UI 层可在上层链接它。
add_library(sensor_core
    src/business/DataProcessor.cpp
    src/business/AlarmManager.cpp
    src/storage/DatabaseManager.cpp
    src/storage/CsvExporter.cpp
)
target_include_directories(sensor_core PUBLIC src)
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
