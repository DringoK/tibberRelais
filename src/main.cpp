//Ingos Tibber-Relais
//
//nach dem Bauvorschlag von Uwe Siebert make 5/24 im heise-Verlag
//ESP8266 schaltet ein Relais nach den Strom-Preisen von tibber
//Unterstützt werden stündliche oder viertelstündliche Strompreise
//
// * Über ingosserver.h wird eine html-Beidienoberfläche bereitgestellt
// * diese zeigt auch die Preise als svg-Grafik
// * Tiberpreise werden über JSON-API bei Tibber aberufen
// * Die aktuelle Zeit wird über time.h und Internetzeit.h ermittelt
//
//Autor: DringoK
//Version: siehe ***** Version ********** unten

#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecureBearSSL.h>
#include <time.h>
//#include "ota.h"
#include "Internetzeit.h"
#include <EEPROM.h>

// ********* Version **************
#define vers "2025.1005 Tibber Relais (Ingo)"
//const char *myHostName = "TibberRelais"; //<<<<<<<<<<<<<<<<<<<<<<<<<<< für Release: Namen ändern <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
const char *myHostName = "TibberTest";
const char *tibberApi = "https://api.tibber.com/v1-beta/gql";

//Debugging auf Konsole RS232 falls define aktiv (=nicht auskommentiert)
#define DEB_CONNECT      //wifi-Connect
#define DEB_MAINLOOP     //alle Aktivitäten in der Main-Loop
#define DEB_SCHALTEN     //Schalt-Vorgänge
#define DEB_PREISE       //Preise nach dem Einlesen von tibber
#define DEB_HTML         //HTML-Verkehr in ingosserver.h
//#define DEB_SVG        //SVG-Generierung in ingosserver.h
//#define DEB_Zeit       //Zeit zyklisch ausgeben als Lebenszeichen, nur dann wird GET_TIME_INTERVAL verwendet
#define GET_TIME_INTERVAL 10 //Zeit-Ausgabe-Intervall in Sekunden (unabhängig davon holt time.h die Zeit nur alle 60min aus vom NTP-Server)

//**********Shelly-Steckdose unterstützen
//const char* shelly_addr="192.168.178.126";  //hier Adresse der Shelly-Steckdose eintragen (aus html-Maske entfernt, alle anderen ip-Adressen sind ja auch hier im Code)
const char* shelly_addr="";  //wenn leer, dann Shelly-Steckdose nicht verwenden

//****** WLAN / Tibber-Server - Zugriff ***************
//const char *ssid = "mySSID";         //--> replace with your own ssid and put it in a file credentials.h
//const char *password = "myPassword"; //--> replace with your own wifi password and put it in a file credentials.h
// API Tibber, Demo Token --> replace with your own token and put it in a file credentials.h
//const char *token = "Bearer 3A77EECF61BD445F47241A5A36202185C35AF3AF58609E19B53F3A8872AD7BE1-1"; //das Wort "Bearer " muss vor den eigentlichen Token
#include "credentials.h" //here are Wifi Access credentials and tibber token stored, but not shared in GIT --> built your own credentials.h with just 3 lines like above with your credentials



//********* Wifi-Client
bool connectToBestAccessPoint();    //wifiscan, um die richtige Fritzbox mit "SSID" zu finden, gibt true zurück, falls scan erfolgreich
void connectWifi();                 //Verbinde mit Wifi als Client
u16_t Reconnect_Zaehler = 0;

//********* Tibber Connection
WiFiClientSecure client_sec;  
HTTPClient https;

//************* Tibber-Preise
const int STUNDE_PREISE_NEU = 13;    //um ca. 13 Uhr gibt es die neuen Preise bei Tibber, ab da wird jede Minute versucht, sie zu bekommen, bis es geklappt hat
#define PREISE_GET_RETRY_DELAY 10*60  //zwischen jedem Versuch ...Sekunden Pause

bool morgenGelesen=false;            //true, sobald "morgen" erfolgreich in preise_aus_jsonDOC() gelesen wurde. Wird in tagesWechsel() zurückgesetzt und in preise_aus_jsonDOC() versucht wieder zu setzen

