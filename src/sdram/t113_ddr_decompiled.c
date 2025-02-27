//------------------------------------------------------------------------------------------------

/*
 Author: Rep Stosw <repstosw2018@gmail.com>
 27.06.2022

 This file was made from disassembled and decompiled \tina-t113\lichee\brandy-2.0\spl-pub\board\t113\libsun8iw20p1_fes.a
 IDA Pro 7.5 SP3 + Hex-Rays decompiler

 Just add in your code:
 extern void sys_dram_init(void);
 and use it :)
*/

//------------------------------------------------------------------------------------------------

#include "hardware.h"

#if WITHSDRAMHW
#if CPUSTYPE_T113

#include "formats.h"
#include <string.h>

#define HIBYTE(x) ((x)>>24)
#define LOBYTE(x) (*((uint8_t *)(x)))	/* в присвоениях - слева */

#define HIWORD(x)  ((x)>>16)
#define HIDWORD(x) ((x)>>32)

#define WORD1(x) (((x)>>16) &0xFFFF)

#define MEMORY(x) (*(volatile unsigned int*)(x))

#define CPU_CONFIG_REG 		0x070005D4uL
#define CPU_CONFIG2_REG 	0x07010250uL
#define CPU_CONFIG3_REG 	0x07010254uL


struct dram_para_t
{
	//normal configuration
	unsigned int       	dram_clk;		// 	0	0 0x000
	unsigned int        dram_type;		// 	1	4 dram_type DDR2: 2 DDR3: 3 LPDDR2: 6 LPDDR3: 7 DDR3L: 31
	//unsigned int      lpddr2_type;	//		LPDDR2 type S4:0 S2:1 NVM:2
	unsigned int        dram_zq;		// 	2	8	do not need
	unsigned int		dram_odt_en;	// 	3	12

	//control configuration
	unsigned int		dram_para1;		// 	4	16
	unsigned int		dram_para2;		// 	5	20 high bit - autodetect DDR RAM size (high word - size in megabytes)

	//timing configuration
	unsigned int		dram_mr0;	// 	6	24
	unsigned int		dram_mr1;	// 	7	28
	unsigned int		dram_mr2;	// 	8	32
	unsigned int		dram_mr3;	// 	9	36
	unsigned int		dram_tpr0;	// 	10	40	DRAMTMG0
	unsigned int		dram_tpr1;	// 	11	44	DRAMTMG1
	unsigned int		dram_tpr2;	// 	12	48	DRAMTMG2	// +48
	unsigned int		dram_tpr3;	// 	13	52	DRAMTMG3
	unsigned int		dram_tpr4;	// 	14	56	DRAMTMG4
	unsigned int		dram_tpr5;	// 	15	60	DRAMTMG5
   	unsigned int		dram_tpr6;	// 	16	64	DRAMTMG8	// +64

	//reserved for future use
	unsigned int		dram_tpr7;	// 	17	68
	unsigned int		dram_tpr8;	// 	18	72
	unsigned int		dram_tpr9;	// 	19	76
	unsigned int		dram_tpr10;	// 	20	80
	unsigned int		dram_tpr11;	// 	21	84 lo
	unsigned int		dram_tpr12;	// 	22	88 hi
	unsigned int		dram_tpr13;	// 	23	92
};


static struct dram_para_t ddr3 =
{
	.dram_clk = 792,			// 0x000	0
	.dram_type = 3,				// 0x004	4
	.dram_zq = 0x7b7bfb,		// 0x008	8
	.dram_odt_en = 0x00,		// 0x00C	12
	.dram_para1 = 0x000010d2,	// 0x010	16
	.dram_para2 = 0x0000,		// 0x014	20
	.dram_mr0 = 0x1c70,			// 0x018	24
	.dram_mr1 = 0x042,			// 0x01C	28
	.dram_mr2 = 0x18,			// 0x020	32
	.dram_mr3 = 0x0,			// 0x024	36
	.dram_tpr0 = 0x004A2195,	// 0x028	40
	.dram_tpr1 = 0x02423190,	// 0x02C	44
	.dram_tpr2 = 0x0008B061,	// 0x030	48
	.dram_tpr3 = 0xB4787896,	// 0x034	52
	.dram_tpr4 = 0x0,			// 0x038	56
	.dram_tpr5 = 0x48484848,	// 0x03C	60
	.dram_tpr6 = 0x00000048,	// 0x040	64
	.dram_tpr7 = 0x1620121e,	// 0x044	68
	.dram_tpr8 = 0x0,			// 0x048	72
	.dram_tpr9 = 0x0,			// 0x04C	76
	.dram_tpr10 = 0x0,			// 0x050	80
	.dram_tpr11 = 0x00340000,	// 0x054	84
	.dram_tpr12 = 0x00000046,	// 0x058	88
	.dram_tpr13 = 0x34000100,	// 0x05C	92
};

//------------------------------------------------------------------------------------------------

static int dram_vol_set(int a1, struct dram_para_t * a2param);
static int auto_scan_dram_config(int *a1, struct dram_para_t * a2param);
static int auto_scan_dram_rank_width(int *a1, struct dram_para_t * a2param);
static int mctl_core_init(int *a1);
static int mctl_sys_init(int *a1);
static int ccm_set_pll_ddr_clk(int a1, int *a2);
static int dram_disable_all_master(void);
static uint32_t *mctl_vrefzq_init(uint32_t *result);
static uint32_t *mctl_com_init(uint32_t *result);
static int auto_set_timing_para(int a1);
static unsigned int auto_cal_timing(int a1, int a2);
static int mctl_channel_init(int a1, unsigned int *a2);
static int eye_delay_compensation(int a1);
static int dram_enable_all_master(void);
static int dramc_simple_wr_test(unsigned int a1, int a2);
static int dqs_gate_detect(int a1, int a2, unsigned int a3);
static int auto_scan_dram_size(int *a1, struct dram_para_t * a2param);
static int memcpy_self(int result, char *a2, int a3);
static int init_DRAM(int a1, int a2, struct dram_para_t * a2param);
static int sid_read_ldoB_cal(int result);
static int DRAMC_get_dram_size(void);
static void mctl_phy_ac_remapping(int a1);

//------------------------------------------------------------------------------------------------

static inline void sdelay(int loops)
{
 __asm__ __volatile__ (
                       "1:\n" "subs %0, %1, #1\n"
		       "bne 1b":"=r" (loops):"0"(loops)
                      );
}

static int _usdelay(int loops)
{
 sdelay(loops*100);
 return 1;
}

//------------------------------------------------------------------------------------------------

