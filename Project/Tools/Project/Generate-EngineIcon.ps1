param([string]$Destination)
$ErrorActionPreference = 'Stop'
if(-not $Destination) { $Destination = Join-Path $PSScriptRoot '../../Resources/Editor/Branding/NexusEngine.ico' }
$Destination = [IO.Path]::GetFullPath($Destination)
Add-Type -AssemblyName System.Drawing
$images = @()
foreach($size in @(16,32,48,64,128,256)) {
    $bitmap = [Drawing.Bitmap]::new($size,$size,[Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $graphics = [Drawing.Graphics]::FromImage($bitmap)
    $pen = [Drawing.Pen]::new([Drawing.Color]::FromArgb(242,125,46),13)
    $background = [Drawing.SolidBrush]::new([Drawing.Color]::FromArgb(35,35,35))
    $shape = [Drawing.Drawing2D.GraphicsPath]::new()
    $stream = [IO.MemoryStream]::new()
    try {
        $graphics.SmoothingMode = [Drawing.Drawing2D.SmoothingMode]::AntiAlias
        $graphics.ScaleTransform(($size/256.0),($size/256.0))
        $shape.AddArc(8,8,88,88,180,90); $shape.AddArc(160,8,88,88,270,90)
        $shape.AddArc(160,160,88,88,0,90); $shape.AddArc(8,160,88,88,90,90); $shape.CloseFigure()
        $graphics.FillPath($background,$shape)
        $pen.StartCap = $pen.EndCap = [Drawing.Drawing2D.LineCap]::Round
        $pen.LineJoin = [Drawing.Drawing2D.LineJoin]::Round
        $graphics.DrawLines($pen,[Drawing.PointF[]]@([Drawing.PointF]::new(42,180),[Drawing.PointF]::new(42,76),[Drawing.PointF]::new(114,180),[Drawing.PointF]::new(114,76)))
        $graphics.DrawLine($pen,146,76,214,180); $graphics.DrawLine($pen,214,76,146,180)
        $bitmap.Save($stream,[Drawing.Imaging.ImageFormat]::Png)
        $images += ,@{Size=$size; Bytes=$stream.ToArray()}
    } finally { $stream.Dispose(); $shape.Dispose(); $background.Dispose(); $pen.Dispose(); $graphics.Dispose(); $bitmap.Dispose() }
}
# 各DPI用のPNGを一つのICOへ格納する。外部画像やライセンス済みロゴは使わない。
[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($Destination)) | Out-Null
$file = [IO.MemoryStream]::new(); $writer = [IO.BinaryWriter]::new($file)
try {
    $writer.Write([uint16]0); $writer.Write([uint16]1); $writer.Write([uint16]$images.Count)
    $offset = 6 + 16 * $images.Count
    foreach($item in $images) {
        $dimension = if($item.Size -eq 256) { 0 } else { $item.Size }
        $writer.Write([byte]$dimension); $writer.Write([byte]$dimension)
        $writer.Write([byte]0); $writer.Write([byte]0); $writer.Write([uint16]1); $writer.Write([uint16]32)
        $writer.Write([uint32]$item.Bytes.Length); $writer.Write([uint32]$offset); $offset += $item.Bytes.Length
    }
    foreach($item in $images) { $writer.Write([byte[]]$item.Bytes) }
    $writer.Flush(); [IO.File]::WriteAllBytes($Destination,$file.ToArray())
} finally { $writer.Dispose(); $file.Dispose() }
Write-Output "Generated NexusEngine icon: $Destination"
