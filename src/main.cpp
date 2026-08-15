#include <M5GFX.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <math.h>
#include "esp_sleep.h"
#include "secrets.h"

#include "SpaceMono26.h"
#include "SpaceMono36.h"

M5GFX display;

// ---------- Boutons de page ----------
constexpr gpio_num_t G37 = GPIO_NUM_37;   // page précédente
constexpr gpio_num_t G39 = GPIO_NUM_39;   // page suivante
constexpr uint32_t BTN_DEBOUNCE_MS = 250;

int page = 1;
unsigned long lastButton = 0, lastActivity = 0;
void resetActivityTimer();

// ---------- Deep sleep ----------
constexpr bool DEEPSLEEP_ENABLED = true;
constexpr uint32_t DEEPSLEEP_TIMEOUT_MS = 60000;
constexpr gpio_num_t TOUCH_INT = GPIO_NUM_36;
constexpr gpio_num_t MAIN_PWR  = GPIO_NUM_2;

// ---------- Home Assistant ----------
const char* ENTITY_CLIMATE  = "climate.clim";
const char* ENTITY_TEMP     = "sensor.salon_temperature";
const char* ENTITY_HUMIDITY = "sensor.salon_humidite";   // capteur d'humidité séparé
const char* ENTITY_OUTDOOR_TEMP     = "sensor.merignac_temperature";
const char* ENTITY_OUTDOOR_HUMIDITY = "sensor.merignac_humidity";
const char* ENTITY_WIFI_BUTTON = "input_button.creer_voucher";
const char* ENTITY_WIFI_SENSOR = "sensor.liste_hotspot_vouchers";

// ---------- Géométrie ----------
struct Rect { int x,y,w,h; };
bool inRect(const Rect& r,int x,int y){return x>=r.x&&x<=r.x+r.w&&y>=r.y&&y<=r.y+r.h;}

constexpr int R=0, BT=2, LT=2; // radius, border thickness, line thickness

// ---------- Hauteur des lignes ----------
constexpr int LINE_SMALL  = 40;
constexpr int LINE_MEDIUM = 80;
constexpr int LINE_LARGE  = 100;

// ---------- Actions ----------
constexpr int N6=6, N3=3;
struct ActionCard {
  const char* title;
  const char* domain[N6];
  const char* service[N6];
  const char* entity[N6];
  const char* label[N6];
  const char* state[N6];
  bool active[N6];
  Rect r;
  int titleH, buttonsH, cellW;
};

ActionCard lighting={
  "Éclairage",
  {"scene","scene","scene","scene","scene","scene"},
  {"turn_on","turn_on","turn_on","turn_on","turn_on","turn_on"},
  {"scene.canap","scene.diner","scene.all_off_cuisine_et_salon",
   "scene.welcome_home","scene.film_2","scene.veilleuse"},
  {"CANAP","DINER","OFF","ENTREE","KINO","VEILLEUSE"},
  {nullptr,nullptr,nullptr,nullptr,nullptr,nullptr}
};

ActionCard modes={
  "Modes",
  {"input_boolean","input_boolean","automation",nullptr,nullptr,nullptr},
  {"toggle","toggle","toggle",nullptr,nullptr,nullptr},
  {"input_boolean.annonce_audio_bus","input_boolean.absence_prolongee",
   "automation.fermeture_automatique_volets",nullptr,nullptr,nullptr},
  {"BUS","ABSENCE","SUNSHIELD",nullptr,nullptr,nullptr},
  {"input_boolean.annonce_audio_bus","input_boolean.absence_prolongee",
   "automation.fermeture_automatique_volets",nullptr,nullptr,nullptr}
};

ActionCard* actions[]={&lighting,&modes};
constexpr int ACTION_COUNT=2;

// ---------- Volets ----------
// Une seule ligne de commandes (monter/stop/descendre), partagee entre volets.
// Une ligne de selection permet de choisir quel volet elle pilote.
struct CoverCard {
  const char* title;
  const char* entity;
  String state;   // "open","closed","opening","closing",...
};
CoverCard covers[]={
  {"CUISINE","cover.shellyplus2pm_485519965dac"},
  {"SALON","cover.shellyswitch25_4c752532f095"}
};
constexpr int COVER_COUNT=2;

struct CoversBlock { Rect r; int titleH,selectH,buttonsH; } coversBlock;
int selectedCover=0;   // index du volet actuellement piloté

// ---------- Climatisation ----------
// Mode de fonctionnement affiché/piloté : Chauffage / Clim / Off
// (correspond aux hvac_mode Home Assistant "heat" / "cool" / "off")
constexpr float TEMP_STEP = 1.0f;
constexpr float TEMP_MIN  = 10.0f;
constexpr float TEMP_MAX  = 30.0f;
const char* HVAC_LABELS[3] = {"CHAUFFAGE","CLIM","OFF"};
const char* HVAC_MODES[3]  = {"heat","cool","off"};

