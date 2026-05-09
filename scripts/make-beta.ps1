#requires -Version 5
<#
.SYNOPSIS
  One-button rebuild of Tankoban-Beta-Setup.exe + (optional) silent reinstall.

.DESCRIPTION
  Refreshes the beta-flavored installer from current src/ state. Handles the
  TANKOBAN_BETA configure-toggle dance, restages the runtime tree, runs
  windeployqt + makensis, optionally uninstalls + reinstalls the running beta,
  then restores the dev tree (TANKOBAN_BETA=OFF + rebuild) so build_and_run.bat
  still produces a normal dev binary.

.PARAMETER SkipInstall
  Build the installer .exe but don't auto-uninstall + reinstall. Use when you
  want to ship the installer to someone else, or test it manually.

.PARAMETER NoDevRestore
  Skip the final TANKOBAN_BETA=OFF reconfigure + rebuild. Faster, but leaves
  out\Tankoban.exe beta-flavored - you'll need to manually restore later.

.EXAMPLE
  pwsh -File scripts\make-beta.ps1
  # Default: rebuild installer, uninstall existing beta, install fresh, restore dev tree.

.EXAMPLE
  pwsh -File scripts\make-beta.ps1 -SkipInstall
  # Just rebuild the installer .exe and leave it on Desktop. Useful for sharing.

.NOTES
  Authored 2026-05-07. Sibling to installer\tankoban-beta.nsi.
  See installer\tankoban.nsi for the GitHub Releases path (driven by release.yml on tag push).
#>

[CmdletBinding()]
param(
    [switch]$SkipInstall,
    [switch]$NoDevRestore
)

$ErrorActionPreference = "Stop"

# --- Paths -----------------------------------------------------------------
$repoRoot = Resolve-Path "$PSScriptRoot\.."
$buildDir = Join-Path $repoRoot "out"
$stage    = "$env:USERPROFILE\Desktop\Tankoban-Beta"
$installer= "$env:USERPROFILE\Desktop\Tankoban-Beta-Setup.exe"
$nsi      = Join-Path $repoRoot "installer\tankoban-beta.nsi"
$qtDir    = "C:\tools\qt6sdk\6.10.2\msvc2022_64"
$winDeploy= "$qtDir\bin\windeployqt.exe"
$nsisExe  = "C:\Program Files (x86)\NSIS\makensis.exe"
$vcvars   = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat"

# --- Pre-flight ------------------------------------------------------------
Write-Host "=== Tankoban Beta Refresh ===" -ForegroundColor Cyan
$tStart = Get-Date

foreach ($p in @($winDeploy, $nsisExe, $vcvars, $nsi)) {
    if (-not (Test-Path $p)) {
        Write-Host "ABORT: prerequisite not found: $p" -ForegroundColor Red
        exit 1
    }
}

