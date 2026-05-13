#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>
#include <WiFi.h>
#include <time.h>
#include <Preferences.h>

// ===== PINES =====
#define TFT_CS    10
#define TFT_RST    9
#define TFT_DC     8

#define ENC_S1    2
#define ENC_S2    3
#define ENC_KEY   4

// ===== COLORES =====
#define MI_NEGRO    0x0000
#define MI_BLANCO   0xFFFF
#define MI_VERDE    0x07E0
#define MI_ROJO     0xF800
#define MI_CIAN     0x07FF
#define MI_NARANJA  0xFBE0
#define MI_GRIS     0x4208
#define MI_AMARILLO 0xFFE0
#define MI_AZUL_OSC 0x2945

struct InfoCultivo {
  const char* fase;
  uint16_t colorFase;
  int ppm;
  float mS;
  float progreso;
  bool tocaRegar;
  bool tocaFertilizar;
};

enum EstadoUI
{
  UI_CALENDARIO,
  UI_CONFIG,
  UI_EDIT_VEG,
  UI_EDIT_FLOR
};

enum ItemConfig
{
  CFG_ITEM_VEG = 0,
  CFG_ITEM_FLOR = 1,
  CFG_ITEM_VOLVER = 2
};

Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);
Preferences prefs;

const char* SSID = "IZZI-367E";
const char* PASSWORD = "ehwa3pX7btcw";

int diaHoy, mesHoy, anioHoy;
time_t fechaBroteUnix;
int diaInicioVeg;
int diaInicioFlor;
int offsetVer = 0;

EstadoUI estadoUI = UI_CALENDARIO;
ItemConfig itemSeleccionado = CFG_ITEM_VEG;
bool necesitaRedibujar = true;

// Encoder robusto (gray code + acumulación por detente)
uint8_t encPrevAB = 0;
int8_t encAcumulado = 0;
unsigned long encUltimaLecturaMs = 0;

bool keyStable = HIGH;
bool keyLastRaw = HIGH;
unsigned long keyLastChangeMs = 0;
unsigned long keyPressStartMs = 0;

bool wifiConectado = false;
unsigned long ultimoIntentoNtpMs = 0;
unsigned long ultimoIntentoWifiMs = 0;

const long GMT_OFFSET_SEC = -21600;
const int DAYLIGHT_OFFSET_SEC = 0;

const char* MESES[] = {"Ene","Feb","Mar","Abr","May","Jun","Jul","Ago","Sep","Oct","Nov","Dic"};
const char* DIA_SEM[] = {"Dom","Lun","Mar","Mie","Jue","Vie","Sab"};

int mesDesdeTexto(const char* m)
{
  if      (strcmp(m, "Jan") == 0) return 0;
  else if (strcmp(m, "Feb") == 0) return 1;
  else if (strcmp(m, "Mar") == 0) return 2;
  else if (strcmp(m, "Apr") == 0) return 3;
  else if (strcmp(m, "May") == 0) return 4;
  else if (strcmp(m, "Jun") == 0) return 5;
  else if (strcmp(m, "Jul") == 0) return 6;
  else if (strcmp(m, "Aug") == 0) return 7;
  else if (strcmp(m, "Sep") == 0) return 8;
  else if (strcmp(m, "Oct") == 0) return 9;
  else if (strcmp(m, "Nov") == 0) return 10;
  else if (strcmp(m, "Dec") == 0) return 11;
  return 0;
}

InfoCultivo calcularCultivo(int d, int m, int a)
{
  InfoCultivo r;
  struct tm td = {0};
  td.tm_mday = d; td.tm_mon = m; td.tm_year = a - 1900;
  time_t tFecha = mktime(&td);

  struct tm* infoFecha = localtime(&tFecha);
  int diaSemana = infoFecha ? infoFecha->tm_wday : 0;
  r.tocaFertilizar = (diaSemana == 0);

  int diasVida = (tFecha > fechaBroteUnix) ? (int)((tFecha - fechaBroteUnix) / 86400) : 0;
  r.tocaRegar = (diasVida % 3 == 0);

  int ppmMin, ppmMax, inicioFase, finFase, diasRel;
  if (diasVida < diaInicioVeg) {
    r.fase = "PLANTULA"; r.colorFase = MI_CIAN;
    ppmMin = 100; ppmMax = 250; inicioFase = 0; finFase = diaInicioVeg; diasRel = diasVida;
  } else if (diasVida < diaInicioFlor) {
    r.fase = "VEGETA"; r.colorFase = MI_VERDE;
    ppmMin = 300; ppmMax = 450; inicioFase = diaInicioVeg; finFase = diaInicioFlor; diasRel = diasVida - diaInicioVeg;
  } else {
    int diasFlor = diasVida - diaInicioFlor;
    if (diasFlor <= 47) {
      r.fase = "FLOR T"; r.colorFase = MI_AMARILLO;
      ppmMin = 500; ppmMax = 1100; inicioFase = 0; finFase = 47; diasRel = diasFlor;
    } else {
      r.fase = "FLOR A"; r.colorFase = MI_ROJO;
      ppmMin = 1200; ppmMax = 1600; inicioFase = 48; finFase = 68; diasRel = diasFlor;
    }
  }

  int rango = max(1, finFase - inicioFase);
  float prog = constrain((float)(diasRel - inicioFase) / rango, 0, 1);
  r.progreso = prog;
  r.ppm = (int)(ppmMin + (ppmMax - ppmMin) * prog);
  r.mS = r.ppm / 500.0;
  return r;
}

