#include "drvrt.h"
void* memcpy(void* d, const void* s, size_t n) { uint8_t* a = (uint8_t*)d; const uint8_t* b = (const uint8_t*)s; while (n--) *a++ = *b++; return d; }
void* memset(void* d, int c, size_t n) { uint8_t* a = (uint8_t*)d; while (n--) *a++ = (uint8_t)c; return d; }
size_t strlen(const char* s) { size_t n = 0; while (s[n]) n++; return n; }
int strcmp(const char* a, const char* b) { while (*a && *a == *b) { a++; b++; } return (unsigned char)*a - (unsigned char)*b; }

// قسمة 64 بت (RV32 لا يملك تعليمة قسمة 64 بت، والمترجم يستدعي هذه الدوال)
unsigned long long __udivmoddi4(unsigned long long n, unsigned long long d, unsigned long long* rem) {
  unsigned long long q = 0, r = 0;
  if (d == 0) { if (rem) *rem = n; return ~0ull; }
  for (int i = 63; i >= 0; i--) {
    r = (r << 1) | ((n >> i) & 1);
    if (r >= d) { r -= d; q |= 1ull << i; }
  }
  if (rem) *rem = r;
  return q;
}
unsigned long long __udivdi3(unsigned long long n, unsigned long long d) { return __udivmoddi4(n, d, 0); }
unsigned long long __umoddi3(unsigned long long n, unsigned long long d) { unsigned long long r; __udivmoddi4(n, d, &r); return r; }
long long __divdi3(long long n, long long d) {
  int neg = (n < 0) ^ (d < 0);
  unsigned long long q = __udivmoddi4(n < 0 ? 0ull - (unsigned long long)n : (unsigned long long)n, d < 0 ? 0ull - (unsigned long long)d : (unsigned long long)d, 0);
  return neg ? (long long)(0ull - q) : (long long)q;
}
long long __moddi3(long long n, long long d) {
  unsigned long long r;
  __udivmoddi4(n < 0 ? 0ull - (unsigned long long)n : (unsigned long long)n, d < 0 ? 0ull - (unsigned long long)d : (unsigned long long)d, &r);
  return n < 0 ? (long long)(0ull - r) : (long long)r;
}
