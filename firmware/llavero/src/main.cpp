#include <Arduino.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <GxEPD2_BW.h>
#include "esp_sleep.h"
#include "esp_ota_ops.h"
#include "cert_raiz.h"

// Pinout de la pantalla e-ink en la PCB final (D-021, no el de breadboard
// D-001/D-017 que usa firmware/test-consumo/). Seis pines contiguos de un
// solo lado del conector del XIAO para simplificar el trazado de la PCB.
// Confirmado línea por línea contra la tabla de D-021 antes de fijarlo acá.
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

// ---------- Cliente HTTPS (D-008, D-009, D-023) ----------
constexpr const char *URL_DEVICE_WAKE = "https://caspiandomain.dev/device/wake";
constexpr uint32_t TIMEOUT_HTTP_MS = 15000;
constexpr uint16_t ANCHO_PANTALLA = 200;
constexpr uint16_t ALTO_PANTALLA = 200;
constexpr size_t TAMANO_BUFFER_ESPERADO = 5000;  // D-009: 200x200/8, 1bpp

// ---------- OTA (D-004, D-012, D-027) ----------
constexpr const char *URL_DEVICE_FIRMWARE = "https://caspiandomain.dev/device/firmware";

// Versión de ESTE firmware compilado. Distinta del X-Fw-Version que
// devuelve /device/wake (D-015: la versión mínima que el servidor anuncia
// como compatible para el protocolo de imagen — un concepto de
// compatibilidad de API, no la versión real de ningún binario). Esta es
// la que se compara contra /device/firmware para decidir si hay una
// actualización real que aplicar (D-027). Subir a mano en cada release,
// junto con el nombre del .bin que se sube al servidor — ver README.
constexpr const char *FW_VERSION = "0.1.1";

// Placeholder para cuando no hay token guardado en NVS todavía. Nunca es el
// token real de producción — ese vive solo en NVS del dispositivo, jamás en
// este código fuente que se sube a un repo público. Ver README para cómo
// configurarlo.
constexpr const char *TOKEN_PLACEHOLDER = "REEMPLAZAR_CON_TOKEN_REAL";

// Namespace y claves de Preferences (NVS). Claves de máximo 15 caracteres.
constexpr const char *NVS_NAMESPACE = "llavero";
constexpr const char *NVS_CLAVE_CANTIDAD = "netCount";
constexpr const char *NVS_CLAVE_BACKOFF = "backoffS";
constexpr const char *NVS_CLAVE_TOKEN = "deviceToken";
constexpr const char *NVS_CLAVE_CHECKSUM = "ultimoChecksum";

Preferences prefs;
WiFiMulti wifiMulti;
DNSServer dnsServer;
WebServer webServer(80);
GxEPD2_BW<GxEPD2_154_D67, GxEPD2_154_D67::HEIGHT> display(GxEPD2_154_D67(PIN_CS, PIN_DC, PIN_RST, PIN_BUSY));

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

// ---------- Cliente HTTPS ----------

// Configurable sin recompilar, mismo patrón que las redes Wi-Fi (D-023):
// vive en NVS, no en el código fuente. Si no hay token guardado todavía
// (dispositivo recién flasheado, antes de que el usuario lo configure)
// devuelve el placeholder — el servidor lo va a rechazar con 401, lo cual
// es el comportamiento correcto y visible en el log, no un fallo silencioso.
String leerTokenDispositivo() {
  prefs.begin(NVS_NAMESPACE, true);
  String token = prefs.getString(NVS_CLAVE_TOKEN, "");
  prefs.end();
  if (token.length() == 0) {
    Serial.println(
        "ADVERTENCIA: no hay X-Device-Token guardado en NVS. Usando "
        "placeholder (el servidor lo va a rechazar) — configuralo, ver README.");
    return String(TOKEN_PLACEHOLDER);
  }
  return token;
}

// Guarda el token en la misma clave que ya lee leerTokenDispositivo() — no
// es una clave nueva, D-023 ya la definía (NVS_CLAVE_TOKEN = "deviceToken",
// namespace NVS_NAMESPACE = "llavero"). El llamador (manejarGuardar) es
// responsable de no invocar esto si el campo del formulario vino vacío: así
// reconfigurar solo el Wi-Fi desde el portal no borra un token ya guardado.
void guardarTokenDispositivo(const String &token) {
  prefs.begin(NVS_NAMESPACE, false);
  prefs.putString(NVS_CLAVE_TOKEN, token);
  prefs.end();
  Serial.println("Token del dispositivo actualizado desde el portal cautivo.");
}

