# Audit CTest Accounting for WinIOS
$ErrorActionPreference = "Stop"

function Audit-Log($logPath) {
    if (-not (Test-Path $logPath)) {
        Write-Error "Log not found: $logPath"
    }

    $lines = Get-Content $logPath
    $totalRegistered = 0
    $passed = 0
    $skipped = 0
    $failed = 0
    $other = 0

    $testRegex = [regex]'^\s*(\d+)/(\d+)\s+Test\s+#(\d+):\s+(\S+)\s+\.{3,}\s*(\*\*\*Skipped|\*\*\*Failed|Passed)'

    $testEntries = @{}

    foreach ($line in $lines) {
        $m = $testRegex.Match($line)
        if ($m.Success) {
            $currIndex = [int]$m.Groups[1].Value
            $totalRegistered = [int]$m.Groups[2].Value
            $testId = [int]$m.Groups[3].Value
            $testName = $m.Groups[4].Value
            $status = $m.Groups[5].Value

            $testEntries[$testId] = @{
                Name = $testName
                Status = $status
                Index = $currIndex
            }

            if ($status -eq "Passed") {
                $passed++
            } elseif ($status -eq "***Skipped") {
                $skipped++
            } elseif ($status -eq "***Failed") {
                $failed++
            } else {
                $other++
            }
        }
    }

    $summaryLine = $lines | Where-Object { $_ -match '(\d+)%\s+tests\s+passed\s+out\s+of\s+(\d+)' } | Select-Object -Last 1

    [PSCustomObject]@{
        LogFile = $logPath
        TotalRegistered = $totalRegistered
        ParsedEntries = $testEntries.Count
        Passed = $passed
        Skipped = $skipped
        Failed = $failed
        Other = $other
        CTestSummaryString = $summaryLine
        EffectivePassRate = if ($testEntries.Count - $skipped -gt 0) { [math]::Round(($passed / ($testEntries.Count - $skipped)) * 100, 3) } else { 0 }
        RawPassRate = if ($testEntries.Count -gt 0) { [math]::Round(($passed / $testEntries.Count) * 100, 3) } else { 0 }
    }
}

$relAudit = Audit-Log "docs/campaign200/test_results_release.txt"
$dbgAudit = Audit-Log "docs/campaign200/test_results_debug.txt"

Write-Output "=== Release Log Audit ==="
$relAudit | Format-List

Write-Output "=== Debug Log Audit ==="
$dbgAudit | Format-List
