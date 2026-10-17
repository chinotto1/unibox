/* UNIBOX: what the screen (main.c) and the sound (engine.c, drums.c, synths.c, sampler.c) share. */
#ifndef GROOVE_H
#define GROOVE_H
#include "fm1.h"

#define TRACKS 9 /* each plays one machine: a drum machine, or a synth engine */
#define STEPS 16
#define PATTERNS 16
#define SONG_VERSION 8
#define ENGINE_PARAMS 16 /* a synth engine's own parameters: up to four tabs of four */

/* a step: what plays (a drum machine's voice, or a synth's MIDI note) and how */
#define STEP_ON 1
#define STEP_ACC 2
#define STEP_SLIDE 4 /* a synth: glide into the next step */
#define STEP_RAT 0x38 /* a ratchet: the step played RATCHET[(flag & STEP_RAT) >> 3] times across it */
#define RAT_SHIFT 3
static const uint8_t RATCHET[8] = {1, 2, 3, 4, 6, 8, 8, 8};
#define RATCHETS 6
#define GATE_TIE 101 /* GATE: a note held until the track's next one */

/* a track. Every knob value is 0..127 (64 = as the machine has it) unless said otherwise */
typedef struct {
  uint8_t machine, voice; /* voice: what new steps get (a drum voice, or a synth's note) */
  uint8_t level, send; /* send: to the delay */
  uint8_t tune, decay, tone, extra;     /* a drum machine's (a synth: TUNE) */
  uint8_t slide;                        /* a synth's glide */
  uint8_t rsend, csend, duck;           /* sends to the reverb and the chorus; how much track 1 ducks it */
  int8_t e[ENGINE_PARAMS];              /* a synth engine's own parameters (synth_param) */
  uint8_t atk, dec, sus, rel;           /* its envelope */
  int8_t fenv;                          /* its envelope's sweep of the filter, -64..63 */
  uint8_t ftype, preset, mono;          /* ANALOG's filter type; the preset it came from; 1: one voice, legato */
  uint8_t mch, prog, gate, velo;        /* MIDI: the channel out (0..15), the program (0: none, else 1 + it), velocity;
                                         * GATE (a synth's and MIDI's): % of the step a note is held, GATE_TIE: to the next */
  uint8_t cc[4], ccn[4];                /* MIDI: four controllers' values and numbers */
} track_t;

typedef struct {
  uint8_t val[TRACKS][STEPS];  /* the voice or the note */
  uint8_t flag[TRACKS][STEPS]; /* STEP_* */
} pattern_t;

/* the whole song: it is the saved settings too */
typedef struct {
  uint8_t version, bpm, swing, length, theme, dtime, dfdbk, drive;
  uint16_t mute; /* bit t: track t is silent */
  uint8_t pattern, clock; /* clock: 1 sends MIDI clock (and start / stop) */
  track_t track[TRACKS];
  pattern_t pat[PATTERNS];
  /* (added in version 7: a version 6 song loads without them) */
  uint8_t rsize, rdamp;   /* the reverb */
  uint8_t crate, cdepth;  /* the chorus */
  uint8_t pump;           /* how fast a ducked track comes back */
  uint8_t filter, reso;   /* the master filter: 0..63 low-pass, 64 off, 65..127 high-pass */
  uint8_t dtype;          /* the distortion's kind (D_*); DRIVE is song.drive */
  uint8_t ccout;          /* 1: parameters changed here are sent out as MIDI CC (CC.md) */
  /* (added in version 8) */
  uint8_t chain_a, chain_b; /* inclusive pattern range that plays in order; equal: just the one */
  uint8_t take_bpm;         /* the tempo the last free take found, 0: none yet */
  uint8_t pad[1];
} song_t;

_Static_assert(sizeof(song_t) <= SETTINGS_MAX, "the song must fit in the settings");

extern song_t song; /* written by loop() only; audio() reads it */