struct ClimateCard { Rect r; int titleH,modeH,tempH,infoH,infoH2,cellW; } climate;
String hvacMode="off";
bool hasTarget=false, hasCurrent=false, hasHumidity=false;
float targetTemp=20.0f, currentTemp=0, currentHumidity=0;
bool hasOutdoorTemp=false, hasOutdoorHumidity=false;
float outdoorTemp=0, outdoorHumidity=0;

// ---------- Wi-Fi ----------
struct WifiCard { Rect r; int titleH,buttonH,passwordH,codeH; } wifiCard;
String voucherCode="";
bool hasVoucher=false;

// ---------- Spotify ----------
struct SpotifyCard { Rect r; int titleH,infoH,buttonsH; } spotifyCard;
String spotifyTitle="";
String spotifyArtist="";
String spotifyState="";
bool hasSpotify=false;

// ---------- Dessin ----------
void thickRound(int x,int y,int w,int h,int rad,uint32_t c,int t){
  for(int i=0;i<t;i++) display.drawRoundRect(x+i,y+i,w-2*i,h-2*i,max(0,rad-i),c);
}
void hline(int x,int y,int w,uint32_t c,int t){display.fillRect(x,y-t/2,w,t,c);}
void vline(int x,int y,int h,uint32_t c,int t){display.fillRect(x-t/2,y,t,h,c);}
void arrowUp(int x,int y,int s,uint32_t c){display.fillTriangle(x,y-s,x-s,y+s,x+s,y+s,c);}
void arrowDown(int x,int y,int s,uint32_t c){display.fillTriangle(x,y+s,x-s,y-s,x+s,y-s,c);}
void stopIcon(int x,int y,int s,uint32_t c){display.fillRect(x-s,y-s,2*s,2*s,c);}
void prevIcon(int x,int y,int s,uint32_t c){
  display.fillRect(x-s,y-s,s/3,2*s,c);
  display.fillTriangle(x+s,y-s,x-s/3,y,x+s,y+s,c);
}
void nextIcon(int x,int y,int s,uint32_t c){
  display.fillRect(x+s-s/3,y-s,s/3,2*s,c);
  display.fillTriangle(x-s,y-s,x+s/3,y,x-s,y+s,c);
}
void pauseIcon(int x,int y,int s,uint32_t c){
  int w=max(2,s/3);
  display.fillRect(x-s/2,y-s,w,2*s,c);
  display.fillRect(x+s/2-w,y-s,w,2*s,c);
}

// Ligne ondulee (volute de chaleur), tracee par segments successifs.
// topY/botY : etendue verticale ; amp : amplitude horizontale de l'ondulation.
void heatWave(int cx,int topY,int botY,int amp,uint32_t c){
  const int N=6;
  int px=cx,py=topY;
  for(int i=1;i<=N;i++){
    float t=(float)i/N;
    int x=cx+(int)(amp*sinf(t*6.9f));      // ~2 ondulations sur la hauteur
    int y=topY+(int)(t*(botY-topY));
    display.drawLine(px,py,x,y,c);
    display.drawLine(px+1,py,x+1,y,c);     // epaississement
    px=x;py=y;
  }
}

// Icone chauffage : basee sur le pictogramme "surface chaude"
// (barre horizontale + 3 volutes de chaleur qui s'en elevent).
void heatingIcon(int cx,int cy,int s,uint32_t c){
  int barY=cy+(int)(s*0.85f);
  display.fillRect(cx-(int)(s*0.8f),barY,(int)(s*1.6f),(int)(s*0.22f),c);
  int botY=barY-(int)(s*0.15f), topY=cy-(int)(s*1.0f);
  int amp=(int)(s*0.22f);
  heatWave(cx-(int)(s*0.55f),topY,botY,amp,c);
  heatWave(cx,               topY,botY,amp,c);
  heatWave(cx+(int)(s*0.55f),topY,botY,amp,c);
}

// Icone clim : flocon hexagonal (3 axes) avec ramifications sur chaque branche.
void snowIcon(int cx,int cy,int s,uint32_t c){
  for(int k=0;k<3;k++){
    float a=k*PI/3.0f, ux=cosf(a), uy=sinf(a);
    display.drawLine(cx-ux*s,   cy-uy*s,   cx+ux*s,   cy+uy*s,   c);
    display.drawLine(cx-ux*s,   cy-uy*s+1, cx+ux*s,   cy+uy*s+1, c);
    for(int sg=-1;sg<=1;sg+=2){
      float px=cx+ux*s*0.55f*sg, py=cy+uy*s*0.55f*sg;
      for(int sd=-1;sd<=1;sd+=2){
        float ba=a+sd*(PI/3.0f);
        display.drawLine(px,py,px+cosf(ba)*s*0.32f*sg,py+sinf(ba)*s*0.32f*sg,c);
      }
    }
  }
}

void cardFrame(const Rect& r,const char* title,int titleH){
  display.fillRoundRect(r.x,r.y,r.w,r.h,R,TFT_WHITE);
  display.setTextDatum(middle_center);
  display.setTextColor(TFT_BLACK,TFT_WHITE);
  display.setTextSize(1);
  display.drawString(title,r.x+r.w/2,r.y+titleH/2);
  hline(r.x,r.y+titleH,r.w,TFT_BLACK,LT);
}

