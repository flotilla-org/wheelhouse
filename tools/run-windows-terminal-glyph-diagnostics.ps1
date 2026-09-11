param(
  [string]$FixturePpm = $env:WHEELHOUSE_WINDOWS_D3D11_FIXTURE_PPM,
  [string[]]$BuildArgs = @(),
  [switch]$ValidateOnly
)

$ErrorActionPreference = "Stop"

function Test-PpmWhitespace {
  param([byte]$Byte)
  return ($Byte -eq 32 -or $Byte -eq 9 -or $Byte -eq 10 -or $Byte -eq 13)
}

function Read-PpmToken {
  param(
    [byte[]]$Bytes,
    [ref]$Offset
  )

  while($true) {
    while($Offset.Value -lt $Bytes.Length -and (Test-PpmWhitespace $Bytes[$Offset.Value])) {
      $Offset.Value += 1
    }
    if($Offset.Value -lt $Bytes.Length -and $Bytes[$Offset.Value] -eq [byte][char]'#') {
      while($Offset.Value -lt $Bytes.Length -and $Bytes[$Offset.Value] -ne 10 -and $Bytes[$Offset.Value] -ne 13) {
        $Offset.Value += 1
      }
      continue
    }
    break
  }

  if($Offset.Value -ge $Bytes.Length) {
    throw "Unexpected end of PPM header"
  }

  $Start = $Offset.Value
  while($Offset.Value -lt $Bytes.Length -and -not (Test-PpmWhitespace $Bytes[$Offset.Value])) {
    $Offset.Value += 1
  }
  if($Offset.Value -eq $Start) {
    throw "Empty token in PPM header"
  }

  return [System.Text.Encoding]::ASCII.GetString($Bytes, $Start, $Offset.Value - $Start)
}

function Assert-TerminalFixturePpm {
  param([string]$Path)

  if(-not (Test-Path $Path)) {
    throw "Expected fixture PPM was not written: $Path"
  }

  $Bytes = [System.IO.File]::ReadAllBytes($Path)
  if($Bytes.Length -lt 16) {
    throw "Fixture PPM is too small: $Path"
  }

  $Offset = 0
  $Magic = Read-PpmToken -Bytes $Bytes -Offset ([ref]$Offset)
  $WidthToken = Read-PpmToken -Bytes $Bytes -Offset ([ref]$Offset)
  $HeightToken = Read-PpmToken -Bytes $Bytes -Offset ([ref]$Offset)
  $MaxToken = Read-PpmToken -Bytes $Bytes -Offset ([ref]$Offset)

  if($Magic -ne "P6") {
    throw "Fixture PPM did not start with P6 header: $Path"
  }
  if($MaxToken -ne "255") {
    throw "Fixture PPM has unsupported max value '$MaxToken': $Path"
  }

  $Width = [int]$WidthToken
  $Height = [int]$HeightToken
  if($Width -le 0 -or $Height -le 0) {
    throw "Fixture PPM dimensions are invalid: ${Width}x${Height}: $Path"
  }

  if($Offset -ge $Bytes.Length -or -not (Test-PpmWhitespace $Bytes[$Offset])) {
    throw "Fixture PPM is missing whitespace after the header: $Path"
  }
  while($Offset -lt $Bytes.Length -and (Test-PpmWhitespace $Bytes[$Offset])) {
    $Offset += 1
  }

  $ExpectedPixelBytes = [int64]$Width * [int64]$Height * 3
  $ActualPixelBytes = [int64]$Bytes.Length - [int64]$Offset
  if($ActualPixelBytes -ne $ExpectedPixelBytes) {
    throw "Fixture PPM pixel payload size mismatch: expected $ExpectedPixelBytes bytes for ${Width}x${Height}, got $ActualPixelBytes: $Path"
  }

  return @{
    Width = $Width
    Height = $Height
    PixelBytes = $ActualPixelBytes
    PixelOffset = $Offset
    Bytes = $Bytes
  }
}

function New-FixtureRect {
  param(
    [int]$CellWidth,
    [int]$CellHeight,
    [int]$Col0,
    [int]$Row0,
    [int]$Col1,
    [int]$Row1
  )

  return @{
    X0 = $CellWidth * $Col0
    Y0 = $CellHeight * $Row0
    X1 = $CellWidth * $Col1
    Y1 = $CellHeight * $Row1
  }
}

