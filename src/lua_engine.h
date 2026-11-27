#pragma once
#include <Arduino.h>

// محرك Lua: مهمة FreeRTOS واحدة تملك كل حالات Lua (حالة معزولة لكل سكريبت)
void luaeng_begin();
// تشغيل سكريبت من نص (يُنسخ داخلياً). يستبدل سكريبتاً يحمل نفس الاسم. يعيد false عند خطأ في الطلب
bool luaeng_run(const char* name, const char* code, size_t len);
bool luaeng_run_file(const char* name);   // يقرأ /scripts/<name>.lua
bool luaeng_stop(const char* name);
// حفظ السكريبت كملف /scripts/<name>.lua (مع مسار صفحته اختياريًا) والتشغيل التلقائي عند الإقلاع
bool luaeng_save(const char* name, const char* code, size_t len, const char* page);
bool luaeng_set_auto(const char* name, bool on);
bool luaeng_delete(const char* name);
String luaeng_files_json();
bool luaeng_save_lib(const char* name, const char* code, size_t len);   // /scripts/lib/<name>.lua لـ require
bool luaeng_delete_lib(const char* name);
String luaeng_libs_json();
bool luaeng_bus_publish(const char* topic, const char* text);
String luaeng_storage_dump(const char* name);
void luaeng_stop_all();
String luaeng_list_text();                // عرض نصي للطرفية
String luaeng_list_json();                // JSON للواجهة
bool luaeng_valid_name(const char* name);

// لوحات الواجهة: تعريفها JSON، وتحديثاتها عبر الاستطلاع، وأحداث المستخدم تُرسل للسكريبت
String luaeng_ui_panels_json();
String luaeng_ui_poll_json(uint32_t since);
bool luaeng_ui_event(int panel, const char* name, const char* valJson);

// يُنفَّذ في main.cpp
const char* luaeng_fw_version();
void logPrintf(const char* fmt, ...);
bool requestDeepSleep(uint32_t secs, uint32_t mask, int level);   // نوم عميق مؤجل 0.8s
const char* wakeReason(int* pin);                                  // "timer" | "gpio" | "reset"
