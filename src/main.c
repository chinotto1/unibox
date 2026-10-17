/* UNIBOX: a groovebox. It has 9 tracks; each plays its own machine, either a drum machine (808, 909, 707,
 * DMX, LinnDrum, CR-78, ...) with a voice per step, a sampled instrument, or a synth engine (ANALOG, SH-101,
 * TB-303, Juno, wavetable, ...) or MIDI out with a note per step. There are 16 patterns of 16 steps.
 *
 * This file is the user interface. Every page has the same three parts: a header (page, track, pattern,
 * tempo), the body, and a knob bar that shows what KNOB1-4 do on that page, always in the same four colors.
 *
 *   SEQ        PATTERN   white keys are steps, black keys pick tracks
 *   EDIT       SOUND     the track's sound; ALGORITHM or SEL moves the tabs; the keys play
 *   HOME       MIX       levels; white keys mute tracks, black keys pick tracks
 *   HOME again PATTERNS  white keys pick a pattern; SAVE + a key copies the current one; two keys chain
 *   HOME again SCOPE     the output
 *   HOME again MIDI IN   what comes in; HOME once more returns to MIX
 *   LFO        SAMPLE    record USB audio into eight slots, then name the instrument with the keys
 *   FX         FX        sends, delay, reverb, chorus, ducking, the master filter and distortion
 *   GLO        GLOBAL    tempo, swing, length, theme, MIDI clock
 *   SELECT     picks the track        PRESETS    picks the pattern (it changes at the end of the bar)
 *   ALGORITHM  on SOUND, FX and GLOBAL: the next tab; elsewhere the tempo
 *   PLAY / SAVE / OCT- OCT+
 *   REC        a short tap: step record. Hold it and play: a free take (up to 64 beats), even if stopped
 *   SEL        the next page of knobs; on PATTERN with a synth, toggles slide on the cursor's step
 *   ENV        clears the pattern after asking (OCT+ answers yes, OCT- no, for every question)
 *   ARP        on PATTERN, sets the cursor's step's ratchet: x1 x2 x3 x4 x6 x8
 *
 * The sound is in engine.c, drums.c, synths.c and sampler.c; the colors are in themes.h. */
#include "groove.h"
#include "sampler.h"
#include "themes.h"

/* ---- the layout, the same on every page */
#define HEAD_H 24
#define BODY_Y 26
#define BODY_H 156
#define KNOB_Y 184

enum { PG_PATTERN,
       PG_SOUND,
       PG_MIX,
       PG_FX,
       PG_GLOBAL,
       PG_SCOPE, /* HOME again, from MIX */
       PG_MIDI,  /* and again */
       PG_ORDER, /* patterns, copy and chain: HOME again, before SCOPE */
       PG_SAMPLE, /* LFO: sample from USB audio */
       PAGES };
static const char *const PAGE_NAME[PAGES] = {"PATTERN", "SOUND", "MIX", "FX", "GLOBAL", "SCOPE", "MIDI IN", "PATTERNS", "SAMPLE"};
static const uint8_t PAGE_BTN[PAGES] = {BTN_SEQ, BTN_EDIT, BTN_HOME, BTN_FX, BTN_GLO, BTN_HOME, BTN_HOME, BTN_HOME, BTN_LFO};

static int page, sub, track, cursor, octave = -1, rec, taking, pasted;
static int chain_key = -1; /* a white key held on PATTERNS, waiting for a second key to chain */
static const theme_t *th;

/* ---- the knobs: every value is a parameter with a name, a range and a way to show it */
enum { F_NUM,
       F_SEMI,
       F_MACHINE,
       F_VOICE,
       F_ONOFF,
       F_STEP,
       F_THEME,
       F_16TH,
       F_PCT,
       F_ENGINE,
       F_BIPOLAR,
       F_PRESET,
       F_CHAN,
       F_GATE,
       F_PROG,
       F_FILTER,
       F_DIST,
       F_NONE };
enum { P_TUNE,
       P_DECAY,
       P_TONE,
       P_LEVEL,
       P_MACHINE,
       P_VOICE,
       P_EXTRA,
       P_SEND,
       P_SLIDE,
       P_STEP,
       P_STACC,
       P_STSLIDE,
       P_STVOICE,
       P_MUTE,
       P_DRIVE,
       P_BPM,
       P_SWING,
       P_LENGTH,
       P_THEME,
       P_DTIME,
       P_DFDBK,
       P_SE0, /* a synth engine's eight */
       P_SE1,
       P_SE2,
       P_SE3,
       P_SE4,
       P_SE5,
       P_SE6,
       P_SE7,
       P_SE8,
       P_SE9,
       P_SE10,
       P_SE11,
       P_SE12,
       P_SE13,
       P_SE14,
       P_SE15,
       P_ATK,
       P_DEC,
       P_SUS,
       P_REL,
       P_FENV,
       P_PRESET,
       P_MONO,
       P_MCH, /* a MIDI track's */
       P_PROG,
       P_GATE,
       P_VELO,
       P_CC0,
       P_CC1,
       P_CC2,
       P_CC3,
       P_CCN0,
       P_CCN1,
       P_CCN2,
       P_CCN3,
       P_CLOCK,
       P_RSEND, /* the effects (fx.c) */
       P_CSEND,
       P_DUCK,
       P_RSIZE,
       P_RDAMP,
       P_CRATE,
       P_CDEPTH,
       P_PUMP,
       P_FILTER,
       P_RESO,
       P_DTYPE,
       P_CCOUT,
       P_STIME, /* the sampler: tenths of a second, and which bank */
       P_SBANK,
       P_NONE, /* an empty knob */
       PARAMS };
static const struct {
  const char *name;
  uint8_t lo, hi, fmt;
} PARAM[PARAMS] = {
    [P_TUNE] = {"TUNE", 0, 127, F_SEMI},
    [P_DECAY] = {"DECAY", 0, 127, F_NUM},
    [P_TONE] = {"TONE", 0, 127, F_NUM},
    [P_LEVEL] = {"LEVEL", 0, 127, F_NUM},
    [P_MACHINE] = {"MACHINE", 0, 0, F_MACHINE}, /* up to: phi() */
    [P_VOICE] = {"VOICE", 0, 0, F_VOICE},
    [P_EXTRA] = {"EXTRA", 0, 127, F_NUM},
    [P_SEND] = {"DELAY", 0, 127, F_NUM},
    [P_SLIDE] = {"SLIDE", 0, 127, F_NUM},
    [P_STEP] = {"STEP", 0, STEPS - 1, F_STEP},
    [P_STACC] = {"ACCENT", 0, 1, F_ONOFF},
    [P_STSLIDE] = {"SLIDE", 0, 1, F_ONOFF},
    [P_STVOICE] = {"VOICE", 0, 0, F_VOICE},
    [P_MUTE] = {"MUTE", 0, 1, F_ONOFF},
    [P_DRIVE] = {"DRIVE", 0, 127, F_NUM},
    [P_BPM] = {"BPM", 40, 250, F_NUM},
    [P_SWING] = {"SWING", 0, 100, F_PCT},
    [P_LENGTH] = {"LENGTH", 1, STEPS, F_NUM},
    [P_THEME] = {"THEME", 0, THEME_COUNT - 1, F_THEME},
    [P_DTIME] = {"TIME", 1, 16, F_16TH},
    [P_DFDBK] = {"FDBK", 0, 127, F_NUM},
    [P_SE0] = {"", 0, 0, F_ENGINE}, /* names, ranges: the engine's (synth_param) */
    [P_SE1] = {"", 0, 0, F_ENGINE},
    [P_SE2] = {"", 0, 0, F_ENGINE},
    [P_SE3] = {"", 0, 0, F_ENGINE},
    [P_SE4] = {"", 0, 0, F_ENGINE},
    [P_SE5] = {"", 0, 0, F_ENGINE},
    [P_SE6] = {"", 0, 0, F_ENGINE},
    [P_SE7] = {"", 0, 0, F_ENGINE},
    [P_SE8] = {"", 0, 0, F_ENGINE},
    [P_SE9] = {"", 0, 0, F_ENGINE},
    [P_SE10] = {"", 0, 0, F_ENGINE},
    [P_SE11] = {"", 0, 0, F_ENGINE},
    [P_SE12] = {"", 0, 0, F_ENGINE},
    [P_SE13] = {"", 0, 0, F_ENGINE},
    [P_SE14] = {"", 0, 0, F_ENGINE},
    [P_SE15] = {"", 0, 0, F_ENGINE},
    [P_ATK] = {"ATTACK", 0, 127, F_NUM},
    [P_DEC] = {"DECAY", 0, 127, F_NUM},
    [P_SUS] = {"SUSTN", 0, 127, F_NUM},
    [P_REL] = {"RELEASE", 0, 127, F_NUM},
    [P_FENV] = {"ENV>FLT", 0, 127, F_BIPOLAR}, /* -64..63: plo() */
    [P_PRESET] = {"PRESET", 0, 0, F_PRESET},
    [P_MONO] = {"MONO", 0, 1, F_ONOFF},
    [P_MCH] = {"CHAN", 0, 15, F_CHAN},
    [P_PROG] = {"PROG", 0, 128, F_PROG},
    [P_GATE] = {"GATE", 1, GATE_TIE, F_GATE},
    [P_VELO] = {"VELO", 1, 127, F_NUM},
    [P_CC0] = {"CC", 0, 127, F_NUM}, /* named by their number: pname() */
    [P_CC1] = {"CC", 0, 127, F_NUM},
    [P_CC2] = {"CC", 0, 127, F_NUM},
    [P_CC3] = {"CC", 0, 127, F_NUM},
    [P_CCN0] = {"CC1 #", 0, 127, F_NUM},
    [P_CCN1] = {"CC2 #", 0, 127, F_NUM},
    [P_CCN2] = {"CC3 #", 0, 127, F_NUM},
    [P_CCN3] = {"CC4 #", 0, 127, F_NUM},
    [P_CLOCK] = {"CLOCK", 0, 1, F_ONOFF},
    [P_RSEND] = {"REVERB", 0, 127, F_NUM},
    [P_CSEND] = {"CHORUS", 0, 127, F_NUM},
    [P_DUCK] = {"DUCK", 0, 127, F_NUM},
    [P_RSIZE] = {"SIZE", 0, 127, F_NUM},
    [P_RDAMP] = {"DAMP", 0, 127, F_NUM},
    [P_CRATE] = {"RATE", 0, 127, F_NUM},
    [P_CDEPTH] = {"DEPTH", 0, 127, F_NUM},
    [P_PUMP] = {"PUMP", 0, 127, F_NUM},
    [P_FILTER] = {"FILTER", 0, 127, F_FILTER},
    [P_RESO] = {"RESO", 0, 127, F_NUM},
    [P_DTYPE] = {"TYPE", 0, DISTS - 1, F_DIST},
    [P_CCOUT] = {"CC OUT", 0, 1, F_ONOFF},
    [P_STIME] = {"TIME", 2, 20, F_NUM},
    [P_SBANK] = {"BANK", 0, SAMP_BANKS - 1, F_NUM},
    [P_NONE] = {"", 0, 0, F_NONE},
};

static int knobs[4];

static int machine_of(int t) { return song.track[t].machine; }
static int is_synth(int t) { return machine_synth(machine_of(t)); } /* a synth engine: notes */
static int engine_of(int t) { return machine_engine(machine_of(t)); }

static int is_se(int id) { return id >= P_SE0 && id <= P_SE15; }

static int phi(int id) { /* a parameter's highest value */
  synth_param_t sp;
  if (is_se(id)) {
    synth_param(track, id - P_SE0, &sp);
    return sp.hi;
  }
  return id == P_MACHINE                    ? machine_count() - 1
         : id == P_VOICE || id == P_STVOICE ? machine_voices(machine_of(track)) - 1
         : id == P_PRESET                   ? (engine_of(track) >= 0 ? synth_presets(engine_of(track)) - 1 : 0)
         : id == P_EXTRA && machine_usr(machine_of(track)) ? SAMP_BANKS - 1
         : id == P_FENV                     ? 63
                                            : PARAM[id].hi;
}

static int plo(int id) { /* and its lowest */
  synth_param_t sp;
  if (is_se(id)) {
    synth_param(track, id - P_SE0, &sp);
    return sp.lo;
  }
  return id == P_FENV ? -64 : PARAM[id].lo;
}

