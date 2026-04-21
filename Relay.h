#ifndef __RELAY_H
#define __RELAY_H

#include "stm32f10x.h"

/*
 * 继电器矩阵寻址：GPIOB（行）+ GPIOA（列），双引脚同时高电平 = 闭合
 *
 *   继电器   PB    PA
 *   K32     PB0   PA0
 *   K33     PB0   PA1
 *   K35     PB1   PA0
 *   K39     PB1   PA1
 *   K34     PB0   PA2
 *   K43     PB1   PA2
 *   K44     PB0   PA3
 *   K53     PB1   PA3
 *   K13     PB0   PA4
 *   K15     PB1   PA4
 *   K19     PB5   PA0    (放电)
 *   K18     PB5   PA1    (放电)
 *   K17     PB5   PA2    (放电)
 *   K16     PB5   PA3    (放电)
 *   K14     PB5   PA4    (放电)
 *
 * 测量模式 → 继电器组合
 *   CISS : K32 K33 K35 K39  → PB{0,1} PA{0,1}
 *   COSS : K32 K34 K35 K43  → PB{0,1} PA{0,2}
 *   CRSS : K32 K44 K35 K53  → PB{0,1} PA{0,3}
 *   RG   : K32 K13 K15      → PB{0,1} PA{0,4}
 *
 * 模式切换时序：断开全部 → 放电(20ms) → 断开放电 → 闭合新模式
 */

typedef enum {
    MODE_NONE = 0,
    MODE_CISS,
    MODE_COSS,
    MODE_CRSS,
    MODE_RG
} RelayMode_t;

typedef enum {
    RELAY_OK    = 0,
    RELAY_ERROR = 1
} RelayStatus_t;

void          Relay_Init(void);
RelayStatus_t Relay_SetMode(RelayMode_t mode);
RelayMode_t   Relay_GetMode(void);
void          Relay_AllOff(void);
uint8_t       Relay_ReadState(void);

#endif /* __RELAY_H */
