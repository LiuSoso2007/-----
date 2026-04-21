#include "stm32f10x.h"                  // Device header
#include <stdio.h>
#include <stdarg.h>

/**
 * 串口数据包定义：
 * 发送数据包：3字节有效负载，由用户填充 Serial_TxPacket[] 后调用 Serial_SendPacket() 发送。
 * 接收数据包：接收到完整帧后存入 Serial_RxPacket[]，并置位接收标志 Serial_RxFlag。
 */
uint8_t Serial_TxPacket[3];
uint8_t Serial_RxPacket[3];
volatile uint8_t Serial_RxFlag;          // 接收完成标志，volatile 确保中断修改后主循环能正确读取

/**
 * @brief  串口初始化函数
 * @note   配置 USART1：波特率 9600，8位数据，1停止位，无校验，无流控。
 *         使能接收中断，并配置 NVIC 中断优先级。
 */
void Serial_Init(void)
{
	// 使能 USART1 和 GPIOA 的时钟
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1, ENABLE);
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
	
	// 配置 TX 引脚 (PA9) 为复用推挽输出
	GPIO_InitTypeDef GPIO_InitStructure;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_9;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOA, &GPIO_InitStructure);
	
	// 配置 RX 引脚 (PA10) 为上拉输入（串口空闲状态为高电平）
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOA, &GPIO_InitStructure);
	
	// USART 基本参数配置
	USART_InitTypeDef USART_InitStructure;
	USART_InitStructure.USART_BaudRate = 9600;
	USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
	USART_InitStructure.USART_Mode = USART_Mode_Tx | USART_Mode_Rx; // 同时使能发送和接收
	USART_InitStructure.USART_Parity = USART_Parity_No;
	USART_InitStructure.USART_StopBits = USART_StopBits_1;
	USART_InitStructure.USART_WordLength = USART_WordLength_8b;
	USART_Init(USART1, &USART_InitStructure);
	
	// 使能接收中断（RXNE：接收数据寄存器非空）
	USART_ITConfig(USART1, USART_IT_RXNE, ENABLE);
	
	// 配置中断优先级分组为 2 位抢占优先级，2 位响应优先级
	NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
	
	// 使能 USART1 中断通道，并设置抢占优先级和子优先级均为 1
	NVIC_InitTypeDef NVIC_InitStructure;
	NVIC_InitStructure.NVIC_IRQChannel = USART1_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;
	NVIC_Init(&NVIC_InitStructure);
	
	// 使能 USART1 外设
	USART_Cmd(USART1, ENABLE);
}

/**
 * @brief  发送单个字节
 * @param  Byte: 待发送的字节数据
 * @note   等待发送数据寄存器空标志（TXE）置位后再写入下一字节
 */
void Serial_SendByte(uint8_t Byte)
{
	USART_SendData(USART1, Byte);
	while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET);
}

/**
 * @brief  发送一个字节数组
 * @param  Array: 数组首地址
 * @param  Length: 数组长度（字节数）
 */
void Serial_SendArray(uint8_t *Array, uint16_t Length)
{
	uint16_t i;
	for (i = 0; i < Length; i ++)
	{
		Serial_SendByte(Array[i]);
	}
}

/**
 * @brief  发送字符串（以 '\0' 结尾）
 * @param  String: 字符串指针
 */
void Serial_SendString(char *String)
{
	uint8_t i;
	for (i = 0; String[i] != '\0'; i ++)
	{
		Serial_SendByte(String[i]);
	}
}

/**
 * @brief  整数的幂运算（用于数字转字符串时的位值计算）
 * @param  X: 底数
 * @param  Y: 指数
 * @retval X 的 Y 次方
 */
uint32_t Serial_Pow(uint32_t X, uint32_t Y)
{
	uint32_t Result = 1;
	while (Y --)
	{
		Result *= X;
	}
	return Result;
}

