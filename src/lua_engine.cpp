#include "lua_engine.h"
#include "drv_loader.h"
#include <LittleFS.h>
#include <Wire.h>
#include <SPI.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

// ---------- حدود ----------
#define MAX_SCRIPTS   3
#define MAX_TIMERS    8
#define MAX_WATCH     6
#define MAX_SUBS      8
#define NAME_LEN      24
#define MEM_LIMIT     (48 * 1024)
#define CALL_BUDGET   200      // ms كحد أقصى لكل استدعاء (حارس الحلقات اللانهائية)
#define MAX_CODE      8192
#define PWM_MAX       6
#define NUM_PINS      22
#define PIN_I2C       255   // مالك خاص: أرجل ناقل I2C المشترك
#define PIN_SPI       254   // مالك خاص: أرجل ناقل SPI المشترك

enum { ST_FREE = 0, ST_RUN = 1, ST_ERR = 2 };

struct LSub { bool used; char topic[16]; int ref; };
struct LWatch { bool used; bool pending; uint8_t pin; uint8_t stable; uint8_t pendVal; uint16_t debounce; uint32_t changedAt; int ref; };
struct LTimer { bool used; bool repeat; uint32_t period; uint32_t next; int ref; };

struct Script {
  uint8_t   state;
  char      name[NAME_LEN];
  lua_State* L;
  size_t    mem, limit;
  uint32_t  deadline;
  uint32_t  startMs;
  char      err[96];
  LTimer    timers[MAX_TIMERS];
  LWatch    watches[MAX_WATCH];
  LSub      subs[MAX_SUBS];
  JsonDocument* kv;      // مخزن storage (يُحمَّل عند أول استعمال)
  bool      kvDirty;
  uint32_t  kvSaveAt;
  bool      usesI2c;
  bool      usesSpi;
  uint8_t   requireDepth;
  uint8_t   drvOpen;      // درايفرات مفتوحة لهذا السكريبت
};

static Script scripts[MAX_SCRIPTS];
static uint8_t pinOwner[NUM_PINS];     // 0 = حر، وإلا (فهرس السكريبت + 1)
static uint8_t pwmPins[PWM_MAX];       // رقم الدبوس + 1 (0 = فارغ)
static SemaphoreHandle_t statusMux;
static QueueHandle_t cmdQ;

enum { C_RUN, C_STOP, C_STOPALL };
struct Cmd { uint8_t type; char name[NAME_LEN]; char* code; size_t len; };

// ---------- أدوات ----------
bool luaeng_valid_name(const char* n) {
  if (!n || !*n) return false;
  size_t l = strlen(n);
  if (l >= NAME_LEN) return false;
  for (size_t i = 0; i < l; i++) {
    char c = n[i];
    if (!(isalnum((unsigned char)c) || c == '_' || c == '-')) return false;
  }
  return true;
}

static int idxOf(Script* s) { return (int)(s - scripts); }
static Script* findByName(const char* n) {
  for (auto& s : scripts) if (s.state != ST_FREE && strcmp(s.name, n) == 0) return &s;
  return nullptr;
}
static Script* me(lua_State* L) { void* ud; lua_getallocf(L, &ud); return (Script*)ud; }

static bool pinAllowed(int p) {
  // GPIO11-17 فلاش، 18-19 USB. المسموح: 0-10 و20 و21
  return (p >= 0 && p <= 10) || p == 20 || p == 21;
}

static int claimPin(lua_State* L, int p) {
  Script* s = me(L);
  if (!pinAllowed(p)) return luaL_error(L, "pin %d not allowed", p);
  int o = pinOwner[p];
  if (o == PIN_I2C) return luaL_error(L, "pin %d is used by the I2C bus", p);
  if (o == PIN_SPI) return luaL_error(L, "pin %d is used by the SPI bus", p);
  if (o && o != idxOf(s) + 1) return luaL_error(L, "pin %d owned by %s", p, scripts[o - 1].name);
  pinOwner[p] = idxOf(s) + 1;
  return 0;
}

// ---------- مخصص الذاكرة المحدود ----------
static void* limAlloc(void* ud, void* ptr, size_t osize, size_t nsize) {
  Script* s = (Script*)ud;
  if (!ptr) osize = 0;
  if (nsize == 0) {
    if (ptr) { s->mem -= osize; free(ptr); }
    return NULL;
  }
  if (nsize > osize && s->mem - osize + nsize > s->limit) return NULL;
  void* p = realloc(ptr, nsize);
  if (p) s->mem = s->mem - osize + nsize;
  return p;
}

static void hook(lua_State* L, lua_Debug*) {
  Script* s = me(L);
  if ((int32_t)(millis() - s->deadline) >= 0) luaL_error(L, "script too slow (>%d ms without yielding)", CALL_BUDGET);
}

// ---------- واجهة Lua ----------
static int l_print(lua_State* L) {
  Script* s = me(L);
  char line[140];
  int n = snprintf(line, sizeof(line), "[%s] ", s->name);
  int top = lua_gettop(L);
  for (int i = 1; i <= top && n < (int)sizeof(line) - 2; i++) {
    size_t l; const char* v = luaL_tolstring(L, i, &l);
    int w = snprintf(line + n, sizeof(line) - n, "%s%s", i > 1 ? "\t" : "", v);
    if (w > 0) n += w;
    lua_pop(L, 1);
  }
  if (n >= (int)sizeof(line) - 1) n = sizeof(line) - 2;
  line[n] = '\n'; line[n + 1] = 0;
  logPrintf("%s", line);
  return 0;
}

static int l_gpio_mode(lua_State* L) {
  int p = luaL_checkinteger(L, 1);
  const char* m = luaL_checkstring(L, 2);
  claimPin(L, p);
  if (!strcmp(m, "out")) pinMode(p, OUTPUT);
  else if (!strcmp(m, "in")) pinMode(p, INPUT);
  else if (!strcmp(m, "pullup") || !strcmp(m, "in_pullup")) pinMode(p, INPUT_PULLUP);
  else if (!strcmp(m, "pulldown") || !strcmp(m, "in_pulldown")) pinMode(p, INPUT_PULLDOWN);
  else return luaL_error(L, "mode must be out|in|in_pullup|in_pulldown");
  return 0;
}
static int l_gpio_write(lua_State* L) {
  int p = luaL_checkinteger(L, 1);
  int v = lua_isboolean(L, 2) ? lua_toboolean(L, 2) : (int)luaL_checkinteger(L, 2);
  claimPin(L, p);
  digitalWrite(p, v ? HIGH : LOW);
  return 0;
}
static int l_gpio_read(lua_State* L) {
  int p = luaL_checkinteger(L, 1);
  claimPin(L, p);
  lua_pushinteger(L, digitalRead(p));
  return 1;
}
static int l_adc_read(lua_State* L) {
  int p = luaL_checkinteger(L, 1);
  if (p < 0 || p > 4) return luaL_error(L, "ADC only on GPIO0-4");
  claimPin(L, p);
  lua_pushinteger(L, analogRead(p));
  return 1;
}
static int l_adc_mv(lua_State* L) {
  int p = luaL_checkinteger(L, 1);
  if (p < 0 || p > 4) return luaL_error(L, "ADC only on GPIO0-4");
  claimPin(L, p);
  lua_pushinteger(L, analogReadMilliVolts(p));
  return 1;
}
// pwm.set(pin, freqHz, duty%)
static int l_pwm_set(lua_State* L) {
  int p = luaL_checkinteger(L, 1);
  int f = luaL_checkinteger(L, 2);
  lua_Number d = luaL_checknumber(L, 3);
  if (f < 1 || f > 40000) return luaL_error(L, "freq 1..40000");
  if (d < 0) d = 0; if (d > 100) d = 100;
  claimPin(L, p);
  int slot = -1;
  for (int i = 0; i < PWM_MAX; i++) if (pwmPins[i] == p + 1) slot = i;
  if (slot < 0) {
    for (int i = 0; i < PWM_MAX; i++) if (!pwmPins[i]) { slot = i; break; }
    if (slot < 0) return luaL_error(L, "no free PWM channel (max %d)", PWM_MAX);
    if (!ledcAttach(p, f, 10)) return luaL_error(L, "PWM attach failed");
    pwmPins[slot] = p + 1;
  } else ledcChangeFrequency(p, f, 10);
  ledcWrite(p, (uint32_t)(d * 1023.0f / 100.0f));
  return 0;
}

static int l_gpio_toggle(lua_State* L) {
  int p = luaL_checkinteger(L, 1);
  claimPin(L, p);
  digitalWrite(p, digitalRead(p) ? LOW : HIGH);
  return 0;
}
// gpio.on_change(pin, fn [, debounce_ms]): يُستدعى fn(value) عند استقرار القيمة الجديدة (فحص دوري كل ~10ms)
static int l_gpio_on_change(lua_State* L) {
  Script* s = me(L);
  int p = luaL_checkinteger(L, 1);
  luaL_checktype(L, 2, LUA_TFUNCTION);
  lua_Integer db = luaL_optinteger(L, 3, 20);
  if (db < 0 || db > 5000) return luaL_error(L, "debounce 0..5000 ms");
  claimPin(L, p);
  int slot = -1;
  for (int i = 0; i < MAX_WATCH; i++) if (s->watches[i].used && s->watches[i].pin == p) slot = i;
  if (slot < 0) for (int i = 0; i < MAX_WATCH; i++) if (!s->watches[i].used) { slot = i; break; }
  if (slot < 0) return luaL_error(L, "too many watchers (max %d)", MAX_WATCH);
  if (s->watches[slot].used) luaL_unref(L, LUA_REGISTRYINDEX, s->watches[slot].ref);
  lua_pushvalue(L, 2);
  s->watches[slot] = { true, false, (uint8_t)p, (uint8_t)digitalRead(p), 0, (uint16_t)db, 0, luaL_ref(L, LUA_REGISTRYINDEX) };
  return 0;
}
static int l_gpio_off_change(lua_State* L) {
  Script* s = me(L);
  int p = luaL_checkinteger(L, 1);
  for (auto& w : s->watches) if (w.used && w.pin == p) { luaL_unref(L, LUA_REGISTRYINDEX, w.ref); w.used = false; }
  return 0;
}
static int l_adc_atten(lua_State* L) {
  int p = luaL_checkinteger(L, 1);
  const char* l = luaL_checkstring(L, 2);
  if (p < 0 || p > 4) return luaL_error(L, "ADC only on GPIO0-4");
  adc_attenuation_t a;
  if (!strcmp(l, "0db")) a = ADC_0db;
  else if (!strcmp(l, "2.5db")) a = ADC_2_5db;
  else if (!strcmp(l, "6db")) a = ADC_6db;
  else if (!strcmp(l, "11db")) a = ADC_11db;
  else return luaL_error(L, "level must be 0db|2.5db|6db|11db");
  claimPin(L, p);
  analogSetPinAttenuation(p, a);
  return 0;
}
static int findPwm(int p) { for (int i = 0; i < PWM_MAX; i++) if (pwmPins[i] == p + 1) return i; return -1; }
static int l_pwm_stop(lua_State* L) {
  int p = luaL_checkinteger(L, 1);
  claimPin(L, p);
  int slot = findPwm(p);
  if (slot >= 0) { ledcDetach(p); pwmPins[slot] = 0; pinMode(p, OUTPUT); digitalWrite(p, LOW); }
  return 0;
}
// pwm.duty(pin, percent): يغيّر النسبة فقط (بعد pwm.set)
static int l_pwm_duty(lua_State* L) {
  int p = luaL_checkinteger(L, 1);
  lua_Number d = luaL_checknumber(L, 2);
  if (d < 0) d = 0;
  if (d > 100) d = 100;
  claimPin(L, p);
  if (findPwm(p) < 0) return luaL_error(L, "call pwm.set(pin, freq, duty) first");
  ledcWrite(p, (uint32_t)(d * 1023.0f / 100.0f));
  return 0;
}
static int l_sys_uptime(lua_State* L) { lua_pushinteger(L, (lua_Integer)(millis() / 1000)); return 1; }
static int l_sys_version(lua_State* L) { lua_pushstring(L, luaeng_fw_version()); return 1; }
static int l_sys_millis(lua_State* L) { lua_pushinteger(L, (lua_Integer)millis()); return 1; }
static int l_sys_temp(lua_State* L) { lua_pushnumber(L, temperatureRead()); return 1; }
// sys.deep_sleep(seconds, {pins...}, level): يتوقف السكريبت والجهاز بعد لحظات. level: 1 (افتراضي) استيقاظ بجهد عالٍ، 0 منخفض. أرجل الاستيقاض 0..5 فقط
static int l_sys_deep_sleep(lua_State* L) {
  lua_Integer secs = luaL_optinteger(L, 1, 0);
  uint32_t mask = 0;
  if (lua_istable(L, 2)) {
    int n = (int)lua_rawlen(L, 2);
    for (int i = 1; i <= n; i++) {
      lua_rawgeti(L, 2, i);
      lua_Integer p = luaL_optinteger(L, -1, -1); lua_pop(L, 1);
      if (p < 0 || p > 5) return luaL_error(L, "wake pin must be 0..5");
      mask |= 1u << p;
    }
  }
  int level = (int)luaL_optinteger(L, 3, 1);
  if (secs < 0 || !requestDeepSleep((uint32_t)secs, mask, level)) return luaL_error(L, "deep_sleep: need seconds (<=30d) and/or wake pins");
  return 0;
}
// sys.wake_reason() -> "timer" | "gpio" | "reset", pin|nil
static int l_sys_wake_reason(lua_State* L) {
  int pin; lua_pushstring(L, wakeReason(&pin));
  if (pin >= 0) { lua_pushinteger(L, pin); return 2; }
  return 1;
}
static int l_sys_heap(lua_State* L) { lua_pushinteger(L, ESP.getFreeHeap()); return 1; }

