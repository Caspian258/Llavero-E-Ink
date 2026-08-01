"""Servidor del Llavero E-Ink: health check y endpoint de despertar del dispositivo."""

import json
import secrets

from fastapi import FastAPI, Header, HTTPException, Response

from app.config import (
    CURRENT_BIN_PATH,
    CURRENT_JSON_PATH,
    DATA_DIR,
    DEVICE_AUTH_TOKEN,
    FW_VERSION,
)

DATA_DIR.mkdir(parents=True, exist_ok=True)

app = FastAPI(title="Llavero E-Ink")


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

    return Response(
        content=buffer,
        media_type="application/octet-stream",
        headers={
            "X-Sleep-Seconds": str(metadata["sleep_seconds"]),
            "X-Fw-Version": FW_VERSION,
            "X-Image-Checksum": str(metadata["checksum"]),
        },
    )