//Preise-Statistik: 3 Tage werden ausgewertet: gestern / heute / morgen, aber nur 2 werden angezeigt wie in der Tibber-App
//"gestern" steht erst zur Verfügung, sobald das erste Mal um Mitternacht alle Werte umkopiert werden (siehe tagesWechsel() )

#define TAG_ANZ 3
enum TAG_Index {  //Index im preis-Array
       gestern = 0,
       heute = 1,
       morgen = 2
};

//Preise jeweils für den aktuellen und den folgenden Tag bzw. für den aktuellen und gestern
int preis[TAG_ANZ][24] ;    //Preise aus Json-Datei von Tibber
int preisSort[TAG_ANZ][24]; //Preise in aufsteigender Reihenfolge
int preisSortAnzahl=0;      //Anzahl der gültigen Einträge in PreisSort (an allen Tagen gleich viele) = Anzahl Stunden im Zeitfenster

int preisMittel[TAG_ANZ];   //Mittlerer Preis des jeweiligen Tages.
int preisMin[TAG_ANZ];      //Minimaler Preis des jeweiligen Tages
int preisMax[TAG_ANZ];      //Maximaler Preis des jeweiligen Tages

//********* Schalten
bool currentSwitchState = false; //aktueller = zuletzt geschalteter Schaltwert für die Bestimmung der Hintergrundfarbe in ingosserver.h
#define OUTPIN_INVERTED     //wenn definiert, dann wird der Schaltausgang invertiert, also ein=LOW, aus=HIGH

ADC_MODE(ADC_VCC);
#define relay 16  //D0
#define red 13    //D7
#define green 15  //D8
const uint16 LED_AN = 5 * 1023/100; //5% PWM im Dauerbetrieb. Vorher, beim Start werden die LEDs digital=voll angesteuert.


//************************** forward declarations
void preiseAnalysieren();
void bubbleSort(int arr[], int n); //aufsteigend sortieren eines Arrays

bool istInZeitfenster(int h); //liegt h im Zeitfenster aus der html-Maske? Da sich das Zeitfenster täglich wiederholt, ist der Tag egal

bool getSchaltWert(int tag, int stunde);  //Ermittelt den Schalt-Wert für einen übergebenen Zeitpunkt: false=ausschalten, true=einschalten
void preisabhaengig_schalten();  //schalte für die aktuelle Uhrzeit am aktuellen Tag
int h_schalt = 0; //Stunde, an der zuletzt geschaltet wurde (um festzustellen, wann sie um ist und wieder geschaltet werden muss), siehe loop()

void schalten (bool ein);  //schalte LEDs, Relais und Shelly Steckdose
void shelly(bool ein);     //shelly Steckdose schalten

void preiseRuecksetzen();  //Initalisiern der preise[][]
void hole_tibber_preise(); //Json-Dokument als string von tibber holen: aufgerufen direkt nach dem Start und wenn neue Preise zur Verfügung stehen
bool preise_aus_jsonStr(const char* jsonStr); //Json-Dokument in preise[] übernehmen. Return true, wenn für "morgen" Werte geliefert wurden

void eeprom_loadAll();     //alle gespeicherten Parameter von html-Maske aus EEPROM laden (während setup())
void eeprom_requestSaveAll(); //verzögertes Schreiben aller Parameter aus html-Maske initiieren. Jeder Aufruf während delay setzt Wartezeit zurück (=erneut Warten=weiter Änderungen sammeln)
void eeprom_saveAll_ifRequestedAndDelay(); //in loop() regelmäßig aufrufen: prüfen ob Delay abgelaufen ist und dann speichern, sonst nichts tun
#define EEPROM_DELAY 20*1000 //in ms solange werden Änderungen vor dem Schreiben gesammelt

void tagesWechsel();       //Aufruf einmal um Mitternacht: preis[gestern]=preis[heute], preis[heute]=preis[morgen], preis[morgen]=0, 
bool tageswechselDurchgefuehrt=false;         //true, wenn der Tageswechsel bereits durchgeführt wurde.


