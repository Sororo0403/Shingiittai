param(
    [string]$FontPath = "$env:USERPROFILE\Downloads\MPLUS1p-ExtraBold.ttf"
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing

$repoRoot = Get-Location

$fonts = New-Object System.Drawing.Text.PrivateFontCollection
$fonts.AddFontFile($FontPath)
$fontFamily = $fonts.Families[0]

$targetLeft = 1
$targetTop = 25
$keyLabelEmptyGap = 10
$itemGap = 28
$fontSizePx = 30
$gold = [System.Drawing.Color]::FromArgb(255, 255, 204, 55)
$white = [System.Drawing.Color]::FromArgb(255, 255, 255, 255)
$shadow = [System.Drawing.Color]::FromArgb(170, 0, 0, 0)

function Get-TextBounds {
    param(
        [System.Drawing.Font]$Font,
        [string]$Text,
        [single]$X,
        [single]$Y
    )

    $bmp = New-Object System.Drawing.Bitmap(900, 160, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $graphics = [System.Drawing.Graphics]::FromImage($bmp)
    $graphics.Clear([System.Drawing.Color]::Transparent)
    $graphics.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAliasGridFit
    $brush = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::White)
    $graphics.DrawString($Text, $Font, $brush, $X, $Y)

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

    $brush.Dispose()
    $graphics.Dispose()
    $bmp.Dispose()
    return [pscustomobject]@{
        MinX = $minX
        MinY = $minY
        MaxX = $maxX
        MaxY = $maxY
    }
}

function New-PromptImage {
    param(
        [object[]]$Items,
        [string]$Path,
        [int]$CanvasWidth = 520,
        [int]$CanvasHeight = 74,
        [int[]]$LineTops = @(25),
        [int]$FontSizePx = $script:fontSizePx,
        [switch]$TrimToContent,
        [int]$TrimPadding = 6
    )

    $font = New-Object System.Drawing.Font($script:fontFamily, $FontSizePx, [System.Drawing.FontStyle]::Regular, [System.Drawing.GraphicsUnit]::Pixel)

    $bmp = New-Object System.Drawing.Bitmap($CanvasWidth, $CanvasHeight, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $graphics = [System.Drawing.Graphics]::FromImage($bmp)
    $graphics.Clear([System.Drawing.Color]::Transparent)
    $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
    $graphics.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAliasGridFit
    $shadowBrush = New-Object System.Drawing.SolidBrush($script:shadow)
    $keyBrush = New-Object System.Drawing.SolidBrush($script:gold)
    $labelBrush = New-Object System.Drawing.SolidBrush($script:white)

    $lineIndex = 0
    $cursorX = [single]$script:targetLeft
    foreach ($item in $Items) {
        if ($item -eq $null) {
            $lineIndex++
            $cursorX = [single]$script:targetLeft
            continue
        }

        $key = [string]$item.Key
        $label = [string]$item.Label
        $labelBoundsAtOrigin = Get-TextBounds -Font $font -Text $label -X 0 -Y 0
        if ([string]::IsNullOrEmpty($key)) {
            $keyBoundsAtOrigin = $null
            $minYAtOrigin = $labelBoundsAtOrigin.MinY
        } else {
            $keyBoundsAtOrigin = Get-TextBounds -Font $font -Text $key -X 0 -Y 0
            $minYAtOrigin = [Math]::Min($keyBoundsAtOrigin.MinY, $labelBoundsAtOrigin.MinY)
        }
        $textY = [single]($LineTops[$lineIndex] - $minYAtOrigin)
        if ($keyBoundsAtOrigin -eq $null) {
            $labelX = [single]($cursorX - $labelBoundsAtOrigin.MinX)
        } else {
            $keyX = [single]($cursorX - $keyBoundsAtOrigin.MinX)
            $keyBounds = Get-TextBounds -Font $font -Text $key -X $keyX -Y $textY
            $labelX = [single]($keyBounds.MaxX + $script:keyLabelEmptyGap + 1 - $labelBoundsAtOrigin.MinX)
            $graphics.DrawString($key, $font, $shadowBrush, [single]($keyX + 2.0), [single]($textY + 2.0))
            $graphics.DrawString($key, $font, $keyBrush, $keyX, $textY)
        }
        $labelBounds = Get-TextBounds -Font $font -Text $label -X $labelX -Y $textY

        $graphics.DrawString($label, $font, $shadowBrush, [single]($labelX + 2.0), [single]($textY + 2.0))
        $graphics.DrawString($label, $font, $labelBrush, $labelX, $textY)

        $cursorX = [single]($labelBounds.MaxX + $script:itemGap + 1)
    }

    if ($TrimToContent) {
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

        if ($maxX -ge $minX -and $maxY -ge $minY) {
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
    }

    $resolvedPath = Join-Path $script:repoRoot $Path
    New-Item -ItemType Directory -Force -Path (Split-Path $resolvedPath -Parent) | Out-Null
    $bmp.Save($resolvedPath, [System.Drawing.Imaging.ImageFormat]::Png)

    $shadowBrush.Dispose()
    $keyBrush.Dispose()
    $labelBrush.Dispose()
    $graphics.Dispose()
    $bmp.Dispose()
    $font.Dispose()
}

function New-TriangleKeyImage {
    param(
        [string]$Letter,
        [string]$Path,
        [ValidateSet("Left", "Right")]
        [string]$Direction
    )

    $canvas = 128
    $bmp = New-Object System.Drawing.Bitmap($canvas, $canvas, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $graphics = [System.Drawing.Graphics]::FromImage($bmp)
    $graphics.Clear([System.Drawing.Color]::Transparent)
    $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
    $graphics.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAliasGridFit

    $outlineColor = [System.Drawing.Color]::FromArgb(220, 255, 204, 55)
    $fillColor = [System.Drawing.Color]::FromArgb(34, 255, 204, 55)
    $letterColor = [System.Drawing.Color]::FromArgb(255, 255, 255, 255)
    $shadowColor = [System.Drawing.Color]::FromArgb(180, 0, 0, 0)
    $pen = New-Object System.Drawing.Pen($outlineColor, 5.0)
    $fillBrush = New-Object System.Drawing.SolidBrush($fillColor)
    $letterBrush = New-Object System.Drawing.SolidBrush($letterColor)
    $shadowBrush = New-Object System.Drawing.SolidBrush($shadowColor)
    $font = New-Object System.Drawing.Font($script:fontFamily, 56, [System.Drawing.FontStyle]::Regular, [System.Drawing.GraphicsUnit]::Pixel)

    if ($Direction -eq "Left") {
        $points = @(
            [System.Drawing.PointF]::new(22.0, 64.0),
            [System.Drawing.PointF]::new(102.0, 18.0),
            [System.Drawing.PointF]::new(102.0, 110.0)
        )
    } else {
        $points = @(
            [System.Drawing.PointF]::new(106.0, 64.0),
            [System.Drawing.PointF]::new(26.0, 18.0),
            [System.Drawing.PointF]::new(26.0, 110.0)
        )
    }
    $graphics.FillPolygon($fillBrush, $points)
    $graphics.DrawPolygon($pen, $points)

    $format = New-Object System.Drawing.StringFormat
    $format.Alignment = [System.Drawing.StringAlignment]::Center
    $format.LineAlignment = [System.Drawing.StringAlignment]::Center
    $rect = [System.Drawing.RectangleF]::new(0.0, 3.0, [single]$canvas, [single]($canvas - 6))
    $shadowRect = [System.Drawing.RectangleF]::new(2.0, 5.0, [single]$canvas, [single]($canvas - 6))
    $graphics.DrawString($Letter, $font, $shadowBrush, $shadowRect, $format)
    $graphics.DrawString($Letter, $font, $letterBrush, $rect, $format)

    $resolvedPath = Join-Path $script:repoRoot $Path
    New-Item -ItemType Directory -Force -Path (Split-Path $resolvedPath -Parent) | Out-Null
    $bmp.Save($resolvedPath, [System.Drawing.Imaging.ImageFormat]::Png)

    $format.Dispose()
    $font.Dispose()
    $shadowBrush.Dispose()
    $letterBrush.Dispose()
    $fillBrush.Dispose()
    $pen.Dispose()
    $graphics.Dispose()
    $bmp.Dispose()
}

$selectControls = @(
    @{ Key = "A/D"; Label = "選択" },
    @{ Key = "SPACE"; Label = "決定" }
)
$tabRanking = @(@{ Key = "TAB"; Label = "ランキング" })
$tabSelect = @(@{ Key = "TAB"; Label = "セレクト" })
$tabMenu = @(@{ Key = "TAB"; Label = "メニュー" })
$tabCredit = @(@{ Key = "TAB"; Label = "クレジット" })
$tabTitle = @(@{ Key = "TAB"; Label = "タイトル" })
$tutorialExit = @(@{ Key = "ESC"; Label = "やめる" })
$soundTestControls = @(
    @{ Key = "A/D"; Label = "選曲" },
    @{ Key = "SPACE"; Label = "再生" }
)
$resultClear = @(
    @{ Key = "TAB"; Label = "リトライ" },
    @{ Key = "SPACE"; Label = "タイトル" }
)
$resultGameOver = @(
    @{ Key = "SPACE"; Label = "リトライ" },
    @{ Key = "TAB"; Label = "タイトル" }
)
$resultHand = @(
    @{ Key = "SWING x3"; Label = "RETRY" },
    $null,
    @{ Key = "KEEP STILL"; Label = "TITLE" }
)

New-PromptImage -Items $selectControls -Path "app\resources\ui\weapon_select\text\weapon_controls.png"
New-PromptImage -Items $selectControls -Path "app\resources\ui\tutorial_select\text\controls.png"
New-PromptImage -Items $tabRanking -Path "app\resources\ui\common\tab_ranking.png"
New-PromptImage -Items $tabSelect -Path "app\resources\ui\common\tab_select.png"
New-PromptImage -Items $tabMenu -Path "app\resources\ui\common\tab_menu.png"
New-PromptImage -Items $tabCredit -Path "app\resources\ui\title\tab_credit.png"
New-PromptImage -Items $tutorialExit -Path "app\resources\ui\title\esc_exit.png"
New-PromptImage -Items $tabTitle -Path "app\resources\ui\credits\credits_controls.png"
New-PromptImage -Items $tutorialExit -Path "app\resources\ui\tutorial_dynamic\exit.png"
New-PromptImage -Items @(@{ Key = ""; Label = "メニュー" }) -Path "app\resources\ui\menu\menu_title.png" -CanvasWidth 160
New-PromptImage -Items @(@{ Key = ""; Label = "ランキング" }) -Path "app\resources\ui\menu\option_ranking.png" -CanvasWidth 190
New-PromptImage -Items @(@{ Key = ""; Label = "サウンドテスト" }) -Path "app\resources\ui\menu\option_sound_test.png" -CanvasWidth 260
New-PromptImage -Items @(@{ Key = ""; Label = "クレジット" }) -Path "app\resources\ui\menu\option_credits.png" -CanvasWidth 190
New-PromptImage -Items @(@{ Key = ""; Label = "サウンドテスト" }) -Path "app\resources\ui\sound_test\title.png" -CanvasWidth 380 -CanvasHeight 96 -LineTops @(30) -FontSizePx 42 -TrimToContent -TrimPadding 8
New-PromptImage -Items $soundTestControls -Path "app\resources\ui\sound_test\controls.png" -CanvasWidth 760
New-PromptImage -Items @(@{ Key = ""; Label = "再生中" }) -Path "app\resources\ui\sound_test\playing.png" -CanvasWidth 150 -TrimToContent
New-TriangleKeyImage -Letter "A" -Path "app\resources\ui\sound_test\key_a.png" -Direction "Left"
New-TriangleKeyImage -Letter "D" -Path "app\resources\ui\sound_test\key_d.png" -Direction "Right"
New-PromptImage -Items @(@{ Key = ""; Label = "タイトルテーマ" }) -Path "app\resources\ui\sound_test\track_title.png" -CanvasWidth 280 -TrimToContent
New-PromptImage -Items @(@{ Key = ""; Label = "メニューテーマ" }) -Path "app\resources\ui\sound_test\track_menu.png" -CanvasWidth 280 -TrimToContent
New-PromptImage -Items @(@{ Key = ""; Label = "チュートリアルテーマ" }) -Path "app\resources\ui\sound_test\track_tutorial.png" -CanvasWidth 390 -TrimToContent
New-PromptImage -Items @(@{ Key = ""; Label = "バトルテーマ" }) -Path "app\resources\ui\sound_test\track_battle.png" -CanvasWidth 260 -TrimToContent
New-PromptImage -Items $resultClear -Path "app\resources\ui\result\mplus\controls_kbm_clear.png"
New-PromptImage -Items $resultGameOver -Path "app\resources\ui\result\mplus\controls_kbm_gameover.png"
New-PromptImage -Items $resultHand -Path "app\resources\ui\result\mplus\controls_hand.png" -CanvasHeight 122 -LineTops @(25, 70)

$fonts.Dispose()
