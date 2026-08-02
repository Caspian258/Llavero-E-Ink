"""Verifica app/horario.py con momentos simulados, sin depender del reloj
real del sistema (D-026).

Uso:
    python server/scripts/verificar_horario.py

Cubre: antes/después de las 6 AM en el mismo día, el piso mínimo de
seguridad, horario de verano (CEST) e invierno (CET) por separado, y —lo
más importante— los dos cruces reales de cambio de horario de 2026
(confirmados contra zoneinfo, no asumidos a mano: 29-mar-2026 y
25-oct-2026), donde una resta de horas locales "a mano" da un resultado
distinto (e incorrecto) al tiempo real transcurrido.
"""

from __future__ import annotations

import sys
from datetime import datetime, timezone
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from app.horario import ZONA_COPENHAGUE, SLEEP_SECONDS_MINIMO, segundos_hasta_proximo_amanecer  # noqa: E402


def copenhague(anio, mes, dia, hora, minuto=0):
    return datetime(anio, mes, dia, hora, minuto, tzinfo=ZONA_COPENHAGUE)


CASOS = [
    (
        "3:00 AM, verano (CEST, 15-jul-2026) -> hoy 6 AM",
        copenhague(2026, 7, 15, 3, 0),
        3 * 3600,
    ),
    (
        "5:59 AM, verano (CEST) -> faltan 60s, pero el piso mínimo es 300s",
        copenhague(2026, 7, 15, 5, 59),
        SLEEP_SECONDS_MINIMO,
    ),
    (
        "6:00:00 AM exacto, verano (CEST) -> ya es la hora, pasa a mañana",
        copenhague(2026, 7, 15, 6, 0),
        24 * 3600,
    ),
    (
        "6:01 AM, verano (CEST) -> ya pasó, mañana 6 AM",
        copenhague(2026, 7, 15, 6, 1),
        23 * 3600 + 59 * 60,
    ),
    (
        "11:00 PM, verano (CEST) -> mañana 6 AM",
        copenhague(2026, 7, 15, 23, 0),
        7 * 3600,
    ),
    (
        "3:00 AM, invierno (CET, 15-ene-2026) -> hoy 6 AM",
        copenhague(2026, 1, 15, 3, 0),
        3 * 3600,
    ),
    (
        "11:00 PM, invierno (CET) -> mañana 6 AM",
        copenhague(2026, 1, 15, 23, 0),
        7 * 3600,
    ),
    (
        "Cambio de horario de OTOÑO 2026 (confirmado con zoneinfo: "
        "25-oct-2026 pasa de CEST a CET a la 01:00 UTC / 02:00->01:00 hora "
        "local -- 'ida' de un ejemplo de D-005): 24-oct 23:00 CEST -> "
        "25-oct 06:00 CET. Diferencia de RELOJ local ingenua sería 7h; la "
        "diferencia REAL es 8h porque esa noche se repite una hora.",
        copenhague(2026, 10, 24, 23, 0),
        8 * 3600,
    ),
    (
        "Cambio de horario de PRIMAVERA 2026 (confirmado con zoneinfo: "
        "29-mar-2026 pasa de CET a CEST a la 01:00 UTC / 02:00->03:00 hora "
        "local): 28-mar 23:00 CET -> 29-mar 06:00 CEST. Diferencia de "
        "reloj local ingenua sería 7h; la diferencia REAL es 6h porque "
        "esa noche se salta una hora.",
        copenhague(2026, 3, 28, 23, 0),
        6 * 3600,
    ),
]


def main() -> int:
    fallo = False
    for descripcion, ahora_local, esperado in CASOS:
        # Se pasa como UTC a propósito: así el test ejercita exactamente lo
        # que le llega a la función en producción (datetime.now(timezone.utc)),
        # no un atajo que solo funcione si el caller ya conoce la zona horaria.
        ahora_utc = ahora_local.astimezone(timezone.utc)
        resultado = segundos_hasta_proximo_amanecer(ahora_utc)
        ok = resultado == esperado
        estado = "OK" if ok else "FALLO"
        print(f"[{estado}] {descripcion}")
        print(f"       ahora Copenhague: {ahora_local.isoformat()} (offset {ahora_local.utcoffset()})")
        print(f"       esperado: {esperado}s | resultado: {resultado}s")
        if not ok:
            fallo = True
    print()
    print("TODOS LOS CASOS PASARON" if not fallo else "HAY CASOS QUE FALLARON")
    return 1 if fallo else 0


if __name__ == "__main__":
    raise SystemExit(main())
