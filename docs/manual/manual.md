---
title: SP-1 Master
subtitle: A mastering processor. Four mastering chains on the TRACK buttons, four stages on the faders.
pdf-name: SP-1-Master-manual
---

<!-- Keep this manual in step with the firmware: any change to what a control does, a light shows,
     a range, a timing or the flashing steps belongs here too. CI builds it into
     SP-1-Master-manual.pdf and attaches it to every release. -->

# Before you start

SP-1 Master is replacement firmware for the Teenage Engineering SP-1. It turns the SP-1 into a
mastering processor: you play audio into it over USB-C, pick one of four mastering chains with
the TRACK buttons, shape it with the four faders, and hear the result on headphones or the
built-in speaker.

> **Test build.** This firmware's mastering engine passes its tests, and its platform code follows
> firmware that does run on the SP-1, but it has not yet been run on a real SP-1. Read
> [Flashing and getting back](#flashing-and-getting-back) before you install it: the SP-1 can
> always be returned to other firmware.

## What you need

- A Teenage Engineering SP-1.
- A USB-C cable.
- An audio source that can play to a USB sound card: a computer, phone, tablet or Linux handheld.
- For installing: a computer with Chrome or Edge.

## Quick start

1. Install the firmware (see [Flashing and getting back](#flashing-and-getting-back)).
2. Hold **FUNCTION** for 1.5 seconds to power on.
3. Connect the SP-1 to your computer or phone with USB-C. It appears as a sound card called
   **SP-1 Master**. Choose it as the audio output and play something.
4. Plug in headphones, or listen on the speaker.
5. Press **TRACK 1-4** to pick a chain, and push the faders up to bring in its stages. All four
   faders down means no processing at all.

# The chains

Each TRACK button picks one mastering chain. Only one chain is in use at a time. The chains are
in the style of four classic mastering approaches; they are not circuit models of any
particular equipment.

| TRACK | Chain | In the style of | Sound |
|---|---|---|---|
| 1 | **London** | a 1960s London studio master | warm, round, gently glued |
| 2 | **Punch** | a New York rock and pop master | loud, forward, punchy |
| 3 | **Clean** | a modern streaming master | transparent, even, hi-fi |
| 4 | **Lacquer** | a vinyl cutting room | vintage but tight, tame top, mono bass |

When you switch chains, the old chain fades out in 15 ms, the new one settles for 30 ms while
muted (so its compressor has already caught the music when you hear it), then fades in over
15 ms. The faders keep their positions, so you can compare chains at the same settings.

# The faders

The four faders set the same four stages in every chain, in the order the sound passes through
them. What each stage does depends on the chain. Each fader also has a **second parameter** on
page 2 (see [Pages](#pages-and-pick-up)).

| Fader | Stage | Down | Up | Page 2 |
|---|---|---|---|---|
| 1 | **Tone** | flat | the chain's EQ curve | tilt: darker to brighter |
| 2 | **Glue** | off | more compression, with makeup gain | speed: slow to fast |
| 3 | **Colour** | off | more of the chain's character | stereo width: mono to wide |
| 4 | **Loudness** | 0 dB | up to +12 dB into the limiter | limiter release: slow to fast |

With all four faders down and every page 2 parameter in the middle, the chain is flat: the output
is the input, bit for bit, 1 ms later.

## 1 · Tone

The chain's EQ curve, from flat at the bottom to the full curve at the top.

| Chain | Lows | Middle | Highs |
|---|---|---|---|
| London | +4 dB shelf at 100 Hz | +1.5 dB at 2.8 kHz | +3 dB shelf at 10 kHz |
| Punch | +5 dB shelf at 60 Hz | -2.5 dB dip at 220 Hz | +3.5 dB shelf at 10 kHz |
| Clean | +2.5 dB broad shelf at 80 Hz | -1.5 dB at 350 Hz | +4 dB air shelf at 12 kHz |
| Lacquer | +2.5 dB shelf at 120 Hz | +1 dB at 3.5 kHz | -2.5 dB shelf at 9 kHz |

**Page 2: tilt.** Moves the low and high shelves in opposite directions by up to 4 dB: down for a
darker sound, up for a brighter one. The middle tenth of the fader is neutral. Tilt works even
with the Tone fader down.

## 2 · Glue

The chain's compressor. Pushing up lowers the threshold and adds makeup gain, so the sound gets
denser while the level stays roughly where it was.

| Chain | Compressor |
|---|---|
| London | a slow tube-style compressor: very soft knee, about 2.5:1, threshold -8 to -24 dBFS |
| Punch | a bus compressor: 4:1, threshold -10 to -26 dBFS, auto release; it ignores the lows below 90 Hz, so the kick doesn't pump the mix |
| Clean | three bands (split at 180 Hz and 3.5 kHz), each 2.5:1, threshold -10 to -24 dBFS: a loud bass no longer pushes the vocals down |
| Lacquer | a gentle tube-style compressor: 2:1, soft knee, threshold -6 to -24 dBFS, a release that slows after long, loud passages |

**Page 2: speed.** Attack and release together, from slow (down) to fast (up):

| Chain | Attack | Release |
|---|---|---|
| London | 25 ms to 5 ms | 1.2 s to 0.25 s |
| Punch | 30 ms to 1 ms | 300 ms to 80 ms, longer after loud passages |
| Clean | 20 ms to 3 ms | 300 ms to 60 ms |
| Lacquer | 20 ms to 5 ms | 0.8 s to 0.2 s, longer after loud passages |

## 3 · Colour

Each chain's character stage:

- **London: console saturation.** Soft, slightly asymmetric saturation, like signal driven
  through a vintage console's transformers. Rounder and warmer as it goes up.
- **Punch: parallel compression.** A heavily compressed copy of the signal blended in, up to
  half, with the level trimmed. It brings up the body and the room without squashing the
  transients.
- **Clean: air.** The band above 7 kHz, gently saturated and blended in: more sheen and detail on
  top, softer than a plain EQ boost on loud passages.
- **Lacquer: mono bass.** The low end is made mono, as a cutting lathe needs: just above the
  bottom of the fader below 40 Hz, at the top below 300 Hz. Everything above stays in stereo.

**Page 2: stereo width.** The middle tenth leaves the stereo image as it is. Down narrows it to
mono at the bottom; up widens it, with the sides up to 4 dB louder at the top.

## 4 · Loudness

Drives the chain into the limiter, from 0 dB at the bottom to +12 dB at the top. The higher you
go, the louder and denser the master, and the harder the limiter works.

- **Punch** also rounds the peaks off with a soft clipper before the limiter, the more the
  higher the fader. It sounds louder and grittier than limiting alone.
- **Lacquer** always holds the band above 6 kHz under -16 dBFS, as a cutting lathe needs. It
  tames sibilance and harsh cymbals and leaves the rest untouched.

**Page 2: limiter release.** How fast the limiter lets go: 400 ms at the bottom (clean), 100 ms
in the middle, 25 ms at the top (louder, denser, with more risk of pumping).

## The limiter

After the chain, a look-ahead limiter with its ceiling at -1 dBFS is always on, also in bypass.
It sees 1 ms ahead, so the output never goes over the ceiling. On its own it only acts on overs;
the Loudness fader drives the music into it.

# Controls

| Control | Action |
|---|---|
| TRACK 1-4 | pick the chain: London, Punch, Clean, Lacquer (also leaves bypass) |
| Faders 1-4 | the stages (page 1) or their second parameters (page 2) |
| FWD (>>) | go to page 2 |
| RWD (<<) | go to page 1 |
| PLAY | bypass: the dry signal, and back |
| VOL + / VOL - | output volume, 3 dB steps |
| FUNCTION, hold 1.5 s | power on |
| FUNCTION, hold 2.5 s | power off |
| TRACK 1 + TRACK 4, hold 3 s | restart into the bootloader, for flashing |

## Pages and pick-up

The faders have two pages. **Page 1** sets the four stages. **Page 2** sets each stage's second
parameter. Press **FWD (>>)** for page 2 and **RWD (<<)** to go back to page 1. For 0.6 seconds
after you press either, the status lights show the page: the left pair for page 1, the right pair
for page 2.

When you change page, the faders are usually not where that page's values are. So nothing
jumps, a parameter keeps its value until you move its fader to that value (or sweep past it).
Its track light blinks until the fader has picked it up.

## Bypass

**PLAY** switches to the dry signal and back, with a 50 ms crossfade, so you can compare the
master with the original. The limiter stays on, and the timing doesn't change. Bypass is not
level-matched: a master pushed with the Loudness fader will sound louder than the dry signal, so
judge the sound, not just the level. In bypass the status lights breathe slowly. Pressing a TRACK
button also leaves bypass.

## Volume

**VOL +** and **VOL -** change the output volume in 3 dB steps, from 0 dB down to -60 dB. SP-1
Master starts at -6 dB. For a second after a change the status lights show the volume. The volume
comes after the limiter, so it changes what you hear, not what the chain does.

## Headphones and speaker

Plug in headphones and the speaker turns off. Unplug them and the speaker comes back.

# Lights

**Track lights** show the chain: the selected chain's light is on, and glows faintly in bypass.
A fader waiting to pick up its parameter after a page change blinks its light.

**Status lights** normally meter the gain reduction, the chain's compressor and the limiter
together: one light from 0.5 dB, two from 2 dB, three from 4 dB and four from 8 dB. On page 2 the
fourth light blinks. They switch to other information for a moment:

- for 0.6 s after FWD or RWD: the page (left pair = page 1, right pair = page 2);
- for 1 s after VOL + or VOL -: the volume;
- in bypass: all four breathe slowly.

# Power and charging

- **Power on:** hold **FUNCTION** for 1.5 seconds. The status lights fill up as you hold.
- **Power off:** hold **FUNCTION** for 2.5 seconds.
- **Charging:** while the SP-1 is off and plugged in, the status lights show the battery level,
  with the top lit light blinking while it charges.
- **Settings:** the chain, page and volume are not stored. Each time you power on, SP-1 Master
  starts with the London chain, on page 1, with the volume at -6 dB, every page 2 parameter in
  the middle, and the stages set by where the faders are.

If the firmware ever hangs, a watchdog restarts it within 4 seconds and it comes straight back
up.

# Flashing and getting back

The SP-1 is flashed with the [Solderless updater](https://solderless.engineering) in Chrome or
Edge. Download `sp1master.bin` from the release page first.

1. Connect the SP-1 to the computer with a USB-C cable.
2. Put it in firmware-loading mode: **hold TRACK 1 + TRACK 4 while you plug in USB**. This is the
   bootloader's own check, so it works whatever firmware is on the SP-1. With SP-1 Master (or
   SP-1FX, or the tape looper) already running, you can instead hold TRACK 1 + TRACK 4 for 3
   seconds. The four track lights come on.
3. In the updater, choose `sp1master.bin` and flash it.
4. Unplug the SP-1 and plug it back in.

**Going back** to other firmware uses the same steps with that firmware's `.bin` file. The SP-1
has no reset button: if it won't start at all, the plug-in hold in step 2 still reaches the
bootloader.

# Troubleshooting

| Problem | Try |
|---|---|
| No sound | Check that **SP-1 Master** is selected as the output on your computer or phone, and that the volume is up (VOL +). |
| SP-1 Master doesn't appear as a sound card | Use a USB-C cable that carries data, not a charge-only one. Power the SP-1 on with FUNCTION. |
| The faders do nothing | PLAY bypass may be on (the status lights breathe): press PLAY or a TRACK button. |
| A fader doesn't respond after a page change | It is waiting to pick up: move it to where its value is, or sweep it past. |
| The sound pumps or distorts | Bring the Loudness fader down, or its page 2 release toward slow. |
| Crackles or dropouts | Send the status line (below). |
| It won't start | Hold TRACK 1 + TRACK 4 while plugging in USB and reflash. |

## The status line

For a detailed report, open the SP-1's USB serial port in any serial terminal (any baud rate). It
prints one line a second:

| Field | Meaning |
|---|---|
| `chain=` | the selected chain, 1 to 4 |
| `f=` | fader readings |
| `byp=` | bypass on |
| `usb=` | whether audio is streaming |
| `fill=` | how full the input buffer is |
| `under=` `over=` | input buffer underruns and overruns |
| `blk=last/max us` | how long one 2.67 ms audio block takes; must stay well under 2667 |
| `hp=` | headphones detected |
| `gr=` | gain reduction, the compressor and the limiter together |
| `flt=` | a crash from the previous boot, if any |

If something is wrong on the hardware, this line is the thing to send with your report.

# Specifications

| | |
|---|---|
| Audio in | USB-C, class-compliant USB audio (UAC2), 48 kHz stereo, 16- or 24-bit (the host picks) |
| Audio out | headphones or speaker, 48 kHz / 24-bit |
| Processing | 32-bit floating point at 48 kHz |
| Latency | 48 samples (1 ms); with every fader down and page 2 centred the output is the input, bit for bit |
| Limiter | -1 dBFS ceiling, 1 ms look-ahead, always on |
| Chain switch | 15 ms fade out, 30 ms settling, 15 ms fade in |
| Volume | 0 to -60 dB in 3 dB steps |
| CPU | about 41% of the 64 MHz Cortex-M4 worst case (estimated, the Clean chain) |
| RAM | about 39 KB of 256 KB (estimated) |

# Credits

- [sydilaxe/SP-1FX](https://github.com/sydilaxe/SP-1FX-dev): the platform code, the filter and
  compressor building blocks, and the look-ahead limiter this firmware starts from.
- [chattock/sp1-tape-looper](https://github.com/chattock/sp1-tape-looper) (MIT): the SP-1 board
  definition, the USB audio class backport, and the platform code SP-1FX follows.
- [timknapen/SP-1-dev](https://github.com/timknapen/SP-1-dev): the SP-1's pinout, peripherals and
  bootloader documentation.

SP-1 Master is not made or endorsed by Teenage Engineering, or by any studio or equipment maker
whose style a chain follows.
