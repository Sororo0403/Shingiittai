param(
    [string]$FontPath = "$env:USERPROFILE\Downloads\MPLUS1p-ExtraBold.ttf"
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$fonts = New-Object System.Drawing.Text.PrivateFontCollection
$fonts.AddFontFile($FontPath)
$fontFamily = $fonts.Families[0]

$white = [System.Drawing.Color]::FromArgb(255, 255, 255, 255)
$gold = [System.Drawing.Color]::FromArgb(255, 255, 204, 55)
$green = [System.Drawing.Color]::FromArgb(255, 90, 255, 130)
$red = [System.Drawing.Color]::FromArgb(255, 255, 86, 76)
$shadow = [System.Drawing.Color]::FromArgb(185, 0, 0, 0)

function New-TutorialTextImage {
    param(
        [string]$Text,
        [string]$Path,
        [System.Drawing.Color]$Color = $script:white,
        [int]$FontSizePx = 50,
        [int]$CanvasWidth = 1120,
        [int]$CanvasHeight = 96,
        [int]$TrimPadding = 10,
        [switch]$NoTrim
    )

    $font = New-Object System.Drawing.Font($script:fontFamily, $FontSizePx, [System.Drawing.FontStyle]::Regular, [System.Drawing.GraphicsUnit]::Pixel)
    $bmp = New-Object System.Drawing.Bitmap($CanvasWidth, $CanvasHeight, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $graphics = [System.Drawing.Graphics]::FromImage($bmp)
    $graphics.Clear([System.Drawing.Color]::Transparent)
    $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
    $graphics.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAliasGridFit

    $format = New-Object System.Drawing.StringFormat
    $format.Alignment = [System.Drawing.StringAlignment]::Center
    $format.LineAlignment = [System.Drawing.StringAlignment]::Center
    $rect = [System.Drawing.RectangleF]::new(0.0, 0.0, [single]$CanvasWidth, [single]$CanvasHeight)
    $shadowRect = [System.Drawing.RectangleF]::new(3.0, 3.0, [single]$CanvasWidth, [single]$CanvasHeight)
    $shadowBrush = New-Object System.Drawing.SolidBrush($script:shadow)
    $brush = New-Object System.Drawing.SolidBrush($Color)
    $graphics.DrawString($Text, $font, $shadowBrush, $shadowRect, $format)
    $graphics.DrawString($Text, $font, $brush, $rect, $format)

    $minX = $bmp.Width
    $minY = $bmp.Height
    $maxX = -1
    $maxY = -1
    for ($py = 0; $py -lt $bmp.Height; $py++) {
        for ($px = 0; $px -lt $bmp.Width; $px++) {
            $pixel = $bmp.GetPixel($px, $py)
            if ($pixel.A -gt 12) {
                if ($px -lt $minX) { $minX = $px }
                if ($py -lt $minY) { $minY = $py }
                if ($px -gt $maxX) { $maxX = $px }
                if ($py -gt $maxY) { $maxY = $py }
            }
        }
    }

    if (!$NoTrim -and $maxX -ge $minX -and $maxY -ge $minY) {
        $trimLeft = [Math]::Max(0, $minX - $TrimPadding)
        $trimTop = [Math]::Max(0, $minY - $TrimPadding)
        $trimRight = [Math]::Min($bmp.Width - 1, $maxX + $TrimPadding)
        $trimBottom = [Math]::Min($bmp.Height - 1, $maxY + $TrimPadding)
        $trimW = $trimRight - $trimLeft + 1
        $trimH = $trimBottom - $trimTop + 1
        $trimmed = New-Object System.Drawing.Bitmap($trimW, $trimH, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
        $trimGraphics = [System.Drawing.Graphics]::FromImage($trimmed)
        $trimGraphics.Clear([System.Drawing.Color]::Transparent)
        $srcRect = [System.Drawing.Rectangle]::new($trimLeft, $trimTop, $trimW, $trimH)
        $dstRect = [System.Drawing.Rectangle]::new(0, 0, $trimW, $trimH)
        $trimGraphics.DrawImage($bmp, $dstRect, $srcRect, [System.Drawing.GraphicsUnit]::Pixel)
        $trimGraphics.Dispose()
        $bmp.Dispose()
        $bmp = $trimmed
    }

    $resolvedPath = Join-Path $script:repoRoot $Path
    New-Item -ItemType Directory -Force -Path (Split-Path $resolvedPath -Parent) | Out-Null
    $bmp.Save($resolvedPath, [System.Drawing.Imaging.ImageFormat]::Png)

    $brush.Dispose()
    $shadowBrush.Dispose()
    $format.Dispose()
    $graphics.Dispose()
    $bmp.Dispose()
    $font.Dispose()
}

$items = @(
    @{ Path = "app\resources\ui\tutorial_dynamic\left_right_kbm.png"; Text = "W/A/S/Dで左の剣を3回振ろう"; Color = $white },
    @{ Path = "app\resources\ui\tutorial_dynamic\right_sword_kbm.png"; Text = "マウスを動かして右の剣を3回振ろう"; Color = $white },
    @{ Path = "app\resources\ui\tutorial_dynamic\left_right_hand.png"; Text = "左手の剣を3回振ろう"; Color = $white },
    @{ Path = "app\resources\ui\tutorial_dynamic\right_sword_hand.png"; Text = "右手の剣を3回振ろう"; Color = $white },
    @{ Path = "app\resources\ui\tutorial_dynamic\wait.png"; Text = "赤い線が出ている間は待とう"; Color = $red },
    @{ Path = "app\resources\ui\tutorial_dynamic\release.png"; Text = "緑になったら斬ろう"; Color = $green },
    @{ Path = "app\resources\ui\tutorial_dynamic\vertical.png"; Text = "縦線に合わせて斬ろう"; Color = $white },
    @{ Path = "app\resources\ui\tutorial_dynamic\horizontal.png"; Text = "横線に合わせて斬ろう"; Color = $white },
    @{ Path = "app\resources\ui\tutorial_dynamic\success.png"; Text = "成功"; Color = $gold },
    @{ Path = "app\resources\ui\tutorial_dynamic\miss.png"; Text = "もう一度"; Color = $red },
    @{ Path = "app\resources\ui\tutorial_dynamic\complete.png"; Text = "実戦練習"; Color = $gold },
    @{ Path = "app\resources\ui\tutorial_dynamic\excellent.png"; Text = "EXCELLENT!"; Color = $green; FontSizePx = 64; CanvasHeight = 120 }
)

foreach ($item in $items) {
    $fontSize = if ($item.ContainsKey("FontSizePx")) { $item.FontSizePx } else { 50 }
    $width = if ($item.ContainsKey("CanvasWidth")) { $item.CanvasWidth } else { 1120 }
    $height = if ($item.ContainsKey("CanvasHeight")) { $item.CanvasHeight } else { 96 }
    New-TutorialTextImage -Text $item.Text -Path $item.Path -Color $item.Color -FontSizePx $fontSize -CanvasWidth $width -CanvasHeight $height -NoTrim:($item.ContainsKey("NoTrim") -and $item.NoTrim)
}

$fonts.Dispose()
