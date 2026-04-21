#include "stm32f10x.h"
#include "Relay.h"
#include "Delay.h"

/* 行引脚 (GPIOB) */
#define ROW_PORT        GPIOB  // 行引脚所在的 GPIO 端口
#define ROW_CLK         RCC_APB2Periph_GPIOB  // 行引脚 GPIO 时钟
#define ROW_PIN_0       GPIO_Pin_0  // 行引脚 PB0
#define ROW_PIN_1       GPIO_Pin_1  // 行引脚 PB1
#define ROW_PIN_5       GPIO_Pin_5  // 行引脚 PB5（放电行）
#define ROW_ALL         (ROW_PIN_0 | ROW_PIN_1 | ROW_PIN_5)  // 所有行引脚的位掩码

/* 列引脚 (GPIOA) */
#define COL_PORT        GPIOA  // 列引脚所在的 GPIO 端口
#define COL_CLK         RCC_APB2Periph_GPIOA  // 列引脚 GPIO 时钟
#define COL_PIN_0       GPIO_Pin_0  // 列引脚 PA0
#define COL_PIN_1       GPIO_Pin_1  // 列引脚 PA1
#define COL_PIN_2       GPIO_Pin_2  // 列引脚 PA2
#define COL_PIN_3       GPIO_Pin_3  // 列引脚 PA3
#define COL_PIN_4       GPIO_Pin_4  // 列引脚 PA4
#define COL_ALL         (COL_PIN_0 | COL_PIN_1 | COL_PIN_2 | COL_PIN_3 | COL_PIN_4)  // 所有列引脚的位掩码

/* 放电行 */
#define DISCHARGE_ROW   ROW_PIN_5  // 放电行 PB5
#define DISCHARGE_COL   COL_ALL    // 放电列 PA0-PA4

/* 时序参数 */
#define SETTLE_MS       20  // 继电器闭合后的稳定时间（毫秒）
#define DISCHARGE_MS    20  // 放电时间（毫秒）
#define GAP_MS          5   // 模式切换间隔时间（毫秒）

/* 每个模式对应的行/列 mask */
static const uint16_t MODE_ROW[5] = {
    0,                              /* MODE_NONE: 无模式 */
    ROW_PIN_0 | ROW_PIN_1,         /* MODE_CISS: 行 PB0, PB1 */
    ROW_PIN_0 | ROW_PIN_1,         /* MODE_COSS: 行 PB0, PB1 */
    ROW_PIN_0 | ROW_PIN_1,         /* MODE_CRSS: 行 PB0, PB1 */
    ROW_PIN_0 | ROW_PIN_1,         /* MODE_RG:   行 PB0, PB1 */
};

static const uint16_t MODE_COL[5] = {
    0,                              /* MODE_NONE: 无模式 */
    COL_PIN_0 | COL_PIN_1,         /* MODE_CISS: 列 PA0, PA1 */
    COL_PIN_0 | COL_PIN_2,         /* MODE_COSS: 列 PA0, PA2 */
    COL_PIN_0 | COL_PIN_3,         /* MODE_CRSS: 列 PA0, PA3 */
    COL_PIN_0 | COL_PIN_4,         /* MODE_RG:   列 PA0, PA4 */
};

static RelayMode_t s_currentMode = MODE_NONE;  // 当前继电器模式

/* ---------------------------------------------------------------*/
static void all_pins_low(void)
{
    // 将所有行和列引脚拉低，断开所有继电器
    GPIO_ResetBits(ROW_PORT, ROW_ALL);
    GPIO_ResetBits(COL_PORT, COL_ALL);
}

static void discharge(void)
{
    // 放电操作：闭合放电行和所有列，释放残余电荷
    GPIO_SetBits(ROW_PORT, DISCHARGE_ROW);
    GPIO_SetBits(COL_PORT, DISCHARGE_COL);
    Delay_ms(DISCHARGE_MS);  // 等待放电完成
    all_pins_low();          // 断开所有引脚
    Delay_ms(GAP_MS);        // 等待间隔时间
}

void Relay_Init(void)
{
    // 初始化继电器 GPIO 配置
    GPIO_InitTypeDef gpio;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO, ENABLE);  // 开启复用功能时钟
    GPIO_PinRemapConfig(GPIO_Remap_SWJ_JTAGDisable, ENABLE);  // 禁用 JTAG，释放 PB3 和 PB4

    RCC_APB2PeriphClockCmd(ROW_CLK | COL_CLK, ENABLE);  // 开启行和列 GPIO 时钟

    gpio.GPIO_Mode  = GPIO_Mode_Out_PP;  // 推挽输出模式
    gpio.GPIO_Speed = GPIO_Speed_2MHz;   // GPIO 输出速度

    gpio.GPIO_Pin = ROW_ALL;  // 配置行引脚
    GPIO_Init(ROW_PORT, &gpio);

    gpio.GPIO_Pin = COL_ALL;  // 配置列引脚
    GPIO_Init(COL_PORT, &gpio);

    all_pins_low();  // 初始化时断开所有继电器
    s_currentMode = MODE_NONE;  // 设置当前模式为无模式
}

RelayStatus_t Relay_SetMode(RelayMode_t mode)
{
    if (mode > MODE_RG)
        return RELAY_ERROR;  // 检查模式是否有效

    all_pins_low();  // 断开所有继电器
    Delay_ms(GAP_MS);  // 等待间隔时间

    if (s_currentMode != MODE_NONE)
        discharge();  // 如果当前模式不为空，执行放电操作

    if (mode != MODE_NONE) {
        GPIO_SetBits(ROW_PORT, MODE_ROW[mode]);  // 设置行引脚
        GPIO_SetBits(COL_PORT, MODE_COL[mode]);  // 设置列引脚
        Delay_ms(SETTLE_MS);  // 等待继电器触点稳定
    }

    s_currentMode = mode;  // 更新当前模式
    return RELAY_OK;
}

RelayMode_t Relay_GetMode(void)
{
    return s_currentMode;  // 返回当前继电器模式
}

void Relay_AllOff(void)
{
    if (s_currentMode != MODE_NONE) {
        all_pins_low();  // 断开所有继电器
        Delay_ms(GAP_MS);  // 等待间隔时间
        discharge();  // 执行放电操作
    }
    all_pins_low();  // 确保所有引脚断开
    s_currentMode = MODE_NONE;  // 更新模式为无模式
}

uint8_t Relay_ReadState(void)
{
    uint16_t odr_b = ROW_PORT->ODR;  // 读取行引脚的输出数据寄存器
    uint16_t odr_a = COL_PORT->ODR;  // 读取列引脚的输出数据寄存器
    uint8_t state = 0;

    /* 根据引脚状态生成状态位图 */
    if (odr_b & ROW_PIN_0) state |= 0x01;  // bit 0: PB0
    if (odr_b & ROW_PIN_1) state |= 0x02;  // bit 1: PB1
    if (odr_b & ROW_PIN_5) state |= 0x04;  // bit 2: PB5
    if (odr_a & COL_PIN_0) state |= 0x08;  // bit 3: PA0
    if (odr_a & COL_PIN_1) state |= 0x10;  // bit 4: PA1
    if (odr_a & COL_PIN_2) state |= 0x20;  // bit 5: PA2
    if (odr_a & COL_PIN_3) state |= 0x40;  // bit 6: PA3
    if (odr_a & COL_PIN_4) state |= 0x80;  // bit 7: PA4

    return state;  // 返回状态位图
}
