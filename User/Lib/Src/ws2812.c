#include "ws2812.h"
#include <string.h>

#ifndef WS2812_MAX_INSTANCES
#define WS2812_MAX_INSTANCES 4U
#endif

/* 注册对象用于让共享的 HAL DMA 完成回调找到对应灯带对象。 */
static WS2812 *ws2812_instances[WS2812_MAX_INSTANCES];

static uint32_t ws2812_required_buffer_length(uint16_t led_count, uint16_t reset_slots)
{
  return ((uint32_t)led_count * WS2812_BITS_PER_LED) + reset_slots;
}

static HAL_StatusTypeDef ws2812_register_instance(WS2812 *self)
{
  /* 每个活动 WS2812 对象占一个槽位；重复注册同一个对象直接视为成功。 */
  uint32_t free_index = WS2812_MAX_INSTANCES;

  for (uint32_t i = 0; i < WS2812_MAX_INSTANCES; i++)
  {
    if (ws2812_instances[i] == self)
    {
      return HAL_OK;
    }

    if ((ws2812_instances[i] == NULL) && (free_index == WS2812_MAX_INSTANCES))
    {
      free_index = i;
    }
  }

  if (free_index == WS2812_MAX_INSTANCES)
  {
    return HAL_ERROR;
  }

  ws2812_instances[free_index] = self;
  return HAL_OK;
}

static void ws2812_unregister_instance(WS2812 *self)
{
  for (uint32_t i = 0; i < WS2812_MAX_INSTANCES; i++)
  {
    if (ws2812_instances[i] == self)
    {
      ws2812_instances[i] = NULL;
    }
  }
}

static uint8_t ws2812_scale_color(uint8_t value, uint8_t brightness)
{
  return (uint8_t)(((uint16_t)value * brightness) / WS2812_MAX_BRIGHTNESS);
}

static void ws2812_get_ordered_color(const WS2812 *self,
                                     WS2812_Color color,
                                     uint8_t ordered[3])
{
  /* 亮度只在生成 DMA 帧时生效，pixels 中保存的原始颜色不被修改。 */
  uint8_t red = ws2812_scale_color(color.r, self->brightness);
  uint8_t green = ws2812_scale_color(color.g, self->brightness);
  uint8_t blue = ws2812_scale_color(color.b, self->brightness);

  switch (self->color_order)
  {
    case WS2812_COLOR_ORDER_RGB:
      ordered[0] = red;
      ordered[1] = green;
      ordered[2] = blue;
      break;

    case WS2812_COLOR_ORDER_BRG:
      ordered[0] = blue;
      ordered[1] = red;
      ordered[2] = green;
      break;

    case WS2812_COLOR_ORDER_BGR:
      ordered[0] = blue;
      ordered[1] = green;
      ordered[2] = red;
      break;

    case WS2812_COLOR_ORDER_RBG:
      ordered[0] = red;
      ordered[1] = blue;
      ordered[2] = green;
      break;

    case WS2812_COLOR_ORDER_GBR:
      ordered[0] = green;
      ordered[1] = blue;
      ordered[2] = red;
      break;

    case WS2812_COLOR_ORDER_GRB:
    default:
      ordered[0] = green;
      ordered[1] = red;
      ordered[2] = blue;
      break;
  }
}

static void ws2812_build_dma_buffer(WS2812 *self)
{
  uint32_t buffer_index = 0;

  /* 每个数据位编码成一个 PWM CCR 值；WS2812 按高位先发。 */
  for (uint16_t led = 0; led < self->led_count; led++)
  {
    uint8_t ordered[3];
    ws2812_get_ordered_color(self, self->pixels[led], ordered);

    for (uint32_t byte_index = 0; byte_index < 3U; byte_index++)
    {
      for (int8_t bit = 7; bit >= 0; bit--)
      {
        self->dma_buffer[buffer_index++] =
            ((ordered[byte_index] & (uint8_t)(1U << bit)) != 0U) ? self->one_pulse : self->zero_pulse;
      }
    }
  }

  /* 末尾追加一段 0 占空比 PWM，让数据线保持低电平完成复位/锁存。 */
  for (uint16_t i = 0; i < self->reset_slots; i++)
  {
    self->dma_buffer[buffer_index++] = 0;
  }
}

static void ws2812_bind_methods(WS2812 *self);

