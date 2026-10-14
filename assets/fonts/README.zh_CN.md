<p align="right"><strong>简体中文</strong> · <a href="README.md">English</a></p>

# 机核 Passport 字体素材

播放器使用比例排版的 Noto Sans CJK SC 黑体，生成 2 bpp 抗锯齿、未压缩的
LVGL 位图。动态节目标题使用真实 Black 特粗体 22 px，正文使用 Bold 粗体 16 px，章节标签
使用常规体 12 px。

## 来源与许可

Noto Sans CJK SC 2.004 版权归 Adobe（2014–2021），按
[SIL Open Font License](OFL.txt) 再分发。三个字源及改名后的衍生字体均保存于
本目录。来源为 [notofonts/noto-cjk](https://github.com/notofonts/noto-cjk/tree/main/Sans/OTF/SimplifiedChinese)。

| 字源 | SHA-256 |
| --- | --- |
| `NotoSansCJKsc-Regular.otf` | `2c76254f6fc379fddfce0a7e84fb5385bb135d3e399294f6eeb6680d0365b74b` |
| `NotoSansCJKsc-Bold.otf` | `b5f0d1a190a7f9b43c310a8850630af12553df32c4c050543f9059732d9b4c0a` |
| `NotoSansCJKsc-Black.otf` | `2267c4a0312d267dff8c0d1609948f7d949a3da8be3e126f5e2690cb9cc883b4` |

## 生成字体与体积

| 符号 | 字重与字号 | 覆盖范围 | 行高 / 基线 | 位图 + large 字形描述符 |
| --- | --- | --- | --- | --- |
| `passport_cjk_bold_22` | 特粗体 22 px | 完整受支持的动态文字字符集 | 32 / 8 px | 2,849,802 B |
| `passport_cjk_16` | 粗体 16 px | 完整受支持的动态文字字符集 | 24 / 6 px | 1,664,216 B |
| `passport_cjk_12` | 常规体 12 px | 完整受支持的动态文字字符集 | 17 / 4 px | 1,098,530 B |
| `passport_sans_12` | 常规体 12 px | 95 个可打印 ASCII 字符 | 14 / 3 px | 2,789 B |
| `passport_sans_14` | 常规体 14 px | 95 个可打印 ASCII 字符 | 17 / 4 px | 3,212 B |
| `passport_channel_11` | 粗体 11 px | ASCII 与全部五个固定频道名称 | 13 / 3 px | 3,832 B |
| `passport_hint_12` | 特粗体 12 px | ASCII 与实际固定按键提示 | 15 / 3 px | 6,792 B |

估算包含每字形 16 字节的 large 描述符；映射和字体元数据另占少量空间。
三个完整字库合计 5,612,548 字节。固件门禁必须按 8 MiB 设备的实际应用分区
检查链接结果，不能根据 C 源码文件大小判断能否放下。字体位于只读 Flash，
不会整体复制到 RAM。

每个动态字号都覆盖全部 **20,976 个基本汉字 U+4E00–9FEF**，以及字源支持的
ASCII、Latin-1、常规与中日韩标点、全角形式。U+9FF0–9FFF、中日韩扩展区和
emoji 不属于支持范围。U+3031/U+3032 的竖排形式会抬高行高，因此排除。
目前每个完整字体实际包含 21,488 个字形。动态节目和章节标题必须选择完整
字体，不能使用固定频道或按键提示子集。

## 复现与集成

在本目录执行 `./generate-font.ps1`。脚本安装由 lockfile 固定的
`lv_font_conv@1.5.3`，重新生成完整和 ASCII 字体，然后执行
`generate-channel-font.py`、`generate-hint-font.py` 与 `verify-font.py`。
频道字符直接取自 `main/passport_catalog.cpp` 的 `kChannels` 表，按键提示取自
`main/main.c`。修改固定文案后重新生成。动态字库不根据当前 RSS 内容缩减。

启用 `CONFIG_LV_FONT_FMT_TXT_LARGE=y`，每个选用的字体仅编译一次，并在应用
声明对应符号。fallback 使用应用自有的可写 `lv_font_t` 描述符，不能修改
生成的只读描述符。保留自然字体度量：22 px 标题默认双行需要 64 px；若 UI
缩小行间距，需要实际检查混合文字是否重叠。12 px 动态字体行高为 17 px。

`python verify-font.py` 检查实际输出字符集、汉字与 ASCII 边界、每个字形的
垂直范围、未压缩 2 bpp 格式、符号名称、固定文案覆盖和完整字库 Flash 预算。
UI 会检查 UTF-8 与所选字体的字形覆盖，不支持的码位显示为可见的 `?`，并
记录 U+ 码位。编译与覆盖检查不能证明位图解码或设备显示效果，仍需检查真实
LVGL 渲染和设备画面。

为兼容源代码，保留 `passport_cjk_bold_22` 符号，但实际字重为 Black；
不使用描边伪粗体或烘焙节目标题。固定底栏为 Black12（行高 15 px），固定
频道名为 Bold11，完整正文行高为 24 px。[字重对照图](font-weight-comparison.png)
将 240 px 样本放大三倍，可运行 `python generate-weight-preview.py` 复现（需 Pillow）。
该字源预览仅为辅助，仍须检查真实 LVGL 和设备。

## 保留的像素字体素材

`Monocraft.ttf`、`Monocraft-OFL.txt`、`passport_pixel_14.c`、
`passport_pixel_20.c` 与 `generate-pixel-font.ps1` 保留作可选设计素材，当前
黑体 UI 不链接它们。Monocraft 作者为 Idrees Hassan，版权年份为 2022，许可
为 SIL OFL 1.1。固定的
[上游字源](https://github.com/IdreesInc/Monocraft/blob/50e2d1c5405e9ca19c6c697d3aad0ded5a44aef9/dist/Monocraft-ttf/Monocraft.ttf)
SHA-256 为 `262e31822e0a75be567b02320bfa092143d871e8a273e529d1c5ab0c04cd215f`。
