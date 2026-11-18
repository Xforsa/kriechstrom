# KRIECHSTROM 0.4

Six drum kits of its own, and the screen read aloud for blind musicians.

## Added

- **Six drum kits** on the DRUM engine (KIT ROOM, GATE, MARCH, ARC, ROT, SLAB), each with its own voices and its
  own room or grit, not Felucca's kits re-tuned. Every hit is a short impact, not a note.
  - **ROOM**: live drums in a big room squashed by a compressor, the whole kit driven and crushed in parallel.
  - **GATE**: the huge gated snare, a kick gated too, the kit through a 12-bit sampler.
  - **MARCH**: only impacts: a kick with a beater click, a dry crack, boots, war drums, a drag on RIM.
  - **ARC**: electricity: a crackle of sparks on the kick and through the snare, a hiss of current for the hats.
  - **ROT**: digital decay: the sample rate falls through every hit.
  - **SLAB**: power electronics, every drum made of noise.

  TUNE, TONE and DECY move every lane, DRV pushes the kit's grit. Six sounds load them: ROOM KIT, GATE KIT,
  MARCH KIT, ARC KIT, ROT KIT, SLAB KIT.
- **Screen reader support** for Charles Vestal's [FM-1 Screen Reader](https://charlesvestal.github.io/fm1-reader/):
  plug the FM-1 in, open the reader page in Chrome or Edge and click Connect. It says where you are (the page, the
  track and its sound, the four knobs), each knob as you turn it, the menu's rows, the dialogs, messages, PLAY and STOP,
  in its own voice or through VoiceOver, NVDA, JAWS or TalkBack. The words come from the site; the firmware sends
  nothing until a reader asks.

## Install

Back up first, then the [web installer](https://xforsa.github.io/kriechstrom/webapp/installer/) in Chrome or Edge,
or `python3 tools/fm1_install.py kriechstrom-0.4.fwsc`. Projects and presets of 0.1 to 0.3 load as they are. Hear each
kit on its own beat on the [site](https://xforsa.github.io/kriechstrom/) and read about them in the
[manual](https://xforsa.github.io/kriechstrom/manual/en.html).

## Files

| File | What |
| --- | --- |
| `kriechstrom-0.4.fwsc` | the package: the app and the update loader |
| Source code (zip, tar.gz) | the complete source of this build (GPL-3.0): GitHub's archive of the tag |
| `SHA256SUMS` | checksums |

Built on [Felucca](https://github.com/hugelton/Felucca) by Leo Kuroshita, Hügelton Instruments, with parts of
[Glitchbox](https://github.com/erikdevos/glitchbox) by erikdevos. The screen reader's device side is Charles Vestal's
(fm1-base, MPL-2.0). GPL-3.0-only. Full history:
[CHANGELOG.md](https://github.com/Xforsa/kriechstrom/blob/main/CHANGELOG.md).