# --- Helper: invoke a .bat that needs vcvars in the same shell -------------
function Invoke-VcvarsBuild {
    param([string]$BetaFlag)  # "ON" or "OFF"
    $tmpBat = [System.IO.Path]::GetTempFileName() + ".bat"
    $body = @"
@echo off
setlocal
call "$vcvars" x64 >nul 2>&1
if errorlevel 1 ( echo VCVARS FAILED & exit /b 1 )
if "%VCPKG_ROOT%"=="" (
    if exist "C:\vcpkg\vcpkg.exe" set "VCPKG_ROOT=C:\vcpkg"
)
cmake -B "$buildDir" -DTANKOBAN_BETA=$BetaFlag
if errorlevel 1 ( echo CONFIGURE FAILED & exit /b 1 )
cmake --build "$buildDir" --config Release --target Tankoban
if errorlevel 1 ( echo BUILD FAILED & exit /b 1 )
exit /b 0
"@
    [System.IO.File]::WriteAllText($tmpBat, ($body -replace "`r?`n", "`r`n"), [System.Text.Encoding]::ASCII)
    $tail = & cmd.exe /c "`"$tmpBat`"" 2>&1 | Select-Object -Last 5
    $code = $LASTEXITCODE
    Remove-Item $tmpBat -Force -ErrorAction SilentlyContinue
    if ($code -ne 0) {
        Write-Host "BUILD FAILED with TANKOBAN_BETA=$BetaFlag - last lines:" -ForegroundColor Red
        $tail | ForEach-Object { Write-Host "  $_" }
        exit $code
    }
}

function Format-Sec { param([double]$s) return ("{0:N1}s" -f $s) }

# --- 1. Kill any running Tankoban -----------------------------------------
Write-Host "[1/7] Killing any running Tankoban.exe..."
taskkill /F /IM Tankoban.exe 2>&1 | Out-Null
taskkill /F /IM ffmpeg_sidecar.exe 2>&1 | Out-Null
Start-Sleep -Milliseconds 500

# --- 2. Build with TANKOBAN_BETA=ON ---------------------------------------
$t = Get-Date
Write-Host "[2/7] Building Tankoban.exe with TANKOBAN_BETA=ON ..."
Invoke-VcvarsBuild -BetaFlag "ON"
Write-Host ("       OK (" + (Format-Sec ((Get-Date) - $t).TotalSeconds) + ")")

# --- 3. Stage runtime tree ------------------------------------------------
$t = Get-Date
Write-Host "[3/7] Staging Tankoban-Beta\..."
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory -Path $stage -Force | Out-Null
$exclF = @("*.pdb","*.ilk","*.exp","*.lib","*.obj","*.log","*.txt","*.csv","*.png","*.ass","*.diff","*.json","build.ninja","cmake_install.cmake","CMakeCache.txt","CTestTestfile.cmake","tankoctl.exe","libmpv-2.dll","LICENSE.libmpv.txt")
$exclD = @("CMakeFiles","Tankoban_autogen","tankoctl_autogen","_deps","vcpkg_installed","Testing",".qt","tmp","src","_player_debug","build_artifacts")
robocopy "$buildDir" $stage /E /NP /NJH /NJS /XF $exclF /XD $exclD | Out-Null
Copy-Item "$repoRoot\resources\ffmpeg_sidecar\*" -Destination $stage -Force
& $winDeploy --release --no-translations --no-system-d3d-compiler --no-opengl-sw "$stage\Tankoban.exe" 2>&1 | Out-Null
$stageMb = [math]::Round((Get-ChildItem -Recurse $stage | Measure-Object Length -Sum).Sum / 1MB, 1)
Write-Host ("       OK (" + (Format-Sec ((Get-Date) - $t).TotalSeconds) + ", " + $stageMb + " MB)")

# --- 4. Run makensis ------------------------------------------------------
$t = Get-Date
Write-Host "[4/7] Compiling installer (LZMA solid, expect ~9 min)..."
$nsisOut = & $nsisExe /V2 $nsi 2>&1
if (-not (Test-Path $installer)) {
    Write-Host "ABORT: makensis failed - output below:" -ForegroundColor Red
    $nsisOut | Select-Object -Last 15 | ForEach-Object { Write-Host "  $_" }
    exit 1
}
$installMb = [math]::Round((Get-Item $installer).Length / 1MB, 1)
Write-Host ("       OK (" + (Format-Sec ((Get-Date) - $t).TotalSeconds) + ", " + $installMb + " MB -> " + $installer + ")")

# --- 5. Uninstall existing beta (if installed) ----------------------------
if (-not $SkipInstall) {
    $t = Get-Date
    $existing = "$env:LOCALAPPDATA\Tankoban Beta\Uninstall.exe"
    if (Test-Path $existing) {
        Write-Host "[5/7] Uninstalling existing beta..."
        Start-Process -FilePath $existing -ArgumentList "/S" -Wait
        $waited = 0
        while ((Test-Path "$env:LOCALAPPDATA\Tankoban Beta\Tankoban.exe") -and $waited -lt 30) {
            Start-Sleep -Seconds 1
            $waited++
        }
        Write-Host ("       OK (" + (Format-Sec ((Get-Date) - $t).TotalSeconds) + ")")
    } else {
        Write-Host "[5/7] No existing beta install - skipping uninstall."
    }

    # --- 6. Install fresh ---------------------------------------------------
    $t = Get-Date
    Write-Host "[6/7] Installing fresh beta..."
    $proc = Start-Process -FilePath $installer -ArgumentList "/S" -Wait -PassThru
    if ($proc.ExitCode -ne 0) {
        Write-Host ("ABORT: installer exit code " + $proc.ExitCode) -ForegroundColor Red
        exit $proc.ExitCode
    }
    Start-Sleep -Seconds 3
    if (-not (Test-Path "$env:LOCALAPPDATA\Tankoban Beta\Tankoban.exe")) {
        Write-Host "ABORT: post-install Tankoban.exe not present at expected location" -ForegroundColor Red
        exit 1
    }
    Write-Host ("       OK (" + (Format-Sec ((Get-Date) - $t).TotalSeconds) + ")")
} else {
    Write-Host "[5-6/7] -SkipInstall set - installer .exe ready, not running it."
}

# --- 7. Restore dev tree --------------------------------------------------
if (-not $NoDevRestore) {
    $t = Get-Date
    Write-Host "[7/7] Restoring dev tree (TANKOBAN_BETA=OFF + rebuild)..."
    taskkill /F /IM Tankoban.exe 2>&1 | Out-Null
    Invoke-VcvarsBuild -BetaFlag "OFF"
    Write-Host ("       OK (" + (Format-Sec ((Get-Date) - $t).TotalSeconds) + ")")
} else {
    Write-Host "[7/7] -NoDevRestore set - dev tree left in BETA=ON state. Restore later with:"
    Write-Host "        cmake -B out -DTANKOBAN_BETA=OFF" -ForegroundColor Yellow
    Write-Host "        cmake --build out --target Tankoban" -ForegroundColor Yellow
}

# --- Done -----------------------------------------------------------------
$total = ((Get-Date) - $tStart).TotalSeconds
$totalMin = [math]::Round($total / 60, 1)
Write-Host ""
Write-Host ("=== DONE in " + (Format-Sec $total) + " (" + $totalMin + " min) ===") -ForegroundColor Green
Write-Host ("Installer:    " + $installer)
if (-not $SkipInstall) {
    Write-Host ("Installed at: " + $env:LOCALAPPDATA + "\Tankoban Beta\")
    Write-Host "Launch via:   Start Menu -> Tankoban Beta, or Desktop shortcut"
}
