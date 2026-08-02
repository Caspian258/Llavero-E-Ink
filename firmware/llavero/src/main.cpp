#include <Arduino.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include "esp_sleep.h"

// Pinout de la pantalla e-ink en la PCB final (D-021, no el de breadboard
// D-001/D-017 que usa firmware/test-consumo/). Seis pines contiguos de un
// solo lado del conector del XIAO para simplificar el trazado de la PCB.
// Reservado para la tarea de pintado/HTTPS que sigue después de esta —
// esta tarea solo implementa la capa de conectividad Wi-Fi, no toca el
// bus SPI ni la pantalla, pero los valores quedan fijados ya como
// constantes para que esa tarea no tenga que releer decisiones.md.
constexpr uint8_t PIN_BUSY = 3;   // D1
constexpr uint8_t PIN_RST = 4;    // D2
constexpr uint8_t PIN_DC = 5;     // D3
constexpr uint8_t PIN_CS = 6;     // D4
constexpr uint8_t PIN_CLK = 7;    // D5
constexpr uint8_t PIN_DIN = 21;   // D6 — U0TXD, no es strapping pin (D-021)

// ---------- Backoff exponencial (D-010) ----------
constexpr uint32_t INTERVALO_BASE_S = 900;     // 15 min
constexpr uint32_t INTERVALO_TECHO_S = 14400;  // 4 h

// ---------- Redes guardadas (D-011) ----------
constexpr uint8_t MAX_REDES = 5;
constexpr uint32_t TIMEOUT_WIFI_MS = 15000;  // por intento de WiFiMulti.run()

// ---------- Portal cautivo (D-011, D-020) ----------
constexpr const char *AP_SSID = "Llavero-Setup";
constexpr const char *AP_PASSWORD = "";  // abierto: ver D-020
constexpr uint32_t TIMEOUT_PORTAL_MS = 5UL * 60UL * 1000UL;  // 5 min en modo AP

// Namespace y claves de Preferences (NVS). Claves de máximo 15 caracteres.
constexpr const char *NVS_NAMESPACE = "llavero";
constexpr const char *NVS_CLAVE_CANTIDAD = "netCount";
constexpr const char *NVS_CLAVE_BACKOFF = "backoffS";

Preferences prefs;
WiFiMulti wifiMulti;
DNSServer dnsServer;
WebServer webServer(80);

bool credencialesRecibidas = false;

// ---------- Almacenamiento de redes ----------

uint8_t cargarRedesGuardadas() {
  prefs.begin(NVS_NAMESPACE, true);  // solo lectura
  uint8_t cantidad = prefs.getUChar(NVS_CLAVE_CANTIDAD, 0);
  for (uint8_t i = 0; i < cantidad && i < MAX_REDES; i++) {
    String claveSsid = "ssid" + String(i);
    String clavePass = "pass" + String(i);
    String ssid = prefs.getString(claveSsid.c_str(), "");
    String pass = prefs.getString(clavePass.c_str(), "");
    if (ssid.length() > 0) {
      wifiMulti.addAP(ssid.c_str(), pass.c_str());
      Serial.printf("Red guardada cargada (%d): %s\n", i, ssid.c_str());
    }
  }
  prefs.end();
  return cantidad;
}

void guardarRedNueva(const String &ssid, const String &pass) {
  prefs.begin(NVS_NAMESPACE, false);  // lectura/escritura
  uint8_t cantidad = prefs.getUChar(NVS_CLAVE_CANTIDAD, 0);

  // Si ya existe esa SSID, actualiza la password en su lugar (no duplica).
  for (uint8_t i = 0; i < cantidad && i < MAX_REDES; i++) {
    String claveSsid = "ssid" + String(i);
    if (prefs.getString(claveSsid.c_str(), "") == ssid) {
      String clavePass = "pass" + String(i);
      prefs.putString(clavePass.c_str(), pass);
      prefs.end();
      Serial.printf("Red existente actualizada: %s\n", ssid.c_str());
      return;
    }
  }

  uint8_t indice;
  if (cantidad < MAX_REDES) {
    indice = cantidad;
    cantidad++;
  } else {
    // Lista llena: se sobreescribe la más antigua (índice 0). Ver D-020.
    indice = 0;
    Serial.println("Lista de redes llena (5/5): se sobreescribe la más antigua.");
  }

  String claveSsid = "ssid" + String(indice);
  String clavePass = "pass" + String(indice);
  prefs.putString(claveSsid.c_str(), ssid);
  prefs.putString(clavePass.c_str(), pass);
  prefs.putUChar(NVS_CLAVE_CANTIDAD, cantidad);
  prefs.end();
  Serial.printf("Red nueva guardada en índice %d: %s\n", indice, ssid.c_str());
}

