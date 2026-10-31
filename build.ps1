param([string]$Compiler = 'gcc')
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$addon = Join-Path $root 'addon\BattleBGMExtend'
New-Item -ItemType Directory -Force -Path $addon | Out-Null
$mh = Join-Path $root 'src\third_party\minhook'
$sources = @(
    (Join-Path $root 'src\battlebgmextend.c'),
    (Join-Path $mh 'src\hook.c'),
    (Join-Path $mh 'src\buffer.c'),
    (Join-Path $mh 'src\trampoline.c'),
    (Join-Path $mh 'src\hde\hde64.c')
)
& $Compiler -shared -O2 -Wall -Wextra -Wno-unused-parameter -static-libgcc @sources '-I' (Join-Path $mh 'include') '-I' (Join-Path $mh 'src\hde') '-o' (Join-Path $addon 'battlebgmextend.dll')
if ($LASTEXITCODE -ne 0) { throw 'BattleBGMExtend DLL 编译失败' }
foreach ($file in @('scr.py', 'addon_info.json', 'il2cpp-api-names.txt', 'rules.json')) {
    if ($file -eq 'addon_info.json' -and (Test-Path -LiteralPath (Join-Path $addon $file))) { continue }
    Copy-Item -LiteralPath (Join-Path $root ('src\' + $file)) -Destination (Join-Path $addon $file) -Force
}
Copy-Item -LiteralPath (Join-Path $root 'THIRD_PARTY_NOTICES.txt') -Destination (Join-Path $addon 'THIRD_PARTY_LICENSES.txt') -Force
foreach ($file in @('LICENSE', 'README.md', 'RULES.md')) {
    Copy-Item -LiteralPath (Join-Path $root $file) -Destination (Join-Path $addon $file) -Force
}
Write-Output '已生成完整插件：addon/BattleBGMExtend (Windows x64)'
