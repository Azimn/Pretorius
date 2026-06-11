$ErrorActionPreference = "SilentlyContinue"
$Here = Split-Path -Parent $MyInvocation.MyCommand.Path
$pidFile = Join-Path $Here ".persona_host.pid"

if (Test-Path -LiteralPath $pidFile) {
  $pidValue = Get-Content -LiteralPath $pidFile | Select-Object -First 1
  if ($pidValue) {
    Stop-Process -Id ([int]$pidValue) -Force
  }
  Remove-Item -LiteralPath $pidFile -Force
  Write-Host "PersonaConsole stopped."
} else {
  Get-Process persona_host -ErrorAction SilentlyContinue | Stop-Process -Force
  Write-Host "Stopped any persona_host process that was running."
}
