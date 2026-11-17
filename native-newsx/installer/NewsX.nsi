Unicode true
!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "FileFunc.nsh"
!include "x64.nsh"

!ifndef PAYLOAD_DIR
  !error "PAYLOAD_DIR must point to the deployed application directory"
!endif
!ifndef OUTPUT_FILE
  !error "OUTPUT_FILE must point to the installer executable"
!endif
!ifndef ICON_FILE
  !error "ICON_FILE must point to the NewsX icon"
!endif

Name "NewsX"
OutFile "${OUTPUT_FILE}"
InstallDir "$LOCALAPPDATA\Programs\NewsX"
RequestExecutionLevel user
SetCompressor /SOLID lzma
ShowInstDetails show
ShowUninstDetails show
BrandingText "NewsX"

!define MUI_ICON "${ICON_FILE}"
!define MUI_UNICON "${ICON_FILE}"
!define MUI_ABORTWARNING
!define MUI_FINISHPAGE_RUN "$INSTDIR\NewsXNative.exe"
!define MUI_FINISHPAGE_RUN_TEXT "安装完成后运行 NewsX"
!insertmacro MUI_PAGE_WELCOME
!define MUI_PAGE_CUSTOMFUNCTION_LEAVE CheckInstallDir
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_UNPAGE_FINISH
!insertmacro MUI_LANGUAGE "SimpChinese"

Function .onInit
  ${IfNot} ${RunningX64}
    MessageBox MB_ICONSTOP "NewsX 需要 64 位 Windows。"
    Abort
  ${EndIf}
  SetRegView 64
  SetShellVarContext current
  ReadRegStr $0 HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\NewsX" "InstallLocation"
  ${If} $0 != ""
    StrCpy $INSTDIR $0
  ${EndIf}
FunctionEnd

Function CheckInstallDir
  ${GetRoot} "$INSTDIR" $0
  ${If} "$INSTDIR" == "$0\"
    MessageBox MB_ICONEXCLAMATION "不能直接安装到磁盘根目录。"
    Abort
  ${EndIf}
  IfFileExists "$INSTDIR\*.*" 0 done
  IfFileExists "$INSTDIR\.newsx-install" done
  ReadRegStr $1 HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\NewsX" "InstallLocation"
  ${If} "$INSTDIR" == "$1"
    Goto done
  ${EndIf}
  MessageBox MB_ICONEXCLAMATION "目标文件夹已有其他文件，请选择空文件夹或现有的 NewsX 安装位置。"
  Abort
done:
FunctionEnd

Section "NewsX 主程序" SecMain
  SectionIn RO
  ReadRegDWORD $0 HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\NewsX" "DesktopShortcut"
  ${If} $0 == 1
    Delete "$DESKTOP\NewsX.lnk"
  ${EndIf}
  SetOutPath "$INSTDIR"
  File /r "${PAYLOAD_DIR}\*.*"
  FileOpen $0 "$INSTDIR\.newsx-install" w
  FileWrite $0 "NewsX"
  FileClose $0
  WriteUninstaller "$INSTDIR\Uninstall.exe"

  Delete "$SMPROGRAMS\NewsX.lnk"
  CreateDirectory "$SMPROGRAMS\NewsX"
  CreateShortCut "$SMPROGRAMS\NewsX\NewsX.lnk" "$INSTDIR\NewsXNative.exe"
  CreateShortCut "$SMPROGRAMS\NewsX\卸载 NewsX.lnk" "$INSTDIR\Uninstall.exe"

  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\NewsX" "DisplayName" "NewsX"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\NewsX" "DisplayVersion" "0.1.0"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\NewsX" "Publisher" "NewsX"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\NewsX" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\NewsX" "DisplayIcon" "$INSTDIR\NewsXNative.exe"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\NewsX" "UninstallString" "$\"$INSTDIR\Uninstall.exe$\""
  WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\NewsX" "NoModify" 1
  WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\NewsX" "NoRepair" 1
  WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\NewsX" "DesktopShortcut" 0
SectionEnd

Section "创建桌面快捷方式" SecDesktop
  CreateShortCut "$DESKTOP\NewsX.lnk" "$INSTDIR\NewsXNative.exe"
  WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\NewsX" "DesktopShortcut" 1
SectionEnd

Section "Uninstall"
  IfFileExists "$INSTDIR\.newsx-install" managed
  MessageBox MB_ICONSTOP "未找到 NewsX 安装标记，已取消卸载以保护目录中的文件。"
  Abort
managed:
  ReadRegDWORD $0 HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\NewsX" "DesktopShortcut"
  ${If} $0 == 1
    Delete "$DESKTOP\NewsX.lnk"
  ${EndIf}
  Delete "$SMPROGRAMS\NewsX\NewsX.lnk"
  Delete "$SMPROGRAMS\NewsX\卸载 NewsX.lnk"
  RMDir "$SMPROGRAMS\NewsX"
  DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\NewsX"
  RMDir /r "$INSTDIR"
SectionEnd

Function un.onInit
  SetRegView 64
  SetShellVarContext current
FunctionEnd