static int init_DRAM(int a1,int a2, struct dram_para_t * a2param)
{
  int v9; // r3
  int v15; // r3
  unsigned int v16; // r2
  int v22; // r1
  int v26; // r5
  int v27; // r5
  int v28; // r3
  int v29; // r1
  int v30; // r0
  unsigned int v31; // r3
  //int v32; // r3
  unsigned int v33; // r2
  int v38; // r0
  int v40; // r3
  int v41; // r2

  if ( (a2param->dram_tpr13 & 0x10000) != 0 )	// 92
  {
    SYS_CFG->RESCAL_CTRL_REG |= 0x100u;
    SYS_CFG->RES240_CTRL_REG = 0;
    _usdelay(10);
  }
  else
  {
    MEMORY(CPU_CONFIG3_REG) = 0;
    SYS_CFG->RESCAL_CTRL_REG &= 0xFFFFFFFC;
    _usdelay(10);

    SYS_CFG->RESCAL_CTRL_REG &= 0xFFFFFEFB;
    SYS_CFG->RESCAL_CTRL_REG |= 2u;
    _usdelay(10);

    SYS_CFG->RESCAL_CTRL_REG |= 1u;
    _usdelay(20);

//    PRINTF("ZQ value = 0x%x\n", MEMORY(0x0300016C), v3, 50332012);
  }

/*
  v4 = get_pmu_exist();

  PRINTF("get_pmu_exist() = %d\n", v4, v5, v6);

  if ( v4 >= 0 )
  {
    v19 = *(uint32_t *)(a2 + 4);
    if ( v19 == 2 )
    {
      v20 = 1800;
    }
    else
    {
      if ( v19 != 3 )
        goto LABEL_5;
      v20 = 1500;
    }

    set_ddr_voltage(v20, v7, v8);

    goto LABEL_5;
  }
*/

  dram_vol_set(a2, a2param); //OK

LABEL_5:
  v9 = a2param->dram_tpr13;	// 92

  if ( (v9 & 1) == 0 && !auto_scan_dram_config((int *)a2, a2param) ) //OK
    return 0;

/*
  PRINTF("DRAM BOOT DRIVE INFO: %s\n", (int)"V0.33", v8, v9);
  PRINTF("DRAM CLK = %d MHz\n", *(uint32_t *)a2, v10, v11);
  PRINTF("DRAM Type = %d (2:DDR2,3:DDR3)\n", *(uint32_t *)(a2 + 4), v12, v13);
*/

  v15 = *(uint32_t *)(a2 + 12);	// dram_odt_en = 0x00
  v16 = v15 << 31;

/*
  if ( (v15 & 1) != 0 )
    PRINTF("DRAMC ZQ value: 0x%x\n", *(uint32_t *)(a2 + 8), v16, v15);
  else
    PRINTF("DRAMC read ODT  off.\n", v14, v16, v15);
*/

  v22 = *(uint32_t *)(a2 + 28);	// dram_mr1 = 0x042

/*
  if ( (v22 & 0x44) != 0 )
    PRINTF("DRAM ODT value: 0x%x.\n", v22, v17, v18);
  else
    PRINTF("DRAM ODT off.\n", v22, v17, v18);
*/

  v26 = mctl_core_init((int *)a2); //OK

  if ( v26 )
  {
    v27 = *(uint32_t *)(a2 + 20);	// dram_para2 = 0x0000

    if ( v27 >= 0 )
    {
      v26 = DRAMC_get_dram_size(); //OK

//      PRINTF("DRAM SIZE =%d M\n", v26, v42, v43);

      a2param->dram_para2 |= (v26 << 16);	// dram_para2 = 0x0000
    }
    else
    {
      v26 = HIWORD(v27) & 0x7FFF;
    }

    if ( (a2param->dram_tpr13 & 0x40000000) != 0 )	// 92
    {
      v28 = a2param->dram_tpr8;	// dram_tpr8 = 0x0,			// 0x048	72

      if ( !v28 )
        v28 = 0x10000200;

      DDRPHYC->PHYC_REG_0A0 = v28;
      DDRPHYC->PHYC_REG_09C = 1034;
      DDRPHYC->PHYC_REG_004 |= 1u;

//      PRINTF("Enable Auto SR\n", 1034, DDRPHYC_BASE + 0x004, DDRPHYC->PHYC_REG_004);

    }
    else
    {
      DDRPHYC->PHYC_REG_0A0 = HIWORD(DDRPHYC->PHYC_REG_0A0) << 16;
      DDRPHYC->PHYC_REG_004 &= 0xFFFFFFFE;
    }

    v29 = *(uint32_t *)(a2 + 92);	// dram_tpr13
    v30 = *(uint32_t *)(a2 + 4);	// dram_type
    v31 = DDRPHYC->PHYC_REG_100 & 0xFFFF0FFF;

    if ( (v29 & 0x200) != 0 || v30 == 6 )
      v31 |= 0x5000u;

    DDRPHYC->PHYC_REG_100 = v31;
    DDRPHYC->PHYC_REG_140 |= 0x80000000;

    if ( (v29 & 0x100) != 0 )
      DDRPHYC->PHYC_REG_0B8 |= 0x0300u;

    //v32 = DDRPHYC_BASE + 0x108;

    if ( (v29 & 0x4000000) != 0 )
      DDRPHYC->PHYC_REG_108 &= ~ 0x2000;
    else
      DDRPHYC->PHYC_REG_108 |= 0x2000;

    if ( v30 == 7 )
    {
      //v33 = DDRPHYC_BASE + 0x07C;
    	DDRPHYC->PHYC_REG_07C = (DDRPHYC->PHYC_REG_07C & 0xFFF0FFFF) | 0x10000;
    }

/*
    PRINTF("dram_tpr4:0x%x\n", *(uint32_t *)(a2 + 56), v33, v32);
    PRINTF("PLL_DDR_CTRL_REG:0x%x\n", CCU->PLL_DDR_CTRL_REG, v34, 33558544);
    PRINTF("DRAM_CLK_REG:0x%x\n", CCU->DRAM_CLK_REG, v35, 33560576);
    PRINTF("[TIMING DEBUG] MR2= 0x%x\n", *(uint16_t *)(a2 + 32), v36, v37);
*/

    v38 = dram_enable_all_master(); //OK

    v40 = *(uint32_t *)(a2 + 92);
    v41 = 8 * v40;

    if ( (v40 & 0x10000000) != 0 )
    {
      v40 = MEMORY(CPU_CONFIG_REG) << 15;

      if ( (MEMORY(CPU_CONFIG_REG) & 0x10000) == 0 )
      {
        v38 = dramc_simple_wr_test(v26, 4096); //OK

        if ( v38 )
          return 0;
      }
    }

//    handler_super_standby(v38, v39, v41, v40);

  }
  else
  {

//    PRINTF("DRAM initial error : 1 !\n", v23, v24, v25);

  }

  return v26;
}

static int dram_vol_set(int a1, struct dram_para_t * a2param)
{
  int v2; // r3
  int v3; // r2

  v2 = a2param->dram_type;	// 4

  if ( v2 == 2 )
  {
    v3 = 47;
  }
  else if ( v2 == 3 )
  {
    v3 = 25;
  }
  else
  {
    v3 = 0;
  }

  SYS_CFG->SYS_LDO_CTRL_REG = (SYS_CFG->SYS_LDO_CTRL_REG & 0xFFFF00FF | (v3 << 8)) & 0xFFDFFFFF;

  _usdelay(1);

  return sid_read_ldoB_cal(a1); //OK
}

static int sid_read_ldoB_cal(int result) //ok
{
  int v1; // r3
  int v2; // r2

  v1 = MEMORY(SID_BASE + 0x21C);

  if ( MEMORY(SID_BASE + 0x21C) )
  {
    v2 = *(uint32_t *)(result + 4);

    if ( v2 != 2 )
    {

      if ( v2 == 3 )
      {

        if ( MEMORY(SID_BASE + 0x21C) > 0x20u )
          v1 = MEMORY(SID_BASE + 0x21C) - 22;

      }
      else
      {
        v1 = 0;
      }

    }

    SYS_CFG->SYS_LDO_CTRL_REG = (SYS_CFG->SYS_LDO_CTRL_REG & 0xFFFF00FF) | (v1 << 8);

  }

  return result;
}

static int auto_scan_dram_config(int *a1, struct dram_para_t * a2param)
{
  int v5; // r4
  int v8; // r3

  if ( (a1[23] & 0x4000) == 0 )
  {
    v5 = auto_scan_dram_rank_width(a1, a2param); //OK

    if ( !v5 )
    {
//      v6 = "[ERROR DEBUG] auto scan dram rank&width fail !\n";

//LABEL_4:

//      PRINTF(v6, v2, v3, v4);

      return v5;
    }
  }

  if ( (a1[23] & 1) == 0 )
  {
    v5 = auto_scan_dram_size(a1, a2param); //OK
    if ( !v5 )
    {
//      v6 = "[ERROR DEBUG] auto scan dram size fail !\n";

      //goto LABEL_4;
//      PRINTF(v6, v2, v3, v4);
     return v5;
    }
  }

  v8 = a1[23];
  v5 = 1;

  if ( (v8 & 0x8000) == 0 )
    a1[23] = v8 | 0x6003;

  return v5;
}

static int auto_scan_dram_rank_width(int *a1, struct dram_para_t * a2param)
{
  int v1; // r6
  int v3; // r5
  int v4; // r1
  unsigned int v5; // r2
  int result; // r0

  v1 = a1[4];
  a1[4] = 0x00B000B0;
  v3 = a1[23];
  a1[5] = (a1[5] & 0xFFFFFFF0) | 0x1000;
  a1[23] = (v3 & 0xFFFFFFF7) | 5;

  mctl_core_init(a1); //OK

  if ( (DDRPHYC->PHYC_REG_010 & 0x100000) != 0 || !dqs_gate_detect((int)a1, v4, v5) ) //OK
    return 0;

  a1[23] = v3;
  result = 1;
  a1[4] = v1;

  return result;
}

static int mctl_core_init(int *a1) //OK
{
  uint32_t *v2; // r0

  mctl_sys_init(a1); //OK

  v2 = mctl_vrefzq_init((void*)a1); //OK

  mctl_com_init(v2); //OK

  mctl_phy_ac_remapping((int)a1); //OK

  auto_set_timing_para((int)a1); //OK

  return mctl_channel_init(0, (unsigned int *)a1); //OK
}

static int mctl_sys_init(int *a1) //OK
{
	CCU->MBUS_CLK_REG &= 0xBFFFFFFFu;
	CCU->DRAM_BGR_REG &= 0xFFFFFFFEu;
	CCU->DRAM_BGR_REG &= 0xFFFEFFFFu;
	CCU->DRAM_CLK_REG &= 0xBFFFFFFFu;
	CCU->DRAM_CLK_REG &= 0x7FFFFFFFu;
	CCU->DRAM_CLK_REG |= 0x08000000u;
	_usdelay(10);

	*a1 = (unsigned int) ccm_set_pll_ddr_clk(0, a1) >> 1; //OK

	_usdelay(100);

	dram_disable_all_master(); //OK

	CCU->DRAM_BGR_REG |= 0x00010000u;
	CCU->MBUS_CLK_REG |= 0x40000000u;
	CCU->DRAM_CLK_REG |= 0x40000000u;
	_usdelay(5);

	CCU->DRAM_BGR_REG |= 1u;
	CCU->DRAM_CLK_REG |= 0x80000000;
	CCU->DRAM_CLK_REG |= 0x8000000u;
	_usdelay(5);

	DDRPHYC->PHYC_REG_00C = 0x8000;
	_usdelay(10);

	return 0;
}

