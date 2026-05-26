param(
  [string]$DemoDir = "",
  [int]$Port = 7797,
  [int]$MaxZipMb = 25,
  [int]$MaxHostWorkingSetMb = 64
)

$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $PSScriptRoot
if (-not $DemoDir) {
  $DemoDir = Join-Path $Root "dist\PersonaConsole_v5_demo"
}
$ZipPath = "$DemoDir.zip"

function Fail($Message) {
  throw "release_check failed: $Message"
}

function Require-File($Path) {
  if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
    Fail "missing file: $Path"
  }
}

function Require-Dir($Path) {
  if (-not (Test-Path -LiteralPath $Path -PathType Container)) {
    Fail "missing directory: $Path"
  }
}

function Invoke-JsonPost($Uri, $Body) {
  Invoke-WebRequest -Uri $Uri -Method POST -ContentType "application/json" -Body $Body -UseBasicParsing
}

function Remove-TestRuntimeState($CharacterDir) {
  $names = @("aether", "relations", "state.bin", "memory.bin", "chapters.bin", "reflections.bin")
  foreach ($name in $names) {
    $target = Join-Path $CharacterDir $name
    if (Test-Path -LiteralPath $target) {
      $resolved = (Resolve-Path -LiteralPath $target).Path
      $base = (Resolve-Path -LiteralPath $CharacterDir).Path
      if (-not $resolved.StartsWith($base)) {
        Fail "refusing to clean runtime state outside character directory: $resolved"
      }
      Remove-Item -LiteralPath $resolved -Recurse -Force
    }
  }
}

Write-Host "--- PersonaConsole release check ---"
Write-Host "Demo dir: $DemoDir"

Require-Dir $DemoDir
Require-File $ZipPath
Require-File (Join-Path $DemoDir "START_HERE.html")
Require-File (Join-Path $DemoDir "README_FIRST.txt")
Require-File (Join-Path $DemoDir "TESTER_GUIDE.md")
Require-File (Join-Path $DemoDir "TESTER_GUIDE.html")
Require-File (Join-Path $DemoDir "Run_Pretorius.cmd")
Require-File (Join-Path $DemoDir "Run_Kiki.cmd")
Require-File (Join-Path $DemoDir "Stop_Server.cmd")
Require-File (Join-Path $DemoDir "Health_Check.cmd")
Require-File (Join-Path $DemoDir "Health_Check.ps1")
Require-File (Join-Path $DemoDir "Collect_Diagnostics.cmd")
Require-File (Join-Path $DemoDir "Collect_Diagnostics.ps1")
Require-File (Join-Path $DemoDir "Reset_Demo_State.cmd")
Require-File (Join-Path $DemoDir "Reset_Demo_State.ps1")
Require-File (Join-Path $DemoDir "persona_host.exe")
Require-File (Join-Path $DemoDir "cygwin1.dll")
Require-File (Join-Path $DemoDir "cyggcc_s-seh-1.dll")
Require-File (Join-Path $DemoDir "characters\pretorius\pretorius.cart")
Require-File (Join-Path $DemoDir "characters\kiki\kiki.cart")
Require-File (Join-Path $DemoDir "host\web\index.html")
Require-File (Join-Path $DemoDir "host\web\app.js")
Require-File (Join-Path $DemoDir "host\web\style.css")
Require-File (Join-Path $DemoDir "Forge\forge.html")
Require-File (Join-Path $DemoDir "Inspector\cartridge_inspector.html")

$zipMb = [math]::Round((Get-Item -LiteralPath $ZipPath).Length / 1MB, 2)
Write-Host "Zip size: $zipMb MB"
if ($zipMb -gt $MaxZipMb) {
  Fail "demo zip is $zipMb MB, over ${MaxZipMb} MB budget"
}

