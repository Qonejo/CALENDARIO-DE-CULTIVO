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
#define BTN_TOUCH  5

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

enum CampoConfig {
  CAMPO_VEG_DIA, CAMPO_VEG_MES, CAMPO_VEG_ANIO,
  CAMPO_FLOR_DIA, CAMPO_FLOR_MES, CAMPO_FLOR_ANIO,
  CAMPO_VOLVER, TOTAL_CAMPOS
};

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

bool wifiConectado = false;
unsigned long ultimoIntentoNtpMs = 0, ultimoIntentoWifiMs = 0;
const long GMT_OFFSET_SEC = -21600;
const int DAYLIGHT_OFFSET_SEC = 0;

volatile int8_t deltaEncoder = 0;
uint8_t ultimoEstadoAB = 0;
int ultimoCLK = HIGH, ultimoDT = HIGH;
bool ultimoEstadoBtn = false;
unsigned long ultimoMsEncoder = 0, ultimoBtnMs = 0, ultimoMsPaso = 0;

const unsigned long DEBOUNCE_ENCODER_MS = 2;
const unsigned long STEP_GAP_MS = 3;
const unsigned long DEBOUNCE_BTN_MS = 70;

const char* MESES[] = {"Ene","Feb","Mar","Abr","May","Jun","Jul","Ago","Sep","Oct","Nov","Dic"};
const char* DIA_SEM[] = {"Dom","Lun","Mar","Mie","Jue","Vie","Sab"};

#define CARD_X 4
#define CARD_W 155
#define CARD_H 62
#define CARD_Y0 8
#define CARD_Y1 78
#define CARD_Y2 148
#define CFG_X 255
#define CFG_Y 6
#define CFG_W 58
#define CFG_H 24
#define BTN_VOLVER_X 10
#define BTN_VOLVER_Y 200
#define BTN_VOLVER_W 100
#define BTN_VOLVER_H 30

bool esBisiesto(int anio) { return ((anio % 4 == 0 && anio % 100 != 0) || (anio % 400 == 0)); }
int diasEnMes(int mes, int anio) {
  if (mes < 1 || mes > 12) return 30;
  const int dm[] = {31,28,31,30,31,30,31,31,30,31,30,31};
  if (mes == 2 && esBisiesto(anio)) return 29;
  return dm[mes - 1];
}

bool fechaValida(const FechaConfig &f) {
  if (f.a < 2020 || f.a > 2100 || f.m < 1 || f.m > 12) return false;
  return f.d >= 1 && f.d <= diasEnMes(f.m, f.a);
}

time_t fechaToTime(const FechaConfig &f) {
  struct tm t = {0};
  t.tm_year = f.a - 1900;
  t.tm_mon = f.m - 1;
  t.tm_mday = f.d;
  t.tm_hour = 12;
  t.tm_isdst = -1;
  return mktime(&t);
}

void normalizarFecha(FechaConfig &f) {
  f.a = constrain(f.a, 2020, 2100);
  f.m = constrain(f.m, 1, 12);
  f.d = constrain(f.d, 1, diasEnMes(f.m, f.a));
}

void validarFechasConfig() {
  normalizarFecha(fechaVeg);
  normalizarFecha(fechaFlor);
  if (fechaToTime(fechaFlor) <= fechaToTime(fechaVeg)) {
    fechaFlor = fechaVeg;
    fechaFlor.m += 1;
    if (fechaFlor.m > 12) { fechaFlor.m = 1; fechaFlor.a++; }
    normalizarFecha(fechaFlor);
  }
}

InfoCultivo calcularCultivo(int d, int m, int a) {
  InfoCultivo r = {"PLANTULA", MI_CIAN, 100, 0.2f, 0, false, false};
  FechaConfig hoy = {d, m + 1, a};
  time_t tHoy = fechaToTime(hoy);
  time_t tVeg = fechaToTime(fechaVeg);
  time_t tFlor = fechaToTime(fechaFlor);

  int diasVida = (int)((tHoy - tVeg) / 86400);
  if (diasVida < 0) diasVida = 0;
  int diasVegetacion = (int)((tFlor - tVeg) / 86400);
  if (diasVegetacion < 1) diasVegetacion = 1;

  struct tm* infoFecha = localtime(&tHoy);
  r.tocaFertilizar = (infoFecha && infoFecha->tm_wday == 0);
  r.tocaRegar = (diasVida % 3 == 0);

  int ppmMin = 100, ppmMax = 250;
  float prog = 0;

  if (tHoy < tVeg) {
    r.fase = "PLANTULA"; r.colorFase = MI_CIAN;
    prog = 0;
  } else if (tHoy < tFlor) {
    r.fase = "VEGETA"; r.colorFase = MI_VERDE;
    ppmMin = 300; ppmMax = 450;
    prog = (float)diasVida / max(1, diasVegetacion);
  } else {
    int diasFlor = (int)((tHoy - tFlor) / 86400);
    if (diasFlor <= 47) {
      r.fase = "FLOR T"; r.colorFase = MI_AMARILLO;
      ppmMin = 500; ppmMax = 1100;
      prog = (float)diasFlor / 47.0f;
    } else {
      r.fase = "FLOR A"; r.colorFase = MI_ROJO;
      ppmMin = 1200; ppmMax = 1600;
      prog = (float)(diasFlor - 48) / 20.0f;
    }
  }

  prog = constrain(prog, 0.0f, 1.0f);
  r.progreso = prog;
  r.ppm = (int)(ppmMin + (ppmMax - ppmMin) * prog);
  r.mS = r.ppm / 500.0f;
  return r;
}

