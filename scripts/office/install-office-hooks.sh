#!/usr/bin/env bash
# Install the Office git hooks into .git/hooks (reproducible: the SOURCE lives in
# scripts/office/, tracked; this just wires it into the local repo's .git/hooks).
# If a post-commit hook already exists and isn't ours, we append a call (chain).
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(git -C "$HERE" rev-parse --show-toplevel)"
HOOK="$REPO/.git/hooks/post-commit"
SRC="$HERE/git-post-commit-mirror.sh"
MARK="# >>> office mirror >>>"

if [ -f "$HOOK" ] && grep -q "$MARK" "$HOOK"; then
  echo "office hooks: already installed"
  exit 0
fi

if [ ! -f "$HOOK" ]; then
  printf '#!/usr/bin/env bash\n' > "$HOOK"
fi
{
  echo "$MARK"
  echo "bash \"$SRC\" \"\$@\""
  echo "# <<< office mirror <<<"
} >> "$HOOK"
chmod +x "$HOOK"
echo "office hooks: installed post-commit mirror -> $HOOK"
