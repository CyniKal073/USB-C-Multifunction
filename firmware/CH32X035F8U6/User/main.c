/********************************** (C) COPYRIGHT *******************************
 * File Name          : main.c
 * Author             : WCH
 * Version            : V1.0.0
 * Date               : 2023/12/26
 * Description        : Main program body.
*********************************************************************************
* Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
* Attention: This software (modified or not) and binary are used for
* microcontroller manufactured by Nanjing Qinheng Microelectronics.
*******************************************************************************/

/*
 *@Note
 *GPIO routine:
 *PA0 push-pull output.
 *
 ***Only PA0--PA15 and PC16--PC17 support input pull-down.
 */

#include "debug.h"
#include "PD_Process.h"
#include "usbd_composite_km.h" // 引入 HID 头文件
#include "ch32x035_usbfs_device.h"

void TIM1_UP_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));

volatile UINT8  Tim_Ms_Cnt = 0x00;

void Switch_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_12;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP; // 推挽输出保证驱动电平稳定
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &GPIO_InitStructure);

    /* 默认输出低电平，初始化导通通道 1 (CC1) */
    GPIO_WriteBit(GPIOB, GPIO_Pin_12, Bit_RESET);
}

/*********************************************************************
 * @fn      TIM1_Init
 *
 * @brief   Initialize TIM1
 *
 * @return  none
 */
void TIM1_Init( u16 arr, u16 psc )
{
    TIM_TimeBaseInitTypeDef TIM_TimeBaseInitStructure={0};
    NVIC_InitTypeDef NVIC_InitStructure={0};
    RCC_APB2PeriphClockCmd( RCC_APB2Periph_TIM1, ENABLE );
    TIM_TimeBaseInitStructure.TIM_Period = arr;
    TIM_TimeBaseInitStructure.TIM_Prescaler = psc;
    TIM_TimeBaseInitStructure.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseInitStructure.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInitStructure.TIM_RepetitionCounter = 0x00;
    TIM_TimeBaseInit( TIM1, &TIM_TimeBaseInitStructure);
    TIM_ClearITPendingBit( TIM1, TIM_IT_Update );
    NVIC_InitStructure.NVIC_IRQChannel = TIM1_UP_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 3;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);
    TIM_ITConfig( TIM1, TIM_IT_Update, ENABLE );
    TIM_Cmd( TIM1, ENABLE );
}

#include "ch32x035.h"

/* 线控模拟全局变量 */
uint8_t  LineControl_Timer = 0;       // 模拟按键保持时间 (ms)
uint8_t  Current_Switch_State = 0;    // 当前模拟开关通道 (0:断开 1:播放 2:音量+ 3:音量-)

/*********************************************************************
 * @fn      EC11_Init
 * @brief   初始化旋转编码器引脚 (PB11, PB3, PB0)
 *********************************************************************/
void EC11_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = { 0 };
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);

    /* A相(PB11), B相(PB3), 按键SW(PB0) 均配置为上拉输入 */
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_11 | GPIO_Pin_3 | GPIO_Pin_0;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &GPIO_InitStructure);
}

/*********************************************************************
 * @fn      Analog_Switch_Init
 * @brief   初始化 TS3A5017 控制引脚 (PA0=IN1, PA1=IN2)
 *********************************************************************/
void Analog_Switch_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = { 0 };
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);

    /* 配置 PA0 和 PA1 为推挽输出 */
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0 | GPIO_Pin_1;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    /* 默认切到空闲通道 (IN2=0, IN1=0 对应的 NO1 必须物理悬空！) */
    GPIO_WriteBit(GPIOA, GPIO_Pin_1, Bit_RESET); // IN2
    GPIO_WriteBit(GPIOA, GPIO_Pin_0, Bit_RESET); // IN1
}

/*********************************************************************
 * @fn      TS3A5017_Select_Channel
 * @brief   控制模拟开关通道
 * @param   ch - 0:全断开, 1:播放/暂停, 2:音量+, 3:音量-
 *********************************************************************/
