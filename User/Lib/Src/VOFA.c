/* VOFA+ 上位机通信模块 */

#include "VOFA.h"
// 条件编译DMA相关配置
#ifdef USE_UART_DMA
    DoubleBufferUartDMATypeDef uartToVOFA; // 双缓冲UART DMA结构体
#endif

uint8_t rxBuffer_VOFA[128] = {0};          // VOFA数据接收缓冲区
#define DATA_LIST_SIZE 64                  // 可控数据列表最大容量
VOFACtrlTypedef ctrledDataList[DATA_LIST_SIZE] = {0}; // 可被VOFA控制的数据列表


#ifdef USE_UART_DMA
    /**
     * @brief  VOFA模块初始化
     * @note   初始化UART和DMA通信，配置空闲中断接收模式
     *         清空可控数据列表，准备接收控制指令
     */
    void VOFA_Init()
    {   
        DoubleBufferUartDMA_Init(&uartToVOFA);         // 初始化双缓冲DMA结构体
        uartToVOFA.huart = VOFA_UART;                  // 绑定UART实例
        
        __HAL_UART_ENABLE_IT(VOFA_UART, UART_IT_IDLE); // 启用空闲中断

#ifdef VOFA_DEBUG
        VOFA_Receive(VOFA_UART, rxBuffer_VOFA, 128);    
#endif
        // 初始化可控数据列表为空
        ClearCtrlDataList();
    }

    void VOFA_Receive(UART_HandleTypeDef *huart, uint8_t *pData, uint16_t size)
    {
        HAL_UARTEx_ReceiveToIdle_DMA(huart, pData, size); // 启动DMA空闲中断接收
        __HAL_DMA_DISABLE_IT(VOFA_DMA_HANDEL, DMA_IT_HT); // 禁用DMA半传输中断
    }

#elif defined USE_USB_VIRTUAL
    /**
     * @brief  VOFA模块初始化
     * @note   初始化USB虚拟串口通信，配置空闲中断接收模式
     *         清空可控数据列表，准备接收控制指令
     */
    void VOFA_Init()
    {   
        // 初始化可控数据列表为空
        ClearCtrlDataList();
    }

    void VOFA_Receive(UART_HandleTypeDef *huart, uint8_t *pData, uint16_t size){}

#else
 /**
     * @brief  VOFA模块初始化
     * @note   初始化UART和DMA通信，配置空闲中断接收模式
     *         清空可控数据列表，准备接收控制指令
     */
    void VOFA_Init()
    {   
  
        
        VOFA_Receive(VOFA_UART, rxBuffer_VOFA, 128);
        // 初始化可控数据列表为空
        ClearCtrlDataList();
    }

    void VOFA_Receive(UART_HandleTypeDef *huart, uint8_t *pData, uint16_t size)
    {
        HAL_UARTEx_ReceiveToIdle_IT(huart, pData, size); // 启动空闲中断接收
    }

#endif

/**
  * @brief  清空可控数据列表
  * @note   将 `ctrledDataList` 中的所有条目重置为初始状态，
  *         即将 `dataName` 和 `controlData` 指针设置为 `NULL`。
  *         该函数通常在初始化或需要重置 VOFA 控制数据时调用。
  * @retval None
  */
void ClearCtrlDataList() 
{
    for (int i = 0; i < DATA_LIST_SIZE; i++) 
    {
        ctrledDataList[i].dataName = NULL;
        ctrledDataList[i].controlData_float = NULL;
        ctrledDataList[i].controlData_int = NULL;

    }
}

/**
  * @brief  发送JustFloat格式数据
  * @param  length 浮点参数个数
  * @param  ...    待发送的浮点数据（可变参数）
  * @note   数据格式：每个float占4字节，数据包尾添加0x0000807F
  *         适用于VOFA+ JustFloat波形显示模式
  */
void VOFA_SendJustFloat(int length, ...)
{
    va_list floats;
    va_start(floats, length);
    uint8_t txData[64];
    
    // 将浮点参数按字节存入发送缓冲区
    for (int i = 0; i < length; i++)
    {
        float value = va_arg(floats, double);
        memcpy(txData + i * sizeof(float), &value, sizeof(float));
    }
    va_end(floats);
    
    // 添加协议规定的4字节尾帧
    const uint8_t tail[4] = {0x00, 0x00, 0x80, 0x7F};
    memcpy(txData + length * sizeof(float), tail, 4);
    
    VOFA_UART_SEND(UART_TO_VOFA, txData, length * sizeof(float) + 4);
}
void VOFA_SendJustFloat_Queue(float *data, uint16_t length)
{
    uint8_t txData[64];

    for (int i = 0; i < length; i++)
    {
        memcpy(txData + i * sizeof(float), &data[i], sizeof(float));
    }
    const uint8_t tail[4] = {0x00, 0x00, 0x80, 0x7F};
    memcpy(txData + length * sizeof(float), tail, 4);
    
    VOFA_UART_SEND(UART_TO_VOFA, txData, length * sizeof(float) + 4);
}