static int addTimer(lua_State* L, bool repeat) {
  Script* s = me(L);
  lua_Integer ms = luaL_checkinteger(L, 1);
  luaL_checktype(L, 2, LUA_TFUNCTION);
  if (ms < (repeat ? 10 : 0) || ms > 86400000) return luaL_error(L, "bad interval");
  for (int i = 0; i < MAX_TIMERS; i++) if (!s->timers[i].used) {
    lua_pushvalue(L, 2);
    s->timers[i] = { true, repeat, (uint32_t)ms, millis() + (uint32_t)ms, luaL_ref(L, LUA_REGISTRYINDEX) };
    lua_pushinteger(L, i + 1);
    return 1;
  }
  return luaL_error(L, "too many timers (max %d)", MAX_TIMERS);
}
static int l_timer_every(lua_State* L) { return addTimer(L, true); }
static int l_timer_after(lua_State* L) { return addTimer(L, false); }
static void freeTimer(lua_State* L, LTimer& t) {
  if (t.used) { luaL_unref(L, LUA_REGISTRYINDEX, t.ref); t.used = false; }
}
static int l_timer_cancel(lua_State* L) {
  Script* s = me(L);
  int id = luaL_checkinteger(L, 1);
  if (id >= 1 && id <= MAX_TIMERS) freeTimer(L, s->timers[id - 1]);
  return 0;
}

static void reg(lua_State* L, const char* tbl, const luaL_Reg* fns) {
  lua_newtable(L);
  luaL_setfuncs(L, fns, 0);
  lua_setglobal(L, tbl);
}


// ---------- لوحات الواجهة (UI panels) ----------
#define MAX_PANELS        6
#define PANELS_PER_SCRIPT 4
#define MAX_WIDGETS       40
#define MAX_MSGS          48
#define ID_LEN            16
#define VAL_LEN           48

enum { W_LABEL, W_BUTTON, W_SWITCH, W_SLIDER, W_PROGRESS, W_INPUT, W_COUNT };
static const char* WTYPES[W_COUNT] = { "label", "button", "switch", "slider", "progress", "input" };

struct Panel  { bool used; uint8_t owner; char script[NAME_LEN]; char title[24]; char page[40]; };
struct Widget { bool used; uint8_t panel; uint8_t type; char id[ID_LEN]; char label[24]; float mn, mx; char val[VAL_LEN]; };
struct Msg    { uint32_t seq; uint8_t panel; char name[ID_LEN]; char val[VAL_LEN]; };
struct UiEv   { uint8_t panel; char name[ID_LEN]; char val[VAL_LEN]; };

static Panel  panels[MAX_PANELS];
static Widget widgets[MAX_WIDGETS];
static Msg    msgs[MAX_MSGS];
static uint32_t msgSeq = 0, uiVer = 1;
static SemaphoreHandle_t uiMux;
static QueueHandle_t evQ;

static bool safeName(const char* n, size_t max) {
  size_t l = strlen(n);
  if (!l || l >= max) return false;
  for (size_t i = 0; i < l; i++) {
    char c = n[i];
    if (!(isalnum((unsigned char)c) || c == '_' || c == '-')) return false;
  }
  return true;
}

// تحويل قيمة Lua إلى نص JSON (مقتطع ليتسع في cap)
static void encodeVal(lua_State* L, int idx, char* out, size_t cap) {
  switch (lua_type(L, idx)) {
    case LUA_TBOOLEAN: snprintf(out, cap, "%s", lua_toboolean(L, idx) ? "true" : "false"); break;
    case LUA_TNUMBER:
      if (lua_isinteger(L, idx)) snprintf(out, cap, "%lld", (long long)lua_tointeger(L, idx));
      else {
        double d = (double)lua_tonumber(L, idx);
        if (d != d || d > 3e38 || d < -3e38) snprintf(out, cap, "null");
        else snprintf(out, cap, "%.6g", d);
      }
      break;
    case LUA_TSTRING: {
      size_t l; const char* str = lua_tolstring(L, idx, &l);
      size_t n = 0; out[n++] = '"';
      bool cut = false; size_t i = 0;
      for (; i < l; i++) {
        unsigned char c = (unsigned char)str[i];
        char esc[2]; int el;
        if (c == '"' || c == '\\') { esc[0] = '\\'; esc[1] = (char)c; el = 2; }
        else if (c < 0x20) { esc[0] = ' '; el = 1; }
        else { esc[0] = (char)c; el = 1; }
        if (n + el > cap - 2) { cut = true; break; }
        for (int k = 0; k < el; k++) out[n++] = esc[k];
      }
      if (cut && (((unsigned char)str[i]) & 0xC0) == 0x80) {   // لا نقطع حرف UTF-8 في المنتصف
        while (n > 1 && (((unsigned char)out[n - 1]) & 0xC0) == 0x80) n--;
        if (n > 1 && (((unsigned char)out[n - 1]) & 0xC0) == 0xC0) n--;
      }
      out[n++] = '"'; out[n] = 0;
      break;
    }
    default: snprintf(out, cap, "null");
  }
}

static void pushMsg(uint8_t panel, const char* name, const char* val) {
  xSemaphoreTake(uiMux, portMAX_DELAY);
  int slot = -1; uint32_t oldest = 0xFFFFFFFF;
  for (int i = 0; i < MAX_MSGS; i++) {
    if (msgs[i].seq && msgs[i].panel == panel && !strcmp(msgs[i].name, name)) msgs[i].seq = 0;   // القيمة الأحدث تحل محل القديمة
  }
  for (int i = 0; i < MAX_MSGS; i++) {
    if (msgs[i].seq == 0) { slot = i; break; }
    if (msgs[i].seq < oldest) { oldest = msgs[i].seq; slot = i; }
  }
  Msg& m = msgs[slot];
  m.seq = ++msgSeq; m.panel = panel;
  strncpy(m.name, name, ID_LEN - 1); m.name[ID_LEN - 1] = 0;
  strncpy(m.val, val, VAL_LEN - 1); m.val[VAL_LEN - 1] = 0;
  xSemaphoreGive(uiMux);
}

static void uiDrop(int slot) {
  xSemaphoreTake(uiMux, portMAX_DELAY);
  for (int i = 0; i < MAX_PANELS; i++) if (panels[i].used && panels[i].owner == slot) {
    panels[i].used = false;
    for (auto& w : widgets) if (w.used && w.panel == i) w.used = false;
    for (auto& m : msgs) if (m.seq && m.panel == i) m.seq = 0;
  }
  uiVer++;
  xSemaphoreGive(uiMux);
}

static int ownedPanel(lua_State* L, int arg) {
  int i = (int)luaL_checkinteger(L, arg);
  if (i < 0 || i >= MAX_PANELS || !panels[i].used || panels[i].owner != idxOf(me(L))) luaL_error(L, "invalid panel");
  return i;
}

static int l_ui_new(lua_State* L) {
  Script* s = me(L);
  const char* title = luaL_checkstring(L, 1);
  int mine = 0, slot = -1;
  for (int i = 0; i < MAX_PANELS; i++) {
    if (panels[i].used && panels[i].owner == idxOf(s)) mine++;
    if (!panels[i].used && slot < 0) slot = i;
  }
  if (mine >= PANELS_PER_SCRIPT) return luaL_error(L, "too many panels (max %d per script)", PANELS_PER_SCRIPT);
  if (slot < 0) return luaL_error(L, "no free panel (max %d total)", MAX_PANELS);
  xSemaphoreTake(uiMux, portMAX_DELAY);
  panels[slot].used = true; panels[slot].owner = idxOf(s);
  strncpy(panels[slot].script, s->name, NAME_LEN - 1); panels[slot].script[NAME_LEN - 1] = 0;
  strncpy(panels[slot].title, title, sizeof(panels[slot].title) - 1); panels[slot].title[sizeof(panels[slot].title) - 1] = 0;
  panels[slot].page[0] = 0;
  uiVer++;
  xSemaphoreGive(uiMux);
  lua_pushinteger(L, slot);
  return 1;
}

// ui._w(panel, type, id, label, min, max, step, value)
static int l_ui_w(lua_State* L) {
  int pi = ownedPanel(L, 1);
  const char* ty = luaL_checkstring(L, 2);
  const char* id = luaL_checkstring(L, 3);
  const char* label = luaL_optstring(L, 4, "");
  float mn = (float)luaL_optnumber(L, 5, 0), mx = (float)luaL_optnumber(L, 6, 100);
  int type = -1;
  for (int i = 0; i < W_COUNT; i++) if (!strcmp(ty, WTYPES[i])) type = i;
  if (type < 0) return luaL_error(L, "unknown widget type");
  if (!safeName(id, ID_LEN)) return luaL_error(L, "bad widget id (letters, digits, _ -; max %d)", ID_LEN - 1);
  char val[VAL_LEN]; encodeVal(L, 8, val, sizeof(val));
  int slot = -1, freeSlot = -1;
  for (int i = 0; i < MAX_WIDGETS; i++) {
    if (widgets[i].used && widgets[i].panel == pi && !strcmp(widgets[i].id, id)) slot = i;
    if (!widgets[i].used && freeSlot < 0) freeSlot = i;
  }
  if (slot < 0) slot = freeSlot;
  if (slot < 0) return luaL_error(L, "too many widgets (max %d)", MAX_WIDGETS);
  xSemaphoreTake(uiMux, portMAX_DELAY);
  Widget& w = widgets[slot];
  w.used = true; w.panel = pi; w.type = type; w.mn = mn; w.mx = mx;
  strncpy(w.id, id, ID_LEN - 1); w.id[ID_LEN - 1] = 0;
  strncpy(w.label, label, sizeof(w.label) - 1); w.label[sizeof(w.label) - 1] = 0;
  strcpy(w.val, val);
  uiVer++;
  xSemaphoreGive(uiMux);
  return 0;
}

// ui._set(panel, id, value): يحدّث أداة (إن وُجدت) ويبثّ القيمة للواجهة
static int l_ui_set(lua_State* L) {
  int pi = ownedPanel(L, 1);
  const char* id = luaL_checkstring(L, 2);
  if (!safeName(id, ID_LEN)) return luaL_error(L, "bad id");
  char val[VAL_LEN]; encodeVal(L, 3, val, sizeof(val));
  xSemaphoreTake(uiMux, portMAX_DELAY);
  for (auto& w : widgets) if (w.used && w.panel == pi && !strcmp(w.id, id)) strcpy(w.val, val);
  xSemaphoreGive(uiMux);
  pushMsg(pi, id, val);
  return 0;
}

static int l_ui_emit(lua_State* L) {
  int pi = ownedPanel(L, 1);
  const char* n = luaL_checkstring(L, 2);
  if (!safeName(n, ID_LEN)) return luaL_error(L, "bad event name (letters, digits, _ -; max %d)", ID_LEN - 1);
  char val[VAL_LEN]; encodeVal(L, 3, val, sizeof(val));
  pushMsg(pi, n, val);
  return 0;
}

