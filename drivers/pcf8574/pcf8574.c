// pcf8574: موسّع 8 أرجل I2C (PCF8574/PCF8574A)
// cfg: addr=0x20, init=255 (الحالة الابتدائية؛ البت 1 = دخل/خرج عالٍ)
// الدوال: write(byte) · read() -> byte · pin(n, on) · toggle(n)
#include "drvrt.h"

typedef struct { const drv_api_t* api; uint8_t addr, state; } Pcf;
static const char NAME[] = "pcf8574";

static int do_open(void** ctx, const drv_api_t* api, const drv_kv_t* cfg, int n) {
  Pcf* d = (Pcf*)api->alloc(sizeof(Pcf));
  if (!d) return 1;
  d->api = api;
  d->addr = (uint8_t)drv_cfg_int(cfg, n, "addr", 0x20);
  d->state = (uint8_t)drv_cfg_int(cfg, n, "init", 0xFF);
  int e = api->i2c_write(d->addr, &d->state, 1);
  if (e) { api->release(d); return e; }
  *ctx = d;
  return 0;
}
static void do_close(void* ctx) { ((Pcf*)ctx)->api->release(ctx); }

static int put(Pcf* d, uint8_t v) { d->state = v; return d->api->i2c_write(d->addr, &v, 1); }

static int m_write(void* ctx, const drv_val_t* a, int na, drv_val_t* r, int* nr) {
  (void)r; (void)nr;
  if (na < 1 || a[0].v.i < 0 || a[0].v.i > 255) return DRV_E_ARGS;
  return put((Pcf*)ctx, (uint8_t)a[0].v.i);
}
static int m_read(void* ctx, const drv_val_t* a, int na, drv_val_t* r, int* nr) {
  (void)a; (void)na;
  Pcf* d = (Pcf*)ctx; uint8_t b;
  int e = d->api->i2c_read(d->addr, &b, 1);
  if (e) return e;
  drv_ret_int(&r[0], b); *nr = 1;
  return 0;
}
static int m_pin(void* ctx, const drv_val_t* a, int na, drv_val_t* r, int* nr) {
  (void)r; (void)nr;
  Pcf* d = (Pcf*)ctx;
  if (na < 2 || a[0].v.i < 0 || a[0].v.i > 7) return DRV_E_ARGS;
  uint8_t m = (uint8_t)(1u << a[0].v.i);
  return put(d, a[1].v.i ? (uint8_t)(d->state | m) : (uint8_t)(d->state & ~m));
}
static int m_toggle(void* ctx, const drv_val_t* a, int na, drv_val_t* r, int* nr) {
  (void)r; (void)nr;
  Pcf* d = (Pcf*)ctx;
  if (na < 1 || a[0].v.i < 0 || a[0].v.i > 7) return DRV_E_ARGS;
  return put(d, (uint8_t)(d->state ^ (1u << a[0].v.i)));
}
static const drv_method_t METHODS[] = { { "write", "i", m_write }, { "read", "", m_read }, { "pin", "ib", m_pin }, { "toggle", "i", m_toggle } };
static const drv_ops_t OPS = { DRV_API_VERSION, NAME, "PCF8574 8-bit I/O expander (I2C)", METHODS, 4, do_open, do_close };
DRV_EXPORT const drv_ops_t* drv_entry(void) { return &OPS; }
