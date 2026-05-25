param(
  [string]$DemoDir = "",
  [int]$Port = 7797,
  [int]$MaxZipMb = 25,
  [int]$MaxHostWorkingSetMb = 96
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
Require-File (Join-Path $DemoDir "Collect_Diagnostics.cmd")
Require-File (Join-Path $DemoDir "Collect_Diagnostics.ps1")
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

$readme = Get-Content -LiteralPath (Join-Path $DemoDir "README_FIRST.txt") -Raw
if ($readme -notmatch "No GPU" -or $readme -notmatch "internet") {
  Fail "README_FIRST.txt must state low-hardware/no-internet expectations"
}
if ($readme -notmatch "Collect_Diagnostics") {
  Fail "README_FIRST.txt must mention diagnostics collection"
}

$testerGuide = Get-Content -LiteralPath (Join-Path $DemoDir "TESTER_GUIDE.md") -Raw
if ($testerGuide -notmatch "Most alive moment" -or $testerGuide -notmatch "Most fake moment" -or $testerGuide -notmatch "Pretorius felt") {
  Fail "TESTER_GUIDE.md must include structured feedback prompts"
}
$testerGuideHtml = Get-Content -LiteralPath (Join-Path $DemoDir "TESTER_GUIDE.html") -Raw
if ($testerGuideHtml -notmatch "Most alive moment" -or $testerGuideHtml -notmatch "Most fake moment" -or $testerGuideHtml -notmatch "Pretorius felt") {
  Fail "TESTER_GUIDE.html must include structured feedback prompts"
}

$start = Get-Content -LiteralPath (Join-Path $DemoDir "START_HERE.html") -Raw
if ($start -notmatch "Run_Pretorius\.cmd" -or $start -notmatch "Forge/forge\.html" -or $start -notmatch "Inspector/cartridge_inspector\.html" -or $start -notmatch "TESTER_GUIDE\.html" -or $start -notmatch "Collect_Diagnostics\.cmd") {
  Fail "START_HERE.html does not expose first-run, Forge, and Inspector paths"
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