//************* forward declarations visible also for ingosserver.h also, daher erst hier #inlude
#include "ingosserver.h"       // erledigt den Webseiteaufbau und Abfrage



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
  
  WiFi.mode(WIFI_STA);  //station
  WiFi.hostname(myHostName);
  connectWifi();              //erster Connect hier im Setup (dann für Reconnect zyklisch im loop falls not WL_CONNECTED)
    
    //Init Web-Server
  server.begin();

  //Werte der Eingabemaske aus EEPROM holen
  eeprom_loadAll();
  
  //initialisiere Wifi-Objects
  client_sec.setInsecure(); //the magic line, use with caution

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

    //eeprom_requestSaveAll(); !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!! Diese Zeile wieder aktivieren vor Release!!!!!!!!!!!!!!!!!!!!!!!!!!

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

//*****************************************************************************
//wird aufgerufen wenn neue Preise von Tibber geholt wurden
//pro Tag wiederholen sich die Schaltzeiten unabhängig davon, ob die Schaltzeit über Mitternacht geht!
//daher werden pro Tag preisMin,preisMittel und preisMax für alle zu berücksichtigenden Stunden gebildet
//Bemerkung: wenn laut html-Maske nur in einer Stunde h geschaltet werden soll ist preisMin=preisMittel=preisMax=preis[tag][h]
//Berechnet werden hier:
// preisSort[] enthält alle zu berücksichtigenden Werte
// PreisSortAnzahl: Anzahl Preise im Sort-Array = Zahl der Stunden, die es pro Tag zu berücksichtigen gilt (an allen Tagen gleich, daher nur ein Wert)
// preisMin[]: Minimaler Preis des jeweiligen Tages
// preisMittel[]: Mittelwert des Preises des jeweiligen Tages
// preisMax[]: Maximaler Preis des jeweiligen Tages
// preisSort[] wird sortiert
void preiseAnalysieren(){
  //--- alle Tage durchgehen
  for (int day=gestern; day<=morgen; day++){
    preisSortAnzahl=0;  //wird zwei mal gleich berechnet
    long preisSumme=0;  //zur Berechnung des Mittelwerts für den Tag in diesem Durchlauf
    preisMin[day] = INT_MAX;
    preisMax[day] = INT_MIN;

    //--- alle Stunden des Tags durchgehen
    for (int hour=0; hour <24; hour++){
      if (istInZeitfenster(hour)){
        //in Sort-Array übernehmen
        preisSort[day][preisSortAnzahl] = preis[day][hour];
        preisSortAnzahl++;

        //Statistik
        preisMin[day] = min (preisMin[day], preis[day][hour]);
        preisMax[day] = max (preisMax[day], preis[day][hour]);
        preisSumme = preisSumme + preis[day][hour];
      }
    }  //for hour

    preisMittel[day] = preisSumme / preisSortAnzahl;
    bubbleSort(preisSort[day], preisSortAnzahl);
  }//for day

  #ifdef DEB_PREISE
    Serial.printf("SortAnzahl=%d, preisMin[0]=%d, preisMittel[0]=%d, preisMax[0]=%d, preisMin[1]=%d, preisMittel[1]=%d, preisMax[1]=%d\n",preisSortAnzahl, preisMin[0], preisMittel[0], preisMax[0], preisMin[1], preisMittel[1], preisMax[1]);
  #endif  
} //preiseAnalysieren

//*****************************************************************************
//Aufruf einmal um Mitternacht: preis[gestern]=preis[heute], preis[heute]=preis[morgen], preis[morgen]=0
//Neue Statistik: preiseAnalysieren() wird am Ende aufgerufen nötig, da das alte "gestern" rausgefallen ist
void tagesWechsel(){
  for (int h=0; h<24; h++){
    preis[gestern][h]= preis[heute][h];
    preis[heute][h]  = preis[morgen][h];
    preis[morgen][h] = 0; //neue Preise erst wieder ab STUNDE_PREISE_NEU
  }
  morgenGelesen=false; //um STUNDE_PREISE_NEU wieder neue Preise holen und bis dahin gestern/heute anzeigen

  preiseAnalysieren();
}

//*****************************************************************************
//Werte a und b vertauschen
void swap(int &a, int &b) {
  int c = a;
  a = b;
  b = c;
}

