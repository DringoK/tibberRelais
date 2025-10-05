#pragma once
//ingosserver.h abgeleitet von uwesserver.h (make 5/24 im heise-Verlag)
//Einfacher HTML-Server beantwortet GET / HTTP - Anfragen
//  mit einer Maske
//    Zeitfenster
//    Modus (an/aus/Preis Mittelwert + Max halbe, Mittelwert, Mittelwert + Min Halbe, Preiswerteste Stunden-Anzahl
//  Buttons Daten Senden / Refresh
//  SVG-Grafik: Balkendiagramm mit aktuellen Preisen
//
//Autor: DringoK
//Version: 2025.1005


//Debugging auf Konsole RS232 falls define aktiv (=nicht auskommentiert)
#define DEB_HTML         //HTML-Verkehr in ingosserver.h
//#define DEB_SVG        //SVG-Generierung in ingosserver.h

#include <ESP8266WiFi.h>

#define MAX_PACKAGE_SIZE 2048

extern int Aufruf_Zaehler;
extern int switch_color;
extern String request;   //Anfrage des HTML-Clients (also vom Browser)

//Werte, aus HTML-Maske (und deren Default-Werte in ingosserver.cpp)
extern int startZeit;
extern int endeZeit;
extern int stundenzahl;          //wieviele Stunden aus html-Maske für mode_stundenzahl
enum MODUS {
       mode_ein=0,
       mode_aus=1,
       mode_mittelwert_max=2, //(Mittelwert+preisMax)/2
       mode_mittelwert=3,
       mode_mittelwert_min=4, //(Mittelwert+preisMin)/2
       mode_stundenzahl=5
       };
extern int modus;
extern bool refreshPressed; //ist nach handleWifi_traffic() true, wenn dort Aktualisieren gedrückt wurde, sonst false


//sonstige angezeigte globale Werte
extern String displayVersion;  //wird in main.cpp setup() gesetzt
extern u16_t Reconnect_Zaehler;

//********** Methoden ***********************************************************************************************************

void initIngosServer(); //in main->setup() aufrufen!!

WiFiClient serverAccept(); //der oben definierte client word +ber server.accept ermittelt und wird zurückgegeben
void clientStop(); //Beendet eine bestehende Verbindung des client, der mit serverAccept verbunden wurde

// ** http-Request
bool clientReceiveGETRequest();  //true, wenn es eine GET-Anfrage war
bool interpretRequestValues(const char *req); //true, wenn /X in der Anfrage enthalten war und Werte übernommen wurden

// ** html-Antwort
void clientPrint404NotFound();
void clientPrintHTTPHeader();
void clientPrintHTMLAnwser(int current_index, const char* tm_str);

void clientPrintSVGHeader();
void clientPrintSVGBarChart(int current_index);
void clientPrintSVGFooter();

//** String-Helper
int findEnd(const char *, const char *) ;
int findStart(const char *, const char *) ;
bool contains(const char * such, const char * str);

int pickDec(const char *, int ) ;
int pickZahl(const char*, const char*);

