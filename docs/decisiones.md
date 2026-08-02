## D-001 — Pinout corregido (2026-07-28)
El mapeo D→GPIO del XIAO ESP32C3 no es lineal. Se reasignan los seis pines
del e-paper para evitar los strapping pins (GPIO2/8/9) y U0TXD (GPIO21, D6),
y para colocar RST y BUSY en RTC GPIOs (GPIO5/GPIO4).
CLK=D4/GPIO6 · DIN=D5/GPIO7 · CS=D10/GPIO10 · DC=D7/GPIO20 · RST=D3/GPIO5 · BUSY=D2/GPIO4
D1/GPIO3 queda libre y reservado para gate de MOSFET.

## D-002 — Estrategia de sueño de la pantalla (2026-07-28)
Antes del deep sleep: hibernación por software, RST enganchado en LOW vía
gpio_hold_en() por pin + gpio_deep_sleep_hold_en() global (API correcto
para ESP32-C3, que no implementa RTCIO clásico — confirmado contra
soc_caps.h del SDK instalado y la documentación de Espressif durante la
implementación del firmware de prueba en /firmware/test-consumo/), y los
seis pines en LOW. En módulos Waveshare recientes,
RST en LOW corta la lógica del level shifter y la alimentación del panel.
Fallback si la medición supera 400 µA: P-MOSFET de canal P sobre VCC,
siempre combinado con los pines en LOW (evitar alimentación fantasma por
diodos ESD). Se renuncia al refresco parcial; irrelevante a 1 refresco/día.

## D-003 — Criterio de aceptación de autonomía (2026-07-28)
Umbral de corriente total en deep sleep: verde ≤150 µA, ámbar 150–400 µA,
rojo >400 µA. Medición con multímetro en serie y jumper de bypass durante
las fases activas.

## D-004 — OTA con rollback obligatorio (2026-07-28)
Una imagen OTA nueva solo se marca válida tras completar un ciclo entero
exitoso (Wi-Fi + descarga + pintado). Sin esto, un push defectuoso deja el
dispositivo irrecuperable en Dinamarca.

## D-005 — Cálculo de horario en el servidor (2026-07-28)
El servidor calcula los segundos de sueño usando la zona Europe/Copenhagen,
no un offset fijo respecto a CDMX. La diferencia pasa de 8 h a 7 h el
25-oct-2026 por el fin del horario de verano europeo.

## D-006 — Resultado de medición de corriente parásita (2026-07-31)
Medición con multímetro Truper MUT-39 (rango 2mA, resolución ~1µA),
firmware de tres modos en /firmware/test-consumo/, batería LiPo real
(no USB — ver nota de comportamiento anómalo abajo).

Resultados:
- Modo 1 (XIAO solo, pantalla sin tocar): 0.007 mA = 7 µA
- Modo 2 (XIAO + pantalla, hibernación por software): 0.007 mA = 7 µA
- Modo 3 (XIAO + pantalla, RST/pines forzados LOW): 0.007 mA = 7 µA

Los tres modos dieron el mismo resultado, reproducible con esperas de
15-20s antes de leer (se descartó lectura de transición). Contra los
umbrales de D-003 (verde ≤150 µA), el resultado cae muy por debajo en los
tres casos.

Decisión: NO se implementa el MOSFET de canal P (AO3401A) ni el
transistor NPN (2N3904) comprados como respaldo. La hibernación por
software del panel (display.hibernate()) ya deja el consumo del módulo
completo al mismo nivel que si la pantalla no estuviera conectada;
forzar los pines en LOW no aporta mejora medible. Ambos componentes se
conservan sin soldar por si una revisión futura de hardware (ej. cambio
de módulo de pantalla) lo amerita, pero no forman parte del diseño
actual.

Autonomía estimada revisada: ~0.8 mAh/día (sleep 7µA + ciclo activo
~0.63mAh estimado en 00-contexto-tecnico.md, pendiente de medir en Fase
2). Con 200 mAh utilizables: ~250 días por carga, muy por encima del
objetivo de "semanas" de autonomía.

Nota de campo: el módulo mostró comportamiento anómalo (voltaje sin
sentido físico, lectura ~5.7V) al alimentarse solo por USB sin batería
conectada. No se investigó a fondo por no ser el escenario de uso real
(producción siempre corre con batería). Con batería, comportamiento
consistente y esperado en las tres mediciones.

## D-007 — Backend: hosting y stack (2026-08-01)
VPS Hetzner CX22 (~€4.5/mes), siempre encendido. Python + FastAPI + Pillow.
Se descarta serverless por cold-start (batería del dispositivo no lo tolera)
y self-hosting en casa por dependencia de la conexión doméstica como
camino crítico sin monitoreo remoto.