static int ccm_set_pll_ddr_clk(int a1, int *a2) //OK
{
	const unsigned ref_MHz = 24;
  int v2; // r2
  unsigned int v3; // r5
  int result; // r0

  if ( (((unsigned int)a2[23] >> 6) & 1) == a1 )
    v2 = *a2;
  else
    v2 = a2[19];

  v3 = 2 * v2 / ref_MHz;

  CCU->PLL_DDR_CTRL_REG = CCU->PLL_DDR_CTRL_REG & 0xFFF800FC |
	  0xC0000000 |
	  ((v3 - 1) << 8) |
	  0;
  CCU->PLL_DDR_CTRL_REG &= 0xDFFFFFFFuL;
  CCU->PLL_DDR_CTRL_REG |= 0x20000000uL;

  while ( (CCU->PLL_DDR_CTRL_REG & 0x10000000) == 0 )
    ;

  _usdelay(20);

  result = ref_MHz * v3;

  CCU->PLL_DDR_CTRL_REG |= 0x8000000u;
  CCU->DRAM_CLK_REG = CCU->DRAM_CLK_REG & 0xFCFFFCFC | 0x80000000;

  return result;
}

static int dram_disable_all_master(void) //OK
{
  MSI_MEMC->MEMC_REG_020 = 1;
  MSI_MEMC->MEMC_REG_024 = 0;
  MSI_MEMC->MEMC_REG_028 = 0;

  return _usdelay(10);
}

static uint32_t *mctl_vrefzq_init(uint32_t *result) //OK
{
  int v1; // r2

  v1 = result[23];

  if ( (v1 & 0x20000) == 0 )
  {
   DDRPHYC->PHYC_REG_110 = (DDRPHYC->PHYC_REG_110 & 0x80808080) | result[15];

    if ( (v1 & 0x10000) == 0 )
      DDRPHYC->PHYC_REG_114 = (result[16] & 0x7F) | (DDRPHYC->PHYC_REG_114 & 0xFFFFFF80);

  }

  return result;
}

static uint32_t *mctl_com_init(uint32_t *result) //OK
{
  int v1; // r5
  unsigned int v2; // r1
  int v3; // r7
  int v4; // r2
  unsigned int v5; // r5
  unsigned int v6; // r2
  int v7; // r3
  int v8; // r3
  unsigned int v9; // r4
  int v10; // r1
  unsigned int v11; // r6
  uintptr_t v12; // r5
  int v13; // r4
  int v14; // r1
  int v15; // r2
  unsigned int v16; // r3
  int v17; // r3
  int v18; // r3
  int v19; // r1

  v1 = result[1];
  v2 = result[5];

  MSI_MEMC->MEMC_REG_008 = (MSI_MEMC->MEMC_REG_008 & 0xFFFFC0FF) | 0x2000;

  v3 = v2 & 1;
  v4 = (v1 << 16) & 0x070000;
  v5 = v1 - 6;
  v6 = v4 | (MSI_MEMC->MEMC_REG_000 & 0xFF000FFF) | 0x400000;

  if ( (v2 & 1) != 0 )
    v7 = 0;
  else
    v7 = 4096;

  v8 = v7 | v6;

  if ( v5 <= 1 )
    v8 |= 0x80000u;
  else
    v6 = result[23];

  if ( v5 > 1 )
    v8 |= (v6 << 14) & 0x80000;

  MSI_MEMC->MEMC_REG_000 = v8;

  v9 = v2 >> 12;

  if ( (v2 & 0x100) != 0 )
  {

    if ( (uint16_t) v2 >> 12 == 1 )
      v10 = 2;
    else
      v10 = 1;

  }
  else
  {
    v10 = 1;
  }

  v11 = result[4];
  v12 = MSI_MEMC_BASE;
  v13 = v9 & 3;
  v14 = 16 * v10 + 4;
  v15 = 4;

  do
  {
    v16 = (uint8_t) (16 * ((v11 >> v15) - 1)) |
    		((4 * (v11 >> (v15 + 8))) & 4) |
			(*(volatile uint32_t *) v12 & 0xFFFFF000) |
			v13;
    switch ( (v11 >> (v15 - 4)) & 0xF )
    {
      case 1u:
        v17 = v16 | 0x0700;
        break;
      case 2u:
        v17 = v16 | 0x800;
        break;
      case 4u:
        v17 = v16 | 0x900;
        break;
      case 8u:
        v17 = v16 | 0xA00;
        break;
      default:
        v17 = v16 | 0x600;
        break;
    }
    v15 += 16;
    *(volatile uint32_t *)v12 = v17;
    v12 += 4;
  }
  while ( v14 != v15 );

  v18 = 513;

  if ( (MSI_MEMC->MEMC_REG_000 & 1) != 0 )
    v18 = 771;

  DDRPHYC->PHYC_REG_120 = v18;

  if ( v3 )
    DDRPHYC->PHYC_REG_3C4 = 0;

  v19 = result[14];

  if ( v19 )
  {
    MSI_MEMC->MEMC_REG_000 |= (v19 << 25) & 0x6000000;

    result = (uint32_t *)MSI_MEMC->MEMC_REG_004;

    MSI_MEMC->MEMC_REG_004 |= (v19 << 10) & 0x1FF000;
  }

  return result;
}

static const char unk_6B40[22]={  1,  9,  3,  7,  8,0x12,   4,0xD,   5,   6,0xA,   2,0xE,0xC,  0,   0,0x15,0x11,0x14,0x13, 0xB,0x16};
static const char unk_6B56[22]={  4,  9,  3,  7,  8,0x12,   1,0xD,   2,   6,0xA,   5,0xE,0xC,  0,   0,0x15,0x11,0x14,0x13, 0xB,0x16};
static const char unk_6B6C[22]={  1,  7,  8,0xC,0xA,0x12,   4,0xD,   5,   6,  3,   2,  9,  0,  0,   0,0x15,0x11,0x14,0x13, 0xB,0x16};
static const char unk_6B82[22]={  4,0xC,0xA,  7,  8,0x12,   1,0xD,   2,   6,  3,   5,  9,  0,  0,   0,0x15,0x11,0x14,0x13, 0xB,0x16};
static const char unk_6B98[22]={  0xD,  2,  7,  9,0xC,0x13,   5,  1,   6,   3,  4,   8,0xA,  0,  0,   0,0x15,0x16,0x12,0x11, 0xB,0x14};
static const char unk_6BAE[22]={  3,0xA,  7,0xD,  9, 0xB,   1,  2,   4,   6,  8,   5,0xC,  0,  0,   0,0x14,0x12,   0,0x15,0x16,0x11};
static const char unk_6BC4[22]={  3,  2,  4,  7,  9,   1,0x11,0xC,0x12, 0xE,0xD,   8,0xF,  6,0xA,   5,0x13,0x16,0x10,0x15,0x14, 0xB};
static const char unk_6BDA[22]={  2,0x13, 8,  6,0xE,   5,0x14,0xA,   3,0x12,0xD, 0xB,  7,0xF,  9,   1,0x16,0x15,0x11, 0xC,   4,0x10};
static const char unk_6BF0[22]={  1,  2,0xD,  8,0xF, 0xC,0x13,0xA,   3,0x15,  6,0x11,  9,0xE,  5,0x10,0x14,0x16, 0xB,   7,   4,0x12};

