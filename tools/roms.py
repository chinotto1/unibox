#!/usr/bin/env python3
"""The ROM drum machines: recordings of their own samples, converted to build/gen/roms.h (4-bit IMA ADPCM).

The build runs this. It downloads the recordings from tidal-drum-machines (pinned to one commit), one file at a
time, and keeps them in roms/ (which is not in git). Without a network connection, the build continues without
them. The recordings are not ours, and that repository doesn't state a license for them; see LICENSING.md.

Machines in MACHINES below are added in order until BUDGET is used up, and the build lists the ones left out.
To include one, move it higher in the list, or raise BUDGET as long as the FM-1 build still fits (the build
reports how full it is). The voice names are the ones in tools/drums.py.
"""

import hashlib
import struct
import sys
import urllib.parse
import urllib.request
import wave
from pathlib import Path

REPO = "https://raw.githubusercontent.com/geikha/tidal-drum-machines"
REF = "6577395a4d05031728ced2ec3e5637fa89d8be48"
RATE = 44100
MAX_SECONDS = 0.8
FADE_SECONDS = 0.02
HALVE_BELOW = 1e-3  # halve a recording's rate when less than this share of its energy is above a quarter of its rate

sys.path.insert(0, str(Path(__file__).resolve().parent))
import drums  # noqa: E402

BUDGET = 200 * 1024  # bytes of flash for them all
# UNIBOX keeps only these (in this order); the rest of MACHINES stays for reference
INCLUDE = ["LINN", "DMX"]

