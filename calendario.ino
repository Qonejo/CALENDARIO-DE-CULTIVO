#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>
#include <SD.h>
#include <WiFi.h>
#include <esp_now.h>
#include <time.h>
#include <Preferences.h>

#define TFT_CS    10
#define TFT_RST    9
#define TFT_DC     8
#define ENC_S1     2
#define ENC_S2     3
#define BTN_KEY    4
#define TTP223_PIN  5
#define SD_CS       6

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

typedef struct struct_message {

    int lightHours;
    int darkHours;

    int daysVeg;
    int daysFlower;

    bool isVegetative;
    bool inLightMode;

    float progressPercent;

    int hour;
    int minute;
    int second;

} struct_message;

enum EstadoUI { UI_CALENDARIO, UI_CONFIG, UI_EDITANDO };

enum CampoConfig {
  CAMPO_VEG_DIA, CAMPO_VEG_MES, CAMPO_VEG_ANIO,
  CAMPO_FLOR_DIA, CAMPO_FLOR_MES, CAMPO_FLOR_ANIO,
  CAMPO_VOLVER, TOTAL_CAMPOS
};

Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);
Preferences prefs;

const char* CONFIG_SD_PATH = "/cultivo.cfg";
bool sdDisponible = false;

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
bool modoPlanta = false;
bool botonPresionado = false;
unsigned long ultimoFramePlanta = 0;
const unsigned long FRAME_MS = 80;
bool fondoPlantaDibujado = false;
bool ultimoEsDiaRender = true;
uint8_t framePlanta = 0;
bool animacionFeliz = false;
bool animacionRiego = false;
unsigned long inicioAnimacionFeliz = 0;
unsigned long inicioAnimacionRiego = 0;
bool touchActivoTTP223 = false;
bool ultimoEstadoTouch = false;
unsigned long ultimoTouchMs = 0;
uint8_t interaccionesPlanta = 0;
const unsigned long DURACION_ANIMACION_FELIZ_MS = 1200;
const unsigned long DURACION_ANIMACION_RIEGO_MS = 2500;
const unsigned long DEBOUNCE_TOUCH_MS = 70;
enum EstadoAnimoPlanta { ANIMO_FELIZ, ANIMO_TRISTE, ANIMO_DORMIDA, ANIMO_ESTRESADA };
struct StatsVivas { uint8_t agua, felicidad, salud, energia; };
struct StatsRPG { uint8_t genetica, vigor, resina, terpenos; };
StatsRPG statsRpg = {85, 85, 85, 85};
StatsVivas statsVivas = {100, 100, 100, 100};
char faseAnterior[10] = "";
char mensajeEvento[26] = "";
unsigned long msCambioFase = 0, msMensajeEvento = 0;
bool rpgInicializado = false;
struct_message outgoingData;
uint8_t macCentro[] = {
    0x94,
    0xA9,
    0x90,
    0x37,
    0x7A,
    0xEC
};
unsigned long lastEspNowSendMs = 0;

struct Particula { int x; int y; int oldX; int oldY; int vx; int vy; uint16_t color; bool activa; };
Particula viento[12];
Particula corazones[5];
Particula sparkle[10];
Particula lluvia[26];
Particula terpenicas[7];
unsigned long ultimoRpgMs = 0;
unsigned long ultimoGuardadoRpgMs = 0;

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

// Animacion pixel-art tipo GIF (sin decoder): sprites 32x32 en PROGMEM.
const uint8_t PROGMEM SPRITE_PLANTA_BASE[32 * 4] = {
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x80,0x00,
  0x00,0x03,0xC0,0x00,0x00,0x07,0xE0,0x00,0x00,0x03,0xC0,0x00,0x00,0x01,0x80,0x00,
  0x00,0x01,0x80,0x00,0x00,0x01,0x80,0x00,0x00,0x01,0x80,0x00,0x00,0x03,0xC0,0x00,
  0x00,0x07,0xE0,0x00,0x00,0x0F,0xF0,0x00,0x00,0x1F,0xF8,0x00,0x00,0x3F,0xFC,0x00,
  0x00,0x7F,0xFE,0x00,0x00,0x3F,0xFC,0x00,0x00,0x1F,0xF8,0x00,0x00,0x0F,0xF0,0x00,
  0x00,0x07,0xE0,0x00,0x00,0x03,0xC0,0x00,0x00,0x01,0x80,0x00,0x00,0x01,0x80,0x00,
  0x00,0x01,0x80,0x00,0x00,0x01,0x80,0x00,0x00,0x01,0x80,0x00,0x00,0x01,0x80,0x00,
  0x00,0x03,0xC0,0x00,0x00,0x03,0xC0,0x00,0x00,0x03,0xC0,0x00,0x00,0x00,0x00,0x00
};
const uint8_t PROGMEM SPRITE_OJOS_ABIERTOS[8] = {0x00,0x00,0x66,0x00,0x66,0x00,0x00,0x00};
const uint8_t PROGMEM SPRITE_OJOS_CERRADOS[8] = {0x00,0x00,0x7E,0x00,0x7E,0x00,0x00,0x00};
const uint8_t PROGMEM SPRITE_HOJAS[3][16] = {
  {0x18,0x00,0x3C,0x00,0x7E,0x00,0xFF,0x00,0x7E,0x00,0x3C,0x00,0x18,0x00,0x00,0x00},
  {0x0C,0x00,0x1E,0x00,0x3F,0x00,0x7F,0x80,0x3F,0x00,0x1E,0x00,0x0C,0x00,0x00,0x00},
  {0x30,0x00,0x78,0x00,0xFC,0x00,0xFE,0x00,0xFC,0x00,0x78,0x00,0x30,0x00,0x00,0x00}
};

