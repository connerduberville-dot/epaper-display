/* Minimal Cortex-M3 startup in C (replaces the vendor's Keil-syntax startup_stm32f10x_hd.s). */
#include <stdint.h>
#include <stddef.h>
extern uint32_t _estack, _sidata, _sdata, _edata, _sbss, _ebss;
extern void SystemInit(void);
extern int main(void);

void Reset_Handler(void) {
  uint32_t *src = &_sidata, *dst = &_sdata;
  while (dst < &_edata) *dst++ = *src++;
  for (dst = &_sbss; dst < &_ebss; ) *dst++ = 0;
  SystemInit();
  main();
  for (;;) {}
}
static void Default_Handler(void) { for (;;) {} }

__attribute__((section(".isr_vector"), used))
const void *vector_table[16] = {
  &_estack, (void *)Reset_Handler, (void *)Default_Handler, (void *)Default_Handler,
  (void *)Default_Handler, (void *)Default_Handler, (void *)Default_Handler, 0, 0, 0, 0,
  (void *)Default_Handler, (void *)Default_Handler, 0, (void *)Default_Handler, (void *)Default_Handler,
};

/* The compiler may emit calls to these even in freestanding code. Built with -fno-builtin. */
void *memset(void *d, int c, size_t n) { volatile uint8_t *p = d; while (n--) *p++ = (uint8_t)c; return d; }
void *memcpy(void *d, const void *s, size_t n) { volatile uint8_t *p = d; const uint8_t *q = s; while (n--) *p++ = *q++; return d; }
void *memmove(void *d, const void *s, size_t n) {
  volatile uint8_t *p = d; const uint8_t *q = s;
  if (p < q) while (n--) *p++ = *q++; else { p += n; q += n; while (n--) *--p = *--q; }
  return d;
}
