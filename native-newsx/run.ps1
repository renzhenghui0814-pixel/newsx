param(
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$AppArgs
)

$ErrorActionPreference = 'Stop'
$sourceDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$buildDir = Join-Path $sourceDir 'build'
$runtimeDir = Join-Path $buildDir 'app'
$cachePath = Join-Path $buildDir 'CMakeCache.txt'
if (Test-Path -LiteralPath $cachePath) {
    $runtimeEntry = Get-Content -LiteralPath $cachePath |
        Where-Object { $_ -match '^NEWSX_RUNTIME_OUTPUT_DIR:(PATH|STRING)=' } |
        Select-Object -First 1
    if ($runtimeEntry) {
        $configuredDir = ($runtimeEntry -split '=', 2)[1].Trim()
        if (-not [string]::IsNullOrWhiteSpace($configuredDir)) {
            $runtimeDir = if ([IO.Path]::IsPathRooted($configuredDir)) {
                $configuredDir
            } else {
                Join-Path $buildDir $configuredDir
            }
        }
    }
}
$app = Get-Item -LiteralPath (Join-Path $runtimeDir 'NewsXNative.exe') -ErrorAction SilentlyContinue
if ($null -eq $app) {
    [Console]::Error.WriteLine("NewsXNative.exe was not found under `"$runtimeDir`". Run build.cmd first.")
    exit 1
}

$qtDir = Join-Path $sourceDir 'qt\6.8.3\msvc2022_64'
$env:PATH = (Join-Path $qtDir 'bin') + ';' + $env:PATH
Write-Host "Launching `"$($app.FullName)`""
$launch = @{
    FilePath = $app.FullName
    WorkingDirectory = $app.DirectoryName
}
if ($AppArgs.Count -gt 0) { $launch.ArgumentList = $AppArgs }
try {
    $launchStartedUtc = [DateTime]::UtcNow
    $launchedApp = Start-Process @launch -PassThru -ErrorAction Stop
    # Start-Process succeeding only confirms process creation, not startup.
    # Keep run.cmd open on an early native crash or QML initialization failure.
    if ($launchedApp.WaitForExit(2500)) {
        $launchedApp.Refresh()
        if ($launchedApp.ExitCode -ne 0) {
            $exitHex = '{0:X8}' -f $launchedApp.ExitCode
            $startupLog = Join-Path $env:APPDATA 'NewsXWidget\startup-error.log'
            $startupLogFile = Get-Item -LiteralPath $startupLog -ErrorAction SilentlyContinue
            if ($startupLogFile -and $startupLogFile.LastWriteTimeUtc -ge $launchStartedUtc.AddSeconds(-1)) {
                [Console]::Error.WriteLine("Startup details ($startupLog):")
                [Console]::Error.WriteLine((Get-Content -LiteralPath $startupLog -Raw -Encoding UTF8))
            }
            throw "NewsX exited during startup (exit code 0x$exitHex). Executable: $($app.FullName)"
        }
    }
} catch {
    [Console]::Error.WriteLine("Unable to launch NewsX: $($_.Exception.Message)")
    exit 1
}
