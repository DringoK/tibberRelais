//ingosserver.h abgeleitet von uwesserver.h (make 5/24 im heise-Verlag)
WiFiServer server(80);
WiFiClient client1;
#define MAX_PACKAGE_SIZE 2048
char HTTP_Header[110];
int Aufruf_Zaehler = 0, switch_color=0;
#define ACTION_Tor 1
#define ACTION_Wallbox 2
#define ACTION_Refresh 3
int action; //, leistung, solar;
//*********************************************************************************************************************
bool wifi_traffic() ;
int Pick_Parameter_Zahl(const char*, char*);
void send_HTML() ;

void makeSVGHeader();
void makeSVGBarChart();
void makeSVGFooter();

void send_bin(const unsigned char * , int, const char * , const char * ) ;
int Find_End(const char *, const char *) ;
int Find_Start(const char *, const char *) ;
int Pick_Dec(const char *, int ) ;
void exhibit(const char *, int) ;
void exhibit(const char *, unsigned int) ;
void exhibit(const char *, unsigned long) ;
void exhibit(const char *, const char *) ;

//*********************************************************************************************************************
bool wifi_traffic() {
  char my_char;
  int htmlPtr = 0;
  unsigned long my_timeout;

  client1 = server.accept();  // Check if a client1 has connected
  if (!client1) return false;
  
  my_timeout = millis() + 250L;
  
  while (!client1.available() && (millis() < my_timeout) ) delay(10);
  delay(10);
  
  if (millis() > my_timeout) return(-1);
  
  htmlPtr = 0;
  my_char = '\0';
  while (client1.available() && my_char != '\r') { //\r = Return
    my_char = client1.read();
    puffer[htmlPtr++] = my_char;
  }
  
  client1.flush();
  puffer[htmlPtr] = '\0';
  #ifdef DEB_HTML
    Serial.print("Empfangen: ");Serial.println(puffer);
  #endif  
  
  if ( (Find_Start ("/X?", puffer) < 0 && Find_Start ("/t", puffer) < 0) && Find_Start ("r=R", puffer) <0  && Find_Start ("GET / HTTP", puffer) < 0 ) {
    client1.print("HTTP/1.1 404 Not Found\r\n\r\n");
    delay(20);
    client1.stop();

    return false;
  }
  
  refreshPressed=false;
  if (Find_Start ("r=R", puffer) > 0 ) { //Refresh wurde gedrückt
    refreshPressed=true;
    #ifdef DEB_HTML
      Serial.println("REFRESH");
    #endif  
  }
  if (Find_Start ("/X?", puffer) > 0) {
    startZeit = Pick_Parameter_Zahl("s=", puffer);         //Startzeit: Benutzereingaben einlesen und verarbeiten
    endeZeit = Pick_Parameter_Zahl("e=", puffer);          //Endezeit: Benutzereingaben einlesen und verarbeiten
    modus = Pick_Parameter_Zahl("m=", puffer);             //Mode: Benutzereingaben einlesen und verarbeiten
    stundenzahl = Pick_Parameter_Zahl("h=", puffer);       //Stundenzahl: Benutzereingaben einlesen und verarbeiten
  }
  preisabhaengig_schalten(); //und gleich auf den neuen Modus reagieren, auf jeden Fall vor send_HTML!
  
  // Header aufbauen
  client1.println("HTTP/1.1 200 OK"); 
  client1.println("\r\nContent-Type: text/html\r\nConnection: close\r\n");

  send_HTML();  //Antwortseite aufbauen

  client1.stop();

  return true;
}
//*********************************************************************************************************************
//die html-Antwort für das Webserver-Interface zusammenbauen und auf client1 ausgeben (client1 muss über server.accept verbunden sein und client.available() muss true sein)
//
String backColor;

