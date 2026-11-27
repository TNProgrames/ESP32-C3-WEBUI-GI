#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <FS.h>
#include <ArduinoJson.h>  // Use Version 7.x
#include <Update.h>
#include <esp_timer.h>
#include <esp_sleep.h>
#include <driver/gpio.h>
#include <soc/soc_caps.h>
#include <stdarg.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "lua_engine.h"
#include "drv_loader.h"
#include "em.h"           // صفحات الطوارئ المضغوطة (مولَّد من web/*.html)
#include "sha_util.h"
#include <string.h>

#define MAX_NAME_LEN   32
#define MAX_VALUE_LEN  96

// --- Settings On Ram ---
char localhost[40] = "";
char internethost[33] = "";
char pinreset[33] = "";
char wifimode[33] = "AP";
char wifissid[33] = "ESP32_STA";
char wifipass[65] = "12345678";
char apwifissid[33] = "ESP32_AP";
char apwifipass[65] = "12345678";
char hostname_[33] = "esp32";   // الاسم المحلي: <hostname>.local

// --- Network Settings ---
char access_key[32] = "1234";     // مفتاح صفحة الطوارئ /em: الافتراضي 1234، ويمكن تغييره بملف /em_key (حتى 31 حرفًا)
static char emSess[33] = "";      // رمز جلسة الطوارئ العشوائي (يُولَّد كل إقلاع) ويوضع في الكوكي بدل المفتاح نفسه

// --- Token Store ---
char Admin_Token[33] = "";
char User_Token[33] = "";
// --- WiFi is Working 
volatile bool wifiIsWorking = false;
// --- النوم العميق (deep sleep): يُنفَّذ مؤجلًا كي يصل ردّ HTTP وتُسجَّل الرسائل ---
static volatile uint32_t sleepAt = 0;
static uint32_t sleepSecs = 0, sleepMask = 0;
static int sleepLevel = 1;
// secs: مؤقت الاستيقاظ (0 = بلا مؤقت) · mask: بتّات الأرجل 0..5 (RTC) · level: 1 عالٍ / 0 منخفض. يعيد false عند معاملات غير صالحة
bool requestDeepSleep(uint32_t secs, uint32_t mask, int level) {
  if ((mask & ~0x3Fu) || secs > 30UL * 86400UL || (secs == 0 && mask == 0)) return false;
  sleepSecs = secs; sleepMask = mask; sleepLevel = level ? 1 : 0;
  sleepAt = millis() + 800; if (!sleepAt) sleepAt = 1;
  logPrintf("Deep sleep in 0.8s (timer=%us pins=0x%02x level=%d)\n", (unsigned)secs, (unsigned)mask, sleepLevel);
  return true;
}
// سبب الاستيقاظ: "timer" أو "gpio" أو "reset" (إقلاع عادي)، وpin = رقم الرجل أو -1
const char* wakeReason(int* pin) {
  *pin = -1;
  esp_sleep_wakeup_cause_t c = esp_sleep_get_wakeup_cause();
  if (c == ESP_SLEEP_WAKEUP_TIMER) return "timer";
  if (c == ESP_SLEEP_WAKEUP_GPIO || c == ESP_SLEEP_WAKEUP_EXT1) {
    uint64_t m = esp_sleep_get_gpio_wakeup_status();
    for (int i = 0; i < 6; i++) if (m & (1ULL << i)) { *pin = i; break; }
    return "gpio";
  }
  return "reset";
}
static void doDeepSleep() {
  luaeng_stop_all();
  WiFi.disconnect(true); WiFi.mode(WIFI_OFF);
  if (sleepSecs) esp_sleep_enable_timer_wakeup((uint64_t)sleepSecs * 1000000ULL);
  if (sleepMask) {
    for (int i = 0; i < 6; i++) if (sleepMask & (1u << i)) {
      gpio_set_direction((gpio_num_t)i, GPIO_MODE_INPUT);
      if (sleepLevel) { gpio_pullup_dis((gpio_num_t)i); gpio_pulldown_en((gpio_num_t)i); }
      else { gpio_pulldown_dis((gpio_num_t)i); gpio_pullup_en((gpio_num_t)i); }
      gpio_hold_en((gpio_num_t)i);                 // يُبقي السحب فعّالًا أثناء النوم
    }
    gpio_deep_sleep_hold_en();
    esp_sleep_enable_gpio_wakeup_on_hp_periph_powerdown(sleepMask, sleepLevel ? ESP_GPIO_WAKEUP_GPIO_HIGH : ESP_GPIO_WAKEUP_GPIO_LOW);
  }
  esp_deep_sleep_start();
}

volatile uint32_t restartAt = 0;          // إعادة تشغيل مؤجلة (ms) كي يصل ردّ HTTP أولاً
volatile bool mdnsApplyPending = false;  // إعادة تشغيل mDNS داخل loop() لا داخل callback
volatile bool wifiApplyPending = false; // يُنفَّذ applyWiFi() داخل loop() وليس داخل callback

// --- Auth / Config / OTA Synchronization ---
static portMUX_TYPE authMux = portMUX_INITIALIZER_UNLOCKED;
static portMUX_TYPE otaMux = portMUX_INITIALIZER_UNLOCKED;
static SemaphoreHandle_t cfgMux = nullptr;
struct CfgLock {
  CfgLock()  { if (cfgMux) xSemaphoreTakeRecursive(cfgMux, portMAX_DELAY); }
  ~CfgLock() { if (cfgMux) xSemaphoreGiveRecursive(cfgMux); }
};
static bool otaBusy = false;
struct OtaReq {
  bool active = false;
  bool started = false;
  bool fs = false;
  bool fsUnmounted = false;
  bool busyReject = false;
  char err[64] = "";
};
volatile bool restartPending = false; // إعادة تشغيل مؤجلة (OTA / استعادة النسخة الاحتياطية)

// --- Firmware version (يمكن تمريره من البناء: -DFW_VERSION=\"v1.0.0\") ---
#ifndef FW_VERSION
#define FW_VERSION "dev"
#endif

// --- Log ring buffer (يُعرض في تبويب الطرفية) ---
#define LOG_SIZE 4096
static char logBuf[LOG_SIZE];
static uint32_t logTotal = 0;
static portMUX_TYPE logMux = portMUX_INITIALIZER_UNLOCKED;

void logPrintf(const char* fmt, ...) {
  char tmp[160];
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
  va_end(ap);
  if (n <= 0) return;
  if (n >= (int)sizeof(tmp)) n = sizeof(tmp) - 1;
  Serial.write((const uint8_t*)tmp, n);
  portENTER_CRITICAL(&logMux);
  for (int i = 0; i < n; i++) logBuf[(logTotal++) % LOG_SIZE] = tmp[i];
  portEXIT_CRITICAL(&logMux);
}

const char* luaeng_fw_version() { return FW_VERSION; }

static uint32_t uptimeSec() { return (uint32_t)(esp_timer_get_time() / 1000000ULL); }

// --- Login lockout & password reset (بزر فيزيائي) ---
static uint8_t  loginFails = 0;
static uint32_t loginLockUntil = 0;
static uint32_t resetWaitUntil = 0;   // != 0 : بانتظار الضغط على الزر
static bool     resetDone = false;
static uint32_t resetDoneAt = 0;
static bool     resetSawHigh = false;
static int      resetPin = -1;

AsyncWebServer server(80);

// --- Token & Auth Helpers ---
static bool constTimeEq(const char* a, const char* b) {
    if (!a || !b) return false;
    size_t la = strlen(a), lb = strlen(b);
    uint8_t diff = (uint8_t)(la != lb);
    for (size_t i = 0; i < la; i++) diff |= (uint8_t)(a[i] ^ b[i < lb ? i : 0]);
    return diff == 0;
}

static bool parseLongStrict(const String& s, long& out) {
    if (s.length() == 0) return false;
    char* end = nullptr;
    long v = strtol(s.c_str(), &end, 10);
    if (end == s.c_str() || *end != '\0') return false;
    out = v;
    return true;
}

static bool hashPassword(const char* plain, char* out, size_t outSz) {
    if (!plain || !out || outSz < 89) return false;
    uint8_t saltBytes[8];
    uint32_t r0 = esp_random(), r1 = esp_random();
    memcpy(saltBytes, &r0, 4);
    memcpy(saltBytes + 4, &r1, 4);
    char salt[17];
    for (int i = 0; i < 8; i++) snprintf(salt + i * 2, 3, "%02x", saltBytes[i]);
    salt[16] = '\0';

    size_t plen = strlen(plain);
    size_t msgLen = 16 + 1 + plen;
    char* msg = (char*)malloc(msgLen + 1);
    if (!msg) return false;
    memcpy(msg, salt, 16);
    msg[16] = ':';
    memcpy(msg + 17, plain, plen);
    uint8_t digest[32];
    bool ok = sha256_once((const uint8_t*)msg, msgLen, digest);
    memset(msg, 0, msgLen);
    free(msg);
    if (!ok) return false;

    char hex[65];
    for (int i = 0; i < 32; i++) snprintf(hex + i * 2, 3, "%02x", digest[i]);
    hex[64] = '\0';
    snprintf(out, outSz, "sha256$%s$%s", salt, hex);
    return true;
}