void drawLeafCannabis(int x, int y, uint16_t color) {
  tft.drawLine(x, y + 11, x, y + 2, color);
  tft.drawPixel(x, y + 1, color);
  tft.drawPixel(x, y, color);

  for (int i = 0; i < 4; i++) {
    tft.drawLine(x, y + 9 - i * 2, x - (2 + i), y + 7 - i * 2, color);
    tft.drawLine(x, y + 9 - i * 2, x + (2 + i), y + 7 - i * 2, color);
  }

  tft.drawPixel(x - 6, y + 1, color); tft.drawPixel(x + 6, y + 1, color);
  tft.drawPixel(x - 5, y + 3, color); tft.drawPixel(x + 5, y + 3, color);
  tft.drawPixel(x - 4, y + 5, color); tft.drawPixel(x + 4, y + 5, color);
}

void drawCogolloPixel(int x, int y, uint16_t c1, uint16_t c2, uint16_t pistilo) {
  tft.drawFastHLine(x - 2, y, 5, c1);
  tft.drawFastHLine(x - 3, y + 1, 7, c1);
  tft.drawFastHLine(x - 3, y + 2, 7, c2);
  tft.drawFastHLine(x - 2, y + 3, 5, c2);
  tft.drawPixel(x - 1, y + 1, pistilo);
  tft.drawPixel(x + 1, y + 2, pistilo);
}

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

void dibujarFondoModoPlanta() {
  tft.fillScreen(MI_NEGRO);
  tft.drawLine(0, 190, 319, 190, MI_GRIS);
  tft.fillRect(0, 191, 320, 49, MI_AZUL_OSC);
}

void mostrarMensajeEvento(const char* txt) {
  strncpy(mensajeEvento, txt, sizeof(mensajeEvento) - 1);
  mensajeEvento[sizeof(mensajeEvento) - 1] = '\0';
  msMensajeEvento = millis();
}

void iniciarAnimacionFeliz() {
  animacionFeliz = true;
  inicioAnimacionFeliz = millis();
  interaccionesPlanta = min<uint8_t>(interaccionesPlanta + 1, 25);
  if (statsVivas.felicidad < 100) statsVivas.felicidad = min(100, statsVivas.felicidad + 3);
}

void iniciarAnimacionRiego() {
  animacionRiego = true;
  inicioAnimacionRiego = millis();
  interaccionesPlanta = min<uint8_t>(interaccionesPlanta + 2, 25);
  if (statsVivas.agua < 100) statsVivas.agua = min(100, statsVivas.agua + 6);
  if (statsVivas.felicidad < 100) statsVivas.felicidad = min(100, statsVivas.felicidad + 5);
}

void actualizarAnimacionesPlanta(unsigned long ahora) {
  if (animacionFeliz && (ahora - inicioAnimacionFeliz > DURACION_ANIMACION_FELIZ_MS)) animacionFeliz = false;
  if (animacionRiego && (ahora - inicioAnimacionRiego > DURACION_ANIMACION_RIEGO_MS)) animacionRiego = false;
}

void abrirMenuOpciones() {
  modoPlanta = false;
  fondoPlantaDibujado = false;
  estadoUI = UI_CONFIG;
  indiceCfg = CAMPO_VEG_DIA;
  necesitaRedibujar = true;
}

void procesarTouchTTP223(unsigned long ahora) {
  bool tocando = (digitalRead(TTP223_PIN) == HIGH);

  if (tocando != ultimoEstadoTouch && (ahora - ultimoTouchMs) >= DEBOUNCE_TOUCH_MS) {
    ultimoTouchMs = ahora;
    ultimoEstadoTouch = tocando;

    if (tocando) {
      touchActivoTTP223 = true;
    } else if (touchActivoTTP223) {
      abrirMenuOpciones();
      touchActivoTTP223 = false;
    }
  }
}


StatsVivas calcularStatsVivas(const InfoCultivo &cult) {
  StatsVivas s;
  s.agua = cult.tocaRegar ? 30 : 92;
  s.felicidad = cult.tocaRegar ? 50 : 88;
  bool ppmOk = (!strcmp(cult.fase, "PLANTULA") && cult.ppm >= 100 && cult.ppm <= 250) ||
               (!strcmp(cult.fase, "VEGETA") && cult.ppm >= 300 && cult.ppm <= 450) ||
               (!strcmp(cult.fase, "FLOR T") && cult.ppm >= 500 && cult.ppm <= 1100) ||
               (!strcmp(cult.fase, "FLOR A") && cult.ppm >= 1200 && cult.ppm <= 1600);
  s.salud = ppmOk ? 100 : 62;
  s.energia = (!cult.tocaRegar && !cult.tocaFertilizar) ? 90 : 60;
  return s;
}


