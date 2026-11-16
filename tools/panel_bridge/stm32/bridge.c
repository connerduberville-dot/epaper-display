/* Panel bridge for the Qiyun DESPI-M01VE demo board (STM32F103VET6).
 * Lets a PC drive the e-paper through the board's CH340 USB-serial port, so the project's own driver
 * (firmware/SeptaBoard/epd_qyeg1085.cpp) can run on the PC against the real panel.
 *
 * Link: USART1 (PA9/PA10 via P4 jumpers -> CH340), 921600 8N1.
 * Packet from PC: 0xA5, len_lo, len_hi, <len bytes of ops>, xor(ops)
 * Reply:          0x5A, status (0 ok, 0xE1 checksum, 0xE2 timeout, 0xE3 too long, 0xE4 bad op), BUSY level, version
 * Ops:  0x01 s      set pins: bit0 CS, bit1 CS2, bit2 D/C, bit3 RES (1 = high)
 *       0x02 lo hi  then n bytes: SPI write, mode 0, MSB first (bit-banged like the vendor demo)
 *       0x03 lo hi  delay n ms
 * Panel pins (vendor EPAPER.h): CS PD8, SCK PD9, MOSI PD10, CS2 PD12, BUSY PE13, RES PE14, D/C PE15, LED PE12.
 */
#include "stm32f10x.h"

#define VERSION   1
#define BUF_MAX   4200
#define P_CS      GPIO_Pin_8
#define P_SCK     GPIO_Pin_9
#define P_MOSI    GPIO_Pin_10
#define P_CS2     GPIO_Pin_12
#define P_LED     GPIO_Pin_12
#define P_BUSY    GPIO_Pin_13
#define P_RES     GPIO_Pin_14
#define P_DC      GPIO_Pin_15

static uint8_t buf[BUF_MAX];
#define NOP() __asm volatile ("nop")

static void init(void) {
  GPIO_InitTypeDef g;
  RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOD | RCC_APB2Periph_GPIOE | RCC_APB2Periph_USART1 | RCC_APB2Periph_AFIO, ENABLE);
  GPIOD->BSRR = P_CS | P_CS2;  GPIOD->BRR = P_SCK | P_MOSI;                 /* idle: CS high, clock low */
  GPIOE->BSRR = P_RES | P_DC;
  g.GPIO_Speed = GPIO_Speed_50MHz; g.GPIO_Mode = GPIO_Mode_Out_PP;
  g.GPIO_Pin = P_CS | P_SCK | P_MOSI | P_CS2; GPIO_Init(GPIOD, &g);
  g.GPIO_Pin = P_LED | P_RES | P_DC;          GPIO_Init(GPIOE, &g);
  g.GPIO_Mode = GPIO_Mode_IN_FLOATING; g.GPIO_Pin = P_BUSY; GPIO_Init(GPIOE, &g);
  g.GPIO_Mode = GPIO_Mode_AF_PP;       g.GPIO_Pin = GPIO_Pin_9;  GPIO_Init(GPIOA, &g);   /* TX */
  g.GPIO_Mode = GPIO_Mode_IN_FLOATING; g.GPIO_Pin = GPIO_Pin_10; GPIO_Init(GPIOA, &g);   /* RX */
  USART1->CR1 = 0; USART1->CR2 = 0; USART1->CR3 = 0;
  USART1->BRR = (SystemFrequency + 921600 / 2) / 921600;                  /* 72 MHz -> 78 -> 923 kbaud, 0.16 % */
  USART1->CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE;
}

static void wait_ms(uint32_t ms) {
  SysTick->LOAD = SystemFrequency / 1000 - 1; SysTick->VAL = 0;
  SysTick->CTRL = SysTick_CTRL_CLKSOURCE | SysTick_CTRL_ENABLE;
  while (ms--) while (!(SysTick->CTRL & SysTick_CTRL_COUNTFLAG)) {}
  SysTick->CTRL = 0;
}

static void putc_(uint8_t c) { while (!(USART1->SR & USART_SR_TXE)) {} USART1->DR = c; }
static int getc_wait(void) {                       /* blocks forever: waiting for the next packet */
  for (;;) {
    uint16_t sr = USART1->SR;
    if (sr & (USART_SR_ORE | USART_SR_FE | USART_SR_NE)) { (void)USART1->DR; continue; }
    if (sr & USART_SR_RXNE) return USART1->DR & 0xFF;
  }
}
static int getc_to(void) {                         /* inside a packet: give up after ~50 ms */
  uint32_t n = 900000;
  while (n--) {
    uint16_t sr = USART1->SR;
    if (sr & USART_SR_RXNE) return USART1->DR & 0xFF;
    if (sr & (USART_SR_ORE | USART_SR_FE | USART_SR_NE)) { (void)USART1->DR; return -1; }
  }
  return -1;
}

static void spi_byte(uint8_t b) {
  int i;
  for (i = 0; i < 8; i++) {
    if (b & 0x80) GPIOD->BSRR = P_MOSI; else GPIOD->BRR = P_MOSI;
    NOP(); NOP(); NOP();
    GPIOD->BSRR = P_SCK; NOP(); NOP(); NOP(); NOP();
    GPIOD->BRR = P_SCK;
    b <<= 1;
  }
  NOP(); NOP();
}

static void set_pins(uint8_t s) {
  GPIOD->BSRR = (s & 1) ? P_CS  : (uint32_t)P_CS  << 16;
  GPIOD->BSRR = (s & 2) ? P_CS2 : (uint32_t)P_CS2 << 16;
  GPIOE->BSRR = (s & 4) ? P_DC  : (uint32_t)P_DC  << 16;
  GPIOE->BSRR = (s & 8) ? P_RES : (uint32_t)P_RES << 16;
  NOP(); NOP(); NOP(); NOP();
}

static uint8_t run(uint16_t len) {
  uint16_t i = 0;
  while (i < len) {
    uint8_t op = buf[i++];
    if (op == 0x01) { if (i >= len) return 0xE4; set_pins(buf[i++]); }
    else if (op == 0x02 || op == 0x03) {
      uint16_t n;
      if (i + 2 > len) return 0xE4;
      n = buf[i] | (buf[i + 1] << 8); i += 2;
      if (op == 0x03) { wait_ms(n); continue; }
      if (i + n > len) return 0xE4;
      while (n--) spi_byte(buf[i++]);
    }
    else return 0xE4;
  }
  return 0;
}

static void reply(uint8_t status) {
  putc_(0x5A); putc_(status); putc_((GPIOE->IDR & P_BUSY) ? 1 : 0); putc_(VERSION);
}

int main(void) {
  uint32_t packets = 0;
  init();
  for (;;) {
    int c, lo, hi, ck; uint16_t len, i; uint8_t x = 0;
    if (getc_wait() != 0xA5) continue;
    if ((lo = getc_to()) < 0 || (hi = getc_to()) < 0) { reply(0xE2); continue; }
    len = (uint16_t)(lo | (hi << 8));
    if (len > BUF_MAX) { reply(0xE3); continue; }
    for (i = 0; i < len; i++) { if ((c = getc_to()) < 0) break; buf[i] = (uint8_t)c; x ^= (uint8_t)c; }
    if (i < len || (ck = getc_to()) < 0) { reply(0xE2); continue; }
    if ((uint8_t)ck != x) { reply(0xE1); continue; }
    reply(run(len));
    if ((++packets & 63) == 0) GPIOE->ODR ^= P_LED;          /* LED flickers while the PC is drawing */
  }
}
