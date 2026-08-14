> **Languages / 语言**: [English](#english) · [中文](#中文)

# Contributing

Thanks for contributing to Radar Development Framework.

## English

### Report issues

Open an issue with:

- Enforce / Workbench version and world (map)
- Reproduction steps
- Expected vs actual behaviour

For radar behaviour, attach `GetStatusShort()` and the AutoTest report under
`$profile:RDF/RadarTests/`.

### Pull requests

- Branch from `main`; one logical change per PR.
- Commit messages: imperative, English (match the existing git log).
- Code style: Google C++ Style — see [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md).
- Enforce constraints (see DEVELOPMENT.md § Enforce 关键约束): no ternary
  `? :`, no `float(x)` casts, no `string.ToLower()`, `out` is a keyword,
  keep `.c` files pure ASCII.

### Tests (required)

- **Offline golden** (drift guard): `cd tools/dem && python -m unittest discover -s . -p "test_rdf_*.py" -v` must stay green. Any physics change must keep the
  in-game `.c` and the Python mirror **identical** — the offline golden is the
  source of truth that catches drift.
- **In-game**: run the relevant AutoTest in Workbench Play
  (`RDF_RadarAutoTestSuite.StartAll()` + the standalone regressions in
  [README.md](README.md)).
- **Test rule**: no detection assists — targets must be found by the discovery
  sweep and detected from their own returns (`discovered_unaided`). No emitter
  flags, no RCS edits, no registry pre-seeding for objects under test.

### Docs

Bilingual — see [docs/I18N.md](docs/I18N.md).

Contact: 747384120@qq.com

---

## 中文

感谢为 Radar Development Framework 贡献。

### 报告问题

开 issue 时请附：

- Enforce / Workbench 版本与地图
- 复现步骤
- 预期 vs 实际行为

雷达行为问题请附 `GetStatusShort()` 与 `$profile:RDF/RadarTests/` 下的 AutoTest 报告。

### 提交 PR

- 从 `main` 分支；一个 PR 一个逻辑改动。
- 提交信息：祈使句、英文（对齐现有 git log）。
- 代码风格：Google C++ Style——见 [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md)。
- Enforce 约束（见 DEVELOPMENT.md § Enforce 关键约束）：无三目 `? :`、无
  `float(x)` 转换、无 `string.ToLower()`、`out` 是关键字、`.c` 保持纯 ASCII。

### 测试（必须）

- **离线金标**（防漂移）：`cd tools/dem && python -m unittest discover -s . -p "test_rdf_*.py" -v` 必须全绿。任何物理改动须保持游戏侧 `.c` 与 Python 镜像**一致**——离线金标是防漂移的权威。
- **游戏内**：在 Workbench Play 跑对应 AutoTest（`RDF_RadarAutoTestSuite.StartAll()` + [README.md](README.md) 里的独立回归）。
- **测试铁律**：不给被测物体任何探测助攻——目标须由发现扫掠找到、靠自身回波被检测（`discovered_unaided`）。不加辐射源标记、不改 RCS、不预填散射体表。

### 文档

双语——见 [docs/I18N.md](docs/I18N.md)。

联系：747384120@qq.com
