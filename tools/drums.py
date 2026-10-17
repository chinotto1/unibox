#!/usr/bin/env python3
"""Write src/drums.h: the drum machines as integer tables (the chip has no floating point).

  python3 tools/drums.py

The modeled machines (808, 909, SDS-V, CR-78) and the list of voices come from trommelsynthesizer (in poketrack,
zlib license, by the same author). Each voice is one of a few types (swept sine, snare, metallic, ping, clap,
noise) with a handful of constants. This script turns every time into an index into TAU (a per-frame decay),
every filter frequency into an index into FREQ (a semitone grid starting at 20 Hz), and every amount into a Q12
number, so the engine only needs to multiply and shift.
"""

import math
from pathlib import Path

FS = 44100
OUT = Path(__file__).resolve().parents[1] / "src" / "drums.h"

VOICES = ["BD", "BD2", "SD", "SD2", "RS", "CP", "LT", "MT", "HT", "CH", "OH", "CY", "RD", "CB", "CB2", "TB", "LC", "MC", "HC", "TI", "MA", "CL"]
V = {n: i for i, n in enumerate(VOICES)}

# peak each voice is brought to at the default knobs (before LEVEL): kicks loud, hats quiet
TARGET = {
  "BD": 0.85,
  "BD2": 0.85,
  "SD": 0.75,
  "SD2": 0.75,
  "RS": 0.6,
  "CP": 0.6,
  "LT": 0.7,
  "MT": 0.7,
  "HT": 0.7,
  "CH": 0.4,
  "OH": 0.45,
  "CY": 0.5,
  "RD": 0.45,
  "CB": 0.5,
  "CB2": 0.5,
  "TB": 0.4,
  "LC": 0.65,
  "MC": 0.65,
  "HC": 0.65,
  "TI": 0.6,
  "MA": 0.35,
  "CL": 0.5,
}

# a voice a machine lacks plays its nearest neighbor, retuned (semitones)
FALLBACK = {
  "BD2": [("BD", 0)],
  "SD2": [("SD", 2)],
  "RS": [("CL", -5), ("SD", 5)],
  "CP": [("RS", -3), ("SD", 0)],
  "LT": [("MT", -6), ("HT", -11), ("BD", 12)],
  "MT": [("LT", 4), ("HT", -7)],
  "HT": [("MT", 5), ("LT", 9)],
  "CH": [("MA", 0)],
  "OH": [("CH", 0)],
  "CY": [("OH", 3), ("CH", -3)],
  "RD": [("CY", 3), ("OH", 2)],
  "CB": [("CB2", 4), ("CL", -12)],
  "CB2": [("CB", -4)],
  "TB": [("MA", 0), ("CH", 2)],
  "LC": [("LT", 4), ("MC", -4)],
  "MC": [("LC", 4), ("MT", 4)],
  "HC": [("MC", 5), ("HT", 5)],
  "TI": [("HC", 5), ("HT", 7)],
  "MA": [("TB", 0), ("CH", 3)],
  "CL": [("RS", 5), ("CB", 12)],
}

# (kind, constants), as in trommelsynthesizer's CFG
SW = lambda *c: ("SWEPT", c)  # f0 Hz, sweep, sweep tau, amp tau, click, click lp Hz, drive, noise body, tone->drive
SN = lambda *c: ("SNARE", c)  # osc1 Hz, osc2 Hz, osc1 tau, osc2 tau, drop, noise hp Hz, noise lp Hz, noise tau, noise level, snap
MT = lambda *c: ("METAL", c)  # freq scale, bp Hz, hp Hz, fast tau, slow tau, slow mix, noise fraction, click
PG = lambda *c: ("PING", c)  # f1, f2, tau1, tau2, mix2, bp Hz (0 none), drive, squares, tail mix, tail tau
CL = lambda *c: ("CLAP", c)  # bp Hz, Q, burst spacing s, bursts, burst tau, tail tau, tail level, hp Hz
NZ = lambda *c: ("NOISE", c)  # hp Hz, attack tau, decay tau