static void mctl_phy_ac_remapping(int a1)
{
  int v2; // r4
  int v3; // r5
  int v5; // r3
  char *v6; // r1
  int v7; // r3
  static char  v8[24];
  static char v9 [24]={  1,  9,  3,  7,  8,0x12,   4,0xD,   5,   6,0xA,   2,0xE,0xC,  0,   0,0x15,0x11,0x14,0x13, 0xB,0x16};
  static char v10[24]={  4,  9,  3,  7,  8,0x12,   1,0xD,   2,   6,0xA,   5,0xE,0xC,  0,   0,0x15,0x11,0x14,0x13, 0xB,0x16};
  static char v11[24]={  1,  7,  8,0xC,0xA,0x12,   4,0xD,   5,   6,  3,   2,  9,  0,  0,   0,0x15,0x11,0x14,0x13, 0xB,0x16};
  static char v12[24]={  4,0xC,0xA,  7,  8,0x12,   1,0xD,   2,   6,  3,   5,  9,  0,  0,   0,0x15,0x11,0x14,0x13, 0xB,0x16};
  static char v13[24]={  0xD,  2,  7,  9,0xC,0x13,   5,  1,   6,   3,  4,   8,0xA,  0,  0,   0,0x15,0x16,0x12,0x11, 0xB,0x14};
  static char v14[24]={  3,0xA,  7,0xD,  9, 0xB,   1,  2,   4,   6,  8,   5,0xC,  0,  0,   0,0x14,0x12,   0,0x15,0x16,0x11};
  static char v15[24]={  3,  2,  4,  7,  9,   1,0x11,0xC,0x12, 0xE,0xD,   8,0xF,  6,0xA,   5,0x13,0x16,0x10,0x15,0x14, 0xB};
  static char v16[24]={  2,0x13, 8,  6,0xE,   5,0x14,0xA,   3,0x12,0xD, 0xB,  7,0xF,  9,   1,0x16,0x15,0x11, 0xC,   4,0x10};
  static char v17[40]={  1,  2,0xD,  8,0xF, 0xC,0x13,0xA,   3,0x15,  6,0x11,  9,0xE,  5,0x10,0x14,0x16, 0xB,   7,   4,0x12};

  v2 = (MEMORY(SID_BASE + 0x228) >> 8) & 0xF;

  v3 = MEMORY(SID_BASE + 0x200);

	//PRINTF("ddr_efuse_type: 0x%02X\n", v2);

  v5 = *(uint32_t *)(a1 + 92);

  if ( (v5 & 0xC0000) != 0 || v3 == 80 || v3 == 124 )
  {

    if ( (v5 & 0x40000) != 0 || v3 == 80 )
      memcpy_self((int)v8, v15, 22);

    if ( (*(uint32_t *)(a1 + 92) & 0x80000) != 0 || v3 == 124 )
    {
      v6 = v16;

LABEL_6:

      memcpy_self((int)v8, v6, 22);
    }

  }
  else
  {
    switch ( v2 )
    {
      case 8:
        v6 = v10;
        goto LABEL_6;
      case 9:
        v6 = v11;
        goto LABEL_6;
      case 10:
        v6 = v13;
        goto LABEL_6;
      case 12:
        v6 = v9;
        goto LABEL_6;
      case 13:
      case 14:
        break;
      default:
        v6 = v12;
        goto LABEL_6;
    }
  }

  v7 = *(uint32_t *)(a1 + 4);

  if ( v7 != 2 )
  {

    if ( v7 != 3 )
      return;

LABEL_25:
    MEMORY(MSI_MEMC_BASE + 0x504) = ((uint8_t) v8[7] << 10) | (32 * (uint8_t) v8[6]) | (uint8_t) v8[5] | ((uint8_t) v8[8] << 15) | ((uint8_t) v8[9] << 20) | ((uint8_t) v8[10] << 25);
    MEMORY(MSI_MEMC_BASE + 0x508) = ((uint8_t) v8[13] << 10) | (32 * (uint8_t) v8[12]) | (uint8_t) v8[11] | ((uint8_t) v8[14] << 15) | ((uint8_t) v8[15] << 20);
    MEMORY(MSI_MEMC_BASE + 0x50C) = ((uint8_t) v8[18] << 10) | (32 * (uint8_t) v8[17]) | (uint8_t) v8[16] | ((uint8_t) v8[19] << 15) | ((uint8_t) v8[20] << 20) | ((uint8_t) v8[21] << 25);
    MEMORY(MSI_MEMC_BASE + 0x500) = ((uint8_t) v8[1] << 10) | (32 * (uint8_t) v8[0]) | 1 | ((uint8_t) v8[2] << 15) | ((uint8_t) v8[3] << 20) | ((uint8_t) v8[4] << 25);

    return;
  }

  if ( (*(uint32_t *)(a1 + 92) & 0x80000) != 0 || v3 == 124 )
  {
    memcpy_self((int)v8, v17, 22);

    MEMORY(MSI_MEMC_BASE + 0x504) = ((uint8_t) v8[7] << 10) | (32 * (uint8_t) v8[6]) | (uint8_t) v8[5] | ((uint8_t) v8[8] << 15) | ((uint8_t) v8[9] << 20) | ((uint8_t) v8[10] << 25);
    MEMORY(MSI_MEMC_BASE + 0x508) = ((uint8_t) v8[13] << 10) | (32 * (uint8_t) v8[12]) | (uint8_t) v8[11] | ((uint8_t) v8[14] << 15) | ((uint8_t) v8[15] << 20);
    MEMORY(MSI_MEMC_BASE + 0x50C) = ((uint8_t) v8[18] << 10) | (32 * (uint8_t) v8[17]) | (uint8_t) v8[16] | ((uint8_t) v8[19] << 15) | ((uint8_t) v8[20] << 20) | ((uint8_t) v8[21] << 25);
    MEMORY(MSI_MEMC_BASE + 0x500) = ((uint8_t) v8[1] << 10) | (32 * (uint8_t) v8[0]) | 1 | ((uint8_t) v8[2] << 15) | ((uint8_t) v8[3] << 20) | ((uint8_t) v8[4] << 25);
  }

  if ( (unsigned int)(v2 - 13) <= 1 )
  {
    memcpy_self((int)v8, v14, 22);

    goto LABEL_25;
  }
}

