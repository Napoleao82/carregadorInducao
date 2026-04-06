#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <Adafruit_INA219.h>
#include <esp_system.h>
#include <time.h>

// Wi-Fi: defina WIFI_SSID e WIFI_PASS em secrets.h (copie secrets.h.example).
#include "secrets.h"
#ifndef WIFI_SSID
#error "Crie ServerEsp32/secrets.h a partir de secrets.h.example com WIFI_SSID e WIFI_PASS."
#endif

// I2C: SDA/SCL com pull-up (os breakouts INA219 costumam trazer). Todos os GND juntos.
// Alimentar INA219 em 3V3 com o ESP32 (evitar SDA/SCL a 5 V nos pinos do ESP32).
#define PIN_SDA 21
#define PIN_SCL 22
#define I2C_HZ_SLOW 100000U
#define I2C_HZ_FAST 400000U

Adafruit_INA219 ina_prim(0x41);
Adafruit_INA219 ina_sec(0x40);
Adafruit_INA219 ina_bms_bat(0x44);

struct InaChannel {
  Adafruit_INA219 *sensor;
  const char *id;
  const char *name;
  uint8_t addr;
  bool ok;
  uint32_t avg_samples;
  double sum_power_mw;
  double sum_current_ma;
  double energy_mWs;
};

static constexpr size_t INA_COUNT = 3;
static InaChannel ina_channels[INA_COUNT] = {
    {&ina_prim, "indutor_primario", "Indutor Primario", 0x41, false, 0, 0.0, 0.0, 0.0},
    {&ina_sec, "indutor_secundario_bms", "Indutor Secundario -> BMS", 0x40, false, 0, 0.0, 0.0, 0.0},
    {&ina_bms_bat, "bms_bateria_1650", "BMS -> Bateria 1650", 0x44, false, 0, 0.0, 0.0, 0.0},
};

WebServer server(80);

static bool g_time_synced = false;
static bool g_proc_running = false;
static unsigned long g_proc_start_ms = 0;
static unsigned long g_proc_end_ms = 0;
static time_t g_proc_start_wall = 0;
static time_t g_proc_end_wall = 0;

static constexpr size_t I2C_SCAN_MAX = 16;
static uint8_t i2c_scan_addrs[I2C_SCAN_MAX];
static uint8_t i2c_scan_count = 0;

static const char *esp32ResetReasonStr() {
  switch (esp_reset_reason()) {
    case ESP_RST_UNKNOWN:
      return "UNKNOWN";
    case ESP_RST_POWERON:
      return "POWERON";
    case ESP_RST_EXT:
      return "EXT";
    case ESP_RST_SW:
      return "SW";
    case ESP_RST_PANIC:
      return "PANIC";
    case ESP_RST_INT_WDT:
      return "INT_WDT";
    case ESP_RST_TASK_WDT:
      return "TASK_WDT";
    case ESP_RST_WDT:
      return "WDT";
    case ESP_RST_DEEPSLEEP:
      return "DEEPSLEEP";
    case ESP_RST_BROWNOUT:
      return "BROWNOUT";
    case ESP_RST_SDIO:
      return "SDIO";
    default:
      return "?";
  }
}

static void runI2cScan() {
  i2c_scan_count = 0;
  for (uint8_t addr = 1; addr < 127 && i2c_scan_count < I2C_SCAN_MAX; addr++) {
    Wire.beginTransmission(addr);
    uint8_t err = Wire.endTransmission();
    if (err == 0) {
      i2c_scan_addrs[i2c_scan_count++] = addr;
    }
  }
}

static bool inaAllModulesOk() {
  for (size_t i = 0; i < INA_COUNT; i++) {
    if (!ina_channels[i].ok) {
      return false;
    }
  }
  return true;
}

