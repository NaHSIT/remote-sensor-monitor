# 成员 B 代码入门讲解

这份文档给第一次接触 C++ 项目的同学使用。成员 B 的工作可以概括为：

```text
收到 SensorData
      ↓
DataProcessor 检查和整理数据
      ↓
AlarmManager 判断是否越过告警阈值
      ↓
DatabaseManager 保存历史数据
      ↓
CsvExporter 把历史数据导出成 CSV
```

## 1. 先认识 SensorData

文件：`src/common/SensorData.h`

`SensorData` 是项目中各模块共同使用的一条传感器记录：

```cpp
SensorData data;
data.deviceId = "sensor-001";
data.temperature = 25.2;
data.humidity = 60.3;
data.pressure = 101.2;
data.vibration = 0.15;
```

可以把它理解成一行表格。网络模块负责“填表”，业务模块负责“检查表”，数据库模块负责“保存表”。`timestamp` 是采样时间，类型是 `std::chrono::system_clock::time_point`，比直接使用字符串更适合排序和时间范围查询。

## 2. DataProcessor：数据进入系统后的第一关

文件：`src/business/DataProcessor.h/.cpp`

### 2.1 数据校验

`DataValidationRules` 保存允许的最小值和最大值。例如默认温度范围是 `-80 ~ 85`，湿度范围是 `0 ~ 100`。`process()` 会检查：

1. `deviceId` 是否为空；
2. 温度、湿度、压力、振动是否是有限数字；
3. 每个数值是否落在配置的上下限之内。

只要有一项不符合，就返回 `std::nullopt`：

```cpp
DataProcessor processor;
auto result = processor.process(data);
if (!result) {
    // 数据无效：可以记录日志，并丢弃这一条数据。
    return;
}
SensorData cleanData = *result;
```

`std::optional<SensorData>` 的意思是“可能有一个 SensorData，也可能没有”。它比返回一个特殊的错误数值更清楚。

### 2.2 滑动平均

传感器读数可能有轻微抖动，因此处理器支持滑动平均：

```cpp
DataProcessor processor({}, 3); // 每个设备最多参考最近 3 个样本
```

每个设备都有自己的一条 `deque`（双端队列）。新数据放到队尾，超过窗口大小时从队首删除最旧数据，然后计算平均值。不同设备的历史数据不会混在一起。

窗口大小为 `1` 时不做平均，直接返回原始数据。窗口设置为 `0` 也会自动修正为 `1`，这样可以避免除以零。

### 2.3 为什么有 mutex

采集程序可能有多个线程同时送入数据。`mutex_` 是互斥锁，保证同一时刻只有一个线程修改规则或平均值缓存。`std::lock_guard` 会自动加锁和解锁，即使函数中途返回也不会忘记解锁。

## 3. AlarmManager：只报告状态变化

文件：`src/business/AlarmManager.h/.cpp`

### 3.1 阈值和事件

`AlarmThresholds` 为温度、湿度、压力和振动分别设置上下限。数值小于下限或大于上限时，认为当前告警为 active。

`AlarmEvent` 记录一次状态变化：设备编号、测量类型、当时的值、阈值、时间和 `active` 状态。

### 3.2 为什么不会重复报警

管理器使用 `(deviceId, measurementType)` 作为 key 保存上一次状态：

```text
正常 → 越界：产生一条 active=true 的告警
越界 → 越界：不重复产生告警
越界 → 正常：产生一条 active=false 的恢复事件
```

这样 UI 或日志不会每秒收到大量完全相同的告警。

### 3.3 回调示例

```cpp
AlarmManager alarms;
alarms.setCallback([](const AlarmEvent& event) {
    if (event.active) {
        // 弹窗、写日志或通知 UI。
    } else {
        // 显示告警已恢复。
    }
});

for (const SensorData& data : samples) {
    alarms.evaluate(data);
}
```

回调会在内部锁释放后执行，避免 UI 或日志操作耗时太久时阻塞其他传感器。

## 4. DatabaseManager：SQLite 历史存储