void drawThree(const Rect& r,const char* title,int titleH,int bh,int pressed,
               const bool* active,const char* labels[3],bool icons){
  cardFrame(r,title,titleH);
  int cw=r.w/3, y=r.y+titleH;
  for(int i=0;i<3;i++){
    bool hi=(i==pressed)||(active&&active[i]);
    uint32_t bg=hi?TFT_BLACK:TFT_WHITE, fg=hi?TFT_WHITE:TFT_BLACK;
    int x=r.x+i*cw,cx=x+cw/2,cy=y+bh/2;
    display.fillRect(x,y,cw,bh,bg);
    if(icons){
      int s=min(cw,bh)/4;
      if(i==0) arrowUp(cx,cy,s,fg);
      else if(i==1) stopIcon(cx,cy,(int)(s*.8),fg);
      else arrowDown(cx,cy,s,fg);
    }else{
      display.setTextColor(fg,bg); display.setTextSize(1);
      display.drawString(labels[i],cx,cy);
    }
  }
  vline(r.x+cw,y,bh,TFT_BLACK,LT); vline(r.x+2*cw,y,bh,TFT_BLACK,LT);
  thickRound(r.x,r.y,r.w,r.h,R,TFT_BLACK,BT);
}

void drawLighting(ActionCard& a,int pressed=-1){
  cardFrame(a.r,a.title,a.titleH);
  int cw=a.r.w/3,rh=a.buttonsH/2,y=a.r.y+a.titleH;
  for(int i=0;i<6;i++){
    int row=i/3,col=i%3,x=a.r.x+col*cw,yy=y+row*rh;
    bool hi=(i==pressed)||a.active[i];
    uint32_t bg=hi?TFT_BLACK:TFT_WHITE,fg=hi?TFT_WHITE:TFT_BLACK;
    display.fillRect(x,yy,cw,rh,bg);
    display.setTextColor(fg,bg); display.setTextSize(1);
    display.drawString(a.label[i],x+cw/2,yy+rh/2);
  }
  vline(a.r.x+cw,y,a.buttonsH,TFT_BLACK,LT);
  vline(a.r.x+2*cw,y,a.buttonsH,TFT_BLACK,LT);
  hline(a.r.x,y+rh,a.r.w,TFT_BLACK,LT);
  thickRound(a.r.x,a.r.y,a.r.w,a.r.h,R,TFT_BLACK,BT);
}

void drawAction(ActionCard& a,int pressed=-1){
  if(&a==&lighting) drawLighting(a,pressed);
  else drawThree(a.r,a.title,a.titleH,a.buttonsH,pressed,a.active,a.label,false);
}

// pressedSel : bouton de selection en cours d'appui (-1 = aucun)
// pressedBtn : bouton monter/stop/descendre en cours d'appui (-1 = aucun)
void drawCovers(int pressedSel=-1,int pressedBtn=-1){
  cardFrame(coversBlock.r,"Volets",coversBlock.titleH);
  int selCw=coversBlock.r.w/COVER_COUNT;
  int y=coversBlock.r.y+coversBlock.titleH;

  // ---- selecteur de volet ----
  for(int i=0;i<COVER_COUNT;i++){
    bool hi=(i==pressedSel)||(i==selectedCover);
    uint32_t bg=hi?TFT_BLACK:TFT_WHITE, fg=hi?TFT_WHITE:TFT_BLACK;
    int x=coversBlock.r.x+i*selCw;
    display.fillRect(x,y,selCw,coversBlock.selectH,bg);
    display.setTextColor(fg,bg); display.setTextSize(1);
    display.drawString(covers[i].title,x+selCw/2,y+coversBlock.selectH/2);
  }
  for(int i=1;i<COVER_COUNT;i++) vline(coversBlock.r.x+i*selCw,y,coversBlock.selectH,TFT_BLACK,LT);
  y+=coversBlock.selectH; hline(coversBlock.r.x,y,coversBlock.r.w,TFT_BLACK,LT);

  // ---- commandes du volet selectionne (affichees une seule fois) ----
  CoverCard& c=covers[selectedCover];
  int cw=coversBlock.r.w/3, s=min(cw,coversBlock.buttonsH)/4;
  for(int i=0;i<3;i++){
    bool hi=(i==pressedBtn);
    uint32_t bg=hi?TFT_BLACK:TFT_WHITE, fg=hi?TFT_WHITE:TFT_BLACK;
    int x=coversBlock.r.x+i*cw,cx=x+cw/2,cy=y+coversBlock.buttonsH/2;
    display.fillRect(x,y,cw,coversBlock.buttonsH,bg);
    if(i==0) arrowUp(cx,cy,s,fg);
    else if(i==1) stopIcon(cx,cy,(int)(s*.8),fg);
    else arrowDown(cx,cy,s,fg);
  }
  vline(coversBlock.r.x+cw,y,coversBlock.buttonsH,TFT_BLACK,LT);
  vline(coversBlock.r.x+2*cw,y,coversBlock.buttonsH,TFT_BLACK,LT);

  thickRound(coversBlock.r.x,coversBlock.r.y,coversBlock.r.w,coversBlock.r.h,R,TFT_BLACK,BT);
}

