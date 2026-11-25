# Mono GACのような長いパスにも、Windows PowerShellのモジュール読み込みに依存せずアクセスする。
function Get-NexusFileSha256([string]$Path) {
    $full = [IO.Path]::GetFullPath($Path)
    if([IO.Path]::DirectorySeparatorChar -eq '\' -and !$full.StartsWith('\\?\')) {
        if($full.StartsWith('\\')) {
            $full = '\\?\UNC\' + $full.Substring(2)
        } else {
            $full = '\\?\' + $full
        }
    }
    $inputFile = [IO.File]::OpenRead($full)
    try {
        $hasher = [Security.Cryptography.SHA256]::Create()
        try {
            return [BitConverter]::ToString($hasher.ComputeHash($inputFile)).Replace('-','')
        } finally {
            $hasher.Dispose()
        }
    } finally {
        $inputFile.Dispose()
    }
}