void inicializarParticulasViento() {
  for (int i = 0; i < 12; i++) {
    viento[i].x = random(0, 320);
    viento[i].y = random(84, 188);
    viento[i].oldX = viento[i].x;
    viento[i].oldY = viento[i].y;
    viento[i].vx = random(1, 3);
    viento[i].vy = random(-1, 2);
    viento[i].color = (i % 3 == 0) ? MI_VERDE : ((i % 3 == 1) ? MI_CIAN : MI_BLANCO);
    viento[i].activa = true;
  }
  for (int i = 0; i < 5; i++) {
    corazones[i].x = 0; corazones[i].y = 0; corazones[i].oldX = 0; corazones[i].oldY = 0;
    corazones[i].vx = 0; corazones[i].vy = 0; corazones[i].color = MI_ROJO; corazones[i].activa = false;
  }
  for (int i = 0; i < 10; i++) {
    sparkle[i].x = 0; sparkle[i].y = 0; sparkle[i].oldX = 0; sparkle[i].oldY = 0;
    sparkle[i].vx = 1; sparkle[i].vy = 0; sparkle[i].color = MI_VERDE; sparkle[i].activa = false;
  }
  for (int i = 0; i < 26; i++) {
    lluvia[i].x = random(0, 320); lluvia[i].y = random(78, 186); lluvia[i].oldX = lluvia[i].x; lluvia[i].oldY = lluvia[i].y;
    lluvia[i].vx = 0; lluvia[i].vy = 3; lluvia[i].color = MI_CIAN; lluvia[i].activa = false;
  }
  for (int i = 0; i < 7; i++) {
    terpenicas[i].x = 130 + i * 8; terpenicas[i].y = 110 + ((i % 2) ? 3 : 0); terpenicas[i].oldX = terpenicas[i].x; terpenicas[i].oldY = terpenicas[i].y;
    terpenicas[i].vx = 0; terpenicas[i].vy = -1; terpenicas[i].color = MI_BLANCO; terpenicas[i].activa = false;
  }
}

void actualizarStatsRpgLentas(const InfoCultivo &cult, unsigned long ahora) {
  if (ahora - ultimoRpgMs < 18000UL) return;
  ultimoRpgMs = ahora;
  bool cambios = false;
  if (statsVivas.salud > 92 && !cult.tocaRegar && random(0, 100) < 20 && statsRpg.resina < 100) { statsRpg.resina++; cambios = true; }
  if (statsVivas.salud > 88 && (!strcmp(cult.fase, "FLOR T") || !strcmp(cult.fase, "FLOR A")) && random(0, 100) < 20 && statsRpg.vigor < 100) { statsRpg.vigor++; cambios = true; }
  if (interaccionesPlanta > 8 && random(0, 100) < 20 && statsRpg.terpenos < 100) { statsRpg.terpenos++; cambios = true; interaccionesPlanta--; }
  if (cambios && (ahora - ultimoGuardadoRpgMs > 60000UL)) {
    prefs.putUChar("rpgVig", statsRpg.vigor);
    prefs.putUChar("rpgRes", statsRpg.resina);
    prefs.putUChar("rpgTer", statsRpg.terpenos);
    guardarConfigSD();
    ultimoGuardadoRpgMs = ahora;
  }
}