// ui._page(panel, path): الملف يجب أن يكون تحت /ui/ وبامتداد .html
static int l_ui_page(lua_State* L) {
  int pi = ownedPanel(L, 1);
  const char* path = luaL_checkstring(L, 2);
  if (path[0] == '/') path++;
  size_t l = strlen(path);
  bool ok = l > 8 && l < 38 && !strncmp(path, "ui/", 3) && (!strcmp(path + l - 5, ".html"));
  for (size_t i = 0; ok && i < l; i++) {
    char c = path[i];
    if (!(isalnum((unsigned char)c) || c == '_' || c == '-' || c == '.' || c == '/')) ok = false;
  }
  if (ok && strstr(path, "..")) ok = false;
  if (!ok) return luaL_error(L, "page must be ui/<name>.html");
  xSemaphoreTake(uiMux, portMAX_DELAY);
  snprintf(panels[pi].page, sizeof(panels[pi].page), "/%s", path);
  uiVer++;
  xSemaphoreGive(uiMux);
  return 0;
}

// طبقة Lua فوق الدوال الأساسية: كائن اللوحة والأدوات والأحداث
static const char UI_PRELUDE[] = R"LUA(
local U = {}
U.__index = U
local H = {}
local f_new, f_w, f_set, f_page, f_emit = ui._new, ui._w, ui._set, ui._page, ui._emit
ui._new, ui._w, ui._set, ui._page, ui._emit = nil, nil, nil, nil, nil
function ui.panel(title)
  local i = f_new(tostring(title or "panel"))
  H[i] = {}
  return setmetatable({ i = i }, U)
end
function U:page(path) f_page(self.i, path) return self end
function U:emit(n, v) f_emit(self.i, n, v) return self end
function U:on(n, fn) H[self.i][n] = fn return self end
function U:set(id, v) f_set(self.i, id, v) return self end
function U:label(id, text) f_w(self.i, "label", id, "", 0, 0, 0, text or "") return self end
function U:button(id, text, fn) f_w(self.i, "button", id, text or id, 0, 0, 0, nil) if fn then self:on(id, fn) end return self end
function U:switch(id, text, fn, init) f_w(self.i, "switch", id, text or id, 0, 0, 0, init and true or false) if fn then self:on(id, fn) end return self end
function U:slider(id, text, min, max, fn, init) f_w(self.i, "slider", id, text or id, min or 0, max or 100, 0, init or min or 0) if fn then self:on(id, fn) end return self end
function U:progress(id, text, v) f_w(self.i, "progress", id, text or id, 0, 100, 0, v or 0) return self end
function U:input(id, text, fn, init) f_w(self.i, "input", id, text or id, 0, 0, 0, init or "") if fn then self:on(id, fn) end return self end
function __ui_dispatch(i, n, v)
  local h = H[i] and H[i][n]
  if h then h(v) end
end
)LUA";

static void pushVariant(lua_State* L, JsonVariantConst v, int depth) {
  if (v.is<bool>()) lua_pushboolean(L, v.as<bool>());
  else if (v.is<long long>()) lua_pushinteger(L, (lua_Integer)v.as<long long>());
  else if (v.is<float>()) lua_pushnumber(L, v.as<float>());
  else if (v.is<const char*>()) lua_pushstring(L, v.as<const char*>());
  else if (depth < 3 && v.is<JsonObjectConst>()) {
    lua_newtable(L);
    for (JsonPairConst kv : v.as<JsonObjectConst>()) {
      pushVariant(L, kv.value(), depth + 1);
      lua_setfield(L, -2, kv.key().c_str());
    }
  } else if (depth < 3 && v.is<JsonArrayConst>()) {
    lua_newtable(L);
    int i = 1;
    for (JsonVariantConst e : v.as<JsonArrayConst>()) { pushVariant(L, e, depth + 1); lua_rawseti(L, -2, i++); }
  } else lua_pushnil(L);
}

static void pushJsonValue(lua_State* L, const char* json) {
  JsonDocument d;
  if (deserializeJson(d, json)) { lua_pushnil(L); return; }
  pushVariant(L, d.as<JsonVariantConst>(), 0);
}

// تُعرَّف لاحقًا (storage و bus)
static void kvRelease(Script* s);
static void kvFlushIfDue(Script* s, uint32_t now);
static int l_storage_get(lua_State* L);
static int l_storage_set(lua_State* L);
static int l_storage_remove(lua_State* L);
static int l_storage_clear(lua_State* L);
static int l_bus_publish(lua_State* L);
static int l_bus_subscribe(lua_State* L);
static int l_bus_unsubscribe(lua_State* L);
static int l_require(lua_State* L);
static int l_i2c_begin(lua_State* L);
static int l_i2c_stop(lua_State* L);
static int l_i2c_scan(lua_State* L);
static int l_i2c_write(lua_State* L);
static int l_i2c_read(lua_State* L);
static int l_i2c_write_read(lua_State* L);
static int l_i2c_write_reg(lua_State* L);
static int l_i2c_read_reg(lua_State* L);
static void i2cRelease(Script* s);
static void spiRelease(Script* s);
static int l_spi_begin(lua_State* L);
static int l_spi_stop(lua_State* L);
static int l_drv_open(lua_State* L);
static int l_drv_list(lua_State* L);
static void drvRegister(lua_State* L);

// ---------- دورة حياة السكريبت ----------
static void setStatus(Script* s, uint8_t st, const char* err) {
  xSemaphoreTake(statusMux, portMAX_DELAY);
  s->state = st;
  if (err) { strncpy(s->err, err, sizeof(s->err) - 1); s->err[sizeof(s->err) - 1] = 0; }
  xSemaphoreGive(statusMux);
}

static void releaseResources(Script* s) {
  int id = idxOf(s) + 1;
  if (s->L) { lua_close(s->L); s->L = nullptr; }   // أولًا: تُغلق مقابض الدرايفرات (__gc) قبل تحرير ناقل I2C
  s->drvOpen = 0;
  for (int p = 0; p < NUM_PINS; p++) if (pinOwner[p] == id) {
    for (int i = 0; i < PWM_MAX; i++) if (pwmPins[i] == p + 1) { ledcDetach(p); pwmPins[i] = 0; }
    pinMode(p, INPUT);
    pinOwner[p] = 0;
  }
  kvRelease(s);
  spiRelease(s);
  i2cRelease(s);
  uiDrop(idxOf(s));
  s->mem = 0;
  memset(s->timers, 0, sizeof(s->timers));
  memset(s->watches, 0, sizeof(s->watches));
  memset(s->subs, 0, sizeof(s->subs));
}

static void failScript(Script* s, const char* msg) {
  logPrintf("[%s] ERROR: %s\n", s->name, msg);
  releaseResources(s);
  setStatus(s, ST_ERR, msg);
}

static bool protectedCall(Script* s, int nargs);

static void killScript(Script* s) {
  // on_stop(): فرصة لإعادة الأرجل لحالة آمنة قبل الإغلاق (عند الإيقاف أو الاستبدال، لا عند الخطأ)
  if (s->state == ST_RUN && s->L) {
    lua_getglobal(s->L, "on_stop");
    if (lua_isfunction(s->L, -1)) protectedCall(s, 0); else lua_pop(s->L, 1);
  }
  releaseResources(s);
  xSemaphoreTake(statusMux, portMAX_DELAY);
  s->state = ST_FREE; s->name[0] = 0; s->err[0] = 0;
  xSemaphoreGive(statusMux);
}

// استدعاء Lua محمي مع ميزانية زمنية
static bool protectedCall(Script* s, int nargs) {
  lua_State* L = s->L;
  s->deadline = millis() + CALL_BUDGET;
  if (lua_pcall(L, nargs, 0, 0) != LUA_OK) {
    const char* m = lua_tostring(L, -1);
    failScript(s, m ? m : "error");
    return false;
  }
  return true;
}

static void startScript(const char* name, const char* code, size_t len) {
  Script* s = findByName(name);
  if (s) killScript(s);
  else for (auto& x : scripts) if (x.state == ST_FREE) { s = &x; break; }
  if (!s) { logPrintf("[%s] ERROR: max %d scripts\n", name, MAX_SCRIPTS); return; }
  xSemaphoreTake(statusMux, portMAX_DELAY);
  memset(s, 0, sizeof(*s));
  strncpy(s->name, name, NAME_LEN - 1);
  s->limit = MEM_LIMIT;
  s->startMs = millis();
  s->state = ST_RUN;
  xSemaphoreGive(statusMux);
  s->L = lua_newstate(limAlloc, s);
  if (!s->L) { failScript(s, "out of memory"); return; }
  lua_State* L = s->L;
  luaL_requiref(L, "_G", luaopen_base, 1); lua_pop(L, 1);
  luaL_requiref(L, "table", luaopen_table, 1); lua_pop(L, 1);
  luaL_requiref(L, "string", luaopen_string, 1); lua_pop(L, 1);
  luaL_requiref(L, "math", luaopen_math, 1); lua_pop(L, 1);
  luaL_requiref(L, "utf8", luaopen_utf8, 1); lua_pop(L, 1);
  luaL_requiref(L, "coroutine", luaopen_coroutine, 1); lua_pop(L, 1);
  // عزل: إزالة ما يقرأ ملفات أو يحمّل شيفرة ثنائية
  for (const char* g : { "dofile", "loadfile", "load", "loadstring", "require" }) { lua_pushnil(L); lua_setglobal(L, g); }
  lua_getglobal(L, "string"); lua_pushnil(L); lua_setfield(L, -2, "dump"); lua_pop(L, 1);
  lua_pushcfunction(L, l_print); lua_setglobal(L, "print");
  lua_pushcfunction(L, l_print); lua_setglobal(L, "log");

  static const luaL_Reg gpioF[] = { {"mode", l_gpio_mode}, {"write", l_gpio_write}, {"read", l_gpio_read}, {"toggle", l_gpio_toggle}, {"on_change", l_gpio_on_change}, {"off_change", l_gpio_off_change}, {NULL, NULL} };
  static const luaL_Reg adcF[]  = { {"read", l_adc_read}, {"mv", l_adc_mv}, {"read_mv", l_adc_mv}, {"atten", l_adc_atten}, {NULL, NULL} };
  static const luaL_Reg pwmF[]  = { {"set", l_pwm_set}, {"duty", l_pwm_duty}, {"stop", l_pwm_stop}, {NULL, NULL} };
  static const luaL_Reg timF[]  = { {"every", l_timer_every}, {"after", l_timer_after}, {"cancel", l_timer_cancel}, {NULL, NULL} };
  static const luaL_Reg sysF[]  = { {"millis", l_sys_millis}, {"heap", l_sys_heap}, {"temp", l_sys_temp}, {"deep_sleep", l_sys_deep_sleep}, {"wake_reason", l_sys_wake_reason}, {"uptime", l_sys_uptime}, {"version", l_sys_version}, {NULL, NULL} };
  static const luaL_Reg uiF[]   = { {"_new", l_ui_new}, {"_w", l_ui_w}, {"_set", l_ui_set}, {"_page", l_ui_page}, {"_emit", l_ui_emit}, {NULL, NULL} };
  static const luaL_Reg stoF[]  = { {"get", l_storage_get}, {"set", l_storage_set}, {"remove", l_storage_remove}, {"clear", l_storage_clear}, {NULL, NULL} };
  static const luaL_Reg busF[]  = { {"publish", l_bus_publish}, {"subscribe", l_bus_subscribe}, {"unsubscribe", l_bus_unsubscribe}, {NULL, NULL} };
  static const luaL_Reg i2cF[]  = { {"begin", l_i2c_begin}, {"stop", l_i2c_stop}, {"scan", l_i2c_scan}, {"write", l_i2c_write}, {"read", l_i2c_read},
                                    {"write_read", l_i2c_write_read}, {"write_reg", l_i2c_write_reg}, {"read_reg", l_i2c_read_reg}, {NULL, NULL} };
  static const luaL_Reg drvF[] = { {"open", l_drv_open}, {"list", l_drv_list}, {NULL, NULL} };
  static const luaL_Reg spiF[] = { {"begin", l_spi_begin}, {"stop", l_spi_stop}, {NULL, NULL} };
  reg(L, "spi", spiF);
  reg(L, "drv", drvF); drvRegister(L);
  reg(L, "storage", stoF); reg(L, "bus", busF); reg(L, "i2c", i2cF);
  lua_pushcfunction(L, l_require); lua_setglobal(L, "require");
  reg(L, "gpio", gpioF); reg(L, "adc", adcF); reg(L, "pwm", pwmF); reg(L, "timer", timF); reg(L, "sys", sysF); reg(L, "ui", uiF);
  if (luaL_loadbufferx(L, UI_PRELUDE, sizeof(UI_PRELUDE) - 1, "=ui", "t") != LUA_OK || !protectedCall(s, 0)) {
    if (s->state == ST_RUN) { const char* m = lua_tostring(L, -1); failScript(s, m ? m : "ui init failed"); }
    return;
  }

  lua_sethook(L, hook, LUA_MASKCOUNT, 5000);
  s->deadline = millis() + CALL_BUDGET;
  if (luaL_loadbufferx(L, code, len, name, "t") != LUA_OK) {
    const char* m = lua_tostring(L, -1);
    failScript(s, m ? m : "syntax error");
    return;
  }
  if (protectedCall(s, 0)) logPrintf("[%s] started (%u bytes RAM)\n", name, (unsigned)s->mem);
}

