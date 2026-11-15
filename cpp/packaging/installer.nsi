; One-file installer for Pic Viewer.
;
;   .msys64\usr\bin\bash -lc "bash cpp/packaging/build_installer.sh"
;
; The same install as install.ps1, as a single .exe. This copies the files;
; everything else -- shortcuts, the registration with Windows, the uninstall
; entry -- is install.ps1's, run from the install directory with -Register,
; so there is one description of an install and the two cannot drift apart.
; Per-user throughout -- nothing under HKLM or Program Files -- so it needs
; no administrator prompt.
;
; Silent use: PicViewerCpp-<version>-setup.exe /S [/DESKTOP]
;
; There is no uninstall.exe. The first build wrote one, and on the development
; machine Avira quarantined it (HEUR/APC.Misc, a reputation heuristic for an
; unsigned executable) the moment it was run: the launch was refused,
; the file disappeared, and so did the Settings > Apps entry pointing at it,
; leaving an install nothing could remove. Uninstalling goes through the same
; uninstall.ps1 that install.ps1 leaves, which that machine has run many times.

Unicode true
ManifestDPIAware true
RequestExecutionLevel user
SetCompressor /SOLID lzma

!include "MUI2.nsh"
!include "FileFunc.nsh"
!include "LogicLib.nsh"
!include "Sections.nsh"

!ifndef VERSION
  !error "VERSION is defined by build_installer.sh"
!endif

!define APPNAME "Pic Viewer"
!define EXE "picviewer.exe"

Name "${APPNAME}"
OutFile "${OUTFILE}"
; Fixed, as install.ps1 has it: the one place it installs to, and the one
; place uninstall.ps1 removes.
InstallDir "$LOCALAPPDATA\Programs\PicViewerCpp"
!define FIXED_DIR "$LOCALAPPDATA\Programs\PicViewerCpp"

VIProductVersion "${VERSION}.0"
VIAddVersionKey "ProductName" "${APPNAME}"
VIAddVersionKey "FileDescription" "${APPNAME} installer"
VIAddVersionKey "FileVersion" "${VERSION}"
VIAddVersionKey "ProductVersion" "${VERSION}"
VIAddVersionKey "LegalCopyright" "ZivKao"

!define MUI_ICON "${ICON}"
!define MUI_FINISHPAGE_RUN "${FIXED_DIR}\${EXE}"
!define MUI_FINISHPAGE_RUN_TEXT "Start ${APPNAME}"

!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_LANGUAGE "English"
!insertmacro MUI_LANGUAGE "TradChinese"

Section "${APPNAME}" SecApp
  SectionIn RO

  ; A copy started from the install directory locks its DLLs. Only that copy
  ; is closed -- a development build running from elsewhere is left alone.
  nsExec::Exec `powershell -NoProfile -Command "Get-Process picviewer -ErrorAction SilentlyContinue | Where-Object { $$_.Path -like '${FIXED_DIR}\*' } | Stop-Process -Force"`
  Pop $0
  Sleep 800

  ; A clean copy, as install.ps1 makes: nothing left over from an older build.
  RMDir /r "${FIXED_DIR}"
  SetOutPath "${FIXED_DIR}"
  File /r "${DIST}\*.*"
  ; install.ps1 doubles as the uninstaller, as it does for its own installs.
  File "/oname=uninstall.ps1" "${UNINSTALLER}"
SectionEnd

; Ticked by /DESKTOP, or on the components page; -Register reads the choice.
Section /o "Desktop shortcut" SecDesktop
SectionEnd

; Hidden, and last, so it runs after the files are in place.
Section "-Register"
  StrCpy $1 ""
  ${If} ${SectionIsSelected} ${SecDesktop}
    StrCpy $1 "-DesktopShortcut"
  ${EndIf}
  nsExec::ExecToLog `powershell -NoProfile -ExecutionPolicy Bypass -File "${FIXED_DIR}\uninstall.ps1" -Register $1`
  Pop $0
  ${If} $0 != 0
    MessageBox MB_ICONSTOP "${APPNAME} was copied but could not be registered with Windows (exit code $0)." /SD IDOK
    SetErrorLevel 2
    Abort
  ${EndIf}
SectionEnd

Function .onInit
  ; /DESKTOP adds the desktop shortcut in a silent install.
  ${GetParameters} $0
  ClearErrors
  ${GetOptions} $0 "/DESKTOP" $1
  ${IfNot} ${Errors}
    SectionSetFlags ${SecDesktop} ${SF_SELECTED}
  ${EndIf}
FunctionEnd
