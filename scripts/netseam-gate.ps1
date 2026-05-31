# NetSeam CI grep-gate (Congress 9, 2026-05-31).
#
# Fails if any raw `new QNetworkAccessManager` creation exists in src/ OUTSIDE
# the NetSeam factory itself (src/core/net/NetSeam.cpp owns the one legit vanilla
# path). Every domain's outbound HTTP must be vended by NetSeam::createManager so
# the observable network layer can see/throttle/block it. This gate is the
# convention-enforcer the brotherhood ratified — it makes "no raw managers"
# checkable, not hope-based.
#
# Usage: powershell -NoProfile -File scripts/netseam-gate.ps1
#   exit 0 = clean; exit 1 = one or more raw creation sites (printed file:line).
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

$files = Get-ChildItem -Path (Join-Path $root 'src') -Recurse -Include *.cpp,*.h -File
$hits = $files |
    Select-String -Pattern 'new\s+QNetworkAccessManager' |
    Where-Object { $_.Path -notmatch 'NetSeam\.cpp$' }

if ($hits) {
    Write-Output "NETSEAM GATE FAILED - raw QNetworkAccessManager creation outside the NetSeam factory:"
    foreach ($h in $hits) { Write-Output ("  {0}:{1}" -f $h.Path, $h.LineNumber) }
    Write-Output "Route it through tankoban::net::NetSeam::instance()->createManager(parent, sourceTag)."
    exit 1
}
Write-Output "NETSEAM GATE OK - no raw QNetworkAccessManager outside the factory."
exit 0