// pressedMode : bouton de mode en cours d'appui (-1 = aucun)
// pressedTemp : bouton -/+ en cours d'appui (-1 = aucun)
void drawClimate(int pressedMode=-1,int pressedTemp=-1){
  cardFrame(climate.r,"Climatisation",climate.titleH);
  int cw=climate.r.w/3;
  int y=climate.r.y+climate.titleH;

  // ---- ligne mode : icone volutes (chauffage) / icone flocon (clim) / OFF ----
  int modeIconS=min(cw,climate.modeH)/4;
  for(int i=0;i<3;i++){
    bool hi=(i==pressedMode)||(hvacMode==HVAC_MODES[i]);
    uint32_t bg=hi?TFT_BLACK:TFT_WHITE, fg=hi?TFT_WHITE:TFT_BLACK;
    int x=climate.r.x+i*cw,cx=x+cw/2,cy=y+climate.modeH/2;
    display.fillRect(x,y,cw,climate.modeH,bg);
    if(i==0) heatingIcon(cx,cy,modeIconS,fg);
    else if(i==1) snowIcon(cx,cy,modeIconS,fg);
    else{ display.setTextColor(fg,bg); display.setTextSize(1); display.drawString(HVAC_LABELS[i],cx,cy); }
  }
  vline(climate.r.x+cw,y,climate.modeH,TFT_BLACK,LT);
  vline(climate.r.x+2*cw,y,climate.modeH,TFT_BLACK,LT);
  y+=climate.modeH; hline(climate.r.x,y,climate.r.w,TFT_BLACK,LT);

  // ---- ligne consigne : - / valeur / + ----
  String tempLabel=hasTarget?String(targetTemp,1)+"°C":"--";
  for(int i=0;i<3;i++){
    bool hi=(i==pressedTemp);
    uint32_t bg=hi?TFT_BLACK:TFT_WHITE, fg=hi?TFT_WHITE:TFT_BLACK;
    int x=climate.r.x+i*cw,cx=x+cw/2,cy=y+climate.tempH/2;
    display.fillRect(x,y,cw,climate.tempH,bg);
    display.setTextColor(fg,bg); display.setTextSize(1);
    if(i==0) display.drawString("-",cx,cy);
    else if(i==1) display.drawString(tempLabel.c_str(),cx,cy);
    else display.drawString("+",cx,cy);
  }
  vline(climate.r.x+cw,y,climate.tempH,TFT_BLACK,LT);
  vline(climate.r.x+2*cw,y,climate.tempH,TFT_BLACK,LT);
  y+=climate.tempH; hline(climate.r.x,y,climate.r.w,TFT_BLACK,LT);

  // ---- ligne info : température actuelle + humidité (salon) ----
  String t=hasCurrent?String(currentTemp,1)+"°C":"--";
  String h=hasHumidity?String(currentHumidity,0)+"%":"--";
  display.setTextColor(TFT_BLACK,TFT_WHITE); display.setTextSize(1);
  display.drawString(("Salon "+t+" "+h).c_str(),climate.r.x+climate.r.w/2,y+climate.infoH/2);
  y+=climate.infoH; hline(climate.r.x,y,climate.r.w,TFT_BLACK,LT);

  // ---- ligne info : température/humidité extérieure ----
  String ot=hasOutdoorTemp?String(outdoorTemp,1)+"°C":"--";
  String oh=hasOutdoorHumidity?String(outdoorHumidity,0)+"%":"--";
  display.drawString(("Exter "+ot+" "+oh).c_str(),climate.r.x+climate.r.w/2,y+climate.infoH2/2);

  thickRound(climate.r.x,climate.r.y,climate.r.w,climate.r.h,R,TFT_BLACK,BT);
}