String leerUltimoChecksum() {
  prefs.begin(NVS_NAMESPACE, true);
  String checksum = prefs.getString(NVS_CLAVE_CHECKSUM, "");
  prefs.end();
  return checksum;
}

void guardarUltimoChecksum(const String &checksum) {
  prefs.begin(NVS_NAMESPACE, false);
  prefs.putString(NVS_CLAVE_CHECKSUM, checksum);
  prefs.end();
}

// ---------- Pintado de pantalla (D-018, D-021, D-024) ----------

// Pinta el buffer de 200x200 1bpp descargado (D-009), con un refresco
// completo (no parcial). El buffer ya viene en la convención nativa del
// controlador (1=blanco, 0=negro, MSB-first, row-major — D-018, verificada
// en esa tarea contra el código fuente de GxEPD2), así que se pasa directo
// a display.drawImage() en vez de Adafruit_GFX::drawBitmap(): esa última
// tiene otra convención (bit=1 dibuja un color foreground pixel por pixel
// vía writePixel(), pensada para bitmaps con fg/bg separados, no para un
// framebuffer nativo ya armado) y es más lenta al no escribir directo a la
// memoria del controlador. Confirmado leyendo
// GxEPD2_154_D67::drawImage() en el código fuente vendorizado (mismo que
// usa firmware/test-consumo/): hace writeImage() + refresh() +
// writeImageAgain() — un refresco completo en un solo llamado.
//
// display.hibernate() se llama siempre al final, sin condicionales de por
// medio: es la misma condición crítica de presupuesto de energía validada
// en D-006 (sin hibernar el panel antes del deep sleep, la corriente
// parásita del módulo domina el consumo).
void pintarPantalla(const uint8_t *buffer) {
  SPI.begin(PIN_CLK, -1, PIN_DIN, PIN_CS);  // SPI por hardware con pines custom (D-021, no son los nativos del C3)
  display.init(115200);
  display.setRotation(0);
  display.setFullWindow();
  display.drawImage(buffer, 0, 0, ANCHO_PANTALLA, ALTO_PANTALLA);
  display.hibernate();
}