/**
 * @brief  发送指定长度的数字（十进制）
 * @param  Number: 待发送的无符号整数
 * @param  Length: 数字的位数（例如发送 123，Length=3）
 * @note   通过除法和取模逐位发送，转换为 ASCII 码（+ '0'）
 */
void Serial_SendNumber(uint32_t Number, uint8_t Length)
{
	uint8_t i;
	for (i = 0; i < Length; i ++)
	{
		Serial_SendByte(Number / Serial_Pow(10, Length - i - 1) % 10 + '0');
	}
}

/**
 * @brief  重定向 printf 底层输出函数
 * @note   使得可以使用标准库 printf 通过串口输出
 */
int fputc(int ch, FILE *f)
{
	Serial_SendByte(ch);
	return ch;
}

/**
 * @brief  自定义格式化输出函数（类似 printf）
 * @param  format: 格式化字符串
 * @param  ...: 可变参数列表
 * @note   内部使用 vsprintf 格式化到缓冲区，再通过串口发送
 */
void Serial_Printf(char *format, ...)
{
	char String[100];
	va_list arg;
	va_start(arg, format);
	vsprintf(String, format, arg);
	va_end(arg);
	Serial_SendString(String);
}

/**
 * @brief  发送一个完整的数据包
 * @note   帧格式：帧头 0xAA + 3字节负载 + 帧尾 0x55
 *         负载数据由全局数组 Serial_TxPacket[] 提供
 */
void Serial_SendPacket(void)
{
	Serial_SendByte(0xAA);
	Serial_SendArray(Serial_TxPacket, 3);
	Serial_SendByte(0x55);
}

/**
 * @brief  获取接收完成标志并清除
 * @retval 1: 已接收到一帧完整数据，Serial_RxPacket 中为有效负载
 *         0: 无新数据帧
 * @note   该函数非阻塞，调用后标志位自动清零
 */
uint8_t Serial_GetRxFlag(void)
{
	if (Serial_RxFlag == 1)
	{
		Serial_RxFlag = 0;
		return 1;
	}
	return 0;
}

/**
 * @brief  USART1 中断服务函数
 * @note   采用状态机解析帧：帧头 0xAA -> 接收3字节负载 -> 帧尾 0x55
 *         若中途任何字节不符合协议则丢弃当前帧并复位状态
 */
void USART1_IRQHandler(void)
{
	static uint8_t RxState = 0;    // 接收状态：0=等待帧头，1=接收负载，2=等待帧尾
	static uint8_t pRxPacket = 0;  // 当前负载字节索引（0~2）
	
	// 检查是否为 RXNE（接收数据寄存器非空）中断
	if (USART_GetITStatus(USART1, USART_IT_RXNE) == SET)
	{
		uint8_t RxData = USART_ReceiveData(USART1); // 读取接收到的字节（同时自动清除 RXNE 标志）
		
		// 状态机处理
		if (RxState == 0)          // 等待帧头 0xAA
		{
			if (RxData == 0xAA)
			{
				RxState = 1;       // 进入负载接收状态
				pRxPacket = 0;     // 负载索引归零
			}
			// 否则继续保持状态0，丢弃该字节
		}
		else if (RxState == 1)     // 接收负载数据
		{
			Serial_RxPacket[pRxPacket] = RxData; // 存储负载字节
			pRxPacket++;
			if (pRxPacket >= 3)    // 已接收满3字节负载
			{
				RxState = 2;       // 进入等待帧尾状态
			}
		}
		else if (RxState == 2)     // 等待帧尾 0x55
		{
			if (RxData == 0x55)
			{
				RxState = 0;       // 帧尾正确，回到初始状态等待下一帧
				Serial_RxFlag = 1; // 通知主循环：有效数据帧已接收
			}
			else
			{
				RxState = 0;       /* 帧尾不匹配，丢弃当前帧，返回状态0重新同步 */
			}
		}
		
		// 清除 USART1 的 RXNE 中断挂起位（函数内部操作）
		USART_ClearITPendingBit(USART1, USART_IT_RXNE);
	}
}