void drawSpotify(int pressed=-1){
  cardFrame(spotifyCard.r,"Spotify",spotifyCard.titleH);

  int y=spotifyCard.r.y+spotifyCard.titleH;

  // ---- titre + artiste ----
  display.setTextColor(TFT_BLACK,TFT_WHITE);
  display.setTextSize(1);
  String title=hasSpotify && spotifyTitle.length()?spotifyTitle:"Aucune lecture";
  String artist=hasSpotify && spotifyArtist.length()?spotifyArtist:"";

  int lineH=spotifyCard.infoH/2;

display.drawString(
  title.c_str(),
  spotifyCard.r.x+spotifyCard.r.w/2,
  y+lineH/2
);

if(artist.length()){
  display.drawString(
    artist.c_str(),
    spotifyCard.r.x+spotifyCard.r.w/2,
    y+lineH+lineH/2
  );
}

  y+=spotifyCard.infoH;
  hline(spotifyCard.r.x,y,spotifyCard.r.w,TFT_BLACK,LT);

  // ---- commandes : précédent / pause / suivant ----
  int cw=spotifyCard.r.w/3;
  int s=min(cw,spotifyCard.buttonsH)/4;
  for(int i=0;i<3;i++){
    bool hi=(i==pressed);
    uint32_t bg=hi?TFT_BLACK:TFT_WHITE, fg=hi?TFT_WHITE:TFT_BLACK;
    int x=spotifyCard.r.x+i*cw,cx=x+cw/2,cy=y+spotifyCard.buttonsH/2;
    display.fillRect(x,y,cw,spotifyCard.buttonsH,bg);

    if(i==0) prevIcon(cx,cy,s,fg);
    else if(i==1) pauseIcon(cx,cy,s,fg);
    else nextIcon(cx,cy,s,fg);
  }
  vline(spotifyCard.r.x+cw,y,spotifyCard.buttonsH,TFT_BLACK,LT);
  vline(spotifyCard.r.x+2*cw,y,spotifyCard.buttonsH,TFT_BLACK,LT);

  thickRound(spotifyCard.r.x,spotifyCard.r.y,spotifyCard.r.w,spotifyCard.r.h,R,TFT_BLACK,BT);
}

void drawWifi(bool pressed=false){
  cardFrame(wifiCard.r,"Hotspot Wi-Fi",wifiCard.titleH);
  int y=wifiCard.r.y+wifiCard.titleH;
  if(hasVoucher){
    display.setTextColor(TFT_BLACK,TFT_WHITE); display.setTextSize(1);
    display.drawString(WIFI_NAME,wifiCard.r.x+wifiCard.r.w/2,y+wifiCard.buttonH/2);
    y+=wifiCard.buttonH; hline(wifiCard.r.x,y,wifiCard.r.w,TFT_BLACK,LT);
    display.drawString(("Mot de passe : "+String(WIFI_GUEST_PASSWORD)).c_str(),
                       wifiCard.r.x+wifiCard.r.w/2,y+wifiCard.passwordH/2);
    y+=wifiCard.passwordH; hline(wifiCard.r.x,y,wifiCard.r.w,TFT_BLACK,LT);
    display.drawString(("Code : "+voucherCode).c_str(),
                       wifiCard.r.x+wifiCard.r.w/2,y+wifiCard.codeH/2);
  }else{
    int h=wifiCard.buttonH+wifiCard.passwordH+wifiCard.codeH;
    uint32_t bg=pressed?TFT_BLACK:TFT_WHITE,fg=pressed?TFT_WHITE:TFT_BLACK;
    display.fillRect(wifiCard.r.x,y,wifiCard.r.w,h,bg);
    display.setTextColor(fg,bg); display.setTextSize(1);
    display.drawString("Activer hotspot",wifiCard.r.x+wifiCard.r.w/2,y+h/2);
  }
  thickRound(wifiCard.r.x,wifiCard.r.y,wifiCard.r.w,wifiCard.r.h,R,TFT_BLACK,BT);
}

void status(const char* s){
  display.startWrite();
  display.fillRect(0,0,display.width(),35,TFT_WHITE);
  display.setTextDatum(top_left); display.setTextColor(TFT_BLACK,TFT_WHITE);
  display.setTextSize(1); display.drawString(s,10,8);
  display.endWrite();
}

// ---------- Layout ----------
void setupLayout(){
  int W=display.width(),m=12,cw=W-2*m,g=12;

  // ---------- Page 1 ----------
  // Eclairage, puis Modes, puis Spotify, puis Climatisation.
  int y=45;

  for(auto a:actions){
    a->titleH=LINE_SMALL;
    a->buttonsH=(a==&lighting)?(2*LINE_MEDIUM):LINE_MEDIUM;
    a->cellW=cw/3;

    a->r={
      m,
      y,
      cw,
      a->titleH+a->buttonsH
    };

    y+=a->r.h+g;

    // Spotify est place juste sous les Modes.
    if(a==&modes){
      spotifyCard.titleH=LINE_SMALL;
      spotifyCard.infoH=LINE_MEDIUM;
      spotifyCard.buttonsH=LINE_MEDIUM;

      spotifyCard.r={
        m,
        y,
        cw,
        spotifyCard.titleH+
        spotifyCard.infoH+
        spotifyCard.buttonsH
      };

      y+=spotifyCard.r.h+g;
    }
  }

  // ---------- Climatisation ----------
  climate.titleH=LINE_SMALL;
  climate.modeH=LINE_MEDIUM;
  climate.tempH=LINE_MEDIUM;
  climate.infoH=LINE_SMALL;
  climate.infoH2=LINE_SMALL;
  climate.cellW=cw/3;

  climate.r={
    m,
    y,
    cw,
    climate.titleH+
    climate.modeH+
    climate.tempH+
    climate.infoH+
    climate.infoH2
  };


  // ---------- Page 2 ----------
  // Volets en premier, puis Hotspot Wi-Fi.
  int y2=45;

  coversBlock.titleH=LINE_SMALL;
  coversBlock.selectH=56;
  coversBlock.buttonsH=LINE_LARGE;

  coversBlock.r={
    m,
    y2,
    cw,
    coversBlock.titleH+
    coversBlock.selectH+
    coversBlock.buttonsH
  };

  y2+=coversBlock.r.h+g;

  // ---------- Hotspot Wi-Fi ----------
  wifiCard.titleH=LINE_SMALL;
  wifiCard.buttonH=LINE_SMALL;
  wifiCard.passwordH=LINE_SMALL;
  wifiCard.codeH=LINE_SMALL;

  wifiCard.r={
    m,
    y2,
    cw,
    wifiCard.titleH+
    wifiCard.buttonH+
    wifiCard.passwordH+
    wifiCard.codeH
  };
}
// ---------- Home Assistant ----------
// extra : champs JSON additionnels, ex: ",\"temperature\":21.5"
bool haCall(const char* domain,const char* service,const char* entity,const String& extra=""){
  if(WiFi.status()!=WL_CONNECTED){status("Wi-Fi déconnecté");return false;}
  HTTPClient http;
  http.begin(String(HA_HOST)+"/api/services/"+domain+"/"+service);
  http.addHeader("Authorization",String("Bearer ")+HA_TOKEN);
  http.addHeader("Content-Type","application/json");
  String body=String("{\"entity_id\":\"")+entity+"\""+extra+"}";
  int code=http.POST(body);
  http.end();
  return code>0&&code<300;
}