static const char *pname(int id) {  /* EXTRA is what the voice's original called it; a synth has notes */
  if (id >= P_CC0 && id <= P_CC3) { /* a controller by its number: CC74 */
    static char s[4][8];
    char *o = s[id - P_CC0];
    o[0] = 'C', o[1] = 'C';
    format_int(o + 2, song.track[track].ccn[id - P_CC0]);
    return o;
  }
  if (id == P_STVOICE && is_synth(track)) {
    return "NOTE";
  }
  if (is_se(id)) {
    synth_param_t sp;
    synth_param(track, id - P_SE0, &sp);
    return sp.name;
  }
  if (id == P_SLIDE && engine_of(track) >= 0) {
    return "GLIDE";
  }
  if (id == P_EXTRA) {
    if (machine_usr(machine_of(track))) {
      return "BANK";
    }
    const char *v = voice_name(machine_of(track), song.track[track].voice);
    return v[0] == 'B' && v[1] == 'D' ? "ATTACK" : v[0] == 'S' && v[1] == 'D' ? "SNAPPY"
                                                                              : "EXTRA";
  }
  return PARAM[id].name;
}

static int is_midi(int t) { return machine_midi(machine_of(t)); }

static int engine_tabs(void) { /* the engine's own tabs, then ENV (unless it has its own), PTCH, PLAY */
  int e = engine_of(track);
  if (e < 0) {
    return 0;
  }
  return synth_pages(e) + (synth_ownenv(e) ? 2 : 3);
}

static int subpages(void) {
  return page == PG_GLOBAL ? 2 : page == PG_FX       ? 4
                             : page != PG_SOUND      ? 1
                             : engine_of(track) >= 0 ? engine_tabs()
                             : is_midi(track)        ? 3
                                                     : 2;
}

static const char *subpage_name(int i) { /* the tabs on the SOUND and GLOBAL pages */
  static const char *const DRUM[2] = {"PLAY", "MORE"}, *const SE[3] = {"ENV", "PTCH", "PLAY"}, *const TAIL[2] = {"PTCH", "PLAY"},
                           *const MIDI[3] = {"NOTE", "CC", "CC#"}, *const GLOBAL[2] = {"TEMPO", "MIDI"},
                           *const FX[4] = {"SENDS", "SPACE", "MOTION", "MASTER"};
  int e = engine_of(track);
  if (page == PG_GLOBAL) {
    return GLOBAL[i & 1];
  }
  if (page == PG_FX) {
    return FX[i & 3];
  }
  if (e >= 0) {
    int n = synth_pages(e);
    if (i < n) {
      const char *s = synth_page(e, i);
      return s ? s : "";
    }
    return synth_ownenv(e) ? TAIL[i - n] : SE[i - n];
  }
  return is_midi(track) ? MIDI[i % 3] : DRUM[i & 1];
}

static void pick_knobs(int *k) {
  static const uint8_t DRUM_STEP[4] = {P_STEP, P_STVOICE, P_STACC, P_MACHINE};
  static const uint8_t DRUM_PLAY[4] = {P_TUNE, P_DECAY, P_TONE, P_LEVEL};
  static const uint8_t DRUM_MORE[4] = {P_MACHINE, P_VOICE, P_EXTRA, P_SEND};
  static const uint8_t SYN_STEP[4] = {P_STEP, P_STVOICE, P_STACC, P_MACHINE}; /* SLIDE: the SEL button */
  static const uint8_t ENV[4] = {P_ATK, P_DEC, P_SUS, P_REL};
  static const uint8_t PATCH[4] = {P_PRESET, P_FENV, P_LEVEL, P_SEND};
  static const uint8_t PLAY[4] = {P_GATE, P_MONO, P_SLIDE, P_TUNE};
  static const uint8_t MIX[4] = {P_LEVEL, P_SEND, P_RSEND, P_MUTE};
  static const uint8_t FX[4][4] = {{P_SEND, P_RSEND, P_CSEND, P_DUCK}, {P_DTIME, P_DFDBK, P_RSIZE, P_RDAMP}, {P_CRATE, P_CDEPTH, P_PUMP, P_NONE}, {P_FILTER, P_RESO, P_DRIVE, P_DTYPE}};
  static const uint8_t GLOBAL[2][4] = {{P_BPM, P_SWING, P_LENGTH, P_THEME}, {P_CLOCK, P_CCOUT, P_NONE, P_NONE}};
  static const uint8_t MIDI[3][4] = {{P_MCH, P_PROG, P_GATE, P_VELO}, {P_CC0, P_CC1, P_CC2, P_CC3}, {P_CCN0, P_CCN1, P_CCN2, P_CCN3}};
  static const uint8_t SAMPLE[4] = {P_STIME, P_SBANK, P_NONE, P_NONE};
  static const uint8_t NONE[4] = {P_NONE, P_NONE, P_NONE, P_NONE};
  if (engine_of(track) >= 0 && page == PG_SOUND) {
    int e = engine_of(track), n = synth_pages(e), own = synth_ownenv(e);
    if (sub < n) {
      for (int i = 0; i < 4; i++) {
        k[i] = P_SE0 + sub * 4 + i;
      }
    } else {
      int s = sub - n;
      const uint8_t *row = !own ? (s == 0 ? ENV : s == 1 ? PATCH : PLAY) : (s == 0 ? PATCH : PLAY);
      for (int i = 0; i < 4; i++) {
        k[i] = row[i];
      }
    }
    return;
  }
  const uint8_t *src = page == PG_SAMPLE                             ? SAMPLE
                       : page == PG_ORDER                             ? NONE
                       : page == PG_MIX || page == PG_SCOPE || page == PG_MIDI ? MIX
                       : page == PG_FX                                ? FX[sub & 3]
                       : page == PG_GLOBAL                            ? GLOBAL[sub & 1]
                       : is_midi(track) && page == PG_SOUND           ? MIDI[sub % 3]
                       : is_synth(track)                              ? SYN_STEP
                       : page == PG_PATTERN                           ? DRUM_STEP
                                                                      : (sub ? DRUM_MORE : DRUM_PLAY);
  for (int i = 0; i < 4; i++) {
    k[i] = src[i];
  }
}

static pattern_t *pat(void) { return &song.pat[song.pattern]; }

static uint8_t *pbyte(int id) { /* the byte behind a parameter, if it is one */
  track_t *d = &song.track[track];
  switch (id) {
    case P_TUNE:
      return &d->tune;
    case P_DECAY:
      return &d->decay;
    case P_TONE:
      return &d->tone;
    case P_LEVEL:
      return &d->level;
    case P_MACHINE:
      return &d->machine;
    case P_VOICE:
      return &d->voice;
    case P_EXTRA:
      return &d->extra;
    case P_SEND:
      return &d->send;
    case P_RSEND:
      return &d->rsend;
    case P_CSEND:
      return &d->csend;
    case P_DUCK:
      return &d->duck;
    case P_RSIZE:
      return &song.rsize;
    case P_RDAMP:
      return &song.rdamp;
    case P_CRATE:
      return &song.crate;
    case P_CDEPTH:
      return &song.cdepth;
    case P_PUMP:
      return &song.pump;
    case P_FILTER:
      return &song.filter;
    case P_RESO:
      return &song.reso;
    case P_DTYPE:
      return &song.dtype;
    case P_CCOUT:
      return &song.ccout;
    case P_SLIDE:
      return &d->slide;
    case P_ATK:
      return &d->atk;
    case P_DEC:
      return &d->dec;
    case P_SUS:
      return &d->sus;
    case P_REL:
      return &d->rel;
    case P_PRESET:
      return &d->preset;
    case P_MONO:
      return &d->mono;
    case P_MCH:
      return &d->mch;
    case P_PROG:
      return &d->prog;
    case P_GATE:
      return &d->gate;
    case P_VELO:
      return &d->velo;
    case P_CC0:
    case P_CC1:
    case P_CC2:
    case P_CC3:
      return &d->cc[id - P_CC0];
    case P_CCN0:
    case P_CCN1:
    case P_CCN2:
    case P_CCN3:
      return &d->ccn[id - P_CCN0];
    case P_CLOCK:
      return &song.clock;
    case P_DRIVE:
      return &song.drive;
    case P_BPM:
      return &song.bpm;
    case P_SWING:
      return &song.swing;
    case P_LENGTH:
      return &song.length;
    case P_THEME:
      return &song.theme;
    case P_DTIME:
      return &song.dtime;
    case P_DFDBK:
      return &song.dfdbk;
  }
  return 0;
}

static int pget(int id) {
  if (is_se(id)) {
    return song.track[track].e[id - P_SE0];
  }
  uint8_t *b = pbyte(id);
  if (b) {
    return *b;
  }
  switch (id) {
    case P_STEP:
      return cursor;
    case P_FENV:
      return song.track[track].fenv;
    case P_STACC:
      return !!(pat()->flag[track][cursor] & STEP_ACC);
    case P_STVOICE: /* -1: a rest */
      return pat()->flag[track][cursor] & STEP_ON ? pat()->val[track][cursor] : -1;
    case P_STSLIDE:
      return !!(pat()->flag[track][cursor] & STEP_SLIDE);
    case P_MUTE:
      return song.mute >> track & 1;
    case P_STIME:
      return sampler_time_ds();
    case P_SBANK:
      return sampler_bank();
  }
  return 0;
}

static void flip(uint8_t *f, int bit, int on) {
  *f = (uint8_t)(on ? *f | bit : *f & ~bit);
}

static void flip16(uint16_t *bits, int b, int on) {
  *bits = (uint16_t)(on ? *bits | 1u << b : *bits & ~(1u << b));
}

static void pset(int id, int v) {
  uint8_t *b = pbyte(id);
  if (is_se(id)) {
    song.track[track].e[id - P_SE0] = (int8_t)v;
  } else if (id == P_FENV) {
    song.track[track].fenv = (int8_t)v;
  } else if (b) {
    *b = (uint8_t)v;
    if (id == P_PRESET) {
      synth_load(track, v);
    } else if (id == P_MACHINE && engine_of(track) >= 0) { /* a synth engine: its first preset */
      synth_load(track, 0);
    }
  } else if (id == P_STEP) {
    cursor = v;
  } else if (id == P_STACC || id == P_STSLIDE) { /* on a rest: the step comes on too */
    uint8_t *f = &pat()->flag[track][cursor];
    if (v && !(*f & STEP_ON)) {
      pat()->val[track][cursor] = song.track[track].voice;
      *f |= STEP_ON;
    }
    flip(f, id == P_STACC ? STEP_ACC : STEP_SLIDE, v);
  } else if (id == P_STVOICE) {
    uint8_t *f = &pat()->flag[track][cursor];
    if (v < 0) {
      *f = 0;
    } else {
      pat()->val[track][cursor] = (uint8_t)v;
      *f |= STEP_ON;
      song.track[track].voice = (uint8_t)v; /* new steps get it too */
    }
  } else if (id == P_MUTE) {
    flip16(&song.mute, track, v);
  } else if (id == P_STIME) {
    sampler_set_time(v);
  } else if (id == P_SBANK) {
    sampler_set_bank(v);
  }
}