void fechaConDelta(int delta, int &d, int &m, int &a) {
  struct tm t = {0};
  t.tm_mday = diaHoy + delta;
  t.tm_mon = mesHoy;
  t.tm_year = anioHoy - 1900;
  t.tm_hour = 12;
  mktime(&t);
  d = t.tm_mday; m = t.tm_mon; a = 1900 + t.tm_year;
}

void drawCampo(int x, int y, int w, int h, int idx, int valor) {
  uint16_t borde = (indiceCfg == idx) ? MI_NARANJA : MI_GRIS;
  if (estadoUI == UI_EDITANDO && indiceCfg == idx) borde = MI_AMARILLO;
  tft.drawRoundRect(x, y, w, h, 4, borde);
  tft.setTextColor(MI_BLANCO); tft.setTextSize(2); tft.setCursor(x + 8, y + 6);
  tft.printf("%02d", valor);
}

void dibujarCalendario() { /* igual estilo */
  tft.fillScreen(MI_NEGRO);
  for (int i = -1; i <= 1; i++) {
    int y = (i == -1) ? CARD_Y0 : (i == 0 ? CARD_Y1 : CARD_Y2), d, m, a;
    fechaConDelta(offsetVer + i, d, m, a);
    struct tm t = {0}; t.tm_mday = d; t.tm_mon = m; t.tm_year = a - 1900; t.tm_hour = 12;
    int diaSem = localtime(&mktime(&t))->tm_wday;
    bool foco = (i == 0);
    tft.fillRect(CARD_X, y, CARD_W, CARD_H, foco ? MI_AZUL_OSC : MI_NEGRO);
    tft.drawRect(CARD_X, y, CARD_W, CARD_H, foco ? MI_BLANCO : MI_GRIS);
    tft.setTextSize(1); tft.setTextColor(foco ? MI_AMARILLO : MI_GRIS); tft.setCursor(CARD_X + 6, y + 6);
    if (offsetVer + i == 0) tft.print("HOY"); else if (offsetVer + i < 0) tft.print("AYER"); else tft.print("MANANA");
    tft.setTextSize(2); tft.setTextColor(MI_BLANCO); tft.setCursor(CARD_X + 6, y + 28); tft.print(DIA_SEM[diaSem]);
    tft.setTextSize(3); tft.setTextColor(foco ? MI_AMARILLO : MI_BLANCO); tft.setCursor(CARD_X + CARD_W - 45, y + 12); tft.print(d);
    tft.setTextSize(1); tft.setTextColor(foco ? MI_BLANCO : MI_GRIS); tft.setCursor(CARD_X + CARD_W - 55, y + 45); tft.printf("%s %d", MESES[m], a);
  }
  int d, m, a; fechaConDelta(offsetVer, d, m, a); InfoCultivo cult = calcularCultivo(d, m, a);
  tft.setTextSize(2); tft.setTextColor(cult.colorFase); tft.setCursor(168, 78); tft.print(cult.fase);
  tft.setTextColor(MI_BLANCO); tft.setCursor(168, 103); tft.printf("%d ppm", cult.ppm);
  tft.setCursor(168, 123); tft.printf("%.1f mS", cult.mS);
  if (cult.tocaRegar) { tft.setTextColor(MI_CIAN); tft.setCursor(168, 20); tft.print("TOCA REGAR"); }
  if (cult.tocaFertilizar) { tft.setTextColor(MI_AMARILLO); tft.setCursor(168, 35); tft.print("FERTILIZAR"); }
  tft.drawRect(168, 160, 140, 14, MI_GRIS);
  tft.fillRect(170, 162, (int)(136 * cult.progreso), 10, cult.colorFase);
  tft.drawRoundRect(CFG_X, CFG_Y, CFG_W, CFG_H, 4, MI_NARANJA);
  tft.setTextSize(1); tft.setTextColor(MI_NARANJA); tft.setCursor(CFG_X + 8, CFG_Y + 8); tft.print("CONFIG");
}