static int auto_set_timing_para(int a1) //OK
{
  unsigned int v1; // r3
  unsigned int v2; // r9
  int v4; // r1
  int v5; // r6
  unsigned int v6; // r8
  unsigned int v7; // r3
  unsigned int v8; // r3
  unsigned int v9; // r0
  unsigned int v10; // r5
  int v11; // r7
  int v12; // r10
  unsigned int v13; // r11
  unsigned int v14; // r2
  unsigned int v15; // r0
  int v16; // r3
  unsigned int v17; // r0
  int v18; // r1
  unsigned int v19; // r0
  int v20; // r3
  //char *v21; // r0
  unsigned int v22; // r0
  unsigned int v23; // r0
  int v24; // r3
  unsigned int v25; // r0
  unsigned int v26; // r3
  unsigned int v27; // r0
  unsigned int v28; // r0
  unsigned int v29; // r0
  unsigned int v30; // r0
  int v31; // r3
  unsigned int v32; // r0
  unsigned int v33; // r3
  unsigned int v34; // r0
  unsigned int v35; // r0
  unsigned int v36; // r0
  unsigned int v37; // r0
  int v38; // r3
  uint8_t /* bool */ v39; // cc
  int v40; // r12
  int v41; // lr
  int v42; // r3
  int v43; // r3
  int v44; // r0
  int v45; // r9
  int v46; // r1
  int v47; // r3
  int v48; // r7
  int v49; // r7
  int v50; // r12
  unsigned int v51; // r5
  int v52; // r7
  unsigned int v53; // r4
  unsigned int v54; // r3
  int result; // r0
  int v56; // r7
  unsigned int v57; // r5
  int v58; // r3
  int v59; // r3
  int v60; // r3
  int v61; // r3
  uint8_t /* bool */ v62; // zf
  int64_t v63; // r0
  int v64; // r7
  unsigned int v65; // r5
  int64_t v66; // r0
  int v67; // r3
  int v68; // r3
  int v69; // r3
  unsigned int v70; // [sp+0h] [bp-90h]
  int v71; // [sp+4h] [bp-8Ch]
  int v72; // [sp+8h] [bp-88h]
  int v73; // [sp+Ch] [bp-84h]
  int v74; // [sp+10h] [bp-80h]
  int v75; // [sp+14h] [bp-7Ch]
  unsigned int v76; // [sp+18h] [bp-78h]
  int v77; // [sp+18h] [bp-78h]
  unsigned int v78; // [sp+1Ch] [bp-74h]
  int v79; // [sp+20h] [bp-70h]
  int v80; // [sp+24h] [bp-6Ch]
  int v81; // [sp+28h] [bp-68h]
  int v82; // [sp+2Ch] [bp-64h]
  int v83; // [sp+30h] [bp-60h]
  unsigned int v84; // [sp+34h] [bp-5Ch]
  int v85; // [sp+38h] [bp-58h]
  unsigned int v86; // [sp+3Ch] [bp-54h]
  int v87; // [sp+40h] [bp-50h]
  int v88; // [sp+44h] [bp-4Ch]
  int v89; // [sp+48h] [bp-48h]
  unsigned int v90; // [sp+4Ch] [bp-44h]
  int v91; // [sp+50h] [bp-40h]
  unsigned int v92; // [sp+54h] [bp-3Ch]
  int v93; // [sp+58h] [bp-38h]
  unsigned int v94; // [sp+5Ch] [bp-34h]
  int v95; // [sp+60h] [bp-30h]

  v1 = *(uint32_t *)(a1 + 92);
  v2 = *(uint32_t *)a1;
  v4 = v1 << 30;
  v76 = v1;
  v5 = *(uint32_t *)(a1 + 4);
  v6 = *(uint32_t *)a1 >> 1;

  if ( (v1 & 2) != 0 )
  {
    v7 = *(uint32_t *)(a1 + 40);
    v95 = (v7 >> 21) & 7;
    v74 = (v7 >> 15) & 0x3F;
    v72 = (v7 >> 11) & 0xF;
    v70 = (v7 >> 6) & 0x1F;
    v73 = v7 & 0x3F;
    v8 = *(uint32_t *)(a1 + 44);
    v9 = *(uint32_t *)(a1 + 48);
    v10 = (v8 >> 20) & 7;
    v71 = (v8 >> 23) & 0x1F;
    v11 = (v8 >> 11) & 0xF;
    v12 = (v8 >> 6) & 0x1F;
    v13 = v9 & 0xFFF;
    v75 = v8 & 0x3F;
    v93 = (v8 >> 15) & 0x1F;
    v81 = (v9 >> 12) & 0x1FF;
  }
  else
  {
    switch ( v5 )
    {
      case 3:
        v10 = auto_cal_timing(8, v2 >> 1); //OK
        v70 = auto_cal_timing(15, v2 >> 1);
        v15 = auto_cal_timing(7800, v2 >> 1);
        v16 = v15 & 0x1F;

        if ( (v15 & 0x1F) != 0 )
          v16 = 1;

        v13 = v16 + (v15 >> 5);
        v17 = auto_cal_timing(350, v2 >> 1);
        v11 = v70;

        if ( v10 < 2 )
          v10 = 2;

        v81 = v17;
        v18 = v2 >> 1;

        if ( v70 < 2 )
          v11 = 2;

        if ( v2 > 0x320 )
        {
          v74 = auto_cal_timing(35, v18);
          v22 = auto_cal_timing(6, v2 >> 1);

          if ( v22 < 2 )
            v22 = 2;

          v72 = v22;
          v70 = auto_cal_timing(14, v2 >> 1);
          v73 = auto_cal_timing(48, v2 >> 1);
          v75 = auto_cal_timing(34, v2 >> 1);
          v71 = v72;
        }
        else
        {
          v74 = auto_cal_timing(50, v18);
          v19 = auto_cal_timing(10, v2 >> 1);

          if ( v19 < 2 )
            v19 = 2;

          v72 = v19;
          v73 = auto_cal_timing(53, v2 >> 1);
          v71 = v10;
          v75 = auto_cal_timing(38, v2 >> 1);
        }
        v12 = v70;
        break;
      case 2:
        v74 = auto_cal_timing(50, v2 >> 1);
        v72 = auto_cal_timing(10, v2 >> 1);
        v70 = auto_cal_timing(20, v2 >> 1);
        v73 = auto_cal_timing(65, v2 >> 1);
        v10 = auto_cal_timing(8, v2 >> 1);
        v12 = auto_cal_timing(15, v2 >> 1);
        v11 = v12;
        v75 = auto_cal_timing(45, v2 >> 1);
        v23 = auto_cal_timing(7800, v2 >> 1);
        v24 = v23 & 0x1F;

        if ( (v23 & 0x1F) != 0 )
          v24 = 1;

        v13 = v24 + (v23 >> 5);
        v71 = 2;
        v81 = auto_cal_timing(328, v2 >> 1);
        break;
      case 6:
        v25 = auto_cal_timing(50, v2 >> 1);

        if ( v25 < 4 )
          v25 = 4;

        v74 = v25;
        v26 = auto_cal_timing(10, v2 >> 1);

        if ( !v26 )
          v26 = 1;

        v72 = v26;
        v27 = auto_cal_timing(24, v2 >> 1);

        if ( v27 < 2 )
          v27 = 2;

        v70 = v27;
        v73 = auto_cal_timing(70, v2 >> 1);
        v28 = auto_cal_timing(8, v2 >> 1);
        v71 = v28;
        if ( v28 )
        {
          v10 = v28;

          if ( v28 < 2 )
            v10 = 2;
        }
        else
        {
          v10 = 2;
          v71 = 1;
        }
        v29 = auto_cal_timing(15, v2 >> 1);

        if ( v29 < 2 )
          v29 = 2;

        v11 = v29;
        v12 = auto_cal_timing(17, v2 >> 1);
        v75 = auto_cal_timing(42, v2 >> 1);
        v30 = auto_cal_timing(3900, v2 >> 1);
        v31 = v30 & 0x1F;

        if ( (v30 & 0x1F) != 0 )
          v31 = 1;

        v13 = v31 + (v30 >> 5);
        v81 = auto_cal_timing(210, v2 >> 1);
        break;
      case 7:
        v32 = auto_cal_timing(50, v2 >> 1);

        if ( v32 < 4 )
          v32 = 4;

        v74 = v32;
        v33 = auto_cal_timing(10, v2 >> 1);

        if ( !v33 )
          v33 = 1;

        v72 = v33;
        v34 = auto_cal_timing(24, v2 >> 1);

        if ( v34 < 2 )
          v34 = 2;

        v70 = v34;
        v73 = auto_cal_timing(70, v2 >> 1);
        v35 = auto_cal_timing(8, v2 >> 1);

        if ( v35 < 2 )
          v35 = 2;

        v10 = v35;
        v36 = auto_cal_timing(15, v2 >> 1);

        if ( v36 < 2 )
          v36 = 2;

        v11 = v36;
        v12 = auto_cal_timing(17, v2 >> 1);
        v75 = auto_cal_timing(42, v2 >> 1);
        v37 = auto_cal_timing(3900, v2 >> 1);
        v38 = v37 & 0x1F;

        if ( (v37 & 0x1F) != 0 )
          v38 = 1;

        v13 = v38 + (v37 >> 5);
        v71 = v10;
        v81 = auto_cal_timing(210, v2 >> 1);
        break;
      default:
        v81 = 128;
        v71 = 10;
        v10 = 3;
        v12 = 6;
        v74 = 16;
        v75 = 14;
        v13 = 98;
        v11 = 8;
        v73 = 20;
        v70 = 6;
        v72 = 3;
        break;
    }
    *(uint32_t *)(a1 + 40) = v73 | 0x400000 | (v70 << 6) | (v72 << 11) | (v74 << 15);
    *(uint32_t *)(a1 + 44) = (v11 << 11) | (v12 << 6) | v75 | (v10 << 15) | (v10 << 20) | (v71 << 23);

    v20 = (uint16_t) *(uint32_t *)(a1 + 56) >> 12;

    if ( v20 == 1 )
    {
//      v21 = "trefi:3.9ms\n";
      v13 >>= 1;
    }
    else if ( v20 == 2 )
    {
      v13 >>= 2;
//      v21 = "trefi:1.95ms\n";
    }
    else
    {
//      v21 = "trefi:7.8ms\n";
    }

//    PRINTF(v21, v4, v71, v20);

    v93 = v10;
    *(uint32_t *)(a1 + 48) = v13 | (v81 << 12);
    v95 = 2;
  }

  v14 = *(uint32_t *)a1;
  v94 = *(uint32_t *)(a1 + 36);
  v92 = *(uint32_t *)(a1 + 28);

  switch ( v5 )
  {
    case 2:
      v39 = v6 > 0xCC;

      if ( v6 > 0xCC )
        v6 = v5;

      v40 = v5;
      v5 = 3;

      if ( !v39 )
        v6 = 1;

      v41 = 3;
      v90 = v2 / 0x1E;

      if ( v39 )
        v42 = 4;
      else
        v42 = 3;

      v79 = v42;
      v43 = 2659;

      if ( v39 )
        v43 = 3699;

      v87 = v43;
      v83 = 400 * v14 + 1;
      v44 = 4;
      v45 = 4;
      v46 = 5;
      v84 = 500 * v14 / 0x3E8 + 1;
      v85 = 200 * v14 + 1;
      v86 = v14 + 1;
      v89 = v11 + 5;
      v91 = v10 + 5;
      v82 = 1;
      v77 = *(uint32_t *)(a1 + 28);
      v80 = 12;
      v47 = 0;
      v78 = 0;
      v88 = 0;
      break;
    case 3:

      if ( v14 <= 0x320 )
        v6 = 4;
      else
        v6 = 5;

      v56 = v11 + 2;
      v57 = v10 + 2;
      v41 = v6;
      v90 = v2 / 0x1E;

      if ( v14 <= 0x320 )
        v58 = 6;
      else
        v58 = 7;

      v79 = v58;

      if ( v14 <= 0x320 )
        v59 = 2;
      else
        v59 = v5;

      v82 = v59;

      if ( v14 <= 0x320 )
        v60 = 24;
      else
        v60 = 32;

      v88 = v60;
      v61 = 7280;

      if ( v14 > 0x320 )
        v61 = 7700;

      v87 = v61;
      v83 = 500 * v14 + 1;
      v44 = 4;
      v46 = 5;
      v84 = 360 * v14 / 0x3E8 + 1;
      v85 = 200 * v14 + 1;
      v86 = v14 + 1;
      v89 = v56 + v6;
      v91 = v57 + v6;
      v62 = ((v76 >> 2) & 3) == 1;
      v77 = *(uint32_t *)(a1 + 28);

      if ( v62 || v14 <= 0x390 )
        v45 = 5;
      else
        v45 = 6;

      v40 = 4;
      v80 = 12;
      v47 = 0;
      v78 = 0;
      break;
    case 6:
      v88 = v5;
      v5 = 2;
      v41 = 2;
      v63 = 0x88888889LL * v2;
      v45 = 10;
      v90 = HIDWORD(v63) >> 5;
      v83 = 200 * v14 + 1;
      v46 = 5;
      v44 = 5;
      v80 = 5;
      v40 = 5;
      v84 = 100 * v14 / 0x3E8 + 1;
      v85 = 11 * v14 + 1;
      v86 = v14 + 1;
      v89 = v11 + 5;
      v91 = v10 + 5;
      v79 = 4;
      v82 = 1;
      v78 = *(uint32_t *)(a1 + 36);
      v47 = 3;
      v6 = 3;
      goto LABEL_105;
    case 7:

      if ( v14 <= 0x320 )
        v41 = 3;
      else
        v41 = 4;

      v64 = v11 + 5;
      v65 = v10 + 5;

      if ( v14 <= 0x320 )
        v6 = 5;
      else
        v6 = 6;

      v66 = 0x88888889LL * v2;
      v45 = 13;
      v90 = HIDWORD(v66) >> 5;

      if ( v14 <= 0x320 )
        v67 = 2;
      else
        v67 = 3;

      v82 = v67;

      if ( v14 <= 0x320 )
        v68 = 10;
      else
        v68 = 12;

      v88 = v68;
      v83 = 200 * v14 + 1;
      v46 = 5;
      v44 = 5;
      v40 = 5;
      v84 = 100 * v14 / 0x3E8 + 1;
      v85 = 11 * v14 + 1;
      v86 = v14 + 1;
      v89 = v64 + v41;
      v91 = v65 + v41;

      if ( v14 <= 0x320 )
        v69 = 6;
      else
        v69 = v5;

      v5 = 3;
      v79 = v69;
      v78 = *(uint32_t *)(a1 + 36);
      v80 = 12;
      v47 = 5;
LABEL_105:
      v77 = 195;
      v87 = 0;
      break;
    default:
      v90 = 27;
      v91 = 8;
      v89 = 12;
      v44 = 3;
      v46 = 4;
      v5 = 2;
      v80 = 6;
      v47 = 0;
      v45 = 4;
      v40 = 2;
      v41 = 3;
      v79 = 3;
      v82 = 1;
      v6 = 1;
      v78 = 0;
      v88 = 0;
      v77 = 0;
      v86 = 0;
      v87 = 0;
      v84 = 0;
      v85 = 0;
      v83 = 0;
      break;
  }

  if ( v93 + v12 < (unsigned int)(v79 + 2) )
    v93 = 2 - v12 + v79;

  if ( !*(uint16_t *)(a1 + 26) )
    *(uint32_t *)(a1 + 24) = v87;

  v48 = *(uint16_t *)(a1 + 34);

  if ( !HIWORD(v92) )
    *(uint32_t *)(a1 + 28) = v77;

  if ( !v48 )
    *(uint32_t *)(a1 + 32) = v88;

  v49 = *(uint16_t *)(a1 + 24);
  v50 = (v40 << 12) | (v47 << 16);
  v51 = HIWORD(v94);

  if ( !HIWORD(v94) )
    v51 = v78;

  if ( !HIWORD(v94) )
    *(uint32_t *)(a1 + 36) = v51;

  DDRPHYC->PHYC_REG_030 = v49;
  DDRPHYC->PHYC_REG_034 = *(uint16_t *)(a1 + 28);
  DDRPHYC->PHYC_REG_038 = *(uint16_t *)(a1 + 32);

  v52 = *(uint16_t *)(a1 + 36);
  v53 = *(uint32_t *)(a1 + 12);

  DDRPHYC->PHYC_REG_03C = v52;
  DDRPHYC->PHYC_REG_02C = (v53 >> 4) & 3;
  DDRPHYC->PHYC_REG_058 = v75 | (v74 << 16) | (v89 << 24) | (v90 << 8);
  DDRPHYC->PHYC_REG_05C = v73 | (v71 << 16) | (v93 << 8);
  DDRPHYC->PHYC_REG_060 = (v79 << 16) | (v41 << 24) | v91 | (v45 << 8);
  DDRPHYC->PHYC_REG_064 = v50 | v80;
  DDRPHYC->PHYC_REG_068 = (v95 << 16) | (v70 << 24) | v12 | (v72 << 8);
  DDRPHYC->PHYC_REG_06C = v5 | (v46 << 16) | (v46 << 24) | (v44 << 8);

  if ( v14 > 0x320 )
    v54 = (DDRPHYC->PHYC_REG_078 & 0xFFF0000) | 0xF0007600;
  else
    v54 = (DDRPHYC->PHYC_REG_078 & 0xFFF0000) | 0xF0006600;

  DDRPHYC->PHYC_REG_078 = v54 | 0x10;

  result = (v13 << 15) & 0xFFF0000;

  DDRPHYC->PHYC_REG_080 = v82 | 0x2000100 | (v6 << 16);
  DDRPHYC->PHYC_REG_050 = v83 | (v84 << 20);
  DDRPHYC->PHYC_REG_054 = v85 | (v86 << 20);
  DDRPHYC->PHYC_REG_090 = v81 | (v13 << 16);
  DDRPHYC->PHYC_REG_094 = result;

  return result;
}