//*****************************************************************************
//Die ersten n Werte des Arrays arr[] aufsteigend sortieren
//@param arr[] das zu sortierende Array
//@param n     Anzahl der zu berücksichtigenden Elemente aus dem Array
void bubbleSort(int arr[], int n) { //aufsteigend sortieren eines Arrays
  for (int i = 0; i < n - 1; i++)  // das Letzte i Element ist bereits am Platz
     for (int j = 0; j < n - i - 1; j++)
      if (arr[j] > arr[j + 1])
        swap(arr[j], arr[j + 1]);
}

//*****************************************************************************
//@param h Stunde 0..23, tag ist egal, da es sich täglich wiederholt
//@returns true, falls innerhalb der angegebenen Schaltzeit, sonst false 
bool istInZeitfenster(int h){
  if (startZeit < endeZeit) { //Schaltzeit an einem Tag
    return (h>=startZeit && h<=endeZeit); 
  }else{ //Schaltzeit über Nacht
    return (h>=0 && h<=endeZeit) || (h>=startZeit && h<=23);
  }
}

//*****************************************************************************
//Schalte Shelly und Relais entsprechend dem getSchaltWert() für die aktuelle Stunde schalten
void preisabhaengig_schalten(){
  schalten(getSchaltWert(heute, tm.tm_hour));
}

//*****************************************************************************
//Ermittelt den Schalt-Wert für einen übergebenen Zeitpunkt:

//@param tag = 0 = vor STUNDE_PREISE_NEU = gestern, nach STUNDE_PREISE_NEU = heute, Stunde = 0..23
//@param tag = 1 = vor STUNDE_PREISE_NEU = heute, nach STUNDE_PREISE_NEU = morgen, Stunde = 0..23
//@param stunde = 0..23

//return: true = hier muss eingeschaltet werden, false = hier muss ausgeschaltet werden
bool getSchaltWert(int tag, int stunde){
  if (!istInZeitfenster(stunde))  //Tag egal, da sich das Schaltprogramm täglich wiederholt
    return false;
    
  switch (modus) {    
    case mode_mittelwert_min:
      return preis[tag][stunde] <= ((preisMittel[tag]+preisMin[tag]) / 2 );
    break;

    case mode_mittelwert:
      return preis[tag][stunde] <= preisMittel[tag];
    break;

    case mode_mittelwert_max:
      return preis[tag][stunde] <= ((preisMittel[tag]+preisMax[tag]) / 2 );
    break;
    
    case mode_stundenzahl:
      return preis[tag][stunde] <= preisSort[tag][stundenzahl-1];
    break;

    case mode_ein:
      return true;
    break;

    case mode_aus:
      return false;
    break;    

    default:
      return true;
    break;
  }
} // schaltWert Ende

//*****************************************************************************************
// Initalisiern der preise[][]
void preiseRuecksetzen(){
  for (int day=gestern; day<=morgen; day++){
    for (int hour=0; hour <24; hour++){
      preis[day][hour]=0;
    }
  }
} 

//*****************************************************************************************
// Json-String von tibber holen und mit preise_aus_jsonStr() in preise[] übernehmen
// aufgerufen direkt nach dem Start und wenn neue Preise zur Verfügung stehen
void hole_tibber_preise() { 
  //strcpy(puffer, "Bearer ");
  //strcat(puffer, token);
  https.begin(client_sec, tibberApi);
  https.addHeader("Content-Type", "application/json");  // add necessary headers
  https.addHeader("Authorization",  token);             // add necessary headers

  static const char *anfrage = "{\"query\": \"{viewer { homes { currentSubscription{ priceInfo (resolution: HOURLY){ today{ total  } tomorrow { total  }}}}}}\"}";
  int httpCode = https.POST(anfrage);
  if (httpCode == HTTP_CODE_OK) {
    String response = https.getString();
    Serial.printf("hole_tibber_preise()-getString Free Heap %d (soll >3500)...\n", ESP.getFreeHeap());
    morgenGelesen=preise_aus_jsonStr(response.c_str()); //wenn alles in Ordnung, dann in preise[][] übernehmen
  } else {
    #ifdef DEB_PREISE
      Serial.print("hole_tibber_preise-Fehler: ");
      Serial.println(httpCode);
    #endif  
  }
  https.end();
  client_sec.stop();  // Disconnect from the server
}