static void tickScript(Script* s) {
  uint32_t now = millis();
  kvFlushIfDue(s, now);
  for (int i = 0; i < MAX_WATCH && s->state == ST_RUN; i++) {
    LWatch& w = s->watches[i];
    if (!w.used) continue;
    uint8_t v = (uint8_t)digitalRead(w.pin);
    if (v == w.stable) { w.pending = false; continue; }
    if (!w.pending || w.pendVal != v) { w.pending = true; w.pendVal = v; w.changedAt = now; }
    if ((uint16_t)(now - w.changedAt) >= w.debounce || (now - w.changedAt) > 60000) {
      w.stable = v; w.pending = false;
      lua_rawgeti(s->L, LUA_REGISTRYINDEX, w.ref);
      lua_pushinteger(s->L, v);
      if (!protectedCall(s, 1)) return;
    }
  }
  for (int i = 0; i < MAX_TIMERS && s->state == ST_RUN; i++) {
    LTimer& t = s->timers[i];
    if (!t.used || (int32_t)(now - t.next) < 0) continue;
    lua_rawgeti(s->L, LUA_REGISTRYINDEX, t.ref);
    if (t.repeat) t.next = now + t.period;
    else freeTimer(s->L, t);   // المرجع يبقى على المكدس
    if (!protectedCall(s, 0)) return;
  }
}


// ---------- حفظ السكريبتات والتشغيل التلقائي ----------
#define SCRIPTS_DIR "/scripts"
#define AUTO_FILE   "/scripts/autorun.txt"
#define BOOT_FILE   "/scripts/.boot"

static uint32_t bootClearAt = 0;

static String readText(const String& p) {
  File f = LittleFS.open(p, "r");
  if (!f || f.isDirectory()) return String("");
  String s = f.readString();
  f.close();
  return s;
}
static bool writeText(const String& p, const String& s) {
  File f = LittleFS.open(p, "w");
  if (!f) return false;
  size_t w = f.print(s);
  f.close();
  return w == s.length();
}
static void ensureDir() { if (!LittleFS.exists(SCRIPTS_DIR)) LittleFS.mkdir(SCRIPTS_DIR); }
static bool autoHas(const String& list, const char* name) {
  int i = 0, n = (int)list.length();
  while (i < n) {
    int j = list.indexOf('\n', i);
    if (j < 0) j = n;
    String l = list.substring(i, j);
    l.trim();
    if (l == name) return true;
    i = j + 1;
  }
  return false;
}
static char* loadFile(const char* name, size_t* len) {
  String p = String(SCRIPTS_DIR) + "/" + name + ".lua";
  File f = LittleFS.open(p, "r");
  if (!f || f.isDirectory() || f.size() == 0 || f.size() > MAX_CODE) { if (f) f.close(); return nullptr; }
  size_t n = f.size();
  char* buf = (char*)malloc(n + 1);
  if (!buf) { f.close(); return nullptr; }
  size_t r = f.read((uint8_t*)buf, n);
  f.close();
  if (r != n) { free(buf); return nullptr; }
  buf[r] = 0;
  *len = r;
  return buf;
}

// عند الإقلاع: يشغّل السكريبتات المعلَّمة تلقائيًا. 3 إقلاعات متتالية خلال 30 ثانية = حلقة انهيار → تُعطَّل كل العلامات
static void autorunBoot() {
  String list = readText(AUTO_FILE);
  list.trim();
  if (!list.length()) return;
  int n = readText(BOOT_FILE).toInt();
  if (n >= 3) {
    logPrintf("autorun disabled: %d quick reboots in a row\n", n);
    writeText(AUTO_FILE, "");
    writeText(BOOT_FILE, "0");
    return;
  }
  writeText(BOOT_FILE, String(n + 1));
  bootClearAt = millis() + 30000;
  int started = 0, i = 0, len = (int)list.length();
  while (i < len && started < MAX_SCRIPTS) {
    int j = list.indexOf('\n', i);
    if (j < 0) j = len;
    String name = list.substring(i, j);
    i = j + 1;
    name.trim();
    if (!luaeng_valid_name(name.c_str())) continue;
    size_t l = 0;
    char* buf = loadFile(name.c_str(), &l);
    if (!buf) { logPrintf("autorun: cannot read %s\n", name.c_str()); continue; }
    logPrintf("autorun %s\n", name.c_str());
    startScript(name.c_str(), buf, l);
    free(buf);
    started++;
  }
}


// ---------- storage: مخزن قيم دائم لكل سكريبت ----------
#define KV_DIR   "/scripts/.data"
#define KV_LIMIT 2048

static String kvPath(const char* name) { return String(KV_DIR) + "/" + name + ".json"; }

static JsonDocument* kvGet(Script* s) {
  if (!s->kv) {
    s->kv = new JsonDocument();
    String txt = readText(kvPath(s->name));
    if (txt.length()) deserializeJson(*s->kv, txt);       // ملف تالف = مخزن فارغ
    if (!s->kv->is<JsonObject>()) s->kv->to<JsonObject>();
  }
  return s->kv;
}
static void kvFlush(Script* s) {
  if (!s->kv || !s->kvDirty) return;
  s->kvDirty = false;
  ensureDir();
  if (!LittleFS.exists(KV_DIR)) LittleFS.mkdir(KV_DIR);
  String out; serializeJson(*s->kv, out);
  writeText(kvPath(s->name), out);
}
static void kvRelease(Script* s) {
  kvFlush(s);
  delete s->kv;
  s->kv = nullptr;
}
// الكتابة على الفلاش تُدمج: خلال 3 ثوانٍ من أول تعديل
static void kvFlushIfDue(Script* s, uint32_t now) {
  if (s->kvDirty && (int32_t)(now - s->kvSaveAt) >= 0) kvFlush(s);
}
static void kvTouch(Script* s) {
  if (!s->kvDirty) { s->kvDirty = true; s->kvSaveAt = millis() + 3000; }
}

static int l_storage_get(lua_State* L) {
  Script* s = me(L);
  const char* key = luaL_checkstring(L, 1);
  JsonObjectConst o = kvGet(s)->as<JsonObjectConst>();
  JsonVariantConst v = o[key];
  if (v.isNull()) { if (lua_gettop(L) >= 2) lua_pushvalue(L, 2); else lua_pushnil(L); }
  else pushVariant(L, v, 0);
  return 1;
}
static int kvRemove(lua_State* L, Script* s, const char* key) {
  kvGet(s)->remove(String(key));
  kvTouch(s);
  return 0;
}
static int l_storage_set(lua_State* L) {
  Script* s = me(L);
  const char* key = luaL_checkstring(L, 1);
  if (!safeName(key, 16)) return luaL_error(L, "bad key (letters, digits, _ -; max 15)");
  int t = lua_type(L, 2);
  if (t == LUA_TNIL || t == LUA_TNONE) return kvRemove(L, s, key);
  if (t != LUA_TBOOLEAN && t != LUA_TNUMBER && t != LUA_TSTRING) return luaL_error(L, "storage value must be number|string|boolean");
  JsonDocument* kv = kvGet(s);
  size_t vlen = 24;
  if (t == LUA_TSTRING) {
    size_t l; const char* str = lua_tolstring(L, 2, &l);
    if (l > 200) return luaL_error(L, "string too long (max 200)");
    vlen = l + 2;
    for (size_t i = 0; i < l; i++) if (str[i] == '"' || str[i] == '\\' || (unsigned char)str[i] < 0x20) vlen += 1;
  }
  JsonObjectConst o = kv->as<JsonObjectConst>();
  size_t cur = measureJson(*kv);
  JsonVariantConst ev = o[key];
  size_t oldLen = ev.isNull() ? 0 : measureJson(ev) + strlen(key) + 4;
  size_t addLen = vlen + strlen(key) + 4;
  if (cur - oldLen + addLen > KV_LIMIT) return luaL_error(L, "storage full (max %d bytes)", KV_LIMIT);
  String k(key);
  if (t == LUA_TBOOLEAN) (*kv)[k] = (bool)lua_toboolean(L, 2);
  else if (t == LUA_TSTRING) (*kv)[k] = String(lua_tostring(L, 2));
  else if (lua_isinteger(L, 2)) (*kv)[k] = (long long)lua_tointeger(L, 2);
  else (*kv)[k] = (float)lua_tonumber(L, 2);
  kvTouch(s);
  return 0;
}
static int l_storage_remove(lua_State* L) {
  return kvRemove(L, me(L), luaL_checkstring(L, 1));
}
static int l_storage_clear(lua_State* L) {
  Script* s = me(L);
  kvGet(s)->to<JsonObject>();
  kvTouch(s);
  return 0;
}

// ---------- bus: نشر/اشتراك بين السكريبتات ----------
#define BUS_Q    16
#define BUS_JSON 120
struct BusEv { uint8_t src; char topic[16]; char json[BUS_JSON]; };
static BusEv busq[BUS_Q];
static int busHead = 0, busCount = 0;
static portMUX_TYPE busMux = portMUX_INITIALIZER_UNLOCKED;

static bool busPush(uint8_t src, const char* topic, const char* json) {
  bool ok = false;
  portENTER_CRITICAL(&busMux);
  if (busCount < BUS_Q) {
    BusEv& e = busq[(busHead + busCount) % BUS_Q];
    e.src = src;
    strncpy(e.topic, topic, sizeof(e.topic) - 1); e.topic[sizeof(e.topic) - 1] = 0;
    strncpy(e.json, json, sizeof(e.json) - 1); e.json[sizeof(e.json) - 1] = 0;
    busCount++;
    ok = true;
  }
  portEXIT_CRITICAL(&busMux);
  return ok;
}

// قيمة Lua → JSON: رقم/نص/منطقي، أو جدول بسيط (مصفوفة أو كائن بحقول نصية وقيم بسيطة، حتى 12 عنصرًا)
static bool encodeBus(lua_State* L, int idx, char* out, size_t cap) {
  idx = lua_absindex(L, idx);
  int t = lua_type(L, idx);
  if (t != LUA_TTABLE) {
    if (t == LUA_TFUNCTION || t == LUA_TUSERDATA || t == LUA_TTHREAD || t == LUA_TLIGHTUSERDATA) return false;
    encodeVal(L, idx, out, cap);
    return strlen(out) + 1 < cap;
  }
  int cnt = 0; bool arr = true;
  lua_pushnil(L);
  while (lua_next(L, idx)) {
    cnt++;
    if (!(lua_isinteger(L, -2) && lua_tointeger(L, -2) == cnt)) arr = false;
    lua_pop(L, 1);
    if (cnt > 12) { lua_pop(L, 1); return false; }
  }
  size_t n = 0;
  out[n++] = arr ? '[' : '{';
  out[n] = 0;
  int i = 0;
  lua_pushnil(L);
  while (lua_next(L, idx)) {
    char vb[64];
    int vt = lua_type(L, -1);
    bool bad = (vt == LUA_TTABLE || vt == LUA_TFUNCTION || vt == LUA_TUSERDATA || vt == LUA_TTHREAD || vt == LUA_TLIGHTUSERDATA);
    if (!arr && (lua_type(L, -2) != LUA_TSTRING || !safeName(lua_tostring(L, -2), 16))) bad = true;
    if (bad) { lua_pop(L, 2); return false; }
    encodeVal(L, -1, vb, sizeof(vb));
    int w = arr ? snprintf(out + n, cap - n, "%s%s", i ? "," : "", vb)
                : snprintf(out + n, cap - n, "%s\"%s\":%s", i ? "," : "", lua_tostring(L, -2), vb);
    if (w < 0 || n + w + 2 >= cap) { lua_pop(L, 2); return false; }
    n += w; i++;
    lua_pop(L, 1);
  }
  out[n++] = arr ? ']' : '}';
  out[n] = 0;
  return true;
}

