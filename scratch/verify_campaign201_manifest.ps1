# WinIOS Campaign 201 Manifest Verification Script
$ErrorActionPreference = "Stop"

$repoRoot = "D:\WinISO26\WinIOS"
$manifestPath = Join-Path $repoRoot "docs\campaign201\manifest.json"

if (-not (Test-Path $manifestPath)) {
    Write-Error "Manifest not found: $manifestPath"
    exit 1
}

$manifest = Get-Content -Path $manifestPath -Raw | ConvertFrom-Json
$artifacts = $manifest.artifacts
$total = $artifacts.Count
$passed = 0
$failed = 0

Write-Host "Verifying $total Campaign 201 artifacts from manifest..."

foreach ($a in $artifacts) {
    $fullPath = Join-Path $repoRoot ($a.relative_path.Replace('/', '\'))
    if (-not (Test-Path $fullPath)) {
        Write-Host "  [MISSING] $($a.relative_path)" -ForegroundColor Red
        $failed++
        continue
    }

    $actualHash = (Get-FileHash -Path $fullPath -Algorithm SHA256).Hash.ToLower()
    if ($actualHash -eq $a.sha256.ToLower()) {
        $passed++
    } else {
        Write-Host "  [MISMATCH] $($a.relative_path)" -ForegroundColor Red
        Write-Host "    Expected: $($a.sha256)"
        Write-Host "    Actual:   $actualHash"
        $failed++
    }
}

Write-Host ""
Write-Host "=== Campaign 201 Manifest Verification Summary ==="
$failColor = if ($failed -gt 0) { "Red" } else { "Green" }
Write-Host "Total:    $total"
Write-Host "Verified: $passed" -ForegroundColor Green
Write-Host "Failed:   $failed" -ForegroundColor $failColor

if ($failed -gt 0) {
    exit 1
} else {
    Write-Host "100% Manifest Integrity Verified!" -ForegroundColor Green
    exit 0
}