static bool verifyPassword(const char* plain, const char* stored) {
    if (!plain || !stored || !stored[0]) return false;
    if (strncmp(stored, "sha256$", 7) != 0) {
        return constTimeEq(plain, stored); // توافق رجعي مع النسخ القديمة غير المعمّاة
    }
    const char* salt = stored + 7;
    const char* sep = strchr(salt, '$');
    if (!sep) return false;
    size_t saltLen = (size_t)(sep - salt);
    const char* expectedHex = sep + 1;
    if (saltLen == 0 || saltLen > 32 || strlen(expectedHex) != 64) return false;

    size_t plen = strlen(plain);
    size_t msgLen = saltLen + 1 + plen;
    char* msg = (char*)malloc(msgLen + 1);
    if (!msg) return false;
    memcpy(msg, salt, saltLen);
    msg[saltLen] = ':';
    memcpy(msg + saltLen + 1, plain, plen);
    uint8_t digest[32];
    bool ok = sha256_once((const uint8_t*)msg, msgLen, digest);
    memset(msg, 0, msgLen);
    free(msg);
    if (!ok) return false;

    char hex[65];
    for (int i = 0; i < 32; i++) snprintf(hex + i * 2, 3, "%02x", digest[i]);
    hex[64] = '\0';
    return constTimeEq(hex, expectedHex);
}

// الرمز الفارغ مرفوض دائماً (قبل أول تسجيل دخول يكون الرمز فارغاً)
static bool tokenMatches(AsyncWebServerRequest *request, const char* stored) {
    if (!request->hasHeader("User_Token")) return false;
    char tok[33];
    portENTER_CRITICAL(&authMux);
    strlcpy(tok, stored, sizeof(tok));
    portEXIT_CRITICAL(&authMux);
    if (tok[0] == '\0') return false;
    const char* g = request->getHeader("User_Token")->value().c_str();
    return constTimeEq(tok, g);
}
static bool isAdminToken(AsyncWebServerRequest *request) { return tokenMatches(request, Admin_Token); }
static bool isAnyToken(AsyncWebServerRequest *request)   { return tokenMatches(request, Admin_Token) || tokenMatches(request, User_Token); }

// --- Hardware Usage Functions ---

char* RamUsage(char* buffer, size_t bufferSize) {
    float totalHeap = ESP.getHeapSize();
    float freeHeap = ESP.getFreeHeap();
    float usagePercentage = ((totalHeap - freeHeap) / totalHeap) * 100.0;
    snprintf(buffer, bufferSize, "%.1f", usagePercentage);
    return buffer;
}

char* StorageUsage(char* buffer, size_t bufferSize) {
    size_t total = LittleFS.totalBytes();
    size_t used = LittleFS.usedBytes();
    if (total == 0) {
        snprintf(buffer, bufferSize, "0.0");
        return buffer;
    }
    float usagePercentage = ((float)used / (float)total) * 100.0;
    snprintf(buffer, bufferSize, "%.1f", usagePercentage);
    return buffer;
}

char* generateToken(char* buffer, size_t bufferSize) {
    if (!buffer || bufferSize < 33) return buffer;
    const char alphanum[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    for (int i = 0; i < 32; i++) {
        buffer[i] = alphanum[esp_random() % (sizeof(alphanum) - 1)];
    }
    buffer[32] = '\0';
    return buffer;
}

// --- JSON Engines (Optimized for ArduinoJson 7) ---

bool updateOrCreateJson(const char* filePath, const char* path, const char* newValue) {
  if (!path || !newValue || strlen(newValue) > MAX_VALUE_LEN) return false;
  CfgLock _lk;

  JsonDocument doc;
  File file = LittleFS.open(filePath, "r");
  if (file) {
    if (!file.isDirectory()) deserializeJson(doc, file);
    file.close();
  }

  if (!doc.is<JsonArray>()) doc.to<JsonArray>();
  JsonVariant current = doc.as<JsonArray>();

  char temp[192];
  strlcpy(temp, path, sizeof(temp));
  char* sp = nullptr;
  char* token = strtok_r(temp, ".", &sp);
  
  while (token) {
    if (strcmp(token, "newAry") == 0) {
      if (!current.is<JsonArray>()) return false;
      current = current.as<JsonArray>().add<JsonArray>();
    } else if (strcmp(token, "newObj") == 0) {
      if (!current.is<JsonArray>()) return false;
      current = current.as<JsonArray>().add<JsonObject>();
    } else if (strstr(token, "_obj") || strstr(token, "_Obj")) {
      int idx = atoi(token);
      if (idx < 0 || !current.is<JsonArray>()) return false;
      current = current.as<JsonArray>()[idx];
    } else if (token[strlen(token) - 1] == '-') {
      token[strlen(token) - 1] = 0;
      if (!current[token].is<JsonArray>()) current[token].to<JsonArray>();
      current = current[token];
    } else {
      if (!current[token].is<JsonObject>()) current[token].to<JsonObject>();
      current = current[token];
    }
    token = strtok_r(NULL, ".", &sp);
  }
  
  current.set(newValue);
  File fileW = LittleFS.open(filePath, "w");
  if (!fileW) return false;
  size_t w = serializeJsonPretty(doc, fileW);
  fileW.close();
  return w > 0;
}

bool getJson(const char* filePath, const char* path, char* outValue, size_t outValueSize) {
  CfgLock _lk;
  if (!LittleFS.exists(filePath)) return false;
  JsonDocument doc;
  File file = LittleFS.open(filePath, "r");
  if (!file || file.isDirectory()) { if (file) file.close(); return false; }
  DeserializationError err = deserializeJson(doc, file);
  file.close();
  if (err) return false;

  JsonVariant current = doc.as<JsonArray>();
  char temp[192];
  strlcpy(temp, path, sizeof(temp));
  char* sp = nullptr;
  char* token = strtok_r(temp, ".", &sp);
  
  while (token) {
    if (strstr(token, "_obj") || strstr(token, "_Obj")) {
      int idx = atoi(token);
      if (idx < 0 || !current.is<JsonArray>()) return false;
      current = current.as<JsonArray>()[idx];
    } else {
      current = current[token];
    }
    if (current.isNull()) return false;
    token = strtok_r(NULL, ".", &sp);
  }

  if (!current.is<const char*>()) return false;
  strlcpy(outValue, current.as<const char*>(), outValueSize);
  return true;
}

// --- الشبكات المحفوظة (STA): /wifi_nets.json = [{"ssid":..,"pass":..}] بحد أقصى MAX_NETS ---
#define NETS_FILE "/wifi_nets.json"
#define MAX_NETS 6
static void loadNets(JsonDocument& d) {
  CfgLock _lk;
  File f = LittleFS.open(NETS_FILE, "r");
  if (f) { deserializeJson(d, f); f.close(); }
  if (!d.is<JsonArray>()) d.to<JsonArray>();
  if (d.as<JsonArray>().size() == 0) {          // ترقية من الإعداد القديم (شبكة واحدة)
    char s[33] = "", p[65] = "";
    getJson("/settings.json", "0_Obj.wifi.ssid", s, sizeof(s)); getJson("/settings.json", "0_Obj.wifi.pass", p, sizeof(p));
    if (*s) { JsonObject o = d.as<JsonArray>().add<JsonObject>(); o["ssid"] = s; o["pass"] = p; }
  }
}

// اسم المضيف: أحرف صغيرة وأرقام وشرطة فقط (حد DNS)، وإلا esp32
static void cleanHost(char* h) {
  char o[33]; int n = 0;
  for (int i = 0; h[i] && n < 32; i++) { char c = (char)tolower((unsigned char)h[i]); if (isalnum((unsigned char)c) || (c == '-' && n > 0)) o[n++] = c; }
  while (n > 0 && o[n - 1] == '-') n--;
  o[n] = 0; strlcpy(h, n ? o : "esp32", 33);
}
// الاسم المحلي = إعداد «المضيف المحلي» في تبويب الاتصال (مثل esp32.local)
// قيود الاسم المحلي: 1–32 حرفًا من a-z و0-9 والشرطة، بلا شرطة في الأول أو الآخر (يُقبل لاحقة .local وتُحذف)
static bool validHost(String h) {
  if (h.endsWith(".local")) h.remove(h.length() - 6);
  int n = h.length();
  if (n < 1 || n > 32 || h[0] == '-' || h[n - 1] == '-') return false;
  for (int i = 0; i < n; i++) { char c = h[i]; if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) return false; }
  return true;
}
static void loadHost() {
  char h[40] = "esp32.local"; getJson("/settings.json", "0_Obj.localhost", h, sizeof h);
  char* dot = strchr(h, '.'); if (dot) *dot = 0;          // نحتفظ بالجزء قبل أول نقطة
  cleanHost(h); strlcpy(hostname_, h, sizeof(hostname_));
}
static void startMdns() {
  MDNS.end();
  if (MDNS.begin(hostname_)) { MDNS.addService("http", "tcp", 80); logPrintf("mDNS: http://%s.local\n", hostname_); }
  else logPrintf("mDNS failed\n");
}

// صفحات الطوارئ مضغوطة gzip (web/*.html → src/em.h)
static AsyncWebServerResponse* gzPage(AsyncWebServerRequest* r, int code, const uint8_t* data, size_t len) {
  AsyncWebServerResponse* resp = r->beginResponse_P(code, "text/html", data, len);
  resp->addHeader("Content-Encoding", "gzip");
  resp->addHeader("Cache-Control", "no-store");
  return resp;
}

// --- Apply WiFi Settings ---