MACHINES = [
  (
    "808",
    {
      "BD": SW(52, 1.1, 0.010, 0.22, 0.35, 900, 1.4, 0, 0),
      "SD": SN(180, 330, 0.075, 0.040, 0.2, 1800, 7000, 0.060, 1.0, 0.1),
      "RS": PG(455, 1667, 0.008, 0.004, 0.8, 0, 4.0, 0, 0, 0),
      "CP": CL(1000, 2.2, 0.0105, 3, 0.0035, 0.075, 0.9, 400),
      "LT": SW(88, 0.5, 0.012, 0.17, 0.10, 2500, 1.1, 0, 0),
      "MT": SW(131, 0.5, 0.012, 0.13, 0.10, 2500, 1.1, 0, 0),
      "HT": SW(190, 0.5, 0.012, 0.10, 0.10, 2500, 1.1, 0, 0),
      "CH": MT(1.0, 3400, 7000, 0.022, 0.022, 0.0, 0.0, 0.3),
      "OH": MT(1.0, 3400, 7000, 0.040, 0.120, 0.6, 0.0, 0.3),
      "CY": MT(1.0, 3400, 6000, 0.200, 0.600, 0.6, 0.0, 0.3),
      "CB": PG(540, 800, 0.009, 0.009, 1.0, 2640, 1.0, 1, 0.35, 0.12),
      "LC": SW(165, 0.25, 0.008, 0.12, 0.20, 3000, 1.0, 0, 0),
      "MC": SW(250, 0.25, 0.008, 0.10, 0.20, 3000, 1.0, 0, 0),
      "HC": SW(370, 0.25, 0.008, 0.08, 0.20, 3000, 1.0, 0, 0),
      "MA": NZ(5000, 0.0008, 0.018),
      "CL": PG(2450, 1850, 0.012, 0.008, 0.5, 0, 2.5, 0, 0, 0),
    },
  ),
  (
    "909",
    {
      "BD": SW(52, 3.2, 0.011, 0.13, 0.8, 3500, 2.0, 0, 1),
      "SD": SN(185, 330, 0.050, 0.028, 0.35, 1200, 9500, 0.090, 1.1, 0.5),
      "RS": PG(480, 1700, 0.006, 0.004, 0.9, 0, 5.0, 0, 0, 0),
      "CP": CL(1150, 2.4, 0.0095, 3, 0.0030, 0.120, 0.9, 500),
      "LT": SW(100, 1.0, 0.018, 0.18, 0.35, 3000, 1.2, 0, 0),
      "MT": SW(150, 1.0, 0.018, 0.14, 0.35, 3000, 1.2, 0, 0),
      "HT": SW(222, 1.0, 0.018, 0.11, 0.35, 3000, 1.2, 0, 0),
      "CH": MT(1.09, 3600, 7800, 0.028, 0.028, 0.0, 0.5, 0.5),
      "OH": MT(1.09, 3600, 7800, 0.060, 0.140, 0.55, 0.5, 0.5),
      "CY": MT(1.09, 3600, 5500, 0.300, 0.700, 0.55, 0.4, 0.5),
      "RD": MT(0.93, 3600, 5000, 0.400, 0.900, 0.5, 0.3, 0.3),
    },
  ),
  (
    "SDS",  # Simmons SDS-V: the swept "pew" toms
    {
      "BD": SW(58, 1.2, 0.020, 0.16, 0.25, 1500, 1.3, 0, 0),
      "SD": SN(210, 340, 0.060, 0.035, 0.6, 1500, 8500, 0.130, 1.2, 0.3),
      "CP": CL(1300, 1.8, 0.012, 3, 0.004, 0.090, 1.1, 600),
      "LT": SW(105, 3.5, 0.035, 0.22, 0.30, 3500, 1.5, 0.15, 0),
      "MT": SW(155, 3.5, 0.035, 0.17, 0.30, 3500, 1.5, 0.15, 0),
      "HT": SW(230, 3.5, 0.035, 0.13, 0.30, 3500, 1.5, 0.15, 0),
      "CH": MT(1.0, 6000, 6000, 0.030, 0.030, 0.0, 0.9, 0.2),
      "OH": MT(1.0, 6000, 6000, 0.080, 0.150, 0.5, 0.9, 0.2),
    },
  ),
  (
    "CR78",  # Roland CR-78 (1978): soft analog voices
    {
      "BD": SW(70, 0.8, 0.012, 0.10, 0.15, 1200, 1.0, 0, 0),
      "SD": SN(190, 290, 0.060, 0.035, 0.1, 1800, 6000, 0.110, 0.9, 0.05),
      "RS": PG(600, 1800, 0.010, 0.005, 0.7, 0, 3.0, 0, 0, 0),
      "CH": MT(0.95, 4500, 5500, 0.040, 0.040, 0.0, 0.3, 0.2),
      "OH": MT(0.95, 4500, 5500, 0.080, 0.150, 0.5, 0.3, 0.2),
      "CY": MT(0.95, 4500, 4500, 0.300, 0.700, 0.5, 0.3, 0.2),
      "CB": PG(560, 845, 0.010, 0.010, 1.0, 2400, 1.0, 1, 0.30, 0.10),
      "TB": NZ(4500, 0.0015, 0.080),
      "MA": NZ(6000, 0.0008, 0.030),
      "CL": PG(2400, 1800, 0.014, 0.009, 0.5, 0, 2.5, 0, 0, 0),
      "HC": SW(480, 0.15, 0.005, 0.08, 0.15, 4000, 1.0, 0, 0),
      "MC": SW(330, 0.15, 0.005, 0.10, 0.15, 4000, 1.0, 0, 0),
    },
  ),
]

