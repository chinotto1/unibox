/* SUPERSAW: the Roland JP-8000's super saw, written in the same form as Felucca's engines (felucca_engine.h),
 * with extra controls for dark, witch-house pads. It is included by synths.c rather than compiled on its own,
 * hence the .h.
 *
 * Based on Adam Szabo's measurements of the JP-8000 ("How to Emulate the Super Saw", 2010):
 *   - seven band-limited saws, detuned by fixed ratios (SS_OFFSET) scaled by a DETN curve that rises slowly for
 *     most of the knob's travel and then steeply (SS_DETUNE, his fit to the hardware);
 *   - free-running phases, randomized when a note starts, so no two notes sound the same and there is no flam;
 *   - MIX balances the center saw (SS_CENTER) against the six around it (SS_SIDE);
 *   - a high-pass at the note's fundamental (HPF 64), which removes the rumble the detuning leaves below it.
 * After those comes a resonant low-pass (CUT, RES, plus the envelope sweep from the host). For the dark, worn
 * sound: DRFT is a slow, random pitch wander (like tape wow, different for each voice), and CRSH lowers the bit
 * depth and sample rate. VOIC sets how many of the seven saws play (the center first, then pairs outward).
 * Each voice's state lives in the track's engine memory (ENG_MEM, since a track only plays one engine at a time). */

/* Szabo's curves, 0..127 (made from his fits with floats, once) */
static const int32_t SS_DETUNE[128] = { /* Q16 */
    197, 458, 588, 640, 650, 645, 642, 653, 683, 735, 808, 899,
    1006, 1125, 1252, 1384, 1516, 1647, 1775, 1897, 2013, 2123, 2226, 2323,
    2415, 2503, 2588, 2672, 2754, 2837, 2922, 3009, 3099, 3192, 3290, 3391,
    3496, 3604, 3715, 3829, 3945, 4061, 4179, 4295, 4411, 4525, 4636, 4745,
    4851, 4954, 5054, 5151, 5245, 5338, 5429, 5519, 5611, 5703, 5798, 5897,
    6001, 6111, 6228, 6353, 6488, 6633, 6789, 6957, 7137, 7329, 7534, 7752,
    7983, 8226, 8481, 8747, 9024, 9311, 9608, 9914, 10228, 10550, 10879, 11214,
    11556, 11904, 12258, 12619, 12986, 13361, 13743, 14132, 14531, 14938, 15355, 15782,
    16217, 16662, 17115, 17574, 18038, 18504, 18968, 19427, 19877, 20313, 20730, 21123,
    21488, 21823, 22125, 22398, 22646, 22880, 23118, 23387, 23726, 24185, 24837, 25770,
    27103, 28982, 31592, 35161, 39965, 46345, 54705, 65533};
static const int16_t SS_CENTER[128] = { /* Q15 */
    32697, 32554, 32411, 32268, 32125, 31982, 31839, 31697, 31554, 31411, 31268, 31125, 30982, 30840, 30697, 30554,
    30411, 30268, 30125, 29982, 29840, 29697, 29554, 29411, 29268, 29125, 28982, 28840, 28697, 28554, 28411, 28268,
    28125, 27983, 27840, 27697, 27554, 27411, 27268, 27125, 26983, 26840, 26697, 26554, 26411, 26268, 26126, 25983,
    25840, 25697, 25554, 25411, 25268, 25126, 24983, 24840, 24697, 24554, 24411, 24268, 24126, 23983, 23840, 23697,
    23554, 23411, 23269, 23126, 22983, 22840, 22697, 22554, 22411, 22269, 22126, 21983, 21840, 21697, 21554, 21412,
    21269, 21126, 20983, 20840, 20697, 20554, 20412, 20269, 20126, 19983, 19840, 19697, 19554, 19412, 19269, 19126,
    18983, 18840, 18697, 18555, 18412, 18269, 18126, 17983, 17840, 17697, 17555, 17412, 17269, 17126, 16983, 16840,
    16698, 16555, 16412, 16269, 16126, 15983, 15840, 15698, 15555, 15412, 15269, 15126, 14983, 14840, 14698, 14555};
