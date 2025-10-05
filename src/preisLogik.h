//preisLogik.h abgeleitet von tibberRelais aus make 5/24 im heise-Verlag
//
//Hier befinden sich alle Logiken rund um die Tibber-Preise
//
//Autor: DringoK
//Version: 2025.1005


//Debugging auf Konsole RS232 falls define aktiv (=nicht auskommentiert)
#define DEB_PREISE       //Preise nach dem Einlesen von tibber


//********* Tibber Connection
#define TIBBER_API "https://api.tibber.com/v1-beta/gql";

//************* Tibber-Preise
const int STUNDE_PREISE_NEU = 13;    //um ca. 13 Uhr gibt es die neuen Preise bei Tibber, ab da wird jede Minute versucht, sie zu bekommen, bis es geklappt hat
#define PREISE_GET_RETRY_DELAY 10*60  //zwischen jedem Versuch ...Sekunden Pause

extern bool morgenGelesen;            //true, sobald "morgen" erfolgreich in preise_aus_jsonDOC() gelesen wurde. Wird in tagesWechsel() zurückgesetzt und in preise_aus_jsonDOC() versucht wieder zu setzen

//Preise-Statistik: 3 Tage werden ausgewertet: gestern / heute / morgen, aber nur 2 werden angezeigt wie in der Tibber-App
//"gestern" steht erst zur Verfügung, sobald das erste Mal um Mitternacht alle Werte umkopiert werden (siehe tagesWechsel() )

#define TAG_ANZ 3
enum TAG_Index {  //Index im preis-Array
       gestern = 0,
       heute = 1,
       morgen = 2
};

//Preise jeweils für den aktuellen und den folgenden Tag bzw. für den aktuellen und gestern
extern int preis[TAG_ANZ][24];    //Preise aus Json-Datei von Tibber
extern int preisSort[TAG_ANZ][24]; //Preise in aufsteigender Reihenfolge
extern int preisSortAnzahl;      //Anzahl der gültigen Einträge in PreisSort (an allen Tagen gleich viele) = Anzahl Stunden im Zeitfenster

extern int preisMittel[TAG_ANZ];   //Mittlerer Preis des jeweiligen Tages.
extern int preisMin[TAG_ANZ];      //Minimaler Preis des jeweiligen Tages
extern int preisMax[TAG_ANZ];      //Maximaler Preis des jeweiligen Tages

extern bool tageswechselDurchgefuehrt;         //true, wenn der Tageswechsel bereits durchgeführt wurde.

extern int h_schalt; //Stunde, an der zuletzt geschaltet wurde (um festzustellen, wann sie um ist und wieder geschaltet werden muss), siehe loop()
extern bool currentSwitchState; //aktueller = zuletzt geschalteter Schaltwert für die Bestimmung der Hintergrundfarbe in ingosserver.h

//********** Methoden ***********************************************************************************************************
void initPreisLogik();
void preiseAnalysieren();
void tagesWechsel();       //Aufruf einmal um Mitternacht: preis[gestern]=preis[heute], preis[heute]=preis[morgen], preis[morgen]=0, 

void bubbleSort(int arr[], int n); //aufsteigend sortieren eines Arrays
bool istInZeitfenster(int h); //liegt h im Zeitfenster aus der html-Maske? Da sich das Zeitfenster täglich wiederholt, ist der Tag egal

bool getSchaltWert(int tag, int stunde);  //Ermittelt den Schalt-Wert für einen übergebenen Zeitpunkt: false=ausschalten, true=einschalten

void preiseRuecksetzen();  //Initalisiern der preise[][]
void hole_tibber_preise(); //Json-Dokument als string von tibber holen: aufgerufen direkt nach dem Start und wenn neue Preise zur Verfügung stehen
bool preise_aus_jsonStr(const char* jsonStr); //Json-Dokument in preise[] übernehmen. Return true, wenn für "morgen" Werte geliefert wurden


