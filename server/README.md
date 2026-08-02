# Servidor — Llavero E-Ink

Backend del llavero: expone el endpoint que el dispositivo consulta al
despertar. Incluye el esqueleto (health check + `/device/wake` sirviendo
estado desde archivo plano), el pipeline de imagen (`app/pipeline.py`:
foto o texto → `data/current.bin` + `data/current.json`), y el webhook de
Telegram (`app/main.py`) que conecta mensajes reales del bot con ese
pipeline.

## Variables de entorno

| Variable | Obligatoria | Descripción |
|---|---|---|
| `DEVICE_AUTH_TOKEN` | Sí | Token que el dispositivo envía en el header `X-Device-Token`. Si no está seteada, el servidor falla al arrancar. |
| `FW_VERSION` | No | Valor devuelto en el header `X-Fw-Version`. Default: `0.1.0`. |
| `TELEGRAM_BOT_TOKEN` | Sí | Token del bot, de BotFather. Se usa para autenticar contra la API de Telegram y forma parte de la ruta del webhook (D-019). Si no está seteada, el servidor falla al arrancar. |
| `TELEGRAM_WEBHOOK_SECRET` | Sí | Secreto que se registra como `secret_token` del webhook y que Telegram reenvía en el header `X-Telegram-Bot-Api-Secret-Token` en cada request. Si no está seteada, el servidor falla al arrancar. |

## Correr localmente

```bash
cd server
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt

export DEVICE_AUTH_TOKEN="un-token-de-prueba-cualquiera"
export TELEGRAM_BOT_TOKEN="el-token-real-de-botfather"
export TELEGRAM_WEBHOOK_SECRET="un-secreto-largo-y-aleatorio"
uvicorn app.main:app --reload --port 8000
```

Si falta cualquiera de las tres, el arranque falla con un `RuntimeError`
explicando qué falta — no arranca con un token vacío.

**`TELEGRAM_BOT_TOKEN` tiene que ser un token real de BotFather incluso en
desarrollo local**, aunque todavía no se registre el webhook contra
Telegram: `python-telegram-bot` valida el token llamando a `getMe()` al
arrancar (parte de su ciclo de vida estándar, D-019), y esa llamada sí
sale a internet. Un token inventado hace que el arranque falle con
`InvalidToken`.

El servidor crea `server/data/` automáticamente si no existe (no se versiona,
ver `.gitignore`).

## Pruebas con curl

Con el servidor corriendo en otra terminal (`DEVICE_AUTH_TOKEN` exportada
en la terminal donde corre uvicorn):

**1. Health check, sin token:**

```bash
curl -i http://localhost:8000/health
# 200, {"status":"ok"}
```

**2. `/device/wake` sin header de token:**

```bash
curl -i http://localhost:8000/device/wake
# 401
```

**3. `/device/wake` con token incorrecto:**

```bash
curl -i http://localhost:8000/device/wake -H "X-Device-Token: incorrecto"
# 401
```

**4. `/device/wake` con token correcto pero sin `current.bin` todavía:**

```bash
curl -i http://localhost:8000/device/wake -H "X-Device-Token: un-token-de-prueba-cualquiera"
# 503
```

**5. Crear datos de prueba y repetir la llamada:**

```bash
mkdir -p data
python3 -c "open('data/current.bin', 'wb').write(bytes(5000))"
cat > data/current.json <<'EOF'
{"checksum": "deadbeef", "sleep_seconds": 86400, "updated_at": "2026-08-01T00:00:00Z"}
EOF

curl -i http://localhost:8000/device/wake \
  -H "X-Device-Token: un-token-de-prueba-cualquiera" \
  -o /tmp/current.bin

# Esperado: 200, headers X-Sleep-Seconds: 86400, X-Fw-Version: 0.1.0,
# X-Image-Checksum: deadbeef, y /tmp/current.bin con 5000 bytes.
wc -c /tmp/current.bin
```

## Pipeline de imagen (foto o texto → buffer para el dispositivo)

`app/pipeline.py` convierte una foto o un texto en el par de archivos que
`/device/wake` sirve: `data/current.bin` (5000 bytes, 1bpp, ver convención
de bit en D-018) y `data/current.json` (checksum + metadata, D-009). Se
prueba de forma standalone con `scripts/probar_pipeline.py`, sin depender
de Telegram.

Como `pipeline.py` importa las rutas desde `app/config.py`, el script
necesita la misma variable de entorno que el servidor:

```bash
cd server
source .venv/bin/activate  # venv de "Correr localmente" arriba
export DEVICE_AUTH_TOKEN="un-token-de-prueba-cualquiera"

# Modo foto: recorta al centro (sin deformar), escala a 200x200,
# escala de grises, dithering Floyd-Steinberg.
python scripts/probar_pipeline.py --imagen ruta/a/foto.jpg

# Modo texto: tamaño de fuente adaptativo (32px a 12px), word wrap,
# centrado, trunca con "..." si ni el tamaño mínimo alcanza.
python scripts/probar_pipeline.py --texto "Buenos días, mi amor"
```