// ---------- Backoff exponencial ----------

void resetearBackoff() {
  prefs.begin(NVS_NAMESPACE, false);
  prefs.putUInt(NVS_CLAVE_BACKOFF, INTERVALO_BASE_S);
  prefs.end();
}

// Devuelve el intervalo a dormir AHORA (el que estaba vigente antes de esta
// falla) y deja guardado el doble (con techo) para la próxima falla.
uint32_t avanzarBackoffYObtenerIntervaloActual() {
  prefs.begin(NVS_NAMESPACE, false);
  uint32_t actual = prefs.getUInt(NVS_CLAVE_BACKOFF, INTERVALO_BASE_S);
  uint32_t siguiente = actual * 2;
  if (siguiente > INTERVALO_TECHO_S) siguiente = INTERVALO_TECHO_S;
  prefs.putUInt(NVS_CLAVE_BACKOFF, siguiente);
  prefs.end();
  return actual;
}

// ---------- Portal cautivo ----------

// CSS inline compartido por las tres páginas (formulario, confirmación,
// error) — mobile-first, sin dependencias externas (el AP no tiene
// internet). font-size:16px en los inputs evita el zoom automático que
// hace iOS Safari al enfocar un campo con letra más chica.
constexpr const char *ESTILO_BASE =
    "body{margin:0;padding:32px 16px;background:#f2f4f7;"
    "font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;"
    "color:#1c1e26;}"
    ".tarjeta{max-width:380px;margin:0 auto;background:#fff;border-radius:16px;"
    "padding:28px 24px;box-shadow:0 4px 20px rgba(20,20,40,0.08);}"
    "h1{font-size:1.35rem;margin:0 0 4px;}"
    ".subtitulo{font-size:0.9rem;color:#6a6f7a;margin:0 0 18px;}"
    "p{line-height:1.5;margin:0 0 20px;}"
    "label{display:block;font-size:0.85rem;font-weight:600;margin:0 0 6px;color:#3a3d46;}"
    "input{display:block;width:100%;box-sizing:border-box;padding:12px 14px;"
    "margin:0 0 18px;border:1px solid #d8dbe2;border-radius:10px;font-size:16px;}"
    "input:focus{outline:none;border-color:#5468ff;}"
    "button{width:100%;padding:14px;border:none;border-radius:10px;"
    "background:#5468ff;color:#fff;font-size:1rem;font-weight:600;}"
    "button:active{background:#3f52d9;}"
    ".chip{display:inline-block;background:#eaedff;color:#4351b8;padding:2px 10px;"
    "border-radius:999px;font-size:0.8rem;font-weight:600;}";

// Envuelve cualquier fragmento de HTML en la página base (doctype, viewport,
// estilo). Las tres respuestas del portal (formulario, guardado, error) lo
// usan para verse consistentes sin repetir el bloque de estilo tres veces.
String envolverPagina(const String &cuerpo) {
  String html;
  html += "<!DOCTYPE html><html><head><meta charset='utf-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<title>Llavero E-Ink</title><style>";
  html += ESTILO_BASE;
  html += "</style></head><body><div class='tarjeta'>";
  html += cuerpo;
  html += "</div></body></html>";
  return html;
}

String paginaConfiguracion() {
  String cuerpo;
  cuerpo += "<h1>Llavero E-Ink</h1>";
  cuerpo += "<p class='subtitulo'>Conectado a <span class='chip'>" + String(AP_SSID) + "</span></p>";
  cuerpo += "<p>Ingresa el nombre y la contraseña de la red Wi-Fi a la que el ";
  cuerpo += "llavero debe conectarse para descargar la imagen del día:</p>";
  cuerpo += "<form method='POST' action='/guardar'>";
  cuerpo += "<label for='ssid'>Red (SSID)</label>";
  cuerpo += "<input id='ssid' type='text' name='ssid' maxlength='32' autocapitalize='off' autocorrect='off' required>";
  cuerpo += "<label for='pass'>Contraseña</label>";
  cuerpo += "<input id='pass' type='password' name='pass' maxlength='64'>";
  cuerpo += "<button type='submit'>Guardar y reiniciar</button>";
  cuerpo += "</form>";
  return envolverPagina(cuerpo);
}

void manejarRaiz() {
  webServer.send(200, "text/html; charset=utf-8", paginaConfiguracion());
}

