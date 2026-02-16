# USED_APIS — Radar-Development-Framework (LiDAR)

本文档列出本项目在 `scripts/` 中实际调用的引擎/运行时 API、常见用法示例，以及我使用 MCP（代码检索 + 手动静态检查）发现的任何不正确或可改进的用法。

---

## 1) 概要（按类别） ✅

- 世界 / 时间 / 调度
  - `GetGame().GetWorld()`, `World.GetWorldTime()`, `GetGame().GetCallqueue().CallLater(...)`
  - 示例文件：`scripts/Game/RDF/Lidar/Network/RDF_LidarNetworkScanner.c`

- 实体与变换
  - `IEntity`, `subject.GetWorldTransform(...)`, `GetWorldCameraTransform(...)`, `GetOwner()`
  - 示例文件：`RDF_LidarScanner.c`, `RDF_LidarVisualizer.c`

- 向量 / 数学
  - `vector`, `Vector(...)`, `Math.Clamp/Max/Sqrt/Atan2/Lerp/...`
  - 示例文件：采样策略与可视化（`Core/*SampleStrategy.c`, `Visual/*`）

- 网络 / 同步（RPL / RPC）
  - `RplComponent`, `[RplRpc(...)]`, `Rpc(...)`, `RplProp`, `RplChannel`, `RplRcver`
  - 示例文件：`RDF_LidarNetworkComponent.c`, `RDF_LidarNetworkScanner.c`

- I/O / 序列化 / 日志
  - `FileIO.OpenFile()`, `FileHandle.WriteLine()`, `Print()`, 自定义 CSV 序列化/Parse（`RDF_LidarExport`）
  - 示例文件：`RDF_LidarExport.c`

- 语言/集合/字符串
  - `array<ref ...>`, `string.Split()`, `Substring()`, `ToFloat()/ToInt()` 等

---

## 2) 关键用法示例（项目内真实调用）

- 世界时间： `float now = GetGame().GetWorld().GetWorldTime();` — `RDF_LidarNetworkScanner.c` / `RDF_LidarNetworkComponent.c`
- RPC 客户端→服务器：`Rpc(RpcAsk_SetDemoConfig, rc, ui, rw, doa, vb);`（Guarded by `m_RplComponent.IsProxy()`）
- RPC 广播（扫描结果）：`Rpc(RpcDo_ScanCompleteWithPayload, csv);`（handler 标注为 `RplChannel.Unreliable, RplRcver.Broadcast`）
- 实体变换：`subject.GetWorldTransform(worldMat);` → `worldMat[3]` 用作原点
- CSV 序列化/反序列化：`RDF_LidarExport.SamplesToCSV()` <-> `RDF_LidarExport.ParseCSVToSamples()`（包含可选 RLE 压缩）

---

## 3) MCP 检查结果（我检索并审查了关键文件：`RDF_LidarNetworkComponent.c`, `RDF_LidarScanner.c`, `RDF_LidarExport.c`）🔍

Summary: 未发现**致命或明显错误的 API 用法**。网络与本地逻辑遵循良好实践（例如：在代理端发送 RPC 前检查 `IsProxy()`；对来自客户端的参数在服务器端做了验证；RPC handler 的 `RplRpc` 注解与 `Rpc()` 调用方向一致）。

已实现的改进（本次提交）

- 序列化优化：新增 `RDF_LidarExport.SamplesToCSVParts()` 并在 `RDF_LidarNetworkComponent.PerformScanInternal()` 中采用“parts 流式打包 + 分片发送”策略，避免为中等大小负载构建单个巨型字符串（降低内存峰值与临时分配）。
- 缓存扫描器：组件内复用 `RDF_LidarScanner` 实例以减少频繁分配。 
- RPC 接收强化：在 `RpcDo_ScanComplete*` 路径增加了解析失败 / 空负载的可选警告（`m_Verbose` 模式）。
- ExportToFile 改进：对文件导出采用“先写临时文件，再覆盖目标”的 best-effort 策略以减少写出不完整文件的窗口（注意：真正的原子重命名取决于运行时 FileIO 是否提供重命名/删除 API）。

建议的进一步优化（仍可选）

- 如引擎支持：用原子文件重命名（rename/move）替换当前的 best-effort 逻辑以实现真正原子写入。
- 若需要更高效网络序列化：考虑引入二进制打包或更强压缩算法（需兼容客户端解析）。

总体评级：代码中的 API 使用模式是正确且一致的（无需要立即修复的问题）。

---

## 4) 我已添加的文件

- `docs/USED_APIS.md`（本文件） — 包含 API 列表、示例与 MCP 检查结论。

---

## 5) 后续建议（可选）

- 我可以把上面提到的“备选时间源”和“额外日志”补丁提交为 PR。✅
- 如果你需要，我可以生成按文件的可机读 JSON 清单，或把每个 API 的用例行号写入文档。

---

如果你想让我把建议直接修到代码里，回复 “修复建议” + 你要采纳的项（例如："添加 world-time 备用值"）。