Ambos casos imprimen el checksum CRC32 calculado y dejan
`data/current.bin` / `data/current.json` listos — los mismos que ya lee
`/device/wake`. `data/` no se versiona (ver `.gitignore`).

### Fuente Noto Sans (modo texto)

El pipeline busca Noto Sans Regular en, en este orden:

1. La ruta en la variable de entorno `NOTO_SANS_PATH`, si está definida.
2. `/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf` — la ruta que
   publica el paquete apt `fonts-noto-core` en Ubuntu 24.04 (el VPS de
   producción, D-007). Instalar con:

   ```bash
   sudo apt install fonts-noto-core
   ```

   Después de instalar, conviene confirmar la ruta real en el VPS (los
   nombres de archivo pueden variar entre versiones del paquete):

   ```bash
   dpkg -L fonts-noto-core | grep 'NotoSans-Regular'
   ```

3. `/usr/share/fonts/google-noto-vf/NotoSans[wght].ttf` — donde vive Noto
   Sans en el entorno de desarrollo usado para esta tarea (Fedora, paquete
   `google-noto-sans-vf-fonts`). Es una fuente variable; Pillow la abre
   directo y resuelve a la instancia "Regular" por default. `apt install
   fonts-noto-core` no existe en Fedora — esta ruta es solo para
   desarrollo local, no aplica al VPS.

Si ninguna ruta existe, el pipeline falla con `FileNotFoundError` y lista
las rutas que probó, en vez de fallar en silencio con una fuente
default de baja calidad.

## Webhook de Telegram

### 1. Obtener el token del bot (BotFather)