// Consulta GET /device/wake (D-008/D-009): valida TLS contra CERT_RAIZ (sin
// setInsecure()), manda X-Device-Token, lee X-Sleep-Seconds/X-Fw-Version/
// X-Image-Checksum, y descarga el body completo SOLO si el checksum cambió
// respecto al último guardado en NVS. Deja el intervalo de sueño indicado
// por el servidor en segundosDormir (solo tiene sentido si devuelve true).
//
// Devuelve true únicamente si el servidor respondió 200 con los headers
// esperados — con o sin descarga nueva, eso ya cuenta como éxito. Cualquier
// otro caso (error de conexión/TLS, 401, 503, headers faltantes, cualquier
// código que no sea 200) devuelve false: un fallo de servidor no se trata
// distinto de un fallo de Wi-Fi para efectos de cuándo reintentar (D-010) —
// quien llama debe aplicar el mismo backoff exponencial.
bool consultarServidor(uint32_t &segundosDormir) {
  String token = leerTokenDispositivo();

  WiFiClientSecure clienteSeguro;
  clienteSeguro.setCACert(CERT_RAIZ);
  clienteSeguro.setTimeout(TIMEOUT_HTTP_MS / 1000);           // segundos (verificado: WiFiClientSecure::setTimeout toma segundos)
  clienteSeguro.setHandshakeTimeout(TIMEOUT_HTTP_MS / 1000);  // ídem

  HTTPClient http;
  http.setConnectTimeout(TIMEOUT_HTTP_MS);  // ms (verificado: HTTPClient::setConnectTimeout/setTimeout toman ms)
  http.setTimeout(TIMEOUT_HTTP_MS);

  if (!http.begin(clienteSeguro, URL_DEVICE_WAKE)) {
    Serial.println("No se pudo iniciar la conexión HTTPS (begin() falló).");
    return false;
  }

  http.addHeader("X-Device-Token", token);

  // collectHeaders() es obligatorio: sin esto, header()/hasHeader() no
  // encuentran headers custom de la respuesta aunque estén presentes
  // (verificado contra HTTPClient.h — la API por default no expone
  // headers arbitrarios, hay que pedirlos explícitamente).
  const char *encabezadosAColeccionar[] = {"X-Sleep-Seconds", "X-Fw-Version", "X-Image-Checksum"};
  http.collectHeaders(encabezadosAColeccionar, 3);

  Serial.printf("Consultando %s ...\n", URL_DEVICE_WAKE);
  int codigo = http.GET();

  if (codigo <= 0) {
    // Códigos negativos son errores de conexión/TLS, no códigos HTTP (D-008:
    // si la validación del certificado fallara, cae acá).
    Serial.printf("Error de conexión HTTPS: %s (%d)\n", HTTPClient::errorToString(codigo).c_str(), codigo);
    http.end();
    return false;
  }

  if (codigo != HTTP_CODE_OK) {
    Serial.printf("El servidor respondió con código %d (se esperaba 200)\n", codigo);
    http.end();
    return false;
  }

  if (!http.hasHeader("X-Sleep-Seconds") || !http.hasHeader("X-Image-Checksum")) {
    Serial.println("Respuesta 200 sin los headers esperados (X-Sleep-Seconds/X-Image-Checksum).");
    http.end();
    return false;
  }

  segundosDormir = (uint32_t)http.header("X-Sleep-Seconds").toInt();
  String checksumNuevo = http.header("X-Image-Checksum");
  String versionFw = http.hasHeader("X-Fw-Version") ? http.header("X-Fw-Version") : "?";
  Serial.printf("Respuesta OK. X-Fw-Version=%s, X-Sleep-Seconds=%u, X-Image-Checksum=%s\n", versionFw.c_str(),
                (unsigned)segundosDormir, checksumNuevo.c_str());

  String checksumAnterior = leerUltimoChecksum();
  if (checksumNuevo.length() > 0 && checksumNuevo == checksumAnterior) {
    Serial.println("Imagen sin cambios, no se descarga.");
    http.end();
    return true;
  }

  // static, no en el stack: 5000 bytes es más de la mitad del stack default
  // de la tarea de Arduino (8 KB), y esta función ya corre en medio de un
  // handshake TLS con su propio uso de stack — un array local de este
  // tamaño arriesgaba overflow. En .bss no compite con la pila.
  static uint8_t buffer[TAMANO_BUFFER_ESPERADO];
  WiFiClient &stream = http.getStream();
  size_t leidos = stream.readBytes(buffer, TAMANO_BUFFER_ESPERADO);

  if (leidos != TAMANO_BUFFER_ESPERADO) {
    Serial.printf("Se esperaban %u bytes y se leyeron %u — no se guarda el checksum nuevo ni se pinta.\n",
                  (unsigned)TAMANO_BUFFER_ESPERADO, (unsigned)leidos);
  } else {
    Serial.printf("Imagen nueva descargada: %u bytes, checksum %s\n", (unsigned)leidos, checksumNuevo.c_str());
    pintarPantalla(buffer);
    guardarUltimoChecksum(checksumNuevo);
  }

  http.end();
  return true;
}

// ---------- OTA ----------

// Confirma ante el bootloader que ESTE firmware completó un ciclo
// funcional exitoso (Wi-Fi + /device/wake), cancelando cualquier rollback
// pendiente (D-004/D-012). Se llama SIEMPRE tras un ciclo exitoso, no solo
// la primera vez después de instalar un OTA:
// esp_ota_mark_app_valid_cancel_rollback() es idempotente — si la
// partición ya estaba marcada válida, no hace nada (confirmado contra
// esp_ota_ops.h del SDK). Si el firmware nuevo NUNCA llega a este punto
// (crash, reinicio inesperado antes de completar el ciclo), el bootloader
// de ESP-IDF revierte solo a la partición anterior en el siguiente
// arranque — comportamiento nativo
// (CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y, confirmado en el sdkconfig
// del ESP32-C3 que usa este framework), no reimplementado a mano.
void marcarFirmwareValido() {
  esp_err_t resultado = esp_ota_mark_app_valid_cancel_rollback();
  if (resultado != ESP_OK) {
    Serial.printf("No se pudo marcar el firmware como válido (err=%d)\n", (int)resultado);
  }
}

