[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
$assetRoot = Join-Path $repoRoot 'console\assets'
New-Item -ItemType Directory -Force -Path $assetRoot | Out-Null

function New-DingGraphic {
    param(
        [Parameter(Mandatory)][string]$Path,
        [Parameter(Mandatory)][int]$Width,
        [Parameter(Mandatory)][int]$Height,
        [switch]$Card
    )

    $bitmap = [System.Drawing.Bitmap]::new($Width, $Height, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    try {
        $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
        try {
            $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
            $graphics.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAliasGridFit
            $graphics.Clear([System.Drawing.Color]::FromArgb(11, 15, 12))

            $margin = [Math]::Round([Math]::Min($Width, $Height) * 0.12)
            $markSize = if ($Card) { [Math]::Round($Height * 0.55) } else { $Width - (2 * $margin) }
            $markX = $margin
            $markY = [Math]::Round(($Height - $markSize) / 2)
            $markRect = [System.Drawing.RectangleF]::new($markX, $markY, $markSize, $markSize)
            $markBrush = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(0, 82, 48))
            $accentPen = [System.Drawing.Pen]::new([System.Drawing.Color]::FromArgb(130, 217, 165), [Math]::Max(6, $markSize * 0.035))
            try {
                $graphics.FillEllipse($markBrush, $markRect)
                $graphics.DrawArc($accentPen, $markRect, 32, 296)
            } finally {
                $markBrush.Dispose()
                $accentPen.Dispose()
            }

            $dFont = [System.Drawing.Font]::new('Segoe UI', $markSize * 0.44, [System.Drawing.FontStyle]::Bold, [System.Drawing.GraphicsUnit]::Pixel)
            $dBrush = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(159, 247, 196))
            try {
                $format = [System.Drawing.StringFormat]::new()
                $format.Alignment = [System.Drawing.StringAlignment]::Center
                $format.LineAlignment = [System.Drawing.StringAlignment]::Center
                $graphics.DrawString('D', $dFont, $dBrush, $markRect, $format)
                $format.Dispose()
            } finally {
                $dFont.Dispose()
                $dBrush.Dispose()
            }

            if ($Card) {
                $titleX = $markX + $markSize + [Math]::Round($Height * 0.1)
                $titleWidth = $Width - $titleX - $margin
                $titleFont = [System.Drawing.Font]::new('Segoe UI', $Height * 0.105, [System.Drawing.FontStyle]::Bold, [System.Drawing.GraphicsUnit]::Pixel)
                $bodyFont = [System.Drawing.Font]::new('Segoe UI', $Height * 0.046, [System.Drawing.FontStyle]::Regular, [System.Drawing.GraphicsUnit]::Pixel)
                $titleBrush = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(223, 228, 220))
                $bodyBrush = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(159, 247, 196))
                try {
                    $graphics.DrawString('Ding PBX Console', $titleFont, $titleBrush, [System.Drawing.RectangleF]::new($titleX, $Height * 0.29, $titleWidth, $Height * 0.2))
                    $graphics.DrawString('Guided Asterisk administration', $bodyFont, $bodyBrush, [System.Drawing.RectangleF]::new($titleX, $Height * 0.53, $titleWidth, $Height * 0.12))
                } finally {
                    $titleFont.Dispose(); $bodyFont.Dispose(); $titleBrush.Dispose(); $bodyBrush.Dispose()
                }
            }
        } finally {
            $graphics.Dispose()
        }
        $bitmap.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png)
    } finally {
        $bitmap.Dispose()
    }
}

New-DingGraphic -Path (Join-Path $assetRoot 'icon.png') -Width 512 -Height 512
New-DingGraphic -Path (Join-Path $repoRoot 'social-preview.png') -Width 1200 -Height 630 -Card

Write-Host "Generated console/assets/icon.png and social-preview.png from the committed local brand recipe."
