#!/usr/bin/env python3
"""Generate BAR-ROCK demo data from the supplied Dido MIDI.

The canonical demo is firmware/bar-rock/demo/2465dido.mid -- a multi-track MIDI
of Henry Purcell's "Dido's Lament" (track metadata credits Emily Gray for the
sequence).  This tool does not compose or write a MIDI; it only reads that file
and emits:

  * wasmtest-demo.json  -- a scripted render of the whole piece, tempo map intact
  * demo_snippet.h      -- a compact 12 s excerpt for the on-device DEMO button

Standard library only.
"""

import json
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DEMO_MID = ROOT / "firmware" / "bar-rock" / "demo" / "2465dido.mid"
OUT_JSON = ROOT / "firmware" / "bar-rock" / "wasmtest-demo.json"
OUT_HEADER = ROOT / "firmware" / "bar-rock" / "demo_snippet.h"

TAIL_SECONDS = 2.5
SHOTS = [
    (5.0, "dido-01-opening"),
    (45.0, "dido-02-middle"),
    (90.0, "dido-03-development"),
    (135.0, "dido-04-lament"),
]

# A musically complete phrase: the first entry of the busy middle section.
SNIPPET_START = 24.4
SNIPPET_SECONDS = 12.0
SNIPPET_END = SNIPPET_START + SNIPPET_SECONDS


def read_vlq(data, pos):
    value = 0
    while True:
        byte = data[pos]
        pos += 1
        value = (value << 7) | (byte & 0x7F)
        if not byte & 0x80:
            return value, pos


def parse_midi(path):
    """Return (events, ticks_per_beat); events are (tick, priority, payload)."""
    data = path.read_bytes()
    if data[:4] != b"MThd":
        raise ValueError("%s is not a Standard MIDI File" % path)
    header_len = struct.unpack_from(">I", data, 4)[0]
    _fmt, tracks, division = struct.unpack_from(">HHH", data, 8)
    if division & 0x8000:
        raise ValueError("SMPTE time divisions are not supported")

    events = []
    pos = 8 + header_len
    for _track in range(tracks):
        if data[pos:pos + 4] != b"MTrk":
            raise ValueError("missing MTrk at byte %d" % pos)
        length = struct.unpack_from(">I", data, pos + 4)[0]
        body = data[pos + 8:pos + 8 + length]
        pos += 8 + length

        tick = 0
        i = 0
        running = None
        while i < len(body):
            delta, i = read_vlq(body, i)
            tick += delta
            byte = body[i]
            if byte == 0xFF:
                i += 1
                kind = body[i]
                i += 1
                size, i = read_vlq(body, i)
                payload = body[i:i + size]
                i += size
                if kind == 0x51 and len(payload) == 3:
                    events.append((tick, -1, ("tempo", int.from_bytes(payload, "big"))))
            elif byte in (0xF0, 0xF7):
                i += 1
                size, i = read_vlq(body, i)
                i += size
            else:
                if byte & 0x80:
                    status = byte
                    i += 1
                    running = status
                else:
                    status = running
                    if status is None:
                        raise ValueError("running status without a status byte")
                kind = status & 0xF0
                if kind in (0x80, 0x90, 0xA0, 0xB0, 0xE0):
                    note, velocity = body[i], body[i + 1]
                    i += 2
                    if kind == 0x90 and velocity > 0:
                        events.append((tick, 1, ("on", note, velocity)))
                    elif kind == 0x80 or (kind == 0x90 and velocity == 0):
                        events.append((tick, 0, ("off", note, 0)))
                elif kind in (0xC0, 0xD0):
                    i += 1
                else:
                    raise ValueError("unsupported MIDI status 0x%02X" % status)

    events.sort(key=lambda e: (e[0], e[1]))
    return events, division