void fechaConDelta(int delta, int &d, int &m, int &a)
{
  struct tm t = {0};
  t.tm_mday = diaHoy + delta;
  t.tm_mon = mesHoy;
  t.tm_year = anioHoy - 1900;
  mktime(&t);
  d = t.tm_mday; m = t.tm_mon; a = 1900 + t.tm_year;
}

void dibujarCalendario()
{
  tft.fillScreen(MI_NEGRO);
  for (int i = -1; i <= 1; i++) {
    int y = (i == -1) ? 8 : (i == 0 ? 78 : 148);
    int d, m, a; fechaConDelta(offsetVer + i, d, m, a);

    struct tm t = {0}; t.tm_mday = d; t.tm_mon = m; t.tm_year = a - 1900;
    time_t tiempo = mktime(&t);
    struct tm* infoDia = localtime(&tiempo);
    int diaSem = infoDia ? infoDia->tm_wday : 0;

    bool foco = (i == 0);
    tft.fillRect(4, y, 155, 62, foco ? MI_AZUL_OSC : MI_NEGRO);
    tft.drawRect(4, y, 155, 62, foco ? MI_BLANCO : MI_GRIS);

    tft.setTextSize(1);
    tft.setTextColor(foco ? MI_AMARILLO : MI_GRIS);
    tft.setCursor(10, y + 6);
    if (offsetVer + i == 0) tft.print("HOY");
    else if (offsetVer + i < 0) tft.print("AYER");
    else tft.print("MANANA");

    tft.setTextSize(2);
    tft.setTextColor(MI_BLANCO);
    tft.setCursor(10, y + 28);
    tft.print(DIA_SEM[diaSem]);

    tft.setTextSize(3);
    tft.setTextColor(foco ? MI_AMARILLO : MI_BLANCO);
    tft.setCursor(114, y + 12);
    tft.print(d);

    tft.setTextSize(1);
    tft.setTextColor(foco ? MI_BLANCO : MI_GRIS);
    tft.setCursor(104, y + 45);
    tft.printf("%s %d", MESES[m], a);
  }

  int d, m, a; fechaConDelta(offsetVer, d, m, a);
  InfoCultivo cult = calcularCultivo(d, m, a);

  tft.setTextSize(2); tft.setTextColor(cult.colorFase); tft.setCursor(168, 78); tft.print(cult.fase);
  tft.setTextColor(MI_BLANCO); tft.setCursor(168, 103); tft.printf("%d ppm", cult.ppm);
  tft.setCursor(168, 123); tft.printf("%.1f mS", cult.mS);

  if (cult.tocaRegar) { tft.setTextColor(MI_CIAN); tft.setCursor(168, 20); tft.print("TOCA REGAR"); }
  if (cult.tocaFertilizar) { tft.setTextColor(MI_AMARILLO); tft.setCursor(168, 35); tft.print("FERTILIZAR"); }

  tft.drawRect(168, 160, 140, 14, MI_GRIS);
  tft.fillRect(170, 162, (int)(136 * cult.progreso), 10, cult.colorFase);

  tft.drawRoundRect(255, 6, 58, 24, 4, MI_NARANJA);
  tft.setTextSize(1); tft.setTextColor(MI_NARANJA); tft.setCursor(263, 14); tft.print("CONFIG");
}

