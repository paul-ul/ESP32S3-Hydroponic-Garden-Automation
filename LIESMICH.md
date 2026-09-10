# Smart Garden Kopfteil

*[English](README.en.md)*

Firmware für den Lichtcontroller der Smart Growbox Bierkiste. Läuft auf einem
**Seeed XIAO ESP32-S3** und steuert bis zu drei LED-Streifen (Zonen A/B/C)
automatisch nach dem echten Sonnenauf-/-untergang am Standort.

## Funktionen

- **Sonnenrhythmus statt Zeitschaltuhr**: Sonnenauf-/-untergang werden lokal
  über die NOAA/USNO-Formel berechnet (kein Internetdienst nötig, nur die
  Uhrzeit kommt per NTP). Jede Zone hat einen eigenen Offset zu Sonnenauf-/
  -untergang, eine maximale Helligkeit, eine Ein-/Ausblendzeit sowie eine
  Mindest- und Maximal-Lichtdauer.
- **Pflanzenart-Vorlagen**: Statt einzelner Zahlenwerte wählt man je Zone in
  der Weboberfläche nur eine Pflanzenart (z. B. Schattenpflanze,
  Sonnenpflanze, Kräuter, Sukkulente/Kaktus …) aus einer Dropdown-Liste.
- **1 oder 3 LED-Streifen**: Umschaltbar über die Weboberfläche. Bei "1
  Streifen" läuft nur Zone A (gleicher Ausgang, PIN D1), Zone B/C werden
  ausgeblendet und abgeschaltet.
- **Lichttest-Schalter**: Setzt alle aktiven Zonen sofort auf 100 % Helligkeit,
  zum schnellen Prüfen der Verkabelung ohne auf den Sonnenrhythmus zu warten.
- **Deutsch/Englisch**: Die Weboberfläche lässt sich per Klick umschalten,
  die Wahl wird dauerhaft gespeichert.
- **WLAN-Einrichtung per Bluetooth (BLE-Provisioning)**: Keine WLAN-Zugangs-
  daten im Code. Einrichtung/Neueinrichtung per App direkt am Gerät.
- **Boot-Animation "Röhrenlampen-Flackern"**: Beim Start flackert jede Zone
  kurz auf wie eine alte Leuchtstoffröhre, zeitversetzt je Zone. Ist laut
  Sonnenrhythmus gerade "Nacht", flackert die Zone nur erfolglos und bleibt
  dunkel (statt anzugehen).
- **Status-LEDs**: Rote LED blinkt im Herzschlag-Rhythmus, solange kein WLAN
  verbunden ist, und erlischt bei bestehender Verbindung. Gelbe LED leuchtet,
  solange BLE-Provisioning aktiv ist.
- **OTA-Updates** und lokale Weboberfläche unter `http://smartgarden.local`.

## Hardware

Board: **Seeed XIAO ESP32-S3**

| Funktion                         | Pin  | GPIO  | Hinweis                          |
|-----------------------------------|------|-------|-----------------------------------|
| PWM Kanal A (Zone 1)              | D1   | GPIO2 |                                   |
| PWM Kanal B (Zone 2)              | D2   | GPIO3 | Boot-Strapping-Pin der S3         |
| PWM Kanal C (Zone 3)              | D10  | GPIO9 |                                   |
| Rote Status-LED (Herzschlag)      | D3   | GPIO4 |                                   |
| Gelbe Status-LED (BLE aktiv)      | D4   | GPIO5 |                                   |

## Arduino-IDE-Einrichtung

1. **Board**: Werkzeuge → Board → "XIAO_ESP32S3" auswählen.
2. **Boardverwalter-Version**: Werkzeuge → Board → Boardverwalter → "esp32"
   (Espressif Systems) → **Version 3.3.6** installieren/auswählen.
   > ⚠️ Version 3.3.7 hat einen bekannten Fehler (Absturz beim BLE-
   > Provisioning auf ESP32-S3, siehe [espressif/arduino-esp32#12436](https://github.com/espressif/arduino-esp32/issues/12436)).
   > Nicht auf 3.3.7 aktualisieren, solange der Bug nicht behoben ist.
3. **Partition Scheme**: Werkzeuge → Partition Scheme → eine Variante mit
   mehr App-Speicher wählen (z. B. "Minimal SPIFFS (1.9MB APP/190KB SPIFFS)"),
   sonst meldet der Compiler "Sketch too big".
4. Benötigte Bibliotheken (im Boardpaket bereits enthalten, keine separate
   Installation nötig): `WiFi`, `WiFiProv`, `WebServer`, `Preferences`,
   `ArduinoOTA`, `ESPmDNS`.

## Erstinbetriebnahme

1. Sketch hochladen.
2. Auf dem Smartphone die App **"ESP BLE Provisioning"** (Espressif,
   kostenlos, Android/iOS) installieren.
3. In der App nach dem Gerät **`PROV_SMARTGARDEN`** suchen und verbinden.
4. Als PIN (Proof of Possession) **`abcd1234`** eingeben (im Code unter
   `PROV_POP` änderbar).
5. WLAN-SSID und -Passwort über die App an das Gerät übertragen.
6. Gerät verbindet sich, danach ist die Weboberfläche unter
   `http://smartgarden.local` erreichbar.

Ändert sich das WLAN-Passwort später, reicht ein Klick auf "WLAN-Zugangsdaten
per Bluetooth neu einrichten" in der Weboberfläche — kein erneutes Flashen
nötig.

## Weboberfläche

Erreichbar unter `http://smartgarden.local` (im gleichen WLAN):

- Aktuelle Zeit, Sonnenauf-/-untergang und aktuelle Helligkeit je Zone
- Pflanzenart je Zone auswählen und speichern
- Anzahl LED-Streifen (1 oder 3) umschalten
- Lichttest starten/beenden (100 % auf allen aktiven Zonen)
- Sprache Deutsch/Englisch umschalten
- WLAN-Zugangsdaten per Bluetooth neu einrichten

## Bekannte Probleme & Fehlerbehebung

- **Absturz beim Booten ("Guru Meditation Error", "HLI Magic mismatch")**:
  Tritt bei esp32-Boardpaket-Version 3.3.7 auf ESP32-S3 auf (siehe oben).
  Fix: auf Version 3.3.6 herunterstufen und neu hochladen.
- **`Failed to connect to ESP32-S3: No serial data received"` beim Hochladen**:
  Kann passieren, wenn die native USB-Verbindung durch eine Absturzschleife
  instabil ist. Abhilfe: BOOT-Taste gedrückt halten während USB angeschlossen
  wird (oder BOOT gedrückt halten und kurz RESET drücken), dann hochladen.
  Nach dem Hochladen einmal RESET drücken, um die Firmware normal zu starten.
  Vor dem Hochladen den Serial Monitor schließen.