function Get-RectArea {
  param([hashtable]$Rect)
  return [Math]::Max(0, $Rect.X1 - $Rect.X0) * [Math]::Max(0, $Rect.Y1 - $Rect.Y0)
}

function Get-FixturePixelClassCount {
  param(
    [hashtable]$PpmInfo,
    [hashtable]$Rect,
    [string]$Class
  )

  $Width = [int]$PpmInfo.Width
  $Height = [int]$PpmInfo.Height
  $Bytes = [byte[]]$PpmInfo.Bytes
  $PixelOffset = [int64]$PpmInfo.PixelOffset
  $X0 = [Math]::Max(0, [Math]::Min($Width, [int]$Rect.X0))
  $X1 = [Math]::Max(0, [Math]::Min($Width, [int]$Rect.X1))
  $Y0 = [Math]::Max(0, [Math]::Min($Height, [int]$Rect.Y0))
  $Y1 = [Math]::Max(0, [Math]::Min($Height, [int]$Rect.Y1))
  $Count = 0
  for($Y = $Y0; $Y -lt $Y1; $Y += 1) {
    $RowOffset = $PixelOffset + [int64]$Y * [int64]$Width * 3
    for($X = $X0; $X -lt $X1; $X += 1) {
      $Idx = $RowOffset + [int64]$X * 3
      $R = [int]$Bytes[$Idx]
      $G = [int]$Bytes[$Idx + 1]
      $B = [int]$Bytes[$Idx + 2]
      $Max = [Math]::Max($R, [Math]::Max($G, $B))
      $Min = [Math]::Min($R, [Math]::Min($G, $B))
      $Match = $false
      switch($Class) {
        "visible"   { $Match = ($Max -gt 32) }
        "dark"      { $Match = ($Max -le 32) }
        "light"     { $Match = ($R -ge 96 -and $G -ge 96 -and $B -ge 96) }
        "green"     { $Match = ($G -ge 96 -and $G -gt $R + 16 -and $G -gt $B + 16) }
        "red"       { $Match = ($R -ge 96 -and $R -gt $G + 16 -and $R -gt $B + 16) }
        "chromatic" { $Match = ($Max -gt 32 -and $Max -gt $Min + 16) }
        default     { throw "Unknown fixture pixel class: $Class" }
      }
      if($Match) {
        $Count += 1
      }
    }
  }
  return $Count
}

function Get-FixtureVisibleBounds {
  param(
    [hashtable]$PpmInfo,
    [hashtable]$Rect
  )

  $Width = [int]$PpmInfo.Width
  $Height = [int]$PpmInfo.Height
  $Bytes = [byte[]]$PpmInfo.Bytes
  $PixelOffset = [int64]$PpmInfo.PixelOffset
  $X0 = [Math]::Max(0, [Math]::Min($Width, [int]$Rect.X0))
  $X1 = [Math]::Max(0, [Math]::Min($Width, [int]$Rect.X1))
  $Y0 = [Math]::Max(0, [Math]::Min($Height, [int]$Rect.Y0))
  $Y1 = [Math]::Max(0, [Math]::Min($Height, [int]$Rect.Y1))
  $MinX = $Width
  $MinY = $Height
  $MaxX = -1
  $MaxY = -1
  for($Y = $Y0; $Y -lt $Y1; $Y += 1) {
    $RowOffset = $PixelOffset + [int64]$Y * [int64]$Width * 3
    for($X = $X0; $X -lt $X1; $X += 1) {
      $Idx = $RowOffset + [int64]$X * 3
      $R = [int]$Bytes[$Idx]
      $G = [int]$Bytes[$Idx + 1]
      $B = [int]$Bytes[$Idx + 2]
      if([Math]::Max($R, [Math]::Max($G, $B)) -gt 32) {
        $MinX = [Math]::Min($MinX, $X)
        $MinY = [Math]::Min($MinY, $Y)
        $MaxX = [Math]::Max($MaxX, $X + 1)
        $MaxY = [Math]::Max($MaxY, $Y + 1)
      }
    }
  }
  if($MaxX -lt $MinX -or $MaxY -lt $MinY) {
    return @{ HasPixels = $false; X0 = 0; Y0 = 0; X1 = 0; Y1 = 0 }
  }
  return @{ HasPixels = $true; X0 = $MinX; Y0 = $MinY; X1 = $MaxX; Y1 = $MaxY }
}

