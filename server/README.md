# Servidor — Llavero E-Ink

Backend del llavero: expone el endpoint que el dispositivo consulta al
despertar. Por ahora solo incluye el esqueleto (health check + `/device/wake`
sirviendo estado desde archivo plano). El webhook de Telegram y el pipeline
de imagen (dithering, texto) son tareas separadas, todavía no implementadas.

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
