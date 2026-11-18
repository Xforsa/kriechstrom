# KRIECHSTROM 0.5

HAUNT: the tape of the whole mix haunted, one of three ways.

## Added

- **HAUNT** (FX > HAUNT: MODE, AMT) on the master, one at a time:
  - **PRINT**: print-through, as on old tape. A dark, wobbling ghost of what plays a beat later is heard before it
    (a loop's next beat is what it played three beats ago).
  - **SUCK**: a reversed reverb rising into every snare and clap. The drum tracks' steps say where the next one falls;
    without a drum track it rises into beats 2 and 4.
  - **DRAG**: the tape drags. Over the last beat of every bar the mix slows, about a semitone down at the end, and on the one it is back at once.

  AMT sets how loud the ghost and the swell are, and how far DRAG drags. HAUNT is stored with the project; projects
  of earlier versions load with it off.

## Changed

- The screen reader says the page and its knobs first, then the track and its sound, and has words for more of the
  screen.

## Install

Back up first, then the [web installer](https://xforsa.github.io/kriechstrom/webapp/installer/) in Chrome or Edge,
or `python3 tools/fm1_install.py kriechstrom-0.5.fwsc`. Projects and presets of 0.1 to 0.4 load as they are. Hear
PRINT, SUCK and DRAG on the [site](https://xforsa.github.io/kriechstrom/) and read about them in the
[manual](https://xforsa.github.io/kriechstrom/manual/en.html#haunt).

## Files

| File | What |
| --- | --- |
| `kriechstrom-0.5.fwsc` | the package: the app and the update loader |
| `kriechstrom-0.5-src.tar.gz` | the complete source of this build (GPL-3.0) |
| `SHA256SUMS` | checksums |

Built on [Felucca](https://github.com/hugelton/Felucca) by Leo Kuroshita, Hügelton Instruments, with parts of
[Glitchbox](https://github.com/erikdevos/glitchbox) by erikdevos. The screen reader's device side is Charles Vestal's
(fm1-base, MPL-2.0). GPL-3.0-only. Full history:
[CHANGELOG.md](https://github.com/Xforsa/kriechstrom/blob/main/CHANGELOG.md).
