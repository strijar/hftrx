/* $Id$ */
//
// Проект HF Dream Receiver (КВ приёмник мечты)
// автор Гена Завидовский mgs2001@mail.ru
// UA1ARN
//

#include "hardware.h"
#include "board.h"
#include "src/display/display.h"
#include "formats.h"
#include "gpio.h"

#if defined (TSC1_TYPE) && (TSC1_TYPE == TSC_TYPE_STMPE811)
#include "stmpe811.h"

uint_fast8_t board_tsc_is_pressed(void) /* Return 1 if touch detection */
{
	return stmpe811_is_pressed();
}

// On AT070TN90 with touch screen attached Y coordinate increments from bottom to top, X from left to right
uint_fast8_t
board_tsc_getxy(uint_fast16_t * xr, uint_fast16_t * yr)
{
	static uint_fast16_t x = 0, y = 0;
	uint_fast8_t z;
	if (board_tsc_is_pressed())
	{
		if (stmpe811_TS_GetXYZ(& x, & y, & z))
		{
			* xr = x;
			* yr = y;
			return 1;
		}
		* xr = x;
		* yr = y;
		return 1;
	}
	* xr = x;
	* yr = y;
	return 0;
}

#endif /* defined (TSC1_TYPE) && (TSC1_TYPE == TSC_TYPE_STMPE811) */

#if defined (TSC1_TYPE) && (TSC1_TYPE == TSC_TYPE_GT911)
#include "gt911.h"

uint_fast8_t
board_tsc_getxy(uint_fast16_t * xr, uint_fast16_t * yr)
{
	static uint_fast16_t x = 0, y = 0;

	if (gt911_getXY(& x, & y))
	{
		* xr = x;
		* yr = y;
		return 1;
	}
	* xr = x;
	* yr = y;
	return 0;
}

#endif /* defined (TSC1_TYPE) && (TSC1_TYPE == TSC_TYPE_GT911) */

#if defined (TSC1_TYPE) && (TSC1_TYPE == TSC_TYPE_FT5336)
#include "ft5336.h"

static TS_StateTypeDef ts_ft5336;

static void
tsc_interrupt_handler(void)
{
	TP();
}

uint_fast8_t
board_tsc_getxy(uint_fast16_t * xr, uint_fast16_t * yr)
{
	static uint_fast16_t x = 0, y = 0;

	ft5336_GetState(& ts_ft5336);

	if (ts_ft5336.touchDetected)
	{
		* xr = ts_ft5336.touchX[0];
		* yr = ts_ft5336.touchY[0];
		return 1;
	}
	* xr = x;
	* yr = y;
	return 0;
}

#endif /* defined (TSC1_TYPE) && (TSC1_TYPE == TSC_TYPE_FT5336) */

#if defined (TSC1_TYPE) && TSC1_TYPE == TSC_TYPE_XPT2046

#include "xpt2046.h"

uint_fast8_t
board_tsc_getxy(uint_fast16_t * xr, uint_fast16_t * yr)
{
	return xpt2046_getxy(xr, yr);
}

#endif /* defined (TSC1_TYPE) && TSC1_TYPE == TSC_TYPE_XPT2046 */

#if defined(TSC1_TYPE) && (TSC1_TYPE == TSC_TYPE_S3402)

#define TSC_I2C_ADDR (0x20 * 2)

static void s3402_initialize(void)
{
	// BOARD_TP_RESX - active low
	//	x-gpios = <&gpiog 0 GPIO_ACTIVE_HIGH>; /* TP_RESX_18 */
	const portholder_t BOARD_TP_RESX = (1uL << 0);	// PG0 - TP_RESX_18 - pin 03
	arm_hardware_piog_outputs(BOARD_TP_RESX, 0 * BOARD_TP_RESX);
	local_delay_ms(5);
	arm_hardware_piog_outputs(BOARD_TP_RESX, 1 * BOARD_TP_RESX);
	local_delay_ms(50);

	const unsigned i2caddr = TSC_I2C_ADDR;


	i2c_start(i2caddr | 0x00);
	i2c_write(0xFF);		// set page addr
	i2c_write(0x00);		// page #0
	i2c_waitsend();
    i2c_stop();
}

int s3402_get_id(void)
{
	const unsigned i2caddr = TSC_I2C_ADDR;

	uint8_t v0;

	i2c_start(i2caddr | 0x00);
	i2c_write_withrestart(0xE1);	//  Manufacturer ID register
	i2c_start(i2caddr | 0x01);
	i2c_read(& v0, I2C_READ_ACK_NACK);	// ||	The Manufacturer ID register always returns data $01.

	PRINTF("tsc id=%08lX (expected 0x01)\n", v0);

	return v0;
}

