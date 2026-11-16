# Verify Campaign 200 Manifest
$ErrorActionPreference = "Stop"

if (-not (Test-Path "docs/campaign200/manifest.json")) {
    Write-Error "manifest.json not found"
}

$raw = Get-Content "docs/campaign200/manifest.json" -Raw | ConvertFrom-Json

$total = $raw.files.Count
$matched = 0
$mismatched = 0
$missing = 0

$results = @()

foreach ($entry in $raw.files) {
    $p = $entry.path
    $expHash = $entry.sha256.ToLower()
    $expBytes = $entry.bytes

    if (-not (Test-Path $p)) {
        $missing++
        $results += [PSCustomObject]@{
            Path = $p
            Status = "MISSING"
            ExpectedHash = $expHash
            ActualHash = "N/A"
            ExpectedBytes = $expBytes
            ActualBytes = 0
        }
    } else {
        $actHash = (Get-FileHash -Path $p -Algorithm SHA256).Hash.ToLower()
        $actBytes = (Get-Item $p).Length

        if ($actHash -eq $expHash -and $actBytes -eq $expBytes) {
            $matched++
            $results += [PSCustomObject]@{
                Path = $p
                Status = "MATCH"
                ExpectedHash = $expHash
                ActualHash = $actHash
                ExpectedBytes = $expBytes
                ActualBytes = $actBytes
            }
        } else {
            $mismatched++
            $results += [PSCustomObject]@{
                Path = $p
                Status = "MISMATCH"
                ExpectedHash = $expHash
                ActualHash = $actHash
                ExpectedBytes = $expBytes
                ActualBytes = $actBytes
            }
        }
    }
}

Write-Output "=== Campaign 200 Manifest Verification Summary ==="
Write-Output "Total files: $total"
Write-Output "Matched:     $matched"
Write-Output "Mismatched:  $mismatched"
Write-Output "Missing:     $missing"

if ($mismatched -gt 0 -or $missing -gt 0) {
    Write-Output "`nDiscrepancies:"
    $results | Where-Object { $_.Status -ne "MATCH" } | Format-Table -AutoSize
} else {
    Write-Output "`nAll $matched artifacts match cryptographic SHA-256 digests perfectly."
}