static int l_bus_publish(lua_State* L) {
  Script* s = me(L);
  const char* topic = luaL_checkstring(L, 1);
  if (!safeName(topic, 16)) return luaL_error(L, "bad topic (letters, digits, _ -; max 15)");
  char json[BUS_JSON];
  if (!encodeBus(L, 2, json, sizeof(json))) return luaL_error(L, "bus value must be number|string|boolean|simple table (max %d bytes)", BUS_JSON - 1);
  if (!busPush(idxOf(s), topic, json)) logPrintf("[%s] bus queue full, dropped '%s'\n", s->name, topic);
  return 0;
}
static int l_bus_subscribe(lua_State* L) {
  Script* s = me(L);
  const char* topic = luaL_checkstring(L, 1);
  luaL_checktype(L, 2, LUA_TFUNCTION);
  if (!safeName(topic, 16)) return luaL_error(L, "bad topic");
  int slot = -1;
  for (int i = 0; i < MAX_SUBS; i++) if (s->subs[i].used && !strcmp(s->subs[i].topic, topic)) slot = i;
  if (slot < 0) for (int i = 0; i < MAX_SUBS; i++) if (!s->subs[i].used) { slot = i; break; }
  if (slot < 0) return luaL_error(L, "too many subscriptions (max %d)", MAX_SUBS);
  if (s->subs[slot].used) luaL_unref(L, LUA_REGISTRYINDEX, s->subs[slot].ref);
  lua_pushvalue(L, 2);
  s->subs[slot].used = true;
  strncpy(s->subs[slot].topic, topic, sizeof(s->subs[slot].topic) - 1);
  s->subs[slot].ref = luaL_ref(L, LUA_REGISTRYINDEX);
  return 0;
}
static int l_bus_unsubscribe(lua_State* L) {
  Script* s = me(L);
  const char* topic = luaL_checkstring(L, 1);
  for (auto& sub : s->subs) if (sub.used && !strcmp(sub.topic, topic)) { luaL_unref(L, LUA_REGISTRYINDEX, sub.ref); sub.used = false; }
  return 0;
}

// يوزّع ما نُشر قبل هذه اللحظة فقط (لا حلقة لا نهائية بين سكريبتين يتبادلان الردود)
static void busDrain() {
  int n;
  portENTER_CRITICAL(&busMux);
  n = busCount;
  portEXIT_CRITICAL(&busMux);
  while (n-- > 0) {
    BusEv e;
    portENTER_CRITICAL(&busMux);
    if (!busCount) { portEXIT_CRITICAL(&busMux); return; }
    e = busq[busHead];
    busHead = (busHead + 1) % BUS_Q;
    busCount--;
    portEXIT_CRITICAL(&busMux);
    JsonDocument d;
    bool ok = !deserializeJson(d, e.json);
    for (auto& s : scripts) {
      if (s.state != ST_RUN || !s.L || idxOf(&s) == e.src) continue;
      for (int i = 0; i < MAX_SUBS && s.state == ST_RUN; i++) {
        if (!s.subs[i].used || strcmp(s.subs[i].topic, e.topic)) continue;
        lua_checkstack(s.L, 8);
        lua_rawgeti(s.L, LUA_REGISTRYINDEX, s.subs[i].ref);
        if (ok) pushVariant(s.L, d.as<JsonVariantConst>(), 0); else lua_pushnil(s.L);
        protectedCall(&s, 1);
      }
    }
  }
}


// ---------- I2C (ناقل مشترك بين السكريبتات؛ المهمة الوحيدة التي تلمسه هي مهمة المحرك) ----------
#define I2C_MAX 128
static bool i2cOn = false;
static int i2cSda = -1, i2cScl = -1;
static uint32_t i2cFreq = 0;

// عمليات الناقل المحجوبة لا تُحسب من ميزانية الـ200ms
struct IoTimer { Script* s; uint32_t t0; IoTimer(Script* sc) : s(sc), t0(millis()) {} ~IoTimer() { s->deadline += millis() - t0; } };

static void i2cRelease(Script* s) {
  if (!s->usesI2c) return;
  s->usesI2c = false;
  for (auto& x : scripts) if (x.usesI2c) return;      // ما زال هناك مستعمل آخر
  if (i2cOn) {
    Wire.end();
    if (i2cSda >= 0) { pinOwner[i2cSda] = 0; pinMode(i2cSda, INPUT); }
    if (i2cScl >= 0) { pinOwner[i2cScl] = 0; pinMode(i2cScl, INPUT); }
    i2cOn = false; i2cSda = i2cScl = -1;
  }
}

static void needI2c(lua_State* L) {
  if (!i2cOn || !me(L)->usesI2c) luaL_error(L, "call i2c.begin(sda, scl) first");
}
static int checkAddr(lua_State* L, int arg) {
  lua_Integer a = luaL_checkinteger(L, arg);
  if (a < 1 || a > 126) luaL_error(L, "I2C address 1..126");
  return (int)a;
}
// بايتات من جدول أرقام (0..255) أو من نص
static size_t getBytes(lua_State* L, int idx, uint8_t* buf, size_t max) {
  idx = lua_absindex(L, idx);
  if (lua_type(L, idx) == LUA_TSTRING) {
    size_t l; const char* p = lua_tolstring(L, idx, &l);
    if (l > max) luaL_error(L, "too many bytes (max %d)", (int)max);
    memcpy(buf, p, l);
    return l;
  }
  luaL_checktype(L, idx, LUA_TTABLE);
  size_t n = (size_t)luaL_len(L, idx);
  if (n > max) luaL_error(L, "too many bytes (max %d)", (int)max);
  for (size_t i = 0; i < n; i++) {
    lua_rawgeti(L, idx, (lua_Integer)i + 1);
    if (!lua_isinteger(L, -1)) luaL_error(L, "bytes must be integers 0..255");
    lua_Integer v = lua_tointeger(L, -1);
    if (v < 0 || v > 255) luaL_error(L, "bytes must be integers 0..255");
    buf[i] = (uint8_t)v;
    lua_pop(L, 1);
  }
  return n;
}
static void pushBytes(lua_State* L, const uint8_t* b, size_t n) {
  lua_createtable(L, (int)n, 0);
  for (size_t i = 0; i < n; i++) { lua_pushinteger(L, b[i]); lua_rawseti(L, -2, (lua_Integer)i + 1); }
}
static const char* i2cErr(int e) {
  switch (e) { case 2: return "nack_addr"; case 3: return "nack_data"; case 5: return "timeout"; case 7: return "i2c_not_started"; case 8: return "short_read"; default: return "error"; }
}
static int failResult(lua_State* L, const char* why) { lua_pushnil(L); lua_pushstring(L, why); return 2; }

static int l_i2c_begin(lua_State* L) {
  Script* s = me(L);
  int sda = (int)luaL_checkinteger(L, 1), scl = (int)luaL_checkinteger(L, 2);
  lua_Integer f = luaL_optinteger(L, 3, 100000);
  if (f < 10000 || f > 1000000) return luaL_error(L, "freq 10000..1000000");
  if (i2cOn) {
    if (sda != i2cSda || scl != i2cScl) return luaL_error(L, "I2C already on pins %d,%d", i2cSda, i2cScl);
    s->usesI2c = true;
    if ((uint32_t)f != i2cFreq) { Wire.setClock((uint32_t)f); i2cFreq = (uint32_t)f; }
    return 0;
  }
  if (sda == scl) return luaL_error(L, "sda and scl must differ");
  if (!pinAllowed(sda) || !pinAllowed(scl)) return luaL_error(L, "pin not allowed");
  int mine = idxOf(s) + 1;
  if ((pinOwner[sda] && pinOwner[sda] != mine) || (pinOwner[scl] && pinOwner[scl] != mine)) return luaL_error(L, "I2C pin already owned by another script");
  if (!Wire.begin(sda, scl, (uint32_t)f)) return luaL_error(L, "I2C init failed");
  Wire.setTimeOut(30);
  pinOwner[sda] = pinOwner[scl] = PIN_I2C;
  i2cOn = true; i2cSda = sda; i2cScl = scl; i2cFreq = (uint32_t)f;
  s->usesI2c = true;
  return 0;
}
static int l_i2c_stop(lua_State* L) { i2cRelease(me(L)); return 0; }

static int l_i2c_scan(lua_State* L) {
  needI2c(L);
  IoTimer t(me(L));
  lua_newtable(L);
  int n = 0;
  for (int a = 8; a < 120; a++) {
    Wire.beginTransmission((uint8_t)a);
    if (Wire.endTransmission() == 0) { lua_pushinteger(L, a); lua_rawseti(L, -2, ++n); }
  }
  return 1;
}
static int l_i2c_write(lua_State* L) {
  needI2c(L);
  int addr = checkAddr(L, 1);
  uint8_t buf[I2C_MAX]; size_t n = getBytes(L, 2, buf, I2C_MAX);
  IoTimer t(me(L));
  Wire.beginTransmission((uint8_t)addr);
  Wire.write(buf, n);
  int e = Wire.endTransmission(true);
  if (e == 0) { lua_pushboolean(L, 1); return 1; }
  lua_pushboolean(L, 0); lua_pushstring(L, i2cErr(e));
  return 2;
}
static int doRead(lua_State* L, int addr, size_t n) {
  size_t got = Wire.requestFrom((uint16_t)addr, n, true);
  uint8_t b[I2C_MAX];
  size_t i = 0;
  while (i < got && i < n && Wire.available()) b[i++] = (uint8_t)Wire.read();
  if (i != n) return failResult(L, "short_read");
  pushBytes(L, b, n);
  return 1;
}
static size_t checkCount(lua_State* L, int arg) {
  lua_Integer n = luaL_checkinteger(L, arg);
  if (n < 1 || n > I2C_MAX) luaL_error(L, "count 1..%d", I2C_MAX);
  return (size_t)n;
}
static int l_i2c_read(lua_State* L) {
  needI2c(L);
  int addr = checkAddr(L, 1);
  size_t n = checkCount(L, 2);
  IoTimer t(me(L));
  return doRead(L, addr, n);
}
static int l_i2c_write_read(lua_State* L) {
  needI2c(L);
  int addr = checkAddr(L, 1);
  uint8_t tx[I2C_MAX]; size_t tn = getBytes(L, 2, tx, I2C_MAX);
  size_t n = checkCount(L, 3);
  IoTimer t(me(L));
  Wire.beginTransmission((uint8_t)addr);
  Wire.write(tx, tn);
  int e = Wire.endTransmission(false);                 // إعادة بدء بلا إيقاف
  if (e != 0) return failResult(L, i2cErr(e));
  return doRead(L, addr, n);
}
// i2c.write_reg(addr, reg, value): value رقم أو جدول/نص بايتات
static int l_i2c_write_reg(lua_State* L) {
  needI2c(L);
  int addr = checkAddr(L, 1);
  lua_Integer reg = luaL_checkinteger(L, 2);
  if (reg < 0 || reg > 255) return luaL_error(L, "reg 0..255");
  uint8_t buf[I2C_MAX]; size_t n = 0;
  buf[n++] = (uint8_t)reg;
  if (lua_isinteger(L, 3)) {
    lua_Integer v = lua_tointeger(L, 3);
    if (v < 0 || v > 255) return luaL_error(L, "value 0..255");
    buf[n++] = (uint8_t)v;
  } else n += getBytes(L, 3, buf + 1, I2C_MAX - 1);
  IoTimer t(me(L));
  Wire.beginTransmission((uint8_t)addr);
  Wire.write(buf, n);
  int e = Wire.endTransmission(true);
  if (e == 0) { lua_pushboolean(L, 1); return 1; }
  lua_pushboolean(L, 0); lua_pushstring(L, i2cErr(e));
  return 2;
}
// i2c.read_reg(addr, reg [, n]): بلا n تعيد رقمًا واحدًا، ومع n جدول بايتات
static int l_i2c_read_reg(lua_State* L) {
  needI2c(L);
  int addr = checkAddr(L, 1);
  lua_Integer reg = luaL_checkinteger(L, 2);
  if (reg < 0 || reg > 255) return luaL_error(L, "reg 0..255");
  bool single = lua_isnoneornil(L, 3);
  size_t n = single ? 1 : checkCount(L, 3);
  IoTimer t(me(L));
  Wire.beginTransmission((uint8_t)addr);
  Wire.write((uint8_t)reg);
  int e = Wire.endTransmission(false);
  if (e != 0) return failResult(L, i2cErr(e));
  int r = doRead(L, addr, n);
  if (r == 1 && single) { lua_rawgeti(L, -1, 1); lua_remove(L, -2); }
  return r;
}


