#!/usr/bin/env pwsh
# build.ps1 — Configure, build, and install the native FFmpeg sidecar binary.
# Usage: .\build.ps1          (from native_sidecar/ directory)
#        .\build.ps1 -Clean   (clean build)

param([switch]$Clean)

$ErrorActionPreference = "Stop"

# Ensure MinGW + CMake are on PATH
$env:PATH = "C:\tools\mingw64\bin;C:\tools\cmake-3.31.6-windows-x86_64\bin;$env:PATH"

$buildDir = Join-Path $PSScriptRoot "build"
$repoRoot = Split-Path $PSScriptRoot -Parent
$installDir = Join-Path $repoRoot "resources\ffmpeg_sidecar"

if ($Clean -and (Test-Path $buildDir)) {
    Write-Host "Cleaning build directory..."
    Remove-Item -Recurse -Force $buildDir
}

# Configure
Write-Host "Configuring..."
cmake -B $buildDir -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release $PSScriptRoot
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }

# Build
Write-Host "Building..."
cmake --build $buildDir -j4
if ($LASTEXITCODE -ne 0) { throw "CMake build failed" }

# Install binary
Write-Host "Installing to $installDir..."
New-Item -ItemType Directory -Force -Path $installDir | Out-Null
Copy-Item "$buildDir\ffmpeg_sidecar.exe" "$installDir\ffmpeg_sidecar.exe" -Force

# Copy ffprobe.exe from the same MinGW ffmpeg build — consumed by the main-app
# AudiobookMetaCache (AUDIOBOOK_PAIRED_READING_FIX Phase 1.1) to extract
# audiobook chapter durations. Uses the same avformat/avcodec/avutil DLLs
# already copied below, so no separate DLL set is required.
$ffprobeExe = "C:\tools\ffmpeg-master-latest-win64-gpl-shared\bin\ffprobe.exe"
if (Test-Path $ffprobeExe) {
    Copy-Item $ffprobeExe "$installDir\ffprobe.exe" -Force
}

# Copy MinGW runtime DLLs
$mingwBin = "C:\tools\mingw64\bin"
$runtimeDlls = @("libstdc++-6.dll", "libgcc_s_seh-1.dll", "libwinpthread-1.dll")
foreach ($dll in $runtimeDlls) {
    $src = Join-Path $mingwBin $dll
    if (Test-Path $src) {
        Copy-Item $src "$installDir\$dll" -Force
    }
}

# Copy FFmpeg shared DLLs — include avfilter (used by FilterGraph) alongside
# core codec/format/util/resample/swscale.
$ffmpegBin = "C:\tools\ffmpeg-master-latest-win64-gpl-shared\bin"
$ffmpegDlls = @(
    "avcodec-62.dll", "avformat-62.dll", "avutil-60.dll",
    "swscale-9.dll", "swresample-6.dll", "avfilter-11.dll",
    "postproc-59.dll", "avdevice-62.dll"
)
foreach ($dll in $ffmpegDlls) {
    $src = Join-Path $ffmpegBin $dll
    if (Test-Path $src) {
        Copy-Item $src "$installDir\$dll" -Force
    }
}

# Copy PortAudio DLL
$portaudioDll = "C:\tools\portaudio\bin\libportaudio.dll"
if (Test-Path $portaudioDll) {
    Copy-Item $portaudioDll "$installDir\libportaudio.dll" -Force
}

# Copy subtitle renderer chain (libass + text-shaping deps). Sidecar fails
# with STATUS_DLL_NOT_FOUND (0xC0000135) at launch without these.
$toolDllRoots = @(
    @{ root = "C:\tools\libass";    patterns = @("libass*.dll", "ass*.dll") },
    @{ root = "C:\tools\freetype";  patterns = @("libfreetype*.dll", "freetype*.dll") },
    @{ root = "C:\tools\fribidi";   patterns = @("libfribidi*.dll", "fribidi*.dll") },
    @{ root = "C:\tools\harfbuzz";  patterns = @("libharfbuzz*.dll", "harfbuzz*.dll") },
    @{ root = "C:\tools\uchardet";  patterns = @("libuchardet*.dll", "uchardet*.dll") },
    @{ root = "C:\tools\lcms2";     patterns = @("liblcms2*.dll", "lcms2*.dll") },
    @{ root = "C:\tools\libplacebo"; patterns = @("libplacebo*.dll", "placebo*.dll") },
    @{ root = "C:\tools\vulkan";    patterns = @("vulkan-1.dll") },
    @{ root = "C:\tools\glslang";   patterns = @("libglslang*.dll", "glslang*.dll", "SPIRV*.dll") }
)
$copiedExtras = @()
foreach ($entry in $toolDllRoots) {
    foreach ($subdir in @("bin", "lib", "")) {
        $base = if ($subdir) { Join-Path $entry.root $subdir } else { $entry.root }
        if (-not (Test-Path $base)) { continue }
        foreach ($pattern in $entry.patterns) {
            Get-ChildItem -Path $base -Filter $pattern -ErrorAction SilentlyContinue | ForEach-Object {
                Copy-Item $_.FullName "$installDir\$($_.Name)" -Force
                $copiedExtras += $_.Name
            }
        }
    }
}

Write-Host ""
Write-Host "Build complete: $installDir\ffmpeg_sidecar.exe"
Write-Host "Core DLLs: $($runtimeDlls + $ffmpegDlls + @('libportaudio.dll') -join ', ')"
if ($copiedExtras.Count -gt 0) {
    Write-Host "Subtitle/render DLLs: $(($copiedExtras | Sort-Object -Unique) -join ', ')"
}
