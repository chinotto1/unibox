#!/usr/bin/env python3
"""Convert samples/*.wav to build/gen/samples.h: 16-bit mono at 22050 Hz, stored in flash.

The build runs this automatically, so a .wav dropped into samples/ is in the next build. Any PCM .wav works
(8, 16, 24 or 32-bit, mono or stereo, any sample rate). The name shown on screen is the file name in capitals,
cut to 6 characters, so keep names short (01-kick.wav sorts first and shows as KICK).
"""

import re
import struct
import sys
import wave
from pathlib import Path

RATE = 22050
MAX_SECONDS = 2.0
BUDGET = 300 * 1024  # bytes of flash for all the samples together


def read(path):
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
  mono = [sum(vals[i : i + ch]) // ch for i in range(0, len(vals), ch)]
  return mono, rate


def resample(x, rate):
  if rate == RATE or not x:
    return x
  step, out, pos = rate / RATE, [], 0.0
  while pos < len(x) - 1:
    i = int(pos)
    f = pos - i
    out.append(round(x[i] * (1 - f) + x[i + 1] * f))
    pos += step
  return out


def trim(x):
  x = x[: int(RATE * MAX_SECONDS)]
  end = len(x)
  while end > 1 and abs(x[end - 1]) < 64:  # the silent tail
    end -= 1
  return x[:end] + [0, 0]  # two zeros: the player reads one ahead


def label(path):
  name = re.sub(r"^[0-9]+[-_ ]*", "", path.stem).upper()
  return re.sub(r"[^A-Z0-9]", "", name)[:6] or "SMP"


def generate(src, out):
  files = sorted(Path(src).glob("*.wav")) if Path(src).is_dir() else []
  parts, table, total = [], [], 0
  for i, f in enumerate(files):
    try:
      x, rate = read(f)
    except (wave.Error, EOFError, struct.error) as e:
      print(f"samples: {f.name} skipped ({e})", file=sys.stderr)
      continue
    x = trim(resample(x, rate))
    total += 2 * len(x)
    rows = ",".join(str(max(-32768, min(32767, v))) for v in x)
    parts.append(f"static const int16_t SMP{i}[{len(x)}] = {{{rows}}};\n")
    table.append(f'  {{"{label(f)}", SMP{i}, {len(x)}}},\n')
  if total > BUDGET:
    raise ValueError(f"samples: {total // 1024} KiB, more than the {BUDGET // 1024} KiB there is room for: shorten or remove some")
  count = len(table)
  if not table:
    parts.append("static const int16_t SMP_NONE[2] = {0, 0};\n")
    table.append('  {"NONE", SMP_NONE, 2},\n')
  text = (
    "/* made by tools/samples.py from the .wav files in samples/: do not edit */\n#ifndef SAMPLES_H\n#define SAMPLES_H\n#include <stdint.h>\n\n"
    + f"#define SAMPLE_RATE_SMP {RATE}\n#define SAMPLE_COUNT {count}\n\n"
    + "".join(parts)
    + "\nstatic const struct {\n  const char *name;\n  const int16_t *data;\n  uint32_t len;\n} SAMPLES[] = {\n"
    + "".join(table)
    + "};\n\n#endif\n"
  )
  out = Path(out)
  out.parent.mkdir(parents=True, exist_ok=True)
  if not out.exists() or out.read_text() != text:  # unchanged: leave it, the dev server watches it
    out.write_text(text)
  return count, total


if __name__ == "__main__":
  root = Path(__file__).resolve().parents[1]
  n, size = generate(root / "samples", root / "build" / "gen" / "samples.h")
  print(f"{n} samples, {size // 1024} KiB")