KINDS = ["NONE", "SWEPT", "SNARE", "METAL", "PING", "CLAP", "NOISE"]


def tau(s):
  """seconds -> TAU index: 0.1 ms * 2^(i/12)"""
  return max(0, min(255, round(12 * math.log2(s / 0.0001))))


def freq(hz):
  """Hz -> FREQ index: 20 Hz * 2^(i/12)"""
  return max(0, min(127, round(12 * math.log2(hz / 20))))


def q12(x):
  return max(-32768, min(32767, round(x * 4096)))


# the filter resonances used (k = 1/Q): each gets its own row of coefficients
QS = [0.7, 1.2, 1.5, 1.8, 2.2, 2.4]


def qclass(q):
  return QS.index(q)


def patch(kind, c):
  """-> (kind, hz[2], t[4], f[3], qc, k[6])"""
  hz, t, f, k, qc = [0, 0], [0, 0, 0, 0], [0, 0, 0], [0] * 6, 0
  if kind == "SWEPT":
    hz[0] = round(c[0])
    t[0], t[1] = tau(c[2]), tau(c[3])
    f[0] = freq(c[5])
    k[:5] = [q12(c[1]), q12(c[4]), q12(c[6]), q12(c[7]), int(c[8] > 0)]
  elif kind == "SNARE":
    hz = [round(c[0]), round(c[1])]
    t[0], t[1], t[2] = tau(c[2]), tau(c[3]), tau(c[7])
    f[0], f[1] = freq(c[5]), freq(c[6])
    qc = qclass(0.7)
    k[:3] = [q12(c[4]), q12(c[8]), q12(c[9])]
  elif kind == "METAL":
    t[0], t[1] = tau(c[3]), tau(c[4])
    f[0], f[1] = freq(c[1]), freq(c[2])
    qc = qclass(1.2)
    k[:4] = [q12(c[0]), q12(c[5]), q12(c[6]), q12(c[7])]
  elif kind == "PING":
    hz = [round(c[0]), round(c[1])]
    t[0], t[1], t[2] = tau(c[2]), tau(c[3]), tau(c[9]) if c[9] else 0
    f[0] = freq(c[5]) if c[5] else 0
    qc = qclass(1.5)
    k[:5] = [q12(c[4]), q12(c[6]), int(c[7] > 0), q12(c[8]), int(c[5] > 0)]
  elif kind == "CLAP":
    hz[0] = round(c[2] * FS)  # burst spacing, frames
    t[0], t[1] = tau(c[4]), tau(c[5])
    f[0], f[1] = freq(c[0]), freq(c[7])
    qc = qclass(c[1])
    k[:2] = [int(c[3]), q12(c[6])]
  elif kind == "NOISE":
    t[0], t[1] = tau(c[1]), tau(c[2])
    f[0] = freq(c[0])
    qc = qclass(0.7)
  return KINDS.index(kind), hz, t, f, qc, k