void TS3A5017_Select_Channel(uint8_t ch)
{
    /*
     * 真值表映射：PA1 控制 IN2，PA0 控制 IN1
     * 如果您的实际 PCB 电阻位置与此不同，请修改这里的 0/1 组合
     */
    switch(ch)
    {
        case 0: // 闲置/断开 (NO1 悬空)
            GPIO_WriteBit(GPIOA, GPIO_Pin_1, Bit_RESET); // IN2 = 0
            GPIO_WriteBit(GPIOA, GPIO_Pin_0, Bit_RESET); // IN1 = 0
            break;

        case 1: // 播放/暂停 (NO2 接 0 欧姆)
            GPIO_WriteBit(GPIOA, GPIO_Pin_1, Bit_RESET); // IN2 = 0
            GPIO_WriteBit(GPIOA, GPIO_Pin_0, Bit_SET);   // IN1 = 1
            break;

        case 2: // 音量 + (NO3 接 240 欧姆)
            GPIO_WriteBit(GPIOA, GPIO_Pin_1, Bit_SET);   // IN2 = 1
            GPIO_WriteBit(GPIOA, GPIO_Pin_0, Bit_RESET); // IN1 = 0
            break;

        case 3: // 音量 - (NO4 接 470 欧姆)
            GPIO_WriteBit(GPIOA, GPIO_Pin_1, Bit_SET);   // IN2 = 1
            GPIO_WriteBit(GPIOA, GPIO_Pin_0, Bit_SET);   // IN1 = 1
            break;
    }
}

/*********************************************************************
 * @fn      EC11_Process_1ms
 * @brief   带有缓冲队列的旋转编码器与线控模拟处理
 *********************************************************************/
void EC11_Process_1ms(void)
{
    /* 1. 读取引脚当前电平 */
    uint8_t pin_A  = GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_11);
    uint8_t pin_B  = GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_3);
    uint8_t pin_SW = GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_0);

    static uint8_t last_A = 1;
    static int8_t  vol_queue = 0;     // 核心机制：音量脉冲队列（正数为加，负数为减）

    static uint8_t sw_debounce = 0;   // 中键防抖
    static uint8_t sw_pressed = 0;    // 中键状态

    static uint8_t pulse_state = 0;   // 发射状态机 (0:空闲, 1:按下导通, 2:松开断开)
    static uint8_t pulse_timer = 0;   // 状态维持时间

    /* --------- 1. 极速捕获旋转并压入队列 --------- */
    if (last_A == 1 && pin_A == 0)
    {
        if (pin_B == 1)
        {
            if (vol_queue < 20) vol_queue++; // 顺时针，加入队列
        }
        else
        {
            if (vol_queue > -20) vol_queue--; // 逆时针，加入队列
        }
    }
    last_A = pin_A; // 这里不再锁定定时器，允许极快连续旋转累加

    /* --------- 2. 中键按下的平滑消抖 --------- */
    if (pin_SW == 0) // 按下接地
    {
        if (sw_debounce < 20) sw_debounce++;
        if (sw_debounce == 15) sw_pressed = 1; // 稳定按下了 15ms
    }
    else
    {
        if (sw_debounce > 0) sw_debounce--;
        if (sw_debounce == 0) sw_pressed = 0;  // 彻底松开
    }

    /* --------- 3. 执行脉冲发射队列 --------- */
    if (sw_pressed)
    {
        // 物理中键优先级最高，直接霸占开关切到播放/暂停，打断音量队列
        TS3A5017_Select_Channel(1);
        pulse_state = 0; // 松开后，重新从空闲状态开始处理积累的音量
    }
    else
    {
        switch (pulse_state)
        {
            case 0: // 空闲状态：检查是否有待发送的音量脉冲
                if (vol_queue > 0)
                {
                    TS3A5017_Select_Channel(2); // 连通 240Ω
                    vol_queue--;                // 消耗一次记录
                    pulse_timer = 50;           // 导通维持 25ms
                    pulse_state = 1;
                }
                else if (vol_queue < 0)
                {
                    TS3A5017_Select_Channel(3); // 连通 470Ω
                    vol_queue++;                // 消耗一次记录
                    pulse_timer = 50;           // 导通维持 25ms
                    pulse_state = 1;
                }
                else
                {
                    TS3A5017_Select_Channel(0); // 队列清空，保持物理悬空
                }
                break;

            case 1: // 导通状态：欺骗声卡说“我按下了”
                if (pulse_timer > 0)
                {
                    pulse_timer--;
                }
                else
                {
                    TS3A5017_Select_Channel(0); // 必须切回全断开状态
                    pulse_timer = 40;           // 强制断开至少 25m
                    pulse_state = 2;
                }
                break;

            case 2: // 强制断开状态：给声卡反应时间，让它识别出“这是两次独立的点击”
                if (pulse_timer > 0)
                {
                    pulse_timer--;
                }
                else
                {
                    pulse_state = 0; // 冷却完毕，回到空闲，允许发射队列里的下一个脉冲
                }
                break;
        }
    }
}

