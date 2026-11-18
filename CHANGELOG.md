# Changelog

All notable changes to KRIECHSTROM. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/);
versions are `X.Y`; the package identity is `FM-1_57810002` for every release since 0.2.

## [Unreleased]

## [0.5] - 2026-11-18

### Added
- HAUNT (FX > HAUNT: MODE, AMT), one of three on the master: PRINT, a ghost of the mix three beats back (what a loop
  plays a beat later) before it, dark and wobbling; SUCK, a reversed reverb rising into each snare and clap (the drum
  tracks' steps say where); DRAG, the tape slowing over each bar's last beat, back on the one; stored in G_LOAD's word
- sound demos for PRINT, SUCK and DRAG on the site and in the manual

### Changed
- the screen reader says the page and its knobs first, then the track and its sound; more of the screen in words

## [0.4] - 2026-11-18

### Added
- six drum kits of its own on DRUM (KIT 13..18: ROOM GATE MARCH ARC ROT SLAB): each lane its own voice (swept impacts,
  modes, noise, sparks, a falling sample rate, metal), and the part's own bus: a room squashed by a compressor, a
  parallel drive and crush, GATE's comb rooms cut dead and 12-bit sampler; six sounds ROOM KIT .. SLAB KIT
- screen reader support (fm1-reader, Charles Vestal's): the screen said in words over USB-MIDI SysEx, silent until a
  reader says HELLO; the words file is published with the site (web/words/)
- tests: the kits' hits against their designs, levels, knobs, the engine and the bus (tests/krst_kits_test.c); the
  screen reader's paths (tests/krst_speak_test.c) and its words audited (tools/krst_speak_audit.sh)

### Changed
- the USB SysEx send queue holds 128 packets (a whole screen-reader message)

## [0.3.1] - 2026-11-17

### Changed
- RESONATE rings out: the strings are brighter (metal, not felt) and come back at the level of what struck them, so
  you hear them clearly in every gap; AMT sets the ring from half a second to about two
- its demo is a sparse piece with room to ring; the manual has the sound demos too, under each feature

## [0.3] - 2026-11-17

### Added
- INFECT: a sidechain pedal (INSERT INFCT) through which one track drives another: GATE, DUCK, CRUSH, FOLD, RING or
  VOX (an 8-band vocoder) by the source track's sound, which drives even at LEVEL 0
- GLCH HOWL and MIC, a feedback family: no oscillator, a loop that feeds itself (GAIN CHAOS BITE BLOOM); eight sounds
- RESONATE (FX > RESONATE): four strings on the master tuned to a chord (AMT, KEY, CHORD), rung by the mix
- SPLICE (FX > SPLICE): the tape cut and glued back wrong; on the sixteenths a piece replaces the mix (a repeat, a
  reverse, a piece from a beat ago, half speed, a cut to silence), as often and as hard as AMT says
- sound demos on the site, a short piece for each of DAMAGE, WALL, INFECT, VOX, HOWL, RESONATE and SPLICE
  (tools/krst_demos.sh)

### Changed
- WALL is a wall of sound now: the mix double-tracked (two drifting copies, 13 and 21 ms, left and right) and an
  octave added under its low end, before the squeeze it had
- the installer returns to the official firmware from V16 as well as V15 (newer FM-1s ship with V16)
- the installer and the editor find an FM-1 whose MIDI port macOS still calls Clipper or SLOOP; the installer says it is
  looking for the FM-1 at once
- the installer shows a progress bar with a percentage for both installing and returning to the official firmware;
  the technical log is folded under Details
- release builds keep the identity `FM-1_57810002` instead of taking a new one per version
- the power-on screen shows the name as the site does: KRIECH in white, STROM in red
- the site shows nine screens, with the sound list, DAMAGE, INFECT, HOWL and RESONATE; the manual covers the new
  features

## [0.2] - 2026-11-12

### Changed
- HISS hisses only while the tape runs: while PLAY plays, and when stopped while notes sound and a second after;
  stopped and silent it is quiet (it opens in ~50 ms, closes over 0.6 s)

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

[0.3.1]: https://github.com/Xforsa/kriechstrom/releases/tag/v0.3.1
[0.3]: https://github.com/Xforsa/kriechstrom/releases/tag/v0.3
[0.2]: https://github.com/Xforsa/kriechstrom/releases/tag/v0.2
[0.1]: https://github.com/Xforsa/kriechstrom/releases/tag/v0.1
