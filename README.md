# FOMAJAM

A groovebox firmware for the M-VAVE FM-1, made with [foma](https://github.com/konsumer/foma).

- 9 tracks, each playing its own machine:
  - **drum machines:** modeled 808, 909, SDS-V and CR-78; the 707, 606, DMX, LinnDrum, 505, DR-55, SK-1, KPR-77 and
    Rhythm Ace from recordings of their ROMs; or your own samples
  - **synths:** [Felucca](https://github.com/hugelton/Felucca)'s engines (ANALOG, LOFI, TRIO, WHEEL, NOISE, PHASE,
    VOICE, PHYS), plus STRING (Karplus-Strong) and SUPERSAW (JP-8000 style), all with presets
  - **MIDI out**, to sequence other gear
- 8 patterns of 16 steps, with accents, slides and ratchets
- effects: delay, reverb and chorus sends, kick ducking, a master filter and 11 kinds of distortion
- MIDI clock, and color themes

Try it in your browser, and install it on the FM-1 from Chrome or Edge: <https://konsumer.github.io/fomajam/>

## Playing it

Every page has the same layout: a **header** (page, track, pattern, tempo, play/stop), the **body**, and the **knob
bar**. The knob bar shows what KNOB1–4 do on the current page, always in the same four colors.

| Control     | What it does                                                                         |
| ----------- | ------------------------------------------------------------------------------------ |
| `SELECT`    | Choose the track (1–9).                                                              |
| `PRESETS`   | Choose the pattern (1–8). While playing, the change happens at the end of the bar.   |
| `ALGORITHM` | Set the tempo.                                                                       |
| `PLAY`      | Start or stop.                                                                       |
| `REC`       | Record what you play into the pattern.                                               |
| `SAVE`      | Save the song. If it's playing, it saves when you stop (saving pauses the sound).    |
| `SEL`       | Show the next page of knobs. On PATTERN with a synth track: toggle slide on a step.  |
| `ARP`       | On PATTERN: set the ratchet of the step under the cursor.                            |
| `ENV`       | Clear the pattern. It asks first: `OCT+` clears, `OCT-` cancels.                     |
| `OCT-/OCT+` | Change the keyboard's octave. When a question is shown, `OCT+` is yes, `OCT-` is no. |

| Button       | Page                | Keys                                            | Knobs                                                                                                                   |
| ------------ | ------------------- | ----------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------- |
| `SEQ`        | PATTERN             | white keys: the 16 steps; black keys: tracks    | STEP, VOICE (or NOTE), ACCENT, MACHINE                                                                                  |
| `EDIT`       | SOUND               | play the track                                  | the track's sound (see [Machines](#machines))                                                                           |
| `HOME`       | MIX                 | white keys: mute tracks 1–9; black keys: tracks | LEVEL, DELAY, REVERB, MUTE                                                                                              |
| `HOME` again | SCOPE, then MIDI IN | play the track                                  | the MIX knobs. SCOPE shows the output; MIDI IN lists the last messages that came in. `HOME` once more goes back to MIX. |
| `FX`         | FX                  | play the track                                  | 4 pages (press `SEL`): see [Effects](#effects)                                                                          |
| `GLO`        | GLOBAL              | play the track                                  | TEMPO page: BPM, SWING, LENGTH, THEME; MIDI page: CLOCK, CC OUT. Also shows CPU load.                                   |

**Playing the keys:** on a synth track, the keys are a keyboard. On a drum track, they play the machine's kit: the
white keys are BD SD CH OH CP RS LT MT HT CY RD CB TB MA CL TI, and the black keys are BD2 SD2 CB2 and the congas. (On
the SMP machine, each key plays one sample.) The last voice or note you play is what new steps use.

**Recording:** with `REC` on, what you play goes into the step that is playing. When stopped, it goes into the step
under the cursor, and the cursor moves to the next step. `REC` also makes the keys play on the PATTERN page, so you can
watch the grid fill in. Turn it off to go back to editing steps.

**Editing steps:** on the PATTERN page, a white key turns a step on or off and moves the cursor to it. A new step uses
the track's current voice (or a synth's last note); KNOB2 changes it. Drum steps are colored by voice, so kicks,
snares and hats on the same track look different.

**Ratchets** (the rolls in trap and witch house): press `ARP` on the PATTERN page to cycle the step under the cursor
through ×1, ×2, ×3, ×4, ×6 and ×8. The step then plays that many times, evenly spaced across the step (and swung with
it). Every repeat is as loud as the step, so an accented step accents the whole roll. Ratchets work on every track:
drums roll, synths retrigger their note, and MIDI tracks send the repeats. A ratcheted step doesn't slide. On the grid,
the cell is split into its repeats (×6 and ×8 show four segments with a line underneath).

## Effects

Each track sends to a delay, a reverb and a chorus. The mix then goes through the master filter and the distortion.
The FX page has four pages of knobs (press `SEL` to move on), and each page draws what its knobs do:

| Page   | Knobs                       | What they do                                                                                                       |
| ------ | --------------------------- | ------------------------------------------------------------------------------------------------------------------ |
| SENDS  | DELAY, REVERB, CHORUS, DUCK | the selected track's sends, and how much track 1 ducks it (track 1 shows `KEY`: it does the ducking)               |
| SPACE  | TIME, FDBK, SIZE, DAMP      | the delay's time (in 16ths) and feedback; the reverb's size and how dark it gets                                   |
| MOTION | RATE, DEPTH, PUMP           | the chorus's speed and depth; how fast a ducked track comes back                                                   |
| MASTER | FILTER, RESO, DRIVE, TYPE   | one-knob filter (left of center low-pass, right high-pass, center off), its resonance, the distortion and its kind |

**Ducking** gives the pumping sound of trap and witch house: put the kick on track 1 and turn up DUCK on the bass and
pads. **Distortion types:** SOFT (a gentle saturation, the default), HARD (clipping), TUBE (warm, uneven), DIODE (soft
above, hard below), FUZZ, FOLD (wavefolding), WAVE (rounder folding), RECT (rectified: an octave up), BITS (fewer
bits), RATE (lower sample rate) and CRUSH (both).

## Machines

`MACHINE` sets what a track plays: a drum machine, a synth engine or MIDI out. Two tracks on the same machine are two
separate instruments. For a 303-style bassline, use ANALOG's ACID preset with MONO on, and add accents and slides to
the steps.

### Synth engines

Each engine has eight parameters and a set of presets. Choosing an engine loads its first preset.

| Engine   | What it is                                                                                                                                                                                                                                                                                          |
| -------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| ANALOG   | Two band-limited oscillators (saw, square, triangle, sine, PWM, sync, sub), noise, drive and a resonant filter.                                                                                                                                                                                     |
| LOFI     | A chip-style voice: pulse, triangle, saw, shift-register noise, 4-bit wavetables, bit depth and arpeggio.                                                                                                                                                                                           |
| TRIO     | Three oscillators with ring modulation, hard sync and AND waves, into a multimode filter.                                                                                                                                                                                                           |
| WHEEL    | A tonewheel organ: nine drawbars, percussion, key click, drive and rotary speaker.                                                                                                                                                                                                                  |
| NOISE    | White, pink and brown noise, impulses and shift-register noise through a resonant filter, with a bit crusher.                                                                                                                                                                                       |
| PHASE    | Phase distortion, in the style of Casio's CZ synths.                                                                                                                                                                                                                                                |
| VOICE    | Formant synthesis: vowels, choirs and a talking bass.                                                                                                                                                                                                                                               |
| PHYS     | Physical models: struck bars and bells, strings, membranes and sympathetic strings.                                                                                                                                                                                                                 |
| STRING   | A plucked string (Karplus-Strong): pick and pluck position, damping, decay and a bowing option.                                                                                                                                                                                                     |
| SUPERSAW | The JP-8000 super saw, based on Adam Szabo's measurements: seven free-running saws, his detune and mix curves, and a high-pass at the note's fundamental. A resonant low-pass, DRFT (tape wobble) and CRSH (lower bit depth and sample rate) make it dark and worn. Try WITCH, DRAG CHOIR or GHOST. |

STRING and SUPERSAW are written for this firmware in the same form as Felucca's engines.

On a synth track, the SOUND page has five pages of knobs (`SEL` steps through the tabs):

| Page                 | Knobs                                                                  |
| -------------------- | ---------------------------------------------------------------------- |
| the engine's 2 pages | its own parameters (for ANALOG: OSC and FLT)                           |
| ENV                  | ATTACK, DECAY, SUSTN (sustain), RELEASE                                |
| PATCH                | PRESET, ENV>FLT (how far the envelope sweeps the filter), LEVEL, DELAY |
| PLAY                 | GATE, MONO, GLIDE, TUNE                                                |

`GATE` sets how much of its step a sequenced note is held. At `TIE`, a note is held until the track's next note, which
takes over from it; use this for pad lines. With MONO on, a tied note glides into the next one without a new attack.
Choosing a preset sets GATE for you: TIE for slow-attack presets (pads), otherwise half a step.

An accented step opens the filter, and a step with slide (`SEL` on the PATTERN page) glides into the next one, even
with MONO off. Each track has two voices, and at
most eight synth voices play at once (the oldest gives way).

### Drum machines

On a drum machine, each step plays one voice: BD BD2 SD SD2 RS CP LT MT HT CH OH CY RD CB CB2 TB, the congas, TI MA
CL. If a machine doesn't have a voice, its closest voice plays it instead, retuned (for example, the DR-55 has no open
hi-hat, so its closed hi-hat stands in). `VOICE` skips voices a machine can't play at all. A closed hi-hat cuts off
an open one on the same track. To mix machines, give tracks different ones: a 909 kick, 808 claps, a 707 tambourine.

