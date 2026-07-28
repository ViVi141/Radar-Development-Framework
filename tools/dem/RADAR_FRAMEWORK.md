> **Languages / 语言**: [English](#english) · [中文](#中文)

# RDF Radar Simulation Framework

## English

The offline prototype is technology- and era-agnostic. Named radars are example
presets, not core dependencies.

## Layers

- `rdf_dem_io.py`: terrain, material, and vertical-span environment.
- `rdf_dem_pack.py` / `rdf_dem_bake_help.py` / `rdf_dem_preview.py`: pack,
  bake helpers, and heightfield preview (also covered in `docs/DEM.md`).
- `rdf_dem_pack_bin.py` / `rdf_dem_pack_json.py` / `rdf_dem_pack_surface_json.py`:
  pack V3 bake into `.dem.data`, full DEM JSON, or SURF JSON (workshop path).
- `rdf_sig_pack_bin.py` / `rdf_sig_pack_json.py` / `rdf_sig_pack_conf.py`:
  pack signature tables for game / profile / `.conf`.
- `rdf_ttile_unpack.py`: unpack official `.ttile` height for offline overlays.
- `rdf_radar_materials.py`: calibratable band/sea-state sigma-zero tables
  (`calib/sigma0_x_ss3.json`).
- `rdf_radar_physics.py`: hardware, waveform, elevation beams, radar equation,
  pulse compression, integration, Doppler, MTI, and CFAR.
- `rdf_radar_channel.py`: frequency retune, multipath, Swerling RCS, spectral
  overlap helpers.
- `rdf_radar_targets.py`: target state and time-parametric trajectories.
- `rdf_radar_sector_sim.py` / `rdf_radar_sector_preview.py`: static full-sector
  RF/clutter snapshot and sector preview plots.
- `rdf_radar_scan.py`: `scan(t)`, dwell updates, persistent PPI, moving-track
  intercepts, per-beam measurements, hop-aware retune.
- `rdf_radar_ew.py`: pluggable noise/deception effects and frequency schedules.
- `rdf_radar_track.py`: nearest-neighbor association and alpha-beta filtering.
- `rdf_radar_framework_demo.py`: clean/EW validation output and CSV dumps.
- `rdf_radar_shellfire_offline.py`: offline shell-fire / WLR mirror of the
  in-game ShellFire regression.
- `rdf_radar_mass_battle_sim.py`: DEM-backed multi-radar / multi-target battle,
  trajectory prediction, and WLR evaluation (large monolith; split tracked in
  TODO.md).
- `test_rdf_radar_ballistics.py`: deterministic ballistics, wind, drag, DEM
  intersection, and WLR regression tests.

Install: `pip install -r tools/dem/requirements.txt` (numpy + matplotlib).
Generated images/CSV under `tools/dem/out/` are gitignored — do not commit.

## Framework contracts

- `RadarHardware`: waveform and receiver parameters.
- `ElevationBeam[]`: zero, one, or many elevation beams.
- `TargetTrajectory.sample(t)`: target motion without coupling to the radar.
- `hardware_at_frequency(f)`: retune wavelength / atmosphere / band label.
- `EWEffect.apply(power, context)`: optional EW processing before CFAR.
- `FrequencyHopSchedule.frequency_at(t)`: configurable frequency agility.
- `associate_and_filter(detections)`: track birth / update / coast.

## Processing chain per dwell

1. Mechanical/electronic `boresight_at(t)`
2. Frequency hop → retuned `RadarHardware`
3. Scale clutter by radar-constant and mild σ⁰ frequency factor
4. Target RCS (Swerling) × multipath × pattern × radar equation
5. Pulse compression / integration / MTI
6. EW plugins (noise spectral overlap, deception)
7. CA-CFAR
8. Cross-scan alpha-beta tracker

## Examples

```powershell
python tools\dem\rdf_radar_framework_demo.py --preset shorad
python tools\dem\rdf_radar_framework_demo.py --preset shorad --ew
python tools\dem\rdf_radar_framework_demo.py --preset p18
python tools\dem\rdf_radar_framework_demo.py --use-ttile --world GM_Eden --preset shorad
python tools\dem\rdf_radar_mass_battle_sim.py
python tools\dem\rdf_radar_mass_battle_sim.py --use-ttile --world GM_Eden
python tools\dem\rdf_ttile_unpack.py
python tools\dem\test_rdf_radar_ballistics.py
```

Outputs under `tools/dem/out/`:

- `*_framework_*.png`
- `*_detections.csv`
- `*_tracks.csv`
- `mass_battle_*.png`
- `mass_battle_*.csv`
- `*_ttile_height.png` / `.npz` (from `rdf_ttile_unpack.py`)

`--use-ttile` loads `out/<World>_ttile_height.npz` (2 m height from official
`.ttile`) and overlays packaged `DemData/<GM_World>/` SURF JSON for
`surface_class` → σ⁰. Use `--no-surf` to skip the overlay, or `--surf-dir`.

## Current fidelity boundaries

- No IQ baseband sampling, phase noise, AGC loops, or full PD Doppler filter bank.
- Multipath is a two-ray engineering factor, not a full terrain-bounce solver.
- Moving-target LOS currently uses terrain height (static clutter still uses spans).
- Frequency-dependent clutter remapping is a scale factor, not a full re-bake.
- Long-range presets are geometrically limited by the loaded map DEM.

---

## 中文

离线原型与具体技术体制、年代解耦。具名雷达仅为示例预设，不是核心依赖。

## 分层

- `rdf_dem_io.py`：地形、材质与垂直跨度环境。
- `rdf_dem_pack.py` / `rdf_dem_bake_help.py` / `rdf_dem_preview.py`：打包、烘焙辅助与高程预览（亦见 `docs/DEM.md`）。
- `rdf_dem_pack_bin.py` / `rdf_dem_pack_json.py` / `rdf_dem_pack_surface_json.py`：将 V3 烘焙打成 `.dem.data`、全量 DEM JSON 或 SURF JSON（工坊路径）。
- `rdf_sig_pack_bin.py` / `rdf_sig_pack_json.py` / `rdf_sig_pack_conf.py`：为游戏 / profile / `.conf` 打包特征表。
- `rdf_ttile_unpack.py`：解包官方 `.ttile` 高程，供离线叠加。
- `rdf_radar_materials.py`：可标定的频段/海况 σ⁰ 表（`calib/sigma0_x_ss3.json`）。
- `rdf_radar_physics.py`：硬件、波形、俯仰波束、雷达方程、脉压、积累、多普勒、MTI、CFAR。
- `rdf_radar_channel.py`：频率重调、多径、Swerling RCS、频谱重叠辅助。
- `rdf_radar_targets.py`：目标状态与时间参数化轨迹。
- `rdf_radar_sector_sim.py` / `rdf_radar_sector_preview.py`：静态全扇区 RF/杂波快照与扇区预览图。
- `rdf_radar_scan.py`：`scan(t)`、驻留更新、持久 PPI、动目标截获、分波束量测、跳频感知重调。
- `rdf_radar_ew.py`：可插拔噪声/欺骗效果与频率时间表。
- `rdf_radar_track.py`：最近邻关联与 α-β 滤波。
- `rdf_radar_framework_demo.py`：干净通道 / EW 验证输出与 CSV。
- `rdf_radar_shellfire_offline.py`：游戏内 ShellFire 回归的离线炮弹 / WLR 镜像。
- `rdf_radar_mass_battle_sim.py`：基于 DEM 的多雷达 / 多目标战场、轨迹预测与 WLR 评估（大单体；拆分见 TODO.md）。
- `test_rdf_radar_ballistics.py`：确定性弹道、风、阻力、DEM 交点与 WLR 回归测试。

安装：`pip install -r tools/dem/requirements.txt`（numpy + matplotlib）。
`tools/dem/out/` 下生成的图片/CSV 已 gitignore — 请勿提交。

## 框架契约

- `RadarHardware`：波形与接收机参数。
- `ElevationBeam[]`：零个、一个或多个俯仰波束。
- `TargetTrajectory.sample(t)`：目标运动，不与雷达耦合。
- `hardware_at_frequency(f)`：重调波长 / 大气 / 频段标签。
- `EWEffect.apply(power, context)`：CFAR 前可选 EW 处理。
- `FrequencyHopSchedule.frequency_at(t)`：可配置频率捷变。
- `associate_and_filter(detections)`：航迹起始 / 更新 / 滑行（coast）。

## 每次驻留的处理链

1. 机械/电子 `boresight_at(t)`
2. 频率跳变 → 重调后的 `RadarHardware`
3. 按雷达常数与轻度 σ⁰ 频率因子缩放杂波
4. 目标 RCS（Swerling）× 多径 × 方向图 × 雷达方程
5. 脉压 / 积累 / MTI
6. EW 插件（噪声频谱重叠、欺骗）
7. CA-CFAR
8. 跨扫描 α-β 跟踪器

## 示例

```powershell
python tools\dem\rdf_radar_framework_demo.py --preset shorad
python tools\dem\rdf_radar_framework_demo.py --preset shorad --ew
python tools\dem\rdf_radar_framework_demo.py --preset p18
python tools\dem\rdf_radar_framework_demo.py --use-ttile --world GM_Eden --preset shorad
python tools\dem\rdf_radar_mass_battle_sim.py
python tools\dem\rdf_radar_mass_battle_sim.py --use-ttile --world GM_Eden
python tools\dem\rdf_ttile_unpack.py
python tools\dem\test_rdf_radar_ballistics.py
```

输出位于 `tools/dem/out/`：

- `*_framework_*.png`
- `*_detections.csv`
- `*_tracks.csv`
- `mass_battle_*.png`
- `mass_battle_*.csv`
- `*_ttile_height.png` / `.npz`（来自 `rdf_ttile_unpack.py`）

`--use-ttile` 加载 `out/<World>_ttile_height.npz`（官方 `.ttile` 的 2 m 高程），并叠加打包的 `DemData/<GM_World>/` SURF JSON，用于 `surface_class` → σ⁰。用 `--no-surf` 跳过叠加，或用 `--surf-dir` 指定目录。

## 当前保真度边界

- 无 IQ 基带采样、相位噪声、AGC 环或完整 PD 多普勒滤波器组。
- 多径为双射线工程因子，不是完整地形反射求解器。
- 动目标通视目前用地形高度（静态杂波仍用垂直跨度）。
- 频率相关杂波重映射是比例因子，不是完整重烘焙。
- 远距离预设受已加载地图 DEM 的几何范围限制。
