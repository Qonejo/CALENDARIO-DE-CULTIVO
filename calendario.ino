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

int16_t deltaEncoder = 0;
uint8_t ultimoEstadoAB = 0;
int8_t acumuladorEncoder = 0;
bool ultimoEstadoBtn = false;
unsigned long ultimoMsEncoder = 0, ultimoBtnMs = 0;

const unsigned long DEBOUNCE_ENCODER_MS = 2;
const unsigned long DEBOUNCE_BTN_MS = 70;

const int8_t TABLA_ENCODER[16] = {
   0, -1,  1,  0,
   1,  0,  0, -1,
  -1,  0,  0,  1,
   0,  1, -1,  0
};

const char* MESES[] = {"Ene","Feb","Mar","Abr","May","Jun","Jul","Ago","Sep","Oct","Nov","Dic"};
const char* DIA_SEM[] = {"Dom","Lun","Mar","Mie","Jue","Vie","Sab"};

#define CARD_X 4
#define CARD_W 155
#define CARD_H 62
#define CARD_Y0 8
#define CARD_Y1 78
#define CARD_Y2 148
#define BTN_VOLVER_X 10
#define BTN_VOLVER_Y 200
#define BTN_VOLVER_W 100
#define BTN_VOLVER_H 30

// ---- Modo planta (Tamagotchi / screensaver) ----
bool modoPlanta = false;
bool keyPresionado = false;
unsigned long tiempoInicioHold = 0;
const unsigned long HOLD_PLANTA_MS = 5000;
const unsigned long FRAME_PLANTA_MS = 100; // ~10 FPS
unsigned long ultimoFramePlanta = 0;
uint32_t framePlanta = 0;
bool plantaInicializada = false;
const char* ultimaFasePlanta = "";
int ultimoDiaPlanta = -1, ultimoMesPlanta = -1, ultimoAnioPlanta = -1;

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
    time_t tiempo = mktime(&t);
    int diaSem = localtime(&tiempo)->tm_wday;
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
  if (modoPlanta) return;
  if (estadoUI == UI_CALENDARIO) offsetVer += dir;
  else if (estadoUI == UI_CONFIG) indiceCfg = (indiceCfg + (dir > 0 ? 1 : -1) + TOTAL_CAMPOS) % TOTAL_CAMPOS;
  else editarCampo(dir);
  necesitaRedibujar = true;
}