$unzipDir = Join-Path ([System.IO.Path]::GetTempPath()) ("PersonaConsole_release_unzip_" + [System.Guid]::NewGuid().ToString("N"))
try {
  Expand-Archive -LiteralPath $ZipPath -DestinationPath $unzipDir -Force
  $unzippedDemo = Join-Path $unzipDir (Split-Path -Leaf $DemoDir)
  if (-not (Test-Path -LiteralPath $unzippedDemo -PathType Container)) {
    $children = Get-ChildItem -LiteralPath $unzipDir -Directory
    if ($children.Count -eq 1) { $unzippedDemo = $children[0].FullName }
  }
  foreach ($rel in @(
    "START_HERE.html",
    "Run_Pretorius.cmd",
    "Run_Kiki.cmd",
    "Stop_Server.cmd",
    "Health_Check.cmd",
    "persona_host.exe",
    "cygwin1.dll",
    "characters\pretorius\pretorius.cart",
    "host\web\index.html",
    "Forge\forge.html",
    "Inspector\cartridge_inspector.html"
  )) {
    if (-not (Test-Path -LiteralPath (Join-Path $unzippedDemo $rel))) {
      Fail "clean unzip is missing $rel"
    }
  }
} finally {
  if (Test-Path -LiteralPath $unzipDir) {
    Remove-Item -LiteralPath $unzipDir -Recurse -Force
  }
}

$readme = Get-Content -LiteralPath (Join-Path $DemoDir "README_FIRST.txt") -Raw
if ($readme -notmatch "No GPU" -or $readme -notmatch "internet" -or $readme -notmatch "WSL") {
  Fail "README_FIRST.txt must state low-hardware/no-internet expectations"
}
if ($readme -notmatch "Collect_Diagnostics") {
  Fail "README_FIRST.txt must mention diagnostics collection"
}
if ($readme -notmatch "Health_Check") {
  Fail "README_FIRST.txt must mention the one-click health check"
}
if ($readme -notmatch "Reset_Demo_State") {
  Fail "README_FIRST.txt must mention resetting local state"
}

$testerGuide = Get-Content -LiteralPath (Join-Path $DemoDir "TESTER_GUIDE.md") -Raw
if ($testerGuide -notmatch "Most alive moment" -or $testerGuide -notmatch "Most fake moment" -or $testerGuide -notmatch "Pretorius felt") {
  Fail "TESTER_GUIDE.md must include structured feedback prompts"
}
if ($testerGuide -notmatch "export transcript" -or $testerGuide -notmatch "Collect_Diagnostics.cmd.*does not include chat text") {
  Fail "TESTER_GUIDE.md must explain opt-in transcript export vs diagnostics privacy"
}
$testerGuideHtml = Get-Content -LiteralPath (Join-Path $DemoDir "TESTER_GUIDE.html") -Raw
if ($testerGuideHtml -notmatch "Most alive moment" -or $testerGuideHtml -notmatch "Most fake moment" -or $testerGuideHtml -notmatch "Pretorius felt") {
  Fail "TESTER_GUIDE.html must include structured feedback prompts"
}
if ($testerGuideHtml -notmatch "export transcript" -or $testerGuideHtml -notmatch "Collect_Diagnostics.cmd.*does not include chat text") {
  Fail "TESTER_GUIDE.html must explain opt-in transcript export vs diagnostics privacy"
}

$start = Get-Content -LiteralPath (Join-Path $DemoDir "START_HERE.html") -Raw
if ($start -notmatch "Run_Pretorius\.cmd" -or $start -notmatch "No account" -or $start -notmatch "WSL" -or $start -notmatch "Forge/forge\.html" -or $start -notmatch "Inspector/cartridge_inspector\.html" -or $start -notmatch "TESTER_GUIDE\.html" -or $start -notmatch "Health_Check\.cmd" -or $start -notmatch "Collect_Diagnostics\.cmd" -or $start -notmatch "Reset_Demo_State\.cmd") {
  Fail "START_HERE.html does not expose first-run, Forge, and Inspector paths"
}

$webIndex = Get-Content -LiteralPath (Join-Path $DemoDir "host\web\index.html") -Raw
$webApp = Get-Content -LiteralPath (Join-Path $DemoDir "host\web\app.js") -Raw
if ($webIndex -notmatch "export-transcript" -or $webApp -notmatch "PersonaConsole_Transcript_" -or $webApp -notmatch "Review it before sharing") {
  Fail "web UI must include opt-in transcript export with privacy wording"
}

