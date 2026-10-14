<p align="right"><a href="README.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# Standard Chinese character inventory

`characters.txt` contains exactly the 8,105 distinct characters of the 2013
Table of General Standard Chinese Characters, in level order. The three source
files contain 3,500 / 3,000 / 1,605 characters. Whitespace is removed when
constructing the canonical list; Unicode normalization is deliberately avoided.
There are 273 entries outside U+4E00–9FEF, including 196 supplementary-plane
characters. Count Unicode code points, not UTF-16 code units.

## Source and provenance

The UTF-8 transcription comes from
[shengdoushi/common-standard-chinese-characters-table](https://github.com/shengdoushi/common-standard-chinese-characters-table/tree/d9b599a9c9cc0dd2d58cad829e285bc780cd4451),
commit `d9b599a9c9cc0dd2d58cad829e285bc780cd4451`. The local level files match
that commit byte for byte. The upstream README attributes the table to the
Ministry of Education and State Language Commission and links the
[official State Council publication](https://www.gov.cn/gzdt/att/att/site1/20130819/tygfhzb.pdf).
The official scanned PDF's explanation (PDF page 5, printed page 7) confirms
8,105 total and the three level counts. It contains no extractable text;
this does not constitute an automated character-by-character audit against the
scan. The pinned transcription is the executable inventory source.

Official PDF SHA-256: `af85c706a53d3b3bbad818bcce7415ac9a2284ea14f79fe7f54ce1248a7bdac9`.

The transcription repository does not supply a separate license file. These
files retain the factual character enumeration of the published government
standard, without redistributing its PDF, introductory prose or typography.
Do not attribute the transcription to SIL OFL: that license applies to the
Noto source fonts and generated font derivatives, documented in the parent
[font README](../README.md).

## Reproduction and integrity

`manifest.json` records fixed provenance, counts and the following byte hashes.
`non-chinese-codepoints.txt` preserves the prior 512 supported non-Chinese
code points. It is separate from the exact 8,105-Chinese-character contract.

| File | SHA-256 |
| --- | --- |
| `characters.txt` | `2f05a159f4a10c75c8e361648cab265cc7311a31a02ba908ae37227ae2418420` |
| `level-1.txt` | `79a6c710013cc86617d5db65871f59b2d67dee72415d380a2cc7145a51450fe4` |
| `level-2.txt` | `d597a6e99ea7b8c41215081f824c6e8587bf1c9a3f45fb13086a826306789785` |
| `level-3.txt` | `c9fbc83a9f8cd860306b218bf16c6a4cc56d7c4a22a686d403b176a0dbec7931` |
| `non-chinese-codepoints.txt` | `2b0963584106d45c8f79ea9a0b940f2f3f1a2cfd775530de36fc60064eabe763` |

Run `node ../generate-standard-fonts.js` from this directory, or the parent
`generate-font.ps1`. The generator checks every source hash, level count,
uniqueness, canonical list and every requested glyph in all three source faces.
`python ../verify-font.py` independently checks generated inventories for exact
8,105 + 512 membership, supplementary characters, unsupported probes, metrics,
bitmap format and Flash budget. It does not add arbitrary UI or historical RSS
characters to the standard set.