## D-008 — Protocolo dispositivo↔servidor (2026-08-01)
HTTPS REST, un solo endpoint `GET /device/wake`. Se descarta MQTT (no hay
push en tiempo real que justifique conexión persistente ni broker) y TCP
crudo (reimplementaría TLS a mano, innecesario).

## D-009 — Formato del payload (2026-08-01)
Body binario crudo (5000 bytes, 1bpp), sin JSON ni base64. Metadata en
headers custom: `X-Sleep-Seconds`, `X-Fw-Version`, `X-Image-Checksum`
(CRC32 hex). El firmware lee el checksum antes de leer el body completo
y corta la conexión si coincide con el último pintado (ahorra el ciclo
activo completo en días sin imagen nueva).

## D-010 — Política de reintentos de Wi-Fi (2026-08-01)
Backoff exponencial entre ciclos completos de despertar: 15min → 30 →
60 → 120, techo en 4h. Al conectar exitosamente, el intervalo vuelve al
valor base. Estado (intervalo actual, último éxito) persiste en NVS
para sobrevivir el deep sleep. Se descarta backoff corto dentro del
mismo despertar por gastar batería reintentando toda la noche si el
hotspot está apagado por horas.

## D-011 — Portal cautivo y almacenamiento de redes (2026-08-01)
`WiFiMulti` (core oficial arduino-esp32) para guardar y probar varias
redes por orden, + portal cautivo propio (`WebServer` + `DNSServer`)
como fallback si todas fallan. Se descarta WiFiManager (tzapu) por
soportar una sola red a la vez, contrario al requisito ya fijado.

## D-012 — Mecanismo OTA (2026-08-01)
`HTTPUpdate` (arduino-esp32) sobre el mismo cliente TLS del endpoint de
imagen. Rollback vía particiones OTA duales nativas de ESP-IDF:
`esp_ota_mark_app_valid_cancel_rollback()` se llama solo tras un ciclo
completo exitoso (Wi-Fi + descarga + pintado), cumpliendo D-004.
Requiere tabla de particiones custom (4MB flash del XIAO ESP32C3, no
usar el default sin OTA).

## D-013 — Autenticación del endpoint (2026-08-01)
Token estático de 32+ bytes aleatorios, embebido en firmware, header
`X-Device-Token`, verificado contra variable de entorno en el servidor.
Sin rotación automática — amenaza real es baja (URL no listada, HTTPS).
Rotación, si algún día se necesita, es manual vía un OTA con el nuevo
valor.

## D-014 — Ingesta de Telegram y persistencia (2026-08-01)
Webhook (no polling) — el servidor ya está siempre encendido con HTTPS.
Estado guardado como archivo plano: buffer binario (`current.bin`) +
JSON de metadata (checksum, sleep_seconds, updated_at). Se descarta
SQLite por no resolver ningún problema real a esta escala (1 imagen
activa, sin concurrencia, sin historial requerido).

## D-015 — Origen de X-Fw-Version en el esqueleto del servidor (2026-08-01)
El header `X-Fw-Version` (D-009) no se guarda en `current.json`: no es
metadata de la imagen, es la versión de firmware que el servidor anuncia
como compatible, independiente de qué imagen esté cargada. Se sirve desde
una variable de entorno `FW_VERSION` con default `0.1.0`, separada del
archivo de estado por imagen.

## D-016 — Pin de Pillow fijado a la versión del VPS de producción (2026-08-01)
El VPS de producción corre Ubuntu 24.04 + Python 3.12, no la versión 3.14
del entorno de desarrollo local usada al armar el esqueleto del servidor
(D-014/D-015). El rango abierto `pillow>=10.3,<13.0` en `requirements.txt`
se había ampliado solo para sortear la falta de wheel precompilado de
Pillow 10.3 bajo Python 3.14 local, que forzaba compilar desde código
fuente y fallaba por headers de libjpeg ausentes. Bajo Python 3.12 ese
problema no existe.

Se fija `pillow==12.3.0`: última versión estable publicada en PyPI al
momento de esta decisión, con wheel confirmado
(`pillow-12.3.0-cp312-cp312-manylinux_2_27_x86_64.manylinux_2_28_x86_64.whl`)
para Python 3.12 en Linux x86_64.

Verificación: se creó un venv con `python3.12 -m venv .venv312` y se instaló
`requirements.txt` — Pillow instaló desde el wheel de arriba, sin paso de
compilación. Los 6 casos de aceptación documentados en `server/README.md`
(health sin auth, `/device/wake` sin token, con token incorrecto, con
token correcto sin `current.bin`, y con token correcto y datos de prueba
verificando los headers `X-Sleep-Seconds`/`X-Fw-Version`/`X-Image-Checksum`
y los 5000 bytes del body) pasaron corriendo bajo ese venv de 3.12.
El venv y los archivos de prueba (`data/current.bin`, `data/current.json`)
se borraron después de verificar; no quedan versionados.

