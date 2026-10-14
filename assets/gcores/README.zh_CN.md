<p align="right"><strong>简体中文</strong> · <a href="README.md">English</a></p>

# Gcores Passport 视觉素材

本目录保存 240 × 320 竖屏界面使用的参考美术、提取脚本、Flash 常量 RGB565
素材与原生预览。界面实现见 [passport_ui.c](../../main/passport_ui.c)。

## 当前界面

播放页保留所提供原图的封面、角色、暗色纹理、时间轴装饰和底栏美术。
节目标题、频道名、章节、进度、音量与电量由 LVGL 动态绘制。通用背景没有
内嵌底栏控件，首页与设置页各自使用对应的底栏裁片。设置页使用文字和分割线，
不显示列表徽标。

首页强调色跟随当前浏览频道：游戏 `#FF453A`、生活 `#51C878`、兴趣
`#FF922E`、核市 `#4D9EFF`、银屏 `#AD78F5`。选中标签、频道文字、列表行框
和播放状态文字使用这些颜色，其余页面保留各自配色。

标题使用 Noto Sans CJK SC Black 22 px，正文使用 Bold 16 px，章节使用
Regular 12 px。溢出的标题与章节通过原生 LVGL 标签滚动显示；息屏时暂停，
唤醒后恢复，未变化的文字不会重置滚动。缺少章节数据时明确显示
`暂无时间轴`，并隐藏时间戳。字库覆盖、生成与验证见
[字体说明](../fonts/README.zh_CN.md)。

## 美术来源与提取

所提供的图片保留在 [references/](references/)。
[extract-player-background.py](extract-player-background.py) 在
[extracted/](extracted/) 中生成七份 PNG 与 RGB565 背景及裁片。

| 素材 | 来源 | 处理 |
| --- | --- | --- |
| 播放页背景 | [style-07-player.png](references/style-07-player.png) | 清除动态文字与状态区域，缩至 240 × 320，应用共享顶栏 |
| 共享顶栏 | [style-08-home-footer.png](references/style-08-home-footer.png) | 清除动态音量与电量，缩放后保留顶部 240 × 26 |
| 通用背景 | [style-07-player.png](references/style-07-player.png) | 提取暗色纹理与电脑图案，处理亮度和遮罩，应用共享顶栏 |
| 首页底栏 | [style-12-home-footer.png](references/style-12-home-footer.png) | 缩至 240 × 320，裁取底部 240 × 34 |
| 设置底栏 | [style-11-settings-footer.png](references/style-11-settings-footer.png) | 缩至 240 × 320，裁取底部 240 × 34 |
| 喇叭、方向键和确定键图案 | [style-07-player.png](references/style-07-player.png) | 提取后分别缩至 12 × 12、29 × 11、18 × 18 |

七份素材的原始 RGB565 像素数据共 341,414 字节，作为 Flash 常量使用，
不在堆中复制整页背景。BSP 的局部 LCD 帧缓冲另行分配；实际链接后的 Flash
占用取决于保留的素材。

[extract-reference-assets.py](extract-reference-assets.py) 从
[style-10-list-icon.png](references/style-10-list-icon.png) 的
`(125, 128, 1128, 1131)` 区域提取当前列表徽标，用 LANCZOS 缩至 28 × 28。
脚本也保留 [style-02-player.png](references/style-02-player.png) 的封面、
角色和波形原始裁片及缩小版本，坐标和尺寸记录在脚本中；这些独立裁片不构成
当前播放页背景。

安装 Python 3 与 Pillow 后，在仓库根目录运行：

```powershell
python assets/gcores/extract-player-background.py
python assets/gcores/extract-reference-assets.py
```

## 原生预览

[preview-lvgl.png](preview-lvgl.png) 是
[原生渲染器](host-render/) 生成的可复现总览。渲染器使用锁定的 LVGL 组件，
编译实际界面、字库和素材，并应用 BSP 的 30 px 圆角遮罩。
[player-reference-lvgl.png](player-reference-lvgl.png) 与
[player-reference-comparison.png](player-reference-comparison.png) 分别展示
播放页渲染及其与缩放原图的对照。
[社区展示图片](community/README.zh_CN.md) 提供播放、首页和设置的独立画面。

测试中的节目标题、章节和 Wi-Fi 数据均为虚构示例。这些图片来自软件渲染，
不是实体屏幕照片或截图；可用于核对布局与字形，不验证 LCD 传输、颜色顺序、
背光或电气行为。

先安装项目的 ESP-IDF 依赖，使 `managed_components/lvgl__lvgl` 存在。
将原生 MinGW 编译器加入 PATH，并备好 CMake、Ninja、Python 3 与 Pillow，
在仓库根目录运行：

```powershell
cmake -S assets/gcores/host-render -B assets/gcores/host-render/build -G Ninja
cmake --build assets/gcores/host-render/build -j 4
Push-Location assets/gcores/host-render
./build/passport_ui_host.exe
Pop-Location
python assets/gcores/render-lvgl-contact.py
python assets/gcores/compare-player-reference.py
python assets/fonts/verify-font.py
```

渲染器使用 32 KiB LVGL 内存池，检查滚动、息屏与唤醒、定位反馈、完整的最长
UTF-8 字符串、重复创建和删除页面、字形绘制及分配器一致性。桌面分配器测量
不等于 MCU 总 SRAM 占用，也不代表实机验证结果。

## 权利与许可

项目源码使用 [GPL-3.0](../../LICENSE)。Noto Sans CJK SC 字体独立使用
[SIL Open Font License](../fonts/OFL.txt)。机核名称、标志与所提供参考美术的
权利属于各自权利人；收录这些素材不表示官方背书，也不额外授予第三方美术
的使用许可。
