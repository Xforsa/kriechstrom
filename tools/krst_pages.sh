#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# KRIECHSTROM: put build/site on the gh-pages branch (GitHub Pages: Settings > Pages > branch gh-pages, / (root)).
#   tools/krst_pages.sh            commits build/site to gh-pages (a worktree in build/pages); push it yourself:
#   git push origin gh-pages
set -e
cd "$(dirname "$0")/.."
[ -f build/site/index.html ] || { echo "no build/site: run tools/krst_release.sh first"; exit 1; }
if ! git show-ref --quiet refs/heads/gh-pages; then
    git worktree add --detach build/pages
    (cd build/pages && git checkout --orphan gh-pages && git rm -rfq . )
else
    [ -d build/pages ] || git worktree add build/pages gh-pages
fi
rsync -a --delete --exclude .git build/site/ build/pages/
touch build/pages/.nojekyll
(cd build/pages && git add -A && git commit -qm "site $(git -C .. describe --always)" && git log --oneline -1)
