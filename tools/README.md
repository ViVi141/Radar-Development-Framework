> **Languages / 语言**: [English](#english) · [中文](#中文)

# RDF tools

Offline helpers for DEM packing and radar simulation prototypes.
Scripts stay flat under [`dem/`](dem/) (shared imports + CI). This file is the
**catalog**; physics contracts live in [`dem/RADAR_FRAMEWORK.md`](dem/RADAR_FRAMEWORK.md).
Bake / SURF publish: [`docs/DEM.md`](../docs/DEM.md).

Install: `pip install -r tools/dem/requirements.txt`

Quick entry:

```powershell
cd tools\dem
.\run_tools.ps1 help
.\run_tools.ps1 test        # unittest discover test_rdf_*.py (MTD/CPA included)
.\run_tools.ps1 full-sim
.\run_tools.ps1 demo
.\run_tools.ps1 demo -Ew
```

```bash
cd tools/dem
chmod +x run_tools.sh   # once
./run_tools.sh test
./run_tools.sh full-sim
```

CI: `.github/workflows/python-dem-tests.yml` runs unittest discover + `full_sim` coverage checks (`detection.mtd_bank`, rotor CPA, PRF stagger, `propagation.refraction_horizon` / `ambiguity_fold`).

Offline HW bake-back: `python rdf_radar_hw_calibrate.py --preset shorad` → `calib/prf_clutter_*.json`.
Copy to `$profile:RDF/RadarData/HwCalib.json` (PulseDoppler loads it) or rely on `m_DeriveMtdLeakageFromSigmaVr`.  
Heli rotor conf fill: `python rdf_sig_patch_heli_rotors.py` (UH-1 / Mi-8 airframes; skips VehParts).  
Mass-battle sim: `rdf_radar_mass_battle_sim.py` (facade) + `mass_battle/` package
(`_impl.py` holds the logic; thematic modules are re-exports — see
`mass_battle/README.md`).  
In-game AutoTest without Debugger: see [docs/AUTOTEST_CI_LIMITS.md](../docs/AUTOTEST_CI_LIMITS.md).

Generated files go to `tools/dem/out/` (gitignored except `.gitkeep`). Do not commit.

---

## English

### Layout

| Path | Role |
|------|------|
| `dem/` | All Python scripts (pack + sim + tests) |
| `dem/calib/` | σ⁰ (`sigma0_*.json`) and PRF/clutter HW bake-back (`prf_clutter_*.json`) |
| `dem/mass_battle/` | Multi-radar DEM battle package (facade modules + `_impl.py`) |
| `dem/out/` | Generated png / csv / json / npz |
| `dem/run_tools.ps1` | Windows helper (`test` / `full-sim` / `demo`) |
| `dem/RADAR_FRAMEWORK.md` | Offline radar framework contracts |

### Pack / Bake

| Script | Purpose |
|--------|---------|
| `rdf_dem_io.py` | Terrain / material / vertical-span I/O |
| `rdf_dem_bake_help.py` | Workbench bake flag helper |
| `rdf_dem_preview.py` | Heightfield preview plots |
| `rdf_dem_pack.py` | Pack V3 bake → offline `TrainData` npz |
| `rdf_dem_pack_bin.py` | Pack → `.dem.data` (legacy / offline) |
| `rdf_dem_pack_json.py` | Pack → full DEM JSON |
| `rdf_dem_pack_surface_json.py` | Pack → SURF JSON (workshop path) |
| `rdf_sig_pack_bin.py` | Signature table → binary |
| `rdf_sig_pack_json.py` | Signature table → JSON |
| `rdf_sig_pack_conf.py` | Signature table → `.conf` (workshop) |
| `rdf_sig_patch_heli_rotors.py` | Fill UH-1 / Mi-8 rotor tip/blades/hub in signature conf |
| `rdf_ttile_unpack.py` | Unpack official `.ttile` height for overlays |

### Simulation libraries

| Script | Purpose |
|--------|---------|
| `rdf_radar_physics.py` | Hardware, radar equation, MTI / MTD bank, CA-CFAR |
| `rdf_radar_materials.py` | Band / sea-state σ⁰ tables |
| `rdf_radar_channel.py` | Retune, Swerling, aspect RCS; `ChannelFidelity` opt-in (LOS two-ray, 4/3 refraction, PRF folds; default off) |
| `rdf_radar_targets.py` | Trajectories, ballistics integrate |
| `rdf_radar_scan.py` | Time-stepped scan / dwell; `ScanConfig.fidelity` opt-in multipath / horizon / plot folds |
| `rdf_radar_sector_sim.py` | Static sector RF / clutter snapshot |
| `rdf_radar_track.py` | NN association, α-β, WLR vacuum fit |
| `rdf_radar_ew.py` | Noise / deception / hop schedule |
| `rdf_radar_diffraction.py` | Knife-edge diffraction helpers |
| `rdf_radar_fusion.py` | Multi-radar associate / cross-fix |
| `rdf_radar_systems.py` | Lock, ESM/RWR/ARM, GO/SO-CFAR, Network policy; `MeasurementModel` + optional `ChannelFidelity` folds |

### CLI demos / validation

| Script | Purpose |
|--------|---------|
| `rdf_radar_full_sim.py` | No-DEM capability suite → `out/full_sim_report.json` |
| `rdf_radar_framework_demo.py` | Clean / EW PPI demo + CSV |
| `rdf_radar_sector_preview.py` | Sector preview plots |
| `rdf_radar_shellfire_offline.py` | ShellFire / WLR offline mirror |
| `rdf_radar_mass_battle_sim.py` | Multi-radar DEM battle facade → `mass_battle/` |
| `rdf_radar_hw_calibrate.py` | Offline HW bake-back → `calib/prf_clutter_*.json` |
| `rdf_knife_edge_eden_validate.py` | Eden knife-edge validation helper |

### Golden tests

| Script | Purpose |
|--------|---------|
| `test_rdf_radar_ballistics.py` | Ballistics / wind / drag / DEM hit |
| `test_rdf_radar_cfar.py` | CA-CFAR golden + Enforce CA parity |
| `test_rdf_radar_track.py` | Association / α-β / vacuum fit |
| `test_rdf_radar_ew.py` | Noise jam coupling / burn-through |
| `test_rdf_radar_diffraction.py` | Knife-edge factors |
| `test_rdf_radar_fusion.py` | Associate / cross-fix |
| `test_rdf_radar_systems.py` | Systems + full_sim smoke |
| `test_rdf_radar_mtd.py` | MTD bank / rotor micro-Doppler |
| `test_rdf_radar_heli_cpa.py` | Helicopter CPA / body-null vs tip survival |
| `test_rdf_radar_propagation.py` | Two-ray / refraction / PRF ambiguity folds |
| `test_rdf_radar_hw_calibrate.py` | HW calib suggest / preset JSON |
| `test_rdf_radar_sig_pack.py` | Signature pack / rotor fields |
| `test_rdf_sig_patch_heli_rotors.py` | Heli rotor conf patch |

```powershell
cd tools\dem
python -m unittest discover -s . -p "test_rdf_*.py" -v
```

CI: `.github/workflows/python-dem-tests.yml` (triggers on `tools/dem/**`).

---

## 中文

离线 DEM 打包与雷达仿真原型工具。脚本仍扁平放在 [`dem/`](dem/)（共享 import + CI）。
本文件是**目录索引**；物理契约见 [`dem/RADAR_FRAMEWORK.md`](dem/RADAR_FRAMEWORK.md)；
烘焙 / SURF 发布见 [`docs/DEM.md`](../docs/DEM.md)。

安装：`pip install -r tools/dem/requirements.txt`

Windows 快捷入口：

```powershell
cd tools\dem
.\run_tools.ps1 help
.\run_tools.ps1 test
.\run_tools.ps1 full-sim
.\run_tools.ps1 demo
.\run_tools.ps1 demo -Ew
```

生成物在 `tools/dem/out/`（除 `.gitkeep` 外 gitignore）——请勿提交。

### 布局

| 路径 | 作用 |
|------|------|
| `dem/` | 全部 Python（打包 + 仿真 + 测试） |
| `dem/calib/` | σ⁰（`sigma0_*.json`）与 PRF/杂波 HW 回灌（`prf_clutter_*.json`） |
| `dem/mass_battle/` | 多雷达 DEM 战场包（facade 模块 + `_impl.py`） |
| `dem/out/` | 生成的 png / csv / json / npz |
| `dem/run_tools.ps1` | Windows 助手（`test` / `full-sim` / `demo`） |
| `dem/RADAR_FRAMEWORK.md` | 离线雷达框架契约 |

### Pack / 烘焙

| 脚本 | 用途 |
|------|------|
| `rdf_dem_io.py` | 地形 / 材质 / 垂直跨度 I/O |
| `rdf_dem_bake_help.py` | Workbench 烘焙 flag 辅助 |
| `rdf_dem_preview.py` | 高程预览图 |
| `rdf_dem_pack.py` | V3 烘焙 → 离线 `TrainData` npz |
| `rdf_dem_pack_bin.py` | → `.dem.data`（遗留 / 离线） |
| `rdf_dem_pack_json.py` | → 全量 DEM JSON |
| `rdf_dem_pack_surface_json.py` | → SURF JSON（工坊路径） |
| `rdf_sig_pack_bin.py` | 特征表 → 二进制 |
| `rdf_sig_pack_json.py` | 特征表 → JSON |
| `rdf_sig_pack_conf.py` | 特征表 → `.conf`（工坊） |
| `rdf_sig_patch_heli_rotors.py` | 为 UH-1 / Mi-8 签名 conf 填 tip/blades/hub |
| `rdf_ttile_unpack.py` | 解包官方 `.ttile` 高程叠加 |

### 仿真库

| 脚本 | 用途 |
|------|------|
| `rdf_radar_physics.py` | 硬件、雷达方程、MTI / MTD、CA-CFAR |
| `rdf_radar_materials.py` | 频段 / 海况 σ⁰ 表 |
| `rdf_radar_channel.py` | 重调、Swerling、方位 RCS；`ChannelFidelity` 按需开启（LOS 双射线、4/3 折射、PRF 折叠；默认关） |
| `rdf_radar_targets.py` | 轨迹、弹道积分 |
| `rdf_radar_scan.py` | 时间步进扫描 / 驻留；`ScanConfig.fidelity` 按需多径 / 地平线 / 点迹折叠 |
| `rdf_radar_sector_sim.py` | 静态扇区 RF / 杂波快照 |
| `rdf_radar_track.py` | 最近邻关联、α-β、WLR 真空拟合 |
| `rdf_radar_ew.py` | 噪声 / 欺骗 / 跳频表 |
| `rdf_radar_diffraction.py` | 刀刃绕射辅助 |
| `rdf_radar_fusion.py` | 多雷达关联 / 交会 |
| `rdf_radar_systems.py` | 锁定、ESM/RWR/ARM、GO/SO-CFAR、Network 策略；`MeasurementModel` + 可选 `ChannelFidelity` 折叠 |

### CLI / 校验

| 脚本 | 用途 |
|------|------|
| `rdf_radar_full_sim.py` | 无 DEM 全能力套件 → `out/full_sim_report.json` |
| `rdf_radar_framework_demo.py` | 干净 / EW PPI Demo + CSV |
| `rdf_radar_sector_preview.py` | 扇区预览图 |
| `rdf_radar_shellfire_offline.py` | ShellFire / WLR 离线镜像 |
| `rdf_radar_mass_battle_sim.py` | 多雷达 DEM 战场 facade → `mass_battle/` |
| `rdf_radar_hw_calibrate.py` | 离线 HW 回灌 → `calib/prf_clutter_*.json` |
| `rdf_knife_edge_eden_validate.py` | Eden 刀刃绕射校验 |

### Golden 测试

| 脚本 | 用途 |
|------|------|
| `test_rdf_radar_ballistics.py` | 弹道 / 风 / 阻 / DEM 交点 |
| `test_rdf_radar_cfar.py` | CA-CFAR golden + Enforce CA 对齐 |
| `test_rdf_radar_track.py` | 关联 / α-β / 真空拟合 |
| `test_rdf_radar_ew.py` | 噪声耦合 / 烧穿 |
| `test_rdf_radar_diffraction.py` | 刀刃因子 |
| `test_rdf_radar_fusion.py` | 关联 / 交会 |
| `test_rdf_radar_systems.py` | systems + full_sim 冒烟 |
| `test_rdf_radar_mtd.py` | MTD 滤波器组 / 旋翼微多普勒 |
| `test_rdf_radar_heli_cpa.py` | 直升机 CPA / 机身盲速 vs tip 存活 |
| `test_rdf_radar_propagation.py` | 双射线 / 折射 / PRF 模糊折叠 |
| `test_rdf_radar_hw_calibrate.py` | HW 标定 suggest / 预设 JSON |
| `test_rdf_radar_sig_pack.py` | 签名打包 / 旋翼字段 |
| `test_rdf_sig_patch_heli_rotors.py` | 旋翼 conf 填数补丁 |

```powershell
cd tools\dem
python -m unittest discover -s . -p "test_rdf_*.py" -v
```

CI：`.github/workflows/python-dem-tests.yml`（`tools/dem/**` 变更触发）。
