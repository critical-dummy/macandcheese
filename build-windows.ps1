[CmdletBinding()]
param(
    [switch]$Clean
)

$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot

function Invoke-Native {
    param(
        [string]$Command,
        [string[]]$Arguments
    )
    & $Command @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed with exit code $LASTEXITCODE`: $Command $($Arguments -join ' ')"
    }
}

Write-Host '[Mac&Cheese] Checking CMake...' -ForegroundColor Cyan
Invoke-Native 'cmake' @('--version')

$buildDir = Join-Path $PSScriptRoot 'build'
if ($Clean -and (Test-Path $buildDir)) {
    Write-Host "[Mac&Cheese] Removing build cache: $buildDir" -ForegroundColor Yellow
    Remove-Item -Recurse -Force $buildDir
}
$cmakeArgs = @('-S', '.', '-B', $buildDir, '-DMNC_BUILD_TESTS=ON')
$buildConfigArgs = @()
$testConfigArgs = @()

$vswhereCandidates = @(
    (Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'),
    (Join-Path ${env:ProgramFiles} 'Microsoft Visual Studio\Installer\vswhere.exe')
)
$vswhere = $vswhereCandidates | Where-Object { $_ -and (Test-Path $_) } | Select-Object -First 1
$vsInstallPath = ''
if ($vswhere) {
    $vsInstallPath = (& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Out-String).Trim()
}
$vsInstalled = $vsInstallPath -and (Test-Path $vsInstallPath)

if ($vsInstalled) {
    $vsMajor = if ($vsInstallPath -match '\\(\d+)\\BuildTools') { [int]$Matches[1] } else { 17 }
    $generator = if ($vsMajor -ge 18) { 'Visual Studio 18 2026' } else { 'Visual Studio 17 2022' }
    $availableGenerators = (& cmake --help | Out-String)
    if ($availableGenerators -notmatch [regex]::Escape($generator)) {
        throw "CMake does not support the detected Visual Studio generator '$generator'. Update CMake or install a compatible generator."
    }
    Write-Host "[Mac&Cheese] Visual Studio C++ toolchain detected at $vsInstallPath; using '$generator'." -ForegroundColor Cyan
    $cmakeArgs += @('-G', $generator, '-A', 'x64')
    $buildConfigArgs = @('--config', 'Release')
    $testConfigArgs = @('-C', 'Release')
} elseif ((Get-Command mingw32-make -ErrorAction SilentlyContinue) -and (Get-Command g++ -ErrorAction SilentlyContinue)) {
    Write-Host '[Mac&Cheese] MinGW detected; using MinGW Makefiles.' -ForegroundColor Cyan
    $cmakeArgs += @('-G', 'MinGW Makefiles')
} elseif (Get-Command ninja -ErrorAction SilentlyContinue) {
    Write-Host '[Mac&Cheese] Ninja detected; using Ninja.' -ForegroundColor Cyan
    $cmakeArgs += @('-G', 'Ninja')
} else {
    throw 'No supported C++ toolchain found. Install Visual Studio Build Tools with C++ workload, or install MinGW-w64/Ninja.'
}

$cache = Join-Path $buildDir 'CMakeCache.txt'
if (Test-Path $cache) {
    $cachedGenerator = Select-String -Path $cache -Pattern '^CMAKE_GENERATOR:INTERNAL=' | Select-Object -First 1
    $generatorIndex = [Array]::IndexOf([string[]]$cmakeArgs, '-G')
    $wantedGenerator = if ($generatorIndex -ge 0) { $cmakeArgs[$generatorIndex + 1] } else { 'default' }
    if ($cachedGenerator -and $cachedGenerator.Line -notmatch [regex]::Escape($wantedGenerator)) {
        Write-Host '[Mac&Cheese] Removing stale build cache because the generator changed.' -ForegroundColor Yellow
        Remove-Item -Recurse -Force $buildDir
    }
}

Write-Host '[Mac&Cheese] Configuring...' -ForegroundColor Cyan
Invoke-Native 'cmake' $cmakeArgs

Write-Host '[Mac&Cheese] Building Release...' -ForegroundColor Cyan
Invoke-Native 'cmake' (@('--build', $buildDir, '--parallel') + $buildConfigArgs)

Write-Host '[Mac&Cheese] Running tests...' -ForegroundColor Cyan
Invoke-Native 'ctest' (@('--test-dir', $buildDir, '--output-on-failure') + $testConfigArgs)

Write-Host '[Mac&Cheese] Build and tests completed successfully.' -ForegroundColor Green
if ($buildConfigArgs.Count -gt 0) {
    Write-Host "Executable: $buildDir\Release\mnc-inspect.exe"
} else {
    Write-Host "Executable: $buildDir\mnc-inspect.exe"
}