// ---------- SPI (ناقل مشترك؛ يفتحه السكريبت والنقل تتولاه الدرايفرات) ----------
static bool spiOn = false;
static int spiSck = -1, spiMosi = -1, spiMiso = -1;

static void spiRelease(Script* s) {
  if (!s->usesSpi) return;
  s->usesSpi = false;
  for (auto& x : scripts) if (x.usesSpi) return;      // ما زال هناك مستعمل آخر
  if (spiOn) {
    SPI.end();
    if (spiSck >= 0)  { pinOwner[spiSck] = 0;  pinMode(spiSck, INPUT); }
    if (spiMosi >= 0) { pinOwner[spiMosi] = 0; pinMode(spiMosi, INPUT); }
    if (spiMiso >= 0) { pinOwner[spiMiso] = 0; pinMode(spiMiso, INPUT); }
    spiOn = false; spiSck = spiMosi = spiMiso = -1;
  }
}
// spi.begin(sck, mosi [, miso])
static int l_spi_begin(lua_State* L) {
  Script* s = me(L);
  int sck = (int)luaL_checkinteger(L, 1), mosi = (int)luaL_checkinteger(L, 2);
  int miso = lua_isnoneornil(L, 3) ? -1 : (int)luaL_checkinteger(L, 3);
  if (spiOn) {
    if (sck != spiSck || mosi != spiMosi || miso != spiMiso) return luaL_error(L, "SPI already on pins %d,%d,%d", spiSck, spiMosi, spiMiso);
    s->usesSpi = true; return 0;
  }
  int pins[3] = { sck, mosi, miso };
  for (int i = 0; i < 3; i++) {
    if (pins[i] < 0) { if (i == 2) continue; return luaL_error(L, "pin required"); }
    if (!pinAllowed(pins[i])) return luaL_error(L, "pin not allowed");
    for (int j = 0; j < i; j++) if (pins[j] == pins[i]) return luaL_error(L, "SPI pins must differ");
  }
  int mine = idxOf(s) + 1;
  for (int i = 0; i < 3; i++) if (pins[i] >= 0 && pinOwner[pins[i]] && pinOwner[pins[i]] != mine) return luaL_error(L, "SPI pin already owned");
  SPI.begin(sck, miso, mosi, -1);
  for (int i = 0; i < 3; i++) if (pins[i] >= 0) pinOwner[pins[i]] = PIN_SPI;
  spiOn = true; spiSck = sck; spiMosi = mosi; spiMiso = miso;
  s->usesSpi = true;
  return 0;
}
static int l_spi_stop(lua_State* L) { spiRelease(me(L)); return 0; }

// ---------- drv: الدرايفرات الموقّعة (انظر docs/ARCHITECTURE.md) ----------
// Lua: drv.open(name, cfg) ← d ؛ d:method(...) ؛ d:close() ؛ drv.list()
// الفيرموير يتحقق من اسم الدالة وأنواع معاملاتها من جدول الدرايفر نفسه قبل استدعائها.
#define DRV_HANDLES_PER_SCRIPT 2
#define DRV_STR_MAX 4096
#define DRV_MT "esp.drv"

static Script* g_drvScript = nullptr;   // السكريبت الذي يستدعي الدرايفر الآن (المحرك مهمة واحدة)
static int g_drvErr = 0;                // آخر خطأ من واجهة العتاد

static int apiBus(int err) { g_drvErr = err; return err; }
static bool apiBusReady() { return i2cOn && g_drvScript && g_drvScript->usesI2c; }
static void apiLog(const char* m) { logPrintf("[drv] %s\n", m ? m : ""); }
static void* apiAlloc(size_t n) { return n && n <= 16384 ? malloc(n) : nullptr; }
static void apiRelease(void* p) { free(p); }
static uint32_t apiMillis() { return millis(); }
static void apiDelayMs(uint32_t ms) { delay(ms > 200 ? 200 : ms); }
static void apiDelayUs(uint32_t us) { delayMicroseconds(us > 10000 ? 10000 : us); }
static int apiI2cWrite(uint8_t addr, const uint8_t* d, size_t n) {
  if (!apiBusReady()) return apiBus(7);
  if (n > I2C_MAX) return apiBus(4);
  Wire.beginTransmission(addr); Wire.write(d, n);
  return apiBus(Wire.endTransmission(true));
}
static int apiRead(uint8_t addr, uint8_t* d, size_t n) {
  if (n > I2C_MAX) return apiBus(4);
  size_t got = Wire.requestFrom((uint16_t)addr, n, true), i = 0;
  while (i < got && i < n && Wire.available()) d[i++] = (uint8_t)Wire.read();
  return apiBus(i == n ? 0 : 8);
}
static int apiI2cRead(uint8_t addr, uint8_t* d, size_t n) { return apiBusReady() ? apiRead(addr, d, n) : apiBus(7); }
static int apiI2cWriteRead(uint8_t addr, const uint8_t* tx, size_t txn, uint8_t* rx, size_t rxn) {
  if (!apiBusReady()) return apiBus(7);
  if (txn > I2C_MAX) return apiBus(4);
  Wire.beginTransmission(addr); Wire.write(tx, txn);
  int e = Wire.endTransmission(false);
  return e ? apiBus(e) : apiRead(addr, rx, rxn);
}

static bool apiOwnPin(int pin) {            // يحجز الدبوس لسكريبت المستدعي (يُحرَّر مع السكريبت)
  if (!g_drvScript || pin < 0 || !pinAllowed(pin)) return false;
  int o = pinOwner[pin], mine = idxOf(g_drvScript) + 1;
  if (o == PIN_I2C || o == PIN_SPI || (o && o != mine)) return false;
  pinOwner[pin] = (uint8_t)mine;
  return true;
}
static int apiSpiReady(uint8_t cs, uint32_t freq, uint8_t mode, size_t n) {
  if (!spiOn || !g_drvScript || !g_drvScript->usesSpi) return apiBus(10);
  if (freq < 10000 || freq > 40000000 || mode > 3 || n > 4096) return apiBus(4);
  if (!apiOwnPin(cs)) return apiBus(9);
  return 0;
}
static int apiSpiWrite(uint8_t cs, uint32_t freq, uint8_t mode, const uint8_t* d, size_t n) {
  int e = apiSpiReady(cs, freq, mode, n); if (e) return e;
  pinMode(cs, OUTPUT);
  SPI.beginTransaction(SPISettings(freq, MSBFIRST, mode));
  digitalWrite(cs, LOW); SPI.writeBytes(d, (uint32_t)n); digitalWrite(cs, HIGH);
  SPI.endTransaction();
  return apiBus(0);
}
static int apiSpiTransfer(uint8_t cs, uint32_t freq, uint8_t mode, const uint8_t* tx, uint8_t* rx, size_t n) {
  int e = apiSpiReady(cs, freq, mode, n); if (e) return e;
  pinMode(cs, OUTPUT);
  SPI.beginTransaction(SPISettings(freq, MSBFIRST, mode));
  digitalWrite(cs, LOW); SPI.transferBytes(tx, rx, (uint32_t)n); digitalWrite(cs, HIGH);
  SPI.endTransaction();
  return apiBus(0);
}
static int apiGpioMode(uint8_t pin, uint8_t mode) {
  if (!apiOwnPin(pin)) return apiBus(9);
  pinMode(pin, mode == 1 ? OUTPUT : (mode == 2 ? INPUT_PULLUP : INPUT));
  return apiBus(0);
}
static int apiGpioWrite(uint8_t pin, uint8_t val) {
  if (!apiOwnPin(pin)) return apiBus(9);
  digitalWrite(pin, val ? HIGH : LOW);
  return apiBus(0);
}
static int apiGpioRead(uint8_t pin) {
  if (!g_drvScript || pin >= NUM_PINS || pinOwner[pin] != idxOf(g_drvScript) + 1) return -1;
  return digitalRead(pin);
}
static const char* drvErrName(int c) {
  switch (c) {
    case DRV_E_ARGS: return "bad_args";
    case DRV_E_FAULT: return "driver_fault";
    case 2: case 3: case 5: case 7: case 8: return i2cErr(c);
    case 9: return "pin_unavailable";
    case 10: return "spi_not_started";
    default: return "device_error";
  }
}
// يُستدعى من المفسّر حين ينادي الدرايفر دالة من api (الأرقام = ترتيب الحقول في drv_api_t بعد version).
// كل مؤشر من الدرايفر يُترجَم ويُفحص حدوده أولًا؛ أي عنوان خارج ذاكرته يُرفض بـDRV_E_ARGS.
uint32_t drv_host_call(DrvVM* v, int idx, const uint32_t* a) {
  const uint32_t BAD = (uint32_t)DRV_E_ARGS;
  switch (idx) {
    case 1: { const char* m = drv_vm_str(v, a[0], 200); apiLog(m ? m : ""); return 0; }
    case 2: return drv_vm_alloc(v, a[0]);
    case 3: drv_vm_free(v, a[0]); return 0;
    case 4: return apiMillis();
    case 5: apiDelayMs(a[0]); return 0;
    case 6: apiDelayUs(a[0]); return 0;
    case 7: { const uint8_t* d = drv_vm_ptr(v, a[1], a[2]); return d ? (uint32_t)apiI2cWrite((uint8_t)a[0], d, a[2]) : BAD; }
    case 8: { uint8_t* d = drv_vm_ptr(v, a[1], a[2]); return d ? (uint32_t)apiI2cRead((uint8_t)a[0], d, a[2]) : BAD; }
    case 9: { const uint8_t* tx = drv_vm_ptr(v, a[1], a[2]); uint8_t* rx = drv_vm_ptr(v, a[3], a[4]);
              return tx && rx ? (uint32_t)apiI2cWriteRead((uint8_t)a[0], tx, a[2], rx, a[4]) : BAD; }
    case 10: { const uint8_t* d = drv_vm_ptr(v, a[3], a[4]); return d ? (uint32_t)apiSpiWrite((uint8_t)a[0], a[1], (uint8_t)a[2], d, a[4]) : BAD; }
    case 11: { const uint8_t* tx = drv_vm_ptr(v, a[3], a[5]); uint8_t* rx = drv_vm_ptr(v, a[4], a[5]);
               return tx && rx ? (uint32_t)apiSpiTransfer((uint8_t)a[0], a[1], (uint8_t)a[2], tx, rx, a[5]) : BAD; }
    case 12: return (uint32_t)apiGpioMode((uint8_t)a[0], (uint8_t)a[1]);
    case 13: return (uint32_t)apiGpioWrite((uint8_t)a[0], (uint8_t)a[1]);
    case 14: return (uint32_t)apiGpioRead((uint8_t)a[0]);
  }
  return BAD;
}

// جدول api الحقيقي للدرايفرات الأصلية (حين تتوفر ذاكرة تنفيذ)؛ المؤشرات هنا حقيقية فلا ترجمة
static const drv_api_t NATIVE_API = { DRV_API_VERSION, apiLog, apiAlloc, apiRelease, apiMillis, apiDelayMs, apiDelayUs,
                                      apiI2cWrite, apiI2cRead, apiI2cWriteRead,
                                      apiSpiWrite, apiSpiTransfer, apiGpioMode, apiGpioWrite, apiGpioRead };
const drv_api_t* drv_native_api() { return &NATIVE_API; }

struct DrvHandle { DrvEntry* e; uint32_t ctx; };

static void drvClose(lua_State* L, DrvHandle* h) {
  if (!h->e) return;
  Script* s = me(L);
  g_drvScript = s; g_drvErr = 0;
  drv_vm_close(h->e, h->ctx);
  g_drvScript = nullptr;
  drv_release(h->e);
  if (s->drvOpen > 0) s->drvOpen--;
  h->e = nullptr; h->ctx = 0;
}

