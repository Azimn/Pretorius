param(
  [string]$DemoDir = "",
  [int]$Port = 7797,
  [int]$MaxZipMb = 40,
  [int]$MaxHostWorkingSetMb = 128
)

$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $PSScriptRoot
if (-not $DemoDir) {
  $DemoDir = Join-Path $Root "dist\PersonaConsole_v6_test"
}
$ZipPath = "$DemoDir.zip"

function Fail($Message) { throw "release_check failed: $Message" }
function Require-File($Path) { if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { Fail "missing file: $Path" } }
function Require-Dir($Path) { if (-not (Test-Path -LiteralPath $Path -PathType Container)) { Fail "missing directory: $Path" } }

function Invoke-JsonPost($Uri, $Body) {
  Invoke-WebRequest -Uri $Uri -Method POST -ContentType "application/json" -Body $Body -UseBasicParsing
}

function Remove-TestRuntimeState($CharacterDir) {
  $names = @("aether", "relations", "state.bin", "memory.bin", "chapters.bin", "reflections.bin", "speech_events.bin", "open_loops.bin", "actor_index.bin", "dissonance.bin", "speech_habits.bin")
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

Write-Host "--- PersonaConsole V6 release check ---"
Write-Host "Demo dir: $DemoDir"

Require-Dir $DemoDir
Require-File $ZipPath
Require-File (Join-Path $DemoDir "START_HERE.html")
Require-File (Join-Path $DemoDir "README_FIRST.txt")
Require-File (Join-Path $DemoDir "TESTER_GUIDE.md")
Require-File (Join-Path $DemoDir "Run_Character.ps1")
Require-File (Join-Path $DemoDir "Run_pretorius.cmd")
Require-File (Join-Path $DemoDir "Run_kiki.cmd")
Require-File (Join-Path $DemoDir "Run_friendly.cmd")
Require-File (Join-Path $DemoDir "Run_rival.cmd")
Require-File (Join-Path $DemoDir "Run_quiet.cmd")
Require-File (Join-Path $DemoDir "Run_mentor.cmd")
Require-File (Join-Path $DemoDir "Run_Pretorius_Ollama_Optional.cmd")
Require-File (Join-Path $DemoDir "Stop_Server.cmd")
Require-File (Join-Path $DemoDir "persona_host.exe")
Require-File (Join-Path $DemoDir "cygwin1.dll")
Require-File (Join-Path $DemoDir "cyggcc_s-seh-1.dll")
Require-File (Join-Path $DemoDir "host\web\index.html")
Require-File (Join-Path $DemoDir "host\web\app.js")
Require-File (Join-Path $DemoDir "host\web\style.css")
Require-File (Join-Path $DemoDir "Forge\forge.html")
Require-File (Join-Path $DemoDir "Inspector\cartridge_inspector.html")

foreach ($slug in @("pretorius","kiki","friendly","rival","quiet","mentor")) {
  Require-File (Join-Path $DemoDir ("characters\" + $slug + "\" + $slug + ".cart"))
}

$zipMb = [math]::Round((Get-Item -LiteralPath $ZipPath).Length / 1MB, 2)
Write-Host "Zip size: $zipMb MB"
if ($zipMb -gt $MaxZipMb) { Fail "demo zip is $zipMb MB, over ${MaxZipMb} MB budget" }

$readme = Get-Content -LiteralPath (Join-Path $DemoDir "README_FIRST.txt") -Raw
foreach ($needle in @("no GPU", "internet", "model download", "Ollama", "Cloud/API renderers are not required")) {
  if ($readme -notmatch [regex]::Escape($needle)) { Fail "README_FIRST.txt missing expectation: $needle" }
}

$start = Get-Content -LiteralPath (Join-Path $DemoDir "START_HERE.html") -Raw
foreach ($needle in @("Run_*.cmd", "Forge/forge.html", "Inspector/cartridge_inspector.html", "TESTER_GUIDE.md", "Optional local LLM")) {
  if ($start -notmatch [regex]::Escape($needle)) { Fail "START_HERE.html missing path: $needle" }
}

$hostExe = Join-Path $DemoDir "persona_host.exe"
$cart = Join-Path $DemoDir "characters\pretorius\pretorius.cart"
$charDir = Split-Path -Parent $cart
$webRoot = Join-Path $DemoDir "host\web"
$base = "http://127.0.0.1:$Port"
$proc = $null

try {
  Remove-TestRuntimeState $charDir
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
    } catch { $ready = $false }
  }
  if (-not $ready) { Fail "persona_host did not answer /state on port $Port" }

  $html = Invoke-WebRequest -Uri "$base/" -UseBasicParsing -TimeoutSec 3
  if ($html.StatusCode -ne 200 -or $html.Content -notmatch "PersonaHost - chat" -or $html.Content -notmatch 'id="messages"') {
    Fail "web UI did not serve index.html"
  }

  $chat = Invoke-JsonPost "$base/chat" '{"text":"Remember the brass key."}'
  if ($chat.StatusCode -ne 200 -or $chat.Content -notmatch '"reply"') { Fail "chat endpoint did not return a reply" }

  $idle = Invoke-JsonPost "$base/idle_probe" '{}'
  if ($idle.StatusCode -ne 200 -or $idle.Content -notmatch '"reply"') { Fail "idle_probe endpoint did not return JSON" }

  $proc.Refresh()
  $workingSetMb = [math]::Round($proc.WorkingSet64 / 1MB, 2)
  Write-Host "Host working set: $workingSetMb MB"
  if ($workingSetMb -gt $MaxHostWorkingSetMb) {
    Fail "persona_host working set is $workingSetMb MB, over ${MaxHostWorkingSetMb} MB budget"
  }
}
finally {
  if ($proc -and -not $proc.HasExited) { Stop-Process -Id $proc.Id -Force }
}

$proc = $null
try {
  $proc = Start-Process -FilePath $hostExe `
    -ArgumentList @("--port", "$Port", "--web-root", $webRoot, $cart) `
    -WorkingDirectory $DemoDir -WindowStyle Hidden -PassThru
  Start-Sleep -Milliseconds 900
  $state2 = Invoke-WebRequest -Uri "$base/state" -UseBasicParsing -TimeoutSec 3
  if ($state2.StatusCode -ne 200 -or $state2.Content -notmatch '"turn_count"') {
    Fail "close/reopen state did not load"
  }
}
finally {
  if ($proc -and -not $proc.HasExited) { Stop-Process -Id $proc.Id -Force }
  Remove-TestRuntimeState $charDir
}

Write-Host "PASSED -- V6 demo package is ready for first external testers"