/*********************************************************************
 * @fn      main
 *
 * @brief   Main program.
 *
 * @return  none
 */
int main(void)
{
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_1);
    SystemCoreClockUpdate();
    Delay_Init();

    /* 1. 初始化 CH443K 模拟开关控制引脚 */
    Switch_Init();

    /* 2. 初始化 PD 底层比较器 (内部会自动调用 PD_SINK_Init) */
    PD_Init();

    /* -------- 新增 HID 初始化 -------- */
    KB_Scan_Init();
    /* 初始化 USBFS 设备 (开启内部上拉，模拟设备插入) */
    USBFS_RCC_Init();
    USBFS_Device_Init( ENABLE , PWR_VDD_SupplyVoltage());
    USB_Sleep_Wakeup_CFG();
    Analog_Switch_Init();  // TS3A5017 输出引脚 (PA0, PA1)
    EC11_Init();           // 旋转编码器输入引脚 (PB11, PB3, PB0)
    /* 3. 启动定时器，系统主频设为 48MHz，这里分频后产生 1ms 中断 */
    TIM1_Init( 999, 48-1);

    while(1)
    {
        /* 获取定时器毫秒级时间差（关中断保证数据原子性） */
        TIM_ITConfig( TIM1, TIM_IT_Update , DISABLE );
        Tmr_Ms_Dlt = Tim_Ms_Cnt - Tmr_Ms_Cnt_Last;
        Tmr_Ms_Cnt_Last = Tim_Ms_Cnt;
        TIM_ITConfig( TIM1, TIM_IT_Update , ENABLE );

        PD_Ctl.Det_Timer += Tmr_Ms_Dlt;
        if(USBFS_DevEnumStatus)
        {
            if( USBFS_DevEnumStatus )
            {
                /* Handle keyboard scan data */
                KB_Scan_Handle();
            }
        }
        /* 定时轮询：每隔 5ms 唤醒一次底层 CC 电平检测 */
        if( PD_Ctl.Det_Timer > 4 )
        {
            PD_Ctl.Det_Timer = 0;

            /* 调用 PD_Process.c 中的核心检测防抖函数 */
            /* 该函数内部会自动处理手机插拔、CC 判定并直接翻转 PB12 */
            PD_Det_Proc();
        }

        /* 原版的 PD_Main_Proc() 巨型状态机已被彻底删除 */
    }
}


/*********************************************************************
 * @fn      TIM1_UP_IRQHandler
 *
 * @brief   This function handles TIM1 interrupt.
 *
 * @return  none
 */
void TIM1_UP_IRQHandler(void)
{
    if( TIM_GetITStatus( TIM1, TIM_IT_Update ) != RESET )
    {
        Tim_Ms_Cnt++;
        /* 每 1ms 扫描一次 PA4-PA7 键盘矩阵 */
        KB_Scan();
        EC11_Process_1ms(); // 新增的 EC11 与线控处理
        TIM_ClearITPendingBit( TIM1, TIM_IT_Update );
    }
}
