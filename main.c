/*
 * ============================================================
 * 硬件接口速查表  (STM32F103C8T6 Blue-Pill)
 * ============================================================
 *
 * 串口 / RS232（MAX3232 或同类电平转换）
 *   PA9   USART1_TX  →  转换芯片 T1IN  →  DB9 Pin2 (RXD)
 *   PA10  USART1_RX  ←  转换芯片 R1OUT ←  DB9 Pin3 (TXD)
 *   GND   ─────────────────────────────  DB9 Pin5 (GND)
 *   波特率 9600  8N1  无流控
 *
 * 继电器矩阵驱动 IO（GPIOB 行 + GPIOA 列，双引脚同时高电平 = 闭合）
 *   行引脚：PB0, PB1（测量）  PB5（放电）
 *   列引脚：PA0, PA1, PA2, PA3, PA4
 *
 *   继电器   PB    PA        继电器   PB    PA
 *   K32     PB0   PA0       K19     PB5   PA0  (放电)
 *   K33     PB0   PA1       K18     PB5   PA1  (放电)
 *   K35     PB1   PA0       K17     PB5   PA2  (放电)
 *   K39     PB1   PA1       K16     PB5   PA3  (放电)
 *   K34     PB0   PA2       K14     PB5   PA4  (放电)
 *   K43     PB1   PA2
 *   K44     PB0   PA3
 *   K53     PB1   PA3
 *   K13     PB0   PA4
 *   K15     PB1   PA4
 *
 * 测量模式 → 继电器组合
 *   Ciss（栅源电容）    ：K32 K33 K35 K39  → PB{0,1} PA{0,1}
 *   Coss（漏源电容）    ：K32 K34 K35 K43  → PB{0,1} PA{0,2}
 *   Crss（反向传输电容）：K32 K44 K35 K53  → PB{0,1} PA{0,3}
 *   Rg  （栅极电阻）    ：K32 K13 K15      → PB{0,1} PA{0,4}
 *
 * 模式切换时序：断开全部(5ms) → 放电(20ms) → 断开放电(5ms) → 闭合新模式(20ms)
 *
 * ============================================================
 * 上位机联动说明（与本下位机无关，仅供阅读参考）
 * ============================================================
 *
 * 上位机设备：
 *   - TH1992 源表（SCPI 控制，GPIB/USB）
 *   - TH2840E LCR 电桥（SCPI 控制，GPIB/USB）
 *
 * 联动时序：
 *   1. 上位机通过 RS232 向本下位机发送模式切换指令
 *   2. 下位机完成继电器切换后回复 ACK/READY
 *   3. 上位机收到 READY 后，再向 TH1992/TH2840E 发送 SCPI 测量指令
 *   4. 本下位机不参与 SCPI 通信，仅负责硬件回路切换
 *
 * 串口协议（RS232，9600 8N1）：
 *   帧格式：  [0xAA] [CMD] [DATA] [0x55]
 *
 *   CMD 定义：
 *     0x01  切换至 Ciss 模式
 *     0x02  切换至 Coss 模式
 *     0x03  切换至 Crss 模式
 *     0x04  切换至 Rg   模式
 *     0x05  查询当前模式与继电器状态
 *     0x06  复位（断开所有继电器）
 *
 *   应答帧：  [0xAA] [0x80|CMD] [DATA] [0x55]
 *     DATA = 0x00 成功 / 0x01 参数错误
 * ============================================================ */

#include "stm32f10x.h"
#include "Delay.h"
#include "Serial.h"
#include "Relay.h"

/* ---- 通讯超时看门狗（单位：主循环计数，约 1 ms/次） ---- */
#define COMM_TIMEOUT_MS   5000u  // 定义通讯超时时间为 5000 ms，用于检测通讯中断

/* ---- 帧定界符 ---- */
#define FRAME_HEAD  0xAAu  // 帧头标志
#define FRAME_TAIL  0x55u  // 帧尾标志

/* ---- CMD 定义 ---- */
#define CMD_CISS    0x01u
#define CMD_COSS    0x02u
#define CMD_CRSS    0x03u
#define CMD_RG      0x04u
#define CMD_QUERY   0x05u
#define CMD_RESET   0x06u

/* ---- 应答 DATA 定义 ---- */
#define ACK_OK      0x00u
#define ACK_PARAM   0x01u

/* ------------------------------------------------------------------ */
static void send_ack(uint8_t cmd, uint8_t data)
{
    Serial_SendByte(FRAME_HEAD);
    Serial_SendByte((uint8_t)(0x80u | cmd));
    Serial_SendByte(data);
    Serial_SendByte(FRAME_TAIL);
}

/* ------------------------------------------------------------------ */
static void process_command(uint8_t cmd, uint8_t data)
{
    switch (cmd) {
    case CMD_CISS:
        Relay_SetMode(MODE_CISS);
        send_ack(cmd, ACK_OK);
        break;
    case CMD_COSS:
        Relay_SetMode(MODE_COSS);
        send_ack(cmd, ACK_OK);
        break;
    case CMD_CRSS:
        Relay_SetMode(MODE_CRSS);
        send_ack(cmd, ACK_OK);
        break;
    case CMD_RG:
        Relay_SetMode(MODE_RG);
        send_ack(cmd, ACK_OK);
        break;
    case CMD_QUERY:
        send_ack(cmd, (uint8_t)Relay_GetMode());
        break;
    case CMD_RESET:
        Relay_AllOff();
        send_ack(cmd, ACK_OK);
        break;
    default:
        send_ack(cmd, ACK_PARAM);
        break;
    }
}

/* ------------------------------------------------------------------ */
int main(void)
{
    uint32_t timeout_cnt = 0;  // 通讯超时计数器

    Relay_Init();       /* 上电：所有继电器断开 */
    Serial_Init();      /* 初始化串口：RS232 9600 8N1，中断接收 */

    while (1)
    {
        if (Serial_GetRxFlag())
        {
            /* RxPacket: [CMD][DATA]  (帧头/帧尾已由 IRQ 剥离) */
            uint8_t cmd  = (uint8_t)Serial_RxPacket[0];
            uint8_t data = (uint8_t)Serial_RxPacket[1];
            process_command(cmd, data);  // 处理命令
            timeout_cnt = 0;  // 重置超时计数器
        }
        else
        {
            Delay_ms(1);  // 延时 1 ms
            timeout_cnt++;
            if (timeout_cnt >= COMM_TIMEOUT_MS)
            {
                /* 通讯超时：回到安全状态 */
                Relay_AllOff();
                timeout_cnt = 0;
            }
        }
    }
}
