// pca9685: 16 قناة PWM/سيرفو عبر I2C
// cfg: addr=0x40, freq=50
// الدوال: freq(hz) · pwm(ch, duty 0..4095) · servo(ch, angle 0..180) · off(ch)
#include "drvrt.h"

typedef struct { const drv_api_t* api; uint8_t addr; uint16_t freq; } Pca;
static const char NAME[] = "pca9685";

static int wr(Pca* d, uint8_t reg, uint8_t v) { uint8_t b[2] = { reg, v }; return d->api->i2c_write(d->addr, b, 2); }

static int set_freq(Pca* d, int hz) {
  if (hz < 24 || hz > 1526) return DRV_E_ARGS;
  int pre = (int)((25000000u + (uint32_t)hz * 2048u) / ((uint32_t)hz * 4096u)) - 1;   // prescale = round(25MHz/(4096*f)) - 1
  int e = wr(d, 0x00, 0x10);                           // نوم كي يُكتب prescale
  if (!e) e = wr(d, 0xFE, (uint8_t)pre);
  if (!e) e = wr(d, 0x00, 0x00);
  if (e) return e;
  d->api->delay_ms(1);
  e = wr(d, 0x00, 0xA0);                               // إعادة تشغيل + زيادة العنوان تلقائيًا
  if (!e) d->freq = (uint16_t)hz;
  return e;
}
static int chan(Pca* d, int ch, uint16_t on, uint16_t off) {
  uint8_t b[5] = { (uint8_t)(0x06 + 4 * ch), (uint8_t)on, (uint8_t)(on >> 8), (uint8_t)off, (uint8_t)(off >> 8) };
  return d->api->i2c_write(d->addr, b, 5);
}
static int do_open(void** ctx, const drv_api_t* api, const drv_kv_t* cfg, int n) {
  Pca* d = (Pca*)api->alloc(sizeof(Pca));
  if (!d) return 1;
  d->api = api;
  d->addr = (uint8_t)drv_cfg_int(cfg, n, "addr", 0x40);
  int e = wr(d, 0x00, 0x00);                           // استيقاظ
  if (!e) e = set_freq(d, drv_cfg_int(cfg, n, "freq", 50));
  if (e) { api->release(d); return e; }
  *ctx = d;
  return 0;
}
static void do_close(void* ctx) { ((Pca*)ctx)->api->release(ctx); }

static int m_freq(void* ctx, const drv_val_t* a, int na, drv_val_t* r, int* nr) {
  (void)r; (void)nr;
  return na < 1 ? DRV_E_ARGS : set_freq((Pca*)ctx, a[0].v.i);
}
static int m_pwm(void* ctx, const drv_val_t* a, int na, drv_val_t* r, int* nr) {
  (void)r; (void)nr;
  if (na < 2 || a[0].v.i < 0 || a[0].v.i > 15 || a[1].v.i < 0 || a[1].v.i > 4095) return DRV_E_ARGS;
  int duty = a[1].v.i;
  if (duty == 0) return chan((Pca*)ctx, a[0].v.i, 0, 0x1000);        // مطفأ تمامًا
  if (duty == 4095) return chan((Pca*)ctx, a[0].v.i, 0x1000, 0);     // مضاء تمامًا
  return chan((Pca*)ctx, a[0].v.i, 0, (uint16_t)duty);
}
static int m_servo(void* ctx, const drv_val_t* a, int na, drv_val_t* r, int* nr) {
  (void)r; (void)nr;
  Pca* d = (Pca*)ctx;
  if (na < 2 || a[0].v.i < 0 || a[0].v.i > 15 || a[1].v.i < 0 || a[1].v.i > 180) return DRV_E_ARGS;
  uint32_t us = 500u + (uint32_t)a[1].v.i * 2000u / 180u;            // 0.5..2.5 ms
  uint32_t ticks = us * 4096u * d->freq / 1000000u;
  return chan(d, a[0].v.i, 0, (uint16_t)(ticks > 4095u ? 4095u : ticks));
}
static int m_off(void* ctx, const drv_val_t* a, int na, drv_val_t* r, int* nr) {
  (void)r; (void)nr;
  if (na < 1 || a[0].v.i < 0 || a[0].v.i > 15) return DRV_E_ARGS;
  return chan((Pca*)ctx, a[0].v.i, 0, 0x1000);
}
static const drv_method_t METHODS[] = { { "freq", "i", m_freq }, { "pwm", "ii", m_pwm }, { "servo", "ii", m_servo }, { "off", "i", m_off } };
static const drv_ops_t OPS = { DRV_API_VERSION, NAME, "PCA9685 16-ch PWM/servo (I2C)", METHODS, 4, do_open, do_close };
DRV_EXPORT const drv_ops_t* drv_entry(void) { return &OPS; }