## D-017 — DC reubicado a D1/GPIO3 para balancear el conector (2026-08-01)
Modifica el pinout base de D-001: se mueve **solo** DC, de D7/GPIO20 a
D1/GPIO3. DIN (D5/GPIO7), CLK (D4/GPIO6), CS (D10/GPIO10), RST (D3/GPIO5)
y BUSY (D2/GPIO4) quedan exactamente igual.

Motivo: balancear el conector físico en 3 pines por lado — DIN/CLK/CS del
lado derecho, RST/BUSY/DC del lado izquierdo — para facilitar el cableado
a mano. DC no es parte del periférico SPI nativo (a diferencia de
DIN/CLK/CS, que si lo son y por eso no se tocan), así que reubicarlo no
tiene costo de rendimiento ni pierde ruteo por hardware.

GPIO3 no es strapping pin (los que se evitan en el C3 son GPIO2/8/9,
según D-001) ni forma parte de UART0 (GPIO20/21, también evitado en
D-001) — confirmado contra la documentación de Espressif/Seeed antes de
fijarlo.

Nota sobre D-001: ese pinout original dejaba D1/GPIO3 libre y "reservado
para gate de MOSFET". Esa reserva ya no aplica: D-006 decidió no
implementar el MOSFET de canal P (la hibernación por software del panel
resultó suficiente), así que GPIO3 quedó libre de facto antes de esta
decisión. D-001 no se edita; esta nota documenta por qué D-017 no
contradice esa entrada.

DC no participaba en los holds de deep sleep de D-002 (solo RST y BUSY
los tenían) y sigue sin participar — sin cambio de estrategia ahí.

## D-018 — Pipeline de imagen: texto adaptativo y convención de bit 1bpp (2026-08-01)

**Modo texto — tamaño adaptativo + Noto Sans.** Lienzo 200×200 blanco,
texto negro, margen de 10px por lado (área útil 180×180). Empieza en
fuente 32px, envuelve por palabra al ancho disponible, y si el bloque no
cabe en alto reduce de 2 en 2px hasta un mínimo de 12px. Si ni al mínimo
cabe, trunca el texto carácter por carácter agregando "..." hasta que
quepa, en vez de fallar o cortar a la mitad. Centrado horizontal y
vertical del bloque completo. Fuente: Noto Sans Regular, con lista de
rutas candidatas y variable de entorno `NOTO_SANS_PATH` de override,
porque la ruta real difiere entre el entorno de desarrollo (Fedora, sin
`fonts-noto-core`) y el VPS de producción (Ubuntu 24.04, con
`fonts-noto-core`) — ver `server/README.md` para el comando de
instalación exacto. Implementado en `server/app/pipeline.py`.

**Convención de bit del empaquetado 1bpp — verificada, no asumida.**
Se inspeccionó el código fuente de GxEPD2 tal como está vendorizado en
`firmware/test-consumo/.pio/libdeps/seeed_xiao_esp32c3/GxEPD2/src/`:

- `GxEPD2_BW.h::drawPixel()`: con `color != 0` (GxEPD_WHITE) fija el bit a
  1; con `color == 0` (GxEPD_BLACK) lo deja en 0. La posición del bit es
  `1 << (7 - x % 8)` — MSB-first, el primer píxel de cada grupo de 8 cae
  en el bit más significativo.
- `GxEPD2_BW.h::fillScreen()`: blanco llena el byte con `0xFF`, negro con
  `0x00`. Mismo criterio a nivel de byte completo.
- `epd/GxEPD2_154_D67.cpp::_writeImage()`: transfiere el buffer recibido
  al controlador tal cual, sin invertir (salvo que se pida `invert=true`
  explícitamente, que el firmware de este proyecto no usa). El buffer que
  arma el servidor debe seguir entonces la misma convención que el
  framebuffer interno de GxEPD2, no la inversa.

Convención fijada para el buffer de 5000 bytes que produce el servidor:
**1 = blanco, 0 = negro, MSB-first, row-major.** Se verificó además que
`PIL.Image.convert("1", ...).tobytes()` empaqueta con exactamente esta
convención (pixel 255/blanco → bit 1, pixel 0/negro → bit 0, MSB-first),
así que el pipeline no arma los bytes a mano — usa el empaquetado nativo
de Pillow. Esto es crítico: si el firmware alguna vez cambia de método
para pintar el buffer (ej. deja de usar `writeImage`/`drawImage` directo y
pasa por otra ruta de GxEPD2), hay que re-verificar esta convención contra
el nuevo método antes de asumir que sigue aplicando — de lo contrario la
imagen sale invertida o corrida.

Implementado en `server/app/pipeline.py`, probado con
`server/scripts/probar_pipeline.py` (foto no cuadrada con center-crop,
texto corto, texto largo multi-oración, y un texto deliberadamente
gigante para forzar el camino de truncado). No se agregó ninguna
dependencia nueva a `requirements.txt`: Pillow 12.3.0, ya fijada en
D-016, cubre dithering, redimensionado y renderizado de texto TrueType.

