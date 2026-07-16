#include "ws2812.h"
#include <string.h>

#ifndef WS2812_MAX_INSTANCES
#define WS2812_MAX_INSTANCES 4U
#endif

/* 注册对象用于让共享的 HAL DMA 完成回调找到对应灯带对象。 */
static WS2812_HandleTypeDef *ws2812_instances[WS2812_MAX_INSTANCES];

static uint32_t WS2812_RequiredBufferLength(uint16_t led_count, uint16_t reset_slots)
{
  return ((uint32_t)led_count * WS2812_BITS_PER_LED) + reset_slots;
}

static HAL_StatusTypeDef WS2812_RegisterInstance(WS2812_HandleTypeDef *handle)
{
  /* 每个活动 WS2812 对象占一个槽位；重复注册同一个对象直接视为成功。 */
  uint32_t free_index = WS2812_MAX_INSTANCES;

  for (uint32_t i = 0; i < WS2812_MAX_INSTANCES; i++)
  {
    if (ws2812_instances[i] == handle)
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

  ws2812_instances[free_index] = handle;
  return HAL_OK;
}

static void WS2812_UnregisterInstance(WS2812_HandleTypeDef *handle)
{
  for (uint32_t i = 0; i < WS2812_MAX_INSTANCES; i++)
  {
    if (ws2812_instances[i] == handle)
    {
      ws2812_instances[i] = NULL;
    }
  }
}

static uint8_t WS2812_ScaleColor(uint8_t value, uint8_t brightness)
{
  return (uint8_t)(((uint16_t)value * brightness) / WS2812_MAX_BRIGHTNESS);
}

static void WS2812_GetOrderedColor(const WS2812_HandleTypeDef *handle,
                                   WS2812_ColorTypeDef color,
                                   uint8_t ordered[3])
{
  /* 亮度只在生成 DMA 帧时生效，pixels 中保存的原始颜色不被修改。 */
  uint8_t red = WS2812_ScaleColor(color.r, handle->brightness);
  uint8_t green = WS2812_ScaleColor(color.g, handle->brightness);
  uint8_t blue = WS2812_ScaleColor(color.b, handle->brightness);

  switch (handle->color_order)
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

static void WS2812_BuildDmaBuffer(WS2812_HandleTypeDef *handle)
{
  uint32_t buffer_index = 0;

  /* 每个数据位编码成一个 PWM CCR 值；WS2812 按高位先发。 */
  for (uint16_t led = 0; led < handle->led_count; led++)
  {
    uint8_t ordered[3];
    WS2812_GetOrderedColor(handle, handle->pixels[led], ordered);

    for (uint32_t byte_index = 0; byte_index < 3U; byte_index++)
    {
      for (int8_t bit = 7; bit >= 0; bit--)
      {
        handle->dma_buffer[buffer_index++] =
            ((ordered[byte_index] & (uint8_t)(1U << bit)) != 0U) ? handle->one_pulse : handle->zero_pulse;
      }
    }
  }

  /* 末尾追加一段 0 占空比 PWM，让数据线保持低电平完成复位/锁存。 */
  for (uint16_t i = 0; i < handle->reset_slots; i++)
  {
    handle->dma_buffer[buffer_index++] = 0;
  }
}

HAL_StatusTypeDef WS2812_Init(WS2812_HandleTypeDef *handle,
                              TIM_HandleTypeDef *htim,
                              uint32_t channel,
                              WS2812_ColorTypeDef *pixel_buffer,
                              uint16_t led_capacity,
                              uint16_t *dma_buffer,
                              uint32_t dma_buffer_length)
{
  if ((handle == NULL) || (htim == NULL) || (pixel_buffer == NULL) ||
      (dma_buffer == NULL) || (led_capacity == 0U))
  {
    return HAL_ERROR;
  }

  if (dma_buffer_length < WS2812_RequiredBufferLength(led_capacity, WS2812_DEFAULT_RESET_SLOTS))
  {
    return HAL_ERROR;
  }

  memset(handle, 0, sizeof(*handle));
  handle->htim = htim;
  handle->channel = channel;
  handle->pixels = pixel_buffer;
  handle->dma_buffer = dma_buffer;
  handle->dma_buffer_length = dma_buffer_length;
  handle->led_count = led_capacity;
  handle->led_capacity = led_capacity;
  handle->reset_slots = WS2812_DEFAULT_RESET_SLOTS;
  handle->zero_pulse = WS2812_DEFAULT_ZERO_PULSE;
  handle->one_pulse = WS2812_DEFAULT_ONE_PULSE;
  handle->brightness = WS2812_MAX_BRIGHTNESS;
  handle->color_order = WS2812_COLOR_ORDER_GRB;
  handle->busy = 0U;

  /* 初始化为确定的黑色/熄灭状态。 */
  memset(handle->pixels, 0, sizeof(WS2812_ColorTypeDef) * led_capacity);
  memset(handle->dma_buffer, 0, sizeof(uint16_t) * dma_buffer_length);

  if (WS2812_RegisterInstance(handle) != HAL_OK)
  {
    memset(handle, 0, sizeof(*handle));
    return HAL_ERROR;
  }

  return HAL_OK;
}

HAL_StatusTypeDef WS2812_DeInit(WS2812_HandleTypeDef *handle)
{
  if (handle == NULL)
  {
    return HAL_ERROR;
  }

  if ((handle->busy != 0U) && (handle->htim != NULL))
  {
    (void)HAL_TIM_PWM_Stop_DMA(handle->htim, handle->channel);
  }

  WS2812_UnregisterInstance(handle);
  memset(handle, 0, sizeof(*handle));
  return HAL_OK;
}

HAL_StatusTypeDef WS2812_SetLedCount(WS2812_HandleTypeDef *handle, uint16_t led_count)
{
  if ((handle == NULL) || (led_count == 0U) || (led_count > handle->led_capacity))
  {
    return HAL_ERROR;
  }

  if (handle->busy != 0U)
  {
    return HAL_BUSY;
  }

  if (WS2812_RequiredBufferLength(led_count, handle->reset_slots) > handle->dma_buffer_length)
  {
    return HAL_ERROR;
  }

  handle->led_count = led_count;
  return HAL_OK;
}

HAL_StatusTypeDef WS2812_SetTiming(WS2812_HandleTypeDef *handle,
                                   uint16_t zero_pulse,
                                   uint16_t one_pulse,
                                   uint16_t reset_slots)
{
  if ((handle == NULL) || (zero_pulse == 0U) || (one_pulse == 0U) || (reset_slots == 0U))
  {
    return HAL_ERROR;
  }

  if (handle->busy != 0U)
  {
    return HAL_BUSY;
  }

  if (WS2812_RequiredBufferLength(handle->led_count, reset_slots) > handle->dma_buffer_length)
  {
    return HAL_ERROR;
  }

  handle->zero_pulse = zero_pulse;
  handle->one_pulse = one_pulse;
  handle->reset_slots = reset_slots;
  return HAL_OK;
}

void WS2812_SetColorOrder(WS2812_HandleTypeDef *handle, WS2812_ColorOrderTypeDef color_order)
{
  if (handle != NULL)
  {
    handle->color_order = color_order;
  }
}

void WS2812_SetBrightness(WS2812_HandleTypeDef *handle, uint8_t brightness)
{
  if (handle != NULL)
  {
    handle->brightness = brightness;
  }
}

uint8_t WS2812_GetBrightness(const WS2812_HandleTypeDef *handle)
{
  return (handle != NULL) ? handle->brightness : 0U;
}

uint16_t WS2812_GetLedCount(const WS2812_HandleTypeDef *handle)
{
  return (handle != NULL) ? handle->led_count : 0U;
}

uint8_t WS2812_IsBusy(const WS2812_HandleTypeDef *handle)
{
  return (handle != NULL) ? handle->busy : 0U;
}

HAL_StatusTypeDef WS2812_SetPixel(WS2812_HandleTypeDef *handle,
                                  uint16_t index,
                                  WS2812_ColorTypeDef color)
{
  if ((handle == NULL) || (index >= handle->led_count))
  {
    return HAL_ERROR;
  }

  handle->pixels[index] = color;
  return HAL_OK;
}

HAL_StatusTypeDef WS2812_SetPixelRGB(WS2812_HandleTypeDef *handle,
                                     uint16_t index,
                                     uint8_t red,
                                     uint8_t green,
                                     uint8_t blue)
{
  WS2812_ColorTypeDef color = {red, green, blue};
  return WS2812_SetPixel(handle, index, color);
}

HAL_StatusTypeDef WS2812_SetAll(WS2812_HandleTypeDef *handle, WS2812_ColorTypeDef color)
{
  if (handle == NULL)
  {
    return HAL_ERROR;
  }

  for (uint16_t i = 0; i < handle->led_count; i++)
  {
    handle->pixels[i] = color;
  }

  return HAL_OK;
}

HAL_StatusTypeDef WS2812_Clear(WS2812_HandleTypeDef *handle)
{
  WS2812_ColorTypeDef black = {0U, 0U, 0U};
  return WS2812_SetAll(handle, black);
}

HAL_StatusTypeDef WS2812_Show(WS2812_HandleTypeDef *handle)
{
  HAL_StatusTypeDef status;
  uint32_t transfer_length;

  if ((handle == NULL) || (handle->htim == NULL) ||
      (handle->pixels == NULL) || (handle->dma_buffer == NULL))
  {
    return HAL_ERROR;
  }

  if (handle->busy != 0U)
  {
    return HAL_BUSY;
  }

  transfer_length = WS2812_RequiredBufferLength(handle->led_count, handle->reset_slots);
  if (transfer_length > handle->dma_buffer_length)
  {
    return HAL_ERROR;
  }

  WS2812_BuildDmaBuffer(handle);
  handle->busy = 1U;

  /*
   * HAL 接口参数类型是 uint32_t*，但 DMA 已配置为半字传输。
   * CCR 值本身 16 位足够，所以实际缓冲区使用 uint16_t。
   */
  status = HAL_TIM_PWM_Start_DMA(handle->htim,
                                 handle->channel,
                                 (uint32_t *)handle->dma_buffer,
                                 transfer_length);
  if (status != HAL_OK)
  {
    handle->busy = 0U;
  }

  return status;
}

void WS2812_HandleTimerPulseFinished(TIM_HandleTypeDef *htim)
{
  /* 最后一个复位周期发送完成后停止 PWM，让空闲时数据线保持低电平。 */
  for (uint32_t i = 0; i < WS2812_MAX_INSTANCES; i++)
  {
    WS2812_HandleTypeDef *handle = ws2812_instances[i];

    if ((handle != NULL) && (handle->htim == htim) && (handle->busy != 0U))
    {
      (void)HAL_TIM_PWM_Stop_DMA(handle->htim, handle->channel);
      __HAL_TIM_SET_COMPARE(handle->htim, handle->channel, 0U);
      handle->busy = 0U;
    }
  }
}

#ifndef WS2812_NO_HAL_CALLBACK
/* 如果其他文件已经实现该 HAL 回调，请定义 WS2812_NO_HAL_CALLBACK。 */
void HAL_TIM_PWM_PulseFinishedCallback(TIM_HandleTypeDef *htim)
{
  WS2812_HandleTimerPulseFinished(htim);
}
#endif
