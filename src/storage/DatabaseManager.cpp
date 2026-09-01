#include "DatabaseManager.h"

#include <sqlite3.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <sstream>
#include <utility>

namespace {
// 建表 SQL：timestamp_ms 使用整数保存 Unix epoch 毫秒，便于排序和范围查询。
constexpr char kCreateTableSql[] = R"sql(
    CREATE TABLE IF NOT EXISTS sensor_data (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        device_id TEXT NOT NULL,
        temperature REAL NOT NULL,
        humidity REAL NOT NULL,
        pressure REAL NOT NULL,
        vibration REAL NOT NULL,
        timestamp_ms INTEGER NOT NULL
    );
    CREATE INDEX IF NOT EXISTS idx_sensor_data_device_time
        ON sensor_data(device_id, timestamp_ms);
)sql";

// 使用 ? 占位符的参数化 SQL，可以避免手动拼接字符串带来的注入和转义问题。
constexpr char kInsertSql[] = R"sql(
    INSERT INTO sensor_data
        (device_id, temperature, humidity, pressure, vibration, timestamp_ms)
    VALUES (?, ?, ?, ?, ?, ?);
)sql";

long long toMilliseconds(const std::chrono::system_clock::time_point& time) {
    // 数据库保存整数毫秒，读取后再还原为 time_point。
    return std::chrono::duration_cast<std::chrono::milliseconds>(time.time_since_epoch()).count();
}

std::chrono::system_clock::time_point fromMilliseconds(long long milliseconds) {
    return std::chrono::system_clock::time_point{std::chrono::milliseconds{milliseconds}};
}

bool hasValidMeasurements(const SensorData& data) {
    // 存储层可能被调用方绕过 DataProcessor 直接使用；再次拒绝非有限值，
    // 避免 NaN/无穷大进入 SQLite 后在历史查询或 CSV 导出中扩散。
    return !data.deviceId.empty() && std::isfinite(data.temperature) &&
           std::isfinite(data.humidity) && std::isfinite(data.pressure) &&
           std::isfinite(data.vibration);
}
} // namespace

DatabaseManager::DatabaseManager() = default;

DatabaseManager::DatabaseManager(std::string databasePath, std::size_t batchSize) {
    open(databasePath, batchSize);
}

DatabaseManager::~DatabaseManager() {
    // RAII：对象销毁时自动释放 SQLite 连接。
    close();
}

bool DatabaseManager::open(const std::string& databasePath, std::size_t batchSize) {
    std::lock_guard<std::mutex> lock(mutex_);
    // 先校验路径，再处理当前连接；失败时保留已有连接，调用者仍可继续使用它。
    if (databasePath.empty()) {
        setErrorLocked("Database path cannot be empty");
        return false;
    }
    if (database_ != nullptr) {
        // 重新打开前先落盘；失败时保留旧连接和缓存，避免切换数据库时丢数据。
        if (!flushLocked()) return false;
        sqlite3_close(database_);
        database_ = nullptr;
    }
    pending_.clear();
    lastError_.clear();
    // 即使调用者传入 0，也保证批次至少有 1 条数据。
    batchSize_ = std::max<std::size_t>(1, batchSize);

    if (sqlite3_open_v2(databasePath.c_str(), &database_, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK) {
        setErrorLocked(database_ != nullptr ? sqlite3_errmsg(database_) : "Unable to allocate SQLite database");
        if (database_ != nullptr) {
            sqlite3_close(database_);
            database_ = nullptr;
        }
        return false;
    }
    // 数据库被其他线程短暂占用时，最多等待 5 秒而不是立即失败。
    sqlite3_busy_timeout(database_, 5'000);
    if (!createTableLocked()) {
        sqlite3_close(database_);
        database_ = nullptr;
        return false;
    }
    return true;
}

void DatabaseManager::close() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (database_ == nullptr) {
        return;
    }
    // 写入失败时保留连接和 pending_，调用者可修复磁盘/权限后再次 flush。
    if (!flushLocked()) return;
    if (sqlite3_close(database_) != SQLITE_OK) {
        setErrorLocked(sqlite3_errmsg(database_));
        return;
    }
    database_ = nullptr;
    pending_.clear();
}

bool DatabaseManager::isOpen() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return database_ != nullptr;
}

std::string DatabaseManager::lastError() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return lastError_;
}

bool DatabaseManager::save(const SensorData& data) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (database_ == nullptr) {
        setErrorLocked("Database is not open");
        return false;
    }
    if (data.deviceId.empty()) {
        setErrorLocked("Device ID cannot be empty");
        return false;
    }
    if (!hasValidMeasurements(data)) {
        setErrorLocked("Sensor data contains invalid values");
        return false;
    }
    pending_.push_back(data);
    return pending_.size() < batchSize_ || flushLocked();
}

bool DatabaseManager::saveBatch(const std::vector<SensorData>& samples) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (database_ == nullptr) {
        setErrorLocked("Database is not open");
        return false;
    }
    if (std::any_of(samples.begin(), samples.end(),
                    [](const SensorData& data) { return data.deviceId.empty(); })) {
        setErrorLocked("Device ID cannot be empty");
        return false;
    }
    if (std::any_of(samples.begin(), samples.end(),
                    [](const SensorData& data) { return !hasValidMeasurements(data); })) {
        setErrorLocked("Sensor data contains invalid values");
        return false;
    }
    if (samples.empty()) return true;
    pending_.insert(pending_.end(), samples.begin(), samples.end());
    return pending_.size() < batchSize_ || flushLocked();
}

