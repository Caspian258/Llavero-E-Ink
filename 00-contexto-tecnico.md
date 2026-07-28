# Contexto técnico congelado — Llavero E-Ink

## Hardware

| Componente | Estado |
|---|---|
| Seeed Studio XIAO ESP32C3 (con antena Wi-Fi externa) | Ya lo tengo |
| Batería LiPo 1S 3.7V 250 mAh | Ya la tengo |
| Waveshare 1.54" E-Ink Module 200×200, B/N, SPI | **Falta comprar — camino crítico** |
| Interruptor deslizable, carcasa impresa, argolla | Por definir en Fase 1 |
| Cautín, multímetro, impresora 3D | Disponibles |

## Pinout (validado, no cambiar sin discusión)

**Batería → XIAO (pads traseros):** Rojo a `BAT+` / `B+`, Negro a `BAT-` / `B-`.

**Pantalla → XIAO (pines frontales):**

| Pantalla | XIAO | GPIO | Nota |
|---|---|---|---|
| VCC | 3V3 | — | Salida del regulador interno, alimentada por batería cuando no hay USB |
| GND | GND | — | |
| DIN | D10 | GPIO10 | MOSI del SPI por hardware |
| CLK | D8 | GPIO8 | SCK del SPI por hardware |
| CS | D7 | GPIO7 | También UART0 TX; sin conflicto porque el serial va por USB nativo |
| DC | D6 | GPIO6 | También UART0 RX; misma nota |
| RST | D5 | GPIO5 | |
| BUSY | D4 | GPIO4 | Entrada |

DIN y CLK caen en el SPI por hardware del C3, así que el refresco es rápido y no por bit-banging. Es la asignación óptima.

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
