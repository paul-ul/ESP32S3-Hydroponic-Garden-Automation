/*
 * Smart Garden Kopfteil v23 - Firmware fuer XIAO ESP32-S3
 * (in der Arduino-IDE unter Werkzeuge > Board "XIAO_ESP32S3" auswaehlen)
 * ----------------------------------------------------------
 * WLAN-Zugangsdaten werden NICHT mehr im Code hinterlegt, sondern per
 * Bluetooth (BLE) mit der App "ESP BLE Provisioning" (Espressif,
 * kostenlos, Android/iOS) auf dem Geraet eingerichtet. Aendert sich das
 * WLAN-Passwort spaeter, reicht ein Klick auf "WLAN per Bluetooth neu
 * einrichten" in der Weboberflaeche - kein erneutes Flashen noetig.
 *
 * Geraetename in der App : PROV_SMARTGARDEN
 * PIN (Proof of Possession) : abcd1234   (unten aenderbar)
 *
 * Steuerungskonzept: Der Lichtrhythmus folgt dem echten Sonnenauf-/
 * -untergang, lokal berechnet (NOAA/USNO-Formel), kein Internetdienst
 * dafuer noetig - nur die Uhrzeit kommt per NTP. Jede der drei Zonen
 * (A/B/C) hat ein eigenes Profil (Start-/Ende-Offset, max. Helligkeit,
 * Fade-Dauer, Mindest-/Maximal-Lichtdauer), einstellbar ueber eine
 * lokale Weboberflaeche, passend zur jeweiligen Pflanzenart.
 * Die LED-Ansteuerung laeuft auch weiter, wenn gerade kein WLAN da ist.
 *
 * Pinbelegung (siehe Schaltplan, GPIO-Nummern gelten fuer die XIAO ESP32-S3):
 *   D1  (GPIO2) -> PWM Kanal A (Einsatz 1)
 *   D2  (GPIO3) -> PWM Kanal B (Einsatz 2) - Achtung: Boot-Strapping-Pin der S3
 *   D10 (GPIO9) -> PWM Kanal C (Einsatz 3)
 *   D3  (GPIO4) -> rote Status-LED (Herzschlag)
 *   D4  (GPIO5) -> gelbe Status-LED (BLE-Provisioning aktiv)
 *
 * WICHTIG beim Flashen: Werkzeuge > Partition Scheme > eine Variante mit
 * mehr App-Speicher waehlen (z. B. "Minimal SPIFFS (1.9MB APP/190KB SPIFFS)"),
 * sonst meldet der Compiler "Sketch too big" - BLE-Provisioning braucht Platz.
 */

#include <WiFi.h>
#include "WiFiProv.h"
#include <WebServer.h>
#include <Preferences.h>
#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <time.h>
#include <math.h>

// ---------- BLE-Provisioning ----------
const char* PROV_POP          = "abcd1234";          // PIN, in der App einzugeben
const char* PROV_SERVICE_NAME = "PROV_SMARTGARDEN";  // Geraetename in der App
const char* HOSTNAME          = "smartgarden";

// ---------- Standort & Zeitzone ----------
const float STANDORT_LAT = 51.2277;   // Duesseldorf als Platzhalter - anpassen
const float STANDORT_LON = 6.7735;
const char* TZ_STRING = "CET-1CEST,M3.5.0,M10.5.0/3";  // Deutschland inkl. Sommerzeit

// ---------- Pins & PWM ----------
const int PIN_A = D1;
const int PIN_B = D2;
const int PIN_C = D10;
const int PWM_FREQ = 5000;
const int PWM_BITS = 10;      // Aufloesung: 0-1023
const int PWM_MAX  = 1023;

// ---------- Status-LEDs ----------
// Pins bei Bedarf an die tatsaechliche Verkabelung anpassen.
const int LED_ROT_PIN  = D3;   // Herzschlag: Firmware laeuft, aber (noch) kein WLAN
const int LED_GELB_PIN = D4;   // Leuchtet, solange BLE-Provisioning aktiv ist

