# Predictive Buffer v5 — informe y guía

## Origen
- Entrada: `PredictiveBuffer.geode` aportado por el usuario (identificador `ai.predictivebuffer`, Geode 5.11.2, GD 2.2081 Windows).
- El DLL de 141312 bytes contiene el literal `v4.7 LOADED (Zero-Lag + Up Arrow Fix)`.
- El repositorio público de fraudian muestra código que se identifica como v4.6, NO el código exacto de la DLL suministrada.
- Por lo tanto, este proyecto es un rediseño independiente de la lógica. No constituye un parche de la DLL original.

## Mejoras incluidas
1. Decisión predictiva independiente y testeable (`include/pb/Engine.hpp`).
2. Presupuesto máximo de 8 evaluaciones por pulsación, incluidas las del instante inicial. El presupuesto reduce los candidatos inspeccionados, no garantiza FPS.
3. Comprobación inicial: una trayectoria segura no necesita examinar retrasos alternativos.
4. Retrasos muestreados progresivamente y refinados si ofrecen una ganancia significativa. Se evitan retrasos innecesarios.
5. Cola ordenada de 64 transiciones por jugador, con desbordamiento explícito sin perder eventos.
6. Dos canales independientes; en dual de un jugador el adaptador refleja el primer canal al segundo.
7. Cancelación de entradas pendientes y conciliación con el botón físico al cambiar de modo. Estado limpio al morir.
8. F6 por detección de flanco, sin repetir ciclos mientras se mantiene pulsada la tecla.
9. Fallo seguro del predictor si una simulación marca condiciones de colisión desconocidas.
10. Cálculo del resultado desde el instante inicial para que los retrasos sean comparables sobre el mismo horizonte.

## Lo que falta comprobar
- Adaptador Geode: no hay SDK/compilador nativo de Windows configurado en este entorno.
- Hooks y firmas de clases para Geode 5.11.2 + GD 2.2081.
- Física real, portales, triggers, orbes, ball, robot, swing y dual en niveles reales.
- Simulación especulativa puede provocar efectos secundarios no previstos en objetos/mods de terceros.
- Interacciones con CBF, Speedhack/TPS y hotkeys en programas externos.
- Comparación A/B en niveles con al menos 100 intentos por nivel y variante; anotar tasa de supervivencia, retraso medio y FPS 1% low.

## Compilación en Windows
1. Instala Visual Studio 2022 con el workload «Desktop development with C++», CMake, Geode CLI y SDK compatible.
2. Configura la variable `GEODE_SDK` apuntando al directorio del SDK.
3. Desde la raíz ejecuta:

```powershell
geode sdk install-binaries
cmake -S . -B build-geode -DPB_BUILD_GEODE=ON -DBUILD_TESTING=OFF
cmake --build build-geode --config Release
```

4. Verifica la instalación local de GD 2.2081 y Geode 5.11.2. Si CMake indica incompatibilidades, corrige las firmas del adaptador según los headers instalados.
5. Instala **solo** la compilación experimental, desactivando temporalmente el `ai.predictivebuffer` original. No cargues los dos mods de entrada simultáneamente.

## Pruebas automáticas del núcleo

```sh
cmake -S . -B build-test -DCMAKE_BUILD_TYPE=Debug
cmake --build build-test
ctest --test-dir build-test --output-on-failure
```

Prueba de estrés: 10.000 eventos de pulsación/liberación alternados en dos canales, verificación de orden y de que no haya eventos pendientes al terminar.

**Estado:** núcleo en C++ compilado y validado en Linux; adaptador de Geode aún experimental y sin pruebas en el juego. No se proporciona un `.geode` compilado porque sería incorrecto presentarlo como funcional.