/* ---- the engine (engine.c). Written by loop(): */
extern volatile uint8_t playing;
/* written by audio(): */
extern volatile uint8_t cur_pattern;   /* the pattern playing (song.pattern takes over at the next bar) */
extern volatile int8_t play_step;      /* -1: stopped */
extern volatile uint16_t hits[TRACKS]; /* counts each sound started, for the screen */
extern volatile uint8_t cpu_load, cpu_peak; /* how much of each block's time audio() used, in % (FM-1 only) */
#define SCOPE_LEN 1024
extern int16_t scope[SCOPE_LEN];   /* the last of the output, for the screen's oscilloscope */
extern volatile uint32_t scope_at; /* where the next frame goes (it only counts up) */

void engine_tempo(void);             /* after bpm, swing or delay time change */
void live_on(int t, int v, int vel); /* play track t now: a drum voice, or a synth note (held) */
void live_off(int t, int v);         /* let a synth note go */
void midi_out(int status, int d1, int d2); /* a message out over USB MIDI, sent by audio() (its only sender) */

/* ---- the machines: the modeled drums, the ROM drums, SMP (samples/) if there are any, the synth engines */
int machine_count(void);
const char *machine_name(int m);
int machine_find(const char *name, int otherwise);
int machine_synth(int m);      /* 1: a synth engine (its voices are notes) */
int machine_engine(int m);     /* the synth engine (synths.c) it is, or -1 */
int machine_midi(int m);       /* 1: MIDI out (its voices are notes, sent, not heard) */
int machine_usr(int m);        /* 1: a sampled instrument (notes, from F3) */
const char *machine_label(int m); /* 3 characters, for the grid */
int machine_voices(int m);     /* how many voice numbers it has (22, the samples, or 128 notes) */
int machine_has(int m, int v); /* 1: its own voice; 2: played by a neighbor; 0: silent */
const char *voice_name(int m, int v);
void drum_envelope(int t, int v, uint8_t *h, int n); /* the loudness over 1 s in n columns, 0..255 */

/* ---- the synth engines (synths.c, Felucca's): each has 8 parameters and presets */
typedef struct {
  const char *name;
  const char *const *names; /* the values' names, or 0 */
  int16_t lo, hi, def;
  uint8_t fmt; /* SF_* */
} synth_param_t;
enum { SF_NUM,
       SF_PCT,
       SF_SEMI,
       SF_ENUM };
int synth_count(void);
const char *synth_name(int e);
const char *synth_label(int e);
const char *synth_page(int e, int page); /* one tab title, or 0 past the engine's last tab */
int synth_pages(int e);                  /* how many tabs of four parameters the engine has (1..4) */
int synth_ownenv(int e);                 /* 1: the engine's own envelope is the voice (the 303) */
void synth_param(int t, int k, synth_param_t *out); /* parameter k of track t's engine, as it has it set */
int synth_presets(int e);
const char *synth_preset_name(int e, int p);
void synth_load(int t, int p); /* preset p of track t's engine into song.track[t] */

/* between engine.c and drums.c / synths.c */
void synths_note(int t, int note, int vel, int slide, int32_t gate); /* gate: frames, -1 held */
void synths_off(int t, int note);
void synths_render(int t, int32_t *out, uint32_t n); /* track t, at its level; n: CTL (32) frames */
void synths_stop(void);
void drums_hit(int t, int v, int vel);
void drums_render(int t, int32_t *out, uint32_t n); /* track t's drums, at its level */

/* ---- the effects (fx.c), run by audio() */
enum { D_SOFT,
       D_HARD,
       D_TUBE,
       D_DIODE,
       D_FUZZ,
       D_FOLD,
       D_WAVE,
       D_RECT,
       D_BITS,
       D_RATE,
       D_CRUSH,
       DISTS };
extern const char *const DIST_NAME[DISTS];
void fx_reverb(const int32_t *in, int32_t *out, uint32_t n); /* adds what in sends it to out */
void fx_chorus(const int32_t *in, int32_t *out, uint32_t n);
void fx_master(int32_t *x, uint32_t n); /* the filter and the distortion, in place */
int32_t fx_curve(int32_t x);            /* the distortion as it is set, for the screen */

#endif