uint_fast8_t
board_tsc_getxy(uint_fast16_t * px, uint_fast16_t * py)
{
	const unsigned i2caddr = TSC_I2C_ADDR;


	uint8_t v0, v1, v2, v3, v4, v5, v6, v7;

	i2c_start(i2caddr | 0x00);
	i2c_write_withrestart(0x06);	// Address=0x0006 is used to read coordinate.
	i2c_start(i2caddr | 0x01);
	i2c_read(& v0, I2C_READ_ACK_1);	// ||
	i2c_read(& v1, I2C_READ_ACK);	// ||
	i2c_read(& v2, I2C_READ_ACK);	// ||
	i2c_read(& v3, I2C_READ_ACK);	// ||
	i2c_read(& v4, I2C_READ_ACK);	// ||
	i2c_read(& v5, I2C_READ_ACK);	// ||
	i2c_read(& v6, I2C_READ_ACK);	// ||
	i2c_read(& v7, I2C_READ_NACK);	// ||

	if (v0 != 0)
	{
		* px = v1 + v2 * 256;
		* py = v3 + v4 * 256;
		return 1;
	}
	return 0;
}
#endif /* defined(TSC1_TYPE) && (TSC1_TYPE == TSC_TYPE_S3402) */

#if defined(TSC1_TYPE) && (TSC1_TYPE == TSC_TYPE_ILI2102)

#define TSC_ILI2102_ADDR		0x82

#define REG_TOUCHDATA         	0x10
#define REG_PANEL_INFO        	0x20
#define REG_FIRMWARE_VERSION  	0x40
#define REG_CALIBRATE         	0xCC

#define MAX_TOUCHES    			2

static uint8_t tsc_ili2102_present = 0;

uint_fast8_t
board_tsc_getxy(uint_fast16_t * xr, uint_fast16_t * yr)
{
	static uint_fast16_t x = 0, y = 0;
	uint8_t command = REG_TOUCHDATA;
	uint8_t read_buf[9];

	if (! tsc_ili2102_present)
	{
		* xr = 0;
		* yr = 0;
		return 0;
	}

	i2chw_write(TSC_ILI2102_ADDR, & command, 1);
	i2chw_read(TSC_ILI2102_ADDR, read_buf, sizeof(read_buf));

	if (read_buf[0])
	{
		* xr = read_buf[1] | read_buf[2] << 8;
		* yr = read_buf[3] | read_buf[4] << 8;
		return 1;
	}

	* xr = x;
	* yr = y;
	return 0;
}

void ili2102_initialize(void)
{
	BOARD_GT911_RESET_INITIO_1();
	BOARD_GT911_RESET_SET(1);
	local_delay_us(100);
	BOARD_GT911_RESET_SET(0);
	local_delay_us(100);
	BOARD_GT911_RESET_SET(1);
	local_delay_us(500);

	uint8_t command = REG_FIRMWARE_VERSION;
	uint8_t read_buf[3], status;

	status = i2chw_write(TSC_ILI2102_ADDR, & command, 1);
	i2chw_read(TSC_ILI2102_ADDR, read_buf, sizeof(read_buf));

	if (status)
	{
		tsc_ili2102_present = 0;
		PRINTF("ili2102 initialize error\n");
		return;
	}

	tsc_ili2102_present = 1;
	PRINTF("ili2102 initialize successful\n");
}

#endif /*defined(TSC1_TYPE) && (TSC1_TYPE == TSC_TYPE_ILI2102) */

#if defined (TSC1_TYPE)

void board_tsc_initialize(void)
{
#if TSC1_TYPE == TSC_TYPE_GT911
	if (gt911_initialize())
		PRINTF("gt911 initialization successful\n");
	else
		PRINTF("gt911 initialization error\n");
#endif /* TSC1_TYPE == TSC_TYPE_GT911 */

#if TSC1_TYPE == TSC_TYPE_STMPE811
	stmpe811_initialize();
#endif /* TSC1_TYPE == TSC_TYPE_STMPE811 */

#if TSC1_TYPE == TSC_TYPE_FT5336
	if (ft5336_Initialize(DIM_X, DIM_Y) == FT5336_I2C_INITIALIZED)
		PRINTF("ft5336 initialization successful\n");
	else
	{
		PRINTF("ft5336 initialization error\n");
	}
#endif /* TSC1_TYPE == TSC_TYPE_FT5336 */

#if TSC1_TYPE == TSC_TYPE_XPT2046
	xpt2046_initialize();
#endif /* TSC1_TYPE == TSC_TYPE_XPT2046 */

#if TSC1_TYPE == TSC_TYPE_S3402
	s3402_initialize();
	s3402_get_id();	// test
#endif /* TSC1_TYPE == TSC_TYPE_XPT2046 */

#if TSC1_TYPE == TSC_TYPE_ILI2102
	ili2102_initialize();
#endif /* TSC1_TYPE == TSC_TYPE_ILI2102 */
}

#endif /* defined (TSC1_TYPE) */
