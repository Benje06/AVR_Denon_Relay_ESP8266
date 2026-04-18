# AVR Denon Relay — ESP8266 + Interface Web

Contrôle d'un ampli-tuner **Denon AVR-X3000** depuis n'importe quel navigateur sur le réseau local, via un **ESP8266 Huzzah** servant de proxy HTTPS/CORS.

---

## Architecture

```
[Navigateur] ──HTTPS──► [ESP8266 :443] ──HTTP──► [AVR-X3000 :80]
                              │
                         LittleFS
                         ampli.html
```

Le Denon n'expose qu'un serveur HTTP sur le port 80, sans TLS ni en-têtes CORS. L'ESP8266 résout les deux problèmes : il sert l'interface en HTTPS et relaie toutes les requêtes vers l'AVR en HTTP local.

---

## Fichiers

| Fichier | Rôle |
|---|---|
| `AVR_Denon_Relay_ESP8266.ino` | Firmware Arduino — proxy HTTPS + serveur LittleFS |
| `ampli.html` | Interface web mono-fichier (HTML/CSS/JS) |

---

## Matériel requis

- **Adafruit Feather HUZZAH ESP8266** (ou équivalent ESP8266 avec 4 Mo flash)
- Réseau WiFi local avec accès à l'AVR
- Arduino IDE avec le support ESP8266 installé

---

## Configuration du firmware

Avant de flasher, éditer les constantes en tête du `.ino` :

```cpp
#define WLAN_SSID  "votre-ssid"
#define WLAN_PASS  "votre-mot-de-passe"
#define AVR_IP     "192.168.x.x"   // IP fixe de l'AVR sur le réseau
#define AVR_PORT   80
```

---

## Librairies Arduino requises

Toutes disponibles via le gestionnaire de bibliothèques Arduino IDE :

- `ESP8266WiFi` (incluse dans le support ESP8266)
- `ESP8266WebServer` (incluse)
- `ESP8266WebServerSecure` (incluse)
- `ESP8266HTTPClient` (incluse)
- `LittleFS` (incluse)

Support ESP8266 à ajouter dans les préférences Arduino IDE :
```
https://arduino.esp8266.com/stable/package_esp8266com_index.json
```

---

## Certificat TLS auto-signé

Le firmware embarque un certificat et une clé privée auto-signés (valides 10 ans). Le navigateur affichera un avertissement de sécurité lors du premier accès — il suffit d'accepter l'exception.

Pour regénérer le certificat :

```bash
openssl req -x509 -newkey rsa:2048 -keyout key.pem -out cert.pem \
  -days 3650 -nodes -subj "/CN=AVR-Relay/O=Local/C=FR"
```

Puis remplacer `server_cert[]` et `server_key[]` dans le `.ino`.

---

## Déploiement de l'interface

L'interface `ampli.html` est servie depuis le système de fichiers LittleFS de l'ESP8266.

**Étapes :**

1. Installer le plugin **ESP8266 LittleFS Data Upload** dans Arduino IDE
2. Placer `ampli.html` dans le dossier `data/` du sketch
3. Menu → *Outils → ESP8266 LittleFS Data Upload* pour flasher le filesystem
4. Flasher le firmware normalement

---

## Accès

Une fois démarré, l'ESP affiche son IP sur le port série (115200 baud) :

```
=== AVR Denon Relay ESP8266 v1.2 (littlefs) ===
https://192.168.x.x
AVR : 192.168.0.94
Serveur HTTPS démarré
```

Ouvrir `https://192.168.x.x` dans le navigateur, accepter le certificat auto-signé, et configurer l'IP de l'AVR dans l'interface (⚙).

---

## Routes HTTP de l'ESP

| Méthode | Route | Fonction |
|---|---|---|
| `GET` | `/` | Sert `ampli.html` depuis LittleFS |
| `GET` | `/config` | Retourne `{"ampliIP":"...","ampliName":"..."}` |
| `POST` | `/config` | Met à jour l'IP et le nom de l'AVR |
| `*` | `/*` | Proxy transparent vers l'AVR (toute autre route) |

---

## Interface — Fonctionnalités

### Contrôles principaux
- **Power / Mute / Eco** — toggles en ligne
- **Volume** — slider horizontal (−80 dB à +18 dB, pas 0.5 dB), valeur absolue Denon affichée, repère jaune à 70 (−10 dB)
- **Vol − / Vol +** — boutons d'ajustement rapide

### Entrées (Zone 1)
Grille 4×3 — commandes `SI*` :

| Bouton | Commande |
|---|---|
| CBL/SAT | `SISAT/CBL` |
| DVD | `SIDVD` |
| Blu-ray | `SIBD` |
| Game | `SIGAME` |
| AUX | `SIAUX1` |
| Media Player | `SIMPLAY` |
| iPod/USB | `SIUSB/IPOD` |
| CD | `SICD` |
| Tuner | `SITUNER` |
| Network | `SINET` |
| TV Audio | `SITV` |
| Internet Radio | `SIIRADIO` |