/** Nova tentativa de begin + scan (hot-plug / cabo solto) e JSON sempre atual. */
static void refreshI2cIfInaFaulty() {
  if (inaAllModulesOk()) {
    return;
  }
  Wire.setClock(I2C_HZ_SLOW);
  for (size_t i = 0; i < INA_COUNT; i++) {
    InaChannel &ch = ina_channels[i];
    if (!ch.ok) {
      ch.ok = ch.sensor->begin(&Wire);
      if (ch.ok) {
        ch.sensor->setCalibration_32V_2A();
      }
    }
  }
  runI2cScan();
  if (inaAllModulesOk()) {
    Wire.setClock(I2C_HZ_FAST);
  }
}

static void logIna219DiagnosticsAllFailed(uint32_t clock_hz) {
  Serial.println();
  Serial.println("======== INA219 / I2C diagnostico ========");
  Serial.printf("SDA=GPIO%d SCL=GPIO%d clock=%lu Hz\n", PIN_SDA, PIN_SCL,
                static_cast<unsigned long>(clock_hz));
  Serial.println("Enderecos esperados: 0x41, 0x40, 0x44");

  runI2cScan();
  if (i2c_scan_count == 0) {
    Serial.println("Scan I2C: nenhum dispositivo respondeu.");
    Serial.println("Dicas: VCC 3V3 (nao 5V no ESP), GND comum, SDA/SCL trocados?,");
    Serial.println("       fios curtos, pull-ups (modulo costuma ter).");
  } else {
    Serial.printf("Scan I2C: %u dispositivo(s):\n", static_cast<unsigned>(i2c_scan_count));
    for (uint8_t i = 0; i < i2c_scan_count; i++) {
      Serial.printf("  0x%02X\n", i2c_scan_addrs[i]);
    }
  }
  Serial.println("==========================================");
  Serial.println();
}

static String i2cAddrHex(uint8_t addr) {
  String s = "0x";
  if (addr < 16) {
    s += "0";
  }
  s += String(addr, HEX);
  return s;
}

static constexpr unsigned long INA_SAMPLE_MS = 100;
static unsigned long lastInaSampleMs = 0;

static void tickIna219Average() {
  unsigned long now = millis();
  if (now - lastInaSampleMs < INA_SAMPLE_MS) {
    return;
  }
  lastInaSampleMs = now;

  const double dt_s = static_cast<double>(INA_SAMPLE_MS) / 1000.0;

  for (size_t i = 0; i < INA_COUNT; i++) {
    InaChannel &ch = ina_channels[i];
    if (!ch.ok) {
      continue;
    }
    float p = ch.sensor->getPower_mW();
    float c = ch.sensor->getCurrent_mA();
    ch.energy_mWs += static_cast<double>(p) * dt_s;
    ch.avg_samples++;
    ch.sum_power_mw += static_cast<double>(p);
    ch.sum_current_ma += static_cast<double>(c);
  }
}

static String jsonEscape(const String &s) {
  String out;
  out.reserve(s.length() + 8);
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    switch (c) {
      case '"':  out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:   out += c; break;
    }
  }
  return out;
}

static void appendOneInaModuleJson(String &body, InaChannel &ch) {
  body += "\"";
  body += ch.id;
  body += "\":{\"name\":\"";
  body += jsonEscape(String(ch.name));
  body += "\",\"i2c_addr\":\"";
  body += i2cAddrHex(ch.addr);
  body += "\",\"ok\":";
  body += ch.ok ? "true" : "false";

  if (!ch.ok) {
    body += "}";
    return;
  }

  float shunt_mv = ch.sensor->getShuntVoltage_mV();
  float bus_v = ch.sensor->getBusVoltage_V();
  float current_ma = ch.sensor->getCurrent_mA();
  float load_v = bus_v + (shunt_mv / 1000.0f);
  float power_mw = ch.sensor->getPower_mW();

  body += ",\"instant\":{";
  body += "\"bus_voltage_v\":" + String(bus_v, 3) + ",";
  body += "\"shunt_voltage_mv\":" + String(shunt_mv, 3) + ",";
  body += "\"load_voltage_v\":" + String(load_v, 3) + ",";
  body += "\"current_ma\":" + String(current_ma, 2) + ",";
  body += "\"power_mw\":" + String(power_mw, 2);
  body += "},\"avg\":{";

  if (ch.avg_samples > 0) {
    double avg_p = ch.sum_power_mw / static_cast<double>(ch.avg_samples);
    double avg_c = ch.sum_current_ma / static_cast<double>(ch.avg_samples);
    body += "\"power_mw\":" + String(avg_p, 2) + ",";
    body += "\"current_ma\":" + String(avg_c, 2) + ",";
    body += "\"samples\":" + String(ch.avg_samples);
  } else {
    body += "\"power_mw\":0,\"current_ma\":0,\"samples\":0";
  }
  body += "},\"energy\":{";
  {
    const double wh = ch.energy_mWs / 3600000.0;
    const double mwh = ch.energy_mWs / 3600.0;
    body += "\"mws\":" + String(ch.energy_mWs, 2) + ",";
    body += "\"wh\":" + String(wh, 6) + ",";
    body += "\"mwh\":" + String(mwh, 4);
  }
  body += "}}";
}

