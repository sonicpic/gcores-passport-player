**简体中文** · [English](README.md)

# 机核随身听文档

根 [README](../README.zh_CN.md) 提供产品概览与快速上手，当前播放器说明如下：

| 主题 | 文档 |
| --- | --- |
| 配网、按键、设置与安装 | [使用手册](development/gcores-player.zh_CN.md) |
| 格式、流式播放、定位与音频生命周期 | [音频](development/gcores-audio.zh_CN.md) |
| RAM 分配、任务持有者与安全余量 | [内存](development/gcores-memory.zh_CN.md) |
| 构建、宿主测试与设备验收 | [验证](development/gcores-validation.zh_CN.md) |
| 来源与许可 | [来源](development/gcores-sources.zh_CN.md) |
| UI 美术与原生渲染器 | [视觉素材](../assets/gcores/README.zh_CN.md) |
| 字体来源与范围 | [字体](../assets/fonts/README.zh_CN.md) |

## 贡献与硬件参考

以下指南描述可复用的 AI Passport 硬件、BSP 和模板工作流；应用行为与分区以
上面的播放器文档为准。

- [开发索引](development/README.zh_CN.md)
- [环境准备](development/engineering/environment-setup.zh_CN.md)
- [构建与固件打包](development/engineering/build-and-test.zh_CN.md)
- [硬件指南](hardware-design/AI_HARDWARE_DEVELOPMENT_GUIDE.zh_CN.md)
- [文档约定](contribution/doc-conventions.zh_CN.md)
- [提交约定](contribution/commit-and-pr.zh_CN.md)
- [参考应用](reference/README.zh_CN.md)
- [仓库指引](../AGENTS.zh_CN.md)

公开文档描述当前行为与长期工程约束，本地调查日记、机器路径和协作交接放在
忽略的 `dev-notes/`，其中同样不能存放密钥或凭据值。
