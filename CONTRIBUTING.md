# Contributing to KRIECHSTROM

Thanks for wanting to help. KRIECHSTROM is firmware for the M-VAVE FM-1 only; ports to other hardware are out of scope.

| What | Where |
|---|---|
| A bug you can reproduce | [Issues](https://github.com/Xforsa/kriechstrom/issues/new/choose), one issue per bug |
| An idea or a request | [Discussions](https://github.com/Xforsa/kriechstrom/discussions) |
| Music, demos, patches | [Discussions](https://github.com/Xforsa/kriechstrom/discussions) |

A bug in Felucca itself (it happens with Felucca too) belongs to [Felucca](https://github.com/hugelton/Felucca/issues).

## Reporting a bug

The version (hold HOME, then SYSTEM > ABOUT), the steps that make it happen, what you expected and what happened.
A photo or a short video helps; so does saying whether MENU > RESTORE LAST was on.

## Pull requests

- one thing per pull request, small enough to read;
- explain what and why, and how you tested it on a real FM-1;
- run `sh tests/run_tests.sh` (about ten minutes, see [BUILDING.md](BUILDING.md)); CI runs it too;
- a change of the sound updates the golden renders on purpose: `GOLDEN_UPDATE=1 sh tests/run_tests.sh`, and say so;
- new files carry `SPDX-License-Identifier: GPL-3.0-only`; by contributing you license your work under the GPL-3.0.

## Style

The code follows Felucca's: C99, integer DSP, comments that say what a thing is. Stored formats are append-only:
new parameters, presets, patterns and enum values go at the end, never in between.
