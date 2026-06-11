param(
  [string]$OutDir = ""
)

$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $PSScriptRoot
if (-not $OutDir) {
  $OutDir = Join-Path $Root "dist\PersonaConsole_v6_test"
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

if (Test-Path -LiteralPath $OutDir) { Remove-Item -LiteralPath $OutDir -Recurse -Force }
if (Test-Path -LiteralPath $ZipPath) { Remove-Item -LiteralPath $ZipPath -Force }
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

Copy-RequiredFile (Join-Path $Root "build\persona_host.exe") (Join-Path $OutDir "persona_host.exe")
Copy-RequiredFile "C:\cygwin64\bin\cygwin1.dll" (Join-Path $OutDir "cygwin1.dll")
Copy-RequiredFile "C:\cygwin64\bin\cyggcc_s-seh-1.dll" (Join-Path $OutDir "cyggcc_s-seh-1.dll")

$characters = @(
  @{Slug="pretorius"; Name="Dr. Pretorius"; Source="profiles\pretorius\pretorius.cart"},
  @{Slug="kiki"; Name="Kiki"; Source="profiles\kiki\kiki.cart"},
  @{Slug="friendly"; Name="Mira"; Source="profiles\friendly\friendly.cart"},
  @{Slug="rival"; Name="Cassian Vale"; Source="profiles\rival\rival.cart"},
  @{Slug="quiet"; Name="Eli Rowan"; Source="profiles\quiet\quiet.cart"},
  @{Slug="mentor"; Name="Marin Hale"; Source="profiles\mentor\mentor.cart"}
)

foreach ($c in $characters) {
  Copy-RequiredFile (Join-Path $Root $c.Source) (Join-Path $OutDir ("characters\" + $c.Slug + "\" + $c.Slug + ".cart"))
}

Copy-Item -LiteralPath (Join-Path $Root "bridges\web") -Destination (Join-Path $OutDir "host\web") -Recurse -Force
Copy-RequiredFile (Join-Path $Root "CartridgeForge\forge.html") (Join-Path $OutDir "Forge\forge.html")
Copy-RequiredFile (Join-Path $Root "CartridgeForge\README.md") (Join-Path $OutDir "Forge\README.md")
Copy-RequiredFile (Join-Path $Root "CartridgeInspector\cartridge_inspector.html") (Join-Path $OutDir "Inspector\cartridge_inspector.html")
if (Test-Path -LiteralPath (Join-Path $Root "CartridgeInspector\README.md")) {
  Copy-RequiredFile (Join-Path $Root "CartridgeInspector\README.md") (Join-Path $OutDir "Inspector\README.md")
}

$docCopies = @(
  @{From="docs\EXTERNAL_TESTER_GUIDE.md"; To="TESTER_GUIDE.md"},
  @{From="docs\FIRST_RUN_DEMO_V6.md"; To="docs\FIRST_RUN_DEMO_V6.md"},
  @{From="docs\DEMO_CARTRIDGE_PACK_V6.md"; To="docs\DEMO_CARTRIDGE_PACK_V6.md"},
  @{From="docs\BELIEVABILITY_BATTERY_V6.md"; To="docs\BELIEVABILITY_BATTERY_V6.md"},
  @{From="docs\MICRO_MODE_V6.md"; To="docs\MICRO_MODE_V6.md"},
  @{From="docs\IMPORTED_PERSONA_FIDELITY_V6.md"; To="docs\IMPORTED_PERSONA_FIDELITY_V6.md"}
)
foreach ($d in $docCopies) {
  Copy-RequiredFile (Join-Path $Root $d.From) (Join-Path $OutDir $d.To)
}

$launcher = @'
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
$env:PATH = "$Here;$env:PATH"

$hostExe = Join-Path $Here "persona_host.exe"
$cart = Join-Path $Here ("characters\" + $Character + "\" + $Character + ".cart")
$pidFile = Join-Path $Here ".persona_host.pid"
$url = "http://127.0.0.1:$Port/"

if (-not (Test-Path -LiteralPath $cart)) {
  throw "Missing cartridge: $cart"
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
} elseif ($Renderer -eq "ollama") {
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

$args = @("--port", "$Port", "--web-root", "host/web", $cart)
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
'@
Write-Utf8NoBom (Join-Path $OutDir "Run_Character.ps1") $launcher

$stopScript = @'
$ErrorActionPreference = "SilentlyContinue"
$Here = Split-Path -Parent $MyInvocation.MyCommand.Path
$pidFile = Join-Path $Here ".persona_host.pid"
if (Test-Path -LiteralPath $pidFile) {
  $pidValue = Get-Content -LiteralPath $pidFile | Select-Object -First 1
  if ($pidValue) { Stop-Process -Id ([int]$pidValue) -Force }
  Remove-Item -LiteralPath $pidFile -Force
  Write-Host "PersonaConsole stopped."
} else {
  Get-Process persona_host -ErrorAction SilentlyContinue | Stop-Process -Force
  Write-Host "Stopped any persona_host process that was running."
}
'@
Write-Utf8NoBom (Join-Path $OutDir "Stop_Server.ps1") $stopScript

foreach ($c in $characters) {
  $cmd = "@echo off`r`npowershell.exe -NoProfile -ExecutionPolicy Bypass -File ""%~dp0Run_Character.ps1"" -Character " + $c.Slug + " -Renderer template`r`npause`r`n"
  Write-Utf8NoBom (Join-Path $OutDir ("Run_" + $c.Slug + ".cmd")) $cmd
}
$cmdOllama = '@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Run_Character.ps1" -Character pretorius -Renderer ollama -OllamaModel gemma2:2b
pause
'
$cmdStop = '@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Stop_Server.ps1"
pause
'
Write-Utf8NoBom (Join-Path $OutDir "Run_Pretorius_Ollama_Optional.cmd") $cmdOllama
Write-Utf8NoBom (Join-Path $OutDir "Stop_Server.cmd") $cmdStop

$startHere = @'
<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>PersonaConsole V6 Test Build</title>
<style>
body{font-family:Segoe UI,Arial,sans-serif;margin:0;background:#101114;color:#f2eee7}
main{max-width:920px;margin:0 auto;padding:40px 22px}
h1{font-size:34px;margin:0 0 8px}
p{line-height:1.55;color:#cfc8bd}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(230px,1fr));gap:14px;margin-top:24px}
a,.card{display:block;border:1px solid #39342f;background:#191a1e;border-radius:8px;padding:16px;color:#f2eee7;text-decoration:none}
a:hover{border-color:#9f8664}
code{background:#24262b;padding:2px 5px;border-radius:4px}
.muted{color:#9c9489}
</style>
</head>
<body>
<main>
  <h1>PersonaConsole V6 Test Build</h1>
  <p>Start with any <code>Run_*.cmd</code> file. Template mode is the default and needs no GPU, account, internet, model download, or cloud service.</p>
  <div class="grid">
    <div class="card"><strong>Characters</strong><br><span class="muted">Pretorius, Kiki, Mira, Cassian, Eli, and Marin are included in <code>characters/</code>.</span></div>
    <a href="Forge/forge.html"><strong>Open Cartridge Forge</strong><br><span class="muted">Create or inspect V6 cartridge authoring data.</span></a>
    <a href="Inspector/cartridge_inspector.html"><strong>Open Cartridge Inspector</strong><br><span class="muted">Check cartridge structure before sharing.</span></a>
    <a href="TESTER_GUIDE.md"><strong>Tester Guide</strong><br><span class="muted">Prompts and feedback questions for real-user testing.</span></a>
    <div class="card"><strong>Optional local LLM</strong><br><span class="muted">Run <code>Run_Pretorius_Ollama_Optional.cmd</code> after starting Ollama locally. Templates remain the fallback.</span></div>
    <div class="card"><strong>Stop the server</strong><br><span class="muted">Run <code>Stop_Server.cmd</code> when finished.</span></div>
  </div>
  <p>Close and reopen the same character to test local memory persistence. Runtime sidecars stay beside each cartridge on your disk.</p>
</main>
</body>
</html>
'@
Write-Utf8NoBom (Join-Path $OutDir "START_HERE.html") $startHere

$readme = @'
PersonaConsole V6 Test Build

Fast start:
1. Double-click Run_pretorius.cmd, Run_kiki.cmd, Run_friendly.cmd, Run_rival.cmd, Run_quiet.cmd, or Run_mentor.cmd.
2. Your browser should open to http://127.0.0.1:7777/.
3. Talk to the character.
4. Run Stop_Server.cmd when finished.
5. Reopen the same character and ask what they remember.

Included characters:
- Pretorius: sardonic gothic scientist.
- Kiki: affectionate late-80s/90s vernacular with modern competence.
- Mira: friendly companion.
- Cassian Vale: rival antagonist.
- Eli Rowan: quiet restrained character.
- Marin Hale: practical mentor.

Optional local LLM:
- Run_Pretorius_Ollama_Optional.cmd uses PE_RENDER_BACKEND=slm and PE_SLM_PROVIDER=ollama.
- Ollama must already be running locally.
- If Ollama is unavailable, PersonaConsole falls back to deterministic templates.
- Cloud/API renderers are not required and are not enabled in this release-candidate binary.

Hardware:
- Template mode needs no GPU, account, internet, model download, Node runtime, or cloud service.
- The included cartridges are designed for very low hardware requirements.
'@
Write-Utf8NoBom (Join-Path $OutDir "README_FIRST.txt") $readme

Compress-Archive -LiteralPath $OutDir -DestinationPath $ZipPath -Force
Write-Host "Demo folder: $OutDir"
Write-Host "Demo zip:    $ZipPath"
