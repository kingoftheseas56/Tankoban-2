# REPO_HYGIENE Phase 6 (2026-04-26) — semantic-version bump helper.
#
# Bumps the version in CMakeLists.txt + installer/tankoban.nsi + vcpkg.json,
# commits the change, tags the result, and pushes the tag to trigger
# .github/workflows/release.yml.
#
# Usage:
#   pwsh scripts/version-bump.ps1 -Version 0.2.0
#
# After this script: the release workflow runs on GitHub, builds the
# installer, and publishes the GitHub Release. Allow ~30 minutes for the
# Windows runner + NSIS pipeline to complete.

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^\d+\.\d+\.\d+$')]
    [string]$Version,

    [switch]$NoTag,
    [switch]$NoPush,
    [switch]$DryRun
)

$repoRoot = Split-Path -Parent $PSScriptRoot
Push-Location $repoRoot

try {
    Write-Host "Bumping Tankoban version to $Version..." -ForegroundColor Cyan

    # ── 1. CMakeLists.txt project version ───────────────────────────────────
    $cmakeFile = "CMakeLists.txt"
    $cmakeContent = Get-Content $cmakeFile -Raw
    $newCmake = $cmakeContent -replace 'project\(Tankoban VERSION \d+\.\d+\.\d+', "project(Tankoban VERSION $Version"
    if ($cmakeContent -eq $newCmake) {
        Write-Warning "CMakeLists.txt: project(Tankoban VERSION ...) line not found or already at $Version."
    } else {
        if (-not $DryRun) { Set-Content $cmakeFile $newCmake -NoNewline }
        Write-Host "  CMakeLists.txt → $Version" -ForegroundColor Green
    }

    # ── 2. installer/tankoban.nsi APP_VERSION ──────────────────────────────
    $nsiFile = "installer/tankoban.nsi"
    if (Test-Path $nsiFile) {
        $nsiContent = Get-Content $nsiFile -Raw
        $newNsi = $nsiContent -replace '!define APP_VERSION\s+"\d+\.\d+\.\d+"', "!define APP_VERSION        `"$Version`""
        if ($nsiContent -eq $newNsi) {
            Write-Warning "installer/tankoban.nsi: APP_VERSION not found or already at $Version."
        } else {
            if (-not $DryRun) { Set-Content $nsiFile $newNsi -NoNewline }
            Write-Host "  installer/tankoban.nsi → $Version" -ForegroundColor Green
        }
    }

    # ── 3. vcpkg.json version-string ───────────────────────────────────────
    $vcpkgFile = "vcpkg.json"
    if (Test-Path $vcpkgFile) {
        $vcpkgContent = Get-Content $vcpkgFile -Raw
        $newVcpkg = $vcpkgContent -replace '"version-string":\s*"\d+\.\d+\.\d+"', "`"version-string`": `"$Version`""
        if ($vcpkgContent -eq $newVcpkg) {
            Write-Warning "vcpkg.json: version-string not found or already at $Version."
        } else {
            if (-not $DryRun) { Set-Content $vcpkgFile $newVcpkg -NoNewline }
            Write-Host "  vcpkg.json → $Version" -ForegroundColor Green
        }
    }

    if ($DryRun) {
        Write-Host "DryRun: skipping commit + tag." -ForegroundColor Yellow
        return
    }

    # ── 4. Commit + tag ────────────────────────────────────────────────────
    $tag = "v$Version"
    Write-Host "`nStaging changes..." -ForegroundColor Cyan
    git add CMakeLists.txt installer/tankoban.nsi vcpkg.json
    git commit -m "Bump version to $Version"

    if (-not $NoTag) {
        Write-Host "Creating tag $tag..." -ForegroundColor Cyan
        git tag -a $tag -m "Tankoban $Version"
    }

    if (-not $NoPush) {
        Write-Host "Pushing commit + tag to origin..." -ForegroundColor Cyan
        git push origin
        if (-not $NoTag) {
            git push origin $tag
            Write-Host "`nTag pushed. The release workflow will run on GitHub:"
            Write-Host "  https://github.com/kingoftheseas56/Tankoban-2/actions" -ForegroundColor Cyan
            Write-Host "Allow ~30 minutes for the Windows runner + NSIS pipeline."
        }
    } else {
        Write-Host "NoPush: commit + tag created locally; push manually with:" -ForegroundColor Yellow
        Write-Host "  git push origin && git push origin $tag" -ForegroundColor Cyan
    }

} finally {
    Pop-Location
}
