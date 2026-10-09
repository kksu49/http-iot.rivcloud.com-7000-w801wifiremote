param([ValidateRange(1,1)][int]$Jobs = 1)
$ErrorActionPreference = 'Stop'
$taskRoot = $PSScriptRoot
& python (Join-Path $taskRoot 'generate_project_config.py')
if ($LASTEXITCODE -ne 0) { throw '统一配置校验或生成失败' }
$sdkPath = Join-Path $taskRoot 'w801-sdk\wm_sdk_w80x_20211115'
$compiler = Get-ChildItem -LiteralPath (Join-Path $taskRoot 'toolchains') -Filter 'csky-elfabiv2-gcc.exe' -Recurse | Select-Object -First 1
if (-not $compiler) { throw '未找到工作区内的 C-SKY 编译器' }
$env:PATH = $compiler.DirectoryName + ';C:\MinGW\msys\1.0\bin;C:\MinGW\bin;' + $env:PATH
Push-Location -LiteralPath $sdkPath
try {
    & 'C:\MinGW\msys\1.0\bin\sh.exe' -c "make -j $Jobs CONFIG_W800_TOOLCHAIN_PREFIX=csky-elfabiv2 CONFIG_W800_TOOLCHAIN_PATH= CONFIG_W800_DOWNLOAD_PORT=COM10"
    if ($LASTEXITCODE -ne 0) { throw "W801 编译失败，退出码 $LASTEXITCODE" }
    $image = Join-Path $sdkPath 'bin\w800\w800.fls'
    if (-not (Test-Path -LiteralPath $image)) { throw '未生成预期的 .fls 镜像，请检查构建输出' }
    Get-Item -LiteralPath $image | Select-Object FullName,Length
} finally {
    Pop-Location
}
