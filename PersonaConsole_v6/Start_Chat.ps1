param(
  [ValidateSet("pretorius","kiki","r0r1","friendly","rival","quiet","mentor")]
  [string]$Character = ""
)

$ErrorActionPreference = "Stop"
$Here = Split-Path -Parent $MyInvocation.MyCommand.Path
$runner = Join-Path $Here "Run_Character.ps1"

if (-not (Test-Path -LiteralPath $runner)) {
  throw "Missing Run_Character.ps1"
}

  $choices = @(
  @{Label="Dr. Pretorius - sardonic gothic scientist"; Slug="pretorius"},
  @{Label="Kiki - 90s-style affectionate friend"; Slug="kiki"},
  @{Label="R0-R1 - child-safe creative companion droid"; Slug="r0r1"},
  @{Label="Mira - friendly companion"; Slug="friendly"},
  @{Label="Cassian Vale - rival antagonist"; Slug="rival"},
  @{Label="Eli Rowan - quiet restrained character"; Slug="quiet"},
  @{Label="Marin Hale - practical mentor"; Slug="mentor"}
)

if (-not $Character) {
  try {
    Add-Type -AssemblyName System.Windows.Forms
    Add-Type -AssemblyName System.Drawing

    $form = New-Object System.Windows.Forms.Form
    $form.Text = "PersonaConsole V6 - Start Chat"
    $form.StartPosition = "CenterScreen"
    $form.Width = 430
    $form.Height = 180
    $form.FormBorderStyle = "FixedDialog"
    $form.MaximizeBox = $false

    $label = New-Object System.Windows.Forms.Label
    $label.Text = "Choose a character. This starts offline template mode."
    $label.Left = 16
    $label.Top = 18
    $label.Width = 380
    $form.Controls.Add($label)

    $combo = New-Object System.Windows.Forms.ComboBox
    $combo.Left = 16
    $combo.Top = 48
    $combo.Width = 380
    $combo.DropDownStyle = "DropDownList"
    foreach ($c in $choices) { [void]$combo.Items.Add($c.Label) }
    $combo.SelectedIndex = 0
    $form.Controls.Add($combo)

    $start = New-Object System.Windows.Forms.Button
    $start.Text = "Start Chat"
    $start.Left = 220
    $start.Top = 92
    $start.Width = 85
    $start.DialogResult = [System.Windows.Forms.DialogResult]::OK
    $form.AcceptButton = $start
    $form.Controls.Add($start)

    $cancel = New-Object System.Windows.Forms.Button
    $cancel.Text = "Cancel"
    $cancel.Left = 312
    $cancel.Top = 92
    $cancel.Width = 85
    $cancel.DialogResult = [System.Windows.Forms.DialogResult]::Cancel
    $form.CancelButton = $cancel
    $form.Controls.Add($cancel)

    $result = $form.ShowDialog()
    if ($result -ne [System.Windows.Forms.DialogResult]::OK) { exit 0 }
    $Character = $choices[$combo.SelectedIndex].Slug
  } catch {
    Write-Host "Choose a character:"
    for ($i = 0; $i -lt $choices.Count; $i++) {
      Write-Host ("{0}. {1}" -f ($i + 1), $choices[$i].Label)
    }
    $pick = Read-Host "Enter 1-7"
    $idx = [int]$pick - 1
    if ($idx -lt 0 -or $idx -ge $choices.Count) { $idx = 0 }
    $Character = $choices[$idx].Slug
  }
}

& $runner -Character $Character -Renderer template
