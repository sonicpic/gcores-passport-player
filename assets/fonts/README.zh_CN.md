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

动态中文严格支持 **2013 年《通用规范汉字表》的 8,105 字**，同时保留原有
512 个 ASCII、Latin-1、标点和全角字形。这是固定产品字符集，不按当前 RSS
节目内容缩减。规范字表、三级原始数据、固定来源与校验值见
[standard-characters](standard-characters/README.zh_CN.md)。

| 符号 | 字重与字号 | 实际字形 | 行高 / 基线 | 位图 + large 描述符 |
| --- | --- | --- | --- | --- |
| `passport_cjk_bold_22` | Black 特粗体 22 px | 8,617 | 32 / 8 px | 1,121,087 B |
| `passport_cjk_16` | Bold 粗体 16 px | 8,617 | 24 / 6 px | 655,798 B |
| `passport_cjk_12` | 常规体 12 px | 8,617 | 17 / 4 px | 433,176 B |
| `passport_sans_12` | 常规体 12 px | 95 个 ASCII | 14 / 3 px | 2,789 B |
| `passport_sans_14` | 常规体 14 px | 95 个 ASCII | 17 / 4 px | 3,212 B |
| `passport_channel_11` | 粗体 11 px | 固定频道文案 | 13 / 3 px | 3,832 B |
| `passport_hint_12` | 特粗体 12 px | 固定按键提示 | 15 / 3 px | 6,792 B |

三个动态字库的位图加每字形 16 字节描述符共 2,210,061 字节，相比相同字重和
字号下原有 20,976 基本汉字字库节省 3,402,487 字节。映射与元数据另占少量
空间，仍需固件门禁检查实际应用分区。字体位于只读 Flash，不整体复制到 RAM。

规范表含 273 个旧基本汉字范围外的字符，其中 196 个位于补充平面。
三个字源和生成结果均已核对全部 8,105 字。表外繁体、罕见字、扩展汉字以及
emoji 不属于支持范围；UI 用可见 `?` 替换，不静默丢字。U+9F98 和 emoji
作为不支持的反例，U+2C27C 则因在规范表内受到支持。节目标题不烘焙成图片。

## 复现与集成

在本目录执行 `./generate-font.ps1`。脚本安装由 lockfile 固定的
`lv_font_conv@1.5.3`，通过 `generate-standard-fonts.js` 生成动态字库并重新生成 ASCII 字体，然后执行
`generate-channel-font.py`、`generate-hint-font.py` 与 `verify-font.py`。
频道字符直接取自 `main/passport_catalog.cpp` 的 `kChannels` 表，按键提示取自
`main/main.c`。修改固定文案后重新生成。动态字库不根据当前 RSS 内容缩减。Node 包装脚本在进程内读取校验过的
8,105 字清单，避免 Windows 命令行长度限制；转换器版本不是 1.5.3、数据
校验值或数量改变、字源缺少任一规范字时，生成立即失败。

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
