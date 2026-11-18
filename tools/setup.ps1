param(
    [string]$SdkPath = 'E:\Codex\work\esp32-speaker-foundation\esp-idf-v5.5.6',
    [string]$ToolsPath = 'E:\Codex\work\esp32-speaker-foundation\idf-tools'
)
$ErrorActionPreference = 'Stop'
$projectPath = Split-Path $PSScriptRoot -Parent
$lock = Get-Content -LiteralPath (Join-Path $projectPath 'sdk.lock.json') -Raw | ConvertFrom-Json
if (-not (Test-Path -LiteralPath $SdkPath)) {
    New-Item -ItemType Directory -Force -Path (Split-Path $SdkPath -Parent) | Out-Null
    & git clone --branch $lock.tag --depth 1 --recursive --shallow-submodules $lock.repository $SdkPath
    if ($LASTEXITCODE -ne 0) { throw 'SDK clone incomplete; retry submodules after checking connectivity' }
}
$revision = & git -C $SdkPath rev-parse HEAD
if ($LASTEXITCODE -ne 0 -or $revision -ne $lock.commit) { throw 'Existing SDK has unexpected commit; no files overwritten' }
& git -C $SdkPath submodule update --init --recursive --depth 1
if ($LASTEXITCODE -ne 0) { throw 'SDK submodule download incomplete' }
$env:IDF_TOOLS_PATH = $ToolsPath
& python (Join-Path $SdkPath 'tools\idf_tools.py') install --targets=esp32
if ($LASTEXITCODE -ne 0) { throw 'Toolchain installation failed' }
& python (Join-Path $SdkPath 'tools\idf_tools.py') install-python-env
if ($LASTEXITCODE -ne 0) { throw 'Python environment installation failed' }
