/*
 * AVR_Denon_Relay_ESP8266.ino v1.5
 * Proxy CORS + serveur HTTPS sur ESP8266 Huzzah
 * Sert ampli.html + relaie toutes les requêtes vers l'AVR Denon
 *
 * Tous les paramètres (WiFi, AVR, nom) sont lus depuis LittleFS :
 *   /LocalConfig.json  — configuration réseau (JSON)
 *   /cert.pem          — certificat TLS
 *   /key.pem           — clé privée TLS
 *
 * Librairies :
 *   - ESP8266WiFi (incluse)
 *   - ESP8266WebServer (incluse)
 *   - ESP8266WebServerSecure (incluse)
 *   - LittleFS (incluse)
 *   - ArduinoJson >= 7.x (gestionnaire de bibliothèques Arduino IDE)
 */

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266WebServerSecure.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

// ─── Paramètres réseau — lus depuis /LocalConfig.conf ────────────────────────
char wlanSSID[64] = "";
char wlanPass[64] = "";
char avrIP[32]    = "";
char avrName[64]  = "";
int  avrPort      = 0;

BearSSL::ServerSessions serverCache(5);
ESP8266WebServerSecure server(443);

// ─── Lecture d'un fichier LittleFS en String ─────────────────────────────────
String readFile(const char* path) {
  File f = LittleFS.open(path, "r");
  if (!f) {
    Serial.print(F("❌ Fichier introuvable : ")); Serial.println(path);
    return String();
  }
  String content = f.readString();
  f.close();
  Serial.print(F("✅ Lu : ")); Serial.print(path);
  Serial.print(F(" (")); Serial.print(content.length()); Serial.println(F(" octets)"));
  return content;
}

// ─── Chargement de LocalConfig.json depuis LittleFS ──────────────────────────
bool loadLocalConfig() {
  File f = LittleFS.open("/LocalConfig.json", "r");
  if (!f) {
    Serial.println(F("❌ LocalConfig.json introuvable"));
    return false;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();

  if (err) {
    Serial.print(F("❌ LocalConfig.json — erreur JSON : "));
    Serial.println(err.c_str());
    return false;
  }

  // Lecture des 5 paramètres obligatoires
  const char* ssid = doc["WLAN_SSID"];
  const char* pass = doc["WLAN_PASS"];
  const char* ip   = doc["AVR_IP"];
  const char* name = doc["AVR_NAME"];
  int         port = doc["AVR_PORT"] | 0;

  if (!ssid || !ip || !name || port == 0) {
    Serial.println(F("❌ LocalConfig.json — paramètre manquant (WLAN_SSID, AVR_IP, AVR_NAME ou AVR_PORT)"));
    return false;
  }

  strncpy(wlanSSID, ssid, sizeof(wlanSSID) - 1);
  strncpy(wlanPass, pass ? pass : "", sizeof(wlanPass) - 1);
  strncpy(avrIP,   ip,   sizeof(avrIP)   - 1);
  strncpy(avrName, name, sizeof(avrName) - 1);
  avrPort = port;

  Serial.print(F("  WLAN_SSID = ")); Serial.println(wlanSSID);
  Serial.println(F("  WLAN_PASS = ***"));
  Serial.print(F("  AVR_IP    = ")); Serial.println(avrIP);
  Serial.print(F("  AVR_NAME  = ")); Serial.println(avrName);
  Serial.print(F("  AVR_PORT  = ")); Serial.println(avrPort);
  return true;
}

void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);
  delay(800);

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);

  Serial.println();
  Serial.println(F("=== AVR Denon Relay ESP8266 v1.5 (littlefs) ==="));

  // ─── LittleFS en premier — nécessaire pour lire la config ────────────────
  if (!LittleFS.begin()) {
    Serial.println(F("❌ LittleFS : échec de montage !"));
    return;
  }
  Serial.println(F("✅ LittleFS : monté"));

  // ─── Chargement de la configuration ──────────────────────────────────────
  if (!loadLocalConfig()) return;

  // ─── Connexion WiFi ───────────────────────────────────────────────────────
  Serial.print(F("Connexion WiFi..."));
  WiFi.begin(wlanSSID, wlanPass);
  while (WiFi.status() != WL_CONNECTED) {
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
    delay(500); Serial.print('.');
  }
  digitalWrite(LED_BUILTIN, HIGH);

  Serial.print(F("\nhttps://")); Serial.println(WiFi.localIP());
  Serial.print(F("AVR : ")); Serial.println(avrIP);

  // ─── Chargement certificat et clé depuis LittleFS ────────────────────────
  String cert = readFile("/cert.pem");
  String key  = readFile("/key.pem");

  if (cert.isEmpty() || key.isEmpty()) {
    Serial.println(F("❌ Certificat ou clé manquant — serveur HTTPS non démarré"));
    Serial.println(F("   Placer /cert.pem et /key.pem dans le dossier data/ et reflasher LittleFS"));
    return;
  }

  server.getServer().setRSACert(
    new BearSSL::X509List(cert.c_str()),
    new BearSSL::PrivateKey(key.c_str())
  );
  server.getServer().setCache(&serverCache);
  server.enableCORS(true);

  server.on("/",           HTTP_GET,     handleRoot);
  server.on("/config",     HTTP_GET,     handleConfigGet);
  server.on("/config",     HTTP_POST,    handleConfigPost);
  server.on("/config",     HTTP_OPTIONS, handleOptions);
  server.onNotFound(handleRequest);

  server.begin();
  Serial.println(F("✅ Serveur HTTPS démarré"));
}

