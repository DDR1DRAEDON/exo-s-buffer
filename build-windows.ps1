# PowerShell: run from the project root with Visual Studio 2022 C++ and Geode CLI/SDK installed.
$ErrorActionPreference = 'Stop'
if (-not $env:GEODE_SDK) { throw 'Falta GEODE_SDK. Ejecuta primero: geode sdk install <ruta-de-SDK>' }
if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) { throw 'CMake no instalado.' }

Write-Host 'Configurando Predictive Buffer V5 para Windows x64 / GD 2.2081...'
cmake -S . -B build-windows -A x64 -DPB_BUILD_GEODE=ON -DBUILD_TESTING=OFF
if ($LASTEXITCODE -ne 0) { throw 'Error configurando CMake.' }
cmake --build build-windows --config Release --parallel 4
if ($LASTEXITCODE -ne 0) { throw 'Error compilando Geode. Revisa salida del compilador.' }
$package = Get-ChildItem -Path build-windows -Filter '*.geode' -Recurse | Select-Object -First 1
if (-not $package) { throw 'No se genero .geode; revisa la salida de setup_geode_mod.' }
Write-Host ('Paquete: ' + $package.FullName)
