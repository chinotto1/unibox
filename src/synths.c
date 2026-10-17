/* FOMAJAM's synth engines: a host for Felucca's engines (in vendor/) and our own. Each track that uses one
 * gets two voices with an ADSR envelope, an envelope sweep of the filter, accent, glide and mono mode, all
 * rendered in 32-frame blocks as Felucca does. At most MAXV voices play across all tracks (Felucca's own
 * limit on this chip); when there are more, the oldest gives way.
 *
 *   ANALOG LOFI TRIO WHEEL NOISE   from Felucca-Engines (MIT)
 *   PHASE VOICE PHYS               from Felucca (GPL-3.0-only)
 *   STRING                         ours: Karplus-Strong (eng_string.h)
 *   SUPERSAW                       ours: the JP-8000's, after Szabo (eng_supersaw.h)
 *
 * The engines read their parameters from Felucca's track struct (p[]), which is filled from song.track every
 * block. This code runs in audio(), except the synth_* functions, which the screen uses. */
#define track_t song_track_t /* ours is a song's track; Felucca's is its own */
#include "groove.h"
#undef track_t
#include "tables.h"

#define NVOICE 2
#define NPART TRACKS
#define MAXV 8
#include "fe_tables.h"
#include "felucca_engine.h"

enum { P_ATK,
       P_DEC,
       P_SUS,
       P_REL,
       P_FTYPE,
       P_ROOT,
       P_E0,
       P_E1,
       P_E2,
       P_E3,
       P_E4,
       P_E5,
       P_E6,
       P_E7,
       P_COUNT };
typedef struct track {
  int16_t p[P_COUNT];
  voice_t v[NVOICE];
} track_t;
static track_t trk[NPART];

/* what PHYS needs from its host: a block of memory per track, and random numbers */
#define PHYS_POLY NVOICE
#define ENG_MEM_PART 8600u
#define ENG_MEM_FITS(name, size) _Static_assert((size) <= ENG_MEM_PART, name " state over ENG_MEM_PART")
static uint8_t eng_mem[NPART][ENG_MEM_PART] POOL;
#define ENG_MEM(p) ((void *)eng_mem[p])
static uint32_t rng(void) { return random32(); }

#include "dsp.c"
#include "eng_analog.c"
#include "eng_formant.c"
#include "eng_lofi.c"
#include "eng_noise.c"
#include "eng_phase.c"
#include "eng_phys.c"
#include "eng_trio.c"
#include "eng_wheel.c"
#include "eng_string.h"
#include "eng_supersaw.h"

static const engine_t *const ENGINES[] = {&ENG_ANALOG, &ENG_LOFI, &ENG_TRIO, &ENG_WHEEL, &ENG_NOISE, &ENG_PHASE, &ENG_FORMANT, &ENG_PHYS, &ENG_STRING, &ENG_SUPERSAW};
static const char *const LABEL[] = {"ANA", "LOF", "TRI", "WHL", "NOI", "PHS", "VOX", "PHY", "STR", "SSW"};
#define ENGINES_N ((int)NELEM(ENGINES))

/* ---- for the screen */
int synth_count(void) { return ENGINES_N; }
const char *synth_name(int e) { return ENGINES[e]->name; }
const char *synth_label(int e) { return LABEL[e]; }
const char *synth_page(int e, int page) { return ENGINES[e]->page_title[page & 1]; }
int synth_presets(int e) { return ENGINES[e]->npresets; }
const char *synth_preset_name(int e, int p) { return ENGINES[e]->presets[p].name; }

static void params(track_t *T, const song_track_t *s) { /* song.track -> Felucca's p[] */
  for (int k = 0; k < 8; k++) {
    T->p[P_E0 + k] = s->e[k];
  }
  T->p[P_ATK] = s->atk;
  T->p[P_DEC] = s->dec;
  T->p[P_SUS] = s->sus;
  T->p[P_REL] = s->rel;
  T->p[P_FTYPE] = s->ftype;
  T->p[P_ROOT] = 0;
}

void synth_param(int t, int k, synth_param_t *out) {
  static track_t ui; /* an engine's parameter names can depend on its other parameters (its mode) */
  const engine_t *E = ENGINES[machine_engine(song.track[t].machine)];
  const param_desc_t *d = 0;
  params(&ui, &song.track[t]);
  if (E->desc) {
    d = E->desc(&ui, (uint32_t)k);
  }
  if (!d) {
    d = &E->edit[k];
  }
  out->name = d->label;
  out->names = d->names;
  out->lo = d->min;
  out->hi = d->max;
  out->def = d->def;
  out->fmt = d->fmt == F_ENUM && d->names ? SF_ENUM : d->fmt == F_PCT ? SF_PCT
                                                  : d->fmt == F_SEMI  ? SF_SEMI
                                                                      : SF_NUM;
}