def timeline(path):
    """Return (timed_events, ticks_per_beat, last_second)."""
    events, ticks_per_beat = parse_midi(path)
    seconds = 0.0
    last_tick = 0
    tempo = 500000  # microseconds per quarter, MIDI default
    timed = []

    for tick, _priority, payload in events:
        seconds += (tick - last_tick) * tempo / ticks_per_beat / 1_000_000.0
        last_tick = tick
        if payload[0] == "tempo":
            tempo = payload[1]
            continue
        timed.append((seconds, payload[0], payload[1], payload[2]))
    return timed, ticks_per_beat, seconds


def note_steps(path):
    timed, _division, last_second = timeline(path)
    steps = [
        {"t": round(sec, 3), "ev": [9, note, 1, vel]}
        if kind == "on" else
        {"t": round(sec, 3), "ev": [9, note, 0, 0]}
        for sec, kind, note, vel in timed
    ]
    for when, shot in SHOTS:
        steps.append({"t": when, "shot": shot})
    steps.sort(key=lambda s: (s["t"], 1 if "shot" in s else 0))
    return steps, last_second


def snippet(path):
    """Return (events, seconds): compact (ms, note, velocity) tuples.

    A zero velocity is a note-off.  Notes still sounding at the end of the
    excerpt get an explicit off so the demo never leaves a voice hanging.
    """
    timed, _division, _last = timeline(path)
    chosen = [e for e in timed if SNIPPET_START <= e[0] < SNIPPET_END]

    active = {}
    for _sec, kind, note, _vel in chosen:
        if kind == "on":
            active[note] = active.get(note, 0) + 1
        elif active.get(note, 0) > 0:
            active[note] -= 1
    for note, count in active.items():
        for _ in range(count):
            chosen.append((SNIPPET_END, "off", note, 0))

    out = []
    for index, (sec, kind, note, vel) in enumerate(chosen):
        ms = int(round((sec - SNIPPET_START) * 1000.0))
        if ms < 0:
            ms = 0
        if ms > 65535:
            ms = 65535
        out.append((ms, note, vel if kind == "on" else 0, index))
    out.sort(key=lambda e: (e[0], e[3]))
    return [(ms, note, vel) for ms, note, vel, _index in out], SNIPPET_SECONDS


def write_header(events, seconds):
    lines = [
        "/* Generated by tools/make_demo.py from firmware/bar-rock/demo/2465dido.mid.",
        " * Dido's Lament -- Henry Purcell, MIDI sequence by Emily Gray.",
        " * Do not edit by hand; rerun `python tools/make_demo.py`.",
        " */",
        "#ifndef BR_DEMO_SNIPPET_H",
        "#define BR_DEMO_SNIPPET_H",
        "",
        "#define BR_DEMO_SECONDS %.3ff" % seconds,
        "#define BR_DEMO_NOTE_COUNT %d" % len(events),
        "",
        "/* A note-on has velocity 1..127; velocity 0 releases that pitch. */",
        "typedef struct {",
        "  unsigned short t_ms;",
        "  unsigned char note;",
        "  unsigned char vel;",
        "} br_demo_note;",
        "",
        "static const br_demo_note BR_DEMO_NOTES[BR_DEMO_NOTE_COUNT] = {",
    ]
    for ms, note, vel in events:
        lines.append("  { %5d, %3d, %3d }," % (ms, note, vel))
    lines.extend(["};", "", "#endif", ""])
    OUT_HEADER.write_text("\n".join(lines), encoding="utf-8")
    return len(events)


def main():
    steps, last_second = note_steps(DEMO_MID)
    document = {
        "seconds": round(last_second + TAIL_SECONDS, 3),
        "float": True,
        "steps": steps,
    }
    OUT_JSON.write_text(json.dumps(document, indent=2) + "\n", encoding="utf-8")

    notes, seconds = snippet(DEMO_MID)
    count = write_header(notes, seconds)
    print("wrote %s (%d steps, %.1f s)" % (OUT_JSON, len(steps), document["seconds"]))
    print("wrote %s (%d notes, %.1f s)" % (OUT_HEADER, count, seconds))


if __name__ == "__main__":
    main()
