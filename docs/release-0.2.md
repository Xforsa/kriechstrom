# KRIECHSTROM 0.2

A small release on top of 0.1.

## Changed

- **HISS** hisses only while the tape runs: while PLAY plays, and when stopped while notes sound and for a second
  after. Stopped and silent, it is quiet. It opens in about 50 ms and closes over 0.6 s, so it neither clicks nor
  breathes between notes.

## Install

Back up first, then the [web installer](https://xforsa.github.io/kriechstrom/webapp/installer/) in Chrome or Edge,
or `python3 tools/fm1_install.py kriechstrom-0.2.fwsc`. Identity `FM-1_57810002`. Projects and presets of 0.1 load as
they are.

## Files

| File | What |
| --- | --- |
| `kriechstrom-0.2.fwsc` | the package: the app and the update loader |
| `kriechstrom-0.2-src.tar.gz` | the complete source of this build (GPL-3.0) |
| `SHA256SUMS` | checksums |

Built on [Felucca](https://github.com/hugelton/Felucca) by Leo Kuroshita, Hügelton Instruments, with parts of
[Glitchbox](https://github.com/erikdevos/glitchbox) by erikdevos. GPL-3.0-only. Full history:
[CHANGELOG.md](https://github.com/Xforsa/kriechstrom/blob/main/CHANGELOG.md).
