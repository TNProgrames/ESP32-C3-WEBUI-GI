// ads1115: محوّل تماثلي-رقمي 16 بت (4 قنوات) ADS1115 عبر I2C
// cfg: addr=0x48, gain=1 (0:±6144 1:±4096 2:±2048 3:±1024 4:±512 5:±256 mV)
// الدوال: read(ch) -> mv, raw · gain(g)
#include "drvrt.h"

typedef struct { const drv_api_t* api; uint8_t addr, gain; } Adc;
static const char NAME[] = "ads1115";
static const int16_t FS_MV[6] = { 6144, 4096, 2048, 1024, 512, 256 };

static int do_open(void** ctx, const drv_api_t* api, const drv_kv_t* cfg, int n) {
  Adc* d = (Adc*)api->alloc(sizeof(Adc));
  if (!d) return 1;
  d->api = api;
  d->addr = (uint8_t)drv_cfg_int(cfg, n, "addr", 0x48);
  int g = drv_cfg_int(cfg, n, "gain", 1);
  d->gain = (uint8_t)((g < 0 || g > 5) ? 1 : g);
  uint8_t reg = 0x01, b[2];
  int e = api->i2c_write_read(d->addr, &reg, 1, b, 2);   // تحقق من وجود الجهاز
  if (e) { api->release(d); return e; }
  *ctx = d;
  return 0;
}
static void do_close(void* ctx) { ((Adc*)ctx)->api->release(ctx); }

static int m_read(void* ctx, const drv_val_t* a, int na, drv_val_t* r, int* nr) {
  Adc* d = (Adc*)ctx;
  if (na < 1 || a[0].v.i < 0 || a[0].v.i > 3) return DRV_E_ARGS;
  uint16_t cfg = 0x8000 | (uint16_t)((4 + a[0].v.i) << 12) | (uint16_t)(d->gain << 9) | 0x0100 | 0x0080 | 0x0003;  // قياس منفرد، 128SPS
  uint8_t w[3] = { 0x01, (uint8_t)(cfg >> 8), (uint8_t)cfg };
  int e = d->api->i2c_write(d->addr, w, 3);
  if (e) return e;
  uint8_t reg = 0x01, b[2] = { 0, 0 };
  for (int i = 0; i < 20; i++) {                         // انتظر اكتمال التحويل (بت OS)
    d->api->delay_ms(2);
    e = d->api->i2c_write_read(d->addr, &reg, 1, b, 2);
    if (e) return e;
    if (b[0] & 0x80) break;
  }
  if (!(b[0] & 0x80)) return 5;
  reg = 0x00;
  e = d->api->i2c_write_read(d->addr, &reg, 1, b, 2);
  if (e) return e;
  int16_t raw = (int16_t)(((uint16_t)b[0] << 8) | b[1]);
  drv_ret_int(&r[0], ((int32_t)raw * FS_MV[d->gain]) / 32768);
  drv_ret_int(&r[1], raw);
  *nr = 2;
  return 0;
}
static int m_gain(void* ctx, const drv_val_t* a, int na, drv_val_t* r, int* nr) {
  (void)r; (void)nr;
  if (na < 1 || a[0].v.i < 0 || a[0].v.i > 5) return DRV_E_ARGS;
  ((Adc*)ctx)->gain = (uint8_t)a[0].v.i;
  return 0;
}
static const drv_method_t METHODS[] = { { "read", "i", m_read }, { "gain", "i", m_gain } };
static const drv_ops_t OPS = { DRV_API_VERSION, NAME, "ADS1115 16-bit ADC (I2C)", METHODS, 2, do_open, do_close };
DRV_EXPORT const drv_ops_t* drv_entry(void) { return &OPS; }