// ---------- Pro-Zone-Konfiguration ----------
struct ZoneConfig {
  int offsetStartMin;   // Minuten relativ zum Sonnenaufgang (negativ = frueher an)
  int offsetEndMin;     // Minuten relativ zum Sonnenuntergang (positiv = spaeter aus)
  int maxBrightness;    // 0-100 %
  int fadeMin;          // Minuten sanftes Ein-/Ausblenden
  int minPhotoMin;      // Mindest-Lichtdauer in Minuten (Kappung nach unten)
  int maxPhotoMin;      // Maximale Lichtdauer in Minuten (Kappung nach oben)
};

// ---------- Pflanzenart-Vorlagen ----------
// Statt einzelner Zahlen waehlt man in der Weboberflaeche nur noch eine Pflanzenart;
// die passenden Werte werden aus dieser Liste uebernommen.
struct Pflanzenpreset {
  const char* name;
  const char* nameEn;
  ZoneConfig config;
};

const Pflanzenpreset PFLANZEN_PRESETS[] = {
  {"Schattenpflanze",        "Shade plant",           {-30, 30, 80, 20, 480, 960}},
  {"Sonnenpflanze",          "Sun plant",             {0, 0, 100, 15, 480, 960}},
  {"Empfindliche Art",       "Sensitive species",     {-15, 15, 60, 30, 480, 960}},
  {"Kraeuter",               "Herbs",                 {-10, 10, 70, 20, 600, 900}},
  {"Gemuese/Fruchtpflanze",  "Vegetable/fruit plant", {0, 30, 100, 15, 720, 1080}},
  {"Sukkulente/Kaktus",      "Succulent/cactus",      {30, -30, 90, 10, 360, 600}},
};
const int PFLANZEN_ANZAHL = sizeof(PFLANZEN_PRESETS) / sizeof(PFLANZEN_PRESETS[0]);

int presetIndexA = 0;   // Schattenpflanze
int presetIndexB = 1;   // Sonnenpflanze
int presetIndexC = 2;   // Empfindliche Art

ZoneConfig zoneA = PFLANZEN_PRESETS[presetIndexA].config;
ZoneConfig zoneB = PFLANZEN_PRESETS[presetIndexB].config;
ZoneConfig zoneC = PFLANZEN_PRESETS[presetIndexC].config;

// ---------- Licht-Testmodus ----------
bool testModusAktiv = false;   // true = alle drei Zonen fest auf 100%

// ---------- LED-Streifen-Anzahl ----------
// 1 = nur ein Streifen vorhanden, haengt am Ausgang von Zone A (PIN_A); Zone B/C bleiben aus.
// 3 = ein Streifen je Zone (A/B/C), Normalfall.
int ledStreifenAnzahl = 3;

// ---------- Sprache ----------
bool spracheEnglisch = false;   // false = Deutsch, true = Englisch (per /sprache umschaltbar)

Preferences prefs;
WebServer server(80);

volatile bool wlanVerbunden = false;   // wird auch von der Status-LED-Timer-ISR gelesen
bool serverGestartet = false;
unsigned long letzterUpdate = 0;

// ---------- Status-LEDs ----------
// Laufen ueber eine echte Hardware-Timer-ISR, nicht ueber loop() oder einen Ticker/
// esp_timer-Task - ein Task kann waehrend des BLE-Stack-Starts in WiFiProv.beginProvision()
// mehrere Sekunden lang keine CPU-Zeit bekommen (dann bleibt die LED die ganze Zeit stehen);
// eine Hardware-ISR hat dagegen hoehere Prioritaet als jeder normale Task und feuert weiter.
hw_timer_t *statusLedTimer = nullptr;
volatile bool bleAktiv = false;   // von SysProvEvent gesetzt (laeuft in eigenem Task)

