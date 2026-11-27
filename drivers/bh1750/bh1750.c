// bh1750: حساس إضاءة ROHM BH1750 عبر I2C
// cfg: addr=0x23 (أو 0x5C)
// الدوال: read() -> lux_x10, raw · info() -> model
#include "drvrt.h"

typedef struct { const drv_api_t* api; uint8_t addr; } Bh;
static const char NAME[] = "bh1750";

static int do_open(void** ctx, const drv_api_t* api, const drv_kv_t* cfg, int n) {
  Bh* d = (Bh*)api->alloc(sizeof(Bh));
  if (!d) return 1;
  d->api = api;
  d->addr = (uint8_t)drv_cfg_int(cfg, n, "addr", 0x23);
  uint8_t c = 0x01;                                    // تشغيل
  int e = api->i2c_write(d->addr, &c, 1);
  if (!e) { c = 0x10; e = api->i2c_write(d->addr, &c, 1); }   // قياس مستمر بدقة 1 lx
  if (e) { api->release(d); return e; }
  api->delay_ms(180);                                  // أول قياس
  *ctx = d;
  return 0;
}
static void do_close(void* ctx) {
  Bh* d = (Bh*)ctx; uint8_t c = 0x00;                  // إيقاف
  d->api->i2c_write(d->addr, &c, 1);
  d->api->release(d);
}
static int m_read(void* ctx, const drv_val_t* a, int na, drv_val_t* r, int* nr) {
  (void)a; (void)na;
  Bh* d = (Bh*)ctx; uint8_t b[2];
  int e = d->api->i2c_read(d->addr, b, 2);
  if (e) return e;
  uint32_t raw = ((uint32_t)b[0] << 8) | b[1];
  drv_ret_int(&r[0], (int32_t)(raw * 25u / 3u));       // lux = raw/1.2  → x10 = raw*25/3
  drv_ret_int(&r[1], (int32_t)raw);
  *nr = 2;
  return 0;
}
static int m_info(void* ctx, const drv_val_t* a, int na, drv_val_t* r, int* nr) {
  (void)ctx; (void)a; (void)na;
  drv_ret_str(&r[0], "BH1750"); *nr = 1; return 0;
}
static const drv_method_t METHODS[] = { { "read", "", m_read }, { "info", "", m_info } };
static const drv_ops_t OPS = { DRV_API_VERSION, NAME, "BH1750 light sensor (I2C)", METHODS, 2, do_open, do_close };
DRV_EXPORT const drv_ops_t* drv_entry(void) { return &OPS; }
