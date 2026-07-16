#ifndef __WS2812_H__
#define __WS2812_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f4xx_hal.h"
#include <stdint.h>

#ifndef Class
#define Class(class)            \
  typedef struct class class;   \
  struct class
#endif

/* 一个 WS2812 灯珠需要 24 个 PWM 周期：8 位绿、8 位红、8 位蓝。 */
#define WS2812_BITS_PER_LED             24U

/* 60 个复位周期，每个 1.25 us，总低电平约 75 us。 */
#define WS2812_DEFAULT_RESET_SLOTS      60U

/* 默认 CCR 值适用于 TIM 时钟 84 MHz、ARR = 105 - 1 的配置。 */
#define WS2812_DEFAULT_ZERO_PULSE       32U
#define WS2812_DEFAULT_ONE_PULSE        64U

#define WS2812_MAX_BRIGHTNESS           255U

/* 用于声明 DMA 缓冲区长度，缓冲区由用户自己分配。 */
#define WS2812_DMA_BUFFER_LENGTH(count) ((uint32_t)(count) * WS2812_BITS_PER_LED + WS2812_DEFAULT_RESET_SLOTS)

/* 不同灯带可能使用不同颜色字节顺序，WS2812 通常是 GRB。 */
typedef enum
{
  WS2812_COLOR_ORDER_GRB = 0,
  WS2812_COLOR_ORDER_RGB,
  WS2812_COLOR_ORDER_BRG,
  WS2812_COLOR_ORDER_BGR,
  WS2812_COLOR_ORDER_RBG,
  WS2812_COLOR_ORDER_GBR
} WS2812_ColorOrder;

/* 颜色按常用 RGB 顺序存储，方便用户设置。 */
typedef struct
{
  uint8_t r;
  uint8_t g;
  uint8_t b;
} WS2812_Color;

Class(WS2812)
{
  /* 用于生成 WS2812 波形的定时器 PWM 输出。 */
  TIM_HandleTypeDef *htim;
  uint32_t channel;

  /* 用户提供的缓冲区，本驱动不使用动态内存分配。 */
  WS2812_Color *pixels;
  uint16_t *dma_buffer;
  uint32_t dma_buffer_length;

  /* led_count 可运行时修改，但不能超过 led_capacity。 */
  uint16_t led_count;
  uint16_t led_capacity;

  /* 时序参数是 CCR 占空比值，不是纳秒时间。 */
  uint16_t reset_slots;
  uint16_t zero_pulse;
  uint16_t one_pulse;

  /* 亮度范围 0~255；0 为输出全灭，255 为原始颜色值。 */
  uint8_t brightness;
  WS2812_ColorOrder color_order;

  /* DMA 正在发送一帧数据时置位。 */
  volatile uint8_t busy;

  HAL_StatusTypeDef (*Init)(WS2812 *self,
                            TIM_HandleTypeDef *htim,
                            uint32_t channel,
                            WS2812_Color *pixel_buffer,
                            uint16_t led_capacity,
                            uint16_t *dma_buffer,
                            uint32_t dma_buffer_length);
  HAL_StatusTypeDef (*DeInit)(WS2812 *self);
  HAL_StatusTypeDef (*SetLedCount)(WS2812 *self, uint16_t led_count);
  HAL_StatusTypeDef (*SetTiming)(WS2812 *self,
                                 uint16_t zero_pulse,
                                 uint16_t one_pulse,
                                 uint16_t reset_slots);
  void (*SetColorOrder)(WS2812 *self, WS2812_ColorOrder color_order);
  void (*SetBrightness)(WS2812 *self, uint8_t brightness);
  uint8_t (*GetBrightness)(const WS2812 *self);
  uint16_t (*GetLedCount)(const WS2812 *self);
  uint8_t (*IsBusy)(const WS2812 *self);
  HAL_StatusTypeDef (*SetPixel)(WS2812 *self, uint16_t index, WS2812_Color color);
  HAL_StatusTypeDef (*SetPixelRGB)(WS2812 *self,
                                   uint16_t index,
                                   uint8_t red,
                                   uint8_t green,
                                   uint8_t blue);
  HAL_StatusTypeDef (*SetAll)(WS2812 *self, WS2812_Color color);
  HAL_StatusTypeDef (*Clear)(WS2812 *self);
  HAL_StatusTypeDef (*Show)(WS2812 *self);
};

/* 创建对象：清空成员并绑定所有方法函数指针。 */
void WS2812_Create(WS2812 *self);

/*
 * 如果应用层已经自己实现 HAL_TIM_PWM_PulseFinishedCallback，
 * 可以定义 WS2812_NO_HAL_CALLBACK，并在自己的回调中调用本函数。
 */
void WS2812_TimerPulseFinishedCallback(TIM_HandleTypeDef *htim);

#ifdef __cplusplus
}
#endif

#endif /* __WS2812_H__ */
