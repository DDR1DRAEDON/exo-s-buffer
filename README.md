# Predictive Buffer V5 — Rework experimental (v0.3.0)

## Diagnosticos nuevos: ventana CMD + registro persistente

En Windows, al cargar el mod se intenta abrir **una consola Win32 real** usando
`AllocConsole` (igual que el binario de referencia). Los mensajes tambien se guardan en
`%LOCALAPPDATA%\PredictiveBufferV5\debug.log`, incluso si ocultas la consola.
El visor `VER_REGISTRO_EN_VIVO.cmd` permite ver el archivo en CMD, segundo a segundo.

**Teclas durante una partida:** F6 cambia el preset; F7 activa/desactiva el registro
detallado de inputs y entregas; F8 oculta/muestra la consola del mod; F9 imprime
estadisticas (inputs, entregas, predicciones, retrasos e incertidumbre); **F10 activa el paso directo de inputs sin predictor** para comparar con el juego base (suelta salto antes de alternar).

**Para encontrar el fallo:** abre Geometry Dash, entra a un nivel sencillo y comprueba
que ves `[BOOT]`, `[LIFECYCLE]` y `[STATS]`. Presiona F7, haz clics y revisa
`[INPUT]`, `[FORECAST]`, `[QUEUE]` y `[EMIT]`. Si el juego cierra, comparte
`debug.log` y los logs de Geode. **La ventana CMD no permite que ChatGPT observe
tu PC remotamente**: debes compartir el archivo de registro.

Si **todavia no compila**, en Windows ejecuta `COMPILAR_Y_VER_ERRORES.cmd`.
Su salida se guarda en `compilacion.log` para detectar el primer error.

**Estado real:** código fuente y pruebas del núcleo listos; **no hay DLL de Windows ni `.geode` compilado** en este ZIP. No es un parche del DLL original: es una implementación independiente y experimental dirigida a GD 2.2081 (Windows 64 bits) / Geode 5.11.2.

## Cambios incluidos

- Motor de decisión con presupuesto estricto de hasta 8 evaluaciones por transición, exploración progresiva de retrasos y rechazo si la simulación no es confiable.
- Colas ordenadas e independientes para P1/P2, que conservan los clics rápidos (incluidos press + release antes de aplicar el primero).
- Restauración del estado físico al cambiar presets y al entrar en Ship, para reducir estados de tecla atascada.
- Detección de delta de tiempo anómalo que desactiva la predicción para esa evaluación.
- Protección adicional ante efectos que la simulación no puede reproducir, incluida la animación de Spider.
- Pruebas automatizadas del motor, incluyendo 10.000 cambios de input aleatorios.
- Compilación automatizada para Windows a través de GitHub Actions, además de script PowerShell.

## Compilación más simple: GitHub Actions

1. Sube **el contenido de esta carpeta**, incluida `.github/workflows/build-windows.yml`, a un repositorio de GitHub (público o privado).
2. Ve a **Actions → Test and build Predictive Buffer V5 (Windows) → Run workflow**.
3. Si pasa la compilación de Windows, descarga el artefacto `PredictiveBuffer-V5-Win64` del job.
4. Dentro del artefacto estará el `.geode` de Windows listo para instalación. **Si falla la compilación de Geode, el registro señalará los cambios de API que aún haya que corregir.**

El workflow utiliza `geode-sdk/build-geode-mod@main`, Geode SDK **5.11.2** indicado en `mod.json` y la opción de destino **Win64**.

## Compilación local (Windows)

Instala Visual Studio 2022 con herramientas de C++, CMake, Geode CLI y el SDK Geode 5.11.2. Desde PowerShell en esta carpeta:

```powershell
geode sdk install-binaries --platform Win64
.\build-windows.ps1
```

El script asume que `GEODE_SDK` ya está configurado. También puedes configurar directamente:

```powershell
cmake -S . -B build-windows -A x64 -DPB_BUILD_GEODE=ON -DBUILD_TESTING=OFF
cmake --build build-windows --config Release
```

## Verificar el motor sin Geode

```bash
cmake -S . -B build-tests -DBUILD_TESTING=ON -DPB_BUILD_GEODE=OFF
cmake --build build-tests --config Release
ctest --test-dir build-tests --output-on-failure
```

Estas pruebas **no prueban la precisión de predicción dentro de Geometry Dash**: solo prueban invariantes de eventos, límites y decisiones con escenarios simulados.

## Instalación y pruebas recomendadas

1. **Haz una copia** del `PredictiveBuffer.geode` original.
2. **Desactiva el mod original `ai.predictivebuffer`**, ya que ambos interceptan `GJBaseGameLayer::handleButton`.
3. Instala el `.geode` generado en la carpeta de mods de Geode.
4. Prueba primero en el editor y niveles sencillos; usa F6 para cambiar entre `2/2`, `4/4`, `8/8`, `16/8`, `OFF` (ticks normales/wave).
5. Prueba pulsaciones cortas, portal de Ship, cambios de gravedad, wave mini, modo Dual y reinicios. Comprueba si hay diferencias entre el movimiento real y la predicción.

## Advertencias importantes

**El adaptador aún NO ha sido compilado contra el SDK de Geode en esta sesión ni probado en Windows dentro del juego.** Puede necesitar cambios de firmas de métodos o detalles de API. La física predictiva con ghosts es aproximada, y triggers, portales, objetos en movimiento, Click Between Frames y otros mods pueden invalidar predicciones y causar crashes. La lógica se diseñó para no cambiar inputs cuando detecta incertidumbre, pero eso no cubre todos los efectos desconocidos.

**No uses estos resultados para afirmar que es superior a v4.7 sin un ensayo controlado dentro del juego.**

## Archivos

- `include/pb/Engine.hpp`: predictor y cola de inputs testeados.
- `src/geode_adapter.cpp`: adaptador de Geode **sin validar**.
- `tests/test_engine.cpp`: pruebas portables.
- `.github/workflows/build-windows.yml`: integración continua y empaquetado Win64.
- `src/DebugConsole.hpp`: consola Windows y registro de ejecucion.
- `VER_REGISTRO_EN_VIVO.cmd`: muestra el log en vivo.
- `COMPILAR_Y_VER_ERRORES.cmd`: compilacion en CMD con salida a archivo.
- `build-windows.ps1`: compilación local.
- `docs/GUIA_ES.md`: detalles técnicos de la primera versión del prototipo.