static int l_drv_open(lua_State* L) {
  Script* s = me(L);
  const char* name = luaL_checkstring(L, 1);
  bool hasCfg = !lua_isnoneornil(L, 2);
  if (hasCfg) luaL_checktype(L, 2, LUA_TTABLE);
  if (s->drvOpen >= DRV_HANDLES_PER_SCRIPT) return luaL_error(L, "too many open drivers (max %d)", DRV_HANDLES_PER_SCRIPT);
  drv_kv_t cfg[DRV_MAX_ARGS]; int nc = 0;
  if (hasCfg) {
    lua_pushnil(L);
    while (lua_next(L, 2)) {
      if (lua_type(L, -2) != LUA_TSTRING) luaL_error(L, "config keys must be strings");
      if (nc >= DRV_MAX_ARGS) luaL_error(L, "config: at most %d entries", DRV_MAX_ARGS);
      drv_kv_t& kv = cfg[nc++];
      kv.key = lua_tostring(L, -2);
      memset(&kv.val, 0, sizeof(kv.val));
      int t = lua_type(L, -1);
      if (t == LUA_TNUMBER && lua_isinteger(L, -1)) { lua_Integer v = lua_tointeger(L, -1); if (v < INT32_MIN || v > INT32_MAX) luaL_error(L, "config '%s' out of range", kv.key); kv.val.type = DRV_INT; kv.val.v.i = (int32_t)v; }
      else if (t == LUA_TBOOLEAN) { kv.val.type = DRV_BOOL; kv.val.v.i = lua_toboolean(L, -1); }
      else if (t == LUA_TSTRING) { size_t l; kv.val.v.s = lua_tolstring(L, -1, &l); kv.val.type = DRV_STR; kv.val.len = (uint16_t)(l > DRV_STR_MAX ? DRV_STR_MAX : l); }
      else luaL_error(L, "config '%s': integer, boolean or string expected", kv.key);
      lua_pop(L, 1);
    }
  }
  // خصّص userdata والجدول المرفق قبل حجز الدرايفر، كي لا يتسرب users أو ctx عند نفاد ذاكرة Lua
  DrvHandle* h = (DrvHandle*)lua_newuserdatauv(L, sizeof(DrvHandle), 1);
  h->e = nullptr; h->ctx = 0;
  luaL_setmetatable(L, DRV_MT);
  lua_newtable(L);                    // ذاكرة مؤقتة للدوال المنشأة
  lua_setiuservalue(L, -2, 1);

  DrvEntry* e = drv_acquire(name);        // يحجز الدرايفر فلا يُحذف أو يُستبدل أثناء الاستعمال
  if (!e) return failResult(L, "driver_not_installed");
  uint32_t ctx = 0; int rc;
  {
    IoTimer t(s);
    g_drvScript = s; g_drvErr = 0;
    rc = drv_vm_open(e, cfg, nc, &ctx);
    g_drvScript = nullptr;
  }
  if (rc) {
    drv_release(e);
    if (rc == DRV_E_ARGS) return failResult(L, "bad_args");
    int c = g_drvErr ? g_drvErr : rc;
    return failResult(L, drvErrName(c));
  }
  h->e = e; h->ctx = ctx; s->drvOpen++;
  return 1;
}

// d:method(...) — upvalue 1 = رقم الدالة في جدول الدرايفر
static int l_drv_call(lua_State* L) {
  DrvHandle* h = (DrvHandle*)luaL_checkudata(L, 1, DRV_MT);
  if (!h->e) return failResult(L, "closed");
  Script* s = me(L);
  const int mi = (int)lua_tointeger(L, lua_upvalueindex(1));
  const DrvMethodInfo& m = h->e->ops.methods[mi];
  int nargs = lua_gettop(L) - 1, total = (int)strlen(m.sig), need = 0;
  for (const char* c = m.sig; *c; c++) if (islower((unsigned char)*c)) need++;
  if (nargs < need || nargs > total) return failResult(L, "bad_args");
  drv_val_t a[DRV_MAX_ARGS]; memset(a, 0, sizeof(a));
  for (int i = 0; i < nargs; i++) {
    int idx = i + 2, ok = 0; drv_val_t& v = a[i];
    switch (tolower((unsigned char)m.sig[i])) {
      case 'i': { int isn = 0; lua_Integer n = lua_tointegerx(L, idx, &isn);
                  if (isn && lua_type(L, idx) == LUA_TNUMBER && n >= INT32_MIN && n <= INT32_MAX) { v.type = DRV_INT; v.v.i = (int32_t)n; ok = 1; } break; }
      case 'b': if (lua_type(L, idx) == LUA_TBOOLEAN) { v.type = DRV_BOOL; v.v.i = lua_toboolean(L, idx); ok = 1; } break;
      case 's': case 'd': if (lua_type(L, idx) == LUA_TSTRING) {
                  size_t l; const char* p = lua_tolstring(L, idx, &l);
                  if (l <= DRV_STR_MAX) { v.type = tolower((unsigned char)m.sig[i]) == 's' ? DRV_STR : DRV_BLOB; v.len = (uint16_t)l; v.v.s = p; ok = 1; } } break;
    }
    if (!ok) return failResult(L, "bad_args");
  }
  drv_val_t r[DRV_MAX_ARGS]; int nr = 0, rc;
  {
    IoTimer t(s);
    g_drvScript = s; g_drvErr = 0;
    rc = drv_vm_invoke(h->e, mi, h->ctx, a, nargs, r, &nr);
    g_drvScript = nullptr;
  }
  if (rc == DRV_E_ARGS) return failResult(L, "bad_args");
  if (rc) { int c = g_drvErr ? g_drvErr : rc; lua_pushnil(L); lua_pushstring(L, drvErrName(c)); return 2; }
  if (nr <= 0) { lua_pushboolean(L, 1); return 1; }
  if (nr > DRV_MAX_ARGS) nr = DRV_MAX_ARGS;
  luaL_checkstack(L, nr, "drv results");
  for (int i = 0; i < nr; i++) {
    switch (r[i].type) {
      case DRV_BOOL: lua_pushboolean(L, r[i].v.i != 0); break;
      case DRV_INT:  lua_pushinteger(L, r[i].v.i); break;
      case DRV_STR: case DRV_BLOB:
        if (r[i].v.p) lua_pushlstring(L, (const char*)r[i].v.p, r[i].len);
        else if (r[i].len == 0) lua_pushliteral(L, "");
        else lua_pushnil(L);
        break;
      default: lua_pushnil(L);
    }
  }
  return nr;
}
static int l_drv_unknown(lua_State* L) { return failResult(L, "unknown_method"); }
static int l_drv_close(lua_State* L) { drvClose(L, (DrvHandle*)luaL_checkudata(L, 1, DRV_MT)); return 0; }

static int l_drv_index(lua_State* L) {
  DrvHandle* h = (DrvHandle*)luaL_checkudata(L, 1, DRV_MT);
  const char* key = luaL_checkstring(L, 2);
  if (!strcmp(key, "close")) { lua_pushcfunction(L, l_drv_close); return 1; }
  if (!h->e) { lua_pushcfunction(L, l_drv_unknown); return 1; }
  lua_getiuservalue(L, 1, 1);
  lua_getfield(L, -1, key);
  if (!lua_isnil(L, -1)) return 1;
  lua_pop(L, 1);
  const DrvOps* o = &h->e->ops;
  for (int i = 0; i < o->nmethods; i++) if (!strcmp(o->methods[i].name, key)) {
    lua_pushinteger(L, i);
    lua_pushcclosure(L, l_drv_call, 1);
    lua_pushvalue(L, -1); lua_setfield(L, -3, key);
    return 1;
  }
  lua_pushcfunction(L, l_drv_unknown);
  return 1;
}
static int l_drv_gc(lua_State* L) { drvClose(L, (DrvHandle*)lua_touserdata(L, 1)); return 0; }
static int l_drv_tostring(lua_State* L) {
  DrvHandle* h = (DrvHandle*)lua_touserdata(L, 1);
  lua_pushfstring(L, "drv:%s", h->e ? h->e->name : "closed"); return 1;
}

static int l_drv_list(lua_State* L) {
  DrvBrief b[DRV_MAX_LOADED];
  int cnt = drv_snapshot(b, DRV_MAX_LOADED);      // النسخ تحت القفل ثم نبني جدول Lua بلا قفل
  lua_newtable(L);
  for (int i = 0; i < cnt; i++) {
    lua_newtable(L);
    lua_pushstring(L, b[i].name); lua_setfield(L, -2, "name");
    lua_pushstring(L, b[i].version); lua_setfield(L, -2, "version");
    lua_pushstring(L, b[i].info); lua_setfield(L, -2, "info");
    lua_rawseti(L, -2, i + 1);
  }
  return 1;
}

static void drvRegister(lua_State* L) {
  luaL_newmetatable(L, DRV_MT);
  lua_pushcfunction(L, l_drv_index);    lua_setfield(L, -2, "__index");
  lua_pushcfunction(L, l_drv_gc);       lua_setfield(L, -2, "__gc");
  lua_pushcfunction(L, l_drv_tostring); lua_setfield(L, -2, "__tostring");
  lua_pop(L, 1);
}


// ---------- require: مكتبات Lua من /scripts/lib/<name>.lua ----------
#define LIB_DIR "/scripts/lib"

static char* loadPath(const String& p, size_t* len) {
  File f = LittleFS.open(p, "r");
  if (!f || f.isDirectory() || f.size() == 0 || f.size() > MAX_CODE) { if (f) f.close(); return nullptr; }
  size_t n = f.size();
  char* buf = (char*)malloc(n + 1);
  if (!buf) { f.close(); return nullptr; }
  size_t r = f.read((uint8_t*)buf, n);
  f.close();
  if (r != n) { free(buf); return nullptr; }
  buf[r] = 0; *len = r;
  return buf;
}

static int l_require(lua_State* L) {
  Script* s = me(L);
  const char* name = luaL_checkstring(L, 1);
  if (!strncmp(name, "lib/", 4)) name += 4;                 // require("lib/x") = require("x")
  if (!luaeng_valid_name(name)) return luaL_error(L, "bad module name '%s'", name);
  lua_getfield(L, LUA_REGISTRYINDEX, "esp_loaded");
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1); lua_newtable(L);
    lua_pushvalue(L, -1); lua_setfield(L, LUA_REGISTRYINDEX, "esp_loaded");
  }
  lua_getfield(L, -1, name);
  if (!lua_isnil(L, -1)) return 1;                           // محمَّلة سابقًا
  lua_pop(L, 1);
  if (s->requireDepth >= 4) return luaL_error(L, "require nested too deep (circular?)");
  size_t len = 0;
  uint32_t t0 = millis();
  char* buf = loadPath(String(LIB_DIR) + "/" + name + ".lua", &len);
  s->deadline += millis() - t0;
  if (!buf) return luaL_error(L, "module '%s' not found (/scripts/lib/%s.lua)", name, name);
  String chunk = String("lib/") + name;
  int st = luaL_loadbufferx(L, buf, len, chunk.c_str(), "t");
  free(buf);
  if (st != LUA_OK) return lua_error(L);
  lua_pushstring(L, name);
  s->requireDepth++;
  st = lua_pcall(L, 1, 1, 0);
  s->requireDepth--;
  if (st != LUA_OK) return lua_error(L);
  if (lua_isnil(L, -1)) { lua_pop(L, 1); lua_pushboolean(L, 1); }
  lua_pushvalue(L, -1);
  lua_setfield(L, -3, name);
  return 1;
}

static void handleEvent(const UiEv& e) {
  if (e.panel >= MAX_PANELS || !panels[e.panel].used) return;
  Script* s = &scripts[panels[e.panel].owner];
  if (s->state != ST_RUN || !s->L) return;
  xSemaphoreTake(uiMux, portMAX_DELAY);
  for (auto& w : widgets) if (w.used && w.panel == e.panel && !strcmp(w.id, e.name) &&
      (w.type == W_SWITCH || w.type == W_SLIDER || w.type == W_INPUT)) strcpy(w.val, e.val);
  xSemaphoreGive(uiMux);
  lua_State* L = s->L;
  lua_getglobal(L, "__ui_dispatch");
  if (!lua_isfunction(L, -1)) { lua_pop(L, 1); return; }
  lua_pushinteger(L, e.panel);
  lua_pushstring(L, e.name);
  pushJsonValue(L, e.val);
  protectedCall(s, 3);
}

