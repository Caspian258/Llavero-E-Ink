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