void synth_load(int t, int p) {
  song_track_t *s = &song.track[t];
  int e = machine_engine(s->machine);
  if (e < 0 || p < 0 || p >= ENGINES[e]->npresets) {
    return;
  }
  const preset_t *pr = &ENGINES[e]->presets[p];
  for (int k = 0; k < 8; k++) {
    s->e[k] = pr->e[k];
  }
  s->atk = pr->env[0];
  s->dec = pr->env[1];
  s->sus = pr->env[2];
  s->rel = pr->env[3];
  s->fenv = pr->fenv;
  s->mono = pr->mono;
  s->gate = pr->env[0] >= 30 ? GATE_TIE : 50; /* a slow attack (a pad): held to the next note, else half the step */
  s->ftype = 0;
  s->preset = (uint8_t)p;
}

/* ---- the voices */
static int8_t cur[NPART] = {-1, -1, -1, -1, -1, -1, -1, -1, -1}; /* the engine each track's voices are of */
static int32_t gate_left[NPART][NVOICE];                         /* frames until the gate falls; -1 held */
static int32_t last_amp[NPART][NVOICE];
static uint32_t age;

static void voice_on(track_t *T, const engine_t *E, voice_t *v, int note, int vel) { /* as Felucca's */
  int sounding = v->active && v->stage != 0;
  uint32_t ph0 = v->ph[0], ph1 = v->ph[1], ph2 = v->ph[2];
  int32_t s[8];
  for (int k = 0; k < 8; k++) {
    s[k] = v->s[k];
  }
  v->note = (uint8_t)note;
  v->vel = (uint8_t)vel;
  v->gate = 1;
  v->active = 1;
  v->stage = 1;
  v->age = ++age;
  v->pitch16 = v->pitch_cur = note * 16;
  v->fine = 0;
  if (!sounding) {
    v->env = v->env_out = 0;
  }
  E->note_on(T, v);
  if (sounding) { /* a retrigger keeps the phases and what the engine asks to keep */
    v->ph[0] = ph0;
    v->ph[1] = ph1;
    v->ph[2] = ph2;
    for (uint32_t k = 0; k < 8u; k++) {
      if (k != 2u && k != 3u && (E->keep >> k & 1u)) {
        v->s[k] = s[k];
      }
    }
  }
}

static void make_room(void) { /* when MAXV voices are playing, the oldest fades out over one block */
  int n = 0;
  voice_t *old = 0;
  for (int t = 0; t < NPART; t++) {
    for (int i = 0; i < NVOICE; i++) {
      voice_t *v = &trk[t].v[i];
      if (v->active && v->stage != 4) {
        n++;
        if (!old || v->age < old->age) {
          old = v;
        }
      }
    }
  }
  if (n >= MAXV && old) {
    old->gate = 0;
    old->stage = 4;
  }
}

static void release(voice_t *v) {
  if (v->active && v->stage != 4) {
    v->gate = 0;
    v->stage = 3;
  }
}

void synths_note(int t, int note, int vel, int slide, int32_t gate) {
  const song_track_t *s = &song.track[t];
  int e = machine_engine(s->machine);
  if (e < 0 || cur[t] != e) {
    return; /* (the track's first block sets the engine up) */
  }
  const engine_t *E = ENGINES[e];
  track_t *T = &trk[t];
  params(T, s);
  if (gate == -2 && !s->mono) { /* tied: the note held from the last step goes now (its release under this one's
                                 * attack); MONO: it is held on into this one, legato (no new attack) */
    for (int i = 0; i < NVOICE; i++) {
      if (gate_left[t][i] == -2 && T->v[i].note != note) {
        release(&T->v[i]);
        gate_left[t][i] = 0;
      }
    }
  }
  int pick = 0, held = -1;
  for (int i = 0; slide && i < NVOICE; i++) { /* a slide: the newest held voice glides on */
    voice_t *v = &T->v[i];
    if (v->active && v->gate && v->stage != 4 && (held < 0 || v->age > T->v[held].age)) {
      held = i;
    }
  }
  if (held >= 0) {
    pick = held;
  } else if (!s->mono) { /* the same note, else a free voice, else the oldest */
    for (int i = 0; i < NVOICE; i++) {
      voice_t *v = &T->v[i];
      if (v->active && v->note == note) {
        pick = i;
        break;
      }
      if (!v->active && T->v[pick].active) {
        pick = i;
      } else if (v->active && T->v[pick].active && (v->gate < T->v[pick].gate || (v->gate == T->v[pick].gate && v->age < T->v[pick].age))) {
        pick = i; /* (a voice let go before one held, then the oldest) */
      }
    }
  }
  voice_t *v = &T->v[pick];
  if ((slide || s->mono) && v->active && v->gate && v->stage != 4) { /* legato: glide there, no new attack */
    v->note = (uint8_t)note;
    v->pitch16 = note * 16;
    v->vel = (uint8_t)vel;
    if (!slide) {
      v->pitch_cur = v->pitch16;
    }
  } else {
    if (!v->active) {
      make_room();
    }
    voice_on(T, E, v, note, vel);
  }
  gate_left[t][pick] = gate;
  hits[t]++;
}

