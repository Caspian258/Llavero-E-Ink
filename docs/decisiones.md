## D-001 — Pinout corregido (2026-07-28)
El mapeo D→GPIO del XIAO ESP32C3 no es lineal. Se reasignan los seis pines
del e-paper para evitar los strapping pins (GPIO2/8/9) y U0TXD (GPIO21, D6),
y para colocar RST y BUSY en RTC GPIOs (GPIO5/GPIO4).
CLK=D4/GPIO6 · DIN=D5/GPIO7 · CS=D10/GPIO10 · DC=D7/GPIO20 · RST=D3/GPIO5 · BUSY=D2/GPIO4
D1/GPIO3 queda libre y reservado para gate de MOSFET.

## D-002 — Estrategia de sueño de la pantalla (2026-07-28)
Antes del deep sleep: hibernación por software, RST enganchado en LOW vía
gpio_hold_en() por pin + gpio_deep_sleep_hold_en() global (API correcto
para ESP32-C3, que no implementa RTCIO clásico — confirmado contra
soc_caps.h del SDK instalado y la documentación de Espressif durante la
implementación del firmware de prueba en /firmware/test-consumo/), y los
seis pines en LOW. En módulos Waveshare recientes,
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

## D-006 — Resultado de medición de corriente parásita (2026-07-31)
Medición con multímetro Truper MUT-39 (rango 2mA, resolución ~1µA),
firmware de tres modos en /firmware/test-consumo/, batería LiPo real
(no USB — ver nota de comportamiento anómalo abajo).

Resultados:
- Modo 1 (XIAO solo, pantalla sin tocar): 0.007 mA = 7 µA
- Modo 2 (XIAO + pantalla, hibernación por software): 0.007 mA = 7 µA
- Modo 3 (XIAO + pantalla, RST/pines forzados LOW): 0.007 mA = 7 µA

Los tres modos dieron el mismo resultado, reproducible con esperas de
15-20s antes de leer (se descartó lectura de transición). Contra los
umbrales de D-003 (verde ≤150 µA), el resultado cae muy por debajo en los
tres casos.

Decisión: NO se implementa el MOSFET de canal P (AO3401A) ni el
transistor NPN (2N3904) comprados como respaldo. La hibernación por
software del panel (display.hibernate()) ya deja el consumo del módulo
completo al mismo nivel que si la pantalla no estuviera conectada;
forzar los pines en LOW no aporta mejora medible. Ambos componentes se
conservan sin soldar por si una revisión futura de hardware (ej. cambio
de módulo de pantalla) lo amerita, pero no forman parte del diseño
actual.

Autonomía estimada revisada: ~0.8 mAh/día (sleep 7µA + ciclo activo
~0.63mAh estimado en 00-contexto-tecnico.md, pendiente de medir en Fase
2). Con 200 mAh utilizables: ~250 días por carga, muy por encima del
objetivo de "semanas" de autonomía.

Nota de campo: el módulo mostró comportamiento anómalo (voltaje sin
sentido físico, lectura ~5.7V) al alimentarse solo por USB sin batería
conectada. No se investigó a fondo por no ser el escenario de uso real
(producción siempre corre con batería). Con batería, comportamiento
consistente y esperado en las tres mediciones.
