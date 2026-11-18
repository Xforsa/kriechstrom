# KRIECHSTROM 0.5.1

HAUNT redone: SUCK and DRAG you can hear, PRINT gone.

## Changed

- **SUCK** is a reverse swell now, as a reversed cymbal: noise whose band opens as it rises over the beat before each
  snare and clap, cut dead on the hit. Nothing is recorded, so nothing crackles.
- **DRAG** is a tape brake on the last beat of every bar: the tape slows and its level sags, down to an octave at the
  bar line with AMT at the top, and on the one it is back at once.
- **PRINT** is gone: its ghost did not come through, and its 8-bit tape crackled. MODE is OFF, SUCK, DRAG.

## Fixed

- DRAG read the oldest sample of its tape instead of the newest, so it smeared instead of slowing down.
- HAUNT turned on in the middle of a song kept a grid of its own from that moment; it sits on the song's grid from PLAY
  now.

## Install

Back up first, then the [web installer](https://xforsa.github.io/kriechstrom/webapp/installer/) in Chrome or Edge,
or `python3 tools/fm1_install.py kriechstrom-0.5.1.fwsc`. Hear SUCK and DRAG on the
[site](https://xforsa.github.io/kriechstrom/) and in the [manual](https://xforsa.github.io/kriechstrom/manual/en.html#haunt).

## Files

| File | What |
| --- | --- |
| `kriechstrom-0.5.1.fwsc` | the package: the app and the update loader |
| Source code (zip, tar.gz) | the complete source of this build (GPL-3.0): GitHub's archive of the tag |
| `SHA256SUMS` | checksums |

Built on [Felucca](https://github.com/hugelton/Felucca) by Leo Kuroshita, Hügelton Instruments, with parts of
[Glitchbox](https://github.com/erikdevos/glitchbox) by erikdevos. The screen reader's device side is Charles Vestal's
(fm1-base, MPL-2.0). GPL-3.0-only. Full history:
[CHANGELOG.md](https://github.com/Xforsa/kriechstrom/blob/main/CHANGELOG.md).
