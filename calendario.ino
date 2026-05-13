#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>
#include <WiFi.h>
#include <time.h>
#include <Preferences.h>

#define TFT_CS    10
#define TFT_RST    9
#define TFT_DC     8
#define ENC_S1     2
#define ENC_S2     3
#define BTN_KEY    4
#define BTN_TTP223 5

#define MI_NEGRO    0x0000
#define MI_BLANCO   0xFFFF
#define MI_VERDE    0x07E0
#define MI_ROJO     0xF800
#define MI_CIAN     0x07FF
#define MI_NARANJA  0xFBE0
#define MI_GRIS     0x4208
#define MI_AMARILLO 0xFFE0
#define MI_AZUL_OSC 0x2945

struct FechaConfig { int d; int m; int a; };
struct InfoCultivo {
  const char* fase;
  uint16_t colorFase;
  int ppm;
  float mS;
  float progreso;
  bool tocaRegar;
  bool tocaFertilizar;
};

enum EstadoUI { UI_CALENDARIO, UI_CONFIG, UI_EDITANDO };
enum EtapaPlanta { ET_PLANTULA, ET_VEGETACION, ET_PREFLORA, ET_INICIO_FLORA, ET_MEDIA_FLORA, ET_FLORA_AVANZADA, ET_COSECHA };

Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);
Preferences prefs;

const char* SSID = "IZZI-367E";
const char* PASSWORD = "ehwa3pX7btcw";

int diaHoy = 1, mesHoy = 0, anioHoy = 2024;
FechaConfig fechaVeg, fechaFlor;

int offsetVer = 0;
bool necesitaRedibujar = true;
EstadoUI estadoUI = UI_CALENDARIO;
int indiceCfg = 0;

bool modoPlanta = false;
unsigned long ultimaInteraccion = 0;
unsigned long ultimoFramePlanta = 0;
unsigned long ultimoIntentoNtpMs = 0, ultimoIntentoWifiMs = 0;

bool wifiConectado = false;
const long GMT_OFFSET_SEC = -21600;
const int DAYLIGHT_OFFSET_SEC = 0;

int16_t deltaEncoder = 0;
uint8_t ultimoEstadoAB = 0;
int8_t acumuladorEncoder = 0;
bool ultimoEstadoKey = false, ultimoEstadoTtp = false;
unsigned long ultimoMsEncoder = 0, ultimoKeyMs = 0, ultimoTtpMs = 0;

const unsigned long DEBOUNCE_ENCODER_MS = 2;
const unsigned long DEBOUNCE_BTN_MS = 70;
const unsigned long TIMEOUT_MODO_PLANTA_MS = 1800000UL;
const unsigned long FRAME_PLANTA_MS = 80;

const int8_t TABLA_ENCODER[16] = {0,-1,1,0, 1,0,0,-1, -1,0,0,1, 0,1,-1,0};
const char* MESES[] = {"Ene","Feb","Mar","Abr","May","Jun","Jul","Ago","Sep","Oct","Nov","Dic"};
const char* DIA_SEM[] = {"Dom","Lun","Mar","Mie","Jue","Vie","Sab"};
const char* ETAPAS_TXT[] = {"PLANTULA","VEGETACION","PREFLORA","INICIO FLORA","MEDIA FLORA","FLORA AVANZADA","COSECHA"};

#define CARD_X 4
#define CARD_W 155
#define CARD_H 62
#define CARD_Y0 8
#define CARD_Y1 78
#define CARD_Y2 148
#define CAMPO_VOLVER 6
#define TOTAL_CAMPOS 7

bool esBisiesto(int anio){ return ((anio%4==0&&anio%100!=0)||(anio%400==0)); }
int diasEnMes(int mes,int anio){ const int dm[]={31,28,31,30,31,30,31,31,30,31,30,31}; if(mes<1||mes>12) return 30; return (mes==2&&esBisiesto(anio))?29:dm[mes-1]; }
time_t fechaToTime(const FechaConfig &f){ struct tm t={0}; t.tm_year=f.a-1900; t.tm_mon=f.m-1; t.tm_mday=f.d; t.tm_hour=12; t.tm_isdst=-1; return mktime(&t);} 

