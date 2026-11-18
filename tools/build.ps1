param(
    [string]$SdkPath = 'E:\Codex\work\esp32-speaker-foundation\esp-idf-v5.5.6',
    [string]$ToolsPath = 'E:\Codex\work\esp32-speaker-foundation\idf-tools',
    [string]$BuildPath = 'E:\Codex\work\esp32-speaker-foundation\build',
    [string]$Port,
    [ValidateSet('build','menuconfig','flash','monitor','size')][string]$Action = 'build'
)
$ErrorActionPreference = 'Stop'
$projectPath = Split-Path $PSScriptRoot -Parent
$lock = Get-Content -LiteralPath (Join-Path $projectPath 'sdk.lock.json') -Raw | ConvertFrom-Json
$revision = & git -C $SdkPath rev-parse HEAD
if ($LASTEXITCODE -ne 0 -or $revision -ne $lock.commit) { throw 'SDK commit differs from sdk.lock.json' }
$env:IDF_TOOLS_PATH = $ToolsPath
. (Join-Path $SdkPath 'export.ps1')
if ($LASTEXITCODE -ne 0) { throw 'ESP-IDF environment activation failed' }
$idfArgs = @('-C', $projectPath, '-B', $BuildPath)
if ($Port) { $idfArgs += @('-p', $Port) }
$idfArgs += $Action
& python (Join-Path $SdkPath 'tools\idf.py') @idfArgs
exit $LASTEXITCODE
