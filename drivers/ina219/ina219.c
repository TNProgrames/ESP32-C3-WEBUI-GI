// ina219: مراقب تيار وجهد وقدرة TI INA219 عبر I2C
// cfg: addr=0x40, shunt_mohm=100 (مقاومة الشنت بالملّي أوم)
// الدوال: read() -> bus_mv, current_ma, power_mw
#include "drvrt.h"

typedef struct { const drv_api_t* api; uint8_t addr; int32_t shunt; } Ina;
static const char NAME[] = "ina219";

static int rd16(Ina* d, uint8_t reg, int16_t* v) {
  uint8_t b[2];
  int e = d->api->i2c_write_read(d->addr, &reg, 1, b, 2);
  if (!e) *v = (int16_t)(((uint16_t)b[0] << 8) | b[1]);
  return e;
}
static int do_open(void** ctx, const drv_api_t* api, const drv_kv_t* cfg, int n) {
  Ina* d = (Ina*)api->alloc(sizeof(Ina));
  if (!d) return 1;
  d->api = api;
  d->addr = (uint8_t)drv_cfg_int(cfg, n, "addr", 0x40);
  int sh = drv_cfg_int(cfg, n, "shunt_mohm", 100);
  d->shunt = sh < 1 ? 100 : sh;
  int16_t t; int e = rd16(d, 0x00, &t);                // تحقق من وجود الجهاز
  if (e) { api->release(d); return e; }
  *ctx = d;
  return 0;
}
static void do_close(void* ctx) { ((Ina*)ctx)->api->release(ctx); }

static int m_read(void* ctx, const drv_val_t* a, int na, drv_val_t* r, int* nr) {
  (void)a; (void)na;
  Ina* d = (Ina*)ctx; int16_t sv, bv;
  int e = rd16(d, 0x01, &sv);
  if (!e) e = rd16(d, 0x02, &bv);
  if (e) return e;
  int32_t bus_mv = (int32_t)(((uint16_t)bv >> 3) * 4);          // 4mV لكل خطوة
  int32_t cur_ma = ((int32_t)sv * 10) / d->shunt;               // I = V/R : (10µV*raw)/mΩ = mA
  drv_ret_int(&r[0], bus_mv);
  drv_ret_int(&r[1], cur_ma);
  drv_ret_int(&r[2], (bus_mv * cur_ma) / 1000);
  *nr = 3;
  return 0;
}
static const drv_method_t METHODS[] = { { "read", "", m_read } };
static const drv_ops_t OPS = { DRV_API_VERSION, NAME, "INA219 current/power (I2C)", METHODS, 1, do_open, do_close };
DRV_EXPORT const drv_ops_t* drv_entry(void) { return &OPS; }