void normalizarFecha(FechaConfig &f){ f.a=constrain(f.a,2020,2100); f.m=constrain(f.m,1,12); f.d=constrain(f.d,1,diasEnMes(f.m,f.a)); }
void validarFechasConfig(){ normalizarFecha(fechaVeg); normalizarFecha(fechaFlor); if(fechaToTime(fechaFlor)<=fechaToTime(fechaVeg)){ fechaFlor=fechaVeg; fechaFlor.m++; if(fechaFlor.m>12){fechaFlor.m=1;fechaFlor.a++;} normalizarFecha(fechaFlor);} }

InfoCultivo calcularCultivo(int d,int m,int a){
  InfoCultivo r={"PLANTULA",MI_CIAN,100,0.2f,0,false,false};
  FechaConfig hoy={d,m+1,a}; time_t tHoy=fechaToTime(hoy), tVeg=fechaToTime(fechaVeg), tFlor=fechaToTime(fechaFlor);
  int diasVida=max(0,(int)((tHoy-tVeg)/86400)); int diasVegetacion=max(1,(int)((tFlor-tVeg)/86400));
  struct tm* infoFecha=localtime(&tHoy); r.tocaFertilizar=(infoFecha&&infoFecha->tm_wday==0); r.tocaRegar=(diasVida%3==0);
  int ppmMin=100,ppmMax=250; float prog=0;
  if(tHoy<tVeg){ r.fase="PLANTULA"; r.colorFase=MI_CIAN; }
  else if(tHoy<tFlor){ r.fase="VEGETA"; r.colorFase=MI_VERDE; ppmMin=300; ppmMax=450; prog=(float)diasVida/diasVegetacion; }
  else { int diasFlor=(int)((tHoy-tFlor)/86400); if(diasFlor<=47){ r.fase="FLOR T"; r.colorFase=MI_AMARILLO; ppmMin=500; ppmMax=1100; prog=diasFlor/47.0f; } else { r.fase="FLOR A"; r.colorFase=MI_ROJO; ppmMin=1200; ppmMax=1600; prog=(diasFlor-48)/20.0f; } }
  r.progreso=constrain(prog,0.0f,1.0f); r.ppm=(int)(ppmMin+(ppmMax-ppmMin)*r.progreso); r.mS=r.ppm/500.0f; return r;
}

EtapaPlanta etapaVisual(const InfoCultivo &c){
  if(strcmp(c.fase,"PLANTULA")==0) return ET_PLANTULA;
  if(strcmp(c.fase,"VEGETA")==0) return (c.progreso<0.45f)?ET_VEGETACION:ET_PREFLORA;
  if(strcmp(c.fase,"FLOR T")==0){ if(c.progreso<0.33f) return ET_INICIO_FLORA; if(c.progreso<0.75f) return ET_MEDIA_FLORA; return ET_FLORA_AVANZADA; }
  return (c.progreso>0.95f)?ET_COSECHA:ET_FLORA_AVANZADA;
}

void registrarInteraccion(){ ultimaInteraccion=millis(); if(modoPlanta){ modoPlanta=false; necesitaRedibujar=true; } }

