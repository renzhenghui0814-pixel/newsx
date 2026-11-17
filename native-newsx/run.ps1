param(
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$AppArgs
)

$sourceDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$buildDir = Join-Path $sourceDir 'build'
$app = Get-Item -LiteralPath (Join-Path $buildDir 'app\NewsXNative.exe') -ErrorAction SilentlyContinue
if ($null -eq $app) {
    [Console]::Error.WriteLine("NewsXNative.exe was not found under `"$buildDir\app`". Run build.cmd first.")
    exit 1
}

Write-Host "Launching `"$($app.FullName)`""
$launch = @{
    FilePath = $app.FullName
    WorkingDirectory = $app.DirectoryName
}
if ($AppArgs.Count -gt 0) { $launch.ArgumentList = $AppArgs }
Start-Process @launch
