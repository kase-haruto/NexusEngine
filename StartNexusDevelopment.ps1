[CmdletBinding()]
param(
    [switch]$NoBuild
)

$ErrorActionPreference = 'Stop'

# スクリプトの配置場所を基準にし、どの作業ディレクトリからでも起動可能にする。
$repositoryRoot = $PSScriptRoot
$solutionPath = Join-Path $repositoryRoot 'Project\NexusEngine.slnx'
$toolProjectPath = Join-Path $repositoryRoot 'Tools\NexusProjectTool\NexusProjectTool.csproj'
$toolDllPath = Join-Path $repositoryRoot 'Tools\NexusProjectTool\bin\Debug\net8.0\NexusProjectTool.dll'
$configPath = Join-Path $repositoryRoot 'ProjectSync.json'

if (-not (Test-Path -LiteralPath $solutionPath)) {
    throw "Solution not found: $solutionPath"
}

# Visual Studio Installer付属のvswhereを使い、最新のVisual Studioを検出する。
$vswherePath = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswherePath)) {
    throw 'vswhere.exe was not found. Check the Visual Studio Installer.'
}

$visualStudioPath = & $vswherePath `
    -latest `
    -products * `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    -property productPath

if ([string]::IsNullOrWhiteSpace($visualStudioPath) -or
    -not (Test-Path -LiteralPath $visualStudioPath)) {
    throw 'Visual Studio with the C++ development tools was not found.'
}

# 初回またはツール更新後は、watchを起動する前にC#ツールをビルドする。
if (-not $NoBuild) {
    & dotnet build $toolProjectPath --nologo
    if ($LASTEXITCODE -ne 0) {
        throw "NexusProjectTool build failed with exit code $LASTEXITCODE."
    }
}

if (-not (Test-Path -LiteralPath $toolDllPath)) {
    throw "NexusProjectTool was not found. Run again without -NoBuild: $toolDllPath"
}

$watchProcess = $null
$visualStudioProcess = $null

try {
    # watchは非表示のバックグラウンドプロセスとして起動する。
    $watchProcess = Start-Process `
        -FilePath 'dotnet' `
        -ArgumentList @(
            $toolDllPath,
            'watch',
            '--config',
            $configPath
        ) `
        -WorkingDirectory $repositoryRoot `
        -WindowStyle Hidden `
        -PassThru

    # watchの初期化を待ち、即時終了していないことを確認してからVisual Studioを開く。
    Start-Sleep -Milliseconds 500
    if ($watchProcess.HasExited) {
        throw "NexusProjectTool watch failed with exit code $($watchProcess.ExitCode)."
    }

    $visualStudioProcess = Start-Process `
        -FilePath $visualStudioPath `
        -ArgumentList @($solutionPath) `
        -WorkingDirectory $repositoryRoot `
        -PassThru

    Write-Host 'NexusProjectTool watch started.'
    Write-Host "Visual Studio: $visualStudioPath"

    # このスクリプトから起動したVisual Studioが終了するまで待機する。
    $visualStudioProcess.WaitForExit()
}
finally {
    # Visual Studio終了後、対応するwatchプロセスだけを停止する。
    if ($null -ne $watchProcess -and -not $watchProcess.HasExited) {
        Stop-Process -Id $watchProcess.Id
        $watchProcess.WaitForExit()
    }
}