void dibujarFilaConfig(const char* label, int value, int y, bool selected, bool editing)
{
  uint16_t bg = selected ? MI_AZUL_OSC : MI_NEGRO;
  uint16_t borde = selected ? MI_AMARILLO : MI_GRIS;
  uint16_t txt = selected ? MI_AMARILLO : MI_BLANCO;

  tft.fillRoundRect(8, y - 4, 304, 34, 4, bg);
  tft.drawRoundRect(8, y - 4, 304, 34, 4, borde);
  tft.setTextColor(txt);
  tft.setTextSize(2);
  tft.setCursor(14, y + 6);
  tft.printf("%s: %d", label, value);

  if (editing) {
    tft.setTextColor(MI_CIAN);
    tft.setTextSize(1);
    tft.setCursor(235, y + 10);
    tft.print("EDITANDO");
  }
}

void dibujarVolver(int y, bool selected)
{
  uint16_t bg = selected ? MI_AZUL_OSC : MI_NEGRO;
  uint16_t borde = selected ? MI_NARANJA : MI_GRIS;
  uint16_t txt = selected ? MI_AMARILLO : MI_NARANJA;
  tft.fillRoundRect(8, y - 4, 304, 34, 4, bg);
  tft.drawRoundRect(8, y - 4, 304, 34, 4, borde);
  tft.setTextSize(2);
  tft.setTextColor(txt);
  tft.setCursor(14, y + 6);
  tft.print("VOLVER");
}

void dibujarConfiguracion()
{
  tft.fillScreen(MI_NEGRO);
  tft.setTextColor(MI_BLANCO);
  tft.setTextSize(2);
  tft.setCursor(10, 12);
  tft.print("CONFIG");

  dibujarFilaConfig("Inicio VEG", diaInicioVeg, 58, itemSeleccionado == CFG_ITEM_VEG, estadoUI == UI_EDIT_VEG);
  dibujarFilaConfig("Inicio FLOR", diaInicioFlor, 102, itemSeleccionado == CFG_ITEM_FLOR, estadoUI == UI_EDIT_FLOR);
  dibujarVolver(146, itemSeleccionado == CFG_ITEM_VOLVER);

  tft.setTextSize(1);
  tft.setTextColor(wifiConectado ? MI_VERDE : MI_ROJO);
  tft.setCursor(10, 190);
  tft.print(wifiConectado ? "WiFi: Conectado" : "WiFi: Sin conexion");

  tft.setTextColor(MI_GRIS);
  tft.setCursor(10, 208);
  if (estadoUI == UI_EDIT_VEG || estadoUI == UI_EDIT_FLOR) tft.print("Gira para ajustar, click para guardar.");
  else tft.print("Gira para navegar, click para seleccionar.");
}

void guardarConfiguracion()
{
  prefs.putInt("iniVeg", diaInicioVeg);
  prefs.putInt("iniFlor", diaInicioFlor);
}

void procesarPasoEncoder(int dir)
{
  if (dir == 0) return;

  switch (estadoUI)
  {
    case UI_CALENDARIO:
      offsetVer += dir;
      necesitaRedibujar = true;
      break;

    case UI_CONFIG:
      if (dir > 0) itemSeleccionado = (ItemConfig)min(2, (int)itemSeleccionado + 1);
      else itemSeleccionado = (ItemConfig)max(0, (int)itemSeleccionado - 1);
      necesitaRedibujar = true;
      break;

    case UI_EDIT_VEG:
      diaInicioVeg += dir;
      diaInicioVeg = constrain(diaInicioVeg, 0, diaInicioFlor - 1);
      necesitaRedibujar = true;
      break;

    case UI_EDIT_FLOR:
      diaInicioFlor += dir;
      diaInicioFlor = constrain(diaInicioFlor, diaInicioVeg + 1, 240);
      necesitaRedibujar = true;
      break;
  }
}

void procesarClickCorto()
{
  switch (estadoUI)
  {
    case UI_CALENDARIO:
      estadoUI = UI_CONFIG;
      itemSeleccionado = CFG_ITEM_VEG;
      necesitaRedibujar = true;
      break;

    case UI_CONFIG:
      if (itemSeleccionado == CFG_ITEM_VEG) estadoUI = UI_EDIT_VEG;
      else if (itemSeleccionado == CFG_ITEM_FLOR) estadoUI = UI_EDIT_FLOR;
      else estadoUI = UI_CALENDARIO;
      necesitaRedibujar = true;
      break;

    case UI_EDIT_VEG:
    case UI_EDIT_FLOR:
      guardarConfiguracion();
      estadoUI = UI_CONFIG;
      necesitaRedibujar = true;
      break;
  }
}

