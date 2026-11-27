// sht31: حساس حرارة ورطوبة Sensirion SHT3x عبر I2C (مع فحص CRC)
// cfg: addr=0x44 (أو 0x45)
// الدوال: read() -> temp_c_x100, hum_x100 · reset()
#include "drvrt.h"

typedef struct { const drv_api_t* api; uint8_t addr; } Sht;
static const char NAME[] = "sht31";

static uint8_t crc8(const uint8_t* d, int n) {
  uint8_t c = 0xFF;
  for (int i = 0; i < n; i++) { c ^= d[i]; for (int b = 0; b < 8; b++) c = (c & 0x80) ? (uint8_t)((c << 1) ^ 0x31) : (uint8_t)(c << 1); }
  return c;
}
static int do_open(void** ctx, const drv_api_t* api, const drv_kv_t* cfg, int n) {
  Sht* d = (Sht*)api->alloc(sizeof(Sht));
  if (!d) return 1;
  d->api = api;
  d->addr = (uint8_t)drv_cfg_int(cfg, n, "addr", 0x44);
  uint8_t soft[2] = { 0x30, 0xA2 };                    // إعادة ضبط برمجية (تتحقق من وجود الجهاز)
  int e = api->i2c_write(d->addr, soft, 2);
  if (e) { api->release(d); return e; }
  api->delay_ms(2);
  *ctx = d;
  return 0;
}
static void do_close(void* ctx) { ((Sht*)ctx)->api->release(ctx); }

static int m_read(void* ctx, const drv_val_t* a, int na, drv_val_t* r, int* nr) {
  (void)a; (void)na;
  Sht* d = (Sht*)ctx; uint8_t cmd[2] = { 0x24, 0x00 }, b[6];   // قياس مرة واحدة، دقة عالية
  int e = d->api->i2c_write(d->addr, cmd, 2);
  if (e) return e;
  d->api->delay_ms(20);
  e = d->api->i2c_read(d->addr, b, 6);
  if (e) return e;
  if (crc8(b, 2) != b[2] || crc8(b + 3, 2) != b[5]) return 6;   // بيانات تالفة
  uint32_t t = ((uint32_t)b[0] << 8) | b[1], h = ((uint32_t)b[3] << 8) | b[4];
  drv_ret_int(&r[0], (int32_t)((17500u * t) / 65535u) - 4500);  // T = -45 + 175*raw/65535
  drv_ret_int(&r[1], (int32_t)((10000u * h) / 65535u));         // RH = 100*raw/65535
  *nr = 2;
  return 0;
}
static int m_reset(void* ctx, const drv_val_t* a, int na, drv_val_t* r, int* nr) {
  (void)a; (void)na; (void)r; (void)nr;
  Sht* d = (Sht*)ctx; uint8_t soft[2] = { 0x30, 0xA2 };
  return d->api->i2c_write(d->addr, soft, 2);
}
static const drv_method_t METHODS[] = { { "read", "", m_read }, { "reset", "", m_reset } };
static const drv_ops_t OPS = { DRV_API_VERSION, NAME, "SHT3x temp/hum (I2C)", METHODS, 2, do_open, do_close };
DRV_EXPORT const drv_ops_t* drv_entry(void) { return &OPS; }
