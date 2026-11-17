$ErrorActionPreference = 'Stop'
if (Get-Process -Name NewsXNative -ErrorAction SilentlyContinue) {
    throw 'Please exit NewsX from the system tray before uninstalling.'
}

$installDir = Join-Path $env:LOCALAPPDATA 'Programs\NewsX'
$shortcutPath = Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs\NewsX.lnk'
$uninstallKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\NewsX'
Remove-Item -LiteralPath $shortcutPath -Force -ErrorAction SilentlyContinue
Remove-Item -Path $uninstallKey -Recurse -Force -ErrorAction SilentlyContinue
if (Test-Path -LiteralPath $installDir) { Remove-Item -LiteralPath $installDir -Recurse -Force }
Write-Host 'NewsX uninstalled.'
