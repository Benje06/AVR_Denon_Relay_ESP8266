/*
 * AVR_Denon_Relay_ESP8266.ino v1.3
 * Proxy CORS + serveur HTTPS sur ESP8266 Huzzah
 * Sert ampli.html + relaie toutes les requêtes vers l'AVR Denon
 *
 * Certificat et clé privée chargés depuis LittleFS (/cert.pem, /key.pem)
 * plutôt qu'embarqués en dur dans le firmware.
 *
 * Librairies :
 *   - ESP8266WiFi (incluse)
 *   - ESP8266WebServer (incluse)
 *   - ESP8266WebServerSecure (incluse)
 *   - LittleFS (incluse)
 */

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266WebServerSecure.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>
#include <LittleFS.h>

#define WLAN_SSID  "votre-ssid"
#define WLAN_PASS  "votre-mot-de-passe"
#define AVR_IP     "192.168.x.x"   // IP fixe de l'AVR sur le réseau
#define AVR_PORT   80

char avrIP[32]   = AVR_IP;
char avrName[64] = "AVR-X3000";

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

void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);
  delay(800);

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);

  Serial.println();
  Serial.println(F("=== AVR Denon Relay ESP8266 v1.3 (littlefs) ==="));

  Serial.print(F("Connexion WiFi..."));
  WiFi.begin(WLAN_SSID, WLAN_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
    delay(500); Serial.print('.');
  }
  digitalWrite(LED_BUILTIN, HIGH);

  Serial.print(F("\nhttps://")); Serial.println(WiFi.localIP());
  Serial.print(F("AVR : ")); Serial.println(avrIP);

  if (!LittleFS.begin()) {
    Serial.println(F("❌ LittleFS : échec de montage !"));
    return;
  }
  Serial.println(F("✅ LittleFS : monté"));

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
    WiFi.begin(WLAN_SSID, WLAN_PASS);
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
  if (!avrClient.connect(avrIP, AVR_PORT)) {
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
  int p = json.indexOf("\"ampliIP\":\"");
  if (p >= 0) { p+=11; String v=json.substring(p,json.indexOf('"',p)); v.toCharArray(avrIP,sizeof(avrIP)); }
  p = json.indexOf("\"ampliName\":\"");
  if (p >= 0) { p+=13; String v=json.substring(p,json.indexOf('"',p)); v.toCharArray(avrName,sizeof(avrName)); }
  Serial.print(F("Config: ")); Serial.println(avrIP);
}