# name on the screen -> its 3-character label on the grid, folder, {voice: file}
MACHINES = {
  "707": (
    "707",
    "RolandTR707",
    {
      "BD": "rolandtr707-bd/Bassdrum-01",
      "BD2": "rolandtr707-bd/Bassdrum-02",
      "SD": "rolandtr707-sd/Snaredrum-01",
      "SD2": "rolandtr707-sd/Snaredrum-02",
      "RS": "rolandtr707-rim/Rimshot",
      "CP": "rolandtr707-cp/Clap",
      "LT": "rolandtr707-lt/Tom L",
      "MT": "rolandtr707-mt/Tom M",
      "HT": "rolandtr707-ht/Tom H",
      "CH": "rolandtr707-hh/Hat Closed",
      "OH": "rolandtr707-oh/Hat Open",
      "CY": "rolandtr707-cr/Crash",
      "CB": "rolandtr707-cb/Cowbell",
      "TB": "rolandtr707-tb/Tambourine",
    },
  ),
  "606": (
    "606",
    "RolandTR606",
    {
      "BD": "rolandtr606-bd/Bassdrum",
      "SD": "rolandtr606-sd/Snaredrum",
      "LT": "rolandtr606-lt/Tom L",
      "HT": "rolandtr606-ht/Tom H",
      "CH": "rolandtr606-hh/Hat Closed",
      "OH": "rolandtr606-oh/Hat Open",
      "CY": "rolandtr606-cr/Cymbal",
    },
  ),
  "DMX": (
    "DMX",
    "OberheimDMX",
    {
      "BD": "oberheimdmx-bd/Bassdrum-01",
      "BD2": "oberheimdmx-bd/Bassdrum-02",
      "SD": "oberheimdmx-sd/Snaredrum-01",
      "SD2": "oberheimdmx-sd/Snaredrum-02",
      "RS": "oberheimdmx-rim/Rim Shot",
      "CP": "oberheimdmx-cp/Clap",
      "LT": "oberheimdmx-lt/Tom L",
      "MT": "oberheimdmx-mt/Tom M",
      "HT": "oberheimdmx-ht/Tom H",
      "CH": "oberheimdmx-hh/Hat Closed",
      "OH": "oberheimdmx-oh/Hat Open",
      "CY": "oberheimdmx-cr/Crash",
      "RD": "oberheimdmx-rd/Ride",
      "TB": "oberheimdmx-tb/Tamborine",
      "MA": "oberheimdmx-sh/Cabasa",
      "TI": "oberheimdmx--perc/Timbale H",
    },
  ),
  "LINN": (
    "LIN",
    "LinnDrum",
    {
      "BD": "linndrum-bd/Bassdrum",
      "SD": "linndrum-sd/0Snarderum-01",
      "SD2": "linndrum-sd/0Snarderum-02",
      "RS": "linndrum-rim/Sidestick-01",
      "CP": "linndrum-cp/Clap",
      "LT": "linndrum-lt/Tom L-01",
      "MT": "linndrum-mt/Tom M-01",
      "HT": "linndrum-ht/Tom H-01",
      "CH": "linndrum-hh/Hat Closed-01",
      "OH": "linndrum-oh/Hat Open",
      "CY": "linndrum-cr/Crash",
      "RD": "linndrum-rd/Ride",
      "CB": "linndrum-cb/Cowbell",
      "TB": "linndrum-tb/Tambourine",
      "LC": "linndrum-perc/Conga L-01",
      "MC": "linndrum-perc/Conga M-01",
      "HC": "linndrum-perc/Conga H-01",
      "MA": "linndrum-sh/Cabasa",
    },
  ),
  "505": (
    "505",
    "RolandTR505",
    {
      "BD": "rolandtr505-bd/Bassdrum",
      "SD": "rolandtr505-sd/Snaredrum",
      "RS": "rolandtr505-rim/Rimshot",
      "CP": "rolandtr505-cp/Clap",
      "LT": "rolandtr505-lt/Tom L",
      "MT": "rolandtr505-mt/Tom M",
      "HT": "rolandtr505-ht/Tom H",
      "CH": "rolandtr505-hh/Hat Closed",
      "OH": "rolandtr505-oh/Hat Open",
      "CY": "rolandtr505-cr/Crash",
      "RD": "rolandtr505-rd/Ride",
      "CB": "rolandtr505-cb/Cowbell H",
      "CB2": "rolandtr505-cb/Cowbell L",
      "LC": "rolandtr505-perc/Conga L",
      "HC": "rolandtr505-perc/Conga H",
      "TI": "rolandtr505-perc/Timbale",
    },
  ),
  "DR55": (
    "D55",
    "BossDR55",
    {
      "BD": "bossdr55-bd/Bassdrum-01",
      "BD2": "bossdr55-bd/Bassdrum-02",
      "SD": "bossdr55-sd/Snaredrum-01",
      "SD2": "bossdr55-sd/Snaredrum-02",
      "RS": "bossdr55-rim/Rimshot",
      "CH": "bossdr55-hh/Hihat1",
    },
  ),
  "SK1": (
    "SK1",
    "CasioSK1",
    {
      "BD": "casiosk1-bd/Bassdrum",
      "SD": "casiosk1-sd/Snaredrum",
      "MT": "casiosk1-mt/Tom L",
      "HT": "casiosk1-ht/Tom H",
      "CH": "casiosk1-hh/Hat Closed",
      "OH": "casiosk1-oh/Hat Open",
    },
  ),
  "KPR77": (
    "KPR",
    "KorgKPR77",
    {
      "BD": "korgkpr77-bd/Bassdrum",
      "SD": "korgkpr77-sd/Snaredrum",
      "CP": "korgkpr77-cp/Clap",
      "CH": "korgkpr77-hh/Hat Closed",
      "OH": "korgkpr77-oh/Hat Open",
    },
  ),
  "ACE": (
    "ACE",
    "RhythmAce",
    {
      "BD": "rhythmace-bd/Bassdrum-01",
      "BD2": "rhythmace-bd/Bassdrum-02",
      "SD": "rhythmace-sd/Snaredrum-01",
      "SD2": "rhythmace-sd/Snaredrum-02",
      "LT": "rhythmace-lt/Tom L",
      "HT": "rhythmace-ht/Tom H",
      "CH": "rhythmace-hh/Hat Closed",
      "OH": "rhythmace-oh/Hat Open",
      "CL": "rhythmace-perc/Clave",
    },
  ),
  "LM1": (
    "LM1",
    "LinnLM1",
    {
      "BD": "linnlm1-bd/LM-1_BD_1_TL",
      "BD2": "linnlm1-bd/LM-1_BD_2_TL",
      "SD": "linnlm1-sd/LM-1_SD_1_TL",
      "RS": "linnlm1-rim/LM-1_RIMSHOT_1_TL",
      "CP": "linnlm1-cp/LM-1_CLAP_1_TL",
      "LT": "linnlm1-lt/LM-1_Tom_1_TL",
      "HT": "linnlm1-ht/LM-1_Tom_2_TL",
      "CH": "linnlm1-hh/LM-1_HH_1_TL",
      "OH": "linnlm1-oh/LM-1_HH_2_TL",
      "CB": "linnlm1-cb/LM-1_COWBELL_TL",
      "TB": "linnlm1-tb/LM-1_TAMB_TL",
      "MA": "linnlm1-sh/LM-1_SHAKER_1_TL",
      "MC": "linnlm1-perc/LM-1_BONGO_1_TL",
      "HC": "linnlm1-perc/LM-1_BONGO_2_TL",
      "CL": "linnlm1-perc/LM-1_WOODBLOCK_TL",
    },
  ),
  "SPACE": (
    "SPC",
    "ViscoSpaceDrum",
    {
      "BD": "viscospacedrum-bd/Bassdrum-01",
      "BD2": "viscospacedrum-bd/Bassdrum-02",
      "SD": "viscospacedrum-sd/Snaredrum-01",
      "SD2": "viscospacedrum-sd/Snaredrum-02",
      "RS": "viscospacedrum-rim/Rimshot",
      "CB": "viscospacedrum-cb/Cowbell",
      "LT": "viscospacedrum-lt/Synth Tom L",
      "MT": "viscospacedrum-mt/Synth Tom M-01",
      "HT": "viscospacedrum-ht/Synth Tom H",
      "CH": "viscospacedrum-hh/Hat Closed-01",
      "OH": "viscospacedrum-oh/Hat Open-01",
      "CL": "viscospacedrum-perc/Woodblock1",
    },
  ),
  "RZ1": (
    "RZ1",
    "CasioRZ1",
    {
      "BD": "casiorz1-bd/Bassdrum",
      "SD": "casiorz1-sd/0Snaredrum",
      "RS": "casiorz1-rim/Rim Shot",
      "CP": "casiorz1-cp/Clap",
      "LT": "casiorz1-lt/Tom L",
      "MT": "casiorz1-mt/Tom M",
      "HT": "casiorz1-ht/Tom H",
      "CH": "casiorz1-hh/Hat Closed",
      "OH": "casiorz1-rd/Hat Open",
      "CY": "casiorz1-cr/Crash",
      "RD": "casiorz1-rd/Ride",
      "CB": "casiorz1-cb/Cowbell",
    },
  ),
  "DR110": (
    "110",
    "BossDR110",
    {
      "BD": "bossdr110-bd/Bassdrum",
      "SD": "bossdr110-sd/Snaredrum",
      "CP": "bossdr110-cp/Clap",
      "CH": "bossdr110-hh/Hat Closed",
      "OH": "bossdr110-oh/Hat Open",
      "CY": "bossdr110-cr/Crash",
      "RD": "bossdr110-rd/Ride",
    },
  ),
  "EMU": (
    "EMU",
    "EmuDrumulator",
    {
      "BD": "emudrumulator-bd/Bassdrum",
      "SD": "emudrumulator-sd/0Snaredrum",
      "RS": "emudrumulator-rim/Rim Shot",
      "CP": "emudrumulator-cp/Clap",
      "LT": "emudrumulator-lt/Tom L",
      "MT": "emudrumulator-mt/Tom M",
      "HT": "emudrumulator-ht/Tom H",
      "CH": "emudrumulator-hh/Hat Closed",
      "OH": "emudrumulator-oh/Hat Open",
      "CY": "emudrumulator-cr/Cymbal",
      "CB": "emudrumulator-cb/Cowbell",
      "CL": "emudrumulator-perc/Claves",
    },
  ),
  "RX5": (
    "RX5",
    "YamahaRX5",
    {
      "BD": "yamaharx5-bd/Bassdrum",
      "BD2": "yamaharx5-bd/Bassdrum-02",
      "SD": "yamaharx5-sd/Snaredrum",
      "SD2": "yamaharx5-sd/Snaredrum-02",
      "RS": "yamaharx5-rim/Rimshot",
      "LT": "yamaharx5-lt/Tom",
      "CH": "yamaharx5-hh/Hat Closed",
      "OH": "yamaharx5-oh/Hat Open",
      "CB": "yamaharx5-cb/Cowbell",
      "TB": "yamaharx5-tb/Tambourine",
      "MA": "yamaharx5-sh/Shaker",
    },
  ),
  "HR16": (
    "H16",
    "AlesisHR16",
    {
      "BD": "alesishr16-bd/Bassdrum",
      "SD": "alesishr16-sd/Snaredrum",
      "RS": "alesishr16-rim/Rim",
      "CP": "alesishr16-cp/Clap",
      "LT": "alesishr16-lt/Tom-1",
      "HT": "alesishr16-ht/Tom-2",
      "CH": "alesishr16-hh/Closed Hat",
      "OH": "alesishr16-oh/Open Hat",
      "LC": "alesishr16-perc/Conga L",
      "HC": "alesishr16-perc/Conga H",
      "TI": "alesishr16-perc/Timbale",
      "CL": "alesishr16-perc/Claves",
      "CB2": "alesishr16-perc/Agogo Bell",
      "MA": "alesishr16-sh/Maracas",
    },
  ),
  "SDS5": (
    "SD5",
    "SimmonsSDS5",
    {
      "BD": "simmonssds5-bd/Bassdrum-01",
      "SD": "simmonssds5-sd/Snaredrum-01",
      "SD2": "simmonssds5-sd/Snaredrum-02",
      "RS": "simmonssds5-rim/Rimshot-01",
      "LT": "simmonssds5-lt/Tom-07",
      "MT": "simmonssds5-mt/Tom-02",
      "HT": "simmonssds5-ht/Tom-01",
      "CH": "simmonssds5-hh/Hat Closed-01",
      "OH": "simmonssds5-oh/Hat Open-01",
    },
  ),
  "TRAKS": (
    "TRK",
    "SequentialCircuitsDrumtracks",
    {
      "BD": "sequentialcircuitsdrumtracks-bd/Bassdrum",
      "SD": "sequentialcircuitsdrumtracks-sd/Snaredrum",
      "RS": "sequentialcircuitsdrumtracks-rim/Rim Shot",
      "CP": "sequentialcircuitsdrumtracks-cp/Clap",
      "HT": "sequentialcircuitsdrumtracks-ht/Tom",
      "CH": "sequentialcircuitsdrumtracks-hh/Hat Closed",
      "OH": "sequentialcircuitsdrumtracks-oh/Hat Open",
      "CY": "sequentialcircuitsdrumtracks-cr/Crash",
      "RD": "sequentialcircuitsdrumtracks-rd/Ride",
      "CB": "sequentialcircuitsdrumtracks-cb/Cowbell",
      "TB": "sequentialcircuitsdrumtracks-tb/Tambourine",
      "MA": "sequentialcircuitsdrumtracks-sh/Cabasa",
    },
  ),
  "MPC60": (
    "MPC",
    "AkaiMPC60",
    {
      "BD": "akaimpc60-bd/0 Bassdrum",
      "BD2": "akaimpc60-bd/Bassdrum Gated",
      "SD": "akaimpc60-sd/Snare 1",
      "SD2": "akaimpc60-sd/Snare 2",
      "RS": "akaimpc60-rim/Rim Gated",
      "CP": "akaimpc60-cp/Clap",
      "LT": "akaimpc60-lt/Tom L",
      "MT": "akaimpc60-mt/Tom M",
      "HT": "akaimpc60-ht/Tom H",
      "CH": "akaimpc60-hh/Closed Hat",
      "OH": "akaimpc60-oh/Open Hat",
      "CY": "akaimpc60-cr/Crash",
      "RD": "akaimpc60-rd/Ride",
      "LC": "akaimpc60-perc/Conga L",
      "HC": "akaimpc60-perc/Conga H",
      "MC": "akaimpc60-perc/Bongo",
      "TI": "akaimpc60-perc/Timbale",
      "CL": "akaimpc60-perc/Click",
    },
  ),
}


