#include "preisLogik.h"
#include "ingosserver.h"  //für alle Werte aus der HTML-Maske, z.B. startZeit, endeZeit, ...
#include "credentials.h"  //für den Tibber-Token

#include <WiFiClientSecureBearSSL.h>
#include <ESP8266HTTPClient.h>
WiFiClientSecure client_sec;  
HTTPClient https;

//**************** Definition der Variablen aus der .h-Datei
bool morgenGelesen=false;            //true, sobald "morgen" erfolgreich in preise_aus_jsonDOC() gelesen wurde. Wird in tagesWechsel() zurückgesetzt und in preise_aus_jsonDOC() versucht wieder zu setzen

//Preise jeweils für den aktuellen und den folgenden Tag bzw. für den aktuellen und gestern
int preis[TAG_ANZ][24];    //Preise aus Json-Datei von Tibber
int preisSort[TAG_ANZ][24]; //Preise in aufsteigender Reihenfolge
int preisSortAnzahl=0;      //Anzahl der gültigen Einträge in PreisSort (an allen Tagen gleich viele) = Anzahl Stunden im Zeitfenster

int preisMittel[TAG_ANZ];   //Mittlerer Preis des jeweiligen Tages.
int preisMin[TAG_ANZ];      //Minimaler Preis des jeweiligen Tages
int preisMax[TAG_ANZ];      //Maximaler Preis des jeweiligen Tages

bool tageswechselDurchgefuehrt=false;         //true, wenn der Tageswechsel bereits durchgeführt wurde.

int h_schalt = 0; //Stunde, an der zuletzt geschaltet wurde (um festzustellen, wann sie um ist und wieder geschaltet werden muss), siehe loop()
bool currentSwitchState = false; //aktueller = zuletzt geschalteter Schaltwert für die Bestimmung der Hintergrundfarbe in ingosserver.h


//***** lokale Variablen
const char *token = MY_TOKEN; // API Tibber, Ingos Token. Das Wort "Bearer " muss vor den eigentlichen Token
const char *tibberApi = TIBBER_API;

//******************* Aufrufen in main->setup()
void initPreisLogik(){
  //initialisiere Wifi-Objects
  client_sec.setInsecure(); //prüft die Identität des Servers nicht, damit ohne Fingerprinting aber trotzdem https :-)
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
