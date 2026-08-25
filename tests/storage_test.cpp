#include "storage/CsvExporter.h"
#include "storage/DatabaseManager.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iterator>

int main() {
    const auto base = std::filesystem::temp_directory_path();
    const auto databasePath = base / "remote_sensor_monitor_test.sqlite";
    const auto csvPath = base / "remote_sensor_monitor_test.csv";
    std::filesystem::remove(databasePath);
    std::filesystem::remove(csvPath);
    DatabaseManager database(databasePath.string(), 2);
    assert(database.isOpen());
    SensorData data;
    data.deviceId = "sensor,001";
    data.temperature = 25.2;
    data.humidity = 60.3;
    data.pressure = 101.2;
    data.vibration = 0.15;
    assert(database.save(data));
    assert(database.queryHistory().size() == 1); // 查询会先自动 flush 缓存。
    const auto history = database.queryHistory(std::string{"sensor,001"});
    assert(history.size() == 1 && history.front().deviceId == data.deviceId);
    std::string error;
    assert(CsvExporter::exportToFile(history, csvPath.string(), &error));
    std::ifstream csv(csvPath, std::ios::binary);
    const std::string content((std::istreambuf_iterator<char>(csv)), {});
    assert(content.find("device_id") != std::string::npos);
    assert(content.find("\"sensor,001\"") != std::string::npos);
    database.close();
    std::filesystem::remove(databasePath);
    std::filesystem::remove(csvPath);
    return 0;
}
