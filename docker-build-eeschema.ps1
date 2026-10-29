# Run inside the KiCad CI container.
# Source is mounted at C:\kicad-source; build goes to C:\kicad-source\build\docker.

$ErrorActionPreference = 'Stop'

Write-Host "=== Setting up build env ===" -ForegroundColor Cyan
C:\builder\build.ps1 -Env -Arch x64

$buildDir = 'C:\kicad-source\build\docker'
New-Item -ItemType Directory -Force -Path $buildDir | Out-Null
Push-Location $buildDir

Write-Host "=== CMake configure ===" -ForegroundColor Cyan
cmake `
    -G 'Ninja' `
    -DCMAKE_TOOLCHAIN_FILE='C:\builder\vcpkg\scripts\buildsystems\vcpkg.cmake' `
    -DCMAKE_BUILD_TYPE=Debug `
    -DKICAD_USE_OCC=ON `
    -DKICAD_BUILD_PNS_DEBUG_TOOL=ON `
    -DKICAD_USE_3DCONNEXION=ON `
    -DVCPKG_BUILD_TYPE=debug `
    -DVCPKG_INSTALL_OPTIONS='--x-abi-tools-use-exact-versions' `
    -DVCPKG_OVERLAY_TRIPLETS='C:\kicad-source\tools\custom_vcpkg_triplets' `
    -DKICAD_USE_PCH=OFF `
    -DKICAD_BUILD_QA_TESTS=OFF `
    'C:\kicad-source'

if ($LASTEXITCODE -ne 0) {
    Write-Host "=== CONFIGURE FAILED ===" -ForegroundColor Red
    exit $LASTEXITCODE
}

Write-Host "=== Building eeschema ===" -ForegroundColor Cyan
cmake --build . --target eeschema 2>&1 | Tee-Object -FilePath 'C:\kicad-source\build\docker\build.log'

Pop-Location

if (Test-Path "$buildDir\eeschema\eeschema.exe") {
    Write-Host "=== eeschema.exe BUILT ===" -ForegroundColor Green
    Get-Item "$buildDir\eeschema\eeschema.exe" | Select-Object FullName, Length
} else {
    Write-Host "=== eeschema.exe NOT FOUND ===" -ForegroundColor Red
    exit 1
}
