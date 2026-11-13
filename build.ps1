$ErrorActionPreference = 'Stop'
$sdk = (Resolve-Path -LiteralPath 'vendor\PythonAPI_20260915').Path
$localPackages = Resolve-Path -LiteralPath '.preview-deps' -ErrorAction SilentlyContinue
if ($localPackages) {
  $env:PYTHONPATH = if ($env:PYTHONPATH) { "$($localPackages.Path);$env:PYTHONPATH" } else { $localPackages.Path }
}
New-Item -ItemType Directory -Force '.build' | Out-Null
$mpvExe = (Resolve-Path -LiteralPath 'vendor\mpv\mpv.exe').Path
$mpvVulkan = (Resolve-Path -LiteralPath 'vendor\mpv\vulkan-1.dll').Path
$mpvLicense = (Resolve-Path -LiteralPath 'vendor\mpv\LICENSE.GPL').Path
$mpvCopyright = (Resolve-Path -LiteralPath 'vendor\mpv\Copyright').Path
python -m PyInstaller --noconfirm --clean --onefile --windowed --name NewsX `
  --distpath release-news-weibo-inline-v3 --workpath .build\pyinstaller-weibo-inline --specpath .build\weibo-inline `
  --paths $sdk --paths "$sdk\x64" `
  --add-data "$sdk;vendor\PythonAPI_20260915" `
  --add-binary "$mpvExe;mpv" --add-binary "$mpvVulkan;mpv" `
  --add-data "$mpvLicense;mpv" --add-data "$mpvCopyright;mpv" NewsX.pyw
if ($LASTEXITCODE -ne 0) { throw 'PyInstaller build failed' }
Write-Host 'Built release-news-weibo-inline-v3\NewsX.exe'
