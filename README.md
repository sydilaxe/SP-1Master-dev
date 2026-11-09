# SP-1 Master

Firmware that turns the Teenage Engineering SP-1 into a mastering processor. Play audio into it
over USB-C (it shows up as a sound card, "SP-1 Master", on a computer, phone or tablet), pick one
of four mastering chains with the TRACK buttons, shape it with the four faders, and hear it on
headphones or the speaker.

**Firmware:** `sp1master.bin` on each [release](../../releases) (CI builds it on every push)

**Manual:** `SP-1-Master-manual.pdf` on each [release](../../releases) (source: [`docs/manual/manual.md`](docs/manual/manual.md))

> **Not yet run on a real SP-1.** The engine passes its tests, and the platform code is
> [SP-1FX](https://github.com/sydilaxe/SP-1FX-dev)'s (which follows firmware that does run on the
> SP-1), but this binary has not been on hardware. Before flashing, read
> [Flashing and getting back](#flashing-and-getting-back).

## The chains

One at a time, picked with the TRACK buttons. Each is in the style of a classic mastering
approach; none is a circuit model of a particular piece of equipment.

| TRACK | Chain | In the style of | Sound |
|---|---|---|---|
| 1 | **London** | a 1960s London studio master | warm, round, gently glued |
| 2 | **Punch** | a New York rock and pop master | loud, forward, punchy |
| 3 | **Clean** | a modern streaming master | transparent, even, hi-fi |
| 4 | **Lacquer** | a vinyl cutting room | vintage but tight, tame top, mono bass |

Switching dips: the old chain fades out in 15 ms, the new one settles muted for 30 ms (so its
compressor has caught the music), then fades in over 15 ms. Only one chain ever runs.

## The faders

The same four stages in every chain, in signal order; each has a second parameter on page 2.

| Fader | Stage | Down | Up | Page 2 |
|---|---|---|---|---|
| 1 | **Tone** | flat | the chain's EQ curve | tilt, ±4 dB at the shelves, centre neutral |
| 2 | **Glue** | off | more compression, with makeup gain | speed: attack and release, slow to fast |
| 3 | **Colour** | off | the chain's character stage | stereo width: mono, as is (centre), wide |
| 4 | **Loudness** | 0 dB | +12 dB into the limiter | limiter release: 400 ms, 100 ms, 25 ms |

| Chain | Tone at full | Glue | Colour | Loudness extra |
|---|---|---|---|---|
| London | +4 dB at 100 Hz, +1.5 dB at 2.8 kHz, +3 dB at 10 kHz | slow tube-style, very soft knee, about 2.5:1 | console transformer saturation | |
| Punch | +5 dB at 60 Hz, -2.5 dB at 220 Hz, +3.5 dB at 10 kHz | 4:1 bus compressor, auto release, ignores below 90 Hz | parallel compression, up to half, level-trimmed | soft clipper before the limiter |
| Clean | +2.5 dB at 80 Hz, -1.5 dB at 350 Hz, +4 dB at 12 kHz | three bands (180 Hz, 3.5 kHz), 2.5:1 each | a saturated air band above 7 kHz | |
| Lacquer | +2.5 dB at 120 Hz, +1 dB at 3.5 kHz, -2.5 dB at 9 kHz | gentle 2:1 tube-style, slower after loud passages | mono bass, below 40 Hz up to 300 Hz | the band above 6 kHz held under -16 dBFS |

## The crush page

**FWD (>>)** puts a bit crusher and filter, from SP-1FX, on the faders. It sits **before the
chain**, so the chain masters the crushed sound, and it stays set when you switch chains.

| Fader | Control | Down | Up |
|---|---|---|---|
| 1 | **Amount** | off | all crushed (a mix in between) |
| 2 | **Bits** | 16 bits | 1 bit (2^N levels, so 1 bit is a square at the music's level) |
| 3 | **Rate** | 48 kHz | 2 kHz, sample-and-hold |
| 4 | **Filter** | high-pass, up to 6 kHz | low-pass, down to 60 Hz (the middle fifth flat; 24 dB/oct, resonant) |

The filter works whatever Amount is, after the crusher. At power on Amount is down and Filter is
centred, so the page does nothing until used. Bypass skips it.

After the chain, a **look-ahead limiter** (-1 dBFS ceiling, 1 ms look-ahead) is always on, also
in bypass.

**Full bandwidth, 16- or 24-bit:** USB audio is 48 kHz, 16-bit or 24-bit (the host picks), out at
24-bit; the engine runs at 48 kHz in 32-bit floating point. With every fader down, every page 2
parameter centred and the crush page off, the output is the input, bit for bit, 48 samples (1 ms) later.

## Controls

| | |
|---|---|
| TRACK 1-4 | the chain (also leaves bypass) |
| Faders 1-4 | the stages (page 1), their second parameters (page 2) or the crusher (crush page) |
| FWD (>>) | the crush page; again for page 1 |
| RWD (<<) | page 2; again for page 1 |
| PLAY | bypass: the dry signal, crossfaded over 50 ms (the limiter stays; not level-matched) |
| VOL + / VOL - | output volume, 3 dB steps |
| FUNCTION, hold 1.5 s | power on |
| FUNCTION, hold 2.5 s | power off |
| TRACK 1 + TRACK 4, hold 3 s | restart into the bootloader, for flashing |

**Pick-up:** after a page change, a parameter keeps its value until its fader reaches it (or
sweeps past it), so nothing jumps. Its track light blinks until then.

Lights: the track lights show the chain (faint in bypass). For 0.6 s after FWD or RWD the status
lights show the page (left pair = page 1, right pair = page 2, all four = crush). Otherwise they
show the gain reduction, compressor and limiter together (0.5, 2, 4 and 8 dB, the fourth blinking
while on page 2, the first on the crush page), the volume for a second after you change it, and breathe slowly in bypass. Plug in
headphones and the speaker turns off.

## Flashing and getting back

The SP-1 is flashed with the [Solderless updater](https://solderless.engineering) in Chrome or Edge:

1. Connect the SP-1 with a USB-C cable.
2. Put it in firmware-loading mode: **hold TRACK 1 + TRACK 4 while you plug in USB** (the
   bootloader's own check, so it works whatever firmware is on it), or, with SP-1 Master, SP-1FX
   or the tape looper running, hold them for 3 seconds. The four track lights come on.
3. Choose `sp1master.bin`, flash, unplug, plug back in.

To go back to other firmware, the same steps with that firmware's `.bin`. The SP-1 has no reset
button: if anything ever hangs, the watchdog restarts it within 4 seconds; if it won't start
at all, step 2's plug-in hold still reaches the bootloader.

## Status line

Open the SP-1's USB serial port (any baud rate) for a line a second: `chain=` the chain (1-4),
`page=` the page (3 = crush), `f=` fader readings, `cr=` the crush page in percent, `byp=` bypass, `usb=` streaming, `fill=` the input buffer, `under= over=`
buffer under/overruns, `blk=last/max us` the time one 2.67 ms audio block takes (the CPU check: it
must stay well under 2667), `hp=` headphones, `gr=` gain reduction, `flt=` a crash from the last
boot. If something is wrong on the hardware, this line is the thing to send.

## CPU and memory

- RAM: about 39 KB of 256 KB (the engine is under 4 KB of it).
- CPU, worst case (the Clean chain, every fader full and moving, the crush page fully on and
  moving, full-scale noise): about 51% of
  the 64 MHz Cortex-M4, estimated by `tools/bench_m4.py`, which runs the engine built for the M4
  in an emulator and counts cycles from the M4's instruction timings. Only one chain ever runs,
  so a switch never adds to it. The estimate leaves out USB interrupts, cache misses and pipeline
  stalls, which on the SP-1 add a good deal, so CI fails it above 55%. The real figure is the
  status line's `blk=`.

## Building

```bash
make test        # the engine on your computer: checks, and WAVs in build/test
make firmware    # sp1master.bin; needs ZEPHYR_BASE (Zephyr v4.3.1) and the Zephyr SDK 0.17.4
make bench       # the Cortex-M4 cycle estimate (pip install unicorn capstone)
./scripts/build-manual.sh   # the user manual, build/manual/SP-1-Master-manual.pdf (pandoc + Chrome)
```

`build/master_test OUTDIR IN.wav CHAIN F1 F2 F3 F4` also runs your own 48 kHz WAV through chain
1-4 at those fader positions, into `OUTDIR/yours.wav`.

## Layout

| | |
|---|---|
| `master/` | the mastering engine: `master.c`, `master.h`, `fx_math.h` (no libm, no heap) |
| `firmware/` | Zephyr application: USB audio (UAC2), I2S, codecs, faders, buttons, lights, power |
| `boards/` | the SP-1 board definition (`stem_player`) |
| `test/master_test.c` | the engine's host checks, and WAVs to listen to |
| `tools/bench_m4.py` | the CPU estimate |
| `docs/manual/` | the user manual |

## Credits

- [sydilaxe/SP-1FX](https://github.com/sydilaxe/SP-1FX-dev): the platform code, the filter and
  compressor building blocks, and the look-ahead limiter this firmware starts from.
- [chattock/sp1-tape-looper](https://github.com/chattock/sp1-tape-looper) (MIT): the board
  definition, the UAC2 backport and the platform code SP-1FX follows. Its licence is in
  `firmware/LICENSE-sp1-tape-looper`.
- [timknapen/SP-1-dev](https://github.com/timknapen/SP-1-dev): the SP-1's pinout, peripherals and
  bootloader.

Not made or endorsed by Teenage Engineering, or by any studio or equipment maker whose style a
chain follows.