void dibujarModoPlanta() {
  int d, m, a;
  fechaConDelta(offsetVer, d, m, a);
  InfoCultivo cult = calcularCultivo(d, m, a);

  struct tm ti;
  bool hayHora = getLocalTime(&ti, 5);
  int hora = hayHora ? ti.tm_hour : 12;
  bool esDia = (hora >= 7 && hora < 19);

  unsigned long ahora = millis();
  if (strcmp(faseAnterior, cult.fase) != 0) {
    strncpy(faseAnterior, cult.fase, sizeof(faseAnterior) - 1);
    faseAnterior[sizeof(faseAnterior) - 1] = '\0';
    msCambioFase = ahora;
    if (!strcmp(cult.fase, "FLOR T")) mostrarMensajeEvento("+ FLORACION INICIADA");
  }
  statsVivas = calcularStatsVivas(cult);
  actualizarStatsRpgLentas(cult, ahora);

  int respiracion = (int)((ahora / (esDia ? 80 : 150)) % 8);
  if (respiracion > 4) respiracion = 8 - respiracion;
  int sway = (int)((ahora / 190) % 8);
  if (sway > 4) sway = 8 - sway;
  sway -= 2;
  bool ojosAbiertos = esDia && ((ahora / 2300) % 8) != 0;
  int brillo = ((ahora / 180) % 6);
  int bonusAlegria = min(10, interaccionesPlanta / 2);
  int frameHojas = framePlanta % 3;

  if (!fondoPlantaDibujado || esDia != ultimoEsDiaRender) {
    tft.fillScreen(esDia ? MI_AZUL_OSC : MI_NEGRO);
    if (esDia) {
      tft.fillCircle(278, 22, 11, MI_AMARILLO);
      for (int r = 0; r < 8; r++) tft.drawLine(278, 22, 278 + (int)(14 * cos(r * 0.78f)), 22 + (int)(14 * sin(r * 0.78f)), MI_AMARILLO);
    } else {
      tft.fillCircle(278, 20, 10, MI_GRIS);
      tft.fillCircle(282, 18, 8, MI_NEGRO);
      for (int i = 0; i < 20; i++) tft.fillCircle((i * 17) % 320, (i * 19) % 90, 1, MI_BLANCO);
    }
      fondoPlantaDibujado = true;
    ultimoEsDiaRender = esDia;
  }

  EstadoAnimoPlanta animo = !esDia ? ANIMO_DORMIDA : (statsVivas.salud < 70 ? ANIMO_ESTRESADA : (statsVivas.agua < 50 ? ANIMO_TRISTE : ANIMO_FELIZ));
  int cx = 160;
  int swayExterno = sway + (animo == ANIMO_ESTRESADA ? ((ahora / 180) % 3) - 1 : 0);
  int baseY = 190;
  int alto = 26 + respiracion;
  int etapaVisual = 0;
  if (!strcmp(cult.fase, "PLANTULA")) etapaVisual = 0;
  else if (!strcmp(cult.fase, "VEGETA")) etapaVisual = (cult.progreso < 0.5f) ? 1 : 2;
  else if (!strcmp(cult.fase, "FLOR T")) etapaVisual = (cult.progreso < 0.5f) ? 3 : 4;
  else if (cult.progreso < 0.85f) etapaVisual = 5;
  else etapaVisual = 6;

  uint16_t verdeVivo = (uint16_t)min(0x07E0, MI_VERDE + bonusAlegria * 20);
  uint16_t verdeHoja = (animo == ANIMO_ESTRESADA) ? 0x04E0 : verdeVivo;
  uint16_t verdeOsc = 0x03A0;
  uint16_t colorPistilo = 0xFFDF;
  uint16_t paletaFlores[] = {0xA81F, MI_VERDE, MI_NARANJA, MI_AMARILLO, MI_ROJO, 0xF81F};
  uint8_t idxFlorA = (ahora / 9 + framePlanta) % 6;
  uint8_t idxFlorB = (idxFlorA + 2 + ((ahora / 33) % 2)) % 6;
  uint8_t idxFlorC = (idxFlorA + 4 + ((ahora / 47) % 2)) % 6;
  uint16_t colorCogolloA = paletaFlores[idxFlorA];
  uint16_t colorCogolloB = paletaFlores[idxFlorB];
  uint16_t colorCogolloC = paletaFlores[idxFlorC];

  tft.drawFastVLine(cx, baseY - alto + 8, alto - 8, verdeOsc);
  tft.drawFastVLine(cx - 1, baseY - alto + 12, alto - 12, verdeHoja);
  tft.drawFastVLine(cx + 1, baseY - alto + 12, alto - 12, verdeHoja);

  int yTop = baseY - alto - 2;
  tft.drawLine(cx, baseY - alto + 6, cx - 12 + swayExterno, baseY - alto - 3, verdeOsc);
  tft.drawLine(cx, baseY - alto + 6, cx + 12 - swayExterno, baseY - alto - 3, verdeOsc);
  tft.drawLine(cx, baseY - alto + 14, cx - 10 + swayExterno, baseY - alto + 6, verdeOsc);
  tft.drawLine(cx, baseY - alto + 14, cx + 10 - swayExterno, baseY - alto + 6, verdeOsc);

  if (etapaVisual == 0) { // 1. PLANTULA
    drawLeafCannabis(cx - 4, baseY - alto + 7, verdeHoja);
    drawLeafCannabis(cx + 4, baseY - alto + 7, verdeHoja);
    drawLeafCannabis(cx, baseY - alto - 1, 0x05EF);
  } else if (etapaVisual == 1) { // 2. VEGETACION
    drawLeafCannabis(cx - 12 + swayExterno, baseY - alto - 7, verdeHoja);
    drawLeafCannabis(cx + 12 - swayExterno, baseY - alto - 7, verdeHoja);
    drawLeafCannabis(cx - 10 + swayExterno, baseY - alto + 2, verdeHoja);
    drawLeafCannabis(cx + 10 - swayExterno, baseY - alto + 2, verdeHoja);
    drawLeafCannabis(cx, yTop, verdeHoja);
  } else if (etapaVisual == 2) { // 3. PREFLORA
    drawLeafCannabis(cx - 12 + swayExterno, baseY - alto - 7, verdeHoja);
    drawLeafCannabis(cx + 12 - swayExterno, baseY - alto - 7, verdeHoja);
    drawLeafCannabis(cx - 10 + swayExterno, baseY - alto + 2, verdeHoja);
    drawLeafCannabis(cx + 10 - swayExterno, baseY - alto + 2, verdeHoja);
    drawLeafCannabis(cx, yTop, verdeHoja);
    tft.drawPixel(cx - 3, yTop + 5, colorPistilo);
    tft.drawPixel(cx + 3, yTop + 5, colorPistilo);
    tft.drawPixel(cx, yTop + 2, colorPistilo);
  } else if (etapaVisual == 3) { // 4. INICIO FLORA
    drawLeafCannabis(cx - 12 + swayExterno, baseY - alto - 7, verdeHoja);
    drawLeafCannabis(cx + 12 - swayExterno, baseY - alto - 7, verdeHoja);
    drawLeafCannabis(cx - 10 + swayExterno, baseY - alto + 2, verdeHoja);
    drawLeafCannabis(cx + 10 - swayExterno, baseY - alto + 2, verdeHoja);
    drawLeafCannabis(cx, yTop, verdeHoja);
    drawCogolloPixel(cx, yTop + 1, colorCogolloA, colorCogolloB, colorPistilo);
  } else if (etapaVisual == 4) { // 5. MEDIA FLORA
    drawLeafCannabis(cx - 12 + swayExterno, baseY - alto - 7, verdeHoja);
    drawLeafCannabis(cx + 12 - swayExterno, baseY - alto - 7, verdeHoja);
    drawLeafCannabis(cx - 10 + swayExterno, baseY - alto + 2, verdeHoja);
    drawLeafCannabis(cx + 10 - swayExterno, baseY - alto + 2, verdeHoja);
    drawLeafCannabis(cx, yTop, verdeHoja);
    drawCogolloPixel(cx, yTop, colorCogolloA, colorCogolloB, colorPistilo);
    drawCogolloPixel(cx - 8 + swayExterno, yTop + 5, colorCogolloB, colorCogolloC, colorPistilo);
    drawCogolloPixel(cx + 8 - swayExterno, yTop + 5, colorCogolloC, colorCogolloA, colorPistilo);
  } else if (etapaVisual == 5) { // 6. FLORA AVANZADA
    drawLeafCannabis(cx - 12 + swayExterno, baseY - alto - 7, verdeHoja);
    drawLeafCannabis(cx + 12 - swayExterno, baseY - alto - 7, verdeHoja);
    drawLeafCannabis(cx - 10 + swayExterno, baseY - alto + 2, verdeHoja);
    drawLeafCannabis(cx + 10 - swayExterno, baseY - alto + 2, verdeHoja);
    drawLeafCannabis(cx, yTop, verdeHoja);
    drawCogolloPixel(cx, yTop, colorCogolloA, colorCogolloB, colorPistilo);
    drawCogolloPixel(cx - 10 + swayExterno, yTop + 4, colorCogolloB, colorCogolloC, colorPistilo);
    drawCogolloPixel(cx + 10 - swayExterno, yTop + 4, colorCogolloC, colorCogolloA, colorPistilo);
    drawCogolloPixel(cx, yTop - 5, colorCogolloC, colorCogolloB, colorPistilo);
  } else { // 7. COSECHA
    tft.drawFastVLine(cx, baseY - 168, 18, 0x7BEF);
    tft.drawLine(cx - 4, baseY - 150, cx + 4, baseY - 150, MI_BLANCO);
  }

  if (etapaVisual != 6) {
    tft.drawBitmap(cx - 8, baseY - alto - 1, ojosAbiertos ? SPRITE_OJOS_ABIERTOS : SPRITE_OJOS_CERRADOS, 8, 8, MI_NEGRO);
  }
  drawLeafCannabis(cx - 15 + swayExterno + (frameHojas - 1), baseY - alto + 4, verdeHoja);
  drawLeafCannabis(cx + 15 - swayExterno - (frameHojas - 1), baseY - alto + 2, verdeHoja);

  uint16_t colorFondo = esDia ? MI_AZUL_OSC : MI_NEGRO;
  for (int i = 0; i < 12; i++) {
    if (!viento[i].activa) continue;
    int radio = 1 + (i % 2);
    tft.fillCircle(viento[i].oldX, viento[i].oldY, radio, colorFondo);
    viento[i].oldX = viento[i].x;
    viento[i].oldY = viento[i].y;
    viento[i].x += viento[i].vx;
    viento[i].y += viento[i].vy;
    if (viento[i].x > 322) { viento[i].x = -2; viento[i].y = random(84, 188); }
    if (viento[i].y < 82) viento[i].y = 188;
    if (viento[i].y > 188) viento[i].y = 82;
    viento[i].oldX = viento[i].x;
    viento[i].oldY = viento[i].y;
    uint16_t pCol = (statsRpg.terpenos > 90 && (i % 2 == 0)) ? MI_NARANJA : viento[i].color;
    tft.fillCircle(viento[i].x, viento[i].y, radio, pCol);
  }

  tft.setTextSize(1);
  tft.setTextColor(MI_BLANCO);
  tft.setCursor(12, 36);
  tft.printf("FASE: %s", cult.fase);
  tft.setCursor(12, 50);
  tft.printf("%d ppm  %.1f mS  %02d:%02d", cult.ppm, cult.mS, hayHora ? ti.tm_hour : 0, hayHora ? ti.tm_min : 0);
  tft.drawRect(12, 64, 90, 7, MI_GRIS); tft.fillRect(13, 65, (statsVivas.agua * 88) / 100, 5, MI_CIAN);
  tft.drawRect(12, 74, 90, 7, MI_GRIS); tft.fillRect(13, 75, (statsVivas.felicidad * 88) / 100, 5, MI_AMARILLO);
  tft.drawRect(12, 84, 90, 7, MI_GRIS); tft.fillRect(13, 85, (statsVivas.salud * 88) / 100, 5, MI_VERDE);
  tft.drawRect(12, 94, 90, 7, MI_GRIS); tft.fillRect(13, 95, (statsVivas.energia * 88) / 100, 5, MI_BLANCO);
  tft.drawRect(208, 10, 100, 60, MI_GRIS);
  tft.setCursor(212, 14); tft.print("RPG");
  tft.setCursor(212, 26); tft.printf("GEN %d", statsRpg.genetica);
  tft.setCursor(212, 36); tft.printf("VIG %d", statsRpg.vigor);
  tft.setCursor(212, 46); tft.printf("RES %d", statsRpg.resina);
  tft.setCursor(212, 56); tft.printf("TER %d", statsRpg.terpenos);
  for (int i = 0; i < 7; i++) {
    if (terpenicas[i].activa) tft.fillCircle(terpenicas[i].oldX, terpenicas[i].oldY, 2, colorFondo);
    if (ahora - msCambioFase < 900) {
      terpenicas[i].activa = true;
      terpenicas[i].x = 130 + i * 8;
      terpenicas[i].y = 110 + ((i % 2) ? 3 : 0);
      tft.fillCircle(terpenicas[i].x, terpenicas[i].y, 2, MI_BLANCO);
      terpenicas[i].oldX = terpenicas[i].x;
      terpenicas[i].oldY = terpenicas[i].y;
    } else {
      terpenicas[i].activa = false;
    }
  }

  if (animacionFeliz) {
    tft.drawLine(cx - 6, baseY - alto + 3, cx - 2, baseY - alto + 6, MI_NEGRO);
    tft.drawLine(cx + 2, baseY - alto + 6, cx + 6, baseY - alto + 3, MI_NEGRO);
    tft.drawLine(cx - 6, baseY - alto + 10, cx, baseY - alto + 14, MI_NEGRO);
    tft.drawLine(cx, baseY - alto + 14, cx + 6, baseY - alto + 10, MI_NEGRO);
    for (int i = 0; i < 5; i++) {
      tft.fillCircle(corazones[i].oldX, corazones[i].oldY, 2, colorFondo);
      tft.drawPixel(corazones[i].oldX - 2, corazones[i].oldY, colorFondo); tft.drawPixel(corazones[i].oldX + 2, corazones[i].oldY, colorFondo);
      tft.drawPixel(corazones[i].oldX, corazones[i].oldY - 2, colorFondo); tft.drawPixel(corazones[i].oldX, corazones[i].oldY + 2, colorFondo);
      corazones[i].oldX = corazones[i].x;
      corazones[i].oldY = corazones[i].y;
      corazones[i].x = cx - 26 + i * 12;
      corazones[i].y = 126 - ((int)((ahora / 70 + i * 5) % 18));
      tft.fillCircle(corazones[i].x, corazones[i].y, 2, MI_ROJO);
      tft.drawPixel(corazones[i].x - 2, corazones[i].y, MI_ROJO); tft.drawPixel(corazones[i].x + 2, corazones[i].y, MI_ROJO);
      tft.drawPixel(corazones[i].x, corazones[i].y - 2, MI_ROJO); tft.drawPixel(corazones[i].x, corazones[i].y + 2, MI_ROJO);
      corazones[i].oldX = corazones[i].x;
      corazones[i].oldY = corazones[i].y;
      corazones[i].activa = true;
    }
    for (int i = 0; i < 10; i++) {
      tft.drawPixel(sparkle[i].oldX, sparkle[i].oldY, colorFondo);
      tft.drawPixel(sparkle[i].oldX + 1, sparkle[i].oldY, colorFondo);
      sparkle[i].oldX = sparkle[i].x;
      sparkle[i].oldY = sparkle[i].y;
      sparkle[i].x = (i * 29 + (int)(ahora / 20)) % 320;
      sparkle[i].y = 95 + ((i * 11 + (int)(ahora / 35)) % 55);
      tft.drawPixel(sparkle[i].x, sparkle[i].y, MI_VERDE);
      tft.drawPixel(sparkle[i].x + 1, sparkle[i].y, MI_BLANCO);
      sparkle[i].oldX = sparkle[i].x;
      sparkle[i].oldY = sparkle[i].y;
      sparkle[i].activa = true;
    }
  } else {
    for (int i = 0; i < 5; i++) {
      if (!corazones[i].activa) continue;
      tft.fillCircle(corazones[i].oldX, corazones[i].oldY, 2, colorFondo);
      tft.drawPixel(corazones[i].oldX - 2, corazones[i].oldY, colorFondo); tft.drawPixel(corazones[i].oldX + 2, corazones[i].oldY, colorFondo);
      tft.drawPixel(corazones[i].oldX, corazones[i].oldY - 2, colorFondo); tft.drawPixel(corazones[i].oldX, corazones[i].oldY + 2, colorFondo);
      corazones[i].activa = false;
    }
    for (int i = 0; i < 10; i++) {
      if (!sparkle[i].activa) continue;
      tft.drawPixel(sparkle[i].oldX, sparkle[i].oldY, colorFondo);
      tft.drawPixel(sparkle[i].oldX + 1, sparkle[i].oldY, colorFondo);
      sparkle[i].activa = false;
    }
  }

  if (animacionRiego) {
    for (int i = 0; i < 26; i++) {
      tft.drawLine(lluvia[i].oldX, lluvia[i].oldY, lluvia[i].oldX, lluvia[i].oldY + 3, colorFondo);
      lluvia[i].oldX = lluvia[i].x;
      lluvia[i].oldY = lluvia[i].y;
      lluvia[i].x = (i * 13 + (int)(ahora / 12)) % 320;
      lluvia[i].y = 78 + ((i * 19 + (int)(ahora / 9)) % 108);
      tft.drawLine(lluvia[i].x, lluvia[i].y, lluvia[i].x, lluvia[i].y + 3, MI_CIAN);
      lluvia[i].oldX = lluvia[i].x;
      lluvia[i].oldY = lluvia[i].y;
      lluvia[i].activa = true;
    }
    tft.fillCircle(cx - 24, baseY - alto - 16, 4, MI_CIAN);
    tft.fillCircle(cx + 24, baseY - alto - 16, 4, MI_CIAN);
    tft.fillCircle(cx, baseY - alto - 24, 5, MI_CIAN);
  } else {
    for (int i = 0; i < 26; i++) {
      if (!lluvia[i].activa) continue;
      tft.drawLine(lluvia[i].oldX, lluvia[i].oldY, lluvia[i].oldX, lluvia[i].oldY + 3, colorFondo);
      lluvia[i].activa = false;
    }
  }
  if (statsRpg.resina > 90) { for (int i = 0; i < 10; i++) { int sx = cx - 24 + (i * 5); int sy = baseY - alto - 22 + (i % 4) * 4; tft.drawPixel(sx, sy, MI_BLANCO); } }
  if (animo == ANIMO_DORMIDA) { tft.setCursor(cx + 22, baseY - alto - 28); tft.print("Zz"); }
  if (ahora - msMensajeEvento < 2500) { tft.setTextColor(MI_AMARILLO); tft.setCursor(12, 108); tft.print(mensajeEvento); }
}