static const int16_t SS_SIDE[128] = { /* Q15 */
    1454, 1784, 2111, 2434, 2755, 3073, 3388, 3700, 4008, 4314, 4617, 4917, 5214, 5508, 5799, 6086,
    6371, 6653, 6932, 7208, 7481, 7751, 8017, 8281, 8542, 8800, 9055, 9307, 9556, 9802, 10044, 10284,
    10521, 10755, 10986, 11214, 11439, 11661, 11880, 12096, 12309, 12518, 12725, 12929, 13130, 13328, 13523, 13715,
    13904, 14090, 14273, 14453, 14630, 14804, 14975, 15143, 15308, 15470, 15629, 15785, 15938, 16088, 16235, 16379,
    16520, 16658, 16793, 16925, 17054, 17180, 17303, 17423, 17540, 17654, 17765, 17873, 17978, 18080, 18179, 18275,
    18368, 18458, 18545, 18629, 18710, 18788, 18863, 18935, 19004, 19070, 19133, 19193, 19250, 19305, 19356, 19404,
    19449, 19491, 19530, 19566, 19599, 19629, 19656, 19680, 19702, 19720, 19735, 19747, 19756, 19762, 19765, 19765,
    19763, 19757, 19748, 19736, 19721, 19703, 19682, 19658, 19632, 19602, 19569, 19533, 19494, 19452, 19408, 19360};
static const int32_t SS_OFFSET[7] = {-7210, -4121, -1279, 0, 1305, 4074, 7042}; /* each saw's ratio, Q16 */
static const uint8_t SS_ORDER[7] = {3, 2, 4, 1, 5, 0, 6};                     /* VOIC takes them in this order */

typedef struct {
  uint32_t ph[7];
  int32_t ic1, ic2, hp;          /* the low-pass, the high-pass's state */
  uint32_t lfo, lrate;           /* DRFT: its slow sine, how fast */
  int32_t walk, aim, seed;       /* DRFT: a random wander and where it heads */
  int32_t hold, holdn;           /* CRSH: the sample held, how long more */
} ss_voice_t;
ENG_MEM_FITS("SUPERSAW", sizeof(ss_voice_t) * NVOICE);

static ss_voice_t *ss_of(track_t *t, voice_t *v) { return (ss_voice_t *)ENG_MEM(t - trk) + (v - t->v); }

static void ss_note_on(track_t *t, voice_t *v) {
  ss_voice_t *S = ss_of(t, v);
  if (v->active && v->stage) {
    return; /* a retrigger, a slide: the saws run on (free-running, as the JP-8000's) */
  }
  if (!S->seed) {
    S->seed = 0x3C6EF372 + (int32_t)v->age * 2654435761u;
  }
  for (int k = 0; k < 7; k++) { /* random phases: no two notes alike */
    S->ph[k] = noise32(&S->seed);
  }
  S->ic1 = S->ic2 = S->hp = 0;
  S->lfo = noise32(&S->seed);
  S->lrate = 700000u + (noise32(&S->seed) >> 12); /* ~0.23 .. 0.55 Hz a block */
  S->walk = S->aim = 0;
  S->hold = 0;
  S->holdn = 0;
}

