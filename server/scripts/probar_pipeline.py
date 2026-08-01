"""Script de línea de comandos para probar app/pipeline.py sin Telegram.

Uso:
    python server/scripts/probar_pipeline.py --imagen ruta/a/foto.jpg
    python server/scripts/probar_pipeline.py --texto "Buenos días, mi amor"

Escribe server/data/current.bin y server/data/current.json, e imprime el
checksum resultante.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from app import pipeline  # noqa: E402


def main() -> None:
    parser = argparse.ArgumentParser(description="Prueba el pipeline de imagen del llavero.")
    grupo = parser.add_mutually_exclusive_group(required=True)
    grupo.add_argument("--imagen", help="Ruta a una foto")
    grupo.add_argument("--texto", help="Texto a renderizar")
    parser.add_argument(
        "--sleep-seconds", type=int, default=86400, help="Placeholder de sueño (D-005 resuelve el cálculo real)"
    )
    args = parser.parse_args()

    if args.imagen:
        resultado = pipeline.generar_desde_foto(args.imagen)
    else:
        resultado = pipeline.generar_desde_texto(args.texto)

    pipeline.guardar(resultado, sleep_seconds=args.sleep_seconds)

    print(f"OK: {len(resultado.buffer)} bytes escritos en {pipeline.CURRENT_BIN_PATH}")
    print(f"metadata escrita en {pipeline.CURRENT_JSON_PATH}")
    print(f"checksum: {resultado.checksum}")


if __name__ == "__main__":
    main()
