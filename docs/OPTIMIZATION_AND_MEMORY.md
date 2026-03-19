# LiDAR 优化与内存防溢出方案

本文档针对 RDF LiDAR 模块（不含电磁波雷达与体素场）的优化与**内存溢出防护**给出方案与实施要点。

---

## 一、内存风险点概览

| 风险点 | 位置 | 现象 | 优先级 |
|--------|------|------|--------|
| 无界数组增长 | `RDF_LidarNetworkComponent.m_PayloadBuffers` | 分片未到齐或清理滞后时 buffer 累积 | 高 |
| 无界轮询器列表 | `RDF_LidarNetworkScanner.s_Pollers` | 频繁调用 `ScanAsync` 且不完成时堆积 | 高 |
| 每帧大量分配 | `RDF_LidarVisualizer.Render` | 每帧 `Clear`+ 大量 `Shape`+ 批处理时 `trisHits/trisMisses` 多数组 | 中 |
| 每帧新建数组 | `RDF_LidarHUD.UpdatePPI` | 每次 `m_AllCmds = new array` 且每 blip 新建 `array<float>`、`PolygonDrawCommand` | 中 |
| 扫描热路径 | `RDF_LidarScanner.Scan` | 每射线 `new RDF_LidarSample()`，射线数大时对象数线性增长 | 中 |
| 解析时临时数组 | `RDF_LidarExport.ParseCSVToSamples` | 每 part 新建 `array<string> f` 与 `vals`，样本多时分配次数多 | 低 |
| GetLastSamples 拷贝 | `RDF_LidarVisualizer.GetLastSamples` | 每次调用 `new array` 并完整拷贝，频繁调用会加重 GC | 低 |

---

## 二、已实施与建议的防护与优化

### 2.1 网络层：防止无界增长（高优先级）

- **m_PayloadBuffers 上限**  
  - 已有 10 秒超时清理，建议增加**数量上限**（如 16）：超过时丢弃最旧 buffer，避免异常情况下无限堆积导致内存溢出。  
  - 实施：在 `RDF_LidarNetworkComponent` 中定义 `RDF_MAX_PAYLOAD_BUFFERS = 16`，在插入新 buffer 前若 `m_PayloadBuffers.Count() >= RDF_MAX_PAYLOAD_BUFFERS` 则移除最旧项再插入。

- **s_Pollers 上限**  
  - 对 `RDF_LidarNetworkScanner` 的静态列表 `s_Pollers` 设**上限**（如 8）：达到上限时拒绝新 `ScanAsync` 或丢弃最旧 poller，防止因频繁发起异步扫描导致列表与回调堆积。  
  - 实施：在 `ScanAsync` 入口检查 `s_Pollers.Count()`，若已达上限则直接 fallback 本地扫描并回调，不加入新 poller。

### 2.2 可视化：每帧分配与规模上限（中优先级）

- **RDF_LidarVisualizer**  
  - **Shape 数量**：`estShapes = 16 + rays * (segs + extraPoint)` 在 `rays` 很大时（如 10 万）会申请大量 Shape，易导致峰值内存与帧率崩溃。  
  - 建议：对**有效参与绘制的射线数**或 **Shape 数量**设上限（例如 50000 条射线或 200000 个 Shape），超出部分仅做扫描不绘制，或降采样后再绘制。  
  - **DrawBatchedMeshes**：每帧 `new array<ref array<vector>> trisHits/trisMisses` 且每 bucket 一个 `array<vector>`，总顶点数 ≈ rays × segs × 6。可对 `rays` 或总顶点数设上限，超出时跳过批处理或降采样。

- **RDF_LidarHUD.UpdatePPI**  
  - **复用 m_AllCmds**：不要每帧 `m_AllCmds = new array`，改为 `m_AllCmds.Clear()` 后复用，仅重新填入静态命令 + 本帧 blip 命令，避免每 0.5s 一次的大数组分配。  
  - **Blip 数量上限**：已有 `maxBlips = 512` 的步长降采样，建议同时**硬性限制**单帧插入的 blip 命令数量（如 1024），防止异常大样本导致 HUD 分配过多 `PolygonDrawCommand` 与 `array<float>`。  
  - 可选：对 blip 用**对象池**复用 `PolygonDrawCommand` 与顶点数组，减少 GC 压力。

### 2.3 扫描与导出：降低分配频率（中/低优先级）

- **RDF_LidarScanner.Scan**  
  - 当前每射线一个 `new RDF_LidarSample()`，由引擎/GC 回收。若未来在极高射线数（如 50 万+）下出现峰值内存或卡顿，可考虑**样本对象池**：预分配或复用固定大小的 `RDF_LidarSample` 数组，Scan 时只填字段不 new，用完后归还池。  
  - 短期：在文档与配置中建议**射线数上限**（如 100000），避免误配导致 OOM。

- **RDF_LidarExport.ParseCSVToSamples**  
  - 在 `foreach (string part : parts)` 内复用临时数组：**单例 `array<string> vals`** 在循环内每轮 `vals.Clear()` 后重复使用；**单例 `array<string> f`** 同样在每 part 前 `Clear()` 再 `part.Split("|", f, false)`，避免每 part 两次 `new array<string>`，显著减少大 CSV 解析时的分配次数。

### 2.4 GetLastSamples 与调用方（低优先级）

- **GetLastSamples** 每次返回**防御性拷贝**，会 `new array` 并遍历插入。若调用方（如导出、HUD）在每帧或高频率调用，建议：  
  - 导出/HUD 改为使用**回调或单次拉取**，避免每帧调用；或  
  - 提供 `GetLastSamplesView()`（若引擎支持只读视图）避免拷贝；或  
  - 在文档中明确“避免每帧调用 GetLastSamples”。

---

## 三、配置与运行时可调上限建议

在 `RDF_LidarSettings` 或全局配置中可增加（或文档化）以下**安全上限**，防止误配或异常数据导致 OOM：

- **m_RayCount**：运行时 clamp 到例如 `[1, 100000]`（当前仅最小值 1，无上限）。  
- **单次扫描样本数**：若从网络或文件加载的 CSV 单帧样本数超过阈值（如 100000），可拒绝或截断并打日志。  
- **HUD PPI blip 数**：单帧绘制 blip 数不超过 1024（或可配置）。

---

## 四、实施检查清单

- [x] 文档：本优化与内存防溢出方案（本文档）
- [x] 代码：`m_PayloadBuffers` 数量上限（16）+ 超限丢弃最旧
- [x] 代码：`s_Pollers` 数量上限（8）+ 超限时 fallback 本地扫描
- [x] 代码：`RDF_LidarHUD.UpdatePPI` 复用 `m_AllCmds` + blip 数硬上限（1024）
- [x] 代码：`ParseCSVToSamples` 复用 `vals`/`f` 临时数组
- [x] 代码：Visualizer `MAX_DRAW_RAYS = 50000`，绘制与 Reserve 均受上限约束
- [ ] 文档：在 API.md 或 DEVELOPMENT.md 中说明“射线数建议上限”与“避免每帧 GetLastSamples”（可选）

---

## 五、与 C++/引擎差异提醒

- Enforce 无手动 `delete`，依赖引用计数与 GC。**减少分配与对象数量**是减轻内存与溢出风险的主要手段。  
- 大数组（如数万条样本）的复制与传递应尽量避免在热路径重复进行；复用 buffer、设上限、降采样比“先全部分配再依赖 GC”更安全。
