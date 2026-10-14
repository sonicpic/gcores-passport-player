**简体中文** · [English](gcores-validation.md)

# 构建与验收

## 可复现门禁

激活 ESP-IDF 5.5.3，并提供宿主 C/C++ 编译器，在仓库根目录执行：

```bash
./tools/validate.sh --static
./tools/validate.sh --firmware
./tools/validate.sh --all
```

静态门禁检查文档、语言、链接、仓库安全和宿主回归，覆盖目录、缓存、RSS、
时间轴、导航、进度保存、Wi-Fi 回滚、音频、HTTP/Range 和 BSP 故障。
固件门禁由 `sdkconfig.defaults` 生成全新隔离配置，验证分区并归档匹配的合并
镜像、应用、ELF 和 MAP。

输出为 `build/FoloToy-AI-Passport-full.bin`，内容寻址调试包位于
`build/firmware/<full-image-sha256>/`，使用前验证：

```bash
python tools/archive_firmware.py verify build/firmware/<full-image-sha256>
```

生成二进制和原始设备日志不进入 Git。失败构建不能验证 `build/` 中遗留旧文件。
仅用设备所装镜像的匹配 ELF 解析崩溃，见[构建指南](engineering/build-and-test.zh_CN.md)。

## 原生 UI 检查

原生渲染器用真实 UI、字体和 LVGL，在 32 KiB 池中覆盖主页面与子页面、频道
配色、音量电量、完整长文字、同文字更新保持滚动、息屏唤醒、提示时长、反复
销毁页面和内存完整性。fixture 为合成显示数据。宿主渲染不能验证 LCD 传输、
实际颜色、按键时序或声音，见[渲染环境](../../assets/gcores/README.zh_CN.md)。

## 设备验收

1. 识别设备、验证产物与写入偏移；保留 NVS/PHY 需要分区兼容的仅应用更新。
2. 检查启动、当前 UI、最长标题、三键、切频道、设置帮助及返回当前节目不重播。
3. 测试首次配网、错误凭据、重试、换网、退出和热点自动关闭；确认 HTTPS 前时间同步。
4. 测试播放暂停、前后定位、不支持格式、断网、进度保存与重启和反复换节目。
5. 验证息屏播放、唤醒动作消费、定时与深睡；观察堆、最大块、栈水位与长时间运行。
6. 若声称省电，应在电池上按相同音量、媒体、网络条件测量；USB 日志运行不够。

分别报告 Build、Host tests、Device tests 和 Unverified。编译成功或烧录应答
不代表实体验收；全部格式、最坏网络、长期运行和电流须有具体证据。
本地测试日记属于忽略的工程笔记。