def svf_row(q):
  """the TPT state-variable filter's a1 a2 a3, Q24, for each FREQ index"""
  out = []
  for i in range(128):
    fc = min(20 * 2 ** (i / 12), FS * 0.45)
    g = math.tan(math.pi * fc / FS)
    k = 1 / q
    a1 = 1 / (1 + g * (g + k))
    out += [round(a1 * 2**24), round(g * a1 * 2**24), round(g * g * a1 * 2**24)]
  return out


def ima_step():
  import roms

  return roms.STEP


def table(name, ctype, values, per_line=12):
  rows = [", ".join(str(v) for v in values[i : i + per_line]) for i in range(0, len(values), per_line)]
  return f"static const {ctype} {name}[{len(values)}] = {{\n  " + ",\n  ".join(rows) + ",\n};\n"


def main():
  coef = [min(2**31 - 1, round(math.exp(-1 / (0.0001 * 2 ** (i / 12) * FS)) * 2**31)) for i in range(256)]
  onepole = [round((1 - math.exp(-2 * math.pi * min(20 * 2 ** (i / 12), FS * 0.45) / FS)) * 65536) for i in range(128)]
  tanh = [round(math.tanh(i / 32) * 32767) for i in range(257)]
  snappy = [round(12 * math.log2(0.6 + 0.8 * e / 127)) for e in range(128)]
  sample_decay = [tau(0.02 * 40 ** (d / 64)) for d in range(64)]  # DECAY below 64 on a sample: its tau
  sample_lp = [freq(400 * 50 ** (t / 64)) for t in range(64)]  # TONE below 64 on a sample: low-pass
  svf = []
  for q in QS:
    svf += svf_row(q)

  lines = [
    "/* made by tools/drums.py: do not edit */",
    "#ifndef DRUMS_H",
    "#define DRUMS_H",
    "#include <stdint.h>",
    "",
    "enum { " + ", ".join(f"V_{n}" for n in VOICES) + ", V_COUNT };",
    "enum { " + ", ".join(f"K_{n}" for n in KINDS) + " };",
    f"enum {{ {', '.join(f'M_{n}' for n, _ in MACHINES)}, M_MODELLED }};",
    "",
    "static const char *const VOICE_NAME[V_COUNT] = {" + ", ".join(f'"{n}"' for n in VOICES) + "};",
    "static const char *const MODEL_NAME[M_MODELLED] = {" + ", ".join(f'"{n}"' for n, _ in MACHINES) + "};",
    "static const char *const MODEL_LABEL[M_MODELLED] = {" + ", ".join(f'"{n if len(n) <= 3 else n[0] + n[2:]}"' for n, _ in MACHINES) + "};",
    "",
    "/* peak at the default knobs, Q15 */",
    "static const int16_t TARGET[V_COUNT] = {" + ", ".join(str(round(TARGET[n] * 32767)) for n in VOICES) + "};",
    "",
    "/* a missing voice plays one of these instead: voice + 1 (0 = none), semitones */",
    "static const int8_t FALLBACK[V_COUNT][3][2] = {",
  ]
  for n in VOICES:
    fb = FALLBACK.get(n, [])
    cells = [f"{{{V[v] + 1}, {s}}}" for v, s in fb] + ["{0, 0}"] * (3 - len(fb))
    lines.append(f"  {{{', '.join(cells)}}}, /* {n} */")
  lines += [
    "};",
    "",
    "typedef struct {",
    "  uint8_t kind, qc;  /* K_*, the resonance row of SVF */",
    "  uint8_t t[4], f[3]; /* TAU and FREQ indexes */",
    "  uint16_t hz[2];",
    "  int16_t k[6]; /* amounts, Q12 (or counts and flags) */",
    "} patch_t;",
    "",
    "/* what each kind keeps where:",
    " *   SWEPT  hz0 pitch; t0 sweep, t1 amp; f0 click low-pass; k0 sweep, k1 click, k2 drive, k3 noise body, k4 TONE drives",
    " *   SNARE  hz0 hz1 the two drums; t0 t1 their decays, t2 the noise; f0 noise high-pass, f1 low-pass; k0 drop, k1 noise, k2 snap",
    " *   METAL  t0 fast, t1 slow decay; f0 band-pass, f1 high-pass; k0 pitch scale, k1 slow mix, k2 noise, k3 click",
    " *   PING   hz0 hz1; t0 t1 decays, t2 tail; f0 band-pass; k0 mix2, k1 drive, k2 squares, k3 tail mix, k4 band-pass on",
    " *   CLAP   hz0 burst spacing (frames); t0 burst, t1 tail; f0 band-pass, f1 high-pass; k0 bursts, k1 tail level",
    " *   NOISE  t0 attack, t1 decay; f0 high-pass */",
    "static const patch_t PATCH[M_MODELLED][V_COUNT] = {",
  ]
  for name, voices in MACHINES:
    lines.append(f"  {{ /* {name} */")
    for n in VOICES:
      if n in voices:
        kind, hz, t, f, qc, k = patch(*voices[n])
        lines.append(
          f"    [V_{n}] = {{{kind}, {qc}, {{{', '.join(map(str, t))}}}, {{{', '.join(map(str, f))}}}, {{{hz[0]}, {hz[1]}}}, {{{', '.join(map(str, k))}}}}},"
        )
    lines.append("  },")
  lines += [
    "};",
    "",
    f"#define TAU_CLICK {tau(0.0015)}  /* the kick's click */",
    f"#define TAU_SNAP {tau(0.0007)}   /* the snare's snap */",
    f"#define TAU_TICK {tau(0.0008)}   /* the hats' click */",
    f"#define TAU_DROP {tau(0.015)}    /* the snare's pitch drop */",
    f"#define TAU_CHOKE {tau(0.004)}   /* an open hat closed by a closed one */",
    f"#define FREQ_SHELF {freq(3000)}  /* a sample's TONE above 64 lifts what is above this */",
    "",
    "/* per-frame decay multiplier, Q31: index i dies to 1/e in 0.1 ms * 2^(i/12) */",
    table("TAU", "uint32_t", coef, 8),
    "/* one-pole low-pass coefficient, Q16, at 20 Hz * 2^(i/12) */",
    table("ONEPOLE", "int32_t", onepole),
    "/* state-variable filter a1 a2 a3, Q24: SVF[(row * 128 + FREQ index) * 3] */",
    "#define SVF_ROWS " + str(len(QS)),
    "/* 1/Q of each row, Q24 */",
    "static const int32_t SVF_K[SVF_ROWS] = {" + ", ".join(str(round(2**24 / q)) for q in QS) + "};",
    table("SVF", "int32_t", svf, 9),
    "/* tanh(i / 32), Q15 */",
    table("TANH", "int16_t", tanh),
    "/* the snare's EXTRA (snappy): the noise decay moves this many TAU steps */",
    table("SNAPPY", "int8_t", snappy, 16),
    "/* a sample's DECAY below 64: the TAU it fades with */",
    table("SAMPLE_DECAY", "uint8_t", sample_decay, 16),
    "/* a sample's TONE below 64: the FREQ of its low-pass */",
    table("SAMPLE_LP", "uint8_t", sample_lp, 16),
    "/* IMA ADPCM (the ROM machines, tools/roms.py): the step sizes, the index change a code makes */",
    table("IMA_STEP", "int16_t", ima_step()),
    "static const int8_t IMA_INDEX[8] = {-1, -1, -1, -1, 2, 4, 6, 8};",
    "#endif",
    "",
  ]
  OUT.write_text("\n".join(lines))


if __name__ == "__main__":
  main()
