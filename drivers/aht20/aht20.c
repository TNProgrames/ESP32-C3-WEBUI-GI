// aht20: حساس حرارة ورطوبة Aosong AHT20 عبر I2C (حسابات صحيحة بلا float)
// cfg: addr=0x38
// الدوال: read() -> temp_c_x100, hum_x100 · info() -> model
#include "drvrt.h"

typedef struct { const drv_api_t* api; uint8_t addr; } Aht;
static const char NAME[] = "aht20";

static int do_open(void** ctx, const drv_api_t* api, const drv_kv_t* cfg, int n) {
  Aht* d = (Aht*)api->alloc(sizeof(Aht));
  if (!d) return 1;
  d->api = api;
  d->addr = (uint8_t)drv_cfg_int(cfg, n, "addr", 0x38);
  api->delay_ms(40);                                   // زمن الإقلاع
  uint8_t cmd = 0x71, st = 0;
  int e = api->i2c_write_read(d->addr, &cmd, 1, &st, 1);
  if (e) { api->release(d); return e; }
  if ((st & 0x18) != 0x18) {                           // غير مُعايَر: أرسل أمر التهيئة
    uint8_t init[3] = { 0xBE, 0x08, 0x00 };
    e = api->i2c_write(d->addr, init, 3);
    if (e) { api->release(d); return e; }
    api->delay_ms(10);
  }
  *ctx = d;
  return 0;
}
static void do_close(void* ctx) { ((Aht*)ctx)->api->release(ctx); }

static int m_read(void* ctx, const drv_val_t* a, int na, drv_val_t* r, int* nr) {
  (void)a; (void)na;
  Aht* d = (Aht*)ctx;
  uint8_t trig[3] = { 0xAC, 0x33, 0x00 }, b[7];
  int e = d->api->i2c_write(d->addr, trig, 3);
  if (e) return e;
  for (int i = 0; i < 10; i++) {                       // انتظر انتهاء القياس (~80ms)
    d->api->delay_ms(10);
    e = d->api->i2c_read(d->addr, b, 7);
    if (e) return e;
    if (!(b[0] & 0x80)) break;
  }
  if (b[0] & 0x80) return 5;                           // ما زال مشغولاً
  uint32_t hum  = ((uint32_t)b[1] << 12) | ((uint32_t)b[2] << 4) | (b[3] >> 4);
  uint32_t temp = ((uint32_t)(b[3] & 0x0F) << 16) | ((uint32_t)b[4] << 8) | b[5];
  drv_ret_int(&r[0], (int32_t)(((temp >> 4) * 20000u) >> 16) - 5000);   // temp = raw/2^20*200-50  (x100)
  drv_ret_int(&r[1], (int32_t)(((hum >> 4) * 10000u) >> 16));           // hum  = raw/2^20*100     (x100)
  *nr = 2;
  return 0;
}
static int m_info(void* ctx, const drv_val_t* a, int na, drv_val_t* r, int* nr) {
  (void)ctx; (void)a; (void)na;
  drv_ret_str(&r[0], "AHT20"); *nr = 1; return 0;
}
static const drv_method_t METHODS[] = { { "read", "", m_read }, { "info", "", m_info } };
static const drv_ops_t OPS = { DRV_API_VERSION, NAME, "AHT20 temp/hum (I2C)", METHODS, 2, do_open, do_close };
DRV_EXPORT const drv_ops_t* drv_entry(void) { return &OPS; }