static void appendIna219Json(String &body) {
  refreshI2cIfInaFaulty();
  const bool all_ok = inaAllModulesOk();

  body += ",\"ina219\":{";
  body += "\"all_ok\":";
  body += all_ok ? "true" : "false";
  body += ",\"modules\":{";

  for (size_t i = 0; i < INA_COUNT; i++) {
    if (i > 0) {
      body += ",";
    }
    appendOneInaModuleJson(body, ina_channels[i]);
  }

  body += "}";

  if (!all_ok) {
    body += ",\"i2c_sda\":" + String(PIN_SDA) + ",";
    body += "\"i2c_scl\":" + String(PIN_SCL) + ",";
    body += "\"i2c_detected\":[";
    for (uint8_t i = 0; i < i2c_scan_count; i++) {
      if (i) {
        body += ",";
      }
      body += "\"";
      body += i2cAddrHex(i2c_scan_addrs[i]);
      body += "\"";
    }
    body += "]";
    if (i2c_scan_count == 0) {
      body += ",\"i2c_hint\":\"Nenhum ACK no barramento: 3V3+GND comuns nos 3 INA219, SDA->GPIO";
      body += String(PIN_SDA);
      body += " SCL->GPIO";
      body += String(PIN_SCL);
      body += ", enderecos 0x40/0x41/0x44, fios curtos; evitar 5V nos pinos I2C do ESP32.\"";
    }
  }

  body += "}";
}

static void appendProcessJson(String &body) {
  body += ",\"process\":{";
  body += "\"ntp_ok\":";
  body += g_time_synced ? "true" : "false";
  body += ",\"running\":";
  body += g_proc_running ? "true" : "false";

  unsigned long elapsed = 0;
  const bool never_started = (g_proc_start_ms == 0);
  if (!never_started) {
    if (g_proc_running) {
      elapsed = millis() - g_proc_start_ms;
    } else {
      elapsed = g_proc_end_ms - g_proc_start_ms;
    }
  }
  body += ",\"elapsed_ms\":" + String(elapsed);
  body += ",\"start_wall_unix\":" + String(static_cast<long>(g_proc_start_wall));
  body += ",\"end_wall_unix\":" + String(static_cast<long>(g_proc_end_wall));

  body += ",\"start_iso\":";
  if (g_proc_start_wall > 1000000000L) {
    struct tm *tp = localtime(&g_proc_start_wall);
    char buf[40];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", tp);
    body += "\"";
    body += buf;
    body += "\"";
  } else {
    body += "null";
  }

  body += ",\"end_iso\":";
  if (g_proc_end_wall > 1000000000L) {
    struct tm *tp = localtime(&g_proc_end_wall);
    char buf[40];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", tp);
    body += "\"";
    body += buf;
    body += "\"";
  } else {
    body += "null";
  }

  body += "}";
}

static void addCorsHeaders() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
  // Chrome (Private Network Access): página em localhost ou file:// → IP 192.168.x.x
  server.sendHeader("Access-Control-Allow-Private-Network", "true");
}

