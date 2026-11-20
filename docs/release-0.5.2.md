# KRIECHSTROM 0.5.2

A fix for the screen reader.

## Fixed

- With [Charles Vestal's FM-1 Screen Reader](https://charlesvestal.github.io/fm1-reader/), OCT- and OCT+ said
  nothing. They say the keys' octave now: "octave plus 1", "octave minus 2", "octave 0". Thanks to João Durães for the
  report.

## Install

Back up first, then the [web installer](https://xforsa.github.io/kriechstrom/webapp/installer/) in Chrome or Edge,
or `python3 tools/fm1_install.py kriechstrom-0.5.2.fwsc`.

## Files

| File | What |
| --- | --- |
| `kriechstrom-0.5.2.fwsc` | the package: the app and the update loader |
| Source code (zip, tar.gz) | the complete source of this build (GPL-3.0): GitHub's archive of the tag |
| `SHA256SUMS` | checksums |

Built on [Felucca](https://github.com/hugelton/Felucca) by Leo Kuroshita, Hügelton Instruments, with parts of
[Glitchbox](https://github.com/erikdevos/glitchbox) by erikdevos. The screen reader's device side is Charles Vestal's
(fm1-base, MPL-2.0). GPL-3.0-only. Full history:
[CHANGELOG.md](https://github.com/Xforsa/kriechstrom/blob/main/CHANGELOG.md).