static void pformat(int id, int v, char *out) {
  const char *s = 0;
  int n;
  if (id == P_DUCK && !track) { /* track 1 is what ducks the others */
    out[0] = 'K', out[1] = 'E', out[2] = 'Y', out[3] = 0;
    return;
  }
  if (id == P_STIME) { /* tenths of a second: 1.0 */
    out[0] = (char)('0' + v / 10);
    out[1] = '.';
    out[2] = (char)('0' + v % 10);
    out[3] = 0;
    return;
  }
  if (id == P_SBANK) {
    s = sampler_bank_name(v);
    n = 0;
    for (; s[n] && n < 7; n++) {
      out[n] = s[n];
    }
    out[n] = 0;
    return;
  }
  switch (PARAM[id].fmt) {
    case F_SEMI:
      n = (v - 64) * 12 / 64;
      out[0] = n < 0 ? '-' : '+';
      format_int(out + 1, n < 0 ? -n : n);
      if (!n) {
        out[0] = '0', out[1] = 0;
      }
      return;
    case F_MACHINE:
      s = machine_name(v);
      break;
    case F_VOICE:
      s = v < 0 ? "--" : voice_name(machine_of(track), v);
      break;
    case F_ONOFF:
      s = v ? "ON" : "OFF";
      break;
    case F_THEME:
      s = THEMES[v].name;
      break;
    case F_STEP:
      format_int(out, v + 1);
      return;
    case F_16TH: {
      format_int(out, v);
      char *o = out;
      while (*o) {
        o++;
      }
      o[0] = '/', o[1] = '1', o[2] = '6', o[3] = 0;
      return;
    }
    case F_PCT:
      format_int(out, 50 + v / 4); /* the swung 16th's share of the pair */
      return;
    case F_BIPOLAR:
      out[0] = v < 0 ? '-' : '+';
      format_int(out + 1, v < 0 ? -v : v);
      return;
    case F_GATE:
      if (v >= GATE_TIE) {
        s = "TIE";
        break;
      }
      format_int(out, v);
      return;
    case F_CHAN:
      format_int(out, v + 1);
      return;
    case F_PROG:
      if (!v) {
        s = "--";
        break;
      }
      format_int(out, v - 1);
      return;
    case F_FILTER: /* LP or HP and how far, OFF in the middle */
      if (v == 64) {
        s = "OFF";
        break;
      }
      out[0] = v < 64 ? 'L' : 'H', out[1] = 'P', out[2] = ' ';
      format_int(out + 3, v < 64 ? 64 - v : v - 64);
      return;
    case F_DIST:
      s = DIST_NAME[v % DISTS];
      break;
    case F_NONE:
      out[0] = 0;
      return;
    case F_PRESET:
      s = engine_of(track) >= 0 ? synth_preset_name(engine_of(track), v) : "";
      break;
    case F_ENGINE: {
      synth_param_t sp;
      synth_param(track, id - P_SE0, &sp);
      if (sp.fmt == SF_ENUM) {
        s = sp.names[v - sp.lo];
        break;
      }
      if (sp.fmt == SF_SEMI) {
        out[0] = v < 0 ? '-' : '+';
        format_int(out + 1, v < 0 ? -v : v);
        return;
      }
      format_int(out, v);
      return;
    }
    default:
      format_int(out, v);
      return;
  }
  int i = 0;
  for (; s[i] && i < 7; i++) { /* (7: what fits a knob) */
    out[i] = s[i];
  }
  out[i] = 0;
}

/* ---- drawing helpers */
static int len(const char *s) {
  int n = 0;
  while (s[n]) {
    n++;
  }
  return n;
}

static void text_center(int x, int w, int y, const char *s, uint16_t color, int font) {
  text(x + (w - text_width(s, font)) / 2, y, s, color, font);
}

static uint16_t tcolor(int t) { return (song.mute >> t & 1) ? th->dim : th->track[t]; }

static const char *track_short(int t) { return machine_label(machine_of(t)); }

static int append(char *out, int i, const char *a) {
  for (; *a && i < 14; a++) {
    out[i++] = *a;
  }
  out[i] = 0;
  return i;
}

static void track_title(int t, char *out, int label) { /* "3 909 CH", "9 ANALOG A1"; label: "9 ANA A1" */
  out[0] = (char)('1' + t);
  out[1] = ' ';
  int i = append(out, 2, label ? machine_label(machine_of(t)) : machine_name(machine_of(t)));
  out[i++] = ' ';
  append(out, i, voice_name(machine_of(t), song.track[t].voice));
}

/* ---- the header: page, track (or a message), pattern, tempo, transport */
static char toast_msg[16];
static uint32_t toast_until;

static void draw_transport(void) {
  char s[12];
  int chained = song.chain_a != song.chain_b;
  int shown = chained && playing ? cur_pattern : song.pattern;
  int queued = playing && cur_pattern != song.pattern;
  rect(166, 0, 74, HEAD_H, th->panel);
  s[0] = 'P';
  format_int(s + 1, shown + 1);
  text(168, 4, s, queued || chained ? th->accent : th->dim, FONT_SMALL);
  format_int(s, song.bpm);
  text(218 - text_width(s, FONT_SMALL), 4, s, th->text, FONT_SMALL);
  uint16_t c = rec || taking || sampler_recording() ? RGB(255, 60, 60) : th->text;
  if (playing) {
    for (int i = 0; i < 6; i++) {
      rect(224 + i, 6 + i, 1, 13 - 2 * i, c);
      rect(224 + i, 6 + i, 1, 1, c);
    }
  } else {
    rect(224, 7, 10, 10, rec ? c : th->dim);
  }
}

static void draw_track_chip(void) {
  int x = text_width(PAGE_NAME[page], FONT_SMALL) + 16;
  rect(x, 0, 166 - x, HEAD_H, th->panel);
  if (toast_until) { /* cut to the room there is */
    char s[16];
    int n = 0;
    for (; toast_msg[n]; n++) {
      s[n] = toast_msg[n];
    }
    s[n] = 0;
    while (n > 0 && text_width(s, FONT_SMALL) > 164 - x) {
      s[--n] = 0;
    }
    text(x + 2, 4, s, th->accent, FONT_SMALL);
    return;
  }
  char s[16];
  track_title(track, s, 0);
  if (text_width(s, FONT_SMALL) + 10 > 164 - x) { /* too long for the room: the machine's label */
    track_title(track, s, 1);
  }
  int w = text_width(s, FONT_SMALL) + 10;
  rect(x, 3, w, 18, th->track[track]);
  text(x + 5, 4, s, th->bg, FONT_SMALL);
}

static void draw_header(void) {
  int w = text_width(PAGE_NAME[page], FONT_SMALL) + 12;
  rect(0, 0, w, HEAD_H, th->accent);
  text(6, 4, PAGE_NAME[page], th->bg, FONT_SMALL);
  rect(w, 0, 4, HEAD_H, th->panel);
  draw_track_chip();
  draw_transport();
}

static void toast(const char *s) {
  int i = 0;
  for (; s[i] && i < 15; i++) {
    toast_msg[i] = s[i];
  }
  toast_msg[i] = 0;
  toast_until = millis() + 1200;
  draw_track_chip();
}

/* ---- the knob bar */
static void draw_knob(int k) {
  int id = knobs[k], x = k * 60, v = pget(id);
  uint16_t c = th->knob[k];
  char s[16];
  rect(x, KNOB_Y, 59, 56, th->panel);
  if (id == P_NONE) {
    return;
  }
  rect(x, KNOB_Y, 59, 2, c);
  text_center(x, 59, KNOB_Y + 4, pname(id), c, FONT_SMALL);
  pformat(id, v, s);
  if (len(s) <= 3) {
    text_center(x, 59, KNOB_Y + 18, s, th->text, FONT_BIG);
  } else {
    text_center(x, 59, KNOB_Y + 27, s, th->text, FONT_SMALL);
  }
  int lo = plo(id), hi = phi(id), fill = v < lo ? 0 : hi > lo ? (v - lo) * 51 / (hi - lo)
                                                              : 51;
  rect(x + 4, KNOB_Y + 50, fill, 3, c);
  rect(x + 4 + fill, KNOB_Y + 50, 51 - fill, 3, th->off);
}

/* ---- PATTERN: 9 rows of 16 steps */
#define GRID_Y 28
#define ROW_H 16
#define CELL_X 30
#define CELL_W 13
static uint32_t flash_until[TRACKS];

static void draw_label(int t) {
  int y = page == PG_MIX ? 28 : GRID_Y + t * ROW_H, x = page == PG_MIX ? 3 + t * 26 : 0, w = page == PG_MIX ? 22 : 27;
  int lit = t == track || flash_until[t];
  rect(x, y, w, 15, lit ? tcolor(t) : th->bg);
  text_center(x, w, y, track_short(t), lit ? th->bg : tcolor(t), FONT_SMALL);
}

/* a drum step is colored by its voice (kicks like the first track's color, snares the second's ...);
 * a synth's by its track */
static uint16_t voice_color(int t, int v) {
  static const uint8_t FAMILY[22] = {0, 0, 1, 1, 2, 2, 5, 5, 6, 3, 4, 4, 4, 7, 7, 3, 5, 5, 6, 6, 3, 2};
  int n = machine_voices(machine_of(t));
  if (song.mute >> t & 1) {
    return th->dim;
  }
  return n == 22 ? th->track[FAMILY[v % 22]] : n == 128 ? th->track[t]
                                                        : th->track[v % 8];
}

static void draw_cell(int t, int s) {
  int x = CELL_X + s * CELL_W, y = GRID_Y + t * ROW_H;
  pattern_t *p = pat();
  if (s >= song.length) {
    rect(x, y, CELL_W, 15, th->bg);
    return;
  }
  int f = p->flag[t][s], on = f & STEP_ON, acc = f & STEP_ACC;
  uint16_t c = !on ? ((s & 3) ? th->off : th->beat) : voice_color(t, p->val[t][s]);
  rect(x, y, CELL_W - 2, 15, c);
  rect(x + CELL_W - 2, y, 2, 15, th->bg);
  if (on && acc) {
    rect(x, y, CELL_W - 2, 3, th->text);
  }
  if (on && (f & STEP_SLIDE)) {
    rect(x + CELL_W - 2, y + 6, 2, 3, tcolor(t));
  }
  int r = RATCHET[(f & STEP_RAT) >> RAT_SHIFT];
  if (on && r > 1) { /* a ratchet: the cell cut in its hits (up to 4), x6 and x8 underlined too */
    int segs = r > 4 ? 4 : r;
    for (int k = 1; k < segs; k++) {
      rect(x + k * (CELL_W - 2) / segs, y + 3, 1, 12, th->bg);
    }
    if (r > 4) {
      rect(x, y + 13, CELL_W - 2, 2, th->text);
    }
  }
  if (t == track && s == cursor) {
    frame(x, y, CELL_W - 2, 15, th->text);
  }
}

static int playhead_drawn = -1;

static void draw_playhead(int s) {
  if (playhead_drawn >= 0) {
    rect(CELL_X + playhead_drawn * CELL_W, 174, CELL_W - 2, 4, th->bg);
  }
  playhead_drawn = s;
  if (s >= 0) {
    rect(CELL_X + s * CELL_W, 174, CELL_W - 2, 4, th->accent);
  }
}

static void draw_row(int t) {
  draw_label(t);
  for (int s = 0; s < STEPS; s++) {
    draw_cell(t, s);
  }
}

/* ---- SOUND: the track's name, its decay drawn, and what both knob pages hold */
static void draw_tabs(void) { /* a page's pages of knobs, as tabs: SEL or ALGORITHM moves on */
  int n = subpages(), w = n ? SCREEN_W / n : SCREEN_W;
  for (int i = 0; i < n; i++) {
    int x = i * w;
    rect(x, 160, w - 2, 20, i == sub ? th->accent : th->panel);
    text_center(x, w - 2, 162, subpage_name(i), i == sub ? th->bg : th->dim, FONT_SMALL);
  }
}