void dibujarConfiguracion() {
  tft.fillScreen(MI_NEGRO);
  tft.setTextColor(MI_BLANCO); tft.setTextSize(2); tft.setCursor(10, 12); tft.print("MENU CONFIG");
  tft.setTextSize(1); tft.setCursor(10, 40); tft.print("VEGETACION");
  tft.setCursor(10, 110); tft.print("FLORACION");
  tft.setCursor(10, 62); tft.print("Dia"); tft.setCursor(94, 62); tft.print("Mes"); tft.setCursor(178, 62); tft.print("Anio");
  tft.setCursor(10, 132); tft.print("Dia"); tft.setCursor(94, 132); tft.print("Mes"); tft.setCursor(178, 132); tft.print("Anio");
  drawCampo(10, 74, 64, 30, CAMPO_VEG_DIA, fechaVeg.d);
  drawCampo(94, 74, 64, 30, CAMPO_VEG_MES, fechaVeg.m);
  uint16_t b = (indiceCfg == CAMPO_VEG_ANIO ? (estadoUI == UI_EDITANDO ? MI_AMARILLO : MI_NARANJA) : MI_GRIS);
  tft.drawRoundRect(178, 74, 84, 30, 4, b); tft.setTextSize(2); tft.setTextColor(MI_BLANCO); tft.setCursor(186, 80); tft.print(fechaVeg.a);
  drawCampo(10, 144, 64, 30, CAMPO_FLOR_DIA, fechaFlor.d);
  drawCampo(94, 144, 64, 30, CAMPO_FLOR_MES, fechaFlor.m);
  b = (indiceCfg == CAMPO_FLOR_ANIO ? (estadoUI == UI_EDITANDO ? MI_AMARILLO : MI_NARANJA) : MI_GRIS);
  tft.drawRoundRect(178, 144, 84, 30, 4, b); tft.setCursor(186, 150); tft.print(fechaFlor.a);
  uint16_t colVolver = (indiceCfg == CAMPO_VOLVER) ? (estadoUI == UI_EDITANDO ? MI_AMARILLO : MI_NARANJA) : MI_GRIS;
  tft.drawRoundRect(BTN_VOLVER_X, BTN_VOLVER_Y, BTN_VOLVER_W, BTN_VOLVER_H, 4, colVolver);
  tft.setTextColor(MI_NARANJA); tft.setTextSize(1); tft.setCursor(36, 210); tft.print("VOLVER");
}

void guardarFechasPrefs() {
  prefs.putInt("vegDia", fechaVeg.d); prefs.putInt("vegMes", fechaVeg.m); prefs.putInt("vegAnio", fechaVeg.a);
  prefs.putInt("florDia", fechaFlor.d); prefs.putInt("florMes", fechaFlor.m); prefs.putInt("florAnio", fechaFlor.a);
}

void editarCampo(int dir) {
  switch (indiceCfg) {
    case CAMPO_VEG_DIA: fechaVeg.d += dir; break; case CAMPO_VEG_MES: fechaVeg.m += dir; break; case CAMPO_VEG_ANIO: fechaVeg.a += dir; break;
    case CAMPO_FLOR_DIA: fechaFlor.d += dir; break; case CAMPO_FLOR_MES: fechaFlor.m += dir; break; case CAMPO_FLOR_ANIO: fechaFlor.a += dir; break;
  }
  validarFechasConfig();
}

void aplicarPasoEncoder(int8_t dir) {
  if (!dir) return;
  if (estadoUI == UI_CALENDARIO) offsetVer += dir;
  else if (estadoUI == UI_CONFIG) indiceCfg = (indiceCfg + (dir > 0 ? 1 : -1) + TOTAL_CAMPOS) % TOTAL_CAMPOS;
  else editarCampo(dir);
  necesitaRedibujar = true;
}

void manejarConfirmacion() {
  if (estadoUI == UI_CALENDARIO) { estadoUI = UI_CONFIG; indiceCfg = CAMPO_VEG_DIA; }
  else if (estadoUI == UI_CONFIG) {
    if (indiceCfg == CAMPO_VOLVER) { validarFechasConfig(); guardarFechasPrefs(); estadoUI = UI_CALENDARIO; }
    else estadoUI = UI_EDITANDO;
  } else {
    validarFechasConfig(); guardarFechasPrefs();
    if (indiceCfg < CAMPO_FLOR_ANIO) indiceCfg++; else indiceCfg = CAMPO_VOLVER;
    estadoUI = UI_CONFIG;
  }
  necesitaRedibujar = true;
}

