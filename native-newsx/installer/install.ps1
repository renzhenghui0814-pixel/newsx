param([Parameter(Mandatory = $true)][string]$Payload)
$ErrorActionPreference = 'Stop'

if (Get-Process -Name NewsXNative -ErrorAction SilentlyContinue) {
    throw 'Please exit NewsX from the system tray before installing.'
}

$installDir = Join-Path $env:LOCALAPPDATA 'Programs\NewsX'
$unpackDir = Join-Path $env:TEMP ('NewsX-Install-' + [guid]::NewGuid().ToString('N'))
try {
    Expand-Archive -LiteralPath $Payload -DestinationPath $unpackDir -Force
    if (-not (Test-Path -LiteralPath (Join-Path $unpackDir 'NewsXNative.exe'))) {
        throw 'The installation package is missing NewsXNative.exe.'
    }
    New-Item -ItemType Directory -Path $installDir -Force | Out-Null
    Copy-Item -Path (Join-Path $unpackDir '*') -Destination $installDir -Recurse -Force

    $shortcutPath = Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs\NewsX.lnk'
    $shell = New-Object -ComObject WScript.Shell
    $shortcut = $shell.CreateShortcut($shortcutPath)
    $shortcut.TargetPath = Join-Path $installDir 'NewsXNative.exe'
    $shortcut.WorkingDirectory = $installDir
    $shortcut.IconLocation = "$($shortcut.TargetPath),0"
    $shortcut.Save()

    $uninstallKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\NewsX'
    New-Item -Path $uninstallKey -Force | Out-Null
    New-ItemProperty -Path $uninstallKey -Name DisplayName -Value 'NewsX' -PropertyType String -Force | Out-Null
    New-ItemProperty -Path $uninstallKey -Name DisplayVersion -Value '0.1.0' -PropertyType String -Force | Out-Null
    New-ItemProperty -Path $uninstallKey -Name Publisher -Value 'NewsX' -PropertyType String -Force | Out-Null
    New-ItemProperty -Path $uninstallKey -Name InstallLocation -Value $installDir -PropertyType String -Force | Out-Null
    New-ItemProperty -Path $uninstallKey -Name DisplayIcon -Value (Join-Path $installDir 'NewsXNative.exe') -PropertyType String -Force | Out-Null
    $uninstallCommand = "powershell.exe -NoProfile -ExecutionPolicy Bypass -File `"$(Join-Path $installDir 'uninstall.ps1')`""
    New-ItemProperty -Path $uninstallKey -Name UninstallString -Value $uninstallCommand -PropertyType String -Force | Out-Null
    New-ItemProperty -Path $uninstallKey -Name NoModify -Value 1 -PropertyType DWord -Force | Out-Null
    New-ItemProperty -Path $uninstallKey -Name NoRepair -Value 1 -PropertyType DWord -Force | Out-Null
    Write-Host "NewsX installed to $installDir"
} finally {
    if (Test-Path -LiteralPath $unpackDir) { Remove-Item -LiteralPath $unpackDir -Recurse -Force }
}