void applyWiFi() {
  wifiIsWorking = true;
  logPrintf("Reconfiguring WiFi...\n");
  WiFi.disconnect();
  WiFi.softAPdisconnect(true);
  delay(500);

  getJson("/settings.json", "0_Obj.wifi.mode", wifimode, sizeof(wifimode));
  getJson("/settings.json", "0_Obj.wifi.ssid", wifissid, sizeof(wifissid));
  getJson("/settings.json", "0_Obj.wifi.pass", wifipass, sizeof(wifipass));
  getJson("/settings.json", "0_Obj.wifi.ap_ssid", apwifissid, sizeof(apwifissid));
  getJson("/settings.json", "0_Obj.wifi.ap_pass", apwifipass, sizeof(apwifipass));
  loadHost();

  bool wantSta = (strcmp(wifimode, "STA") == 0 || strcmp(wifimode, "AP_STA") == 0);
  bool wantAp  = !wantSta || strcmp(wifimode, "AP_STA") == 0; // أي قيمة غير معروفة = AP

  WiFi.setHostname(hostname_);
  // اضبط الوضع أولاً ثم شغّل الواجهات
  WiFi.mode(wantSta && wantAp ? WIFI_AP_STA : (wantSta ? WIFI_STA : WIFI_AP));

  if (wantAp) {
    WiFi.softAP(apwifissid, apwifipass);
  }

  if (wantSta) {
    // جرّب الشبكات المحفوظة: الظاهرة في المسح أولاً بترتيب قوة الإشارة. إن لم تنجح أي واحدة → وضع الاسترداد
    JsonDocument nd; loadNets(nd);
    JsonArray arr = nd.as<JsonArray>();
    int cnt = arr.size(); if (cnt > MAX_NETS) cnt = MAX_NETS;
    int order[MAX_NETS], rssi[MAX_NETS]; int seen = 0;
    int found = WiFi.scanNetworks(false, true);
    for (int i = 0; i < cnt; i++) {
      order[i] = i; rssi[i] = -999;
      const char* ss = arr[i]["ssid"] | "";
      for (int k = 0; k < found; k++) if (WiFi.SSID(k) == ss) { rssi[i] = WiFi.RSSI(k); seen++; break; }
    }
    WiFi.scanDelete();
    for (int i = 1; i < cnt; i++)                       // ترتيب إدراج بسيط بحسب rssi تنازلياً
      for (int j = i; j > 0 && rssi[order[j]] > rssi[order[j - 1]]; j--) { int t = order[j]; order[j] = order[j - 1]; order[j - 1] = t; }
    for (int i = 0; i < cnt && WiFi.status() != WL_CONNECTED; i++) {
      int k = order[i];
      if (seen > 0 && rssi[k] == -999) continue;         // غير ظاهرة: تخطَّها إن ظهرت غيرها
      const char* ss = arr[k]["ssid"] | ""; const char* pw = arr[k]["pass"] | "";
      if (!*ss) continue;
      logPrintf("WiFi: trying %s\n", ss);
      WiFi.begin(ss, pw);
      for (int a = 0; a < 20 && WiFi.status() != WL_CONNECTED; a++) delay(500);   // حتى 10 ثوانٍ لكل شبكة
      if (WiFi.status() != WL_CONNECTED) WiFi.disconnect();
    }
    if (WiFi.status() != WL_CONNECTED && !wantAp) {
      logPrintf("STA failed on all saved networks, reverting to recovery mode\n");
      WiFi.mode(WIFI_AP);
      WiFi.softAP("RECOVERY_MODE", "12345678");
    }
  }
  startMdns();
  if (WiFi.status() == WL_CONNECTED) logPrintf("WiFi connected: %s\n", WiFi.localIP().toString().c_str());
  wifiIsWorking = false;
}

// --- System Initialization ---

static void seedDefaultFiles() {
  CfgLock _lk;
  if (!LittleFS.exists("/users.json")) {
    char ha[97], hu[97];
    updateOrCreateJson("/users.json", "newObj.username", "admin");
    if (hashPassword("admin123", ha, sizeof(ha))) updateOrCreateJson("/users.json", "0_Obj.password", ha);
    updateOrCreateJson("/users.json", "newObj.username", "user");
    if (hashPassword("user123", hu, sizeof(hu))) updateOrCreateJson("/users.json", "1_Obj.password", hu);
  }

  if (!LittleFS.exists("/settings.json")) {
    updateOrCreateJson("/settings.json", "newObj.localhost", "esp32.local");
    updateOrCreateJson("/settings.json", "0_Obj.internethost", "api.esp-controller.io");
    updateOrCreateJson("/settings.json", "0_Obj.pinreset", "9");
    updateOrCreateJson("/settings.json", "0_Obj.wifi.mode", "AP");
    updateOrCreateJson("/settings.json", "0_Obj.wifi.ssid", "ESP32_STA");
    updateOrCreateJson("/settings.json", "0_Obj.wifi.pass", "12345678");
    updateOrCreateJson("/settings.json", "0_Obj.wifi.ap_ssid", "ESP32_AP");
    updateOrCreateJson("/settings.json", "0_Obj.wifi.ap_pass", "12345678");
  }
}

void loadsetting() {
  Serial.begin(115200);
  logPrintf("Boot %s\n", FW_VERSION);
  if (!cfgMux) cfgMux = xSemaphoreCreateRecursiveMutex();
  if (!LittleFS.begin(true)) {
    logPrintf("LittleFS mount failed\n");
    return;
  }

  seedDefaultFiles();
  getJson("/settings.json", "0_Obj.localhost", localhost, sizeof(localhost));
  getJson("/settings.json", "0_Obj.internethost", internethost, sizeof(internethost));
  getJson("/settings.json", "0_Obj.pinreset", pinreset, sizeof(pinreset));
  applyWiFi();
  
}

// --- Emergency Tools Helper ---
static bool hasCookie(AsyncWebServerRequest *request, const char* name) {
  char sess[33];
  portENTER_CRITICAL(&authMux);
  strlcpy(sess, emSess, sizeof(sess));
  portEXIT_CRITICAL(&authMux);
  if (!sess[0] || !request->hasHeader("Cookie")) return false;
  char want[64]; snprintf(want, sizeof(want), "%s=%s", name, sess);
  const char* c = request->header("Cookie").c_str();
  size_t wl = strlen(want);
  for (const char* p = c; (p = strstr(p, want)) != nullptr; p += wl)
    if ((p == c || p[-1] == ' ' || p[-1] == ';') && (p[wl] == 0 || p[wl] == ';')) return true;
  return false;
}
bool isAuthorizedEm(AsyncWebServerRequest *request) {
    return hasCookie(request, "session_key_em");
}

bool isAuthorized(AsyncWebServerRequest *request) {
    return isAdminToken(request);
}

void refreshSessionEm(AsyncWebServerResponse *response) {
    char sess[33];
    portENTER_CRITICAL(&authMux);
    strlcpy(sess, emSess, sizeof(sess));
    portEXIT_CRITICAL(&authMux);
    char c[112]; snprintf(c, sizeof(c), "session_key_em=%s; Max-Age=900; Path=/; HttpOnly; SameSite=Strict", sess);
    response->addHeader("Set-Cookie", c);
}

bool deleteRecursive(String path) {
    File root = LittleFS.open(path);
    if (!root) return false;
    if (!root.isDirectory()) {
        root.close();
        return LittleFS.remove(path);
    }
    File file = root.openNextFile();
    while (file) {
        String filePath = path + "/" + file.name();
        if (file.isDirectory()) { file.close(); deleteRecursive(filePath); }
        else { file.close(); LittleFS.remove(filePath); }
        file = root.openNextFile();
    }
    root.close();
    return LittleFS.rmdir(path);
}

// --- Path / Param Helpers ---
// المسار يجب أن يبدأ بـ / ولا يحتوي على ..
static bool validPath(const String& p) {
    return p.length() > 0 && p[0] == '/' && p.indexOf("..") < 0;
}
static bool getPathParam(AsyncWebServerRequest *request, const char* name, String& out) {
    if (!request->hasParam(name)) return false;
    out = request->getParam(name)->value();
    return validPath(out);
}
static bool collectBody(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total, size_t maxBytes) {
    if (total == 0 || total > maxBytes) { if (index == 0) request->send(413); return false; }
    if (index == 0) {
        request->_tempObject = malloc(total + 1);
        if (!request->_tempObject) { request->send(500); return false; }
    }
    char* buf = (char*)request->_tempObject;
    if (!buf) return false;
    if (index + len > total) { request->send(400); return false; }
    if (len > 0) memcpy(buf + index, data, len);
    if (index + len < total) return false;
    buf[total] = 0;
    return true;
}

// --- Terminal commands ---
static String formatUptime(uint32_t sec) {
  char b[40];
  snprintf(b, sizeof(b), "%lud %02luh %02lum %02lus", (unsigned long)(sec / 86400), (unsigned long)((sec / 3600) % 24), (unsigned long)((sec / 60) % 60), (unsigned long)(sec % 60));
  return String(b);
}

