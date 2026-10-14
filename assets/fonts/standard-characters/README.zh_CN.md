<p align="right"><strong>简体中文</strong> · <a href="README.md">English</a></p>

# 规范汉字字符集

`characters.txt` 按等级顺序保存 2013 年《通用规范汉字表》严格 8,105 个不同
字符。三级原始表分别有 3,500 / 3,000 / 1,605 字。拼接时只删除空白，
不做 Unicode 归一化。273 字位于原 U+4E00–9FEF 范围外，其中 196 字位于
补充平面。计数按 Unicode 码位，不按 UTF-16 代码单元。

## 来源与归属

UTF-8 转录来自
[shengdoushi/common-standard-chinese-characters-table](https://github.com/shengdoushi/common-standard-chinese-characters-table/tree/d9b599a9c9cc0dd2d58cad829e285bc780cd4451)，
固定提交 `d9b599a9c9cc0dd2d58cad829e285bc780cd4451`。本地三级文件与该提交逐字节一致。
上游说明来源为教育部、国家语委，并链接
[国务院公布的正式字表](https://www.gov.cn/gzdt/att/att/site1/20130819/tygfhzb.pdf)。
官方扫描 PDF 的说明（PDF 第 5 页、印刷页码 7）确认总字数 8,105 和三级
数量。扫描件无可提取文字，因此不声称已完成与扫描件逐字自动比对；
固定转录文件是生成器实际使用的字符集来源。

官方 PDF SHA-256：`af85c706a53d3b3bbad818bcce7415ac9a2284ea14f79fe7f54ce1248a7bdac9`。

转录仓库没有单独的许可文件。本目录保留政府公布规范的事实性字符枚举，
不再分发 PDF、介绍性文字或排版。不能将转录清单标为 SIL OFL；该许可只
适用于 Noto 字源及衍生字体，见上一级[字体说明](../README.zh_CN.md)。

## 复现与完整性

`manifest.json` 保存固定来源、计数和下表的逐字节校验值。
`non-chinese-codepoints.txt` 单独保留原有 512 个非汉字码位，不混入严格
8,105 汉字的支持范围。

| 文件 | SHA-256 |
| --- | --- |
| `characters.txt` | `2f05a159f4a10c75c8e361648cab265cc7311a31a02ba908ae37227ae2418420` |
| `level-1.txt` | `79a6c710013cc86617d5db65871f59b2d67dee72415d380a2cc7145a51450fe4` |
| `level-2.txt` | `d597a6e99ea7b8c41215081f824c6e8587bf1c9a3f45fb13086a826306789785` |
| `level-3.txt` | `c9fbc83a9f8cd860306b218bf16c6a4cc56d7c4a22a686d403b176a0dbec7931` |
| `non-chinese-codepoints.txt` | `2b0963584106d45c8f79ea9a0b940f2f3f1a2cfd775530de36fc60064eabe763` |

在本目录运行 `node ../generate-standard-fonts.js`，或运行上一级
`generate-font.ps1`。生成器校验来源哈希、等级数量、去重、规范列表，以及
三个字源是否提供全部字形。`python ../verify-font.py` 独立核对真实生成
字符集是否严格等于 8,105 + 512，核对补充平面、反例、字体度量、位图格式
与 Flash 预算。不向规范字表追加任意 UI 用字或历史 RSS 内容。
