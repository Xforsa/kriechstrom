# KRIECHSTROM 0.3.1

A fix to RESONATE from 0.3.

## Changed

- **RESONATE** rings out now. In 0.3 its strings were dull and came back quieter the longer they rang, so in a busy
  mix they were hard to hear at all. They are brighter now, like metal, and ring back at the level of what struck
  them: you hear the chord in every gap. AMT sets how long they ring, from half a second to about two.
- The manual has the sound demos too, under each new feature, and the RESONATE demo is a sparse piece with room to
  ring.

## Install

Back up first, then the [web installer](https://xforsa.github.io/kriechstrom/webapp/installer/) in Chrome or Edge,
or `python3 tools/fm1_install.py kriechstrom-0.3.1.fwsc`. Projects of 0.3 load as they are; a project with RESONATE
on will ring louder and brighter than it did.

## Files

| File | What |
| --- | --- |
| `kriechstrom-0.3.1.fwsc` | the package: the app and the update loader, built and signed by GitHub from this tag |
| `SHA256SUMS` | its checksum |

The source is this tag (GitHub's source archives below); the site has it as a tarball too. The package is built from
it on GitHub by the Firmware Hub's shared workflow and attested; check it with
`gh attestation verify kriechstrom-0.3.1.fwsc --repo Xforsa/kriechstrom --signer-workflow zvenson/fm1-build/.github/workflows/build.yml`.

Built on [Felucca](https://github.com/hugelton/Felucca) by Leo Kuroshita, Hügelton Instruments, with parts of
[Glitchbox](https://github.com/erikdevos/glitchbox) by erikdevos. GPL-3.0-only. Full history:
[CHANGELOG.md](https://github.com/Xforsa/kriechstrom/blob/main/CHANGELOG.md).
