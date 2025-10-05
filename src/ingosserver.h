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

WiFiServer server(80);
WiFiClient client;
#define MAX_PACKAGE_SIZE 2048
char HTTP_Header[110];
int Aufruf_Zaehler = 0, switch_color=0;

//Werte, aus HTML-Maske und deren Default-Werte
int startZeit=0, endeZeit=23;  //default-Werte für Zeitfenster aus der Maske
int stundenzahl = 15;          //wieviele Stunden aus html-Maske für mode_stundenzahl
enum MODUS {
       mode_ein=0,
       mode_aus=1,
       mode_mittelwert_max=2, //(Mittelwert+preisMax)/2
       mode_mittelwert=3,
       mode_mittelwert_min=4, //(Mittelwert+preisMin)/2
       mode_stundenzahl=5
       };
int modus = mode_mittelwert;
bool refreshPressed=false; //ist nach handleWifi_traffic() true, wenn dort Aktualisieren gedrückt wurde, sonst false

//*********************************************************************************************************************
void connectWifi();

bool handleWifiTraffic(); //pollling des servers. Liefert true, wenn eine Server-Anfrage beantwortet wurde (=ein client erzeugt wurde)

bool clientReceiveGETRequest();  //true, wenn es eine GET-Anfrage war
bool interpretRequestValues(const char *req); //true, wenn /X in der Anfrage enthalten war und Werte übernommen wurden

void clientPrintHTTPHeader();
void clientPrintHTMLAnwser();

void clientPrintSVGHeader();
void clientPrintSVGBarChart();
void clientPrintSVGFooter();

int findEnd(const char *, const char *) ;
int findStart(const char *, const char *) ;
bool contains(const char * such, const char * str);

int pickDec(const char *, int ) ;
int pickZahl(const char*, const char*);

//*****************************************************************************************************************************
//pollling des servers. Liefert true, wenn eine Server-Anfrage beantwortet wurde (=ein client erzeugt wurde)
bool handleWifiTraffic() {
  client = server.accept();   //Gab es einen Server-Aufruf?

  if (client) {               // Bei einem Aufruf des Servers
    if (clientReceiveGETRequest()){   //nur antworten, wenn es ein GET-Request war
      //Antwort senden
      clientPrintHTTPHeader(); 
      clientPrintHTMLAnwser();  //Antwortseite aufbauen
    }else{
      client.print("HTTP/1.1 404 Not Found\r\n\r\n");
    }  

    // Die Verbindung beenden
    client.stop();
    #ifdef DEB_HTML
      Serial.println("Disconnected: Client.Stop");
    #endif

    return true;
  }else{
    return false;
  }

}

//********************************************************************************************************************
bool clientReceiveGETRequest(){
  String request;   //Anfrage des HTML-Clients (Browsers)
  bool isGETRequest = false;  //bisher noch kein GET empfangen

  bool emptyLine = false;     //leere Zeile empfangen?
  char c = 0;                 //empfangenes Zeichen des Aufrufs

  while (client.connected() && client.available()) {           // Loop, solange Client verbunden bis Leerzeile
    c = client.read();                  // Ein (1) Zeichen der Anfrage des Clients lesen (bei available()==false würde 255 kommen)

    #ifdef DEB_HTML
      Serial.write(c);                  // gelesenes auf Serial ausgeben
    #endif  
    request += c;

    if (c == '\n') {                    // eine Zeile des Requests abgeschlossen?
      if (emptyLine) break;             //falls leere Zeile empfangen, dann Request-Ende erreicht
      else emptyLine = true;            //neue Zeile ist neue Chance auf Leerzeile
    }else{
      emptyLine = false;               //jedes andere Zeichen bewirkt, dass es keine Leerzeile war
    }  
  } 

  client.flush();
  delay(30);

  const char* requestChr = request.c_str();

  isGETRequest = contains("GET", requestChr);

  #ifdef DEB_HTML
    Serial.printf("request=[%s]\n", requestChr);                    // gelesenes auf Serial ausgeben
    Serial.printf("isGetRequest=%d\n", isGETRequest);
  #endif  

  if (interpretRequestValues(requestChr)){
    preisabhaengig_schalten(); //und gleich auf den neuen Modus reagieren, auf jeden Fall vor clientPrintHTMLAnwser!
  }  

  return isGETRequest;
}