static void draw_sound(void) {
  rect(0, BODY_Y, SCREEN_W, BODY_H, th->bg);
  if (is_midi(track)) { /* where it goes */
    const track_t *d = &song.track[track];
    char s[24] = "CHANNEL ";
    text(8, 30, "MIDI OUT", th->track[track], FONT_BIG);
    format_int(s + 8, d->mch + 1);
    text(8, 76, s, th->text, FONT_SMALL);
    pformat(P_PROG, d->prog, s);
    text(8, 96, "PROGRAM", th->dim, FONT_SMALL);
    text(80, 96, s, th->text, FONT_SMALL);
    text(8, 116, "GATE", th->dim, FONT_SMALL);
    rect(80, 120, 150, 8, th->off);
    rect(80, 120, d->gate * 150 / 100, 8, th->track[track]);
  } else if (engine_of(track) >= 0) { /* the engine, its preset, its envelope */
    const track_t *d = &song.track[track];
    uint16_t c = th->track[track];
    text(8, 30, machine_name(machine_of(track)), c, FONT_BIG);
    text(8, 62 - 12 + 4, synth_preset_name(engine_of(track), d->preset), th->text, FONT_SMALL);
    if (synth_ownenv(engine_of(track))) {
      text(8, 100, "INTERNAL ENV", th->dim, FONT_SMALL);
      draw_tabs();
      return;
    }
    int x = 8, w = d->atk * 56 / 127 + 1, y0 = 140, top = 76, sus = y0 - d->sus * (y0 - top) / 127;
    for (int i = 0; i < w; i++, x++) { /* attack */
      int h = (y0 - top) * (i + 1) / w;
      rect(x, y0 - h, 1, h + 1, c);
    }
    w = d->dec * 56 / 127 + 1;
    for (int i = 0; i < w; i++, x++) { /* decay */
      int y = top + (sus - top) * (i + 1) / w;
      rect(x, y, 1, y0 - y + 1, c);
    }
    rect(x, sus, 48, y0 - sus + 1, c); /* sustain */
    x += 48;
    w = d->rel * 56 / 127 + 1;
    for (int i = 0; i < w; i++, x++) { /* release */
      int y = sus + (y0 - sus) * (i + 1) / w;
      rect(x, y, 1, y0 - y + 1, c);
    }
  } else if (machine_usr(machine_of(track))) {
    text(8, 30, "USER", th->track[track], FONT_BIG);
    text(8, 72, sampler_bank_name(song.track[track].extra & 3), th->text, FONT_BIG);
    text(8, 116, "NOTES FROM F3", th->dim, FONT_SMALL);
  } else { /* the machine, the voice, and how it rings */
    uint8_t h[224];
    int v = song.track[track].voice, x = text(8, 30, machine_name(machine_of(track)), th->track[track], FONT_BIG);
    text(x + 10, 30, voice_name(machine_of(track), v), voice_color(track, v), FONT_BIG);
    drum_envelope(track, v, h, 224);
    rect(8, 70, 224, 71, th->bg);
    for (int i = 0; i < 224; i++) {
      int hh = h[i] * 70 / 255;
      rect(8 + i, 140 - hh, 1, hh + 1, voice_color(track, v));
    }
  }
  draw_tabs();
}

/* ---- MIX: a strip per track */
static void draw_strip(int t) {
  int x = 3 + t * 26, h = song.track[t].level * 116 / 127;
  draw_label(t);
  rect(x, 48, 22, 116 - h, th->off);
  rect(x, 48 + 116 - h, 22, h, tcolor(t));
  rect(x, 166, 22, 15, th->bg);
  if (song.mute >> t & 1) {
    text_center(x, 22, 166, "M", th->text, FONT_SMALL);
  }
}

/* ---- FX: what each tab's knobs do, drawn */
static void draw_fx(void) {
  rect(0, BODY_Y, SCREEN_W, 160 - BODY_Y, th->bg);
  if (sub == 0) { /* SENDS: every track's three sends and its ducking */
    static const char *const ROW[4] = {"DLY", "REV", "CHO", "DCK"};
    for (int r = 0; r < 4; r++) {
      int y = 34 + r * 30;
      text(8, y + 4, ROW[r], th->dim, FONT_SMALL);
      for (int t = 0; t < TRACKS; t++) {
        const track_t *d = &song.track[t];
        int v = r == 0 ? d->send : r == 1 ? d->rsend
                               : r == 2   ? d->csend
                               : t        ? d->duck
                                          : 0,
            h = v * 24 / 127, x = 44 + t * 21;
        rect(x, y, 18, 24 - h, th->off);
        rect(x, y + 24 - h, 18, h, r == 3 && !t ? th->off : tcolor(t));
        if (t == track) {
          rect(x, y + 25, 18, 2, th->text);
        }
      }
    }
  } else if (sub == 1) { /* SPACE: the echoes, and the reverb's tail */
    text(8, 30, "DELAY", th->dim, FONT_SMALL);
    for (int s = 0; s < STEPS; s++) {
      rect(8 + s * 14, 96, 12, 2, (s & 3) ? th->off : th->beat);
    }
    int h = 48;
    rect(8, 94 - h, 10, h, th->text);
    h = h * 3 / 4;
    for (int s = song.dtime; s < STEPS && h > 1; s += song.dtime) {
      rect(8 + s * 14, 94 - h, 10, h, th->accent);
      h = h * song.dfdbk * 230 >> 15;
    }
    text(8, 104, "REVERB", th->dim, FONT_SMALL);
    int len = 40 + song.rsize * 180 / 127, dk = 256 - (12000 / len); /* how long it rings; DAMP darkens it */
    uint16_t c = song.rdamp > 80 ? th->dim : th->accent;
    for (int x = 0, a = 30 << 8; x < 224 && a > 255; x += 3, a = a * dk >> 8) {
      rect(8 + x, 152 - (a >> 8), 2, a >> 8, c);
    }
  } else if (sub == 2) { /* MOTION: the chorus's sweep, and how a duck comes back */
    text(8, 30, "CHORUS", th->dim, FONT_SMALL);
    int per = 200 - song.crate * 180 / 127, amp = 2 + song.cdepth * 18 / 127;
    for (int x = 0; x < 224; x++) {
      int p = x % per, tri = p < per / 2 ? p * 4 * amp / per - amp : (per - p) * 4 * amp / per - amp;
      rect(8 + x, 70 - tri, 1, 2, th->accent);
    }
    text(8, 100, "DUCK", th->dim, FONT_SMALL);
    int back = 4 + song.pump * 52 / 127; /* pixels to come back, per beat of 56 */
    for (int x = 0; x < 224; x++) {
      int p = x % 56, g = p >= back ? 40 : 4 + p * 36 / back;
      rect(8 + x, 152 - g, 1, g, (x % 56) < 2 ? tcolor(0) : th->off);
    }
  } else { /* MASTER: the filter's shape, and what the distortion does to a wave */
    text(8, 30, "FILTER", th->dim, FONT_SMALL);
    int f = song.filter, cut = f < 64 ? 4 + f * 100 / 63 : 4 + (f - 65) * 100 / 62, bump = song.reso * 20 / 127;
    for (int x = 0; x < 108; x++) {
      int d = f < 64 ? x - cut : cut - x, g = f == 64 ? 60 : d <= 0 ? 60
                                                                    : 60 - d * 2;
      if (f != 64 && d > -10 && d <= 0) {
        g += bump * (10 + d) / 10;
      }
      g = g < 1 ? 1 : g > 84 ? 84
                             : g;
      rect(8 + x, 150 - g, 1, g, th->accent);
    }
    text(128, 30, DIST_NAME[song.dtype % DISTS], th->dim, FONT_SMALL);
    rect(128, 101, 104, 1, th->off);
    for (int x = 0; x < 104; x++) {
      int32_t y = fx_curve((x - 52) * 32767 / 52);
      int yy = (y < -49151 ? -49151 : y > 49151 ? 49151
                                                : y) *
               48 / 49151;
      rect(128 + x, 101 - yy, 2, 2, th->text);
    }
  }
  draw_tabs();
}

/* ---- SCOPE: the output, from a rising zero crossing, 2 frames a pixel (~10 ms across) */
#define SCOPE_X 8
#define SCOPE_W 224
#define SCOPE_MID 100
static uint8_t scope_lo[SCOPE_W], scope_hi[SCOPE_W]; /* what each column has drawn */

static void draw_scope(int all) {
  static uint32_t next;
  uint32_t now = millis();
  if (!all && (int32_t)(now - next) < 0) {
    return;
  }
  next = now + 40;
  if (all) {
    rect(0, BODY_Y, SCREEN_W, 180 - BODY_Y, th->bg);
    for (int x = 0; x < SCOPE_W; x++) {
      scope_lo[x] = scope_hi[x] = SCOPE_MID;
    }
  }
  uint32_t end = scope_at, start = end - SCOPE_LEN + 1, at = end - SCOPE_W * 2; /* (no crossing: the latest) */
  for (uint32_t i = end - SCOPE_W * 2; i > start; i--) {
    if (scope[(i - 1) & (SCOPE_LEN - 1)] < 0 && scope[i & (SCOPE_LEN - 1)] >= 0) {
      at = i;
      break;
    }
  }
  rect(SCOPE_X, SCOPE_MID, SCOPE_W, 1, th->off);
  int prev = SCOPE_MID;
  for (int x = 0; x < SCOPE_W; x++) {
    int y = SCOPE_MID - scope[(at + (uint32_t)x * 2) & (SCOPE_LEN - 1)] * 68 / 32768;
    int lo = x ? (y < prev ? y : prev) : y, hi = x ? (y > prev ? y : prev) : y;
    prev = y;
    if (lo == scope_lo[x] && hi == scope_hi[x]) {
      continue;
    }
    rect(SCOPE_X + x, scope_lo[x], 1, scope_hi[x] - scope_lo[x] + 1, th->bg);
    if (scope_lo[x] <= SCOPE_MID && scope_hi[x] >= SCOPE_MID) {
      rect(SCOPE_X + x, SCOPE_MID, 1, 1, th->off);
    }
    rect(SCOPE_X + x, lo, 1, hi - lo + 1, th->accent);
    scope_lo[x] = (uint8_t)lo;
    scope_hi[x] = (uint8_t)hi;
  }
}

#define MLOG 9
static uint8_t mlog[MLOG][3]; /* the last messages in (midi_in() keeps them) */
static uint32_t mlog_n;

/* ---- MIDI IN: the last messages that came in, newest at the bottom, colored by the track they reach */
static void note_name(int n, char *o) {
  static const char NAME[12][3] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
  int i = 0;
  for (const char *p = NAME[n % 12]; *p; p++) {
    o[i++] = *p;
  }
  format_int(o + i, n / 12 - 1);
}

static void put(char *s, int *n, const char *a) {
  while (*a && *n < 30) {
    s[(*n)++] = *a++;
  }
  s[*n] = 0;
}

static void put_int(char *s, int *n, int v) {
  char d[12];
  format_int(d, v);
  put(s, n, d);
}

static void draw_midi(int all) {
  static uint32_t shown = ~0u;
  if (!all && shown == mlog_n) {
    return;
  }
  shown = mlog_n;
  rect(0, BODY_Y, SCREEN_W, 180 - BODY_Y, th->bg);
  if (!mlog_n) {
    text(8, 30, "NOTHING YET", th->dim, FONT_SMALL);
    text(8, 50, "CH 1-9: TRACKS 1-9", th->dim, FONT_SMALL);
    text(8, 70, "CH 10: SELECTED TRACK", th->dim, FONT_SMALL);
    text(8, 90, "CH 16: SONG (CC.md)", th->dim, FONT_SMALL);
    return;
  }
  int lines = mlog_n < MLOG ? (int)mlog_n : MLOG;
  for (int k = 0; k < lines; k++) {
    const uint8_t *m = mlog[(mlog_n - lines + k) % MLOG];
    int ch = m[0] & 15, kind = m[0] & 0xF0, n = 0;
    char s[32];
    s[0] = 0;
    put_int(s, &n, ch + 1);
    put(s, &n, ch < 9 ? "  " : " ");
    if (kind == 0x90 && m[2]) {
      char nn[8];
      note_name(m[1], nn);
      put(s, &n, "NOTE ");
      put(s, &n, nn);
      put(s, &n, " ");
      put_int(s, &n, m[2]);
    } else if (kind == 0x80 || kind == 0x90) {
      char nn[8];
      note_name(m[1], nn);
      put(s, &n, "OFF  ");
      put(s, &n, nn);
    } else if (kind == 0xB0) {
      put(s, &n, "CC ");
      put_int(s, &n, m[1]);
      put(s, &n, " = ");
      put_int(s, &n, m[2]);
    } else if (kind == 0xC0) {
      put(s, &n, "PROG ");
      put_int(s, &n, m[1]);
    } else if (kind == 0xE0) {
      put(s, &n, "BEND ");
      put_int(s, &n, (m[2] << 7 | m[1]) - 8192);
    } else if (kind == 0xA0 || kind == 0xD0) {
      put(s, &n, "PRESS ");
      put_int(s, &n, kind == 0xA0 ? m[2] : m[1]);
    }
    uint16_t c = ch < TRACKS ? th->track[ch] : ch == 9 ? th->track[track]
                                                       : th->accent;
    text(8, 28 + k * 17, s, k == lines - 1 ? c : th->dim, FONT_SMALL);
    rect(2, 32 + k * 17, 3, 9, c);
  }
}