int mesDesdeTexto(const char* m) { const char* en[] = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"}; for (int i=0;i<12;i++) if(!strcmp(m,en[i])) return i; return 0; }
void actualizarEncoder(){ unsigned long a=millis(); int clk=digitalRead(ENC_S1),dt=digitalRead(ENC_S2); if((clk!=ultimoCLK||dt!=ultimoDT)&&(a-ultimoMsEncoder>=DEBOUNCE_ENCODER_MS)){ ultimoMsEncoder=a; uint8_t e=(clk<<1)|dt; uint8_t tr=(ultimoEstadoAB<<2)|e; int8_t p=0; if(tr==0b1101||tr==0b0100||tr==0b0010||tr==0b1011)p=1; else if(tr==0b1110||tr==0b0111||tr==0b0001||tr==0b1000)p=-1; if(p!=0&&(a-ultimoMsPaso>=STEP_GAP_MS)){ deltaEncoder+=p; ultimoMsPaso=a;} ultimoEstadoAB=e; ultimoCLK=clk; ultimoDT=dt;} bool eb=(digitalRead(BTN_TOUCH)==HIGH); if(eb!=ultimoEstadoBtn&&(a-ultimoBtnMs>=DEBOUNCE_BTN_MS)){ ultimoBtnMs=a; ultimoEstadoBtn=eb; if(eb)manejarConfirmacion(); } }
void consumirEncoder(){ int8_t p=deltaEncoder; if(!p)return; deltaEncoder=0; while(p>0){aplicarPasoEncoder(1);p--;} while(p<0){aplicarPasoEncoder(-1);p++;} }

void actualizarFechaSiEsPosible() {
  if (WiFi.status() != WL_CONNECTED) { wifiConectado = false; if (millis()-ultimoIntentoWifiMs>30000UL){ WiFi.begin(SSID, PASSWORD); ultimoIntentoWifiMs=millis(); } return; }
  wifiConectado = true;
  struct tm ti;
  if (getLocalTime(&ti, 50)) {
    int nd = ti.tm_mday, nm = ti.tm_mon, na = 1900 + ti.tm_year;
    if (nd != diaHoy || nm != mesHoy || na != anioHoy) { diaHoy = nd; mesHoy = nm; anioHoy = na; necesitaRedibujar = true; }
    return;
  }
  if (millis() - ultimoIntentoNtpMs > 600000UL) { configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, "pool.ntp.org", "time.nist.gov"); ultimoIntentoNtpMs = millis(); }
}

void setup() {
  Serial.begin(115200); SPI.begin(12, 13, 11);
  pinMode(ENC_S1, INPUT_PULLUP); pinMode(ENC_S2, INPUT_PULLUP); pinMode(BTN_TOUCH, INPUT);
  ultimoCLK=digitalRead(ENC_S1); ultimoDT=digitalRead(ENC_S2); ultimoEstadoAB=(ultimoCLK<<1)|ultimoDT; ultimoEstadoBtn=(digitalRead(BTN_TOUCH)==HIGH);
  tft.init(240, 320); tft.setRotation(1); tft.invertDisplay(false);

  prefs.begin("cultivo", false);
  fechaVeg.d = prefs.getInt("vegDia", 10); fechaVeg.m = prefs.getInt("vegMes", 7); fechaVeg.a = prefs.getInt("vegAnio", 2026);
  fechaFlor.d = prefs.getInt("florDia", 15); fechaFlor.m = prefs.getInt("florMes", 9); fechaFlor.a = prefs.getInt("florAnio", 2026);
  validarFechasConfig();

  WiFi.begin(SSID, PASSWORD);
  unsigned long inicioIntento = millis(); while (WiFi.status() != WL_CONNECTED && millis() - inicioIntento < 15000) delay(120);
  wifiConectado = (WiFi.status() == WL_CONNECTED);
  if (wifiConectado) { configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, "pool.ntp.org", "time.nist.gov"); ultimoIntentoNtpMs = millis(); }

  struct tm ti;
  if (wifiConectado && getLocalTime(&ti)) { diaHoy = ti.tm_mday; mesHoy = ti.tm_mon; anioHoy = 1900 + ti.tm_year; }
  else {
    char mesTxt[4] = {0}; int diaComp = 1, anioComp = 2024;
    if (sscanf(__DATE__, "%3s %d %d", mesTxt, &diaComp, &anioComp) == 3) { diaHoy = diaComp; mesHoy = mesDesdeTexto(mesTxt); anioHoy = anioComp; }
  }
  necesitaRedibujar = true;
}

void loop() {
  actualizarFechaSiEsPosible(); actualizarEncoder(); consumirEncoder();
  if (necesitaRedibujar) { if (estadoUI == UI_CALENDARIO) dibujarCalendario(); else dibujarConfiguracion(); necesitaRedibujar = false; }
}