## D-019 — Webhook de Telegram: librería, seguridad y escritura atómica (2026-08-01)

**Librería: `python-telegram-bot` v22.8 sobre llamadas HTTP crudas.**
Se descarta reimplementar el webhook a mano (parseo de `Update`, manejo de
`getFile`/descarga de fotos, `sendMessage`) porque la librería ya resuelve
correctamente los casos borde del API de Telegram (versionado del schema,
reintentos HTTP vía `httpx`, tipos fuertes para `Update`/`Message`/`File`)
sin costo real: es Python puro (`python_telegram_bot-22.8-py3-none-any.whl`,
sin compilación, wheel universal — no depende de la versión de Python del
VPS como sí le pasaba a Pillow en D-016). Versión fijada tras instalarla en
un venv de Python 3.12 (misma versión que el VPS de producción, D-007) y
confirmar que resuelve desde wheel sin pasos de compilación.

**Integración con el ciclo de vida de FastAPI — patrón documentado, no
inventado.** Se siguió el ejemplo oficial de la librería
`examples/customwebhookbot/starlettebot.py` (Starlette y FastAPI comparten
el mismo contrato de `lifespan` de ASGI, así que el patrón traslada
directo): `Application.builder().token(...).updater(None).build()` — se
pasa `updater(None)` porque no se usa el `Updater` propio de la librería
(ni su polling ni su servidor de webhook incorporado); las actualizaciones
llegan por la ruta propia de FastAPI y se empujan a `update_queue`. El
`lifespan` de FastAPI hace `async with telegram_application: await
telegram_application.start(); yield; await telegram_application.stop()` —
el `async with` inicializa (`initialize()`) y apaga (`shutdown()`) el bot;
`start()`/`stop()` arrancan y frenan la tarea de fondo que consume
`update_queue` y despacha a los handlers. Responder rápido (200) y dejar
que el procesamiento (potencialmente lento: dithering, ajuste de tamaño de
fuente, la llamada a `reply_text`) corra en la tarea de fondo es también el
comportamiento correcto para un webhook — Telegram espera una respuesta
rápida.

**Consecuencia a tener presente:** `Application.initialize()` llama a
`Bot.initialize()`, que llama a `get_me()` — un request real contra la API
de Telegram para validar el token. Esto significa que el servidor, incluso
en desarrollo local sin registrar el webhook contra Telegram todavía,
necesita un `TELEGRAM_BOT_TOKEN` real (de BotFather) y conexión a internet
para arrancar. No se evadió esto con una inicialización perezosa porque
hacerlo hubiera sido apartarse del ciclo de vida documentado de la
librería. La verificación de esta tarea corrió con `Bot._post` parcheado
(sin tocar `main.py`) para no depender de un token real — ver detalle en
el reporte de la tarea.

**Seguridad del webhook — nueva decisión, no existía antes.**

1. *Ruta no adivinable:* `/telegram/webhook/{TELEGRAM_BOT_TOKEN}` — el
   token del propio bot forma parte de la ruta, patrón estándar de
   despliegues de python-telegram-bot (evita rutas genéricas tipo
   `/telegram/webhook` que cualquiera podría probar a ciegas).
2. *Validación del secret token:* Telegram permite fijar un
   `secret_token` al registrar el webhook (parámetro de `setWebhook`, no
   invocado todavía por este servidor — ver nota abajo) y lo reenvía en el
   header `X-Telegram-Bot-Api-Secret-Token` en cada request. El endpoint
   compara ese header contra `TELEGRAM_WEBHOOK_SECRET` con
   `secrets.compare_digest` (mismo patrón que `X-Device-Token` en D-013) y
   responde `401` si falta o no coincide, **antes** de tocar el `Update` o
   la cola de la librería. Las dos capas son independientes: adivinar la
   ruta no alcanza sin el secreto, y viceversa.

No se llama a `bot.set_webhook()` desde el código del servidor todavía:
registrar el webhook contra Telegram de verdad requiere el VPS desplegado
con dominio y TLS válido (Telegram exige HTTPS), que es tarea aparte. El
comando manual para cuando eso exista queda documentado en
`server/README.md`.

**Escritura atómica de `current.bin`/`current.json` — nueva decisión.**
`pipeline.guardar()` (D-018) escribe directo con `Path.write_bytes()` /
`Path.write_text()`, sin swap atómico — correcto para
`scripts/probar_pipeline.py` (proceso de un solo uso, sin concurrencia
real) pero insuficiente para el webhook, que corre en el mismo proceso
que ya sirve `/device/wake`: sin atomicidad, una lectura de `/device/wake`
concurrente con una escritura del webhook podría leer un archivo a medio
escribir, o un crash a mitad del guardado podría dejar `current.bin`
corrupto. Por eso `server/app/main.py` no llama a `pipeline.guardar()`;
implementa su propio `_guardar_atomico()`: escribe a un archivo temporal
único (`tempfile.mkstemp(dir=DATA_DIR, ...)`, mismo directorio que el
destino para garantizar mismo filesystem) y hace `os.replace()` al final
— atómico en POSIX. `pipeline.py` no se tocó, según el alcance de esta
tarea.

