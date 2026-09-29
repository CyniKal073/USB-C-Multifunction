#include "debug.h"
#include "PD_Process.h"
#include "string.h"

/* 移除了所有 PD_Rx_Buf, PD_Tx_Buf, PDO_Len 等报文相关的变量 */

UINT8  Tmr_Ms_Cnt_Last;      /* 系统定时器毫秒计时上次值 */
UINT8  Tmr_Ms_Dlt;           /* 系统定时器毫秒计时本次间隔值 */
PD_CONTROL PD_Ctl;           /* PD 控制结构体 */

/*********************************************************************
 * @fn      PD_SINK_Init
 * @brief   初始化 SNK 检测模式 (注意：芯片外部必须自己挂 5.1k 下拉电阻)
 */
void PD_SINK_Init( void )
{
    PD_Ctl.Flag.Bit.PR_Role = 0;
    /* 配置 CC 引脚为比较器模式，用于检测手机端送来的 Rp 上拉电压 */
    USBPD->PORT_CC1 = CC_CMP_66 | CC_PD;
    USBPD->PORT_CC2 = CC_CMP_66 | CC_PD;
}

/*********************************************************************
 * @fn      PD_PHY_Reset
 * @brief   重置物理层状态
 */
void PD_PHY_Reset( void )
{
    PD_SINK_Init( );
    PD_Ctl.Flag.Bit.Stop_Det_Chk = 0;
    PD_Ctl.PD_State = STA_IDLE;
}

/*********************************************************************
 * @fn      PD_Init
 * @brief   初始化 PD 模块外设 (砍掉了 DMA 和 中断相关的配置)
 */
void PD_Init( void )
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO, ENABLE);
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_USBPD, ENABLE);

    /* 配置 PC14(CC1) 和 PC15(CC2) 为浮空输入 */
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_14 | GPIO_Pin_15;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOC, &GPIO_InitStructure);

    AFIO->CTLR |= USBPD_IN_HVT | USBPD_PHY_V33;

    memset( &PD_Ctl.PD_State, 0x00, sizeof( PD_CONTROL ) );
    PD_PHY_Reset( );

    /* 注意：彻底移除了原版代码中开启接收中断的 PD_Rx_Mode() */
}

/*********************************************************************
 * @fn      PD_Detect
 * @brief   底层物理电平检测核心逻辑
 * @return  0:无连接; 1:CC1 正插; 2:CC2 反插
 */
UINT8 PD_Detect( void )
{
    UINT8  ret = 0;
    UINT8  cmp_cc1 = 0;
    UINT8  cmp_cc2 = 0;

    if(PD_Ctl.Flag.Bit.Connected)
    {
        /* 已经连接后，检测是否拔出。
         * 这里使用最符合 Type-C 规范且无需额外硬件走线的“CC 电压跌落法”。
         * 当手机（Source）拔出时，其端提供的 Rp 上拉电阻消失，
         * 此时 CC 线在板载 5.1k 下拉电阻的作用下，会瞬间跌回 0V。
         */
         USBPD->PORT_CC1 &= ~( CC_CMP_Mask | PA_CC_AI );
         USBPD->PORT_CC1 |= CC_CMP_22; // 切入 0.22V 比较器阈值
         Delay_Us(2);
         UINT8 cc1_is_high = (USBPD->PORT_CC1 & PA_CC_AI); // 只要大于0.22V就会返回非零值

         USBPD->PORT_CC2 &= ~( CC_CMP_Mask | PA_CC_AI );
         USBPD->PORT_CC2 |= CC_CMP_22;
         Delay_Us(2);
         UINT8 cc2_is_high = (USBPD->PORT_CC2 & PA_CC_AI);

         if( (cc1_is_high == 0) && (cc2_is_high == 0) )
         {
             /* 两个 CC 均低于 0.22V，判定为彻底拔出 */
             PD_Ctl.Flag.Bit.Connected = 0;
             ret = 0;
             printf("Device Disconnected\r\n");
          }
          else
          {
              /* 依然保持连接，回传当前维持高电平的通道 */
              if(cc1_is_high) { ret = 1; }
              else { ret = 2; }
           }
    }
    else
    {
        /* 检测插入状态：切入 0.22V 比较器阈值 */
        USBPD->PORT_CC1 &= ~( CC_CMP_Mask|PA_CC_AI );
        USBPD->PORT_CC1 |= CC_CMP_22;
        Delay_Us(2);
        if( USBPD->PORT_CC1 & PA_CC_AI ) { cmp_cc1 |= bCC_CMP_22; }

        USBPD->PORT_CC2 &= ~( CC_CMP_Mask|PA_CC_AI );
        USBPD->PORT_CC2 |= CC_CMP_22;
        Delay_Us(2);
        if( USBPD->PORT_CC2 & PA_CC_AI ) { cmp_cc2 |= bCC_CMP_22; }

        if (USBPD->PORT_CC1 & CC_PD)
        {
            if ((cmp_cc1 & bCC_CMP_22) == bCC_CMP_22) { ret = 1; }
            if ((cmp_cc2 & bCC_CMP_22) == bCC_CMP_22)
            {
                if( ret ) { ret = 1; } /* 应对某些带有双上拉的奇葩线材，默认优选 CC1 */
                else { ret = 2; }
            }
        }
    }
    return( ret );
}

/*********************************************************************
 * @fn      PD_Det_Proc
 * @brief   连接状态防抖与事件触发 (定时器中轮询调用)
 */
void PD_Det_Proc( void )
{
    UINT8  status;

    if( PD_Ctl.Flag.Bit.Connected )
    {
        /* 已连接状态的维护逻辑 */
    }
    else
    {
        status = PD_Detect( );

        if( status == 0 ) { PD_Ctl.Det_Cnt = 0; }
        else { PD_Ctl.Det_Cnt++; }

        /* 连续 5 次检测到相同的状态，确认插入 (防抖) */
                if( PD_Ctl.Det_Cnt >= 5 )
                {
                    PD_Ctl.Det_Cnt = 0;
                    PD_Ctl.Flag.Bit.Connected = 1;

                    if( status == 1 )
                    {
                        printf("Phone Connected: CC1 (正插)\r\n");
                        /* PB12 输出低电平 (0)，控制 CH443K 导通通道 1 */
                        GPIO_WriteBit(GPIOB, GPIO_Pin_12, Bit_RESET);
                    }
                    else if( status == 2 )
                    {
                        printf("Phone Connected: CC2 (反插)\r\n");
                        /* PB12 输出高电平 (1)，控制 CH443K 导通通道 2 */
                        GPIO_WriteBit(GPIOB, GPIO_Pin_12, Bit_SET);
                    }
                }
    }
}

/*********************************************************************
 * @fn      PD_Main_Proc
 * @brief   原版的巨型状态机，现在可以直接留空，或者处理简单的用户逻辑
 */
void PD_Main_Proc( )
{
    /* 所有的 PD 握手状态机已删除，主循环仅需维持计时器给 PD_Det_Proc 即可 */
}
