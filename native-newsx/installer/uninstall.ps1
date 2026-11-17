$ErrorActionPreference = 'Stop'
if (Get-Process -Name NewsXNative -ErrorAction SilentlyContinue) {
    throw 'Please exit NewsX from the system tray before uninstalling.'
}

$installDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$shortcutPath = Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs\NewsX.lnk'
$desktopShortcut = Join-Path ([System.Environment]::GetFolderPath('DesktopDirectory')) 'NewsX.lnk'
$uninstallKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\NewsX'
if (-not (Test-Path -LiteralPath (Join-Path $installDir '.newsx-install'))) {
    throw 'NewsX installation marker is missing; refusing to remove this directory.'
}
Remove-Item -LiteralPath $shortcutPath -Force -ErrorAction SilentlyContinue
if (Test-Path -LiteralPath $desktopShortcut) {
    $shell = New-Object -ComObject WScript.Shell
    $shortcut = $shell.CreateShortcut($desktopShortcut)
    if ($shortcut.TargetPath -eq (Join-Path $installDir 'NewsXNative.exe')) {
        Remove-Item -LiteralPath $desktopShortcut -Force
    }
}
Remove-Item -Path $uninstallKey -Recurse -Force -ErrorAction SilentlyContinue
if (Test-Path -LiteralPath $installDir) { Remove-Item -LiteralPath $installDir -Recurse -Force }
Write-Host 'NewsX uninstalled.'
