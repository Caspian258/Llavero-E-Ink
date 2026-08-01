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

DATA_DIR = Path(__file__).resolve().parent.parent / "data"
CURRENT_BIN_PATH = DATA_DIR / "current.bin"
CURRENT_JSON_PATH = DATA_DIR / "current.json"