static unsigned int auto_cal_timing(int a1, int a2) //OK
{
  unsigned int v2; // r4
  unsigned int v3; // r1
  unsigned int result; // r0

  v2 = a2 * a1;
  v3 = a2 * a1 % 0x3E8u;
  result = v2 / 0x3E8;

  if ( v3 )
    ++result;

  return result;
}

static int mctl_channel_init(int a1, unsigned int *a2) //OK
{
  unsigned int v3; // r4
  unsigned int v4; // r1
  int v5; // r4
  unsigned int v6; // r2
  int v7; // r2
  unsigned int v8; // r3
  unsigned int v9; // r3
  unsigned int v10; // r3
  unsigned int v11; // r3
  unsigned int v12; // r3
  unsigned int v13; // r3
  int v14; // r1
  int v15; // r2
  unsigned int v16; // r3
  unsigned int v17; // r3
  unsigned int v18; // r3
  unsigned int v19; // r3
  int v20; // r3
  int v21; // r3
  unsigned int v22; // r2
  int v23; // r3
  int v24; // r1
  int v25; // r5

  v3 = a2[23];
  v4 = *a2;
  v5 = (v3 >> 2) & 3;
  v6 = a2[3];

  MEMORY(MSI_MEMC_BASE + 0x00C) = ((v4 >> 1) - 1) | (MEMORY(MSI_MEMC_BASE + 0x00C) & 0xFFFFF000);

  v7 = (32 * ~(uint8_t)v6) & 0x20;

  DDRPHYC->PHYC_REG_108 = (DDRPHYC->PHYC_REG_108 & 0xFFFFF0FF) | 0x0300;
  v8 = (DDRPHYC->PHYC_REG_344 & 0xFFFFFFCF) | v7;

  if ( v4 <= 0x2A0 )
    v9 = v8 & 0xFFFF0FFF;
  else
    v9 = v8 & 0xFFFF09FF;

  v10 = v9 & 0xFFFFFFE1;

  if ( v4 > 0x2A0 )
    v10 |= 0x400u;

  DDRPHYC->PHYC_REG_344 = v10;
  v11 = (DDRPHYC->PHYC_REG_3C4 & 0xFFFFFFCF) | v7;

  if ( v4 <= 0x2A0 )
    v12 = v11 & 0xFFFF0FFF;
  else
    v12 = v11 & 0xFFFF09FF;

  v13 = v12 & 0xFFFFFFE1;

  if ( v4 > 0x2A0 )
    v13 |= 0x400u;

  DDRPHYC->PHYC_REG_3C4 = v13;
  DDRPHYC->PHYC_REG_208 |= 2u;

  eye_delay_compensation((int)a2); //OK

  if ( v5 == 1 )
  {
    v15 = (int) & DDRPHYC->PHYC_REG_0BC;
    DDRPHYC->PHYC_REG_108 &= 0xFFFFFF3F;
    v16 = DDRPHYC->PHYC_REG_0BC & 0xFFFFFEF8;
  }
  else
  {
    if ( v5 != 2 )
    {
      DDRPHYC->PHYC_REG_108 &= 0xFFFFFFBF;
      _usdelay(10);

      DDRPHYC->PHYC_REG_108 |= 0xC0u;

      goto LABEL_16;
    }

    v14 = (int) & DDRPHYC->PHYC_REG_0BC;

    DDRPHYC->PHYC_REG_108 = (DDRPHYC->PHYC_REG_108 & 0xFFFFFF3F) | 0x80;

    v15 = (int) & DDRPHYC->PHYC_REG_11C;

    DDRPHYC->PHYC_REG_0BC = ((HIWORD(DDRPHYC->PHYC_REG_060) & 0x1F) - 2) | (DDRPHYC->PHYC_REG_0BC & 0xFFFFFEF8) | 0x100;
    v16 = (DDRPHYC->PHYC_REG_11C & 0x7FFFFFFF) | 0x8000000;
  }

   *(volatile uint32_t *) v15 = v16;

LABEL_16:

  if ( a2[1] - 6 <= 1 )
  {

    if ( v5 == 1 )
      v17 = DDRPHYC->PHYC_REG_11C & 0xF7FFFFFF;
    else
      v17 = DDRPHYC->PHYC_REG_11C & 0x88FFFFFF;

    if ( v5 == 1 )
      v18 = v17 & 0xFFFFFF3F;
    else
      v18 = v17 | 0x22000000;

    if ( v5 == 1 )
      v18 |= 0x80000000;

    DDRPHYC->PHYC_REG_11C = v18;
  }

  if ( (a2[5] & 0x1000) != 0 )
  {
    v19 = DDRPHYC->PHYC_REG_0C0;
  }
  else
  {
    v14 = DDRPHYC->PHYC_REG_0C0;
    v19 = 0x01003087;
  }

  if ( (a2[5] & 0x1000) != 0 )
    v19 = (v19 & 0xF0000000) | 0x3000000;
  else
    v14 &= 0xF0000000;

  if ( (a2[5] & 0x1000) != 0 )
    v20 = v19 | 1;
  else
    v20 = v19 | v14;

  DDRPHYC->PHYC_REG_0C0 = v20;

  if ( (MEMORY(CPU_CONFIG_REG) & 0x10000) != 0 )
  {
    MEMORY(CPU_CONFIG2_REG) &= 0xFFFFFFFD;
    _usdelay(10);
  }

  DDRPHYC->PHYC_REG_140 = (a2[2] & 0xFFFFFF) | (DDRPHYC->PHYC_REG_140 & 0xFC000000) | 0x2000000;

  if ( v5 == 1 )
  {

    DDRPHYC->PHYC_REG_000 = 83;

    while ( (DDRPHYC->PHYC_REG_010 & 1) == 0 )
      ;

    _usdelay(10);

    if ( a2[1] == 3 )
      v21 = 1440;
    else
      v21 = 1312;

  }
  else if ( (MEMORY(CPU_CONFIG_REG) & 0x10000) != 0 )
  {
    v21 = 98;
  }
  else if ( a2[1] == 3 )
  {
    v21 = 498;
  }
  else
  {
    v21 = 370;
  }

  DDRPHYC->PHYC_REG_000 = v21 | 1;
  _usdelay(10);

  while ( (DDRPHYC->PHYC_REG_010 & 1) == 0 )
    ;

  v23 = MEMORY(CPU_CONFIG_REG);
  v24 = MEMORY(CPU_CONFIG_REG) << 15;

  if ( (MEMORY(CPU_CONFIG_REG) & 0x10000) != 0 )
  {
    DDRPHYC->PHYC_REG_10C = (DDRPHYC->PHYC_REG_10C & 0xF9FFFFFF) | 0x4000000;
    _usdelay(10);

    DDRPHYC->PHYC_REG_004 |= 1u;
    while ( (DDRPHYC->PHYC_REG_018 & 7) != 3 )
      ;

    MEMORY(CPU_CONFIG2_REG) &= 0xFFFFFFFE;
    _usdelay(10);

    DDRPHYC->PHYC_REG_004 &= 0xFFFFFFFE;

    while ( (DDRPHYC->PHYC_REG_018 & 7) != 1 )
      ;

    _usdelay(15);

    if ( v5 == 1 )
    {
      DDRPHYC->PHYC_REG_108 &= 0xFFFFFF3F;
      DDRPHYC->PHYC_REG_10C = (DDRPHYC->PHYC_REG_10C & 0xF9FFFFFF) | 0x2000000;
      _usdelay(1);

      DDRPHYC->PHYC_REG_000 = 0x0401;

      do
      {
        v23 = DDRPHYC->PHYC_REG_010;
        v22 = DDRPHYC->PHYC_REG_010 << 31;
      }
      while ( (DDRPHYC->PHYC_REG_010 & 1) == 0 );

    }

  }

  if ( (DDRPHYC->PHYC_REG_010 & 0xFF00000) != 0 )
  {
    v25 = DDRPHYC->PHYC_REG_010 & 0x100000;
    if ( (DDRPHYC->PHYC_REG_010 & 0x100000) != 0 )
    {
      v25 = 0;

//      PRINTF("ZQ calibration error,check external 240 ohm resistor.\n", v24, v22, v23);

      return v25;
    }
  }
  else
  {
    v25 = 1;
  }

  while ( (DDRPHYC->PHYC_REG_018 & 1) == 0 )
    ;

  DDRPHYC->PHYC_REG_08C |= 0x80000000uL;
  _usdelay(10);

  DDRPHYC->PHYC_REG_08C &= ~ 0x80000000uL;
  _usdelay(10);

  MEMORY(MSI_MEMC_BASE + 0x014) |= 0x80000000uL;
  _usdelay(10);

  DDRPHYC->PHYC_REG_10C &= ~ 0x06000000uL;

  if ( v5 == 1 )
    DDRPHYC->PHYC_REG_11C = (DDRPHYC->PHYC_REG_11C & ~ 0x000000C0uL) | 0x40;

  return v25;
}