// Compara dos versiones "x.y.z" numéricamente, segmento por segmento — no
// como texto, para que "0.10.0" no se lea como menor que "0.9.0" por
// comparación de caracteres (D-027). Devuelve true si `remota` es
// estrictamente mayor que `local`.
bool versionEsMayor(const String &remota, const String &local) {
  int iRemota = 0, iLocal = 0;
  while (iRemota < (int)remota.length() || iLocal < (int)local.length()) {
    int finRemota = remota.indexOf('.', iRemota);
    int finLocal = local.indexOf('.', iLocal);
    String segRemota = (finRemota == -1) ? remota.substring(iRemota) : remota.substring(iRemota, finRemota);
    String segLocal = (finLocal == -1) ? local.substring(iLocal) : local.substring(iLocal, finLocal);
    int numRemota = segRemota.length() ? segRemota.toInt() : 0;
    int numLocal = segLocal.length() ? segLocal.toInt() : 0;
    if (numRemota != numLocal) return numRemota > numLocal;
    iRemota = (finRemota == -1) ? remota.length() : finRemota + 1;
    iLocal = (finLocal == -1) ? local.length() : finLocal + 1;
  }
  return false;  // iguales
}

// Revisa si hay firmware nuevo en /device/firmware (D-012/D-027) y lo
// aplica si la versión remota es mayor a FW_VERSION. Independiente del
// ciclo de imagen: se llama después de una consulta exitosa a
// /device/wake, pero un fallo acá (red, o el servidor simplemente no
// tiene ningún firmware subido todavía) solo se loggea — nunca bloquea ni
// afecta el dormir() que sigue.
//
// RIESGO CONOCIDO, NO RESUELTO (D-027): este proyecto no mide batería
// (D-006, decisión ya tomada). Un corte de energía real a mitad de la
// escritura OTA (Update.write() escribiendo la partición nueva) podría
// dejar esa partición a medio escribir. El rollback de ESP-IDF protege
// contra un firmware que SÍ terminó de escribirse pero falla al arrancar
// o al completar un ciclo — no protege contra un corte de energía literal
// a mitad del propio proceso de escritura. Queda fuera del alcance de
// esta tarea, aceptado como riesgo del proyecto.
void verificarActualizacionFirmware() {
  WiFiClientSecure clienteConsulta;
  clienteConsulta.setCACert(CERT_RAIZ);
  clienteConsulta.setTimeout(TIMEOUT_HTTP_MS / 1000);
  clienteConsulta.setHandshakeTimeout(TIMEOUT_HTTP_MS / 1000);

  HTTPClient http;
  http.setConnectTimeout(TIMEOUT_HTTP_MS);
  http.setTimeout(TIMEOUT_HTTP_MS);

  if (!http.begin(clienteConsulta, URL_DEVICE_FIRMWARE)) {
    Serial.println("OTA: no se pudo iniciar la conexión HTTPS.");
    return;
  }

  String token = leerTokenDispositivo();
  http.addHeader("X-Device-Token", token);

  const char *encabezados[] = {"X-Fw-Version"};
  http.collectHeaders(encabezados, 1);

  Serial.printf("OTA: consultando %s ...\n", URL_DEVICE_FIRMWARE);
  int codigo = http.GET();

  if (codigo == HTTP_CODE_NOT_FOUND) {
    Serial.println("OTA: el servidor no tiene ningún firmware disponible todavía.");
    http.end();
    return;
  }
  if (codigo <= 0) {
    Serial.printf("OTA: error de conexión HTTPS: %s (%d)\n", HTTPClient::errorToString(codigo).c_str(), codigo);
    http.end();
    return;
  }
  if (codigo != HTTP_CODE_OK) {
    Serial.printf("OTA: el servidor respondió con código %d\n", codigo);
    http.end();
    return;
  }
  if (!http.hasHeader("X-Fw-Version")) {
    Serial.println("OTA: respuesta 200 sin X-Fw-Version, se ignora.");
    http.end();
    return;
  }

  String versionRemota = http.header("X-Fw-Version");
  http.end();  // cierra esta consulta; httpUpdate.update() abre su propia conexión

  Serial.printf("OTA: versión disponible en el servidor: %s (actual: %s)\n", versionRemota.c_str(), FW_VERSION);

  if (!versionEsMayor(versionRemota, FW_VERSION)) {
    Serial.println("OTA: no hay versión más nueva. Nada que hacer.");
    return;
  }

  Serial.println("OTA: versión nueva detectada, descargando y aplicando...");

  // No reiniciar automáticamente al terminar: Update.h ya deja la
  // partición nueva marcada como la próxima a arrancar
  // (esp_ota_set_boot_partition(), confirmado en Updater.cpp) y el
  // dispositivo va a dormir normalmente después de esto. El PRÓXIMO
  // despertar por temporizador ya arranca el firmware nuevo: el deep
  // sleep en ESP32 reinicia el chip por completo, así que el bootloader
  // vuelve a elegir partición como en cualquier arranque — no hace falta
  // un reinicio inmediato aparte.
  httpUpdate.rebootOnUpdate(false);

  WiFiClientSecure clienteOta;
  clienteOta.setCACert(CERT_RAIZ);
  clienteOta.setTimeout(TIMEOUT_HTTP_MS / 1000);
  clienteOta.setHandshakeTimeout(TIMEOUT_HTTP_MS / 1000);

  t_httpUpdate_return resultado = httpUpdate.update(
      clienteOta, URL_DEVICE_FIRMWARE, FW_VERSION,
      [token](HTTPClient *httpInterno) { httpInterno->addHeader("X-Device-Token", token); });

  switch (resultado) {
    case HTTP_UPDATE_OK:
      Serial.println("OTA: actualización escrita OK. Arranca con el firmware nuevo en el próximo despertar.");
      break;
    case HTTP_UPDATE_NO_UPDATES:
      Serial.println("OTA: httpUpdate no encontró nada para actualizar (inesperado, ya se había confirmado versión nueva).");
      break;
    case HTTP_UPDATE_FAILED:
    default:
      Serial.printf("OTA: falló la actualización: %s (%d)\n", httpUpdate.getLastErrorString().c_str(),
                    httpUpdate.getLastError());
      break;
  }
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
  cuerpo += "llavero debe conectarse para descargar la imagen del día. El token ";
  cuerpo += "del dispositivo es opcional: dejalo vacío para no tocar el que ya ";
  cuerpo += "esté guardado.</p>";
  cuerpo += "<form method='POST' action='/guardar'>";
  cuerpo += "<label for='ssid'>Red (SSID)</label>";
  cuerpo += "<input id='ssid' type='text' name='ssid' maxlength='32' autocapitalize='off' autocorrect='off' required>";
  cuerpo += "<label for='pass'>Contraseña</label>";
  cuerpo += "<input id='pass' type='password' name='pass' maxlength='64'>";
  cuerpo += "<label for='token'>Token del dispositivo (opcional)</label>";
  cuerpo += "<input id='token' type='password' name='token' maxlength='128' autocapitalize='off' autocorrect='off' placeholder='Dejar vacío para no cambiarlo'>";
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

  // El token es opcional: si el campo vino vacío, no se toca el valor que
  // ya hubiera en NVS — así se puede reconfigurar solo el Wi-Fi sin tener
  // que volver a pegar el token cada vez.
  String token = webServer.hasArg("token") ? webServer.arg("token") : "";
  if (token.length() > 0) {
    guardarTokenDispositivo(token);
  }

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

      // Un fallo de servidor (consultarServidor() devuelve false) NO
      // resetea el backoff — se trata igual que un fallo de Wi-Fi (D-010).
      uint32_t segundosDormir = INTERVALO_BASE_S;
      if (consultarServidor(segundosDormir)) {
        resetearBackoff();
        // Ciclo funcional completo (Wi-Fi + /device/wake) — confirma este
        // firmware ante el bootloader (D-012/D-027) antes de dormir.
        marcarFirmwareValido();
        // Chequeo de OTA independiente del de imagen (D-027): si falla,
        // solo se loggea, no afecta el dormir() que sigue.
        verificarActualizacionFirmware();
        dormir(segundosDormir);
      } else {
        Serial.println("Fallo al consultar el servidor. Aplicando backoff.");
        uint32_t espera = avanzarBackoffYObtenerIntervaloActual();
        dormir(espera);
      }
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