//********************************************************************************************************************
bool interpretRequestValues(const char *req){ //true, wenn /X in der Anfrage enthalten war und Werte übernommen wurden
  refreshPressed=false;
    if (contains("r=R", req)) { //Refresh wurde gedrückt
      refreshPressed=true;
      #ifdef DEB_HTML
        Serial.println("REFRESH");
      #endif  
    }

  if (contains("/X", req) ){    //falls im GET-Request Werte aus der Maske übermittelt wurden
    startZeit = pickZahl("s=", req);         //Startzeit: Benutzereingaben einlesen und verarbeiten
    endeZeit = pickZahl("e=", req);          //Endezeit: Benutzereingaben einlesen und verarbeiten
    modus = pickZahl("m=", req);             //Mode: Benutzereingaben einlesen und verarbeiten
    stundenzahl = pickZahl("h=", req);       //Stundenzahl: Benutzereingaben einlesen und verarbeiten

    return true;
  }else{
    return false;
  }  
    

}

//********************************************************************************************************************
void clientPrintHTTPHeader(){
    // Der Server sendet nun eine Antwort an den Client
  client.println("HTTP/1.1 200 OK");
  client.println("Content-type:text/html");
  client.println("Connection: close");
  client.println();
}

//*********************************************************************************************************************
//die html-Antwort für das Webserver-Interface zusammenbauen und auf client ausgeben (client muss über server.accept verbunden sein und client.available() muss true sein)
//
String backColor;

