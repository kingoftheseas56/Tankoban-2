#!/usr/bin/env bash
# REPO_HYGIENE Phase 5 v1 (2026-04-26) — repo-consistency lint checks.
#
# CI-callable subset of the broader scripts/repo-health.ps1 drift audit,
# focused on the static invariants Phase 1 was designed to enforce. Runs
# on every push + PR via .github/workflows/repo-consistency.yml.
#
# Local invocation: `bash scripts/repo-consistency.sh` from repo root.
# Exits 0 on all-green, 1 on any check failure.
#
# Checks:
#   1. No hardcoded developer paths (C:/Users/, TankobanQTGroundWork) in
#      committed src/ + native_sidecar/src/.
#   2. Every CMakeLists.txt SOURCES + HEADERS entry exists on disk.
#   3. No bare debug-log filename literals in non-comment context.

set -u

cd "$(dirname "$0")/.."

fail=0

echo "== Check 1: hardcoded developer paths =="
# grep -vE filter excludes comment lines: // line comment, /*/* block, *
# block-comment continuation. Both forward-slash (C:/Users) and backslash
# (C:\Users) variants are caught.
hits=$(grep -rn -E '(C:[\\/]Users[\\/]|TankobanQTGroundWork)' \
    src/ native_sidecar/src/ \
    --include='*.cpp' --include='*.h' --include='*.hpp' 2>/dev/null \
    | grep -vE '^[^:]+:[0-9]+:[[:space:]]*(//|/\*|\*)' \
    || true)
if [ -n "$hits" ]; then
    echo "FAIL: hardcoded developer path(s) found in committed source:"
    echo "$hits"
    fail=1
else
    echo "PASS: no hardcoded developer paths."
fi
echo

echo "== Check 2: CMakeLists.txt SOURCES + HEADERS entries exist =="
# Extract file paths from set(SOURCES ...) and set(HEADERS ...) blocks.
# awk: state machine that flips on at "set(SOURCES" / "set(HEADERS" and
# off at the matching close-paren on its own line. Strips inline #
# comments. Skips blank + comment-only lines.
extract_block() {
    local file="$1"
    awk '
        /^set\((SOURCES|HEADERS)/ { in_block = 1; next }
        in_block && /^\)/ { in_block = 0; next }
        in_block {
            sub(/#.*$/, "")
            sub(/^[[:space:]]+/, "")
            sub(/[[:space:]]+$/, "")
            if ($0 != "") print $0
        }
    ' "$file"
}

# Same shape for native_sidecar/CMakeLists.txt's add_executable block.
extract_native_sidecar_sources() {
    awk '
        /^add_executable\(ffmpeg_sidecar/ { in_block = 1; next }
        in_block && /^\)/ { in_block = 0; next }
        in_block {
            sub(/#.*$/, "")
            sub(/^[[:space:]]+/, "")
            sub(/[[:space:]]+$/, "")
            if ($0 != "") print $0
        }
    ' native_sidecar/CMakeLists.txt
}

missing=0
while IFS= read -r path; do
    [ -z "$path" ] && continue
    if [ ! -f "$path" ]; then
        echo "FAIL: CMakeLists.txt references missing file: $path"
        missing=1
    fi
done < <(extract_block CMakeLists.txt)

if [ -f native_sidecar/CMakeLists.txt ]; then
    while IFS= read -r path; do
        [ -z "$path" ] && continue
        # Native sidecar paths are relative to native_sidecar/ in its
        # add_executable block.
        full="native_sidecar/$path"
        if [ ! -f "$full" ]; then
            echo "FAIL: native_sidecar/CMakeLists.txt references missing file: $full"
            missing=1
        fi
    done < <(extract_native_sidecar_sources)
fi

if [ "$missing" -eq 0 ]; then
    echo "PASS: every CMakeLists entry exists on disk."
else
    fail=1
fi
echo

echo "== Check 3: no bare debug-log filenames in non-comment source =="
# Match the bare literals; exclude lines that start (post-leading-whitespace)
# with // or * (block-comment continuation). Also exclude allow-listed
# legitimate gated-freopen callsite in native_sidecar/src/main.cpp.
log_hits=$(grep -rn -E '"_boot_debug\.txt"|"_player_debug\.txt"|"sub_debug\.log"|"sidecar_debug_live\.log"' \
    src/ native_sidecar/src/ \
    --include='*.cpp' --include='*.h' 2>/dev/null \
    | grep -vE '^[^:]+:[0-9]+:[[:space:]]*(//|\*)' \
    || true)
# Allow-list: the env-gated freopen in native_sidecar/src/main.cpp (P1.2
# shipped this as opt-in via TANKOBAN_SIDECAR_DEBUG=1; the literal is still
# present in source but gated at runtime).
if [ -n "$log_hits" ]; then
    filtered=$(echo "$log_hits" | grep -v 'native_sidecar/src/main.cpp' || true)
    if [ -n "$filtered" ]; then
        echo "FAIL: bare debug-log filename literal in non-comment source:"
        echo "$filtered"
        fail=1
    else
        echo "PASS: only allow-listed env-gated debug-log literal present."
    fi
else
    echo "PASS: no bare debug-log literals."
fi
echo

if [ "$fail" -eq 0 ]; then
    echo "All checks passed."
else
    echo "One or more checks FAILED."
fi
exit "$fail"