// Herzschlag-Muster (an/aus/an/aus-lang), wie ein "lub-dub":
struct HerzschlagPhase { unsigned long dauerMs; bool an; };
const HerzschlagPhase HERZSCHLAG_MUSTER[] = {
  {100, true}, {150, false}, {100, true}, {650, false}
};
const int HERZSCHLAG_PHASEN = sizeof(HERZSCHLAG_MUSTER) / sizeof(HERZSCHLAG_MUSTER[0]);
int herzschlagIndex = 0;
unsigned long herzschlagWechsel = 0;

// ---------- Boot-Animation (Roehrenlampen-Flackern) ----------
// Beim ersten Anwenden der Zielhelligkeit flackert jede Zone kurz auf wie eine
// alte Leuchtstoffroehre beim Einschalten - zeitversetzt je Zone, wie in einem
// Raum mit mehreren Roehren. Ist die Zone laut Sonnen-Rhythmus gerade "Nacht"
// (Zielhelligkeit 0), flackert sie nur erfolglos und bleibt dunkel.
const HerzschlagPhase ROEHRE_ERFOLG[] = {   // Ziellicht soll angehen
  {50, false}, {40, true}, {80, false}, {30, true}, {30, false},
  {25, true}, {150, false}, {60, true}, {40, false}, {400, true}
};
const HerzschlagPhase ROEHRE_FEHLSCHLAG[] = {   // bleibt dunkel (Nacht im Rhythmus)
  {60, false}, {35, true}, {90, false}, {30, true}, {40, false}, {25, true}, {200, false}
};
const int ROEHRE_ERFOLG_N     = sizeof(ROEHRE_ERFOLG) / sizeof(ROEHRE_ERFOLG[0]);
const int ROEHRE_FEHLSCHLAG_N = sizeof(ROEHRE_FEHLSCHLAG) / sizeof(ROEHRE_FEHLSCHLAG[0]);
const int ROEHRE_FLACKER_DUTY = PWM_MAX * 0.25;   // schwaches Flackern bei einem Fehlversuch

enum BootPhase { BOOT_WARTEN, BOOT_FLACKERN, BOOT_FERTIG };
struct BootZustand {
  BootPhase phase;
  unsigned long startVersatzMs;   // Verzoegerung ggue. den anderen Zonen (nacheinander angehen)
  int schritt;
  unsigned long naechsterSchritt;
  int zielDuty;
  bool erfolgreich;
};
BootZustand bootA = {BOOT_WARTEN, 0,    0, 0, 0, false};
BootZustand bootB = {BOOT_WARTEN, 500,  0, 0, 0, false};
BootZustand bootC = {BOOT_WARTEN, 1000, 0, 0, 0, false};
bool bootAnimationAktiv = true;
bool bootAnimationGestartet = false;
unsigned long bootAnimationStartMs = 0;

int cachedYday = -1;
float sonnenaufgangMin = 360;
float sonnenuntergangMin = 1080;

// ================= Sonnenauf-/-untergang =================
// Klassische NOAA/USNO-Formel (Almanac for Computers, 1990).
float berechneSonnenzeit(int dayOfYear, float lat, float lon, bool aufgang) {
  float lngHour = lon / 15.0;
  float t = dayOfYear + ((aufgang ? 6.0 : 18.0) - lngHour) / 24.0;

  float M = (0.9856 * t) - 3.289;
  float L = M + (1.916 * sin(radians(M))) + (0.020 * sin(radians(2 * M))) + 282.634;
  L = fmod(L + 360.0, 360.0);

  float RA = degrees(atan(0.91764 * tan(radians(L))));
  RA = fmod(RA + 360.0, 360.0);
  float Lq = floor(L / 90.0) * 90.0;
  float RAq = floor(RA / 90.0) * 90.0;
  RA = (RA + (Lq - RAq)) / 15.0;

  float sinDec = 0.39782 * sin(radians(L));
  float cosDec = cos(asin(sinDec));

  float cosH = (cos(radians(90.833)) - (sinDec * sin(radians(lat)))) / (cosDec * cos(radians(lat)));
  if (cosH > 1.0 || cosH < -1.0) return -1;

  float H = aufgang ? (360.0 - degrees(acos(cosH))) : degrees(acos(cosH));
  H = H / 15.0;

  float T = H + RA - (0.06571 * t) - 6.622;
  float UT = fmod(T - lngHour + 24.0, 24.0);
  return UT * 60.0;
}

