/********************************** (C) COPYRIGHT *******************************
 * File Name          : usbd_composite_km.c
 * Author             : WCH
 * Version            : V1.0.0
 * Date               : 2023/04/06
 * Description        : USB keyboard and mouse processing.
*********************************************************************************
* Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
* Attention: This software (modified or not) and binary are used for 
* microcontroller manufactured by Nanjing Qinheng Microelectronics.
*******************************************************************************/


/*******************************************************************************/
/* Header Files */
#include <ch32x035_usbfs_device.h>
#include "usbd_composite_km.h"
#include "debug.h"

/*******************************************************************************/
/* Global Variable Definition */

/* Keyboard */
volatile uint8_t  KB_Scan_Done = 0x00;                                          // Keyboard Keys Scan Done
volatile uint16_t KB_Scan_Result = 0x00F0;                                      // Keyboard Keys Current Scan Result
volatile uint16_t KB_Scan_Last_Result = 0x00F0;                                // Keyboard Keys Last Scan Result
uint8_t  KB_Data_Pack[ 8 ] = { 0x00 };                                          // Keyboard IN Data Packet
volatile uint8_t  KB_LED_Last_Status = 0x00;                                    // Keyboard LED Last Result
volatile uint8_t  KB_LED_Cur_Status = 0x00;                                     // Keyboard LED Current Result
volatile uint16_t scan_cnt = 0;
volatile uint16_t scan_result = 0;


/*********************************************************************
 * @fn      KB_Scan_Init
 * @brief   初始化 GPIOA (PA4-PA7) 作为键盘扫描输入
 *********************************************************************/
void KB_Scan_Init( void )
{
    GPIO_InitTypeDef GPIO_InitStructure = { 0 };
    RCC_APB2PeriphClockCmd( RCC_APB2Periph_GPIOA, ENABLE );

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_4 | GPIO_Pin_5 | GPIO_Pin_6 | GPIO_Pin_7;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU; // 上拉输入
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init( GPIOA, &GPIO_InitStructure );
}

/*********************************************************************
 * @fn      KB_Sleep_Wakeup_Cfg
 *
 * @brief   Configure keyboard wake up mode.
 *
 * @return  none
 */
void KB_Sleep_Wakeup_Cfg( void )
{
    EXTI_InitTypeDef EXTI_InitStructure = { 0 };

    /* Enable GPIOB clock */
    RCC_APB2PeriphClockCmd( RCC_APB2Periph_AFIO, ENABLE );

    GPIO_EXTILineConfig( GPIO_PortSourceGPIOA, GPIO_PinSource4 );
    EXTI_InitStructure.EXTI_Line = EXTI_Line4;
    EXTI_InitStructure.EXTI_Mode = EXTI_Mode_Event;
    EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Falling;
    EXTI_InitStructure.EXTI_LineCmd = ENABLE;
    EXTI_Init( &EXTI_InitStructure );

    GPIO_EXTILineConfig( GPIO_PortSourceGPIOA, GPIO_PinSource5 );
    EXTI_InitStructure.EXTI_Line = EXTI_Line5;
    EXTI_InitStructure.EXTI_Mode = EXTI_Mode_Event;
    EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Falling;
    EXTI_InitStructure.EXTI_LineCmd = ENABLE;
    EXTI_Init( &EXTI_InitStructure );

    GPIO_EXTILineConfig( GPIO_PortSourceGPIOA, GPIO_PinSource6 );
    EXTI_InitStructure.EXTI_Line = EXTI_Line6;
    EXTI_InitStructure.EXTI_Mode = EXTI_Mode_Event;
    EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Falling;
    EXTI_InitStructure.EXTI_LineCmd = ENABLE;
    EXTI_Init( &EXTI_InitStructure );

    GPIO_EXTILineConfig( GPIO_PortSourceGPIOA, GPIO_PinSource7 );
    EXTI_InitStructure.EXTI_Line = EXTI_Line7;
    EXTI_InitStructure.EXTI_Mode = EXTI_Mode_Event;
    EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Falling;
    EXTI_InitStructure.EXTI_LineCmd = ENABLE;
    EXTI_Init( &EXTI_InitStructure );

    EXTI->INTENR |= EXTI_INTENR_MR4 | EXTI_INTENR_MR5 | EXTI_INTENR_MR6 | EXTI_INTENR_MR7;
}

/*********************************************************************
 * @fn      KB_Scan
 * @brief   键盘扫描底层防抖
 *********************************************************************/
