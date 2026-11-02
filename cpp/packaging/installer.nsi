; One-file installer for the C++ Pic Viewer.
;
;   .msys64\usr\bin\bash -lc "bash cpp/packaging/build_installer.sh"
;
; The same install as install.ps1, as a single .exe: the same directory,
; Start Menu name, "Open with" registration and uninstall key, so either can
; replace or remove what the other put there. Per-user throughout -- nothing
; under HKLM or Program Files -- so it needs no administrator prompt.
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

!ifndef VERSION
  !error "VERSION is defined by build_installer.sh"
!endif

!define APPNAME "Pic Viewer (C++)"
!define EXE "picviewer.exe"
!define UNINSTALL_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\PicViewerCpp"
!define APP_KEY "Software\Classes\Applications\${EXE}"

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

!macro SupportedType EXT
  WriteRegStr HKCU "${APP_KEY}\SupportedTypes" "${EXT}" ""
!macroend

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

  CreateShortcut "$SMPROGRAMS\${APPNAME}.lnk" "${FIXED_DIR}\${EXE}" "" "${FIXED_DIR}\${EXE}" 0

  ; "Open with" only: Windows 10 and later do not let an installer make
  ; itself the default for a file type.
  WriteRegStr HKCU "${APP_KEY}" "FriendlyAppName" "${APPNAME}"
  WriteRegStr HKCU "${APP_KEY}\shell\open\command" "" '"${FIXED_DIR}\${EXE}" "%1"'
  !insertmacro SupportedType ".jpg"
  !insertmacro SupportedType ".jpeg"
  !insertmacro SupportedType ".png"
  !insertmacro SupportedType ".gif"
  !insertmacro SupportedType ".bmp"
  !insertmacro SupportedType ".tif"
  !insertmacro SupportedType ".tiff"
  !insertmacro SupportedType ".webp"
  !insertmacro SupportedType ".heic"
  !insertmacro SupportedType ".heif"
  !insertmacro SupportedType ".avif"
  !insertmacro SupportedType ".ico"
  !insertmacro SupportedType ".tga"
  !insertmacro SupportedType ".ppm"
  !insertmacro SupportedType ".cr2"
  !insertmacro SupportedType ".cr3"
  !insertmacro SupportedType ".nef"
  !insertmacro SupportedType ".arw"
  !insertmacro SupportedType ".dng"
  !insertmacro SupportedType ".orf"
  !insertmacro SupportedType ".rw2"
  !insertmacro SupportedType ".raf"
  !insertmacro SupportedType ".pef"
  !insertmacro SupportedType ".srw"

  ${GetSize} "${FIXED_DIR}" "/S=0K" $0 $1 $2
  WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayName" "${APPNAME}"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayVersion" "${VERSION}"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "Publisher" "ZivKao"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayIcon" "${FIXED_DIR}\${EXE}"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "InstallLocation" "${FIXED_DIR}"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "UninstallString" 'powershell -ExecutionPolicy Bypass -File "${FIXED_DIR}\uninstall.ps1" -Uninstall'
  WriteRegStr HKCU "${UNINSTALL_KEY}" "QuietUninstallString" 'powershell -ExecutionPolicy Bypass -File "${FIXED_DIR}\uninstall.ps1" -Uninstall'
  WriteRegDWORD HKCU "${UNINSTALL_KEY}" "NoModify" 1
  WriteRegDWORD HKCU "${UNINSTALL_KEY}" "NoRepair" 1
  WriteRegDWORD HKCU "${UNINSTALL_KEY}" "EstimatedSize" $0

  ; Tell Explorer the "Open with" list changed.
  System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, p 0, p 0)'
SectionEnd

Section /o "Desktop shortcut" SecDesktop
  CreateShortcut "$DESKTOP\${APPNAME}.lnk" "${FIXED_DIR}\${EXE}" "" "${FIXED_DIR}\${EXE}" 0
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