def fetch(cache, folder, rel):
  path = Path(cache) / folder / (rel + ".wav")
  if not path.exists():
    url = f"{REPO}/{REF}/machines/{urllib.parse.quote(folder + '/' + rel)}.wav"
    with urllib.request.urlopen(url, timeout=20) as r:
      data = r.read()
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)
  return path


def load_mono(path):
  with wave.open(str(path), "rb") as w:
    ch, width, rate, n = w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getnframes()
    raw = w.readframes(n)
  if width == 1:
    vals = [(b - 128) << 8 for b in raw]
  elif width == 2:
    vals = list(struct.unpack(f"<{len(raw) // 2}h", raw))
  elif width == 3:
    vals = [int.from_bytes(raw[i + 1 : i + 3], "little", signed=True) for i in range(0, len(raw), 3)]
  else:
    vals = [v >> 16 for v in struct.unpack(f"<{len(raw) // 4}i", raw)]
  return [sum(vals[i : i + ch]) / ch for i in range(0, len(vals), ch)], rate


HALFBAND = None


def lowpass(x):
  """a windowed-sinc low-pass at a quarter of the rate (31 taps)"""
  global HALFBAND
  if HALFBAND is None:
    import math

    mid, h = 15, []
    for i in range(31):
      n = i - mid
      s = 0.5 if n == 0 else math.sin(math.pi * n * 0.5) / (math.pi * n)
      h.append(s * (0.54 + 0.46 * math.cos(math.pi * n / (mid + 1))))
    g = sum(h)
    HALFBAND = [v / g for v in h]
  out = []
  for i in range(len(x)):
    acc = 0.0
    for j, c in enumerate(HALFBAND):
      k = i + j - 15
      if 0 <= k < len(x):
        acc += c * x[k]
    out.append(acc)
  return out


