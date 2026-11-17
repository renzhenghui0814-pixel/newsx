param([Parameter(Mandatory = $true)][string]$Payload)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
[System.Windows.Forms.Application]::EnableVisualStyles()

function Show-InstallError([string]$message) {
    [System.Windows.Forms.MessageBox]::Show($message, 'NewsX 安装失败',
        [System.Windows.Forms.MessageBoxButtons]::OK,
        [System.Windows.Forms.MessageBoxIcon]::Error) | Out-Null
}

if (Get-Process -Name NewsXNative -ErrorAction SilentlyContinue) {
    Show-InstallError '请先从系统托盘退出正在运行的 NewsX，然后重试安装。'
    exit 1
}

$uninstallKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\NewsX'
$oldInstallDir = (Get-ItemProperty -Path $uninstallKey -ErrorAction SilentlyContinue).InstallLocation
$defaultInstallDir = Join-Path $env:LOCALAPPDATA 'Programs\NewsX'
$initialInstallDir = if ($oldInstallDir) { $oldInstallDir } else { $defaultInstallDir }

$form = New-Object System.Windows.Forms.Form
$form.Text = '安装 NewsX'
$form.StartPosition = 'CenterScreen'
$form.ClientSize = New-Object System.Drawing.Size(580, 235)
$form.FormBorderStyle = 'FixedDialog'
$form.MaximizeBox = $false
$form.MinimizeBox = $false
$form.Font = New-Object System.Drawing.Font('Microsoft YaHei UI', 9)

$title = New-Object System.Windows.Forms.Label
$title.Text = '选择 NewsX 安装位置'
$title.Font = New-Object System.Drawing.Font('Microsoft YaHei UI', 13, [System.Drawing.FontStyle]::Bold)
$title.Location = New-Object System.Drawing.Point(20, 18)
$title.AutoSize = $true
$form.Controls.Add($title)
$hint = New-Object System.Windows.Forms.Label
$hint.Text = '默认安装到当前用户目录，无需管理员权限。'
$hint.Location = New-Object System.Drawing.Point(22, 53)
$hint.AutoSize = $true
$form.Controls.Add($hint)

$pathBox = New-Object System.Windows.Forms.TextBox
$pathBox.Text = $initialInstallDir
$pathBox.Location = New-Object System.Drawing.Point(22, 88)
$pathBox.Size = New-Object System.Drawing.Size(440, 25)
$form.Controls.Add($pathBox)
$browseButton = New-Object System.Windows.Forms.Button
$browseButton.Text = '浏览…'
$browseButton.Location = New-Object System.Drawing.Point(472, 85)
$browseButton.Size = New-Object System.Drawing.Size(85, 30)
$browseButton.Add_Click({
    $picker = New-Object System.Windows.Forms.FolderBrowserDialog
    $picker.Description = '选择上级文件夹，安装程序会在其中创建 NewsX 文件夹'
    $currentParent = Split-Path -Parent $pathBox.Text
    if (Test-Path -LiteralPath $currentParent) { $picker.SelectedPath = $currentParent }
    if ($picker.ShowDialog($form) -eq [System.Windows.Forms.DialogResult]::OK) {
        $pathBox.Text = Join-Path $picker.SelectedPath 'NewsX'
    }
    $picker.Dispose()
})
$form.Controls.Add($browseButton)

$desktopCheck = New-Object System.Windows.Forms.CheckBox
$desktopCheck.Text = '创建桌面快捷方式（始终创建开始菜单快捷方式）'
$desktopCheck.Checked = $true
$desktopCheck.AutoSize = $true
$desktopCheck.Location = New-Object System.Drawing.Point(22, 130)
$form.Controls.Add($desktopCheck)
$runCheck = New-Object System.Windows.Forms.CheckBox
$runCheck.Text = '安装完成后运行 NewsX'
$runCheck.Checked = $true
$runCheck.AutoSize = $true
$runCheck.Location = New-Object System.Drawing.Point(22, 158)
$form.Controls.Add($runCheck)

