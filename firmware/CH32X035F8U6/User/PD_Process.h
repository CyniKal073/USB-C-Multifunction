/********************************** (C) COPYRIGHT *******************************
* File Name          : PD_Process.h
* Author             : WCH
* Version            : V1.0.0
* Date               : 2023/04/06
* Description        : This file contains all the functions prototypes for the
*                      PD library.
*********************************************************************************
* Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
* Attention: This software (modified or not) and binary are used for
* microcontroller manufactured by Nanjing Qinheng Microelectronics.
*******************************************************************************/

#ifndef USER_PD_PROCESS_H_
#define USER_PD_PROCESS_H_

#ifdef __cplusplus
 extern "C" {
#endif

#include "ch32x035_usbpd.h"

/******************************************************************************/
/* Variable extents */
extern UINT8  Tmr_Ms_Cnt_Last;
extern UINT8  Tmr_Ms_Dlt;
extern volatile UINT8  Tim_Ms_Cnt;

extern UINT8  PDO_Len;
extern PD_CONTROL PD_Ctl;

extern UINT8 send_data[ ];
extern UINT8 PD_Ack_Buf[ ];

extern __attribute__ ((aligned(4))) UINT8 PD_Rx_Buf[ 34 ];
extern __attribute__ ((aligned(4))) UINT8 PD_Tx_Buf[ 34 ];


/***********************************************************************************************************************/
/* Function extensibility */
extern void PD_SINK_Init( void );
extern void PD_PHY_Reset( void );
extern void PD_Init( void );
extern UINT8 PD_Detect( void );
extern void PD_Det_Proc( void );
extern void PD_Main_Proc( void );


#ifdef __cplusplus
}
#endif

#endif /* USER_PD_PROCESS_H_ */
