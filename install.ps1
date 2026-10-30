param(
    [Parameter(Mandatory = $true)][string]$LauncherPath,
    [switch]$ReplaceRules
)
$ErrorActionPreference = 'Stop'
$launcher = (Resolve-Path -LiteralPath $LauncherPath).Path
if (-not (Test-Path -LiteralPath (Join-Path $launcher 'FaustLauncher.exe'))) {
    throw 'LauncherPath 应指向包含 FaustLauncher.exe 的文件夹。'
}
$source = Join-Path $PSScriptRoot 'addon\BattleBGMExtend'
$files = @('scr.py', 'battlebgmextend.dll', 'addon_info.json', 'rules.json', 'il2cpp-api-names.txt', 'THIRD_PARTY_LICENSES.txt', 'LICENSE', 'README.md', 'RULES.md')
foreach ($file in $files) {
    if (-not (Test-Path -LiteralPath (Join-Path $source $file))) { throw ('插件缺少文件：' + $file) }
}
$target = Join-Path $launcher 'addons\BattleBGMExtend'
New-Item -ItemType Directory -Force -Path $target | Out-Null
foreach ($file in $files) {
    $destination = Join-Path $target $file
    if (Test-Path -LiteralPath $destination) {
        if ($file -eq 'rules.json' -and -not $ReplaceRules) { continue }
        # Preserve the user's manifest byte-for-byte, including author links,
        # description, settings and formatting. Only create it on first install.
        if ($file -eq 'addon_info.json') { continue }
        if ($file -eq 'rules.json') {
            Copy-Item -LiteralPath $destination -Destination ($destination + '.backup-' + [guid]::NewGuid().ToString('N'))
        }
    }
    Copy-Item -LiteralPath (Join-Path $source $file) -Destination $destination -Force
}
Write-Output ('已安装：' + $target)
Write-Output '请重启启动器并启用 BattleBGMExtend；更新 DLL 前需正常退出游戏。'