void clientPrintHTMLAnwser() {
  // Die Webseite anzeigen
  //client.println("<!DOCTYPE html><html>");
  //client.println("<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">");
  //client.println("<link rel=\"icon\" href=\"data:,\"></head>");
  //client.println("<body><h1 align=\"center\">Hier spricht dein neuer Server! :)</h1>");
  static boolean farbe=LOW;
  client.print  ("<!DOCTYPE html>");
  client.println("<html lang=\"de\">");
  client.println("<meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">"); //für Schriftgroesse Smartphone
  client.println("<head><title>Tibber-Relais></title></head>");

  if(farbe==LOW) //Farbe hin und her schalten, als Quittung fürs Absenden
    if (currentSwitchState)
      backColor = "#d6f7ab";    //grün-gelb
    else  
      backColor = "#fc9a8d";    //rot-gelb
  else 
    if (currentSwitchState)
      backColor = "#abf7c9";    //grün-blau
    else  
      backColor = "#fc8da7";    //rot-blau
  farbe=!farbe;
  client.printf("<body style=\"background-color:%s; font-family:verdana\">\r\n", backColor.c_str());
  client.println("<h2>Tibber Relais</h2>");
  
//**************************** HTML-Formular - Tabellen *******************
  client.println("<form action=\"/X\">");

//Tabelle Zeitfenster von bis
  client.println("<table>");
  client.println( "<tr>");
  client.println(   "<td><label for=\"s\"> Zeitfenster (0 ... 23) von : </label></td>");
  client.print  (   "<td><input type=\"number\" style= \"width:40px\" id=\"s\" name=\"s\" min=\"0\" max=\"23\" value=\"");
  client.print  (startZeit);
  client.println("\"></td>");
  client.println(   "<td><label for=\"e\">&nbsp;&nbsp;  bis: </label></td>"); //&nbsp Leerzeichen
  client.print  (   "<td><input type=\"number\" style= \"width:40px\" id=\"e\" name=\"e\" min=\"0\" max=\"23\" value=\"");
  client.print  (endeZeit);
  client.println("\"></td>");
  client.println("</tr>");
  client.println("</table>");

//Tabelle Sperre aufheben : Modus (Radiobuttons)
  client.println("<br>Sperre aufheben (Heizen), wenn billiger als: ");
  client.println("<table>");

  Serial.printf("Modus=%d", modus);

  client.println("<tr>");
  client.printf("<td><input type=\"radio\" name=\"m\" id=\"%d\" value=\"%d\"", mode_ein, mode_ein);
  if(modus==mode_ein) client.print(" CHECKED");
  client.println("></td>");
  client.printf("<td><label for=\"%d\"> an </label><br></td>\r\n", mode_ein);
  client.println("</tr>");

  client.println("<tr>");
  client.printf("<td><input type=\"radio\" name=\"m\" id=\"%d\" value=\"%d\"", mode_aus, mode_aus);
  if(modus==mode_aus) client.print(" CHECKED");
  client.println("></td>");
  client.printf("<td><label for=\"%d\"> aus </label><br></td>", mode_aus);
  client.println("</tr>");

  client.println("<tr>");
  client.printf("<td><input type=\"radio\" name=\"m\" id=\"%d\" value=\"%d\"", mode_mittelwert_max, mode_mittelwert_max);
  if(modus==mode_mittelwert_max) client.print(" CHECKED");
  client.println("></td>");
  client.printf("<td><label for=\"%d\"> (Preis Mittelwert + Max)/2 </label><br></td>\r\n", mode_mittelwert_max);
  client.println("</tr>");

  client.println("<tr>");
  client.printf("<td><input type=\"radio\" name=\"m\" id=\"%d\" value=\"%d\"", mode_mittelwert, mode_mittelwert);
  if(modus==mode_mittelwert) client.print(" CHECKED");
  client.println("></td>");
  client.printf("<td><label for=\"%d\"> Preis Mittelwert</label><br></td>\r\n", mode_mittelwert);
  client.println("</tr>");

  client.println("<tr>");
  client.printf("<td><input type=\"radio\" name=\"m\" id=\"%d\" value=\"%d\"", mode_mittelwert_min, mode_mittelwert_min);
  if(modus==mode_mittelwert_min) client.print(" CHECKED");
  client.println("></td>");
  client.printf("<td><label for=\"%d\"> (Preis Mittelwert + Min)/2 </label></td>\r\n", mode_mittelwert_min);
  client.println("</tr>");
  
  client.println("<tr>");
  client.printf("<td><input type=\"radio\" name=\"m\" id=\"%d\" value=\"%d\"", mode_stundenzahl, mode_stundenzahl);
  if(modus==mode_stundenzahl) client.print(" CHECKED");
  client.println("></td>");
  client.printf("<td><label for=\"%d\"> Preiswerteste Stunden (Anzahl): </label></td>\r\n", mode_stundenzahl);
  client.print ("<td><input type=\"number\" style= \"width:40px\" id=\"h\" name=\"h\" min=\"1\" max=\"23\"value=\"");
  if(stundenzahl>0) client.print(stundenzahl);
  client.println("\"></td>");
  client.println("</tr>");

  client.println("</table>");

//Tabelle mit Senden / Aktualisieren Buttons
  client.println("<table><tr>");
  client.println(   "<td><input type=\"submit\"></td>");
  client.println(   "<td><button style= \"width:130px\" name=\"r\" value=\"R\">Preise aktualisieren</button></td>"); 
  client.println( "</tr></table>")
  ;
  client.println("</form>");


//******************** Preise-Balkendiagramm als SVG ***********************
  clientPrintSVGHeader();
  clientPrintSVGBarChart();
  clientPrintSVGFooter();

//*********************** graue Statuszeilee *******************************
  client.printf("<br><p style=\"font-size:11px;color:gray\"> Version: %s, RSSI: %d, %s %02d.%02d.%04d %02d:%02d:%02d, recon=%d, count=%d\r\n",
                                 vers, wifi_station_get_rssi(), wochentag[tm.tm_wday],tm.tm_mday,tm.tm_mon,tm.tm_year,tm.tm_hour,tm.tm_min,tm.tm_sec, Reconnect_Zaehler, Aufruf_Zaehler++);
//*********************** HTML Ende **************************
  client.println("</body>");
  client.println("</html>"); 


  // Die Antwort mit einer Leerzeile beenden
  client.println();
} // Ende clientPrintHTMLAnwser

void clientPrintSVGHeader(){
  client.println("<svg xmlns=\"http://www.w3.org/2000/svg\" xml:space=\"preserve\" width=\"125mm\" height=\"45mm\" style=\"shape-rendering:geometricPrecision; text-rendering:geometricPrecision; image-rendering:optimizeQuality; fill-rule:evenodd; clip-rule:evenodd\"");
  client.println("viewBox=\"0 0 510 115\">");
  client.println("<defs>"); //Definitionen für SVG als Bibliothek
  client.println( "<style type=\"text/css\">");
  client.println( "<![CDATA[");
  client.println(   "@font-face{font-family:\"Verdana\";font-variant:normal;font-style:normal;font-weight:normal;src:url(\"#FontID0\") format(svg)}");

  client.println(   ".sGnN {stroke:#006633;stroke-width:1}"); //stroke Green Normal
  client.println(   ".sGnB {stroke:#006633;stroke-width:3}"); //stroke Green Bold
  client.println(   ".sRdN {stroke:#CC3300;stroke-width:1}"); //stroke Red Normal
  client.println(   ".sRdB {stroke:#CC3300;stroke-width:3}"); //stroke Red Bold
  client.println(   ".sGrN {stroke:gray;stroke-width:0.5}");  //stroke Gray Normal
  client.println(   ".sGrB {stroke:gray;stroke-width:2}");    //stroke Gray Bold
  client.println(   ".sBlB {stroke:blue;stroke-width:3}");    //stroke Blue Bold
  client.println(   ".fiNo {fill:none}");     //fill None
  client.println(   ".fiGn {fill:#33CC66}");  //fill Green
  client.println(   ".fiRd {fill:#FF6633}");  //fill Red
  client.println(   ".fiGr {fill:gray}");     //fill Gray
  client.println(   ".fnt {font-size:11px}");
  client.println("]]>");
  client.println("</style>");
  client.println("</defs>");
} //end clientPrintSVGHeader  


static const int SB=32;          //Schriftbreite linker Rand
static const int WIDTH=7;        //Breite pro Balken
static const int WIDTH_BALKEN=5; //Breite des eigentlichen Balkens

static const int MAX_HOEHE = 115;       //Höhe der Grafik
static const int ZF_POS = MAX_HOEHE+3;  //die Zeitfenster-Striche sind sogar noch 3 Pixel drunter
static const int MAX_HOEHE_BALKEN=100;  //maximale Gesamthöhe der Balken
static const int MIN_HOEHE_BALKEN=10;   //Minhöhe Balken = Abstand zwischen 0 und preisMin = min (so dass man die Farbe noch sieht)
static const int MIN_MAX_HOEHE_BALKEN = MAX_HOEHE_BALKEN-MIN_HOEHE_BALKEN; //Höhe der Balken von min bis max

int preisMinAlleTage=0; //Minwert über beide Tage für Balkengrafik
int preisMaxAlleTage=0; //Maxwert über beide Tage für Balkengrafik

//************************
//@param preis = Preis in 1/100ct
//@return y-Wert im Balkendiagramm über 2 Tage
int getPreis2PixelHoehe(int preis){
  if (preisMinAlleTage < preisMaxAlleTage){ //normalerweise sind die beiden Werte unterschiedlich
    //(preisMaxAlleTage - preisMinAlleTage) --> MIN_MAX_HOEHE_BALKEN
    //(preis         - preisMinAlleTage) --> y
    //return y + MIN_HOEHE_BALKEN
    #ifdef DEB_SVG
      Serial.printf(" preis=%d\n", preis);
    #endif  
    return (int)((float)MIN_MAX_HOEHE_BALKEN / (preisMaxAlleTage - preisMinAlleTage) * (preis-preisMinAlleTage) + MIN_HOEHE_BALKEN );
  }else{ //falls alle Werte gleich sind würde obige Formel durch 0 teilen, aber dann sind alle Balken gleich groß
    #ifdef DEB_SVG
      Serial.print("_\n");
    #endif  
    return MAX_HOEHE_BALKEN;
  }  
}

//************************
//Balkengrafik als SVG an client1 senden
//client1 muss available()=true sein
void clientPrintSVGBarChart(){
  int i=0;     //Nummer des Balkens
  int hoehe=0; //Höhe des Balkens: preisMaxAlleTage = 100%, schalttabelle[t][h]=hoehe%
  String fill;
  String stroke;
  String tagLinks;
  String tagRechts;

  //morgenGelesen->heute/morgen anzeigen, sonst gestern/heute anzeigen
  int startTag = 0;
  if (morgenGelesen){
    startTag=heute;
    //sprintf(tagLinks, "HEUTE (%s %d.%d.)", wochentag[tm.tm_wday], tm.tm_mday, tm.tm_mon);
    tagLinks ="HEUTE";
    tagRechts="MORGEN";
  }else{
    startTag=gestern;
    tagLinks = "GESTERN";
    tagRechts ="HEUTE";
    //sprintf(tagRechts, "HEUTE (%s %d.%d.)", wochentag[tm.tm_wday], tm.tm_mday, tm.tm_mon);
  }

  preisMinAlleTage = min( preisMin[startTag], preisMin[startTag+1]); 
  preisMaxAlleTage = max( preisMax[startTag], preisMax[startTag+1]);
  
  //Balken und Linie drunter für Zeitfenster
  for(int t=startTag; t<(startTag+2); t++) {
    #ifdef DEB_SVG
      Serial.printf("\nTag=%d Min=%d, Mittel=%d, Max=%d\n", t, preisMin[t], preisMittel[t], preisMax[t]);
    #endif  
    
    for(int h=0; h<24; h++) {
      #ifdef DEB_SVG
        Serial.printf("Std=%d ", h);
      #endif

      //<rect class="fiGn sGnN" x="0" y="100" width="8" height="10"/>
      if (getSchaltWert(t, h)){
        #ifdef DEB_SVG
          Serial.print("->1,");
        #endif  
        fill = "fiGn";
        stroke = "sGnN";
      }else{
        #ifdef DEB_SVG
          Serial.print("->0,");
        #endif  
        fill = "fiRd";
        stroke = "sRdN";
      }

      if (t==heute && h==tm.tm_hour){
        stroke = "sBlB";
      }

      hoehe = getPreis2PixelHoehe(preis[t][h]);
      client.printf("<rect class=\"%s %s\" x=\"%d\" y=\"%d\" width=\"%d\" height=\"%d\"/>\n", fill.c_str(), stroke.c_str(), i*WIDTH + SB, MAX_HOEHE-hoehe, WIDTH_BALKEN, hoehe);

      //grüne/rote Linie = aktuelle Stunde ist /ist nicht im Zeitfenster
      if (istInZeitfenster(h)){
        stroke = "sGnB"; //green bold
      }else{
        stroke = "sRdB"; //red bold
      }
      client.printf("<line class=\"fiNo %s\" x1=\"%d\" y1=\"%d\" x2=\"%d\" y2=\"%d\"/>\n", stroke.c_str(), i*WIDTH + SB, ZF_POS, i*WIDTH + WIDTH_BALKEN + SB, ZF_POS);

      i++;
    }
  }

  //Min/Max Beschriftung links
  client.printf("<text x=\"0\" y=\"%d\" class=\"fiGr fnt\">%.2f</text>\n", MAX_HOEHE - getPreis2PixelHoehe(preisMinAlleTage), preisMinAlleTage/100.0);
  client.printf("<text x=\"0\" y=\"%d\" class=\"fiGr fnt\">%.2f   %s</text>\n", MAX_HOEHE - getPreis2PixelHoehe(preisMaxAlleTage), preisMaxAlleTage/100.0, tagLinks.c_str()); //hier Beschriftung für linken Tag anhängen

  //waagrechter Strich "Schaltgrenze" je Tag
  int grenze=0;   //in 1/100 ct
  int grenzPos=0; //in Pixeln

  int tagIndex=0; //der wievielte Tag wird dargestellt: 0=links, 1=rechts
  for (int tag=startTag; tag < startTag+2; tag++){
    switch (modus) {
      case mode_ein:
        grenze = preisMittel[tag];
      break;

      case mode_aus:
        grenze = preisMittel[tag];
      break;

      case mode_mittelwert:
        grenze = preisMittel[tag];
      break;

      case mode_mittelwert_min:
        grenze = (preisMittel[tag]+preisMin[tag]) / 2;
      break;

      case mode_mittelwert_max:
        grenze = (preisMittel[tag]+preisMax[tag]) / 2;
      break;

      case mode_stundenzahl:
        if (stundenzahl>0){
          grenze = preisSort[tag][stundenzahl-1];
        }else{
          grenze = preisMin[tag];
        }  

      break;

      default:
        grenze = preisMin[tag];
      break;
    }
    grenzPos = MAX_HOEHE -  getPreis2PixelHoehe(grenze);
    if (tag==startTag){ //zugehörige Beschriftung für den 1./linken Tag
      client.printf("<text x=\"0\" y=\"%d\" class=\"fiGr fnt\">%.2f</text>\n", grenzPos, grenze/100.0);
    }else{ //für den 2. /rechten Tag
      client.printf("<text x=\"%d\" y=\"%d\" class=\"fiGr fnt\">%.2f</text>\n", ((tagIndex+1) * 24*WIDTH) + SB + 2 , grenzPos, grenze/100.0);
    }
    //Linie
    client.printf("<line class=\"fiNo sGrN\" x1=\"%d\" y1=\"%d\" x2=\"%d\" y2=\"%d\"/>\n", SB + (tagIndex * 24*WIDTH), grenzPos, SB + ((tagIndex+1) * 24*WIDTH), grenzPos);

    tagIndex++;
  }

  //senkrechte graue Linien
  client.printf("<line class=\"fiNo sGrN\" x1=\"%d\" y1=\"%d\" x2=\"%d\" y2=\"%d\"/>\n", 12*WIDTH -1 + SB, MAX_HOEHE, 12*WIDTH -1 + SB, MAX_HOEHE-MAX_HOEHE_BALKEN);
  client.printf("<line class=\"fiNo sGrB\" x1=\"%d\" y1=\"%d\" x2=\"%d\" y2=\"%d\"/>\n", 24*WIDTH -1 + SB, MAX_HOEHE, 24*WIDTH -1 + SB, MAX_HOEHE-MAX_HOEHE_BALKEN);
  client.printf("<line class=\"fiNo sGrN\" x1=\"%d\" y1=\"%d\" x2=\"%d\" y2=\"%d\"/>\n", 36*WIDTH -1 + SB, MAX_HOEHE, 36*WIDTH -1 + SB, MAX_HOEHE-MAX_HOEHE_BALKEN);

  //Beschriftung senkrechte Linien
  client.printf("<text x=\"%d\" y=\"%d\" class=\"fiGr fnt\">12</text>\n",      12*WIDTH -7 + SB, MAX_HOEHE-MAX_HOEHE_BALKEN);
  client.printf("<text x=\"%d\" y=\"%d\" class=\"fiGr fnt\">0   %s</text>\n",  24*WIDTH -5 + SB, MAX_HOEHE-MAX_HOEHE_BALKEN, tagRechts.c_str()); //hier Beschriftung für rechten Tag anhängen
  client.printf("<text x=\"%d\" y=\"%d\" class=\"fiGr fnt\">12</text>\n",      36*WIDTH -7 + SB, MAX_HOEHE-MAX_HOEHE_BALKEN);
}  

void clientPrintSVGFooter(){
  client.println("</svg>");
}


//*********************************************************************************************************************
int findStart(const char * such, const char * str) {
  char* occurance = strstr(str, such);
  if (occurance != NULL){
    return occurance-str;
  }else{
    return -1;
  }
}


//*********************************************************************************************************************
int findEnd(const char * such, const char * str) {
  int tmp = findStart(such, str);
  if (tmp >= 0)tmp += strlen(such);
  return tmp;
}

//*********************************************************************************************************************
bool contains(const char * such, const char * str) {
  return strstr(str, such) != NULL;
}

//*********************************************************************************************************************
int pickDec(const char * tx, int idx ) {
  int tmp = 0;
  for (int p = idx; p < idx + 5 && (tx[p] >= '0' && tx[p] <= '9') ; p++) {//alle Ziffern kopieren
    tmp = 10 * tmp + tx[p] - '0';
  }
  return tmp;
}

//*********************************************************************************************************************
int pickZahl(const char * par, const char * str) {
  int myIdx = findEnd(par, str);
  if (myIdx >= 0) return  pickDec(str, myIdx);
  else return -1;
}