bool parseLineaConfigSD(const String &linea, String &clave, String &valor) {
  int separador = linea.indexOf('=');
  if (separador <= 0) return false;
  clave = linea.substring(0, separador);
  valor = linea.substring(separador + 1);
  clave.trim();
  valor.trim();
  return clave.length() > 0 && valor.length() > 0;
}

bool cargarConfigSD() {
  if (!sdDisponible || !SD.exists(CONFIG_SD_PATH)) return false;

  File archivo = SD.open(CONFIG_SD_PATH, FILE_READ);
  if (!archivo) return false;

  while (archivo.available()) {
    String linea = archivo.readStringUntil('\n');
    String clave, valor;
    if (!parseLineaConfigSD(linea, clave, valor)) continue;

    int numero = valor.toInt();
    if (clave == "vegDia") fechaVeg.d = numero;
    else if (clave == "vegMes") fechaVeg.m = numero;
    else if (clave == "vegAnio") fechaVeg.a = numero;
    else if (clave == "florDia") fechaFlor.d = numero;
    else if (clave == "florMes") fechaFlor.m = numero;
    else if (clave == "florAnio") fechaFlor.a = numero;
    else if (clave == "rpgGen") statsRpg.genetica = constrain(numero, 0, 100);
    else if (clave == "rpgVig") statsRpg.vigor = constrain(numero, 0, 100);
    else if (clave == "rpgRes") statsRpg.resina = constrain(numero, 0, 100);
    else if (clave == "rpgTer") statsRpg.terpenos = constrain(numero, 0, 100);
    else if (clave == "rpgInit") rpgInicializado = (numero != 0);
  }

  archivo.close();
  validarFechasConfig();
  return true;
}

