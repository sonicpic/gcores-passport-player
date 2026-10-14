**简体中文** · [English](gcores-sources.md)

# 来源与许可

| 内容 | 来源 | 许可 / 位置 |
| --- | --- | --- |
| 播客解析、缓存、时间轴和播放逻辑 | [sonicpic/gcores-pocket-player](https://github.com/sonicpic/gcores-pocket-player)，提交 `6d7b9bd8b0303c72b44731e9101677e50a085b7d` | GPL-3.0-only；`main/gcores`、`tests/gcores`、`licenses/gcores-GPL-3.0.txt` |
| BSP 与参考指南 | [FoloToy/ai-passport](https://gitee.com/FoloToy/ai-passport)，基线 `a06235e` | MIT 声明和 `licenses/ai-passport-MIT.txt` |
| 解码与流解析 2.6.2 | [Espressif esp-adf-libs](https://github.com/espressif/esp-adf-libs)，提交 `a1c9747e62e9f78444570ce6dfb0c4143de98393` | `components/esp_audio_codec/LICENSE`；C3 库与头文件 |
| Noto Sans CJK SC Regular/Bold/Black 2.004 | [notofonts/noto-cjk](https://github.com/notofonts/noto-cjk) | SIL OFL 1.1；`assets/fonts/OFL.txt` |
| UI 与参考图 | 项目提供的视觉素材 | 来源与裁切见[视觉素材](../../assets/gcores/README.zh_CN.md)，原有权利保留 |
| 托管依赖 | `dependencies.lock` | 各依赖许可 |
| 开发 SDK | ESP-IDF 5.5.3 | SDK 和所带组件许可 |

应用源码按根 `LICENSE` 使用 GPL-3.0-only；各组件分别保留 MIT、OFL 和厂商
声明。预编译解码库不被声称为 GPL 源码或可移植开源解码器。源码许可不授予第三方
商标、播客音频或美术权利，本项目不代表机核官方背书。

当前仓库为 [sonicpic/gcores-passport-player](https://github.com/sonicpic/gcores-passport-player)。
公开 RSS 在运行时访问，固件不包含播客音频或个人凭据。
