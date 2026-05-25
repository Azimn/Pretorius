param(
  [string]$OutDir = ""
)

$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $PSScriptRoot
if (-not $OutDir) {
  $OutDir = Join-Path $Root "dist\PersonaConsole_v5_demo"
}
$ZipPath = "$OutDir.zip"

function Copy-RequiredFile($From, $To) {
  if (-not (Test-Path -LiteralPath $From)) {
    throw "Missing required file: $From"
  }
  $parent = Split-Path -Parent $To
  if ($parent -and -not (Test-Path -LiteralPath $parent)) {
    New-Item -ItemType Directory -Force -Path $parent | Out-Null
  }
  Copy-Item -LiteralPath $From -Destination $To -Force
}

function Write-Utf8NoBom($Path, $Text) {
  $parent = Split-Path -Parent $Path
  if ($parent -and -not (Test-Path -LiteralPath $parent)) {
    New-Item -ItemType Directory -Force -Path $parent | Out-Null
  }
  [System.IO.File]::WriteAllText($Path, $Text, [System.Text.UTF8Encoding]::new($false))
}

if (Test-Path -LiteralPath $OutDir) {
  Remove-Item -LiteralPath $OutDir -Recurse -Force
}
if (Test-Path -LiteralPath $ZipPath) {
  Remove-Item -LiteralPath $ZipPath -Force
}
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

Copy-RequiredFile (Join-Path $Root "build\persona_host.exe") (Join-Path $OutDir "persona_host.exe")
Copy-RequiredFile "C:\cygwin64\bin\cygwin1.dll" (Join-Path $OutDir "cygwin1.dll")
Copy-RequiredFile "C:\cygwin64\bin\cyggcc_s-seh-1.dll" (Join-Path $OutDir "cyggcc_s-seh-1.dll")

Copy-RequiredFile (Join-Path $Root "profiles\pretorius\pretorius.cart") (Join-Path $OutDir "characters\pretorius\pretorius.cart")
Copy-RequiredFile (Join-Path $Root "profiles\kiki\kiki.cart") (Join-Path $OutDir "characters\kiki\kiki.cart")

Copy-Item -LiteralPath (Join-Path $Root "bridges\web") -Destination (Join-Path $OutDir "host\web") -Recurse -Force

Copy-RequiredFile (Join-Path $Root "CartridgeForge\forge.html") (Join-Path $OutDir "Forge\forge.html")
Copy-RequiredFile (Join-Path $Root "CartridgeForge\README.md") (Join-Path $OutDir "Forge\README.md")
Copy-RequiredFile (Join-Path $Root "CartridgeInspector\cartridge_inspector.html") (Join-Path $OutDir "Inspector\cartridge_inspector.html")
if (Test-Path -LiteralPath (Join-Path $Root "CartridgeInspector\README.md")) {
  Copy-RequiredFile (Join-Path $Root "CartridgeInspector\README.md") (Join-Path $OutDir "Inspector\README.md")
}
Copy-RequiredFile (Join-Path $Root "docs\EXTERNAL_TESTER_GUIDE.md") (Join-Path $OutDir "TESTER_GUIDE.md")

$runTemplate = @'
param(
  [int]$Port = 7777
)

$ErrorActionPreference = "Stop"
$Here = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location -LiteralPath $Here
$env:PATH = "$Here;$env:PATH"

$cart = "__CART__"
$hostExe = Join-Path $Here "persona_host.exe"
$pidFile = Join-Path $Here ".persona_host.pid"
$url = "http://127.0.0.1:$Port/"

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

if (Test-PortOpen $Port) {
  Start-Process $url
  Write-Host "PersonaConsole is already running on $url"
  exit 0
}

$args = @("--port", "$Port", "--web-root", "host/web", $cart)
$proc = Start-Process -FilePath $hostExe -ArgumentList $args -WindowStyle Hidden -PassThru
Set-Content -LiteralPath $pidFile -Value $proc.Id
Start-Sleep -Seconds 1
Start-Process $url

Write-Host "PersonaConsole started on $url"
Write-Host "To stop it, run Stop_Server.ps1"
'@