static String runCommand(String line) {
  line.trim();
  if (line.length() == 0) return "";
  int sp = line.indexOf(' ');
  String c = sp < 0 ? line : line.substring(0, sp);
  String arg = sp < 0 ? String("") : line.substring(sp + 1);
  arg.trim();
  c.toLowerCase();

  if (c == "help") return "help | version | uptime | heap | wifi | ls [path] | df | echo <text> | reboot\nscripts | drivers | run <name> | stop <name|all> | pub <topic> <value> | data <name>";
  if (c == "scripts") return luaeng_list_text();
  if (c == "drivers") return drv_list_text();
  if (c == "pub") {
    int sp2 = arg.indexOf(' ');
    if (sp2 < 0) return "usage: pub <topic> <value>";
    String tp = arg.substring(0, sp2), val = arg.substring(sp2 + 1);
    return luaeng_bus_publish(tp.c_str(), val.c_str()) ? String("published ") + tp : String("cannot publish (bad topic, too long or queue full)");
  }
  if (c == "data") return luaeng_storage_dump(arg.c_str());
  if (c == "run") return luaeng_run_file(arg.c_str()) ? String("started ") + arg : String("cannot run (need /scripts/") + arg + ".lua)";
  if (c == "stop") { if (arg == "all") { luaeng_stop_all(); return "stopping all"; } return luaeng_stop(arg.c_str()) ? String("stopping ") + arg : String("invalid name"); }
  if (c == "version") return String("firmware ") + FW_VERSION + "\nchip " + ESP.getChipModel() + " rev " + String(ESP.getChipRevision()) + "\nsdk " + ESP.getSdkVersion();
  if (c == "uptime") return formatUptime(uptimeSec());
  if (c == "heap") return String("free ") + String(ESP.getFreeHeap()) + " / total " + String(ESP.getHeapSize()) + " / min free " + String(ESP.getMinFreeHeap());
  if (c == "df") return String("used ") + String((unsigned long)LittleFS.usedBytes()) + " / total " + String((unsigned long)LittleFS.totalBytes());
  if (c == "echo") return arg;
  if (c == "reboot") { restartPending = true; return "Rebooting..."; }
  if (c == "wifi") {
    wifi_mode_t m = WiFi.getMode();
    String o = String("mode: ") + (m == WIFI_AP ? "AP" : (m == WIFI_STA ? "STA" : (m == WIFI_AP_STA ? "AP_STA" : "OFF")));
    o += "\nAP ip: " + WiFi.softAPIP().toString();
    if (WiFi.status() == WL_CONNECTED) o += "\nSTA ssid: " + WiFi.SSID() + "\nSTA ip: " + WiFi.localIP().toString() + "\nrssi: " + String(WiFi.RSSI());
    else o += "\nSTA: disconnected";
    return o;
  }
  if (c == "ls") {
    String p = arg.length() ? arg : String("/");
    if (!validPath(p)) return "invalid path";
    File d = LittleFS.open(p);
    if (!d || !d.isDirectory()) { if (d) d.close(); return "not a directory"; }
    String o;
    File f = d.openNextFile();
    while (f) {
      o += f.isDirectory() ? "[D] " : "    ";
      o += f.name();
      if (!f.isDirectory()) { o += "  "; o += String((unsigned long)f.size()); }
      o += "\n";
      f.close();
      f = d.openNextFile();
    }
    d.close();
    return o.length() ? o : String("(empty)");
  }
  return "Unknown command: " + c + " (type help)";
}

// --- Password reset (يتطلب ضغط الزر الفيزيائي) ---
static int resetPinNumber() { return pinreset[0] ? atoi(pinreset) : 9; }

static void doPasswordReset() {
  {
    CfgLock _lk;
    char ha[97], hu[97];
    if (hashPassword("admin123", ha, sizeof(ha))) updateOrCreateJson("/users.json", "0_Obj.password", ha);
    if (hashPassword("user123", hu, sizeof(hu))) updateOrCreateJson("/users.json", "1_Obj.password", hu);
  }
  portENTER_CRITICAL(&authMux);
  Admin_Token[0] = 0; User_Token[0] = 0;
  portEXIT_CRITICAL(&authMux);
  loginFails = 0; loginLockUntil = 0;
  logPrintf("Passwords reset to defaults\n");
}

// OTA: admin يحدّث البرنامج أو الواجهة، ومفتاح الطوارئ يحدّث الواجهة (type=fs) فقط
static bool otaAllowed(AsyncWebServerRequest *request) {
    if (isAdminToken(request)) return true;
    bool fs = request->hasParam("type") && request->getParam("type")->value() == "fs";
    return fs && isAuthorizedEm(request);
}

// --- Emergency APIs Setup ---

void EmSetupAPI() {
    server.on("/em", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (isAuthorizedEm(request)) {
            AsyncWebServerResponse *response = gzPage(request, 200, em_html_gz, em_html_gz_len);
            refreshSessionEm(response);
            request->send(response);
        } else {
            // غير مصرّح: اعرض نافذة إدخال المفتاح
            request->send(gzPage(request, 401, em_login_gz, em_login_gz_len));
        }
    });

    // التحقق من مفتاح الطوارئ عبر POST فقط (مع قفل مؤقت بعد 5 محاولات خاطئة ومقارنة ثابتة الزمن)
    server.on("/em/check", HTTP_POST, [](AsyncWebServerRequest *request) {
        static uint8_t fails = 0;
        static uint32_t lockUntil = 0;
        uint32_t now = millis();
        if (lockUntil && (int32_t)(now - lockUntil) < 0) { request->send(429); return; }
        if (request->hasParam("pass", true) && constTimeEq(request->getParam("pass", true)->value().c_str(), access_key)) {
            fails = 0; lockUntil = 0;
            AsyncWebServerResponse *response = request->beginResponse(200);
            refreshSessionEm(response);
            request->send(response);
            return;
        }
        if (++fails >= 5) { fails = 0; lockUntil = now + 60000; }
        request->send(401);
    });

    server.on("/em/logout", HTTP_POST, [](AsyncWebServerRequest *request) {
        AsyncWebServerResponse *response = request->beginResponse(200);
        response->addHeader("Set-Cookie", "session_key_em=; Max-Age=0; Path=/; HttpOnly; SameSite=Strict");
        request->send(response);
    });

    server.on("/api/list", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!isAuthorized(request) && !isAuthorizedEm(request)) { request->send(403); return; }
        String path = "/";
        if (request->hasParam("path") && !getPathParam(request, "path", path)) { request->send(400); return; }
        File root = LittleFS.open(path);
        if (!root || !root.isDirectory()) { if (root) root.close(); request->send(400); return; }
        JsonDocument doc;
        doc["total"] = LittleFS.totalBytes();
        doc["used"] = LittleFS.usedBytes();
        JsonArray files = doc["files"].to<JsonArray>();
        File file = root.openNextFile();
        while (file) {
            JsonObject f = files.add<JsonObject>();
            f["name"] = String(file.name());
            f["isDir"] = file.isDirectory();
            f["size"] = file.size();
            file.close();
            file = root.openNextFile();
        }
        root.close();
        String output; serializeJson(doc, output);
        AsyncWebServerResponse *response = request->beginResponse(200, "application/json", output);
        if (isAuthorizedEm(request)) {refreshSessionEm(response);}
        request->send(response);
    });

    server.on("/api/upload", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!isAuthorized(request) && !isAuthorizedEm(request)) { request->send(403); return; }
        uint8_t* st = (uint8_t*)request->_tempObject;
        if (!st || *st != 0) { request->send((st && *st == 1) ? 400 : 500); return; }
        AsyncWebServerResponse *response = request->beginResponse(200);
        if (isAuthorizedEm(request)) {refreshSessionEm(response);}
        request->send(response);
    }, [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
        if (!isAuthorized(request) && !isAuthorizedEm(request)) return;
        if (!index) {
            uint8_t* st = (uint8_t*)malloc(1);
            request->_tempObject = st;
            if (!st) return;
            if (!validPath(filename)) { *st = 1; return; }
            for (int i = 1; i < (int)filename.length(); i++)
              if (filename[i] == '/') { String d = filename.substring(0, i); if (!LittleFS.exists(d)) LittleFS.mkdir(d); }
            request->_tempFile = LittleFS.open(filename, "w");
            *st = request->_tempFile ? 0 : 2;
        }
        uint8_t* st = (uint8_t*)request->_tempObject;
        if (!st || *st != 0) return;
        if (request->_tempFile && len > 0 && request->_tempFile.write(data, len) != len) *st = 2;
        if (final && request->_tempFile) request->_tempFile.close();
    });

    server.on("/api/download", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!isAuthorized(request) && !isAuthorizedEm(request)) { request->send(403); return; }
        String path;
        if (!getPathParam(request, "path", path)) { request->send(400); return; }
        File f = LittleFS.open(path, "r");
        bool ok = f && !f.isDirectory();
        if (f) f.close();
        if (!ok) { request->send(400); return; }
        AsyncWebServerResponse *response = request->beginResponse(LittleFS, path, "application/octet-stream");
        if (isAuthorizedEm(request)) {refreshSessionEm(response);}
        request->send(response);
    });
    
    server.on("/api/mkdir", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!isAuthorized(request) && !isAuthorizedEm(request)) { request->send(403); return; }
        String path;
        if (!getPathParam(request, "path", path)) { request->send(400); return; }
        File ex = LittleFS.open(path, "r");
        bool ok = ex ? ex.isDirectory() : LittleFS.mkdir(path);
        if (ex) ex.close();
        AsyncWebServerResponse *response = request->beginResponse(ok ? 200 : 500);
        if (isAuthorizedEm(request)) {refreshSessionEm(response);}
        request->send(response);
    });
    
    server.on("/api/read", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!isAuthorized(request) && !isAuthorizedEm(request)) { request->send(403); return; }
        String path;
        if (!getPathParam(request, "path", path)) { request->send(400); return; }
        File f = LittleFS.open(path, "r");
        bool ok = f && !f.isDirectory();
        if (f) f.close();
        if (!ok) { request->send(400); return; }
        AsyncWebServerResponse *response = request->beginResponse(LittleFS, path, "text/plain");
        if (isAuthorizedEm(request)) {refreshSessionEm(response);}
        request->send(response);
    });

    server.on("/api/save", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!isAuthorized(request) && !isAuthorizedEm(request)) { request->send(403); return; }
        String path;
        if (!getPathParam(request, "path", path) || !request->hasParam("content", true)) { request->send(400); return; }
        for (int i = 1; i < (int)path.length(); i++)      // أنشئ المجلدات الأب إن لم تكن موجودة
          if (path[i] == '/') { String d = path.substring(0, i); if (!LittleFS.exists(d)) LittleFS.mkdir(d); }
        File f = LittleFS.open(path, "w");
        if (!f) { request->send(500); return; }
        const String& content = request->getParam("content", true)->value();
        size_t w = f.write((const uint8_t*)content.c_str(), content.length());
        f.close();
        if (w != content.length()) { request->send(500); return; }
        AsyncWebServerResponse *response = request->beginResponse(200);
        if (isAuthorizedEm(request)) {refreshSessionEm(response);}
        request->send(response);
    });

    server.on("/api/delete", HTTP_DELETE, [](AsyncWebServerRequest *request) {
        if (!isAuthorized(request) && !isAuthorizedEm(request)) { request->send(403); return; }
        String path;
        if (!getPathParam(request, "path", path) || path == "/") { request->send(400); return; }
        bool success = deleteRecursive(path);
        AsyncWebServerResponse *response = request->beginResponse(success ? 200 : 500);
        if (isAuthorizedEm(request)) {refreshSessionEm(response);}
        request->send(response);
    });
    
    server.on("/api/rename", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!isAuthorized(request) && !isAuthorizedEm(request)) { request->send(403); return; }
        String oldPath, newPath;
        if (!getPathParam(request, "old", oldPath) || !getPathParam(request, "new", newPath)) { request->send(400); return; }
        bool ok = LittleFS.rename(oldPath, newPath);
        AsyncWebServerResponse *response = request->beginResponse(ok ? 200 : 500);
        if (isAuthorizedEm(request)) {refreshSessionEm(response);}
        request->send(response);
    });
    
    server.on("/api/format", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!isAuthorized(request) && !isAuthorizedEm(request)) { request->send(403); return; }
        luaeng_stop_all();
        delay(50);
        bool ok = false;
        {
          CfgLock _lk;
          ok = LittleFS.format();
          if (ok) seedDefaultFiles();
        }
        if (ok) {
          portENTER_CRITICAL(&authMux);
          Admin_Token[0] = 0; User_Token[0] = 0;
          portEXIT_CRITICAL(&authMux);
        }
        AsyncWebServerResponse *response = request->beginResponse(ok ? 200 : 500);
        if (isAuthorizedEm(request)) {refreshSessionEm(response);}
        request->send(response);
    });
}