static void engineTask(void*) {
  Cmd c;
  vTaskDelay(pdMS_TO_TICKS(2500));   // أمهل الواي فاي والخادم قبل تشغيل السكريبتات التلقائية
  autorunBoot();
  for (;;) {
    if (bootClearAt && (int32_t)(millis() - bootClearAt) >= 0) { bootClearAt = 0; writeText(BOOT_FILE, "0"); }
    if (xQueueReceive(cmdQ, &c, pdMS_TO_TICKS(10)) == pdTRUE) {
      if (c.type == C_RUN) { startScript(c.name, c.code, c.len); free(c.code); }
      else if (c.type == C_STOP) {
        Script* s = findByName(c.name);
        if (s) { killScript(s); logPrintf("[%s] stopped\n", c.name); }
      } else if (c.type == C_STOPALL) {
        for (auto& s : scripts) if (s.state != ST_FREE) killScript(&s);
      }
    }
    UiEv ev;
    while (xQueueReceive(evQ, &ev, 0) == pdTRUE) handleEvent(ev);
    busDrain();
    for (auto& s : scripts) if (s.state == ST_RUN) tickScript(&s);
    // تحديث حالة الذاكرة للعرض
  }
}

// ---------- الواجهة العامة ----------
void luaeng_begin() {
  statusMux = xSemaphoreCreateMutex();
  uiMux = xSemaphoreCreateMutex();
  cmdQ = xQueueCreate(4, sizeof(Cmd));
  evQ = xQueueCreate(8, sizeof(UiEv));
  xTaskCreate(engineTask, "lua", 16384, NULL, 2, NULL);
}

bool luaeng_run(const char* name, const char* code, size_t len) {
  if (!cmdQ || !luaeng_valid_name(name) || len == 0 || len > MAX_CODE) return false;
  Cmd c = {}; c.type = C_RUN; strncpy(c.name, name, NAME_LEN - 1);
  c.code = (char*)malloc(len + 1);
  if (!c.code) return false;
  memcpy(c.code, code, len); c.code[len] = 0; c.len = len;
  if (xQueueSend(cmdQ, &c, 0) != pdTRUE) { free(c.code); return false; }
  return true;
}

bool luaeng_run_file(const char* name) {
  if (!luaeng_valid_name(name)) return false;
  String p = String("/scripts/") + name + ".lua";
  File f = LittleFS.open(p, "r");
  if (!f || f.isDirectory() || f.size() == 0 || f.size() > MAX_CODE) { if (f) f.close(); return false; }
  size_t n = f.size();
  char* buf = (char*)malloc(n + 1);
  if (!buf) { f.close(); return false; }
  size_t r = f.read((uint8_t*)buf, n);
  f.close();
  if (r != n) { free(buf); return false; }
  bool ok = luaeng_run(name, buf, r);
  free(buf);
  return ok;
}

bool luaeng_stop(const char* name) {
  if (!cmdQ || !luaeng_valid_name(name)) return false;
  Cmd c = {}; c.type = C_STOP; strncpy(c.name, name, NAME_LEN - 1);
  return xQueueSend(cmdQ, &c, 0) == pdTRUE;
}

void luaeng_stop_all() { if (cmdQ) { Cmd c = {}; c.type = C_STOPALL; xQueueSend(cmdQ, &c, 0); } }

String luaeng_list_text() {
  if (!statusMux) return String("(no scripts)");
  String o;
  xSemaphoreTake(statusMux, portMAX_DELAY);
  for (auto& s : scripts) if (s.state != ST_FREE) {
    o += String(s.name) + "  " + (s.state == ST_RUN ? "running" : "error") + "  " + String((unsigned)s.mem) + "B";
    if (s.state == ST_ERR) o += "  " + String(s.err);
    o += "\n";
  }
  xSemaphoreGive(statusMux);
  return o.length() ? o : String("(no scripts)");
}

String luaeng_list_json() {
  JsonDocument d;
  JsonArray a = d.to<JsonArray>();
  if (statusMux) {
    xSemaphoreTake(statusMux, portMAX_DELAY);
    for (auto& s : scripts) if (s.state != ST_FREE) {
      JsonObject o = a.add<JsonObject>();
      o["name"] = s.name;
      o["state"] = s.state == ST_RUN ? "running" : "error";
      o["mem"] = (uint32_t)s.mem;
      o["up"] = (millis() - s.startMs) / 1000;
      if (s.state == ST_ERR) o["err"] = s.err;
    }
    xSemaphoreGive(statusMux);
  }
  String out; serializeJson(d, out);
  return out;
}

// ---------- واجهة HTTP للوحات ----------
String luaeng_ui_panels_json() {
  JsonDocument d;
  JsonObject root = d.to<JsonObject>();
  if (uiMux) {
    xSemaphoreTake(uiMux, portMAX_DELAY);
    root["ver"] = uiVer;
    JsonArray ps = root["panels"].to<JsonArray>();
    for (int i = 0; i < MAX_PANELS; i++) if (panels[i].used) {
      JsonObject po = ps.add<JsonObject>();
      po["id"] = i;
      po["script"] = panels[i].script;
      po["title"] = panels[i].title;
      if (panels[i].page[0]) po["page"] = panels[i].page;
      JsonArray ws = po["widgets"].to<JsonArray>();
      for (auto& w : widgets) if (w.used && w.panel == i) {
        JsonObject wo = ws.add<JsonObject>();
        wo["id"] = w.id;
        wo["type"] = WTYPES[w.type];
        wo["label"] = w.label;
        wo["min"] = w.mn;
        wo["max"] = w.mx;
        wo["value"] = serialized(w.val);
      }
    }
    xSemaphoreGive(uiMux);
  }
  String out; serializeJson(d, out);
  return out;
}

String luaeng_ui_poll_json(uint32_t since) {
  if (!uiMux) return String("{\"seq\":0,\"ver\":1,\"msgs\":[]}");
  int idx[MAX_MSGS]; int n = 0;
  String o;
  xSemaphoreTake(uiMux, portMAX_DELAY);
  for (int i = 0; i < MAX_MSGS; i++) if (msgs[i].seq > since) {
    int k = n++;
    while (k > 0 && msgs[idx[k - 1]].seq > msgs[i].seq) { idx[k] = idx[k - 1]; k--; }
    idx[k] = i;
  }
  o.reserve(64 + n * 80);
  char hd[64]; snprintf(hd, sizeof(hd), "{\"seq\":%u,\"ver\":%u,\"msgs\":[", (unsigned)msgSeq, (unsigned)uiVer);
  o += hd;                                                  // بلا String مؤقتة فتبقى الذاكرة المحجوزة
  for (int k = 0; k < n; k++) {
    char b[110];
    snprintf(b, sizeof(b), "%s{\"p\":%u,\"n\":\"%s\",\"v\":%s}", k ? "," : "", (unsigned)msgs[idx[k]].panel, msgs[idx[k]].name, msgs[idx[k]].val);
    o += b;
  }
  o += "]}";
  xSemaphoreGive(uiMux);
  return o;
}

bool luaeng_ui_event(int panel, const char* name, const char* valJson) {
  if (!evQ || panel < 0 || panel >= MAX_PANELS || !safeName(name, ID_LEN) || !valJson || !*valJson || strlen(valJson) >= VAL_LEN) return false;
  JsonDocument d;
  if (deserializeJson(d, valJson) != DeserializationError::Ok) return false;
  UiEv e = {}; e.panel = panel;
  strncpy(e.name, name, ID_LEN - 1); strncpy(e.val, valJson, VAL_LEN - 1);
  return xQueueSend(evQ, &e, 0) == pdTRUE;
}

bool luaeng_save(const char* name, const char* code, size_t len, const char* page) {
  if (!luaeng_valid_name(name) || len == 0 || len > MAX_CODE) return false;
  ensureDir();
  File f = LittleFS.open(String(SCRIPTS_DIR) + "/" + name + ".lua", "w");
  if (!f) return false;
  size_t w = f.write((const uint8_t*)code, len);
  f.close();
  if (w != len) return false;
  String pp = String(SCRIPTS_DIR) + "/" + name + ".page";
  if (page && *page) writeText(pp, String(page));
  else if (LittleFS.exists(pp)) LittleFS.remove(pp);
  return true;
}

bool luaeng_set_auto(const char* name, bool on) {
  if (!luaeng_valid_name(name)) return false;
  ensureDir();
  String list = readText(AUTO_FILE), out;
  int i = 0, n = (int)list.length();
  while (i < n) {
    int j = list.indexOf('\n', i);
    if (j < 0) j = n;
    String l = list.substring(i, j);
    i = j + 1;
    l.trim();
    if (l.length() && l != name) out += l + "\n";
  }
  if (on) out += String(name) + "\n";
  return writeText(AUTO_FILE, out);
}

bool luaeng_delete(const char* name) {
  if (!luaeng_valid_name(name)) return false;
  luaeng_stop(name);
  luaeng_set_auto(name, false);
  String b = String(SCRIPTS_DIR) + "/" + name;
  if (LittleFS.exists(b + ".lua")) LittleFS.remove(b + ".lua");
  if (LittleFS.exists(b + ".page")) LittleFS.remove(b + ".page");
  String dp = kvPath(name);
  if (LittleFS.exists(dp)) LittleFS.remove(dp);
  return true;
}

String luaeng_files_json() {
  JsonDocument d;
  JsonArray a = d.to<JsonArray>();
  String list = readText(AUTO_FILE);
  File dir = LittleFS.open(SCRIPTS_DIR);
  if (dir && dir.isDirectory()) {
    File f = dir.openNextFile();
    while (f) {
      String n = f.name();
      if (!f.isDirectory() && n.endsWith(".lua")) {
        n = n.substring(0, n.length() - 4);
        if (luaeng_valid_name(n.c_str())) {
          JsonObject o = a.add<JsonObject>();
          o["name"] = n;
          o["size"] = (uint32_t)f.size();
          o["auto"] = autoHas(list, n.c_str());
          String pg = readText(String(SCRIPTS_DIR) + "/" + n + ".page");
          pg.trim();
          if (pg.length()) o["page"] = pg;
        }
      }
      f.close();
      f = dir.openNextFile();
    }
    dir.close();
  }
  String out; serializeJson(d, out);
  return out;
}

// للطرفية: pub <topic> <value> (القيمة JSON صالح أو نص عادي)
bool luaeng_bus_publish(const char* topic, const char* text) {
  if (!safeName(topic, 16) || strlen(text) >= BUS_JSON - 2) return false;
  JsonDocument d;
  if (!deserializeJson(d, text)) return busPush(255, topic, text);
  char q[BUS_JSON]; size_t n = 0;
  q[n++] = '"';
  for (const char* p = text; *p && n < sizeof(q) - 3; p++) {
    if (*p == '"' || *p == '\\') q[n++] = '\\';
    q[n++] = ((unsigned char)*p < 0x20) ? ' ' : *p;
  }
  q[n++] = '"'; q[n] = 0;
  return busPush(255, topic, q);
}

// للطرفية: محتوى مخزن سكريبت كما هو محفوظ على الجهاز
String luaeng_storage_dump(const char* name) {
  if (!luaeng_valid_name(name)) return String("invalid name");
  String t = readText(kvPath(name));
  return t.length() ? t : String("(empty)");
}

bool luaeng_save_lib(const char* name, const char* code, size_t len) {
  if (!luaeng_valid_name(name) || len == 0 || len > MAX_CODE) return false;
  ensureDir();
  if (!LittleFS.exists(LIB_DIR)) LittleFS.mkdir(LIB_DIR);
  File f = LittleFS.open(String(LIB_DIR) + "/" + name + ".lua", "w");
  if (!f) return false;
  size_t w = f.write((const uint8_t*)code, len);
  f.close();
  return w == len;
}

bool luaeng_delete_lib(const char* name) {
  if (!luaeng_valid_name(name)) return false;
  String p = String(LIB_DIR) + "/" + name + ".lua";
  if (LittleFS.exists(p)) LittleFS.remove(p);
  return true;
}

String luaeng_libs_json() {
  JsonDocument d;
  JsonArray a = d.to<JsonArray>();
  File dir = LittleFS.open(LIB_DIR);
  if (dir && dir.isDirectory()) {
    File f = dir.openNextFile();
    while (f) {
      String n = f.name();
      if (!f.isDirectory() && n.endsWith(".lua")) {
        n = n.substring(0, n.length() - 4);
        if (luaeng_valid_name(n.c_str())) {
          JsonObject o = a.add<JsonObject>();
          o["name"] = n;
          o["size"] = (uint32_t)f.size();
        }
      }
      f.close();
      f = dir.openNextFile();
    }
    dir.close();
  }
  String out; serializeJson(d, out);
  return out;
}