//*****************************************************************************************
// Nachdem hole_tibber_preise den json-String geholt hat wird er hier geparsed und in preise[] übernommen.
// Aus Speicherplatzgründen verwende ich nicht mehr JsonDoc. Außerdem ist das Parsen der Antwort sehr einfach.
// Das erste "homes" wird verwendet, es sollte also mit einem Vertrag gekoppelt sein, damit die Preise geholt werden können.
//
// Es wird davon ausgegangen, dass die Json-Antwort von tibber genau so aussieht.
// {"data":{"viewer":{"homes":[{"currentSubscription":{"priceInfo":{"today":[{"total": 0.3593},{"total":0.3507}, ..... ,{"total":0.3562}],"tomorrow":[....]}}}]}}}
//
// falls das nicht so ist, wird so lange wie möglich gelesen und dann abgebrochen. Im schlimmsten Fall (z.B. leerer String) passiert also gar nichts
//
// @param jsonStr der rohe String der Json-Antwort von tibber
// @Return true, wenn für "morgen" Werte enthalten waren
bool preise_aus_jsonStr(const char* jsonStr){
  double preis_in_double;

  char* cursor;       //aktuelle Position beim parsen
  char* endeCursor;   //"weitersuchen"=Endeposition eines Bereichs
  //"today": suchen
  cursor = strstr(jsonStr, "today");
  if (cursor==0){ //not found
    Serial.println("Ungültige JSON-Antwort: \"today:\" nicht gefunden");
    return false;
  } 
  cursor = strstr(cursor, ":"); //Doppelpunkt zu today
  if (cursor==0) return false; //not found
  cursor++; //hinter den Doppelpunkt stellen

  //24x Doppelpunkt (von "total") bis geschweifte Klammer zu } suchen
  int h=0;
  while (h<24){
    cursor = strstr(cursor, ":"); //Doppelpunkt zu total
    if (cursor==0) return false; //not found

    cursor++; //hinter den : stellen

    endeCursor= strstr(cursor, "}");
    if (endeCursor==0) return false; //not found
    *endeCursor = char(0); //zum parsen hier String-Ende setzen

    preis_in_double = atof(cursor);
    preis[heute][h] = int (10000*preis_in_double);
    #ifdef DEB_PREISE
      Serial.printf("heute[%d]=%d\n", h, preis[heute][h]);
    #endif  
    h++;
    cursor=endeCursor+1;  //nach der geschweiften Klammer (durch #0 ersetzt) weiter, bei String-Ende wäre das #0= Rest ist Leerstring, aber gültig
  }

  //"tomorrow": suchen
  cursor = strstr(cursor, "tomorrow");
  if (cursor==0) return false; //not found
  cursor = strstr(cursor, ":"); //Doppelpunkt zu tomorrow
  if (cursor==0) return false; //not found
  cursor++; //hinter den Doppelpunkt stellen

  //24x Doppelpunkt (von "total") bis geschweifte Klammer zu } suchen
  //falls keine Preise für tomorrow, wird gleich der erste : nicht gefunden und false zurückgegeben
  //die evtl. vorhandenen Preise sind damit unverändert aber durch Rückgabe von false ungültig markiert
  h=0;
  while (h<24){
    cursor = strstr(cursor, ":"); //Doppelpunkt zu total
    if (cursor==0) return false; //not found

    cursor++; //hinter den : stellen

    endeCursor= strstr(cursor, "}");
    if (endeCursor==0) return false; //not found
    *endeCursor = char(0); //zum parsen hier String-Ende setzen

    preis_in_double = atof(cursor);
    preis[morgen][h] = int (10000*preis_in_double);
    #ifdef DEB_PREISE
      Serial.printf("morgen[%d]=%d\n", h, preis[morgen][h]);
    #endif  
    h++;
    cursor=endeCursor+1;  //nach der geschweiften Klammer (durch #0 ersetzt) weiter, falls im worst case String-Ende, wäre das #0 (= Rest ist Leerstring), aber auch dann gültig
  }

  return true; //wenn bis hierher gekommen, dann sind auch die Preise "tomorrow" gültig
}//preise_aus_jsonStr

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
