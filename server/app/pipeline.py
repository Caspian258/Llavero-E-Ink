"""Pipeline de imagen: convierte una foto o un texto en el buffer 1bpp que
`/device/wake` sirve al dispositivo (D-009), más su metadata (D-014).

Convención de empaquetado 1bpp (D-018) — verificada contra el código fuente
de GxEPD2 instalado en
`firmware/test-consumo/.pio/libdeps/seeed_xiao_esp32c3/GxEPD2/src/`, no
asumida:

- `GxEPD2_BW.h::drawPixel()`: si `color != 0` (GxEPD_WHITE) fija el bit a 1;
  si `color == 0` (GxEPD_BLACK) lo deja en 0. El bit usado es
  `1 << (7 - x % 8)`, o sea MSB-first: el primer píxel de cada grupo de 8
  cae en el bit más significativo del byte.
- `GxEPD2_BW.h::fillScreen()`: blanco -> byte 0xFF, negro -> byte 0x00.
  Mismo criterio a nivel de byte completo.
- `epd/GxEPD2_154_D67.cpp::_writeImage()`: manda el buffer recibido al
  controlador tal cual (sin invertir, salvo `invert=True`), así que el
  buffer que arma este pipeline debe seguir ya la misma convención que el
  framebuffer interno de GxEPD2.

Convención fijada: **1 = blanco, 0 = negro, MSB-first, row-major**.

Por conveniencia, `Image.convert("1", ...).tobytes()` de Pillow empaqueta
con esta misma convención de forma nativa (verificado empíricamente: un
píxel de valor 255/blanco produce bit 1, uno de valor 0/negro produce bit
0, MSB-first) — no hace falta empaquetar los bits a mano.
"""

from __future__ import annotations

import json
import os
import zlib
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont, ImageOps

from app.config import CURRENT_BIN_PATH, CURRENT_JSON_PATH, DATA_DIR

ANCHO = 200
ALTO = 200
BUFFER_BYTES = (ANCHO * ALTO) // 8  # 5000. ANCHO es múltiplo de 8: Pillow no
# agrega padding de fila al empaquetar, así que tobytes() da el tamaño exacto.

MARGEN_TEXTO = 10
TAMANO_FUENTE_MAX = 32
TAMANO_FUENTE_MIN = 12
ESPACIO_ENTRE_LINEAS = 4

# Ruta de Noto Sans Regular, en orden de preferencia. El VPS de producción
# (Ubuntu 24.04) instala el paquete apt `fonts-noto-core`, que publica una
# instancia estática en /usr/share/fonts/truetype/noto/NotoSans-Regular.ttf
# (ver server/README.md para el comando de instalación). Ese paquete no
# existe en el sistema de desarrollo usado para esta tarea (Fedora): ahí
# Noto Sans solo está disponible como fuente variable, vía el paquete
# `google-noto-sans-vf-fonts`, que Pillow abre directo y resuelve a la
# instancia "Regular" por default (confirmado con ImageFont.truetype(...).getname()).
RUTAS_FUENTE_CANDIDATAS = [
    "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
    "/usr/share/fonts/google-noto-vf/NotoSans[wght].ttf",
]


@dataclass
class ResultadoPipeline:
    buffer: bytes
    checksum: str


def _cargar_fuente(tamano: int) -> ImageFont.FreeTypeFont:
    ruta_override = os.environ.get("NOTO_SANS_PATH")
    rutas = [ruta_override] if ruta_override else RUTAS_FUENTE_CANDIDATAS
    for ruta in rutas:
        if ruta and Path(ruta).exists():
            return ImageFont.truetype(ruta, tamano)
    raise FileNotFoundError(
        "No se encontró Noto Sans Regular en ninguna ruta candidata "
        f"({rutas}). En el VPS: 'apt install fonts-noto-core'. También se "
        "puede fijar la variable de entorno NOTO_SANS_PATH con la ruta al .ttf."
    )


def procesar_foto(ruta: str | Path) -> Image.Image:
    """Recorta al centro (sin deformar), escala a 200x200, escala de grises
    y dithering Floyd-Steinberg a 1 bit."""
    img = Image.open(ruta)
    img = ImageOps.exif_transpose(img)  # corrige orientación EXIF antes de recortar

    ancho, alto = img.size
    lado = min(ancho, alto)
    izquierda = (ancho - lado) // 2
    arriba = (alto - lado) // 2
    img = img.crop((izquierda, arriba, izquierda + lado, arriba + lado))

    img = img.resize((ANCHO, ALTO), Image.LANCZOS)
    img = img.convert("L")
    img = img.convert("1", dither=Image.FLOYDSTEINBERG)
    return img


def _envolver_texto(
    texto: str, fuente: ImageFont.FreeTypeFont, ancho_disponible: int, draw: ImageDraw.ImageDraw
) -> list[str]:
    palabras = texto.split()
    if not palabras:
        return [""]
    lineas = [palabras[0]]
    for palabra in palabras[1:]:
        candidata = f"{lineas[-1]} {palabra}"
        bbox = draw.textbbox((0, 0), candidata, font=fuente)
        if bbox[2] - bbox[0] <= ancho_disponible:
            lineas[-1] = candidata
        else:
            lineas.append(palabra)
    return lineas


