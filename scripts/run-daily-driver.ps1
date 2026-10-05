# Native Wheelhouse + the remote Flotilla catalog. Python owns lifecycle/cleanup.
$ErrorActionPreference = 'Stop'
$launcher = Join-Path $PSScriptRoot '..\tools\daily-driver.py'
if ($env:WHEELHOUSE_PYTHON_BIN) {
    & $env:WHEELHOUSE_PYTHON_BIN $launcher @args
} elseif (Get-Command py -ErrorAction SilentlyContinue) {
    & py -3 $launcher @args
} else {
    & python $launcher @args
}
exit $LASTEXITCODE