static void sendJsonCors(int code, const String &payload) {
  addCorsHeaders();
  server.send(code, "application/json; charset=utf-8", payload);
}

static void handleCorsOptions() {
  addCorsHeaders();
  server.send(204, "text/plain", "");
}

static void resetInaMetrics() {
  for (size_t i = 0; i < INA_COUNT; i++) {
    InaChannel &ch = ina_channels[i];
    ch.avg_samples = 0;
    ch.sum_power_mw = 0.0;
    ch.sum_current_ma = 0.0;
    ch.energy_mWs = 0.0;
  }
  lastInaSampleMs = 0;
  Serial.println("API /api/reset: medias e energia zeradas (novo ciclo).");
}

void handleApiStatus() {
  if (server.method() == HTTP_OPTIONS) {
    handleCorsOptions();
    return;
  }
  if (server.method() != HTTP_GET) {
    sendJsonCors(405, "{\"error\":\"method_not_allowed\",\"hint\":\"GET /api/status\"}");
    return;
  }

  String body = "{";
  body += "\"device\":\"ESP32\",";
  body += "\"chip\":\"" + jsonEscape(String(ESP.getChipModel())) + "\",";
  body += "\"uptime_ms\":" + String(millis()) + ",";
  body += "\"free_heap\":" + String(ESP.getFreeHeap()) + ",";
  body += "\"wifi_rssi\":" + String(WiFi.RSSI()) + ",";
  body += "\"ip\":\"" + jsonEscape(WiFi.localIP().toString()) + "\"";
  appendIna219Json(body);
  appendProcessJson(body);
  body += "}";

  sendJsonCors(200, body);
}

void handleApiReset() {
  if (server.method() == HTTP_OPTIONS) {
    handleCorsOptions();
    return;
  }
  if (server.method() != HTTP_POST) {
    sendJsonCors(405,
                 "{\"error\":\"method_not_allowed\",\"hint\":\"POST /api/reset\"}");
    return;
  }
  resetInaMetrics();
  sendJsonCors(200, "{\"ok\":true,\"message\":\"metricas_zeradas\"}");
}

void handleApiProcessStart() {
  if (server.method() == HTTP_OPTIONS) {
    handleCorsOptions();
    return;
  }
  if (server.method() != HTTP_POST) {
    sendJsonCors(405,
                 "{\"error\":\"method_not_allowed\",\"hint\":\"POST /api/process/start\"}");
    return;
  }
  g_proc_running = true;
  g_proc_start_ms = millis();
  g_proc_end_ms = 0;
  g_proc_end_wall = 0;
  if (g_time_synced) {
    time(&g_proc_start_wall);
  } else {
    g_proc_start_wall = 0;
  }
  Serial.println("Processo: INICIO");
  sendJsonCors(200, "{\"ok\":true,\"message\":\"processo_iniciado\"}");
}

void handleApiProcessStop() {
  if (server.method() == HTTP_OPTIONS) {
    handleCorsOptions();
    return;
  }
  if (server.method() != HTTP_POST) {
    sendJsonCors(405,
                 "{\"error\":\"method_not_allowed\",\"hint\":\"POST /api/process/stop\"}");
    return;
  }
  if (!g_proc_running) {
    sendJsonCors(409, "{\"error\":\"not_running\",\"hint\":\"inicie com POST /api/process/start\"}");
    return;
  }
  g_proc_running = false;
  g_proc_end_ms = millis();
  if (g_time_synced) {
    time(&g_proc_end_wall);
  } else {
    g_proc_end_wall = 0;
  }
  const unsigned long dur = g_proc_end_ms - g_proc_start_ms;
  Serial.printf("Processo: FIM (duracao %lu ms)\n", dur);
  String msg = "{\"ok\":true,\"message\":\"processo_terminado\",\"elapsed_ms\":" + String(dur) + "}";
  sendJsonCors(200, msg);
}

