// Añade esta línea al principio de tu archivo, justo debajo de los #include
// Si quieres calibrar el touch, DESCOMENTA la siguiente línea:
//#define DEBUG_TOUCH_RAW

// ================================================================
//  CULTIVO CALENDAR v2.1 - TOUCH CORREGIDO Y ESTABLE
//  Fixes:
//  - Colores invertidos: Añadido tft.invertDisplay(false);
//  - Mapeo del Touch: Ajustada la lógica de map() para rotación 1.
//  - Fondo negro y letras blancas: Asegurado con tft.fillScreen(MI_NEGRO)
//    y tft.setTextColor(MI_BLANCO) donde corresponde, y colores específicos.
//  - Zonas de Touch: El código ya implementa hitboxes específicas,
//    la corrección del mapeo del touch es clave para que funcionen bien.
// ================================================================

#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <XPT2046_Touchscreen.h>
#include <SPI.h>
#include <WiFi.h>
#include <time.h>
#include <Preferences.h>

// ===== PINES =====
#define TFT_CS    10
#define TFT_RST    9
#define TFT_DC     8

#define TOUCH_CS   7
#define TOUCH_IRQ  5

// ===== CALIBRACION TOUCH (AJUSTABLE) =====
// ¡IMPORTANTE! Estos valores son CRUCIALES para una calibración correcta de TU pantalla.
// Si el toque sigue desalineado, deberás ajustar estos valores usando el modo DEBUG_TOUCH_RAW.
// Los valores aquí son solo un punto de partida común.
#define TS_MINX 300   // <- ACTUALIZA ESTOS VALORES CON LOS QUE OBTENGAS EN LA CALIBRACIÓN
#define TS_MAXX 3800  // <- ACTUALIZA ESTOS VALORES CON LOS QUE OBTENGAS EN LA CALIBRACIÓN
#define TS_MINY 250   // <- ACTUALIZA ESTOS VALORES CON LOS QUE OBTENGAS EN LA CALIBRACIÓN
#define TS_MAXY 3850  // <- ACTUALIZA ESTOS VALORES CON LOS QUE OBTENGAS EN LA CALIBRACIÓN

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

// ===== HARDWARE =====
Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);
XPT2046_Touchscreen ts(TOUCH_CS, TOUCH_IRQ);
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

bool vistaCfg = false;
bool necesitaRedibujar = true;

// ===== RED =====
bool wifiConectado = false;
unsigned long ultimoIntentoNtpMs = 0;
unsigned long ultimoIntentoWifiMs = 0;

const long GMT_OFFSET_SEC = -21600;
const int DAYLIGHT_OFFSET_SEC = 0;

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

// ===== HITBOXES (Áreas de toque definidas) =====
#define CARD_X 4
#define CARD_W 155
#define CARD_H 62

#define CARD_Y0 8   // Tarjeta superior (Día anterior)
#define CARD_Y1 78  // Tarjeta central (Día actual) - No interactiva en tu código
#define CARD_Y2 148 // Tarjeta inferior (Día siguiente)

#define CFG_X 255   // Esquina superior derecha
#define CFG_Y 6
#define CFG_W 58
#define CFG_H 24

