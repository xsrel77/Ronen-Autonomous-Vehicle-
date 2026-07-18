param(
  [Parameter(Mandatory=$true)]
  [string]$SessionZip
)

$ErrorActionPreference = "Stop"
$projectRoot = Resolve-Path "."
$sessionDataDir = Join-Path $projectRoot "src\session-data"
$tempDir = Join-Path $env:TEMP ("rbv2_session_import_" + [guid]::NewGuid().ToString("N"))

if (!(Test-Path $SessionZip)) {
  throw "Session ZIP not found: $SessionZip"
}

New-Item -ItemType Directory -Force -Path $sessionDataDir | Out-Null
New-Item -ItemType Directory -Force -Path $tempDir | Out-Null

Expand-Archive -Path $SessionZip -DestinationPath $tempDir -Force

$manifest = Get-ChildItem -Path $tempDir -Recurse -Filter "session_manifest.json" | Select-Object -First 1
if ($null -eq $manifest) {
  Remove-Item -Recurse -Force $tempDir
  throw "Could not find session_manifest.json inside ZIP."
}

$sessionFolder = Split-Path $manifest.FullName -Parent
$sessionId = Split-Path $sessionFolder -Leaf
if ($sessionId -notmatch '^session[_-]') {
  Remove-Item -Recurse -Force $tempDir
  throw "Detected folder is not a session folder: $sessionId"
}

$target = Join-Path $sessionDataDir $sessionId
if (Test-Path $target) {
  Write-Host "[import] Removing existing session folder: $target"
  Remove-Item -Recurse -Force $target
}

Move-Item -Path $sessionFolder -Destination $target
Remove-Item -Recurse -Force $tempDir

Write-Host "[import] OK: $sessionId -> $target"
Write-Host "[import] Run: npm run prepare-dashboard-media"