bool haState(const char* entity,DynamicJsonDocument& doc){
  if(WiFi.status()!=WL_CONNECTED)return false;
  HTTPClient http;
  http.begin(String(HA_HOST)+"/api/states/"+entity);
  http.addHeader("Authorization",String("Bearer ")+HA_TOKEN);
  int code=http.GET(); bool ok=false;
  if(code==200) ok=deserializeJson(doc,http.getString())==DeserializationError::Ok;
  http.end(); return ok;
}

void refreshAction(ActionCard& a){
  int n=&a==&lighting?6:3;
  for(int i=0;i<n;i++){
    if(!a.state[i]){a.active[i]=false;continue;}
    DynamicJsonDocument d(512);
    a.active[i]=haState(a.state[i],d)&&d["state"].as<String>()=="on";
  }
}

void refreshCover(CoverCard& c){
  DynamicJsonDocument d(512);
  if(haState(c.entity,d)) c.state=d["state"].as<String>();
}

void refreshClimate(){
  DynamicJsonDocument d(2048);
  if(haState(ENTITY_CLIMATE,d)){
    hvacMode=d["state"].as<String>();   // "heat" / "cool" / "off" / autre
    if(!d["attributes"]["temperature"].isNull()){
      targetTemp=d["attributes"]["temperature"].as<float>();hasTarget=true;
    }
  }
  DynamicJsonDocument s(1024);
  if(haState(ENTITY_TEMP,s)){
    const char* v=s["state"]; if(v){currentTemp=atof(v);hasCurrent=true;}
  }
  DynamicJsonDocument hDoc(1024);
  if(haState(ENTITY_HUMIDITY,hDoc)){
    const char* v=hDoc["state"]; if(v){currentHumidity=atof(v);hasHumidity=true;}
  }
  DynamicJsonDocument otDoc(1024);
  if(haState(ENTITY_OUTDOOR_TEMP,otDoc)){
    const char* v=otDoc["state"]; if(v){outdoorTemp=atof(v);hasOutdoorTemp=true;}
  }
  DynamicJsonDocument ohDoc(1024);
  if(haState(ENTITY_OUTDOOR_HUMIDITY,ohDoc)){
    const char* v=ohDoc["state"]; if(v){outdoorHumidity=atof(v);hasOutdoorHumidity=true;}
  }
}

void refreshSpotify(){
  DynamicJsonDocument d(2048);
  if(!haState(ENTITY_SPOTIFY,d)){
    hasSpotify=false;
    spotifyTitle="";
    spotifyArtist="";
    spotifyState="";
    return;
  }

  spotifyState=d["state"].as<String>();
  spotifyTitle=d["attributes"]["media_title"].as<String>();
  spotifyArtist=d["attributes"]["media_artist"].as<String>();
  hasSpotify=(spotifyTitle.length()>0 || spotifyArtist.length()>0);
}

void refreshVoucher(){
  DynamicJsonDocument d(8192);
  if(!haState(ENTITY_WIFI_SENSOR,d))return;
  JsonArray a=d["attributes"]["data"].as<JsonArray>();
  if(a.isNull()||!a.size()){hasVoucher=false;voucherCode="";return;}
  JsonObject latest; String date;
  for(JsonObject v:a){String z=v["createdAt"].as<String>();if(latest.isNull()||z>date){latest=v;date=z;}}
  const char* code=latest["code"];
  if(code){
    String s=code;
    voucherCode=s.length()==10?s.substring(0,5)+"-"+s.substring(5):s;
    hasVoucher=true;
  }
}

