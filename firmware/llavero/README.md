# Firmware — Llavero E-Ink (dispositivo final)

Proyecto PlatformIO del firmware final del dispositivo, separado de
`firmware/test-consumo/` (diagnóstico de Fase 1, cerrado, no se toca).

**Qué incluye hasta ahora:** conectividad Wi-Fi (`WiFiMulti`, redes
guardadas en NVS, portal cautivo como fallback, backoff exponencial —
D-010, D-011, D-020), un cliente HTTPS que consulta
`GET https://caspiandomain.dev/device/wake` (D-008/D-009) tras conectar,
valida el certificado TLS real (sin `setInsecure()`), y descarga el buffer
de imagen solo si el checksum cambió (D-023) y, desde D-024, el pintado
real de ese buffer en el panel e-ink con GxEPD2 — solo cuando la imagen es
nueva, seguido siempre de `display.hibernate()` antes de dormir. Todavía
**no** hay OTA — tarea separada que viene después.

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

**Nota sobre `board_upload.after_reset = watchdog_reset` en `platformio.ini`:**
el reset por default tras flashear (`hard_reset`) en el XIAO ESP32C3 usa el
USB-Serial/JTAG nativo del chip, que solo dispara un *core reset* — no
vuelve a muestrear los pines de strapping de arranque. Si en algún momento
quedaron muestreados como "modo descarga", el chip se queda atascado ahí
indefinidamente después de cada upload, sin llegar nunca a correr el
firmware (ni un botón de reset físico ni desconectar el USB lo saca,
porque ninguno de los dos fuerza un reset de sistema completo por esta
vía). `watchdog_reset` sí hace un reset de sistema completo, que vuelve a
muestrear los pines y arranca la aplicación normalmente.

Como consecuencia, el reset de sistema completo también reinicia el propio
periférico USB nativo, así que **el puerto serie puede reenumerar con un
nombre distinto después de cada upload** (por ejemplo, de `/dev/ttyACM0` a
`/dev/ttyACM1`). Si `pio device monitor` no conecta o se queda colgado
después de un upload, verificar el puerto actual con `ls /dev/ttyACM*`
antes de reintentar.

## Qué hace cada ciclo de arranque

1. Carga las redes guardadas en NVS (`Preferences`, namespace `llavero`)
   en un objeto `WiFiMulti`.
2. Si hay al menos una red guardada, intenta conectar (`WiFiMulti::run()`,
   hasta 15 s). `WiFiMulti` decide el orden por señal — comportamiento
   nativo de la librería, no reimplementado.
3. **Si conecta:** imprime la IP por Serial y llama a
   `GET /device/wake` (D-023) con el `X-Device-Token` guardado en NVS,
   validando TLS contra `cert_raiz.h`.
   - **Si el servidor responde 200:** compara `X-Image-Checksum` contra
     el último guardado en NVS. Si es igual, no descarga el body ni pinta
     nada — evita desgastar el panel con refrescos innecesarios. Si es
     distinto (o no había ninguno guardado), descarga los 5000 bytes a un
     buffer en RAM, **pinta el panel con `GxEPD2` (D-024, refresco
     completo) e hiberna la pantalla**, y recién después guarda el
     checksum nuevo (en ese orden — si algo falla a mitad del pintado, el
     checksum no se guarda y el próximo ciclo reintenta descarga + pintado
     completos). En ambos casos resetea el backoff a 900 s y duerme los
     segundos que indicó el servidor en `X-Sleep-Seconds` (D-005/D-009 —
     el servidor controla el horario real, no un intervalo fijo del
     firmware).
   - **Si falla** (error de conexión/TLS, 401, 503, o cualquier código
     que no sea 200): no resetea el backoff, se trata igual que un fallo
     de Wi-Fi — aplica backoff exponencial y duerme ese intervalo.
   - OTA queda para una tarea posterior.
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
   (comportamiento estándar de captive portal), **sin que haga falta abrir
   un navegador ni escribir la IP a mano**. Si no aparece sola, entrar
   manualmente a `http://192.168.4.1/` como respaldo.
3. Debe verse la tarjeta de configuración con el chip **"Llavero-Setup"**
   — esa es la confirmación visual de que el celular está hablando con el
   dispositivo correcto.
4. Llenar el formulario con el SSID y password de la red real (ej. el
   hotspot del celular) y tocar "Guardar y reiniciar".
5. Debe verse "Guardado. El llavero se reiniciará para conectarse a la
   red nueva."

**Pendiente de confirmar con hardware real:** `onNotFound` ahora sirve la
página de configuración directo (200) para cualquier ruta no reconocida,
en vez de redirigir con 302 — la razón documentada en el código es que el
sondeo automático de iOS no siempre sigue redirecciones. Esto **no se
puede verificar sin un celular real** conectándose al AP; probarlo
específicamente en iOS/Safari (que es donde se detectó el problema
original) y también en Android, y confirmar si el popup automático
aparece esta vez. Si en algún dispositivo sigue sin aparecer solo, el
paso 2 de arriba ya cubre el respaldo manual (`http://192.168.4.1/`).

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
Consultando https://caspiandomain.dev/device/wake ...
```

seguido de la respuesta del cliente HTTPS (D-023) — ver la sección de
abajo, "Prueba manual del cliente HTTPS", para qué esperar ahí según si
hay token configurado, si el checksum cambió, etc.

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

## Prueba manual del cliente HTTPS y el pintado (requiere hardware real + internet + servidor real + panel real)

**No hay forma de verificar esto sin un XIAO ESP32C3 real, con el panel
Waveshare 1.54" real conectado con el pinout de D-021, conectado a una red
con internet de verdad, contra el servidor real en
`https://caspiandomain.dev`** — la negociación TLS contra el certificado
real, la respuesta real del servidor, la comparación de checksum, y que
la imagen realmente aparezca en el panel físico, no se pueden simular ni
mockear de forma útil. Lo que sigue es el flujo que le toca al usuario.

