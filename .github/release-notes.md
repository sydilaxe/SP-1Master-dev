SP-1 Master test build for the Teenage Engineering SP-1: a mastering processor. Audio in over USB-C (a
class-compliant sound card, 48 kHz / 24-bit), out on headphones or the speaker.

**Not yet tried on a real SP-1.** The engine passes its tests, and the platform code is SP-1FX's, which
follows firmware that runs on the SP-1, but this binary has not been on hardware yet.

**Flash:** open the [Solderless updater](https://solderless.engineering) in Chrome or Edge, hold **TRACK 1 +
TRACK 4** while you plug the SP-1 in (the four track lights come on), choose `sp1master.bin`, flash, then
unplug and plug back in. The same plug-in hold always gets back to the bootloader, whatever firmware is on it.

**The chains**, one at a time on the TRACK buttons (in the style of, not models of, classic masters):
- **1 LONDON:** a 1960s London studio master: warm shelves, a slow tube-style compressor, console saturation.
- **2 PUNCH:** a New York master: a big low end with the dip above it, a 4:1 bus compressor, parallel
  compression, a soft clipper before the limiter.
- **3 CLEAN:** a modern streaming master: broad shelves, a three-band compressor, a saturated air band.
- **4 LACQUER:** a vinyl cutting room: a warm cutting curve, a gentle compressor, mono bass, a
  high-frequency limiter that tames sibilance.

**The faders**, the same four stages in every chain (each has a second parameter on page 2):
**1 TONE** (page 2: tilt), **2 GLUE** (speed), **3 COLOUR** (stereo width), **4 LOUDNESS**, 0 to +12 dB
into the limiter (limiter release). All down and page 2 centred: flat.

**The crush page** (FWD): a bit crusher and filter from SP-1FX, before the chain. **1 AMOUNT** off to
all crushed, **2 BITS** 16 to 1, **3 RATE** 48 to 2 kHz, **4 FILTER** low-pass (down), flat (middle),
high-pass (up), as on SP-1FX. Off at power on; bypass skips it. On the crush page
the track lights show its settings (the filter's light is dark while flat, brighter toward either end).

A look-ahead limiter at -1 dBFS stays on after the chain, also in bypass.

**Buttons:** TRACK 1-4 pick the chain (15 ms out, 30 ms settling, 15 ms in), PLAY bypasses to the dry
signal (50 ms crossfade, not level-matched), FWD switches to the crush page and RWD to page 2, each pressed again for page 1 (a parameter picks up when
its fader reaches it, so nothing jumps), VOL + / - set the volume (3 dB steps), hold FUNCTION to power on
(1.5 s) or off (2.5 s), hold TRACK 1 + 4 for 3 s to restart into the bootloader.

**The sound:** full bandwidth: 48 kHz / 24-bit in and out, 32-bit float inside, 1 ms latency.

**If something is wrong**, open the SP-1's USB serial port: a status line a second shows the chain, the
page, the faders, the crush page, the USB stream, the audio load (`blk=`, microseconds per 2.67 ms block) and the last crash.

**The full user manual** is attached below as `SP-1-Master-manual.pdf`.
