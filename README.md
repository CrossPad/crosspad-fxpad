# FX Pad

The CrossPad as a performance multi-effect: line in, the mics and the computer
over USB through sixteen effects the pads hold, an XY surface for their
parameters, synced to the tempo. The one app of the `fxpad` firmware kit.

- Hold a pad: its effect is on. Touch the XY surface: its two parameters. The
  pad's pressure is WET. LATCH: two effects at once; HOLD: they stay after the
  pad is let go; KILL (or the power button's click): everything off.
- FX picks an effect to edit, EDIT sets its curve and how it stops (TAIL rings
  out, SNAP fades at once), ROUTE the sources, their sends, PRE/POST, MONO,
  THRU and the outputs.

Needs crosspad-core 2.47 (`IFxBus`) and a platform that provides it
(platform-idf: the mixer's FX bus). Design and numbers:
`docs/subsystems/fx-pad.md` in platform-idf.

Host tests of the effects:

```
cmake -S tests -B tests/build && cmake --build tests/build && ctest --test-dir tests/build
```