**Manejo de contenido no soportado.** Tres `MessageHandler` registrados en
orden: `filters.PHOTO`, `filters.TEXT & ~filters.COMMAND`, y un
`filters.ALL` al final como catch-all. python-telegram-bot despacha al
primer handler cuyo filtro matchea dentro de un grupo, así que cualquier
mensaje que no sea foto ni texto plano (sticker, audio, documento,
comando, etc.) cae en el catch-all, que responde un mensaje corto en vez
de fallar en silencio o intentar procesarlo como texto.

Implementado en `server/app/main.py` y `server/app/config.py` (lectura de
`TELEGRAM_BOT_TOKEN`/`TELEGRAM_WEBHOOK_SECRET`, mismo patrón de
fail-fast que `DEVICE_AUTH_TOKEN`). No se tocó `pipeline.py` ni ningún
archivo de `/firmware` o `/hardware`.

## D-020 — Capa de conectividad Wi-Fi del firmware final: detalles menores (2026-08-01)

Proyecto PlatformIO nuevo en `firmware/llavero/` (firmware final del
dispositivo), separado de `firmware/test-consumo/` (diagnóstico de Fase 1,
ya cerrado, no se toca). Esta tarea implementa solo `WiFiMulti` + portal
cautivo + backoff exponencial (D-010/D-011); cliente HTTPS, pintado de
pantalla y OTA quedan para tareas siguientes. Decisiones menores no
cubiertas por D-010/D-011, ninguna las contradice:

- **AP del portal cautivo sin contraseña.** SSID `Llavero-Setup`, abierto.
  El riesgo de que alguien más se conecte a ese AP es bajo (ventana de 5
  minutos, sin datos sensibles expuestos salvo el formulario para *cargar*
  una red — no hay forma de leer las redes ya guardadas desde el portal) y
  una password añadiría fricción justo en el flujo que existe para
  facilitar la primera configuración.
- **Timeout de 5 minutos en modo AP** antes de rendirse y aplicar backoff,
  y **15 segundos por intento de `WiFiMulti.run()`** contra las redes
  guardadas — ambos son los valores sugeridos en el planteamiento de la
  tarea, fijados como constantes (`TIMEOUT_PORTAL_MS`, `TIMEOUT_WIFI_MS`)
  en `src/main.cpp`.
- **Namespace de Preferences (NVS): `llavero`.** Claves: `netCount`
  (cantidad de redes guardadas), `ssid0..4`/`pass0..4` (hasta `MAX_REDES =
  5`), `backoffS` (intervalo de backoff vigente en segundos). Todas dentro
  del límite de 15 caracteres que impone NVS.
- **Política de sobreescritura cuando ya hay 5 redes guardadas y llega una
  SSID nueva:** se sobreescribe el índice 0 (la primera que se guardó).
  No estaba especificado; con un dispositivo de un solo usuario, llegar a
  5 redes simultáneas ya es un caso límite, así que cualquier política
  simple es aceptable — se documenta para que quede claro que es
  intencional, no un bug, si algún día parece que una red guardada
  "desapareció".
- **Tras guardar credenciales nuevas en el portal, el dispositivo hace
  `ESP.restart()` en vez de intentar conectar en el mismo ciclo.** Más
  simple y más confiable: recarga `WiFiMulti` desde cero con la lista
  actualizada en vez de mutar su estado en caliente.
- **Tras una conexión exitosa, el dispositivo resetea el backoff y
  duerme el intervalo base (900 s) directo**, sin todavía llamar a un
  cliente HTTPS ni pintar la pantalla — es el límite explícito del
  alcance de esta tarea. La siguiente tarea (cliente HTTPS) reemplaza ese
  `dormir(INTERVALO_BASE_S)` por el ciclo completo (descarga + pintado +
  dormir el tiempo que indique el servidor, D-009).

Pinout: se usó exactamente D-001 actualizado por D-017 (documentado en un
comentario en `src/main.cpp`, no como constantes activas — esta tarea no
toca el bus SPI ni la pantalla, así que declarar pines sin usar hubiera
sido código muerto; quedan documentados en texto para que la tarea de
pintado los use sin tener que releer `decisiones.md`).

Verificación: `pio run` dentro de `firmware/llavero/` compila limpio (0
errores, 0 warnings) en una recompilación completa desde cero. La lógica
de Wi-Fi en sí (conectar a una red real, levantar el AP y que un celular
se conecte) no se puede probar sin hardware — ver `firmware/llavero/README.md`
para el flujo de prueba manual que le toca al usuario.

