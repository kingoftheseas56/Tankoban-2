; Tankoban Beta installer (2026-05-06).
;
; Sibling to tankoban.nsi (the proper release installer driven by
; release.yml on tag push). This one is for local beta-share builds:
; - Pulls the full pre-staged Tankoban-Beta/ folder recursively (so
;   platforms/, stream_server/, imageformats/, tls/, etc. all come along
;   — the upstream tankoban.nsi predates the runtime layout audit and
;   only bundles *.dll + resources/, which would crash on first launch
;   from a missing Qt platform plugin).
; - Installs to %LOCALAPPDATA%\Tankoban Beta (no admin needed).
; - Name: "Tankoban Beta" so it sits separately from any prior install.
;
; Usage:
;   makensis -DSTAGE_DIR=C:\Users\Suprabha\Desktop\Tankoban-Beta ^
;            -DOUTPUT_FILE=C:\Users\Suprabha\Desktop\Tankoban-Beta-Setup.exe ^
;            installer\tankoban-beta.nsi

!ifndef STAGE_DIR
    !define STAGE_DIR "C:\Users\Suprabha\Desktop\Tankoban-Beta"
!endif
!ifndef OUTPUT_FILE
    !define OUTPUT_FILE "C:\Users\Suprabha\Desktop\Tankoban-Beta-Setup.exe"
!endif

!define APP_NAME           "Tankoban Beta"
!define APP_VERSION        "0.1.0-beta"
!define APP_PUBLISHER      "Hemanth (kingoftheseas56)"
!define APP_URL            "https://github.com/kingoftheseas56/Tankoban-2"
!define APP_EXE            "Tankoban.exe"
!define UNINST_KEY         "Software\Microsoft\Windows\CurrentVersion\Uninstall\TankobanBeta"

Name "${APP_NAME} ${APP_VERSION}"
OutFile "${OUTPUT_FILE}"
InstallDir "$LOCALAPPDATA\Tankoban Beta"
InstallDirRegKey HKCU "Software\TankobanBeta" "InstallDir"
RequestExecutionLevel user
SetCompressor /SOLID lzma

!include "MUI2.nsh"

!define MUI_ABORTWARNING

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "..\LICENSE"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!define MUI_FINISHPAGE_RUN "$INSTDIR\${APP_EXE}"
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

Section "Install"
    SetOutPath "$INSTDIR"

    ; Recursive bundle of the staged Tankoban-Beta/ folder. Pulls
    ; Tankoban.exe + ffmpeg_sidecar.exe + all DLLs + Qt plugin folders
    ; (platforms/, imageformats/, tls/, etc.) + stream_server/ + every
    ; runtime asset, in one go.
    File /r "${STAGE_DIR}\*"

    ; Start Menu + Desktop shortcuts.
    CreateDirectory "$SMPROGRAMS\${APP_NAME}"
    CreateShortCut "$SMPROGRAMS\${APP_NAME}\${APP_NAME}.lnk" "$INSTDIR\${APP_EXE}"
    CreateShortCut "$SMPROGRAMS\${APP_NAME}\Uninstall.lnk" "$INSTDIR\Uninstall.exe"
    CreateShortCut "$DESKTOP\${APP_NAME}.lnk" "$INSTDIR\${APP_EXE}"

    ; Uninstaller.
    WriteUninstaller "$INSTDIR\Uninstall.exe"

    ; Add/Remove Programs registration (per-user, HKCU not HKLM).
    WriteRegStr HKCU "${UNINST_KEY}" "DisplayName"     "${APP_NAME}"
    WriteRegStr HKCU "${UNINST_KEY}" "DisplayVersion"  "${APP_VERSION}"
    WriteRegStr HKCU "${UNINST_KEY}" "Publisher"       "${APP_PUBLISHER}"
    WriteRegStr HKCU "${UNINST_KEY}" "URLInfoAbout"    "${APP_URL}"
    WriteRegStr HKCU "${UNINST_KEY}" "DisplayIcon"     "$INSTDIR\${APP_EXE}"
    WriteRegStr HKCU "${UNINST_KEY}" "UninstallString" "$INSTDIR\Uninstall.exe"
    WriteRegDWORD HKCU "${UNINST_KEY}" "NoModify" 1
    WriteRegDWORD HKCU "${UNINST_KEY}" "NoRepair" 1

    WriteRegStr HKCU "Software\TankobanBeta" "InstallDir" "$INSTDIR"
SectionEnd

Section "Uninstall"
    ; Recursive removal — beta sprawl + many subfolders make per-file
    ; deletion brittle, and the install root is a dedicated dir.
    RMDir /r "$INSTDIR"

    Delete "$SMPROGRAMS\${APP_NAME}\${APP_NAME}.lnk"
    Delete "$SMPROGRAMS\${APP_NAME}\Uninstall.lnk"
    RMDir "$SMPROGRAMS\${APP_NAME}"
    Delete "$DESKTOP\${APP_NAME}.lnk"

    DeleteRegKey HKCU "${UNINST_KEY}"
    DeleteRegKey HKCU "Software\TankobanBeta"
SectionEnd
