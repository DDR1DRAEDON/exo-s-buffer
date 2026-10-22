# Ejecuta desde COMPILAR_Y_VER_ERRORES.cmd o desde PowerShell.
# Registra cada mensaje de CMake y del compilador en compilacion.log.
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
Start-Transcript -Path (Join-Path $root 'compilacion.log') -Append | Out-Null
try {
    Set-Location -LiteralPath $root
    if (-not $env:GEODE_SDK) { throw 'No se encuentra GEODE_SDK. Instala Geode CLI/SDK y abre una nueva terminal: geode sdk install' }
    if (-not (Test-Path -LiteralPath (Join-Path $env:GEODE_SDK 'CMakeLists.txt'))) {
        throw ('GEODE_SDK no contiene un SDK valido: ' + $env:GEODE_SDK)
    }
    if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) { throw 'CMake no esta instalado o falta en PATH.' }
    Write-Host 'Configurando Predictive Buffer V5 con Geode Win64 y simbolos de depuracion...'
    & cmake -S . -B build-windows -A x64 -DPB_BUILD_GEODE=ON -DBUILD_TESTING=OFF
    if ($LASTEXITCODE -ne 0) { throw 'CMake no pudo configurar el proyecto.' }
    & cmake --build build-windows --config RelWithDebInfo --parallel 4
    if ($LASTEXITCODE -ne 0) { throw 'Error de compilacion. Busca la PRIMERA linea error: en compilacion.log.' }
    $package = Get-ChildItem -LiteralPath (Join-Path $root 'build-windows') -Recurse -Filter '*.geode' | Select-Object -First 1
    if (-not $package) { throw 'CMake termino, pero el target no genero un archivo .geode.' }
    Write-Host ('PAQUETE: ' + $package.FullName) -ForegroundColor Green
    Write-Host ('LOG: ' + (Join-Path $root 'compilacion.log'))
}
catch {
    Write-Host ('ERROR: ' + $_.Exception.Message) -ForegroundColor Red
    exit 1
}
finally {
    Stop-Transcript | Out-Null
}