def prepare(x, rate, target):
  x = x[: int(rate * MAX_SECONDS)]
  fade = int(rate * FADE_SECONDS)
  if len(x) >= int(rate * MAX_SECONDS) and fade:
    for i in range(fade):
      x[-fade + i] *= 1 - i / fade
  peak = max(1.0, max(abs(v) for v in x))
  end = len(x)
  while end > 1 and abs(x[end - 1]) < peak / 1000:  # the silent tail
    end -= 1
  x = x[:end]
  if rate >= 32000:
    lp = lowpass(x)
    total = sum(v * v for v in x) or 1.0
    if sum((a - b) ** 2 for a, b in zip(x, lp)) / total < HALVE_BELOW:
      x, rate = lp[::2], rate // 2
  peak = max(1.0, max(abs(v) for v in x))
  return [v * target * 32767 / peak for v in x], rate


STEP = [
  7,
  8,
  9,
  10,
  11,
  12,
  13,
  14,
  16,
  17,
  19,
  21,
  23,
  25,
  28,
  31,
  34,
  37,
  41,
  45,
  50,
  55,
  60,
  66,
  73,
  80,
  88,
  97,
  107,
  118,
  130,
  143,
  157,
  173,
  190,
  209,
  230,
  253,
  279,
  307,
  337,
  371,
  408,
  449,
  494,
  544,
  598,
  658,
  724,
  796,
  876,
  963,
  1060,
  1166,
  1282,
  1411,
  1552,
  1707,
  1878,
  2066,
  2272,
  2499,
  2749,
  3024,
  3327,
  3660,
  4026,
  4428,
  4871,
  5358,
  5894,
  6484,
  7132,
  7845,
  8630,
  9493,
  10442,
  11487,
  12635,
  13899,
  15289,
  16818,
  18500,
  20350,
  22385,
  24623,
  27086,
  29794,
  32767,
]
INDEX = [-1, -1, -1, -1, 2, 4, 6, 8]


