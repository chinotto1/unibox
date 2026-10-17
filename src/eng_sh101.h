/* SH-101: one oscillator, a sub oscillator one or two octaves down, noise, and the same filter ANALOG uses.
 * The envelope and ENV>FLT knobs on the ENV and PATCH tabs are the SH-101's VCA and VCF envelope. */
static const char *const N_SH_WAVE[] = {"SAW", "SQR", "PUL"};
static const char *const N_SH_SUB[] = {"OFF", "-1", "-2"};

static void sh_note_on(track_t *t, voice_t *v) {
  (void)t;
  v->s[0] = v->s[1] = 0;
  if (!v->s[2]) {
    v->s[2] = 0x51A0 + (int32_t)v->age;
  }
}

static void sh_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m) {
  const int16_t *p = t->p;
  int32_t wave = p[P_E0], pwm = p[P_E1], sub = p[P_E2], nz = p[P_E3];
  int32_t cut = (p[P_E4] << 8) + m->cutoff + (p[P_E6] * (v->pitch16 - 60 * 16) >> 4);
  int32_t drv = p[P_E7];
  uint32_t pw = 0x20000000u + (uint32_t)pwm * 10066329u; /* pulse width ~ 12% .. 88% */
  uint32_t ph = v->ph[0], inc = m->inc, sph = v->ph[1];
  uint32_t sinc = sub >= 2 ? inc >> 2 : inc >> 1;
  int32_t ic1 = v->s[0], ic2 = v->s[1], nst = v->s[2];
  int32_t drive = 32768 + drv * 700;
  tsvf_t flt;
  tsvf_coef(&flt, cut, p[P_E5]);
  for (uint32_t i = 0; i < n; i++) {
    int32_t s = wave == 0 ? osc_saw(ph, inc) : wave == 1 ? osc_pulse(ph, inc, 0x80000000u)
                                                         : osc_pulse(ph, inc, pw);
    if (sub) {
      s = (s * 3 + osc_pulse(sph, sinc ? sinc : 1, 0x80000000u)) >> 2;
      sph += sinc ? sinc : 1;
    }
    if (nz) {
      s += mulq15((int32_t)(noise32(&nst) >> 17) - 16384, nz * 180);
    }
    ph += inc;
    if (drv) {
      s = softclip(((s >> 2) * (drive >> 2)) >> 11);
    }
    s = soft_knee(tsvf_lp(&flt, s >> 1, &ic1, &ic2), 16000) * 2;
    out[i] += voice_amp(s, m, i) * 2;
  }
  v->ph[0] = ph;
  v->ph[1] = sph;
  v->s[0] = ic1;
  v->s[1] = ic2;
  v->s[2] = nst;
}

static const preset_t SH_PRESETS[] = {
    {"BASS", {0, 64, 1, 0, 42, 70, 40, 20}, {0, 70, 0, 30}, 40, 1},
    {"LEAD", {0, 40, 0, 0, 80, 40, 50, 10}, {2, 60, 80, 40}, 28, 1},
    {"PWM", {2, 70, 0, 0, 70, 25, 30, 0}, {8, 70, 90, 50}, 18, 0},
    {"SQUARE", {1, 64, 2, 0, 50, 60, 64, 30}, {0, 50, 40, 28}, 36, 1},
};

static const engine_t ENG_SH101 = {
    .name = "SH-101",
    .page_title = {"OSC", "FLT"},
    .edit = {
        {"WAVE", F_ENUM, 0, 2, 0, N_SH_WAVE, 0},
        {"PWM", F_PCT, 0, 127, 64, 0, 0},
        {"SUB", F_ENUM, 0, 2, 1, N_SH_SUB, 0},
        {"NOIS", F_PCT, 0, 127, 0, 0, 0},
        {"CUT", F_CUTOFF, 0, 127, 70, 0, 0},
        {"RES", F_PCT, 0, 127, 40, 0, 0},
        {"KEY", F_PCT, 0, 127, 40, 0, 0},
        {"DRV", F_PCT, 0, 127, 10, 0, 0},
    },
    .presets = SH_PRESETS,
    .npresets = NELEM(SH_PRESETS),
    .note_on = sh_note_on,
    .render = sh_render,
    .keep = 0x03,
    .poly = NVOICE,
};