bool DatabaseManager::flush() {
    std::lock_guard<std::mutex> lock(mutex_);
    return flushLocked();
}

std::vector<SensorData> DatabaseManager::queryHistory(
    const std::optional<std::string>& deviceId,
    const std::optional<std::chrono::system_clock::time_point>& from,
    const std::optional<std::chrono::system_clock::time_point>& to,
    std::size_t limit) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<SensorData> results;
    if (database_ == nullptr) {
        setErrorLocked("Database is not open");
        return results;
    }
    // 查询前先提交缓存，否则查不到刚刚 save 但还没达到批次大小的数据。
    if (!flushLocked()) {
        return results;
    }

    // 根据调用者提供的条件动态添加 WHERE 子句，但值始终使用参数绑定。
    std::string sql = "SELECT device_id, temperature, humidity, pressure, vibration, timestamp_ms FROM sensor_data WHERE 1 = 1";
    if (deviceId) sql += " AND device_id = ?";
    if (from) sql += " AND timestamp_ms >= ?";
    if (to) sql += " AND timestamp_ms <= ?";
    sql += " ORDER BY timestamp_ms ASC, id ASC";
    if (limit > 0) sql += " LIMIT ?";

    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(database_, sql.c_str(), -1, &statement, nullptr) != SQLITE_OK) {
        setErrorLocked(sqlite3_errmsg(database_));
        return results;
    }
    int parameter = 1;
    if (deviceId) sqlite3_bind_text(statement, parameter++, deviceId->c_str(), -1, SQLITE_TRANSIENT);
    if (from) sqlite3_bind_int64(statement, parameter++, toMilliseconds(*from));
    if (to) sqlite3_bind_int64(statement, parameter++, toMilliseconds(*to));
    if (limit > 0) {
        const auto maxSqliteInteger = static_cast<std::size_t>(std::numeric_limits<sqlite3_int64>::max());
        if (limit > maxSqliteInteger) {
            sqlite3_finalize(statement);
            setErrorLocked("History query limit is too large");
            return results;
        }
        sqlite3_bind_int64(statement, parameter++, static_cast<sqlite3_int64>(limit));
    }

    int rc = SQLITE_ROW;
    // sqlite3_step 每次返回一行；SQL_DONE 表示遍历结束。
    while ((rc = sqlite3_step(statement)) == SQLITE_ROW) {
        SensorData data;
        const unsigned char* text = sqlite3_column_text(statement, 0);
        data.deviceId = text == nullptr ? "" : reinterpret_cast<const char*>(text);
        data.temperature = sqlite3_column_double(statement, 1);
        data.humidity = sqlite3_column_double(statement, 2);
        data.pressure = sqlite3_column_double(statement, 3);
        data.vibration = sqlite3_column_double(statement, 4);
        data.timestamp = fromMilliseconds(sqlite3_column_int64(statement, 5));
        results.push_back(std::move(data));
    }
    if (rc != SQLITE_DONE) setErrorLocked(sqlite3_errmsg(database_));
    sqlite3_finalize(statement);
    return results;
}

bool DatabaseManager::createTableLocked() {
    // IF NOT EXISTS 让 open 可以重复调用，不会因表已存在而失败。
    char* error = nullptr;
    if (sqlite3_exec(database_, kCreateTableSql, nullptr, nullptr, &error) != SQLITE_OK) {
        setErrorLocked(error == nullptr ? sqlite3_errmsg(database_) : error);
        sqlite3_free(error);
        return false;
    }
    return true;
}

bool DatabaseManager::flushLocked() {
    if (database_ == nullptr) {
        setErrorLocked("Database is not open");
        return false;
    }
    if (pending_.empty()) return true;

    // 一个事务包住整个批次：要么全部写入，要么全部回滚，避免半批数据。
    char* error = nullptr;
    if (sqlite3_exec(database_, "BEGIN IMMEDIATE TRANSACTION;", nullptr, nullptr, &error) != SQLITE_OK) {
        setErrorLocked(error == nullptr ? sqlite3_errmsg(database_) : error);
        sqlite3_free(error);
        return false;
    }

    sqlite3_stmt* statement = nullptr;
    bool success = sqlite3_prepare_v2(database_, kInsertSql, -1, &statement, nullptr) == SQLITE_OK;
    if (!success) setErrorLocked(sqlite3_errmsg(database_));
    for (const SensorData& data : pending_) {
        if (!success) break;
        sqlite3_bind_text(statement, 1, data.deviceId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_double(statement, 2, data.temperature);
        sqlite3_bind_double(statement, 3, data.humidity);
        sqlite3_bind_double(statement, 4, data.pressure);
        sqlite3_bind_double(statement, 5, data.vibration);
        sqlite3_bind_int64(statement, 6, toMilliseconds(data.timestamp));
        success = sqlite3_step(statement) == SQLITE_DONE;
        if (!success) setErrorLocked(sqlite3_errmsg(database_));
        sqlite3_reset(statement);
        sqlite3_clear_bindings(statement);
    }
    sqlite3_finalize(statement);
    const char* finishSql = success ? "COMMIT;" : "ROLLBACK;";
    error = nullptr;
    if (sqlite3_exec(database_, finishSql, nullptr, nullptr, &error) != SQLITE_OK) {
        setErrorLocked(error == nullptr ? sqlite3_errmsg(database_) : error);
        sqlite3_free(error);
        return false;
    }
    if (success) pending_.clear();
    return success;
}

void DatabaseManager::setErrorLocked(const std::string& message) {
    lastError_ = message;
}
