# Contexto técnico congelado — Llavero E-Ink

## Hardware

| Componente | Estado |
|---|---|
| Seeed Studio XIAO ESP32C3 (con antena Wi-Fi externa) | Ya lo tengo |
| Batería LiPo 1S 3.7V 250 mAh | Ya la tengo |
| Waveshare 1.54" E-Ink Module 200×200, B/N, SPI | **Falta comprar — camino crítico** |
| Interruptor deslizable, carcasa impresa, argolla | Por definir en Fase 1 |
| Cautín, multímetro, impresora 3D | Disponibles |

## Pinout — DOS pinouts activos, para dos placas distintas

**Importante:** a partir de D-021 (2026-08-01) hay dos pinouts vigentes al
mismo tiempo, para dos hardware distintos. No son inconsistentes entre sí
por error — cada uno le corresponde a una placa y un firmware distintos:

| | Pinout de breadboard / diagnóstico | Pinout de PCB final |
|---|---|---|
| Decisiones | D-001, actualizado por D-017 | D-021 |
| Firmware | `firmware/test-consumo/` (cerrado, no se toca) | `firmware/llavero/` (firmware final) |
| Hardware | Protoboard, cableado a mano | PCB fabricada en la escuela |
| Por qué así | RST/BUSY/DC agrupados del lado izquierdo, DIN/CLK/CS del derecho — 3 y 3, para facilitar el cableado manual | 6 pines contiguos de un solo lado del conector — para que el trazado de la PCB sea directo |

**Batería → XIAO (pads traseros, igual en ambos):** Rojo a `BAT+` / `B+`,
Negro a `BAT-` / `B-`.

### Pinout de breadboard / diagnóstico (D-001 + D-017)

| Pantalla | XIAO | GPIO | Nota |
|---|---|---|---|
| VCC | 3V3 | — | Salida del regulador interno |
| GND | GND | — | |
| DIN | D5 | GPIO7 | SPI por hardware, ruteado vía matriz de pines (no es el pin FSPI nativo; ese es GPIO10, ocupado por CS) |
| CLK | D4 | GPIO6 | SPI por hardware, ruteado vía matriz de pines (el nativo, GPIO8, es strapping pin — se evita) |
| CS | D10 | GPIO10 | |
| DC | D1 | GPIO3 | Lado izquierdo del conector, junto a RST y BUSY (ver D-017) |
| RST | D3 | GPIO5 | RTC GPIO — se mantiene en LOW durante deep sleep |
| BUSY | D2 | GPIO4 | RTC GPIO, entrada |

Pinout corregido el 2026-07-28 tras validar el mapeo D→GPIO real del XIAO
ESP32C3 contra la documentación de Seeed; ver docs/decisiones.md D-001.
DC reubicado de D7/GPIO20 a D1/GPIO3 el 2026-08-01 para balancear el
conector en 3 pines por lado; ver docs/decisiones.md D-017. Este pinout
quedó cerrado junto con la medición de consumo de D-006 y no se vuelve a
tocar.

### Pinout de PCB final (D-021)

| Pantalla | XIAO | GPIO | Nota |
|---|---|---|---|
| VCC | 3V3 | — | Salida del regulador interno |
| GND | GND | — | |
| BUSY | D1 | GPIO3 | Entrada |
| RST | D2 | GPIO4 | |
| DC | D3 | GPIO5 | |
| CS | D4 | GPIO6 | |
| CLK | D5 | GPIO7 | |
| DIN | D6 | GPIO21 | UART0 TX (U0TXD) del SoC — no es strapping pin, y `Serial` en este XIAO usa USB-CDC nativo, no UART0; ver docs/decisiones.md D-021 |

Los seis pines quedan contiguos (D1 a D6) de un solo lado del conector del
XIAO, a propósito, para simplificar el trazado de la PCB. Esto reintroduce
GPIO21 (D6), que D-001 había evitado por ser U0TXD — D-021 documenta por
qué en esta placa es un riesgo aceptable. Vigente para
`firmware/llavero/`, el firmware final que corre en la PCB.

## Decisiones ya tomadas

- **Frecuencia:** el dispositivo despierta **una vez al día**. Sin notificación instantánea.
- **Envío:** bot de Telegram. Yo mando foto o texto, nada más.
- **Dirección:** unidireccional (yo → ella). Sin botones, sin respuesta.
- **Conectividad primaria:** hotspot del celular de ella. El SSID y la contraseña del hotspot los define el teléfono, no la compañía telefónica, así que **se precargan antes de que se vaya y siguen funcionando cuando cambie de SIM en Dinamarca**. Esto elimina el riesgo del Wi-Fi de residencia universitaria (WPA2-Enterprise y portales cautivos, que el ESP32 no maneja).
- **Reconfiguración:** portal cautivo. El llavero levanta su propia red, ella entra desde el celular y elige otra red si hace falta. Debe guardar varias redes conocidas e intentarlas por orden.
- **Reparto de trabajo:** **el servidor hace todo el procesamiento pesado.** Recorta la foto a 200×200, la pasa a escala de grises, aplica dithering Floyd-Steinberg y renderiza el texto con acentos y ñ. Guarda un buffer de 1 bit por píxel (5000 bytes) listo para pintar. El llavero solo descarga bytes y los manda por SPI.
- **OTA:** obligatorio. Una vez que el llavero esté en Dinamarca es la única vía para corregir errores.
- **Sin indicador de batería en pantalla.** No se agrega el divisor resistivo al ADC.
- **Firmware:** PlatformIO en VS Code, framework Arduino, librería GxEPD2 para la pantalla.
- **Repositorio:** monorepo público en GitHub, aún no creado.

## Presupuesto de energía (estimación a validar en mesa)

- Deep sleep del XIAO ESP32C3: ~45 µA → 1.08 mAh/día
- Ciclo diario despierto (Wi-Fi + descarga + refresco completo): ~25 s a ~90 mA promedio → ~0.63 mAh
- **Total: ~1.7 mAh/día.** Con 250 mAh nominales y ~200 mAh utilizables, da del orden de **3 meses por carga**, con margen amplio sobre el objetivo de "semanas".

**Condición crítica:** el módulo e-ink debe mandarse a modo hibernación por software antes de que el ESP32 entre en deep sleep. Si no, la corriente parásita del módulo domina el consumo y la autonomía se derrumba. Si la hibernación por software no basta, la alternativa es cortar la alimentación de la pantalla con un MOSFET de canal P. Esto hay que **medirlo con el multímetro en Fase 1**, no asumirlo.

## Riesgos abiertos

1. **Tiempo de envío de la pantalla.** Es el camino crítico. Sin ella no hay pruebas.
2. **Deriva del reloj en deep sleep.** El RTC interno del ESP32 deriva varios minutos al día. Mitigación: que el servidor responda, junto con la imagen, cuántos segundos debe dormir el dispositivo hasta el siguiente despertar. El sistema se autocorrige y yo puedo cambiar el horario de despertar sin tocar el firmware.
3. **Hotspot apagado a la hora de despertar.** Necesita política de reintentos: varios intentos espaciados, y si todos fallan, dormir un lapso corto y reintentar, sin agotar la batería.
4. **Horario de despertar.** Copenhague está 8 horas adelante de la Ciudad de México. Conviene que despierte de madrugada hora danesa para que ella encuentre la imagen nueva al levantarse.
5. **Backend por definir.** Ver decisión pendiente en Fase 0.
6. **Un solo juego de hardware.** Si se quema algo soldando y falta poco para el 16 de agosto, no hay repuesto. Considerar comprar una segunda pantalla.

## Fecha límite

16 de agosto de 2026. No se mueve.