/* ---- GLOBAL: the tempo, the theme, the load */
static void draw_cpu(void) { /* how much of the time the sound takes (on the FM-1: the emulator's clock is not real) */
  rect(120, 30, 116, 32, th->bg);
#ifdef __wasm__
  text(120, 30, "CPU: ON FM-1", th->dim, FONT_SMALL);
#else
  char s[24] = "CPU ";
  format_int(s + 4, cpu_load);
  int i = len(s);
  s[i++] = '%', s[i] = 0;
  text(120, 30, s, cpu_peak > 85 ? RGB(255, 60, 60) : th->text, FONT_SMALL);
  char p[24] = "PEAK ";
  format_int(p + 5, cpu_peak);
  i = len(p);
  p[i++] = '%', p[i] = 0;
  text(120, 46, p, cpu_peak > 85 ? RGB(255, 60, 60) : th->dim, FONT_SMALL);
#endif
}

static void draw_global(void) {
  char s[16];
  rect(0, BODY_Y, SCREEN_W, BODY_H, th->bg);
  format_int(s, song.bpm);
  text(8, 30, s, th->accent, FONT_BIG);
  text(8 + text_width(s, FONT_BIG) + 6, 46, "BPM", th->dim, FONT_SMALL);
  /* the theme: the screen is already in it (turning THEME recolors everything), so its name and colors */
  text(8, 84, "THEME", th->dim, FONT_SMALL);
  text(8, 100, th->name, th->accent, FONT_BIG);
  for (int k = 0; k < TRACKS; k++) {
    rect(8 + k * 25, 142, 22, 14, th->track[k]);
  }
  draw_tabs();
  draw_cpu();
}

static void draw_order(void) { /* 16 patterns; the chain is the marked run, the playing one is underlined */
  int a = song.chain_a, b = song.chain_b;
  rect(0, BODY_Y, SCREEN_W, BODY_H, th->bg);
  if (a > b) {
    int t = a;
    a = b;
    b = t;
  }
  for (int i = 0; i < PATTERNS; i++) {
    int col = i & 3, row = i >> 2, x = 6 + col * 58, y = 30 + row * 32;
    int in = song.chain_a != song.chain_b && i >= a && i <= b;
    int cur = i == song.pattern;
    char s[8];
    rect(x, y, 54, 28, cur ? th->accent : in ? th->panel : th->off);
    format_int(s, i + 1);
    text_center(x, 54, y + 6, s, cur ? th->bg : th->text, FONT_SMALL);
    if (playing && i == cur_pattern) {
      rect(x + 6, y + 22, 42, 3, cur ? th->bg : th->accent);
    }
  }
  text(8, 162, "SAVE+KEY COPY", th->dim, FONT_SMALL);
  text(130, 162, "2 KEYS CHAIN", th->dim, FONT_SMALL);
}

static void draw_sample(void) {
  int b = sampler_bank(), armed = sampler_slot(), recn = sampler_recording();
  rect(0, BODY_Y, SCREEN_W, BODY_H, th->bg);
  if (sampler_naming()) {
    text(8, 36, "NAME", th->dim, FONT_SMALL);
    text(8, 58, sampler_name_buf(), th->accent, FONT_BIG);
    text(8, 110, "WHITE A-P", th->text, FONT_SMALL);
    text(8, 128, "BLACK Q-Z SPACE", th->text, FONT_SMALL);
    text(8, 150, "OCT- DEL   SAVE OK", th->dim, FONT_SMALL);
    return;
  }
  text(8, 28, sampler_bank_name(b), th->accent, FONT_BIG);
  if (usb_audio_active()) {
    text(140, 40, "USB", th->text, FONT_SMALL);
  }
  for (int i = 0; i < SAMP_SLOTS; i++) {
    int col = i & 3, row = i >> 2, x = 6 + col * 58, y = 68 + row * 36;
    int note = sampler_slot_note(b, i);
    int hot = i == armed;
    char s[8];
    rect(x, y, 54, 32, hot ? (recn ? RGB(255, 60, 60) : th->accent) : note >= 0 ? th->panel : th->off);
    if (note >= 0) {
      note_name(note, s);
    } else {
      s[0] = '-', s[1] = '-', s[2] = 0;
    }
    text_center(x, 54, y + 8, s, hot ? th->bg : th->text, FONT_SMALL);
  }
  int vu = sampler_vu();
  rect(8, 148, 224, 8, th->off);
  rect(8, 148, vu * 224 / 255, 8, th->accent);
}

static void draw_body(void) {
  playhead_drawn = -1;
  rect(0, HEAD_H, SCREEN_W, KNOB_Y - HEAD_H, th->bg);
  if (page == PG_PATTERN) {
    for (int t = 0; t < TRACKS; t++) {
      draw_row(t);
    }
  } else if (page == PG_SOUND) {
    draw_sound();
  } else if (page == PG_MIX) {
    for (int t = 0; t < TRACKS; t++) {
      draw_strip(t);
    }
  } else if (page == PG_FX) {
    draw_fx();
  } else if (page == PG_SCOPE) {
    draw_scope(1);
  } else if (page == PG_MIDI) {
    draw_midi(1);
  } else if (page == PG_ORDER) {
    draw_order();
  } else if (page == PG_SAMPLE) {
    draw_sample();
  } else {
    draw_global();
  }
}

static void draw_knobs(void) {
  pick_knobs(knobs);
  for (int k = 0; k < 4; k++) {
    draw_knob(k);
  }
}

static void draw_all(void) {
  th = &THEMES[song.theme];
  clear(th->bg);
  draw_header();
  draw_body();
  draw_knobs();
}

/* ---- changing things */
static int dirty, save_pending;
static uint32_t stopped_at; /* a save waits for the sound to stop */
static uint32_t ask_until;  /* a question is up (clear the pattern?) until then: OCT+ yes, OCT- no */

static void set_page(int p) {
  if (p != page) {
    page = p;
    sub = 0;
    chain_key = -1;
    draw_all();
  }
}

static void cc_send(int id);
static void cc_dump(void);

static void set_track(int t) {
  if (t == track || t < 0 || t >= TRACKS) {
    return;
  }
  int old = track;
  track = t;
  cc_dump(); /* a controller on channel 10 follows the track */
  draw_track_chip();
  if (page == PG_PATTERN) { /* the cursor moves with it */
    draw_row(old);
    draw_row(t);
  } else if (page == PG_MIX) {
    draw_label(old);
    draw_label(t);
  } else if (page == PG_SOUND) {
    draw_sound();
  }
  draw_knobs();
}

static void changed(int id) { /* a parameter changed: show it where it shows (and a MIDI track: send it) */
  dirty = 1;
  cc_send(id);
  const track_t *d = &song.track[track];
  if (id >= P_CC0 && id <= P_CC3) { /* a controller, as it turns */
    midi_out(0xB0 | (d->mch & 15), d->ccn[id - P_CC0], d->cc[id - P_CC0]);
  } else if (id == P_PROG && d->prog) {
    midi_out(0xC0 | (d->mch & 15), d->prog - 1, 0);
  } else if (id >= P_CCN0 && id <= P_CCN3) { /* its value knob is named by it */
    for (int k = 0; k < 4; k++) {
      if (knobs[k] == id - P_CCN0 + P_CC0) {
        draw_knob(k);
      }
    }
  }
  if (id == P_THEME) {
    draw_all();
    return;
  }
  if (id == P_BPM || id == P_SWING || id == P_DTIME) {
    engine_tempo();
  }
  if (id == P_BPM) {
    draw_transport();
  }
  if ((id == P_STIME || id == P_SBANK) && page == PG_SAMPLE) {
    draw_sample();
  }
  if (id == P_MACHINE || id == P_VOICE || id == P_STVOICE) {
    draw_track_chip();
  }
  if (id == P_MACHINE) { /* another machine: other values for the controller (an engine's eight, its preset) */
    cc_dump();
  }
  if (id == P_MACHINE && page == PG_SOUND) { /* another kind of machine: other pages of knobs */
    sub = sub < subpages() ? sub : 0;
  }
  if (page == PG_PATTERN) {
    if (id == P_LENGTH || id == P_MUTE) {
      for (int t = 0; t < TRACKS; t++) {
        draw_row(t);
      }
    } else {
      draw_row(track);
    }
  } else if (page == PG_MIX) {
    draw_strip(track);
  } else if (page == PG_FX) {
    draw_fx();
  } else if (page == PG_SOUND || page == PG_GLOBAL) {
    if (id != P_LEVEL && id != P_SEND && id != P_DRIVE) {
      draw_body();
    }
  }
  if (id == P_STEP || id == P_MACHINE || id == P_VOICE) { /* the other knobs show this step, this machine */
    draw_knobs();
  } else {
    for (int k = 0; k < 4; k++) {
      if (knobs[k] == id) {
        draw_knob(k);
      }
    }
  }
}

/* detents turned this pass -> steps: a slow turn is 1 a detent, a fast spin up to 4 */
static int accel(int e, int32_t detents) {
  static uint32_t last[ENC_COUNT];
  uint32_t now = millis(), dt = now - last[e];
  if (!detents) {
    return 0;
  }
  last[e] = now;
  return detents * (dt < 25 ? 4 : dt < 60 ? 2
                                          : 1);
}

static void turn(int id, int e) {
  int d = encoder(e);
  if (!d || id == P_NONE) {
    return;
  }
  int lo = plo(id), hi = phi(id), v = pget(id);
  if (id == P_VOICE || id == P_STVOICE) {                                            /* one voice a detent, past the ones this machine has none of */
    int m = machine_of(track), n = v < 0 && d > 0 ? song.track[track].voice - 1 : v; /* from a rest: the track's voice */
    lo = id == P_STVOICE ? -1 : 0;
    if (machine_usr(m)) { /* every note is a slot; empty ones are silent, not skipped */
      n = v + (d > 0 ? 1 : -1);
    } else {
      do {
        n += d > 0 ? 1 : -1;
      } while (n >= 0 && n <= hi && !machine_has(m, n));
    }
    if (n >= lo && n <= hi && n != v) {
      pset(id, n);
      changed(id);
      if (n >= 0 && !is_synth(track)) {
        live_on(track, n, 100);
      }
    }
    return;
  }
  d = hi - lo > 20 ? accel(e, d) : d;
  int n = v + d < lo ? lo : v + d > hi ? hi
                                       : v + d;
  if (n != v) {
    pset(id, n);
    changed(id);
  }
}

/* ---- the keys: on PATTERN and MIX, white keys are steps / mutes and black keys tracks; else they play */
static int8_t white_of[KEY_COUNT], black_of[KEY_COUNT];

/* with REC: into the step playing, or (stopped) into the cursor's, which then moves on */
static void record(int t, int v, int vel) {
  int s = playing ? play_step : cursor;
  if (taking || !rec || s < 0) {
    return;
  }
  pat()->val[t][s] = (uint8_t)v;
  pat()->flag[t][s] = (uint8_t)(STEP_ON | (vel > 110 ? STEP_ACC : 0));
  if (!playing) {
    cursor = (cursor + 1) % song.length;
    draw_knobs();
  }
  if (page == PG_PATTERN) {
    draw_row(t);
  }
  dirty = 1;
}

/* a drum voice or a synth note, now; it is also what new steps get */
static void play(int t, int v, int vel) {
  live_on(t, v, vel);
  if (song.track[t].voice != v) {
    song.track[t].voice = (uint8_t)v;
    if (t == track) {
      draw_track_chip();
    }
  }
  record(t, v, vel);
}

/* the drum keys: up the white keys the usual kit, the black keys the rest; SMP: a sample a key */
static int key_voice(int t, int k) {
  static const int8_t ON_WHITE[16] = {0, 2, 9, 10, 5, 4, 6, 7, 8, 11, 12, 13, 15, 20, 21, 19};
  static const int8_t ON_BLACK[11] = {1, 3, 14, 16, 17, 18, -1, -1, -1, -1, -1};
  int m = machine_of(t), v;
  if (machine_voices(m) != 22) {
    return k < machine_voices(m) ? k : -1;
  }
  v = white_of[k] >= 0 ? ON_WHITE[white_of[k]] : ON_BLACK[black_of[k]];
  return v >= 0 && machine_has(m, v) ? v : -1;
}

/* the keys edit steps on PATTERN (until REC: then they play, as everywhere) and mutes on MIX */
static int keys_edit(void) { return page == PG_MIX || (page == PG_PATTERN && !rec); }