## D-021 — Pinout de PCB final: seis pines contiguos, reintroduce GPIO21 (2026-08-01)

El usuario va a fabricar una PCB en la máquina de su escuela. Para que el
layout use 6 pines contiguos de un solo lado del conector del XIAO
ESP32C3 (en vez del agrupamiento 3-y-3 de D-017, pensado para facilitar
cableado a mano en breadboard, no trazado de PCB), se fija un pinout
nuevo, distinto y paralelo al de D-001/D-017:

```
BUSY = D1 / GPIO3
RST  = D2 / GPIO4
DC   = D3 / GPIO5
CS   = D4 / GPIO6
CLK  = D5 / GPIO7
DIN  = D6 / GPIO21
VCC  -> 3V3, GND -> GND (sin cambio)
```

**Esto no reemplaza D-001/D-017.** Ese pinout sigue vigente, cerrado, y
sin tocar en `firmware/test-consumo/` (ligado a la medición de consumo de
D-006). D-021 aplica únicamente a `firmware/llavero/` (firmware final) y
a la PCB que se fabrique. `00-contexto-tecnico.md` documenta los dos
pinouts por separado, con una nota explícita de que la diferencia es
intencional, no un error de sincronización entre archivos.

**La disyuntiva real: GPIO2 (strapping) vs. GPIO21 (UART0 TX).** Encajar
6 señales en pines contiguos del XIAO fuerza a elegir entre D0/GPIO2 (para
completar el rango D1-D6 por el otro extremo) o D6/GPIO21 (para completar
el rango D1-D6 tal como se pidió). D-001 ya había evitado ambos por
motivos distintos: GPIO2 por ser strapping pin, GPIO21 por ser U0TXD usado
por el bootloader ROM. Entre los dos, se elige GPIO21 porque el riesgo es
categóricamente menor:

