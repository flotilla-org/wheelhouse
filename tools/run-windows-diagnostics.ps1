param(
  # Diagnostics to run, by the name in their --<name>_diagnostics flag.
  # terminal_glyph is left out until colour emoji renders on Windows (issue #59);
  # pass it explicitly to see the current failure.
  [string[]]$Diagnostics = @("sidebar", "scroll_region", "preview", "tooltip", "panel"),
  [string]$Exe = "",
  [int]$TimeoutSeconds = 120
)

# Runs each UI diagnostic in its own process with a throwaway --user/--project,
# prints its output, and exits non-zero if any fail or time out. Build first
# with `build wheelhouse`.

$ErrorActionPreference = "Stop"
# `powershell -File` passes "a,b" as one string; accept comma lists either way.
$Diagnostics = @($Diagnostics | ForEach-Object { $_ -split "," } | Where-Object { $_ })
$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
if([string]::IsNullOrWhiteSpace($Exe)) {
  $Exe = Join-Path $RepoRoot "build\wheelhouse.exe"
}
if(-not (Test-Path $Exe)) {
  throw "Wheelhouse executable not found: $Exe (run build.bat wheelhouse first)"
}
$Exe = (Resolve-Path $Exe).Path

$Results = @()
foreach($Name in $Diagnostics) {
  $WorkDir = Join-Path ([System.IO.Path]::GetTempPath()) ("wheelhouse-diag-" + [System.Guid]::NewGuid().ToString("N"))
  New-Item -ItemType Directory -Force -Path $WorkDir | Out-Null
  $StdOut = Join-Path $WorkDir "stdout.txt"
  $StdErr = Join-Path $WorkDir "stderr.txt"
  $Arguments = @("`"--user:$WorkDir\user`"", "`"--project:$WorkDir\project`"", "--${Name}_diagnostics")

  Write-Host "=== $Name"
  $Timer = [System.Diagnostics.Stopwatch]::StartNew()
  $Process = Start-Process -FilePath $Exe -ArgumentList $Arguments -WorkingDirectory $WorkDir `
    -RedirectStandardOutput $StdOut -RedirectStandardError $StdErr -PassThru -NoNewWindow
  # Touching Handle caches it so ExitCode stays readable after the process exits.
  $null = $Process.Handle
  if($Process.WaitForExit($TimeoutSeconds * 1000)) {
    $Process.WaitForExit()
    $Status = if($Process.ExitCode -eq 0) { "pass" } else { "fail (exit $($Process.ExitCode))" }
  }
  else {
    $Process.Kill()
    $Process.WaitForExit()
    $Status = "fail (timed out after ${TimeoutSeconds}s)"
  }
  $Timer.Stop()

  foreach($Stream in @($StdOut, $StdErr)) {
    if(Test-Path $Stream) {
      $Text = (Get-Content -Raw $Stream)
      if(-not [string]::IsNullOrWhiteSpace($Text)) { Write-Host $Text.TrimEnd() }
    }
  }
  Write-Host "--- ${Name}: $Status ($([int]$Timer.Elapsed.TotalSeconds)s)"
  $Results += [pscustomobject]@{ Diagnostic = $Name; Result = $Status }
  Remove-Item -Recurse -Force $WorkDir -ErrorAction SilentlyContinue
}

$Results | Format-Table -AutoSize | Out-String | Write-Host
$Failed = @($Results | Where-Object { $_.Result -ne "pass" })
if($Failed.Count -ne 0) {
  Write-Host "$($Failed.Count) of $($Results.Count) diagnostics failed."
  exit 1
}
Write-Host "All $($Results.Count) diagnostics passed."
exit 0
