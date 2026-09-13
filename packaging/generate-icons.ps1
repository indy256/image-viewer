# Regenerate platform icons from iv-icon.png using Windows PowerShell.
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$source = [System.Drawing.Image]::FromFile("$PSScriptRoot/iv-icon.png")
$images = @{}
try {
    foreach ($size in 16, 32, 48, 64, 128, 256, 512, 1024) {
        $bitmap = [System.Drawing.Bitmap]::new($size, $size)
        $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
        $stream = [IO.MemoryStream]::new()
        try {
            $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
            $graphics.CompositingMode = [System.Drawing.Drawing2D.CompositingMode]::SourceCopy
            $graphics.DrawImage($source, 0, 0, $size, $size)
            $bitmap.Save($stream, [System.Drawing.Imaging.ImageFormat]::Png)
            $images[$size] = $stream.ToArray()
        } finally {
            $stream.Dispose()
            $graphics.Dispose()
            $bitmap.Dispose()
        }
    }
} finally {
    $source.Dispose()
}
[IO.File]::WriteAllBytes("$PSScriptRoot/image-viewer.png", $images[512])

$stream = [IO.File]::Create("$PSScriptRoot/iv.ico")
$writer = [IO.BinaryWriter]::new($stream)
try {
    $sizes = @(16, 32, 48, 64, 128, 256)
    $writer.Write([uint16]0)
    $writer.Write([uint16]1)
    $writer.Write([uint16]$sizes.Count)
    $offset = 6 + 16 * $sizes.Count
    foreach ($size in $sizes) {
        $dimension = if ($size -eq 256) { 0 } else { $size }
        $writer.Write([byte]$dimension)
        $writer.Write([byte]$dimension)
        $writer.Write([uint16]0)
        $writer.Write([uint16]1)
        $writer.Write([uint16]32)
        $writer.Write([uint32]$images[$size].Length)
        $writer.Write([uint32]$offset)
        $offset += $images[$size].Length
    }
    foreach ($size in $sizes) { $writer.Write([byte[]]$images[$size]) }
} finally { $writer.Dispose() }

function Write-BigEndian([IO.BinaryWriter]$Writer, [uint32]$Value) {
    $bytes = [BitConverter]::GetBytes($Value)
    if ([BitConverter]::IsLittleEndian) { [Array]::Reverse($bytes) }
    $Writer.Write($bytes)
}
$chunks = [ordered]@{ icp4 = 16; icp5 = 32; icp6 = 64; ic07 = 128; ic08 = 256; ic09 = 512; ic10 = 1024 }
$length = 8
foreach ($size in $chunks.Values) { $length += 8 + $images[$size].Length }
$stream = [IO.File]::Create("$PSScriptRoot/iv.icns")
$writer = [IO.BinaryWriter]::new($stream)
try {
    $writer.Write([Text.Encoding]::ASCII.GetBytes('icns'))
    Write-BigEndian $writer $length
    foreach ($chunk in $chunks.GetEnumerator()) {
        $writer.Write([Text.Encoding]::ASCII.GetBytes($chunk.Key))
        Write-BigEndian $writer (8 + $images[$chunk.Value].Length)
        $writer.Write([byte[]]$images[$chunk.Value])
    }
} finally { $writer.Dispose() }