void handleNotFound() {
  sendJsonCors(404,
                "{\"error\":\"not_found\",\"hint\":\"GET /api/status, POST /api/reset, "
                "POST /api/process/start|stop\"}");
}

static bool beginInaChannels() {
  Wire.setClock(I2C_HZ_SLOW);
  for (size_t i = 0; i < INA_COUNT; i++) {
    InaChannel &ch = ina_channels[i];
    ch.ok = ch.sensor->begin(&Wire);
  }

  bool any_fail = false;
  for (size_t i = 0; i < INA_COUNT; i++) {
    if (!ina_channels[i].ok) {
      any_fail = true;
      break;
    }
  }
  if (any_fail) {
    delay(50);
    for (size_t i = 0; i < INA_COUNT; i++) {
      InaChannel &ch = ina_channels[i];
      if (!ch.ok) {
        ch.ok = ch.sensor->begin(&Wire);
      }
    }
  }

  bool any_ok = false;
  for (size_t i = 0; i < INA_COUNT; i++) {
    if (ina_channels[i].ok) {
      any_ok = true;
      ina_channels[i].sensor->setCalibration_32V_2A();
    }
  }
  if (any_ok) {
    Wire.setClock(I2C_HZ_FAST);
  }
  return any_ok;
}

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println();
  Serial.printf("Reset: %s\n", esp32ResetReasonStr());
  Serial.println("(A URL da API so aparece uma vez por boot; se repetir, o chip esta reiniciando.)\n");

  Wire.begin(PIN_SDA, PIN_SCL);
  Wire.setClock(I2C_HZ_SLOW);
  delay(150);

  bool any_ok = beginInaChannels();

  Serial.println("INA219 canais:");
  for (size_t i = 0; i < INA_COUNT; i++) {
    InaChannel &ch = ina_channels[i];
    Serial.printf("  %s: %s -> %s\n", i2cAddrHex(ch.addr).c_str(), ch.name,
                  ch.ok ? "OK" : "FALHOU");
  }

  runI2cScan();
  Serial.printf("Scan I2C: %u dispositivo(s)\n", static_cast<unsigned>(i2c_scan_count));
  for (uint8_t i = 0; i < i2c_scan_count; i++) {
    Serial.printf("  0x%02X\n", i2c_scan_addrs[i]);
  }

  if (!any_ok) {
    logIna219DiagnosticsAllFailed(100000);
  }

  Serial.print("Conectando em ");
  Serial.println(WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  while (WiFi.status() != WL_CONNECTED) {
    delay(1);
    yield();
  }
  Serial.println();
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());

  configTime(0, 0, "pool.ntp.org", "time.google.com");
  setenv("TZ", "WET0WEST,M3.5.0/1,M10.5.0", 1);
  tzset();
  // Espera NTP sem depender de getLocalTime(tm, ms) (alguns cores so tem getLocalTime(tm)).
  for (int n = 0; n < 80; n++) {
    const time_t now = time(nullptr);
    if (now > 1700000000) {
      g_time_synced = true;
      Serial.println("NTP: horario OK (Portugal)");
      break;
    }
    delay(250);
  }
  if (!g_time_synced) {
    Serial.println("NTP: sem sync — inicio/fim ISO podem ficar null");
  }

  server.on("/api/status", HTTP_ANY, handleApiStatus);
  server.on("/api/reset", HTTP_ANY, handleApiReset);
  server.on("/api/process/start", HTTP_ANY, handleApiProcessStart);
  server.on("/api/process/stop", HTTP_ANY, handleApiProcessStop);
  server.onNotFound(handleNotFound);
  server.begin();
  const String ip = WiFi.localIP().toString();
  Serial.println("API: GET  http://" + ip + "/api/status");
  Serial.println("API: POST http://" + ip + "/api/reset");
  Serial.println("API: POST http://" + ip + "/api/process/start");
  Serial.println("API: POST http://" + ip + "/api/process/stop");
}

void loop() {
  server.handleClient();
  tickIna219Average();
}
