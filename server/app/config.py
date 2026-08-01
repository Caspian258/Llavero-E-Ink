"""Configuración del servidor, leída desde variables de entorno."""

import os
from pathlib import Path

DEVICE_AUTH_TOKEN = os.environ.get("DEVICE_AUTH_TOKEN")
if not DEVICE_AUTH_TOKEN:
    raise RuntimeError(
        "Falta la variable de entorno DEVICE_AUTH_TOKEN. El servidor no puede "
        "arrancar sin un token de autenticación para el dispositivo. "
        "Ver README.md para cómo definirla."
    )

# Versión mínima de firmware anunciada al dispositivo (X-Fw-Version, D-009).
# No es parte de la metadata por imagen (D-014): no cambia con cada imagen
# nueva, describe la compatibilidad del propio endpoint. Ver decisión D-015.
FW_VERSION = os.environ.get("FW_VERSION", "0.1.0")

# Token del bot de Telegram (BotFather). python-telegram-bot lo usa para
# autenticar contra la API de Telegram y también forma parte de la ruta del
# webhook (D-019: ruta no adivinable). Sin token válido, Application.initialize()
# falla al arrancar (llama a getMe() para validar el token).
TELEGRAM_BOT_TOKEN = os.environ.get("TELEGRAM_BOT_TOKEN")
if not TELEGRAM_BOT_TOKEN:
    raise RuntimeError(
        "Falta la variable de entorno TELEGRAM_BOT_TOKEN. El servidor no puede "
        "arrancar sin el token del bot de Telegram. Ver README.md para cómo "
        "obtenerlo de BotFather."
    )

# Secreto que Telegram reenvía en el header X-Telegram-Bot-Api-Secret-Token en
# cada request al webhook (parámetro secret_token de setWebhook). El endpoint
# lo valida antes de procesar nada (D-019) — sin esto, cualquiera que adivine
# la URL del webhook podría mandar contenido falso a la pantalla del llavero.
TELEGRAM_WEBHOOK_SECRET = os.environ.get("TELEGRAM_WEBHOOK_SECRET")
if not TELEGRAM_WEBHOOK_SECRET:
    raise RuntimeError(
        "Falta la variable de entorno TELEGRAM_WEBHOOK_SECRET. El servidor no "
        "puede arrancar sin un secreto para validar que los webhooks vienen de "
        "Telegram de verdad. Ver README.md para cómo generarlo."
    )

DATA_DIR = Path(__file__).resolve().parent.parent / "data"
CURRENT_BIN_PATH = DATA_DIR / "current.bin"
CURRENT_JSON_PATH = DATA_DIR / "current.json"
