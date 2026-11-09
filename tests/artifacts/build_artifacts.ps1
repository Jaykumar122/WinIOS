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

# REAL-006: compute_ios_O2 (iOS Target: PLATFORM_IOS = 2)
$srcIos = Join-Path $srcDir "compute_arm64.c"
$outIos = Join-Path $binDir "compute_ios_O2"
Write-Host "Building genuine iOS-target Mach-O executable: $srcIos -> $outIos"
& zig cc -target aarch64-ios -nostdlib -fno-sanitize=undefined -fno-stack-protector -O2 "-Wl,-e,_start" $srcIos -o $outIos
if ($LASTEXITCODE -ne 0) {
    Write-Error "Failed to compile $srcIos with zig cc for iOS"
    exit $LASTEXITCODE
}
$hashIos = (Get-FileHash -Algorithm SHA256 $outIos).Hash.ToLower()
$lenIos = (Get-Item $outIos).Length
Write-Host "Successfully built $outIos ($lenIos bytes, SHA256: $hashIos)"

# Three-Image Dependency Chain Artifacts
$srcLeaf = Join-Path $srcDir "libleaf.c"
$outLeaf = Join-Path $binDir "libleaf.dylib"
Write-Host "Building libleaf.dylib: $srcLeaf -> $outLeaf"
& zig cc -target aarch64-macos -dynamiclib -nostdlib -fno-sanitize=undefined -install_name @rpath/libleaf.dylib $srcLeaf -o $outLeaf
if ($LASTEXITCODE -ne 0) { Write-Error "Failed to compile libleaf.dylib"; exit $LASTEXITCODE }

$srcMid = Join-Path $srcDir "libmid.c"
$outMid = Join-Path $binDir "libmid.dylib"
Write-Host "Building libmid.dylib: $srcMid -> $outMid"
& zig cc -target aarch64-macos -dynamiclib -nostdlib -fno-sanitize=undefined -install_name @rpath/libmid.dylib -L $binDir -lleaf "-Wl,-rpath,@loader_path" $srcMid -o $outMid
if ($LASTEXITCODE -ne 0) { Write-Error "Failed to compile libmid.dylib"; exit $LASTEXITCODE }

$srcChain = Join-Path $srcDir "main_chain.c"
$outChain = Join-Path $binDir "main_chain"
Write-Host "Building main_chain: $srcChain -> $outChain"
& zig cc -target aarch64-macos -nostdlib -fno-sanitize=undefined "-Wl,-e,_start" -L $binDir -lmid "-Wl,-rpath,@executable_path" $srcChain -o $outChain
if ($LASTEXITCODE -ne 0) { Write-Error "Failed to compile main_chain"; exit $LASTEXITCODE }

# Initializers and TLV Artifacts (Stage T3)
$srcCtorLeaf = Join-Path $srcDir "ctor_leaf.c"
$outCtorLeaf = Join-Path $binDir "libctor_leaf.dylib"
Write-Host "Building libctor_leaf.dylib: $srcCtorLeaf -> $outCtorLeaf"
& zig cc -target aarch64-macos -dynamiclib -nostdlib -fno-sanitize=undefined -fno-stack-protector -install_name @rpath/libctor_leaf.dylib $srcCtorLeaf -o $outCtorLeaf
if ($LASTEXITCODE -ne 0) { Write-Error "Failed to compile libctor_leaf.dylib"; exit $LASTEXITCODE }

$srcCtorMid = Join-Path $srcDir "ctor_mid.c"
$outCtorMid = Join-Path $binDir "libctor_mid.dylib"
Write-Host "Building libctor_mid.dylib: $srcCtorMid -> $outCtorMid"
& zig cc -target aarch64-macos -dynamiclib -nostdlib -fno-sanitize=undefined -fno-stack-protector -install_name @rpath/libctor_mid.dylib -L $binDir -lctor_leaf "-Wl,-rpath,@loader_path" $srcCtorMid -o $outCtorMid
if ($LASTEXITCODE -ne 0) { Write-Error "Failed to compile libctor_mid.dylib"; exit $LASTEXITCODE }

$srcCtorMain = Join-Path $srcDir "ctor_main.c"
$outCtorMain = Join-Path $binDir "ctor_main"
Write-Host "Building ctor_main: $srcCtorMain -> $outCtorMain"
& zig cc -target aarch64-macos -nostdlib -fno-sanitize=undefined -fno-stack-protector "-Wl,-e,_start" -L $binDir -lctor_mid -lctor_leaf "-Wl,-rpath,@executable_path" $srcCtorMain -o $outCtorMain
if ($LASTEXITCODE -ne 0) { Write-Error "Failed to compile ctor_main"; exit $LASTEXITCODE }

$srcTlvMain = Join-Path $srcDir "tlv_main.c"
$outTlvMain = Join-Path $binDir "tlv_main"
Write-Host "Building tlv_main: $srcTlvMain -> $outTlvMain"
& zig cc -target aarch64-macos -nostdlib -fno-sanitize=undefined -fno-stack-protector "-Wl,-e,_start" $srcTlvMain -o $outTlvMain
if ($LASTEXITCODE -ne 0) { Write-Error "Failed to compile tlv_main"; exit $LASTEXITCODE }

# ARM64e / PAC Probe Artifact (Stage T4)
$srcPacProbe = Join-Path $srcDir "pac_probe.S"
$outPacProbe = Join-Path $binDir "pac_probe"
Write-Host "Building pac_probe: $srcPacProbe -> $outPacProbe"
& zig cc -target aarch64-macos -mcpu=apple_a16 -nostdlib "-Wl,-e,_start" $srcPacProbe -o $outPacProbe
if ($LASTEXITCODE -ne 0) { Write-Error "Failed to compile pac_probe"; exit $LASTEXITCODE }
