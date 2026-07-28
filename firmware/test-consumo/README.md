# test-consumo

Firmware de banco de pruebas, temporal. Sirve para medir con multímetro la
corriente en deep sleep del conjunto XIAO ESP32C3 + panel Waveshare 1.54"
e-Paper, y así validar el presupuesto de energía antes de escribir el
firmware final. No es parte del producto — no tiene Wi-Fi, ni OTA, ni
Telegram, solo inicializa lo mínimo y duerme.

## Cómo cambiar de modo

Editar la constante al principio de `src/main.cpp`:

```cpp
#define MODO_PRUEBA 1   // cambiar a 1, 2 o 3
```

Guardar, compilar y flashear (`pio run -t upload`) entre cada medición.
Solo un modo está activo por compilación.

- **Modo 1** — Solo el XIAO. No inicializa SPI ni toca ningún pin de la
  pantalla. Sirve de línea base: consumo del ESP32C3 solo en deep sleep.
- **Modo 2** — Inicializa la pantalla, dibuja un rectángulo de prueba y la
  manda a hibernar por software (`display.hibernate()`) antes de que el
  XIAO entre en deep sleep. Sin hold de pines todavía.
- **Modo 3** — Igual que el modo 2, pero además fuerza en LOW los seis
  pines de la pantalla antes de dormir, y usa `gpio_hold_en()` +
  `gpio_deep_sleep_hold_en()` sobre RST y BUSY para que el nivel LOW se
  mantenga durante todo el deep sleep.

## Qué esperar ver

Con el monitor serie abierto (115200 baudios):

1. Un par de segundos de margen para que el monitor alcance a conectar.
2. En modo 1: tres líneas `MODO 1: XIAO despierto (n/3)`. En modo 2 y 3:
   mensajes de inicialización de la pantalla.
3. Un último mensaje anunciando que va a dormir.
4. El dispositivo "desaparece" del puerto serie: entró a deep sleep y el
   puerto USB nativo deja de responder hasta el siguiente reset o hasta que
   el temporizador (60 s, fijo para poder medir varios ciclos seguidos)
   despierte al chip y vuelva a correr `setup()` desde el principio.

Para medir corriente: multímetro en serie en la alimentación, y leer una
vez que el dispositivo ya "desapareció" del serie (es decir, ya está en
deep sleep, no en el ciclo activo).

## Nota técnica: por qué no se usa `rtc_gpio_hold_en()`

El ESP32-C3 no tiene el periférico RTCIO clásico de otras variantes de
ESP32 (`SOC_RTCIO_PIN_COUNT` es 0 en este chip), así que
`rtc_gpio_hold_en()` no aplica aunque GPIO4/GPIO5 sean pines capaces de
RTC. El equivalente correcto en el C3 es `gpio_hold_en()` por pin más
`gpio_deep_sleep_hold_en()` global, que es lo que usa el modo 3. Ver
comentarios en `src/main.cpp`.

## Nota técnica: pin de "LED integrado"

El XIAO ESP32C3 no tiene un LED de usuario controlable por GPIO (el único
LED de la placa es el indicador de carga, no programable). Por eso el modo
1 confirma que corrió imprimiendo por el puerto serie en vez de parpadear
un LED.