void manejarConfirmacion() {
  if (modoPlanta) return;
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

void dibujarModoPlanta() {
  int d, m, a;
  fechaConDelta(offsetVer, d, m, a);
  InfoCultivo cult = calcularCultivo(d, m, a);

  int sway = ((framePlanta / 2) % 3) - 1;
  int respirar = ((framePlanta / 4) % 2);
  int brillo = ((framePlanta / 5) % 3);

  tft.fillScreen(MI_NEGRO);
  tft.fillRect(0, 190, 320, 50, MI_AZUL_OSC);

  int cx = 160;
  int baseY = 190;

  tft.fillRect(cx - 34, baseY, 68, 30, MI_GRIS);
  tft.drawRect(cx - 34, baseY, 68, 30, MI_BLANCO);

  int talloH = 22;
  if (strcmp(cult.fase, "PLANTULA") == 0) talloH = 24 + respirar;
  else if (strcmp(cult.fase, "VEGETA") == 0) talloH = 55 + respirar;
  else if (strcmp(cult.fase, "FLOR T") == 0) talloH = 75 + respirar;
  else talloH = 92 + respirar;

  int topY = baseY - talloH;
  tft.drawLine(cx, baseY, cx + sway, topY, MI_VERDE);

  if (strcmp(cult.fase, "PLANTULA") == 0) {
    tft.fillTriangle(cx + sway, topY + 6, cx - 12 + sway, topY + 14, cx - 2 + sway, topY + 18, MI_VERDE);
    tft.fillTriangle(cx + sway, topY + 6, cx + 12 + sway, topY + 14, cx + 2 + sway, topY + 18, MI_VERDE);
  } else {
    for (int i = 0; i < 4; i++) {
      int y = topY + 10 + i * 12;
      tft.drawLine(cx + sway, y, cx - 10 - (i % 2), y + 5, MI_VERDE);
      tft.drawLine(cx + sway, y + 2, cx + 10 + (i % 2), y + 7, MI_VERDE);
      tft.fillTriangle(cx - 10 - (i % 2), y + 5, cx - 18 - (i % 2), y + 8, cx - 12 - (i % 2), y + 12, MI_VERDE);
      tft.fillTriangle(cx + 10 + (i % 2), y + 7, cx + 18 + (i % 2), y + 10, cx + 12 + (i % 2), y + 14, MI_VERDE);
    }
  }

  if (strcmp(cult.fase, "FLOR T") == 0 || strcmp(cult.fase, "FLOR A") == 0) {
    uint16_t colFlor = (strcmp(cult.fase, "FLOR T") == 0) ? MI_AMARILLO : MI_NARANJA;
    int nFlores = (strcmp(cult.fase, "FLOR T") == 0) ? 5 : 11;
    for (int i = 0; i < nFlores; i++) {
      int fx = cx - 28 + (i * 6);
      int fy = topY + 10 + ((i * 9 + brillo * 2) % 42);
      int r = (strcmp(cult.fase, "FLOR T") == 0) ? 2 : 3;
      tft.fillCircle(fx, fy, r, colFlor);
    }
  }

  if (cult.progreso > 0.95f && strcmp(cult.fase, "FLOR A") == 0) {
    tft.drawLine(cx - 10, baseY - 8, cx + 12, baseY - 8, MI_ROJO);
    tft.setTextColor(MI_ROJO); tft.setTextSize(2); tft.setCursor(108, 12); tft.print("COSECHA");
  } else {
    tft.setTextColor(MI_CIAN); tft.setTextSize(2); tft.setCursor(82, 12); tft.print("MODO PLANTA");
  }

  tft.setTextSize(1);
  tft.setTextColor(cult.colorFase); tft.setCursor(12, 42); tft.printf("Fase: %s", cult.fase);
  tft.setTextColor(MI_BLANCO); tft.setCursor(12, 58); tft.printf("PPM: %d   mS: %.1f", cult.ppm, cult.mS);
  tft.setCursor(12, 74); tft.printf("Progreso: %d%%", (int)(cult.progreso * 100));
  if (cult.tocaRegar) { tft.setTextColor(MI_CIAN); tft.setCursor(12, 92); tft.print("TOCA REGAR"); }
  if (cult.tocaFertilizar) { tft.setTextColor(MI_AMARILLO); tft.setCursor(12, 106); tft.print("FERTILIZAR"); }
  tft.setTextColor(MI_GRIS); tft.setCursor(12, 224); tft.print("Hold KEY 5s para salir");
}

int mesDesdeTexto(const char* m) { const char* en[] = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"}; for (int i=0;i<12;i++) if(!strcmp(m,en[i])) return i; return 0; }
void actualizarEncoder() {
  unsigned long ahora = millis();
  uint8_t estadoActual = (digitalRead(ENC_S1) << 1) | digitalRead(ENC_S2);

  if (!modoPlanta && estadoActual != ultimoEstadoAB && (ahora - ultimoMsEncoder) >= DEBOUNCE_ENCODER_MS) {
    ultimoMsEncoder = ahora;
    uint8_t transicion = (ultimoEstadoAB << 2) | estadoActual;
    int8_t movimiento = TABLA_ENCODER[transicion & 0x0F];

    if (movimiento != 0) {
      acumuladorEncoder += movimiento;
      if (acumuladorEncoder >= 4) {
        deltaEncoder++;
        acumuladorEncoder = 0;
      } else if (acumuladorEncoder <= -4) {
        deltaEncoder--;
        acumuladorEncoder = 0;
      }
    }
    ultimoEstadoAB = estadoActual;
  }

  bool estadoBtn = (digitalRead(BTN_TOUCH) == HIGH);
  if (estadoBtn != ultimoEstadoBtn && (ahora - ultimoBtnMs) >= DEBOUNCE_BTN_MS) {
    ultimoBtnMs = ahora;
    ultimoEstadoBtn = estadoBtn;

    if (estadoBtn) {
      keyPresionado = true;
      tiempoInicioHold = ahora;
    } else {
      bool fueHold = keyPresionado && (ahora - tiempoInicioHold >= HOLD_PLANTA_MS);
      if (!fueHold && !modoPlanta) manejarConfirmacion();
      keyPresionado = false;
    }
  }

  if (keyPresionado && estadoBtn && (ahora - tiempoInicioHold >= HOLD_PLANTA_MS)) {
    modoPlanta = !modoPlanta;
    keyPresionado = false;
    plantaInicializada = false;
    if (!modoPlanta) necesitaRedibujar = true;
  }
}
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
  ultimoEstadoAB = (digitalRead(ENC_S1) << 1) | digitalRead(ENC_S2);
  ultimoEstadoBtn = (digitalRead(BTN_TOUCH) == HIGH);
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
  actualizarFechaSiEsPosible();
  actualizarEncoder();

  if (!modoPlanta) consumirEncoder();

  if (modoPlanta) {
    int d, m, a; fechaConDelta(offsetVer, d, m, a);
    InfoCultivo cult = calcularCultivo(d, m, a);
    bool etapaCambio = (!plantaInicializada) || strcmp(cult.fase, ultimaFasePlanta) != 0 || d != ultimoDiaPlanta || m != ultimoMesPlanta || a != ultimoAnioPlanta;
    if (etapaCambio || (millis() - ultimoFramePlanta >= FRAME_PLANTA_MS)) {
      ultimoFramePlanta = millis();
      framePlanta++;
      dibujarModoPlanta();
      ultimaFasePlanta = cult.fase;
      ultimoDiaPlanta = d; ultimoMesPlanta = m; ultimoAnioPlanta = a;
      plantaInicializada = true;
    }
  } else if (necesitaRedibujar) {
    if (estadoUI == UI_CALENDARIO) dibujarCalendario();
    else dibujarConfiguracion();
    necesitaRedibujar = false;
  }
}