$installButton = New-Object System.Windows.Forms.Button
$installButton.Text = '安装'
$installButton.Location = New-Object System.Drawing.Point(378, 191)
$installButton.Size = New-Object System.Drawing.Size(85, 30)
$installButton.Add_Click({
    try {
        $candidate = [System.IO.Path]::GetFullPath($pathBox.Text.Trim())
        $root = [System.IO.Path]::GetPathRoot($candidate)
        if ($candidate.TrimEnd('\') -eq $root.TrimEnd('\')) { throw '不能直接安装到磁盘根目录。' }
        if ($oldInstallDir -and $candidate.StartsWith(
            $oldInstallDir.TrimEnd('\') + '\', [System.StringComparison]::OrdinalIgnoreCase)) {
            throw '新安装目录不能位于旧安装目录内。'
        }
        $existingFiles = @(Get-ChildItem -LiteralPath $candidate -Force -ErrorAction SilentlyContinue)
        $knownInstall = $oldInstallDir -and
            $candidate.Equals($oldInstallDir, [System.StringComparison]::OrdinalIgnoreCase)
        $managedInstall = Test-Path -LiteralPath (Join-Path $candidate '.newsx-install')
        if ($existingFiles.Count -gt 0 -and -not $knownInstall -and -not $managedInstall) {
            throw '目标文件夹已有其他文件，请选择空文件夹或现有的 NewsX 安装位置。'
        }
        $script:selectedInstallDir = $candidate
        $script:createDesktopShortcut = $desktopCheck.Checked
        $script:runAfterInstall = $runCheck.Checked
        $form.DialogResult = [System.Windows.Forms.DialogResult]::OK
        $form.Close()
    } catch { Show-InstallError $_.Exception.Message }
})
$form.Controls.Add($installButton)
$cancelButton = New-Object System.Windows.Forms.Button
$cancelButton.Text = '取消'
$cancelButton.Location = New-Object System.Drawing.Point(472, 191)
$cancelButton.Size = New-Object System.Drawing.Size(85, 30)
$cancelButton.DialogResult = [System.Windows.Forms.DialogResult]::Cancel
$form.Controls.Add($cancelButton)
$form.AcceptButton = $installButton
$form.CancelButton = $cancelButton
if ($form.ShowDialog() -ne [System.Windows.Forms.DialogResult]::OK) { exit 0 }
$form.Dispose()

$progress = New-Object System.Windows.Forms.Form
$progress.Text = '正在安装 NewsX'
$progress.StartPosition = 'CenterScreen'
$progress.ClientSize = New-Object System.Drawing.Size(410, 90)
$progress.FormBorderStyle = 'FixedDialog'
$progress.ControlBox = $false
$progress.TopMost = $true
$progressLabel = New-Object System.Windows.Forms.Label
$progressLabel.Text = '正在复制程序文件，请稍候…'
$progressLabel.Location = New-Object System.Drawing.Point(20, 15)
$progressLabel.AutoSize = $true
$progress.Controls.Add($progressLabel)
$progressBar = New-Object System.Windows.Forms.ProgressBar
$progressBar.Style = 'Marquee'
$progressBar.Location = New-Object System.Drawing.Point(20, 45)
$progressBar.Size = New-Object System.Drawing.Size(370, 20)
$progress.Controls.Add($progressBar)
$progress.Show()
$progress.Refresh()

$unpackDir = Join-Path $env:TEMP ('NewsX-Install-' + [guid]::NewGuid().ToString('N'))
try {
    Expand-Archive -LiteralPath $Payload -DestinationPath $unpackDir -Force
    if (-not (Test-Path -LiteralPath (Join-Path $unpackDir 'NewsXNative.exe'))) {
        throw '安装包缺少 NewsXNative.exe。'
    }
    New-Item -ItemType Directory -Path $selectedInstallDir -Force | Out-Null
    Copy-Item -Path (Join-Path $unpackDir '*') -Destination $selectedInstallDir -Recurse -Force
    Set-Content -LiteralPath (Join-Path $selectedInstallDir '.newsx-install') -Value 'NewsX' -Encoding ASCII
    $installedExe = Join-Path $selectedInstallDir 'NewsXNative.exe'
    $shell = New-Object -ComObject WScript.Shell
    $startShortcut = Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs\NewsX.lnk'
    $shortcut = $shell.CreateShortcut($startShortcut)
    $shortcut.TargetPath = $installedExe
    $shortcut.WorkingDirectory = $selectedInstallDir
    $shortcut.IconLocation = "$installedExe,0"
    $shortcut.Save()
    $desktopShortcut = Join-Path ([System.Environment]::GetFolderPath('DesktopDirectory')) 'NewsX.lnk'
    if ($createDesktopShortcut) {
        $shortcut = $shell.CreateShortcut($desktopShortcut)
        $shortcut.TargetPath = $installedExe
        $shortcut.WorkingDirectory = $selectedInstallDir
        $shortcut.IconLocation = "$installedExe,0"
        $shortcut.Save()
    } elseif (Test-Path -LiteralPath $desktopShortcut) {
        $existingShortcut = $shell.CreateShortcut($desktopShortcut)
        $oldExe = if ($oldInstallDir) { Join-Path $oldInstallDir 'NewsXNative.exe' } else { '' }
        if ($existingShortcut.TargetPath -eq $installedExe -or
            ($oldExe -and $existingShortcut.TargetPath -eq $oldExe)) {
            Remove-Item -LiteralPath $desktopShortcut -Force
        }
    }

    New-Item -Path $uninstallKey -Force | Out-Null
    New-ItemProperty -Path $uninstallKey -Name DisplayName -Value 'NewsX' -PropertyType String -Force | Out-Null
    New-ItemProperty -Path $uninstallKey -Name DisplayVersion -Value '0.1.0' -PropertyType String -Force | Out-Null
    New-ItemProperty -Path $uninstallKey -Name Publisher -Value 'NewsX' -PropertyType String -Force | Out-Null
    New-ItemProperty -Path $uninstallKey -Name InstallLocation -Value $selectedInstallDir -PropertyType String -Force | Out-Null
    New-ItemProperty -Path $uninstallKey -Name DisplayIcon -Value $installedExe -PropertyType String -Force | Out-Null
    $uninstallCommand = "powershell.exe -NoProfile -ExecutionPolicy Bypass -File `"$(Join-Path $selectedInstallDir 'uninstall.ps1')`""
    New-ItemProperty -Path $uninstallKey -Name UninstallString -Value $uninstallCommand -PropertyType String -Force | Out-Null
    New-ItemProperty -Path $uninstallKey -Name NoModify -Value 1 -PropertyType DWord -Force | Out-Null
    New-ItemProperty -Path $uninstallKey -Name NoRepair -Value 1 -PropertyType DWord -Force | Out-Null

    if ($oldInstallDir -and
        -not $oldInstallDir.Equals($selectedInstallDir, [System.StringComparison]::OrdinalIgnoreCase) -and
        (Test-Path -LiteralPath $oldInstallDir) -and
        ($oldInstallDir.Equals($defaultInstallDir, [System.StringComparison]::OrdinalIgnoreCase) -or
            (Test-Path -LiteralPath (Join-Path $oldInstallDir '.newsx-install')))) {
        Remove-Item -LiteralPath $oldInstallDir -Recurse -Force
    }
    $progress.Close()
    $shortcutDescription = if ($createDesktopShortcut) { "`n桌面快捷方式：$desktopShortcut" } else { '' }
    [System.Windows.Forms.MessageBox]::Show(
        "NewsX 已安装到：`n$selectedInstallDir`n`n开始菜单：$startShortcut$shortcutDescription",
        'NewsX 安装完成', [System.Windows.Forms.MessageBoxButtons]::OK,
        [System.Windows.Forms.MessageBoxIcon]::Information) | Out-Null
    if ($runAfterInstall) {
        try { Start-Process -FilePath $installedExe -WorkingDirectory $selectedInstallDir }
        catch { Show-InstallError "安装已完成，但启动 NewsX 失败：$($_.Exception.Message)" }
    }
} catch {
    $progress.Close()
    Show-InstallError $_.Exception.Message
    exit 1
} finally {
    if (Test-Path -LiteralPath $unpackDir) { Remove-Item -LiteralPath $unpackDir -Recurse -Force }
    $progress.Dispose()
}
