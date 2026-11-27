// محمّل الدرايفرات الموقّعة (.drv) من /drivers في LittleFS إلى ذاكرة تنفيذ
#pragma once
#include <Arduino.h>
#include "drv_api.h"
#include "drv_vm.h"

#define DRV_DIR "/drivers"
#define DRV_MAX_LOADED 4
#define DRV_MAX_FILE (24 * 1024)

#define DRV_MAX_METHODS 16
#define DRV_E_FAULT 99          // الدرايفر انهار داخل المفسّر (ذاكرة/تعليمة/ميزانية)
struct DrvMethodInfo { const char* name; const char* sig; uint32_t fn; };            // name/sig تشيران إلى ذاكرة الدرايفر
struct DrvOps { const char* name; const char* info; int nmethods; DrvMethodInfo methods[DRV_MAX_METHODS]; uint32_t open, close; };
struct DrvEntry { char name[16]; char version[8]; uint32_t fileSize; uint32_t memSize; DrvOps ops; DrvVM vm; uint8_t* mem; bool native; int users; };   // native: يعمل مباشرة من ذاكرة قابلة للتنفيذ (إن وُجدت) وإلا داخل المفسّر

void drv_boot_load();                                            // عند الإقلاع: يحمّل كل /drivers/*.drv الصالحة (مع حماية من الانهيار)
bool drv_install(const uint8_t* buf, size_t len, String* err);   // يتحقق ثم يحفظ ثم يحمّل (يستبدل النسخة القديمة إن لم تكن مستعملة)
bool drv_remove(const char* name, String* err);                  // يفرغ من الذاكرة ويحذف الملف
const DrvEntry* drv_find(const char* name);
struct DrvBrief { char name[16]; char version[8]; char info[48]; };
int drv_snapshot(DrvBrief* out, int max);                        // نسخة آمنة من قائمة المحمَّل
DrvEntry* drv_acquire(const char* name);                         // يحجز الدرايفر (users++) أو nullptr
void drv_release(DrvEntry* e);                                   // يفك الحجز (users--)
// استدعاء الدرايفر (يُنفَّذ داخل المفسّر). تُرجع رمز الدرايفر (0 نجاح) أو DRV_E_ARGS أو DRV_E_FAULT
int  drv_vm_open(DrvEntry* d, const drv_kv_t* cfg, int ncfg, uint32_t* ctx);
int  drv_vm_invoke(DrvEntry* d, int method, uint32_t ctx, const drv_val_t* args, int nargs, drv_val_t* ret, int* nret);
void drv_vm_close(DrvEntry* d, uint32_t ctx);
const drv_api_t* drv_native_api();                                // جدول api الحقيقي للدرايفرات الأصلية (lua_engine.cpp)
uint32_t drv_host_call(DrvVM* vm, int idx, const uint32_t* a);   // تنفّذه طبقة الربط (lua_engine.cpp): دوال api للدرايفر
// وضع التنفيذ: "auto" (أصلي إن توفرت ذاكرة تنفيذ وإلا مفسّر) · "vm" (مفسّر دائمًا) · "native" (أصلي فقط). يُحفظ في /drivers/.mode
const char* drv_get_mode();
bool drv_set_mode(const char* mode);          // يعيد تحميل الدرايفرات غير المستعملة بالوضع الجديد
bool drv_exec_available();                    // هل توجد ذاكرة قابلة للتنفيذ؟
String drv_list_json();
String drv_list_text();
bool drv_valid_name(const char* name);
