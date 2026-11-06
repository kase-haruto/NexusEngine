$ErrorActionPreference = 'Stop'
$workspace = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$testRoot = Join-Path $workspace ('Generated/EngineReleaseTests/' + [guid]::NewGuid().ToString('N'))
$engine = Join-Path $testRoot 'Engine'
New-Item -ItemType Directory -Path $engine | Out-Null
[IO.File]::WriteAllText((Join-Path $engine 'NexusEngine.nxengine'), '{"association":"installed","id":"nexus","version":"1.0.0","configuration":"Release"}')
$notes = Join-Path $testRoot 'changes.txt'
$expected = "NexusEngine v1.0.0`nRelease notes"
[IO.File]::WriteAllText($notes, $expected)
$generator = Join-Path $workspace 'Project/Tools/Project/New-EngineRelease.ps1'
foreach($mode in @('Local','Remote')) {
    $output = Join-Path $testRoot $mode
    $parameters = @{EngineDirectory=$engine;ChangesFile=$notes;OutputDirectory=$output}
    if($mode -eq 'Remote') { $parameters.DownloadUrl = 'https://example.com/nexus-1.0.0-Release.zip' }
    $null = & $generator @parameters
    $name = if($mode -eq 'Local') { 'releases.local.json' } else { 'releases.json' }
    $file = Join-Path $output $name
    $catalog = Get-Content -LiteralPath $file -Raw -Encoding UTF8 | ConvertFrom-Json
    $release = $catalog.releases[0]
    if($release.changes -isnot [string] -or $release.changes -cne $expected) { throw 'Changes must be a plain string without PowerShell provider metadata.' }
    if((Get-Item -LiteralPath $file).Length -gt 4096) { throw 'Catalog contains unexpected metadata.' }
    $zip = Join-Path $output 'nexus-1.0.0-Release.zip'
    if($release.bytes -ne (Get-Item -LiteralPath $zip).Length -or $release.sha256 -ine (Get-FileHash -LiteralPath $zip).Hash) { throw 'Package size/hash mismatch.' }
    $url = if($mode -eq 'Local') { [IO.Path]::GetFileName($zip) } else { $parameters.DownloadUrl }
    if($release.url -cne $url) { throw 'Incorrect package URL.' }
}
Write-Output 'PASS: 8 engine release generation checks'