bool guardarConfigSD() {
  if (!sdDisponible) return false;

  if (SD.exists(CONFIG_SD_PATH)) SD.remove(CONFIG_SD_PATH);
  File archivo = SD.open(CONFIG_SD_PATH, FILE_WRITE);
  if (!archivo) return false;

  archivo.printf("vegDia=%d\n", fechaVeg.d);
  archivo.printf("vegMes=%d\n", fechaVeg.m);
  archivo.printf("vegAnio=%d\n", fechaVeg.a);
  archivo.printf("florDia=%d\n", fechaFlor.d);
  archivo.printf("florMes=%d\n", fechaFlor.m);
  archivo.printf("florAnio=%d\n", fechaFlor.a);
  archivo.printf("rpgInit=%d\n", rpgInicializado ? 1 : 0);
  archivo.printf("rpgGen=%d\n", statsRpg.genetica);
  archivo.printf("rpgVig=%d\n", statsRpg.vigor);
  archivo.printf("rpgRes=%d\n", statsRpg.resina);
  archivo.printf("rpgTer=%d\n", statsRpg.terpenos);
  archivo.close();
  return true;
}

void inicializarSD() {
  sdDisponible = SD.begin(SD_CS);
  Serial.print("SD: ");
  Serial.println(sdDisponible ? "OK" : "NO DISPONIBLE");
}