void dibujarPlanta(EtapaPlanta et, const InfoCultivo &cult, uint32_t frame){
  tft.fillScreen(MI_NEGRO);
  int cx=160, baseY=200, sway=(frame/3)%3-1, breath=((frame/5)%2);
  tft.fillRect(cx-24, baseY, 48, 22, MI_GRIS); tft.drawRect(cx-24, baseY, 48, 22, MI_BLANCO);
  if(et==ET_COSECHA){ tft.drawLine(cx, baseY-20, cx, baseY, MI_VERDE); tft.drawLine(cx-6,baseY-6,cx+6,baseY-6,MI_ROJO); }
  else {
    int h=18+((int)et*12)+breath; tft.drawLine(cx, baseY-h, cx+sway, baseY, MI_VERDE);
    for(int i=0;i<=et+1;i++){ int y=baseY-h+10+i*9; tft.fillTriangle(cx, y, cx-10-sway, y+4, cx-4, y+8, MI_VERDE); tft.fillTriangle(cx, y+1, cx+10+sway, y+4, cx+4, y+8, MI_VERDE); }
    if(et>=ET_PREFLORA) for(int i=0;i<et;i++) tft.fillCircle(cx-8+(i*5), baseY-h+10+(i%3)*6, 2+(et>=ET_MEDIA_FLORA), MI_AMARILLO);
  }
  bool triste=cult.tocaRegar; bool hambre=cult.tocaFertilizar;
  tft.drawCircle(44,50,16,MI_BLANCO); tft.fillCircle(38,46,2,MI_BLANCO); tft.fillCircle(50,46,2,MI_BLANCO);
  if(triste) tft.drawLine(38,56,50,54,MI_BLANCO); else tft.drawLine(38,54,50,56,MI_BLANCO);
  if(hambre){ tft.drawRect(8,84,14,18,MI_NARANJA); tft.fillRect(11,88,8,10,MI_NARANJA); }
  if(triste){ tft.fillCircle(20,56+(frame%6),2,MI_CIAN); }
  tft.setTextColor(MI_VERDE); tft.setTextSize(2); tft.setCursor(8,8); tft.print("TAMAGOTCHI");
  tft.setTextColor(MI_BLANCO); tft.setTextSize(1); tft.setCursor(8,112); tft.printf("Etapa:%s", ETAPAS_TXT[et]);
  tft.setCursor(8,126); tft.printf("Agua:%s", triste?"BAJA":"OK");
  tft.setCursor(8,140); tft.printf("Felicidad:%d", triste?35:(hambre?70:95));
  tft.setCursor(8,154); tft.printf("PPM:%d", cult.ppm);
  tft.setCursor(8,168); tft.printf("mS:%.2f", cult.mS);
}

void fechaConDelta(int delta,int &d,int &m,int &a){ struct tm t={0}; t.tm_mday=diaHoy+delta; t.tm_mon=mesHoy; t.tm_year=anioHoy-1900; t.tm_hour=12; mktime(&t); d=t.tm_mday; m=t.tm_mon; a=1900+t.tm_year; }
void dibujarCalendario(){ tft.fillScreen(MI_NEGRO); for(int i=-1;i<=1;i++){int y=(i==-1)?CARD_Y0:(i==0?CARD_Y1:CARD_Y2),d,m,a; fechaConDelta(offsetVer+i,d,m,a); struct tm t={0}; t.tm_mday=d;t.tm_mon=m;t.tm_year=a-1900;t.tm_hour=12; time_t tt=mktime(&t); int diaSem=localtime(&tt)->tm_wday; bool foco=(i==0); tft.fillRect(CARD_X,y,CARD_W,CARD_H,foco?MI_AZUL_OSC:MI_NEGRO); tft.drawRect(CARD_X,y,CARD_W,CARD_H,foco?MI_BLANCO:MI_GRIS); tft.setTextSize(1); tft.setTextColor(foco?MI_AMARILLO:MI_GRIS); tft.setCursor(CARD_X+6,y+6); if(offsetVer+i==0)tft.print("HOY"); else if(offsetVer+i<0)tft.print("AYER"); else tft.print("MANANA"); tft.setTextSize(2); tft.setTextColor(MI_BLANCO); tft.setCursor(CARD_X+6,y+28); tft.print(DIA_SEM[diaSem]); tft.setTextSize(3); tft.setTextColor(foco?MI_AMARILLO:MI_BLANCO); tft.setCursor(CARD_X+CARD_W-45,y+12); tft.print(d); tft.setTextSize(1); tft.setTextColor(foco?MI_BLANCO:MI_GRIS); tft.setCursor(CARD_X+CARD_W-55,y+45); tft.printf("%s %d",MESES[m],a);} int d,m,a; fechaConDelta(offsetVer,d,m,a); InfoCultivo cult=calcularCultivo(d,m,a); tft.setTextSize(2); tft.setTextColor(cult.colorFase); tft.setCursor(168,78); tft.print(cult.fase); tft.setTextColor(MI_BLANCO); tft.setCursor(168,103); tft.printf("%d ppm",cult.ppm); tft.setCursor(168,123); tft.printf("%.1f mS",cult.mS); if(cult.tocaRegar){ tft.setTextColor(MI_CIAN); tft.setCursor(168,20); tft.print("TOCA REGAR"); } if(cult.tocaFertilizar){ tft.setTextColor(MI_AMARILLO); tft.setCursor(168,35); tft.print("FERTILIZAR"); } tft.drawRect(168,160,140,14,MI_GRIS); tft.fillRect(170,162,(int)(136*cult.progreso),10,cult.colorFase); }

