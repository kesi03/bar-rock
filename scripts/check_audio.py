#!/usr/bin/env python3
"""Guard against the looped-demo buzz: fail on DC offset or silent renders.

The VIBE WAVs are 32-bit float; plain 16-bit PCM is accepted too.  No third-party
modules, so this runs in CI with a bare Python 3.
"""
import struct
import sys
from array import array

MAX_DC = 0.002      # 0.2 % full scale -- far below anything audible as a rattle
MIN_PEAK = 0.02     # the note renders must not come back silent


def read_wav(path):
    with open(path, "rb") as f:
        data = f.read()
    if data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        raise ValueError(f"{path}: not a RIFF/WAVE file")
    pos, fmt, pcm = 12, None, None
    while pos + 8 <= len(data):
        cid = data[pos:pos + 4]
        size = struct.unpack_from("<I", data, pos + 4)[0]
        body = data[pos + 8:pos + 8 + size]
        if cid == b"fmt ":
            fmt = struct.unpack_from("<HHIIHH", body, 0)
        elif cid == b"data":
            pcm = body
        pos += 8 + size + (size & 1)
    if fmt is None or pcm is None:
        raise ValueError(f"{path}: missing fmt/data chunk")
    tag, channels, _sr, _rate, _block, bits = fmt
    if tag == 3 and bits == 32:
        samples = array("f")
        samples.frombytes(pcm)
    elif tag == 1 and bits == 16:
        raw = array("h")
        raw.frombytes(pcm)
        samples = array("f", (v / 32768.0 for v in raw))
    else:
        raise ValueError(f"{path}: unsupported WAV format {tag}/{bits}")
    if sys.byteorder != "little":
        samples.byteswap()
    return channels, samples


def check(path):
    channels, samples = read_wav(path)
    n = len(samples)
    if n == 0:
        raise SystemExit(f"FAIL {path}: empty audio")
    per_channel = n // channels
    for i in range(channels):
        ch = samples[i::channels]
        dc = sum(ch) / len(ch)
        peak = max(abs(v) for v in ch)
        print(f"{path} ch{i}: dc={dc:+.6f} peak={peak:.3f}")
        if abs(dc) > MAX_DC:
            raise SystemExit(f"FAIL {path} ch{i}: DC offset {dc:+.6f} > {MAX_DC}")
        if peak < MIN_PEAK:
            raise SystemExit(f"FAIL {path} ch{i}: peak {peak:.3f} < {MIN_PEAK}")
    print(f"OK {path} ({per_channel} frames, {channels} channels)")


if __name__ == "__main__":
    for path in sys.argv[1:]:
        check(path)
