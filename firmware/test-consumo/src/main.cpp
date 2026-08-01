#include <Arduino.h>
#include "driver/gpio.h"
#include "esp_sleep.h"
#include "esp_system.h"

// Cambiar a 1, 2 o 3, volver a compilar y reflashear para cada medición.
#define MODO_PRUEBA 3

// En 1, usa DEEP_SLEEP_SEGUNDOS_DEV (sueño largo) para tener más margen de
// flasheo por USB sin depender del botón BOOT. La medición real de consumo
// (D-003) se hace siempre con MODO_DESARROLLO en 0, con el tiempo de producción.
#define MODO_DESARROLLO 0

#define DEEP_SLEEP_SEGUNDOS 60
#define DEEP_SLEEP_SEGUNDOS_DEV 600

#if MODO_PRUEBA == 2 || MODO_PRUEBA == 3

// Pinout validado en docs/decisiones.md D-001, con DC reubicado según D-017.
// Declarados solo en modo 2/3: en modo 1 no se toca ni se menciona ninguno
// de estos pines.
#define PIN_DIN  7
#define PIN_CLK  6
#define PIN_CS   10
#define PIN_DC   3
#define PIN_RST  5
#define PIN_BUSY 4

#include <GxEPD2_BW.h>

GxEPD2_BW<GxEPD2_154_D67, GxEPD2_154_D67::HEIGHT> display(GxEPD2_154_D67(PIN_CS, PIN_DC, PIN_RST, PIN_BUSY));

void inicializarYDibujarPatron(int modo) {
  SPI.begin(PIN_CLK, -1, PIN_DIN, PIN_CS); // SPI por hardware con pines custom (no son los nativos del C3)
  display.init(115200);
  display.setRotation(0);
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    display.drawRect(20, 20, 160, 160, GxEPD_BLACK);
    display.setTextColor(GxEPD_BLACK);
    display.setTextSize(3);
    display.setCursor(70, 100);
    display.print("MODO ");
    display.print(modo);
  } while (display.nextPage());
}
#endif

// Distingue arranque en frío de un retorno del deep sleep por timer,
// necesario para no confundir mediciones entre modos al ver el log.
void imprimirCausaArranque() {
  if (esp_reset_reason() == ESP_RST_DEEPSLEEP) {
    bool porTimer = esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER;
    Serial.printf("Causa de arranque: %s\n", porTimer
      ? "temporizador de deep sleep" : "despertar de deep sleep (causa distinta a timer)");
  } else {
    Serial.println("Causa de arranque: encendido/reset manual");
  }
}

void entrarDeepSleep(int modo) {
#if MODO_DESARROLLO == 1
  const int segundos = DEEP_SLEEP_SEGUNDOS_DEV;
  Serial.println("Usando temporizador DEV (DEEP_SLEEP_SEGUNDOS_DEV)");
#else
  const int segundos = DEEP_SLEEP_SEGUNDOS;
  Serial.println("Usando temporizador de producción (DEEP_SLEEP_SEGUNDOS)");
#endif
  Serial.printf("Durmiendo %ds, MODO %d\n", segundos, modo);
  Serial.flush();
  delay(150); // margen para que el mensaje salga completo por USB-CDC antes de dormir
  esp_sleep_enable_timer_wakeup((uint64_t)segundos * 1000000ULL);
  esp_deep_sleep_start();
}

void setup() {
  Serial.begin(115200);
  delay(1500); // margen para que el monitor serie llegue a conectar antes de dormir
  imprimirCausaArranque();

#if MODO_PRUEBA == 1
  // Solo XIAO: ningún pin de la pantalla se declara ni se toca.
  for (int i = 1; i <= 3; i++) {
    Serial.printf("MODO 1: XIAO despierto (%d/3)\n", i);
    delay(300);
  }
  entrarDeepSleep(1);

#elif MODO_PRUEBA == 2
  Serial.println("MODO 2: inicializando pantalla...");
  inicializarYDibujarPatron(2);
  display.hibernate(); // apaga el panel y pone su controlador en bajo consumo (hibernación por software)
  entrarDeepSleep(2);

#elif MODO_PRUEBA == 3
  Serial.println("MODO 3: inicializando pantalla...");
  inicializarYDibujarPatron(3);
  display.hibernate(); // apaga el panel y pone su controlador en bajo consumo

  const int pinesPantalla[] = { PIN_DIN, PIN_CLK, PIN_CS, PIN_DC, PIN_RST, PIN_BUSY };
  for (int i = 0; i < 6; i++) {
    pinMode(pinesPantalla[i], OUTPUT);
    digitalWrite(pinesPantalla[i], LOW); // deja los seis pines en LOW antes de dormir
  }

  // El ESP32-C3 no tiene periférico RTCIO clásico (SOC_RTCIO_PIN_COUNT = 0 en
  // este chip), así que rtc_gpio_hold_en() no aplica. El equivalente correcto
  // en el C3 es gpio_hold_en() por pin más gpio_deep_sleep_hold_en() global.
  gpio_hold_en((gpio_num_t)PIN_RST);   // congela el nivel LOW de RST durante el deep sleep
  gpio_hold_en((gpio_num_t)PIN_BUSY);  // congela el nivel LOW de BUSY durante el deep sleep
  gpio_deep_sleep_hold_en();           // habilita que los holds anteriores sobrevivan al deep sleep

  entrarDeepSleep(3);

#else
  #error "MODO_PRUEBA debe ser 1, 2 o 3"
#endif
}

void loop() {}