static void keys_pattern(void) {
  for (int k = 0; k < KEY_COUNT; k++) {
    if (!key_pressed(k)) {
      continue;
    }
    int w = white_of[k], b = black_of[k];
    if (b >= 0 && b < TRACKS) {
      set_track(b);
      if (!is_synth(b)) {
        live_on(b, song.track[b].voice, 100);
      }
    } else if (w >= 0 && w < song.length) { /* a rest, or the track's voice (note) */
      pattern_t *p = pat();
      if (p->flag[track][w] & STEP_ON) {
        p->flag[track][w] = 0;
      } else {
        p->val[track][w] = song.track[track].voice;
        p->flag[track][w] = STEP_ON;
      }
      int old = cursor;
      cursor = w;
      draw_cell(track, old);
      draw_cell(track, w);
      draw_knobs();
      dirty = 1;
    }
  }
}

static void keys_mix(void) {
  for (int k = 0; k < KEY_COUNT; k++) {
    if (!key_pressed(k)) {
      continue;
    }
    int w = white_of[k], b = black_of[k];
    if (b >= 0 && b < TRACKS) {
      set_track(b);
    } else if (w >= 0 && w < TRACKS) {
      song.mute ^= (uint16_t)(1u << w);
      dirty = 1;
      draw_strip(w);
      for (int i = 0; i < 4; i++) {
        if (knobs[i] == P_MUTE) {
          draw_knob(i);
        }
      }
    }
  }
}

/* a free take: notes while REC is held, timed from the first one. Drums are hits; keyed instruments stay open */
#define TAKE_MAX 192
typedef struct {
  uint8_t track, note, vel, open;
  uint16_t on_ms, dur;
} take_ev_t;
static take_ev_t take_ev[TAKE_MAX];
static int take_n;
static uint32_t take_t0, rec_down;

static void take_note_on(int t, int note, int vel) {
  uint32_t ms;
  if (!taking || take_n >= TAKE_MAX) {
    return;
  }
  if (!take_n) {
    take_t0 = millis();
  }
  ms = millis() - take_t0;
  if (ms > 65000u) {
    return;
  }
  take_ev[take_n].track = (uint8_t)t;
  take_ev[take_n].note = (uint8_t)note;
  take_ev[take_n].vel = (uint8_t)vel;
  take_ev[take_n].on_ms = (uint16_t)ms;
  take_ev[take_n].dur = 1;
  take_ev[take_n].open = 1;
  take_n++;
}

static void take_note_off(int t, int note) {
  uint32_t ms, d;
  int i;
  if (!taking || !take_n) {
    return;
  }
  ms = millis() - take_t0;
  if (ms > 65000u) {
    ms = 65000u;
  }
  for (i = take_n - 1; i >= 0; i--) {
    if (take_ev[i].open && take_ev[i].track == t && take_ev[i].note == note) {
      d = ms - take_ev[i].on_ms;
      take_ev[i].dur = (uint16_t)(d ? d : 1);
      take_ev[i].open = 0;
      return;
    }
  }
}

static int keyed_track(int t) { return is_synth(t) || machine_usr(machine_of(t)); }

static void keys_play(void) {
  static uint8_t held[KEY_COUNT], held_t[KEY_COUNT];
  int base = KEY_FIRST_NOTE + 12 * octave;
  int keyed = keyed_track(track);
  for (int k = 0; k < KEY_COUNT; k++) {
    if (key_pressed(k)) {
      if (keyed) {
        held[k] = (uint8_t)(base + k); /* a key stops the note it started, whatever the octave or track is now */
        held_t[k] = (uint8_t)track;
        if (taking) {
          take_note_on(track, held[k], 100);
        }
        play(track, held[k], 100);
      } else if (key_voice(track, k) >= 0) {
        int v = key_voice(track, k);
        if (taking) {
          take_note_on(track, v, 100);
          take_note_off(track, v); /* a drum hit, not a held note */
        }
        play(track, v, 100);
      }
    }
    if (key_released(k) && held[k]) {
      if (taking) {
        take_note_off(held_t[k], held[k]);
      }
      live_off(held_t[k], held[k]);
      held[k] = 0;
    }
  }
}

static void set_pattern(int pt);

static int white_held(int w) {
  uint32_t held = keys();
  for (int k = 0; k < KEY_COUNT; k++) {
    if (white_of[k] == w && (held >> k & 1u)) {
      return 1;
    }
  }
  return 0;
}

static void keys_order(void) {
  for (int k = 0; k < KEY_COUNT; k++) {
    int w = white_of[k];
    if (w < 0 || w >= PATTERNS) {
      continue;
    }
    if (key_pressed(k)) {
      if (button_held(BTN_SAVE)) {
        if (w != song.pattern) {
          song.pat[w] = song.pat[song.pattern];
          pasted = 1;
          save_pending = 0;
          dirty = 1;
          toast("COPIED");
          draw_order();
        }
      } else if (chain_key >= 0 && chain_key != w && white_held(chain_key)) {
        song.chain_a = (uint8_t)(chain_key < w ? chain_key : w);
        song.chain_b = (uint8_t)(chain_key < w ? w : chain_key);
        dirty = 1;
        toast("CHAIN");
        draw_order();
        draw_transport();
      } else {
        chain_key = (int8_t)w;
        set_pattern(w);
      }
    }
    if (key_released(k) && chain_key == w) {
      chain_key = -1;
    }
  }
}

static void keys_sample(void) {
  static const char LETTER[] = "QRSTUVWXYZ ";
  if (!sampler_naming()) {
    keys_play();
    return;
  }
  for (int k = 0; k < KEY_COUNT; k++) {
    int w, b;
    if (!key_pressed(k)) {
      continue;
    }
    w = white_of[k];
    b = black_of[k];
    if (w >= 0 && w < 16) {
      sampler_name_char('A' + w);
    } else if (b >= 0 && b < 11) {
      sampler_name_char(LETTER[b]);
    }
    draw_sample();
  }
}

/* ---- MIDI controllers (CC.md): channels 1..9 set tracks 1..9, channel 10 the track chosen, channel 16 the song.
 * A CC's 0..127 covers the parameter's whole range. With CC OUT on, a change made here is sent back, so a
 * controller's lights follow (not what came in: no echo) */
static const uint8_t TRACK_CC[][2] = {
    {7, P_LEVEL}, {9, P_MUTE}, {14, P_MACHINE}, {15, P_VOICE}, {3, P_PRESET}, {94, P_SEND}, {91, P_RSEND}, {93, P_CSEND}, {89, P_DUCK}, {20, P_SE0}, {21, P_SE1}, {22, P_SE2}, {23, P_SE3}, {24, P_SE4}, {25, P_SE5}, {26, P_SE6}, {27, P_SE7}, {28, P_TUNE}, {29, P_DECAY}, {30, P_TONE}, {31, P_EXTRA}, {73, P_ATK}, {75, P_DEC}, {70, P_SUS}, {72, P_REL}, {85, P_FENV}, {86, P_GATE}, {87, P_MONO}, {5, P_SLIDE}, {102, P_MCH}, {103, P_PROG}, {104, P_VELO}, {105, P_CC0}, {106, P_CC1}, {107, P_CC2}, {108, P_CC3}, {109, P_CCN0}, {110, P_CCN1}, {111, P_CCN2}, {112, P_CCN3}};
enum { X_PLAY = PARAMS,
       X_PATTERN,
       X_TRACK };
static const uint8_t SONG_CC[][2] = {
    {20, P_BPM}, {21, P_SWING}, {22, P_LENGTH}, {23, P_THEME}, {24, P_DTIME}, {25, P_DFDBK}, {26, P_RSIZE}, {27, P_RDAMP}, {28, P_CRATE}, {29, P_CDEPTH}, {30, P_PUMP}, {31, P_FILTER}, {85, P_RESO}, {86, P_DRIVE}, {87, P_DTYPE}, {88, P_CLOCK}, {102, X_PLAY}, {103, X_PATTERN}, {104, X_TRACK}};
#define N_TRACK_CC (int)(sizeof TRACK_CC / sizeof TRACK_CC[0])
#define N_SONG_CC (int)(sizeof SONG_CC / sizeof SONG_CC[0])
static int from_midi; /* the change came in as a CC: don't send it back */

static void set_playing(int on) {
  if (on != playing) {
    playing = (uint8_t)on;
    if (!on) {
      stopped_at = millis();
    }
  }
}

static void set_pattern(int pt) {
  if (pt != song.pattern && pt >= 0 && pt < PATTERNS) {
    char s[16] = "PATTERN ";
    song.pattern = (uint8_t)pt;
    format_int(s + 8, pt + 1);
    toast(s);
    dirty = 1;
    draw_transport();
    if (page == PG_PATTERN || page == PG_ORDER) {
      draw_body();
    }
    draw_knobs();
  }
}

static int has_param(int id) { /* the selected track's machine has it (a drum machine has no envelope ...) */
  int e = engine_of(track) >= 0, m = is_midi(track);
  if (is_se(id) || id == P_PRESET || (id >= P_ATK && id <= P_MONO) || id == P_FENV || id == P_SLIDE) {
    return e;
  }
  if (id == P_GATE) {
    return e || m;
  }
  if (id >= P_MCH && id <= P_CCN3) {
    return m;
  }
  if (id == P_DECAY || id == P_TONE || id == P_EXTRA) {
    return !e && !m;
  }
  if (id == P_VOICE || id == P_TUNE) {
    return !m;
  }
  return 1;
}

static int cc_of(int v, int lo, int hi) { return hi > lo ? ((v - lo) * 127 + (hi - lo) / 2) / (hi - lo) : 0; }

static void cc_send(int id) { /* the selected track's parameter (on its channel and 10), or the song's (16) */
  if (!song.ccout || from_midi) {
    return;
  }
  for (int i = 0; i < N_TRACK_CC; i++) {
    if (TRACK_CC[i][1] == id && has_param(id)) {
      int v = cc_of(pget(id), plo(id), phi(id));
      midi_out(0xB0 | track, TRACK_CC[i][0], v);
      midi_out(0xB9, TRACK_CC[i][0], v);
      return;
    }
  }
  for (int i = 0; i < N_SONG_CC; i++) {
    if (SONG_CC[i][1] == id) {
      midi_out(0xBF, SONG_CC[i][0], cc_of(pget(id), plo(id), phi(id)));
      return;
    }
  }
}

static void cc_dump(void) { /* all of the selected track's, on channel 10 */
  if (!song.ccout) {
    return;
  }
  for (int i = 0; i < N_TRACK_CC; i++) {
    int id = TRACK_CC[i][1];
    if (!has_param(id)) {
      continue;
    }
    midi_out(0xB9, TRACK_CC[i][0], cc_of(pget(id), plo(id), phi(id)));
  }
}

static void cc_in(int ch, int cc, int val) {
  int t = ch < TRACKS ? ch : ch == 9 ? track
                                     : -1,
      id = -1;
  for (int i = 0; t >= 0 && i < N_TRACK_CC; i++) {
    id = TRACK_CC[i][0] == cc ? TRACK_CC[i][1] : id;
  }
  for (int i = 0; ch == 15 && i < N_SONG_CC; i++) {
    id = SONG_CC[i][0] == cc ? SONG_CC[i][1] : id;
  }
  if (id < 0) {
    return;
  }
  if (id == X_PLAY) {
    set_playing(val >= 64);
    return;
  }
  if (id == X_PATTERN) {
    set_pattern(val * PATTERNS / 128);
    return;
  }
  if (id == X_TRACK) {
    set_track(val * TRACKS / 128);
    return;
  }
  int was = track;
  track = t >= 0 ? t : track;
  if (!has_param(id)) {
    track = was;
    return;
  }
  int lo = plo(id), hi = phi(id), v = lo + (val * (hi - lo) + 63) / 127;
  if (id == P_VOICE) { /* the nearest voice the machine has */
    int m = machine_of(track), n = v;
    for (int d = 0; d <= hi; d++) {
      if (v - d >= 0 && machine_has(m, v - d)) {
        n = v - d;
        break;
      }
      if (v + d <= hi && machine_has(m, v + d)) {
        n = v + d;
        break;
      }
    }
    v = n;
  }
  char s[24];
  if (v != pget(id)) {
    pset(id, v);
    from_midi = 1;
    if (track == was) {
      changed(id);
    } else { /* another track: what changed() does for it, then it is shown where it shows */
      const track_t *d = &song.track[track];
      dirty = 1;
      if (id >= P_CC0 && id <= P_CC3) {
        midi_out(0xB0 | (d->mch & 15), d->ccn[id - P_CC0], d->cc[id - P_CC0]);
      } else if (id == P_PROG && d->prog) {
        midi_out(0xC0 | (d->mch & 15), d->prog - 1, 0);
      }
      int other = track;
      track = was;
      if (page == PG_PATTERN) {
        draw_row(other);
      } else if (page == PG_MIX) {
        draw_strip(other);
      } else if (page == PG_FX && !sub) {
        draw_fx();
      }
      track = other;
    }
    from_midi = 0;
  }
  /* what it was, as it is now: "3 CUTOFF 64" */
  int n = 0;
  if (t >= 0) {
    s[n++] = (char)('1' + track), s[n++] = ' ';
  }
  for (const char *p = pname(id); *p && n < 12; p++) {
    s[n++] = *p;
  }
  s[n++] = ' ';
  char val_s[24];
  pformat(id, pget(id), val_s);
  for (int i = 0; val_s[i] && n < 23; i++) {
    s[n++] = val_s[i];
  }
  s[n] = 0;
  track = was;
  toast(s);
}

