param([Parameter(Mandatory=$true)][string]$MonoRoot,[string]$OutputDirectory,[switch]$ValidateOnly)
$ErrorActionPreference='Stop'
$source=[IO.Path]::GetFullPath($MonoRoot).TrimEnd('\','/')
# Windows PowerShell 5.1のFileSystem providerは長いGAC名をMAX_PATHで拒否する。
# Processの設定やOS policyを変えず、明示的な拡張長pathでファイルAPIを利用する。
function Native-Path([string]$Path) {
    $full=[IO.Path]::GetFullPath($Path)
    if($full.StartsWith('\\?\')) { return $full }
    if($full.StartsWith('\\')) { return '\\?\UNC\'+$full.Substring(2) }
    return '\\?\'+$full
}
function File-Digest([string]$Path) {
    $stream=[IO.File]::OpenRead((Native-Path $Path)); $hash=[Security.Cryptography.SHA256]::Create()
    try { return [BitConverter]::ToString($hash.ComputeHash($stream)) }
    finally { $hash.Dispose(); $stream.Dispose() }
}
function Copy-MonoFile([string]$From,[string]$To) {
    $target=Native-Path $To
    [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($target)) | Out-Null
    if([IO.File]::Exists($target) -and (File-Digest $From) -ceq (File-Digest $To)) { return }
    [IO.File]::Copy((Native-Path $From),$target,$true)
}
foreach($required in @('bin/mono.exe','bin/mono-2.0-sgen.dll','lib/mono/4.5/mscorlib.dll','etc/mono/config')) {
    if(-not [IO.File]::Exists((Join-Path $source $required))) { throw "Incomplete Mono SDK: $required" }
}
if(-not [IO.Directory]::Exists((Join-Path $source 'lib/mono/gac'))) { throw 'Incomplete Mono SDK: lib/mono/gac' }
if($ValidateOnly) { return }
if(-not $OutputDirectory) { throw 'OutputDirectory is required for Mono deployment.' }
$destination=[IO.Path]::GetFullPath($OutputDirectory).TrimEnd('\','/')
# 配置済みRuntimeをSDKとして再利用する場合は、その場へのcopyを繰り返さない。
if($source -ieq $destination) { return }
if($destination.StartsWith($source+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) { throw 'Mono deployment cannot be inside its source SDK.' }
# Windows版Monoはstrong-name付きBCLをGACから解決する。4.5だけではSystem等を解決できない。
foreach($directory in @('bin','lib/mono/4.5','lib/mono/gac','etc/mono')) {
    $from=Join-Path $source $directory
    foreach($entry in Get-ChildItem -LiteralPath $from -Recurse -Force) {
        if($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Mono deployment cannot traverse reparse points.' }
        if($entry.PSIsContainer) { continue }
        # CLI / SGen / native helperだけをbinへ配置する。コンパイラ自体は.NET SDKの責務とする。
        if($directory -ceq 'bin' -and $entry.Extension -ine '.dll' -and $entry.Name -ine 'mono.exe' -and $entry.Name -ine 'mono.exe.config') { continue }
        $relative=$entry.FullName.Substring($source.Length+1)
        $target=Join-Path $destination $relative
        Copy-MonoFile $entry.FullName $target
    }
}
# 公式配布のlicenseをRuntimeに同梱し、Packageでそのまま引き継ぐ。
$notice=Join-Path $PSScriptRoot '../../ThirdParty/Mono'
if(Test-Path -LiteralPath $notice) {
    foreach($entry in Get-ChildItem -LiteralPath $notice -File) { Copy-MonoFile $entry.FullName (Join-Path $destination ('Notices/'+$entry.Name)) }
}
