#include "CsvExporter.h"

#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace {
std::string escapeCsv(const std::string& value) {
    // CSV 中包含逗号、引号或换行时必须整体加引号；内部引号要写成两个引号。
    if (value.find_first_of(",\"\r\n") == std::string::npos) return value;
    std::string escaped{"\""};
    for (char character : value) {
        if (character == '\"') escaped += "\"\"";
        else escaped += character;
    }
    return escaped + '"';
}

std::string formatTimestamp(std::chrono::system_clock::time_point time) {
    // CSV 面向人阅读，因此把内部时间点格式化为 YYYY-MM-DD HH:MM:SS。
    const std::time_t rawTime = std::chrono::system_clock::to_time_t(time);
    std::tm localTime{};
#ifdef _WIN32
    localtime_s(&localTime, &rawTime);
#else
    localtime_r(&rawTime, &localTime);
#endif
    std::ostringstream stream;
    stream << std::put_time(&localTime, "%Y-%m-%d %H:%M:%S");
    return stream.str();
}
} // namespace

bool CsvExporter::exportToFile(const std::vector<SensorData>& data, const std::string& filePath,
                               std::string* errorMessage) {
    if (errorMessage) errorMessage->clear();
    if (filePath.empty()) {
        if (errorMessage) *errorMessage = "CSV file path cannot be empty";
        return false;
    }
    // trunc 表示覆盖旧文件，binary 保证 BOM 字节按原样写入。
    std::ofstream output(filePath, std::ios::binary | std::ios::trunc);
    if (!output) {
        if (errorMessage) *errorMessage = "Unable to open CSV file: " + filePath;
        return false;
    }

    output << "\xEF\xBB\xBF"; // UTF-8 BOM。
    output << "time,device_id,temperature,humidity,pressure,vibration\n";
    output << std::setprecision(15);
    // 先写列名，再逐行写入传感器数据。
    for (const SensorData& sample : data) {
        output << escapeCsv(formatTimestamp(sample.timestamp)) << ','
               << escapeCsv(sample.deviceId) << ','
               << sample.temperature << ',' << sample.humidity << ','
               << sample.pressure << ',' << sample.vibration << '\n';
    }
    if (!output) {
        if (errorMessage) *errorMessage = "Failed while writing CSV file: " + filePath;
        return false;
    }
    return true;
}
