# Dido's Lament demo for BAR-ROCK

The demo source is **`2465dido.mid`**, a supplied multi-track Standard MIDI File of Henry Purcell's
"Dido's Lament". The file's own track metadata credits **Emily Gray** for the sequence. The
composition is public domain; the MIDI file is used here as provided.

- **File:** `2465dido.mid` (format 1, 192 PPQ, four tracks, 3/2, three tempo changes)
- **Length:** about 2:19 of music, plus a 2.5 s reverb tail in the scripted render (~141 s total)
- **Range:** MIDI notes 29–82 across three note-bearing channels
- **Script:** `wasmtest-demo.json` is generated from this file; it plays the source notes unchanged,
  including its tempo map. No registration automation is added.
- **On-device snippet:** `../demo_snippet.h` is a generated 12-second excerpt from 24.4 s into the
  piece, with note-offs appended for any voice still sounding at the end. F3 / PLAY plays it from
  fixed memory; pressing F3 again or PANIC stops it early.

## Play it

Drop `2465dido.mid` onto a DAW track whose instrument is BAR-ROCK, or load it into the VIBE FM
emulator. All three MIDI channels feed the same 16-voice allocator; note-offs release one matching
voice, so overlapping pitches in different parts are not cut short together.

On the device or in the Editor, press **F3 / PLAY** (or the Editor's **play demo** button) for a
12-second taster. The excerpt temporarily enables both 8' choirs so it is audible whatever the
current registration; it does not change your stored preset.

## Render it with the firmware

From the repository root:

```sh
python tools/make_demo.py            # regenerate wasmtest-demo.json and demo_snippet.h
mkdir -p sdk/build/bar-rock-demo
node sdk/tools/wasmtest.mjs sdk/build/bar-rock.wasm \
  firmware/bar-rock/wasmtest-demo.json sdk/build/bar-rock-demo

mkdir -p sdk/build/bar-rock-snippet
node sdk/tools/wasmtest.mjs sdk/build/bar-rock.wasm \
  firmware/bar-rock/wasmtest-snippet.json sdk/build/bar-rock-snippet
```

That writes `audio.wav` and display screenshots into each build directory. The converter is
`tools/make_demo.py`; it only reads the MIDI file, it does not compose or overwrite it.
