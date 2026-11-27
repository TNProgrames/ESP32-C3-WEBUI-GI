// مساعدات للدرايفرات (بدون مكتبة C): نصوص وذاكرة وقراءة المعاملات
#pragma once
#include "drv_api.h"

void*  memcpy(void* d, const void* s, size_t n);
void*  memset(void* d, int c, size_t n);
size_t strlen(const char* s);
int    strcmp(const char* a, const char* b);

#define DRV_EXPORT __attribute__((used, visibility("default")))




static inline int32_t drv_cfg_int(const drv_kv_t* c, int n, const char* key, int32_t def) {
  for (int i = 0; i < n; i++) if (!strcmp(c[i].key, key) && c[i].val.type == DRV_INT) return c[i].val.v.i;
  return def;
}
static inline int drv_cfg_bool(const drv_kv_t* c, int n, const char* key, int def) {
  for (int i = 0; i < n; i++) if (!strcmp(c[i].key, key) && c[i].val.type == DRV_BOOL) return c[i].val.v.i ? 1 : 0;
  return def;
}
static inline const char* drv_cfg_str(const drv_kv_t* c, int n, const char* key, const char* def) {
  for (int i = 0; i < n; i++) if (!strcmp(c[i].key, key) && c[i].val.type == DRV_STR) return c[i].val.v.s;
  return def;
}
static inline void drv_ret_int(drv_val_t* r, int32_t v) { r->type = DRV_INT; r->len = 0; r->v.i = v; }
static inline void drv_ret_bool(drv_val_t* r, int v) { r->type = DRV_BOOL; r->len = 0; r->v.i = v ? 1 : 0; }
static inline void drv_ret_str(drv_val_t* r, const char* s) { r->type = DRV_STR; r->len = (uint16_t)strlen(s); r->v.s = s; }