function Test-FixtureBoundsMatch {
  param(
    [hashtable]$Reference,
    [hashtable]$Candidate,
    [int]$Tolerance = 1
  )

  return ($Reference.HasPixels -and
          $Candidate.HasPixels -and
          [Math]::Abs([int]$Reference.Y0 - [int]$Candidate.Y0) -le $Tolerance -and
          [Math]::Abs([int]$Reference.Y1 - [int]$Candidate.Y1) -le $Tolerance)
}

function Assert-TerminalFixtureSemantics {
  param([hashtable]$PpmInfo)

  $Cols = 80
  $Rows = 24
  $Width = [int]$PpmInfo.Width
  $Height = [int]$PpmInfo.Height
  if($Width % $Cols -ne 0 -or $Height % $Rows -ne 0) {
    throw "Fixture dimensions ${Width}x${Height} are not divisible by grid ${Cols}x${Rows}"
  }
  $CellWidth = [int]($Width / $Cols)
  $CellHeight = [int]($Height / $Rows)

  $WholeRect = @{ X0 = 0; Y0 = 0; X1 = $Width; Y1 = $Height }
  $TitleRect = New-FixtureRect $CellWidth $CellHeight 0 0 80 1
  $AsciiRect = New-FixtureRect $CellWidth $CellHeight 0 1 48 2
  $EmptyBgRect = New-FixtureRect $CellWidth $CellHeight 64 1 78 2
  $BlocksRect = New-FixtureRect $CellWidth $CellHeight 8 9 31 10
  $RedTextRect = New-FixtureRect $CellWidth $CellHeight 0 16 22 17
  $EmojiRect = New-FixtureRect $CellWidth $CellHeight 12 14 14 15
  $AlignNormalRect = New-FixtureRect $CellWidth $CellHeight 8 4 12 5
  $AlignColorRect = New-FixtureRect $CellWidth $CellHeight 16 4 20 5
  $AlignBoldRect = New-FixtureRect $CellWidth $CellHeight 24 4 28 5
  $AlignFaintRect = New-FixtureRect $CellWidth $CellHeight 32 4 36 5

  $Visible = Get-FixturePixelClassCount $PpmInfo $WholeRect "visible"
  $TitleVisible = Get-FixturePixelClassCount $PpmInfo $TitleRect "visible"
  $AsciiLight = Get-FixturePixelClassCount $PpmInfo $AsciiRect "light"
  $EmptyBgDark = Get-FixturePixelClassCount $PpmInfo $EmptyBgRect "dark"
  $BlocksGreen = Get-FixturePixelClassCount $PpmInfo $BlocksRect "green"
  $RedText = Get-FixturePixelClassCount $PpmInfo $RedTextRect "red"
  $EmojiVisible = Get-FixturePixelClassCount $PpmInfo $EmojiRect "visible"
  $EmojiChromatic = Get-FixturePixelClassCount $PpmInfo $EmojiRect "chromatic"
  $AlignNormal = Get-FixtureVisibleBounds $PpmInfo $AlignNormalRect
  $AlignColor = Get-FixtureVisibleBounds $PpmInfo $AlignColorRect
  $AlignBold = Get-FixtureVisibleBounds $PpmInfo $AlignBoldRect
  $AlignFaint = Get-FixtureVisibleBounds $PpmInfo $AlignFaintRect
  $EmptyBgArea = Get-RectArea $EmptyBgRect
  $AlignOk = ((Test-FixtureBoundsMatch $AlignNormal $AlignColor) -and
              (Test-FixtureBoundsMatch $AlignNormal $AlignBold) -and
              (Test-FixtureBoundsMatch $AlignNormal $AlignFaint))

  $Failures = @()
  if($Visible -lt 128) { $Failures += "visible pixel count too low: $Visible" }
  if($TitleVisible -lt 16) { $Failures += "title visible pixel count too low: $TitleVisible" }
  if($AsciiLight -lt 32) { $Failures += "ASCII light pixel count too low: $AsciiLight" }
  if($EmptyBgDark -lt [int64]($EmptyBgArea * 9 / 10)) { $Failures += "empty background dark pixels too low: $EmptyBgDark/$EmptyBgArea" }
  if($BlocksGreen -lt 16) { $Failures += "green block pixels too low: $BlocksGreen" }
  if($RedText -lt 16) { $Failures += "red text pixels too low: $RedText" }
  if($EmojiVisible -lt 4) { $Failures += "emoji visible pixels too low: $EmojiVisible" }
  if($EmojiChromatic -lt 4) { $Failures += "emoji chromatic pixels too low: $EmojiChromatic" }
  if(-not $AlignOk) {
    $Failures += "styled alignment bounds mismatch: normal=$($AlignNormal.X0),$($AlignNormal.Y0),$($AlignNormal.X1),$($AlignNormal.Y1) color=$($AlignColor.X0),$($AlignColor.Y0),$($AlignColor.X1),$($AlignColor.Y1) bold=$($AlignBold.X0),$($AlignBold.Y0),$($AlignBold.X1),$($AlignBold.Y1) faint=$($AlignFaint.X0),$($AlignFaint.Y0),$($AlignFaint.X1),$($AlignFaint.Y1)"
  }
  if($Failures.Count -ne 0) {
    throw "Fixture semantic validation failed: $($Failures -join '; ')"
  }

  return @{
    CellWidth = $CellWidth
    CellHeight = $CellHeight
    Visible = $Visible
    EmojiChromatic = $EmojiChromatic
  }
}

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot = Resolve-Path (Join-Path $ScriptDir "..")
Set-Location $RepoRoot