文件：`src/storage/DatabaseManager.h/.cpp`

### 4.1 打开数据库

```cpp
DatabaseManager database("sensor.db", 100);
```

如果文件不存在，SQLite 会创建它。打开时会自动创建 `sensor_data` 表和设备/时间索引。表中的 `timestamp_ms` 是 Unix epoch 毫秒整数，查询和排序都比较方便。

### 4.2 保存数据

```cpp
database.save(cleanData);       // 加入缓存
database.flush();               // 需要时立即写入
database.saveBatch(batch);      // 一次加入多条
```

默认累计 100 条后自动写入。写入时使用事务：

```text
BEGIN → 插入所有样本 → COMMIT
                     ↘ 出错时 ROLLBACK
```

因此不会出现“一个批次只写进去一半”的情况。SQL 中的 `?` 是参数占位符，实际值通过 SQLite API 绑定，避免手动拼接字符串造成转义错误或 SQL 注入。

### 4.3 查询历史

```cpp
auto history = database.queryHistory(
    std::string{"sensor-001"}, // 设备，可传 nullopt 表示全部设备
    from,                       // 起始时间，可选
    to,                         // 结束时间，可选
    100                         // 最多返回 100 条，0 表示不限制
);
```

查询前会先 `flush()`，所以刚刚加入缓存的数据也能被查询到。发生错误时，可以调用 `lastError()` 获取最近一次错误文本。

## 5. CsvExporter：导出给人或表格软件

文件：`src/storage/CsvExporter.h/.cpp`

```cpp
std::string error;
bool ok = CsvExporter::exportToFile(history, "history.csv", &error);
if (!ok) {
    // error 中有打开或写入失败的原因。
}
```

导出文件的第一行是：

```text
time,device_id,temperature,humidity,pressure,vibration
```

设备编号如果包含逗号、引号或换行，CSV 规则要求用双引号包起来，并把内部的一个引号写成两个引号。文件开头写入 UTF-8 BOM，Windows Excel 打开中文内容时不容易出现乱码。

## 6. 一个完整的处理示例

```cpp
DataProcessor processor({}, 3);
AlarmManager alarms;
DatabaseManager database("sensor.db");

alarms.setCallback([](const AlarmEvent& event) {
    // 把 event 转发给 UI 或日志模块。
});

    auto processed = processor.process(raw);
    if (!processed) {
        // 原始数据不合法，不能继续进入告警和数据库。
        return;
    }

    const SensorData& data = *processed;
    alarms.evaluate(data);
    if (!database.save(data)) {
        // 记录 database.lastError()。
    }
}
```

这里的顺序很重要：先过滤，再告警和存储。否则坏数据可能触发错误告警，或者污染历史数据库。

## 7. 初学者常见问题

### `std::move` 是什么？

它允许函数把临时对象的资源“移动”到成员变量中，减少不必要的复制。这里不会改变业务含义，可以先把它理解为一种性能优化。

### `const SensorData&` 为什么不是直接传值？

`const` 表示函数不会修改输入，`&` 表示不复制整个结构体。传感器数据较多时这样更高效。

### 为什么析构函数里还要 `close()`？

这是 RAII 思想：对象离开作用域时自动释放文件、数据库连接等资源，减少忘记关闭导致的数据丢失和资源泄漏。

### 为什么不能复制 `DatabaseManager`？

一个管理器内部只有一个 SQLite 连接句柄。禁止复制可以避免两个对象误用同一个底层连接。

## 8. 推荐联调顺序

1. 先构造一条正常 `SensorData`，确认 `DataProcessor::process()` 能返回数据；
2. 传入空设备 ID、NaN 或越界值，确认返回空值；
3. 连续传入越界/正常数据，确认 `AlarmManager` 只返回产生和恢复两次事件；
4. 调用 `DatabaseManager::save()`、`flush()` 和 `queryHistory()`，确认数量和字段一致；
5. 把查询结果交给 `CsvExporter`，用文本编辑器或表格软件检查列名、时间和设备 ID。