Write-Utf8NoBom (Join-Path $OutDir "Run_Pretorius.ps1") ($runTemplate.Replace("__CART__", "characters/pretorius/pretorius.cart"))
Write-Utf8NoBom (Join-Path $OutDir "Run_Kiki.ps1") ($runTemplate.Replace("__CART__", "characters/kiki/kiki.cart"))

$stopScript = @'
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
'@
Write-Utf8NoBom (Join-Path $OutDir "Stop_Server.ps1") $stopScript

$cmdPretorius = '@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Run_Pretorius.ps1"
pause
'
$cmdKiki = '@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Run_Kiki.ps1"
pause
'
$cmdStop = '@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Stop_Server.ps1"
pause
'
Write-Utf8NoBom (Join-Path $OutDir "Run_Pretorius.cmd") $cmdPretorius
Write-Utf8NoBom (Join-Path $OutDir "Run_Kiki.cmd") $cmdKiki
Write-Utf8NoBom (Join-Path $OutDir "Stop_Server.cmd") $cmdStop

$startHere = @'
<!doctype html>
<meta charset="utf-8">
<title>PersonaConsole V5 Demo</title>
<style>
  body{font-family:Segoe UI,Arial,sans-serif;margin:0;background:#101114;color:#f2eee7}
  main{max-width:860px;margin:0 auto;padding:40px 22px}
  h1{font-size:34px;margin:0 0 8px}
  p{line-height:1.55;color:#cfc8bd}
  .grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(230px,1fr));gap:14px;margin-top:24px}
  a,.card{display:block;border:1px solid #39342f;background:#191a1e;border-radius:8px;padding:16px;color:#f2eee7;text-decoration:none}
  a:hover{border-color:#9f8664}
  code{background:#24262b;padding:2px 5px;border-radius:4px}
  .muted{color:#9c9489}
</style>
<main>
  <h1>PersonaConsole V5 Demo</h1>
  <p>Start with <code>Run_Pretorius.cmd</code> or <code>Run_Kiki.cmd</code>. The script starts the local host and opens the chat at <code>http://127.0.0.1:7777/</code>.</p>
  <div class="grid">
    <a href="Forge/forge.html"><strong>Open Cartridge Forge</strong><br><span class="muted">Create or edit a V5 character cartridge.</span></a>
    <a href="Inspector/cartridge_inspector.html"><strong>Open Cartridge Inspector</strong><br><span class="muted">Check a cartridge before sharing it.</span></a>
    <a href="TESTER_GUIDE.md"><strong>Tester Guide</strong><br><span class="muted">Prompts and feedback questions for real-user testing.</span></a>
    <div class="card"><strong>Stop the server</strong><br><span class="muted">Run <code>Stop_Server.cmd</code> when finished.</span></div>
  </div>
  <p>Each character stores its own local memory beside its cartridge in <code>characters/</code>. This demo is local-first: no account, no network service, no GPU, and no LLM are required for the included cartridges.</p>
</main>
'@
Write-Utf8NoBom (Join-Path $OutDir "START_HERE.html") $startHere

$readme = @'
PersonaConsole V5 Demo

Fast start:
1. Double-click Run_Pretorius.cmd.
2. Your browser should open to http://127.0.0.1:7777/.
3. Chat with the character.
4. Run Stop_Server.cmd when finished.

Other files:
- Run_Kiki.cmd starts the Kiki cartridge.
- Forge/forge.html builds V5 cartridges in the browser.
- Inspector/cartridge_inspector.html checks cartridge health.
- TESTER_GUIDE.md has prompts and a feedback template.
- characters/ contains the included Pretorius and Kiki cartridges.

Hardware:
- Included cartridges run without an LLM and need only a tiny CPU/RAM footprint.
- No GPU, account, or internet connection is required after you have this folder.
'@
Write-Utf8NoBom (Join-Path $OutDir "README_FIRST.txt") $readme

Compress-Archive -LiteralPath $OutDir -DestinationPath $ZipPath -Force
Write-Host "Demo folder: $OutDir"
Write-Host "Demo zip:    $ZipPath"
