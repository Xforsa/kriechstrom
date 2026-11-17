# KRIECHSTROM 0.3

New sounds and effects for the whole mix, and the wall rebuilt.

## Added

- **INFECT**, a sidechain pedal (PEDAL > INFCT): one track reacts to another. GATE chops it on the source's hits,
  DUCK makes it pump, CRUSH and FOLD let the hits distort it, RING multiplies the two, VOX is a vocoder: the drums
  talk through a pad. The source can sit at LEVEL 0 as a hidden trigger.
- **HOWL** and **MIC**, two GLCH waves with no oscillator: a loop that feeds itself. HOWL is a string in overdrive that
  sustains and screams in tune; MIC is a microphone in front of a speaker that howls at the note. Knobs GAIN, CHAOS,
  BITE, BLOOM, and eight sounds: FEEDBACK, STRUNG OUT, SCREAMER, SWELL, WOUNDED, PA HOWL, WRONG ROOM, STAGE FRIGHT.
- **RESONATE** (FX > RESONATE): four strings on the master tuned to a chord (AMT, KEY, CHORD), rung by everything that
  plays.
- **SPLICE** (FX > SPLICE): the tape of the whole mix cut and glued back wrong. On the sixteenths a piece replaces what
  plays: a repeat, a reverse, a piece from a beat ago, half speed or a cut to silence. One knob for how often and how
  hard; the same project is cut the same way each time.

## Changed

- **WALL** is a wall of sound now: the whole mix double-tracked (two drifting copies, left and right) with an octave
  added under its low end, then squeezed as before. The level stays where it was.
- The power-on screen shows the name as the site does: KRIECH in white, STROM in red.
- The installer shows a progress bar with a percentage, returns to the official V15 or V16, and finds an FM-1 whose
  MIDI port macOS still calls by an older name.
- Every release keeps the identity `FM-1_57810002`.

## Install

Back up first, then the [web installer](https://xforsa.github.io/kriechstrom/webapp/installer/) in Chrome or Edge,
or `python3 tools/fm1_install.py kriechstrom-0.3.fwsc`. Projects and presets of 0.1 and 0.2 load as they are; the new
effects start off. Hear each new feature on the [site](https://xforsa.github.io/kriechstrom/) and read about it in the
[manual](https://xforsa.github.io/kriechstrom/manual/en.html).

## Files

| File | What |
| --- | --- |
| `kriechstrom-0.3.fwsc` | the package: the app and the update loader |
| `kriechstrom-0.3-src.tar.gz` | the complete source of this build (GPL-3.0) |
| `SHA256SUMS` | checksums |

Built on [Felucca](https://github.com/hugelton/Felucca) by Leo Kuroshita, Hügelton Instruments, with parts of
[Glitchbox](https://github.com/erikdevos/glitchbox) by erikdevos. GPL-3.0-only. Full history:
[CHANGELOG.md](https://github.com/Xforsa/kriechstrom/blob/main/CHANGELOG.md).