if([string]::IsNullOrWhiteSpace($FixturePpm)) {
  $FixturePpm = Join-Path $RepoRoot "local\screenshots\windows-d3d11-terminal-fixture.ppm"
}
elseif(-not [System.IO.Path]::IsPathRooted($FixturePpm)) {
  $FixturePpm = Join-Path $RepoRoot $FixturePpm
}

if($ValidateOnly) {
  $PpmInfo = Assert-TerminalFixturePpm -Path $FixturePpm
  $SemanticInfo = Assert-TerminalFixtureSemantics -PpmInfo $PpmInfo
  Write-Host "Fixture validation passed."
  Write-Host "Fixture PPM: $FixturePpm ($($PpmInfo.Width)x$($PpmInfo.Height), $($PpmInfo.PixelBytes) pixel bytes)"
  Write-Host "Fixture semantics: cell=$($SemanticInfo.CellWidth)x$($SemanticInfo.CellHeight), visible=$($SemanticInfo.Visible), emoji_chroma=$($SemanticInfo.EmojiChromatic)"
  exit 0
}

$FixtureDir = Split-Path -Parent $FixturePpm
if(-not [string]::IsNullOrWhiteSpace($FixtureDir)) {
  New-Item -ItemType Directory -Force -Path $FixtureDir | Out-Null
}

$BuildScript = Join-Path $RepoRoot "build.bat"
& $BuildScript "wheelhouse" @BuildArgs
if($LASTEXITCODE -ne 0) {
  throw "build.bat wheelhouse failed with exit code $LASTEXITCODE"
}

$UIShellExe = Join-Path $RepoRoot "build\wheelhouse.exe"
if(-not (Test-Path $UIShellExe)) {
  throw "Expected build output was not found: $UIShellExe"
}

& $UIShellExe "--terminal_glyph_diagnostics"
if($LASTEXITCODE -ne 0) {
  throw "terminal glyph diagnostics failed with exit code $LASTEXITCODE"
}

& $UIShellExe "--terminal_glyph_fixture_ppm:$FixturePpm"
if($LASTEXITCODE -ne 0) {
  throw "terminal glyph fixture PPM generation failed with exit code $LASTEXITCODE"
}

$PpmInfo = Assert-TerminalFixturePpm -Path $FixturePpm
$SemanticInfo = Assert-TerminalFixtureSemantics -PpmInfo $PpmInfo

Write-Host "Windows terminal glyph diagnostics passed."
Write-Host "Fixture PPM: $FixturePpm ($($PpmInfo.Width)x$($PpmInfo.Height), $($PpmInfo.PixelBytes) pixel bytes)"
Write-Host "Fixture semantics: cell=$($SemanticInfo.CellWidth)x$($SemanticInfo.CellHeight), visible=$($SemanticInfo.Visible), emoji_chroma=$($SemanticInfo.EmojiChromatic)"
