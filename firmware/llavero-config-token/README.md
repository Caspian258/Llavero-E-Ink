# Configurador de X-Device-Token (herramienta temporal)

Proyecto PlatformIO separado, de un solo uso: escribe el `X-Device-Token`
real en NVS (namespace `llavero`, clave `deviceToken`) para que
`firmware/llavero/` lo use sin tenerlo hardcodeado en el código fuente
(D-023). Existe porque, a diferencia de las redes Wi-Fi, todavía no hay
una interfaz (portal cautivo o Serial) en el firmware real para cargar
este token sin recompilar — ver D-023 y `firmware/llavero/README.md`.

No forma parte del dispositivo final: se flashea una vez, se usa, y
después se vuelve a flashear `firmware/llavero/` encima.

## Pasos exactos

1. Conectar el XIAO ESP32C3 por USB y flashear esta herramienta:

   ```bash
   cd firmware/llavero-config-token
   pio run --target upload
   ```

2. Abrir el monitor serie:

   ```bash
   pio device monitor --baud 115200
   ```

   (Si el monitor no conecta o queda colgado justo después del upload,
   verificar el puerto actual con `ls /dev/ttyACM*` — el mismo
   comportamiento de reenumeración de `firmware/llavero/README.md`
   aplica acá, es el mismo chip.)

3. Cuando aparezca el mensaje pidiendo el token, pegar el
   `DEVICE_AUTH_TOKEN` real (el mismo que ya está en `server/.env` del
   VPS de producción, D-022) y presionar Enter.

4. Confirmar visualmente que el valor que se imprime de vuelta coincide
   con el que se pegó, y que aparece "Coincide con lo que pegaste."

5. Cuando se vea "Listo, ya puedes reflashear el firmware real", volver
   a `firmware/llavero/` y reflashear el firmware de verdad:

   ```bash
   cd ../llavero
   pio run --target upload
   ```

   El token queda en NVS — sobrevive el reflasheo del firmware, porque
   NVS es una partición de flash separada del código de la aplicación.

## Log esperado

```
=== Configurador de X-Device-Token (herramienta temporal) ===
Pega el token real (el mismo DEVICE_AUTH_TOKEN de server/.env)
y presioná Enter:
Token guardado en NVS. Valor leído de vuelta, para confirmar que coincide:
<el token que pegaste>
Coincide con lo que pegaste.

Listo, ya puedes reflashear el firmware real (firmware/llavero/).
```

Si en cambio se ve "ADVERTENCIA: no coincide con lo que pegaste", algo
falló al escribir o leer de NVS — repetir el proceso desde el paso 1.