void aktualisiereSonnenzeiten() {
  time_t now = time(nullptr);
  struct tm t;
  localtime_r(&now, &t);
  if (t.tm_yday == cachedYday) return;
  cachedYday = t.tm_yday;

  float aufgangUTC = berechneSonnenzeit(t.tm_yday + 1, STANDORT_LAT, STANDORT_LON, true);
  float untergangUTC = berechneSonnenzeit(t.tm_yday + 1, STANDORT_LAT, STANDORT_LON, false);

  // tm_gmtoff ist auf diesem Core nicht verfuegbar - Offset stattdessen ueber
  // den Umweg lokale/UTC-Felder via mktime() bestimmen (portabler Standard-Trick).
  struct tm utc;
  gmtime_r(&now, &utc);
  long offsetMin = (long)difftime(mktime(&t), mktime(&utc)) / 60;

  sonnenaufgangMin = fmod(aufgangUTC + offsetMin + 1440, 1440);
  sonnenuntergangMin = fmod(untergangUTC + offsetMin + 1440, 1440);
}

// ================= PWM je Zone berechnen =================
int berechneDuty(const ZoneConfig &z, float nowMin) {
  float start = sonnenaufgangMin + z.offsetStartMin;
  float ende  = sonnenuntergangMin + z.offsetEndMin;
  float dauer = ende - start;

  if (dauer < z.minPhotoMin) {
    float diff = (z.minPhotoMin - dauer) / 2.0;
    start -= diff; ende += diff;
  } else if (dauer > z.maxPhotoMin) {
    float diff = (dauer - z.maxPhotoMin) / 2.0;
    start += diff; ende -= diff;
  }

  if (nowMin < start - z.fadeMin || nowMin > ende + z.fadeMin) return 0;

  float faktor = 1.0;
  int fade = max(1, z.fadeMin);
  if (nowMin < start) faktor = (nowMin - (start - fade)) / fade;
  else if (nowMin > ende) faktor = ((ende + fade) - nowMin) / fade;

  faktor = constrain(faktor, 0.0, 1.0);
  return (int)(faktor * (z.maxBrightness / 100.0) * PWM_MAX);
}

void wendeHelligkeitAn() {
  if (time(nullptr) < 1700000000) return;  // noch keine gueltige Uhrzeit -> Kanaele bleiben aus
  aktualisiereSonnenzeiten();
  time_t now = time(nullptr);
  struct tm t;
  localtime_r(&now, &t);
  float nowMin = t.tm_hour * 60 + t.tm_min + t.tm_sec / 60.0;

  ledcWrite(PIN_A, berechneDuty(zoneA, nowMin));
  if (ledStreifenAnzahl == 3) {
    ledcWrite(PIN_B, berechneDuty(zoneB, nowMin));
    ledcWrite(PIN_C, berechneDuty(zoneC, nowMin));
  }
}

// ================= Boot-Animation (Roehrenlampen-Flackern) =================
int roehreWert(const BootZustand &b, const HerzschlagPhase &schritt) {
  if (!schritt.an) return 0;
  return b.erfolgreich ? b.zielDuty : ROEHRE_FLACKER_DUTY;
}

