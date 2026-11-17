param(
    [Parameter(Mandatory)][string]$AssetRoot,
    [Parameter(Mandatory)][string]$Source,
    [string]$OutputDirectory
)
$ErrorActionPreference = 'Stop'
$assetDirectory = (Resolve-Path -LiteralPath $AssetRoot).Path
$sourcePath = (Resolve-Path -LiteralPath $Source).Path
$relativeSource = [IO.Path]::GetRelativePath($assetDirectory, $sourcePath).Replace('\', '/')
if ($relativeSource -eq '..' -or $relativeSource.StartsWith('../') -or [IO.Path]::IsPathRooted($relativeSource)) {
    throw 'Source must be inside AssetRoot.'
}

# GLBもJSON chunkだけを読む。モデルや画像を複製せず、元ファイルを依存先として保持する。
$extension = [IO.Path]::GetExtension($sourcePath).ToLowerInvariant()
if ($extension -eq '.glb') {
    $stream = [IO.File]::OpenRead($sourcePath)
    $reader = [IO.BinaryReader]::new($stream)
    try {
        if ($reader.ReadUInt32() -ne 0x46546C67 -or $reader.ReadUInt32() -ne 2) {
            throw 'Expected glTF 2.0 GLB.'
        }
        $length = $reader.ReadUInt32()
        if ($length -ne $stream.Length) { throw 'Invalid GLB length.' }
        $chunkLength = $reader.ReadUInt32()
        if ($reader.ReadUInt32() -ne 0x4E4F534A -or $chunkLength -gt ($stream.Length - $stream.Position)) {
            throw 'Invalid GLB JSON chunk.'
        }
        $document = [Text.Encoding]::UTF8.GetString($reader.ReadBytes($chunkLength)) | ConvertFrom-Json
    } finally {
        $reader.Dispose()
    }
} elseif ($extension -eq '.gltf') {
    $document = Get-Content -LiteralPath $sourcePath -Raw -Encoding utf8 | ConvertFrom-Json
} else {
    throw 'Source must be .gltf or .glb.'
}
if (-not $document.animations -or $document.animations.Count -eq 0) { throw 'Source has no animations.' }
if (-not $OutputDirectory) {
    $OutputDirectory = Join-Path $assetDirectory ('Animations/' + [IO.Path]::GetFileNameWithoutExtension($sourcePath))
}
$outputPath = [IO.Path]::GetFullPath($OutputDirectory)
$relativeOutput = [IO.Path]::GetRelativePath($assetDirectory, $outputPath)
if ($relativeOutput -eq '..' -or $relativeOutput.StartsWith('..\') -or [IO.Path]::IsPathRooted($relativeOutput)) {
    throw 'OutputDirectory must be inside AssetRoot.'
}

# 全件の名前と出力先を検証してから書き出し、既存の手編集アセットを上書きしない。
$names = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
$files = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
$pending = @()
for ($index = 0; $index -lt $document.animations.Count; ++$index) {
    $name = [string]$document.animations[$index].name
    if (-not $name) { $name = "Animation_$index" }
    if (-not $names.Add($name)) { throw "Duplicate clip name: $name. Rename source clips first." }
    $fileName = $name -replace '[<>:"/\\|?*\x00-\x1f]', '_'
    $fileName = $fileName.TrimEnd('.', ' ')
    if (-not $fileName -or $fileName -match '^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])($|\.)') {
        $fileName = "Clip_$index"
    }
    $file = Join-Path $outputPath ($fileName + '.nxanim')
    if (-not $files.Add($file) -or (Test-Path -LiteralPath $file)) { throw "Output already exists or names collide: $file" }
    $clipDuration = 0.0
    foreach ($sampler in $document.animations[$index].samplers) {
        $bounds = $document.accessors[$sampler.input].max
        if ($bounds -and $bounds.Count -gt 0) {
            $clipDuration = [Math]::Max($clipDuration, [double]$bounds[0])
        }
    }
    $pending += @{ Path = $file; Clip = $name; Duration = $clipDuration }
}
[IO.Directory]::CreateDirectory($outputPath) | Out-Null
foreach ($item in $pending) {
    $asset = [ordered]@{ version = 2; source = $relativeSource; clip = $item.Clip; duration = $item.Duration; loop = $true }
    [IO.File]::WriteAllText($item.Path, ($asset | ConvertTo-Json) + "`n", [Text.UTF8Encoding]::new($false))
    Write-Output $item.Path
}
