param(
  # "bundled": the pane must run under the conpty.dll + OpenConsole.exe that
  # build.bat places beside wheelhouse.exe. "inbox": run with CLEAT_CONPTY=inbox
  # and expect the system conhost.exe, to show the check can tell them apart.
  [ValidateSet("bundled", "inbox")]
  [string]$Expect = "bundled",
  [string]$Exe = "",
  [int]$TimeoutSeconds = 60
)

# Opens one in-process Cleat terminal pane in a throwaway Wheelhouse
# (--user/--project in a temporary directory) and reports which ConPTY hosts it.
# Cleat's C ABI does not report the ConPTY it chose for an in-process session,
# so this checks the console host process the pane starts as a child of
# wheelhouse.exe: OpenConsole.exe from beside wheelhouse.exe for the bundle,
# conhost.exe for the inbox ConPTY, which drops Kitty graphics (cleat ADR 0006).
# Daemon-backed panes use the files beside cleat.exe; `cleat inspect` reports them.

$ErrorActionPreference = "Stop"
$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
if([string]::IsNullOrWhiteSpace($Exe)) {
  $Exe = Join-Path $RepoRoot "build\wheelhouse.exe"
}
if(-not (Test-Path $Exe)) {
  throw "Wheelhouse executable not found: $Exe (run build.bat wheelhouse first)"
}
$Exe = (Resolve-Path $Exe).Path
$ExeDir = Split-Path -Parent $Exe

if($Expect -eq "bundled") {
  $Missing = @("conpty.dll", "OpenConsole.exe", "conpty-LICENSE.txt") | Where-Object { -not (Test-Path (Join-Path $ExeDir $_)) }
  if($Missing) {
    Write-Host "FAIL: missing beside wheelhouse.exe: $($Missing -join ', ')"
    exit 1
  }
}

$WorkDir = Join-Path ([System.IO.Path]::GetTempPath()) ("wheelhouse-conpty-" + [System.Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Force -Path $WorkDir | Out-Null
$User = Join-Path $WorkDir "user"
$Project = Join-Path $WorkDir "project"
# One window, one panel, one terminal tab running cmd.exe in-process.
Set-Content -Path $User -Encoding ascii -Value @"
// uishell 0.1.0 user file

window:
{
  size: 900.000000 600.000000
  panels:
  {
    1.0:
    {
      terminal:
      {
        expression: "cmd.exe"
        selected
      }
      selected
    }
  }
}
"@
Set-Content -Path $Project -Encoding ascii -Value "// uishell 0.1.0 project file"

$PreviousOverride = $env:CLEAT_CONPTY
if($Expect -eq "inbox") { $env:CLEAT_CONPTY = "inbox" } else { Remove-Item Env:\CLEAT_CONPTY -ErrorAction SilentlyContinue }
try {
  $Process = Start-Process -FilePath $Exe -ArgumentList @("`"--user:$User`"", "`"--project:$Project`"") -WorkingDirectory $WorkDir -PassThru
}
finally {
  if($null -eq $PreviousOverride) { Remove-Item Env:\CLEAT_CONPTY -ErrorAction SilentlyContinue } else { $env:CLEAT_CONPTY = $PreviousOverride }
}

$Hosts = @()
$Deadline = (Get-Date).AddSeconds($TimeoutSeconds)
try {
  while((Get-Date) -lt $Deadline -and -not $Process.HasExited) {
    $Hosts = @(Get-CimInstance Win32_Process -Filter "ParentProcessId=$($Process.Id)" |
      Where-Object { $_.Name -in @("OpenConsole.exe", "conhost.exe") })
    if($Hosts.Count -gt 0) { break }
    Start-Sleep -Milliseconds 250
  }
}
finally {
  # /T also ends the pane's console host and shell.
  if(-not $Process.HasExited) { & taskkill.exe /PID $Process.Id /T /F | Out-Null }
  Remove-Item -Recurse -Force $WorkDir -ErrorAction SilentlyContinue
}

if($Hosts.Count -eq 0) {
  Write-Host "FAIL: no console host started under wheelhouse.exe within ${TimeoutSeconds}s (exited: $($Process.HasExited))"
  exit 1
}
foreach($ConsoleHost in $Hosts) {
  Write-Host "console host: $($ConsoleHost.Name) $($ConsoleHost.ExecutablePath)"
}
$Bundled = @($Hosts | Where-Object { $_.Name -eq "OpenConsole.exe" -and $_.ExecutablePath -eq (Join-Path $ExeDir "OpenConsole.exe") })
$Inbox = @($Hosts | Where-Object { $_.Name -eq "conhost.exe" })
$Observed = if($Bundled.Count -gt 0 -and $Inbox.Count -eq 0) { "bundled" } elseif($Inbox.Count -gt 0 -and $Bundled.Count -eq 0) { "inbox" } else { "unexpected" }
if($Observed -ne $Expect) {
  Write-Host "FAIL: in-process pane ConPTY is $Observed, expected $Expect"
  exit 1
}
Write-Host "pass: in-process pane ConPTY is $Observed"
exit 0