void aktualisiereBootZone(int pin, BootZustand &b, const ZoneConfig &z, float nowMin) {
  if (b.phase == BOOT_FERTIG) return;

  if (b.phase == BOOT_WARTEN) {
    if (millis() - bootAnimationStartMs < b.startVersatzMs) return;   // diese Zone ist erst spaeter dran
    b.zielDuty = berechneDuty(z, nowMin);
    b.erfolgreich = b.zielDuty > 0;
    b.phase = BOOT_FLACKERN;
    b.schritt = 0;
    b.naechsterSchritt = millis();
    const HerzschlagPhase* muster = b.erfolgreich ? ROEHRE_ERFOLG : ROEHRE_FEHLSCHLAG;
    ledcWrite(pin, roehreWert(b, muster[0]));
    return;
  }

  const HerzschlagPhase* muster = b.erfolgreich ? ROEHRE_ERFOLG : ROEHRE_FEHLSCHLAG;
  int anzahl = b.erfolgreich ? ROEHRE_ERFOLG_N : ROEHRE_FEHLSCHLAG_N;

  if (millis() - b.naechsterSchritt < muster[b.schritt].dauerMs) return;

  b.schritt++;
  b.naechsterSchritt = millis();

  if (b.schritt >= anzahl) {
    b.phase = BOOT_FERTIG;
    ledcWrite(pin, b.erfolgreich ? b.zielDuty : 0);   // Endzustand fixieren
    return;
  }

  ledcWrite(pin, roehreWert(b, muster[b.schritt]));
}

void aktualisiereBootAnimation() {
  if (time(nullptr) < 1700000000) return;   // ohne gueltige Uhrzeit ist Tag/Nacht nicht bekannt

  if (!bootAnimationGestartet) {
    bootAnimationGestartet = true;
    bootAnimationStartMs = millis();
  }

  aktualisiereSonnenzeiten();
  time_t now = time(nullptr);
  struct tm t;
  localtime_r(&now, &t);
  float nowMin = t.tm_hour * 60 + t.tm_min + t.tm_sec / 60.0;

  aktualisiereBootZone(PIN_A, bootA, zoneA, nowMin);
  if (ledStreifenAnzahl == 3) {
    aktualisiereBootZone(PIN_B, bootB, zoneB, nowMin);
    aktualisiereBootZone(PIN_C, bootC, zoneC, nowMin);
  }

  bool bFertig = (ledStreifenAnzahl == 3) ? bootB.phase == BOOT_FERTIG : true;
  bool cFertig = (ledStreifenAnzahl == 3) ? bootC.phase == BOOT_FERTIG : true;
  if (bootA.phase == BOOT_FERTIG && bFertig && cFertig) {
    bootAnimationAktiv = false;
  }
}

// ================= Status-LEDs =================
// IRAM_ATTR: laeuft in einer Hardware-Timer-ISR, muss daher im internen RAM liegen
// (kein Zugriff auf ausgelagerten Flash-Code, falls der Flash-Cache gerade gesperrt ist).
void IRAM_ATTR aktualisiereStatusLEDs() {
  digitalWrite(LED_GELB_PIN, bleAktiv ? HIGH : LOW);

  if (wlanVerbunden) {
    digitalWrite(LED_ROT_PIN, LOW);
    herzschlagIndex = 0;
    herzschlagWechsel = millis();   // Muster von vorn beginnen, falls WLAN spaeter wieder weg ist
    return;
  }

  if (millis() - herzschlagWechsel >= HERZSCHLAG_MUSTER[herzschlagIndex].dauerMs) {
    herzschlagWechsel = millis();
    herzschlagIndex = (herzschlagIndex + 1) % HERZSCHLAG_PHASEN;
    digitalWrite(LED_ROT_PIN, HERZSCHLAG_MUSTER[herzschlagIndex].an ? HIGH : LOW);
  }
}

// ================= Einstellungen speichern/laden (NVS) =================
void ladePreset(const char* prefix, int &presetIndex, ZoneConfig &z) {
  presetIndex = prefs.getInt((String(prefix) + "pi").c_str(), presetIndex);
  presetIndex = constrain(presetIndex, 0, PFLANZEN_ANZAHL - 1);
  z = PFLANZEN_PRESETS[presetIndex].config;
}
void speicherePreset(const char* prefix, int presetIndex) {
  prefs.putInt((String(prefix) + "pi").c_str(), presetIndex);
}

// ================= Webinterface =================
// Uebersetzungshelfer: liefert je nach eingestellter Sprache den deutschen oder
// englischen Text zurueck. Heisst bewusst nicht "t", um nicht mit den vielen
// lokalen "struct tm t"-Variablen im Sketch zu kollidieren.
String tr(const char* de, const char* en) {
  return spracheEnglisch ? String(en) : String(de);
}

