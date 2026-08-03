"""Servidor del Llavero E-Ink: health check, endpoint de despertar del
dispositivo, y webhook de Telegram que alimenta el pipeline de imagen."""

import json
import logging
import os
import re
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

# Directorio de subida manual de binarios OTA (D-012/D-027). El usuario los
# coloca acá a mano (ej. por scp) con el nombre exacto "llavero-<version>.bin"
# — no hay pipeline de CI/CD, está fuera de alcance. Ver server/README.md.
FIRMWARE_DIR = DATA_DIR / "firmware"
FIRMWARE_DIR.mkdir(parents=True, exist_ok=True)
PATRON_ARCHIVO_FIRMWARE = re.compile(r"^llavero-(\d+(?:\.\d+)*)\.bin$")

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


def _version_a_tupla(texto: str) -> tuple[int, ...]:
    return tuple(int(parte) for parte in texto.split("."))


def _firmware_mas_reciente() -> tuple[Path, str] | None:
    """Busca en FIRMWARE_DIR el archivo llavero-<version>.bin con la versión
    más alta (D-027). Comparación numérica por segmento, no alfabética —
    "llavero-0.10.0.bin" no debe leerse como anterior a
    "llavero-0.9.0.bin" por orden de caracteres. None si no hay ninguno
    subido todavía, o si ninguno matchea el patrón de nombre esperado.
    """
    mejor: tuple[Path, str, tuple[int, ...]] | None = None
    for ruta in FIRMWARE_DIR.iterdir():
        coincidencia = PATRON_ARCHIVO_FIRMWARE.match(ruta.name)
        if not coincidencia:
            continue
        version_texto = coincidencia.group(1)
        try:
            version_tupla = _version_a_tupla(version_texto)
        except ValueError:
            continue
        if mejor is None or version_tupla > mejor[2]:
            mejor = (ruta, version_texto, version_tupla)
    if mejor is None:
        return None
    return mejor[0], mejor[1]


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

    # X-Fw-Version acá es la versión MÍNIMA de protocolo que el servidor
    # anuncia como compatible (D-015) — un valor fijo de configuración
    # (FW_VERSION, variable de entorno), igual para cualquier request,
    # independiente de qué imagen esté cargada en este momento. NO es la
    # versión de ningún binario OTA: ese es el X-Fw-Version que devuelve
    # /device/firmware más abajo (version_firmware_disponible), un
    # concepto distinto que por historia comparte el mismo nombre de
    # header — confirmado que generó confusión real leyendo logs del
    # dispositivo, de ahí el nombre descriptivo de esta variable.
    version_minima_protocolo = FW_VERSION

    return Response(
        content=buffer,
        media_type="application/octet-stream",
        headers={
            "X-Sleep-Seconds": str(sleep_seconds),
            "X-Fw-Version": version_minima_protocolo,
            "X-Image-Checksum": str(metadata["checksum"]),
        },
    )


@app.get("/device/firmware")
def device_firmware(x_device_token: str | None = Header(default=None)):
    """Sirve el binario OTA más reciente subido a mano a FIRMWARE_DIR
    (D-012/D-027). Mismo token que /device/wake (D-013). El dispositivo
    decide él mismo si la versión es más nueva que la que tiene corriendo
    (X-Fw-Version acá describe el binario que se está sirviendo, distinto
    del X-Fw-Version de /device/wake — D-015 — que es la versión mínima de
    protocolo que el servidor anuncia, no la de ningún binario en
    particular)."""
    if x_device_token is None or not secrets.compare_digest(
        x_device_token, DEVICE_AUTH_TOKEN
    ):
        raise HTTPException(status_code=401, detail="token ausente o inválido")

    encontrado = _firmware_mas_reciente()
    if encontrado is None:
        raise HTTPException(status_code=404, detail="no hay firmware disponible todavía")

    # version_firmware_disponible: la versión del BINARIO OTA que se está
    # sirviendo (D-027), leída del nombre del archivo subido a mano. NO es
    # la versión mínima de protocolo (esa es version_minima_protocolo en
    # device_wake() arriba, D-015) — mismo header X-Fw-Version en los dos
    # endpoints por el contrato ya implementado en el firmware
    # (D-023/D-027), pero dos conceptos distintos.
    ruta, version_firmware_disponible = encontrado
    return Response(
        content=ruta.read_bytes(),
        media_type="application/octet-stream",
        headers={"X-Fw-Version": version_firmware_disponible},
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
