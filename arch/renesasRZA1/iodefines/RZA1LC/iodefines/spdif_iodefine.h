/*******************************************************************************
* DISCLAIMER
* This software is supplied by Renesas Electronics Corporation and is only
* intended for use with Renesas products. No other uses are authorized. This
* software is owned by Renesas Electronics Corporation and is protected under
* all applicable laws, including copyright laws.
* THIS SOFTWARE IS PROVIDED "AS IS" AND RENESAS MAKES NO WARRANTIES REGARDING
* THIS SOFTWARE, WHETHER EXPRESS, IMPLIED OR STATUTORY, INCLUDING BUT NOT
* LIMITED TO WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE
* AND NON-INFRINGEMENT. ALL SUCH WARRANTIES ARE EXPRESSLY DISCLAIMED.
* TO THE MAXIMUM EXTENT PERMITTED NOT PROHIBITED BY LAW, NEITHER RENESAS
* ELECTRONICS CORPORATION NOR ANY OF ITS AFFILIATED COMPANIES SHALL BE LIABLE
* FOR ANY DIRECT, INDIRECT, SPECIAL, INCIDENTAL OR CONSEQUENTIAL DAMAGES FOR
* ANY REASON RELATED TO THIS SOFTWARE, EVEN IF RENESAS OR ITS AFFILIATES HAVE
* BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGES.
* Renesas reserves the right, without notice, to make changes to this software
* and to discontinue the availability of this software. By using this software,
* you agree to the additional terms and conditions found by accessing the
* following link:
* http://www.renesas.com/disclaimer*
* Copyright (C) 2017 Renesas Electronics Corporation. All rights reserved.
*******************************************************************************/
/*******************************************************************************
* File Name : spdif_iodefine.h
* $Rev: 29 $
* $Date:: 2016-12-20 17:40:26 +0900#$
* Description : Definition of I/O Register for RZ/A1LC (V3.00c)
******************************************************************************/
#ifndef SPDIF_IODEFINE_H
#define SPDIF_IODEFINE_H
/* ->QAC 0639 : Over 127 members (C90) */
/* ->QAC 0857 : Over 1024 #define (C90) */
/* ->MISRA 18.4 : Pack unpack union */ /* ->SEC M1.6.2 */
/* ->SEC M1.10.1 : Not magic number */

#define SPDIF   (*(struct st_spdif   *)0xE8012000uL) /* SPDIF */


#define SPDIFTLCA (SPDIF.TLCA)
#define SPDIFTRCA (SPDIF.TRCA)
#define SPDIFTLCS (SPDIF.TLCS)
#define SPDIFTRCS (SPDIF.TRCS)
#define SPDIFTUI (SPDIF.TUI)
#define SPDIFRLCA (SPDIF.RLCA)
#define SPDIFRRCA (SPDIF.RRCA)
#define SPDIFRLCS (SPDIF.RLCS)
#define SPDIFRRCS (SPDIF.RRCS)
#define SPDIFRUI (SPDIF.RUI)
#define SPDIFCTRL (SPDIF.CTRL)
#define SPDIFSTAT (SPDIF.STAT)
#define SPDIFTDAD (SPDIF.TDAD)
#define SPDIFRDAD (SPDIF.RDAD)


typedef struct st_spdif
{
                                                           /* SPDIF            */
    volatile uint32_t  TLCA;                                   /*  TLCA            */
    volatile uint32_t  TRCA;                                   /*  TRCA            */
    volatile uint32_t  TLCS;                                   /*  TLCS            */
    volatile uint32_t  TRCS;                                   /*  TRCS            */
    volatile uint32_t  TUI;                                    /*  TUI             */
    volatile uint32_t  RLCA;                                   /*  RLCA            */
    volatile uint32_t  RRCA;                                   /*  RRCA            */
    volatile uint32_t  RLCS;                                   /*  RLCS            */
    volatile uint32_t  RRCS;                                   /*  RRCS            */
    volatile uint32_t  RUI;                                    /*  RUI             */
    volatile uint32_t  CTRL;                                   /*  CTRL            */
    volatile uint32_t  STAT;                                   /*  STAT            */
    volatile uint32_t  TDAD;                                   /*  TDAD            */
    volatile uint32_t  RDAD;                                   /*  RDAD            */
} r_io_spdif_t;


/* <-SEC M1.10.1 */
/* <-MISRA 18.4 */ /* <-SEC M1.6.2 */
/* <-QAC 0857 */
/* <-QAC 0639 */
#endif