static void ss_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m) {
  ss_voice_t *S = ss_of(t, v);
  const int16_t *p = t->p;
  int32_t nv = clamp(p[P_E0], 1, 7), det = SS_DETUNE[p[P_E1] & 127];
  int32_t c = SS_CENTER[p[P_E2] & 127], sd = SS_SIDE[p[P_E2] & 127];
  int32_t drft = p[P_E3], hpf = p[P_E6], crsh = p[P_E7];
  /* DRFT, once a block: a slow sine and a random wander, at most ~ +-3% of the pitch */
  S->lfo += S->lrate;
  if (!(noise32(&S->seed) & 15)) {
    S->aim = (int32_t)(noise32(&S->seed) >> 17) - 16384;
  }
  S->walk += (S->aim - S->walk) >> 5;
  int32_t wob = drft ? ((sine_i(S->lfo) + S->walk) * drft) >> 12 : 0; /* Q16 */
  uint32_t base = m->inc + (uint32_t)(((int64_t)m->inc * wob) >> 16), inc[7];
  for (int k = 0; k < 7; k++) {
    inc[k] = base + (uint32_t)(((int64_t)base * ((SS_OFFSET[k] * (int64_t)det) >> 16)) >> 16);
  }
  /* the level: the weights' sum, so a wider MIX or more saws come out as loud */
  int32_t tot = c + (nv - 1) * sd, norm = (int32_t)((1u << 30) / (uint32_t)(tot > 1 ? tot : 1)); /* Q15 */
  int32_t hpa = hpf ? clamp((int32_t)(base / 20861u) * hpf / 64, 1, 30000) : 0; /* 2 pi f / FS, Q15 */
  tsvf_t flt;
  int32_t cut = (p[P_E4] << 8) + m->cutoff, ic1 = S->ic1, ic2 = S->ic2, hp = S->hp;
  tsvf_coef(&flt, cut, p[P_E5]);
  int32_t bits = crsh ? 16 - crsh / 12 : 16, hold = 1 + crsh / 18, h = S->hold, hn = S->holdn;
  for (uint32_t i = 0; i < n; i++) {
    int32_t x = 0;
    for (int j = 0; j < nv; j++) {
      int k = SS_ORDER[j];
      x += mulq15(osc_saw(S->ph[k], inc[k]), j ? sd : c);
      S->ph[k] += inc[k];
    }
    x = mulq15(x, norm) * 3 / 2; /* (random phases: the sum is mostly well below its peak) */
    if (hpa) { /* the high-pass at the fundamental: the signal less its one-pole low-pass */
      hp += mulq15(x - hp, hpa);
      x -= hp;
    }
    x = soft_knee(tsvf_lp(&flt, x >> 1, &ic1, &ic2), 16000) * 2;
    if (crsh) { /* fewer bits, a lower rate */
      if (hn-- <= 0) {
        h = x >> (16 - bits) << (16 - bits);
        hn = hold - 1;
      }
      x = h;
    }
    out[i] += voice_amp(x, m, i) * 2;
  }
  S->ic1 = ic1;
  S->ic2 = ic2;
  S->hp = hp;
  S->hold = h;
  S->holdn = hn;
}

static const preset_t SUPERSAW_PRESETS[] = {
    /* name, {VOIC DETN MIX DRFT CUT RES HPF CRSH}, {A D S R}, fenv, mono */
    {"JP PAD", {7, 64, 80, 0, 92, 20, 64, 0}, {40, 90, 110, 80}, 10, 0},
    {"WITCH", {7, 92, 100, 60, 58, 30, 64, 30}, {80, 100, 120, 100}, 0, 0},
    {"DRAG CHOIR", {7, 110, 110, 90, 46, 10, 64, 50}, {95, 100, 127, 110}, 0, 0},
    {"GHOST", {5, 120, 120, 110, 40, 40, 80, 80}, {70, 100, 110, 105}, 15, 0},
    {"TAPE KEYS", {5, 50, 70, 70, 76, 15, 64, 40}, {2, 85, 60, 70}, 10, 0},
    {"TRANCE LD", {7, 70, 90, 0, 100, 25, 64, 0}, {5, 80, 100, 50}, 20, 1},
    {"SAW BASS", {3, 40, 50, 0, 60, 40, 32, 0}, {0, 70, 80, 30}, 30, 1},
};

static const engine_t ENG_SUPERSAW = {
    .name = "SUPERSAW",
    .page_title = {"SAWS", "TONE"},
    .edit = {
        {"VOIC", F_INT, 1, 7, 7, 0, 0},
        {"DETN", F_PCT, 0, 127, 64, 0, 0},
        {"MIX", F_PCT, 0, 127, 80, 0, 0},
        {"DRFT", F_PCT, 0, 127, 0, 0, 0},
        {"CUT", F_CUTOFF, 0, 127, 92, 0, 0},
        {"RES", F_PCT, 0, 127, 20, 0, 0},
        {"HPF", F_PCT, 0, 127, 64, 0, 0},
        {"CRSH", F_PCT, 0, 127, 0, 0, 0},
    },
    .presets = SUPERSAW_PRESETS,
    .npresets = NELEM(SUPERSAW_PRESETS),
    .note_on = ss_note_on,
    .render = ss_render,
    .knob = {P_E1, P_E2, P_E4, P_E3},
};