// --- Main Application Web APIs ---

// مسار صفحة اللوحة: /ui/<name>.html بأحرف آمنة فقط
static bool validUiPage(const String& p) {
  if (p.length() < 9 || p.length() > 39 || !p.startsWith("/ui/") || !p.endsWith(".html") || p.indexOf("..") >= 0) return false;
  for (size_t i = 0; i < p.length(); i++) {
    char c = p[i];
    if (!(isalnum((unsigned char)c) || c == '_' || c == '-' || c == '.' || c == '/')) return false;
  }
  return true;
}

void setupServerWeb() {
  // التزام سياسة نفس المصدر (same-origin) بدلاً من السماح لأي نطاق خارجي بالوصول للواجهات المحمية
  server.serveStatic("/", LittleFS, "/app").setDefaultFile("index.html");

  // --- Login API ---
  server.on("/login", HTTP_POST, [](AsyncWebServerRequest *request){}, NULL,
  [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    if (!collectBody(request, data, len, index, total, 512)) return;
    JsonDocument doc; 
    if (deserializeJson(doc, (const char*)request->_tempObject, total)) { request->send(400); return; }
    const char* u = doc["username"];
    const char* p = doc["password"];
    if (!u || !p) { request->send(400); return; }
    if (loginLockUntil && (int32_t)(millis() - loginLockUntil) < 0) {
      request->send(429, "application/json", "{\"error\":\"locked\"}");
      return;
    }

    char passWord[97] = "", token[33];
    int userIdx = (strcmp(u, "admin") == 0) ? 0 : ((strcmp(u, "user") == 0) ? 1 : -1);

    if (userIdx != -1) {
      char pathP[32];
      snprintf(pathP, sizeof(pathP), "%d_Obj.password", userIdx);

      if (getJson("/users.json", pathP, passWord, sizeof(passWord)) && verifyPassword(p, passWord)) {
        // ترقية تلقائية لأي كلمة مرور قديمة غير معمّاة عند أول دخول ناجح
        if (strncmp(passWord, "sha256$", 7) != 0) {
          char hp[97];
          if (hashPassword(p, hp, sizeof(hp))) updateOrCreateJson("/users.json", pathP, hp);
        }
        generateToken(token, sizeof(token));
        portENTER_CRITICAL(&authMux);
        if (userIdx == 0) strlcpy(Admin_Token, token, sizeof(Admin_Token));
        else strlcpy(User_Token, token, sizeof(User_Token));
        portEXIT_CRITICAL(&authMux);
        JsonDocument resDoc;
        resDoc["token"] = token;
        resDoc["role"] = u;
        String res; serializeJson(resDoc, res);
        loginFails = 0; loginLockUntil = 0;
        logPrintf("Login ok: %s\n", u);
        request->send(200, "application/json", res);
        return;
      }
    }
    logPrintf("Login failed: %s\n", u);
    if (++loginFails >= 5) { loginFails = 0; loginLockUntil = millis() + 30000; if (!loginLockUntil) loginLockUntil = 1; }
    request->send(401);
  });

  // --- إعادة ضبط كلمة المرور: تتطلب ضغط الزر الفيزيائي (GPIO من إعداد pinreset) خلال 30 ثانية ---
  server.on("/login/reset_request", HTTP_POST, [](AsyncWebServerRequest *request){
    int pin = resetPinNumber();
    if (pin < 0 || pin > 21 || pin == 18 || pin == 19) { request->send(400, "application/json", "{\"error\":\"bad pin\"}"); return; }
    resetPin = pin; resetDone = false; resetSawHigh = false;
    resetWaitUntil = millis() + 30000; if (!resetWaitUntil) resetWaitUntil = 1;
    pinMode(pin, INPUT_PULLUP);
    logPrintf("Password reset requested: press GPIO%d within 30s\n", pin);
    char res[48];
    snprintf(res, sizeof(res), "{\"pin\":%d,\"seconds\":30}", pin);
    request->send(200, "application/json", res);
  });

  server.on("/login/reset_status", HTTP_GET, [](AsyncWebServerRequest *request){
    JsonDocument d;
    if (resetDone && (millis() - resetDoneAt) < 60000) { d["state"] = "done"; d["admin"] = "admin123"; d["user"] = "user123"; }
    else if (resetWaitUntil) { int32_t left = (int32_t)(resetWaitUntil - millis()); d["state"] = "waiting"; d["left"] = left > 0 ? left / 1000 : 0; d["pin"] = resetPin; }
    else d["state"] = "idle";
    String o; serializeJson(d, o);
    request->send(200, "application/json", o);
  });

  // --- Settings SAVE APIs ---
  auto saveHandler = [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total, int type) {
    if (!isAdminToken(request)) { if (index == 0) request->send(401); return; }
    if (!collectBody(request, data, len, index, total, 2048)) return;
    JsonDocument doc;
    if (deserializeJson(doc, (const char*)request->_tempObject, total)) { request->send(400); return; }
    if (type == 0) { // WiFi
      CfgLock _lk;
      if (doc["nets"].is<JsonArray>()) {          // قائمة الشبكات المحفوظة (الأولى تُنسخ للحقول القديمة)
        JsonDocument nd; JsonArray out = nd.to<JsonArray>();
        for (JsonObject n : doc["nets"].as<JsonArray>()) {
          const char* ss = n["ssid"] | ""; const char* pw = n["pass"] | "";
          if (!*ss || strlen(ss) > 32 || strlen(pw) > 63 || (int)out.size() >= MAX_NETS) continue;
          JsonObject o = out.add<JsonObject>(); o["ssid"] = ss; o["pass"] = pw;
        }
        File nf = LittleFS.open(NETS_FILE, "w");
        if (!nf) { request->send(500); return; }
        serializeJson(nd, nf); nf.close();
        doc["ssid"] = out.size() ? (const char*)(out[0]["ssid"] | "") : "";
        doc["pass"] = out.size() ? (const char*)(out[0]["pass"] | "") : "";
      }
      if (!updateOrCreateJson("/settings.json", "0_Obj.wifi.mode", doc["mode"]) ||
          !updateOrCreateJson("/settings.json", "0_Obj.wifi.ssid", doc["ssid"]) ||
          !updateOrCreateJson("/settings.json", "0_Obj.wifi.pass", doc["pass"]) ||
          !updateOrCreateJson("/settings.json", "0_Obj.wifi.ap_ssid", doc["ap_ssid"]) ||
          !updateOrCreateJson("/settings.json", "0_Obj.wifi.ap_pass", doc["ap_pass"])) {
        request->send(400); return;
      }
      wifiApplyPending = true; // يُطبَّق في loop() بعد إرسال الرد
    } else if (type == 1) { // Comm
      CfgLock _lk;
      if (doc["localhost"].is<const char*>()) {
        if (!validHost(String(doc["localhost"].as<const char*>()))) { request->send(400); return; }
        String nh = doc["localhost"].as<const char*>(); if (nh.endsWith(".local")) nh.remove(nh.length() - 6);
        bool changed = nh != String(hostname_);
        if (!updateOrCreateJson("/settings.json", "0_Obj.localhost", doc["localhost"])) { request->send(400); return; }
        mdnsApplyPending = true;                       // يُحدَّث mDNS فورًا
        if (changed) restartAt = millis() + 1500;      // وعند تغيّر الاسم: حفظ ثم إعادة تشغيل نظيفة
      }
      if (doc["host"].is<const char*>() && !updateOrCreateJson("/settings.json", "0_Obj.internethost", doc["host"])) {
        request->send(400); return;
      }
    } else if (type == 2) { // Pin
      if (!updateOrCreateJson("/settings.json", "0_Obj.pinreset", doc["pin"])) { request->send(400); return; }
    } else if (type == 3) { // Pass
      const char* ap = doc["ap"].is<const char*>() ? doc["ap"].as<const char*>() : "";
      const char* up = doc["up"].is<const char*>() ? doc["up"].as<const char*>() : "";
      if (!*ap && !*up) { request->send(400); return; }
      if ((*ap && strlen(ap) > 64) || (*up && strlen(up) > 64)) { request->send(400); return; }
      CfgLock _lk;
      if (*ap) {
        char hp[97];
        if (!hashPassword(ap, hp, sizeof(hp)) || !updateOrCreateJson("/users.json", "0_Obj.password", hp)) {
          request->send(500); return;
        }
      }
      if (*up) {
        char hp[97];
        if (!hashPassword(up, hp, sizeof(hp)) || !updateOrCreateJson("/users.json", "1_Obj.password", hp)) {
          request->send(500); return;
        }
      }
    }
    request->send(200);
  };

  server.on("/settings/save_wifi", HTTP_POST, [](AsyncWebServerRequest* r){}, NULL, [saveHandler](AsyncWebServerRequest* r, uint8_t* d, size_t l, size_t i, size_t t){ saveHandler(r,d,l,i,t,0); });
  server.on("/settings/save_comm", HTTP_POST, [](AsyncWebServerRequest* r){}, NULL, [saveHandler](AsyncWebServerRequest* r, uint8_t* d, size_t l, size_t i, size_t t){ saveHandler(r,d,l,i,t,1); });
  server.on("/settings/save_pin", HTTP_POST, [](AsyncWebServerRequest* r){}, NULL, [saveHandler](AsyncWebServerRequest* r, uint8_t* d, size_t l, size_t i, size_t t){ saveHandler(r,d,l,i,t,2); });
  server.on("/settings/save_pass", HTTP_POST, [](AsyncWebServerRequest* r){}, NULL, [saveHandler](AsyncWebServerRequest* r, uint8_t* d, size_t l, size_t i, size_t t){ saveHandler(r,d,l,i,t,3); });

  // --- Settings GET APIs ---
  
  server.on("/settings/get_stat", HTTP_GET, [](AsyncWebServerRequest *request){
    if(wifiIsWorking) {
       request->send(503);
    } else if (WiFi.status() != WL_CONNECTED) {
       JsonDocument doc;
       doc["ip"] = WiFi.softAPIP().toString();
       String output; serializeJson(doc, output);
       request->send(200, "application/json", output);
    } else {
       JsonDocument doc;
       doc["ssid"] = WiFi.SSID();
       doc["ip"] = WiFi.localIP().toString();
       doc["status"] = "CONNECTED";
       doc["rssi"] = WiFi.RSSI();
       String output; serializeJson(doc, output);
       request->send(200, "application/json", output);
    }
  });
  
  server.on("/settings/scan_wifi", HTTP_GET, [](AsyncWebServerRequest *request){
    if(wifiIsWorking) {
       request->send(503);
    } else {
    int n = WiFi.scanComplete();
    if(n == WIFI_SCAN_FAILED) {        // لم يبدأ مسح بعد
       WiFi.scanNetworks(true);
       request->send(202);
    } else if(n == WIFI_SCAN_RUNNING) { // المسح جارٍ، لا تلغِه
       request->send(202);
    } else {
       JsonDocument doc;
       JsonArray root = doc.to<JsonArray>();
       for (int i = 0; i < n; ++i) {
         JsonObject net = root.add<JsonObject>();
         net["ssid"] = WiFi.SSID(i);
         net["rssi"] = WiFi.RSSI(i);
         net["secure"] = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
       }
       WiFi.scanDelete();
       WiFi.scanNetworks(true);
       String output; serializeJson(doc, output);
       request->send(200, "application/json", output);
    }
    }
  });
  
  server.on("/settings/get_wifi", HTTP_GET, [](AsyncWebServerRequest *request){
    if (!isAdminToken(request)) { request->send(401); return; }
    char m[33] = "", ap_s[33] = "", ap_p[65] = "";
    getJson("/settings.json", "0_Obj.wifi.mode", m, sizeof(m));
    getJson("/settings.json", "0_Obj.wifi.ap_ssid", ap_s, sizeof(ap_s));
    getJson("/settings.json", "0_Obj.wifi.ap_pass", ap_p, sizeof(ap_p));
    JsonDocument nd; loadNets(nd);
    JsonDocument r; r["mode"] = m; r["ap_ssid"] = ap_s; r["ap_pass"] = ap_p; r["nets"] = nd;
    String res; serializeJson(r, res);
    request->send(200, "application/json", res);
  });

  server.on("/settings/get_comm", HTTP_GET, [](AsyncWebServerRequest *request){
    if (!isAnyToken(request)) { request->send(401); return; }
    char l[40] = "", h[65] = "";
    getJson("/settings.json", "0_Obj.localhost", l, sizeof(l));
    getJson("/settings.json", "0_Obj.internethost", h, sizeof(h));
    JsonDocument d; d["localhost"] = l; d["host"] = h;
    String res; serializeJson(d, res);
    request->send(200, "application/json", res);
  });

  server.on("/settings/get_pin", HTTP_GET, [](AsyncWebServerRequest *request){
    if (!isAnyToken(request)) { request->send(401); return; }
    char p[33] = "";
    getJson("/settings.json", "0_Obj.pinreset", p, sizeof(p));
    JsonDocument d; d["pin"] = p;
    String res; serializeJson(d, res);
    request->send(200, "application/json", res);
  });

  server.on("/settings/get_pass", HTTP_GET, [](AsyncWebServerRequest *request){
    if (isAdminToken(request)) {
        char ap[97] = "", up[97] = "";
        bool hasAp = getJson("/users.json", "0_Obj.password", ap, sizeof(ap)) && ap[0] != '\0';
        bool hasUp = getJson("/users.json", "1_Obj.password", up, sizeof(up)) && up[0] != '\0';
        JsonDocument d; d["hasAp"] = hasAp; d["hasUp"] = hasUp; d["ap"] = ""; d["up"] = "";
        String res; serializeJson(d, res);
        request->send(200, "application/json", res);
    } else { request->send(401); }
  });

  server.on("/hdusage", HTTP_GET, [](AsyncWebServerRequest *request){
    if (!isAnyToken(request)) { request->send(401); return; }
    char r[16], s[16];
    RamUsage(r, 16); StorageUsage(s, 16);
    int wp; const char* wr = wakeReason(&wp);
    JsonDocument d;
    d["RamUsage"] = r; d["StorageUsage"] = s; d["Version"] = FW_VERSION;
    d["Uptime"] = uptimeSec();
    char tb[16]; snprintf(tb, sizeof(tb), "%.1f", (double)temperatureRead());
    d["Temp"] = atof(tb);
    d["Wake"] = wr; d["WakePin"] = wp;
    String res; serializeJson(d, res);
    request->send(200, "application/json", res);
  });

  // --- OTA (admin، أو مفتاح الطوارئ للواجهة فقط): POST /api/ota?type=fw  (firmware.bin)  أو  type=fs  (littlefs.bin) ---
  server.on("/api/ota", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!otaAllowed(request)) { request->send(401); return; }
    OtaReq* ota = (OtaReq*)request->_tempObject;
    if (!ota) { request->send(400, "application/json", "{\"error\":\"no file\"}"); return; }
    if (ota->busyReject) { request->send(409, "application/json", "{\"error\":\"ota busy\"}"); return; }
    bool active = ota->active, started = ota->started, unmounted = ota->fsUnmounted;
    char err[64]; strlcpy(err, ota->err, sizeof(err));
    ota->active = false;
    if (active) {
      if (err[0] && unmounted) LittleFS.begin(false);
      portENTER_CRITICAL(&otaMux);
      otaBusy = false;
      portEXIT_CRITICAL(&otaMux);
    }
    if (err[0]) {
      JsonDocument d; d["error"] = err;
      String out; serializeJson(d, out);
      request->send(500, "application/json", out);
      return;
    }
    if (!started) { request->send(400, "application/json", "{\"error\":\"no file\"}"); return; }
    request->send(200, "application/json", "{\"ok\":true}");
    restartPending = true;
  }, [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
    if (!index) {
      if (!otaAllowed(request)) return;
      OtaReq* ota = (OtaReq*)calloc(1, sizeof(OtaReq));
      if (!ota) return;
      request->_tempObject = ota;
      bool alreadyBusy = false;
      portENTER_CRITICAL(&otaMux);
      if (otaBusy) alreadyBusy = true;
      else otaBusy = true;
      portEXIT_CRITICAL(&otaMux);
      if (alreadyBusy) { ota->busyReject = true; strlcpy(ota->err, "ota busy", sizeof(ota->err)); return; }
      ota->active = true;
      request->onDisconnect([ota]() {
        if (!ota->active) return;
        ota->active = false;
        if (ota->started) Update.abort();
        if (ota->fsUnmounted) LittleFS.begin(false);
        portENTER_CRITICAL(&otaMux);
        otaBusy = false;
        portEXIT_CRITICAL(&otaMux);
      });
      ota->fs = request->hasParam("type") && request->getParam("type")->value() == "fs";
      if (!ota->fs && (len == 0 || data[0] != 0xE9)) { strlcpy(ota->err, "not a firmware image", sizeof(ota->err)); return; }
      if (ota->fs) { luaeng_stop_all(); delay(50); LittleFS.end(); ota->fsUnmounted = true; }
      if (!Update.begin(UPDATE_SIZE_UNKNOWN, ota->fs ? U_SPIFFS : U_FLASH)) {
        strlcpy(ota->err, Update.errorString(), sizeof(ota->err));
        return;
      }
      ota->started = true;
    }
    OtaReq* ota = (OtaReq*)request->_tempObject;
    if (!ota || !ota->active || ota->err[0] || !ota->started) return;
    if (len > 0 && Update.write(data, len) != len) {
      strlcpy(ota->err, Update.errorString(), sizeof(ota->err));
      Update.abort();
      ota->started = false;
      return;
    }
    if (final) {
      if (!Update.end(true)) strlcpy(ota->err, Update.errorString(), sizeof(ota->err));
      ota->started = false;
    }
  });

  // --- نسخة احتياطية (admin فقط): تنزيل واستعادة settings.json و users.json و wifi_nets.json ---
  server.on("/settings/backup", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!isAdminToken(request)) { request->send(401); return; }
    JsonDocument doc, stDoc, usDoc, netDoc;
    doc["version"] = 1;
    {
      CfgLock _lk;
      File f = LittleFS.open("/settings.json", "r");
      if (f) { deserializeJson(stDoc, f); f.close(); }
      if (!stDoc.is<JsonArray>()) stDoc.to<JsonArray>();
      f = LittleFS.open("/users.json", "r");
      if (f) { deserializeJson(usDoc, f); f.close(); }
      if (!usDoc.is<JsonArray>()) usDoc.to<JsonArray>();
      doc["settings"] = stDoc;
      doc["users"] = usDoc;
      f = LittleFS.open(NETS_FILE, "r");
      if (f) {
        if (!deserializeJson(netDoc, f) && netDoc.is<JsonArray>()) doc["nets"] = netDoc;
        f.close();
      }
    }
    String out; serializeJson(doc, out);
    AsyncWebServerResponse *response = request->beginResponse(200, "application/json", out);
    response->addHeader("Content-Disposition", "attachment; filename=esp32-backup.json");
    request->send(response);
  });

  server.on("/settings/restore", HTTP_POST, [](AsyncWebServerRequest *request){}, NULL,
  [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    if (!isAdminToken(request)) { if (index == 0) request->send(401); return; }
    if (!collectBody(request, data, len, index, total, 4096)) return;

    JsonDocument doc;
    if (deserializeJson(doc, (const char*)request->_tempObject, total)) { request->send(400, "application/json", "{\"error\":\"invalid json\"}"); return; }
    JsonArray st = doc["settings"].as<JsonArray>();
    JsonArray us = doc["users"].as<JsonArray>();
    bool valid = !st.isNull() && st.size() >= 1 && st[0].is<JsonObject>()
              && !us.isNull() && us.size() >= 2
              && us[0]["username"].is<const char*>() && us[0]["password"].is<const char*>()
              && us[1]["username"].is<const char*>() && us[1]["password"].is<const char*>();
    if (!valid) { request->send(400, "application/json", "{\"error\":\"invalid backup\"}"); return; }

    // تعمية كلمات المرور إن كانت من نسخة قديمة غير معمّاة
    char ha[97] = "", hu[97] = "";
    const char* p0 = us[0]["password"].as<const char*>();
    const char* p1 = us[1]["password"].as<const char*>();
    if (strncmp(p0, "sha256$", 7) != 0) {
      if (!*p0 || !hashPassword(p0, ha, sizeof(ha))) { request->send(400, "application/json", "{\"error\":\"invalid backup\"}"); return; }
      us[0]["password"] = (const char*)ha;
    }
    if (strncmp(p1, "sha256$", 7) != 0) {
      if (!*p1 || !hashPassword(p1, hu, sizeof(hu))) { request->send(400, "application/json", "{\"error\":\"invalid backup\"}"); return; }
      us[1]["password"] = (const char*)hu;
    }

    {
      CfgLock _lk;
      File f = LittleFS.open("/settings.json", "w");
      if (!f) { request->send(500); return; }
      size_t ws = serializeJsonPretty(st, f); f.close();
      if (ws == 0) { request->send(500); return; }
      f = LittleFS.open("/users.json", "w");
      if (!f) { request->send(500); return; }
      size_t wu = serializeJsonPretty(us, f); f.close();
      if (wu == 0) { request->send(500); return; }
      if (doc["nets"].is<JsonArray>()) {
        JsonDocument nd; JsonArray out = nd.to<JsonArray>();
        for (JsonObject n : doc["nets"].as<JsonArray>()) {
          const char* ss = n["ssid"] | ""; const char* pw = n["pass"] | "";
          if (!*ss || strlen(ss) > 32 || strlen(pw) > 63 || (int)out.size() >= MAX_NETS) continue;
          JsonObject o = out.add<JsonObject>(); o["ssid"] = ss; o["pass"] = pw;
        }
        File nf = LittleFS.open(NETS_FILE, "w");
        if (nf) { serializeJson(nd, nf); nf.close(); }
      } else if (LittleFS.exists(NETS_FILE)) {
        LittleFS.remove(NETS_FILE);
      }
    }

    request->send(200, "application/json", "{\"ok\":true}");
    restartPending = true;
  });

  // --- إعادة التشغيل (admin) ---
  server.on("/settings/restart", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!isAdminToken(request)) { request->send(401); return; }
    logPrintf("Restart requested\n");
    request->send(200, "application/json", "{\"ok\":true}");
    restartPending = true;
  });

  // --- النوم العميق (admin): /settings/sleep?secs=60&pins=0,3&level=1 ---
  server.on("/settings/sleep", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!isAdminToken(request)) { request->send(401); return; }
    long secsL = 0, levelL = 1;
    if (request->hasParam("secs")) {
      if (!parseLongStrict(request->getParam("secs")->value(), secsL) || secsL < 0 || secsL > 30L * 86400L) {
        request->send(400, "application/json", "{\"error\":\"bad_args\"}"); return;
      }
    }
    if (request->hasParam("level")) {
      if (!parseLongStrict(request->getParam("level")->value(), levelL) || (levelL != 0 && levelL != 1)) {
        request->send(400, "application/json", "{\"error\":\"bad_args\"}"); return;
      }
    }
    uint32_t mask = 0;
    if (request->hasParam("pins")) {
      String pins = request->getParam("pins")->value();
      if (pins.length() > 0) {
        for (int i = 0; i < (int)pins.length(); ) {
          int j = pins.indexOf(',', i); if (j < 0) j = pins.length();
          long n = -1;
          if (j <= i || !parseLongStrict(pins.substring(i, j), n) || n < 0 || n > 5) {
            request->send(400, "application/json", "{\"error\":\"bad_args\"}"); return;
          }
          mask |= 1u << (uint32_t)n;
          i = j + 1;
        }
      }
    }
    if (!requestDeepSleep((uint32_t)secsL, mask, (int)levelL)) { request->send(400, "application/json", "{\"error\":\"bad_args\"}"); return; }
    request->send(200, "application/json", "{\"ok\":true}");
  });

  // --- الطرفية (admin): قراءة السجل وإرسال الأوامر ---
  server.on("/terminal/log", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!isAdminToken(request)) { request->send(401); return; }
    uint32_t since = request->hasParam("since") ? strtoul(request->getParam("since")->value().c_str(), NULL, 10) : 0;
    char* tmp = (char*)malloc(LOG_SIZE + 1);
    if (!tmp) { request->send(500); return; }
    portENTER_CRITICAL(&logMux);
    uint32_t total = logTotal;
    uint32_t oldest = total > LOG_SIZE ? total - LOG_SIZE : 0;
    uint32_t start = (since > total || since < oldest) ? oldest : since;
    uint32_t n = total - start;
    for (uint32_t i = 0; i < n; i++) tmp[i] = logBuf[(start + i) % LOG_SIZE];
    portEXIT_CRITICAL(&logMux);
    tmp[n] = 0;
    JsonDocument d;
    d["seq"] = total;
    d["text"] = (const char*)tmp;
    String o; serializeJson(d, o);
    free(tmp);
    request->send(200, "application/json", o);
  });

  server.on("/terminal/cmd", HTTP_POST, [](AsyncWebServerRequest *request){}, NULL,
  [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    if (!isAdminToken(request)) { if (index == 0) request->send(401); return; }
    if (!collectBody(request, data, len, index, total, 512)) return;
    JsonDocument d;
    if (deserializeJson(d, (const char*)request->_tempObject, total)) { request->send(400); return; }
    const char* c = d["cmd"];
    if (!c) { request->send(400); return; }
    String out = runCommand(String(c));
    JsonDocument r;
    r["out"] = out;
    String o; serializeJson(r, o);
    request->send(200, "application/json", o);
  });

  // --- السكريبتات (Lua) ---
  server.on("/script/list", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!isAdminToken(request)) { request->send(401); return; }
    request->send(200, "application/json", luaeng_list_json());
  });

  server.on("/script/run", HTTP_POST, [](AsyncWebServerRequest *request){}, NULL,
  [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    // الجسم = نص السكريبت الخام، والاسم في ?name=
    if (!isAdminToken(request)) { if (index == 0) request->send(401); return; }
    if (!collectBody(request, data, len, index, total, 8192)) return;
    char* buf = (char*)request->_tempObject;
    const char* name = request->hasParam("name") ? request->getParam("name")->value().c_str() : "";
    if (!luaeng_valid_name(name)) { request->send(400, "application/json", "{\"ok\":false}"); return; }
    if (request->hasParam("lib") && request->getParam("lib")->value() == "1") {   // حفظ مكتبة require بلا تشغيل
      bool okLib = luaeng_save_lib(name, buf, total);
      request->send(okLib ? 200 : 500, "application/json", okLib ? "{\"ok\":true}" : "{\"ok\":false}");
      return;
    }
    bool save = request->hasParam("save") && request->getParam("save")->value() == "1";
    String page = request->hasParam("page") ? request->getParam("page")->value() : String("");
    if (page.length() && !validUiPage(page)) { request->send(400, "application/json", "{\"ok\":false}"); return; }
    if (save && !luaeng_save(name, buf, total, page.c_str())) { request->send(500, "application/json", "{\"ok\":false}"); return; }
    bool ok = luaeng_run(name, buf, total);
    request->send(ok ? 200 : 503, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
  });

  // ---- الدرايفرات الموقّعة (.drv) ----
  server.on("/drv/mode", HTTP_GET, [](AsyncWebServerRequest* r) {
    if (!isAnyToken(r)) { r->send(401); return; }
    JsonDocument d; d["mode"] = drv_get_mode(); d["exec"] = drv_exec_available();
    String out; serializeJson(d, out);
    r->send(200, "application/json", out);
  });
  server.on("/drv/mode", HTTP_POST, [](AsyncWebServerRequest* r) {
    if (!isAdminToken(r)) { r->send(401); return; }
    const AsyncWebParameter* p = r->hasParam("mode") ? r->getParam("mode") : nullptr;
    bool ok = p && drv_set_mode(p->value().c_str());
    r->send(ok ? 200 : 400, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false,\"error\":\"bad mode\"}");
  });
  server.on("/drv/list", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!isAdminToken(request)) { request->send(401); return; }
    request->send(200, "application/json", drv_list_json());
  });
  server.on("/drv/install", HTTP_POST, [](AsyncWebServerRequest *request){}, NULL,
  [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    // الجسم = ملف .drv خام (application/octet-stream)؛ يتحقق الفيرموير من التوقيع قبل أي حفظ
    if (!isAdminToken(request)) { if (index == 0) request->send(401); return; }
    if (total == 0 || total > DRV_MAX_FILE) { if (index == 0) request->send(413, "application/json", "{\"ok\":false,\"error\":\"too large\"}"); return; }
    if (index == 0) {
      request->_tempObject = malloc(total);
      if (!request->_tempObject) { request->send(500, "application/json", "{\"ok\":false,\"error\":\"no memory\"}"); return; }
    }
    uint8_t* buf = (uint8_t*)request->_tempObject;
    if (!buf) return;
    if (index + len > total) { request->send(400, "application/json", "{\"ok\":false,\"error\":\"bad chunk\"}"); return; }
    if (len > 0) memcpy(buf + index, data, len);
    if (index + len < total) return;
    String err;
    bool ok = drv_install(buf, total, &err);
    JsonDocument d; d["ok"] = ok; if (!ok) d["error"] = err;
    String out; serializeJson(d, out);
    request->send(ok ? 200 : 400, "application/json", out);
  });
  server.on("/drv/remove", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!isAdminToken(request)) { request->send(401); return; }
    String n = request->hasParam("name") ? request->getParam("name")->value() : String("");
    String err;
    bool ok = drv_remove(n.c_str(), &err);
    JsonDocument d; d["ok"] = ok; if (!ok) d["error"] = err;
    String out; serializeJson(d, out);
    request->send(ok ? 200 : 400, "application/json", out);
  });

  server.on("/script/files", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!isAdminToken(request)) { request->send(401); return; }
    request->send(200, "application/json", luaeng_files_json());
  });
  server.on("/script/libs", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!isAdminToken(request)) { request->send(401); return; }
    request->send(200, "application/json", luaeng_libs_json());
  });
  server.on("/script/dellib", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!isAdminToken(request)) { request->send(401); return; }
    String n = request->hasParam("name") ? request->getParam("name")->value() : String("");
    bool ok = luaeng_delete_lib(n.c_str());
    request->send(ok ? 200 : 400, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
  });
  server.on("/script/auto", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!isAdminToken(request)) { request->send(401); return; }
    String n = request->hasParam("name") ? request->getParam("name")->value() : String("");
    bool on = request->hasParam("on") && request->getParam("on")->value() == "1";
    bool ok = luaeng_set_auto(n.c_str(), on);
    request->send(ok ? 200 : 400, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
  });
  server.on("/script/delete", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!isAdminToken(request)) { request->send(401); return; }
    String n = request->hasParam("name") ? request->getParam("name")->value() : String("");
    bool ok = luaeng_delete(n.c_str());
    request->send(ok ? 200 : 400, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
  });
  server.on("/script/runfile", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!isAdminToken(request)) { request->send(401); return; }
    String n = request->hasParam("name") ? request->getParam("name")->value() : String("");
    bool ok = luaeng_run_file(n.c_str());
    request->send(ok ? 200 : 400, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
  });

  server.on("/script/stop", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!isAdminToken(request)) { request->send(401); return; }
    String n = request->hasParam("name") ? request->getParam("name")->value() : String("");
    bool ok = n == "all" ? (luaeng_stop_all(), true) : luaeng_stop(n.c_str());
    request->send(ok ? 200 : 400, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
  });

  // --- لوحات الواجهة ---
  server.on("/ui/panels", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!isAdminToken(request)) { request->send(401); return; }
    request->send(200, "application/json", luaeng_ui_panels_json());
  });
  server.on("/ui/poll", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!isAdminToken(request)) { request->send(401); return; }
    uint32_t since = request->hasParam("since") ? strtoul(request->getParam("since")->value().c_str(), NULL, 10) : 0;
    request->send(200, "application/json", luaeng_ui_poll_json(since));
  });
  server.on("/ui/event", HTTP_POST, [](AsyncWebServerRequest *request){}, NULL,
  [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    if (!isAdminToken(request)) { if (index == 0) request->send(401); return; }
    if (index != 0 || len != total || total > 46) { if (index == 0) request->send(413); return; }
    if (!request->hasParam("p") || !request->hasParam("n")) { request->send(400); return; }
    long panel = -1;
    if (!parseLongStrict(request->getParam("p")->value(), panel)) { request->send(400, "application/json", "{\"ok\":false}"); return; }
    char val[48]; memcpy(val, data, len); val[len] = 0;
    bool ok = luaeng_ui_event((int)panel, request->getParam("n")->value().c_str(), val);
    request->send(ok ? 200 : 400, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
  });

  // Fallback للواجهة المفقودة
  server.onNotFound([](AsyncWebServerRequest *request){
    // فُقد /app/index.html أو أول تشغيل: اعرض نافذة مفتاح الطوارئ بدل 404
    if (request->method() == HTTP_GET && request->url() == "/") request->send(gzPage(request, 200, em_login_gz, em_login_gz_len));
    else request->send(404);
  });
}

