// mpu6050: مقياس تسارع ودوران 6 محاور InvenSense MPU-6050 عبر I2C (±2g، ±250°/s)
// cfg: addr=0x68
// الدوال: read() -> ax,ay,az (mg), gx,gy,gz (0.1°/s) · temp() -> temp_c_x100
#include "drvrt.h"

typedef struct { const drv_api_t* api; uint8_t addr; } Imu;
static const char NAME[] = "mpu6050";

static int16_t be16(const uint8_t* p) { return (int16_t)(((uint16_t)p[0] << 8) | p[1]); }

static int do_open(void** ctx, const drv_api_t* api, const drv_kv_t* cfg, int n) {
  Imu* d = (Imu*)api->alloc(sizeof(Imu));
  if (!d) return 1;
  d->api = api;
  d->addr = (uint8_t)drv_cfg_int(cfg, n, "addr", 0x68);
  uint8_t reg = 0x75, who;
  int e = api->i2c_write_read(d->addr, &reg, 1, &who, 1);          // WHO_AM_I: تحقق من وجود الجهاز
  if (e) { api->release(d); return e; }
  uint8_t wake[2] = { 0x6B, 0x00 };                                // إخراجه من وضع النوم
  e = api->i2c_write(d->addr, wake, 2);
  if (e) { api->release(d); return e; }
  api->delay_ms(50);
  *ctx = d;
  return 0;
}
static void do_close(void* ctx) { ((Imu*)ctx)->api->release(ctx); }

static int m_read(void* ctx, const drv_val_t* a, int na, drv_val_t* r, int* nr) {
  (void)a; (void)na;
  Imu* d = (Imu*)ctx; uint8_t reg = 0x3B, b[14];
  int e = d->api->i2c_write_read(d->addr, &reg, 1, b, 14);
  if (e) return e;
  for (int i = 0; i < 3; i++) drv_ret_int(&r[i], ((int32_t)be16(b + 2 * i) * 125) / 2048);          // raw/16384 g → mg
  for (int i = 0; i < 3; i++) drv_ret_int(&r[3 + i], ((int32_t)be16(b + 8 + 2 * i) * 10) / 131);    // raw/131 °/s → x10
  *nr = 6;
  return 0;
}
static int m_temp(void* ctx, const drv_val_t* a, int na, drv_val_t* r, int* nr) {
  (void)a; (void)na;
  Imu* d = (Imu*)ctx; uint8_t reg = 0x41, b[2];
  int e = d->api->i2c_write_read(d->addr, &reg, 1, b, 2);
  if (e) return e;
  drv_ret_int(&r[0], ((int32_t)be16(b) * 100) / 340 + 3653);       // T = raw/340 + 36.53
  *nr = 1;
  return 0;
}
static const drv_method_t METHODS[] = { { "read", "", m_read }, { "temp", "", m_temp } };
static const drv_ops_t OPS = { DRV_API_VERSION, NAME, "MPU6050 accel/gyro (I2C)", METHODS, 2, do_open, do_close };
DRV_EXPORT const drv_ops_t* drv_entry(void) { return &OPS; }
