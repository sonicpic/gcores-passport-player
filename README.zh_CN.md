**简体中文** · [English](README.md)

# 机核随身听 · Gcores Passport Player

面向 FoloToy AI Passport 的机核播客随身播放器，提供五个频道、机核 FM、文本时间轴和进度保存。

![播放页 UI 渲染示意](assets/gcores/community/cover.png)

配图为含示意节目文字的原生 UI 渲染，不是实机截图。本项目为独立应用，非机核官方出品。

## 功能

- 浏览机核游戏、生活、兴趣、核市奇谭、银屏系漫游指南五个频道，每个最多保留近期 100 期；本地分页控制 RAM 占用。
- 播放暂停、调节音量；按住按键预览定位目标，松手后定位。
- 播放页直接显示当前和下一条章节文字，超长文字自动滚动。
- 返回当前节目不从头重播；有上限的逐节目记录定时保存进度，换节目或重启后可继续收听。
- 息屏继续播放、定时停止、亮度调整和自动息屏。
- 收听机核 FM，显示当前及下一节目名称与轻量动态条。
- 手机通过临时热点管理页面保存一个或两个 Wi-Fi，自动尝试可用的已保存网络。

## 快速上手

1. 首次开机，手机连接 `Gcores-Passport`，密码见设备屏幕。浏览器打开 `http://192.168.4.1`，填写一个或两个 2.4 GHz Wi-Fi 配置（第二组可留空），点击“保存并连接”。成功后设备显示网络已连接并关闭热点。
2. 首页短按上下选节目，长按上向左、下向右切频道，按 OK 收听。
3. 播放页上下调音量，长按上后退、下快进，松手定位；OK 播放暂停，长按 OK 回首页。
4. 首页长按 OK 进入设置，可选择机核 FM 或回到当前播放。双击 OK 息屏继续播放，任一功能键首次按下只亮屏。

配网重试、换网和完整操作见[使用手册](docs/development/gcores-player.zh_CN.md)。

## 环境与安装

目标为 ESP32-C3 AI Passport，8 MB Flash、无 PSRAM、240 × 320 竖屏。使用
ESP-IDF **5.5.3**、Git、Python 和宿主 C/C++ 编译器；Windows 验证还需要
Git for Windows Bash 与 PowerShell 7。

```bash
git clone https://github.com/sonicpic/gcores-passport-player.git
cd gcores-passport-player
# 先在当前终端激活 ESP-IDF 5.5.3。
idf.py --version
./tools/validate.sh --all
```

Windows 可使用 `tools/develop.ps1` 并显式提供 `-IdfRoot`、`-ToolsRoot`。
详见[环境准备](docs/development/engineering/environment-setup.zh_CN.md)和
[构建验证](docs/development/gcores-validation.zh_CN.md)。

已验证合并镜像为 `build/FoloToy-AI-Passport-full.bin`，写入偏移 `0x0`；合并
安装可能重置网络和进度。仅应用镜像的偏移为 `0x30000`，须先确认分区表兼容，
不能把它写入 `0x0`。详见[固件安装约束](docs/development/gcores-player.zh_CN.md#固件安装)。

## 文档

- [使用与设置](docs/development/gcores-player.zh_CN.md)
- [音频流、支持格式与定位](docs/development/gcores-audio.zh_CN.md)
- [内存预算与资源生命周期](docs/development/gcores-memory.zh_CN.md)
- [验证与设备验收](docs/development/gcores-validation.zh_CN.md)
- [来源和许可](docs/development/gcores-sources.zh_CN.md)
- [完整文档索引](docs/README.zh_CN.md)

## 许可

应用源码采用 [GPL-3.0-only](LICENSE)，基于
[gcores-pocket-player](https://github.com/sonicpic/gcores-pocket-player)。
AI Passport BSP 保留 MIT 声明，字体保留 SIL OFL 声明，内置解码库保留自身许可。
美术素材不代表机核官方背书，详见[第三方清单](docs/development/gcores-sources.zh_CN.md)。