// ================================================================
//  TOUCH NORMALIZADO (CON MODO DEBUG PARA CALIBRACIÓN)
// ================================================================
bool leerTouch(int &tx, int &ty)
{
  if (!ts.touched()) return false;

  TS_Point p = ts.getPoint();

  // Filtrar toques fuera de un rango de presión razonable (ruido o toques muy suaves)
  if (p.z < 200 || p.z > 4000) return false;

  #ifdef DEBUG_TOUCH_RAW
    // --- MODO DEPURACIÓN: Muestra los valores RAW del touch ---
    // Usa estos valores para actualizar TS_MINX, TS_MAXX, TS_MINY, TS_MAXY
    Serial.printf("RAW TOUCH: P.x:%d P.y:%d P.z:%d\n", p.x, p.y, p.z);
    delay(150); // Pequeño delay para estabilizar la lectura y evitar spam en el Serial Monitor
    return false; // En modo depuración, no queremos que el touch active nada
  #else
    // --- Mapeo CORREGIDO para una pantalla ST7789 de 240x320 rotada a paisaje (320x240 lógica) ---
    // Ajusta la lógica de map() aquí si los ejes están invertidos o cambiados para tu pantalla.
    // Prueba con las 4 opciones explicadas en el procedimiento de calibración.

    // Opción 1 (La más común): p.y del sensor a X de la pantalla, p.x del sensor a Y de la pantalla (invertido)
    tx = map(p.y, TS_MINY, TS_MAXY, 0, 320); // Mapea p.y (sensor) a tx (pantalla)
    ty = map(p.x, TS_MAXX, TS_MINX, 0, 240); // Mapea p.x (sensor) a ty (pantalla), invertido

    // --- POSIBLES ALTERNATIVAS SI LA OPCIÓN 1 NO FUNCIONA ---
    // Descomenta y prueba una de estas a la vez después de ajustar TS_MIN/MAX:
    // Opción 2: p.y a X, p.x a Y (sin inversión en Y)
    // tx = map(p.y, TS_MINY, TS_MAXY, 0, 320);
    // ty = map(p.x, TS_MINX, TS_MAXX, 0, 240);

    // Opción 3: p.x a X, p.y a Y (invertido en Y)
    // tx = map(p.x, TS_MINX, TS_MAXX, 0, 320);
    // ty = map(p.y, TS_MAXY, TS_MINY, 0, 240);

    // Opción 4: p.x a X, p.y a Y (sin inversión en Y)
    // tx = map(p.x, TS_MINX, TS_MAXX, 0, 320);
    // ty = map(p.y, TS_MINY, TS_MAXY, 0, 240);

    // Si alguna dirección se siente invertida, puedes invertir el rango de destino (ej. 320, 0 en lugar de 0, 320)
    // o invertir el rango de origen (ej. TS_MAX, TS_MIN en lugar de TS_MIN, TS_MAX).


    // Restringimos las coordenadas resultantes para que estén dentro de los límites de la pantalla
    tx = constrain(tx, 0, 319);
    ty = constrain(ty, 0, 239);

    Serial.printf("Touch X:%d Y:%d (Raw P.x:%d P.y:%d P.z:%d)\n", tx, ty, p.x, p.y, p.z);
    delay(120); // Pequeño delay para evitar múltiples registros rápidos de un solo toque
    return true;
  #endif
}

// ================================================================
//  LOGICA CULTIVO
// ================================================================
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

  r.tocaFertilizar = (diaSemana == 0); // Fertilizar cada Domingo

  int diasVida = (tFecha > fechaBroteUnix) ?
                 (int)((tFecha - fechaBroteUnix) / 86400) : 0; // 86400 segundos en un día

  r.tocaRegar = (diasVida % 3 == 0); // Regar cada 3 días

  int ppmMin, ppmMax, inicioFase, finFase, diasRel;

  if (diasVida < diaInicioVeg)
  {
    r.fase = "PLANTULA";
    r.colorFase = MI_CIAN;

    ppmMin = 100;
    ppmMax = 250;

    inicioFase = 0;
    finFase = diaInicioVeg;

    diasRel = diasVida;
  }
  else if (diasVida < diaInicioFlor)
  {
    r.fase = "VEGETA";
    r.colorFase = MI_VERDE;

    ppmMin = 300;
    ppmMax = 450;

    inicioFase = diaInicioVeg;
    finFase = diaInicioFlor;

    diasRel = diasVida - diaInicioVeg;
  }
  else
  {
    int diasFlor = diasVida - diaInicioFlor;

    if (diasFlor <= 47)
    {
      r.fase = "FLOR T"; // Floración Temprana
      r.colorFase = MI_AMARILLO;

      ppmMin = 500;
      ppmMax = 1100;

      inicioFase = 0;
      finFase = 47;

      diasRel = diasFlor;
    }
    else
    {
      r.fase = "FLOR A"; // Floración Avanzada
      r.colorFase = MI_ROJO;

      ppmMin = 1200;
      ppmMax = 1600;

      inicioFase = 48;
      finFase = 68; // Asumiendo un ciclo total de floración de ~68 días

      diasRel = diasFlor;
    }
  }

  int rango = max(1, finFase - inicioFase);
  float prog = (float)(diasRel - inicioFase) / rango;
  prog = constrain(prog, 0, 1); // Asegura que el progreso esté entre 0 y 1

  r.progreso = prog;
  r.ppm = (int)(ppmMin + (ppmMax - ppmMin) * prog);
  r.mS = r.ppm / 500.0; // Conversión simple de PPM a mS (ej. 500 ppm = 1 mS)

  return r;
}