String formatMin(float m) {
  int h = ((int)m / 60) % 24;
  int mi = (int)m % 60;
  char buf[6];
  sprintf(buf, "%02d:%02d", h, mi);
  return String(buf);
}

String zoneFormular(const char* id, const char* name, int aktuellerPreset) {
  String s = "<fieldset><legend>" + tr("Zone ", "Zone ") + String(name) + "</legend>";
  s += tr("Pflanzenart: ", "Plant type: ") + "<select name='" + String(id) + "_preset'>";
  for (int i = 0; i < PFLANZEN_ANZAHL; i++) {
    s += "<option value='" + String(i) + "'";
    if (i == aktuellerPreset) s += " selected";
    s += ">" + String(spracheEnglisch ? PFLANZEN_PRESETS[i].nameEn : PFLANZEN_PRESETS[i].name) + "</option>";
  }
  s += "</select>";
  s += "</fieldset>";
  return s;
}

void handleRoot() {
  time_t now = time(nullptr);
  struct tm t; localtime_r(&now, &t);
  char zeitBuf[32];
  strftime(zeitBuf, sizeof(zeitBuf), "%d.%m.%Y %H:%M:%S", &t);

  String helligkeit = "A: " + String(ledcRead(PIN_A) * 100 / PWM_MAX) + "%";
  if (ledStreifenAnzahl == 3) {
    helligkeit += " &middot; B: " + String(ledcRead(PIN_B) * 100 / PWM_MAX) + "%";
    helligkeit += " &middot; C: " + String(ledcRead(PIN_C) * 100 / PWM_MAX) + "%";
  }

  String html = "<html><head><meta charset='utf-8'><title>Smart Garden</title></head><body>";
  html += "<p style='text-align:right'><a href='/sprache'>" + String(spracheEnglisch ? "Deutsch" : "English") + "</a></p>";
  html += "<h2>" + tr("Smart Garden Kopfteil", "Smart Garden Controller") + "</h2>";
  html += "<p>" + tr("Aktuelle Zeit: ", "Current time: ") + String(zeitBuf) + "</p>";
  html += "<p>" + tr("Sonnenaufgang heute: ", "Sunrise today: ") + formatMin(sonnenaufgangMin) + " &middot; " + tr("Sonnenuntergang: ", "Sunset: ") + formatMin(sonnenuntergangMin) + "</p>";
  html += "<p>" + tr("Aktuelle Helligkeit", "Current brightness") + " &ndash; " + helligkeit + "</p>";

  html += "<p>" + tr("LED-Streifen: ", "LED strips: ") + String(ledStreifenAnzahl);
  html += ledStreifenAnzahl == 1 ? tr(" (nur Zone A)", " (Zone A only)") : tr(" (Zone A/B/C)", " (Zone A/B/C)");
  html += " &middot; <a href='/ledcount'>";
  html += ledStreifenAnzahl == 1 ? tr("auf 3 Streifen umschalten", "switch to 3 strips") : tr("auf 1 Streifen umschalten", "switch to 1 strip");
  html += "</a></p>";

  html += "<form method='POST' action='/save'>";
  html += zoneFormular("a", "A", presetIndexA);
  if (ledStreifenAnzahl == 3) {
    html += zoneFormular("b", "B", presetIndexB);
    html += zoneFormular("c", "C", presetIndexC);
  }
  html += "<br><input type='submit' value='" + tr("Speichern", "Save") + "'></form>";
  html += "<hr><p><a href='/test'>" + tr(testModusAktiv ? "Licht-Test beenden (zurueck zum Automatikbetrieb)" : "Licht-Test starten (alle Zonen auf 100%)",
                                          testModusAktiv ? "Stop light test (back to automatic mode)" : "Start light test (all zones to 100%)") + "</a></p>";
  html += "<p><a href='/reprov'>" + tr("WLAN-Zugangsdaten per Bluetooth neu einrichten", "Reconfigure WiFi credentials via Bluetooth") + "</a></p>";
  html += "</body></html>";
  server.send(200, "text/html", html);
}

