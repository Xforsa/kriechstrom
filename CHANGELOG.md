# Changelog

All notable changes to KRIECHSTROM. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/);
versions are `X.Y`, the package identity `FM-1_5781XXYY`.

## [0.1] - 2026-11-12

The first release, built on Felucca 1.8.2.

### Added
- GLCH engine from Glitchbox: clipping, wavefolding, a resonant filter, bit crushing and micro-stutter; 48 sounds
- 86 new sounds across ANALOG, GLCH, PHASE, FM6, NOISE and DRUM (171 in all), 8 new drum kits
- DICE in SEQ TOOLS (F5) with eight moods (G5): MACHINE, GRIND, MARCH 7/8, GLITCH, SPIRAL, GHOST, SURGE, HUSH
- HAPPY ACCIDENT (OOPS) in the EDIT layer (G5): two or three values of the sound jump, one undo for a run
- FX > DAMAGE: DMG and CHAR (tape to digital wear on the master), WALL (quiet to a wall, also FX + SELECT), HISS
- the DAMG pedal: the same wear on one track
- MUFF, BASS and SMASH pedals, ROLL and GATE live effects, the DESTROY kit and pattern (from Glitchbox)
- power-on sounds and patterns: GLCH SICK ENGINE, FM6 BROKEN PIANO, PHASE SICK PAD, DRUM FACTORY
- MIDI CC85 (WALL) and CC86 (DMG)
- the STROM theme (black and red), a splash picture
- a site, a manual and a cheat sheet in English and Russian; the installer and the editor in English and Russian

### Changed
- first start: STROM, LARGE on, HELP on, FX LATCH on (a tap turns an effect on, another off), USB SERIAL off
- effect pages named for players: SENDS, PEDAL, PEDAL MIX, DELAY; a value that turns an effect off reads OFF
- the USB device name and the identity: KRIECHSTROM, `FM-1_57810001`
- Felucca-Engines and felucca-bsp are kept in the tree instead of submodules

[0.1]: https://github.com/Xforsa/kriechstrom/releases/tag/v0.1
