#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build the KRIECHSTROM site into OUT (default build/site):

  index.html, ru.html, style.css     the landing pages (EN, RU)
  krst.js                            the demo player: the demo's waveform by bars, its parts, a live scope
  manual/en.html, manual/ru.html     the manual, the sound list filled in from the firmware's own tables
  shots/*.png                        the screens the pages use, from tests/ui_render (build/ui_new/LARGE_STROM)
  audio/*.m4a                        the sound demos the pages use, from tools/krst_demos.sh (build/demos)
  webapp/installer, webapp/editor    web/make_site.py
  words/*.json                       the screen reader's words files (web/words: every one ever linked; a device
                                     points at the one its build was made with, build/gen/ann_words.h)
  firmware/kriechstrom-VER.fwsc      the package
  src/kriechstrom-VER-src.tar.gz     the source of this build (GPL-3.0): the files git tracks

  python3 site/build.py build/kriechstrom-0.1.fwsc 0.1 [OUT]

The pages, the sound list and the counts come from the tree as it is: build the site from the release's commit.
"""
import array
import math
import re
import shutil
import wave
import subprocess
import sys
import tarfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SITE = ROOT / "site"
SHOTS = ROOT / "build" / "ui_new" / "LARGE_STROM"
DEMOS = ROOT / "build" / "demos"

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


# the demos' parts for the player (site/krst.js): (bar it starts on, label EN, label RU); every demo is 8 bars
DRY = (1, "DRY", "СУХО")
KIT = [(1, "BEAT", "БИТ"), (3, "+ BASS, SYNTH", "+ БАС, СИНТ")]
PARTS = {
    "damage": [DRY, (3, "TAPE", "ЛЕНТА"), (6, "DIGITAL", "ЦИФРА")],
    "wall": [DRY, (3, "WALL", "WALL")],
    "infect": [DRY, (3, "DUCK", "DUCK"), (5, "CRUSH", "CRUSH")],
    "vox": [DRY, (3, "VOX", "VOX")],
    "howl": [DRY, (3, "FEEDBACK", "FEEDBACK"), (6, "SCREAMER", "SCREAMER")],
    "resonate": [DRY, (3, "RESONATE", "RESONATE")],
    "splice": [DRY, (3, "SPLICE", "SPLICE"), (6, "SPLICE MAX", "SPLICE MAX")],
    "suck": [DRY, (3, "SUCK", "SUCK")],
    "drag": [DRY, (3, "DRAG", "DRAG")],
}
B64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_"


def demo_peaks(path, cols=240):
    """the demo's overview for the player: per column its peak, its RMS and how bright it is (the energy of the
    first difference against the signal's: high for hats and noise, low for a sub), 0..63 each, 3 characters"""
    with wave.open(str(path)) as w:
        ch, n = w.getnchannels(), w.getnframes()
        a = array.array("h", w.readframes(n))
    out = []
    for c in range(cols):
        i0, i1 = c * n // cols, (c + 1) * n // cols
        pk = e = d = 0.0
        prev = None
        for i in range(i0, i1, 2):
            x = (a[i * ch] + a[i * ch + ch - 1]) / 65536.0
            ax = abs(x)
            if ax > pk:
                pk = ax
            e += x * x
            if prev is not None:
                d += (x - prev) * (x - prev)
            prev = x
        m = max(1, (i1 - i0 + 1) // 2)
        rms = math.sqrt(e / m)
        br = 0.0 if e <= 1e-9 else min(1.0, max(0.0, (math.log10(d / e + 1e-9) + 2.5) / 2.5))
        out.append(B64[min(63, int(pk * 63.99))] + B64[min(63, int(min(1.0, rms * 2.2) * 63.99))]
                   + B64[min(63, int(br * 63.99))])
    return "".join(out), n / 44100.0


def demo_player(s, lang, cache):
    """the player's data on every demo: data-name, data-parts, data-dur and data-peaks (no wav: a plain player)"""
    def one(m):
        src, name = m.group(1), m.group(2)
        wav = DEMOS / f"{name}.wav"
        if not wav.exists():
            return m.group(0)
        if name not in cache:
            cache[name] = demo_peaks(wav)
        peaks, dur = cache[name]
        kit = name.startswith("kit_")
        title = name[4:].replace("_track", "").upper() if kit else name.upper()
        parts = KIT if kit else PARTS.get(name, [DRY])
        ps = "|".join(f"{b}:{ru if lang == 'ru' else en}" for b, en, ru in parts)
        return (f'<audio controls preload="none" src="{src}" data-name="{title}" data-bars="8" data-parts="{ps}" '
                f'data-dur="{dur:.3f}" data-peaks="{peaks}"></audio>')
    return re.sub(r'<audio controls preload="none" src="((?:\.\./)?audio/([a-z0-9_]+)\.m4a)"></audio>', one, s)


def source_tarball(dst, version):
    """the source of this build: the files git tracks, as they are in the tree (nothing local or untracked: build
    output, notes, editor files); a new file goes in once it is added to git"""
    name = f"kriechstrom-{version}-src"
    path = dst / f"{name}.tar.gz"
    files = subprocess.run(["git", "-C", str(ROOT), "ls-files", "-z"], check=True,
                           capture_output=True).stdout.decode().split("\0")
    with tarfile.open(path, "w:gz") as tar:
        for f in sorted(x for x in files if x):
            if (ROOT / f).is_file() and not f.endswith(".fwsc"):
                tar.add(ROOT / f, arcname=f"{name}/{f}", recursive=False)
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
    sounds = set()
    total = 0
    peaks = {}
    for rel in ("index.html", "ru.html", "style.css", "krst.js", "manual/en.html", "manual/ru.html"):
        s = (SITE / rel).read_text(encoding="utf-8")
        if "<!--SOUNDS-->" in s:
            lst, total = sound_list("ru" if rel.endswith("ru.html") else "en")
            s = s.replace("<!--SOUNDS-->", lst)
        used |= set(re.findall(r'shots/([a-z0-9_]+\.png)', s))
        sounds |= set(re.findall(r'audio/([a-z0-9_]+\.m4a)', s))
        if rel.endswith(".html"):
            s = demo_player(s, "ru" if rel.endswith("ru.html") else "en", peaks)
        (out / rel).parent.mkdir(parents=True, exist_ok=True)
        (out / rel).write_text(s, encoding="utf-8")
    if not total:
        total = sound_list("en")[1]
    nglch = next(len(preset_names(src, table)) for name, src, table, idx in ENGINES if name == "GLCH")
    for rel in ("index.html", "ru.html", "manual/en.html", "manual/ru.html"):
        p = out / rel
        s = p.read_text(encoding="utf-8")
        s = (s.replace("@FWSC@", fwsc).replace("@SRC@", tb.name).replace("@SRCSIZE@", size)
             .replace("@VERSION@", version).replace("@NSOUNDS@", str(total)).replace("@NGLCH@", str(nglch)))
        p.write_text(s, encoding="utf-8")
    shutil.copytree(SITE / "art", out / "art", ignore=shutil.ignore_patterns("splash_preview.png"))   # (tools/krst_art.py)
    (out / "shots").mkdir()
    for n in sorted(used):
        if not (SHOTS / n).exists():
            raise SystemExit(f"shots/{n}: not rendered (run tests/run_tests.sh first)")
        shutil.copy(SHOTS / n, out / "shots" / n)
    if sounds:
        (out / "audio").mkdir()
    for n in sorted(sounds):
        if not (DEMOS / n).exists():
            raise SystemExit(f"audio/{n}: not rendered (run tools/krst_demos.sh first)")
        shutil.copy(DEMOS / n, out / "audio" / n)
    words = sorted((ROOT / "web" / "words").glob("*.json"))
    (out / "words").mkdir()
    for w in words:
        shutil.copy(w, out / "words" / w.name)
    link = (ROOT / "build" / "gen" / "ann_words.h")
    if link.exists():
        m = re.search(r'words/([0-9a-f]+\.json)', link.read_text())
        if m and not (out / "words" / m.group(1)).exists():
            raise SystemExit(f"words/{m.group(1)}: the build links to it, web/words has not got it")
    print(f"site: {out} ({fwsc}, {len(used)} screens, {len(sounds)} demos, {total} sounds, {len(words)} words files, "
          f"source {size})")


if __name__ == "__main__":
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    main(sys.argv[1], sys.argv[2], sys.argv[3] if len(sys.argv) > 3 else ROOT / "build" / "site")
