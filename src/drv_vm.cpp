#include "drv_vm.h"
#include <string.h>

#define HOST_BASE 0xFFFF0000u
#define STOP_ADDR 0xFFFFFFF0u

size_t drv_vm_arena_size(size_t memLen) {
  return ((memLen + 15) & ~15u) + 64 + DRV_VM_HEAP + DRV_VM_SCRATCH + DRV_VM_STACK;
}

bool drv_vm_init(DrvVM* v, uint8_t* arena, size_t memLen, size_t imgLen, DrvHostFn host) {
  if (!arena || imgLen > memLen) return false;
  memset(v, 0, sizeof(*v));
  v->mem = arena; v->imgLen = (uint32_t)imgLen; v->host = host;
  v->apiOff = (uint32_t)((memLen + 15) & ~15u);
  v->heapOff = v->apiOff + 64; v->heapEnd = v->heapOff + DRV_VM_HEAP;
  v->scrOff = v->heapEnd; v->scrEnd = v->scrOff + DRV_VM_SCRATCH;
  v->stackTop = v->scrEnd + DRV_VM_STACK; v->size = v->stackTop;
  // جدول api: [0] = الإصدار يملؤه المستدعي، ثم عناوين وهمية لدوال الفيرموير
  uint32_t* api = (uint32_t*)(arena + v->apiOff);
  for (int i = 1; i < DRV_VM_NHOST; i++) api[i] = HOST_BASE + 4u * i;
  // الكومة: كتلة حرة واحدة [حجم الحمولة, مستعملة]
  uint32_t hdr[2] = { DRV_VM_HEAP - 8, 0 };
  memcpy(arena + v->heapOff, hdr, 8);
  return true;
}

uint32_t drv_vm_api_addr(DrvVM* v) { return DRV_GBASE + v->apiOff; }

uint8_t* drv_vm_ptr(DrvVM* v, uint32_t g, uint32_t len) {
  uint32_t o = g - DRV_GBASE;
  if (o >= v->size) return len == 0 ? v->mem : nullptr;
  return len <= v->size - o ? v->mem + o : nullptr;
}

const char* drv_vm_str(DrvVM* v, uint32_t g, uint32_t maxlen) {
  uint32_t o = g - DRV_GBASE;
  if (o >= v->size) return nullptr;
  uint32_t room = v->size - o; if (room > maxlen) room = maxlen;
  for (uint32_t i = 0; i < room; i++) if (v->mem[o + i] == 0) return (const char*)(v->mem + o);
  return nullptr;
}

// كومة بسيطة: first-fit مع تقسيم ودمج الكتل الحرة المتجاورة
uint32_t drv_vm_alloc(DrvVM* v, uint32_t n) {
  if (n == 0 || n > DRV_VM_HEAP) return 0;
  n = (n + 7) & ~7u;
  for (uint32_t o = v->heapOff; o + 8 <= v->heapEnd;) {
    uint32_t sz, used; memcpy(&sz, v->mem + o, 4); memcpy(&used, v->mem + o + 4, 4);
    if (!used) {
      // دمج الحرّات التالية
      for (;;) {
        uint32_t nx = o + 8 + sz; if (nx + 8 > v->heapEnd) break;
        uint32_t s2, u2; memcpy(&s2, v->mem + nx, 4); memcpy(&u2, v->mem + nx + 4, 4);
        if (u2) break;
        sz += 8 + s2; memcpy(v->mem + o, &sz, 4);
      }
      if (sz >= n) {
        if (sz >= n + 16) {                       // تقسيم
          uint32_t rest[2] = { sz - n - 8, 0 }; memcpy(v->mem + o + 8 + n, rest, 8); sz = n;
        }
        uint32_t h[2] = { sz, 1 }; memcpy(v->mem + o, h, 8);
        memset(v->mem + o + 8, 0, sz);
        return DRV_GBASE + o + 8;
      }
    }
    o += 8 + sz;
  }
  return 0;
}

void drv_vm_free(DrvVM* v, uint32_t g) {
  uint32_t o = g - DRV_GBASE;
  if (g == 0 || o < v->heapOff + 8 || o >= v->heapEnd) return;
  uint32_t used = 0; memcpy(&used, v->mem + o - 4, 4);
  if (used == 1) { used = 0; memcpy(v->mem + o - 4, &used, 4); }
}

// ---------- المفسّر ----------
static inline int32_t sx(uint32_t v, int bits) { return (int32_t)(v << (32 - bits)) >> (32 - bits); }