def _alto_bloque(lineas: list[str], fuente: ImageFont.FreeTypeFont, draw: ImageDraw.ImageDraw) -> int:
    if not lineas:
        return 0
    total = 0
    for i, linea in enumerate(lineas):
        bbox = draw.textbbox((0, 0), linea, font=fuente)
        total += bbox[3] - bbox[1]
        if i < len(lineas) - 1:
            total += ESPACIO_ENTRE_LINEAS
    return total


def _truncar_hasta_que_quepa(
    texto: str,
    fuente: ImageFont.FreeTypeFont,
    ancho_disponible: int,
    alto_disponible: int,
    draw: ImageDraw.ImageDraw,
) -> list[str]:
    recorte = texto
    while recorte:
        lineas = _envolver_texto(recorte.rstrip() + "...", fuente, ancho_disponible, draw)
        if _alto_bloque(lineas, fuente, draw) <= alto_disponible:
            return lineas
        recorte = recorte[:-1]
    return ["..."]


def procesar_texto(texto: str) -> Image.Image:
    """Lienzo 200x200 blanco, texto negro, tamaño adaptativo (D-018): empieza
    grande, envuelve, y si no cabe en alto reduce el tamaño hasta un mínimo;
    si ni al mínimo cabe, trunca con '...' en vez de fallar."""
    img = Image.new("L", (ANCHO, ALTO), color=255)
    draw = ImageDraw.Draw(img)
    ancho_disponible = ANCHO - 2 * MARGEN_TEXTO
    alto_disponible = ALTO - 2 * MARGEN_TEXTO

    tamano = TAMANO_FUENTE_MAX
    fuente = _cargar_fuente(tamano)
    lineas = _envolver_texto(texto, fuente, ancho_disponible, draw)
    while tamano >= TAMANO_FUENTE_MIN:
        fuente = _cargar_fuente(tamano)
        lineas = _envolver_texto(texto, fuente, ancho_disponible, draw)
        if _alto_bloque(lineas, fuente, draw) <= alto_disponible:
            break
        tamano -= 2
    else:
        fuente = _cargar_fuente(TAMANO_FUENTE_MIN)
        lineas = _truncar_hasta_que_quepa(texto, fuente, ancho_disponible, alto_disponible, draw)

    alto_bloque = _alto_bloque(lineas, fuente, draw)
    y = MARGEN_TEXTO + (alto_disponible - alto_bloque) // 2
    for linea in lineas:
        bbox = draw.textbbox((0, 0), linea, font=fuente)
        ancho_linea = bbox[2] - bbox[0]
        x = MARGEN_TEXTO + (ancho_disponible - ancho_linea) // 2
        draw.text((x, y), linea, font=fuente, fill=0)
        y += (bbox[3] - bbox[1]) + ESPACIO_ENTRE_LINEAS

    return img.convert("1")  # sin dithering: ya es blanco/negro puro


def empaquetar(img: Image.Image) -> bytes:
    if img.size != (ANCHO, ALTO):
        raise ValueError(f"la imagen debe ser {ANCHO}x{ALTO}, llegó {img.size}")
    if img.mode != "1":
        img = img.convert("1")
    buffer = img.tobytes()
    if len(buffer) != BUFFER_BYTES:
        raise ValueError(f"buffer empaquetado de {len(buffer)} bytes, se esperaban {BUFFER_BYTES}")
    return buffer


def calcular_checksum(buffer: bytes) -> str:
    return format(zlib.crc32(buffer) & 0xFFFFFFFF, "08x")


def generar_desde_foto(ruta: str | Path) -> ResultadoPipeline:
    buffer = empaquetar(procesar_foto(ruta))
    return ResultadoPipeline(buffer=buffer, checksum=calcular_checksum(buffer))


def generar_desde_texto(texto: str) -> ResultadoPipeline:
    buffer = empaquetar(procesar_texto(texto))
    return ResultadoPipeline(buffer=buffer, checksum=calcular_checksum(buffer))


def guardar(resultado: ResultadoPipeline) -> None:
    """Escribe data/current.bin y data/current.json (D-009/D-014).

    No escribe sleep_seconds (D-026): dejó de ser metadata de la imagen —
    /device/wake lo calcula fresco en cada request (Europe/Copenhagen), no
    en el momento en que se guarda una imagen nueva.
    """
    DATA_DIR.mkdir(parents=True, exist_ok=True)
    CURRENT_BIN_PATH.write_bytes(resultado.buffer)
    metadata = {
        "checksum": resultado.checksum,
        "updated_at": datetime.now(timezone.utc).isoformat(timespec="seconds").replace("+00:00", "Z"),
    }
    CURRENT_JSON_PATH.write_text(json.dumps(metadata, ensure_ascii=False, indent=2))