void connectWifi(){
  status("Connexion Wi-Fi...");
  WiFi.mode(WIFI_STA); WiFi.begin(WIFI_SSID,WIFI_PASSWORD);
  unsigned long t=millis();
  while(WiFi.status()!=WL_CONNECTED&&millis()-t<15000)delay(300);
  status(WiFi.status()==WL_CONNECTED?"Wi-Fi connecté":"Echec WiFi");
}

// ---------- Pages ----------
// Effacement physique complet avant chaque changement de page.
// On utilise un mode EPD de qualite pour eviter les artefacts/ghosting.
void cleanScreen(){
  display.setEpdMode(epd_mode_t::epd_quality);
  display.startWrite();
  display.fillScreen(TFT_WHITE);
  display.endWrite();
  display.setEpdMode(epd_mode_t::epd_fastest);
}

void drawPage(){
  // Toujours effacer completement l'ancien contenu avant de dessiner
  // la nouvelle page.
  cleanScreen();

  display.startWrite();
  if(page==1){
    for(auto a:actions)drawAction(*a);
    drawSpotify();
    drawClimate();
  }else{
    drawCovers();
    drawWifi();
  }
  display.endWrite();
  status(page==1?"Page 1/2":"Page 2/2");
}

void changePage(int delta){
  page+=delta;
  if(page<1)page=2;
  if(page>2)page=1;
  resetActivityTimer();
  if(page==1) refreshSpotify();
  drawPage();
}

void resetActivityTimer(){lastActivity=millis();}

bool pageButtons(){
  if(millis()-lastButton<BTN_DEBOUNCE_MS)return false;
  if(digitalRead(G37)==LOW){lastButton=millis();changePage(-1);return true;}
  if(digitalRead(G39)==LOW){lastButton=millis();changePage(1);return true;}
  return false;
}

// ---------- Deep sleep ----------
void enterDeepSleep(){
  lgfx::touch_point_t tp;
  while(digitalRead(TOUCH_INT)==LOW){display.getTouch(&tp);delay(20);}
  esp_sleep_enable_ext0_wakeup(TOUCH_INT,0);
  display.startWrite(); display.fillScreen(TFT_WHITE);
  display.setTextDatum(middle_center);display.setTextColor(TFT_BLACK,TFT_WHITE);
  display.loadFont(SpaceMono36);
  display.setTextSize(1);display.drawString("RÉVEILLE-MOI",display.width()/2,display.height()/2);
  display.endWrite(); display.display();
  WiFi.disconnect(true);WiFi.mode(WIFI_OFF);display.setBrightness(0);
  gpio_hold_en(MAIN_PWR);gpio_deep_sleep_hold_en();Serial.flush();esp_deep_sleep_start();
}

// ---------- Touch ----------
int threeIndex(const Rect&r,int cw,int x){return constrain((x-r.x)/cw,0,2);}
int sixIndex(const ActionCard&a,int x,int y){
  int cw=a.r.w/3,rh=a.buttonsH/2,by=a.r.y+a.titleH;
  return constrain((y-by)/rh,0,1)*3+constrain((x-a.r.x)/cw,0,2);
}

bool touchAction(ActionCard&a,int x,int y){
  if(!inRect(a.r,x,y)||y<a.r.y+a.titleH)return false;
  int i=&a==&lighting?sixIndex(a,x,y):threeIndex(a.r,a.cellW,x);
  display.startWrite();drawAction(a,i);display.endWrite();
  status((String(a.label[i])+"...").c_str());
  bool ok=haCall(a.domain[i],a.service[i],a.entity[i]);
  status(ok?"OK":"Erreur commande HA");delay(300);
  refreshAction(a);display.startWrite();drawAction(a);display.endWrite();return true;
}

bool touchCovers(int x,int y){
  if(!inRect(coversBlock.r,x,y))return false;
  int selY=coversBlock.r.y+coversBlock.titleH;
  int btnY=selY+coversBlock.selectH;

  // ---- selection du volet a piloter ----
  if(y>=selY&&y<btnY){
    int selCw=coversBlock.r.w/COVER_COUNT;
    selectedCover=constrain((x-coversBlock.r.x)/selCw,0,COVER_COUNT-1);
    display.startWrite();drawCovers();display.endWrite();
    return true;
  }

  // ---- commande monter/stop/descendre : toujours envoyee a HA ----
  if(y>=btnY){
    int cw=coversBlock.r.w/3;
    int i=constrain((x-coversBlock.r.x)/cw,0,2);
    CoverCard& c=covers[selectedCover];
    display.startWrite();drawCovers(-1,i);display.endWrite();
    const char* s=i==0?"open_cover":i==1?"stop_cover":"close_cover";
    status((String(c.title)+" : "+(i==0?"montee...":i==1?"stop...":"descente...")).c_str());
    bool ok=haCall("cover",s,c.entity);
    status(ok?"OK":"Erreur commande HA");delay(300);
    refreshCover(c);
    display.startWrite();drawCovers();display.endWrite();
  }
  return true;
}