void send_HTML() {
  static boolean farbe=LOW;
  client1.println("<!DOCTYPE html>");
  client1.println("<html lang=\"de\">");
  client1.println("<meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">"); //für Schriftgroesse Smartphone
  client1.println("<head><title>Tibber-Relais></title></head>");

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
  client1.printf("<body style=\"background-color:%s; font-family:verdana\">\r\n", backColor.c_str());
  client1.println("<h2>Tibber Relais</h2>");
  client1.println("<form action=\"/X\">");

//*********************** Start und Ende Stunde **************************
  client1.println("<table>");
  client1.println( "<tr>");
  client1.println(   "<td><label for=\"s\"> Zeitfenster (0 ... 23) von : </label></td>");
  client1.print  (   "<td><input type=\"number\" style= \"width:40px\" id=\"s\" name=\"s\" min=\"0\" max=\"23\" value=\"");
  client1.print  (startZeit);
  client1.println("\"></td>");
  client1.println(   "<td><label for=\"e\">&nbsp;&nbsp;  bis: </label></td>"); //&nbsp Leerzeichen
  client1.print  (   "<td><input type=\"number\" style= \"width:40px\" id=\"e\" name=\"e\" min=\"0\" max=\"23\" value=\"");
  client1.print  (endeZeit);
  client1.println("\"></td>");
  client1.println("</tr>");
  client1.println("</table>");

//*********************** Modus: radio ***********************
  client1.println("<br>Sperre aufheben (Heizen), wenn billiger als: ");
  client1.println("<table>");

  client1.println("<tr>");
  client1.printf("<td><input type=\"radio\" name=\"m\" id=\"%d\" value=\"%d\"", mode_ein, mode_ein);
  if(modus==mode_ein) client1.print(" CHECKED");
  client1.println("></td>");
  client1.printf("<td><label for=\"%d\"> an </label><br></td>\r\n", mode_ein);
  client1.println("</tr>");

  client1.println("<tr>");
  client1.printf("<td><input type=\"radio\" name=\"m\" id=\"%d\" value=\"%d\"", mode_aus, mode_aus);
  if(modus==mode_aus) client1.print(" CHECKED");
  client1.println("></td>");
  client1.printf("<td><label for=\"%d\"> aus </label><br></td>", mode_aus);
  client1.println("</tr>");

  client1.println("<tr>");
  client1.printf("<td><input type=\"radio\" name=\"m\" id=\"%d\" value=\"%d\"", mode_mittelwert_max, mode_mittelwert_max);
  if(modus==mode_mittelwert_max) client1.print(" CHECKED");
  client1.println("></td>");
  client1.printf("<td><label for=\"%d\"> (Preis Mittelwert + Max)/2 </label><br></td>\r\n", mode_mittelwert_max);
  client1.println("</tr>");

  client1.println("<tr>");
  client1.printf("<td><input type=\"radio\" name=\"m\" id=\"%d\" value=\"%d\"", mode_mittelwert, mode_mittelwert);
  if(modus==mode_mittelwert) client1.print(" CHECKED");
  client1.println("></td>");
  client1.printf("<td><label for=\"%d\"> Preis Mittelwert</label><br></td>\r\n", mode_mittelwert);
  client1.println("</tr>");

  client1.println("<tr>");
  client1.printf("<td><input type=\"radio\" name=\"m\" id=\"%d\" value=\"%d\"", mode_mittelwert_min, mode_mittelwert_min);
  if(modus==mode_mittelwert_min) client1.print(" CHECKED");
  client1.println("></td>");
  client1.printf("<td><label for=\"%d\"> (Preis Mittelwert + Min)/2 </label></td>\r\n", mode_mittelwert_min);
  client1.println("</tr>");
  
  client1.println("<tr>");
  client1.printf("<td><input type=\"radio\" name=\"m\" id=\"%d\" value=\"%d\"", mode_stundenzahl, mode_stundenzahl);
  if(modus==mode_stundenzahl) client1.print(" CHECKED");
  client1.println("></td>");
  client1.printf("<td><label for=\"%d\"> Preiswerteste Stunden (Anzahl): </label></td>\r\n", mode_stundenzahl);
  client1.println("<td><input type=\"number\" style= \"width:40px\" id=\"h\" name=\"h\" min=\"1\" max=\"23\"value=\"");
  if(stundenzahl>0) client1.print(stundenzahl);
  client1.println("\"></td>");
  client1.println("</tr>");

  client1.println("</table>");

//*********************** Senden / Aktualisieren Button ******************************
  client1.println("<table>");
  client1.println( "<tr>");
  client1.println(   "<td><input type=\"submit\"></td>");
  client1.println(   "<td><button style= \"width:130px\" name=\"r\" value=\"R\">Preise aktualisieren</button></td>"); 
  client1.println( "</tr></table></form>");

//Balkendiagramm als SVG
  makeSVGHeader();
  makeSVGBarChart();
  makeSVGFooter();
//*********************** graue Statuszeilee *******************************
  client1.printf("<br><p style=\"font-size:11px;color:gray\"> Version: %s, RSSI: %d, %s %02d.%02d.%04d %02d:%02d:%02d, %d\r\n",
                                 vers, wifi_station_get_rssi(), wochentag[tm.tm_wday],tm.tm_mday,tm.tm_mon,tm.tm_year,tm.tm_hour,tm.tm_min,tm.tm_sec, Aufruf_Zaehler++);

//*********************** HTML Ende **************************
  client1.println("</body>");
  client1.println("</html> "); 
} // Ende send_HTML

void makeSVGHeader(){
  client1.println("<svg xmlns=\"http://www.w3.org/2000/svg\" xml:space=\"preserve\" width=\"125mm\" height=\"45mm\" style=\"shape-rendering:geometricPrecision; text-rendering:geometricPrecision; image-rendering:optimizeQuality; fill-rule:evenodd; clip-rule:evenodd\"");
  client1.println("viewBox=\"0 0 510 115\">");
  client1.println("<defs>"); //Definitionen für SVG als Bibliothek
  client1.println( "<style type=\"text/css\">");
  client1.println( "<![CDATA[");
  client1.println(   "@font-face{font-family:\"Verdana\";font-variant:normal;font-style:normal;font-weight:normal;src:url(\"#FontID0\") format(svg)}");

  client1.println(   ".sGnN {stroke:#006633;stroke-width:1}"); //stroke Green Normal
  client1.println(   ".sGnB {stroke:#006633;stroke-width:3}"); //stroke Green Bold
  client1.println(   ".sRdN {stroke:#CC3300;stroke-width:1}"); //stroke Red Normal
  client1.println(   ".sRdB {stroke:#CC3300;stroke-width:3}"); //stroke Red Bold
  client1.println(   ".sGrN {stroke:gray;stroke-width:0.5}");  //stroke Gray Normal
  client1.println(   ".sGrB {stroke:gray;stroke-width:2}");    //stroke Gray Bold
  client1.println(   ".sBlB {stroke:blue;stroke-width:3}");    //stroke Blue Bold
  client1.println(   ".fiNo {fill:none}");     //fill None
  client1.println(   ".fiGn {fill:#33CC66}");  //fill Green
  client1.println(   ".fiRd {fill:#FF6633}");  //fill Red
  client1.println(   ".fiGr {fill:gray}");     //fill Gray
  client1.println(   ".fnt {font-size:11px}");
  client1.println("]]>");
  client1.println("</style>");
  client1.println("</defs>");
} //end makeSVGHeader  


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
void makeSVGBarChart(){
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
      client1.printf("<rect class=\"%s %s\" x=\"%d\" y=\"%d\" width=\"%d\" height=\"%d\"/>\n", fill.c_str(), stroke.c_str(), i*WIDTH + SB, MAX_HOEHE-hoehe, WIDTH_BALKEN, hoehe);

      //grüne/rote Linie = aktuelle Stunde ist /ist nicht im Zeitfenster
      if (istInZeitfenster(h)){
        stroke = "sGnB"; //green bold
      }else{
        stroke = "sRdB"; //red bold
      }
      client1.printf("<line class=\"fiNo %s\" x1=\"%d\" y1=\"%d\" x2=\"%d\" y2=\"%d\"/>\n", stroke.c_str(), i*WIDTH + SB, ZF_POS, i*WIDTH + WIDTH_BALKEN + SB, ZF_POS);

      i++;
    }
  }

  //Min/Max Beschriftung links
  client1.printf("<text x=\"0\" y=\"%d\" class=\"fiGr fnt\">%.2f</text>\n", MAX_HOEHE - getPreis2PixelHoehe(preisMinAlleTage), preisMinAlleTage/100.0);
  client1.printf("<text x=\"0\" y=\"%d\" class=\"fiGr fnt\">%.2f   %s</text>\n", MAX_HOEHE - getPreis2PixelHoehe(preisMaxAlleTage), preisMaxAlleTage/100.0, tagLinks.c_str()); //hier Beschriftung für linken Tag anhängen

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
      client1.printf("<text x=\"0\" y=\"%d\" class=\"fiGr fnt\">%.2f</text>\n", grenzPos, grenze/100.0);
    }else{ //für den 2. /rechten Tag
      client1.printf("<text x=\"%d\" y=\"%d\" class=\"fiGr fnt\">%.2f</text>\n", ((tagIndex+1) * 24*WIDTH) + SB + 2 , grenzPos, grenze/100.0);
    }
    //Linie
    client1.printf("<line class=\"fiNo sGrN\" x1=\"%d\" y1=\"%d\" x2=\"%d\" y2=\"%d\"/>\n", SB + (tagIndex * 24*WIDTH), grenzPos, SB + ((tagIndex+1) * 24*WIDTH), grenzPos);

    tagIndex++;
  }

  //senkrechte graue Linien
  client1.printf("<line class=\"fiNo sGrN\" x1=\"%d\" y1=\"%d\" x2=\"%d\" y2=\"%d\"/>\n", 12*WIDTH -1 + SB, MAX_HOEHE, 12*WIDTH -1 + SB, MAX_HOEHE-MAX_HOEHE_BALKEN);
  client1.printf("<line class=\"fiNo sGrB\" x1=\"%d\" y1=\"%d\" x2=\"%d\" y2=\"%d\"/>\n", 24*WIDTH -1 + SB, MAX_HOEHE, 24*WIDTH -1 + SB, MAX_HOEHE-MAX_HOEHE_BALKEN);
  client1.printf("<line class=\"fiNo sGrN\" x1=\"%d\" y1=\"%d\" x2=\"%d\" y2=\"%d\"/>\n", 36*WIDTH -1 + SB, MAX_HOEHE, 36*WIDTH -1 + SB, MAX_HOEHE-MAX_HOEHE_BALKEN);

  //Beschriftung senkrechte Linien
  client1.printf("<text x=\"%d\" y=\"%d\" class=\"fiGr fnt\">12</text>\n",      12*WIDTH -7 + SB, MAX_HOEHE-MAX_HOEHE_BALKEN);
  client1.printf("<text x=\"%d\" y=\"%d\" class=\"fiGr fnt\">0   %s</text>\n",  24*WIDTH -5 + SB, MAX_HOEHE-MAX_HOEHE_BALKEN, tagRechts.c_str()); //hier Beschriftung für rechten Tag anhängen
  client1.printf("<text x=\"%d\" y=\"%d\" class=\"fiGr fnt\">12</text>\n",      36*WIDTH -7 + SB, MAX_HOEHE-MAX_HOEHE_BALKEN);
}  

void makeSVGFooter(){
  client1.println("</svg>");
}


//*********************************************************************************************************************
int Pick_Parameter_Zahl(const char * par, char * str) {
  int myIdx = Find_End(par, str);
  if (myIdx >= 0) return  Pick_Dec(str, myIdx);
  else return -1;
}

//*********************************************************************************************************************
int Find_Start(const char * such, const char * str) {
  int tmp = -1;
  int ll = strlen(such);
  int ww = strlen(str) - ll;
  for (int i = 0; i <= ww && tmp == -1; i++) {
    if (strncmp(such, &str[i], ll) == 0) tmp = i;
  }
  return tmp;
}

//*********************************************************************************************************************
int Find_End(const char * such, const char * str) {
  int tmp = Find_Start(such, str);
  if (tmp >= 0)tmp += strlen(such);
  return tmp;
}

//*********************************************************************************************************************
int Pick_Dec(const char * tx, int idx ) {
  int tmp = 0;
  for (int p = idx; p < idx + 5 && (tx[p] >= '0' && tx[p] <= '9') ; p++) {
    tmp = 10 * tmp + tx[p] - '0';
  }
  return tmp;
}
