; REPO_HYGIENE Phase 6 (2026-04-26) — NSIS installer for Tankoban.
;
; Builds Tankoban-Setup.exe that installs Tankoban to Program Files like a
; normal Windows app. Bundles Tankoban.exe, ffmpeg_sidecar.exe, Qt runtime
; DLLs (deployed via windeployqt before NSIS compile), and book/comic
; reader resources. Excludes: dev-control bridge stays in code but the
; --dev-control flag is NOT set in any installed launcher; debug symbols
; (.pdb); test binaries; tankoctl.exe (dev tool only).
;
; Driven by .github/workflows/release.yml on tag push (v*.*.*).
;
; Build inputs (all paths relative to the build artifacts the workflow
; assembles in `installer-staging/`):
;   - Tankoban.exe
;   - tankoctl.exe              (excluded — dev tool)
;   - ffmpeg_sidecar.exe + DLLs
;   - Qt runtime DLLs (windeployqt output)
;   - resources/book_reader/*
;   - resources/icons/*

!define APP_NAME           "Tankoban"
!define APP_VERSION        "0.1.0"
!define APP_PUBLISHER      "Hemanth (kingoftheseas56)"
!define APP_URL            "https://github.com/kingoftheseas56/Tankoban-2"
!define APP_EXE            "Tankoban.exe"
!define UNINST_KEY         "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}"

Name "${APP_NAME} ${APP_VERSION}"
OutFile "Tankoban-Setup.exe"
InstallDir "$PROGRAMFILES64\${APP_NAME}"
InstallDirRegKey HKLM "Software\${APP_NAME}" "InstallDir"
RequestExecutionLevel admin
SetCompressor /SOLID lzma

!include "MUI2.nsh"

!define MUI_ABORTWARNING

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "..\LICENSE"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

Section "Install"
    SetOutPath "$INSTDIR"

    ; Main exe + dev/dev-bridge code path stays in the binary but the
    ; production launcher (Start Menu shortcut + uninstall registration)
    ; does NOT pass --dev-control, so DevControlServer never listens.
    File "..\installer-staging\Tankoban.exe"

    ; Sidecar + its bundled DLLs (FFmpeg, libplacebo, libass, etc.).
    SetOutPath "$INSTDIR"
    File "..\installer-staging\ffmpeg_sidecar.exe"
    File "..\installer-staging\*.dll"

    ; Resources (book/comic reader assets, icons).
    SetOutPath "$INSTDIR\resources"
    File /r "..\installer-staging\resources\*"

    ; Start Menu + Desktop shortcuts.
    CreateDirectory "$SMPROGRAMS\${APP_NAME}"
    CreateShortCut "$SMPROGRAMS\${APP_NAME}\${APP_NAME}.lnk" "$INSTDIR\${APP_EXE}"
    CreateShortCut "$SMPROGRAMS\${APP_NAME}\Uninstall.lnk" "$INSTDIR\Uninstall.exe"

    ; Uninstaller.
    WriteUninstaller "$INSTDIR\Uninstall.exe"

    ; Add/Remove Programs registration.
    WriteRegStr HKLM "${UNINST_KEY}" "DisplayName"     "${APP_NAME}"
    WriteRegStr HKLM "${UNINST_KEY}" "DisplayVersion"  "${APP_VERSION}"
    WriteRegStr HKLM "${UNINST_KEY}" "Publisher"       "${APP_PUBLISHER}"
    WriteRegStr HKLM "${UNINST_KEY}" "URLInfoAbout"    "${APP_URL}"
    WriteRegStr HKLM "${UNINST_KEY}" "DisplayIcon"     "$INSTDIR\${APP_EXE}"
    WriteRegStr HKLM "${UNINST_KEY}" "UninstallString" "$INSTDIR\Uninstall.exe"
    WriteRegDWORD HKLM "${UNINST_KEY}" "NoModify" 1
    WriteRegDWORD HKLM "${UNINST_KEY}" "NoRepair" 1

    WriteRegStr HKLM "Software\${APP_NAME}" "InstallDir" "$INSTDIR"
SectionEnd

Section "Uninstall"
    Delete "$INSTDIR\${APP_EXE}"
    Delete "$INSTDIR\ffmpeg_sidecar.exe"
    Delete "$INSTDIR\*.dll"
    RMDir /r "$INSTDIR\resources"
    Delete "$INSTDIR\Uninstall.exe"
    RMDir "$INSTDIR"

    Delete "$SMPROGRAMS\${APP_NAME}\${APP_NAME}.lnk"
    Delete "$SMPROGRAMS\${APP_NAME}\Uninstall.lnk"
    RMDir "$SMPROGRAMS\${APP_NAME}"

    DeleteRegKey HKLM "${UNINST_KEY}"
    DeleteRegKey HKLM "Software\${APP_NAME}"
SectionEnd