| Machine                                         | How it's made                                                     |
| ----------------------------------------------- | ----------------------------------------------------------------- |
| 808, 909, SDS (SDS-V), CR78                     | modeled on how the circuits work, ported from trommelsynthesizer  |
| 707, 606, DMX, LINN, 505, DR55, SK1, KPR77, ACE | recordings of their own ROM samples, as 4-bit ADPCM, cut at 0.8 s |
| SMP                                             | your own samples from `samples/` (only shown when there are any)  |

The knobs work the same on every drum machine:

- **TUNE:** ±12 semitones.
- **DECAY:** 64 is the machine's own decay.
- **TONE:** 64 is neutral. On modeled machines it is the voice's tone control; on recordings, lower is darker and
  higher is brighter.
- **LEVEL**
- **EXTRA:** ATTACK on a kick, SNAPPY on a snare.

The ROM machines are listed in [tools/roms.py](tools/roms.py) (`MACHINES`). The first build downloads their
recordings from [tidal-drum-machines](https://github.com/geikha/tidal-drum-machines) (pinned to one commit) into
`roms/`. Without a network connection, the build continues without them.

Machines are added in the order they're listed until `BUDGET` (430 KiB of flash) is used up, and the build tells you
which ones didn't fit. LM-1, Space Drum, RZ-1, DR-110, Drumulator, RX5, HR-16, SDS-5, Drumtraks and MPC60 are listed
but don't fit; to include one, move it higher in the list. See [LICENSING.md](LICENSING.md) about these recordings.

### MIDI

**MIDI in:** each track has its own channel. Channels 1–9 play tracks 1–9, whatever machine they use, so a keyboard
can play any of them. Channel 10 plays the selected track. Drum machines respond to General MIDI drum notes; synths
respond to any note.

**MIDI controllers:** every parameter also has a fixed MIDI CC, on the same channels (channel 16 is the song and
effects), so a controller with lots of knobs reaches everything without menus. See [CC.md](CC.md) for the map. With
`CC OUT` on (GLOBAL page, MIDI page), changes made on the FM-1 are sent back out so a controller's lights follow.

**MIDI out** is a machine (MIDI, shown as `MID` on the grid). Its steps are sent over USB MIDI instead of making a
sound:

- Each step sends a note on the track's channel at its velocity (127 if accented).
- The note-off comes after GATE % of the step, or at the next note with `TIE`.
- A step with slide is sent legato (the next note-on comes before this note-off), so the receiving synth can glide.
- Keys and MIDI in on a MIDI track are sent out on its channel too, so you can play another synth through the FM-1.

Its SOUND page has three pages of knobs:

| Page | Knobs                                                                                          |
| ---- | ---------------------------------------------------------------------------------------------- |
| NOTE | CHAN, PROG (a program change, sent when you turn it and when the song starts), GATE, VELO      |
| CC   | four MIDI controllers, sent as you turn them (CC74, CC71, CC1 and CC10 unless you change them) |
| CC#  | which controller numbers the CC page sends                                                     |

**MIDI clock:** the GLOBAL page's second page (press `SEL`) has `CLOCK`. When it's on, the FM-1 sends MIDI clock
(24 ticks per beat) with start and stop messages.

## Samples

Put `.wav` files in `samples/`. Any PCM WAV works (8, 16 or 24-bit, mono or stereo, any sample rate). The build
converts them to 16-bit mono at 22050 Hz, up to 2 seconds each, and they become the SMP machine: one voice per file,
named after it (`01-boom.wav` becomes `BOOM`). Samples share flash with the ROM machines, so you may need to remove a
ROM machine to make room.

## Themes

The themes are in [src/themes.h](src/themes.h): one for each FM-1 color (BLACK, GREY, BLUE, GREEN, FOREST, PURPLE,
RED, ORANGE, TEAL), plus WHITE (a light screen) and NEON. `THEME` on the GLOBAL page changes the colors as you turn it.
A theme is about a dozen colors; add one to the file and it appears on the GLOBAL page. The eight track colors also
color the drum voices: kicks use the first, snares the second, claps the third, closed hi-hats the fourth, and so on.

## Develop

You only need Docker:

```sh
./fm1 dev          # http://localhost:8000: the emulator, installer and rescue; rebuilds when you save
./fm1 build        # build/fomajam-VERSION.fwsc
```

| File                                     | What it is                                                            |
| ---------------------------------------- | --------------------------------------------------------------------- |
| [src/main.c](src/main.c)                 | the pages, knobs and keys, and the song it starts with                |
| [src/drums.c](src/drums.c)               | the drum machines: voice models, recordings, hi-hat choke             |
| [src/engine.c](src/engine.c)             | the sequencer clock, MIDI out, the mix, the delay and the output      |
| [src/fx.c](src/fx.c)                     | the reverb, chorus, master filter and distortion                      |
| [src/synths.c](src/synths.c)             | the host for the synth engines (Felucca's are in `vendor/`)           |
| [src/eng_string.h](src/eng_string.h)     | the STRING engine                                                     |
| [src/eng_supersaw.h](src/eng_supersaw.h) | the SUPERSAW engine                                                   |
| [src/groove.h](src/groove.h)             | the song format (also what gets saved, 4 KiB at most)                 |
| [src/drums.h](src/drums.h)               | the modeled drum machines as integer tables, made by `tools/drums.py` |
| [src/gains.h](src/gains.h)               | each modeled voice's level, made by `test/render.c --calibrate`       |
| [src/themes.h](src/themes.h)             | the color themes                                                      |
| [src/tables.h](src/tables.h)             | lookup tables, made by `python3 tools/tables.py`                      |
| [tools/samples.py](tools/samples.py)     | converts `samples/*.wav` to `build/gen/samples.h` (run by the build)  |
| [tools/roms.py](tools/roms.py)           | builds the ROM machines into `build/gen/roms.h` (run by the build)    |
| [test/render.c](test/render.c)           | renders and checks the sound on your computer                         |

To hear and check the sound without an FM-1 (after a build, which creates `build/gen/`):

```sh
cc -O2 -Isrc -Iplatform -Ibuild/gen -Ivendor/felucca-engines/src -Ivendor/felucca -D__wasm__ \
  -o build/render test/render.c src/{drums,engine,main,synths}.c -lm
build/render --check          # every drum voice sounds, stays in range and dies away
build/render --wav /tmp/kit   # a .wav for every machine and voice
build/render --synths         # the same checks for every synth preset
build/render --song song.wav  # 8 seconds of the starting song (add N for pattern N)
build/render --midi           # the MIDI messages a MIDI track and the clock send in one bar, with their times
```

The functions the firmware can call are in [platform/fm1.h](platform/fm1.h). The name, version and package identity
are in [firmware.json](firmware.json). The rules of the chip (no floating point, keep `audio()` short, and so on) are
in [foma's README](https://github.com/konsumer/foma#the-rules-of-the-chip).

Every push to `main` publishes the emulator and installer to GitHub Pages (Settings → Pages → Source: GitHub Actions).
Pushing a tag (like `v0.2`) also creates a release with the `.fwsc` file.

Installing firmware is at your own risk. If the FM-1 doesn't start, hold OCT- while switching it on to enter USB
rescue mode, then install from the page.

## License

GPL-3.0-only. For credits and third-party parts, see [LICENSING.md](LICENSING.md).