En Telegram, hablar con [@BotFather](https://t.me/BotFather):

1. `/newbot` (o `/token` si el bot ya existe) y seguir las instrucciones.
2. BotFather devuelve un token con forma `123456789:AA...` — ese es
   `TELEGRAM_BOT_TOKEN`.

### 2. Generar el secreto del webhook

Cualquier string aleatorio de al menos 32 bytes sirve como
`TELEGRAM_WEBHOOK_SECRET` (Telegram acepta hasta 256 caracteres,
`A-Z`, `a-z`, `0-9`, `_` y `-`):

```bash
python3 -c "import secrets; print(secrets.token_urlsafe(32))"
```

### 3. Registrar el webhook contra el VPS (recién cuando esté desplegado)

Esta tarea **no** hace este paso — requiere el VPS con dominio y TLS
(Telegram exige HTTPS). Una vez desplegado, registrar con:

```bash
curl -s "https://api.telegram.org/bot$TELEGRAM_BOT_TOKEN/setWebhook" \
  -d "url=https://tu-dominio.tld/telegram/webhook/$TELEGRAM_BOT_TOKEN" \
  -d "secret_token=$TELEGRAM_WEBHOOK_SECRET"
```

Verificar el registro:

```bash
curl -s "https://api.telegram.org/bot$TELEGRAM_BOT_TOKEN/getWebhookInfo"
```

### 4. Probar el webhook en local con curl (sin Telegram real)

Con el servidor corriendo (`TELEGRAM_BOT_TOKEN` y `TELEGRAM_WEBHOOK_SECRET`
exportadas en la terminal de uvicorn), la ruta es
`/telegram/webhook/$TELEGRAM_BOT_TOKEN` (D-019). Estos payloads son el
formato real de `Update` que manda Telegram.

**a. Sin el header de secret token → 401:**

```bash
curl -i -X POST "http://localhost:8000/telegram/webhook/$TELEGRAM_BOT_TOKEN" \
  -H "Content-Type: application/json" \
  -d '{
    "update_id": 100001,
    "message": {
      "message_id": 1,
      "date": 1735689600,
      "chat": {"id": 12345678, "type": "private", "first_name": "Prueba"},
      "from": {"id": 12345678, "is_bot": false, "first_name": "Prueba"},
      "text": "Buenos días, mi amor"
    }
  }'
# 401
```

**b. Con el header, pero con un tipo de mensaje no soportado (sticker) →
200 (ack a Telegram) y NO toca `current.bin`/`current.json`:**

```bash
curl -i -X POST "http://localhost:8000/telegram/webhook/$TELEGRAM_BOT_TOKEN" \
  -H "Content-Type: application/json" \
  -H "X-Telegram-Bot-Api-Secret-Token: $TELEGRAM_WEBHOOK_SECRET" \
  -d '{
    "update_id": 100002,
    "message": {
      "message_id": 2,
      "date": 1735689600,
      "chat": {"id": 12345678, "type": "private", "first_name": "Prueba"},
      "from": {"id": 12345678, "is_bot": false, "first_name": "Prueba"},
      "sticker": {
        "file_id": "AgADBAAD_fake",
        "file_unique_id": "AQAD_fake",
        "width": 512,
        "height": 512,
        "is_animated": false,
        "is_video": false,
        "type": "regular"
      }
    }
  }'
# 200 (Telegram recibe el ack). El bot responde en el chat que solo
# procesa fotos o texto; current.bin/current.json quedan intactos.
```

**c. Con el header y un mensaje de texto real → procesa, actualiza
`current.bin`/`current.json` (el procesamiento es asíncrono, revisar el
checksum un segundo después):**

```bash
curl -i -X POST "http://localhost:8000/telegram/webhook/$TELEGRAM_BOT_TOKEN" \
  -H "Content-Type: application/json" \
  -H "X-Telegram-Bot-Api-Secret-Token: $TELEGRAM_WEBHOOK_SECRET" \
  -d '{
    "update_id": 100003,
    "message": {
      "message_id": 3,
      "date": 1735689600,
      "chat": {"id": 12345678, "type": "private", "first_name": "Prueba"},
      "from": {"id": 12345678, "is_bot": false, "first_name": "Prueba"},
      "text": "Buenos días, mi amor"
    }
  }'
# 200

sleep 1
cat data/current.json  # el checksum debe haber cambiado
```

Nota: correr el servidor de verdad con `uvicorn` para estas pruebas
requiere un `TELEGRAM_BOT_TOKEN` real (ver arriba, por `getMe()`). Las
respuestas que el bot manda de vuelta a Telegram (`reply_text`) sí
requieren que el token sea válido y que Telegram pueda alcanzar al chat
indicado — con un `chat.id` de prueba que no existe, el `POST` al webhook
igual devuelve 200 (Telegram ya recibió el update) y `current.bin` se
actualiza igual, pero el intento de responder en el chat falla en el log
del servidor sin tumbarlo.

`data/` no se versiona (ver `.gitignore`).

## OTA: subir un firmware nuevo (D-012/D-027)

`GET /device/firmware` sirve el binario OTA más reciente que haya en
`server/data/firmware/`, con el mismo `X-Device-Token` que `/device/wake`
(D-013). No hay pipeline de CI/CD — la subida es manual, a propósito
(fuera de alcance de esta tarea).

### 1. Compilar el firmware con la versión nueva

Antes de compilar, subir a mano `FW_VERSION` en
`firmware/llavero/src/main.cpp` (ej. de `"0.1.0"` a `"0.2.0"`):

```bash
cd firmware/llavero
pio run
```

El binario queda en `.pio/build/seeed_xiao_esp32c3/firmware.bin`.

### 2. Subir el binario al servidor con el nombre correcto

El endpoint solo reconoce archivos con el patrón exacto
`llavero-<version>.bin` (ej. `llavero-0.2.0.bin`) dentro de
`server/data/firmware/` — cualquier otro nombre se ignora en silencio (no
rompe nada, simplemente no se sirve). La versión se compara numéricamente
por segmento, no como texto, así que `llavero-0.10.0.bin` se reconoce
correctamente como más nueva que `llavero-0.9.0.bin`.

```bash
# renombrar localmente antes de subir
cp firmware/llavero/.pio/build/seeed_xiao_esp32c3/firmware.bin /tmp/llavero-0.2.0.bin

# subir por scp al VPS (D-022: usuario de sistema "llavero", repo en
# /home/llavero/Llavero-E-Ink)
scp /tmp/llavero-0.2.0.bin llavero@<IP-DEL-VPS>:/home/llavero/Llavero-E-Ink/server/data/firmware/
```

Si el `scp` se hace como `root` en vez de como el usuario `llavero`,
ajustar los permisos después para que el proceso del servicio
(`llavero.service`, corriendo como `llavero`) pueda leer el archivo:

```bash
chown llavero:llavero /home/llavero/Llavero-E-Ink/server/data/firmware/llavero-0.2.0.bin
```

**No hace falta reiniciar `llavero.service`** — el endpoint lee el
directorio en cada request, no cachea nada.

### 3. Probar con curl

```bash
curl -i http://localhost:8000/device/firmware -H "X-Device-Token: un-token-de-prueba-cualquiera"
# 200, header X-Fw-Version: 0.2.0, body = el binario

curl -i http://localhost:8000/device/firmware
# 401 (sin token)
```

Si `server/data/firmware/` está vacío (o no existe todavía), responde
`404` — comportamiento esperado antes de subir el primer binario, no un
error.

### 4. Qué compara el dispositivo

`X-Fw-Version` de `/device/firmware` describe el binario que se está
sirviendo — **no** es el mismo valor que `X-Fw-Version` de `/device/wake`
(ese sigue siendo la versión mínima de protocolo que el servidor anuncia,
D-015, no la versión de un binario). El dispositivo compara el
`X-Fw-Version` de `/device/firmware` contra su propio `FW_VERSION`
compilado (`firmware/llavero/src/main.cpp`) y solo aplica la
actualización si la del servidor es numéricamente mayor — ver
`firmware/llavero/README.md` para el flujo completo con hardware real.
