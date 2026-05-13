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

#define ENC_S1     2
#define ENC_S2     3
#define ENC_KEY    4

// ===== COLORES =====
#define MI_NEGRO    0x0000 // Negro
#define MI_BLANCO   0xFFFF // Blanco
#define MI_VERDE    0x07E0 // Verde
#define MI_ROJO     0xF800 // Rojo
#define MI_CIAN     0x07FF // Cian
#define MI_NARANJA  0xFBE0 // Naranja
#define MI_GRIS     0x4208 // Gris
#define MI_AMARILLO 0xFFE0 // Amarillo
#define MI_AZUL_OSC 0x2945 // Azul Oscuro

// ===== ESTRUCTURA =====
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

// ===== HARDWARE =====
Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);
Preferences prefs;

// ===== WIFI =====
const char* SSID = "IZZI-367E";
const char* PASSWORD = "ehwa3pX7btcw";

// ===== TIEMPO =====
int diaHoy, mesHoy, anioHoy;
time_t fechaBroteUnix;

int diaInicioVeg;
int diaInicioFlor;

int offsetVer = 0;
bool necesitaRedibujar = true;
EstadoUI estadoUI = UI_CALENDARIO;
int indiceCfg = 0; // 0=VEG, 1=FLOR, 2=VOLVER

// ===== RED =====
bool wifiConectado = false;
unsigned long ultimoIntentoNtpMs = 0;
unsigned long ultimoIntentoWifiMs = 0;

const long GMT_OFFSET_SEC = -21600;
const int DAYLIGHT_OFFSET_SEC = 0;

// ===== ENCODER =====
volatile int8_t deltaEncoder = 0;
uint8_t ultimoEstadoAB = 0;
int ultimoCLK = HIGH;
int ultimoDT = HIGH;
bool ultimoKey = HIGH;
unsigned long ultimoMsEncoder = 0;
unsigned long ultimoMsKey = 0;
unsigned long ultimoMsPaso = 0;

const unsigned long DEBOUNCE_ENCODER_MS = 2;
const unsigned long STEP_GAP_MS = 3;
const unsigned long DEBOUNCE_KEY_MS = 45;

// ===== MESES =====
const char* MESES[] = {
"Ene","Feb","Mar","Abr","May","Jun",
"Jul","Ago","Sep","Oct","Nov","Dic"
};

const char* DIA_SEM[] = {
"Dom","Lun","Mar","Mie","Jue","Vie","Sab"
};

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

// ===== ZONAS UI (para selector visual en CONFIG) =====
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

#define VEG_VAL_X   194
#define VEG_VAL_Y   80
#define VEG_VAL_W   70
#define VEG_VAL_H   26

#define FLOR_VAL_X   194
#define FLOR_VAL_Y   116
#define FLOR_VAL_W   70
#define FLOR_VAL_H   26