static int eye_delay_compensation(int a1) //OK
{
  int64_t v2; // kr00_8
  int i; // r0
  int j; // r6
  int v5; // r5
  unsigned int v6; // r2
  unsigned int v7; // r3
  //int v8; // r2
  int v9; // r1
  int k; // r2
  int result; // r0
  int z;

  v2 = *(uint64_t *)(a1 + 84);

  // Rank 0 ?
  for (z = 0; z < 9; ++ z)
  {
	  DDRPHYC->PHYC_REG_310 [z] |= (((uint32_t)v2 << 9) & 0x1E00) | ((2 * HIDWORD(v2)) & 0x1E);
  }
  // Rank 1 ?
  for (z = 0; z < 9; ++ z)
  {
	  DDRPHYC->PHYC_REG_390 [z] |= ((32 * v2) & 0x1E00) | ((HIDWORD(v2) >> 3) & 0x1E);
  }
//  // 9 elements
//  for ( i = (int) & DDRPHYC->PHYC_REG_310; i != (DDRPHYC_BASE + 0x334); i += 4 )
//    *(volatile uint32_t *)i |= (((uint32_t)v2 << 9) & 0x1E00) | ((2 * HIDWORD(v2)) & 0x1E);
//
//  // 9 elements
//  for ( j = (int) & DDRPHYC->PHYC_REG_390; j != (DDRPHYC_BASE + 0x3B4); j += 4 )
//    *(volatile uint32_t *)j |= ((32 * v2) & 0x1E00) | ((HIDWORD(v2) >> 3) & 0x1E);

  DDRPHYC->PHYC_REG_100 &= 0xFBFFFFFF;

  v5 = ((HIDWORD(v2) >> 15) & 0x1E) | ((WORD1(v2) << 9) & 0x1E00);

  DDRPHYC->PHYC_REG_334 |= v5;
  DDRPHYC->PHYC_REG_338 |= v5;

  v6 = ((HIDWORD(v2) >> 19) & 0x1E) | (((unsigned int)v2 >> 20 << 9) & 0x1E00);

  DDRPHYC->PHYC_REG_3B4 |= v6;
  DDRPHYC->PHYC_REG_3B8 |= v6;
  DDRPHYC->PHYC_REG_33C |= (WORD1(v2) << 25) & 0x1E000000;
  DDRPHYC->PHYC_REG_3BC |= ((unsigned int)v2 >> 20 << 25) & 0x1E000000;
  DDRPHYC->PHYC_REG_100 |= 0x4000000u;
  _usdelay(1);

  v7 = *(uint32_t *)(a1 + 80);
  //v8 = (int) & DDRPHYC->PHYC_REG_240;
  v9 = (16 * v7) & 0xF00;

  for (z = 0; z < 16; ++ z)
  {
	  DDRPHYC->PHYC_REG_240 [z] |= v9;
  }
  for (z = 0; z < 6; ++ z)
  {
	  DDRPHYC->PHYC_REG_228 [z] |= v9;
  }
//  // 16 elements
//  do
//  {
//    *(volatile uint32_t *)v8 |= v9;
//    v8 += 4;
//  }
//  while ( v8 != (DDRPHYC_BASE + 0x27C) );
//
//  // 6 elements
//  for ( k = (int) & DDRPHYC->PHYC_REG_228; k != (DDRPHYC_BASE + 0x240); k += 4 )
//    *(volatile uint32_t *)k |= v9;

  DDRPHYC->PHYC_REG_218 |= (v7 << 8) & 0xF00;

  result = DDRPHYC->PHYC_REG_21C;

  DDRPHYC->PHYC_REG_21C |= v7 & 0xF00;
  DDRPHYC->PHYC_REG_280 |= (v7 >> 4) & 0xF00;

  return result;
}

/* return auto-detected size in megabyes */
static int DRAMC_get_dram_size(void) //ok
{
  int v0;   // r3
  int v1=0; // r2

  v0 = MSI_MEMC->MEMC_REG_000 & 3;

  if ( (MSI_MEMC->MEMC_REG_000 & 3) != 0 )
  {
    LOBYTE(v1) = MSI_MEMC->MEMC_REG_004;

    if ( MSI_MEMC->MEMC_REG_004 << 30 )
    {
      v1 = (MSI_MEMC->MEMC_REG_004 >> 2) & 3;
      v0 = ((MSI_MEMC->MEMC_REG_004 >> 8) & 0xF) + (MSI_MEMC->MEMC_REG_004 >> 4) - 14;
    }
    else
    {
      v0 = 1 << ((MSI_MEMC->MEMC_REG_000 & 0xF) + (MSI_MEMC->MEMC_REG_000 >> 4) - 14 + ((MSI_MEMC->MEMC_REG_000 >> 2) & 3));
    }

    if ( MSI_MEMC->MEMC_REG_004 << 30 )
      v0 = 1 << (v0 + v1);
  }

  return (1 << ((MSI_MEMC->MEMC_REG_000 & 0xF) + (MSI_MEMC->MEMC_REG_000 >> 4) - 14 + ((MSI_MEMC->MEMC_REG_000 >> 2) & 3))) + v0;
}

static int dram_enable_all_master(void) //OK
{
  MSI_MEMC->MEMC_REG_020 = 0xFFFFFFFFuL;
  MSI_MEMC->MEMC_REG_024 = 0x000000FFuL;
  MSI_MEMC->MEMC_REG_028 = 0x0000FFFFuL;

  return _usdelay(10);
}

static int dramc_simple_wr_test(unsigned int a1, int a2) //OK
{
  int v2; // r3
  unsigned int v3; // r0
  int i; // r4
  int v5; // r6
  int j; // r5
  int v8; // r7
  unsigned int v9; // r8
  int v10; // r9
  int v11; // r3

  v2 = DRAM_SPACE_BASE;
  v3 = a1 >> 1 << 20;

  for ( i = 0; i != a2; ++i )
  {
    *(volatile uint32_t *)v2 = i + 0x01234567;
    *(volatile uint32_t *)(v3 + v2) = i - 0x01234568;

    v2 += 4;
  }

  v5 = DRAM_SPACE_BASE;

  for ( j = 0; ; ++j )
  {
    if ( i == j )
    {
//      PRINTF("DRAM simple test OK.\n", 0x01234567, 0xFEDCBA98, v2);

      return 0;
    }

    v8 = *(volatile uint32_t *)(v3 + v5);
    v9 = j - 0x01234568;
    v10 = v3 + v5;

    if ( j - 0x01234568 != v8 )
    {
//      PRINTF("DRAM simple test FAIL.\n", 0x01234567, 0xFEDCBA98, v2);

      v11 = v10;

      goto LABEL_9;
    }

    v9 = j + 0x01234567;
    v2 = v5 + 4;
    v8 = *(volatile uint32_t *)v5;

    if ( j + 0x01234567 != *(volatile uint32_t *)v5 )
      break;

    v5 += 4;
  }

//  PRINTF("DRAM simple test FAIL.\n", 0x01234567, 0xFEDCBA98, v2);

  v11 = v5;

LABEL_9:

//  PRINTF("%x != %x at address %x\n", v8, v9, v11);

  return 1;
}

