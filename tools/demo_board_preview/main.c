/* Fort Washington board preview for the Qiyun DESPI-M01VE demo board (no XIAO, no Wi-Fi).
 * Shows frames rendered on the PC by the project's own layout code (layout_v2.h) from live SEPTA data:
 * frame 0 with a full OTP refresh, then one partial refresh per minute through the rest, then holds.
 * Uses the vendor's EPAPER.c unchanged (pins: CS PD8, SCK PD9, MOSI PD10, CS2 PD12, RES PE14, D/C PE15, BUSY PE13).
 */
#include "stm32f10x.h"
#include "EPAPER.h"
#include "frames.h"

static void gpio_init(void) {
  GPIO_InitTypeDef g;
  RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOD | RCC_APB2Periph_GPIOE, ENABLE);
  g.GPIO_Pin = GPIO_Pin_8 | GPIO_Pin_9 | GPIO_Pin_10 | GPIO_Pin_12; g.GPIO_Mode = GPIO_Mode_Out_PP; g.GPIO_Speed = GPIO_Speed_50MHz;
  GPIO_Init(GPIOD, &g);
  g.GPIO_Pin = GPIO_Pin_12 | GPIO_Pin_14 | GPIO_Pin_15; GPIO_Init(GPIOE, &g);         /* PE12 = LED */
  g.GPIO_Pin = GPIO_Pin_13; g.GPIO_Mode = GPIO_Mode_IN_FLOATING; g.GPIO_Speed = GPIO_Speed_2MHz;
  GPIO_Init(GPIOE, &g);                                                               /* BUSY */
}

/* SysTick-based millisecond wait (the vendor's driver_delay_xms is an uncalibrated busy loop). */
static void wait_ms(uint32_t ms) {
  SysTick->LOAD = SystemFrequency / 1000 - 1; SysTick->VAL = 0;
  SysTick->CTRL = SysTick_CTRL_CLKSOURCE | SysTick_CTRL_ENABLE;
  while (ms--) while (!(SysTick->CTRL & SysTick_CTRL_COUNTFLAG)) {}
  SysTick->CTRL = 0;
}
static void led(int on) { if (on) GPIO_SetBits(GPIOE, GPIO_Pin_12); else GPIO_ResetBits(GPIOE, GPIO_Pin_12); }

int main(void) {
  int k;
  gpio_init();
  led(1);
  EPD_HW_Init(OTP);                 /* full refresh, built-in waveform: best contrast */
  EPD_ALL_image(FRAMES[0]);
  led(0);
  for (k = 1; k < FRAME_COUNT; k++) {
    uint32_t t;
    for (t = 0; t < FRAME_PERIOD_S; t++) { led(t & 1); wait_ms(1000); }   /* LED blinks while waiting */
    led(1);
    EPD_Partial_Init();             /* fast partial waveform, no flash */
    partial_Update_fullscreen(FRAMES[k - 1], FRAMES[k]);
    led(0);
  }
  for (;;) { led(1); wait_ms(100); led(0); wait_ms(2900); }   /* done: short blink every 3 s */
}
