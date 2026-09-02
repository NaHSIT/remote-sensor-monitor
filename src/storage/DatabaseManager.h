#pragma once

#include "../common/SensorData.h"

#include <mutex>
#include <optional>

struct sqlite3;

// SQLite 历史数据管理器。
// 它把 SensorData 暂存在内存批次中，达到数量后再一次性写入数据库。
class DatabaseManager {
public:
    DatabaseManager();
    explicit DatabaseManager(std::string databasePath, std::size_t batchSize = 100);
    ~DatabaseManager();

    // SQLite 连接不应被多个对象复制，所以禁止拷贝。
    DatabaseManager(const DatabaseManager&) = delete;
    DatabaseManager& operator=(const DatabaseManager&) = delete;

    // 打开数据库文件；文件不存在时 SQLite 会自动创建。
    bool open(const std::string& databasePath, std::size_t batchSize = 100);
    // 关闭前会尝试 flush 尚未写入的缓存；写入失败时保留连接和缓存，
    // 便于调用者修复磁盘/权限后重试，而不是静默丢数据。
    void close();
    bool isOpen() const;
    std::string lastError() const;

    // 加入一个待写入样本。缓存达到 batchSize 后自动提交；空设备号和非有限测量值会被拒绝。
    bool save(const SensorData& data);
    // 批量保存同样执行数据有效性检查，保证一个批次不会混入非法数值。
    bool saveBatch(const std::vector<SensorData>& samples);
    // 立即把内存缓存写入数据库。
    bool flush();

    // 查询历史数据。所有筛选条件都是可选的；limit 为 0 表示不限制数量。
    // 查询前会自动 flush，因此返回值包含尚未达到批次阈值的新数据。
    std::vector<SensorData> queryHistory(
        const std::optional<std::string>& deviceId = std::nullopt,
        const std::optional<std::chrono::system_clock::time_point>& from = std::nullopt,
        const std::optional<std::chrono::system_clock::time_point>& to = std::nullopt,
        std::size_t limit = 0);

private:
    // 以下函数要求调用者已经持有 mutex_，避免重复加锁。
    bool createTableLocked();
    bool flushLocked();
    void setErrorLocked(const std::string& message);

    mutable std::mutex mutex_;
    sqlite3* database_ = nullptr; // SQLite 原生连接句柄。
    std::vector<SensorData> pending_; // 尚未提交的内存缓存。
    std::size_t batchSize_ = 100; // 批量写入阈值。
    std::string lastError_; // 最近一次错误，便于 UI 或日志显示。
};
