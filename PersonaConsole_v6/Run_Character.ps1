param(
  [ValidateSet("pretorius","kiki","friendly","rival","quiet","mentor")]
  [string]$Character = "pretorius",
  [ValidateSet("template","ollama")]
  [string]$Renderer = "template",
  [string]$OllamaModel = "gemma2:2b",
  [int]$Port = 7777
)

$ErrorActionPreference = "Stop"
$Here = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location -LiteralPath $Here
$env:PATH = "$Here;C:\cygwin64\bin;$env:PATH"

$hostExe = Join-Path $Here "build\persona_host.exe"
$webRoot = Join-Path $Here "bridges\web"
$pidFile = Join-Path $Here ".persona_host.pid"
$url = "http://127.0.0.1:$Port/"

$cartMap = @{
  pretorius = "profiles\pretorius\pretorius.cart"
  kiki      = "profiles\kiki\kiki.cart"
  friendly  = "profiles\friendly\friendly.cart"
  rival     = "profiles\rival\rival.cart"
  quiet     = "profiles\quiet\quiet.cart"
  mentor    = "profiles\mentor\mentor.cart"
}
$cart = Join-Path $Here $cartMap[$Character]

if (-not (Test-Path -LiteralPath $hostExe)) {
  throw "Missing build\persona_host.exe. Build first with: make build/persona_host cartridges"
}
if (-not (Test-Path -LiteralPath $cart)) {
  throw "Missing cartridge: $cart. Build first with: make cartridges"
}

function Test-PortOpen([int]$PortNumber) {
  try {
    $client = [System.Net.Sockets.TcpClient]::new()
    $iar = $client.BeginConnect("127.0.0.1", $PortNumber, $null, $null)
    $ok = $iar.AsyncWaitHandle.WaitOne(250)
    if ($ok) { $client.EndConnect($iar) }
    $client.Close()
    return $ok
  } catch {
    return $false
  }
}

if ($Renderer -eq "template") {
  $env:PE_RENDER_BACKEND = "template"
  Remove-Item Env:\PE_SLM_PROVIDER -ErrorAction SilentlyContinue
} else {
  $env:PE_RENDER_BACKEND = "slm"
  $env:PE_SLM_PROVIDER = "ollama"
  $env:PE_SLM_MODEL = $OllamaModel
  $env:PE_OLLAMA_MODEL = $OllamaModel
  $env:PE_OLLAMA_HOST = "127.0.0.1"
  $env:PE_OLLAMA_PORT = "11434"
  $env:PE_OLLAMA_TEMP = "0"
  $env:PE_OLLAMA_NUM_PRED = "160"
}

if (Test-PortOpen $Port) {
  Start-Process $url
  Write-Host "PersonaConsole is already running on $url"
  exit 0
}

$args = @("--port", "$Port", "--web-root", $webRoot, $cart)
$proc = Start-Process -FilePath $hostExe -ArgumentList $args -WindowStyle Hidden -PassThru
Set-Content -LiteralPath $pidFile -Value $proc.Id
Start-Sleep -Seconds 1
Start-Process $url

Write-Host "PersonaConsole V6 started on $url"
Write-Host "Character: $Character"
Write-Host "Renderer:  $Renderer"
if ($Renderer -eq "ollama") {
  Write-Host "Ollama must already be running locally with model: $OllamaModel"
  Write-Host "If Ollama is unavailable, the engine falls back to deterministic templates."
}
Write-Host "To stop it, run Stop_Server.ps1 or Stop_Server.cmd"