/* MIDI channels 1..9 play tracks 1..9, channel 10 the track chosen; a drum machine takes General MIDI
 * drum keys, a synth notes */
static void midi_in(void) {
  static const int8_t GM[47] = {1, 0, 4, 2, 5, 3, 6, 9, 6, 9, 7, 10, 7, 8, 11, 8, 12, 11, 12, 15, 11, 13, 11, -1, 12,
                                18, 17, 18, 18, 16, 19, 19, 13, 14, 20, 20, -1, -1, -1, -1, 21, 21, 21, -1, -1, -1, -1};
  uint8_t st, d1, d2;
  while (midi_read(&st, &d1, &d2)) {
    if (st < 0xF0) { /* for the MIDI IN page (not clock and the like: they come too often) */
      mlog[mlog_n % MLOG][0] = st, mlog[mlog_n % MLOG][1] = d1, mlog[mlog_n % MLOG][2] = d2;
      mlog_n++;
    }
    int ch = st & 0x0F, t = ch < TRACKS ? ch : ch == 9 ? track
                                                       : -1;
    int on = (st & 0xF0) == 0x90 && d2, off = (st & 0xF0) == 0x80 || ((st & 0xF0) == 0x90 && !d2);
    if ((st & 0xF0) == 0xB0) {
      cc_in(ch, d1, d2);
      continue;
    }
    if (t < 0) {
      continue;
    }
    if (is_synth(t) || machine_usr(machine_of(t))) {
      if (on) {
        if (taking) {
          take_note_on(t, d1, d2);
        }
        play(t, d1, d2);
      } else if (off) {
        if (taking) {
          take_note_off(t, d1);
        }
        live_off(t, d1);
      }
    } else if (on) {
      int v = d1 >= 35 && d1 <= 81 ? GM[d1 - 35] : -1;
      if (machine_voices(machine_of(t)) != 22) {
        v = d1 - 36; /* SMP: a sample a key from C */
      }
      if (v >= 0 && machine_has(machine_of(t), v)) {
        if (taking) {
          take_note_on(t, v, d2);
          take_note_off(t, v);
        }
        play(t, v, d2);
      }
    }
  }
}

/* ---- the song to start with */
/* "b...s..." -> a drum track's steps: a letter a voice, capitals accented, . a rest */
static void drum_steps(pattern_t *p, int t, const char *s) {
  static const char CODE[] = "b?s?rplmhcoyiw?tg?jeak"; /* BD . SD . RS CP LT MT HT CH OH CY rIde coWbell . Tamb conGa . ... */
  for (int i = 0; i < STEPS && s[i]; i++) {
    char c = s[i] >= 'A' && s[i] <= 'Z' ? (char)(s[i] + 32) : s[i];
    for (int v = 0; CODE[v]; v++) {
      if (CODE[v] == c) {
        p->val[t][i] = (uint8_t)v;
        p->flag[t][i] = (uint8_t)(STEP_ON | (c != s[i] ? STEP_ACC : 0));
      }
    }
  }
}

/* a synth track's steps: the notes, then "x" on, "a" accented, "s" sliding into the next */
static void note_steps(pattern_t *p, int t, const uint8_t *notes, const char *s) {
  for (int i = 0; i < STEPS && s[i]; i++) {
    p->val[t][i] = notes[i];
    p->flag[t][i] = (uint8_t)(s[i] == '.' ? 0 : STEP_ON | (s[i] == 'a' ? STEP_ACC : 0) | (s[i] == 's' ? STEP_SLIDE : 0));
  }
}

static void fx_defaults(void) {
  song.rsize = 80;
  song.rdamp = 50;
  song.crate = 20;
  song.cdepth = 60;
  song.pump = 40;
  song.filter = 64;
  song.reso = 20;
  song.dtype = D_SOFT;
  song.ccout = 0;
}

static void default_song(void) {
  /* a machine per track (a ROM one if it is built in, else the modeled one after it) */
  static const char *const MACHINE[TRACKS][2] = {{"909", "909"}, {"808", "808"}, {"909", "909"}, {"707", "CR78"}, {"DMX", "808"}, {"SDS", "SDS"}, {"606", "CR78"}, {"ANALOG", "ANALOG"}, {"ANALOG", "ANALOG"}};
  static const uint8_t VOICE[TRACKS] = {0, 5, 9, 15, 2, 6, 9, 69, 33};
  /* one pattern, as plain as a beat gets: a kick on the beat, a clap on 2 and 4, hats between, a one-note bass */
  static const char *const BEAT[3] = {"b...b...b...b...", "....p.......p...", "..c...c...c...c."};
  static const uint8_t BASS[STEPS] = {33, 33, 33, 33, 33, 33, 33, 33, 33, 33, 33, 33, 33, 33, 33, 33};
  for (uint32_t i = 0; i < sizeof song; i++) {
    ((uint8_t *)&song)[i] = 0;
  }
  song.version = SONG_VERSION;
  song.bpm = 120;
  song.swing = 0;
  song.length = 16;
  song.dtime = 3;
  song.dfdbk = 60;
  song.drive = 24;
  fx_defaults();
  for (int t = 0; t < TRACKS; t++) {
    track_t *d = &song.track[t];
    d->machine = (uint8_t)machine_find(MACHINE[t][0], machine_find(MACHINE[t][1], 0));
    d->voice = VOICE[t];
    d->tune = d->decay = d->tone = d->extra = 64;
    d->level = 100;
    d->send = 0;
    d->slide = 40;
    d->mch = (uint8_t)t; /* MIDI out (when the track is that): its own channel, the usual controllers */
    d->gate = 50;
    d->velo = 100;
    for (int k = 0; k < 4; k++) {
      static const uint8_t CCN[4] = {74, 71, 1, 10}, CC[4] = {64, 0, 0, 64};
      d->ccn[k] = CCN[k];
      d->cc[k] = CC[k];
    }
  }
  synth_load(7, 0); /* track 8: ANALOG's SAW LEAD, ready (no steps) */
  song.track[7].level = 70;
  for (int k = 0; k < synth_presets(machine_engine(song.track[8].machine)); k++) { /* track 9: ANALOG's SUB BASS */
    const char *n = synth_preset_name(machine_engine(song.track[8].machine), k);
    if (n[0] == 'S' && n[1] == 'U' && n[2] == 'B') {
      synth_load(8, k);
    }
  }
  song.track[8].level = 90;
  for (int t = 0; t < 3; t++) {
    drum_steps(&song.pat[0], t, BEAT[t]);
  }
  note_steps(&song.pat[0], 8, BASS, "x..x..x...x..x..");
}

static void sanitize(void) {
  for (int id = 0; id < PARAMS; id++) { /* each saved value back into its range */
    for (int t = 0; t < TRACKS; t++) {
      int was = track;
      track = t;
      uint8_t *b = pbyte(id);
      if (b && id != P_PRESET) {
        *b = (uint8_t)(*b < PARAM[id].lo ? PARAM[id].lo : *b > phi(id) ? phi(id)
                                                                       : *b);
      }
      track = was;
    }
  }
  song.pattern &= PATTERNS - 1;
  if (song.chain_a >= PATTERNS) {
    song.chain_a = 0;
  }
  if (song.chain_b >= PATTERNS) {
    song.chain_b = song.chain_a;
  }
  for (int t = 0; t < TRACKS; t++) { /* a synth engine's own */
    if (engine_of(t) >= 0) {
      for (int k = 0; k < ENGINE_PARAMS; k++) {
        synth_param_t sp;
        int8_t *e = &song.track[t].e[k];
        synth_param(t, k, &sp);
        *e = (int8_t)(*e < sp.lo ? sp.lo : *e > sp.hi ? sp.hi
                                                      : *e);
      }
    }
  }
  for (int p = 0; p < PATTERNS; p++) { /* a voice this machine has no number for: a rest */
    for (int t = 0; t < TRACKS; t++) {
      for (int s = 0; s < STEPS; s++) {
        if (song.pat[p].val[t][s] >= machine_voices(machine_of(t))) {
          song.pat[p].flag[t][s] = 0;
        }
      }
    }
  }
}

void setup(void) {
  for (int k = 0, w = 0, b = 0; k < KEY_COUNT; k++) {
    static const uint8_t IS_BLACK[12] = {0, 1, 0, 1, 0, 1, 0, 0, 1, 0, 1, 0}; /* from F */
    white_of[k] = IS_BLACK[k % 12] ? -1 : (int8_t)w++;
    black_of[k] = IS_BLACK[k % 12] ? (int8_t)b++ : -1;
  }
  if (settings_load(&song, sizeof song) || song.version != SONG_VERSION) {
    /* a version 6 song: the same, without the effects at the end */
    if (!settings_load(&song, (uint32_t) __builtin_offsetof(song_t, rsize)) && song.version == 6) {
      song.version = SONG_VERSION;
      fx_defaults();
    } else {
      default_song();
    }
  }
  sanitize();
  sampler_init();
  engine_tempo();
  draw_all();
}

/* ---- the LEDs */
static void leds(void) {
  pattern_t *p = pat();
  uint16_t steps = 0;
  for (int s = 0; s < STEPS; s++) {
    steps |= (uint16_t)((p->flag[track][s] & STEP_ON) << s);
  }
  uint32_t held = keys();
  for (int k = 0; k < KEY_COUNT; k++) {
    int w = white_of[k], b = black_of[k], on;
    if (keys_edit()) {
      on = b >= 0           ? b == track
           : page == PG_MIX ? (w < TRACKS && !(song.mute >> w & 1))
                            : w < song.length && ((steps >> w & 1) ^ (w == play_step));
    } else {
      on = held >> k & 1;
    }
    led_key(k, on);
  }
  for (int i = 0; i < PAGES; i++) {
    led_button(PAGE_BTN[i], PAGE_BTN[i] == PAGE_BTN[page]);
  }
  led_button(BTN_ENV, ask_until != 0);
  led_button(BTN_ARP, page == PG_PATTERN && (pat()->flag[track][cursor] & (STEP_ON | STEP_RAT)) > STEP_ON);
  led_button(BTN_SEL, page == PG_PATTERN ? is_synth(track) && pget(P_STSLIDE) : subpages() > 1);
  led_button(BTN_PLAY, playing && (play_step & 3) != 3);
  led_button(BTN_REC, rec || taking || sampler_recording());
  led_button(BTN_SAVE, dirty);
  led_button(BTN_OCT_DOWN, ask_until ? 1 : octave < 0); /* both lit: they answer */
  led_button(BTN_OCT_UP, ask_until ? 1 : octave > 0);
}

static int take_bars(uint32_t ms, int room, int target, int *bpm_out) {
  static const int CAND[5] = {1, 2, 4, 8, 16};
  int best = 1, best_bpm = target, best_err = 100000, c;
  if (room < 1) {
    room = 1;
  }
  if (ms < 1u) {
    ms = 1u;
  }
  for (c = 0; c < 5; c++) {
    int b = CAND[c], bp, err;
    if (b > room) {
      continue;
    }
    bp = (int)((uint32_t)b * 4u * 60000u / ms);
    if (bp < 40) {
      bp = 40;
    }
    if (bp > 250) {
      bp = 250;
    }
    err = bp - target;
    if (err < 0) {
      err = -err;
    }
    if (err < best_err) {
      best_err = err;
      best = b;
      best_bpm = bp;
    }
  }
  *bpm_out = best_bpm;
  return best;
}