void KB_Scan( void )
{
    static uint16_t scan_cnt = 0;
    static uint16_t scan_result = 0;

    scan_cnt++;
    if( ( scan_cnt % 10 ) == 0 )
    {
        scan_cnt = 0;
        /* 使用 0x00F0 掩码适配 PA4-PA7 */
        if( scan_result == ( GPIO_ReadInputData( GPIOA ) & 0x00F0 ) )
        {
            KB_Scan_Done = 1;
            KB_Scan_Result = scan_result;
        }
    }
    else if( ( scan_cnt % 5 ) == 0 )
    {
        scan_result = ( GPIO_ReadInputData( GPIOA ) & 0x00F0 );
    }
}

/*********************************************************************
 * @fn      KB_Scan_Handle
 * @brief   安全无 Bug 的数据打包与发送函数
 *********************************************************************/
void KB_Scan_Handle( void )
{
    uint8_t status;
    static uint8_t flag = 0; // 0:空闲, 1:等待发送

    if( KB_Scan_Done )
    {
        KB_Scan_Done = 0;
        if( KB_Scan_Result != KB_Scan_Last_Result )
        {
            KB_Scan_Last_Result = KB_Scan_Result;

            memset( KB_Data_Pack, 0x00, sizeof( KB_Data_Pack ) );
            uint8_t index = 2;

            if( (KB_Scan_Result & GPIO_Pin_4) == 0 ) { KB_Data_Pack[0] = 0x01;}
            if( (KB_Scan_Result & GPIO_Pin_5) == 0 ) { KB_Data_Pack[index++] = 0x29; }
            if( (KB_Scan_Result & GPIO_Pin_6) == 0 ) { KB_Data_Pack[index++] = 0x28; }
            if( (KB_Scan_Result & GPIO_Pin_7) == 0 ) { KB_Data_Pack[index++] = 0x2C; }

            flag = 1; // 数据准备完毕，触发发送
        }
    }

    if( flag )
    {
        /* 如果端点忙碌，本次不清除 flag，下个 1ms 循环继续死磕重试！ */
        status = USBFS_Endp_DataUp( DEF_UEP1, KB_Data_Pack, sizeof( KB_Data_Pack ), DEF_UEP_CPY_LOAD );
        if( status == 0 ) // 0 代表 READY，发送成功
        {
            flag = 0;
        }
    }
}
/*********************************************************************
 * @fn      USB_Sleep_Wakeup_CFG
 *
 * @brief   Configure USB wake up mode
 *
 * @return  none
 */
void USB_Sleep_Wakeup_CFG( void )
{
    EXTI_InitTypeDef EXTI_InitStructure = { 0 };

    EXTI_InitStructure.EXTI_Line = EXTI_Line28;
    EXTI_InitStructure.EXTI_Mode = EXTI_Mode_Event;
    EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Rising;
    EXTI_InitStructure.EXTI_LineCmd = ENABLE;
    EXTI_Init( &EXTI_InitStructure );
}

/*********************************************************************
 * @fn      MCU_Sleep_Wakeup_Operate
 * @brief   Perform sleep operation (仅支持 PA4-PA7 键盘唤醒)
 *********************************************************************/
void MCU_Sleep_Wakeup_Operate( void )
{
    printf( "Sleep\r\n" );
    __disable_irq();

    /* 1. 清除键盘的外部唤醒中断标志位 */
    EXTI_ClearFlag( EXTI_Line4 | EXTI_Line5 | EXTI_Line6 | EXTI_Line7 );

    /* (已删除鼠标 EXTI_Line14-17 的清除代码) */

    /* 2. 进入睡眠模式，等待事件(WFE)唤醒 */
    PWR_EnterSTOPMode(PWR_STOPEntry_WFE);

    /* 3. 唤醒后，重新初始化系统时钟和 USBFS 时钟 */
    SystemInit();
    SystemCoreClockUpdate();
    USBFS_RCC_Init();

    /* 4. 检测是否是由于键盘按键 (PA4-PA7) 触发的唤醒 */
    if( EXTI_GetFlagStatus( EXTI_Line4 | EXTI_Line5 | EXTI_Line6 | EXTI_Line7 ) != RESET  )
    {
        EXTI_ClearFlag( EXTI_Line4 | EXTI_Line5 | EXTI_Line6 | EXTI_Line7 );
        USBFS_Send_Resume( );
    }

    /* (已删除判断鼠标引脚唤醒的 else if 分支) */

    __enable_irq();
}
