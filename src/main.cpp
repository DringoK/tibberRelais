//Ingos Tibber-Relais
//
//nach dem Bauvorschlag von Uwe Siebert make 5/24 im heise-Verlag
//ESP8266 schaltet ein Relais nach den Strom-Preisen von tibber
//Unterstützt werden stündliche oder viertelstündliche Strompreise
//
// * Über ingosserver.h wird eine html-Beidienoberfläche bereitgestellt
// * diese zeigt auch die Preise als svg-Grafik
// * Tibberpreise werden über JSON-API bei Tibber abgerufen
// * Die aktuelle Zeit wird über time.h und Internetzeit.h ermittelt
//
//Autor: DringoK
//Version: siehe ***** Version ********** unten

// ********* Version **************
#define vers "2025.1005 Tibber Relais (Ingo)"
//const char *myHostName = "TibberRelais"; //<<<<<<<<<<<<<<<<<<<<<<<<<<< für Release: Namen ändern <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
const char *myHostName = "TibberTest";

#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h> //für Shelly Steckdose

//#include "ota.h"
#include <EEPROM.h>
#include "Internetzeit.h"

#include "ingosserver.h"
#include "preisLogik.h"



//Debugging auf Konsole RS232 falls define aktiv (=nicht auskommentiert)
#define DEB_CONNECT      //wifi-Connect
#define DEB_MAINLOOP     //alle Aktivitäten in der Main-Loop
#define DEB_SCHALTEN     //Schalt-Vorgänge
//#define DEB_Zeit       //Zeit zyklisch ausgeben als Lebenszeichen, nur dann wird GET_TIME_INTERVAL verwendet

#define GET_TIME_INTERVAL 10 //Zeit-Ausgabe-Intervall in Sekunden (unabhängig davon holt time.h die Zeit nur alle 60min aus vom NTP-Server)

#define OUTPIN_INVERTED     //wenn definiert, dann wird der Schaltausgang invertiert, also ein=LOW, aus=HIGH

//**********Shelly-Steckdose unterstützen
//const char* shelly_addr="192.168.178.126";  //hier Adresse der Shelly-Steckdose eintragen (aus html-Maske entfernt, alle anderen ip-Adressen sind ja auch hier im Code)
const char* shelly_addr="";  //wenn leer, dann Shelly-Steckdose nicht verwenden

//****** WLAN / Tibber-Server - Zugriff ***************
//#define MY_SSID "mySSID"         //--> replace with your own ssid and put it in a file credentials.h
//#define MY_PASSWORD "myPassword"; //--> replace with your own wifi password and put it in a file credentials.h
// API Tibber, Demo Token --> replace with your own token and put it in a file credentials.h
//#define MY_TOKEN "Bearer 3A77EECF61BD445F47241A5A36202185C35AF3AF58609E19B53F3A8872AD7BE1-1"; //das Wort "Bearer " muss vor den eigentlichen Token

#include "credentials.h" //here are Wifi Access credentials and tibber token stored, but not shared in GIT --> built your own credentials.h with just 3 lines like above with your credentials
const char *ssid = MY_SSID;    //Wifi-Netzwerkname
const char *password = MY_PASSWORD;       //Wifi Password

ADC_MODE(ADC_VCC);
#define relay 16  //D0
#define red 13    //D7
#define green 15  //D8
const uint16 LED_AN = 5 * 1023/100; //5% PWM im Dauerbetrieb. Vorher, beim Start werden die LEDs digital=voll angesteuert.

//************************** forward declarations
//********* Wifi-Client
bool connectToBestAccessPoint();    //wifiscan, um die richtige Fritzbox mit "SSID" zu finden, gibt true zurück, falls scan erfolgreich
void connectWifi();                 //Verbinde mit Wifi als Client
bool handleWifiTraffic(); //pollling des servers. Liefert true, wenn eine Server-Anfrage beantwortet wurde (=ein client erzeugt wurde)

void preisabhaengig_schalten();  //schalte für die aktuelle Uhrzeit am aktuellen Tag
void schalten (bool ein);  //schalte LEDs, Relais und Shelly Steckdose
void shelly(bool ein);     //shelly Steckdose schalten

void eeprom_loadAll();     //alle gespeicherten Parameter von html-Maske aus EEPROM laden (während setup())
void eeprom_requestSaveAll(); //verzögertes Schreiben aller Parameter aus html-Maske initiieren. Jeder Aufruf während delay setzt Wartezeit zurück (=erneut Warten=weiter Änderungen sammeln)
void eeprom_saveAll_ifRequestedAndDelay(); //in loop() regelmäßig aufrufen: prüfen ob Delay abgelaufen ist und dann speichern, sonst nichts tun
#define EEPROM_DELAY 20*1000 //in ms solange werden Änderungen vor dem Schreiben gesammelt