void guardarFechasPrefs() {
  prefs.putInt("vegDia", fechaVeg.d); prefs.putInt("vegMes", fechaVeg.m); prefs.putInt("vegAnio", fechaVeg.a);
  prefs.putInt("florDia", fechaFlor.d); prefs.putInt("florMes", fechaFlor.m); prefs.putInt("florAnio", fechaFlor.a);
  guardarConfigSD();
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
  if (estadoUI == UI_CALENDARIO) return;

  if (estadoUI == UI_CONFIG) {
    if (indiceCfg == CAMPO_VOLVER) {
      validarFechasConfig();
      guardarFechasPrefs();
      estadoUI = UI_CALENDARIO;
    } else {
      estadoUI = UI_EDITANDO;
    }
  } else {
    validarFechasConfig();
    guardarFechasPrefs();
    estadoUI = UI_CONFIG;
  }
  necesitaRedibujar = true;
}

int mesDesdeTexto(const char* m) { const char* en[] = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"}; for (int i=0;i<12;i++) if(!strcmp(m,en[i])) return i; return 0; }
void actualizarEncoder() {
  unsigned long ahora = millis();
  uint8_t estadoActual = (digitalRead(ENC_S1) << 1) | digitalRead(ENC_S2);

  if (estadoActual != ultimoEstadoAB && (ahora - ultimoMsEncoder) >= DEBOUNCE_ENCODER_MS) {
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

  bool estadoBtn = (digitalRead(BTN_KEY) == LOW); // INPUT_PULLUP: LOW = presionado
  if (estadoBtn != ultimoEstadoBtn && (ahora - ultimoBtnMs) >= DEBOUNCE_BTN_MS) {
    ultimoBtnMs = ahora;
    ultimoEstadoBtn = estadoBtn;
    if (estadoBtn) {
      botonPresionado = true;
    } else {
      if (botonPresionado) manejarConfirmacion();
      botonPresionado = false;
    }
  }
}
void consumirEncoder(){ if(modoPlanta){deltaEncoder=0;return;} int8_t p=deltaEncoder; if(!p)return; deltaEncoder=0; while(p>0){aplicarPasoEncoder(1);p--;} while(p<0){aplicarPasoEncoder(-1);p++;} }

void actualizarFechaSiEsPosible() {
  if (WiFi.status() == WL_CONNECTED) {
    wifiConectado = true;
    struct tm ti;
    if (getLocalTime(&ti, 50)) {
      int nd = ti.tm_mday, nm = ti.tm_mon, na = 1900 + ti.tm_year;
      if (nd != diaHoy || nm != mesHoy || na != anioHoy) {
        diaHoy = nd;
        mesHoy = nm;
        anioHoy = na;
        necesitaRedibujar = true;
      }
    }
    if (millis() - ultimoIntentoNtpMs > 600000UL) {
      configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, "pool.ntp.org", "time.nist.gov");
      ultimoIntentoNtpMs = millis();
    }
    return;
  }

  wifiConectado = false;

  if (WiFi.status() == WL_IDLE_STATUS) return;

  if (millis() - ultimoIntentoWifiMs > 30000UL) {
    WiFi.disconnect(true);
    delay(100);
    WiFi.begin(SSID, PASSWORD);
    ultimoIntentoWifiMs = millis();
  }
}

void enviarDatosEspNow() {
  struct tm ti;
  bool hayHora = getLocalTime(&ti, 10);
  int hour = hayHora ? ti.tm_hour : 0;
  int minute = hayHora ? ti.tm_min : 0;
  int second = hayHora ? ti.tm_sec : 0;

  FechaConfig hoy = {diaHoy, mesHoy + 1, anioHoy};
  time_t tHoy = fechaToTime(hoy);
  time_t tVeg = fechaToTime(fechaVeg);
  time_t tFlor = fechaToTime(fechaFlor);

  int daysVeg = max(0, (int)((tHoy - tVeg) / 86400));
  int daysFlower = 0;
  bool isVegetative = (tHoy < tFlor);
  if (!isVegetative) {
    daysFlower = max(0, (int)((tHoy - tFlor) / 86400));
  }

  int lightHours = isVegetative ? 18 : 12;
  int darkHours = isVegetative ? 6 : 12;
  bool inLightMode = (hour >= 0 && hour < lightHours);

  InfoCultivo cult = calcularCultivo(diaHoy, mesHoy, anioHoy);
  outgoingData.lightHours = lightHours;
  outgoingData.darkHours = darkHours;
  outgoingData.daysVeg = daysVeg;
  outgoingData.daysFlower = daysFlower;
  outgoingData.isVegetative = isVegetative;
  outgoingData.inLightMode = inLightMode;
  outgoingData.progressPercent = cult.progreso;
  outgoingData.hour = hour;
  outgoingData.minute = minute;
  outgoingData.second = second;

  esp_now_send(
      macCentro,
      (uint8_t *)&outgoingData,
      sizeof(outgoingData)
  );

  Serial.println("=== ESP NOW SEND ===");
  Serial.print("VEG DAYS: ");
  Serial.println(outgoingData.daysVeg);
  Serial.print("FLOWER DAYS: ");
  Serial.println(outgoingData.daysFlower);
  Serial.print("VEGETATIVE: ");
  Serial.println(outgoingData.isVegetative);
  Serial.print("LIGHT MODE: ");
  Serial.println(outgoingData.inLightMode);
  Serial.print("TIME: ");
  Serial.print(outgoingData.hour);
  Serial.print(":");
  Serial.print(outgoingData.minute);
  Serial.print(":");
  Serial.println(outgoingData.second);
}

void setup() {
  Serial.begin(115200); SPI.begin(12, 13, 11);
  pinMode(ENC_S1, INPUT_PULLUP); pinMode(ENC_S2, INPUT_PULLUP); pinMode(BTN_KEY, INPUT_PULLUP);
  pinMode(TTP223_PIN, INPUT);
  ultimoEstadoAB = (digitalRead(ENC_S1) << 1) | digitalRead(ENC_S2);
  ultimoEstadoBtn = (digitalRead(BTN_KEY) == LOW);
  tft.init(240, 320); tft.setRotation(1); tft.invertDisplay(false);

  prefs.begin("cultivo", false);
  inicializarSD();
  fechaVeg.d = prefs.getInt("vegDia", 10); fechaVeg.m = prefs.getInt("vegMes", 7); fechaVeg.a = prefs.getInt("vegAnio", 2026);
  fechaFlor.d = prefs.getInt("florDia", 15); fechaFlor.m = prefs.getInt("florMes", 9); fechaFlor.a = prefs.getInt("florAnio", 2026);
  rpgInicializado = prefs.getBool("rpgInit", false);
  if (!rpgInicializado) {
    randomSeed(esp_random());
    statsRpg.genetica = random(75, 101);
    statsRpg.vigor = random(70, 100);
    statsRpg.resina = random(60, 100);
    statsRpg.terpenos = random(65, 100);
    prefs.putUChar("rpgGen", statsRpg.genetica);
    prefs.putUChar("rpgVig", statsRpg.vigor);
    prefs.putUChar("rpgRes", statsRpg.resina);
    prefs.putUChar("rpgTer", statsRpg.terpenos);
    prefs.putBool("rpgInit", true);
  } else {
    statsRpg.genetica = prefs.getUChar("rpgGen", 85);
    statsRpg.vigor = prefs.getUChar("rpgVig", 85);
    statsRpg.resina = prefs.getUChar("rpgRes", 85);
    statsRpg.terpenos = prefs.getUChar("rpgTer", 85);
  }

  if (cargarConfigSD()) {
    Serial.println("Config cargada desde SD");
  } else {
    validarFechasConfig();
    guardarConfigSD();
  }

  WiFi.mode(WIFI_STA);
  if (esp_now_init() != ESP_OK) {

    Serial.println("ESP-NOW ERROR");
  } else {
    esp_now_peer_info_t peerInfo = {};

    memcpy(peerInfo.peer_addr, macCentro, 6);

    peerInfo.channel = 0;

    peerInfo.encrypt = false;

    esp_now_add_peer(&peerInfo);
  }

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
  inicializarParticulasViento();
  necesitaRedibujar = true;
}

void loop() {
  actualizarFechaSiEsPosible();
  actualizarEncoder();
  consumirEncoder();

  unsigned long ahora = millis();
  if (millis() - lastEspNowSendMs >= 1000) {
    lastEspNowSendMs = millis();
    enviarDatosEspNow();
  }
  procesarTouchTTP223(ahora);
  actualizarAnimacionesPlanta(ahora);

  if (modoPlanta) {
    if (ahora - ultimoFramePlanta >= FRAME_MS) {
      ultimoFramePlanta = ahora;
      framePlanta++;
      dibujarModoPlanta();
    }
    return;
  }

  if (necesitaRedibujar) { if (estadoUI == UI_CALENDARIO) dibujarCalendario(); else dibujarConfiguracion(); necesitaRedibujar = false; }
}
