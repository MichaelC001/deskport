; DeskPort Windows x64 offline installer.
; Built with NSIS on Linux; every payload file is embedded in the installer.

; Export the exact generated uninstaller for offline PE auditing.
!ifdef AUDIT_UNINSTALLER
  !uninstfinalize 'cp "%1" "${AUDIT_UNINSTALLER}"' = 0
!endif

Unicode true
SetCompressor /SOLID lzma

!ifndef VERSION
  !define VERSION "0.0.0"
!endif
!ifndef PAYLOAD
  !error "PAYLOAD must be defined"
!endif
!ifndef OUTFILE
  !error "OUTFILE must be defined"
!endif

!define APPNAME "DeskPort"
!define PUBLISHER "DeskPort contributors"
!define REGKEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}"

Name "${APPNAME} ${VERSION} (x64)"
OutFile "${OUTFILE}"
InstallDir "$PROGRAMFILES64\${APPNAME}"
InstallDirRegKey HKLM "Software\${APPNAME}" "InstallDir"
RequestExecutionLevel admin
ShowInstDetails show
ShowUninstDetails show
BrandingText "${APPNAME} ${VERSION} x64"

!include "MUI2.nsh"
!include "x64.nsh"
!include "LogicLib.nsh"

!define MUI_ABORTWARNING
!define MUI_ICON "${PAYLOAD}\deskport.ico"
!define MUI_UNICON "${PAYLOAD}\deskport.ico"

!insertmacro MUI_PAGE_LICENSE "${PAYLOAD}\LICENSE.txt"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!define MUI_FINISHPAGE_RUN "$INSTDIR\DeskPort.exe"
!define MUI_FINISHPAGE_RUN_NOTCHECKED
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"
!insertmacro MUI_LANGUAGE "SimpChinese"

Function .onInit
  ${IfNot} ${RunningX64}
    MessageBox MB_ICONSTOP "This package requires 64-bit Windows (x64)."
    Abort
  ${EndIf}
  SetShellVarContext all
  SetRegView 64
FunctionEnd

Function un.onInit
  SetShellVarContext all
  SetRegView 64
FunctionEnd

Section "DeskPort" SecMain
  SectionIn RO
  SetOutPath "$INSTDIR"
  File /r "${PAYLOAD}\*.*"

  WriteRegStr HKLM "Software\${APPNAME}" "InstallDir" "$INSTDIR"
  WriteRegStr HKLM "Software\${APPNAME}" "Version" "${VERSION}"

  WriteUninstaller "$INSTDIR\Uninstall.exe"

  WriteRegStr HKLM "${REGKEY}" "DisplayName" "${APPNAME} ${VERSION} (x64)"
  WriteRegStr HKLM "${REGKEY}" "DisplayVersion" "${VERSION}"
  WriteRegStr HKLM "${REGKEY}" "Publisher" "${PUBLISHER}"
  WriteRegStr HKLM "${REGKEY}" "DisplayIcon" "$INSTDIR\DeskPort.exe"
  WriteRegStr HKLM "${REGKEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKLM "${REGKEY}" "UninstallString" "$\"$INSTDIR\Uninstall.exe$\""
  WriteRegStr HKLM "${REGKEY}" "QuietUninstallString" "$\"$INSTDIR\Uninstall.exe$\" /S"
  WriteRegDWORD HKLM "${REGKEY}" "NoModify" 1
  WriteRegDWORD HKLM "${REGKEY}" "NoRepair" 1

  CreateDirectory "$SMPROGRAMS\${APPNAME}"
  CreateShortCut "$SMPROGRAMS\${APPNAME}\${APPNAME}.lnk" "$INSTDIR\DeskPort.exe"
  CreateShortCut "$SMPROGRAMS\${APPNAME}\Uninstall ${APPNAME}.lnk" "$INSTDIR\Uninstall.exe"
SectionEnd

Section "Desktop shortcut" SecDesktop
  CreateShortCut "$DESKTOP\${APPNAME}.lnk" "$INSTDIR\DeskPort.exe"
SectionEnd

Section "Uninstall"
  Delete "$DESKTOP\${APPNAME}.lnk"
  Delete "$SMPROGRAMS\${APPNAME}\${APPNAME}.lnk"
  Delete "$SMPROGRAMS\${APPNAME}\Uninstall ${APPNAME}.lnk"
  RMDir "$SMPROGRAMS\${APPNAME}"

  Delete "$INSTDIR\Uninstall.exe"
  Delete "$INSTDIR\DeskPort.exe"
  Delete "$INSTDIR\gamecontrollerdb.txt"
  Delete "$INSTDIR\deskport.ico"
  Delete "$INSTDIR\LICENSE.txt"
  Delete "$INSTDIR\BUILD-INFO.txt"
  Delete "$INSTDIR\THIRD-PARTY-NOTICES.txt"
  RMDir /r "$INSTDIR\licenses"
  RMDir "$INSTDIR"

  DeleteRegKey HKLM "${REGKEY}"
  DeleteRegKey HKLM "Software\${APPNAME}"
SectionEnd