//*****************************************************************************************
void setup() {

  // Initialize serial
  Serial.begin(115200);

  //Initialize IO-Pins
  pinMode(relay, OUTPUT);
  digitalWrite(relay, LOW); 

  pinMode(green, OUTPUT);
  digitalWrite(green, HIGH); 

  pinMode(red, OUTPUT);
  digitalWrite(red, LOW); 

    //Init Wifi
  displayVersion = vers;

  WiFi.mode(WIFI_STA);  //station
  WiFi.hostname(myHostName);
  connectWifi();              //erster Connect hier im Setup (dann für Reconnect zyklisch im loop falls not WL_CONNECTED)
    

  //erst nach connectWifi():
  initIngosServer();
  initPreisLogik();

  //Werte der Eingabemaske aus EEPROM holen
  eeprom_loadAll();
  

  //Preise holen und erstmals schalten
  initTime(); //Verbindung zum Zeitserver herstellen
  getTime(); //Internetzeit erstmals holen und struct tm füllen

  preiseRuecksetzen();

  hole_tibber_preise();
  preiseAnalysieren();
  preisabhaengig_schalten();
  h_schalt = tm.tm_hour;

  tageswechselDurchgefuehrt= (h_schalt==0); //wenn setup() zur Stunde 0 durchgeführt wurde, dann Tageswechsel nicht gleich ausführen, da schon alles stimmt (dann nur "heute" gültig)
}

//*****************************************************************************************
// Main-Loop im 100ms Zyklus
unsigned long millis_nextTMUpdate = 0;        //Zeitpunkt in millis, an dem die Internet-Zeit als nächstes wieder abgerufen werden soll
unsigned long millis_nextRetryGetPreise = 0;  //Zeitpunkt in millis, an dem die Preise als nächstes wieder abgerufen werden sollen (Retry delay)
void loop() {
  if (WiFi.status() != WL_CONNECTED){  //damit auch reconnect möglich
    connectWifi();
  }

  //uwes_ota(); //load Sketch over the air (OTA) = WLAN , damit läuft es nicht mehr stabil: der Heap wird immer kleiner bis zum Crash

  //zyklisch die Internet-Zeit in die struct tm übernehmen
  if(millis() > millis_nextTMUpdate ) { 
    millis_nextTMUpdate = millis() + GET_TIME_INTERVAL*1000; // nur alle GET_TIME_INTERVAL Sekunden die struct tm aktualisieren
    getTime();
    #ifdef DEB_Zeit
      serialPrintlnTime();
    #endif
  }

  //ab STUNDE_PREISE_NEU die Preise aktualisieren, falls nicht schon geschehen
  if( !morgenGelesen && (tm.tm_hour>=STUNDE_PREISE_NEU) && (millis() > millis_nextRetryGetPreise)) {
    #ifdef DEB_MAINLOOP
      serialPrintTimeShort();
      Serial.println("-> Preise aktualisieren");
    #endif
    millis_nextRetryGetPreise = millis() + PREISE_GET_RETRY_DELAY * 1000; // nur alle PREISE_GET_RETRY_DELAY Sekunden wieder probieren

    hole_tibber_preise();
    if (morgenGelesen){
      #ifdef DEB_MAINLOOP
        Serial.println("-> Preise von morgen ok :-)");
      #endif
      millis_nextRetryGetPreise = 0; //dann retry-Delay für nächsten Tag zurücksetzen
    }

    preiseAnalysieren();  //min, max, average, preise aufsteigend,...
  }
  
  //um Mitternacht Tageswechsel durchführen
  if (!tageswechselDurchgefuehrt && (tm.tm_hour==0)){
    #ifdef DEB_MAINLOOP
      serialPrintTimeShort();
      Serial.println("-> Tageswechsel durchführen");
    #endif
    tagesWechsel();
    tageswechselDurchgefuehrt = true; //für Stunde 0 merken, dass er schon durchgeführt ist
  }
  if (tm.tm_hour>0) tageswechselDurchgefuehrt=false; //an allen anderen Stunden Variable zurücksetzen

  //zu jeder vollen Stunde schalten
  if(tm.tm_hour != h_schalt) { //wenn noch nicht für aktuelle Stunde geschalten
    #ifdef DEB_MAINLOOP
      serialPrintTimeShort();
      Serial.println("-> Schalten");
    #endif

    preisabhaengig_schalten();
    h_schalt = tm.tm_hour;
  }

  //falls es eine Eingabe über die html-Maske gegeben hat
  if( handleWifiTraffic() ) { // es hat eine Eingabe ueber die html-Maske gegeben, bei Änderungen wird preisabhaengig_schalten() aufgerufen
    #ifdef DEB_MAINLOOP
      serialPrintTimeShort();
      Serial.println("-> Wifi Traffic");
    #endif

    eeprom_requestSaveAll();

    if (refreshPressed){
      hole_tibber_preise();
    }  
  }


  eeprom_saveAll_ifRequestedAndDelay(); //gibt es etwas im EEPROM zu speichern
  
  delay(100); 
} //loop ende