void uebernehmePreset(const char* id, int &presetIndex, ZoneConfig &z) {
  if (server.hasArg(String(id) + "_preset")) {
    presetIndex = constrain(server.arg(String(id) + "_preset").toInt(), 0, PFLANZEN_ANZAHL - 1);
    z = PFLANZEN_PRESETS[presetIndex].config;
  }
}

void handleSave() {
  uebernehmePreset("a", presetIndexA, zoneA); speicherePreset("a", presetIndexA);
  uebernehmePreset("b", presetIndexB, zoneB); speicherePreset("b", presetIndexB);
  uebernehmePreset("c", presetIndexC, zoneC); speicherePreset("c", presetIndexC);
  server.sendHeader("Location", "/");
  server.send(303, "text/plain", "");
}

void handleTest() {
  testModusAktiv = !testModusAktiv;
  if (testModusAktiv) {
    ledcWrite(PIN_A, PWM_MAX);
    if (ledStreifenAnzahl == 3) {
      ledcWrite(PIN_B, PWM_MAX);
      ledcWrite(PIN_C, PWM_MAX);
    }
  }
  server.sendHeader("Location", "/");
  server.send(303, "text/plain", "");
}

void handleLedCount() {
  ledStreifenAnzahl = (ledStreifenAnzahl == 3) ? 1 : 3;
  prefs.putInt("ledCount", ledStreifenAnzahl);
  if (ledStreifenAnzahl == 1) {   // Zone B/C nicht verkabelt - Ausgaenge sauber abschalten
    ledcWrite(PIN_B, 0);
    ledcWrite(PIN_C, 0);
  }
  server.sendHeader("Location", "/");
  server.send(303, "text/plain", "");
}

void handleSprache() {
  spracheEnglisch = !spracheEnglisch;
  prefs.putBool("langEn", spracheEnglisch);
  server.sendHeader("Location", "/");
  server.send(303, "text/plain", "");
}

void handleReprov() {
  prefs.putBool("reprov", true);
  String seite = "<html><body>";
  seite += tr("WLAN wird zurueckgesetzt. Das Geraet startet neu und wartet auf die Bluetooth-App (Geraetename: PROV_SMARTGARDEN).",
              "WiFi is being reset. The device will restart and wait for the Bluetooth app (device name: PROV_SMARTGARDEN).");
  seite += "</body></html>";
  server.send(200, "text/html", seite);
  delay(1000);
  ESP.restart();
}

// ================= BLE-Provisioning: Ereignisse =================
// Wird aus einem eigenen FreeRTOS-Task aufgerufen (nicht aus loop()).
void SysProvEvent(arduino_event_t *sys_event) {
  switch (sys_event->event_id) {
    case ARDUINO_EVENT_PROV_START:
      bleAktiv = true;
      Serial.println("BLE-Provisioning gestartet - jetzt mit der App verbinden");
      break;
    case ARDUINO_EVENT_PROV_END:
      bleAktiv = false;
      Serial.println("BLE-Provisioning beendet");
      break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      Serial.print("WLAN verbunden, IP: ");
      Serial.println(IPAddress(sys_event->event_info.got_ip.ip_info.ip.addr));
      break;
    case ARDUINO_EVENT_PROV_CRED_RECV:
      Serial.println("Neue WLAN-Zugangsdaten per Bluetooth empfangen");
      break;
    case ARDUINO_EVENT_PROV_CRED_FAIL:
      Serial.println("Provisioning fehlgeschlagen - falsches Passwort oder Netz nicht gefunden");
      break;
    case ARDUINO_EVENT_PROV_CRED_SUCCESS:
      Serial.println("Provisioning erfolgreich - starte neu, um sauber zu verbinden");
      delay(2000);
      ESP.restart();   // umgeht einen bekannten Reconnect-Bug nach frischem Provisioning
      break;
    default:
      break;
  }
}