def adpcm(x, idx):
  """IMA ADPCM: 4 bits a sample from step index idx (the decoder in src/drums.c is the same steps)"""
  pred, codes, err = 0, [], 0
  for v in x:
    v = int(round(max(-32768, min(32767, v))))
    step, d, code = STEP[idx], v - pred, 0
    if d < 0:
      code, d = 8, -d
    diff = step >> 3
    if d >= step:
      code, d, diff = code | 4, d - step, diff + step
    if d >= step >> 1:
      code, d, diff = code | 2, d - (step >> 1), diff + (step >> 1)
    if d >= step >> 2:
      code, diff = code | 1, diff + (step >> 2)
    pred = max(-32768, min(32767, pred - diff if code & 8 else pred + diff))
    idx = max(0, min(88, idx + INDEX[code & 7]))
    codes.append(code)
    err += (v - pred) ** 2
  return codes, err


def encode(x):
  """the codes, packed two a byte (the first in the low nibble), and the step index to start from: the one that
  follows the attack best"""
  idx0 = min(range(0, 89, 2), key=lambda i: adpcm(x[:256], i)[1])
  codes, _ = adpcm(x, idx0)
  codes += [0] * (len(codes) & 1)
  return bytes(codes[i] | codes[i + 1] << 4 for i in range(0, len(codes), 2)), idx0


