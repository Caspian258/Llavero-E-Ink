#include <Arduino.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include "esp_sleep.h"

// Pinout de la pantalla e-ink (D-001, actualizado por D-017) — reservado
// para la tarea de pintado/HTTPS que sigue después de esta. Esta tarea
// solo implementa la capa de conectividad Wi-Fi; no toca el bus SPI ni la
// pantalla. Se documenta aquí para que la siguiente tarea use los valores
// correctos sin tener que releer decisiones.md:
//   DIN=D5/GPIO7 · CLK=D4/GPIO6 · CS=D10/GPIO10 · DC=D1/GPIO3 (D-017)
//   RST=D3/GPIO5 (RTC GPIO) · BUSY=D2/GPIO4 (RTC GPIO)

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

String paginaConfiguracion() {
  String html;
  html += "<!DOCTYPE html><html><head><meta charset='utf-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<title>Llavero E-Ink</title></head><body>";
  html += "<h1>Llavero E-Ink</h1>";
  html += "<p>Conectado a la red de configuración: <b>" + String(AP_SSID) + "</b></p>";
  html += "<p>Ingresa el nombre y la contraseña de la red Wi-Fi a la que el ";
  html += "llavero debe conectarse para descargar la imagen del día:</p>";
  html += "<form method='POST' action='/guardar'>";
  html += "<label>SSID:<br><input type='text' name='ssid' maxlength='32' required></label><br><br>";
  html += "<label>Password:<br><input type='password' name='pass' maxlength='64'></label><br><br>";
  html += "<button type='submit'>Guardar y reiniciar</button>";
  html += "</form></body></html>";
  return html;
}

void manejarRaiz() {
  webServer.send(200, "text/html; charset=utf-8", paginaConfiguracion());
}

void manejarGuardar() {
  if (!webServer.hasArg("ssid") || webServer.arg("ssid").length() == 0) {
    webServer.send(400, "text/plain; charset=utf-8", "Falta el SSID.");
    return;
  }
  String ssid = webServer.arg("ssid");
  String pass = webServer.hasArg("pass") ? webServer.arg("pass") : "";
  guardarRedNueva(ssid, pass);
  credencialesRecibidas = true;
  webServer.send(
      200, "text/html; charset=utf-8",
      "<!DOCTYPE html><html><head><meta charset='utf-8'></head><body>"
      "<p>Guardado. El llavero se reiniciará para conectarse.</p>"
      "</body></html>");
}

void manejarNoEncontrado() {
  // Redirige cualquier ruta desconocida a la raíz: fuerza el comportamiento
  // de "captive portal" en el celular (que suele probar una URL de prueba
  // propia antes de mostrar la página).
  webServer.sendHeader("Location", "http://192.168.4.1/", true);
  webServer.send(302, "text/plain", "");
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
  webServer.onNotFound(manejarNoEncontrado);
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
