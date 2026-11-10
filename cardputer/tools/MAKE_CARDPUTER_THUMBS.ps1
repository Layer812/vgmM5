# Convert album PNG/JPEG files into 64x64 RGB565 SD cache files for Cardputer.
# Windows PowerShell 5.1 / built-in System.Drawing. No Python/Pillow required.
# Usage: powershell -NoProfile -ExecutionPolicy Bypass -File MAKE_CARDPUTER_THUMBS.ps1 E:\
# Cache: E:\.VGMM5\XXXXXXXX.RGB (8-character FNV-1a of normalized /album/path).
param(
  [Parameter(Position=0,Mandatory=$true)][string]$SdRoot,
  [switch]$Force
)
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.Drawing
$rootItem=Get-Item -LiteralPath $SdRoot -ErrorAction Stop
if(-not $rootItem.PSIsContainer){throw "SD path is not a folder: $SdRoot"}
$root=$rootItem.FullName.TrimEnd([char]'\',[char]'/')
$cache=Join-Path $rootItem.FullName '.VGMM5'
[void][System.IO.Directory]::CreateDirectory($cache)

function Get-AlbumHash([string]$album) {
  # Match Cardputer's hash_album(): FNV-1a on UTF-8 bytes, ASCII case folded.
  [uint32]$h=2166136261
  foreach($val in [System.Text.Encoding]::UTF8.GetBytes($album)) {
    [byte]$b=$val
    if($b -ge 65 -and $b -le 90){$b=[byte]($b+32)}
    $h=[uint32]((([long]($h -bxor $b))*16777619L) -band 4294967295L)
  }
  return $h.ToString('X8')
}
function Album-Path([string]$absolute) {
  $path=[System.IO.Path]::GetFullPath($absolute).TrimEnd([char]'\',[char]'/')
  if(-not $path.StartsWith($root,[System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Outside SD root: $path"
  }
  $rel=$path.Substring($root.Length).TrimStart([char]'\',[char]'/')
  if([string]::IsNullOrEmpty($rel)){return '/'}
  return '/'+$rel.Replace('\','/')
}
function Find-AlbumArt([System.IO.DirectoryInfo]$folder) {
  $base=$folder.Name
  $names=@('cover64.png','cover_64.png','cover.png','cover.jpg',
           "$base.png","$base.jpg","$base.jpeg",
           'Cover.png','Cover.jpg','folder.png','folder.jpg')
  $files=@(Get-ChildItem -LiteralPath $folder.FullName -File -ErrorAction SilentlyContinue)
  foreach($candidate in $names) {
    $match=$files|Where-Object{$_.Name -ieq $candidate}|Select-Object -First 1
    if($match){return $match.FullName}
  }
  # An album PNG may sit beside, not inside, the album directory.
  if($folder.Parent) {
    $parentFiles=@(Get-ChildItem -LiteralPath $folder.Parent.FullName -File -ErrorAction SilentlyContinue)
    foreach($candidate in @("$base.png","$base.jpg","$base.jpeg")) {
      $match=$parentFiles|Where-Object{$_.Name -ieq $candidate}|Select-Object -First 1
      if($match){return $match.FullName}
    }
  }
  return $null
}
function Save-Rgb565([string]$imagePath,[string]$outputPath) {
  $image=[System.Drawing.Image]::FromFile($imagePath)
  $bmp=New-Object System.Drawing.Bitmap(64,64,[System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
  try {
    $gfx=[System.Drawing.Graphics]::FromImage($bmp)
    try {
      $gfx.Clear([System.Drawing.Color]::FromArgb(8,16,32))
      $gfx.InterpolationMode=[System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
      $gfx.CompositingQuality=[System.Drawing.Drawing2D.CompositingQuality]::HighQuality
      $scale=[Math]::Min(64.0/$image.Width,64.0/$image.Height)
      $width=[Math]::Max(1,[int][Math]::Round($image.Width*$scale))
      $height=[Math]::Max(1,[int][Math]::Round($image.Height*$scale))
      $x=[int][Math]::Floor((64-$width)/2)
      $y=[int][Math]::Floor((64-$height)/2)
      $gfx.DrawImage($image,$x,$y,$width,$height)
    } finally {$gfx.Dispose()}
    $temp=[System.IO.Path]::ChangeExtension($outputPath,'.TMP')
    $fs=[System.IO.File]::Open($temp,[System.IO.FileMode]::Create,[System.IO.FileAccess]::Write)
    $writer=New-Object System.IO.BinaryWriter($fs)
    try {
      for($y=0;$y -lt 64;$y++) {
        for($x=0;$x -lt 64;$x++) {
          $c=$bmp.GetPixel($x,$y)
          [uint16]$rgb565=((($c.R -shr 3) -shl 11) -bor
                            (($c.G -shr 2) -shl 5) -bor ($c.B -shr 3))
          $writer.Write($rgb565) # Little endian, exactly 8192 bytes.
        }
      }
    } finally {$writer.Dispose();$fs.Dispose()}
    if((Get-Item -LiteralPath $temp).Length -ne 8192) {
      Remove-Item -LiteralPath $temp -Force
      throw "Bad cache byte length"
    }
    if(Test-Path -LiteralPath $outputPath){Remove-Item -LiteralPath $outputPath -Force}
    Move-Item -LiteralPath $temp -Destination $outputPath
  } finally {$bmp.Dispose();$image.Dispose()}
}
$folders=@($rootItem)
$folders+=@(Get-ChildItem -LiteralPath $rootItem.FullName -Directory -Recurse -ErrorAction SilentlyContinue |
           Where-Object{$_.FullName -notlike "$cache*"})
$written=0;$skipped=0;$errors=0
foreach($folder in $folders) {
  try {
    $source=Find-AlbumArt $folder
    if(-not $source){continue}
    $hash=Get-AlbumHash (Album-Path $folder.FullName)
    $target=Join-Path $cache ($hash+'.RGB')
    if(-not $Force -and (Test-Path -LiteralPath $target) -and
       (Get-Item -LiteralPath $target).Length -eq 8192) {
      $skipped++
      continue
    }
    Save-Rgb565 $source $target
    Write-Host "OK $($folder.Name) => $hash.RGB"
    $written++
  } catch {
    Write-Warning "$($folder.FullName): $_"
    $errors++
  }
}
Write-Host "Cardputer R67 hashed thumbnail cache: created=$written existing=$skipped errors=$errors"
Write-Host "Images are 64x64 RGB565, 8192 bytes each; the music and original PNG/JPG files are untouched."
if($errors -gt 0){exit 1}
