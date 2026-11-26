$ErrorActionPreference = 'Stop'
$sourceDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$workspaceDir = Split-Path -Parent $sourceDir
$buildDir = Join-Path $sourceDir 'build\app'
$cachePath = Join-Path $sourceDir 'build\CMakeCache.txt'
if (Test-Path -LiteralPath $cachePath) {
    $runtimeEntry = Get-Content -LiteralPath $cachePath |
        Where-Object { $_ -match '^NEWSX_RUNTIME_OUTPUT_DIR:(PATH|STRING)=' } |
        Select-Object -First 1
    if ($runtimeEntry) {
        $configuredDir = ($runtimeEntry -split '=', 2)[1].Trim()
        if (-not [string]::IsNullOrWhiteSpace($configuredDir)) {
            $buildDir = if ([IO.Path]::IsPathRooted($configuredDir)) {
                $configuredDir
            } else {
                Join-Path (Join-Path $sourceDir 'build') $configuredDir
            }
        }
    }
}
$buildExe = Join-Path $buildDir 'NewsXNative.exe'
$qtDir = Join-Path $sourceDir 'qt\6.8.3\msvc2022_64'
$deploy = Join-Path $qtDir 'bin\windeployqt.exe'
$toolsDir = Join-Path $sourceDir '.tools'
$nsisDir = Join-Path $toolsDir 'nsis-3.13'
$makensis = Join-Path $nsisDir 'Bin\makensis.exe'
$distDir = Join-Path $workspaceDir 'dist'
$stageDir = Join-Path $distDir 'newsx-nsis-stage'
$installer = Join-Path $distDir 'NewsX-NSIS-Setup.exe'

if (-not (Test-Path -LiteralPath $buildExe)) { throw "NewsXNative.exe was not found under $buildDir. Run build.cmd first." }
if (-not (Test-Path -LiteralPath (Join-Path $buildDir 'xmdapi.dll'))) { throw 'The TORA runtime xmdapi.dll was not found.' }
if (-not (Test-Path -LiteralPath $deploy)) { throw 'Qt windeployqt.exe was not found.' }

# Resolve the redistributable belonging to the actual compiler. Older
# windeployqt releases cannot discover the VS 2026 CRT automatically.
$crtRoot = $env:VCToolsRedistDir
if (Test-Path -LiteralPath $cachePath) {
    $compilerEntry = Get-Content -LiteralPath $cachePath |
        Where-Object { $_ -match '^CMAKE_CXX_COMPILER:FILEPATH=' } |
        Select-Object -First 1
    if ($compilerEntry -match '^CMAKE_CXX_COMPILER:FILEPATH=(.+?)[\\/]VC[\\/]Tools[\\/]') {
        $redistRoot = Join-Path $matches[1] 'VC\Redist\MSVC'
        $redistVersion = Get-ChildItem -LiteralPath $redistRoot -Directory |
            Where-Object { $_.Name -match '^\d+\.\d+\.\d+$' } |
            Sort-Object { [version]$_.Name } -Descending |
            Select-Object -First 1
        if ($redistVersion) { $crtRoot = $redistVersion.FullName }
    }
}
if ([string]::IsNullOrWhiteSpace($crtRoot)) { throw 'MSVC redistributables were not found. Build with the installed Visual Studio C++ tools first.' }
$crtDir = Get-ChildItem -LiteralPath (Join-Path $crtRoot 'x64') -Directory -Filter 'Microsoft.VC*.CRT' |
    Select-Object -First 1
if (-not $crtDir -or -not (Test-Path -LiteralPath (Join-Path $crtDir.FullName 'vcruntime140.dll'))) {
    throw 'The x64 MSVC CRT redistributable directory is incomplete.'
}

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
    Write-Host "Packaging `"$buildExe`""
    Copy-Item -LiteralPath $buildExe, (Join-Path $buildDir 'xmdapi.dll') -Destination $stageDir
    $env:PATH = (Join-Path $qtDir 'bin') + ';' + $env:PATH
    & $deploy --release --no-compiler-runtime --no-translations --qmldir (Join-Path $sourceDir 'qml') --dir $stageDir (Join-Path $stageDir 'NewsXNative.exe')
    if ($LASTEXITCODE -ne 0) { throw "windeployqt failed with exit code $LASTEXITCODE." }
    Get-ChildItem -LiteralPath $crtDir.FullName -File -Filter '*.dll' |
        Copy-Item -Destination $stageDir
    if (-not (Test-Path -LiteralPath (Join-Path $stageDir 'msvcp140.dll')) -or
        -not (Test-Path -LiteralPath (Join-Path $stageDir 'vcruntime140.dll'))) {
        throw 'MSVC runtime deployment failed.'
    }
    Write-Host "MSVC runtime deployed from `"$($crtDir.FullName)`""

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
