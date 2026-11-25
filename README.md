# FX Pad

The CrossPad as a performance multi-effect: line in, the mics and the computer
over USB through 32 effects in two banks the pads hold, an XY surface for
their parameters, synced to the tempo. Bank B cuts the sound up (stutter,
beat repeat, glitch, slicer, reverse, tape stop, spinback, scratch, …) for
building a track live. The one app of the `fxpad` firmware kit.

- Hold a pad: its effect is on. Touch the XY surface: its two parameters. The
  pad's pressure is WET. LATCH: two effects at once; HOLD: they stay after the
  pad is let go; KILL (or the power button's click): everything off.
- FX picks an effect to edit and the bank on the pads (BANK), EDIT swaps its two parameters between the
  axes (SWAP X/Y), sets its curve and how it stops (TAIL rings out, on the delays, the reverbs and the
  freezes; SNAP fades at once), ROUTE the sources, their sends, PRE/POST, MONO,
  THRU and the outputs; USB REC puts USB in its audio profile so a phone or a
  computer records the effects.

Needs crosspad-core 2.48 (`IFxBus`) and a platform that provides it
(platform-idf: the mixer's FX bus; crosspad-pc: `src/audio/PcFxBus.cpp`, kit
`fxpad`). Design and numbers:
`docs/subsystems/fx-pad.md` in platform-idf.

Host tests of the effects:

```
cmake -S tests -B tests/build && cmake --build tests/build && ctest --test-dir tests/build
```