void loop() {
  server.handleClient();
  if (WiFi.status() != WL_CONNECTED) {
    digitalWrite(LED_BUILTIN, LOW);
    WiFi.begin(wlanSSID, wlanPass);
    while (WiFi.status() != WL_CONNECTED) delay(500);
    digitalWrite(LED_BUILTIN, HIGH);
  }
}

void handleRoot() {
  server.sendHeader("Cache-Control", "max-age=3600");

  File file = LittleFS.open("/ampli.html", "r");
  if (file) {
    server.streamFile(file, "text/html; charset=utf-8");
    file.close();
    Serial.println(F("✅ ampli.html servi depuis LittleFS"));
  } else {
    server.send(404, "text/plain", "Erreur : ampli.html non trouvé dans LittleFS !");
    Serial.println(F("❌ ampli.html introuvable dans LittleFS"));
  }
}

void handleConfigGet() {
  String json = String("{\"ampliIP\":\"") + avrIP + "\",\"ampliName\":\"" + avrName + "\"}";
  server.send(200, "application/json", json);
}

void handleConfigPost() {
  String body = server.arg("plain");
  parseConfig(body);
  server.send(200, "application/json", String("{\"ok\":true,\"ip\":\"") + avrIP + "\"}");
}

void handleOptions() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
  server.send(204);
}

void handleRequest() {
  if (server.method() == HTTP_OPTIONS) { handleOptions(); return; }
  handleProxy();
}

String urlEncode(const String &str) {
  String encoded = "";
  char c;
  char code0;
  char code1;

  for (int i = 0; i < str.length(); i++) {
    c = str.charAt(i);
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      encoded += c;
    } else {
      encoded += '%';
      code0 = (c >> 4) & 0xF;
      code1 = c & 0xF;
      encoded += (code0 > 9) ? char(code0 - 10 + 'A') : char(code0 + '0');
      encoded += (code1 > 9) ? char(code1 - 10 + 'A') : char(code1 + '0');
    }
  }
  return encoded;
}

void handleProxy() {
  String fullPath = server.uri();
  if (server.args() > 0) {
    fullPath += "?";
    for (int i = 0; i < server.args(); i++) {
      if (i > 0) fullPath += "&";
      String name = urlEncode(server.argName(i));
      String val  = urlEncode(server.arg(i));
      if (name == "plain") continue;
      if (val.length() == 0) fullPath += name;
      else fullPath += name + "=" + val;
    }
  }

  Serial.print(F("Proxy : ")); Serial.println(fullPath);

  WiFiClient avrClient;
  if (!avrClient.connect(avrIP, avrPort)) {
    server.send(502, "text/plain", "Cannot connect to AVR");
    return;
  }

  avrClient.print(F("GET ")); avrClient.print(fullPath);
  avrClient.print(F(" HTTP/1.0\r\nHost: ")); avrClient.print(avrIP);
  avrClient.print(F("\r\nConnection: close\r\n\r\n"));

  char hwin[4] = {0};
  bool bodyStart = false;
  String body = "";
  String contentType = "text/plain";
  char ctBuf[64] = {0};
  uint8_t ctLen = 0;
  unsigned long t = millis();

  while (avrClient.connected() && millis()-t < 4000) {
    while (avrClient.available()) {
      char c = avrClient.read(); t = millis();
      if (!bodyStart) {
        hwin[0]=hwin[1]; hwin[1]=hwin[2]; hwin[2]=hwin[3]; hwin[3]=c;
        if (ctLen < 63) ctBuf[ctLen++] = c;
        if (c == '\n') {
          ctBuf[ctLen] = '\0';
          if (strncasecmp(ctBuf, "Content-Type:", 13) == 0) {
            char* v = ctBuf+13; while(*v==' ') v++;
            char* e = v; while(*e&&*e!='\r'&&*e!='\n') e++; *e='\0';
            contentType = String(v);
          }
          ctLen = 0;
        }
        if (hwin[0]=='\r'&&hwin[1]=='\n'&&hwin[2]=='\r'&&hwin[3]=='\n')
          bodyStart = true;
      } else {
        body += c;
      }
    }
    yield();
  }
  avrClient.stop();

  server.send(200, contentType.c_str(), body.c_str());
  Serial.print(F("  -> ")); Serial.print(body.length()); Serial.println(F("b"));
}

void parseConfig(String& json) {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, json);
  if (err) {
    Serial.print(F("parseConfig erreur : ")); Serial.println(err.c_str());
    return;
  }
  if (doc["ampliIP"].is<const char*>())   strncpy(avrIP,   doc["ampliIP"],   sizeof(avrIP)   - 1);
  if (doc["ampliName"].is<const char*>()) strncpy(avrName, doc["ampliName"], sizeof(avrName) - 1);
  Serial.print(F("Config: ")); Serial.println(avrIP);
}