// --- Setup & Loop ---

void setup() {
  if (!cfgMux) cfgMux = xSemaphoreCreateRecursiveMutex();
  gpio_deep_sleep_hold_dis();                      // فكّ تثبيت الأرجل بعد الاستيقاظ من النوم العميق
  for (int i = 0; i < 6; i++) gpio_hold_dis((gpio_num_t)i);
  generateToken(emSess, sizeof(emSess));
  if (LittleFS.begin(true)) {                      // مفتاح الطوارئ المخصص (اختياري)
    File kf = LittleFS.open("/em_key", "r");
    if (kf) { String k = kf.readString(); k.trim(); if (k.length() >= 4 && k.length() < sizeof(access_key)) strlcpy(access_key, k.c_str(), sizeof(access_key)); kf.close(); }
  }
  loadsetting();
  EmSetupAPI();
  setupServerWeb();
  drv_boot_load();
  luaeng_begin();
  server.begin();
}

void loop() {
  if (sleepAt && (int32_t)(millis() - sleepAt) >= 0) { sleepAt = 0; doDeepSleep(); }
  if (restartAt && (int32_t)(millis() - restartAt) >= 0) { restartAt = 0; ESP.restart(); }
  if (mdnsApplyPending) { mdnsApplyPending = false; loadHost(); startMdns(); }
  if (wifiApplyPending) {
    wifiApplyPending = false;
    delay(300); // أمهل الرد 200 وقتاً ليصل قبل إعادة ضبط الواي فاي
    applyWiFi();
  }
  if (resetWaitUntil) {
    if ((int32_t)(millis() - resetWaitUntil) >= 0) {
      resetWaitUntil = 0;
      logPrintf("Password reset timed out\n");
    } else if (!resetSawHigh) {
      if (digitalRead(resetPin) == HIGH) resetSawHigh = true;   // يجب أن يكون الزر غير مضغوط أولاً
    } else if (digitalRead(resetPin) == LOW) {
      delay(50);
      if (digitalRead(resetPin) == LOW) {
        doPasswordReset();
        resetWaitUntil = 0; resetDone = true; resetDoneAt = millis();
      }
    }
  }
  if (restartPending) {
    delay(1000); // أمهل الرد وقتاً ليصل ثم أعد التشغيل
    ESP.restart();
  }
  delay(10);
}