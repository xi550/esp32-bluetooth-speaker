param(
    [string]$SdkPath,
    [string]$ToolsPath = $env:IDF_TOOLS_PATH
)
$ErrorActionPreference = 'Stop'
$projectPath = Split-Path $PSScriptRoot -Parent
if (-not $SdkPath) { $SdkPath = Join-Path $projectPath 'sdk\esp-idf' }
if (-not $ToolsPath) { $ToolsPath = Join-Path $env:USERPROFILE '.espressif' }
$SdkPath = & python -c 'import os,sys; print(os.path.realpath(sys.argv[1]))' $SdkPath
if ($LASTEXITCODE -ne 0) { throw 'SDK path resolution failed' }
& python (Join-Path $PSScriptRoot 'check_sdk.py') $SdkPath
if ($LASTEXITCODE -ne 0) { throw 'SDK snapshot verification failed' }
$env:IDF_TOOLS_PATH = $ToolsPath
& python (Join-Path $SdkPath 'tools\idf_tools.py') install --targets=esp32
if ($LASTEXITCODE -ne 0) { throw 'Toolchain installation failed' }
& python (Join-Path $SdkPath 'tools\idf_tools.py') install cmake ninja
if ($LASTEXITCODE -ne 0) { throw 'Build tools installation failed' }
& python (Join-Path $SdkPath 'tools\idf_tools.py') install-python-env
if ($LASTEXITCODE -ne 0) { throw 'Python environment installation failed' }