def generate(cache, out):
  stamp = Path(out).with_suffix(".stamp")  # the same table and code as last time: the same header
  key = hashlib.sha1((Path(__file__).read_text() + (Path(__file__).parent / "drums.py").read_text()).encode()).hexdigest()
  if Path(out).exists() and stamp.exists() and stamp.read_text().split("\n")[0] == key:
    names, total = stamp.read_text().split("\n")[1:3]
    return names.split(), int(total)
  names, labels, parts, rows, total, left = [], [], [], [], 0, []
  for name in INCLUDE:
    label, folder, voices = MACHINES[name]
    try:
      got = {v: load_mono(fetch(cache, folder, rel)) for v, rel in voices.items()}
    except OSError as e:
      print(f"roms: {name} left out ({e})", file=sys.stderr)
      continue
    enc = {v: (encode(x), rate) for v, (x, rate) in ((v, prepare(x, r, drums.TARGET[v])) for v, (x, r) in got.items())}
    size = sum(len(d) for (d, _), _ in enc.values())
    if total + size > BUDGET:
      left.append(name)
      continue
    m = len(names)
    names.append(name)
    labels.append(label)
    total += size
    cells = []
    for v, ((data, idx0), rate) in enc.items():
      parts.append(f"static const uint8_t R{m}_{v}[{len(data)}] = {{{','.join(map(str, data))}}};\n")
      cells.append(f"[V_{v}] = {{R{m}_{v}, {len(data) * 2}, {round(rate * 65536 / RATE)}, {idx0}}}")
    rows.append("  {" + ", ".join(cells) + "},\n")
  if left:
    print(f"roms: no room for {' '.join(left)} (BUDGET in tools/roms.py)", file=sys.stderr)
  if not names:
    rows.append("  {{0, 0, 0, 0}},\n")
  n = str(max(1, len(names)))
  text = (
    "/* made by tools/roms.py: do not edit */\n#ifndef ROMS_H\n#define ROMS_H\n#include <stdint.h>\n\n/* after drums.h (the V_ voices) */\n"
    + f"#define ROM_MACHINES {len(names)}\n"
    + f"static const char *const ROM_NAME[{n}] = {{"
    + (", ".join(f'"{x}"' for x in names) or '""')
    + "};\n"
    + f"static const char *const ROM_LABEL[{n}] = {{"
    + (", ".join(f'"{x}"' for x in labels) or '""')
    + "};\n\n"
    + "".join(parts)
    + "\ntypedef struct {\n  const uint8_t *data; /* IMA ADPCM, two samples a byte, the first in the low nibble */\n"
    + "  uint32_t len, step; /* samples; step: Q16 samples of it per frame out */\n  uint8_t idx0;       /* the step index it starts at */\n} rom_t;\n\n"
    + f"static const rom_t ROM[{n}][V_COUNT] = {{\n"
    + "".join(rows)
    + "};\n\n#endif\n"
  )
  out = Path(out)
  out.parent.mkdir(parents=True, exist_ok=True)
  if not out.exists() or out.read_text() != text:
    out.write_text(text)
  stamp.write_text(f"{key}\n{' '.join(names)}\n{total}\n")
  return names, total


if __name__ == "__main__":
  root = Path(__file__).resolve().parents[1]
  names, size = generate(root / "roms", root / "build" / "gen" / "roms.h")
  print(f"{len(names)} ROM machines ({' '.join(names)}), {size // 1024} KiB")