bool touchClimate(int x,int y){
  if(!inRect(climate.r,x,y))return false;

  int modeY=climate.r.y+climate.titleH;
  int tempY=modeY+climate.modeH;
  int infoY=tempY+climate.tempH;

  // ---- ligne mode : Chauffage / Clim / Off ----
  if(y>=modeY&&y<tempY){
    int i=constrain((x-climate.r.x)/climate.cellW,0,2);
    display.startWrite();drawClimate(i,-1);display.endWrite();
    status((String("Mode : ")+HVAC_LABELS[i]+"...").c_str());
    bool ok=haCall("climate","set_hvac_mode",ENTITY_CLIMATE,String(",\"hvac_mode\":\"")+HVAC_MODES[i]+"\"");
    if(ok)hvacMode=HVAC_MODES[i];
    status(ok?"OK":"Erreur commande HA");delay(300);
    refreshClimate();
    display.startWrite();drawClimate();display.endWrite();
    status(page==1?"Page 1/2":"Page 2/2");
    return true;
  }

  // ---- ligne consigne : - / valeur / + ----
  if(y>=tempY&&y<infoY){
    int i=constrain((x-climate.r.x)/climate.cellW,0,2);
    if(i!=1){
      float newTemp=constrain(targetTemp+(i==0?-TEMP_STEP:TEMP_STEP),TEMP_MIN,TEMP_MAX);
      display.startWrite();drawClimate(-1,i);display.endWrite();
      status(i==0?"Temperature -...":"Temperature +...");
      char buf[8];snprintf(buf,sizeof(buf),"%.1f",newTemp);
      bool ok=haCall("climate","set_temperature",ENTITY_CLIMATE,String(",\"temperature\":")+buf);
      if(ok){targetTemp=newTemp;hasTarget=true;}
      status(ok?"OK":"Erreur commande HA");delay(300);
    }
    // Redessine uniquement la carte climatisation, sans effacement plein ecran.
    display.startWrite();drawClimate();display.endWrite();
    status(page==1?"Page 1/2":"Page 2/2");
    return true;
  }

  return true; // toucher sur le titre : on ignore mais on consomme l'evenement
}

bool touchSpotify(int x,int y){
  if(!inRect(spotifyCard.r,x,y))return false;

  int by=spotifyCard.r.y+spotifyCard.titleH+spotifyCard.infoH;
  if(y<by)return false;

  int cw=spotifyCard.r.w/3;
  int i=constrain((x-spotifyCard.r.x)/cw,0,2);

  display.startWrite();drawSpotify(i);display.endWrite();

  const char* service = i==0 ? "media_previous_track" :
                        i==1 ? "media_play_pause" :
                               "media_next_track";
  const char* label = i==0 ? "Spotify : precedent..." :
                      i==1 ? "Spotify : pause/lecture..." :
                             "Spotify : suivant...";
  status(label);

  bool ok=haCall("media_player",service,ENTITY_SPOTIFY);
  status(ok?"OK":"Erreur commande HA");
  delay(300);

  // Pas de rafraichissement périodique : on relit uniquement après une commande.
  refreshSpotify();
  display.startWrite();drawSpotify();display.endWrite();
  status(page==1?"Page 1/2":"Page 2/2");
  return true;
}

bool touchWifi(int x,int y){
  if(hasVoucher||!inRect(wifiCard.r,x,y))return false;
  int by=wifiCard.r.y+wifiCard.titleH;
  if(y<by)return false;
  display.startWrite();drawWifi(true);display.endWrite();
  status("Creation voucher Wifi...");
  bool ok=haCall("input_button","press",ENTITY_WIFI_BUTTON);
  if(ok){delay(2000);refreshVoucher();status(hasVoucher?("Code : "+voucherCode).c_str():"Code introuvable");}
  else status("Erreur commande HA");
  drawPage();delay(300);return true;
}

// ---------- Setup / Loop ----------
void setup(){
  Serial.begin(115200);
  display.loadFont(SpaceMono26);
  pinMode(G37,INPUT_PULLUP); pinMode(G39,INPUT_PULLUP);
  display.begin();display.setRotation(0);display.setEpdMode(epd_mode_t::epd_fastest);
  setupLayout();connectWifi();refreshClimate();
  for(auto a:actions)refreshAction(*a);
  for(auto &c:covers)refreshCover(c);
  refreshVoucher();refreshSpotify();resetActivityTimer();drawPage();
}

void loop(){
  if(DEEPSLEEP_ENABLED&&millis()-lastActivity>=DEEPSLEEP_TIMEOUT_MS)enterDeepSleep();
  if(pageButtons())return;

  lgfx::touch_point_t tp;
  if(display.getTouch(&tp)){
    resetActivityTimer();
    if(page==1){
      for(auto a:actions)if(touchAction(*a,tp.x,tp.y))return;
      if(touchSpotify(tp.x,tp.y))return;
      if(touchClimate(tp.x,tp.y))return;
    }else{
      if(touchCovers(tp.x,tp.y))return;
      touchWifi(tp.x,tp.y);
    }
  }
  delay(50);
}
