# 迁移指南 — 重要变更（2026-02）

此文档列出近期针对网络同步与服务器权威的破坏性变更、迁移建议与示例代码，便于升级旧版项目。

## 概要（主要变更）

- **引入独立 Network 模块**：新增 `RDF_LidarNetworkAPI`（基类）与 `RDF_LidarNetworkComponent`（Rpl 实现）以解耦网络逻辑。
- **API 更改：`RequestScan()`** — 变为**无参**方法。服务器会使用组件的 owner 实体作为扫描主体。旧调用 `RequestScan(subject)` 需改为 `RequestScan()`。`
- **配置复制方式变更**：不再直接 Rpl 同步 `RDF_LidarDemoConfig` 对象；改为一组原子 Rpl 属性（`m_RayCount`、`m_UpdateInterval`、`m_RenderWorld`、`m_DrawOriginAxis`、`m_Verbose`）。客户端调用 `SetDemoConfig(cfg)` 会通过 RPC 将这些原子字段发送到服务器。
- **扫描结果同步改为字符串载荷**：服务器执行扫描后将样本序列化为紧凑 CSV 字符串并通过不可靠 RPC 广播；客户端使用 `RDF_LidarExport.ParseCSVToSamples()` 解析回样本数组（避免直接复制复杂类型）。

## 迁移步骤（简洁）

1. 将项目中使用的 `RDF_LidarNetworkComponent` 更新为挂载在合适实体（例如玩家或 GameMode）并确保目标实体包含 `RplComponent`。
2. 若你之前调用 `RequestScan(subject)`，请改为 `RequestScan()`。服务器会以组件所属实体为扫描主体。
3. 若你依赖复制 `RDF_LidarDemoConfig`，请改为使用 `SetDemoConfig(cfg)`（仍兼容），但了解传输为原子字段，服务器会验证并应用这些字段。
4. 若你直接访问或序列化 `RDF_LidarSample`，改为使用 `RDF_LidarExport.SamplesToCSV(samples)` 在服务器端序列化并通过 RPC 广播，客户端使用 `RDF_LidarExport.ParseCSVToSamples(csv)` 解析。

## 示例

- 客户端请求服务器扫描（无需参数）：

```c
// 客户端
RDF_LidarAutoRunner.GetNetworkAPI().RequestScan();
```

- 服务器端序列化并广播（内部流程，示例供参考）：

```c
array<ref RDF_LidarSample> samples = new array<ref RDF_LidarSample>();
scanner.Scan(subject, samples);
string csv = RDF_LidarExport.SamplesToCSV(samples);
Rpc(RpcDo_ScanCompleteWithPayload, csv); // 不可靠广播
```

- 客户端解析：

```c
[RplRpc(RplChannel.Unreliable, RplRcver.Broadcast)]
protected void RpcDo_ScanCompleteWithPayload(string csv)
{
    array<ref RDF_LidarSample> samples = RDF_LidarExport.ParseCSVToSamples(csv);
    // 使用样本更新本地可视化/逻辑
}
```

## 性能与限制

- CSV 字符串不压缩：在高射线计数场景下，建议降低 `m_RayCount` 或实现压缩/分片方案（后续可选）。
- 由于使用不可靠 RPC 广播，数据可能丢失；建议在需要强一致性的场景下增加确认或重试机制。

---

如需我为你的代码库生成自动迁移脚本（批量替换旧调用示例、添加注释）或实现压缩/分片方案，我可以继续实现。