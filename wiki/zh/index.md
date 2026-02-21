# 雷达开发框架

欢迎使用 **雷达开发框架（RDF）** 文档。

RDF 是为 **Arma Reforger** 设计的模块化双传感器框架：

- **激光雷达（LiDAR）** — 激光点云扫描、可视化、演示
- **电磁雷达** — 完整物理模型（FSPL、RCS、多普勒、信噪比）、PPI HUD

两个系统共享一个公共的采样核心和扩展接口。

---

## 文档索引

| 项目 | 链接 |
|------|------|
| **入门指南** | [1. 项目概览](1-getting-started/1.1-project-overview.md) \| [快速入门](1-getting-started/1.2-quick-start.md) |
| **激光雷达模块** | [架构](2-lidar-module/2.1-architecture-overview.md) \| [核心](2-lidar-module/2.2-core-components.md) \| [采样策略](2-lidar-module/2.3-sample-strategies.md) |
| **雷达模块** | [架构](3-radar-module/3.1-architecture-overview.md) \| [物理模型](3-radar-module/3.2-physics-pipeline.md) \| [HUD显示](3-radar-module/3.5-hud-display.md) |
| **开发者指南** | [扩展点](5-developer-guide/5.3-extension-points.md) \| [Enfusion约束](5-developer-guide/5.4-enfusion-constraints.md) |
| **API参考** | [激光雷达API](6-api-reference/6.1-lidar-api.md) \| [雷达API](6-api-reference/6.2-radar-api.md) |
| **附录** | [更新日志](7-appendix/7.1-changelog.md) \| [许可证与联系方式](7-appendix/7.3-license-contact.md) |

---

**代码仓库**：[Radar-Development-Framework](https://github.com/ViVi141/Radar-Development-Framework)  
**许可证**：Apache-2.0