//*********************************************************************************
void connectWifi()
{
  Reconnect_Zaehler++;

  if (!connectToBestAccessPoint()){  //probiere erst über Scan zu verbinden
    WiFi.begin(ssid, password); // und wenn das nicht klappt, dann "normal"
  }  

  #ifdef DEB_CONNECT
    Serial.println("\nConnecting to WiFi..."); // Wait for WiFi connection
  #endif

  bool blink = false;

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    #ifdef DEB_CONNECT
      Serial.print(".");
    #endif
    digitalWrite(red, blink);
    digitalWrite(green, !blink);
    blink = !blink;
  }
}

//***********************************
bool connectToBestAccessPoint() {
  #ifdef DEB_CONNECT
    Serial.println("\nStarte vollständigen WLAN-Scan...");
  #endif  
  
  int n = 0;
  n = WiFi.scanNetworks(false, false, 0, (uint8*)ssid); //nicht async, nicht showHidden, 0=alle Channels, nur solche mit Namen ssid
  #ifdef DEB_CONNECT
    Serial.printf("%d Netzwerke gefunden\n", n);
  #endif  


  if (n == 0) {
    return false;
  }
  int bestNetwork = -1;
  int bestSignal = -100; // Niedriger Wert bedeutet schwaches Signal

  for (int i = 0; i < n; i++) {
    #ifdef DEB_CONNECT
      Serial.printf("SSID=%s | MAC=%s | RSSI=%d\n", WiFi.SSID(i).c_str(), WiFi.BSSIDstr(i).c_str(), WiFi.RSSI(i));
    #endif
    if (WiFi.SSID(i) == ssid && WiFi.RSSI(i) > bestSignal) {
      bestSignal = WiFi.RSSI(i);
      bestNetwork = i;
    }
  }
  
  if (bestNetwork != -1) {
    #ifdef DEB_CONNECT
      Serial.print("Verbinde mit: ");
      Serial.println(WiFi.BSSIDstr(bestNetwork));
    #endif  
    WiFi.begin(ssid, password, 0, WiFi.BSSID(bestNetwork));
  } else {
    #ifdef DEB_CONNECT
      Serial.println("Kein passendes Netzwerk gefunden!");
      return false;
    #endif  
  }
  return true;
}

char tm_Str[25];
//*****************************************************************************************************************************
char* getTM_Str(){
  printf(tm_Str, "%s %02d.%02d.%04d %02d:%02d:%02d\0",wochentag[tm.tm_wday],tm.tm_mday,tm.tm_mon,tm.tm_year,tm.tm_hour,tm.tm_min,tm.tm_sec);
  return tm_Str;
}
//*****************************************************************************************************************************
//polling des servers. Liefert true, wenn eine Server-Anfrage beantwortet wurde (=ein client erzeugt wurde)
bool handleWifiTraffic() {
  if (serverAccept()) {               // Bei einem Aufruf des Servers wird auch client gesetzt
    if (clientReceiveGETRequest()){   //nur antworten, wenn es ein GET-Request war
        if (interpretRequestValues(request.c_str())){
          preisabhaengig_schalten(); //und gleich auf den neuen Modus reagieren, auf jeden Fall vor clientPrintHTMLAnwser!
        }

      //Antwort senden
      clientPrintHTTPHeader();
      clientPrintHTMLAnwser(tm.tm_hour, getTM_Str());  //Antwortseite aufbauen
    }else{
      void clientPrint404NotFound();
    }

    // Die Verbindung beenden
    clientStop();

    return true;
  }else{
    return false;
  }

}