/**
  * @brief  发送FireWater格式数据
  * @param  format 格式化字符串（类似printf）
  * @param  ...    格式化参数
  * @note   使用字符串格式化发送，适用于VOFA+ FireWater脚本解析
  *         示例：VOFA_SendFireWater("sin:%f,cos:%f\n", sin_val, cos_val)
  */
void VOFA_SendFireWater(const char *format, ...) 
{
    char txBuffer[128];  
    va_list args;
    va_start(args, format);
    vsnprintf(txBuffer, sizeof(txBuffer), format, args); // 格式化字符串
    va_end(args);
    
    VOFA_UART_SEND(UART_TO_VOFA, (uint8_t *)txBuffer, strlen(txBuffer));
}

static char g_printfTxBuffer[1024];
static volatile uint8_t g_printfBusy = 0;

void Printf(const char *format, ...) 
{
    // 简单防重入：如果正在用就直接返回（也可以选择等待）
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    if (g_printfBusy) {
        __set_PRIMASK(primask);
        return;
    }
    g_printfBusy = 1;
    __set_PRIMASK(primask);

    va_list args;
    va_start(args, format);
    vsnprintf(g_printfTxBuffer, sizeof(g_printfTxBuffer), format, args);
    va_end(args);

    VOFA_UART_SEND(UART_TO_VOFA, (uint8_t *)g_printfTxBuffer, strlen(g_printfTxBuffer));

    primask = __get_PRIMASK();
    __disable_irq();
    g_printfBusy = 0;
    __set_PRIMASK(primask);
}


void VOFA_RegisterData_float(const char *name, float *data)
{
    // 检查是否已注册
    for (int i = 0; i < DATA_LIST_SIZE; i++)
    {
        if (ctrledDataList[i].controlData_float == data)
            return;
    }
    
    // 在空闲位置注册新变量
    for (int i = 0; i < DATA_LIST_SIZE; i++)
    {
        if (ctrledDataList[i].controlData_float == NULL && ctrledDataList[i].controlData_int == NULL)
        {
            ctrledDataList[i].controlData_float = data;
            ctrledDataList[i].dataName = name;
            break;
        }
    }
}

void VOFA_RegisterData_int(const char *name, int *data)
{
    // 检查是否已注册
    for (int i = 0; i < DATA_LIST_SIZE; i++)
    {
        if (ctrledDataList[i].controlData_int == data)
            return;
    }
    
    // 在空闲位置注册新变量
    for (int i = 0; i < DATA_LIST_SIZE; i++)
    {
        if (ctrledDataList[i].controlData_int == NULL && ctrledDataList[i].controlData_float == NULL)
        {
            ctrledDataList[i].controlData_int = data;
            ctrledDataList[i].dataName = name;
            break;
        }
    }
}

/**
  * @brief  VOFA数据接收回调
  * @note   解析来自VOFA的控制指令，格式为"变量名:数值"
  *         匹配已注册变量并更新其值，完成后重启DMA接收
  */
void VOFA_RxCallBack()
{
    // 遍历控制列表匹配指令
    for (int i = 0; i < DATA_LIST_SIZE; i++)
    {
        if (ctrledDataList[i].dataName == NULL || (ctrledDataList[i].controlData_float == NULL && ctrledDataList[i].controlData_int == NULL))
            continue;  
        
        int nameLength = strlen(ctrledDataList[i].dataName);
        if (strncmp((char *)rxBuffer_VOFA, ctrledDataList[i].dataName, nameLength) == 0 && 
            rxBuffer_VOFA[nameLength] == ':')
        {
            if (ctrledDataList[i].controlData_int != NULL && ctrledDataList[i].controlData_float == NULL)
                sscanf((char *)rxBuffer_VOFA + nameLength + 1, "%d", ctrledDataList[i].controlData_int);
                
            else if (ctrledDataList[i].controlData_float != NULL)
                sscanf((char *)rxBuffer_VOFA + nameLength + 1, "%f", ctrledDataList[i].controlData_float);
        }
    }
    
    // 重启接收
    VOFA_Receive(VOFA_UART, rxBuffer_VOFA, 128);
}

void VOFA_RxCallBack_USB(uint8_t* Buf, uint32_t *Len)
{

    memcpy(rxBuffer_VOFA, Buf, *Len); // 将接收到的数据复制到rxBuffer_VOFA
    // 遍历控制列表匹配指令
    for (int i = 0; i < DATA_LIST_SIZE; i++)
    {
        if (ctrledDataList[i].dataName == NULL || (ctrledDataList[i].controlData_float == NULL && ctrledDataList[i].controlData_int == NULL))
            continue;  
        
        int nameLength = strlen(ctrledDataList[i].dataName);
        if (strncmp((char *)rxBuffer_VOFA, ctrledDataList[i].dataName, nameLength) == 0 && 
            rxBuffer_VOFA[nameLength] == ':')
        {
            if (ctrledDataList[i].controlData_int != NULL && ctrledDataList[i].controlData_float == NULL)
                sscanf((char *)rxBuffer_VOFA + nameLength + 1, "%d", ctrledDataList[i].controlData_int);
                
            else if (ctrledDataList[i].controlData_float != NULL)
                sscanf((char *)rxBuffer_VOFA + nameLength + 1, "%f", ctrledDataList[i].controlData_float);
        }
    }
}