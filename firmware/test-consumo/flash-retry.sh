#!/usr/bin/env bash
# Reintenta el upload por USB hasta que el XIAO responda dentro de su
# ventana de despierto, en vez de depender de sincronizar a mano BOOT/RST
# con el momento exacto de correr pio run.
set -u

cd "$(dirname "$0")"

PUERTO="/dev/ttyACM0"
MAX_INTENTOS=30

for intento in $(seq 1 "$MAX_INTENTOS"); do
  echo "Intento $intento/$MAX_INTENTOS: pio run -t upload --upload-port $PUERTO"
  if pio run -t upload --upload-port "$PUERTO"; then
    echo "Upload exitoso en el intento $intento."
    exec pio device monitor --port "$PUERTO"
  fi
  sleep 1
done

echo "Error: se agotaron los $MAX_INTENTOS intentos sin lograr el upload." >&2
exit 1
