**English** · [简体中文](gcores-sources.zh_CN.md)

# Sources and licensing

| Material | Source | License / location |
| --- | --- | --- |
| Podcast parsing, cache, timelines and player logic | [sonicpic/gcores-pocket-player](https://github.com/sonicpic/gcores-pocket-player), commit `6d7b9bd8b0303c72b44731e9101677e50a085b7d` | GPL-3.0-only; `main/gcores`, `tests/gcores`, `licenses/gcores-GPL-3.0.txt` |
| Board support and reference guides | [FoloToy/ai-passport](https://gitee.com/FoloToy/ai-passport), baseline `a06235e` | MIT notices and `licenses/ai-passport-MIT.txt` |
| Codec and stream parser 2.6.2 | [Espressif esp-adf-libs](https://github.com/espressif/esp-adf-libs), commit `a1c9747e62e9f78444570ce6dfb0c4143de98393` | Vendor license in `components/esp_audio_codec/LICENSE`; C3 binaries and headers |
| Noto Sans CJK SC Regular/Bold/Black 2.004 | [notofonts/noto-cjk](https://github.com/notofonts/noto-cjk) | SIL OFL 1.1; `assets/fonts/OFL.txt` |
| UI artwork and supplied reference images | Project-supplied visual assets | Source/crop records in [visual assets](../../assets/gcores/README.md); original rights retained |
| Managed dependencies | `dependencies.lock` | Each dependency's license |
| Development SDK | ESP-IDF 5.5.3 | SDK and bundled component licenses |

Application source uses GPL-3.0-only under the root `LICENSE`. MIT, OFL and vendor
notices remain applicable to their respective components. Precompiled codec
binaries are not represented as GPL source or portable open-source decoders.
The source license does not grant rights to third-party trademarks, podcast
audio or artwork. This project does not imply Gcores endorsement.

The current repository is [sonicpic/gcores-passport-player](https://github.com/sonicpic/gcores-passport-player).
Public feeds are accessed at runtime; no podcast audio or personal credentials
are bundled into the application.
