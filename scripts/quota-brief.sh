#!/usr/bin/env bash
# Quota vigilance one-liner for Agent 0's /brief (coordinator-only step).
# Compares TODAY's Claude (Max-pool) token burn to the trailing 7-day average and
# prints NORMAL / elevated / HOT. Read-only; ~10-30s via npx (first run downloads
# ccusage, later runs are cached). Claude-only on purpose — that's the Max pool;
# Codex/DeepSeek are separate. The $ figures ccusage shows are NOTIONAL api-equiv,
# not a Max bill — we track TOKENS. Rationale: feedback_quota_vigilance memory.
set -u
SINCE=$(date -d '14 days ago' +%Y%m%d 2>/dev/null || echo 20260518)
JSON=$(npx -y ccusage@latest daily --json --since "$SINCE" 2>/dev/null)
QUOTA_JSON="$JSON" python - <<'PY'
import os, json
raw = os.environ.get("QUOTA_JSON", "").strip()
if not raw:
    print("Quota: ccusage unavailable (offline / npx) - skipped"); raise SystemExit
try:
    d = json.loads(raw)
except Exception:
    print("Quota: ccusage output unparseable - skipped"); raise SystemExit
rows = d.get("daily") or []
def claude_tokens(r):
    mb = r.get("modelBreakdowns") or []
    cl = [m for m in mb if str(m.get("modelName", "")).startswith("claude")]
    if cl:
        return sum(m.get("cacheCreationTokens", 0) + m.get("cacheReadTokens", 0)
                   + m.get("inputTokens", 0) + m.get("outputTokens", 0) for m in cl)
    return r.get("totalTokens", 0)
by_date = {}
for r in rows:
    if r.get("agent") not in ("all", None):
        continue
    p = r.get("period") or r.get("date")
    if p:
        by_date[p] = claude_tokens(r)
dates = sorted(by_date)
if not dates:
    print("Quota: no usage data"); raise SystemExit
today = dates[-1]; tt = by_date[today]
prior = [by_date[x] for x in dates[:-1]][-7:]
avg = sum(prior) / len(prior) if prior else 0
mm = lambda n: "%dM" % round(n / 1e6)
if avg and tt > 1.6 * avg:
    v = "[HOT] %.1fx the %dd avg - heavy day; push mechanical work to DeepSeek/Codex, keep Opus for design" % (tt / avg, len(prior))
elif avg and tt > 1.2 * avg:
    v = "[elevated] %.1fx avg" % (tt / avg)
else:
    v = "NORMAL"
print("Quota (Claude/Max) %s: today %s tok | %dd avg %s | %s" % (today, mm(tt), len(prior), mm(avg), v))
PY