static void commit_take(void) { /* the held performance, onto 16ths, across as many patterns as it needs */
  static const int CAND[5] = {1, 2, 4, 8, 16};
  uint32_t end = 1, step_ms;
  uint16_t used = 0;
  int bars, bpm, room, i;
  char msg[16];
  for (i = 0; i < take_n; i++) {
    uint32_t d = take_ev[i].open ? millis() - take_t0 - take_ev[i].on_ms : take_ev[i].dur;
    uint32_t e;
    if (d > 65000u) {
      d = 65000u;
    }
    if (take_ev[i].open) {
      take_ev[i].dur = (uint16_t)(d ? d : 1);
      take_ev[i].open = 0;
    }
    e = (uint32_t)take_ev[i].on_ms + take_ev[i].dur;
    if (e > end) {
      end = e;
    }
    used = (uint16_t)(used | (1u << take_ev[i].track));
  }
  room = PATTERNS - song.pattern;
  bpm = song.bpm < 40 ? 40 : song.bpm;
  if (!playing) {
    bars = take_bars(end, room, bpm, &bpm);
    song.bpm = (uint8_t)bpm;
    engine_tempo();
  } else {
    uint32_t grid = 60000u / (uint32_t)bpm / 4u, steps;
    int snapped = 1, need;
    if (!grid) {
      grid = 1;
    }
    steps = (end + grid - 1u) / grid;
    need = (int)((steps + 15u) / 16u);
    if (need < 1) {
      need = 1;
    }
    for (i = 0; i < 5; i++) {
      if (CAND[i] <= room) {
        snapped = CAND[i];
      }
      if (CAND[i] >= need && CAND[i] <= room) {
        snapped = CAND[i];
        break;
      }
    }
    bars = snapped;
  }
  song.take_bpm = (uint8_t)bpm;
  step_ms = 60000u / (uint32_t)bpm / 4u;
  if (!step_ms) {
    step_ms = 1;
  }
  for (i = 0; i < bars && song.pattern + i < PATTERNS; i++) {
    int pi = song.pattern + i, t, s;
    for (t = 0; t < TRACKS; t++) {
      if (!(used & (1u << t))) {
        continue;
      }
      for (s = 0; s < STEPS; s++) {
        song.pat[pi].flag[t][s] = 0;
        song.pat[pi].val[t][s] = 0;
      }
    }
  }
  for (i = 0; i < take_n; i++) {
    uint32_t step = (take_ev[i].on_ms + step_ms / 2u) / step_ms;
    int pi, si, tr;
    if (step >= (uint32_t)bars * STEPS) {
      step = (uint32_t)bars * STEPS - 1u;
    }
    pi = song.pattern + (int)(step / STEPS);
    si = (int)(step % STEPS);
    tr = take_ev[i].track;
    if (pi >= PATTERNS || tr >= TRACKS) {
      continue;
    }
    song.pat[pi].val[tr][si] = take_ev[i].note;
    song.pat[pi].flag[tr][si] = (uint8_t)(STEP_ON | (take_ev[i].vel > 110 ? STEP_ACC : 0) | (take_ev[i].dur > step_ms ? STEP_SLIDE : 0));
    song.track[tr].voice = take_ev[i].note;
  }
  song.chain_a = song.pattern;
  song.chain_b = (uint8_t)(song.pattern + bars - 1);
  dirty = 1;
  format_int(msg, bpm);
  i = 0;
  while (msg[i]) {
    i++;
  }
  msg[i++] = ' ';
  format_int(msg + i, bars);
  while (msg[i]) {
    i++;
  }
  msg[i++] = 'B';
  msg[i++] = 'A';
  msg[i++] = 'R';
  msg[i] = 0;
  toast(msg);
  draw_transport();
  if (page == PG_PATTERN || page == PG_ORDER) {
    draw_body();
  }
  draw_knobs();
}

void loop(void) {
  static int8_t step_drawn = -1;
  static uint8_t pattern_drawn, playing_drawn;
  static uint16_t hits_seen[TRACKS];
  uint32_t now = millis();

  /* the buttons */
  if (button_pressed(BTN_SEQ)) {
    set_page(PG_PATTERN);
  }
  if (button_pressed(BTN_EDIT)) {
    set_page(PG_SOUND);
  }
  if (button_pressed(BTN_HOME)) { /* MIX, PATTERNS, SCOPE, MIDI IN, then MIX */
    set_page(page == PG_MIX ? PG_ORDER : page == PG_ORDER ? PG_SCOPE : page == PG_SCOPE ? PG_MIDI : PG_MIX);
  }
  if (button_pressed(BTN_LFO)) {
    set_page(PG_SAMPLE);
  }
  if (button_pressed(BTN_FX)) {
    set_page(PG_FX);
  }
  if (button_pressed(BTN_GLO)) {
    set_page(PG_GLOBAL);
  }
  int answered = 0;                              /* an OCT press that answered a question does not change the octave */
  if (ask_until && button_pressed(BTN_OCT_UP)) { /* yes: clear the pattern */
    pattern_t *p = pat();
    for (uint32_t i = 0; i < sizeof p->flag; i++) {
      (&p->flag[0][0])[i] = 0;
    }
    ask_until = 0;
    answered = dirty = 1;
    toast("CLEARED");
    if (page == PG_PATTERN) {
      draw_body();
    }
    draw_knobs();
  } else if (ask_until && (button_pressed(BTN_OCT_DOWN) || (int32_t)(now - ask_until) >= 0)) { /* no */
    answered = button_pressed(BTN_OCT_DOWN);
    ask_until = 0;
    toast("KEPT");
  }
  if (button_pressed(BTN_ENV)) { /* clear the pattern? */
    ask_until = now + 4000;
    toast("CLEAR? OCT+");
    toast_until = ask_until;
  }
  if (button_pressed(BTN_ARP) && page == PG_PATTERN) { /* the cursor's step: its ratchet, x1 .. x8, round */
    uint8_t *f = &pat()->flag[track][cursor];
    int r = ((*f & STEP_RAT) >> RAT_SHIFT) + 1;
    r = r >= RATCHETS ? 0 : r;
    if (!(*f & STEP_ON)) { /* on a rest: the step comes on with the track's voice */
      pat()->val[track][cursor] = song.track[track].voice;
      *f |= STEP_ON;
      r = 1;
    }
    *f = (uint8_t)((*f & ~STEP_RAT) | r << RAT_SHIFT);
    char m[16] = "RATCHET X";
    format_int(m + 9, RATCHET[r]);
    toast(m);
    draw_cell(track, cursor);
    dirty = 1;
  }
  if (button_pressed(BTN_SEL) && page == PG_PATTERN && is_synth(track)) { /* slide the cursor's step into the next */
    int v = !pget(P_STSLIDE);
    pset(P_STSLIDE, v);
    changed(P_STSLIDE);
    toast(v ? "SLIDE ON" : "SLIDE OFF");
  }
  if (button_pressed(BTN_SEL) && subpages() > 1) {
    sub = (sub + 1) % subpages();
    draw_knobs();
    if (page == PG_SOUND) {
      draw_sound();
    } else if (page == PG_GLOBAL) {
      draw_global();
    } else if (page == PG_FX) {
      draw_fx();
    }
  }
  if (button_pressed(BTN_PLAY)) {
    set_playing(!playing);
  }
  if (button_pressed(BTN_REC)) {
    if (page == PG_SAMPLE && !sampler_naming()) {
      if (!sampler_recording()) {
        if (sampler_armed() == 0) {
          toast("ERASING");
        }
        toast(sampler_start() ? "RECORDING" : "NO ROOM");
        draw_sample();
      }
      draw_transport();
    } else if (!taking) {
      taking = 1;
      take_n = 0;
      rec_down = now;
      draw_transport();
    }
  }
  if (button_pressed(BTN_SAVE)) {
    if (page == PG_SAMPLE) {
      if (sampler_naming()) {
        sampler_name_save();
        toast("NAMED");
      } else {
        sampler_begin_name();
      }
      draw_sample();
    } else if (playing) {
      pasted = 0;
      save_pending = 1;
      toast("SAVE ON STOP");
    } else {
      pasted = 0;
      save_pending = 1;
    }
  }
  if (page == PG_SAMPLE && sampler_naming() && !ask_until && button_pressed(BTN_OCT_DOWN)) {
    sampler_name_back();
    draw_sample();
  }
  int naming = page == PG_SAMPLE && sampler_naming();
  int oct = ask_until || answered || naming ? octave : octave + button_pressed(BTN_OCT_UP) - button_pressed(BTN_OCT_DOWN);
  if (oct != octave && oct >= -3 && oct <= 2) {
    char s[16] = "OCTAVE ";
    octave = oct;
    format_int(s + 7, octave);
    toast(s);
  }

  /* the encoders: the same on every page */
  int t = track + encoder(ENC_SELECT);
  set_track(t < 0 ? 0 : t >= TRACKS ? TRACKS - 1
                                    : t);
  set_pattern(song.pattern + encoder(ENC_PRESETS));
  if (page == PG_SOUND || page == PG_FX || page == PG_GLOBAL) {
    int d = encoder(ENC_ALGORITHM), n = subpages();
    if (d && n > 1) {
      sub = (sub + (d > 0 ? 1 : n - 1)) % n;
      draw_knobs();
      if (page == PG_SOUND) {
        draw_sound();
      } else if (page == PG_GLOBAL) {
        draw_global();
      } else {
        draw_fx();
      }
    }
  } else {
    turn(P_BPM, ENC_ALGORITHM);
  }
  for (int k = 0; k < 4; k++) {
    turn(knobs[k], ENC_KNOB1 + k);
  }

  /* the keys and MIDI */
  if (page == PG_PATTERN && !rec && !taking) {
    keys_pattern();
  } else if (page == PG_MIX) {
    keys_mix();
  } else if (page == PG_ORDER) {
    keys_order();
  } else if (page == PG_SAMPLE) {
    keys_sample();
  } else {
    keys_play();
  }
  midi_in();
  if (taking && button_released(BTN_REC)) {
    uint32_t held_ms = now - rec_down;
    taking = 0;
    if (take_n > 0) {
      commit_take();
    } else if (held_ms < 300) {
      rec = !rec;
      toast(rec ? "REC ON" : "REC OFF");
      draw_transport();
    } else {
      draw_transport();
    }
  }
  {
    char msg[16];
    if (sampler_flush(msg)) {
      toast(msg);
      if (page == PG_SAMPLE) {
        draw_sample();
      }
    }
  }
  if (page == PG_SCOPE) {
    draw_scope(0);
  } else if (page == PG_MIDI) {
    draw_midi(0);
  } else if (page == PG_SAMPLE) {
    static uint32_t sample_at;
    if (now - sample_at > 50) {
      sample_at = now;
      draw_sample();
    }
  }

  /* what the sound side did */
  for (int i = 0; i < TRACKS; i++) {
    if (hits[i] != hits_seen[i]) {
      hits_seen[i] = hits[i];
      if (!flash_until[i] && (page == PG_PATTERN || page == PG_MIX)) {
        flash_until[i] = now + 70;
        draw_label(i);
      }
    } else if (flash_until[i] && (int32_t)(now - flash_until[i]) >= 0) {
      flash_until[i] = 0;
      if (page == PG_PATTERN || page == PG_MIX) {
        draw_label(i);
      }
    }
  }
  int8_t ps = play_step;
  if (ps != step_drawn) {
    step_drawn = ps;
    if (page == PG_PATTERN) {
      draw_playhead(ps);
    }
  }
  if (cur_pattern != pattern_drawn || playing != playing_drawn) {
    pattern_drawn = cur_pattern;
    playing_drawn = playing;
    draw_transport();
  }
  static uint32_t cpu_at;
  if (page == PG_GLOBAL && now - cpu_at > 500) {
    cpu_at = now;
    draw_cpu();
  }
  if (toast_until && (int32_t)(now - toast_until) >= 0) {
    toast_until = 0;
    draw_track_chip();
  }

  /* saving stalls the sound: only when stopped and quiet */
  if (save_pending && !playing && !button_held(BTN_SAVE) && now - stopped_at > 600) {
    save_pending = 0;
    if (!pasted) {
      toast(settings_save(&song, sizeof song) ? "SAVE FAILED" : "SAVED");
      dirty = 0;
    }
    pasted = 0;
  }
  leds();
}
