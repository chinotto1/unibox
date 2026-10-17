# Licensing

UNIBOX is licensed under GPL-3.0-only (see `LICENSE`). It is built on FOMAJAM, which is built on Felucca, SLOOP and
Dinghy. Anyone who receives a firmware built from this project is also entitled to its source code.

## Who wrote what

| Files                                                                                                                                                                                                       | Author                                                                                                                                                                                                                                                                                                                                                |
| ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `platform/hal/`, `platform/loader/`, `platform/src/`, `platform/crt0.S`, `platform/app.ld`, `tools/fm1pkg_make.py`, `tools/lz4blk.py`, `web/fm1ota.js`, `web/fm1pkg.js`, the FM-1 parts of `tools/build.py` | Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments ([Felucca](https://github.com/hugelton/Felucca)), including the changes made by [SLOOP](https://github.com/isod89/sloop-fm1) (isod89), as carried by [Dinghy](https://github.com/zvenson/dinghy) (zvenson) and [X0X](https://github.com/charlesvestal/fm1-x0x) (Charles Vestal) |
| `platform/fm1.c`                                                                                                                                                                                            | Based on Dinghy's `platform.c` (zvenson), adapted to `fm1.h`                                                                                                                                                                                                                                                                                          |
| `platform/fm1.h`, `platform/common.c`, `platform/web.c`, `web/` (the page and the emulator), `fm1`                                                                                                          | [foma](https://github.com/konsumer/foma)                                                                                                                                                                                                                                                                                                              |
| `src/`, `tools/drums.py`, `tools/roms.py`, `tools/samples.py`, `tools/tables.py`, `test/`, `firmware.json`, and FOMAJAM's changes to the files above                                                        | FOMAJAM's authors, with UNIBOX's later changes                                                                                                                                                                                                                                                                                                        |

The modeled drum machines (`tools/drums.py`, the `src/drums.h` it generates, and `src/drums.c`) are ported from
trommelsynthesizer in [poketrack](https://github.com/konsumer/poketrack) (zlib license), by the same author.

## Synth engines

The synth engines in `vendor/` come from [Felucca](https://github.com/hugelton/Felucca), Copyright (C) 2026 Leo
Kuroshita (@kurogedelic), Hügelton Instruments. `src/synths.c` hosts them. See `vendor/README.md` for the exact
versions.

- **From Felucca-Engines (MIT, `vendor/felucca-engines/LICENSE`):** ANALOG, LOFI, TRIO, WHEEL and NOISE, plus `dsp.c`,
  the engine interface and the table generator.
- **From Felucca (GPL-3.0-only):** PHASE, VOICE and PHYS. PHYS includes `phys_dsp.c` (from DaisySP) and `phys_symp.c`
  (from Rings), both MIT; their licenses are in `vendor/felucca/MIT-DaisySP.txt` and `vendor/felucca/MIT-Rings.txt`.

STRING (`src/eng_string.h`) and SUPERSAW (`src/eng_supersaw.h`) are FOMAJAM's own. SUPERSAW's detune and mix curves
follow Adam Szabo's published measurements of the Roland JP-8000 ("How to Emulate the Super Saw", 2010).

UNIBOX adds four engines:

- **SH-101** (`src/eng_sh101.h`) and the **wavetable** synth (`src/eng_wt.h`, `src/wt_data.h`) are written for this
  project. They call Felucca-Engines' `dsp.c` helpers (MIT).
- **TB-303** (`src/eng_303.h`, `src/dsp/bass303.c`, `src/dsp/bass303.h`, `src/dsp/fastmath.h`, `src/dsp/x0x_param.h`)
  is the X0X port of Open303 (Robin Schmidt, MIT) with the Devilfish ranges and the Soft / RAT drive of schwung-303
  (GPL-3.0; the RAT model is from davemollen/dm-Rat, GPL-3.0). Copyright follows those files' headers.
- **Juno** (`src/eng_juno.h`) wraps the FM-106 DCO, ladder filter and chorus (`src/fm106/`, GPL-3.0-only). FM-106
  ports that engine from [ultramaster_kr106](https://github.com/kayrockscreenprinting/ultramaster_kr106).

The 808 and 909 here are FOMAJAM's modeled voices, not the X0X sample engines. LinnDrum and DMX are the ROM recordings
FOMAJAM already ships; CR-78 is the modeled voice.

The reverb in `src/fx.c` uses the delay lengths and structure of Jezar's Freeverb, which is public domain.

## Third-party material

| What                                                                                      | License     | Where                                           |
| ----------------------------------------------------------------------------------------- | ----------- | ----------------------------------------------- |
| Terminus font (ter-u16n), rendered into `platform/gen/felucca_font.h`                     | SIL OFL 1.1 | `platform/gen/Terminus-LICENSE.txt`             |
| `platform/sdk/uboot.boot`, `cfg_tool.bin`, `eq_cfg_hw.bin` from JieLi's AC79NN_SDK_V1.2.1 | Apache-2.0  | <https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK> |
| JieLi's toolchain, downloaded into the Docker image                                       | JieLi's     | <https://pkgman.jieliapp.com/s/linux-toolchain> |

## The ROM drum machine recordings

**These recordings are not ours.** `tools/roms.py` downloads recordings of the TR-707, TR-606, DMX, LinnDrum, TR-505,
DR-55, SK-1, KPR-77 and Rhythm Ace's own sounds from
[tidal-drum-machines](https://github.com/geikha/tidal-drum-machines), which does not state a license for them. The
build includes them in the emulator and in the `.fwsc` firmware, so they also end up on GitHub Pages and in releases.
Make sure you are comfortable distributing them. To build without them, remove them from `MACHINES` in
`tools/roms.py`, or delete `roms/` and build offline.

## Trademarks

"M-VAVE" and "FM-1" belong to their owners; "Felucca" and "Hügelton Instruments" belong to Hügelton Instruments. This
project is not made or endorsed by them.
