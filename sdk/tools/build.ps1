param(
    [string]$SdkPath,
    [string]$ToolsPath = $env:IDF_TOOLS_PATH,
    [string]$BuildPath,
    [string]$Port,
    [ValidateSet('build','build+flash','menuconfig','flash','monitor','size')][string]$Action = 'build'
)
$ErrorActionPreference = 'Stop'
$projectPath = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
if (-not $SdkPath) { $SdkPath = Join-Path $projectPath 'sdk\esp-idf' }
if (-not $ToolsPath) { $ToolsPath = Join-Path $env:USERPROFILE '.espressif' }
$SdkPath = & python -c 'import os,sys; print(os.path.realpath(sys.argv[1]))' $SdkPath
if ($LASTEXITCODE -ne 0) { throw 'SDK path resolution failed' }
if (-not $BuildPath) { $BuildPath = Join-Path $projectPath 'build\windows' }
& python (Join-Path $PSScriptRoot 'check_sdk.py') $SdkPath --version-only
if ($LASTEXITCODE -ne 0) { throw 'SDK snapshot verification failed' }
$env:IDF_TOOLS_PATH = $ToolsPath
$env:IDF_PATH = $SdkPath
. (Join-Path $SdkPath 'export.ps1')
if ($LASTEXITCODE -ne 0) { throw 'ESP-IDF environment activation failed' }
$idfArgs = @('-C', (Join-Path $projectPath 'sdk\apps\esp_app'), '-B', $BuildPath)
if ($Port) { $idfArgs += @('-p', $Port) }
$idfArgs += $Action.Split('+')
& python (Join-Path $PSScriptRoot 'run_idf.py') (Join-Path $SdkPath 'tools\idf.py') @idfArgs
exit $LASTEXITCODE
