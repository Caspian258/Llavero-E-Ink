"""Servidor del Llavero E-Ink: health check, endpoint de despertar del
dispositivo, y webhook de Telegram que alimenta el pipeline de imagen."""

import json
import logging
import os
import secrets
import tempfile
from contextlib import asynccontextmanager
from datetime import datetime, timezone
from pathlib import Path

from fastapi import FastAPI, Header, HTTPException, Request, Response
from telegram import Update
from telegram.ext import Application, ContextTypes, MessageHandler, filters

from app import horario, pipeline
from app.config import (
    CURRENT_BIN_PATH,
    CURRENT_JSON_PATH,
    DATA_DIR,
    DEVICE_AUTH_TOKEN,
    FW_VERSION,
    TELEGRAM_BOT_TOKEN,
    TELEGRAM_WEBHOOK_SECRET,
)

logger = logging.getLogger(__name__)

DATA_DIR.mkdir(parents=True, exist_ok=True)

MENSAJE_CONFIRMACION = "Listo, se actualizará en el próximo despertar del llavero."
MENSAJE_ERROR = "Hubo un error al procesar eso. Intenta de nuevo."
MENSAJE_NO_SOPORTADO = "Solo puedo procesar fotos o mensajes de texto."

# Ruta del webhook: incluye el propio token del bot (D-019), patrón estándar
# de python-telegram-bot para que la URL no sea adivinable. Se combina con la
# validación del header X-Telegram-Bot-Api-Secret-Token abajo — dos capas.
WEBHOOK_PATH = f"/telegram/webhook/{TELEGRAM_BOT_TOKEN}"


def _guardar_atomico(resultado: pipeline.ResultadoPipeline) -> None:
    """Escribe current.bin/current.json de forma atómica (D-019): primero a un
    archivo temporal en el mismo directorio, después swap con os.replace().
    Así /device/wake nunca puede leer un archivo a medio escribir, y si el
    proceso se interrumpe a mitad del guardado no deja current.bin corrupto.

    No usa pipeline.guardar() (que escribe directo, sin swap atómico) porque
    ese comportamiento alcanza para el script de prueba de línea de comandos
    (sin concurrencia real) pero no para el webhook, que corre en el mismo
    proceso que ya sirve /device/wake.

    No guarda sleep_seconds acá (D-026): dejó de ser metadata de la imagen,
    igual que X-Fw-Version no lo es (D-015) — se calcula en tiempo real al
    responder /device/wake, no en el momento en que se guarda una imagen
    nueva (que puede ser horas antes de que el dispositivo la pida).
    """
    DATA_DIR.mkdir(parents=True, exist_ok=True)
    metadata = {
        "checksum": resultado.checksum,
        "updated_at": datetime.now(timezone.utc).isoformat(timespec="seconds").replace("+00:00", "Z"),
    }

    fd_bin, tmp_bin_str = tempfile.mkstemp(dir=DATA_DIR, suffix=".tmp", prefix="current.bin.")
    with os.fdopen(fd_bin, "wb") as f:
        f.write(resultado.buffer)
    os.replace(tmp_bin_str, CURRENT_BIN_PATH)

    fd_json, tmp_json_str = tempfile.mkstemp(dir=DATA_DIR, suffix=".tmp", prefix="current.json.")
    with os.fdopen(fd_json, "w", encoding="utf-8") as f:
        f.write(json.dumps(metadata, ensure_ascii=False, indent=2))
    os.replace(tmp_json_str, CURRENT_JSON_PATH)


async def _procesar_foto(update: Update, context: ContextTypes.DEFAULT_TYPE) -> None:
    message = update.message
    if message is None or not message.photo:
        return

    archivo = await message.photo[-1].get_file()  # mayor resolución disponible

    fd, ruta_tmp_str = tempfile.mkstemp(suffix=".jpg", prefix="llavero_foto_")
    os.close(fd)
    ruta_tmp = Path(ruta_tmp_str)
    try:
        await archivo.download_to_drive(custom_path=ruta_tmp)
        resultado = pipeline.generar_desde_foto(ruta_tmp)
    except Exception:
        logger.exception("Fallo al procesar una foto recibida por Telegram")
        await message.reply_text(MENSAJE_ERROR)
        return
    finally:
        ruta_tmp.unlink(missing_ok=True)

    _guardar_atomico(resultado)
    await message.reply_text(MENSAJE_CONFIRMACION)