// ================= Setup / Loop =================
void setup() {
  Serial.begin(115200);

  ledcAttach(PIN_A, PWM_FREQ, PWM_BITS);
  ledcAttach(PIN_B, PWM_FREQ, PWM_BITS);
  ledcAttach(PIN_C, PWM_FREQ, PWM_BITS);

  pinMode(LED_ROT_PIN, OUTPUT);
  pinMode(LED_GELB_PIN, OUTPUT);
  digitalWrite(LED_ROT_PIN, HIGH);
  digitalWrite(LED_GELB_PIN, LOW);
  herzschlagWechsel = millis();
  // Alle 20ms per Hardware-Timer-Interrupt pruefen/schalten - laeuft unabhaengig von
  // loop() bzw. jedem Task, damit die LED auch waehrend des (mehrere Sekunden dauernden)
  // BLE-Stack-Starts weiter blinkt (eine ISR wird nicht wie ein Task von der CPU verdraengt).
  statusLedTimer = timerBegin(1000000);               // 1 MHz Taktbasis -> 1 Tick = 1us
  timerAttachInterrupt(statusLedTimer, &aktualisiereStatusLEDs);
  timerAlarm(statusLedTimer, 20000, true, 0);         // alle 20000us = 20ms, Auto-Reload

  prefs.begin("garden", false);
  ladePreset("a", presetIndexA, zoneA);
  ladePreset("b", presetIndexB, zoneB);
  ladePreset("c", presetIndexC, zoneC);

  ledStreifenAnzahl = prefs.getInt("ledCount", 3);
  if (ledStreifenAnzahl != 1 && ledStreifenAnzahl != 3) ledStreifenAnzahl = 3;
  if (ledStreifenAnzahl == 1) { ledcWrite(PIN_B, 0); ledcWrite(PIN_C, 0); }

  spracheEnglisch = prefs.getBool("langEn", false);

  bool reprov = prefs.getBool("reprov", false);
  if (reprov) prefs.putBool("reprov", false);   // Flag verbrauchen, nicht bei jedem Boot zuruecksetzen

  WiFi.onEvent(SysProvEvent);
  // reset_provisioned=reprov: normal (false) nutzt gespeicherte Zugangsdaten automatisch,
  // true (nach Klick auf "WLAN neu einrichten") zwingt in den BLE-Provisioning-Modus.
  WiFiProv.beginProvision(NETWORK_PROV_SCHEME_BLE, NETWORK_PROV_SCHEME_HANDLER_FREE_BTDM,
                           NETWORK_PROV_SECURITY_1, PROV_POP, PROV_SERVICE_NAME, NULL, NULL, reprov);

  ArduinoOTA.setHostname(HOSTNAME);

  // Kanaele bleiben aus, bis eine gueltige Uhrzeit da ist - dann uebernimmt
  // aktualisiereBootAnimation() in loop() mit dem Roehrenlampen-Flackern.
}

void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    if (!wlanVerbunden) {
      wlanVerbunden = true;
      configTzTime(TZ_STRING, "pool.ntp.org", "de.pool.ntp.org");  // bei jeder (Wieder-)Verbindung neu synchronisieren
      if (!serverGestartet) {
        if (MDNS.begin(HOSTNAME)) Serial.println("Erreichbar unter http://" + String(HOSTNAME) + ".local");
        server.on("/", handleRoot);
        server.on("/save", HTTP_POST, handleSave);
        server.on("/reprov", handleReprov);
        server.on("/test", handleTest);
        server.on("/ledcount", handleLedCount);
        server.on("/sprache", handleSprache);
        server.begin();
        ArduinoOTA.begin();
        serverGestartet = true;
      }
    }
    ArduinoOTA.handle();
    server.handleClient();
  } else {
    wlanVerbunden = false;
  }

  if (bootAnimationAktiv) {
    aktualisiereBootAnimation();
  } else if (!testModusAktiv && millis() - letzterUpdate > 10000) {   // alle 10 Sekunden nachfuehren, auch ohne WLAN
    letzterUpdate = millis();
    wendeHelligkeitAn();
  }
}