static int dqs_gate_detect(int a1, int a2, unsigned int a3) //OK
{
  int v3; // r4
  int v4; // r3
  //char *v5; // r0
  int result; // r0
 // unsigned int v7; // r2
  //int v8; // r3

  if ( (DDRPHYC->PHYC_REG_010 & 0x400000) == 0 )
  {
    v4 = (*(uint32_t *)(a1 + 20) & 0xFFFFFFF0) | 0x1000;
    *(uint32_t *)(a1 + 20) = v4;

//    v5 = "[AUTO DEBUG] two rank and full DQ!\n";

LABEL_6:

//    PRINTF(v5, a2, a3, v4);

    return 1;
  }

  a2 = (DDRPHYC->PHYC_REG_348 >> 24) & 0x03;
  v3 = (DDRPHYC->PHYC_REG_3C8 >> 24) & 0x03;

  if ( a2 == 2 )
  {
    v4 = *(uint32_t *)(a1 + 20) & 0xFFFF0FF0;

    if ( v3 == 2 )
    {
      *(uint32_t *)(a1 + 20) = v4;

//      v5 = "[AUTO DEBUG] single rank and full DQ!\n";
    }
    else
    {
      v4 |= 1u;

      *(uint32_t *)(a1 + 20) = v4;

//      v5 = "[AUTO DEBUG] single rank and half DQ!\n";
    }
    goto LABEL_6;
  }

  if ( !a2 )
  {
    v4 = (*(uint32_t *)(a1 + 20) & 0xFFFFFFF0) | 0x1001;

    *(uint32_t *)(a1 + 20) = v4;

//    v5 = "[AUTO DEBUG] dual rank and half DQ!\n";

    goto LABEL_6;
  }

  result = *(uint32_t *)(a1 + 92) & 0x20000000;

  if ( result )
  {
//    PRINTF("DX0 state:%d\n", a2, a3, 51393352);

//    PRINTF("DX1 state:%d\n", v3, v7, v8);

    result = 0;
  }

  return result;
}

static int auto_scan_dram_size(int *a1, struct dram_para_t * a2param) //OK
{
  int v2; // r1
  //unsigned int v3; // r2
  //int v4; // r3
  int v5; // r4
  int v7; // r6
  int v8; // r5
  int v9; // r8
  int v10; // r3
  volatile int *v11; // r2
  int i; // r3
  uint8_t /* bool */ v13; // nf
  volatile int32_t *v14; // r4
  unsigned int v15; // r3
  unsigned int j; // r9
  int v17; // r1
  int v18; // r2
  int v19; // r3
  int v20; // r0
  char v21; // r11
  unsigned int v22; // r3
  volatile uint32_t *v23; // r2
  int v24; // r3
  int v25; // r1
  int v26; // r9
  unsigned int v27; // r3
  int v28; // r3
  int v29; // r0
  int k; // r2
  int v31; // r4
  unsigned int v32; // r4
  int v33; // r1
  //char *v34; // r0
  unsigned int v35; // r4
  int v36; // r3
  int v37; // [sp+4h] [bp-2Ch]

  v5 = mctl_core_init(a1); //OK

  if ( v5 )
  {
    v7 = 0x40000000;
    v8 = 0;
    v9 = 0x40000000;

    if ( (uint16_t) a1[5] >> 12 == 1 )
      v10 = 2;
    else
      v10 = 1;

    v37 = v10;

    while ( 2 )
    {
      v11 = (volatile int *)v9;

      for ( i = 0; i != 64; ++i )
      {
        v13 = (i & 1) != 0;

        if ( (i & 1) == 0 )
          v2 = ~v9 - 4 * i;

        if ( v13 )
          v2 = (int)v11;

        *v11++ = v2;
      }

      v14 = (volatile int32_t *) MSI_MEMC_BASE + v8;
      v15 = (*v14 & 0xFFFFF0F3) | 0x6F0;
      *v14 = v15;

      while ( v15 != *v14 )
        ;

      for ( j = 11; j != 17; ++j )
      {
        v17 = v7;
        v18 = 0;
        v19 = (1 << (j + 11)) + v9 - v7;

        while ( 1 )
        {
          v20 = (v18 & 1) != 0 ? v17 : ~v7 - 4 * v18;

          if ( *(uint32_t *)(v19 + v17) != v20 )
            break;

          ++v18;
          v17 += 4;

          if ( v18 == 64 )
            goto LABEL_23;

        }
      }

      j = 16;

LABEL_23:

      v21 = 16 * v8;

//      PRINTF("[AUTO DEBUG] rank %d row = %d \n", v8, j, v19);

      a1[4] = (a1[4] & ~(255 << (16 * v8 + 4))) | (j << (16 * v8 + 4));

      if ( v8 == 1 )
      {
        v9 = 0x40800000;
        MSI_MEMC->MEMC_REG_000 = (MSI_MEMC->MEMC_REG_000 & 0xFFFFF003) | 0x6A4;
      }

      v22 = (*v14 & 0xFFFFF003) | 0x6A4;
      *v14 = v22;

      while ( *v14 != v22 )
        ;

      v23 = (volatile uint32_t *)(v9 + 2048);
      v24 = 0;

      while ( 1 )
      {
        v25 = v7 + 4 * v24;

        if ( (v24 & 1) == 0 )
          v25 = ~v7 - 4 * v24;

        if ( *v23 != v25 )
          break;

        ++v24;
        ++v23;

        if ( v24 == 64 )
        {
          v26 = 0;

          goto LABEL_33;
        }

      }

      v26 = 1;

LABEL_33:

//      PRINTF("[AUTO DEBUG] rank %d bank = %d \n", v8, 4 * (v26 + 1), v24);

      a1[4] = (a1[4] & ~(15 << (v21 + 12))) | (v26 << (v21 + 12));

      if ( v8 == 1 )
      {
        v9 = 0x44000000;
        MSI_MEMC->MEMC_REG_000 = (MSI_MEMC->MEMC_REG_000 & 0xFFFFF003) | 0xAA0;
      }

      v27 = (*v14 & 0xFFFFF003) | 0xAA0;
      *v14 = v27;

      while ( *v14 != v27 )
        ;

      v28 = 9;

LABEL_38:
      v29 = v7;

      for ( k = 0; k != 64; ++k )
      {

        if ( (k & 1) != 0 )
          v31 = v29;
        else
          v31 = ~v7 - 4 * k;

        if ( *(volatile uint32_t *)((1 << v28) + v9 - v7 + v29) != v31 )
        {
          if ( ++v28 == 14 )
          {
            v28 = 13;
LABEL_46:
            v28 -= 10;
            v32 = 1 << v28;

            goto LABEL_47;

          }

          goto LABEL_38;

        }
        v29 += 4;
      }

      if ( v28 == 13 || v28 != 9 )
        goto LABEL_46;

      v32 = 0;

LABEL_47:

      v33 = v8++;

//      v34 = (char *)PRINTF("[AUTO DEBUG] rank %d page size = %d KB \n", v33, v32, v28);

      v35 = (v32 << v21) | (a1[4] & ~(15 << v21));
      a1[4] = v35;

      if ( v37 != v8 )
      {
        if ( v8 == 1 )
        {
          v7 = 0x48000000;
          v9 = 0x48000000;

          MSI_MEMC->MEMC_REG_000 = (MSI_MEMC->MEMC_REG_000 & 0xFFFFF003) | 0x6F0;
          MSI_MEMC->MEMC_REG_004 = (MSI_MEMC->MEMC_REG_004 & 0xFFFFF003) | 0x6F1;
        }

        continue;
      }

      break;
    }

    if ( v37 == 2 )
    {
      v36 = a1[5] & 0xFFFFF0FF;

      if ( (uint16_t) v35 == HIWORD(v35) )
      {
        a1[5] = v36;

//        v34 = "rank1 config same as rank0\n";
      }
      else
      {
        v36 |= 0x100u;
      }

      if ( (uint16_t) v35 != HIWORD(v35) )
      {
        a1[5] = v36;

//        v34 = "rank1 config different from rank0\n";
      }

//      PRINTF(v34, v2, (uint16_t) v35, v36);

    }

    v5 = 1;
  }
  else

  {
//    PRINTF("[ERROR DEBUG] DRAM initial error : 0!\n", v2, v3, v4);

  }

  return v5;
}

static int memcpy_self(int result, char *a2, int a3)
{
  int v3; // r3
  char *v4; // r2
  char v5; // t1

  v3 = result - 1;
  v4 = &a2[a3];
  while ( v4 - a2 > 0 )
  {
    v5 = *a2++;
    *(uint8_t *)++v3 = v5;
  }
  return result;
}

//------------------------------------------------------------------------------------------------

void sys_dram_init(void)
{
 init_DRAM(0, (int) & ddr3, & ddr3);
}
#endif /* CPUSTYPE_T113 */
#endif /* WITHSDRAMHW */