async def _procesar_texto(update: Update, context: ContextTypes.DEFAULT_TYPE) -> None:
    message = update.message
    if message is None or not message.text:
        return

    try:
        resultado = pipeline.generar_desde_texto(message.text)
    except Exception:
        logger.exception("Fallo al procesar un texto recibido por Telegram")
        await message.reply_text(MENSAJE_ERROR)
        return

    _guardar_atomico(resultado)
    await message.reply_text(MENSAJE_CONFIRMACION)


async def _tipo_no_soportado(update: Update, context: ContextTypes.DEFAULT_TYPE) -> None:
    message = update.message
    if message is not None:
        await message.reply_text(MENSAJE_NO_SOPORTADO)


# updater(None): no usamos el Updater propio de la librería (polling ni su
# webserver de webhook incorporado) — las actualizaciones llegan por nuestra
# propia ruta de FastAPI y se empujan a mano a la cola. Patrón documentado en
# examples/customwebhookbot/starlettebot.py de python-telegram-bot.
telegram_application = Application.builder().token(TELEGRAM_BOT_TOKEN).updater(None).build()
telegram_application.add_handler(MessageHandler(filters.PHOTO, _procesar_foto))
telegram_application.add_handler(MessageHandler(filters.TEXT & ~filters.COMMAND, _procesar_texto))
telegram_application.add_handler(MessageHandler(filters.ALL, _tipo_no_soportado))


@asynccontextmanager
async def lifespan(app: FastAPI):
    # async with: inicializa el bot (incluye getMe() para validar el token) al
    # entrar, y lo apaga (shutdown) al salir. start()/stop() arrancan y frenan
    # la tarea de fondo que consume update_queue y despacha a los handlers.
    # No se llama a bot.set_webhook() aquí: registrar el webhook contra
    # Telegram de verdad requiere el VPS con dominio y TLS (tarea aparte); ver
    # server/README.md para el comando manual una vez desplegado.
    async with telegram_application:
        await telegram_application.start()
        yield
        await telegram_application.stop()


app = FastAPI(title="Llavero E-Ink", lifespan=lifespan)


@app.get("/health")
def health() -> dict:
    return {"status": "ok"}


@app.get("/device/wake")
def device_wake(x_device_token: str | None = Header(default=None)):
    if x_device_token is None or not secrets.compare_digest(
        x_device_token, DEVICE_AUTH_TOKEN
    ):
        raise HTTPException(status_code=401, detail="token ausente o inválido")

    if not CURRENT_BIN_PATH.exists() or not CURRENT_JSON_PATH.exists():
        raise HTTPException(
            status_code=503,
            detail="todavía no hay una imagen cargada para el dispositivo",
        )

    metadata = json.loads(CURRENT_JSON_PATH.read_text())
    buffer = CURRENT_BIN_PATH.read_bytes()

    # Calculado fresco en cada request (D-026), no leído de metadata: la
    # imagen puede haberse guardado horas antes de que el dispositivo
    # despierte y pregunte, así que "cuánto falta para las 6 AM" solo tiene
    # sentido calculado en este momento, no en el momento en que se guardó
    # la imagen.
    sleep_seconds = horario.segundos_hasta_proximo_amanecer()

    return Response(
        content=buffer,
        media_type="application/octet-stream",
        headers={
            "X-Sleep-Seconds": str(sleep_seconds),
            "X-Fw-Version": FW_VERSION,
            "X-Image-Checksum": str(metadata["checksum"]),
        },
    )


@app.post(WEBHOOK_PATH)
async def telegram_webhook(
    request: Request,
    x_telegram_bot_api_secret_token: str | None = Header(default=None),
) -> Response:
    if x_telegram_bot_api_secret_token is None or not secrets.compare_digest(
        x_telegram_bot_api_secret_token, TELEGRAM_WEBHOOK_SECRET
    ):
        raise HTTPException(status_code=401, detail="secret token ausente o inválido")

    datos = await request.json()
    update = Update.de_json(datos, telegram_application.bot)
    await telegram_application.update_queue.put(update)
    return Response(status_code=200)
