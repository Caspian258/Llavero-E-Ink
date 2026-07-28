# Llavero E-Ink — Instrucciones para Claude Code

## Regla de autoría (INNEGOCIABLE)

Claude NUNCA aparece como autor ni coautor de ningún commit en este repositorio.

Prohibido, sin excepción:
- El trailer `Co-Authored-By: Claude <noreply@anthropic.com>` o cualquier variante.
- La línea `🤖 Generated with [Claude Code](https://claude.com/claude-code)` o cualquier firma equivalente.
- Emojis de robot o menciones a Claude, Anthropic o cualquier herramienta de IA en mensajes de commit, PRs o issues.
- El uso de `--author` o `--trailer` para atribuir algo a Claude.

Los mensajes de commit son texto plano, en español, en imperativo, describiendo solo el cambio técnico.
Ejemplo correcto: `Agrega hibernación del panel e-ink antes de deep sleep`

Si detectas que un commit previo violó esta regla, detente y avísale al usuario antes de seguir.

## Qué es este proyecto

Llavero con pantalla e-ink de 200x200 que despierta una vez al día, descarga una imagen
de un servidor y la muestra. El contenido lo envía el usuario por un bot de Telegram.
Comunicación unidireccional. Autonomía objetivo: varias semanas con una batería de 250 mAh.

## Estructura

```
/firmware   PlatformIO, framework Arduino, target seeed_xiao_esp32c3
/server     Backend: bot de Telegram + procesamiento de imagen + endpoint del dispositivo
/hardware   Diagramas de conexión, modelos de la carcasa, notas de ensamblaje
/docs       Decisiones de arquitectura, bitácora, guía de armado
```

## Restricciones de hardware (no modificar sin autorización explícita)

Placa: Seeed Studio XIAO ESP32C3
Pantalla: Waveshare 1.54" E-Ink 200x200, blanco y negro, SPI, vía GxEPD2

Pines:

```
DIN  -> D5  (GPIO7)
CLK  -> D4  (GPIO6)
CS   -> D10 (GPIO10)
DC   -> D7  (GPIO20)
RST  -> D3  (GPIO5, RTC GPIO)
BUSY -> D2  (GPIO4, RTC GPIO)
VCC  -> 3V3
GND  -> GND
```

Nota: el mapeo D→GPIO del XIAO ESP32C3 no es lineal. D6 real es GPIO21 = U0TXD,
usado por el bootloader ROM al arrancar; D8 real es GPIO8, pin de strapping.
Ambos se evitan. RST y BUSY se colocan en pines RTC GPIO (GPIO5/GPIO4) porque
el firmware necesita mantener RST en LOW durante el deep sleep vía
`rtc_gpio_hold_en()` — pendiente de confirmar en Fase 1 si el módulo corta la
alimentación del panel al hacerlo. Ver decisión D-001 en `decisiones.md`.

## Reglas de firmware

- La energía es la restricción dominante. Todo camino de código termina en deep sleep.
- El panel e-ink DEBE entrar en hibernación por software antes del deep sleep del ESP32.
- Nada de `delay()` largos ni bucles de espera activa. Usar timeouts con salida a deep sleep.
- Si el Wi-Fi falla, reintentar con retroceso acotado y luego dormir. Jamás quedarse despierto esperando.
- El firmware NO procesa imágenes. Recibe un buffer de 5000 bytes (1 bit por píxel) ya listo
  y lo manda a la pantalla. Todo el procesamiento vive en `/server`.
- El servidor indica en su respuesta cuántos segundos debe dormir el dispositivo. El firmware
  no calcula horarios por su cuenta.
- Credenciales y secretos van en archivos ignorados por git. Nunca hardcodeados en el fuente.
  Mantener siempre un `.example` con la estructura.
- OTA es requisito, no opcional. No romper la capacidad de actualización remota.

## Reglas de servidor

- Salida canónica: 200x200 px, 1 bit por píxel, 5000 bytes, dithering Floyd-Steinberg.
- El renderizado de texto debe soportar acentos y ñ correctamente.
- El endpoint del dispositivo responde el buffer más los segundos de sueño hasta el próximo despertar.
- El endpoint debe estar autenticado con un token; no puede quedar abierto al público.

## Reglas generales

- Comentarios y documentación en español.
- Antes de un cambio grande en la arquitectura, pregunta.
- No agregues dependencias sin justificarlo.
- Después de cada tarea, actualiza `docs/bitacora.md` con qué se hizo y qué falta probar.
