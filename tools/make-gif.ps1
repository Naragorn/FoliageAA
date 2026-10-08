# Animated GIF from a list of PNG frames, looping forever, with one delay
# for every frame. WPF's GifBitmapEncoder writes the frames but neither a
# loop nor a delay, so its output is read back block by block and written
# again with a NETSCAPE2.0 loop extension after the screen descriptor and a
# graphic control extension carrying the delay before every image.
#
#   make-gif.ps1 -Frames <png>[,<png>...] -DelayMs 120 -Out <gif>

[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)][string[]]$Frames,
	[int]$DelayMs = 120,
	[Parameter(Mandatory = $true)][string]$Out
)

Add-Type -AssemblyName PresentationCore

$encoder = New-Object System.Windows.Media.Imaging.GifBitmapEncoder
foreach ($frame in $Frames) {
	$path = (Resolve-Path -LiteralPath $frame).Path
	$decoder = New-Object System.Windows.Media.Imaging.PngBitmapDecoder(
		(New-Object System.Uri($path)),
		[System.Windows.Media.Imaging.BitmapCreateOptions]::PreservePixelFormat,
		[System.Windows.Media.Imaging.BitmapCacheOption]::OnLoad)
	$encoder.Frames.Add([System.Windows.Media.Imaging.BitmapFrame]::Create($decoder.Frames[0]))
}
$raw = New-Object System.IO.MemoryStream
$encoder.Save($raw)
$bytes = $raw.ToArray()

# Walk the GIF: header and logical screen descriptor, the global colour
# table if any, then extension and image blocks up to the trailer.
if ([System.Text.Encoding]::ASCII.GetString($bytes, 0, 3) -ne "GIF") { throw "not a GIF" }
$pos = 13
$packed = $bytes[10]
if ($packed -band 0x80) { $pos += [int](3 * [Math]::Pow(2, ($packed -band 7) + 1)) }
$outStream = New-Object System.IO.MemoryStream
$outStream.Write($bytes, 0, $pos)
# NETSCAPE2.0: loop forever.
$loopList = New-Object System.Collections.Generic.List[byte]
foreach ($v in @(0x21, 0xFF, 0x0B)) { $loopList.Add([byte]$v) }
foreach ($v in [System.Text.Encoding]::ASCII.GetBytes("NETSCAPE2.0")) { $loopList.Add([byte]$v) }
foreach ($v in @(0x03, 0x01, 0x00, 0x00, 0x00)) { $loopList.Add([byte]$v) }
$loop = $loopList.ToArray()
$outStream.Write($loop, 0, $loop.Length)
$delay = [int][Math]::Round($DelayMs / 10)
$gceList = New-Object System.Collections.Generic.List[byte]
foreach ($v in @(0x21, 0xF9, 0x04, 0x00, ($delay -band 0xFF), ([int]($delay / 256) -band 0xFF), 0x00, 0x00)) { $gceList.Add([byte]$v) }
$gce = $gceList.ToArray()

function SkipSubBlocks([int]$at) {
	while ($bytes[$at] -ne 0) { $at += 1 + $bytes[$at] }
	return $at + 1
}

$frameCount = 0  # not $frames: PowerShell variables are case-insensitive and $Frames is the parameter
while ($pos -lt $bytes.Length) {
	$b = $bytes[$pos]
	if ($b -eq 0x3B) { break }
	if ($b -eq 0x21) {
		# An extension: drop any graphic control extension (ours replaces
		# it), keep everything else as it is.
		$label = $bytes[$pos + 1]
		$end = SkipSubBlocks ($pos + 2)
		if ($label -ne 0xF9) { $outStream.Write($bytes, $pos, $end - $pos) }
		$pos = $end
		continue
	}
	if ($b -eq 0x2C) {
		$outStream.Write($gce, 0, $gce.Length)
		$start = $pos
		$flags = $bytes[$pos + 9]
		$pos += 10
		if ($flags -band 0x80) { $pos += [int](3 * [Math]::Pow(2, ($flags -band 7) + 1)) }
		$pos += 1  # LZW minimum code size
		$pos = SkipSubBlocks $pos
		$outStream.Write($bytes, $start, $pos - $start)
		$frameCount++
		continue
	}
	throw "unexpected block 0x$($b.ToString('X2')) at $pos"
}
$outStream.WriteByte(0x3B)
$outPath = [IO.Path]::GetFullPath($Out)
[IO.File]::WriteAllBytes($outPath, $outStream.ToArray())
Write-Output "wrote $outPath ($frameCount frames, $DelayMs ms each, $([int]($outStream.Length / 1024)) KiB)"