void synths_off(int t, int note) {
  for (int i = 0; i < NVOICE; i++) {
    if (trk[t].v[i].note == note && gate_left[t][i] < 0) {
      release(&trk[t].v[i]);
    }
  }
}

void synths_stop(void) {
  for (int t = 0; t < NPART; t++) {
    for (int i = 0; i < NVOICE; i++) {
      release(&trk[t].v[i]);
    }
  }
}

/* one block of Felucca's ADSR; Q15 */
static int32_t env_tick(const track_t *T, voice_t *v) {
  const int16_t *p = T->p;
  int32_t sus = (int32_t)p[P_SUS] << 17;
  switch (v->stage) {
    case 1:
      v->env += (int32_t)ENV_LIN[p[P_ATK] & 127];
      if (v->env >= (1 << 24)) {
        v->env = 1 << 24;
        v->stage = 2;
      }
      break;
    case 2:
      v->env += mulq16(sus - v->env, ENV_EXP[p[P_DEC] & 127]);
      break;
    case 3:
      v->env -= mulq16(v->env, ENV_EXP[p[P_REL] & 127]);
      if (v->env < (1 << 12)) {
        v->env = 0;
        v->stage = 0;
        v->active = 0;
      }
      break;
    case 4: /* given way to another: this block fades it (synths_render ends it after) */
      v->env = 0;
      break;
    default:
      v->env = 0;
      break;
  }
  return v->env >> 9;
}

void synths_render(int t, int32_t *out, uint32_t n) {
  const song_track_t *s = &song.track[t];
  int e = machine_engine(s->machine);
  track_t *T = &trk[t];
  if (e != cur[t]) { /* another engine: its voices start from nothing */
    for (int i = 0; i < NVOICE; i++) {
      voice_t *v = &T->v[i];
      uint8_t *b = (uint8_t *)v;
      for (uint32_t k = 0; k < sizeof *v; k++) {
        b[k] = 0;
      }
      last_amp[t][i] = 0;
    }
    for (uint32_t k = 0; k < ENG_MEM_PART; k++) {
      eng_mem[t][k] = 0;
    }
    cur[t] = (int8_t)e;
  }
  if (e < 0) {
    return;
  }
  const engine_t *E = ENGINES[e];
  int32_t buf[CTL], tune = (s->tune - 64) * 12 / 64 * 16;
  params(T, s);
  if (E->block) {
    E->block(T);
  }
  for (uint32_t i = 0; i < n; i++) {
    buf[i] = 0;
  }
  for (int i = 0; i < NVOICE; i++) {
    voice_t *v = &T->v[i];
    if (!v->active) {
      continue;
    }
    if (gate_left[t][i] > 0 && (gate_left[t][i] -= (int32_t)n) <= 0) {
      release(v);
    }
    int dying = v->stage == 4;
    int32_t a = env_tick(T, v);
    if (E->amp) {
      a = E->amp(T, v, a);
    }
    if (!v->active) {
      continue;
    }
    if (!a && !last_amp[t][i] && v->stage == 2) { /* held at a silent sustain: nothing to hear */
      continue;
    }
    if (v->pitch_cur != v->pitch16) { /* glide: SLIDE sets how fast */
      int32_t d = v->pitch16 - v->pitch_cur, st = (int32_t)(ENV_LIN[(s->slide ? s->slide : 40) & 127] >> 14);
      st = st < 1 ? 1 : st;
      v->pitch_cur += d > 0 ? (d < st ? d : st) : (-d < st ? d : -st);
    }
    vmod_t m;
    int32_t env = a;
    a = a * (v->vel > 110 ? 127 : v->vel) >> 7;
    m.pitch16 = clamp(v->pitch_cur + tune, 0, 2047);
    m.inc = pitch_inc((uint32_t)m.pitch16);
    m.fine = 0;
    m.amp0 = last_amp[t][i];
    m.amp1 = a;
    m.envq15 = env;
    m.cutoff = (env * s->fenv) >> 7;
    if (v->vel > 110) { /* an accent opens the filter with the envelope */
      m.cutoff += (env * 24) >> 7;
    }
    m.shape = 64 << 8;
    last_amp[t][i] = a;
    E->render(T, v, buf, n, &m);
    if (dying) {
      v->active = 0;
      v->stage = 0;
    }
  }
  int32_t level = s->level * 258;
  for (uint32_t i = 0; i < n; i++) {
    out[i] += buf[i] * (level >> 4) >> 11;
  }
}
