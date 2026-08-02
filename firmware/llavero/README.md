# Firmware — Llavero E-Ink (dispositivo final)

Proyecto PlatformIO del firmware final del dispositivo, separado de
`firmware/test-consumo/` (diagnóstico de Fase 1, cerrado, no se toca).

**Esta tarea implementa solo la capa de conectividad Wi-Fi:**
`WiFiMulti` con redes guardadas en NVS, portal cautivo como fallback, y
backoff exponencial persistido (D-010, D-011, D-020). Todavía **no** hay
cliente HTTPS, pintado de pantalla, ni OTA — esas son tareas separadas
que vienen después. Por ahora, una conexión exitosa solo se confirma por
Serial y el dispositivo vuelve a dormir.

## Compilar

```bash
cd firmware/llavero
pio run
```

Para flashear y ver el log por USB:

```bash
pio run --target upload
pio device monitor --baud 115200
```

## Qué hace cada ciclo de arranque

1. Carga las redes guardadas en NVS (`Preferences`, namespace `llavero`)
   en un objeto `WiFiMulti`.
2. Si hay al menos una red guardada, intenta conectar (`WiFiMulti::run()`,
   hasta 15 s). `WiFiMulti` decide el orden por señal — comportamiento
   nativo de la librería, no reimplementado.
3. **Si conecta:** imprime la IP por Serial, resetea el backoff a 900 s
   (15 min) y duerme ese intervalo. (El ciclo completo de descarga +
   pintado llega en una tarea posterior.)
4. **Si no hay redes guardadas, o si `WiFiMulti` no logra conectar:**
   levanta el portal cautivo — AP `Llavero-Setup` (abierto) + página de
   configuración en `192.168.4.1`, durante hasta 5 minutos.
   - Si alguien manda una red nueva desde el portal, la guarda en NVS y
     hace `ESP.restart()` para reintentar con la lista actualizada.
   - Si el portal se agota sin recibir nada, aplica backoff exponencial
     (duplica el intervalo vigente, techo en 4 h) y duerme ese intervalo.

Todo el estado (redes guardadas, intervalo de backoff) sobrevive al deep
sleep porque vive en NVS, no en RAM.

## Flujo de prueba manual (requiere hardware real)

**No hay forma de verificar la lógica de Wi-Fi sin un XIAO ESP32C3 real
conectado a una red de verdad** — esto no se puede simular ni mockear de
forma útil (WiFiMulti, el portal cautivo con DNS/HTTP, y NVS son todos
comportamiento del chip real). Lo que sigue es el flujo que el usuario
debe correr a mano.

### 1. Provocar un primer arranque sin redes guardadas

Las redes viven en NVS, que sobrevive tanto al deep sleep como a un reset
normal — hay que borrar la partición NVS a propósito:

```bash
pio run --target erase   # borra toda la flash, incluyendo NVS
pio run --target upload
pio device monitor --baud 115200
```

Log esperado:

```
=== Llavero E-Ink: capa de conectividad Wi-Fi ===
No hay redes guardadas (primer arranque). Se levanta el portal cautivo.
Levantando portal cautivo...
Portal cautivo activo. SSID: Llavero-Setup | IP: 192.168.4.1
```

### 2. Conectarse al AP del portal cautivo desde un celular

1. En el celular, ir a Wi-Fi y conectarse a la red **Llavero-Setup** (sin
   contraseña).
2. El celular debería abrir solo una página de "iniciar sesión en la red"
   (comportamiento estándar de captive portal). Si no aparece sola, abrir
   un navegador y entrar a `http://192.168.4.1/`.
3. Debe verse una página simple confirmando **"Conectado a la red de
   configuración: Llavero-Setup"** — esa es la confirmación visual de que
   el celular está hablando con el dispositivo correcto.
4. Llenar el formulario con el SSID y password de la red real (ej. el
   hotspot del celular) y tocar "Guardar y reiniciar".
5. Debe verse "Guardado. El llavero se reiniciará para conectarse."

En el monitor serie, en paralelo, debe verse:

```
Red nueva guardada en índice 0: <SSID que escribiste>
Configuración nueva guardada. Reiniciando para reintentar con la lista actualizada...
```

y el dispositivo se reinicia solo (log vuelve a `=== Llavero E-Ink... ===`).

### 3. Confirmar que guardó la red y conectó en el siguiente arranque

Justo después del reinicio automático del paso anterior, el log debería
mostrar:

```
=== Llavero E-Ink: capa de conectividad Wi-Fi ===
Red guardada cargada (0): <SSID que escribiste>
Buscando 1 red(es) guardada(s)...
Conectado a <SSID que escribiste>, IP <IP asignada por esa red>
Durmiendo 900 segundos
```

Si en cambio no conecta (contraseña mal escrita, red fuera de rango),
debería volver a levantar el portal cautivo y, si tampoco se configura
nada ahí, dormir con backoff (30 min la primera vez, luego 1h, 2h, hasta
un techo de 4h) — confirmable dejando el dispositivo sin tocar y viendo
que el intervalo impreso se duplica en arranques sucesivos (cada
`pio device monitor` reconectado tras el deep sleep debería mostrar el
nuevo intento).

### 4. Casos adicionales a probar si hay tiempo

- **Reconexión tras energizar de nuevo** (sin borrar NVS): debe conectar
  directo a la red ya guardada, sin pasar por el portal cautivo.
- **Agregar una segunda red** (ej. mientras la primera está fuera de
  rango): repetir el paso 2 apagado el AP de la primera red — debe
  guardarse como índice 1 sin borrar la red del índice 0.
- **Reintroducir el mismo SSID con una password distinta**: debe
  actualizar la password existente en su índice, no crear una entrada
  duplicada (log: "Red existente actualizada").
