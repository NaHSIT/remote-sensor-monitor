#pragma once

#include "../common/SensorData.h"

#include <string>
#include <vector>

class CsvExporter {
public:
    // 把数据写成 CSV 文件。返回 false 时，可通过 errorMessage 获取错误原因。
    // 文件以 UTF-8 BOM 开头，Windows Excel 等软件能更好地识别中文和 UTF-8。
    static bool exportToFile(const std::vector<SensorData>& data, const std::string& filePath,
                             std::string* errorMessage = nullptr);
};