//*****************************************************************************
//Schalte Shelly und Relais entsprechend dem getSchaltWert() für die aktuelle Stunde schalten
void preisabhaengig_schalten(){
  schalten(getSchaltWert(heute, tm.tm_hour));
}


//*****************************************************************************************
void schalten (bool ein) {
  #ifdef DEB_SCHALTEN
    serialPrintTimeShort();
    Serial.printf("Schalten(): Heizung ein: %d, aktuelle Stunde: %d\n", ein, tm.tm_hour);
  #endif  
  
  if (ein){
    digitalWrite(red, 0);
    analogWrite(green, LED_AN); //LED über PWM dunkler
  }else{
    analogWrite(red, LED_AN); //LED über PWM dunkler
    digitalWrite(green, 0);
  }  

  #ifdef OUTPIN_INVERTED
    digitalWrite(relay, !ein); //Relais bekommt Strom für aus
  #else
    digitalWrite(relay, ein); //Relais bekommt Strom für ein
  #endif  

  if(strlen(shelly_addr)>=7) shelly(ein); // shelly via WLAN ein, bei kleiner 7 liegt definitiv keine gültige IP-Adr vor

  currentSwitchState = ein; //merken für HTML-Oberfläche
}

//*****************************************************************************************
void shelly(bool ein) {
  WiFiClient client;
  HTTPClient http;
  char puffer[50];
  //Serial.print("[HTTP] begin...\n");
  strcpy(puffer,"http://");    //ip-Adresse aus Eingabemaske kopieren, 
  strcat(puffer,shelly_addr);  //Bsp: fertiger String: "http://192.168.178.133/relay/0?turn=on"
  strcat(puffer,"/relay/0?turn=");
  if(ein)
    strcat(puffer,"on");
  else 
    strcat(puffer,"off");
  
  if (http.begin(client, puffer)) {  // HTTP
    //Serial.print("[HTTP] GET...\n");
    int httpCode = http.GET();    // start connection and send HTTP header
    if (httpCode > 0) {
      if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY) {
        String payload = http.getString();
        //Serial.println(payload);
      }
    } 
    else {
      //Serial.printf("hier shelly Funktion: [HTTP] GET... failed, error: %s\n", http.errorToString(httpCode).c_str());
    }
    http.end();
  } else {
    //Serial.println("hier shelly Funktion: [HTTP] Unable to connect");
  }
}

//**********************************************
//Alle Parameter aus dem EEPROM lesen
void eeprom_loadAll(){
  EEPROM.begin(512); //start EEPROM emulation in flash of ESP

  //update schreibt jeweils ein Byte, ruft write aber nur auf, wenn sich der Wert geändert hat
  startZeit   = EEPROM.read(0);
  endeZeit    = EEPROM.read(1); 
  modus       = EEPROM.read(2); 
  stundenzahl = EEPROM.read(3); 

  EEPROM.commit();  //end EEPROM emulation
}

//**********************************************
//Alle Parameter in den EEPROM schreiben
void eeprom_saveAll(){
  Serial.println("saveAll");
  //was hat sich geändert?
  EEPROM.begin(512); //start EEPROM emulation in flash of ESP

  //jeweils nur schreiben, wenn sich der Wert geändert hat
  if (EEPROM.read(0) != startZeit){
    EEPROM.write(0,startZeit);
    Serial.println("startZeit->EEPROM");
  }  
  if (EEPROM.read(1) != endeZeit){
    EEPROM.write(1,endeZeit); 
    Serial.println("endeZeit->EEPROM");
  }    
  if (EEPROM.read(2) != modus){
    EEPROM.write(2,modus);
    Serial.println("modus->EEPROM");
  }
  if (EEPROM.read(3) != stundenzahl){
    EEPROM.write(3,stundenzahl); 
    Serial.println("stundenzahl->EEPROM");
  } 

  EEPROM.commit();  //end EEPROM emulation and physically write
}

//**********************************************
//Alle Parameter verzögert in den EEPROM schreiben
//Jede neue Schreib-Anforderung setzt die Verzögerung wieder zurück (=Verzögerung beginnt von neuem)
bool savingRequested = false;
unsigned long millis_savingRequested = 0;
  
void eeprom_requestSaveAll(){
  savingRequested = true;
  millis_savingRequested = millis();
  Serial.println("requestSaveAll");
} 

void eeprom_saveAll_ifRequestedAndDelay(){
  if (savingRequested && (millis() > (millis_savingRequested+EEPROM_DELAY))){
    eeprom_saveAll();
    savingRequested = false;
  }
}
