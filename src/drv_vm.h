// آلة افتراضية صغيرة RV32IM تنفّذ الدرايفرات الموقّعة داخل ذاكرة خاصة بها.
// لماذا: ESP32-C3 مع Arduino بلا ذاكرة قابلة للتنفيذ (حماية الذاكرة)، فالتنفيذ الأصلي ينهار.
// الفائدة الجانبية: الدرايفر معزول — لا يقرأ ولا يكتب خارج ذاكرته، ولا يصل للعتاد إلا عبر drv_api.
// الكود هنا محمول (بلا Arduino) فيُختبر على الحاسوب: tools/drv/test_vm.cpp
#pragma once
#include <stdint.h>
#include <stddef.h>

#define DRV_GBASE     0x10000u        // عنوان بداية ذاكرة الدرايفر في نظر الدرايفر (0 = مؤشر فارغ غير صالح)
#define DRV_VM_HEAP   3072            // كومة الدرايفر (api->alloc)
#define DRV_VM_SCRATCH 1024           // منطقة تمرير المعاملات من الفيرموير
#define DRV_VM_STACK  2048
#define DRV_VM_BUDGET 8000000u        // أقصى تعليمات لكل استدعاء
#define DRV_VM_NHOST  15              // عدد خانات واجهة الفيرموير (الإصدار + 14 دالة)

struct DrvVM;
// تُستدعى حين ينادي الدرايفر دالة من api. idx = رقمها (1..14)، a = السجلات a0..a7. تُرجع قيمة a0.
typedef uint32_t (*DrvHostFn)(DrvVM* vm, int idx, const uint32_t* a);

struct DrvVM {
  uint8_t* mem; uint32_t size, imgLen;
  uint32_t apiOff, heapOff, heapEnd, scrOff, scrEnd, stackTop;
  uint32_t x[32]; uint32_t pc;
  int fault;                          // 0 سليم · -1 ذاكرة · -2 تعليمة غير صالحة · -3 تجاوز الميزانية · -4 عنوان تنفيذ خاطئ
  DrvHostFn host;
};

size_t   drv_vm_arena_size(size_t memLen);                                         // حجم الذاكرة المطلوب للدرايفر
bool     drv_vm_init(DrvVM* v, uint8_t* arena, size_t memLen, size_t imgLen, DrvHostFn host);   // arena مصفّرة بحجم drv_vm_arena_size
uint8_t* drv_vm_ptr(DrvVM* v, uint32_t g, uint32_t len);                           // عنوان الدرايفر -> مؤشر حقيقي (nullptr إن خرج عن الذاكرة)
const char* drv_vm_str(DrvVM* v, uint32_t g, uint32_t maxlen);                     // نص منتهٍ بصفر داخل الذاكرة أو nullptr
uint32_t drv_vm_alloc(DrvVM* v, uint32_t n);                                       // 0 عند الفشل
void     drv_vm_free(DrvVM* v, uint32_t g);
uint32_t drv_vm_api_addr(DrvVM* v);                                                // عنوان جدول api الذي يُمرَّر للدرايفر
int      drv_vm_call(DrvVM* v, uint32_t fn, const uint32_t* args, int nargs, uint32_t* ret);   // 0 = تم
