> **Languages / 语言**: [English](#english) · [中文](#中文)

# RDF Radar Simulation Framework

## English

The offline prototype is technology- and era-agnostic. Named radars are example
presets, not core dependencies.

## Layers

Catalog index: [tools/README.md](../README.md). Scripts remain flat under `tools/dem/`.

### Pack / Bake

- `rdf_dem_io.py`: terrain, material, and vertical-span environment.
- `rdf_dem_pack.py` / `rdf_dem_bake_help.py` / `rdf_dem_preview.py`: pack,
  bake helpers, and heightfield preview (also covered in `docs/DEM.md`).
- `rdf_dem_pack_bin.py` / `rdf_dem_pack_json.py` / `rdf_dem_pack_surface_json.py` /
  `rdf_dem_pack_height_json.py`:
  pack V3 bake into `.dem.data`, full DEM JSON, SURF JSON, or HEIGHT JSON from official `.ttile`.
- `rdf_sig_pack_bin.py` / `rdf_sig_pack_json.py` / `rdf_sig_pack_conf.py`:
  pack signature tables for game / profile / `.conf`.
- `rdf_sig_patch_heli_rotors.py`: fill UH-1 / Mi-8 rotor tip/blades/hub fields
  in signature conf.
- `rdf_ttile_unpack.py`: unpack official `.ttile` height for offline overlays.

### Simulation libraries

- `rdf_radar_materials.py`: calibratable band/sea-state sigma-zero tables
  (`calib/sigma0_x_ss3.json`).
- `rdf_radar_physics.py`: hardware, waveform, elevation beams, radar equation,
  pulse compression, integration, Doppler, MTI / MTD bank, and CFAR.
- `rdf_radar_channel.py`: frequency retune, Swerling RCS, aspect RCS, spectral
  overlap; `ChannelFidelity` opt-in helpers (`enable_los_two_ray` /
  `enable_refraction` / `enable_prf_ambiguity_folds` / `stabilize_for_regression`)
  for LOS two-ray multipath, 4/3-Earth refraction, and PRF range/Doppler folds
  (default OFF — mirrors in-game Enable* APIs). `MultipathModel.enabled`
  defaults to False.
- `rdf_radar_targets.py`: target state and time-parametric trajectories.
- `rdf_radar_sector_sim.py`: static full-sector RF/clutter snapshot.
- `rdf_radar_scan.py`: `scan(t)`, dwell updates, persistent PPI, moving-track
  intercepts, per-beam measurements, hop-aware retune; `ScanConfig.fidelity`
  applies optional multipath / radio-horizon soft factor / plot folds.
- `rdf_radar_ew.py`: pluggable noise/deception effects and frequency schedules.
- `rdf_radar_diffraction.py`: knife-edge diffraction helpers.
- `rdf_radar_track.py`: nearest-neighbor association and alpha-beta filtering;
  `RadarObservation` inverse DTO + frozen-observation golden in `test_rdf_radar_track.py`.
- `rdf_radar_fusion.py`: multi-radar association / horizontal cross-fix.
- `rdf_radar_systems.py`: lock FSM, ESM Friis, RWR/ARM hooks, GO/SO-CFAR,
  measurement noise, rain path loss, RGPO/angular/intermittent deception,
  Network Reliable/Unreliable / caps / throttle / interest / fingerprint;
  `MeasurementModel.from_fidelity` applies opt-in range/Doppler fold and
  refraction elevation bias (WLR skips Doppler fold via `weapon_locate`).

### CLI demos / validation

- `rdf_radar_sector_preview.py`: sector preview plots.
- `rdf_radar_framework_demo.py`: clean/EW validation output and CSV dumps.
- `rdf_radar_shellfire_offline.py`: offline shell-fire / WLR mirror of the
  in-game ShellFire regression.
- `rdf_radar_mass_battle_sim.py`: thin facade into `mass_battle/` package
  (logic in `_impl.py`; thematic modules are re-exports — see
  `mass_battle/README.md`).
- `rdf_radar_hw_calibrate.py`: offline HW bake-back →
  `calib/prf_clutter_*.json` (copy to `$profile:RDF/RadarData/HwCalib.json`).
- `rdf_radar_full_sim.py`: no-DEM orchestrator exercising capability coverage
  (writes `out/full_sim_report.json`).
- `rdf_knife_edge_eden_validate.py`: Eden knife-edge validation helper.
- `run_tools.ps1` / `run_tools.sh`: entry helpers (`test` / `full-sim` / `demo`).

### Golden tests

- `test_rdf_radar_ballistics.py`: deterministic ballistics, wind, drag, DEM
  intersection, and WLR regression tests.
- `test_rdf_radar_cfar.py`: CA-CFAR golden + Enforce `RDF_RadarCfarGate` CA parity.
- `test_rdf_radar_track.py`: association / α-β / polar roundtrip / vacuum fit golden.
- `test_rdf_radar_ew.py`: noise jam coupling / burn-through.
- `test_rdf_radar_diffraction.py`: knife-edge factor geometry.
- `test_rdf_radar_fusion.py`: associate / cross-fix.
- `test_rdf_radar_systems.py`: systems unit tests + full_sim smoke.
- `test_rdf_radar_mtd.py`: MTD bank / rotor micro-Doppler.
- `test_rdf_radar_heli_cpa.py`: helicopter CPA / body-null vs tip survival.
- `test_rdf_radar_propagation.py`: two-ray / refraction / PRF ambiguity folds.
- `test_rdf_radar_hw_calibrate.py`: HW calib suggest / preset JSON.
- `test_rdf_radar_sig_pack.py`: signature pack / rotor fields.
- `test_rdf_sig_patch_heli_rotors.py`: heli rotor conf patch.

Install: `pip install -r tools/dem/requirements.txt` (numpy + matplotlib).
Golden suite: `cd tools/dem` then `.\run_tools.ps1 test` or
`python -m unittest discover -s . -p "test_rdf_*.py" -v`
(CI: `.github/workflows/python-dem-tests.yml`).
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
7. CA/GO/SO-CFAR (+ optional thermal fill)
8. Cross-scan alpha-beta tracker
9. Lock / RWR / ARM, fusion / IFF, Network bandwidth policy (systems layer)

## Capability coverage matrix (offline)

| Capability | Module / entry |
|------------|----------------|
| Radar equation, MTI, Doppler | `rdf_radar_physics` + `full_sim` |
| MTD bank + rotor micro-Doppler + heli CPA | `test_rdf_radar_mtd` / `heli_cpa` + `full_sim` |
| PRF stagger | `RadarHardware.active_prf_hz` + `full_sim` |
| Signature rotor pack | `rdf_sig_pack_conf` / `test_rdf_radar_sig_pack` |
| Heli rotor conf fill | `rdf_sig_patch_heli_rotors` / `test_rdf_sig_patch_heli_rotors` |
| HW clutter / MTD bake-back | `rdf_radar_hw_calibrate` → `calib/prf_clutter_*.json` |
| Track coast on miss | `TrackerConfig.coast_on_miss` + `test_rdf_radar_track` |
| Atmosphere / rain | `rdf_radar_systems.two_way_path_loss_db` |
| Measurement noise | `MeasurementModel` |
| Multipath / knife-edge | `rdf_radar_channel` / `rdf_radar_diffraction` |
| LOS two-ray / 4/3 refraction / PRF folds | `ChannelFidelity` + `rdf_radar_channel` + `MeasurementModel` / `ScanConfig` + `test_rdf_radar_propagation` + `full_sim` (`propagation.refraction_horizon` / `ambiguity_fold`) |
| CA / GO / SO-CFAR + thermal fill | `cfar_detections` / `fill_thermal_noise` |
| Aspect RCS / Swerling | `rdf_radar_channel` |
| α-β track / WLR fit | `rdf_radar_track` |
| EW noise + burn-through | `rdf_radar_ew` |
| Deception / RGPO / angular / intermittent | `ew` + `systems` |
| Frequency hop | `FrequencyHopSchedule` |
| Fusion / cross-fix / IFF | `rdf_radar_fusion` + `resolve_iff` |
| Lock / RWR / ARM / ESM Friis | `rdf_radar_systems` |
| Network caps / throttle / interest | `network_should_send` |
| DEM sector / mass battle | `sector_sim` / `mass_battle` package (optional DEM) |

Not simulated (by design): FDTD, full DRFM, JPDA, STAP.

## Examples

```powershell
cd tools\dem
.\run_tools.ps1 test
.\run_tools.ps1 full-sim
.\run_tools.ps1 demo
python rdf_radar_framework_demo.py --preset shorad
python rdf_radar_framework_demo.py --preset shorad --ew
python rdf_radar_framework_demo.py --preset p18
python rdf_radar_framework_demo.py --use-ttile --world GM_Eden --preset shorad
python rdf_radar_mass_battle_sim.py
python rdf_radar_mass_battle_sim.py --use-ttile --world GM_Eden
python rdf_radar_full_sim.py
python rdf_radar_hw_calibrate.py --preset shorad
python rdf_sig_patch_heli_rotors.py
python rdf_ttile_unpack.py
python -m unittest discover -s . -p "test_rdf_*.py" -v
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

目录索引：[tools/README.md](../README.md)。脚本仍扁平放在 `tools/dem/`。

### Pack / 烘焙

- `rdf_dem_io.py`：地形、材质与垂直跨度环境。
- `rdf_dem_pack.py` / `rdf_dem_bake_help.py` / `rdf_dem_preview.py`：打包、烘焙辅助与高程预览（亦见 `docs/DEM.md`）。
- `rdf_dem_pack_bin.py` / `rdf_dem_pack_json.py` / `rdf_dem_pack_surface_json.py` / `rdf_dem_pack_height_json.py`：将 V3 打成 `.dem.data`/全量 JSON/SURF，或从官方 `.ttile` 打 HEIGHT JSON（工坊高度 RAM）。
- `rdf_sig_pack_bin.py` / `rdf_sig_pack_json.py` / `rdf_sig_pack_conf.py`：为游戏 / profile / `.conf` 打包特征表。
- `rdf_sig_patch_heli_rotors.py`：为 UH-1 / Mi-8 签名 conf 填 tip/blades/hub。
- `rdf_ttile_unpack.py`：解包官方 `.ttile` 高程，供离线叠加。

### 仿真库

- `rdf_radar_materials.py`：可标定的频段/海况 σ⁰ 表（`calib/sigma0_x_ss3.json`）。
- `rdf_radar_physics.py`：硬件、波形、俯仰波束、雷达方程、脉压、积累、多普勒、MTI / MTD、CFAR。
- `rdf_radar_channel.py`：频率重调、Swerling RCS、方位 RCS、频谱重叠；`ChannelFidelity` 按需助手（`enable_los_two_ray` / `enable_refraction` / `enable_prf_ambiguity_folds` / `stabilize_for_regression`）覆盖 LOS 双射线、4/3 折射、PRF 距离/多普勒折叠（默认关，对齐游戏 Enable*）。`MultipathModel.enabled` 默认 False。
- `rdf_radar_targets.py`：目标状态与时间参数化轨迹。
- `rdf_radar_sector_sim.py`：静态全扇区 RF/杂波快照。
- `rdf_radar_scan.py`：`scan(t)`、驻留更新、持久 PPI、动目标截获、分波束量测、跳频感知重调；`ScanConfig.fidelity` 应用可选多径 / 无线电地平线软因子 / 点迹折叠。
- `rdf_radar_ew.py`：可插拔噪声/欺骗效果与频率时间表。
- `rdf_radar_diffraction.py`：刀刃绕射辅助。
- `rdf_radar_track.py`：最近邻关联与 α-β 滤波；`RadarObservation` 反演 DTO +
  `test_rdf_radar_track.py` 冻结观测 golden。
- `rdf_radar_fusion.py`：多雷达关联 / 水平交会。
- `rdf_radar_systems.py`：锁定状态机、ESM Friis、RWR/ARM、GO/SO-CFAR、测量噪声、降雨损耗、拖距/角闪烁/间歇假点、Network 带宽策略；`MeasurementModel.from_fidelity` 按需应用距离/多普勒折叠与折射俯仰偏置（WLR 经 `weapon_locate` 跳过多普勒折叠）。

### CLI / 校验

- `rdf_radar_sector_preview.py`：扇区预览图。
- `rdf_radar_framework_demo.py`：干净通道 / EW 验证输出与 CSV。
- `rdf_radar_shellfire_offline.py`：游戏内 ShellFire 回归的离线炮弹 / WLR 镜像。
- `rdf_radar_mass_battle_sim.py`：进入 `mass_battle/` 包的薄 facade（逻辑在 `_impl.py`；主题模块为再导出 — 见 `mass_battle/README.md`）。
- `rdf_radar_hw_calibrate.py`：离线 HW 回灌 → `calib/prf_clutter_*.json`（可拷到 `$profile:RDF/RadarData/HwCalib.json`）。
- `rdf_radar_full_sim.py`：无 DEM 全能力编排（写出 `out/full_sim_report.json`）。
- `rdf_knife_edge_eden_validate.py`：Eden 刀刃绕射校验。
- `run_tools.ps1` / `run_tools.sh`：入口助手（`test` / `full-sim` / `demo`）。

### Golden 测试

- `test_rdf_radar_ballistics.py`：确定性弹道、风、阻力、DEM 交点与 WLR 回归测试。
- `test_rdf_radar_cfar.py`：CA-CFAR golden + 与 Enforce `RDF_RadarCfarGate` CA 对齐。
- `test_rdf_radar_track.py`：关联 / α-β / 极坐标往返 / 真空拟合 golden。
- `test_rdf_radar_ew.py`：噪声耦合 / 烧穿。
- `test_rdf_radar_diffraction.py`：刀刃因子几何。
- `test_rdf_radar_fusion.py`：关联 / 交会。
- `test_rdf_radar_systems.py`：systems 单测 + full_sim 冒烟。
- `test_rdf_radar_mtd.py`：MTD 滤波器组 / 旋翼微多普勒。
- `test_rdf_radar_heli_cpa.py`：直升机 CPA / 机身盲速 vs tip 存活。
- `test_rdf_radar_propagation.py`：双射线 / 折射 / PRF 模糊折叠。
- `test_rdf_radar_hw_calibrate.py`：HW 标定 suggest / 预设 JSON。
- `test_rdf_radar_sig_pack.py`：签名打包 / 旋翼字段。
- `test_rdf_sig_patch_heli_rotors.py`：旋翼 conf 填数补丁。

安装：`pip install -r tools/dem/requirements.txt`（numpy + matplotlib）。
Golden 套件：`cd tools/dem` 后 `.\run_tools.ps1 test` 或
`python -m unittest discover -s . -p "test_rdf_*.py" -v`
（CI：`.github/workflows/python-dem-tests.yml`）。
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
7. CA/GO/SO-CFAR（可选热噪声填空）
8. 跨扫描 α-β 跟踪器
9. 锁定 / RWR / ARM、融合 / IFF、Network 带宽策略（systems 层）

## 能力覆盖矩阵（离线）

| 能力 | 模块 / 入口 |
|------|-------------|
| 雷达方程、MTI、多普勒 | `rdf_radar_physics` + `full_sim` |
| MTD 滤波器组 + 旋翼微多普勒 + 直升机 CPA | `test_rdf_radar_mtd` / `heli_cpa` + `full_sim` |
| 参差 PRF | `RadarHardware.active_prf_hz` + `full_sim` |
| Signature 旋翼打包 | `rdf_sig_pack_conf` / `test_rdf_radar_sig_pack` |
| 旋翼 conf 填数 | `rdf_sig_patch_heli_rotors` / `test_rdf_sig_patch_heli_rotors` |
| HW 杂波 / MTD 回灌 | `rdf_radar_hw_calibrate` → `calib/prf_clutter_*.json` |
| 航迹 miss coast | `TrackerConfig.coast_on_miss` + `test_rdf_radar_track` |
| 大气 / 降雨 | `rdf_radar_systems.two_way_path_loss_db` |
| 测量噪声 | `MeasurementModel` |
| 多径 / 刀刃绕射 | `rdf_radar_channel` / `rdf_radar_diffraction` |
| LOS 双射线 / 4/3 折射 / PRF 折叠 | `ChannelFidelity` + `rdf_radar_channel` + `MeasurementModel` / `ScanConfig` + `test_rdf_radar_propagation` + `full_sim`（`propagation.refraction_horizon` / `ambiguity_fold`） |
| CA / GO / SO-CFAR + 热填空 | `cfar_detections` / `fill_thermal_noise` |
| 方位 RCS / Swerling | `rdf_radar_channel` |
| α-β 跟踪 / WLR 拟合 | `rdf_radar_track` |
| EW 噪声 + 烧穿距离 | `rdf_radar_ew` |
| 欺骗 / 拖距 / 角闪烁 / 间歇 | `ew` + `systems` |
| 频率捷变 | `FrequencyHopSchedule` |
| 融合 / 交会 / IFF | `rdf_radar_fusion` + `resolve_iff` |
| 锁定 / RWR / ARM / ESM Friis | `rdf_radar_systems` |
| Network 上限 / 降频 / 兴趣 | `network_should_send` |
| DEM 扇区 / 大规模战场 | `sector_sim` / `mass_battle` 包（可选 DEM） |

故意不模拟：FDTD、完整 DRFM、JPDA、STAP。

## 示例

```powershell
cd tools\dem
.\run_tools.ps1 test
.\run_tools.ps1 full-sim
.\run_tools.ps1 demo
python rdf_radar_framework_demo.py --preset shorad
python rdf_radar_framework_demo.py --preset shorad --ew
python rdf_radar_framework_demo.py --preset p18
python rdf_radar_framework_demo.py --use-ttile --world GM_Eden --preset shorad
python rdf_radar_mass_battle_sim.py
python rdf_radar_mass_battle_sim.py --use-ttile --world GM_Eden
python rdf_radar_full_sim.py
python rdf_radar_hw_calibrate.py --preset shorad
python rdf_sig_patch_heli_rotors.py
python rdf_ttile_unpack.py
python -m unittest discover -s . -p "test_rdf_*.py" -v
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