### 1. Configurar el token real (`X-Device-Token`)

**No hay todavía una forma de cargar esto sin recompilar, a diferencia de
las redes Wi-Fi** (D-023) — el portal cautivo actual solo tiene campos de
SSID/password. Hasta que se agregue esa interfaz (tarea futura), el paso
más simple es flashear una vez un sketch mínimo separado que solo escribe
el token en NVS, y después volver a flashear el firmware real:

1. Guardar esto como `/tmp/set_token/src/main.cpp` (o cualquier carpeta
   fuera del repo) con su propio `platformio.ini` mínimo
   (`[env:seeed_xiao_esp32c3]` igual al de `firmware/llavero/`):

   ```cpp
   #include <Arduino.h>
   #include <Preferences.h>

   void setup() {
     Serial.begin(115200);
     delay(2000);
     Preferences prefs;
     prefs.begin("llavero", false);
     prefs.putString("deviceToken", "PEGAR_AQUI_EL_TOKEN_REAL_DE_PRODUCCION");
     prefs.end();
     Serial.println("Token guardado en NVS.");
   }

   void loop() {}
   ```

2. `pio run --target upload` desde esa carpeta temporal, confirmar por
   `pio device monitor` que imprime "Token guardado en NVS."
3. Volver a `firmware/llavero/` y `pio run --target upload` para
   reflashear el firmware real — el token queda en NVS, sobrevive el
   reflasheo del firmware (NVS es una partición separada del código).

Este token es el mismo que ya generaste para `server/.env`
(`DEVICE_AUTH_TOKEN` en el VPS, D-022) — tienen que coincidir.

### 2. Escenario: imagen nueva (descarga Y pinta)

Con una imagen real cargada en el servidor (vía el bot de Telegram) que
todavía no se descargó nunca en este dispositivo:

```
Conectado a <SSID>, IP <IP>
Consultando https://caspiandomain.dev/device/wake ...
Respuesta OK. X-Fw-Version=0.1.0, X-Sleep-Seconds=<N>, X-Image-Checksum=<hex>
Imagen nueva descargada: 5000 bytes, checksum <hex>
Durmiendo <N> segundos
```

`<N>` (los segundos de sueño) los decide el servidor (D-005) — no tiene
por qué ser 900. **Confirmación visual:** el panel debe mostrar la imagen
o texto real mandado por Telegram — este es el único escenario donde el
panel cambia. El refresco completo del panel (parpadeo blanco/negro
varias veces) tarda unos segundos; el log de "Durmiendo" recién aparece
después de que `display.hibernate()` termina.

### 3. Escenario: imagen sin cambios (NO pinta)

Repitiendo el ciclo (o forzando un arranque manual) sin haber mandado una
imagen nueva por Telegram desde la última vez:

```
Conectado a <SSID>, IP <IP>
Consultando https://caspiandomain.dev/device/wake ...
Respuesta OK. X-Fw-Version=0.1.0, X-Sleep-Seconds=<N>, X-Image-Checksum=<hex>
Imagen sin cambios, no se descarga.
Durmiendo <N> segundos
```

Mismo checksum que la vez anterior — confirma que la comparación contra
NVS funciona y que no se gasta batería/tiempo bajando los 5000 bytes de
nuevo. **Confirmación visual:** el panel NO debe parpadear ni cambiar —
si refresca igual, algo está mal en la condición que gatea `pintarPantalla()`
(D-024).

### 4. Escenario: token incorrecto

Con el sketch temporal del paso 1, guardar a propósito un token que NO
coincida con `DEVICE_AUTH_TOKEN` del servidor:

```
Conectado a <SSID>, IP <IP>
Consultando https://caspiandomain.dev/device/wake ...
El servidor respondió con código 401 (se esperaba 200)
Fallo al consultar el servidor. Aplicando backoff.
Durmiendo 900 segundos
```

(900 la primera vez — el backoff avanza en arranques sucesivos si el
problema persiste, igual que un fallo de Wi-Fi.)

### 5. Escenario: servidor caído o inalcanzable

Cortar la conectividad del VPS (o simplemente apagar `systemctl stop
llavero` en el servidor, D-022, para esta prueba) y dejar que el
dispositivo intente conectarse:

```
Conectado a <SSID>, IP <IP>
Consultando https://caspiandomain.dev/device/wake ...
Error de conexión HTTPS: <descripción> (<código negativo>)
Fallo al consultar el servidor. Aplicando backoff.
Durmiendo 900 segundos
```

El mensaje de error exacto depende de la causa (timeout, DNS, TLS) — lo
importante es que el dispositivo no se cuelga ni crashea, loggea el error
y sigue el mismo camino de backoff que cualquier otro fallo.