### Réseau
Internet Radio · Favorites · Media Server · Spotify · Flickr + section **US ONLY** (Pandora, SiriusXM)

### Lecture directe
Raccourcis démarrant la lecture immédiatement : Internet Radio (`SIIRP`), Favorites (`SIFVP`), USB (`SIUSB`), iPod Direct (`SIIPD`)

### Surround
- Sélection de groupe : Cinéma / Musique / Jeu (commandes `MS*`)
- Sous-presets contextuels (Dolby PLIIx, DTS NEO:X…)
- Modes permanents : Multi Channel Stereo, Virtual, Stereo, Direct, Pure Direct

### Média
Comportement adaptatif selon l'entrée active :

| Mode | Ligne 1 | Ligne 2 |
|---|---|---|
| Normal | ⏮ ▶ ⏸ ⏭ | — |
| **Tuner** | ⏮ `[FM/AM]` `[Auto/Manual]` ⏭ | ⏮ `PRESET` ⏭ |
| **Internet Radio** | ◀ ▲ OK ▼ ▶ (navigation) | ⏮ ▶ ⏸ ⏭ |

**Tuner :**
- Fréquence : `TFANUP` / `TFANDOWN`
- Preset : `TPANUP` / `TPANDOWN`
- Bande AM/FM : `TMANAM` / `TMANFM`
- Mode : `TMANAUTO` / `TMANMANUAL`

**Internet Radio :**
- Navigation : `NS90`↑ `NS91`↓ `NS92`← `NS93`→ `NS94` OK
- Lecture : `NS9A` play, `NS9B` pause, `NS9D` prev, `NS9E` next

### Veille
Timer programmable de 10 à 120 min (commandes `SLP*`)

### Tonalité
Basses et aigus (commandes `BASSUP/DOWN`, `TREBLEUP/DOWN`)

### Zone 2
Contrôles indépendants : power, volume, sélection d'entrée

### Reconnaissance vocale
Commandes vocales en français (Web Speech API) — micro activé via le bouton central.

---

## Modes d'accès (configuration)

| Mode | Description |
|---|---|
| `esp` (défaut) | HTTPS vers l'ESP, qui proxifie vers l'AVR. Fonctionne partout. |
| `avr` | Accès HTTP direct à l'AVR (contourne l'ESP). Requiert `--disable-web-security` ou accès `file://`. La lecture d'état XML n'est pas disponible en mode direct via navigateur standard (CORS opaque). |

---

## Limites connues

- La **lecture de l'état XML** en mode accès direct (`avr`) ne fonctionne pas dans un navigateur standard : `fetch` avec `mode: no-cors` retourne une réponse opaque illisible. Utiliser le mode `esp` pour la synchronisation d'état.
- Le certificat auto-signé déclenche un avertissement navigateur au premier accès.
- Les commandes **Pandora** et **SiriusXM** ne sont disponibles que sur les modèles North America.

---

## Protocole Denon

Référence : *Denon AVR-X3000 RS-232C/IP Protocol v1.02*

Format général : `COMMANDE[PARAMETRE]\r` envoyé en query string sur :
```
GET http://[AVR_IP]/goform/formiPhoneAppDirect.xml?[COMMANDE]
```

État XML disponible sur :
```
GET http://[AVR_IP]/goform/formMainZone_MainZoneXml.xml
GET http://[AVR_IP]/goform/formZone2_Zone2XmlStatus.xml
```

---

## Références externes

### Protocole officiel Denon

| Document | Lien |
|---|---|
| **AVR-X3000 / AVR-X2000 Protocol v10.1.0** (PDF officiel Denon EU) | [assets.denoneu.com](http://assets.denoneu.com/DocumentMaster/DE/AVRX2000_E400_PROTOCOL(10.1.0)_V04.pdf) |
| **AVR-3311 Protocol v7.1.0** (PDF — commandes RS-232/IP compatibles) | [awe-europe.com](http://www.awe-europe.com/documents/Control%20Docs/Denon/Archive/AVR3311CI_AVR3311_991_PROTOCOL_V7.1.0.pdf) |


### Support ESP8266 Arduino

| Ressource | Lien |
|---|---|
| Support ESP8266 pour Arduino IDE | [arduino.esp8266.com](https://arduino.esp8266.com/stable/package_esp8266com_index.json) |
| Documentation LittleFS ESP8266 | [arduino-esp8266.readthedocs.io](https://arduino-esp8266.readthedocs.io/en/latest/filesystem.html) |
| ESP8266WebServerSecure | [github.com/esp8266/Arduino](https://github.com/esp8266/Arduino/tree/master/libraries/ESP8266WebServer) |