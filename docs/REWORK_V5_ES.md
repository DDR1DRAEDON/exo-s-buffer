# Rework V5 v0.2.0 — decisiones técnicas y límites

**Estado:** motor portable probado, adaptador Geode no compilado ni probado en juego.

## Qué sí cambió frente al ZIP anterior

1. `frameStep` ya no acepta deltas claramente incompatibles ni recicla silenciosamente el último si el delta actual es anómalo.
2. El estado de un input pendiente se reconcilia al entrar a Ship, incluso en el frame de transición, para no mantener un input pospuesto de Wave/Cube dentro de Ship.
3. La supresión de P2 solo se plantea durante buffering de P1 en dual de control único; entradas P2 fuera de dual ya no se descartan universalmente.
4. El contador de inputs rápidos ya no empieza como si hubiera existido un clic justo antes del comienzo del nivel.
5. Se agregó limpieza de estado cuando se entra al editor y un guard para efectos de Spider durante simulación.
6. Se añadieron casos de prueba de presupuesto predictivo, incertidumbre, transición a Ship y ganancia mínima.
7. Workflow `windows-latest` para compilar con `geode-sdk/build-geode-mod` y adjuntar automáticamente el `.geode` **solo si el job realmente termina bien**.

## Por qué no se genera un .geode aquí

El mod es para Windows x64 y compila contra bibliotecas/binarios del SDK Geode. La sesión actual es Linux, sin SDK/CLI de Geode, sin toolchain cruzado Windows y sin acceso directo para instalar esas dependencias. Generar un ZIP con metadatos y llamarlo `.geode` sin DLL funcional sería engañoso.

## Pruebas que todavía faltan

- Compilación del adaptador con firmas GD 2.2081 / Geode 5.11.2.
- Smoke test de inicialización, entrada y salida de nivel, muertes y cambios de escena.
- A/B contra el archivo del usuario: frame-perfects, falsos retardos, tasa de clics perdidos, uso de CPU y estabilidad.
- Wave normal/mini, Ship, Robot, Spider, Dual, portales/triggers y modificaciones de TPS.
