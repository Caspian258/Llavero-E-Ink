# Servidor — Llavero E-Ink

Backend del llavero: expone el endpoint que el dispositivo consulta al
despertar. Incluye el esqueleto (health check + `/device/wake` sirviendo
estado desde archivo plano) y el pipeline de imagen (`app/pipeline.py`:
foto o texto → `data/current.bin` + `data/current.json`). El webhook de
Telegram que conecta el pipeline con mensajes reales todavía no está
implementado — por ahora el pipeline se prueba con un script de línea de
comandos, ver más abajo.

## Variables de entorno

| Variable | Obligatoria | Descripción |
|---|---|---|
| `DEVICE_AUTH_TOKEN` | Sí | Token que el dispositivo envía en el header `X-Device-Token`. Si no está seteada, el servidor falla al arrancar. |
| `FW_VERSION` | No | Valor devuelto en el header `X-Fw-Version`. Default: `0.1.0`. |

## Correr localmente

```bash
cd server
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt

export DEVICE_AUTH_TOKEN="un-token-de-prueba-cualquiera"
uvicorn app.main:app --reload --port 8000
```

Si `DEVICE_AUTH_TOKEN` no está seteada, el arranque falla con un
`RuntimeError` explicando qué falta — no arranca con un token vacío.

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
