#!/usr/bin/env bash
# TANKORENT_QUALITY_AND_QUEUE Phase 2 — measures the parity gap between
# nyaa.si's own results and what Tankorent's NyaaIndexer surfaces for the same
# query. Counts result rows across nyaa.si pages 1..N (matching the indexer's
# NYAA_MAX_PAGES sweep) and reports the delta vs Tankorent's effective cap.
#
# Usage:
#   bash scripts/nyaa-parity-probe.sh "One Piece"
#   bash scripts/nyaa-parity-probe.sh "Naruto" 1_2     # optional category
#
# Note: Nyaa is anime/Asian-content focused. Western titles (e.g. "Daredevil")
# legitimately return ~1 row — use an anime title to exercise real pagination.
#
# After this prints nyaa.si's count, compare with Tankorent: search the same
# query in the Tankorent tab and read the result-count label, OR via dev-bridge
# once a sources-search command exists. Counts should match within dedup slack
# once the Phase 2 limit fix lands.

set -u
QUERY="${1:?usage: nyaa-parity-probe.sh \"<query>\" [category]}"
CATEGORY="${2:-0_0}"
MAX_PAGES="${3:-4}"
UA="Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko)"

# URL-encode spaces (good enough for typical queries; curl --data-urlencode is
# overkill for a diagnostic).
ENC="${QUERY// /+}"

echo "Nyaa.si parity probe — query: [$QUERY]  category: $CATEGORY"
printf '%.0s-' {1..56}; echo

total=0
for p in $(seq 1 "$MAX_PAGES"); do
    if [ "$p" = "1" ]; then
        url="https://nyaa.si/?f=0&c=${CATEGORY}&q=${ENC}"
    else
        url="https://nyaa.si/?f=0&c=${CATEGORY}&q=${ENC}&p=${p}"
    fi
    n=$(curl -s -A "$UA" "$url" | grep -oE '<tr class="(default|success|danger)">' | wc -l)
    n=$(echo "$n" | tr -d ' ')
    total=$((total + n))
    printf 'page %2s: %3s rows\n' "$p" "$n"
    # nyaa shows 75/page; fewer means last page.
    if [ "$n" -lt 75 ]; then break; fi
done

printf '%.0s-' {1..56}; echo
echo "Nyaa.si cumulative rows (pages 1..$MAX_PAGES): $total"
echo
echo "Tankorent NyaaIndexer effective ceilings:"
echo "  NYAA_PAGE_SIZE      = 75   (per page)"
echo "  NYAA_MAX_PAGES      = 4    (hard page ceiling = 300 max)"
echo "  TankorentPage limit = 300  (Phase 2 T2.2 — was 80 pre-fix)"
echo
if [ "$total" -gt 300 ]; then
    echo "DELTA: nyaa.si has $total; Tankorent ceiling is 300 → residual gap $((total - 300)) (long-tail, low-seeder)."
else
    echo "PARITY: nyaa.si total ($total) is within Tankorent's 300 ceiling — full coverage."
fi