- **GPIO2 (strapping) controla el modo de arranque del chip.** Según la
  referencia de GPIO del ESP-IDF para ESP32-C3 (verificado ahora,
  <https://docs.espressif.com/projects/esp-idf/en/stable/esp32c3/api-reference/peripherals/gpio.html>):
  *"Strapping pin: GPIO2, GPIO8 and GPIO9 are strapping pins."* Confirmado
  además contra la página de esptool sobre selección de modo de arranque
  (<https://docs.espressif.com/projects/esptool/en/latest/esp32c3/advanced-topics/boot-mode-selection.html>),
  que documenta cómo GPIO8/GPIO9 determinan si el chip entra en modo de
  descarga serie. Un periférico externo cargando esa línea en el momento
  del reset puede, en el peor caso, dejar el chip entrando siempre en modo
  de descarga y parecer "brickeado" sin un análisis cuidadoso. **GPIO21 no
  aparece en ninguna de las dos páginas como strapping pin** — no participa
  en la selección de modo de arranque en absoluto.
- **GPIO21 es U0TXD, pero el XIAO ESP32C3 no usa UART0 para `Serial`.**
  Verificado contra la definición de la placa que usa PlatformIO
  (`~/.platformio/platforms/espressif32/boards/seeed_xiao_esp32c3.json`),
  que fija `-DARDUINO_USB_MODE=1` y `-DARDUINO_USB_CDC_ON_BOOT=1`. Siguiendo
  esas macros hasta el núcleo arduino-esp32 instalado
  (`cores/esp32/HardwareSerial.h` y `cores/esp32/HWCDC.h`): con
  `ARDUINO_USB_CDC_ON_BOOT=1`, el objeto UART0 clásico se renombra a
  `Serial0` (no `Serial`), y `Serial` pasa a ser una instancia de `HWCDC`
  — el periférico USB Serial/JTAG nativo del ESP32-C3, un bus físicamente
  distinto de UART0/GPIO20-21. Esto es justamente lo que ya se documentó
  como ventaja del XIAO ESP32C3 (sin chip USB-serie aparte, como CH340 o
  CP2102): programación y consola serie van por el USB nativo del SoC, no
  por GPIO20/21. En consecuencia, el logging por `Serial` que pide esta
  tarea y el monitor serie de PlatformIO **no comparten bus con GPIO21** —
  UART0 queda sin usar por completo en este firmware. El único riesgo real
  que queda es que el bootloader ROM (antes de que arranque el firmware de
  Arduino) puede escribir bytes de diagnóstico en U0TXD durante el arranque
  más temprano; como DIN es una entrada del panel e-ink que solo se
  interpreta cuando CS está activo y CLK conmuta, ese tráfico espurio no
  llega a latchearse como datos SPI reales.

**Pinout aplicado** en `firmware/llavero/src/main.cpp` (constantes
`PIN_BUSY`, `PIN_RST`, `PIN_DC`, `PIN_CS`, `PIN_CLK`, `PIN_DIN`) y en la
tabla nueva de `00-contexto-tecnico.md`. La lógica de Wi-Fi de la tarea
anterior (D-020) no se tocó — solo cambiaron los valores de pines, que
además siguen sin usarse todavía en este firmware (la tarea de
pintado/HTTPS es la que los va a consumir).

Verificación: `pio run` dentro de `firmware/llavero/` compila limpio (0
errores, 0 warnings) en una recompilación completa desde cero con los
pines nuevos. `firmware/test-consumo/` no se tocó.

## D-022 — Servidor desplegado en producción (2026-08-02)

Complementa D-007/D-008/D-009: el backend descrito ahí ya está desplegado
y corriendo de verdad, no solo diseñado.

**Hosting:** VPS Hetzner CX23, datacenter Helsinki, IP `65.108.156.195`.
Nota: D-007 había estimado un CX22; el plan efectivamente contratado es
CX23. D-007 no se edita — esta entrada documenta el dato real de
producción.

**Dominio y TLS:** `caspiandomain.dev`, DNS apuntado directo al VPS (sin
proxy de Cloudflare por delante). Caddy sirve como reverse proxy hacia
`uvicorn` en `127.0.0.1:8000` y obtiene/renueva el certificado TLS
automáticamente vía Let's Encrypt — sin configuración manual de certbot,
que era justo el motivo de elegir Caddy en vez de nginx+certbot.

**Persistencia y seguridad del proceso:** el servidor corre como
servicio `systemd` (`llavero.service`), habilitado al arranque
(`systemctl enable`) y con `Restart=on-failure` para recuperarse solo si
el proceso muere. Corre bajo un usuario de sistema dedicado no-root
(`llavero`, home propio en `/home/llavero`) — el proceso FastAPI/uvicorn
nunca corre como root, y `uvicorn` solo escucha en `127.0.0.1:8000`
(nunca expuesto directo a internet; todo el tráfico externo entra por
Caddy en 80/443).

**Confirmado end-to-end:** una foto real mandada por Telegram se procesó
por el pipeline (D-018) y quedó servida correctamente por
`GET /device/wake` sobre HTTPS real del dominio de producción — el camino
completo webhook → pipeline → `current.bin`/`current.json` →
`/device/wake` funciona en producción, no solo en pruebas locales.

## D-023 — Cliente HTTPS en el firmware: certificado raíz real y comparación de checksum (2026-08-02)

Agrega a `firmware/llavero/` la llamada a `GET /device/wake` (D-008/D-009)
después de una conexión Wi-Fi exitosa. No incluye pintado de pantalla ni
OTA — el buffer descargado queda en RAM y solo se loggea su tamaño y
checksum; son tareas separadas que vienen después. La lógica de Wi-Fi
existente (D-010/D-011/D-020) no se tocó.

**Hallazgo importante: el certificado raíz real de producción NO es el
que se había asumido.** El planteamiento de la tarea pedía embeber
ISRG Root X1 "verificado contra la documentación oficial, no de memoria".
Se verificó — y **la cadena real que sirve `caspiandomain.dev` en
producción no termina en X1, termina en ISRG Root X2** (confirmado con
`openssl s_client -connect caspiandomain.dev:443 -showcerts` contra el
servidor real):

```
caspiandomain.dev → Let's Encrypt "YE2" → ISRG "Root YE" → ISRG Root X2
```

Se reprodujo la verificación de cadena con `openssl verify` en modo
aislado (`-no-CAstore -no-CApath`, para que no la disimule el almacén de
confianza del sistema): **con solo ISRG Root X1 como raíz confiable, la
verificación falla** (`unable to get local issuer certificate` al llegar
a "Root YE"); con ISRG Root X1 **y** ISRG Root X2 concatenados, verifica
OK. Si se hubiera embebido solo X1 como pedía el planteamiento original,
el cliente HTTPS del dispositivo habría fallado la validación TLS contra
el servidor real de producción — el problema no se habría notado hasta
probar con hardware real.

**Decisión:** `firmware/llavero/src/cert_raiz.h` embebe **ambas** raíces
(ISRG Root X1 RSA y ISRG Root X2 ECDSA), descargadas y verificadas contra
`letsencrypt.org/certs/isrgrootx1.pem` y `letsencrypt.org/certs/isrg-root-x2.pem`
(enlazados desde la página oficial `letsencrypt.org/certificates/`),
confirmando SHA-256 de cada una con `openssl x509 -fingerprint -sha256`
contra los valores públicos conocidos. Un solo `setCACert()` con las dos
concatenadas alcanza: `mbedtls_x509_crt_parse()` (la función que usa
`WiFiClientSecure` internamente, verificado en
`WiFiClientSecure/src/ssl_client.cpp` del core arduino-esp32 instalado)
acepta varios certificados PEM en un mismo buffer y los agrega todos como
raíces de confianza. Esto además deja al dispositivo tolerante si
Let's Encrypt cambia de cuál raíz usa para este dominio en el futuro — no
hace falta elegir una sola de antemano. No se usa `setInsecure()` (D-008).

**APIs verificadas contra el código fuente instalado, no asumidas:**

- `WiFiClientSecure::setCACert(const char*)` — un solo `const char*` con
  el PEM completo (posiblemente varios certificados concatenados).
- `WiFiClientSecure::setTimeout(uint32_t)` y `setHandshakeTimeout(unsigned long)`
  toman **segundos** (multiplican por 1000 internamente) — distinto de
  `HTTPClient::setTimeout(uint16_t)`/`setConnectTimeout(int32_t)`, que
  toman **milisegundos**. Mezclar las unidades sin verificar habría dejado
  timeouts diez o mil veces más cortos o largos de lo esperado.
- `HTTPClient::collectHeaders(headerKeys[], count)` es obligatorio antes
  de `GET()` para poder leer headers de respuesta custom con
  `header()`/`hasHeader()` — sin esto, `X-Sleep-Seconds`/`X-Fw-Version`/
  `X-Image-Checksum` no aparecen aunque el servidor los mande (la API por
  default no expone headers arbitrarios de la respuesta).
- `HTTPClient::GET()` devuelve un código HTTP positivo o un
  `HTTPC_ERROR_*` negativo (fallo de conexión/TLS, no HTTP) — se
  distinguen explícitamente en el manejo de errores.

**Comparación de checksum (D-009).** Nueva clave de NVS `ultimoChecksum`
(namespace `llavero` ya existente). Si `X-Image-Checksum` coincide con el
guardado: loggea "Imagen sin cambios, no se descarga" y no lee el body —
ahorra el ciclo activo completo, tal como plantea D-009. Si es distinto o
no hay checksum guardado: lee el body con `http.getStream().readBytes()`
hacia un buffer de 5000 bytes, verifica el tamaño real leído contra el
esperado (loggea sin crashear si no coincide, y en ese caso NO guarda el
checksum nuevo — para que el próximo ciclo reintente la descarga en vez
de darla por buena), y solo si el tamaño coincide guarda el checksum
nuevo en NVS.

El buffer de 5000 bytes es `static` dentro de la función, no una variable
de pila: es más de la mitad del stack default de la tarea de Arduino en
ESP32 (8 KB), y la función ya corre en medio de un handshake TLS con su
propio uso de stack — un arreglo local de ese tamaño ahí arriesgaba
overflow. `static` lo pone en `.bss`, sin competir con la pila.

**Backoff unificado (D-010).** `consultarServidor()` devuelve `false`
ante *cualquier* fallo — error de conexión/TLS, código HTTP distinto de
200, o headers faltantes (incluye 401 de token incorrecto y 503 de
"todavía no hay imagen"). El llamador solo resetea el backoff si devuelve
`true`; si devuelve `false`, aplica el mismo
`avanzarBackoffYObtenerIntervaloActual()` que ya existía para fallos de
Wi-Fi — un fallo de servidor no se trata distinto, tal como pide la
tarea.

**Token del dispositivo — mismo patrón que las redes Wi-Fi, con una
brecha real.** `X-Device-Token` se guarda en NVS (clave `deviceToken`,
namespace `llavero`), no en el código fuente — evita exponer el token
real de producción en el repo público. Si no hay token guardado, se usa
el placeholder `REEMPLAZAR_CON_TOKEN_REAL` con una advertencia explícita
por Serial (el servidor lo va a rechazar con 401, comportamiento visible,
no un fallo silencioso). **A diferencia de las redes Wi-Fi, todavía no
hay ninguna interfaz (portal cautivo o Serial) para cargar este token sin
recompilar** — el portal cautivo actual solo maneja SSID/password. Como
paso temporal documentado en el README, se flashea una vez un sketch
mínimo separado que solo escribe la clave en NVS, y después se vuelve a
flashear el firmware real. Agregar un campo de token al portal cautivo
(o un comando por Serial) queda pendiente como tarea futura — no se
implementó acá porque la restricción de esta tarea era no tocar la lógica
del portal cautivo existente.

Verificación: `pio run` dentro de `firmware/llavero/` compila limpio (0
errores, 0 warnings) en una recompilación completa desde cero. Flash subió
de 59.3% a 72.1% (mbedTLS/TLS), RAM subió a 13.9% — margen amplio en
ambos. `cert_raiz.h` se verificó byte a byte contra los PEM oficiales
descargados (diff exacto, cero diferencias) y contra `openssl x509`
parseando ambos certificados del bloque embebido. La lógica de red real
(conectar, negociar TLS contra el servidor real, comparar checksums en la
práctica) no se puede probar sin hardware real conectado a internet real
contra el servidor real — ver `firmware/llavero/README.md` para el flujo
de prueba manual.