void leerEncoder()
{
  // Filtro temporal corto para estabilidad sin bloquear
  const unsigned long ENC_SAMPLE_MS = 1;
  if (millis() - encUltimaLecturaMs < ENC_SAMPLE_MS) return;
  encUltimaLecturaMs = millis();

  uint8_t a = (digitalRead(ENC_S1) == LOW) ? 1 : 0;
  uint8_t b = (digitalRead(ENC_S2) == LOW) ? 1 : 0;
  uint8_t ab = (a << 1) | b;

  static const int8_t transicion[16] = {
    0, -1,  1,  0,
    1,  0,  0, -1,
   -1,  0,  0,  1,
    0,  1, -1,  0
  };

  uint8_t idx = (encPrevAB << 2) | ab;
  int8_t mov = transicion[idx];
  if (mov != 0) {
    encAcumulado += mov;
    if (encAcumulado >= 4) {
      procesarPasoEncoder(1);
      encAcumulado = 0;
    } else if (encAcumulado <= -4) {
      procesarPasoEncoder(-1);
      encAcumulado = 0;
    }
  }
  encPrevAB = ab;
}

void leerBotonEncoder()
{
  const unsigned long DEBOUNCE_KEY_MS = 25;
  const unsigned long CLICK_MAX_MS = 350;

  bool raw = digitalRead(ENC_KEY);
  if (raw != keyLastRaw) {
    keyLastRaw = raw;
    keyLastChangeMs = millis();
  }

  if (millis() - keyLastChangeMs >= DEBOUNCE_KEY_MS && raw != keyStable) {
    keyStable = raw;
    if (keyStable == LOW) {
      keyPressStartMs = millis();
    } else {
      unsigned long dur = millis() - keyPressStartMs;
      if (dur <= CLICK_MAX_MS) procesarClickCorto();
    }
  }
}

void actualizarFechaSiEsPosible()
{
  if (WiFi.status() != WL_CONNECTED) {
    wifiConectado = false;
    if (millis() - ultimoIntentoWifiMs > 30000UL) {
      WiFi.begin(SSID, PASSWORD);
      ultimoIntentoWifiMs = millis();
    }
    return;
  }

  wifiConectado = true;
  struct tm ti;
  if (getLocalTime(&ti, 50)) {
    diaHoy = ti.tm_mday; mesHoy = ti.tm_mon; anioHoy = 1900 + ti.tm_year;
    return;
  }

  if (millis() - ultimoIntentoNtpMs > 600000UL) {
    configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, "pool.ntp.org", "time.nist.gov");
    ultimoIntentoNtpMs = millis();
  }
}

void setup()
{
  Serial.begin(115200);
  SPI.begin(12, 13, 11);

  pinMode(ENC_S1, INPUT_PULLUP);
  pinMode(ENC_S2, INPUT_PULLUP);
  pinMode(ENC_KEY, INPUT_PULLUP);

  encPrevAB = (((digitalRead(ENC_S1) == LOW) ? 1 : 0) << 1) | ((digitalRead(ENC_S2) == LOW) ? 1 : 0);
  keyStable = keyLastRaw = digitalRead(ENC_KEY);

  tft.init(240, 320);
  tft.setRotation(1);
  tft.invertDisplay(false);

  prefs.begin("cultivo", false);
  fechaBroteUnix = prefs.getLong("brote", 1710273600L);
  diaInicioVeg = prefs.getInt("iniVeg", 13);
  diaInicioFlor = prefs.getInt("iniFlor", 91);

  WiFi.begin(SSID, PASSWORD);
  unsigned long inicioIntento = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - inicioIntento) < 15000) {
    delay(500);
  }
  wifiConectado = (WiFi.status() == WL_CONNECTED);

  if (wifiConectado) {
    configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, "pool.ntp.org", "time.nist.gov");
    ultimoIntentoNtpMs = millis();
  }

  struct tm ti;
  if (wifiConectado && getLocalTime(&ti)) {
    diaHoy = ti.tm_mday; mesHoy = ti.tm_mon; anioHoy = 1900 + ti.tm_year;
  } else {
    char mesTxt[4] = {0}; int diaComp = 1; int anioComp = 2024;
    if (sscanf(__DATE__, "%3s %d %d", mesTxt, &diaComp, &anioComp) == 3) {
      diaHoy = diaComp; mesHoy = mesDesdeTexto(mesTxt); anioHoy = anioComp;
    } else {
      diaHoy = 1; mesHoy = 0; anioHoy = 2024;
    }
  }

  necesitaRedibujar = true;
}

void loop()
{
  actualizarFechaSiEsPosible();
  leerEncoder();
  leerBotonEncoder();

  if (necesitaRedibujar) {
    if (estadoUI == UI_CALENDARIO) dibujarCalendario();
    else dibujarConfiguracion();
    necesitaRedibujar = false;
  }
}
