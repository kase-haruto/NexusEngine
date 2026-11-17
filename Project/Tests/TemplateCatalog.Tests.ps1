param(
    [Parameter(Mandatory=$true)][string]$Tool,
    [Parameter(Mandatory=$true)][string]$EngineManifest,
    [string]$OutputDirectory = (Join-Path $PSScriptRoot ('../../Generated/TemplateCatalogValidation/' + [guid]::NewGuid().ToString('N')))
)
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
$root = [IO.Path]::GetFullPath($OutputDirectory)
if(Test-Path -LiteralPath $root) { throw 'Choose a new test output directory.' }
[IO.Directory]::CreateDirectory($root) | Out-Null
$source = Join-Path $PSScriptRoot '../Templates'
function Run([string[]]$Arguments, [int]$Expected = 0) {
    $output = & $Tool @Arguments 2>&1 | Out-String
    if($LASTEXITCODE -ne $Expected) { throw "Exit $LASTEXITCODE (expected $Expected): $Arguments`n$output" }
    return $output
}
function Save([string]$Path, $Value) {
    [IO.File]::WriteAllText($Path, ($Value | ConvertTo-Json -Depth 20), [Text.UTF8Encoding]::new($false))
}
function Fixture([string]$Name) {
    $directory = Join-Path $root $Name
    [IO.Directory]::CreateDirectory($directory) | Out-Null
    foreach($file in @('blank.nxtemplate','basic-3d.nxtemplate')) { Copy-Item -LiteralPath (Join-Path $source $file) -Destination $directory }
    foreach($folder in @('Blank','Basic3D')) { Copy-Item -LiteralPath (Join-Path $source $folder) -Destination $directory -Recurse }
    return $directory
}
function Catalog([string]$Directory) {
    return (Run @('template-catalog',$EngineManifest,$Directory) | ConvertFrom-Json).templates
}
$good = Fixture 'Good'
$templates = @(Catalog $good)
if($templates.Count -ne 2 -or @($templates | Where-Object error).Count) { throw 'Two healthy templates were not returned.' }
# 画像参照の欠落はLevel生成と独立している。
$preview = Fixture 'MissingPreview'
$metadata = Get-Content -LiteralPath (Join-Path $preview 'blank.nxtemplate') -Raw | ConvertFrom-Json
$metadata.preview.source = 'missing.png'
Save (Join-Path $preview 'blank.nxtemplate') $metadata
if(@(Catalog $preview | Where-Object error).Count) { throw 'Missing preview blocked a valid project.' }
$null = Run @('create-project',(Join-Path $root 'PreviewFallback'),'Preview Fallback',$EngineManifest,$preview,'blank')
# 一件の破損を局所化し、他方のTemplateを利用可能なまま残す。
foreach($mode in @('Metadata','MissingSource','TraversalSource','TraversalTarget','MissingInitialLevel','CorruptLevel','Duplicate')) {
    $directory = Fixture $mode
    $file = Join-Path $directory 'blank.nxtemplate'
    $data = Get-Content -LiteralPath $file -Raw | ConvertFrom-Json
    switch($mode) {
        Metadata { [IO.File]::WriteAllText($file,'{broken json') }
        MissingSource { $data.files[0].source = 'Blank/missing.level'; Save $file $data }
        TraversalSource { $data.files[0].source = '../Good/Blank/Startup.level'; Save $file $data }
        TraversalTarget { $data.files[0].target = '../Escape.level'; Save $file $data }
        MissingInitialLevel { $data.initialLevel = 'Levels/Missing.level'; Save $file $data }
        CorruptLevel { [IO.File]::WriteAllText((Join-Path $directory 'Blank/Startup.level'),'broken Level') }
        Duplicate { Copy-Item -LiteralPath $file -Destination (Join-Path $directory 'duplicate.nxtemplate') }
    }
    $items = @(Catalog $directory)
    if(@($items | Where-Object error).Count -eq 0 -or @($items | Where-Object { $_.id -eq 'basic-3d' -and -not $_.error }).Count -ne 1) { throw "Error isolation failed: $mode" }
    if($mode -ne 'Metadata') {
        $target = Join-Path $root ('Rejected-' + $mode)
        $null = Run @('create-project',$target,'Rejected',$EngineManifest,$directory,'blank') 1
        if(Test-Path -LiteralPath $target) { throw "Broken template published a Project: $mode" }
    }
}
$null = Run @('template-catalog',(Join-Path $root 'Missing.nxengine'),$good) 1
$null = Run @('template-catalog',$EngineManifest,(Join-Path $root 'MissingTemplates')) 1
# 残したSampleの生成内容も確認する。Compiler・Runtime・画面captureは起動しない。
foreach($sample in @(@('native-module','Cpp'),@('managed-gameplay','CSharp'),@('hybrid-gameplay','Cpp'),@('hybrid-gameplay','CSharp'))) {
    $target = Join-Path $root ($sample[0] + '-' + $sample[1])
    $file = (Run @('create-template',$target,$sample[0],$EngineManifest,$sample[0],$sample[1])).Trim()
    $project = Get-Content -LiteralPath $file -Raw | ConvertFrom-Json
    $expected = if($sample[0] -eq 'hybrid-gameplay') { 2 } else { 1 }
    if(@($project.modules).Count -ne $expected) { throw 'Sample Module count changed.' }
    if(-not (Test-Path -LiteralPath (Join-Path $target 'Config/BuildTargets.json'))) { throw 'Sample has no host build configuration.' }
}
Write-Output "PASS: catalog isolation, missing/corrupt assets and metadata, path containment, duplicate IDs, creation rejection, missing engine/template directory and all retained sample generation modes. $root"