void actualizarEncoder(){
  unsigned long ahora=millis(); uint8_t estadoActual=(digitalRead(ENC_S1)<<1)|digitalRead(ENC_S2);
  if(estadoActual!=ultimoEstadoAB && (ahora-ultimoMsEncoder)>=DEBOUNCE_ENCODER_MS){ ultimoMsEncoder=ahora; uint8_t tr=(ultimoEstadoAB<<2)|estadoActual; int8_t mov=TABLA_ENCODER[tr&0x0F]; if(mov){ acumuladorEncoder+=mov; if(acumuladorEncoder>=4){ deltaEncoder++; acumuladorEncoder=0; registrarInteraccion(); } else if(acumuladorEncoder<=-4){ deltaEncoder--; acumuladorEncoder=0; registrarInteraccion(); }} ultimoEstadoAB=estadoActual; }
  bool key=(digitalRead(BTN_KEY)==HIGH), ttp=(digitalRead(BTN_TTP223)==HIGH);
  if(key!=ultimoEstadoKey && (ahora-ultimoKeyMs)>=DEBOUNCE_BTN_MS){ ultimoKeyMs=ahora; ultimoEstadoKey=key; if(key){ registrarInteraccion(); estadoUI = (estadoUI==UI_CALENDARIO)?UI_CONFIG:UI_CALENDARIO; necesitaRedibujar=true; }}
  if(ttp!=ultimoEstadoTtp && (ahora-ultimoTtpMs)>=DEBOUNCE_BTN_MS){ ultimoTtpMs=ahora; ultimoEstadoTtp=ttp; if(ttp){ registrarInteraccion(); estadoUI = UI_CALENDARIO; necesitaRedibujar=true; }}
}

void setup(){
  Serial.begin(115200); SPI.begin(12,13,11); pinMode(ENC_S1,INPUT_PULLUP); pinMode(ENC_S2,INPUT_PULLUP); pinMode(BTN_KEY,INPUT); pinMode(BTN_TTP223,INPUT);
  tft.init(240,320); tft.setRotation(1); tft.invertDisplay(false);
  prefs.begin("cultivo", false);
  fechaVeg.d=prefs.getInt("vegDia",10); fechaVeg.m=prefs.getInt("vegMes",7); fechaVeg.a=prefs.getInt("vegAnio",2026);
  fechaFlor.d=prefs.getInt("florDia",15); fechaFlor.m=prefs.getInt("florMes",9); fechaFlor.a=prefs.getInt("florAnio",2026); validarFechasConfig();
  WiFi.begin(SSID,PASSWORD); configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, "pool.ntp.org", "time.nist.gov");
  struct tm ti; if(getLocalTime(&ti,3000)){ diaHoy=ti.tm_mday; mesHoy=ti.tm_mon; anioHoy=1900+ti.tm_year; }
  ultimoEstadoAB=(digitalRead(ENC_S1)<<1)|digitalRead(ENC_S2); ultimaInteraccion=millis(); necesitaRedibujar=true;
}

void loop(){
  actualizarEncoder();
  if (millis() - ultimaInteraccion > TIMEOUT_MODO_PLANTA_MS) modoPlanta = true;
  if(modoPlanta){
    int d,m,a; fechaConDelta(offsetVer,d,m,a); InfoCultivo cult=calcularCultivo(d,m,a); EtapaPlanta et=etapaVisual(cult);
    if(millis()-ultimoFramePlanta>=FRAME_PLANTA_MS){ ultimoFramePlanta=millis(); dibujarPlanta(et,cult,ultimoFramePlanta/FRAME_PLANTA_MS); }
  } else if(necesitaRedibujar){ dibujarCalendario(); necesitaRedibujar=false; }
}