static HAL_StatusTypeDef ws2812_init(WS2812 *self,
                                     TIM_HandleTypeDef *htim,
                                     uint32_t channel,
                                     WS2812_Color *pixel_buffer,
                                     uint16_t led_capacity,
                                     uint16_t *dma_buffer,
                                     uint32_t dma_buffer_length)
{
  if ((self == NULL) || (htim == NULL) || (pixel_buffer == NULL) ||
      (dma_buffer == NULL) || (led_capacity == 0U))
  {
    return HAL_ERROR;
  }

  if (dma_buffer_length < ws2812_required_buffer_length(led_capacity, WS2812_DEFAULT_RESET_SLOTS))
  {
    return HAL_ERROR;
  }

  ws2812_bind_methods(self);
  self->htim = htim;
  self->channel = channel;
  self->pixels = pixel_buffer;
  self->dma_buffer = dma_buffer;
  self->dma_buffer_length = dma_buffer_length;
  self->led_count = led_capacity;
  self->led_capacity = led_capacity;
  self->reset_slots = WS2812_DEFAULT_RESET_SLOTS;
  self->zero_pulse = WS2812_DEFAULT_ZERO_PULSE;
  self->one_pulse = WS2812_DEFAULT_ONE_PULSE;
  self->brightness = WS2812_MAX_BRIGHTNESS;
  self->color_order = WS2812_COLOR_ORDER_GRB;
  self->busy = 0U;

  /* 初始化为确定的黑色/熄灭状态。 */
  memset(self->pixels, 0, sizeof(WS2812_Color) * led_capacity);
  memset(self->dma_buffer, 0, sizeof(uint16_t) * dma_buffer_length);

  if (ws2812_register_instance(self) != HAL_OK)
  {
    WS2812_Create(self);
    return HAL_ERROR;
  }

  return HAL_OK;
}

static HAL_StatusTypeDef ws2812_deinit(WS2812 *self)
{
  if (self == NULL)
  {
    return HAL_ERROR;
  }

  if ((self->busy != 0U) && (self->htim != NULL))
  {
    (void)HAL_TIM_PWM_Stop_DMA(self->htim, self->channel);
  }

  ws2812_unregister_instance(self);
  WS2812_Create(self);
  return HAL_OK;
}

static HAL_StatusTypeDef ws2812_set_led_count(WS2812 *self, uint16_t led_count)
{
  if ((self == NULL) || (led_count == 0U) || (led_count > self->led_capacity))
  {
    return HAL_ERROR;
  }

  if (self->busy != 0U)
  {
    return HAL_BUSY;
  }

  if (ws2812_required_buffer_length(led_count, self->reset_slots) > self->dma_buffer_length)
  {
    return HAL_ERROR;
  }

  self->led_count = led_count;
  return HAL_OK;
}

static HAL_StatusTypeDef ws2812_set_timing(WS2812 *self,
                                           uint16_t zero_pulse,
                                           uint16_t one_pulse,
                                           uint16_t reset_slots)
{
  if ((self == NULL) || (zero_pulse == 0U) || (one_pulse == 0U) || (reset_slots == 0U))
  {
    return HAL_ERROR;
  }

  if (self->busy != 0U)
  {
    return HAL_BUSY;
  }

  if (ws2812_required_buffer_length(self->led_count, reset_slots) > self->dma_buffer_length)
  {
    return HAL_ERROR;
  }

  self->zero_pulse = zero_pulse;
  self->one_pulse = one_pulse;
  self->reset_slots = reset_slots;
  return HAL_OK;
}

static void ws2812_set_color_order(WS2812 *self, WS2812_ColorOrder color_order)
{
  if (self != NULL)
  {
    self->color_order = color_order;
  }
}

static void ws2812_set_brightness(WS2812 *self, uint8_t brightness)
{
  if (self != NULL)
  {
    self->brightness = brightness;
  }
}

static uint8_t ws2812_get_brightness(const WS2812 *self)
{
  return (self != NULL) ? self->brightness : 0U;
}

static uint16_t ws2812_get_led_count(const WS2812 *self)
{
  return (self != NULL) ? self->led_count : 0U;
}

static uint8_t ws2812_is_busy(const WS2812 *self)
{
  return (self != NULL) ? self->busy : 0U;
}

