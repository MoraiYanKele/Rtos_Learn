#ifndef __VOFA_H__
#define __VOFA_H__

#include "main.h"
#include "usart.h"
#include "gpio.h"
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "stm32f4xx_it.h"




#define VOFA_UART           &huart1


#ifdef USE_UART_DMA
    #define VOFA_DMA_HANDEL     &hdma_usart1_rx   // 需要在usart.h中extern定义
    extern DoubleBufferUartDMATypeDef uartToVOFA;
    #define UART_TO_VOFA   &uartToVOFA
    #define VOFA_UART_SEND(uart, data, size)    DoubleBufferUartDMA_Send((uart), (data), (size)) 

#elif defined USE_USB_VIRTUAL
    #define UART_TO_VOFA   &huart1
    #define VOFA_UART_SEND(uart, data, size)    CDC_Transmit_FS((data), (size)) // USB虚拟串口发送数据

#else
    #define UART_TO_VOFA   VOFA_UART
    #define VOFA_UART_SEND(uart, data, size)    HAL_UART_Transmit((uart), (data), (size), 0xFFFF)

#endif

typedef struct
{
    float *controlData_float; 
    int *controlData_int;
    const char *dataName;
} VOFACtrlTypedef;

// 用户调用函数

void VOFA_Init();
void VOFA_SendJustFloat(int length, ...);
void VOFA_SendJustFloat_Queue(float *data, uint16_t length);
void VOFA_SendFireWater(const char *format, ...);
void VOFA_RegisterData_float(const char *name, float *data);
void VOFA_RegisterData_int(const char *name, int *data);
void Printf(const char *format, ...);



// 内部函数

void VOFA_Receive(UART_HandleTypeDef *huart, uint8_t *pData, uint16_t size);
void ClearCtrlDataList();
void VOFA_RxCallBack();
void VOFA_RxCallBack_USB(uint8_t* Buf, uint32_t *Len);






#endif

// 回调函数模板
// void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart) 
// {
//   if (huart == VOFA_UART) 
//   {
//     TxCallBack_DoubleBufferUartDMA(&uartToVOFA);
//   }
// }
// ``` # 如果使用双循环dma则定义

// ```c
// void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
// {
//   if (huart == VOFA_UART)
//   {
//     VOFA_RxCallBack();
//   }
// }