void manejarGuardar() {
  if (!webServer.hasArg("ssid") || webServer.arg("ssid").length() == 0) {
    webServer.send(400, "text/html; charset=utf-8",
                    envolverPagina("<h1>Falta el SSID</h1><p>Volvé atrás e ingresá el nombre de la red.</p>"));
    return;
  }
  String ssid = webServer.arg("ssid");
  String pass = webServer.hasArg("pass") ? webServer.arg("pass") : "";
  guardarRedNueva(ssid, pass);
  credencialesRecibidas = true;
  webServer.send(200, "text/html; charset=utf-8",
                  envolverPagina("<h1>Guardado</h1><p>El llavero se reiniciará para conectarse a la red nueva.</p>"));
}

// Levanta AP + portal cautivo. Devuelve true si recibió credenciales nuevas
// antes de agotar el timeout.
bool correrPortalCautivo(uint32_t timeoutMs) {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, strlen(AP_PASSWORD) > 0 ? AP_PASSWORD : nullptr);
  IPAddress ip = WiFi.softAPIP();
  Serial.printf("Portal cautivo activo. SSID: %s | IP: %s\n", AP_SSID, ip.toString().c_str());

  dnsServer.start(53, "*", ip);
  webServer.on("/", HTTP_GET, manejarRaiz);
  webServer.on("/guardar", HTTP_POST, manejarGuardar);
  // Cualquier ruta no reconocida sirve la misma página del formulario
  // directo con 200, en vez de redirigir con 302. Las rutas de sondeo de
  // portal cautivo cambian por OS y con el tiempo (iOS: hotspot-detect.html
  // /library/test/success.html en captive.apple.com; Android/Chrome:
  // generate_204 en connectivitycheck.gstatic.com y clients3.google.com;
  // Windows: connecttest.txt/ncsi.txt en msftconnecttest.com/msftncsi.com;
  // Firefox: canonical.html en detectportal.firefox.com) — en vez de
  // mantener esa lista al día, onNotFound responde igual a cualquier ruta.
  // Servir el contenido real en vez de un 302 es más robusto: iOS en
  // particular no siempre sigue la redirección durante su sondeo
  // automático, mientras que responder directo con contenido (no el
  // "Success"/204/texto exacto que cada OS espera si hay internet) es lo
  // que dispara el aviso de "iniciar sesión en la red" en los tres.
  // Pendiente de confirmar con celular real, ver README.
  webServer.onNotFound(manejarRaiz);
  webServer.begin();

  credencialesRecibidas = false;
  uint32_t inicio = millis();
  while (!credencialesRecibidas && (millis() - inicio) < timeoutMs) {
    dnsServer.processNextRequest();
    webServer.handleClient();
    delay(2);  // cede CPU sin dejar de atender requests; no es espera activa vacía
  }

  webServer.stop();
  dnsServer.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);

  return credencialesRecibidas;
}

// ---------- Deep sleep ----------

void dormir(uint32_t segundos) {
  Serial.printf("Durmiendo %u segundos\n", (unsigned)segundos);
  Serial.flush();
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  esp_sleep_enable_timer_wakeup((uint64_t)segundos * 1000000ULL);
  esp_deep_sleep_start();
}

// ---------- Ciclo principal ----------

void setup() {
  Serial.begin(115200);
  delay(1500);  // margen para que el monitor serie llegue a conectar

  Serial.println();
  Serial.println("=== Llavero E-Ink: capa de conectividad Wi-Fi ===");

  uint8_t cantidadRedes = cargarRedesGuardadas();

  if (cantidadRedes == 0) {
    Serial.println("No hay redes guardadas (primer arranque). Se levanta el portal cautivo.");
  } else {
    Serial.printf("Buscando %u red(es) guardada(s)...\n", cantidadRedes);
    WiFi.mode(WIFI_STA);
    uint8_t estado = wifiMulti.run(TIMEOUT_WIFI_MS);
    if (estado == WL_CONNECTED) {
      Serial.printf("Conectado a %s, IP %s\n", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
      resetearBackoff();
      // El cliente HTTPS y el pintado de pantalla son tareas separadas
      // (fuera del alcance de esta tarea). Por ahora, confirmar la
      // conexión y dormir el intervalo base hasta el próximo ciclo.
      dormir(INTERVALO_BASE_S);
      return;  // no se alcanza: el deep sleep reinicia el chip
    }
    Serial.println("No se pudo conectar a ninguna red guardada dentro del tiempo límite.");
  }

  Serial.println("Levantando portal cautivo...");
  bool configuroNueva = correrPortalCautivo(TIMEOUT_PORTAL_MS);

  if (configuroNueva) {
    Serial.println("Configuración nueva guardada. Reiniciando para reintentar con la lista actualizada...");
    Serial.flush();
    delay(500);
    ESP.restart();
    return;
  }

  Serial.println("Portal cautivo cerrado sin configuración nueva. Aplicando backoff.");
  uint32_t espera = avanzarBackoffYObtenerIntervaloActual();
  dormir(espera);
}

void loop() {}