static HAL_StatusTypeDef ws2812_set_pixel(WS2812 *self,
                                          uint16_t index,
                                          WS2812_Color color)
{
  if ((self == NULL) || (index >= self->led_count))
  {
    return HAL_ERROR;
  }

  self->pixels[index] = color;
  return HAL_OK;
}

static HAL_StatusTypeDef ws2812_set_pixel_rgb(WS2812 *self,
                                              uint16_t index,
                                              uint8_t red,
                                              uint8_t green,
                                              uint8_t blue)
{
  WS2812_Color color = {red, green, blue};
  return ws2812_set_pixel(self, index, color);
}

static HAL_StatusTypeDef ws2812_set_all(WS2812 *self, WS2812_Color color)
{
  if (self == NULL)
  {
    return HAL_ERROR;
  }

  for (uint16_t i = 0; i < self->led_count; i++)
  {
    self->pixels[i] = color;
  }

  return HAL_OK;
}

static HAL_StatusTypeDef ws2812_clear(WS2812 *self)
{
  WS2812_Color black = {0U, 0U, 0U};
  return ws2812_set_all(self, black);
}

static HAL_StatusTypeDef ws2812_show(WS2812 *self)
{
  HAL_StatusTypeDef status;
  uint32_t transfer_length;

  if ((self == NULL) || (self->htim == NULL) ||
      (self->pixels == NULL) || (self->dma_buffer == NULL))
  {
    return HAL_ERROR;
  }

  if (self->busy != 0U)
  {
    return HAL_BUSY;
  }

  transfer_length = ws2812_required_buffer_length(self->led_count, self->reset_slots);
  if (transfer_length > self->dma_buffer_length)
  {
    return HAL_ERROR;
  }

  ws2812_build_dma_buffer(self);
  self->busy = 1U;

  /*
   * HAL 接口参数类型是 uint32_t*，但 DMA 已配置为半字传输。
   * CCR 值本身 16 位足够，所以实际缓冲区使用 uint16_t。
   */
  status = HAL_TIM_PWM_Start_DMA(self->htim,
                                 self->channel,
                                 (uint32_t *)self->dma_buffer,
                                 transfer_length);
  if (status != HAL_OK)
  {
    self->busy = 0U;
  }

  return status;
}

static void ws2812_bind_methods(WS2812 *self)
{
  if (self == NULL)
  {
    return;
  }

  self->Init = ws2812_init;
  self->DeInit = ws2812_deinit;
  self->SetLedCount = ws2812_set_led_count;
  self->SetTiming = ws2812_set_timing;
  self->SetColorOrder = ws2812_set_color_order;
  self->SetBrightness = ws2812_set_brightness;
  self->GetBrightness = ws2812_get_brightness;
  self->GetLedCount = ws2812_get_led_count;
  self->IsBusy = ws2812_is_busy;
  self->SetPixel = ws2812_set_pixel;
  self->SetPixelRGB = ws2812_set_pixel_rgb;
  self->SetAll = ws2812_set_all;
  self->Clear = ws2812_clear;
  self->Show = ws2812_show;
}

void WS2812_Create(WS2812 *self)
{
  if (self == NULL)
  {
    return;
  }

  memset(self, 0, sizeof(*self));
  ws2812_bind_methods(self);
}

void WS2812_TimerPulseFinishedCallback(TIM_HandleTypeDef *htim)
{
  /* 最后一个复位周期发送完成后停止 PWM，让空闲时数据线保持低电平。 */
  for (uint32_t i = 0; i < WS2812_MAX_INSTANCES; i++)
  {
    WS2812 *self = ws2812_instances[i];

    if ((self != NULL) && (self->htim == htim) && (self->busy != 0U))
    {
      (void)HAL_TIM_PWM_Stop_DMA(self->htim, self->channel);
      __HAL_TIM_SET_COMPARE(self->htim, self->channel, 0U);
      self->busy = 0U;
    }
  }
}

#ifndef WS2812_NO_HAL_CALLBACK
/* 如果其他文件已经实现该 HAL 回调，请定义 WS2812_NO_HAL_CALLBACK。 */
void HAL_TIM_PWM_PulseFinishedCallback(TIM_HandleTypeDef *htim)
{
  WS2812_TimerPulseFinishedCallback(htim);
}
#endif
