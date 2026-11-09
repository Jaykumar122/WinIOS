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

function Build-Artifact {
    param(
        [string]$Name,
        [string]$Source,
        [string[]]$ExtraFlags
    )
    $srcPath = Join-Path $srcDir $Source
    $outPath = Join-Path $binDir $Name
    Write-Host "Building real Mach-O artifact: $srcPath -> $outPath"
    $cmd = @("cc", "-target", "aarch64-macos", "-nostdlib", "-Wl,-e,_start") + $ExtraFlags + @($srcPath, "-o", $outPath)
    & zig $cmd
    if ($LASTEXITCODE -ne 0) {
        Write-Error "Failed to compile $srcPath with zig cc"
        exit $LASTEXITCODE
    }
    $hash = (Get-FileHash -Algorithm SHA256 $outPath).Hash.ToLower()
    $length = (Get-Item $outPath).Length
    Write-Host "Successfully built $outPath ($length bytes, SHA256: $hash)"
}

# REAL-001: hello_arm64
Build-Artifact -Name "hello_arm64" -Source "hello_arm64.S" -ExtraFlags @()

# REAL-002: compute_arm64_O0
Build-Artifact -Name "compute_arm64_O0" -Source "compute_arm64.c" -ExtraFlags @("-O0", "-fno-sanitize=undefined")

# REAL-003: compute_arm64_O2
Build-Artifact -Name "compute_arm64_O2" -Source "compute_arm64.c" -ExtraFlags @("-O2")

# REAL-004: libmathreal.dylib
$srcDylib = Join-Path $srcDir "libmathreal.c"
$outDylib = Join-Path $binDir "libmathreal.dylib"
Write-Host "Building real Mach-O dynamic library: $srcDylib -> $outDylib"
& zig cc -target aarch64-macos -dynamiclib -nostdlib -fno-sanitize=undefined -install_name /usr/lib/libmathreal.dylib $srcDylib -o $outDylib
if ($LASTEXITCODE -ne 0) {
    Write-Error "Failed to compile $srcDylib with zig cc"
    exit $LASTEXITCODE
}
$hashDylib = (Get-FileHash -Algorithm SHA256 $outDylib).Hash.ToLower()
$lenDylib = (Get-Item $outDylib).Length
Write-Host "Successfully built $outDylib ($lenDylib bytes, SHA256: $hashDylib)"

# REAL-005: main_with_dylib
$srcMain = Join-Path $srcDir "main_with_dylib.c"
$outMain = Join-Path $binDir "main_with_dylib"
Write-Host "Building real Mach-O dynamically linked executable: $srcMain -> $outMain"
& zig cc -target aarch64-macos -nostdlib -fno-sanitize=undefined "-Wl,-e,_start" -L $binDir -lmathreal $srcMain -o $outMain
if ($LASTEXITCODE -ne 0) {
    Write-Error "Failed to compile $srcMain with zig cc"
    exit $LASTEXITCODE
}
$hashMain = (Get-FileHash -Algorithm SHA256 $outMain).Hash.ToLower()
$lenMain = (Get-Item $outMain).Length
Write-Host "Successfully built $outMain ($lenMain bytes, SHA256: $hashMain)"

# Stack protector artifacts
Build-Artifact -Name "stack_safe_arm64" -Source "stack_safe_arm64.c" -ExtraFlags @("-fno-sanitize=undefined", "-fstack-protector-all")
Build-Artifact -Name "stack_smash_arm64" -Source "stack_smash_arm64.c" -ExtraFlags @("-fno-sanitize=undefined", "-fstack-protector-all")