void aplicarPasoEncoder(int8_t dir)
{
  if (dir == 0) return;

  switch (estadoUI)
  {
    case UI_CALENDARIO:
      offsetVer += dir;
      necesitaRedibujar = true;
      break;

    case UI_CONFIG:
      indiceCfg += (dir > 0) ? 1 : -1;
      if (indiceCfg < 0) indiceCfg = 2;
      if (indiceCfg > 2) indiceCfg = 0;
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

void manejarClick()
{
  switch (estadoUI)
  {
    case UI_CALENDARIO:
      estadoUI = UI_CONFIG;
      indiceCfg = 0;
      necesitaRedibujar = true;
      break;

    case UI_CONFIG:
      if (indiceCfg == 0)
      {
        estadoUI = UI_EDIT_VEG;
      }
      else if (indiceCfg == 1)
      {
        estadoUI = UI_EDIT_FLOR;
      }
      else
      {
        estadoUI = UI_CALENDARIO;
      }
      necesitaRedibujar = true;
      break;

    case UI_EDIT_VEG:
      prefs.putInt("iniVeg", diaInicioVeg);
      estadoUI = UI_CONFIG;
      necesitaRedibujar = true;
      break;

    case UI_EDIT_FLOR:
      prefs.putInt("iniFlor", diaInicioFlor);
      estadoUI = UI_CONFIG;
      necesitaRedibujar = true;
      break;
  }
}

void actualizarEncoder()
{
  unsigned long ahora = millis();

  int clk = digitalRead(ENC_S1);
  int dt = digitalRead(ENC_S2);

  if ((clk != ultimoCLK || dt != ultimoDT) && (ahora - ultimoMsEncoder >= DEBOUNCE_ENCODER_MS))
  {
    ultimoMsEncoder = ahora;
    uint8_t estadoAB = (clk << 1) | dt;
    uint8_t trans = (ultimoEstadoAB << 2) | estadoAB;

    int8_t paso = 0;
    if (trans == 0b1101 || trans == 0b0100 || trans == 0b0010 || trans == 0b1011)
      paso = 1;
    else if (trans == 0b1110 || trans == 0b0111 || trans == 0b0001 || trans == 0b1000)
      paso = -1;

    if (paso != 0 && (ahora - ultimoMsPaso >= STEP_GAP_MS))
    {
      deltaEncoder += paso;
      ultimoMsPaso = ahora;
    }

    ultimoEstadoAB = estadoAB;
    ultimoCLK = clk;
    ultimoDT = dt;
  }

  bool key = digitalRead(ENC_KEY);
  if (key != ultimoKey && (ahora - ultimoMsKey >= DEBOUNCE_KEY_MS))
  {
    ultimoMsKey = ahora;
    ultimoKey = key;
    if (key == LOW)
      manejarClick();
  }
}

void consumirEncoder()
{
  int8_t pasos = deltaEncoder;
  if (pasos == 0) return;

  deltaEncoder = 0;

  while (pasos > 0)
  {
    aplicarPasoEncoder(1);
    pasos--;
  }
  while (pasos < 0)
  {
    aplicarPasoEncoder(-1);
    pasos++;
  }
}

InfoCultivo calcularCultivo(int d, int m, int a)
{
  InfoCultivo r;
  struct tm td = {0};
  td.tm_mday = d;
  td.tm_mon = m;
  td.tm_year = a - 1900;

  time_t tFecha = mktime(&td);
  struct tm* infoFecha = localtime(&tFecha);
  int diaSemana = infoFecha ? infoFecha->tm_wday : 0;

  r.tocaFertilizar = (diaSemana == 0);
  int diasVida = (tFecha > fechaBroteUnix) ? (int)((tFecha - fechaBroteUnix) / 86400) : 0;
  r.tocaRegar = (diasVida % 3 == 0);

  int ppmMin, ppmMax, inicioFase, finFase, diasRel;
  if (diasVida < diaInicioVeg)
  {
    r.fase = "PLANTULA"; r.colorFase = MI_CIAN;
    ppmMin = 100; ppmMax = 250; inicioFase = 0; finFase = diaInicioVeg; diasRel = diasVida;
  }
  else if (diasVida < diaInicioFlor)
  {
    r.fase = "VEGETA"; r.colorFase = MI_VERDE;
    ppmMin = 300; ppmMax = 450; inicioFase = diaInicioVeg; finFase = diaInicioFlor; diasRel = diasVida - diaInicioVeg;
  }
  else
  {
    int diasFlor = diasVida - diaInicioFlor;
    if (diasFlor <= 47)
    {
      r.fase = "FLOR T"; r.colorFase = MI_AMARILLO;
      ppmMin = 500; ppmMax = 1100; inicioFase = 0; finFase = 47; diasRel = diasFlor;
    }
    else
    {
      r.fase = "FLOR A"; r.colorFase = MI_ROJO;
      ppmMin = 1200; ppmMax = 1600; inicioFase = 48; finFase = 68; diasRel = diasFlor;
    }
  }

  int rango = max(1, finFase - inicioFase);
  float prog = (float)(diasRel - inicioFase) / rango;
  prog = constrain(prog, 0, 1);

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
  d = t.tm_mday;
  m = t.tm_mon;
  a = 1900 + t.tm_year;
}

void dibujarCalendario()
{
  tft.fillScreen(MI_NEGRO);

  for (int i = -1; i <= 1; i++)
  {
    int y = (i == -1) ? CARD_Y0 : (i == 0 ? CARD_Y1 : CARD_Y2);
    int d, m, a;
    fechaConDelta(offsetVer + i, d, m, a);

    struct tm t = {0};
    t.tm_mday = d; t.tm_mon = m; t.tm_year = a - 1900;
    time_t tiempo = mktime(&t);
    struct tm* infoDia = localtime(&tiempo);
    int diaSem = infoDia ? infoDia->tm_wday : 0;

    bool foco = (i == 0);
    tft.fillRect(CARD_X, y, CARD_W, CARD_H, foco ? MI_AZUL_OSC : MI_NEGRO);
    tft.drawRect(CARD_X, y, CARD_W, CARD_H, foco ? MI_BLANCO : MI_GRIS);

    tft.setTextSize(1);
    tft.setTextColor(foco ? MI_AMARILLO : MI_GRIS);
    tft.setCursor(CARD_X + 6, y + 6);
    if (offsetVer + i == 0) tft.print("HOY");
    else if (offsetVer + i < 0) tft.print("AYER");
    else tft.print("MAÑANA");

    tft.setTextSize(2);
    tft.setTextColor(MI_BLANCO);
    tft.setCursor(CARD_X + 6, y + 28);
    tft.print(DIA_SEM[diaSem]);

    tft.setTextSize(3);
    tft.setTextColor(foco ? MI_AMARILLO : MI_BLANCO);
    tft.setCursor(CARD_X + CARD_W - 45, y + 12);
    tft.print(d);

    tft.setTextSize(1);
    tft.setTextColor(foco ? MI_BLANCO : MI_GRIS);
    tft.setCursor(CARD_X + CARD_W - 55, y + 45);
    tft.printf("%s %d", MESES[m], a);
  }

  int d, m, a;
  fechaConDelta(offsetVer, d, m, a);
  InfoCultivo cult = calcularCultivo(d, m, a);

  tft.setTextSize(2);
  tft.setTextColor(cult.colorFase);
  tft.setCursor(168, 78);
  tft.print(cult.fase);

  tft.setTextColor(MI_BLANCO);
  tft.setCursor(168, 103);
  tft.printf("%d ppm", cult.ppm);

  tft.setCursor(168, 123);
  tft.printf("%.1f mS", cult.mS);

  if (cult.tocaRegar)
  {
    tft.setTextColor(MI_CIAN);
    tft.setCursor(168, 20);
    tft.print("TOCA REGAR");
  }
  if (cult.tocaFertilizar)
  {
    tft.setTextColor(MI_AMARILLO);
    tft.setCursor(168, 35);
    tft.print("FERTILIZAR");
  }

  tft.drawRect(168, 160, 140, 14, MI_GRIS);
  tft.fillRect(170, 162, (int)(136 * cult.progreso), 10, cult.colorFase);

  tft.drawRoundRect(CFG_X, CFG_Y, CFG_W, CFG_H, 4, MI_NARANJA);
  tft.setTextSize(1);
  tft.setTextColor(MI_NARANJA);
  tft.setCursor(CFG_X + 8, CFG_Y + 8);
  tft.print("CONFIG");
}

void dibujarSelectorConfig()
{
  uint16_t colVeg = (indiceCfg == 0) ? MI_NARANJA : MI_GRIS;
  uint16_t colFlor = (indiceCfg == 1) ? MI_NARANJA : MI_GRIS;
  uint16_t colVolver = (indiceCfg == 2) ? MI_NARANJA : MI_NARANJA;

  if (estadoUI == UI_EDIT_VEG) colVeg = MI_AMARILLO;
  if (estadoUI == UI_EDIT_FLOR) colFlor = MI_AMARILLO;

  tft.drawRoundRect(VEG_VAL_X - 6, VEG_VAL_Y - 4, VEG_VAL_W + 12, VEG_VAL_H + 8, 4, colVeg);
  tft.drawRoundRect(FLOR_VAL_X - 6, FLOR_VAL_Y - 4, FLOR_VAL_W + 12, FLOR_VAL_H + 8, 4, colFlor);
  tft.drawRoundRect(BTN_VOLVER_X, BTN_VOLVER_Y, BTN_VOLVER_W, BTN_VOLVER_H, 4, colVolver);
}

void dibujarConfiguracion()
{
  tft.fillScreen(MI_NEGRO);
  tft.setTextColor(MI_BLANCO);
  tft.setTextSize(2);
  tft.setCursor(10, 12);
  tft.print("CONFIGURACION");

  tft.setTextSize(1);
  tft.setCursor(10, 48);
  tft.print("WiFi:");
  tft.setTextColor(wifiConectado ? MI_VERDE : MI_ROJO);
  tft.setCursor(50, 48);
  tft.print(wifiConectado ? "Conectado" : "Sin conexion");

  tft.setTextColor(MI_BLANCO);
  tft.setCursor(10, 66);
  tft.print("Offset dia visible:");
  tft.setCursor(120, 66);
  tft.printf("%d", offsetVer);

  tft.setCursor(10, 84);
  tft.print("Inicio VEG:");
  tft.setCursor(10, 120);
  tft.print("Inicio FLOR:");

  tft.fillRoundRect(VEG_VAL_X, VEG_VAL_Y, VEG_VAL_W, VEG_VAL_H, 4, MI_NEGRO);
  tft.drawRoundRect(VEG_VAL_X, VEG_VAL_Y, VEG_VAL_W, VEG_VAL_H, 4, MI_GRIS);
  tft.setTextSize(2);
  tft.setTextColor(MI_BLANCO);
  tft.setCursor(VEG_VAL_X + 10, VEG_VAL_Y + 6);
  tft.printf("%d", diaInicioVeg);

  tft.fillRoundRect(FLOR_VAL_X, FLOR_VAL_Y, FLOR_VAL_W, FLOR_VAL_H, 4, MI_NEGRO);
  tft.drawRoundRect(FLOR_VAL_X, FLOR_VAL_Y, FLOR_VAL_W, FLOR_VAL_H, 4, MI_GRIS);
  tft.setCursor(FLOR_VAL_X + 10, FLOR_VAL_Y + 6);
  tft.printf("%d", diaInicioFlor);

  tft.drawRoundRect(10, 200, 100, 30, 4, MI_NARANJA);
  tft.setTextColor(MI_NARANJA);
  tft.setCursor(36, 210);
  tft.print("VOLVER");

  tft.setTextSize(1);
  tft.setTextColor(MI_GRIS);
  tft.setCursor(10, 160);
  tft.print("Use click para editar/confirmar.");

  dibujarSelectorConfig();
}

void actualizarFechaSiEsPosible()
{
  if (WiFi.status() != WL_CONNECTED)
  {
    wifiConectado = false;
    if (millis() - ultimoIntentoWifiMs > 30000UL)
    {
      WiFi.begin(SSID, PASSWORD);
      ultimoIntentoWifiMs = millis();
    }
    return;
  }

  wifiConectado = true;

  struct tm ti;
  if (getLocalTime(&ti, 50))
  {
    diaHoy = ti.tm_mday;
    mesHoy = ti.tm_mon;
    anioHoy = 1900 + ti.tm_year;
    return;
  }

  if (millis() - ultimoIntentoNtpMs > 600000UL)
  {
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

  ultimoCLK = digitalRead(ENC_S1);
  ultimoDT = digitalRead(ENC_S2);
  ultimoEstadoAB = (ultimoCLK << 1) | ultimoDT;
  ultimoKey = digitalRead(ENC_KEY);

  tft.init(240, 320);
  tft.setRotation(1);
  tft.invertDisplay(false);

  prefs.begin("cultivo", false);

  fechaBroteUnix = prefs.getLong("brote", 1710273600L);
  diaInicioVeg = prefs.getInt("iniVeg", 13);
  diaInicioFlor = prefs.getInt("iniFlor", 91);

  Serial.print("Conectando a WiFi ");
  Serial.println(SSID);
  WiFi.begin(SSID, PASSWORD);
  unsigned long inicioIntento = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - inicioIntento) < 15000)
  {
    delay(500);
    Serial.print(".");
  }
  wifiConectado = (WiFi.status() == WL_CONNECTED);

  if (wifiConectado)
  {
    Serial.println("\nWiFi conectado!");
    configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, "pool.ntp.org", "time.nist.gov");
    ultimoIntentoNtpMs = millis();
  }
  else
  {
    Serial.println("\nNo se pudo conectar a WiFi, continuando sin NTP.");
  }

  struct tm ti;
  if (wifiConectado && getLocalTime(&ti))
  {
    diaHoy = ti.tm_mday;
    mesHoy = ti.tm_mon;
    anioHoy = 1900 + ti.tm_year;
  }
  else
  {
    char mesTxt[4] = {0};
    int diaComp = 1;
    int anioComp = 2024;
    if (sscanf(__DATE__, "%3s %d %d", mesTxt, &diaComp, &anioComp) == 3)
    {
      diaHoy = diaComp;
      mesHoy = mesDesdeTexto(mesTxt);
      anioHoy = anioComp;
    }
    else
    {
      diaHoy = 1;
      mesHoy = 0;
      anioHoy = 2024;
    }
  }

  necesitaRedibujar = true;
}

void loop()
{
  actualizarFechaSiEsPosible();
  actualizarEncoder();
  consumirEncoder();

  if (necesitaRedibujar)
  {
    if (estadoUI == UI_CALENDARIO) dibujarCalendario();
    else dibujarConfiguracion();
    necesitaRedibujar = false;
  }
}