$hostExe = Join-Path $DemoDir "persona_host.exe"
$cart = Join-Path $DemoDir "characters\pretorius\pretorius.cart"
$charDir = Split-Path -Parent $cart
$webRoot = Join-Path $DemoDir "host\web"
$base = "http://127.0.0.1:$Port"
$proc = $null

try {
  $proc = Start-Process -FilePath $hostExe `
    -ArgumentList @("--port", "$Port", "--web-root", $webRoot, $cart) `
    -WorkingDirectory $DemoDir -WindowStyle Hidden -PassThru

  $ready = $false
  for ($i = 0; $i -lt 30; $i++) {
    Start-Sleep -Milliseconds 250
    try {
      $state = Invoke-WebRequest -Uri "$base/state" -UseBasicParsing -TimeoutSec 2
      if ($state.StatusCode -eq 200 -and $state.Content -match '"mood"') {
        $ready = $true
        break
      }
    } catch {
      $ready = $false
    }
  }
  if (-not $ready) {
    Fail "persona_host did not answer /state on port $Port"
  }

  $html = Invoke-WebRequest -Uri "$base/" -UseBasicParsing -TimeoutSec 3
  if ($html.StatusCode -ne 200 -or $html.Content -notmatch "PersonaHost - chat" -or $html.Content -notmatch 'id="messages"') {
    Fail "web UI did not serve index.html"
  }

  $chat = Invoke-JsonPost "$base/chat" '{"text":"Good morning, Doctor."}'
  if ($chat.StatusCode -ne 200 -or $chat.Content -notmatch '"reply"') {
    Fail "chat endpoint did not return a reply"
  }

  $idle = Invoke-JsonPost "$base/idle_probe" '{}'
  if ($idle.StatusCode -ne 200 -or $idle.Content -notmatch '"reply"') {
    Fail "idle_probe endpoint did not return JSON"
  }

  powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $DemoDir "Collect_Diagnostics.ps1") | Out-Null
  $diagPath = Join-Path $DemoDir "PersonaConsole_Diagnostics.txt"
  Require-File $diagPath
  $diag = Get-Content -LiteralPath $diagPath -Raw
  if ($diag -notmatch "Privacy note" -or $diag -notmatch "persona_host working set MB") {
    Fail "diagnostics output is missing expected metadata/privacy text"
  }
  if ($diag -match "Good morning, Doctor") {
    Fail "diagnostics output appears to include chat text"
  }

  powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $DemoDir "Health_Check.ps1") -Port ($Port + 1) | Out-Null
  $healthPath = Join-Path $DemoDir "PersonaConsole_Health_Check.txt"
  Require-File $healthPath
  $health = Get-Content -LiteralPath $healthPath -Raw
  if ($health -notmatch "PASSED: PersonaConsole demo is working" -or $health -notmatch "Privacy note") {
    Fail "health check output is missing pass/privacy text"
  }

  powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $DemoDir "Reset_Demo_State.ps1") | Out-Null
  if (Test-Path -LiteralPath $diagPath) {
    Fail "reset script did not remove diagnostics output"
  }
  if (Test-Path -LiteralPath $healthPath) {
    Fail "reset script did not remove health check output"
  }
  foreach ($stateFile in @("state.bin", "memory.bin", "chapters.bin", "reflections.bin")) {
    if (Test-Path -LiteralPath (Join-Path $charDir $stateFile)) {
      Fail "reset script did not remove $stateFile"
    }
  }
  foreach ($stateDir in @("aether", "relations")) {
    if (Test-Path -LiteralPath (Join-Path $charDir $stateDir)) {
      Fail "reset script did not remove $stateDir"
    }
  }
  Require-File $cart

  $proc.Refresh()
  $workingSetMb = [math]::Round($proc.WorkingSet64 / 1MB, 2)
  Write-Host "Host working set: $workingSetMb MB"
  if ($workingSetMb -gt $MaxHostWorkingSetMb) {
    Fail "persona_host working set is $workingSetMb MB, over ${MaxHostWorkingSetMb} MB budget"
  }
}
finally {
  if ($proc -and -not $proc.HasExited) {
    Stop-Process -Id $proc.Id -Force
  }
  Remove-TestRuntimeState $charDir
}

Write-Host "PASSED -- demo package is shippable for first external testers"
