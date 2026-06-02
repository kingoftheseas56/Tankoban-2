# Governance gate — must-hold, mechanically-checkable brotherhood invariants (2026-06-02).
#
# Backs the rules that MUST hold with a deterministic CI BLOCK. Per the 2026-06-02
# best-practices research: written rules + dodgeable hooks are not enough for critical
# invariants — a gate has to physically fail the build. Sibling to netseam-gate.ps1.
# Only mechanically-checkable, must-hold rules live here (behavioral/judgment rules
# like Hemanth-language cannot be gated and stay in GOVERNANCE.md).
#
# Usage: powershell -NoProfile -File scripts/governance-gate.ps1
#   exit 0 = clean; exit 1 = one or more must-hold invariants violated.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root
$fail = 0
$self = 'scripts/governance-gate.ps1'

$tracked = @(git ls-files)
$binExt = '\.(png|jpg|jpeg|gif|ico|pdf|zip|gz|7z|dll|lib|exe|so|dylib|bin|ttf|otf|woff2?|mp4|webm|woff)$'

# --- Invariant 1: NO SECRETS in tracked files (catastrophic) ---
# Real-key shapes: OpenAI/DeepSeek sk-+24hex; Anthropic sk-ant-; Google AIzaSy+.
$secretRe = '(sk-[a-fA-F0-9]{24,}|sk-ant-[A-Za-z0-9_\-]{20,}|AIzaSy[A-Za-z0-9_\-]{20,})'
$secretHits = @()
foreach ($f in $tracked) {
    if ($f -eq $self -or $f -match $binExt -or -not (Test-Path $f)) { continue }
    $m = Select-String -Path $f -Pattern $secretRe -ErrorAction SilentlyContinue
    if ($m) { foreach ($h in $m) { $secretHits += ("  {0}:{1}" -f $f, $h.LineNumber) } }
}
if ($secretHits) {
    Write-Output "GOVERNANCE GATE FAILED [secrets] - API-key-shaped strings in tracked files:"
    $secretHits | ForEach-Object { Write-Output $_ }
    Write-Output "Secrets live in env / a gitignored .env ONLY. Rotate the key and remove it from the tree."
    $fail = 1
}

# --- Invariant 2: agents/routes.yml pointers resolve (no doc-vs-code drift) ---
if (Test-Path 'agents/routes.yml') {
    $refs = (Select-String -Path 'agents/routes.yml' -Pattern '[A-Za-z0-9_./-]+\.(md|bat|yml|py|json|sh)' -AllMatches).Matches.Value | Sort-Object -Unique
    $missing = @($refs | Where-Object { -not (Test-Path $_) })
    if ($missing.Count) {
        Write-Output "GOVERNANCE GATE FAILED [routes] - agents/routes.yml points at files that do not exist:"
        $missing | ForEach-Object { Write-Output ("  {0}" -f $_) }
        Write-Output "Update agents/routes.yml when files move/rename (the kernel routes through it)."
        $fail = 1
    }
}

# --- Invariant 3: no build binaries / oversized files tracked under agents/ ---
$agentsTracked = @($tracked | Where-Object { $_ -like 'agents/*' })
$binHits = @($agentsTracked | Where-Object { $_ -match '\.(dll|lib|exe|so|dylib|bin)$' })
$bigHits = @()
foreach ($f in $agentsTracked) {
    if (Test-Path $f) {
        $sz = (Get-Item $f).Length
        if ($sz -gt 10MB) { $bigHits += ("  {0} ({1:N1} MB)" -f $f, ($sz / 1MB)) }
    }
}
if ($binHits.Count -or $bigHits.Count) {
    Write-Output "GOVERNANCE GATE FAILED [agents-weight] - build binaries or oversized files tracked under agents/:"
    $binHits | ForEach-Object { Write-Output ("  {0} (binary)" -f $_) }
    $bigHits | ForEach-Object { Write-Output $_ }
    Write-Output "agents/ is human-authored record; binaries/large assets belong in resources/ (gitignored)."
    $fail = 1
}

if ($fail -eq 0) {
    Write-Output "GOVERNANCE GATE OK - no leaked secrets, routes.yml resolves, agents/ weight clean."
}
exit $fail
