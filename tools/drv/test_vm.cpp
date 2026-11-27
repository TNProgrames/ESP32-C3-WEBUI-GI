// اختبار المفسّر على الحاسوب: يحمّل كل .drv، يفتحه، ويستدعي كل دواله بمعاملات نموذجية.
// البناء: g++ -std=gnu++17 -O2 -Isrc tools/drv/test_vm.cpp src/drv_vm.cpp -o /tmp/test_vm && /tmp/test_vm drivers/apps/*.drv
#include "drv_vm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include <string>

static int g_i2c_writes = 0, g_spi_writes = 0, g_logs = 0;
static uint32_t host(DrvVM* v, int idx, const uint32_t* a) {
  switch (idx) {
    case 1: { const char* s = drv_vm_str(v, a[0], 200); if (s) g_logs++; return 0; }
    case 2: return drv_vm_alloc(v, a[0]);
    case 3: drv_vm_free(v, a[0]); return 0;
    case 4: return 12345;
    case 5: case 6: return 0;
    case 7: return drv_vm_ptr(v, a[1], a[2]) ? (g_i2c_writes++, 0) : (uint32_t)-2;
    case 8: { uint8_t* p = drv_vm_ptr(v, a[1], a[2]); if (!p) return (uint32_t)-2; memset(p, 0x58, a[2]); return 0; }   // chip id 0x58 = BMP280
    case 9: { uint8_t* p = drv_vm_ptr(v, a[3], a[4]); if (!p || !drv_vm_ptr(v, a[1], a[2])) return (uint32_t)-2; memset(p, 0x58, a[4]); return 0; }
    case 10: return drv_vm_ptr(v, a[3], a[4]) ? (g_spi_writes++, 0) : (uint32_t)-2;
    case 11: return 0;
    case 12: case 13: return 0;
    case 14: return 0;
  }
  return (uint32_t)-2;
}
static uint32_t rd32(const uint8_t* p) { uint32_t v; memcpy(&v, p, 4); return v; }
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); return 1; } } while (0)

