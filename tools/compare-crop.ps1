# Side-by-side crop of the same region from two screenshots, for release
# pictures: the plugin off on the left, on on the right, each crop scaled
# with no smoothing so the pixels stay what the game drew.
#
#   compare-crop.ps1 -Off <png> -On <png> -X 1650 -Y 820 -Width 300 -Height 300 -Scale 3 -Out <png>

[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)][string]$Off,
	[Parameter(Mandatory = $true)][string]$On,
	[Parameter(Mandatory = $true)][int]$X,
	[Parameter(Mandatory = $true)][int]$Y,
	[Parameter(Mandatory = $true)][int]$Width,
	[Parameter(Mandatory = $true)][int]$Height,
	[int]$Scale = 2,
	[string]$LeftLabel = "without FoliageAA",
	[string]$RightLabel = "with FoliageAA",
	[Parameter(Mandatory = $true)][string]$Out
)

Add-Type -AssemblyName System.Drawing

$cropW = [int]$Width * [int]$Scale
$cropH = [int]$Height * [int]$Scale
$crops = @()
foreach ($path in @($Off, $On)) {
	$img = [System.Drawing.Image]::FromFile((Resolve-Path -LiteralPath $path).Path)
	$bmp = New-Object System.Drawing.Bitmap -ArgumentList $cropW, $cropH
	$g = [System.Drawing.Graphics]::FromImage($bmp)
	$g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::NearestNeighbor
	$g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::Half
	$src = New-Object System.Drawing.Rectangle -ArgumentList $X, $Y, $Width, $Height
	$dst = New-Object System.Drawing.Rectangle -ArgumentList 0, 0, $cropW, $cropH
	$g.DrawImage($img, $dst, $src, [System.Drawing.GraphicsUnit]::Pixel)
	$g.Dispose()
	$img.Dispose()
	$crops += $bmp
}

$gap = 12
$label = 44
$sheetW = $cropW * 2 + $gap
$sheetH = $cropH + $label
$sheet = New-Object System.Drawing.Bitmap -ArgumentList $sheetW, $sheetH
$g = [System.Drawing.Graphics]::FromImage($sheet)
$g.Clear([System.Drawing.Color]::FromArgb(24, 24, 24))
$g.DrawImage($crops[0], 0, $label)
$g.DrawImage($crops[1], $cropW + $gap, $label)
$font = New-Object System.Drawing.Font -ArgumentList "Segoe UI", 20, ([System.Drawing.FontStyle]::Bold)
$brush = [System.Drawing.Brushes]::White
$g.DrawString($LeftLabel, $font, $brush, 8, 6)
$g.DrawString($RightLabel, $font, $brush, ($cropW + $gap + 8), 6)
$g.Dispose()
$outPath = [IO.Path]::GetFullPath($Out)
$sheet.Save($outPath, [System.Drawing.Imaging.ImageFormat]::Png)
$sheet.Dispose()
$crops | ForEach-Object { $_.Dispose() }
Write-Output "wrote $outPath (${sheetW}x${sheetH})"
