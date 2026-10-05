# Native Wheelhouse + the remote Flotilla catalog. Python owns lifecycle/cleanup.
$ErrorActionPreference = 'Stop'
$launcher = Join-Path $PSScriptRoot '..\tools\daily-driver.py'
if ($env:WHEELHOUSE_PYTHON_BIN) {
    $pythonExecutable = $env:WHEELHOUSE_PYTHON_BIN
} elseif (Get-Command py -ErrorAction SilentlyContinue) {
    # Resolve once so py.exe does not sit between PowerShell and the owner.
    $pythonExecutable = & py -3 -c 'import sys; print(sys.executable)'
    if ($LASTEXITCODE -ne 0) { throw 'Cannot select a Python 3 interpreter with py.exe' }
} else {
    $pythonExecutable = 'python'
}
& $pythonExecutable $launcher @args
exit $LASTEXITCODE
