# Deterministic linear RGBE panorama. Broad softboxes expose metallic curvature and roughness.
$ErrorActionPreference = 'Stop'
$studioPath = Join-Path $PSScriptRoot '../Resources/Assets/Environments/PBRStudio.hdr'
[System.IO.Directory]::CreateDirectory([System.IO.Path]::GetDirectoryName($studioPath)) | Out-Null
$studioWriter = [System.IO.BinaryWriter]::new([System.IO.File]::Create($studioPath))
try {
    $studioWriter.Write([System.Text.Encoding]::ASCII.GetBytes("#?RADIANCE`nFORMAT=32-bit_rle_rgbe`n`n-Y 128 +X 256`n"))
    for($y=0;$y -lt 128;$y++) { for($x=0;$x -lt 256;$x++) {
        $u=($x+.5)/256; $v=($y+.5)/128
        $key=10*[Math]::Exp(-[Math]::Pow(($u-.24)/.065,8)-[Math]::Pow(($v-.34)/.21,8))
        $top=7*[Math]::Exp(-[Math]::Pow(($u-.68)/.17,8)-[Math]::Pow(($v-.19)/.06,8))
        $rim=5*[Math]::Exp(-[Math]::Pow(($u-.88)/.025,8)-[Math]::Pow(($v-.4)/.2,8))
        $base=.025+.16*[Math]::Max(0,[Math]::Cos($v*[Math]::PI))
        $rgb=@(($base+$key+$top+.65*$rim),($base+$key+.95*$top+.8*$rim),($base+$key+.9*$top+$rim))
        $peak=[Math]::Max($rgb[0],[Math]::Max($rgb[1],$rgb[2]))
        $exponent=[int][Math]::Floor([Math]::Log($peak,2))+1
        $scale=256/[Math]::Pow(2,$exponent)
        foreach($channel in $rgb) { $studioWriter.Write([byte][Math]::Clamp([int]($channel*$scale),0,255)) }
        $studioWriter.Write([byte]($exponent+128))
    } }
} finally { $studioWriter.Dispose() }
