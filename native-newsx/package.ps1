$ErrorActionPreference = 'Stop'
$sourceDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$workspaceDir = Split-Path -Parent $sourceDir
$buildExe = Join-Path $sourceDir 'build\app\NewsXNative.exe'
$qtDir = Join-Path $sourceDir 'qt\6.8.3\msvc2022_64'
$deploy = Join-Path $qtDir 'bin\windeployqt.exe'
$distDir = Join-Path $workspaceDir 'dist'
$stageDir = Join-Path $distDir 'newsx-installer-stage'
$installer = Join-Path $distDir 'NewsX-Setup.exe'

if (-not (Test-Path -LiteralPath $buildExe)) { throw 'Build native-newsx/build/app/NewsXNative.exe first.' }
if (-not (Test-Path -LiteralPath $deploy)) { throw 'Qt windeployqt.exe was not found.' }
if (-not (Test-Path -LiteralPath (Join-Path $sourceDir 'build\app\xmdapi.dll'))) {
    throw 'The TORA runtime xmdapi.dll was not found.'
}
New-Item -ItemType Directory -Path $distDir -Force | Out-Null
$resolvedDist = (Resolve-Path -LiteralPath $distDir).Path
if (-not $stageDir.StartsWith($resolvedDist + '\', [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Unexpected staging path.'
}
if (Test-Path -LiteralPath $stageDir) { Remove-Item -LiteralPath $stageDir -Recurse -Force }
New-Item -ItemType Directory -Path $stageDir -Force | Out-Null

try {
    $runtimeDir = Join-Path $stageDir 'runtime'
    $sourceFiles = Join-Path $stageDir 'source'
    New-Item -ItemType Directory -Path $runtimeDir, $sourceFiles -Force | Out-Null
    Copy-Item -LiteralPath $buildExe, (Join-Path $sourceDir 'build\app\xmdapi.dll') -Destination $runtimeDir
    Copy-Item -LiteralPath (Join-Path $sourceDir 'installer\uninstall.ps1') -Destination $runtimeDir

    $env:PATH = (Join-Path $qtDir 'bin') + ';' + $env:PATH
    & $deploy --release --compiler-runtime --no-translations --qmldir (Join-Path $sourceDir 'qml') --dir $runtimeDir (Join-Path $runtimeDir 'NewsXNative.exe')
    if ($LASTEXITCODE -ne 0) { throw "windeployqt failed with exit code $LASTEXITCODE." }

    Compress-Archive -Path (Join-Path $runtimeDir '*') -DestinationPath (Join-Path $sourceFiles 'payload.zip') -CompressionLevel Optimal
    Copy-Item -LiteralPath (Join-Path $sourceDir 'installer\setup.cmd'), (Join-Path $sourceDir 'installer\install.ps1') -Destination $sourceFiles

    $sedPath = Join-Path $stageDir 'newsx.sed'
    $sed = @"
[Version]
Class=IEXPRESS
SEDVersion=3
[Options]
PackagePurpose=InstallApp
ShowInstallProgramWindow=0
HideExtractAnimation=1
UseLongFileName=1
InsideCompressed=0
CAB_FixedSize=0
CAB_ResvCodeSigning=0
RebootMode=N
InstallPrompt=%InstallPrompt%
DisplayLicense=%DisplayLicense%
FinishMessage=%FinishMessage%
TargetName=%TargetName%
FriendlyName=%FriendlyName%
AppLaunched=%AppLaunched%
PostInstallCmd=%PostInstallCmd%
AdminQuietInstCmd=%AdminQuietInstCmd%
UserQuietInstCmd=%UserQuietInstCmd%
SourceFiles=SourceFiles
[Strings]
InstallPrompt=
DisplayLicense=
FinishMessage=
TargetName="$installer"
FriendlyName=NewsX Setup
AppLaunched=cmd.exe /c setup.cmd
PostInstallCmd=<None>
AdminQuietInstCmd=
UserQuietInstCmd=
FILE0="setup.cmd"
FILE1="install.ps1"
FILE2="payload.zip"
[SourceFiles]
SourceFiles0=$sourceFiles\
[SourceFiles0]
%FILE0%=
%FILE1%=
%FILE2%=
"@
    Set-Content -LiteralPath $sedPath -Value $sed -Encoding ASCII
    if (Test-Path -LiteralPath $installer) { Remove-Item -LiteralPath $installer -Force }
    & "$env:WINDIR\System32\iexpress.exe" /N /Q /M $sedPath
    if ($LASTEXITCODE -ne 0) { throw "IExpress failed with exit code $LASTEXITCODE." }
    $lastSize = -1
    $stableCount = 0
    for ($attempt = 0; $attempt -lt 120; $attempt++) {
        Start-Sleep -Seconds 1
        $output = Get-Item -LiteralPath $installer -ErrorAction SilentlyContinue
        if ($null -ne $output -and $output.Length -gt 1MB) {
            if ($output.Length -eq $lastSize) { $stableCount++ } else { $stableCount = 0 }
            $lastSize = $output.Length
            if ($stableCount -ge 3) { break }
        }
    }
    if ($stableCount -lt 3) { throw 'IExpress did not finish creating the installer.' }
    Get-Item -LiteralPath $installer | Select-Object FullName, Length
} finally {
    if (Test-Path -LiteralPath $stageDir) { Remove-Item -LiteralPath $stageDir -Recurse -Force }
}
