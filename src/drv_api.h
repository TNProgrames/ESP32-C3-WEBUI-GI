// واجهة الدرايفرات الموقّعة (.drv): تُضمَّن في الفيرموير وفي كل درايفر.
// قواعد الدرايفر: C فقط، بلا float ولا مكتبات (freestanding)، وكل شيء عبر جدول drv_api_t.
#pragma once
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

#define DRV_API_VERSION 2
#define DRV_MAX_ARGS 8

enum { DRV_NIL = 0, DRV_BOOL = 1, DRV_INT = 2, DRV_STR = 3, DRV_BLOB = 4 };

typedef struct {
  uint8_t  type;
  uint8_t  _pad;
  uint16_t len;                       // لـ STR/BLOB
  union { int32_t i; const uint8_t* p; const char* s; } v;
} drv_val_t;                          // 8 بايت على RV32

typedef struct { const char* key; drv_val_t val; } drv_kv_t;

// ما يقدمه الفيرموير للدرايفر (الكود يستدعي عبر المؤشرات فقط)
typedef struct {
  uint32_t version;                                                    // = DRV_API_VERSION
  void     (*log)(const char* msg);
  void*    (*alloc)(size_t n);
  void     (*release)(void* p);
  uint32_t (*millis)(void);
  void     (*delay_ms)(uint32_t ms);
  void     (*delay_us)(uint32_t us);
  int      (*i2c_write)(uint8_t addr, const uint8_t* d, size_t n);     // 0 = نجاح
  int      (*i2c_read)(uint8_t addr, uint8_t* d, size_t n);
  int      (*i2c_write_read)(uint8_t addr, const uint8_t* tx, size_t txn, uint8_t* rx, size_t rxn);
  // SPI: الناقل يفتحه السكريبت بـ spi.begin؛ الدرايفر يحدد CS والتردد والنمط لكل نقل. 0 = نجاح
  int      (*spi_write)(uint8_t cs, uint32_t freq_hz, uint8_t mode, const uint8_t* d, size_t n);
  int      (*spi_transfer)(uint8_t cs, uint32_t freq_hz, uint8_t mode, const uint8_t* tx, uint8_t* rx, size_t n);
  // GPIO: يحجز الدبوس لسكريبت المستدعي (يُحرَّر معه). 0 = نجاح، وإلا الدبوس غير مسموح أو مملوك لغيره
  int      (*gpio_mode)(uint8_t pin, uint8_t mode);                    // 0=in, 1=out, 2=in_pullup
  int      (*gpio_write)(uint8_t pin, uint8_t val);
  int      (*gpio_read)(uint8_t pin);                                  // 0/1، أو -1 إن لم يكن الدبوس محجوزًا
} drv_api_t;

// ما يقدمه الدرايفر للفيرموير: جدول دوال + open/close
#define DRV_E_ARGS (-2)             // معاملات خاطئة (تُرجع من الدالة)

// دالة واحدة: تُرجع 0 عند النجاح، أو DRV_E_ARGS، أو رمز خطأ موجب للعتاد.
// المعاملات تحققها الفيرموير مسبقًا حسب sig؛ والقيم المعادة تُكتب في ret (حتى 8) مع *nret.
typedef int (*drv_fn_t)(void* ctx, const drv_val_t* args, int nargs, drv_val_t* ret, int* nret);

// sig: حرف لكل معامل — i عدد صحيح · s نص · b منطقي · d بيانات خام.
// الحرف الكبير (I S B D) معامل اختياري، ويأتي بعد المعاملات الإلزامية.
typedef struct { const char* name; const char* sig; drv_fn_t fn; } drv_method_t;

typedef struct {
  uint32_t version;                 // = DRV_API_VERSION
  const char* name;                 // اسم الدرايفر (= اسم الملف)
  const char* info;                 // وصف قصير
  const drv_method_t* methods;
  int nmethods;
  int  (*open)(void** ctx, const drv_api_t* api, const drv_kv_t* cfg, int ncfg);   // 0 = نجاح
  void (*close)(void* ctx);
} drv_ops_t;

// نقطة الدخول: الرمز drv_entry (يحدد الـ linker العنوان من e_entry)
typedef const drv_ops_t* (*drv_entry_t)(void);

#ifdef __cplusplus
}
#endif
