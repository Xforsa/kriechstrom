#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build the KRIECHSTROM site into OUT (default build/site):

  index.html, ru.html, style.css     the landing pages (EN, RU)
  manual/en.html, manual/ru.html     the manual, the sound list filled in from the firmware's own tables
  shots/*.png                        the screens the pages use, from tests/ui_render (build/ui_new/LARGE_STROM)
  webapp/installer, webapp/editor    web/make_site.py
  firmware/kriechstrom-VER.fwsc      the package
  src/kriechstrom-VER-src.tar.gz     the source of this build (GPL-3.0)

  python3 site/build.py build/kriechstrom-0.1.fwsc 0.1 [OUT]
"""
import re
import shutil
import subprocess
import sys
import tarfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SITE = ROOT / "site"
SHOTS = ROOT / "build" / "ui_new" / "LARGE_STROM"

ENGINES = [  # (name shown, source, table, engine number in ENGINES[])
    ("ANALOG", "firmware/engines/src/eng_analog.c", "ANALOG_PRESETS", 0),
    ("FM6", "firmware/src/eng_fm6.c", "FM6_PRESETS", 12),
    ("PHASE", "firmware/engines/src/eng_phase.c", "PHASE_PRESETS", 2),
    ("LOFI", "firmware/engines/src/eng_lofi.c", "LOFI_PRESETS", 3),
    ("SAMPLE", None, ["PIANO", "FLUTE", "SAX"], 4),   # (SMP_PRESET_TABLE: the 2nd PIANO is an alias)
    ("VOICE", "firmware/src/eng_formant.c", "FORMANT_PRESETS", 5),
    ("TRIO", "firmware/engines/src/eng_trio.c", "TRIO_PRESETS", 6),
    ("GLCH", "firmware/src/eng_glitch.c", "GLITCH_PRESETS", 7),
    ("GRAIN", "firmware/src/eng_grain.c", "GRAIN_PRESETS", 8),
    ("PHYS", "firmware/src/eng_phys.c", "PHYS_PRESETS", 9),
    ("DRUM", "firmware/src/eng_drum.c", "DRUM_PRESETS", 10),
    ("NOISE", "firmware/engines/src/eng_noise.c", "NOISE_PRESETS", 11),
    ("SLICE", "firmware/src/eng_slice.c", "SLICE_PRESETS", 13),
]
CAT = {"B": "BASS", "L": "LEAD", "P": "PAD", "U": "PLUCK", "K": "KEYS", "D": "DRUM", "F": "FX", "O": "OTHER"}
CAT_RU = {"BASS": "бас", "LEAD": "лид", "PAD": "пэд", "PLUCK": "щипок", "KEYS": "клавиши", "DRUM": "драмы",
          "FX": "шум и FX", "OTHER": "другое"}


def preset_names(src, table):
    s = (ROOT / src).read_text(encoding="utf-8")
    m = re.search(r"preset_t " + table + r"\[\] = \{\n(.*?)\n\};", s, re.S)
    if not m:
        raise SystemExit(f"{src}: no {table}")
    body = re.sub(r"/\*.*?\*/", "", m.group(1), flags=re.S)
    names = re.findall(r'^\s*\{"([^"]+)"', body, re.M)
    return names


def categories():
    s = (ROOT / "firmware/src/category.c").read_text(encoding="utf-8")
    block = s[s.index("PRESET_CAT[16] = {"):]
    block = block[:block.index("};")]
    rows = []
    for line in block.split("\n")[1:]:
        line = re.sub(r"/\*.*?(\*/|$)", "", line)
        parts = re.findall(r'"([^"]*)"', line)
        if parts or line.strip().startswith('""'):
            rows.append("".join(parts))
    return rows


def sound_list(lang):
    cats = categories()
    out, total = [], 0
    for name, src, table, idx in ENGINES:
        names = table if src is None else preset_names(src, table)
        letters = cats[idx] if idx < len(cats) else ""
        if src is None:
            letters = letters[:1] + letters[2:]
        items = []
        for k, n in enumerate(names):
            c = CAT.get(letters[k], "") if k < len(letters) else ""
            items.append(f"{n}" + (f" <small>({CAT_RU[c] if lang == 'ru' else c.lower()})</small>" if c else ""))
        total += len(names)
        out.append(f"<h3>{name} · {len(names)}</h3>\n<p>" + ", ".join(items) + "</p>")
    head = (f"<p>{total} звуков. В скобках категория для фильтра LIST на SAVE &gt; PRESETS (K4).</p>" if lang == "ru"
            else f"<p>{total} sounds. In brackets: the category for LIST on SAVE &gt; PRESETS (K4).</p>")
    return head + "\n" + "\n".join(out), total


def source_tarball(dst, version):
    name = f"kriechstrom-{version}-src"
    path = dst / f"{name}.tar.gz"
    skip = {"build", "node_modules", ".DS_Store", "__pycache__", ".git"}

    def keep(ti):
        parts = Path(ti.name).parts
        return None if any(p in skip for p in parts) or ti.name.endswith(".fwsc") else ti

    with tarfile.open(path, "w:gz") as tar:
        tar.add(ROOT, arcname=name, filter=keep)
    return path


def main(pkg, version, out):
    pkg, out = Path(pkg), Path(out)
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)
    subprocess.run([sys.executable, str(ROOT / "web" / "make_site.py"), str(pkg), version, str(out)], check=True)
    fws = sorted((out / "firmware").glob("*.fwsc"))
    if len(fws) != 1:
        raise SystemExit(f"firmware/: {len(fws)} packages")
    fwsc = fws[0].name
    (out / "src").mkdir()
    tb = source_tarball(out / "src", version)
    size = f"{tb.stat().st_size / 1e6:.1f} MB"
    used = set()
    total = 0
    for rel in ("index.html", "ru.html", "style.css", "manual/en.html", "manual/ru.html"):
        s = (SITE / rel).read_text(encoding="utf-8")
        if "<!--SOUNDS-->" in s:
            lst, total = sound_list("ru" if rel.endswith("ru.html") else "en")
            s = s.replace("<!--SOUNDS-->", lst)
        used |= set(re.findall(r'shots/([a-z0-9_]+\.png)', s))
        (out / rel).parent.mkdir(parents=True, exist_ok=True)
        (out / rel).write_text(s, encoding="utf-8")
    if not total:
        total = sound_list("en")[1]
    for rel in ("index.html", "ru.html", "manual/en.html", "manual/ru.html"):
        p = out / rel
        s = p.read_text(encoding="utf-8")
        s = (s.replace("@FWSC@", fwsc).replace("@SRC@", tb.name).replace("@SRCSIZE@", size)
             .replace("@VERSION@", version).replace("@NSOUNDS@", str(total)))
        p.write_text(s, encoding="utf-8")
    shutil.copytree(SITE / "art", out / "art", ignore=shutil.ignore_patterns("splash_preview.png"))   # (tools/krst_art.py)
    (out / "shots").mkdir()
    for n in sorted(used):
        if not (SHOTS / n).exists():
            raise SystemExit(f"shots/{n}: not rendered (run tests/run_tests.sh first)")
        shutil.copy(SHOTS / n, out / "shots" / n)
    print(f"site: {out} ({fwsc}, {len(used)} screens, {total} sounds, source {size})")


if __name__ == "__main__":
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    main(sys.argv[1], sys.argv[2], sys.argv[3] if len(sys.argv) > 3 else ROOT / "build" / "site")
