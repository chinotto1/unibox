# vendor

Synthesis engines from [Felucca](https://github.com/hugelton/Felucca) (Leo Kuroshita, Hügelton Instruments), hosted by
`src/synths.c`.

| Folder             | Source                                                                                                                                                                      | License                                                                                                            |
| ------------------ | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------ |
| `felucca-engines/` | [Felucca-Engines](https://github.com/hugelton/Felucca-Engines) at commit `7511c0a`: ANALOG, LOFI, TRIO, WHEEL, NOISE, `dsp.c`, the engine interface and the table generator | MIT (`felucca-engines/LICENSE`)                                                                                    |
| `felucca/`         | [Felucca](https://github.com/hugelton/Felucca) `firmware/src/` at commit `a53ecf4`: PHASE, VOICE (`eng_formant.c`) and PHYS                                                 | GPL-3.0-only, except `phys_dsp.c` (from DaisySP) and `phys_symp.c` (from Rings), which are MIT (licenses included) |

The files are copied unchanged, except for two small changes marked with `fomajam:` in the code:

- `felucca-engines/src/eng_analog.c`: oscillator 2 is not computed when MIX is 0. The output is identical; it just
  saves processing time.
- `felucca/eng_phys.c`: the host can set `PHYS_POLY` (the number of voices), so FOMAJAM can give PHYS two voices
  instead of three to save memory.
