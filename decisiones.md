## D-001 — Pinout corregido (2026-07-28)
El mapeo D→GPIO del XIAO ESP32C3 no es lineal. Se reasignan los seis pines
del e-paper para evitar los strapping pins (GPIO2/8/9) y U0TXD (GPIO21, D6),
y para colocar RST y BUSY en RTC GPIOs (GPIO5/GPIO4).
CLK=D4/GPIO6 · DIN=D5/GPIO7 · CS=D10/GPIO10 · DC=D7/GPIO20 · RST=D3/GPIO5 · BUSY=D2/GPIO4
D1/GPIO3 queda libre y reservado para gate de MOSFET.

## D-002 — Estrategia de sueño de la pantalla (2026-07-28)
Antes del deep sleep: hibernación por software, RST enganchado en LOW vía
rtc_gpio_hold_en(), y los seis pines en LOW. En módulos Waveshare recientes,
RST en LOW corta la lógica del level shifter y la alimentación del panel.
Fallback si la medición supera 400 µA: P-MOSFET de canal P sobre VCC,
siempre combinado con los pines en LOW (evitar alimentación fantasma por
diodos ESD). Se renuncia al refresco parcial; irrelevante a 1 refresco/día.

## D-003 — Criterio de aceptación de autonomía (2026-07-28)
Umbral de corriente total en deep sleep: verde ≤150 µA, ámbar 150–400 µA,
rojo >400 µA. Medición con multímetro en serie y jumper de bypass durante
las fases activas.

## D-004 — OTA con rollback obligatorio (2026-07-28)
Una imagen OTA nueva solo se marca válida tras completar un ciclo entero
exitoso (Wi-Fi + descarga + pintado). Sin esto, un push defectuoso deja el
dispositivo irrecuperable en Dinamarca.

## D-005 — Cálculo de horario en el servidor (2026-07-28)
El servidor calcula los segundos de sueño usando la zona Europe/Copenhagen,
no un offset fijo respecto a CDMX. La diferencia pasa de 8 h a 7 h el
25-oct-2026 por el fin del horario de verano europeo.
