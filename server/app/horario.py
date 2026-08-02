"""Cálculo de cuántos segundos debe dormir el dispositivo hasta el próximo
despertar (D-005, D-009, D-026).

El dispositivo no tiene RTC propio confiable (deriva de reloj documentada
en D-005) — todo el cálculo de horario vive en el servidor. El dispositivo
solo obedece el número de segundos que recibe en X-Sleep-Seconds (D-009);
nunca calcula su propia hora ni conoce zonas horarias.
"""

from __future__ import annotations

from datetime import datetime, timedelta, timezone
from zoneinfo import ZoneInfo

ZONA_COPENHAGUE = ZoneInfo("Europe/Copenhagen")

# 6:00 AM hora de Copenhague (D-026): margen sobre el riesgo ya identificado
# de hotspot apagado en la madrugada (el backoff de D-010 tiene tiempo de
# reintentar hasta que se conecte) y que la imagen del día ya esté lista
# para cuando la destinataria se levante.
HORA_DESPERTAR = 6

# Piso de seguridad: si el cálculo diera un valor casi nulo o negativo por
# coincidir casi exacto con las 6 AM o por algún error de lógica, nunca se
# le manda al dispositivo un sleep tan corto que lo haga despertar en loop.
SLEEP_SECONDS_MINIMO = 300


def segundos_hasta_proximo_amanecer(ahora_utc: datetime | None = None) -> int:
    """Segundos desde `ahora_utc` hasta el próximo HORA_DESPERTAR:00:00 hora
    de Copenhague.

    Usa zoneinfo, no un offset fijo: CEST/CET se resuelven solos, incluso
    cruzando el cambio de horario de marzo/octubre, porque restar dos
    datetime timezone-aware siempre da el tiempo real transcurrido, sin
    importar qué offset tenía cada uno localmente en el momento en que
    ocurre (zoneinfo aplica el offset correcto a cada fecha según su propio
    calendario histórico/futuro, no uno fijo para todo el año).

    `ahora_utc` es inyectable para poder simular horarios exactos en
    pruebas, sin depender del reloj real del sistema al correr el test.
    Si no se pasa, usa datetime.now(timezone.utc) (comportamiento real en
    producción).
    """
    if ahora_utc is None:
        ahora_utc = datetime.now(timezone.utc)
    if ahora_utc.tzinfo is None:
        raise ValueError("ahora_utc debe ser un datetime timezone-aware")

    ahora_copenhague = ahora_utc.astimezone(ZONA_COPENHAGUE)

    objetivo = ahora_copenhague.replace(hour=HORA_DESPERTAR, minute=0, second=0, microsecond=0)
    if objetivo <= ahora_copenhague:
        objetivo += timedelta(days=1)

    # Conversión explícita a UTC antes de restar — no es cosmética. Cuando
    # dos datetime aware comparten el mismo objeto tzinfo (acá, siempre la
    # misma instancia de ZONA_COPENHAGUE), Python resta las partes "naive"
    # directo e ignora el tzinfo por completo (documentado en la propia
    # documentación de datetime). Eso es correcto para un tzinfo de offset
    # fijo, pero incorrecto para zoneinfo cuando `ahora` y `objetivo` caen a
    # distintos lados de un cambio de horario: da un resultado con una hora
    # de error. Verificado empíricamente con el cruce real de 2026 antes de
    # fijar este `.astimezone(timezone.utc)` — ver
    # server/scripts/verificar_horario.py.
    segundos = round(
        (objetivo.astimezone(timezone.utc) - ahora_copenhague.astimezone(timezone.utc)).total_seconds()
    )
    return max(segundos, SLEEP_SECONDS_MINIMO)
