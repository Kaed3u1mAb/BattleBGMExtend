param([string]$Compiler = 'gcc', [string]$Python = 'python')
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
& $Python (Join-Path $root 'src\tests\test_loader.py')
if ($LASTEXITCODE -ne 0) { throw '加载器测试失败' }
$mh = Join-Path $root 'src\third_party\minhook'
$build = Join-Path $root 'build'
New-Item -ItemType Directory -Force -Path $build | Out-Null
$test = Join-Path $build 'test_policy.exe'
$sources = @(
    (Join-Path $root 'src\tests\test_policy.c'),
    (Join-Path $mh 'src\hook.c'),
    (Join-Path $mh 'src\buffer.c'),
    (Join-Path $mh 'src\trampoline.c'),
    (Join-Path $mh 'src\hde\hde64.c')
)
& $Compiler -O0 -g -Wall -Wextra -Wno-unused-function @sources '-I' (Join-Path $mh 'include') '-I' (Join-Path $mh 'src\hde') '-o' $test
if ($LASTEXITCODE -ne 0) { throw '原生测试编译失败' }
& $test
if ($LASTEXITCODE -ne 0) { throw '原生测试失败' }