static int test(const char* path) {
  FILE* f = fopen(path, "rb"); if (!f) { printf("cannot open %s\n", path); return 1; }
  std::vector<uint8_t> b(32768); size_t n = fread(b.data(), 1, b.size(), f); fclose(f);
  uint32_t entry = rd32(&b[8]), imgLen = rd32(&b[12]), memLen = rd32(&b[16]), nrel = rd32(&b[20]);
  std::vector<uint8_t> arena(drv_vm_arena_size(memLen), 0);
  memcpy(arena.data(), &b[64], imgLen);
  for (uint32_t i = 0; i < nrel; i++) { uint32_t off = rd32(&b[64 + imgLen + 4 * i]); uint32_t v = rd32(&arena[off]) + DRV_GBASE; memcpy(&arena[off], &v, 4); }
  DrvVM vm; CHECK(drv_vm_init(&vm, arena.data(), memLen, imgLen, host), "init");
  uint32_t ops = 0;
  CHECK(drv_vm_call(&vm, DRV_GBASE + entry, nullptr, 0, &ops) == 0, "entry fault %d at 0x%x", vm.fault, vm.pc);
  const uint8_t* o = drv_vm_ptr(&vm, ops, 28); CHECK(o, "ops pointer");
  const char* name = drv_vm_str(&vm, rd32(o + 4), 32); CHECK(name, "name");
  int nm = (int)rd32(o + 16); uint32_t mt = rd32(o + 12), fopen_ = rd32(o + 20), fclose_ = rd32(o + 24);
  printf("%-8s file=%zuB img=%u mem=%u relocs=%u methods=%d\n", name, n, imgLen, memLen, nrel, nm);
  // api
  uint32_t apiAddr = drv_vm_api_addr(&vm); uint32_t ver = 2; memcpy(drv_vm_ptr(&vm, apiAddr, 4), &ver, 4);
  // cfg: i2c-ish أرجل ثابتة تناسب كل الدرايفرات
  const char* keys[] = { "cs", "dc", "rst", "bl" }; int nk = 4;
  uint32_t kvAddr = DRV_GBASE + vm.scrOff, strAddr = kvAddr + 12 * nk;
  uint8_t* kv = drv_vm_ptr(&vm, kvAddr, 12 * nk);
  for (int i = 0; i < nk; i++) {
    uint32_t k = strAddr; memcpy(drv_vm_ptr(&vm, k, strlen(keys[i]) + 1), keys[i], strlen(keys[i]) + 1); strAddr += 8;
    uint8_t val[8] = { 2 /*INT*/, 0, 0, 0, (uint8_t)(4 + i), 0, 0, 0 };
    memcpy(kv + 12 * i, &k, 4); memcpy(kv + 12 * i + 4, val, 8);
  }
  uint32_t ctxSlot = DRV_GBASE + vm.scrOff + 600, rc = 0;
  uint32_t oa[4] = { ctxSlot, apiAddr, kvAddr, (uint32_t)nk };
  CHECK(drv_vm_call(&vm, fopen_, oa, 4, &rc) == 0, "open fault %d pc=0x%x", vm.fault, vm.pc);
  CHECK(rc == 0, "open rc=%d", (int)rc);
  uint32_t ctx; memcpy(&ctx, drv_vm_ptr(&vm, ctxSlot, 4), 4);
  for (int m = 0; m < nm; m++) {
    const uint8_t* me = drv_vm_ptr(&vm, mt + 12 * m, 12);
    const char* mn = drv_vm_str(&vm, rd32(me), 32); const char* sg = drv_vm_str(&vm, rd32(me + 4), 16);
    uint32_t argBase = DRV_GBASE + vm.scrOff + 100; uint8_t* ap = drv_vm_ptr(&vm, argBase, 64);
    uint32_t sAddr = DRV_GBASE + vm.scrOff + 300; memcpy(drv_vm_ptr(&vm, sAddr, 8), "Hi 12\0\0", 8);
    int na = (int)strlen(sg);
    for (int i = 0; i < na; i++) {
      uint8_t t = 2; uint16_t len = 0; uint32_t val = 3 + i;
      char c = (char)tolower(sg[i]);
      if (c == 's') { t = 3; len = 5; val = sAddr; } else if (c == 'b') { t = 1; val = 1; } else if (c == 'd') { t = 4; len = 5; val = sAddr; }
      memcpy(ap + 8 * i, &t, 1); memcpy(ap + 8 * i + 2, &len, 2); memcpy(ap + 8 * i + 4, &val, 4);
    }
    uint32_t retAddr = DRV_GBASE + vm.scrOff + 400, nretAddr = DRV_GBASE + vm.scrOff + 480, r2 = 0;
    uint32_t ca[5] = { ctx, argBase, (uint32_t)na, retAddr, nretAddr };
    int before = g_i2c_writes + g_spi_writes;
    int fr = drv_vm_call(&vm, rd32(me + 8), ca, 5, &r2);
    CHECK(fr == 0, "%s.%s fault %d pc=0x%x", name, mn, vm.fault, vm.pc);
    printf("   %s(%s) -> rc=%d io+%d\n", mn, sg, (int)r2, g_i2c_writes + g_spi_writes - before);
  }
  uint32_t ca[1] = { ctx };
  CHECK(drv_vm_call(&vm, fclose_, ca, 1, &rc) == 0, "close fault");
  return 0;
}

int main(int argc, char** argv) {
  int bad = 0;
  for (int i = 1; i < argc; i++) bad += test(argv[i]);
  // اختبار الحماية: حلقة لا نهائية يجب أن تقطعها الميزانية، وعنوان خارج الذاكرة يجب أن يُمنع
  { uint8_t code[8] = { 0x6f, 0x00, 0x00, 0x00, 0, 0, 0, 0 };           // jal x0, 0  (حلقة)
    std::vector<uint8_t> a(drv_vm_arena_size(16), 0); memcpy(a.data(), code, 8);
    DrvVM vm; drv_vm_init(&vm, a.data(), 16, 8, host);
    if (drv_vm_call(&vm, DRV_GBASE, nullptr, 0, nullptr) != -3) { printf("FAIL: budget not enforced\n"); bad++; } else printf("budget OK\n");
    uint8_t st[8] = { 0x23, 0x20, 0x00, 0x00, 0, 0, 0, 0 };           // sw x0, 0(x0) -> كتابة على العنوان 0
    memcpy(a.data(), st, 8); drv_vm_init(&vm, a.data(), 16, 8, host);
    if (drv_vm_call(&vm, DRV_GBASE, nullptr, 0, nullptr) != -1) { printf("FAIL: wild store allowed\n"); bad++; } else printf("memory fence OK\n"); }
  printf(bad ? "VM TESTS FAILED\n" : "VM OK\n");
  return bad != 0;
}