int drv_vm_call(DrvVM* v, uint32_t fn, const uint32_t* args, int nargs, uint32_t* ret) {
  uint32_t* x = v->x;
  memset(x, 0, sizeof(v->x));
  for (int i = 0; i < nargs && i < 8; i++) x[10 + i] = args[i];
  x[2] = DRV_GBASE + v->stackTop; x[1] = STOP_ADDR;
  uint32_t pc = fn, budget = DRV_VM_BUDGET;
  uint8_t* M = v->mem; const uint32_t size = v->size, imgLen = v->imgLen;
  v->fault = 0;
#define FAULT(c) do { v->fault = (c); v->pc = pc; return (c); } while (0)
  for (;;) {
    uint32_t po = pc - DRV_GBASE;
    if (po > imgLen - 4 || (pc & 3) || imgLen < 4) {
      if (pc == STOP_ADDR) { if (ret) *ret = x[10]; return 0; }
      if (pc > HOST_BASE && pc < HOST_BASE + 4u * DRV_VM_NHOST && !(pc & 3)) {
        x[10] = v->host ? v->host(v, (int)((pc - HOST_BASE) / 4), &x[10]) : 0;
        pc = x[1]; continue;
      }
      FAULT(-4);
    }
    if (!budget--) FAULT(-3);
    uint32_t ins; memcpy(&ins, M + po, 4);
    uint32_t rd = (ins >> 7) & 31, f3 = (ins >> 12) & 7, rs1 = (ins >> 15) & 31, rs2 = (ins >> 20) & 31, f7 = ins >> 25;
    uint32_t a = x[rs1], b = x[rs2], npc = pc + 4, r = 0; bool wr = false;
    switch (ins & 0x7f) {
      case 0x37: r = ins & 0xfffff000u; wr = true; break;                       // LUI
      case 0x17: r = pc + (ins & 0xfffff000u); wr = true; break;                // AUIPC
      case 0x6f: {                                                              // JAL
        uint32_t imm = ((ins >> 31) << 20) | (((ins >> 12) & 0xff) << 12) | (((ins >> 20) & 1) << 11) | (((ins >> 21) & 0x3ff) << 1);
        r = pc + 4; wr = true; npc = pc + (uint32_t)sx(imm, 21); break; }
      case 0x67: r = pc + 4; wr = true; npc = (a + (uint32_t)sx(ins >> 20, 12)) & ~1u; break;   // JALR
      case 0x63: {                                                              // فروع
        uint32_t imm = ((ins >> 31) << 12) | (((ins >> 7) & 1) << 11) | (((ins >> 25) & 0x3f) << 5) | (((ins >> 8) & 0xf) << 1);
        bool t;
        switch (f3) {
          case 0: t = a == b; break; case 1: t = a != b; break;
          case 4: t = (int32_t)a < (int32_t)b; break; case 5: t = (int32_t)a >= (int32_t)b; break;
          case 6: t = a < b; break; case 7: t = a >= b; break;
          default: FAULT(-2);
        }
        if (t) npc = pc + (uint32_t)sx(imm, 13);
        break; }
      case 0x03: {                                                              // تحميل
        uint32_t ad = a + (uint32_t)sx(ins >> 20, 12), o = ad - DRV_GBASE;
        uint32_t n = (f3 & 3) == 0 ? 1 : (f3 & 3) == 1 ? 2 : 4;
        if (f3 == 3 || f3 > 5 || o >= size || n > size - o) FAULT(-1);
        uint32_t t = 0; memcpy(&t, M + o, n);
        if (f3 == 0) t = (uint32_t)sx(t, 8); else if (f3 == 1) t = (uint32_t)sx(t, 16);
        r = t; wr = true; break; }
      case 0x23: {                                                              // تخزين
        uint32_t ad = a + (uint32_t)sx(((ins >> 25) << 5) | ((ins >> 7) & 31), 12), o = ad - DRV_GBASE;
        uint32_t n = f3 == 0 ? 1 : f3 == 1 ? 2 : 4;
        if (f3 > 2 || o >= size || n > size - o) FAULT(-1);
        memcpy(M + o, &b, n); break; }
      case 0x13: {                                                              // عمليات بفوري
        uint32_t im = (uint32_t)sx(ins >> 20, 12); wr = true;
        switch (f3) {
          case 0: r = a + im; break; case 2: r = (int32_t)a < (int32_t)im; break; case 3: r = a < im; break;
          case 4: r = a ^ im; break; case 6: r = a | im; break; case 7: r = a & im; break;
          case 1: r = a << (im & 31); break;
          case 5: r = (ins >> 30) & 1 ? (uint32_t)((int32_t)a >> (im & 31)) : a >> (im & 31); break;
        }
        break; }
      case 0x33: {                                                              // عمليات سجلات
        wr = true;
        if (f7 == 1) {                                                          // امتداد M
          switch (f3) {
            case 0: r = a * b; break;
            case 1: r = (uint32_t)(((int64_t)(int32_t)a * (int64_t)(int32_t)b) >> 32); break;
            case 2: r = (uint32_t)(((int64_t)(int32_t)a * (int64_t)b) >> 32); break;
            case 3: r = (uint32_t)(((uint64_t)a * (uint64_t)b) >> 32); break;
            case 4: r = b == 0 ? 0xFFFFFFFFu : ((int32_t)a == INT32_MIN && (int32_t)b == -1) ? a : (uint32_t)((int32_t)a / (int32_t)b); break;
            case 5: r = b == 0 ? 0xFFFFFFFFu : a / b; break;
            case 6: r = b == 0 ? a : ((int32_t)a == INT32_MIN && (int32_t)b == -1) ? 0 : (uint32_t)((int32_t)a % (int32_t)b); break;
            case 7: r = b == 0 ? a : a % b; break;
          }
        } else if (f7 == 0 || f7 == 0x20) {
          switch (f3) {
            case 0: r = f7 ? a - b : a + b; break; case 1: r = a << (b & 31); break;
            case 2: r = (int32_t)a < (int32_t)b; break; case 3: r = a < b; break;
            case 4: r = a ^ b; break; case 5: r = f7 ? (uint32_t)((int32_t)a >> (b & 31)) : a >> (b & 31); break;
            case 6: r = a | b; break; case 7: r = a & b; break;
          }
        } else FAULT(-2);
        break; }
      case 0x0f: break;                                                         // FENCE / FENCE.I: لا شيء
      default: FAULT(-2);                                                       // ECALL/EBREAK وغيرها ممنوعة
    }
    if (wr && rd) x[rd] = r;
    pc = npc;
  }
#undef FAULT
}