// ================================================================
//  UTILIDADES DE FECHA
// ================================================================
void fechaConDelta(int delta, int &d, int &m, int &a)
{
  struct tm t = {0};

  t.tm_mday = diaHoy + delta;
  t.tm_mon = mesHoy;
  t.tm_year = anioHoy - 1900;

  mktime(&t); // Normaliza la fecha (maneja desbordamientos de días/meses)

  d = t.tm_mday;
  m = t.tm_mon;
  a = 1900 + t.tm_year;
}

// ================================================================
//  DIBUJO CALENDARIO
// ================================================================
void dibujarCalendario()
{
  tft.fillScreen(MI_NEGRO); // Fondo negro según lo solicitado

  for (int i = -1; i <= 1; i++) // Dibuja la tarjeta del día anterior, actual y siguiente
  {
    int y = (i == -1) ? CARD_Y0 : (i == 0 ? CARD_Y1 : CARD_Y2); // Posición Y de la tarjeta

    int d, m, a;
    fechaConDelta(offsetVer + i, d, m, a); // Calcula la fecha para la tarjeta

    struct tm t = {0};
    t.tm_mday = d;
    t.tm_mon = m;
    t.tm_year = a - 1900;
    time_t tiempo = mktime(&t); // Obtiene el tiempo UNIX para calcular el día de la semana
    struct tm* infoDia = localtime(&tiempo);
    int diaSem = infoDia ? infoDia->tm_wday : 0; // 0=Domingo, 6=Sábado

    bool foco = (i == 0); // La tarjeta del día central está en foco

    // Dibujar el recuadro de la tarjeta
    tft.fillRect(CARD_X, y, CARD_W, CARD_H, foco ? MI_AZUL_OSC : MI_NEGRO); // Fondo de la tarjeta
    tft.drawRect(CARD_X, y, CARD_W, CARD_H, foco ? MI_BLANCO : MI_GRIS); // Borde de la tarjeta

    // Título (HOY, AYER, MAÑANA)
    tft.setTextSize(1);
    tft.setTextColor(foco ? MI_AMARILLO : MI_GRIS);
    tft.setCursor(CARD_X + 6, y + 6);
    if (offsetVer + i == 0)      tft.print("HOY");
    else if (offsetVer + i < 0)  tft.print("AYER");
    else                         tft.print("MAÑANA");

    // Día de la semana
    tft.setTextSize(2);
    tft.setTextColor(MI_BLANCO); // Letras blancas
    tft.setCursor(CARD_X + 6, y + 28);
    tft.print(DIA_SEM[diaSem]);

    // Número del día
    tft.setTextSize(3);
    tft.setTextColor(foco ? MI_AMARILLO : MI_BLANCO); // Letras amarillas si en foco, blancas si no
    tft.setCursor(CARD_X + CARD_W - 45, y + 12);
    tft.print(d);

    // Mes y Año
    tft.setTextSize(1);
    tft.setTextColor(foco ? MI_BLANCO : MI_GRIS); // Letras blancas si en foco, grises si no
    tft.setCursor(CARD_X + CARD_W - 55, y + 45);
    tft.printf("%s %d", MESES[m], a);
  }

  // --- Información de Cultivo para el día en foco (tarjeta central) ---
  int d, m, a;
  fechaConDelta(offsetVer, d, m, a); // Obtiene la fecha del día actual
  InfoCultivo cult = calcularCultivo(d, m, a);

  // Fase del cultivo
  tft.setTextSize(2);
  tft.setTextColor(cult.colorFase);
  tft.setCursor(168, 78);
  tft.print(cult.fase);

  // PPM
  tft.setTextColor(MI_BLANCO); // Letras blancas
  tft.setCursor(168, 103);
  tft.printf("%d ppm", cult.ppm);

  // mS
  tft.setCursor(168, 123);
  tft.printf("%.1f mS", cult.mS);

  // Mensajes de acción (Regar/Fertilizar)
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

  // Barra de progreso de la fase
  tft.drawRect(168, 160, 140, 14, MI_GRIS); // Borde de la barra
  tft.fillRect(170, 162, (int)(136 * cult.progreso), 10, cult.colorFase); // Relleno de la barra

  // Botón de Configuración (esquina superior derecha)
  tft.drawRoundRect(CFG_X, CFG_Y, CFG_W, CFG_H, 4, MI_NARANJA);
  tft.setTextSize(1);
  tft.setTextColor(MI_NARANJA);
  tft.setCursor(CFG_X + 8, CFG_Y + 8);
  tft.print("CONFIG");
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
  tft.setCursor(120, 84);
  tft.printf("dia %d", diaInicioVeg);

  tft.setCursor(10, 102);
  tft.print("Inicio FLOR:");
  tft.setCursor(120, 102);
  tft.printf("dia %d", diaInicioFlor);

  tft.drawRoundRect(10, 200, 100, 30, 4, MI_NARANJA);
  tft.setTextColor(MI_NARANJA);
  tft.setCursor(36, 210);
  tft.print("VOLVER");

  tft.setTextColor(MI_GRIS);
  tft.setCursor(10, 128);
  tft.print("Opciones:");
  tft.setCursor(10, 142);
  tft.print("- Ver estado WiFi");
  tft.setCursor(10, 154);
  tft.print("- Ver offset de dia");
  tft.setCursor(10, 166);
  tft.print("- Revisar inicio VEG/FLOR");
}

