#!/bin/sh
# KRIECHSTROM: the screen reader's words, audited by fm1-reader (../fm1-reader, next to this repository): every screen,
# card value, list row, dialog and layer as the device says it (tests/krst_speak_test.c --dump), and the messages in the
# sources, put into words with the words file and checked against fm1-reader's rules. Exit 1 on an error.
#   tools/krst_speak_audit.sh [--spoken]
set -e
cd "$(dirname "$0")/.."
R="${FM1_READER:-../fm1-reader}"
NODE="${NODE:-node}"
[ -f "$R/tools/audit.mjs" ] || { echo "no fm1-reader at $R (set FM1_READER)"; exit 2; }
F="-DKRST_PALETTE=0 -DKRST_LARGE_DEF=0 -DKRST_PANEL_DEF=0 -Ifirmware/hal -Ifirmware/bsp/chip -Ifirmware/bsp/boards/fm1 -Ifirmware/src -Ibuild/gen"
mkdir -p build/host build/sr
python3 tools/gen_words.py firmware/words.tsv web/words build/gen/ann_words.h >/dev/null
cc -O1 -w $F -o build/host/krst_speak_test tests/krst_speak_test.c -lm
build/host/krst_speak_test --dump | grep -v '^ui:\|^speak:' | sort -u > build/sr/dump.tsv
grep -ohE 'ui_(say|message)\("[^"]*"(, "[^"]*")?' firmware/src/*.c | sed -E 's/ui_(say|message)\("//; s/", "//; s/"$//; s/[][]//g; s/->/ to /g; s/>/ to /g' |
    sort -u | awk 'NF { print "MSG\t" $0 }' > build/sr/msgs.tsv
cat build/sr/dump.tsv build/sr/msgs.tsv > build/sr/all.tsv
W=$(sed -n 's/.*words\/\(.*\)\.json.*/\1/p' build/gen/ann_words.h)
"$NODE" "$R/tools/audit.mjs" build/sr/all.tsv --words "web/words/$W.json" --allow tests/krst_speak_allow.txt "$@"
