#include <Arduino.h>
#include <Preferences.h>

// Herramienta temporal: escribe X-Device-Token en NVS para que
// firmware/llavero/ lo use sin tener que hardcodearlo en el código fuente
// (D-023). Mismo namespace y clave que lee firmware/llavero/src/main.cpp —
// si se cambian ahí, hay que cambiarlos acá también.
constexpr const char *NVS_NAMESPACE = "llavero";
constexpr const char *NVS_CLAVE_TOKEN = "deviceToken";

// Espera una línea completa por Serial, sin timeout: a diferencia de
// Serial.readStringUntil() (que por default se rinde al segundo sin datos,
// truncando el token si el usuario tarda en pegar y darle Enter), este
// bucle espera indefinidamente — es una herramienta de banco, no hay
// batería ni deep sleep de por medio que apurar.
String leerLineaSerial() {
  String linea;
  while (true) {
    if (Serial.available()) {
      char c = (char)Serial.read();
      if (c == '\n' || c == '\r') {
        if (linea.length() > 0) break;  // ignora saltos de línea sueltos antes del texto
        continue;
      }
      linea += c;
    }
  }
  return linea;
}

void setup() {
  Serial.begin(115200);
  delay(2000);  // margen para que el monitor serie llegue a conectar

  Serial.println();
  Serial.println("=== Configurador de X-Device-Token (herramienta temporal) ===");
  Serial.println("Pega el token real (el mismo DEVICE_AUTH_TOKEN de server/.env)");
  Serial.println("y presioná Enter:");

  String token = leerLineaSerial();
  token.trim();

  if (token.length() == 0) {
    Serial.println("ERROR: token vacío, no se guardó nada. Reiniciá y volvé a intentar.");
    return;
  }

  Preferences prefs;
  prefs.begin(NVS_NAMESPACE, false);
  prefs.putString(NVS_CLAVE_TOKEN, token);
  prefs.end();

  // Lee de vuelta lo recién escrito (no lo que quedó en la variable local)
  // para confirmar que lo que quedó en NVS es realmente lo que se guardó.
  prefs.begin(NVS_NAMESPACE, true);
  String tokenGuardado = prefs.getString(NVS_CLAVE_TOKEN, "");
  prefs.end();

  Serial.println("Token guardado en NVS. Valor leído de vuelta, para confirmar que coincide:");
  Serial.println(tokenGuardado);

  if (tokenGuardado == token) {
    Serial.println("Coincide con lo que pegaste.");
  } else {
    Serial.println("ADVERTENCIA: no coincide con lo que pegaste — algo salió mal, reintentá.");
  }

  Serial.println();
  Serial.println("Listo, ya puedes reflashear el firmware real (firmware/llavero/).");
}

void loop() {}
