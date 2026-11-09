# WinIOS Real Mach-O Artifact Build Script
# Rebuilds real independently compiled Mach-O arm64 binaries using Zig toolchain
# Target: aarch64-macos freestanding binary

$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$srcDir = Join-Path $scriptDir "src"
$binDir = Join-Path $scriptDir "bin"

if (-not (Test-Path $binDir)) {
    New-Item -ItemType Directory -Force -Path $binDir | Out-Null
}

$sourceFile = Join-Path $srcDir "hello_arm64.S"
$outputFile = Join-Path $binDir "hello_arm64"

Write-Host "Building real Mach-O artifact: $sourceFile -> $outputFile"

# Invoke Zig cc with Darwin aarch64 target, freestanding, custom entry _start
& zig cc -target aarch64-macos -nostdlib "-Wl,-e,_start" $sourceFile -o $outputFile

if ($LASTEXITCODE -ne 0) {
    Write-Error "Failed to compile $sourceFile with zig cc"
    exit $LASTEXITCODE
}

$hash = (Get-FileHash -Algorithm SHA256 $outputFile).Hash.ToLower()
$length = (Get-Item $outputFile).Length
Write-Host "Successfully built $outputFile ($length bytes, SHA256: $hash)"