// ================================================================
//  SETUP
// ================================================================
void setup()
{
  Serial.begin(115200);

  SPI.begin(12, 13, 11);

  tft.init(240, 320);
  tft.setRotation(1);

  tft.invertDisplay(false); // Desactiva la inversión de colores de la pantalla

  ts.begin();
  ts.setRotation(1); // Rota el sistema de coordenadas táctiles para que coincida con la pantalla

  prefs.begin("cultivo", false);

  fechaBroteUnix = prefs.getLong("brote", 1710273600L);
  diaInicioVeg = prefs.getInt("iniVeg", 13);
  diaInicioFlor = prefs.getInt("iniFlor", 91);

  Serial.print("Conectando a WiFi ");
  Serial.println(SSID);
  WiFi.begin(SSID, PASSWORD);
  unsigned long inicioIntento = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - inicioIntento) < 15000) {
    delay(500);
    Serial.print(".");
  }
  wifiConectado = (WiFi.status() == WL_CONNECTED);
  if (wifiConectado) {
    Serial.println("\nWiFi conectado!");
  } else {
    Serial.println("\nNo se pudo conectar a WiFi, continuando sin NTP.");
  }

  if (wifiConectado) {
    configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, "pool.ntp.org", "time.nist.gov");
    ultimoIntentoNtpMs = millis();
  }

  struct tm ti;
  if (wifiConectado && getLocalTime(&ti))
  {
    diaHoy = ti.tm_mday;
    mesHoy = ti.tm_mon;
    anioHoy = 1900 + ti.tm_year;
    Serial.printf("Fecha NTP obtenida: %02d/%02d/%d %02d:%02d:%02d\n",
                  diaHoy, mesHoy + 1, anioHoy, ti.tm_hour, ti.tm_min, ti.tm_sec);
  }
  else
  {
    Serial.println("Error al obtener la hora del NTP. Usando fecha de compilacion como respaldo.");
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
      mesHoy = 0; // Enero
      anioHoy = 2024;
    }
  }

  necesitaRedibujar = true;
}

// ================================================================
//  LOOP
// ================================================================
void actualizarFechaSiEsPosible()
{
  // Si se perdió WiFi, reintenta conexión cada 30 segundos.
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

void loop()
{
  actualizarFechaSiEsPosible();

  int tx, ty;

  // Solo procesa el toque si no estamos en modo DEBUG_TOUCH_RAW
  #ifndef DEBUG_TOUCH_RAW
    if (leerTouch(tx, ty))
    {
      if (vistaCfg)
      {
        if (tx >= 10 && tx <= 110 && ty >= 200 && ty <= 230)
        {
          vistaCfg = false;
          necesitaRedibujar = true;
        }
      }
      else
      {
        if (tx >= CFG_X && tx <= CFG_X + CFG_W && ty >= CFG_Y && ty <= CFG_Y + CFG_H)
        {
          vistaCfg = true;
          necesitaRedibujar = true;
        }
        else if (tx >= CARD_X && tx <= CARD_X + CARD_W && ty >= CARD_Y0 && ty <= CARD_Y0 + CARD_H)
        {
          offsetVer--;
          necesitaRedibujar = true;
        }
        else if (tx >= CARD_X && tx <= CARD_X + CARD_W && ty >= CARD_Y2 && ty <= CARD_Y2 + CARD_H)
        {
          offsetVer++;
          necesitaRedibujar = true;
        }
      }
    }
  #else
    // Si DEBUG_TOUCH_RAW está activo, llamamos a leerTouch solo para que imprima
    leerTouch(tx, ty);
    // No hacemos nada más en el loop principal para evitar interacciones accidentales.
  #endif

  if (necesitaRedibujar)
  {
    if (vistaCfg) {
      dibujarConfiguracion();
    } else {
      dibujarCalendario();
    }
    necesitaRedibujar = false;
  }
}
