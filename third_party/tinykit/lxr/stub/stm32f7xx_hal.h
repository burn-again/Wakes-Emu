#pragma once
#include <stdint.h>
#include <stddef.h>
#define I2S_AUDIOFREQ_44K 44100u
#define __IO volatile
#ifndef UNUSED
#define UNUSED(x) ((void)(x))
#endif
typedef enum { NonMaskableInt_IRQn=-14, SysTick_IRQn=-1, WWDG_IRQn=0 } IRQn_Type;
#define __CM4_REV 1
#define __MPU_PRESENT 0
#define __NVIC_PRIO_BITS 4
#define __Vendor_SysTickConfig 0
#define __FPU_PRESENT 1
#if defined(__arm__)
#include "core_cm4.h"
#else
/* host build: portable versions of the few ARM intrinsics the LXR DSP uses */
#define __STATIC_INLINE static inline
static inline int32_t __SSAT(int32_t x, uint32_t n) { const int32_t hi = (1 << (n - 1)) - 1, lo = -(1 << (n - 1)); return x > hi ? hi : (x < lo ? lo : x); }
static inline int32_t __q16(int32_t v) { return v > 32767 ? 32767 : (v < -32768 ? -32768 : v); }
static inline uint32_t __QADD16(uint32_t a, uint32_t b) { int32_t lo = __q16((int16_t)a + (int16_t)b), hi = __q16((int16_t)(a >> 16) + (int16_t)(b >> 16)); return ((uint32_t)(uint16_t)hi << 16) | (uint16_t)lo; }
static inline uint32_t __QSUB16(uint32_t a, uint32_t b) { int32_t lo = __q16((int16_t)a - (int16_t)b), hi = __q16((int16_t)(a >> 16) - (int16_t)(b >> 16)); return ((uint32_t)(uint16_t)hi << 16) | (uint16_t)lo; }
static inline int32_t __QADD(int32_t a, int32_t b) { int64_t r = (int64_t)a + b; return r > 2147483647LL ? 2147483647 : (r < -2147483648LL ? (int32_t)-2147483648LL : (int32_t)r); }
static inline int32_t __QSUB(int32_t a, int32_t b) { int64_t r = (int64_t)a - b; return r > 2147483647LL ? 2147483647 : (r < -2147483648LL ? (int32_t)-2147483648LL : (int32_t)r); }
static inline uint64_t __SMLALD(uint32_t a, uint32_t b, uint64_t acc) { return (uint64_t)((int64_t)acc + (int16_t)a * (int16_t)b + (int16_t)(a >> 16) * (int16_t)(b >> 16)); }
static inline uint8_t __CLZ(uint32_t x) { return x ? (uint8_t)__builtin_clz(x) : 32; }
#define DWT ((void*)0)
#define __fp16 _Float16          /* half floats appear only in the FX headers */
#endif
typedef struct { int dummy; } RNG_HandleTypeDef;
static inline uint32_t HAL_RNG_GetRandomNumber(RNG_HandleTypeDef* h){ static uint32_t x=2463534242u; (void)h; x^=x<<13; x^=x>>17; x^=x<<5; return x; }
typedef struct { volatile uint32_t CTRL, CYCCNT; } DWT_FakeType;
static DWT_FakeType dwt_fake_inst;
#undef DWT
#define DWT (&dwt_fake_inst)
static inline uint32_t HAL_RCC_GetHCLKFreq(void){ return 64000000u; }
typedef struct { int d; } I2S_HandleTypeDef;
typedef struct { int d; } UART_HandleTypeDef;
typedef struct { int d; } SPI_HandleTypeDef;
typedef struct { int d; } I2C_HandleTypeDef;
typedef struct { int d; } TIM_HandleTypeDef;
typedef struct { int d; } DMA_HandleTypeDef;
typedef struct { int d; } ADC_HandleTypeDef;
typedef struct { int d; } SD_HandleTypeDef;
typedef struct { int d; } RTC_HandleTypeDef;
typedef struct { int d; } QSPI_HandleTypeDef;
typedef struct { int d; } SDRAM_HandleTypeDef;
typedef struct { int d; } PCD_HandleTypeDef;
typedef struct { int d; } CRC_HandleTypeDef;
typedef struct { int d; } GPIO_TypeDef; typedef int HAL_StatusTypeDef;
static GPIO_TypeDef gpio_fake[11];
#define GPIOA (&gpio_fake[0])
#define GPIOB (&gpio_fake[1])
#define GPIOC (&gpio_fake[2])
#define GPIOD (&gpio_fake[3])
#define GPIOE (&gpio_fake[4])
#define GPIO_PIN_0 ((uint16_t)(1u<<0))
#define GPIO_PIN_1 ((uint16_t)(1u<<1))
#define GPIO_PIN_2 ((uint16_t)(1u<<2))
#define GPIO_PIN_3 ((uint16_t)(1u<<3))
#define GPIO_PIN_4 ((uint16_t)(1u<<4))
#define GPIO_PIN_5 ((uint16_t)(1u<<5))
#define GPIO_PIN_6 ((uint16_t)(1u<<6))
#define GPIO_PIN_7 ((uint16_t)(1u<<7))
#define GPIO_PIN_8 ((uint16_t)(1u<<8))
#define GPIO_PIN_9 ((uint16_t)(1u<<9))
#define GPIO_PIN_10 ((uint16_t)(1u<<10))
#define GPIO_PIN_11 ((uint16_t)(1u<<11))
#define GPIO_PIN_12 ((uint16_t)(1u<<12))
#define GPIO_PIN_13 ((uint16_t)(1u<<13))
#define GPIO_PIN_14 ((uint16_t)(1u<<14))
#define GPIO_PIN_15 ((uint16_t)(1u<<15))
typedef enum { GPIO_PIN_RESET=0, GPIO_PIN_SET } GPIO_PinState;
static inline void HAL_GPIO_WritePin(GPIO_TypeDef* g,uint16_t p,GPIO_PinState s){(void)g;(void)p;(void)s;}
static inline GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef* g,uint16_t p){(void)g;(void)p;return GPIO_PIN_RESET;}
static inline void HAL_GPIO_TogglePin(GPIO_TypeDef* g,uint16_t p){(void)g;(void)p;}
