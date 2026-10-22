$ErrorActionPreference = 'Stop'
$path = Join-Path $env:LOCALAPPDATA 'PredictiveBufferV5\debug.log'
Write-Host 'Predictive Buffer V5 - Registro en vivo' -ForegroundColor Cyan
Write-Host ('Ruta: ' + $path)
Write-Host 'Control-C detiene el visor. El registro continua guardandose mientras el juego esta abierto.'
Write-Host ''
if (-not (Test-Path -LiteralPath $path)) {
    Write-Host 'Todavia no existe el log. Abre GD con la V5 de diagnostico instalada.' -ForegroundColor Yellow
    Write-Host 'Esperando a que el mod inicie...'
    while (-not (Test-Path -LiteralPath $path)) { Start-Sleep -Milliseconds 500 }
}
Get-Content -LiteralPath $path -Tail 80 -Wait -Encoding UTF8
