$ErrorActionPreference = 'Stop'
$sourceDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$workspaceDir = Split-Path -Parent $sourceDir
$buildDir = Join-Path $sourceDir 'build\app'
$buildExe = Join-Path $buildDir 'NewsXNative.exe'
$qtDir = Join-Path $sourceDir 'qt\6.8.3\msvc2022_64'
$deploy = Join-Path $qtDir 'bin\windeployqt.exe'
$toolsDir = Join-Path $sourceDir '.tools'
$nsisDir = Join-Path $toolsDir 'nsis-3.13'
$makensis = Join-Path $nsisDir 'Bin\makensis.exe'
$distDir = Join-Path $workspaceDir 'dist'
$stageDir = Join-Path $distDir 'newsx-nsis-stage'
$installer = Join-Path $distDir 'NewsX-NSIS-Setup.exe'

if (-not (Test-Path -LiteralPath $buildExe)) { throw 'Build native-newsx/build/app/NewsXNative.exe first.' }
if (-not (Test-Path -LiteralPath (Join-Path $buildDir 'xmdapi.dll'))) { throw 'The TORA runtime xmdapi.dll was not found.' }
if (-not (Test-Path -LiteralPath $deploy)) { throw 'Qt windeployqt.exe was not found.' }

if (-not (Test-Path -LiteralPath $makensis)) {
    New-Item -ItemType Directory -Path $toolsDir -Force | Out-Null
    $zip = Join-Path $toolsDir 'nsis-3.13.zip'
    if (-not (Test-Path -LiteralPath $zip)) {
        $url = 'https://github.com/tauri-apps/binary-releases/releases/download/nsis-3.13/nsis-3.13.zip'
        Invoke-WebRequest -Uri $url -OutFile $zip -ErrorAction Stop
    }
    $expectedHash = 'DB14F8AF2EA346B786C6AE19343F61AAF5E09B8E'
    $actualHash = (Get-FileHash -LiteralPath $zip -Algorithm SHA1).Hash
    if ($actualHash -ne $expectedHash) { throw "NSIS archive hash mismatch: $actualHash" }
    Expand-Archive -LiteralPath $zip -DestinationPath $toolsDir -Force
    if (-not (Test-Path -LiteralPath $makensis)) { throw 'NSIS makensis.exe was not found in the archive.' }
}

New-Item -ItemType Directory -Path $distDir -Force | Out-Null
$resolvedDist = (Resolve-Path -LiteralPath $distDir).Path
if (-not $stageDir.StartsWith($resolvedDist + '\', [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Unexpected staging path.'
}
if (Test-Path -LiteralPath $stageDir) { Remove-Item -LiteralPath $stageDir -Recurse -Force }
New-Item -ItemType Directory -Path $stageDir -Force | Out-Null

try {
    Copy-Item -LiteralPath $buildExe, (Join-Path $buildDir 'xmdapi.dll') -Destination $stageDir
    $env:PATH = (Join-Path $qtDir 'bin') + ';' + $env:PATH
    & $deploy --release --compiler-runtime --no-translations --qmldir (Join-Path $sourceDir 'qml') --dir $stageDir (Join-Path $stageDir 'NewsXNative.exe')
    if ($LASTEXITCODE -ne 0) { throw "windeployqt failed with exit code $LASTEXITCODE." }

    $icon = Join-Path $sourceDir 'resources\newsx.ico'
    $script = Join-Path $sourceDir 'installer\NewsX.nsi'
    & $makensis /V2 "/DPAYLOAD_DIR=$stageDir" "/DOUTPUT_FILE=$installer" "/DICON_FILE=$icon" $script
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $installer)) {
        throw "NSIS compilation failed with exit code $LASTEXITCODE."
    }
    $oldInstaller = Join-Path $distDir 'NewsX-Setup.exe'
    if (Test-Path -LiteralPath $oldInstaller) { Remove-Item -LiteralPath $oldInstaller -Force }
    Get-Item -LiteralPath $installer | Select-Object FullName, Length
} finally {
    if (Test-Path -LiteralPath $stageDir) { Remove-Item -LiteralPath $stageDir -Recurse -Force }
}
