/* $Id$ */
//
// Проект HF Dream Receiver (КВ приёмник мечты)
// автор Гена Завидовский mgs2001@mail.ru
// UA1ARN
//

#include "hardware.h"
#include "synthcalcs.h"
#include "board.h"
#include "keyboard.h"
#include "encoder.h"
#include "display2.h"

#include "spi.h"	// bootloader_readimage definition
#include "formats.h"

#include "audio.h"
#include "codecs.h"

#if WITHUSEFATFS
	#include "fatfs/ff.h"
#endif /* WITHUSEFATFS */
#if WITHUSEFATFS
	#include "sdcard.h"
#endif /* WITHUSEFATFS */

#include <string.h>
#include <ctype.h>
#include <math.h>

#include "src/speex/arch.h"
#include "src/speex/speex_preprocess.h"

// Определения для работ по оптимизации быстродействия
#if WITHDEBUG && 0

	// stm32f746, no dualwatch:
	//	dtcount=0, dtmax=0, dtlast=0, dtcount2=41807716, dtmax2=1244, dtlast2=739, dtcount3=41806755, dtmax3=1446, dtlast3=916
	// R7S721xxx, Neon, dualwatch:
	//	dtcount=0, dtmax=0, dtlast=0, dtcount2=15890107, dtmax2=1119, dtlast2=590, dtcount3=31778668, dtmax3=1169, dtlast3=723
	// R7S721xxx, no Neon, dualwatch:
	// dtcount=0, dtmax=0, dtlast=0, dtcount2=184728, dtmax2=1461, dtlast2=911, dtcount3=367872, dtmax3=1169, dtlast3=713

	static volatile uint_fast32_t dtmax = 0, dtlast = 0, dtcount = 0;
	static volatile uint_fast32_t dtmax2 = 0, dtlast2 = 0, dtcount2 = 0;
	static volatile uint_fast32_t dtmax3 = 0, dtlast3 = 0, dtcount3 = 0;
	static uint_fast32_t perft = 0;
	static uint_fast32_t perft2 = 0;
	static uint_fast32_t perft3 = 0;

	static void debug_cleardtmax(void)
	{
		dtmax = 0;
		dtmax2 = 0;
		dtmax3 = 0;
	}

	#define BEGIN_STAMP() do { \
			perft = cpu_getdebugticks(); \
		} while (0)

	#define END_STAMP() do { \
			const uint_fast32_t t2 = cpu_getdebugticks(); \
			if (perft < t2) \
			{ \
				const uint_fast32_t vdt = t2 - perft; \
				dtlast = vdt; /* текущее значение длительности */ \
				if (vdt > dtmax) \
					dtmax = vdt; /* максимальное значение длительности */ \
				++ dtcount; \
			} \
		} while (0)

	#define BEGIN_STAMP2() do { \
			perft2 = cpu_getdebugticks(); \
		} while (0)

	#define END_STAMP2() do { \
			const uint_fast32_t t2 = cpu_getdebugticks(); \
			if (perft2 < t2) \
			{ \
				const uint_fast32_t vdt = t2 - perft2; \
				dtlast2 = vdt; /* текущее значение длительности */ \
				if (vdt > dtmax2) \
					dtmax2 = vdt; /* максимальное значение длительности */ \
				++ dtcount2; \
			} \
		} while (0)

	#define BEGIN_STAMP3() do { \
			perft3 = cpu_getdebugticks(); \
		} while (0)

	#define END_STAMP3() do { \
			const uint_fast32_t t2 = cpu_getdebugticks(); \
			if (perft3 < t2) \
			{ \
				const uint_fast32_t vdt = t2 - perft3; \
				dtlast3 = vdt; /* текущее значение длительности */ \
				if (vdt > dtmax3) \
					dtmax3 = vdt; /* максимальное значение длительности */ \
				++ dtcount3; \
			} \
		} while (0)


	//static uint32_t dd [4];
	/* DSP speed test */
	void main_speed_diagnostics(void)
	{
		//PRINTF(PSTR("data=%08lX,%08lX,%08lX,%08lX\n"), dd [0], dd [1], dd [2], dd [3]);
		PRINTF(PSTR("dtcount=%" PRIuFAST32 ", dtmax=%" PRIuFAST32 ", dtlast=%" PRIuFAST32 ", "), dtcount, dtmax, dtlast);
		PRINTF(PSTR("dtcount2=%" PRIuFAST32 ", dtmax2=%" PRIuFAST32 ", dtlast2=%" PRIuFAST32 ", "), dtcount2, dtmax2, dtlast2);
		PRINTF(PSTR("dtcount3=%" PRIuFAST32 ", dtmax3=%" PRIuFAST32 ", dtlast3=%" PRIuFAST32 "\n"), dtcount3, dtmax3, dtlast3);
	}

#else /* WITHDEBUG */

	#define BEGIN_STAMP() do { \
		} while (0)

	#define END_STAMP() do { \
		} while (0)

	#define BEGIN_STAMP2() do { \
		} while (0)

	#define END_STAMP2() do { \
		} while (0)

	#define BEGIN_STAMP3() do { \
		} while (0)

	#define END_STAMP3() do { \
		} while (0)

	static void debug_cleardtmax(void)
	{
	}

	void main_speed_diagnostics(void)
	{
	}

#endif /* WITHDEBUG */

#if WITHRFSG
	#error WITHRFSG now not supported
#endif /* WITHRFSG */

#define NUMLPFADJ 16	/* Коррекция мощности по ФНЧ передачика - количество ФНЧ. */

static char menuw [20];						// буфер для вывода значений системного меню

static uint_fast32_t 
//NOINLINEAT
nextfreq(uint_fast32_t oldfreq, uint_fast32_t freq, 
							   uint_fast32_t step, uint_fast32_t top);
static uint_fast32_t 
//NOINLINEAT
prevfreq(uint_fast32_t oldfreq, uint_fast32_t freq, 
							   uint_fast32_t step, uint_fast32_t bottom);


extern volatile uint_fast8_t spool_lfm_enable;
extern volatile uint_fast8_t spool_lfm_flag;

typedef struct dualctl8_tag
{
	uint_fast8_t value;		/* результирующее знаяение для формирования управляющего воздействия и инфопмирования по CAT */
	uint_fast8_t potvalue;	/* значение после функции гистерезиса от потенциометра */
} dualctl8_t;

typedef struct dualctl16_tag
{
	uint_fast16_t value;	/* результирующее знаяение для формирования управляющего воздействия и инфопмирования по CAT */
	uint_fast16_t potvalue;	/* значение после функции гистерезиса от потенциометра */
} dualctl16_t;

typedef struct dualctl32_tag
{
	uint_fast32_t value;	/* результирующее знаяение для формирования управляющего воздействия и инфопмирования по CAT */
	uint_fast32_t potvalue;	/* значение после функции гистерезиса от потенциометра */
} dualctl32_t;

static uint_fast8_t
getstablev8(const volatile uint_fast8_t * p)
{
	if (sizeof * p == 1)
		return * p;
	else
	{
		uint_fast8_t v1 = * p;
		uint_fast8_t v2;
		do
		{
			v2 = v1;
			v1 = * p;
		} while (v2 != v1);
		return v1;
	}
}

static uint_fast16_t
getstablev16(const volatile uint_fast16_t * p)
{
	if (sizeof * p == 1)
		return * p;
	else
	{
		uint_fast8_t v1 = * p;
		uint_fast8_t v2;
		do
		{
			v2 = v1;
			v1 = * p;
		} while (v2 != v1);
		return v1;
	}
}

static void 
display2_redrawbarstimed(
	uint_fast8_t immed,	// Безусловная перерисовка изображения
	uint_fast8_t extra,		/* находимся в режиме отображения настроек */
	const FLASHMEM struct menudef * mp
	);

static void 
display_redrawmodestimed(
	uint_fast8_t immed	// Безусловная перерисовка изображения
	);


static uint_fast8_t local_isdigit(char c)
{
	//return isdigit((unsigned char) c) != 0;
	return c >= '0' && c <= '9';
}

static uint_fast8_t gtx;	/* текущее состояние прием или передача */
static uint_fast8_t gcwpitch10 = 700 / CWPITCHSCALE;	/* тон при приеме телеграфа или самоконтроль (в десятках герц) */
#if WITHIF4DSP
static dualctl8_t gsquelch = { 0, 0 };	/* squelch level */
static uint_fast8_t gsquelchNFM;	/* squelch level for NFM */
static uint_fast8_t ggainnfmrx10 = 30;	/* дополнительное усиление по НЧ в режиме приёма NFM 100..1000% */
#endif /* WITHIF4DSP */
#if WITHDSPEXTDDC	/* "Воронёнок" с DSP и FPGA */

#endif /* WITHDSPEXTDDC */

//#define DEBUGEXT 1

static void processtxrequest(void);	/* Установка сиквенсору запроса на передачу.	*/

struct menudef;

static void
//NOINLINEAT
processmessages(
	uint_fast8_t * kbch,
	uint_fast8_t * kbready,
	uint_fast8_t inmenu,
	const FLASHMEM struct menudef * mp
	);

static uint_fast8_t getbankindex_raw(uint_fast8_t pathi);
static uint_fast8_t getbankindex_ab(uint_fast8_t ab);
static uint_fast8_t getbankindex_pathi(uint_fast8_t pathi);
static uint_fast8_t getbankindex_tx(uint_fast8_t tx);
static uint_fast8_t getbankindex_ab_fordisplay(uint_fast8_t ab);
//static void updateboard(uint_fast8_t full, uint_fast8_t mute);
static void updateboardZZZ(uint_fast8_t full, uint_fast8_t mute, const char * file, int line);
#define updateboard(full, mute) do { updateboardZZZ((full), (mute), __FILE__, __LINE__); } while (0)
static uint_fast8_t getsubmode(uint_fast8_t bi);		/* bi: vfo bank index */
static uint_fast8_t getactualmainsubrx(void);

#if WITHIF4DSP
struct rxaproc_tag;
static FLOAT_t * afpnoproc(uint_fast8_t pathi, struct rxaproc_tag *, FLOAT_t * p);
static FLOAT_t * afpcw(uint_fast8_t pathi, struct rxaproc_tag *, FLOAT_t * p);
static FLOAT_t * afpcwtx(uint_fast8_t pathi, struct rxaproc_tag *, FLOAT_t * p);
static FLOAT_t * afpssb(uint_fast8_t pathi, struct rxaproc_tag *, FLOAT_t * p);
static FLOAT_t * afpssbtx(uint_fast8_t pathi, struct rxaproc_tag *, FLOAT_t * p);
static FLOAT_t * afprtty(uint_fast8_t pathi, struct rxaproc_tag *, FLOAT_t * p);
#endif /* WITHIF4DSP */

#if WITHCAT

static void processcat_enable(uint_fast8_t catenable);
static void NOINLINEAT cat_answer_request(uint_fast8_t catindex);	// call from user-mode

static uint_fast8_t aistate;		/* autoinformation state flag */
#if WITHTX && (WITHSWRMTR || WITHSHOWSWRPWR)
static uint_fast8_t rmstate;		/* RM answer state type (1..3) */
#endif /* WITHTX && (WITHSWRMTR || WITHSHOWSWRPWR) */

// add/remove codes: modify also catanswers table.
enum
{
#if WITHCATEXT
#if WITHELKEY
	CAT_KY_INDEX,		// kyanswer()	// обрабатыаем первой для ускорения реакции
	CAT_KS_INDEX,		// ksanswer()
#endif /* WITHELKEY */
	CAT_PS_INDEX,		// psanswer()
	CAT_SM_INDEX,		// smanswer()
	CAT_RA_INDEX,		// raanswer()
	CAT_PA_INDEX,		// paanswer()
#if WITHANTSELECT || WITHANTSELECTRX || WITHANTSELECT2
	CAT_AN_INDEX,		// ananswer()
#endif /* WITHANTSELECT || WITHANTSELECTRX || WITHANTSELECT2 */
#if WITHTX && (WITHSWRMTR || WITHSHOWSWRPWR)
	CAT_RM1_INDEX,		// rm1answer()
	CAT_RM2_INDEX,		// rm2answer()
	CAT_RM3_INDEX,		// rm3answer()
#endif /* WITHTX && (WITHSWRMTR || WITHSHOWSWRPWR) */
#endif /* WITHCATEXT */
	CAT_ID_INDEX,		// idanswer()	
	CAT_FV_INDEX,		// fvanswer()
	CAT_DA_INDEX,		// daanswer()
	CAT_FA_INDEX,		// faanswer()
	CAT_FB_INDEX,		// fbanswer()
	CAT_SP_INDEX,		// spanswer()
	CAT_FR_INDEX,		// franswer()
	CAT_FT_INDEX,		// ftanswer()
	CAT_MD_INDEX,		// mdanswer()
	CAT_TX_INDEX,		// txanswer()
	CAT_RX_INDEX,		// rxanswer()
	CAT_AI_INDEX,		// aianswer()
	CAT_PT_INDEX,		// ptanswer()
	CAT_IF_INDEX,		// ifanswer()
	CAT_FW_INDEX,		// fwanswer()
#if WITHIF4DSP//CTLSTYLE_V1D || CTLSTYLE_OLEG4Z_V1 || 1
	CAT_ZY_INDEX,		// zyanswer()
	CAT_ZZ_INDEX,		// zzanswer()
#endif /* CTLSTYLE_V1D || CTLSTYLE_OLEG4Z_V1 */
#if WITHIF4DSP
	CAT_RG_INDEX,		// rganswer()
	CAT_AG_INDEX,		// aganswer()
	CAT_SQ_INDEX,		// sqanswer()
#endif /* WITHIF4DSP */
#if WITHPOWERTRIM
	CAT_PC_INDEX,		// pcanswer()
#endif /* WITHPOWERTRIM */
#if WITHIF4DSP
	CAT_NR_INDEX,		// nranswer()
#endif /* WITHIF4DSP */
	CAT_BADCOMMAND_INDEX,		// badcommandanswer()
	//
	CAT_MAX_INDEX
};

static uint8_t cat_answer_map [CAT_MAX_INDEX];
static uint8_t cat_answerparam_map [CAT_MAX_INDEX];


static uint_fast32_t savedbaudrate;	// Скорость, на которую в последний раз был запрограммирован порт.

static void 
cat_set_speed(uint_fast32_t baudrate)
{
	/* На ATMega и ATXMega перепрограммирование вызывает сбой при приёме если идёт передача из контроллера. */
	if (savedbaudrate == baudrate)
		return;
	savedbaudrate = baudrate;
	HARDWARE_CAT_SET_SPEED(baudrate);
}

#endif /* WITHCAT */


enum {
	VFOMODES_VFOINIT,
	VFOMODES_VFOSPLIT,
	//
	VFOMODES_COUNT
};

/*
	Коды управления постоянной времени АРУ на плате
		AGC_CODE_SLOW = 0x04, //0x07,
		AGC_CODE_MED  = 0x02,	//0x03,
		AGC_CODE_FAST = 0x01, //0x01,
		AGC_CODE_OFF = 0x00

*/

#if WITHNOATTNOPREAMP

	/* строки, выводимые на индикатор для обозначения режимов.
	 */
	static const FLASHMEM struct {
		uint8_t codeatt;	// признак включения аттенюатора
		uint8_t codepre;	// признак включения предусилителя
		int16_t atten10;	// результирующее затухание
		char label [5];
	}  attmodes [] =
	{
		{ 0, 0, 0, "    ", },
	};

	/* строки, выводимые на индикатор для обозначения режимов.
	 */
	static const FLASHMEM struct {
		uint8_t code;
		char label [4];
		int16_t atten10;	// результирующее затухание
	}  pampmodes [] =
	{
		{ 0, "   ", 0 },	// три символа нужны для стирания надписи OVF если используется индикация в одном месте с PRE
	};

#elif WITHONEATTONEAMP
	/* строки, выводимые на индикатор для обозначения режимов.
	 */
	static const FLASHMEM struct {
		uint8_t codeatt;	// признак включения аттенюатора
		uint8_t codepre;	// признак включения предусилителя
		char label [4];
		int16_t atten10;	// результирующее затухание
	}  attmodes [] =
	{
		{ 0, 0, "   ", 0  },
		{ 0, 1, "PRE", 0  },
		{ 0, 0, "   ", 0  },
		{ 1, 0, "ATT", 0  },
	};

	/* строки, выводимые на индикатор для обозначения режимов.
	 */
	static const FLASHMEM struct {
		uint8_t code;
		char label [4];
		int16_t atten10;	// результирующее затухание
	}  pampmodes [] =
	{
		{ 0, "   ", 0 },	// три символа нужны для стирания надписи OVF если используется индикация в одном месте с PRE
	};

#elif WITHPREAMPATT2_6DB
	/* Управление УВЧ и двухкаскадным аттенюатором с затуханиями 0 - 6 - 12 - 18 dB */

	/* строки, выводимые на индикатор для обозначения режимов.
	 */

	static const FLASHMEM struct {
		uint8_t code;
		char label [5];
		int16_t atten10;	// результирующее затухание
	}  attmodes [] =
	{
		{ 0, "    ", 0 },
		{ 1, " 6dB", 60 },
		{ 2, "12dB", 120 },
		{ 3, "18dB", 180 },
	};

	/* строки, выводимые на индикатор для обозначения режимов.
	 */
	static const FLASHMEM struct {
		uint8_t code;
		char label [4];
		int16_t atten10;	// результирующее затухание
	}  pampmodes [] =
	{
		{ 0, "   ", 0 },
		{ 1, "PRE", -120 },
	};

#elif WITHPREAMPATT2_10DB
	/* Управление УВЧ и двухкаскадным аттенюатором с затуханиями 0 - 10 - 20 - 30 dB */

	/* строки, выводимые на индикатор для обозначения режимов.
	 */

	static const FLASHMEM struct {
		uint8_t code;
		char label [5];
		int16_t atten10;	// результирующее затухание
	}  attmodes [] =
	{
		{ 0, "    ", 0  },
		{ 1, "10dB", 100  },
		{ 2, "20dB", 200  },
		{ 3, "30dB", 300  },
	};

	/* строки, выводимые на индикатор для обозначения режимов.
	 */
	static const FLASHMEM struct {
		uint8_t code;
		char label [4];
		int16_t atten10;	// результирующее затухание
	}  pampmodes [] =
	{
		{ 0, "   ", 0 },
		{ 1, "PRE", -120 },
	};

#elif WITHATT2_6DB
	/* Управление двухкаскадным аттенюатором с затуханиями 0 - 6 - 12 - 18 dB без УВЧ */

	/* строки, выводимые на индикатор для обозначения режимов.
	 */

	static const FLASHMEM struct {
		uint8_t code;
		char label [5];
		int16_t atten10;	// результирующее затухание
	}  attmodes [] =
	{
		{ 0, "    ", 0 },
		{ 1, " 6dB", 60 },
		{ 2, "12dB", 120 },
		{ 3, "18dB", 180 },
	};

	/* строки, выводимые на индикатор для обозначения режимов.
	 */
	static const FLASHMEM struct {
		uint8_t code;
		char label [4];
		int16_t atten10;	// результирующее затухание
	}  pampmodes [] =
	{
		{ 0, "   ", 0 },	// три символа нужны для стирания надписи OVF если используется индикация в одном месте с PRE
	};
#elif WITHATT2_10DB
	/* Управление двухкаскадным аттенюатором с затуханиями 0 - 10 - 20 - 30 dB без УВЧ */

	/* строки, выводимые на индикатор для обозначения режимов.
	 */

	static const FLASHMEM struct {
		unsigned char code;
		char label [5];
		int16_t atten10;	// результирующее затухание
	}  attmodes [] =
	{
		{ 0, "    ", 0 },
		{ 1, "10dB", 100  },
		{ 2, "20dB", 200  },
		{ 3, "30dB", 300  },
	};

	/* строки, выводимые на индикатор для обозначения режимов.
	 */
	static const FLASHMEM struct {
		uint8_t code;
		char label [4];
		int16_t atten10;	// результирующее затухание
	}  pampmodes [] =
	{
		{ 0, "   ", 0 },	// три символа нужны для стирания надписи OVF если используется индикация в одном месте с PRE
	};
#elif WITHATT1PRE1

	static const FLASHMEM struct {
		uint8_t code;
		char label [4];
		int16_t atten10;	// результирующее затухание
	}  attmodes [] =
	{
		{ 0, "   ", 0 },
		{ 1, "ATT", 120 },
	};


	/* строки, выводимые на индикатор для обозначения режимов.
	 */
	static const FLASHMEM struct {
		uint8_t code;
		char label [4];
		int16_t atten10;	// результирующее затухание
	}  pampmodes [] =
	{
		{ 0, "   ", 0 },
		{ 1, "PRE", - 120 },
	};
#else
	#error ATT/PREAMP mode undefined
#endif

// парамер меню
static uint_fast8_t gattpresh;	/* корректировка показаний с-метра по включенному аттенюатору и предусилителю */

// вернуть положительное значение в случае необходимости коррекции С-метра на величину аттенюатора
// и отрицательное щначение в случае коррекции на величину усиления.
// Возвращаем с точностью 0.1 дБ

static int_fast16_t gerflossdb10(uint_fast8_t xvrtr, uint_fast8_t att, uint_fast8_t pre)
{
	if (gattpresh && ! xvrtr)
	{
		// если не трансвертор и не отклбчено - корректируем S-meter
		return pampmodes [pre].atten10 + attmodes [att].atten10;
	}
	else
	{
		// без коррекции
		return 0;
	}
}

/* строки, выводимые на индикатор для обозначения режимов.
 */
#if CTLREGMODE24_RK4CI	/* управляющий регистр 24 бита - "Воробей" RK4CI */
	/* перечисление всех возможных режимов АРУ
	 */
	enum { 
		AGCMODE_LONG = 0, 
		AGCMODE_SLOW = 0, 
		AGCMODE_MED, 
		AGCMODE_FAST, 
		AGCMODE_OFF
		//
		};

	static const FLASHMEM struct {
		uint_fast8_t code;
		char label4 [5];
		char label3 [4];
	}  agcmodes [] =
	{
		{ BOARD_AGCCODE_SLOW, "SLOW", "SLO"  },	// 3
		{ BOARD_AGCCODE_MED,  "MED ", "MED" },	// 2
		{ BOARD_AGCCODE_FAST, "FAST", "FST" },	// 1
		{ BOARD_AGCCODE_OFF,  "AGC-", "OFF" },	// 4 ?
	};
#elif CTLREGMODE24_RK4CI_V1	/* управляющий регистр 32 бита (с 3*ULN2003) - "Воробей" RK4CI */
	/* перечисление всех возможных режимов АРУ
	 */
	enum { 
		AGCMODE_LONG = 0, 
		AGCMODE_SLOW = 0, 
		AGCMODE_MED, 
		AGCMODE_FAST, 
		AGCMODE_OFF
		//
		};

	static const FLASHMEM struct {
		uint_fast8_t code;
		char label4 [5];
		char label3 [4];
	}  agcmodes [] =
	{
		{ BOARD_AGCCODE_SLOW, "SLOW", "SLO"  },	// 3
		{ BOARD_AGCCODE_MED,  "MED ", "MED" },	// 2
		{ BOARD_AGCCODE_FAST, "FAST", "FST" },	// 1
		{ BOARD_AGCCODE_OFF,  "AGC-", "OFF" },	// 4 ?
	};
#elif WITHAGCMODENONE
	/* перечисление всех возможных режимов АРУ
	 */
	enum { 
		AGCMODE_LONG = 0, 
		AGCMODE_SLOW = 0, 
		AGCMODE_MED = 0, 
		AGCMODE_FAST = 0
		};
	static const FLASHMEM struct {
		uint_fast8_t code;
		char label4 [5];
		char label3 [4];
	}  agcmodes [] =
	{
		{ 0x00, "    ", "   " },
	};
#elif WITHAGCMODEONOFF
	/* перечисление всех возможных режимов АРУ
	 */
	enum { 
		AGCMODE_LONG = 0, 
		AGCMODE_SLOW = 0, 
		AGCMODE_MED = 0, 
		AGCMODE_FAST = 0, 
		//
		AGCMODE_OFF
		//
		};
	static const FLASHMEM struct {
		uint_fast8_t code;
		char label4 [5];
		char label3 [4];
	}  agcmodes [] =
	{
		{ BOARD_AGCCODE_ON,  "AGC ", "AGC" },
		{ BOARD_AGCCODE_OFF, "OFF ", "OFF" },
	};
#elif WITHAGCMODESLOWFAST	/* среди режимов АРУ есть только быстро-медленно */
	/* перечисление всех возможных режимов АРУ
	 */
	enum { 
		AGCMODE_LONG = 0, 
		AGCMODE_SLOW = 0, 
		AGCMODE_MED = 1, 
		AGCMODE_FAST = 1
		};
	static const FLASHMEM struct {
		uint_fast8_t code;
		char label4 [5];
		char label3 [4];
	}  agcmodes [] =
	{
		{ BOARD_AGCCODE_SLOW, "SLOW", "SLO" },
		{ BOARD_AGCCODE_FAST, "FAST", "FST" },
	};
#elif WITHAGCMODE4STAGES
	/* перечисление всех возможных режимов АРУ
	   выключенно не бывает
	 */
	enum { 
		AGCMODE_LONG, 
		AGCMODE_SLOW, 
		AGCMODE_MED, 
		AGCMODE_FAST, 
		AGCMODE_OFF
		//
		};
	static const FLASHMEM struct {
		uint_fast8_t code;
		char label4 [5];
		char label3 [4];
	}  agcmodes [] =
	{
		{ BOARD_AGCCODE_LONG, "LONG", "lng" },	// 4
		{ BOARD_AGCCODE_SLOW, "SLOW", "slo" },	// 2
		{ BOARD_AGCCODE_MED,  "MED ", "med" },	// 1
		{ BOARD_AGCCODE_FAST, "FAST", "fst" },	// 0
	};
#elif WITHAGCMODE5STAGES
	/* перечисление всех возможных режимов АРУ
	 */
	enum { 
		AGCMODE_LONG, 
		AGCMODE_SLOW, 
		AGCMODE_MED, 
		AGCMODE_FAST, 
		AGCMODE_OFF
		//
		};
	static const FLASHMEM struct {
		uint_fast8_t code;
		char label4 [5];
		char label3 [4];
	}  agcmodes [] =
	{
		{ BOARD_AGCCODE_LONG, "LONG", "lng" },	// 4
		{ BOARD_AGCCODE_SLOW, "SLOW", "slo" },	// 2
		{ BOARD_AGCCODE_MED,  "MED ", "med" },	// 1
		{ BOARD_AGCCODE_FAST, "FAST", "fst" },	// 0
		{ BOARD_AGCCODE_OFF,  "AGC-", "off" },	// 8
	};
#else
	#error WITHAGCMODExxxx undefined
#endif

#if WITHANTSELECT || WITHANTSELECTRX || WITHANTSELECT2
	enum { NANTENNAS = 2 };		// выбираем одну из двух антенн
#else /* WITHANTSELECT || WITHANTSELECTRX || WITHANTSELECT2 */
	enum { NANTENNAS = 1 };		// eдинственная антенна
#endif /* WITHANTSELECT || WITHANTSELECTRX || WITHANTSELECT2 */

#if WITHANTSELECTRX

static const FLASHMEM struct {
	uint8_t code;
	char label2 [3];
}  antmodes [] =
{
	{	0,	"A1" },
	{	1,	"A2" },
};

static const FLASHMEM struct {
	uint8_t code;
	char label2 [3];
}  rxantmodes [] =
{
	{	0,	"  " },
	{	1,	"RA" },
};

#elif WITHANTSELECT

static const FLASHMEM struct {
	uint8_t code [2];	// RX/TX antenna
	char label5 [6];
}  antmodes [] =
{
#if WITHTX
	{	{ 0, 0, },	"1R-1T" },
	{	{ 1, 1, },	"2R-2T" },
	{	{ 0, 1, },	"1R-2T" },
	{	{ 1, 0, },	"2R-1T" },
#else /* WITHTX */
	{	{ 0, 0, },	"ANT1 " },
	{	{ 1, 1, },	"ANT2 " },
#endif /* WITHTX */
};

#elif WITHANTSELECT2

static const FLASHMEM struct {
	uint8_t code;
	char label2 [3];
}  antmodes [] =
{
	{	0,	"A1" },
	{	1,	"A2" },
};
#endif /* WITHANTSELECT || WITHANTSELECTRX */

#if WITHPOWERLPHP

static const FLASHMEM struct {
	uint8_t code;
	char label [5];
}  pwrmodes [] =
{
	{ WITHPOWERTRIMMIN, "LP" },
	{ WITHPOWERTRIMMAX, "HP" },
};

#endif /* WITHPOWERLPHP */

#if WITHNOTCHONOFF || WITHNOTCHFREQ

static const FLASHMEM struct {
	uint8_t code;
	char label [6];
}  notchmodes [] =
{
//	{ BOARD_NOTCH_OFF, 		"     " },
#if WITHLMSAUTONOTCH
	{ BOARD_NOTCH_AUTO, 	"ANTCH" },
#endif /* WITHLMSAUTONOTCH */
	{ BOARD_NOTCH_MANUAL, 	"NOTCH" },
};
#endif /* WITHNOTCHONOFF || WITHNOTCHFREQ */

#if WITHUSEDUALWATCH

static const FLASHMEM struct {
	uint8_t code;
	char label [5];
}  mainsubrxmodes [] =
{
	{ BOARD_RXMAINSUB_A_B, "A/B", },	// Левый/правый, A - main RX, B - sub RX
	{ BOARD_RXMAINSUB_B_A, "B/A", },
	{ BOARD_RXMAINSUB_B_B, "B/B", },	
	{ BOARD_RXMAINSUB_A_A, "A/A", },		
	{ BOARD_RXMAINSUB_TWO, "A&B", },	// в оба аудиоканала поступает сумма выходов приемников.
};

#endif /* WITHUSEDUALWATCH */

#if WITHIF4DSP && 0
// надо бы перейти, но проблема в начальных значениях в таблице mdt - там коды а не индексы в этой таблице
static const FLASHMEM struct {
	uint_fast8_t code;
	char label [6];
}  txaudiosrcs [] =
{
	{ BOARD_TXAUDIO_MIKE, 	"MIKE ", },
#if WITHAFCODEC1HAVELINEINLEVEL	/* кодек имеет управление усилением с линейного входа */
	{ BOARD_TXAUDIO_LINE, 	"LINE ", },
#endif /* WITHAFCODEC1HAVELINEINLEVEL */
#if WITHUSBUACOUT
	{ BOARD_TXAUDIO_USB, 	"USB  ", },
#endif /* WITHUSBUACOUT */
	{ BOARD_TXAUDIO_2TONE, 	"2TONE", },
	{ BOARD_TXAUDIO_NOISE, 	"NOISE", },
	{ BOARD_TXAUDIO_1TONE, 	"1TONE", },
	{ BOARD_TXAUDIO_MUTE, 	"MUTE ", },
};

#endif /* WITHIF4DSP */

#define PWRMODE_COUNT (sizeof pwrmodes / sizeof pwrmodes [0])
#define NOTCHMODE_COUNT (sizeof notchmodes / sizeof notchmodes [0])
#define PAMPMODE_COUNT (sizeof pampmodes / sizeof pampmodes [0])
#define ATTMODE_COUNT (sizeof attmodes / sizeof attmodes [0])
#define RXANTMODE_COUNT (sizeof rxantmodes / sizeof rxantmodes [0])
#define ANTMODE_COUNT (sizeof antmodes / sizeof antmodes [0])
#define AGCMODE_COUNT (sizeof agcmodes / sizeof agcmodes [0])
#define MAINSUBRXMODE_COUNT (sizeof mainsubrxmodes / sizeof mainsubrxmodes [0])
#define TXAUDIOSRC_COUNT (sizeof txaudiosrcs / sizeof txaudiosrcs [0])

#define MENUNONVRAM ((nvramaddress_t) ~ 0)		// такой адрес, что не соответствует ни одному настраиваемому параметру.

// Интерфейсные функции доступа к NVRAM
static uint_fast8_t 
//NOINLINEAT
loadvfy8up(
	nvramaddress_t place, 
	uint_fast8_t bottom, uint_fast8_t upper, uint_fast8_t def)	// upper - inclusive limit
{
#if HARDWARE_IGNORENONVRAM
	return def;
#endif /* HARDWARE_IGNORENONVRAM */

	if (place == MENUNONVRAM)
		return def;

	const uint_fast8_t v = restore_i8(place);

	// pre-chechk default value added for mode row switching with same column as default
	if (def > upper || def < bottom)
		def = bottom;

	if (v > upper || v < bottom)
	{
		save_i8(place, def);
		return def;
	}
	return v;
}

// Интерфейсные функции доступа к NVRAM

static uint_fast16_t 
//NOINLINEAT
loadvfy16up(
	nvramaddress_t place, 
	uint_fast16_t bottom, uint_fast16_t upper, uint_fast16_t def)	// upper - inclusive limit
{
#if HARDWARE_IGNORENONVRAM
	return def;
#endif /* HARDWARE_IGNORENONVRAM */

	if (place == MENUNONVRAM)
		return def;

	const uint_fast16_t v = restore_i16(place);

	if (def > upper || def < bottom)
		def = bottom;
	
	if (v > upper || v < bottom)
	{
		save_i16(place, def);
		return def;
	}
	return v;
}

// проверка и приведение в допустимый диапазон значений, считанных из eeprom или принятых по CAT. Или при autosplit
static uint_fast32_t 
//NOINLINEAT
vfy32up(
	uint_fast32_t v,
	uint_fast32_t bottom, uint_fast32_t upper, uint_fast32_t def)
{

	if (def > upper || def < bottom)
		def = bottom;
	if (v > upper || v < bottom)
		v = def;
	return v;
}

static uint_fast32_t 
//NOINLINEAT
loadvfy32(
	nvramaddress_t place, 
	uint_fast32_t bottom, uint_fast32_t top, uint_fast32_t def)
{
#if HARDWARE_IGNORENONVRAM
	return def;
#endif /* HARDWARE_IGNORENONVRAM */

	if (place == MENUNONVRAM)
		return def;

	const uint_fast32_t v = restore_i32(place);

	if (def >= top || def < bottom)
		def = bottom;
	
	if (v >= top || v < bottom)
	{
		save_i32(place, def);
		return def;
	}
	return v;
}

#if WITHIF4DSP

// профиль ацдиопроцессора

typedef struct mikproc_tag
{
	uint_fast8_t comp;
	uint_fast8_t complevel;
} mikproc_t;

/* Хранимые в NVRAM параметры обработки сигнала перед модулятором */
struct micproc
{
	uint8_t comp;
	uint8_t complevel;
} ATTRPACKED;	// аттрибут GCC, исключает "дыры" в структуре. Так как в ОЗУ нет копии этой структуры, see also NVRAM_TYPE_BKPSRAM

#if WITHAFCODEC1HAVEPROC
struct micprof_cell
{
	uint_fast8_t level;
	uint_fast8_t clip;
	uint_fast8_t agc;
	uint_fast8_t agcgain;
	uint_fast8_t mikeboost20db;
	uint_fast8_t eq_enable;
	uint8_t eq_params [HARDWARE_CODEC1_NPROCPARAMS];
	uint_fast8_t cell_saved;
};

typedef struct micprof_cell	micprof_t;

static micprof_t micprof_cells [NMICPROFCELLS];
#endif /* WITHAFCODEC1HAVEPROC */

static mikproc_t micprofiles [] =
{
	{
		0,
		0,
	},
	{
		0,
		0,
	},
};

#define NMICPROFILES (sizeof micprofiles / sizeof micprofiles [0])

// индекс номера банка параметров обработки звука для данного режима
enum
{
	TXAPROFIG_CW,
	TXAPROFIG_SSB,
	TXAPROFIG_DIGI,
	TXAPROFIG_AM,
	TXAPROFIG_NFM,
	TXAPROFIG_DRM,
	TXAPROFIG_WFM,
	//
	TXAPROFIG_count
};


static uint8_t gtxaprofiles [TXAPROFIG_count];	// индекс профиля для группы режимов передачи - значения 0..NMICPROFILES-1


// параметры фильтра на приеме

#define AFRESPONCESHIFT 192
#define AFRESPONCEMIN 0
#define AFRESPONCEMAX (AFRESPONCESHIFT + 18)	// -96..+18

static int_fast32_t getafresponcebase(void)
{
	return - AFRESPONCESHIFT;
}

enum
{
	BWSET_SINGLE,	// параметры полосы пропускания - это одиночные значения полосы пропускания
	BWSET_PAIR		// параметры полосы пропускания - пара нижний срез/верхний срез
};


enum
{
	BWSET_WIDTHS = 3	// максимальное количество полос пропускания в одном bwsetsc_t
};

// индекс банка полос пропускания и коррекции АЧХ для данного режима
enum
{
	BWSETI_CW,
	BWSETI_SSB,
	BWSETI_SSBTX,
	BWSETI_DIGI,
	BWSETI_AM,
	BWSETI_NFM,
	BWSETI_DRM,
	BWSETI_WFM,
	//
	BWSETI_count
};

static const char FLASHMEM 
	strFlashWFM [] = "WFM",
	strFlashWide [] = "WID",
	strFlashMedium [] = "MED",
	strFlashNarrow [] = "NAR",
	strFlashNormal [] = "NOR";


// Частоты перестройки границ полосы пропускания
typedef struct
{
	uint_fast16_t granulationleft;	// дискретность изменения параметра через CAT или меню
	uint_fast16_t granulationright;	// дискретность изменения параметра через CAT или меню
	uint_fast8_t left10_width10_low, left10_width10_high;	// пределы изменения параметров
	uint_fast8_t right100_low, right100_high;	// пределы изменения параметров
} bwlimits_t;

typedef struct
{
	const bwlimits_t * limits;
	uint_fast8_t bwpropi;	// BWPROPI_xxxx
	uint_fast8_t type;		// BWSET_SINGLE/BWSET_PAIR
	uint_fast8_t left10_width10, right100;	// left выполняет роль width для телеграфных (BWSET_SINGLE) фильтров
	//uint_fast8_t fltsofter;
	uint_fast8_t afresponce;	/* скат АЧХ - на Samplerate/2 АЧХ становится на столько децибел  */
} bwprop_t;

// Частоты границ полосы пропускания
typedef struct
{
	uint8_t last;		// 0..BWSET_WIDTHS-1
	bwprop_t  * prop [BWSET_WIDTHS];				// частоты и другие свойства фильтров
	const char FLASHMEM * labels [BWSET_WIDTHS];	// названия фильтров
} bwsetsc_t;

// Пределы изменения полосы пропускания для меню

#define BWGRANLOW	10		// низкочастотный скат фильтров подстраивается и меню с шагом 10 герц
#define BWGRANHIGH	100		// высокочастотный скат фильтров подстраивается с шагом 100 герц

#define BWCWLEFTMIN (100 / BWGRANLOW)	// 100 Hz..1800 Hz
#define BWCWLEFTMAX (1800 / BWGRANLOW)

#define BWLEFTMIN (50 / BWGRANLOW)	// 50 Hz..700 Hz
#define BWLEFTMAX (700 / BWGRANLOW)

#define BWRIGHTMIN (800 / BWGRANHIGH)	// 0.8 kHz-18 kHz
#define BWRIGHTMAX (18000 / BWGRANHIGH)

static const bwlimits_t bwlimits_cw = { 100 / BWGRANLOW, 100 / BWGRANHIGH, BWCWLEFTMIN, BWCWLEFTMAX, 0, 0,  };
static const bwlimits_t bwlimits_am = { 50 / BWGRANLOW, 100 / BWGRANHIGH, BWLEFTMIN, BWLEFTMAX, BWRIGHTMIN, BWRIGHTMAX,  };
static const bwlimits_t bwlimits_ssb = { 50 / BWGRANLOW, 100 / BWGRANHIGH, BWLEFTMIN, BWLEFTMAX, BWRIGHTMIN, BWRIGHTMAX, };
static const bwlimits_t bwlimits_wfm = { 50 / BWGRANLOW, 100 / BWGRANHIGH, BWLEFTMIN, BWLEFTMAX, BWRIGHTMIN, BWRIGHTMAX, };

// индекс банка полос пропускания для данного режима
enum
{
	BWPROPI_CWNARROW,
	BWPROPI_CWWIDE,	
	BWPROPI_SSBWIDE,	
	BWPROPI_SSBMEDIUM,
	BWPROPI_SSBNARROW,	
	BWPROPI_SSBTX,
	BWPROPI_AMWIDE,	
	BWPROPI_AMNARROW,	
	BWPROPI_DIGIWIDE,	
	BWPROPI_NFMNARROW,
	BWPROPI_NFMWIDE,	
	BWPROPI_WFM,	
	//
	BWPROPI_count
};

// Частоты границ полосы пропускания
// эти значения могут модифицироваться через меню
static bwprop_t bwprop_cwnarrow = { & bwlimits_cw, BWPROPI_CWNARROW, BWSET_SINGLE, 200 / BWGRANLOW, 0, - 0 + AFRESPONCESHIFT, };
static bwprop_t bwprop_cwwide = { & bwlimits_cw, BWPROPI_CWWIDE, BWSET_SINGLE, 500 / BWGRANLOW, 0, - 0 + AFRESPONCESHIFT, };
static bwprop_t bwprop_ssbwide = { & bwlimits_ssb, BWPROPI_SSBWIDE, BWSET_PAIR, 300 / BWGRANLOW, 3400 / BWGRANHIGH, - 36 + AFRESPONCESHIFT,	};
static bwprop_t bwprop_ssbmedium = { & bwlimits_ssb, BWPROPI_SSBMEDIUM, BWSET_PAIR, 300 / BWGRANLOW, 2700 / BWGRANHIGH, - 36 + AFRESPONCESHIFT,	};
static bwprop_t bwprop_ssbnarrow = { & bwlimits_ssb, BWPROPI_SSBNARROW, BWSET_PAIR, 300 / BWGRANLOW, 2200 / BWGRANHIGH, - 36 + AFRESPONCESHIFT,	};
static bwprop_t bwprop_ssbtx = { & bwlimits_ssb, BWPROPI_SSBTX, BWSET_PAIR, 300 / BWGRANLOW, 3400 / BWGRANHIGH, - 0 + AFRESPONCESHIFT,	};
static bwprop_t bwprop_amwide = { & bwlimits_am, BWPROPI_AMWIDE, BWSET_PAIR, 100 / BWGRANLOW, 4500 / BWGRANHIGH, - 36 + AFRESPONCESHIFT,	};
static bwprop_t bwprop_amnarrow = { & bwlimits_am, BWPROPI_AMNARROW, BWSET_PAIR, 100 / BWGRANLOW, 3400 / BWGRANHIGH, - 36 + AFRESPONCESHIFT,	};
static bwprop_t bwprop_digiwide = { & bwlimits_ssb, BWPROPI_DIGIWIDE, BWSET_PAIR, 50 / BWGRANLOW, 5500 / BWGRANHIGH, - 0 + AFRESPONCESHIFT,	};
static bwprop_t bwprop_nfmnarrow = { & bwlimits_am, BWPROPI_NFMNARROW, BWSET_PAIR, 300 / BWGRANLOW, 3400 / BWGRANHIGH, - 36 + AFRESPONCESHIFT,	};
static bwprop_t bwprop_nfmwide = { & bwlimits_am, BWPROPI_NFMWIDE, BWSET_PAIR, 300 / BWGRANLOW, 4000 / BWGRANHIGH, - 36 + AFRESPONCESHIFT,	};
static bwprop_t bwprop_wfm = { & bwlimits_wfm, BWPROPI_WFM, BWSET_PAIR, 100 / BWGRANLOW, 12000 / BWGRANHIGH, + 18 + AFRESPONCESHIFT,	};

// Способ представления частот и количество профилей полосы пропускания,
// а так же названия полос пропускания для отображения
static const FLASHMEM bwsetsc_t bwsetsc [BWSETI_count] =
{
	{ 2, { & bwprop_cwwide, & bwprop_cwnarrow, & bwprop_ssbwide, }, { strFlashWide, strFlashNarrow, strFlashNormal, }, },	// BWSETI_CW
	{ 2, { & bwprop_ssbwide, & bwprop_ssbmedium, & bwprop_ssbnarrow, }, { strFlashWide, strFlashMedium, strFlashNarrow, }, },	// BWSETI_SSB
	{ 0, { & bwprop_ssbtx, }, { strFlashNormal, }, },	// BWSETI_SSBTX
	{ 0, { & bwprop_digiwide, }, { strFlashNormal, }, },	// BWSETI_DIGI
	{ 1, { & bwprop_amwide, & bwprop_amnarrow, }, { strFlashWide, strFlashNarrow, }, },	// BWSETI_AM
	{ 1, { & bwprop_nfmwide, & bwprop_nfmnarrow, }, { strFlashWide, strFlashNarrow, }, },	// BWSETI_NFM
	{ 0, { & bwprop_amwide, }, { strFlashNormal, }, },	// BWSETI_DRM
	{ 0, { & bwprop_wfm, }, { strFlashWFM, }, },	// BWSETI_WFM
};

// выбранная полоса пропускания в каждом режиме
static uint8_t bwsetpos [BWSETI_count];

// Используется для обмена с NVRAN параметрами фильтров
static bwprop_t * const FLASHMEM bwprops [BWPROPI_count] =
{
	& bwprop_cwnarrow,	// BWPROPI_CWNARROW,
	& bwprop_cwwide,	// BWPROPI_CWWIDE,	
	& bwprop_ssbwide,	// BWPROPI_SSBWIDE,	
	& bwprop_ssbmedium,	// BWPROPI_SSBMEDIUM,
	& bwprop_ssbnarrow,	// BWPROPI_SSBNARROW
	& bwprop_ssbtx,		// BWPROPI_SSBTX
	& bwprop_amwide,	// BWPROPI_AMWIDE,	
	& bwprop_amnarrow,	// BWPROPI_AMNARROW,
	& bwprop_digiwide,	// BWPROPI_DIGIWIDE,
	& bwprop_nfmnarrow,	// BWPROPI_NFMNARROW,
	& bwprop_nfmwide,	// BWPROPI_NFMWIDE,
	& bwprop_wfm,		// BWPROPI_WFM,
};

// получить тип фильтра
static int_fast16_t 
bwseti_getwide(
	uint_fast8_t bwseti
	)
{
	const uint_fast8_t pos = bwsetpos [bwseti];
	const bwprop_t * const p = bwsetsc [bwseti].prop [pos];
	switch (p->type)
	{
	case BWSET_SINGLE:
		return 0;

	default:
		return 1;
	}
}

// получить наклон АЧХ фильтра
static int_fast8_t 
bwseti_getafresponce(
	uint_fast8_t bwseti
	)
{
	const uint_fast8_t pos = bwsetpos [bwseti];
	const bwprop_t * const p = bwsetsc [bwseti].prop [pos];
	switch (p->type)
	{
	case BWSET_SINGLE:
		return 0;

	default:
		return p->afresponce - AFRESPONCESHIFT;
	}
}

// получить полосу пропускания фильтра
static int_fast16_t 
bwseti_getwidth(
	uint_fast8_t bwseti
	)
{
	const uint_fast8_t pos = bwsetpos [bwseti];
	const bwprop_t * const p = bwsetsc [bwseti].prop [pos];
	switch (p->type)
	{
	case BWSET_SINGLE:
		{
			const int_fast16_t width = p->left10_width10 * BWGRANLOW;
			const int_fast16_t width_2 = width / 2;
			const int_fast16_t center = gcwpitch10 * CWPITCHSCALE;
			return (center > width_2) ? width : (center * 2);
		}

	default:
	case BWSET_PAIR:
		return p->right100 * BWGRANHIGH - p->left10_width10 * BWGRANLOW;
	}
}

// получить нижнюю частоту среза фильтра
static int_fast16_t 
bwseti_getlow(
	uint_fast8_t bwseti
	)
{
	// gssbtxlowcut10 * BWGRANLOW
	const uint_fast8_t pos = bwsetpos [bwseti];
	const bwprop_t * const p = bwsetsc [bwseti].prop [pos];
	switch (p->type)
	{
	case BWSET_PAIR:
		return p->left10_width10 * BWGRANLOW;

	default:
	case BWSET_SINGLE:
		{
			const int_fast16_t width = p->left10_width10 * BWGRANLOW;
			const int_fast16_t width2 = width / 2;
			const int_fast16_t center = gcwpitch10 * CWPITCHSCALE;
			return (center > width2) ? (center - width2) : 0;
		}
	}
}

// получить верхнюю частоту среза фильтра
static int_fast16_t 
bwseti_gethigh(
	uint_fast8_t bwseti
	)
{
	// gssbtxhighcut100 * BWGRANHIGH
	const uint_fast8_t pos = bwsetpos [bwseti];
	const bwprop_t * const p = bwsetsc [bwseti].prop [pos];
	switch (p->type)
	{
	case BWSET_PAIR:
		return p->right100 * BWGRANHIGH;

	default:
	case BWSET_SINGLE:
		{
			const int_fast16_t width = p->left10_width10 * BWGRANLOW;
			const int_fast16_t width2 = width / 2;
			const int_fast16_t center = gcwpitch10 * CWPITCHSCALE;
			return (center > width2) ? (center + width2) : (center * 2);
		}
	}
}

#if 0
// получить код управления сглаживанием скатов фильтра
static int_fast8_t 
bwseti_getfltsofter(
	uint_fast8_t bwseti
	)
{
	const uint_fast8_t pos = bwsetpos [bwseti];
	const bwprop_t * const p = bwsetsc [bwseti].prop [pos];
	return p->fltsofter;
}
#endif

// индекс набора параметров слухового приема
enum
{
	AGCSETI_SSB,
	AGCSETI_CW,
	AGCSETI_FLAT,	// NFM
	AGCSETI_AM,		// AM, SAM
	AGCSETI_DRM,
	AGCSETI_DIGI,
	//
	AGCSETI_COUNT
};

/* структура хранения параметров в NVRAM */
struct agcseti_tag
{
	/* параметры АРУ по режимам работы */
	uint8_t rate;
	uint8_t t0;
	uint8_t t1;
	uint8_t release10;
	uint8_t t4;
	uint8_t thung10;
} ATTRPACKED;	// аттрибут GCC, исключает "дыры" в структуре. Так как в ОЗУ нет копии этой структуры, see also NVRAM_TYPE_BKPSRAM

struct afsetitempl
{
	/* начальные значения параметров АРУ */
	uint8_t rate; 		// = 10;	// на agc_rate дБ изменения входного сигнала 1 дБ выходного
	uint8_t t0; 		// = 0;	// in 1 mS steps. 0=0 mS	charge fast
	uint8_t t1; 		// = 120;	// in 1 mS steps. 120=120 mS	charge slow
	uint8_t release10; 	// = 5;		// in 0.1 S steps. 0.5 S discharge slow
	uint8_t t4; 		// = 50;	// in 1 mS steps. 35=35 mS discharge fast
	uint8_t thung10; 	// = 3;	// 0.1 S hung time (0.3 S recomennded).
	uint8_t scale; 		// = 100;	// 100% - требуемый выход от АРУ
};

#define AGC_RATE_FLAT	192	//(UINT8_MAX - 1)
#if CTLSTYLE_OLEG4Z_V1
	/* во всех режимах "плоская" АРУ */
	#define AGC_RATE_SSB	AGC_RATE_FLAT //(UINT8_MAX - 1)
	#define AGC_RATE_DIGI	AGC_RATE_FLAT //(UINT8_MAX - 1)
	#define AGC_RATE_DRM	AGC_RATE_FLAT //(UINT8_MAX - 1)
#else /* CTLSTYLE_OLEG4Z_V1 */
	#define AGC_RATE_SSB	20
	#define AGC_RATE_DIGI	6
	#define AGC_RATE_DRM	6
#endif /* CTLSTYLE_OLEG4Z_V1 */

static FLASHMEM const struct afsetitempl aft [AGCSETI_COUNT] =
{
	//AGCSETI_SSB,
	{
		AGC_RATE_SSB,		// agc_rate
		0,		// agc_t0
		120,	// agc_t1
		5,		// agc_release10
		50,		// agc_t4
		3,		// agc_thung10
		100,	// agc_scale
	},
	//AGCSETI_CW,
	{
		AGC_RATE_SSB,		// agc_rate
		0,		// agc_t0
		120,	// agc_t1
		1,		// agc_release10
		50,		// agc_t4
		1,		// agc_thung10
		100,	// agc_scale
	},
	//AGCSETI_FLAT,
	{
		AGC_RATE_FLAT,		// agc_rate
		0,		// agc_t0
		120,	// agc_t1
		1,		// agc_release10
		50,		// agc_t4
		1,		// agc_thung10
		100,	// agc_scale
	},
	//AGCSETI_AM,
	{
#if 0
		AGC_RATE_FLAT,		// agc_rate
		100,	// agc_t0
		100,	// agc_t1
		1,		// agc_release10
		100,	// agc_t4
		0,		// agc_thung10
		25,		// agc_scale
#else
		AGC_RATE_FLAT,		// agc_rate
		0,		// agc_t0
		120,	// agc_t1
		1,		// agc_release10
		50,		// agc_t4
		1,		// agc_thung10
		100,	// agc_scale
#endif
	},
	//AGCSETI_DRM,
	{
		AGC_RATE_DRM,		// agc_rate
		0,		// agc_t0
		120,	// agc_t1
		1,		// agc_release10
		50,		// agc_t4
		1,		// agc_thung10
		100,	// agc_scale
	},
	//AGCSETI_DIGI,
	{
		AGC_RATE_DIGI,		// agc_rate
		0,		// agc_t0
		120,	// agc_t1
		1,		// agc_release10
		50,		// agc_t4
		1,		// agc_thung10
		100,	// agc_scale
	},
};


typedef struct agcp_tag
{
	uint_fast8_t rate; 		// = 10;	// на gagc_rate дБ изменения входного сигнала 1 дБ выходного
	uint_fast8_t t0; 		// = 0;		// in 1 mS steps. 0=0 mS	charge fast
	uint_fast8_t t1; 		// = 120;	// in 1 mS steps. 120=120 mS	charge slow
	uint_fast8_t release10; // = 5;		// in 0.1 S steps. 0.5 S discharge slow - время разряда медленной цепи АРУ
	uint_fast8_t t4; 		// = 50;	// in 1 mS steps. 35=35 mS discharge fast - время разряда быстрой цепи АРУ
	uint_fast8_t thung10; 	// = 3;	// 0.1 S hung time (0.3 S recomennded).
	uint_fast8_t scale; 	// = 100
} agcp_t;

static agcp_t gagc [AGCSETI_COUNT];

#endif /* WITHIF4DSP */

#define	DEFAULT_DRM_PITCH	12000	/* тон DRM - 12 кГц*/
// The standard mark and space tones are 2125 hz and 2295 hz respectively
#define	DEFAULT_RTTY_PITCH	1275	/* mark тон DIGI modes - 2.125 кГц (1275 2125) */

#if WITHIF4DSP
	#if (LO4_SIDE == LOCODE_LOWER) || (LO4_SIDE == LOCODE_UPPER) || (LO4_SIDE == LOCODE_INVALID)
		#define DEFAULT_DSP_LO5SIDE	LOCODE_TARGETED	/* Этот гетеродин управляется для получения требуемой боковой */
	#else
		#define DEFAULT_DSP_LO5SIDE	LOCODE_LOWER	/* При преобразовании на этом гетеродине нет инверсии спектра */
	#endif
	// "Дятел"
	// Комментарий к "Пиону": Чтоб принимать usb надо чтобы приемник был настроен на 4012 Гц ниже, чтоб нбп - ниже на 7012Гц

	/* XX_PROCESSING_PITCH_XX: В случае наличия DSP процессора за смесительным детектором - на какую частоту переносим (в герцах). */
	/* Режимы, использующие смесительный детектор для слухового приёма, так же относятся к требующим включённого последнего гетеродина. */
	#define	SSB_LO5_SIDE_RX		DEFAULT_DSP_LO5SIDE
	#define	SSB_LO5_SIDE_TX		DEFAULT_DSP_LO5SIDE

	#define	DIGI_LO5_SIDE_RX		DEFAULT_DSP_LO5SIDE
	#define	DIGI_LO5_SIDE_TX		DEFAULT_DSP_LO5SIDE

	#define	BPSK_LO5_SIDE_RX		DEFAULT_DSP_LO5SIDE
	#define	BPSK_LO5_SIDE_TX		DEFAULT_DSP_LO5SIDE

	#define	CW_LO5_SIDE_RX		DEFAULT_DSP_LO5SIDE
	#define	CW_LO5_SIDE_TX		DEFAULT_DSP_LO5SIDE

	#define	CWZ_LO5_SIDE_RX		DEFAULT_DSP_LO5SIDE
	#define	CWZ_LO5_SIDE_TX		DEFAULT_DSP_LO5SIDE

	#define	AM_LO5_SIDE_RX		DEFAULT_DSP_LO5SIDE
	#define	AM_LO5_SIDE_TX		DEFAULT_DSP_LO5SIDE

	#define	SAM_LO5_SIDE_RX		DEFAULT_DSP_LO5SIDE
	#define	SAM_LO5_SIDE_TX		DEFAULT_DSP_LO5SIDE

	#define	NFM_LO5_SIDE_RX		DEFAULT_DSP_LO5SIDE
	#define	NFM_LO5_SIDE_TX		DEFAULT_DSP_LO5SIDE

	#define	DRM_LO5_SIDE_RX		DEFAULT_DSP_LO5SIDE
	#define	DRM_LO5_SIDE_TX		DEFAULT_DSP_LO5SIDE

	#define	WFM_LO5_SIDE_RX		DEFAULT_DSP_LO5SIDE
	#define	WFM_LO5_SIDE_TX		DEFAULT_DSP_LO5SIDE

#else /* WITHIF4DSP */
	/* XX_PROCESSING_PITCH_XX: В случае наличия DSP процессора за смесительным детектором - на какую частоту переносим (в герцах). */
	/* Режимы, использующие смесительный детектор для слухового приёма, так же относятся к требующим включённого последнего гетеродина. */
	#define	CW_LO5_SIDE_RX		LOCODE_LOWER	/* При преобразовании на этом гетеродине нет инверсии спектра */
	#define	CW_LO5_SIDE_TX		LOCODE_LOWER	/* При преобразовании на этом гетеродине нет инверсии спектра */

	#define	CWZ_LO5_SIDE_RX		LOCODE_LOWER	/* При преобразовании на этом гетеродине нет инверсии спектра */
	#define	CWZ_LO5_SIDE_TX		LOCODE_LOWER	/* При преобразовании на этом гетеродине нет инверсии спектра */

	#if ATMEGA_CTLSTYLE_V9SF_RX3QSP_H_INCLUDED
		#define	AM_PROCESSING_PITCH_RX		(455000L)	/* переносим на 455 кГц */
		#define	AM_PROCESSING_PITCH_TX		0
		#define	AM_LO5_SIDE_RX		LOCODE_LOWER	/* При преобразовании на этом гетеродине нет инверсии спектра */
		#define	AM_LO5_SIDE_TX		LOCODE_LOWER	/* При преобразовании на этом гетеродине нет инверсии спектра */
	#else
		#define	AM_PROCESSING_PITCH_RX		0
		#define	AM_PROCESSING_PITCH_TX		0
		#define	AM_LO5_SIDE_RX		LOCODE_INVALID	/* Этого гетеродина нет. */
		#define	AM_LO5_SIDE_TX		LOCODE_LOWER	/* При преобразовании на этом гетеродине нет инверсии спектра */
	#endif

	#define	SSB_LO5_SIDE_RX		LOCODE_LOWER	/* При преобразовании на этом гетеродине нет инверсии спектра */
	#define	SSB_LO5_SIDE_TX		LOCODE_LOWER	/* При преобразовании на этом гетеродине нет инверсии спектра */

	#define	DIGI_LO5_SIDE_RX		LOCODE_LOWER	/* При преобразовании на этом гетеродине нет инверсии спектра */
	#define	DIGI_LO5_SIDE_TX		LOCODE_LOWER	/* При преобразовании на этом гетеродине нет инверсии спектра */

	#define	BPSK_LO5_SIDE_RX		LOCODE_LOWER	/* При преобразовании на этом гетеродине нет инверсии спектра */
	#define	BPSK_LO5_SIDE_TX		LOCODE_LOWER	/* При преобразовании на этом гетеродине нет инверсии спектра */

#if CTLSTYLE_SW2016VHF || CTLSTYLE_SW2018XVR
	// CTLSTYLE_SW2016VHF (Si5351)
	#define	NFM_LO5_SIDE_RX		LOCODE_LOWER	/* При преобразовании на этом гетеродине нет инверсии спектра */
	#define	NFM_LO5_SIDE_TX		LOCODE_INVALID	/* Не выключать третий гетеродин - при NFM используется тот же тракт. */
#else
	// раньше было
	#define	NFM_LO5_SIDE_RX		LOCODE_INVALID	/* Этого гетеродина нет. */
	#define	NFM_LO5_SIDE_TX		LOCODE_LOWER	/* Не выключать третий гетеродин - при NFM используется тот же тракт. */
#endif

	#define	DRM_LO5_SIDE_RX		LOCODE_LOWER	/* При преобразовании на этом гетеродине нет инверсии спектра */
	#define	DRM_LO5_SIDE_TX		LOCODE_LOWER	/* При преобразовании на этом гетеродине нет инверсии спектра */

	#define	WFM_LO5_SIDE_RX		LOCODE_INVALID	/* Этого гетеродина нет. */
	#define	WFM_LO5_SIDE_TX		LOCODE_INVALID	/* Этого гетеродина нет. */
#endif /* WITHIF4DSP */
/* шаг настройки для разных режимов приема.
 * После деления на 10 значения должны влезть в 8 бит беззнаковое число.
 */

#define STEP_MINIMAL	10		/* минимальный шаг перестройки */
#define STEP_SSB_HIGH	10		/* шаг для SSB (USB/LSB/CW/CWR) при применении валкдера с большим к-вом шагов */
#define STEP_SSB_LOW	50		/* шаг для SSB (USB/LSB/CW/CWR) при применении валкдера с небольшим к-вом шагов */
#define	STEP_AM_HIGH	100		/* шаг для AM */
#define	STEP_AM_LOW		200		/* шаг для AM */
#define STEP_DRM	1000	/* шаг для DRM */
#define STEP_FM		250		/* шаг для FM */
#define	STEP_CWZ	10		/* шаг для CWZ (калибровка гетеродинов)  */
#define	STEP_WFM_LOW		1000		/* шаг для WFM */
#define	STEP_WFM_HIGH		2500		/* шаг для WFM */

//#define	STEP_GENERAL	100000		/* приращение при перестройке в режиме general coverage */
struct modetempl
{
	uint_fast8_t step10 [2];		/* индексинуется gbigstep */
	int_fast16_t autosplitK;		/* смещение в килогерцах для включения режима autosplit */
	uint_fast8_t nar : 1;			/* разрешение включения НЧ фильтра в этом режиме */
	uint_fast8_t txcw : 1;			/* Включение передачи в обход балансного модулятора */
	uint_fast8_t vox : 1;			/* в этом режиме возможно использование VOX */
	uint_fast8_t wbkin : 1;			/* в этом режиме возможно использование BREAK-IN */
	uint_fast8_t mute : 1;			/* в этом режиме запрещён микрофонный усилитель */
	uint_fast8_t wifshift : 1;		/* в этом режиме разрешено использовать параметры IF SHIFT */
	uint_fast8_t wpbt : 1;			/* в этом режиме разрешено использовать параметры PBT */
	uint_fast8_t subtone: 1;		/* в этом режиме разрешено использовать subtones */
#if ! defined (LO5_SIDE)
	uint_fast8_t lo5side [2];		/* в этом режиме не выключать последний гетеродин (RX и TX)  */
#endif /* ! defined (LO5_SIDE) */
	uint_fast8_t defagcmode;		/* начальный режим АРУ */
#if WITHTX
	portholder_t txgfva [TXGFI_SIZE];	// состояния выходов для разных режимов
	uint_fast8_t sdtnva [TXGFI_SIZE];	// признаки включения самоконтроля для разных режимов
#endif /* WITHTX */
#if WITHIF4DSP
	uint_fast8_t dspmode [2];		// Управление для DSP в режиме приёма и передачи
	uint_fast8_t bwsetis [2];			// индекс банка полос пропускания для данного режима в режиме приёма и передачи
	int_fast16_t bw6s [2];			// фиксированная полоса пропускания в DSP (if6) для данного режима (если не ноль).
	uint_fast8_t txaudio;			// источник звукового сигнала для данного режима
	uint_fast8_t txaprofgp;		// группа профилей обработки звука
	uint_fast8_t agcseti;			// параметры слухового приема
	FLOAT_t * (* afproc [2])(uint_fast8_t pathi, struct rxaproc_tag *, FLOAT_t * p);	// функция обработки звука в user mode в режиме приёма и передачи
#else /* WITHIF4DSP */
	uint_fast8_t detector [2];		/* код детектора RX и TX */
#endif /* WITHIF4DSP */
	char label [4];					// для контроля правильности инициализации структуры
};

// modes
// Порядок элементов в этом массиве должен совпадать с элементми enum,
// в котором определён MODE_COUNT
static FLASHMEM const struct modetempl mdt [MODE_COUNT] = 
{
	/* MODE_CW */
	{
		{ STEP_SSB_HIGH / 10, STEP_SSB_LOW / 10, },	// step for MODE_CW
		1,					/*  смещение в килогерцах для включения режима autosplit */
		1,		/* nar: разрешение включения НЧ фильтра в этом режиме */
		1,		/* TXCW Включение передачи в обход балансного модулятора */
		0,		/* в этом режиме возможно использование VOX */
		1,		/* в этом режиме возможно использование QSK */
		1,		/* в этом режиме запрещён микрофонный усилитель */
		0,		/* в этом режиме разрешено использовать параметры IF SHIFT */
		1,		/* в этом режиме разрешено использовать параметры PBT */
		0,		/* в этом режиме разрешено использовать subtones */
#if ! defined (LO5_SIDE)
		{ CW_LO5_SIDE_RX, CW_LO5_SIDE_TX, },
#endif /* ! defined (LO5_SIDE) */
		AGCMODE_FAST,	/* начальный режим АРУ для MODE_CW */
#if WITHTX
		{ TXGFV_RX, TXGFV_TRANS, TXGFV_TRANS, TXGFV_TX_CW, }, // txgfva для CW
		{ 0, 0, 0, 1 },	// признаки включения самоконтроля для CW
#endif /* WITHTX */
#if WITHIF4DSP
		{ DSPCTL_MODE_RX_NARROW, DSPCTL_MODE_TX_CW, },	// Управление для DSP в режиме приёма и передачи - режим узкого фильтра
		{ BWSETI_CW, BWSETI_CW },				// индекс банка полос пропускания для данного режима
		{ 0, INT16_MAX, },	// фиксированная полоса пропускания в DSP (if6) для данного режима (если не ноль).
		BOARD_TXAUDIO_MUTE,		// источник звукового сигнала для данного режима
		TXAPROFIG_CW,				// группа профилей обработки звука
		AGCSETI_CW,
		{ afpcw, afpnoproc, }, // afproc
#else /* WITHIF4DSP */
		{ BOARD_DETECTOR_SSB, BOARD_DETECTOR_SSB, },		/* ssb detector used */
#endif /* WITHIF4DSP */
		"CW",
	},
	/* MODE_SSB */
	{
		{ STEP_SSB_HIGH / 10, STEP_SSB_LOW / 10, },	// step for MODE_SSB
		5,					/*  смещение в килогерцах для включения режима autosplit */
		0,		/* nar: разрешение включения НЧ фильтра в этом режиме */
		0,		/* TXCW Включение передачи в обход балансного модулятора */
		1,		/* в этом режиме возможно использование VOX */
		0,		/* в этом режиме возможно использование QSK */
		0,		/* в этом режиме запрещён микрофонный усилитель */
		1,		/* в этом режиме разрешено использовать параметры IF SHIFT */
		1,		/* в этом режиме разрешено использовать параметры PBT */
		0,		/* в этом режиме разрешено использовать subtones */
#if ! defined (LO5_SIDE)
		{ SSB_LO5_SIDE_RX, SSB_LO5_SIDE_TX, },
#endif /* ! defined (LO5_SIDE) */
		AGCMODE_SLOW,	/* начальный режим АРУ для MODE_SSB */
#if WITHTX
		{ TXGFV_RX, TXGFV_TRANS, TXGFV_TX_SSB, TXGFV_TX_SSB, }, // txgfva для SSB
		{ 0, 0, 0, 0 },	// признаки включения самоконтроля для SSB
#endif /* WITHTX */
#if WITHIF4DSP
		{ DSPCTL_MODE_RX_WIDE, DSPCTL_MODE_TX_SSB, },	// Управление для DSP в режиме приёма и передачи - режим широкого фильтра
		{ BWSETI_SSB, BWSETI_SSBTX, },				// индекс банка полос пропускания для данного режима
		{ 0, 0, },	// фиксированная полоса пропускания в DSP (if6) для данного режима (если не ноль).
		BOARD_TXAUDIO_MIKE,		// источник звукового сигнала для данного режима
		TXAPROFIG_SSB,				// группа профилей обработки звука
		AGCSETI_SSB,
		{ afpcw, afpnoproc, }, // afproc
#else /* WITHIF4DSP */
		{ BOARD_DETECTOR_SSB, BOARD_DETECTOR_SSB, },		/* ssb detector used */
#endif /* WITHIF4DSP */
		"SSB",
	},
	/* MODE_AM */
	{
		{ STEP_AM_HIGH / 10, STEP_AM_LOW / 10, },	// step for MODE_AM
		5,					/*  смещение в килогерцах для включения режима autosplit */
		0,		/* nar: разрешение включения НЧ фильтра в этом режиме */
		1,		/* TXCW Включение передачи в обход балансного модулятора */
		1,		/* в этом режиме возможно использование VOX */
		0,		/* в этом режиме возможно использование QSK */
		0,		/* в этом режиме запрещён микрофонный усилитель */
		0,		/* в этом режиме разрешено использовать параметры IF SHIFT */
		1,		/* в этом режиме разрешено использовать параметры PBT */
		0,		/* в этом режиме разрешено использовать subtones */
#if ! defined (LO5_SIDE)
		{ AM_LO5_SIDE_RX, AM_LO5_SIDE_TX, },
#endif /* ! defined (LO5_SIDE) */
		AGCMODE_SLOW,	/* начальный режим АРУ для MODE_AM */
#if WITHTX
		{ TXGFV_RX, TXGFV_TRANS, TXGFV_TX_AM, TXGFV_TX_AM, }, // txgfva для AM
		{ 0, 0, 0, 0 },	// признаки включения самоконтроля для AM
#endif /* WITHTX */
#if WITHIF4DSP
		{ DSPCTL_MODE_RX_AM, DSPCTL_MODE_TX_AM, },	// Управление для DSP в режиме приёма и передачи - режим широкого фильтра
		{ BWSETI_AM, BWSETI_SSBTX, },				// индекс банка полос пропускания для данного режима
		{ 0, INT16_MAX, },	// фиксированная полоса пропускания в DSP (if6) для данного режима (если не ноль).
		BOARD_TXAUDIO_MIKE,		// источник звукового сигнала для данного режима
		TXAPROFIG_AM,				// группа профилей обработки звука
		AGCSETI_AM,
		{ afpcw, afpnoproc, }, // afproc
#else /* WITHIF4DSP */
		{ BOARD_DETECTOR_AM, BOARD_DETECTOR_AM, }, 		/* AM detector used */
#endif /* WITHIF4DSP */
		"AM",
	},
#if WITHSAM
	/* MODE_SAM  synchronous AM demodulation */
	{
		{ STEP_AM_HIGH / 10, STEP_AM_LOW / 10, },	// step for MODE_SAM
		5,					/*  смещение в килогерцах для включения режима autosplit */
		0,		/* nar: разрешение включения НЧ фильтра в этом режиме */
		1,		/* TXCW Включение передачи в обход балансного модулятора */
		1,		/* в этом режиме возможно использование VOX */
		0,		/* в этом режиме возможно использование QSK */
		0,		/* в этом режиме запрещён микрофонный усилитель */
		0,		/* в этом режиме разрешено использовать параметры IF SHIFT */
		1,		/* в этом режиме разрешено использовать параметры PBT */
		0,		/* в этом режиме разрешено использовать subtones */
#if ! defined (LO5_SIDE)
		{ SAM_LO5_SIDE_RX, SAM_LO5_SIDE_TX, },
#endif /* ! defined (LO5_SIDE) */
		AGCMODE_SLOW,	/* начальный режим АРУ для MODE_AM */
#if WITHTX
		{ TXGFV_RX, TXGFV_TRANS, TXGFV_TX_AM, TXGFV_TX_AM, }, // txgfva для AM
		{ 0, 0, 0, 0 },	// признаки включения самоконтроля для AM
#endif /* WITHTX */
#if WITHIF4DSP
		{ DSPCTL_MODE_RX_SAM, DSPCTL_MODE_TX_AM, },	// Управление для DSP в режиме приёма и передачи - режим широкого фильтра
		{ BWSETI_AM, BWSETI_SSBTX, },				// индекс банка полос пропускания для данного режима
		{ 0, INT16_MAX, },	// фиксированная полоса пропускания в DSP (if6) для данного режима (если не ноль).
		BOARD_TXAUDIO_MIKE,		// источник звукового сигнала для данного режима
		TXAPROFIG_AM,				// группа профилей обработки звука
		AGCSETI_AM,
		{ afpcw, afpnoproc, }, // afproc
#else /* WITHIF4DSP */
		{ BOARD_DETECTOR_AM, BOARD_DETECTOR_AM, }, 		/* AM detector used */
#endif /* WITHIF4DSP */
		"SAM",
	},
#endif /* WITHSAM */
	/* MODE_NFM */
	{
		{ STEP_FM / 10, STEP_FM / 10, },	// step for MODE_NFM
		- 600,	/*  смещение в килогерцах для включения режима autosplit */
		0,		/* nar: разрешение включения НЧ фильтра в этом режиме */
		1,		/* TXCW Включение передачи в обход балансного модулятора */
		1,		/* в этом режиме возможно использование VOX */
		0,		/* в этом режиме возможно использование QSK */
		0,		/* в этом режиме запрещён микрофонный усилитель */
		0,		/* в этом режиме разрешено использовать параметры IF SHIFT */
		1,		/* в этом режиме разрешено использовать параметры PBT */
		1,		/* в этом режиме разрешено использовать subtones */
#if ! defined (LO5_SIDE)
		{ NFM_LO5_SIDE_RX, NFM_LO5_SIDE_TX, },
#endif /* ! defined (LO5_SIDE) */
		AGCMODE_FAST,	/* начальный режим АРУ для MODE_NFM */
#if WITHTX
		{ TXGFV_RX, TXGFV_TRANS, TXGFV_TX_NFM, TXGFV_TX_NFM, }, // txgfva для NFM
		{ 0, 0, 0, 0 },	// признаки включения самоконтроля для NFM
#endif /* WITHTX */
#if WITHIF4DSP
		{ DSPCTL_MODE_RX_NFM, DSPCTL_MODE_TX_NFM, },	// Управление для DSP в режиме приёма и передачи - режим широкого фильтра
		{ BWSETI_NFM, BWSETI_SSBTX, },				// индекс банка полос пропускания для данного режима
		{ 9000, INT16_MAX, },	// фиксированная полоса пропускания в DSP (if6) для данного режима (если не ноль).
		BOARD_TXAUDIO_MIKE,		// источник звукового сигнала для данного режима
		TXAPROFIG_NFM,				// группа профилей обработки звука
		AGCSETI_FLAT,
		{ afpcw, afpnoproc, }, // afproc
#else /* WITHIF4DSP */
		{ BOARD_DETECTOR_FM, BOARD_DETECTOR_FM, }, 		/* FM detector used */
#endif /* WITHIF4DSP */
		"NFM",
	},
	/* MODE_DRM */
	{
		{ STEP_SSB_HIGH / 10, STEP_SSB_LOW / 10, },	// step for MODE_DRM
		0,					/*  смещение в килогерцах для включения режима autosplit */
		0,		/* nar: разрешение включения НЧ фильтра в этом режиме */
		0,		/* TXCW Включение передачи в обход балансного модулятора */
		1,		/* в этом режиме возможно использование VOX */
		0,		/* в этом режиме возможно использование QSK */
		1,		/* в этом режиме запрещён микрофонный усилитель */
		0,		/* в этом режиме разрешено использовать параметры IF SHIFT */
		1,		/* в этом режиме разрешено использовать параметры PBT */
		0,		/* в этом режиме разрешено использовать subtones */
#if ! defined (LO5_SIDE)
		{ DRM_LO5_SIDE_RX, DRM_LO5_SIDE_TX, },
#endif /* ! defined (LO5_SIDE) */
		AGCMODE_SLOW,	/* начальный режим АРУ для MODE_DRM */
#if WITHTX
		{ TXGFV_RX, TXGFV_TRANS, TXGFV_TX_SSB, TXGFV_TX_SSB, }, // txgfva для DRM
		{ 0, 0, 0, 0 },	// признаки включения самоконтроля для DRM
#endif /* WITHTX */
#if WITHIF4DSP
		{ DSPCTL_MODE_RX_DRM, DSPCTL_MODE_TX_SSB, },	// Управление для DSP в режиме приёма и передачи - режим узкого фильтра
		{ BWSETI_DRM, BWSETI_DRM, },				// индекс банка полос пропускания для данного режима
		{ 12000, INT16_MAX, },	// фиксированная полоса пропускания в DSP (if6) для данного режима (если не ноль).
		BOARD_TXAUDIO_MUTE,		// источник звукового сигнала для данного режима
		TXAPROFIG_AM,				// группа профилей обработки звука
		AGCSETI_DRM,
		{ afpnoproc, afpnoproc, }, // afproc
#else /* WITHIF4DSP */
		{ BOARD_DETECTOR_MUTE, BOARD_DETECTOR_MUTE, },		/* ssb detector used */
#endif /* WITHIF4DSP */
		"DRM",
	},
	/* MODE_CWZ - этот режим при передаче используется во время TUNE. */
	{
		{ STEP_CWZ / 10, STEP_CWZ / 10, },	// step for MODE_CWZ
		0,					/*  смещение в килогерцах для включения режима autosplit */
		0,		/* nar: разрешение включения НЧ фильтра в этом режиме */
		1,		/* TXCW Включение передачи в обход балансного модулятора */
		1,		/* в этом режиме возможно использование VOX */
		0,		/* в этом режиме возможно использование QSK */
		1,		/* в этом режиме запрещён микрофонный усилитель */
		0,		/* в этом режиме разрешено использовать параметры IF SHIFT */
		0,		/* в этом режиме разрешено использовать параметры PBT */
		0,		/* в этом режиме разрешено использовать subtones */
#if ! defined (LO5_SIDE)
		{ CWZ_LO5_SIDE_RX, CWZ_LO5_SIDE_TX, },
#endif /* ! defined (LO5_SIDE) */
		AGCMODE_MED,	/* начальный режим АРУ для MODE_CWZ */
#if WITHTX
		{ TXGFV_RX, TXGFV_TRANS, TXGFV_TX_CW, TXGFV_TX_CW, }, // txgfva для CWZ (TUNE)
		{ 0, 0, 1, 1 },	// признаки включения самоконтроля для CWZ (TUNE)
#endif /* WITHTX */
#if WITHIF4DSP
		{ DSPCTL_MODE_RX_DSB, DSPCTL_MODE_TX_CW, },	// Управление для DSP в режиме приёма и передачи - режим узкого фильтра
		{ BWSETI_SSB, BWSETI_SSBTX, },				// индекс банка полос пропускания для данного режима
		{ 0, INT16_MAX, },	// фиксированная полоса пропускания в DSP (if6) для данного режима (если не ноль).
		BOARD_TXAUDIO_MUTE,		// источник звукового сигнала для данного режима
		TXAPROFIG_SSB,				// группа профилей обработки звука
		AGCSETI_SSB,
		{ afpnoproc, afpnoproc, }, // afproc
#else /* WITHIF4DSP */
		{ BOARD_DETECTOR_SSB, BOARD_DETECTOR_TUNE, },		/* ssb detector used */
#endif /* WITHIF4DSP */
		"CWZ",
	},
#if WITHWFM || WITHMODESETFULLNFMWFM
	/* WFM mode if=10.7 MHz */
	/* MODE_WFM */
	{
		{ STEP_WFM_HIGH / 10, STEP_WFM_LOW / 10, },	// step for MODE_CW
		0,			/*  смещение в килогерцах для включения режима autosplit */
		0,		/* nar: разрешение включения НЧ фильтра в этом режиме */
		1,		/* Включение передачи в обход балансного модулятора */
		1,		/* в этом режиме возможно использование VOX */
		0,		/* в этом режиме возможно использование QSK */
		0,		/* в этом режиме запрещён микрофонный усилитель */
		0,		/* в этом режиме разрешено использовать параметры IF SHIFT */
		0,		/* в этом режиме разрешено использовать параметры PBT */
		0,		/* в этом режиме разрешено использовать subtones */
#if ! defined (LO5_SIDE)
		{ WFM_LO5_SIDE_RX, WFM_LO5_SIDE_TX, },
#endif /* ! defined (LO5_SIDE) */
		AGCMODE_FAST,	/* начальный режим АРУ для MODE_NFM */
#if WITHTX
		{ TXGFV_RX, TXGFV_TRANS, TXGFV_TX_CW, TXGFV_TX_CW, }, // txgfva для AM
		{ 0, 0, 0, 0 },	// признаки включения самоконтроля для AM
#endif /* WITHTX */
#if WITHIF4DSP
		{ DSPCTL_MODE_RX_WFM, DSPCTL_MODE_TX_SSB, },	// Управление для DSP в режиме приёма и передачи - режим широкого фильтра
		{ BWSETI_WFM, BWSETI_WFM, },				// индекс банка полос пропускания для данного режима
		{ INT16_MAX, INT16_MAX, },	// bypass, фиксированная полоса пропускания в DSP (if6) для данного режима (если не ноль).
		BOARD_TXAUDIO_MUTE,		// источник звукового сигнала для данного режима
		TXAPROFIG_SSB,				// группа профилей обработки звука
		AGCSETI_SSB,
		{ afpcw, afpnoproc, }, // afproc
#else /* WITHIF4DSP */
		{ BOARD_DETECTOR_WFM, BOARD_DETECTOR_WFM, },		/* WFM detector used */
#endif /* WITHIF4DSP */
		"WFM",
	},
#endif /* WITHWFM || WITHMODESETFULLNFMWFM */
	/* MODE_DIGI */
	{
		{ STEP_SSB_HIGH / 10, STEP_SSB_LOW / 10, },	// step for MODE_SSB
		5,		/*  смещение в килогерцах для включения режима autosplit */
		0,		/* nar: разрешение включения НЧ фильтра в этом режиме */
		0,		/* TXCW Включение передачи в обход балансного модулятора */
		1,		/* в этом режиме возможно использование VOX */
		0,		/* в этом режиме возможно использование QSK */
		0,		/* в этом режиме запрещён микрофонный усилитель */
		1,		/* в этом режиме разрешено использовать параметры IF SHIFT */
		1,		/* в этом режиме разрешено использовать параметры PBT */
		0,		/* в этом режиме разрешено использовать subtones */
#if ! defined (LO5_SIDE)
		{ SSB_LO5_SIDE_RX, SSB_LO5_SIDE_TX, },
#endif /* ! defined (LO5_SIDE) */
		AGCMODE_SLOW,	/* начальный режим АРУ для MODE_SSB */
#if WITHTX
		{ TXGFV_RX, TXGFV_TRANS, TXGFV_TX_SSB, TXGFV_TX_SSB, }, // txgfva для SSB
		{ 0, 0, 0, 0 },	// признаки включения самоконтроля для SSB
#endif /* WITHTX */
#if WITHIF4DSP
		{ DSPCTL_MODE_RX_WIDE, DSPCTL_MODE_TX_DIGI, },	// Управление для DSP в режиме приёма и передачи - режим широкого фильтра
		{ BWSETI_DIGI, BWSETI_DIGI, },				// индекс банка полос пропускания для данного режима
		{ 0, 0, },	// фиксированная полоса пропускания в DSP (if6) для данного режима (если не ноль).
	#if WITHUSBUACOUT
		BOARD_TXAUDIO_USB,		// источник звукового сигнала для данного режима
	#else /* WITHUSBUACOUT */
		BOARD_TXAUDIO_MIKE,		// источник звукового сигнала для данного режима
	#endif /* WITHUSBUACOUT */
		TXAPROFIG_DIGI,				// группа профилей обработки звука
		AGCSETI_DIGI,
		{ afpnoproc, afpnoproc, }, // afproc - сигнал не обрабатывается
#else /* WITHIF4DSP */
		{ BOARD_DETECTOR_SSB, BOARD_DETECTOR_SSB, },		/* ssb detector used */
#endif /* WITHIF4DSP */
		"DIG",
	},
	/* MODE_RTTY */
	{
		{ STEP_SSB_HIGH / 10, STEP_SSB_LOW / 10, },	// step for MODE_CW
		5,			/*  смещение в килогерцах для включения режима autosplit */
		1,		/* nar: разрешение включения НЧ фильтра в этом режиме */
		0,		/* Включение передачи в обход балансного модулятора */
		1,		/* в этом режиме возможно использование VOX */
		0,		/* в этом режиме возможно использование QSK */
		1,		/* в этом режиме запрещён микрофонный усилитель */
		0,		/* в этом режиме разрешено использовать параметры IF SHIFT */
		1,		/* в этом режиме разрешено использовать параметры PBT */
		0,		/* в этом режиме разрешено использовать subtones */
#if ! defined (LO5_SIDE)
		{ DIGI_LO5_SIDE_RX, DIGI_LO5_SIDE_TX, },
#endif /* ! defined (LO5_SIDE) */
		AGCMODE_SLOW,	/* начальный режим АРУ для MODE_SSB */
#if WITHTX
		{ TXGFV_RX, TXGFV_TRANS, TXGFV_TX_SSB, TXGFV_TX_SSB, }, // txgfva для DIGI
		{ 0, 0, 0, 0 },	// признаки включения самоконтроля для DIGI
#endif /* WITHTX */
#if WITHIF4DSP
		{ DSPCTL_MODE_RX_WIDE, DSPCTL_MODE_TX_SSB, },	// Управление для DSP в режиме приёма и передачи - режим широкого фильтра
		{ BWSETI_DIGI, BWSETI_DIGI, },				// индекс банка полос пропускания для данного режима
		{ 0, INT16_MAX, },	// фиксированная полоса пропускания в DSP (if6) для данного режима (если не ноль).
	#if WITHUSBUACOUT
		BOARD_TXAUDIO_USB,		// источник звукового сигнала для данного режима
	#else /* WITHUSBUACOUT */
		BOARD_TXAUDIO_MIKE,		// источник звукового сигнала для данного режима
	#endif /* WITHUSBUACOUT */
		TXAPROFIG_DIGI,				// группа профилей обработки звука
		AGCSETI_DIGI,
		{ afprtty, afpnoproc, }, // afproc
#else /* WITHIF4DSP */
		{ BOARD_DETECTOR_SSB, BOARD_DETECTOR_SSB, }, 		/* ssb detector used */
#endif /* WITHIF4DSP */
		"TTY",
	},
#if WITHMODEM
	/* MODE_MODEM */
	{
		{ STEP_SSB_HIGH / 10, STEP_SSB_LOW / 10, },	// step for MODE_MODEM
		5,			/*  смещение в килогерцах для включения режима autosplit */
		1,		/* nar: разрешение включения НЧ фильтра в этом режиме */
		0,		/* Включение передачи в обход балансного модулятора */
		1,		/* в этом режиме возможно использование VOX */
		0,		/* в этом режиме возможно использование QSK */
		1,		/* в этом режиме запрещён микрофонный усилитель */
		0,		/* в этом режиме разрешено использовать параметры IF SHIFT */
		0,		/* в этом режиме разрешено использовать параметры PBT */
		0,		/* в этом режиме разрешено использовать subtones */
#if ! defined (LO5_SIDE)
		{ BPSK_LO5_SIDE_RX, BPSK_LO5_SIDE_TX, },
#endif /* ! defined (LO5_SIDE) */
		AGCMODE_SLOW,	/* начальный режим АРУ для MODE_SSB */
#if WITHTX
		{ TXGFV_RX, TXGFV_TRANS, TXGFV_TX_SSB, TXGFV_TX_SSB, }, // txgfva для DIGI
		{ 0, 0, 0, 0 },	// признаки включения самоконтроля для DIGI
#endif /* WITHTX */
#if WITHIF4DSP
		{ DSPCTL_MODE_RX_BPSK, DSPCTL_MODE_TX_BPSK, },	// Управление для DSP в режиме приёма и передачи - режим широкого фильтра
		{ BWSETI_DIGI, BWSETI_DIGI, },				// индекс банка полос пропускания для данного режима
		{ 0, INT16_MAX, },	// фиксированная полоса пропускания в DSP (if6) для данного режима (если не ноль).
		BOARD_TXAUDIO_MUTE,		// источник звукового сигнала для данного режима
		TXAPROFIG_SSB,				// группа профилей обработки звука
		AGCSETI_SSB,
		{ afpnoproc, afpnoproc, }, // afproc
#else /* WITHIF4DSP */
		{ BOARD_DETECTOR_SSB, BOARD_DETECTOR_SSB, }, 		/* ssb detector used */
#endif /* WITHIF4DSP */
		"MDM",
	},
#endif /* WITHMODEM */
	/* MODE_ISB */
	{
		{ STEP_SSB_HIGH / 10, STEP_SSB_LOW / 10, },	// step for MODE_SSB
		5,					/*  смещение в килогерцах для включения режима autosplit */
		0,		/* nar: разрешение включения НЧ фильтра в этом режиме */
		0,		/* TXCW Включение передачи в обход балансного модулятора */
		1,		/* в этом режиме возможно использование VOX */
		0,		/* в этом режиме возможно использование QSK */
		0,		/* в этом режиме запрещён микрофонный усилитель */
		1,		/* в этом режиме разрешено использовать параметры IF SHIFT */
		1,		/* в этом режиме разрешено использовать параметры PBT */
		0,		/* в этом режиме разрешено использовать subtones */
#if ! defined (LO5_SIDE)
		{ SSB_LO5_SIDE_RX, SSB_LO5_SIDE_TX, },
#endif /* ! defined (LO5_SIDE) */
		AGCMODE_SLOW,	/* начальный режим АРУ для MODE_SSB */
#if WITHTX
		{ TXGFV_RX, TXGFV_TRANS, TXGFV_TX_SSB, TXGFV_TX_SSB, }, // txgfva для SSB
		{ 0, 0, 0, 0 },	// признаки включения самоконтроля для SSB
#endif /* WITHTX */
#if WITHIF4DSP
		{ DSPCTL_MODE_RX_ISB, DSPCTL_MODE_TX_ISB, },	// Управление для DSP в режиме приёма и передачи - режим широкого фильтра
		{ BWSETI_SSB, BWSETI_SSBTX, },				// индекс банка полос пропускания для данного режима
		{ 0, 0, },	// фиксированная полоса пропускания в DSP (if6) для данного режима (если не ноль).
	#if WITHUSBUACOUT
		BOARD_TXAUDIO_USB,		// источник звукового сигнала для данного режима
	#else /* WITHUSBUACOUT */
		BOARD_TXAUDIO_MIKE,		// источник звукового сигнала для данного режима
	#endif /* WITHUSBUACOUT */
		TXAPROFIG_SSB,				// группа профилей обработки звука
		AGCSETI_SSB,
		{ afpcw, afpnoproc, }, // afproc
#else /* WITHIF4DSP */
		{ BOARD_DETECTOR_SSB, BOARD_DETECTOR_SSB, },		/* ssb detector used */
#endif /* WITHIF4DSP */
		"ISB",
	},
#if WITHFREEDV
	/* MODE_FREEDV */
	{
		{ STEP_SSB_HIGH / 10, STEP_SSB_LOW / 10, },	// step for MODE_FREEDV
		5,			/*  смещение в килогерцах для включения режима autosplit */
		1,		/* nar: разрешение включения НЧ фильтра в этом режиме */
		0,		/* Включение передачи в обход балансного модулятора */
		1,		/* в этом режиме возможно использование VOX */
		0,		/* в этом режиме возможно использование QSK */
		1,		/* в этом режиме запрещён микрофонный усилитель */
		0,		/* в этом режиме разрешено использовать параметры IF SHIFT */
		0,		/* в этом режиме разрешено использовать параметры PBT */
		0,		/* в этом режиме разрешено использовать subtones */
#if ! defined (LO5_SIDE)
		{ BPSK_LO5_SIDE_RX, BPSK_LO5_SIDE_TX, },
#endif /* ! defined (LO5_SIDE) */
		AGCMODE_SLOW,	/* начальный режим АРУ для MODE_SSB */
#if WITHTX
		{ TXGFV_RX, TXGFV_TRANS, TXGFV_TX_SSB, TXGFV_TX_SSB, }, // txgfva для DIGI
		{ 0, 0, 0, 0 },	// признаки включения самоконтроля для DIGI
#endif /* WITHTX */
#if WITHIF4DSP
		{ DSPCTL_MODE_RX_FREEDV, DSPCTL_MODE_TX_FREEDV, },	// Управление для DSP в режиме приёма и передачи - режим широкого фильтра
		{ BWSETI_SSB, BWSETI_SSBTX, },				// индекс банка полос пропускания для данного режима
		{ 0, 0, },	// фиксированная полоса пропускания в DSP (if6) для данного режима (если не ноль).
		BOARD_TXAUDIO_MIKE,		// источник звукового сигнала для данного режима
		TXAPROFIG_SSB,				// группа профилей обработки звука
		AGCSETI_SSB,
		{ afpcw, afpnoproc, }, // afproc
#else /* WITHIF4DSP */
		{ BOARD_DETECTOR_SSB, BOARD_DETECTOR_SSB, }, 		/* ssb detector used */
#endif /* WITHIF4DSP */
		"FDV",
	},
#endif /* WITHFREEDV */
};


#define ENCRES_24	0	/* значение по умолчанию для индекса при использовании енкодера на 24 позиции */
#define ENCRES_32	1	/* значение по умолчанию для индекса при использовании енкодера на 64 позиций */
#define ENCRES_64	2	/* значение по умолчанию для индекса при использовании енкодера на 64 позиций */
#define ENCRES_100	4	/* значение по умолчанию для индекса при использовании енкодера на 100 позиций */
#define ENCRES_128	5	/* значение по умолчанию для индекса при использовании енкодера на 128 позиций */
#define ENCRES_256	7	/* значение по умолчанию для индекса при использовании енкодера на 256 позиций */
#define ENCRES_400	9	/* значение по умолчанию для индекса при использовании енкодера на 128 позиций */
#define ENCRES_600	10	/* значение по умолчанию для индекса при использовании енкодера на 128 позиций */

/* скорость 115200 не добавлена из соображений невозможностти точного формирования на atmega
   при частоте генератора 8 МГц
   */
static const FLASHMEM uint_fast8_t encresols [] =
{
	24 / ENCRESSCALE,	// 0
	32 / ENCRESSCALE,	// 1
	64 / ENCRESSCALE,	// 2
	96 / ENCRESSCALE,	// 3
	100 / ENCRESSCALE,	// 4
	128 / ENCRESSCALE,	// 5
	144 / ENCRESSCALE,	// 6
	256 / ENCRESSCALE,	// 7
	300 / ENCRESSCALE,	// 8
	400 / ENCRESSCALE,	// 9
	600 / ENCRESSCALE,	// 10
};

#define BANDPAD 15000UL	/* 15 kHz - запас по сторонам от любительского диапазона */

#if (FIXSCALE_48M0_X1_DIV256 || (defined(PLL1_TYPE) && (PLL1_TYPE == PLL_TYPE_SI570))) && WITHMODESETSMART
	#define BANDFUSBFREQ	13800000L	/* Выше этой частоты по умолчанию используется USB */
#else
	#define BANDFUSBFREQ	9000000L	/* Выше этой частоты по умолчанию используется USB */
#endif
/* BANDMIDDLE - граница, по которой происходит разделение двух обзорных диапазонов */
#if defined (BANDMIDDLE)

#elif CTLSTYLE_SW2011ALL

	#define BANDMIDDLE	15000000L
	#define UPPER_DEF	19000000L

#elif TUNE_TOP >= 54000000L

	#define BANDMIDDLE  32000000L //(29700000 + BANDPAD)
	#define UPPER_DEF	40000000L

#elif TUNE_TOP >= 30000000L

	#define BANDMIDDLE	20000000L
	#define UPPER_DEF	24000000L

#elif TUNE_TOP >= 16000000L

	// Р-143 "Багульник"
	#define BANDMIDDLE	15000000L
	#define UPPER_DEF	17000000L

#else
	// Р-143 "Багульник"
	#define BANDMIDDLE	12000000L
	#define UPPER_DEF	13000000L

#endif

#define TUNE_6MBAND	((TUNE_TOP) >= (54000000L + BANDPAD))		// наличие в аппарате диапазона 6 метров
#define TUNE_4MBAND	((TUNE_TOP) >= (70500000L + BANDPAD))		// наличие в аппарате диапазона 4 метра
#define TUNE_2MBAND	((TUNE_TOP) >= (146000000L + BANDPAD))		// наличие в аппарате диапазона 2 метра
#define TUNE_07MBAND	((TUNE_TOP) >= (440000000L + BANDPAD))		// наличие в аппарате диапазона 70 сантиметров

enum
{
	BANDSETF_HAM = 0x00,
	BANDSETF_2M = 0x10,
	BANDSETF_6M = 0x20,
	BANDSETF_4M = 0x30,
	BANDSETF_07M = 0x40,
	BANDSETF_CB = 0x50,
	BANDSETF_ALL = 0x60,	/* диапазон присутствует во всех наборах диапазонов */
	BANDSETF_BCAST = 0x70,	/* вещательный диапазон */
	BANDSETF_HAMWARC = 0x80,
	//
	BANDSET_MASK = 0xf0,
	BANDSET_SUBMODE = 0x0f
};

enum
{
	BANDGROUP_1p8MHz,
	BANDGROUP_3p5MHz,
	BANDGROUP_7MHz,
	BANDGROUP_10p1MHz,
	BANDGROUP_14MHz,
	BANDGROUP_18MHz,
	BANDGROUP_21MHz,
	BANDGROUP_24p8MHz,
	BANDGROUP_28MHz,
	BANDGROUP_70MHz,
	BANDGROUP_144MHz,
	BANDGROUP_430MHz,
	//
	BANDGROUP_COUNT			// Значение, используемое как признак отсутствия группировки диапазонов
};


static const char * const bandlabels [BANDGROUP_COUNT] =
{
	"1.8",
	"3.5",
	"7  ",
	"10 ",
	"14 ",
	"18 ",
	"21 ",
	"24 ",
	"28 ",
	"70 ",
	"144",
	"430",
};

#if (FLASHEND > 0x7FFF)	
#else
#endif

#if (FLASHEND > 0x3FFF)	|| CPUSTYLE_ARM || CPUSTYPE_TMS320F2833X || (TUNE_TOP >= 65535000L)

	/* частоты хранятся с точностью до герца в 32-х битных переменных */
	struct bandrange {
		uint32_t bottom, top;
		uint32_t init;
		uint8_t defsubmode_bandset;
		uint8_t	bandgroup;
		char label [9];
	};

	#define BMF(a) (a)		/* получение инициализационного элемента */
	#define PEEK_BMF(v) (v)	/* выборка значения для использования */

#else
	/* иначе, частоты хранятся с точностью до килогерца в 16-ти битных переменных */
	struct bandrange {
		uint16_t bottom, top;
		uint16_t init;
		uint8_t 	defsubmode_bandset;
		uint8_t	bandgroup;
		char label[9];
	};

	#define BMF_SCALE	1000UL
	#define BMF(a) ((a) / BMF_SCALE )	/* получение инициализационного элемента */
	#define PEEK_BMF(v) ((v) * BMF_SCALE)	/* выборка значения для использования */

#endif

#if WITHMODESETSMART
	#define BANDMAPSUBMODE_LSB	SUBMODE_SSBSMART
	#define BANDMAPSUBMODE_USB	SUBMODE_SSBSMART
	#define BANDMAPSUBMODE_CW	SUBMODE_CWSMART
	#define BANDMAPSUBMODE_CWR	SUBMODE_CWSMART
	#define BANDMAPSUBMODE_AM	SUBMODE_SSBSMART
#else /* WITHMODESETSMART */
	#define BANDMAPSUBMODE_LSB	SUBMODE_LSB
	#define BANDMAPSUBMODE_USB	SUBMODE_USB
	#define BANDMAPSUBMODE_CW	SUBMODE_CW
	#define BANDMAPSUBMODE_CWR	SUBMODE_CWR
	#define BANDMAPSUBMODE_AM	SUBMODE_AM
	#define BANDMAPSUBMODE_WFM	SUBMODE_WFM	
#endif /* WITHMODESETSMART */

/*
	60-meter band (all modes - USB):
	5330.5, 5346.5, 5366.5, 5371.5 and 5403.5 kHz

	UK: ~5298..~5408

*/

static FLASHMEM struct bandrange  const bandsmap [] =
{
#if FQMODEL_FMRADIO	// 87..108.5 MHz
	{ BMF(89000000L), 			BMF(89500000L), 			BMF(79000000L), 	BANDMAPSUBMODE_WFM | BANDSETF_ALL, BANDGROUP_COUNT, "", },				/*  */
	{ BMF(90000000L), 			BMF(90500000L), 			BMF(90000000L), 	BANDMAPSUBMODE_WFM | BANDSETF_ALL, BANDGROUP_COUNT, "", },				/*  */
	{ BMF(102000000L), 			BMF(102500000L), 			BMF(102000000L), 	BANDMAPSUBMODE_WFM | BANDSETF_ALL, BANDGROUP_COUNT, "", },				/*  */
	/* Далее никаких диапазонов добавлять нельзя - это служебные элементы и их порядок зависит от других частей пронграммы (band_up и band_down). */
	{ BMF(TUNE_BOTTOM), 		BMF(90000000L), 			BMF(TUNE_BOTTOM),	BANDMAPSUBMODE_USB | BANDSETF_ALL, BANDGROUP_COUNT, "", },				/* обзорный диапазон - HBANDS_COUNT should equal to this index */
	{ BMF(90000000L), 			BMF(TUNE_TOP), 				BMF(101400000L), 	BANDMAPSUBMODE_USB | BANDSETF_ALL, BANDGROUP_COUNT, },					/* обзорный диапазон */
	/* VFOS */
	{ BMF(TUNE_BOTTOM), 		BMF(TUNE_TOP), 				BMF(101400000L), 	BANDMAPSUBMODE_USB | BANDSETF_ALL, BANDGROUP_COUNT, "", },				/* VFO A - VFOS_BASE should equal to this index */
	{ BMF(TUNE_BOTTOM), 		BMF(TUNE_TOP), 				BMF(101400000L), 	BANDMAPSUBMODE_USB | BANDSETF_ALL, BANDGROUP_COUNT, "", },				/* VFO B */
#else /* FQMODEL_FMRADIO */
//	{ BMF(135000 - 0), 			BMF(138000 + 0), 			BMF(136000), 		SUBMODE_USB, 						BANDGROUP_COUNT, "", },				/* 135),7-137),8 кГц */
	#if TUNE_BOTTOM <= (153000 - BANDPAD)
	{ BMF(153000L - BANDPAD), 	BMF(279000L + BANDPAD), 	BMF(225000L), 		BANDMAPSUBMODE_AM | BANDSETF_ALL, BANDGROUP_COUNT, "LW", },				/*  */
	{ BMF(530000L - BANDPAD), 	BMF(1611000L + BANDPAD), 	BMF(1440000L), 		BANDMAPSUBMODE_AM | BANDSETF_ALL, BANDGROUP_COUNT, "MW", },				/*  */
	#endif
	{ BMF(1810000L - BANDPAD), 	BMF(2000000L + BANDPAD), 	BMF(1810000L), 		BANDMAPSUBMODE_LSB | BANDSETF_HAM, BANDGROUP_1p8MHz, "1.8M"},			/* Ukrainian band from freq 1715 kHz */
	{ BMF(3500000L - BANDPAD), 	BMF(3800000L + BANDPAD), 	BMF(3500000L),		BANDMAPSUBMODE_LSB | BANDSETF_HAM, BANDGROUP_3p5MHz, "3.5M", },			/*  */
#if 0
	{ BMF(2535000L - BANDPAD), 	BMF(2900000L + BANDPAD), 	BMF(2535000L), 		BANDMAPSUBMODE_USB | BANDSETF_HAM, BANDGROUP_COUNT, "", },				/* Old NEDRA stations band */
#endif
	{ BMF(3900000L), 			BMF(4000000L), 				BMF(3900000L), 		BANDMAPSUBMODE_AM | BANDSETF_BCAST, BANDGROUP_COUNT, "75m", },			/*  */
	{ BMF(4750000L), 			BMF(5060000L), 				BMF(4750000L), 		BANDMAPSUBMODE_AM | BANDSETF_BCAST, BANDGROUP_COUNT, "", },				/*  */
	/*
		Частотный план диапазона 5 MHz ( 60m )
		Диапазон: 5351.5-5.366.5 khz
		1. 5.351.5- CW -полоса 200 hz .
		2. 5.354.0, 5.357.0, 5.360.0, 5.363.0 - ALL MODE полоса 2700 hz.
		3. 5366.0 - weak signal полоса 20 hz.
		На самом деле "цифра" около 5,357,0, CW 5.351.5 и выше SSB около 5.360.0
	*/
	{ BMF(5298000L), 			BMF(5408000L), 				BMF(5351500L), 		BANDMAPSUBMODE_CW | BANDSETF_HAMWARC, 	BANDGROUP_COUNT, "", },			/* 60-meters band */
	{ BMF(5730000L), 			BMF(6295000L), 				BMF(5730000L), 		BANDMAPSUBMODE_AM | BANDSETF_BCAST, 	BANDGROUP_COUNT, "49m", },		/*  */
	{ BMF(6890000L), 			BMF(6990000L), 				BMF(6890000L), 		BANDMAPSUBMODE_AM | BANDSETF_BCAST,  	BANDGROUP_3p5MHz, "3.5M", },			/*  */
	{ BMF(7000000L - BANDPAD), 	BMF(7200000L + BANDPAD), 	BMF(7000000L), 		BANDMAPSUBMODE_LSB | BANDSETF_HAM, 		BANDGROUP_7MHz, "7M", },		/* top freq - 7300 in region-2 */
	{ BMF(7200000L), 			BMF(7600000L), 				BMF(7200000L), 		BANDMAPSUBMODE_AM | BANDSETF_BCAST,		BANDGROUP_COUNT, "41m", },		/*  */
	{ BMF(9250000L), 			BMF(9900000L), 				BMF(9250000L), 		BANDMAPSUBMODE_AM | BANDSETF_BCAST, 	BANDGROUP_COUNT, "31m", },		/*  */
	{ BMF(10100000L - BANDPAD), BMF(10150000L + BANDPAD), 	BMF(10100000L), 	BANDMAPSUBMODE_CW | BANDSETF_HAMWARC, 	BANDGROUP_10p1MHz, "10.1M", },			/*  */
#if (TUNE_TOP) >= (19020000)
	{ BMF(11500000L), 			BMF(12160000), 				BMF(11500000L), 	BANDMAPSUBMODE_AM | BANDSETF_BCAST,  	BANDGROUP_COUNT, "25m", },		/*  */
	{ BMF(13570000L), 			BMF(13870000), 				BMF(13570000L), 	BANDMAPSUBMODE_AM | BANDSETF_BCAST,  	BANDGROUP_COUNT, "22m", },		/*  */

	{ BMF(14000000L - BANDPAD), BMF(14105000L), 			BMF(14000000L), 	BANDMAPSUBMODE_CW | BANDSETF_HAM, 		BANDGROUP_14MHz, "14M CW", },	/*  */
	{ BMF(14105000L),			BMF(14350000L + BANDPAD),	BMF(14130000L), 	BANDMAPSUBMODE_USB | BANDSETF_HAM, 		BANDGROUP_14MHz, "14M SSB", },	/*  */

	{ BMF(15030000L), 			BMF(15800000), 				BMF(15030000L), 	BANDMAPSUBMODE_AM | BANDSETF_BCAST,  	BANDGROUP_COUNT, "19m", },		/*  */
	{ BMF(17480000L), 			BMF(17900000), 				BMF(17480000L), 	BANDMAPSUBMODE_AM | BANDSETF_BCAST,  	BANDGROUP_COUNT, "16m", },		/*  */
	{ BMF(18068000L - BANDPAD), BMF(18168000L + BANDPAD), 	BMF(18068000L), 	BANDMAPSUBMODE_USB | BANDSETF_HAMWARC, 	BANDGROUP_18MHz, "18M", },			/*  */
	{ BMF(18900000L), 			BMF(19020000), 				BMF(18900000L), 	BANDMAPSUBMODE_AM | BANDSETF_BCAST,  	BANDGROUP_COUNT, "15m", },		/*  */
#endif
#if (TUNE_TOP) >= (21450000L + BANDPAD)
	{ BMF(21000000L - BANDPAD), BMF(21155000L), 			BMF(21000000L), 	BANDMAPSUBMODE_CW | BANDSETF_HAM, 		BANDGROUP_21MHz, "21M CW", },		/*  */
	{ BMF(21155000L), 			BMF(21450000L + BANDPAD), 	BMF(21160000L), 	BANDMAPSUBMODE_USB | BANDSETF_HAM, 		BANDGROUP_21MHz, "21M SSB", },		/*  */
#endif
#if (TUNE_TOP) >= (21850000L)
	{ BMF(21450000L), 			BMF(21850000), 				BMF(21450000L), 	BANDMAPSUBMODE_AM | BANDSETF_BCAST,  	BANDGROUP_COUNT, "13m", },		/*  */
#endif /* (TUNE_TOP) >= (21850000) */

#if (TUNE_TOP) >= (29700000L + BANDPAD)
	{ BMF(24890000L - BANDPAD), BMF(24990000L + BANDPAD), 	BMF(24890000L), 	BANDMAPSUBMODE_USB | BANDSETF_HAMWARC, 	BANDGROUP_24p8MHz, "24M", },			/*  */
	{ BMF(25670000L), 			BMF(26100000), 				BMF(25670000L), 	BANDMAPSUBMODE_AM | BANDSETF_BCAST,  	BANDGROUP_COUNT, "11m", },		/*  */
	{ BMF(26965000L - BANDPAD), BMF(27405000L + BANDPAD), 	BMF(27120000L), 	BANDMAPSUBMODE_USB | BANDSETF_CB, 		BANDGROUP_COUNT, "CB", },		/* Citizens Band 26.9650 MHz to 27.4050 MHz (40 channels) */

	/* next three sections - one band - "ten". */
	{ BMF(28000000L - BANDPAD), BMF(28320000L), 			BMF(28000000L), 	BANDMAPSUBMODE_CW | BANDSETF_HAM, 		BANDGROUP_28MHz, "28M CW", },	/* CW */
	{ BMF(28320000L), 			BMF(29200000L), 			BMF(28500000L), 	BANDMAPSUBMODE_USB | BANDSETF_HAM, 		BANDGROUP_28MHz, "28M SSB", },	/* SSB */
	{ BMF(29200000L), 			BMF(29700000L + BANDPAD),	BMF(29600000L), 	BANDMAPSUBMODE_USB | BANDSETF_HAM, 		BANDGROUP_28MHz, "28M FM", },	/* FM */
#endif

#if TUNE_6MBAND
	{ BMF(50000000L - BANDPAD), BMF(54000000L + BANDPAD), 	BMF(50100000L), 	BANDMAPSUBMODE_USB | BANDSETF_6M, 		BANDGROUP_COUNT, "50M SSB", },			/* 6 meters HAM band */
#endif /* TUNE_6MBAND */

#if TUNE_4MBAND
	{ BMF(70000000L - BANDPAD),	BMF(70050000L), 			BMF(70000000L), 	BANDMAPSUBMODE_CW | BANDSETF_4M, 		BANDGROUP_70MHz, "70M CW", },			/* CW */
	{ BMF(70050000L), 			BMF(70300000L), 			BMF(70050000L), 	BANDMAPSUBMODE_USB | BANDSETF_4M, 		BANDGROUP_70MHz, "70M SSB", },			/* SSB */
	{ BMF(70300000L), 			BMF(70500000L + BANDPAD),	BMF(70300000L), 	BANDMAPSUBMODE_USB | BANDSETF_4M, 		BANDGROUP_70MHz, "70M FM", },			/* FM */
#endif /* TUNE_4MBAND */

#if TUNE_2MBAND
	/* next three sections - one band - "2 meter". */
	{ BMF(144000000L - BANDPAD),BMF(144200000), 			BMF(144050000L), 	BANDMAPSUBMODE_CW | BANDSETF_2M, 		BANDGROUP_144MHz, "144M CW", },		/* CW */
	{ BMF(144200000L), 			BMF(145000000L), 			BMF(144300000L), 	BANDMAPSUBMODE_USB | BANDSETF_2M, 		BANDGROUP_144MHz, "144M SSB", },		/* SSB */
	{ BMF(144500000L), 			BMF(146000000L + BANDPAD),	BMF(145500000L), 	BANDMAPSUBMODE_USB | BANDSETF_2M, 		BANDGROUP_144MHz, "144M FM", },		/* FM */
#endif /* TUNE_2MBAND */

#if TUNE_07MBAND
	/* next three sections - one band - "0.7 meter". */
	{ BMF(430000000L - BANDPAD),BMF(432100000L), 			BMF(430050000L), 	BANDMAPSUBMODE_CW | BANDSETF_07M, 		BANDGROUP_430MHz, "", },		/* CW */
	{ BMF(432100000L),			BMF(440000000L + BANDPAD), 	BMF(432500000L), 	BANDMAPSUBMODE_USB | BANDSETF_07M, 		BANDGROUP_430MHz, "", },		/* CW */
#endif /* TUNE_2MBAND */
	/* далее никаких диапазонов добавлять нельзя - это служебные элементы и их порядок зависит от других частей пронграммы (band_up и band_down). */
	{ BMF(TUNE_BOTTOM), 		BMF(BANDMIDDLE), 			BMF(4997000L), 		BANDMAPSUBMODE_USB | BANDSETF_ALL, 		BANDGROUP_COUNT, "", },			/* обзорный диапазон - HBANDS_COUNT should equal to this index */
	{ BMF(BANDMIDDLE), 			BMF(TUNE_TOP), 				BMF(UPPER_DEF), 	BANDMAPSUBMODE_USB | BANDSETF_ALL, 		BANDGROUP_COUNT, "", },			/* обзорный диапазон */
	/* VFOS */
	{ BMF(TUNE_BOTTOM), 		BMF(TUNE_TOP), 				BMF(DEFAULTDIALFREQ), BANDMAPSUBMODE_USB | BANDSETF_ALL, 	BANDGROUP_COUNT, "", },			/* VFO A - VFOS_BASE should equal to this index */
	{ BMF(TUNE_BOTTOM), 		BMF(TUNE_TOP), 				BMF(DEFAULTDIALFREQ), BANDMAPSUBMODE_USB | BANDSETF_ALL, 	BANDGROUP_COUNT, "", },			/* VFO B */
#endif /* FQMODEL_FMRADIO */
};

#define VFOS_COUNT 2	/* два VFO - A и B */
#define XBANDS_COUNT 2	/* два обзорных диапазона */
#define HBANDS_COUNT ((sizeof bandsmap / sizeof bandsmap [0]) - XBANDS_COUNT - VFOS_COUNT)
#define VFOS_BASE ((sizeof bandsmap / sizeof bandsmap [0]) - VFOS_COUNT)
#define XBANDS_BASE0	(HBANDS_COUNT + 0)	/* первая из двух ячеек с обзорными диапазонами */
#define XBANDS_BASE1	(HBANDS_COUNT + 1)	/* вторая из двух ячеек с обзорными диапазонами */
#define MBANDS_BASE (HBANDS_COUNT + XBANDS_COUNT + VFOS_COUNT)	/* первая ячейка с фиксированными настройками */


#if WITHSWLMODE

	#if ! defined (NVRAM_TYPE) || ! defined (NVRAM_END)
		#error NVRAM_TYPE or NVRAM_END not defined here
	#endif

	#if NVRAM_END <= 511
		#define MBANDS_COUNT	21	/* количество ячеек фиксированных частот */
		typedef uint_fast8_t vindex_t;
	#elif NVRAM_END <= 4095
		#define MBANDS_COUNT	100 //150	/* количество ячеек фиксированных частот */
		typedef uint_fast8_t vindex_t;
	#else
		#define MBANDS_COUNT	1000 // (254 - MBANDS_BASE)	/* количество ячеек фиксированных частот */
		typedef uint_fast16_t vindex_t;
	#endif

#else

	#define MBANDS_COUNT	0	/* количество ячеек фиксированных частот */
	typedef uint_fast8_t vindex_t;

#endif	/* WITHSWLMODE */

static vindex_t getfreqband(uint_fast32_t freq);

/* получение индекса хранения VFO в памяти в зависимости от текущего режима расстройки
   - в режиме приема
*/
static vindex_t 
getvfoindex(uint_fast8_t bi)
{
	ASSERT(bi < 2);
	return VFOS_BASE + bi;
}

/* получение человекопонятного названия диапазона */
const char *
get_band_label3(unsigned b)	/* b: диапазон в таблице bandsmap */
{
	const uint_fast8_t bandgroup = bandsmap [b].bandgroup;
	if (bandgroup >= ARRAY_SIZE(bandlabels))
		return "ERR";
	if (bandlabels [bandgroup] == NULL)
		return "NUL";	// недостаточно правильно заполненная таблица
	return bandlabels [bandgroup];
}

/* интерфейсная функция доступа к параметра диапазона */
static uint_fast32_t
//NOINLINEAT
get_band_bottom(vindex_t b)	/* b: диапазон в таблице bandsmap */
{
	return PEEK_BMF(bandsmap [b].bottom);
}
/* интерфейсная функция доступа к параметра диапазона */
static uint_fast32_t
//NOINLINEAT
get_band_top(vindex_t b)	/* b: диапазон в таблице bandsmap */
{
	return PEEK_BMF(bandsmap [b].top);
}
/* интерфейсная функция доступа к параметра диапазона */
static uint_fast32_t
//NOINLINEAT
get_band_init(vindex_t b)	/* b: диапазон в таблице bandsmap */
{
	return PEEK_BMF(bandsmap [b].init);
}
/* интерфейсная функция доступа к параметра диапазона */
static uint_fast8_t
//NOINLINEAT
get_band_defsubmode(vindex_t b)	/* b: диапазон в таблице bandsmap */
{
	return bandsmap [b].defsubmode_bandset & BANDSET_SUBMODE;
}

/* интерфейсная функция доступа к параметра диапазона */
static uint_fast8_t
//NOINLINEAT
get_band_bandset(vindex_t b)	/* b: диапазон в таблице bandsmap */
{
	return bandsmap [b].defsubmode_bandset & BANDSET_MASK;
}

static const char * get_band_label(vindex_t b)	/* b: диапазон в таблице bandsmap */
{
	return bandsmap [b].label;
}

/* "карта" режимов,
 * Переход по строкам идет по короткому нажатию кнопки "режим",
 * переход в пределах сроки идет по длинному нажатию кнопки "режим".
 */
#if WITHBBOX && defined (WITHBBOXSUBMODE)
	static const uint_fast8_t modes [][2] =
	{
		{ 1, WITHBBOXSUBMODE, },
	};
#elif WITHMODESETSMART
	static const uint_fast8_t modes [][2] =
	{
		{ 1, SUBMODE_SSBSMART, },
		{ 1, SUBMODE_CWSMART, },
		{ 1, SUBMODE_DIGISMART, },
	};
#elif WITHMODESETMIXONLY	// Use only product detector
	static const uint_fast8_t modes [][3] =
	{
		{ 2, SUBMODE_LSB, SUBMODE_USB, },
		{ 2, SUBMODE_CWR, SUBMODE_CW, },
	};
#elif WITHMODESETMIXONLY3
	static const uint_fast8_t modes [][4] =
	{
		{ 2, SUBMODE_LSB, SUBMODE_USB, },
		{ 2, SUBMODE_CWR, SUBMODE_CW, },
		{ 3, SUBMODE_DGU, SUBMODE_DGL, SUBMODE_CWZ, },
	};
#elif WITHMODESAMONLY
	static const uint_fast8_t modes [][2] =
	{
		{ 1, SUBMODE_AM, },
	};
#elif WITHMODESETMIXONLY3AM
	static const uint_fast8_t modes [][4] =
	{
		{ 2, SUBMODE_LSB, SUBMODE_USB, },
		{ 2, SUBMODE_CWR, SUBMODE_CW, },
		{ 2, SUBMODE_AM, SUBMODE_CWZ, },
		{ 2, SUBMODE_DGU, SUBMODE_DGL, },
	};
#elif WITHMODESETMIXONLY3NFM // SW2014FM modes set
	static const uint_fast8_t modes [][3] =
	{
		{ 2, SUBMODE_LSB, SUBMODE_USB, },
		{ 2, SUBMODE_CWR, SUBMODE_CW, },
		{ 2, SUBMODE_NFM, SUBMODE_CWZ, },
		{ 2, SUBMODE_DGU, SUBMODE_DGL, },
	};
#elif WITHMODESETMIXONLY2
	static const uint_fast8_t modes [][4] =
	{
		{ 2, SUBMODE_LSB, SUBMODE_USB, },
		{ 3, SUBMODE_CWR, SUBMODE_CW, SUBMODE_CWZ },
	};
#elif (WITHMODESETFULLNFM && WITHWFM)
	static const uint_fast8_t modes [][5] =
	{
		{ 2, SUBMODE_LSB, SUBMODE_USB, },
		{ 2, SUBMODE_CWR, SUBMODE_CW, },
		{ 3, SUBMODE_AM, SUBMODE_CWZ, SUBMODE_DRM, },
		{ 4, SUBMODE_NFM, SUBMODE_WFM, SUBMODE_DGU, SUBMODE_DGL, },
	};
#elif WITHMODESETFULLNFM
	#if KEYB_FPANEL30_V3
	static const uint_fast8_t modes [][4] =
		{
			{ 2, SUBMODE_LSB, SUBMODE_USB, },				// ROW 0
			{ 2, SUBMODE_CWR, SUBMODE_CW, },				// ROW 1
			{ 3, SUBMODE_AM, SUBMODE_CWZ, SUBMODE_DRM, },	// ROW 2
			{ 3, SUBMODE_NFM, SUBMODE_DGU, SUBMODE_DGL, },	// ROW 3
		};
	#elif WITHMODEM
		static const uint_fast8_t modes [][5] =
		{
			{ 1, SUBMODE_BPSK, },
		};
	#elif WITHSAM
	static const uint_fast8_t modes [][5] =
		{
			{ 2, SUBMODE_LSB, SUBMODE_USB, },
			{ 2, SUBMODE_CWR, SUBMODE_CW, },
			{ 4, SUBMODE_AM, SUBMODE_SAM, SUBMODE_CWZ, SUBMODE_DRM, },
			{ 3, SUBMODE_NFM, SUBMODE_DGU, SUBMODE_DGL, },
		#if WITHRTTY
			{ 1, SUBMODE_RTTY, },
		#endif /* WITHRTTY */
		};
	#else /* WITHMODEM */
	static const uint_fast8_t modes [][4] =
		{
			{ 2, SUBMODE_LSB, SUBMODE_USB, },
			{ 2, SUBMODE_CWR, SUBMODE_CW, },
			{ 3, SUBMODE_AM, SUBMODE_CWZ, SUBMODE_DRM, },
			{ 3, SUBMODE_NFM, SUBMODE_DGU, SUBMODE_DGL, },
		};
	#endif /* WITHMODEM */
#elif WITHMODESETFULLNFMWFM
	static const uint_fast8_t modes [][5] =
		{
			{ 2, SUBMODE_LSB, SUBMODE_USB, },
			{ 2, SUBMODE_CWR, SUBMODE_CW, },
			{ 4, SUBMODE_AM, SUBMODE_SAM, SUBMODE_CWZ, SUBMODE_DRM, },
			{ 4, SUBMODE_NFM, SUBMODE_WFM, SUBMODE_DGU, SUBMODE_DGL, },
		};
#elif WITHMODESETFULL
	static const uint_fast8_t modes [][4] =
	{
		{ 2, SUBMODE_LSB, SUBMODE_USB, },
		{ 2, SUBMODE_CWR, SUBMODE_CW, },
		{ 3, SUBMODE_AM, SUBMODE_CWZ, SUBMODE_DRM, },
		{ 2, SUBMODE_DGU, SUBMODE_DGL, },
	};
#elif WITHMODESET_IGOR
	static const uint_fast8_t modes [][4] =
	{
		{ 2, SUBMODE_LSB, SUBMODE_USB, },
	};
#else
	#error WITHMODESETxxx not defined
#endif /*  */


#define MODEROW_COUNT (sizeof modes / sizeof modes [0])

/* поиск координаты режима в карте режимов.
   код возврвта функции - колонка
   Если не найден - возврат 0 и строки 0.
   TODO: при попытке поиска режима FM, если его нет в списке, ставится LSB (0-й режим в 0-й строке. Не очень красиво.
*/
static uint_fast8_t
//NOINLINEAT
locatesubmode(
	const uint_fast8_t submode,		/* код режима */
	uint_fast8_t * const xrow		/* найденные координаты */
	)
{
	uint_fast8_t row;

	for (row = 0; row < MODEROW_COUNT; ++ row)
	{	
		const uint_fast8_t n = modes [row] [0];
		uint_fast8_t col;

		for (col = 0; col < n && col < (sizeof modes [row] / sizeof modes [row][0] - 1); ++ col)
		{
			if (modes [row][col + 1] == submode)
			{
				* xrow = row;
				return col;
			}
		}
	}
	* xrow = 0;
	return 0;
}

#if 0
/* проверка наличия режима в карте режимов.
   Если не найден - возврат 0
*/
static uint_fast8_t
validatesubmode(
	const uint_fast8_t submode		/* код режима */
	)
{
	uint_fast8_t row;

	for (row = 0; row < MODEROW_COUNT; ++ row)
	{	
		const uint_fast8_t n = modes [row] [0];
		uint_fast8_t col;

		for (col = 0; col < n && col < (sizeof modes [row] / sizeof modes [row][0] - 1); ++ col)
		{
			if (modes [row][col + 1] == submode)
			{
				return 1;
			}
		}
	}
	return 0;
}
#endif

/* текст (любой), используемый как сигнатура содержимого NVRAM */
/* последний байт этого массива в NVRAM не запоминается и не сравнивается. */
static const FLASHMEM char nvramsign [] =
#if WITHKEEPNVRAM
		/* ослабить проверку совпадения версий прошивок для стирания NVRAM */
		{ 0, 0, 0, 0, 1, 1, 1, 1, };
#else /* WITHKEEPNVRAM */

#if NVRAM_END > 511
	__DATE__
#endif
__TIME__;
#endif /* WITHKEEPNVRAM */

/* Шаблон данных для тестирования доступа к NVRAM */
static const FLASHMEM char nvrampattern [sizeof nvramsign / sizeof nvramsign [0]] = 
{
	"DEADBEEF"
};

/* структура - расположение байтов в конфигурационной памяти.
   bitfields нельзя использовать, так как всё это - только обозначения смещений
	 переменных в конфигурационной памяти.
 	параметры "семейства" режимов - CW&CWR, LSB&USB.
	*/
struct modeprops
{
	uint8_t agc;	/* режим АРУ для данногосемейства режимов */
	uint8_t filter;	/* индекс фильтра в общей таблице фильтров */
	//uint16_t step;	/* шаг валкодера в данном режиме */

#if CTLSTYLE_RA4YBO || CTLSTYLE_RA4YBO_V1 || CTLSTYLE_RA4YBO_V2 || CTLSTYLE_RA4YBO_V3
	uint16_t txpower;		/* мощность */
	uint16_t txcompr;		/* степень компрессии */
#endif /* CTLSTYLE_RA4YBO || CTLSTYLE_RA4YBO_V1 || CTLSTYLE_RA4YBO_V2*/

#if WITHIF4DSP
	uint8_t txaudio;	/* источник звука для передачи */
	uint8_t noisereduct;	/* включение NR для данного режима */
#endif /* WITHIF4DSP */

} ATTRPACKED;// аттрибут GCC, исключает "дыры" в структуре. Так как в ОЗУ нет копии этой структуры, see also NVRAM_TYPE_BKPSRAM

/* структура - расположение байтов в конфигурационном ОЗУ.
   bitfields нельзя использовать, так как всё это - только обозначения смещений
	 переменных в конфигурационном ОЗУ.
	Информация, сохраняемая для каждого диапазона */
struct bandinfo
{	
	uint32_t freq;		/* рабочая частота */
	uint8_t modecols [MODEROW_COUNT];	/* массив режимов работы - каждый байт указывают номер позиции в каждой строке) */
	uint8_t moderow;		/* номер режима работы в маске (номер тройки бит) */
#if ! WITHONEATTONEAMP
	uint8_t pamp;		/* режим УВЧ */
#endif /* ! WITHONEATTONEAMP */
	uint8_t att;		/* режим аттенюатора */
#if WITHANTSELECTRX
	uint8_t rxant;		/* код выбора антенны (0/1) */
	uint8_t ant;		/* признак включения приемной антенны */
#elif WITHANTSELECT2
	uint8_t ant;		/* антенна при ручном выборе */
#elif WITHANTSELECT
	uint8_t ant;		/* код выбора антенны (1T+1R, 2T+1R и так далее, не код антенны (0/1) */
#endif /* WITHANTSELECT || WITHANTSELECTRX */
#if WITHAUTOTUNER
	// todo: добавить учет включенной антенны
	uint8_t tunercap;
	uint8_t tunerind;
	uint8_t tunertype;
	uint8_t tunerwork;
#endif /* WITHAUTOTUNER */
#if WITHSPECTRUMWF
	uint8_t gzoomxpow2;	/* уменьшение отображаемого участка спектра */
	uint8_t gtopdbspe;		/* нижний предел FFT */
	uint8_t gbottomdbspe;	/* верхний предел FFT */
	uint8_t gtopdbwfl;		/* нижний предел FFT waterflow */
	uint8_t gbottomdbwfl;	/* верхний предел FFT waterflow */
#endif /* WITHSPECTRUMWF */
} ATTRPACKED;// аттрибут GCC, исключает "дыры" в структуре. Так как в ОЗУ нет копии этой структуры, see also NVRAM_TYPE_BKPSRAM

/* структура - расположение байтов в конфигурационном ОЗУ.
   bitfields нельзя использовать, так как всё это - только обозначения смещений
	 переменных в конфигурационном ОЗУ.
 	 Информация, сохраняемая для группы диапазонов */
struct bandgroup_tag {
	uint8_t	band;		/* последний диапазон в группе, куда был переход по кнопке диапазона (индекс в bands). */
} ATTRPACKED;	// аттрибут GCC, исключает "дыры" в структуре. Так как в ОЗУ нет копии этой структуры, see also NVRAM_TYPE_BKPSRAM

/* структура - расположение байтов в конфигурационном ОЗУ.
   bitfields нельзя использовать, так как всё это - только обозначения смещений
	 переменных в конфигурационном ОЗУ.
*/
struct nvmap
{
	uint8_t lockmode;			/* блокировка валкодера */
#if WITHENCODER2
	uint8_t enc2state;
	uint8_t enc2pos;			// выбраный пунки меню (второй валкодер)
#endif /* WITHENCODER2 */
#if WITHUSEFAST
	uint8_t gusefast;			/* переключение в режим крупного шага */
#endif /* WITHUSEFAST */
	uint8_t menuset;		/* набор функций кнопок и режим отображения на дисплее */

	/* группы */
	uint8_t ggroup;			/* последняя группа в менюю, с которой работали */
	uint8_t	ggrpdisplay;	// последний посещённый пункт группы
	uint8_t	ggrptxadj;		// последний посещённый пункт группы
	uint8_t	ggrpsecial;		// последний посещённый пункт группы
	uint8_t	ggrpaudio;		// последний посещённый пункт группы
#if WITHSUBTONES && WITHTX
	uint8_t ggrpctcss;		// последний посещённый пункт группы
#endif /* WITHSUBTONES && WITHTX */
#if defined (RTC1_TYPE)
	uint8_t	ggrpclock; // последний посещённый пункт группы
#endif /* defined (RTC1_TYPE) */
	uint8_t	ggrpabout;		// последний посещённый пункт группы

#if LO1MODE_HYBRID
	uint8_t alignmode;			/* режимы для настройки аппаратной части (0-нормальная работа) */
#endif /* LO1MODE_HYBRID */
#if WITHUSEDUALWATCH
	uint8_t mainsubrxmode;		// Левый/правый, A - main RX, B - sub RX
#endif /* WITHUSEDUALWATCH */
#if WITHENCODER
	uint8_t genc1pulses;		/* индекс в таблице разрешений валкодера */
	uint8_t genc1dynamic;	/* включение динамического енкодера */
#endif /* WITHENCODER */

#if defined (DEFAULT_LCD_CONTRAST)
	uint8_t gcontrast;		/* Контрастность LCD */
#endif /* defined (DEFAULT_LCD_CONTRAST) */
#if WITHLCDBACKLIGHT
	uint8_t gbglight;
#endif /* WITHLCDBACKLIGHT */
#if WITHDCDCFREQCTL
	//uint16_t dcdcrefdiv;
#endif /* WITHDCDCFREQCTL */
#if WITHKBDBACKLIGHT
	uint8_t gkblight;
#endif /* WITHKBDBACKLIGHT */
#if WITHLCDBACKLIGHT || WITHKBDBACKLIGHT
	uint8_t gdimmtime;
#endif /* WITHLCDBACKLIGHT || WITHKBDBACKLIGHT */
#if WITHFANTIMER
	uint8_t gfanpatime;
	#if WITHFANPWM
		uint16_t gfanpapwm;
	#endif /* WITHFANPWM */
#endif /* WITHFANTIMER */
#if WITHSLEEPTIMER
	uint8_t gsleeptime;
#endif /* WITHSLEEPTIMER */
#if LCDMODE_COLORED
	//uint8_t gbluebgnd;
#endif /* LCDMODE_COLORED */

#if WITHMIC1LEVEL
	uint16_t gmik1level;
#endif /* WITHMIC1LEVEL */
#if defined(CODEC1_TYPE) && (CODEC1_TYPE == CODEC_TYPE_NAU8822L)
	uint8_t ALCNEN;// = 0;	// ALC noise gate function control bit
	uint8_t ALCNTH;// = 0;	// ALC noise gate threshold level
	uint8_t ALCEN;// = 1;	// only left channel ALC enabled
	uint8_t ALCMXGAIN;// = 7;	// Set maximum gain limit for PGA volume setting changes under ALC control
	uint8_t ALCMNGAIN;// = 0;	// Set minimum gain value limit for PGA volume setting changes under ALC control
#endif /* defined(CODEC1_TYPE) && (CODEC1_TYPE == CODEC_TYPE_NAU8822L) */
#if WITHTX
	uint8_t	ggrptxparams; // последний посещённый пункт группы
	//uint8_t gfitx;		/* номер используемого фильтра на передачу */
	uint8_t gbandf2adj [NUMLPFADJ];	/* коррекция мощности по ФНЧ передачика */
	#if WITHPOWERLPHP
		uint8_t gpwri;		// индекс в pwrmodes - мощность при обычной работе
		uint8_t gpwratunei;	// индекс в pwrmodes - моность при работе автотюнера или по внешнему запросу
	#elif WITHPOWERTRIM
		uint8_t gnormalpower;/* мощность WITHPOWERTRIMMIN..WITHPOWERTRIMMAX */
		uint8_t gclassapower;/* мощность при работе в классе А WITHPOWERTRIMMIN..WITHPOWERTRIMMAX */
		uint8_t gclassamode;	/* использование режима клвсс А при передаче */
		uint8_t gtunepower;/* мощность при работе автоматического согласующего устройства WITHPOWERTRIMMIN..WITHPOWERTRIMMAX */
	#endif /* WITHPOWERLPHP, WITHPOWERTRIM */
#endif /* WITHTX */

#if WITHNOTCHONOFF
	uint8_t	ggrpnotch; // последний посещённый пункт группы
	uint8_t gnotch;
#elif WITHNOTCHFREQ
	uint8_t	ggrpnotch; // последний посещённый пункт группы
	uint8_t gnotch;		// on/off - кнопкой, не через меню
	uint8_t gnotchtype;
	uint16_t gnotchfreq;
	uint16_t gnotchwidth;
#endif /* WITHNOTCHONOFF, WITHNOTCHFREQ */

#if WITHRFSG
	uint8_t userfsg;
#endif /* WITHRFSG */

	uint8_t gdisplayfreqsfps;		/* скорость обновления индикатора частоты */
	uint8_t gdisplaybarsfps;	/* скорость обновления S-метра */
#if WITHSPECTRUMWF
	uint8_t gviewstyle;		/* стиль отображения спектра и панорамы */
	uint8_t gview3dss_mark;	/* Для VIEW_3DSS - индикация полосы пропускания на спектре */
	uint8_t gwflevelsep;	/* чувствительность водопада регулируется отдельной парой параметров */
	uint8_t gtxloopback;		 /* включение спектроанализатора сигнала передачи */
	uint8_t gspecbeta100;	/* beta - парамеры видеофильтра спектра */
	uint8_t gwflbeta100;	/* beta - парамеры видеофильтра водопада */
	uint8_t glvlgridstep;	/* Шаг сетки уровней в децибелах */

#endif /* WITHSPECTRUMWF */

	uint8_t gshowdbm;	/* Отображение уровня сигнала в dBm или S-memter */
#if WITHBCBANDS
	uint8_t gbandsetbcast;	/* Broadcasting radio bands */
#endif /* WITHBCBANDS */
	uint8_t bandset11m;	/* CB radio band */
#if TUNE_6MBAND
	uint8_t bandset6m;	/* используется ли диапазон 6 метров */
#endif /* TUNE_6MBAND */
#if TUNE_4MBAND
	uint8_t bandset4m;	/* используется ли диапазон 6 метров */
#endif /* TUNE_4MBAND */
#if TUNE_2MBAND
	uint8_t bandset2m;	/* используется ли диапазон 2 метра */
#endif /* TUNE_2MBAND */

#if WITHANTSELECT2
	uint8_t gantmanual;		/* 0 - выбор антенны автоматический */
#endif /* WITHANTSELECT2 */
#if WITHSPLIT
	uint8_t splitmode;		/* не-0, если работа с фиксированными ячейками (vfo/vfoa/vfob/mem) */
	uint8_t vfoab;		/* 1, если работа с VFO B, 0 - с VFO A */
#elif WITHSPLITEX
	uint8_t splitmode;		/* не-0, если работа с фиксированными ячейками (vfo/vfoa/vfob/mem) */
	uint8_t vfoab;		/* 1, если работа с VFO B, 0 - с VFO A */
#endif /* WITHSPLIT */

	uint8_t gcwpitch10;	/* тон в CW/CWR режиме */
	uint8_t gkeybeep10;	/* тон озвучки нажатий клавиш */
	uint8_t stayfreq;	/* при изменении режимов кнопками - не меняем частоту */

#if WITHIF4DSP
	uint8_t	ggrpagc; // последний посещённый пункт группы
	uint8_t	ggrpagcssb; // последний посещённый пункт группы
	uint8_t	ggrpagccw; // последний посещённый пункт группы
	uint8_t	ggrpagcdigi; // последний посещённый пункт группы

	uint8_t gnoisereductvl;	// noise reduction level
	uint8_t bwsetpos [BWSETI_count];	/* выбор одной из полос пропускания */

	uint8_t bwpropsleft [BWPROPI_count];	/* значения границ полосы пропускания */
	uint8_t bwpropsright [BWPROPI_count];	/* значения границ полосы пропускания */
	//uint8_t bwpropsfltsofter [BWPROPI_count];	/* Код управления сглаживанием скатов фильтра основной селекции на приёме */
	uint8_t bwpropsafresponce [BWPROPI_count];	/* Наклон АЧХ */

	struct agcseti_tag afsets [AGCSETI_COUNT];	/* режимы приема */

	uint8_t gagcoff;
	uint8_t gamdepth;		/* Глубина модуляции в АМ - 0..100% */
	uint8_t ggainnfmrx10;		/* дополнительное усиление по НЧ в режиме приёма NFM 100..1000% */
	uint8_t gnfmdeviation;	/* Девиация при передаче в NFM - в сотнях герц */
	uint8_t gdacscale;		/* Использование амплитуды сигнала с ЦАП передатчика - 0..100% */
	uint16_t ggaindigitx;		/* Увеличение усиления при передаче в цифровых режимах 100..300% */
	uint16_t ggaincwtx;		/* Увеличение усиления при передаче в CW режимах 50..100% */
	uint16_t gdesignscale;	/* используется при калибровке параметров интерполятора */
	uint8_t	gcwedgetime;			/* Время нарастания/спада огибающей телеграфа при передаче - в 1 мс */
	uint8_t	gsidetonelevel;	/* Уровень сигнала самоконтроля в процентах - 0%..100% */
	uint8_t gmoniflag;		/* разрешение самопрослушивания */
	uint8_t	gsubtonelevel;	/* Уровень сигнала CTCSS в процентах - 0%..100% */
#if WITHWAVPLAYER || WITHSENDWAV
	uint8_t gloopmsg, gloopsec;
#endif /* WITHWAVPLAYER || WITHSENDWAV */
	uint8_t gdigigainmax;	/* диапазон ручной регулировки цифрового усиления - максимальное значение */
	uint8_t gsquelch;		/* уровень открытия шумоподавителя */
	uint8_t gsquelchNFM;	/* sуровень открытия шумоподавителя для NFM */
	uint8_t gvad605;		/* напряжение на AD605 (управление усилением тракта ПЧ */
	uint16_t gfsadcpower10 [2];	/*	Мощность, соответствующая full scale от IF ADC (с тояностью 0.1 дБмВт */
	#if ! WITHPOTAFGAIN
		uint16_t afgain1;	// Параметр для регулировки уровня на выходе аудио-ЦАП
	#endif /* ! WITHPOTAFGAIN */
	#if ! WITHPOTIFGAIN
		uint16_t rfgain1;	// Параметр для регулировки усиления по ПЧ
	#endif /* ! WITHPOTIFGAIN */
	uint16_t glineamp;	// усиление с LINE IN
	uint8_t gmikeboost20db;	// предусилитель микрофона
	uint8_t gmikeagc;	/* Включение программной АРУ перед модулятором */
	uint8_t gmikeagcgain;	/* Максимальное усидение АРУ микрофона */
	uint8_t gmikehclip;		/* Ограничитель */
	#if WITHCOMPRESSOR
		uint8_t	gcompressor_attack;		/* Компрессор - время атаки */
		uint8_t gcompressor_release;	/* Компрессор - время освобождения */
		uint8_t gcompressor_hold;		/* Компрессор - время удержания */
		uint8_t gcompressor_gain;		/* Компрессор - отношение компрессии */
		uint8_t gcompressor_threshold;	/* Компрессор - порог срабатывания */
	#endif /* WITHCOMPRESSOR */
	#if WITHREVERB
		uint8_t greverb;		/* ревербератор */
		uint8_t greverbdelay;		/* ревербератор - задержка */
		uint8_t greverbloss;		/* ревербератор - ослабление на возврате */
	#endif /* WITHREVERB */
	#if WITHUSBUAC
		uint8_t gdatavox;	/* автоматический переход на передачу при появлении звука со стороны компьютера */
		uint8_t gdatamode;	/* передача звука с USB вместо обычного источника */
		uint8_t guacplayer;	/* режим прослушивания выхода компьютера в наушниках трансивера - отладочный режим */
		#if WITHRTS96 || WITHRTS192 || WITHTRANSPARENTIQ
			uint8_t gswapiq;		/* Поменять местами I и Q сэмплы в потоке RTS96 */
		#endif /* WITHRTS96 || WITHRTS192 || WITHTRANSPARENTIQ */
	#endif /* WITHUSBUAC */
	#if WITHAFCODEC1HAVEPROC
		uint8_t gmikeequalizer;	// включение обработки сигнала с микрофона (эффекты, эквалайзер, ...)
		uint8_t gmikeequalizerparams [HARDWARE_CODEC1_NPROCPARAMS];	// Эквалайзер 80Hz 230Hz 650Hz 	1.8kHz 5.3kHz
	#endif /* WITHAFCODEC1HAVEPROC */
	#if WITHAFEQUALIZER
		uint8_t	ggrpafeq;
		uint8_t geqtx;	// эквалайзер в режиме передачи
		uint8_t geqrx;	// эквалайзер в режиме приема
		uint8_t geqtxparams [AF_EQUALIZER_BANDS];
		uint8_t geqrxparams [AF_EQUALIZER_BANDS];
	#endif /* #if WITHAFEQUALIZER */
	struct micproc gmicprocs [NMICPROFILES];
	uint8_t txaprofile [TXAPROFIG_count];	/* параметры обработки звука перед модулятором */
#endif /* WITHIF4DSP */


#if WITHDSPEXTDDC	/* "Воронёнок" с DSP и FPGA */
	uint8_t	ggrprfadc; // последний посещённый пункт группы
	uint8_t gdither;	/* управление зашумлением в LTC2208 */
	uint8_t gadcrand;	/* управление интерфейсом в LTC2208 */
	uint8_t gadcfifo;
	uint16_t gadcoffset;
	uint8_t gdactest;
#endif /* WITHDSPEXTDDC */

#if WITHMODEM
	uint8_t	ggrpmodem; // последний посещённый пункт группы
	uint8_t gmodemspeed;	// индекс в таблице скоростей передачи
	uint8_t gmodemmode;		// применяемая модуляция
#endif /* WITHMODEM */

#if WITHLO1LEVELADJ
	uint8_t lo1level;	/* уровень (амплитуда) LO1 в процентах */
#endif /* WITHLO1LEVELADJ */

#if defined (IF3_MODEL) && (IF3_MODEL == IF3_TYPE_DCRX)

#elif WITHDUALFLTR		/* Переворот боковых за счёт переключения фильтра верхней или нижней боковой полосы */
	uint16_t lo4offset;			/* частота (без базы) третьего гетеродина */
#elif WITHFIXEDBFO
	uint8_t glo4lsb;			/* признак LSB на последней ПЧ */
	uint16_t lo4offset;			/* частота (без базы) третьего гетеродина */
#elif WITHDUALBFO
	uint16_t lo4offsets [2];		/* частота (без базы) третьего гетеродина в USB и LSB */
#endif

#if WITHIFSHIFT
	uint16_t ifshifoffset;	/* смещение частоты пропускания в режиме If SHIFT */
#endif /* WITHIFSHIFT */

	uint8_t	ggrpfilters; // последний посещённый пункт группы

#if CTLSTYLE_RA4YBO_V1 || (defined (IF3_MODEL) && (IF3_MODEL != IF3_TYPE_DCRX) && (IF3_MODEL != IF3_TYPE_BYPASS))

	uint8_t dcrxmode;	/* settings menu option - RX acts as direct conversion */
	uint8_t dctxmodessb, dctxmodecw;	/* settings menu option - TX acts as direct conversion */


#if WITHPBT //&& (LO3_SIDE != LOCODE_INVALID)
	uint8_t	ggrppbts; // последний посещённый пункт группы
	uint16_t pbtoffset;	/* смещение частоты пропускания в режиме PBT */
#endif /* WITHPBT && (LO3_SIDE != LOCODE_INVALID) */

	/* параметры подстройки центральной частоты узких фильтров */
#if (IF3_FMASK & IF3_FMASK_0P3)
	uint8_t hascw0p3;			/* наличие в плате фильтра на 0.3 кГц */
	uint16_t carr0p3;	/* settings menu option */
#endif
#if (IF3_FMASK & IF3_FMASK_0P5)
	uint8_t hascw0p5;			/* наличие в плате фильтра на 0.5 кГц */
	uint16_t carr0p5;	/* settings menu option */
#endif

	/* параметры подстройки частот скатов широких фильтров */
#if (IF3_FMASK & IF3_FMASK_1P8)
	uint8_t hascw1p8;			/* наличие в плате фильтра на 1.8 кГц */
	uint16_t usbe1p8;	/* settings menu option */
	uint16_t lsbe1p8;	/* settings menu option */
#endif
#if (IF3_FMASK & IF3_FMASK_2P4)
	uint8_t hascw2p4;			/* наличие в плате фильтра на 2.4 кГц */
	uint16_t usbe2p4;	/* settings menu option */
	uint16_t lsbe2p4;	/* settings menu option */
	#if WITHSAMEBFO == 0 && (IF3_FMASKTX & IF3_FMASK_2P4) != 0
		uint16_t usbe2p4tx;	/* settings menu option */
		uint16_t lsbe2p4tx;	/* settings menu option */
	#endif
#endif
#if (IF3_FMASK & IF3_FMASK_2P7)
	uint8_t hascw2p7;			/* наличие в плате фильтра на 2.7 кГц */
	uint16_t usbe2p7;	/* settings menu option */
	uint16_t lsbe2p7;	/* settings menu option */
	#if WITHSAMEBFO == 0 && (IF3_FMASKTX & IF3_FMASK_2P7) != 0
		uint16_t usbe2p7tx;	/* settings menu option */
		uint16_t lsbe2p7tx;	/* settings menu option */
	#endif
#endif
#if (IF3_FMASK & IF3_FMASK_3P1)
	uint16_t usbe3p1;	/* settings menu option */
	uint16_t lsbe3p1;	/* settings menu option */
	#if WITHSAMEBFO == 0 && (IF3_FMASKTX & IF3_FMASK_3P1) != 0
		uint16_t usbe3p1tx;	/* settings menu option */
		uint16_t lsbe3p1tx;	/* settings menu option */
	#endif
#endif

	/* параметры подстройки центральной частоты широких фильтров */
#if (IF3_FMASK & IF3_FMASK_6P0)
	uint8_t hascw6p0;			/* наличие в плате фильтра на 6.0 кГц */
	uint16_t cfreq6k;
#endif
#if (IF3_FMASK & IF3_FMASK_7P8)
	uint16_t cfreq7p8k;
#endif
#if (IF3_FMASK & IF3_FMASK_8P0)
	uint16_t cfreq8k;
#endif
#if (IF3_FMASK & IF3_FMASK_9P0)
	uint16_t cfreq9k;
#endif
#if (IF3_FMASK & IF3_FMASK_15P0)
	uint16_t cfreq15k;
	uint16_t cfreq15k_nfm;
#endif
#if (IF3_FMASK & IF3_FMASK_17P0)
	uint16_t cfreq17k;
#endif
#if WITHTX && WITHSAMEBFO == 0 && (IF3_FMASKTX & IF3_FMASK_2P4)
	uint8_t hascw2p4_tx;
#endif /* WITHTX && WITHSAMEBFO == 0 && (IF3_FMASKTX & IF3_FMASK_2P4) */
#if WITHTX && WITHSAMEBFO == 0 && (IF3_FMASKTX & IF3_FMASK_2P7)
	uint8_t hascw2p7_tx;
#endif /* WITHTX && WITHSAMEBFO == 0 && (IF3_FMASKTX & IF3_FMASK_2P7) */
#if WITHTX && WITHSAMEBFO == 0 && (IF3_FMASKTX & IF3_FMASK_3P1)
	uint8_t hascw3p1_tx;
#endif /* WITHTX && WITHSAMEBFO == 0 && (IF3_FMASKTX & IF3_FMASK_2P7) */

#elif CTLSTYLE_RA4YBO_V3
/*
filter_t fi_0p5 =
filter_t fi_3p1 =
filter_t fi_3p0_455 =
filter_t fi_10p0_455 =
filter_t fi_6p0_455 =
filter_t fi_2p0_455 =
*/
	uint16_t cfreq10k;
	uint16_t cfreq2k;
	uint16_t cfreq6k;
	uint16_t usbe3p0;	/* settings menu option */
	uint16_t lsbe3p0;	/* settings menu option */
	uint16_t usbe3p1;	/* settings menu option */
	uint16_t lsbe3p1;	/* settings menu option */
	uint16_t carr0p5;

#endif

#if defined(REFERENCE_FREQ)
#if defined (DAC1_TYPE)
	uint8_t dac1level;	/* напряжение на управлении опорным генератором */
#endif /* defined (DAC1_TYPE) */
	uint16_t refbias;	/* подстройка опорника */

#endif /* defined(REFERENCE_FREQ) */
#if defined(PLL1_TYPE) && (PLL1_TYPE == PLL_TYPE_SI570)
	uint16_t si570_xtall_offset;
#endif

#if WITHCAT
	uint8_t	ggrpcat; // последний посещённый пункт группы
	uint8_t catenable;	/* удаленное управление разрешено */
	uint8_t catbaudrate;	/* номер скорости работы по CAT */
	#if WITHTX
		uint8_t catsigptt;	/* Выбраный сигнал для перехода на передачу по CAT */
	#endif /* WITHTX */
	uint8_t catsigkey;	/* Выбраный сигнал для манипуляции по CAT */
#endif /* WITHCAT */

#if WITHAUTOTUNER
	uint8_t	ggrptuner; // последний посещённый пункт группы
	uint8_t tunerdelay;
#endif /* WITHAUTOTUNER */


#if WITHTX
	#if WITHMUTEALL
		uint8_t gmuteall;	/* Отключить микрофон всегда. */
	#endif /* WITHMUTEALL */
	#if WITHVOX
		uint8_t	ggrpvox; // последний посещённый пункт группы
		uint8_t gvoxenable;	/* автоматическое управление передатчиком (от голоса) */
		uint8_t gvoxlevel;	/* уровень срабатывания VOX */
		uint8_t gavoxlevel;	/* уровень anti-VOX */
		uint8_t voxdelay;	/* задержка отпускания VOX */
	#endif /* WITHVOX */

	#if WITHELKEY
		uint8_t bkinenable;	/* автоматическое управление передатчиком (от телеграфного манипулятора) */
	#endif /* WITHELKEY */

	#if WITHPABIASTRIM
		uint8_t gpabias;	/* регулировка тока покоя оконечного каскада передатчика */
	#endif /* WITHPABIASTRIM */
	uint8_t gtxgate;	/* разрешение предусилителя */
	uint8_t bkindelay;	/* задержка отпускания BREAK-IN */
	uint8_t grgbeep;	/* разрешение (не-0) или запрещение (0) формирования roger beep */
	uint8_t rxtxdelay;	/* приём-передача */

	uint8_t txrxdelay;	/* передача-приём */


	uint8_t swrcalibr;	/* калибровочный параметр SWR-метра */
	uint8_t maxpwrcali;	/* калибровочный параметр PWR-метра */
	uint16_t minforward; // = 10;
	#if WITHSWRMTR && ! WITHSHOWSWRPWR
		uint8_t swrmode;	/* 1 - показ SWR шкалы, 0 - мощность */
	#endif

#if WITHSUBTONES
	uint8_t gsubtonei;	// номер subtone
	uint8_t gctssenable;	// разрешить формирование subtone
#endif /* WITHSUBTONES */
#if WITHTHERMOLEVEL
	uint8_t gtempvmax;
	uint8_t gheatprot;	/* защита от перегрева */
#endif /* WITHTHERMOLEVEL */
#if (WITHSWRMTR || WITHSHOWSWRPWR)
	uint8_t gswrprot;	/* защита от превышения КСВ */
#endif /* (WITHSWRMTR || WITHSHOWSWRPWR) */
#endif /* WITHTX */

#if WITHVOLTLEVEL && ! WITHREFSENSOR
	uint8_t voltcalibr100mV;	/* калибровочный параметр измерителя напряжения АКБ - Напряжение fullscale = VREF * 5.3 = 3.3 * 5.3 = 17.5 вольта */
#endif /* WITHVOLTLEVEL && ! WITHREFSENSOR */
	uint16_t gipacali;
#if WITHELKEY
	uint8_t	ggrpelkey; // последний посещённый пункт группы
	uint8_t elkeywpm;	/* скорость электронного ключа */
	uint8_t elkeymode;	/* режим электронного ключа - 0 - asf, 1 - paddle, 2 - keyer */
	uint8_t dashratio;	/* отношение длителности тире к точке в десятках процентов */
	uint8_t spaceratio;	/* отношение длителности паузы к точке в десятках процентов */
	uint8_t elkeyreverse;	
#if WITHVIBROPLEX
	uint8_t elkeyslope;	/* скорость уменьшения длительности точки и паузы - имитация виброплекса */
	uint8_t elkeyslopeenable;	/* скорость уменьшения длительности точки и паузы - имитация виброплекса */
#endif /* WITHVIBROPLEX */
#endif /* WITHELKEY */


	uint8_t gbigstep;		/* больщой шаг валкодера */
	uint8_t genc1div;		/* во сколько раз уменьшаем разрешение валкодера. */
	uint8_t genc2div;
#if WITHSPKMUTE
	uint8_t gmutespkr;		/* выключение динамика */
#endif /* WITHSPKMUTE */

#if CTLSTYLE_RA4YBO
	uint8_t gaffilter;		/* включение ФНЧ на приёме в аппарате RA4YBO */
#elif CTLSTYLE_RA4YBO_V1 || CTLSTYLE_RA4YBO_V2 || KEYBSTYLE_RA4YBO_AM0
	uint8_t gaffilter;		/* включение ФНЧ на приёме в аппарате RA4YBO */
	uint8_t guser1;
	uint8_t guser2;
	uint8_t guser3;
	uint8_t guser4;
	uint8_t guser5;
#elif CTLSTYLE_RA4YBO_V3
	uint8_t gaffilter;		/* включение ФНЧ на приёме в аппарате RA4YBO */
	uint8_t guser1;
	uint8_t guser2;
	uint8_t guser3;
	uint8_t guser4;
	uint8_t guser5;
#endif /* CTLSTYLE_RA4YBO_V1 || CTLSTYLE_RA4YBO_V2 || CTLSTYLE_RA4YBO_V3 */

#if LO1FDIV_ADJ
	uint8_t lo1powrx;		/* на сколько раз по 2 делим выходную частоту синтезатора первого гетеродина */
	uint8_t lo1powtx;		/* на сколько раз по 2 делим выходную частоту синтезатора первого гетеродина */
#endif /* LO1FDIV_ADJ */
#if LO4FDIV_ADJ
	uint8_t lo4powrx;		/* на сколько раз по 2 делим выходную частоту синтезатора четивертого гетеродина */
	uint8_t lo4powtx;		/* на сколько раз по 2 делим выходную частоту синтезатора четивертого гетеродина */
#endif /* LO1FDIV_ADJ */

	uint8_t gattpresh;	/* корректировка показаний с-метра по включенному аттенюатору и предусилителю */

#if WITHBARS
	uint8_t s9level;			/* уровни калибровки S-метра */
	uint8_t s9delta;	
	uint8_t s9_60_delta;
#endif /* WITHBARS */
	
#if (WITHSWRMTR || WITHSHOWSWRPWR)
	uint8_t gsmetertype;		/* выбор внешнего вида прибора - стрелочный или градусник */
#endif /* (WITHSWRMTR || WITHSHOWSWRPWR) */

#if LO1PHASES
	uint16_t phaserx, phasetx;
#endif /* LO1PHASES */

#if WITHLFM
	uint8_t	ggrplfm; // последний посещённый пункт группы
	uint16_t lfmtoffset;
	uint16_t lfmtinterval;
	uint8_t lfmmode;
	uint16_t lfmstart100k;
	uint16_t lfmstop100k;
	uint16_t lfmspeed1k;
#endif /* WITHLFM */

#if WITHUSEAUDIOREC
	uint8_t recmode;	/* автоматически начинаем запись на SD CARD при включении */
#endif /* WITHUSEAUDIOREC */

#if (LO3_SIDE != LOCODE_INVALID) && LO3_FREQADJ	/* подстройка частоты гетеродина через меню. */
	uint16_t lo3offset;
#endif	/* (LO3_SIDE != LOCODE_INVALID) && LO3_FREQADJ */

	struct modeprops modes [MODE_COUNT];

	struct bandinfo bands [HBANDS_COUNT + XBANDS_COUNT + VFOS_COUNT + MBANDS_COUNT];
#if	WITHDIRECTBANDS
	struct bandgroup_tag bandgroups [BANDGROUP_COUNT];
#endif	/* WITHDIRECTBANDS */

#if WITHANTSELECT2
	uint8_t hffreqswitch; /* выше этой частоты (МГц) выбирается вторая (ВЧ) антенна */
#endif /* WITHANTSELECT2 */

	uint8_t signature [sizeof nvramsign - 1];	/* сигнатура соответствия версии программы и содержимого NVRAM */
} ATTRPACKED;	// аттрибут GCC, исключает "дыры" в структуре. Так как в ОЗУ нет копии этой структуры, see also NVRAM_TYPE_BKPSRAM

/* константы, определяющие расположение параметров в FRAM */

#define RMT_MENUSET_BASE offsetof(struct nvmap, menuset)		/* набор функций кнопок и режим отображения на дисплее */
#define RMT_GROUP_BASE offsetof(struct nvmap, ggroup)		/* байт - последняя группа меню, с которой работали */
#define RMT_SIGNATURE_BASE(i) offsetof(struct nvmap, signature [(i)])			/* расположение сигнатуры */
#define RMT_LOCKMODE_BASE offsetof(struct nvmap, lockmode)		/* признак блокировки валкодера */
#define RMT_USEFAST_BASE offsetof(struct nvmap, gusefast)		/* переключение в режим крупного шага */
#define RMT_AFFILTER_BASE offsetof(struct nvmap, gaffilter)		/* включение ФНЧ на приёме в аппарате RA4YBO */
#define RMT_MUTELOUDSP_BASE offsetof(struct nvmap, gmutespkr)		/* включение ФНЧ на приёме в аппарате RA4YBO */

#define RMT_SPLITMODE_BASE offsetof(struct nvmap, splitmode)		/* (vfo/vfoa/vfob/mem) */
#define RMT_VFOAB_BASE offsetof(struct nvmap, vfoab)		/* (vfoa/vfob) */
#define RMT_MBAND_BASE offsetof(struct nvmap, gmband)		/* ячейка памяти фиксированных частот */
#define RMT_ANTMANUAL_BASE offsetof(struct nvmap, gantmanual)		/* 0 - выбор антенны автоматический */

#define RMT_MAINSUBRXMODE_BASE	offsetof(struct nvmap, mainsubrxmode)
#define RMT_DATAMODE_BASE	offsetof(struct nvmap, gdatamode)


#define RMT_NR_BASE(i)	offsetof(struct nvmap, modes [(i)].noisereduct)
#define RMT_AGC_BASE(i)	offsetof(struct nvmap, modes [(i)].agc)
#define RMT_FILTER_BASE(i)	offsetof(struct nvmap, modes [(i)].filter)
#define RMT_STEP_BASE(i)	offsetof(struct nvmap, modes [(i)].step)

#define RMT_TXPOWER_BASE(i)	offsetof(struct nvmap, modes [(i)].txpower)
#define RMT_TXCOMPR_BASE(i)	offsetof(struct nvmap, modes [(i)].txcompr)
#define RMT_TXAUDIO_BASE(i) offsetof(struct nvmap, modes [(i)].txaudio)
#define RMT_TXAPROFIGLE_BASE(i) offsetof(struct nvmap, txaprofile[(i)])

#define RMT_BANDGROUP(i) offsetof(struct nvmap, bandgroups [(i)].band)	/* последний диапазон в группе, куда был переход по кнопке диапазона (индекс в bands). */
#define RMT_BFREQ_BASE(i) offsetof(struct nvmap, bands [(i)].freq)			/* последняя частота, на которую настроились (4 байта) */
#define RMT_PAMP_BASE(i) offsetof(struct nvmap, bands [(i)].pamp)			/* признак включения аттенюатора (1 байт) */
#define RMT_ATT_BASE(i) offsetof(struct nvmap, bands [(i)].att)			/* признак включения аттенюатора (1 байт) */
#define RMT_RXANTENNA_BASE(i) offsetof(struct nvmap, bands [(i)].rxant)			/* код включённой антенны (1 байт) */
#define RMT_ANTENNA_BASE(i) offsetof(struct nvmap, bands [(i)].ant)			/* код включённой антенны (1 байт) */
#define RMT_MODEROW_BASE(i)	offsetof(struct nvmap, bands [(i)].moderow)			/* номер строки в массиве режимов. */
#define RMT_MODECOLS_BASE(i, j)	offsetof(struct nvmap, bands [(i)].modecols [(j)])	/* выбранный столбец в каждой строке режимов. */
#define RMT_PWR_BASE offsetof(struct nvmap, gpwri)								/* большая мощность sw2012sf */
#define RMT_NOTCH_BASE offsetof(struct nvmap, gnotch)							/* NOTCH on/off */
#define RMT_NOTCHTYPE_BASE offsetof(struct nvmap, gnotchtype)					/* NOTCH filter type */
//#define RMT_NOTCHFREQ_BASE offsetof(struct nvmap, gnotchfreq)					/* Manual NOTCH filter frequency */

#define RMT_USER1_BASE offsetof(struct nvmap, guser1)
#define RMT_USER2_BASE offsetof(struct nvmap, guser2)
#define RMT_USER3_BASE offsetof(struct nvmap, guser3)
#define RMT_USER4_BASE offsetof(struct nvmap, guser4)
#define RMT_USER5_BASE offsetof(struct nvmap, guser5)

#define RMT_BWSETPOS_BASE(i) offsetof(struct nvmap, bwsetpos [(i)])

#define RMT_BWPROPSLEFT_BASE(i) offsetof(struct nvmap, bwpropsleft [(i)])
#define RMT_BWPROPSRIGHT_BASE(i) offsetof(struct nvmap, bwpropsright [(i)])
//#define RMT_BWPROPSFLTSOFTER_BASE(i) offsetof(struct nvmap, bwpropsfltsofter [(i)])
#define RMT_BWPROPSAFRESPONCE_BASE(i) offsetof(struct nvmap, bwpropsafresponce [(i)])

#define RMT_MICLEVEL_BASE(i) offsetof(struct nvmap, micprof_cells [(i)].level)
#define RMT_MICCLIP_BASE(i) offsetof(struct nvmap, micprof_cells [(i)].clip)
#define RMT_MICAGC_BASE(i) offsetof(struct nvmap, micprof_cells [(i)].agc)
#define RMT_MICAGCGAIN_BASE(i) offsetof(struct nvmap, micprof_cells [(i)].agcgain)
#define RMT_MICBOOST_BASE(i) offsetof(struct nvmap, micprof_cells [(i)].mikeboost20db)
#define RMT_MICEQ_BASE(i) offsetof(struct nvmap, micprof_cells [(i)].eq_enable)
#define RMT_MICEQPARAMS_BASE(i, j) offsetof(struct nvmap, micprof_cells [(i)].eq_params[(j)])
#define RMT_MICPSAVE_BASE(i) offsetof(struct nvmap, micprof_cells [(i)].cell_saved)



/* переменные, вынесенные из главной функции - определяют текущий тежим рботы
   и частоту настройки
   */

/* параметры диапазона, переключаемые при смене VFO */
static uint_fast32_t gfreqs [VFOS_COUNT];		/* отображаемая на дисплее частота работы */
#if ! WITHONEATTONEAMP
static uint_fast8_t gpamps [VFOS_COUNT];
#endif /* ! WITHONEATTONEAMP */
static uint_fast8_t gatts [VFOS_COUNT];
#if WITHANTSELECTRX
static uint_fast8_t grxantennas [VFOS_COUNT];
static uint_fast8_t gantennas [VFOS_COUNT];
#elif WITHANTSELECT2
static uint_fast8_t gantennas [VFOS_COUNT];
static uint_fast8_t hffreqswitch = 14; /* выше этой частоты (МГц) выбирается вторая (ВЧ) антенна */
static uint_fast8_t gantmanual;		/* 0 - выбор антенны автоматический */
#elif WITHANTSELECT
static uint_fast8_t gantennas [VFOS_COUNT];
#endif /* WITHANTSELECT || WITHANTSELECTRX */
static uint_fast8_t gvfosplit [VFOS_COUNT];	// At index 0: RX VFO A or B, at index 1: TX VFO A or B
// Параметры, выставляемые в update board
// кэш установленных параметров.
// На эти параметры ориентируемся при работе кнопками управления, переклбчения фильттров и так далее.
static uint_fast8_t gsubmode;		/* код текущего режима */
static uint_fast8_t gmode;		/* текущий код группы режимов */
static uint_fast8_t gfi;			/* номер фильтра (сквозной) для текущего режима */
static uint_fast16_t gstep;
static uint_fast16_t gencderate = 1;
static uint_fast8_t gagcmode;
#if WITHIF4DSP
	static uint_fast8_t gnoisereducts [MODE_COUNT];	// noise reduction
	static uint_fast8_t gnoisereductvl = 25;	// noise reduction
#endif /* WITHIF4DSP */

#if (WITHSWRMTR || WITHSHOWSWRPWR)
#if WITHAFSPECTRE
	static uint_fast8_t gsmetertype = SMETER_TYPE_BARS;
#else
	static uint_fast8_t gsmetertype = SMETER_TYPE_DIAL;	/* выбор внешнего вида прибора - стрелочный или градусник */
#endif /* WITHAFSPECTRE */
#endif /* (WITHSWRMTR || WITHSHOWSWRPWR) */

#if WITHIFSHIFT
	enum { IFSHIFTTMIN = 0, IFSHIFTHALF = 3000, IFSHIFTMAX = 2 * IFSHIFTHALF };
	#if WITHIFSHIFTOFFSET
		/* есть начальный сдвиг полосы пропускания */
		static dualctl16_t ifshifoffset =
		{
				IFSHIFTHALF + WITHIFSHIFTOFFSET,		/* if shift offset value */
				IFSHIFTHALF + WITHIFSHIFTOFFSET,		/* if shift offset value */
		};
	#else
		/* Без начального сдвига полосы пропускания */
		static dualctl16_t ifshifoffset =
		{
				IFSHIFTHALF,		/* if shift offset value */
				IFSHIFTHALF,		/* if shift offset value */
		};
	#endif
	static int_fast32_t getifshiftbase(void)
	{
		return 0 - IFSHIFTHALF;
	}
#endif /* WITHIFSHIFT */

#if WITHPBT // && (LO3_SIDE != LOCODE_INVALID)
	enum { PBTMIN = 0, PBTHALF = 2560, PBTMAX = 5100 };	// Значения для управления потенциометром
	static dualctl16_t gpbtoffset =
	{
			PBTHALF;		/* pbt offset value */
			PBTHALF;		/* pbt offset value */
	};
	static int_fast32_t getpbtbase(void)
	{
		return 0L - PBTHALF;
	}
	// Для отображения на дисплее
	int_fast32_t hamradio_get_pbtvalue(void)
	{
		return gpbtoffset.value + getpbtbase();
	}
#endif /* WITHPBT */

#if (LO3_SIDE != LOCODE_INVALID) && LO3_FREQADJ	/* подстройка частоты гетеродина через меню. */
	enum { LO2AMIN = 0, LO2AHALF = 15000, LO2AMAX = 2 * LO2AHALF };
	static uint_fast16_t lo3offset = LO2AHALF;
	static int_fast32_t lo3base = (int_fast32_t) ((int_fast64_t) REFERENCE_FREQ * LO3_PLL_N / LO3_PLL_R) - LO2AHALF;
	static int_fast32_t getlo3base(void)
	{
		return lo3base;
	}
#endif	/* (LO3_SIDE != LOCODE_INVALID) && LO3_FREQADJ */

// 
//static uint_fast8_t extmenu;

#if WITHUSEDUALWATCH
	uint_fast8_t mainsubrxmode;		// Левый/правый, A - main RX, B - sub RX
#endif /* WITHUSEDUALWATCH */

#if WITHENCODER
	static uint_fast8_t genc1pulses = ENCRES_DEFAULT;		/* 5: 128 индекс в таблице разрешений валкодера */
	#if defined (ENCDIV_DEFAULT)
		static uint_fast8_t genc1div = ENCDIV_DEFAULT;	/* во сколько раз уменьшаем разрешение валкодера. */
	#else /* defined (ENCDIV_DEFAULT) */
		static uint_fast8_t genc1div = 1;	/* во сколько раз уменьшаем разрешение валкодера. */
	#endif /* defined (ENCDIV_DEFAULT) */
	static uint_fast8_t genc1dynamic = 1;
	static uint_fast8_t gbigstep = (ENCRES_24 >= ENCRES_DEFAULT);	/* модифицируется через меню. */

	#if defined (BOARD_ENCODER2_DIVIDE)
		static uint_fast8_t genc2div = BOARD_ENCODER2_DIVIDE;
	#else /* defined (BOARD_ENCODER2_DIVIDE) */
		static uint_fast8_t genc2div = 2;	/* значение для валкодера PEC16-4220F-n0024 (с трещёткой") */
	#endif /* defined (BOARD_ENCODER2_DIVIDE) */


#else
	static const uint_fast8_t gbigstep;
	static const uint_fast8_t genc1div = 1;
	static const uint_fast8_t genc2div = 1;
#endif

static uint_fast8_t lockmode;
#if WITHLCDBACKLIGHTOFF
	// Имеется управление включением/выключением подсветки дисплея
	static uint_fast8_t dimmmode;
#else /* WITHLCDBACKLIGHTOFF */
	enum { dimmmode = 0 };
#endif /* WITHLCDBACKLIGHTOFF */
static uint_fast8_t gusefast;


#if WITHNOTCHONOFF
	static uint_fast8_t gnotchtype;
	static uint_fast8_t gnotch;	// on/off
#elif WITHNOTCHFREQ
	static uint_fast8_t gnotchtype;
	static uint_fast8_t gnotch;	// on/off
	static dualctl16_t gnotchfreq = { 1000, 1000 };
	static dualctl16_t gnotchwidth = { 500, 500 };
#endif /* WITHNOTCHFREQ */

#if WITHSPLIT
	static uint_fast8_t gvfoab;	/* (vfoa/vfob) */
	static uint_fast8_t gsplitmode = VFOMODES_VFOINIT;	/* (vfo/vfoa/vfob/mem) */
#elif WITHSPLITEX
	static uint_fast8_t gvfoab;	/* 1: vfoa/vfob swapped */
	static uint_fast8_t gsplitmode = VFOMODES_VFOINIT;	/* (vfo/vfoa/vfob/mem) */
#else /* WITHSPLIT */
	static const uint_fast8_t gvfoab;	/* (vfoa/vfob) */
	static const uint_fast8_t gsplitmode = VFOMODES_VFOINIT;	/* (vfo/vfoa/vfob/mem) */
#endif /* WITHSPLIT */

#if WITHWARCBANDS
	enum { bandsethamwarc = 1 };	/* WARC HAM radio bands */
#else /* WITHWARCBANDS */
	enum { bandsethamwarc = 0 };	/* WARC HAM radio bands */
#endif /* WITHWARCBANDS */

//static uint_fast8_t bandsetham = 1;	/* HAM radio bands */
static uint_fast8_t gbandsetbcast = 0;	/* Broadcast radio bands */
static uint_fast8_t bandset11m;
#if TUNE_6MBAND
static uint_fast8_t bandset6m = 1;	/* используется ли диапазон 6 метров */
#endif /* TUNE_6MBAND */
#if TUNE_4MBAND
static uint_fast8_t bandset4m = 0;	/* используется ли диапазон 4 метров */
#endif /* TUNE_4MBAND */
#if TUNE_2MBAND
static uint_fast8_t bandset2m = 1;	/* используется ли диапазон 2 метра */
#endif /* TUNE_2MBAND */

#if WITHCAT
	static uint_fast8_t catprocenable;	/* разрешена работа подсистемы CAT */
	static uint_fast8_t catstatetx;		/* запрос перехода трансивера на передачу от CAT команды */
	static uint_fast8_t catstatetxdata;		/* запрос перехода трансивера на передачу от CAT команды TX1 */
	static uint_fast8_t cattunemode;		/* запрос перехода трансивера на передачу от CAT команды */
#else
	enum { catstatetx = 0, catstatetxdata = 0, cattunemode = 0 };
#endif /* WITHCAT */

static uint_fast8_t alignmode;		/* режимы для настройки аппаратной части (0-нормальная работа) */

#if WITHUSEAUDIOREC
	#if defined (WITHBBOX) && defined (WITHBBOXREC)
		static uint_fast8_t recmode = WITHBBOXREC;	/* автоматически начинаем запись на SD CARD при включении */
	#else /* defined (WITHBBOX) && defined (WITHBBOXREC) */
		static uint_fast8_t recmode;	/* автоматически начинаем запись на SD CARD при включении */
	#endif /* defined (WITHBBOX) && defined (WITHBBOXREC) */
#endif /* WITHUSEAUDIOREC */

#if WITHLO1LEVELADJ
	static uint_fast8_t lo1level = WITHLO1LEVELADJINITIAL; //100;	/* уровень (амплитуда) LO1 в процентах */
#endif /* WITHLO1LEVELADJ */

#if defined (DEFAULT_LCD_CONTRAST)
	static uint_fast8_t gcontrast = DEFAULT_LCD_CONTRAST;
#else
	enum { gcontrast = 0 };
#endif

static const uint_fast8_t displaymodesfps = DISPLAYMODES_FPS;
#if defined (WITHDISPLAY_FPS)
	static uint_fast8_t gdisplayfreqsfps = WITHDISPLAY_FPS;
#else /* defined (WITHDISPLAY_FPS) */
	static uint_fast8_t gdisplayfreqsfps = DISPLAY_FPS;
#endif /* defined (WITHDISPLAY_FPS) */
#if defined (WITHDISPLAYSWR_FPS)
	static uint_fast8_t gdisplaybarsfps = WITHDISPLAYSWR_FPS;
#else /* defined (WITHDISPLAYSWR_FPS) */
	static uint_fast8_t gdisplaybarsfps = DISPLAYSWR_FPS;
#endif /* defined (WITHDISPLAYSWR_FPS) */
#if WITHSPECTRUMWF
#if defined (WITHDEFAULTVIEW)		/* стиль отображения спектра и панорамы */
	static uint_fast8_t gviewstyle = WITHDEFAULTVIEW;
#else /* defined (WITHDEFAULTVIEW) */
	static uint_fast8_t gviewstyle = VIEW_COLOR;
#endif /* defined (WITHDEFAULTVIEW) */
#if defined (WITHVIEW_3DSS_MARK)	/* Для VIEW_3DSS - индикация полосы пропускания на спектре */
	static uint_fast8_t gview3dss_mark = WITHVIEW_3DSS_MARK;
#else /* defined (WITHVIEW_3DSS_MARK) */
	static uint_fast8_t gview3dss_mark = 0;
#endif /* defined (WITHVIEW_3DSS_MARK) */
	static uint_fast8_t gtopdbspe [VFOS_COUNT] = { WITHTOPDBDEFAULT, WITHTOPDBDEFAULT, };	/* верхний предел FFT */
	static uint_fast8_t gbottomdbspe [VFOS_COUNT] = { WITHBOTTOMDBDEFAULT, WITHBOTTOMDBDEFAULT, };	/* нижний предел FFT */
	static uint_fast8_t gtopdbwfl [VFOS_COUNT] = { WITHTOPDBDEFAULT, WITHTOPDBDEFAULT, };	/* верхний предел FFT waterflow*/
	static uint_fast8_t gbottomdbwfl [VFOS_COUNT] = { WITHBOTTOMDBDEFAULT, WITHBOTTOMDBDEFAULT, };	/* нижний предел FFT waterflow */
	static uint_fast8_t gwflevelsep;	/* чувствительность водопада регулируется отдельной парой параметров */
	static uint_fast8_t gzoomxpow2 [VFOS_COUNT];		/* степень двойки - состояние растягиваия спектра (уменьшение наблюдаемой полосы частот) */
	static uint_fast8_t gtxloopback = 1;	/* включение спектроанализатора сигнала передачи */
	static int_fast16_t gafspeclow = 100;	// нижняя частота отображения спектроанализатора
	static int_fast16_t gafspechigh = 4000;	// верхняя частота отображения спектроанализатора
	static uint_fast8_t glvlgridstep = 12;	/* Шаг сетки уровней в децибелах */
#if defined (WITHSPECBETA_DEFAULT)
	static uint_fast8_t gspecbeta100 = WITHSPECBETA_DEFAULT;
#else /* defined (WITHSPECBETA_DEFAULT) */
	static uint_fast8_t gspecbeta100 = 50;	/* beta = 0.1 .. 1.0 */
#endif /* defined (WITHSPECBETA_DEFAULT) */
#if defined (WITHWFLBETA_DEFAULT)
	static uint_fast8_t gwflbeta100 = WITHWFLBETA_DEFAULT;
#else /* defined (WITHWFLBETA_DEFAULT) */
	static uint_fast8_t gwflbeta100 = 50;	/* beta = 0.1 .. 1.0 */
#endif /* defined (WITHWFLBETA_DEFAULT) */
#endif /* WITHSPECTRUMWF */
#if WITHLCDBACKLIGHT
	#if WITHISBOOTLOADER 
		static uint_fast8_t gbglight = WITHLCDBACKLIGHTMIN;
	#else /* WITHISBOOTLOADER */
		static uint_fast8_t gbglight = WITHLCDBACKLIGHTMAX;
	#endif /* WITHISBOOTLOADER */
#else /* WITHLCDBACKLIGHT */
	enum { gbglight = 0 };
#endif /* WITHLCDBACKLIGHT */

#if WITHKBDBACKLIGHT
	static uint_fast8_t gkblight /* = 1 */;
#else /* WITHKBDBACKLIGHT */
	enum { gkblight = 0 };
#endif /* WITHKBDBACKLIGHT */

#if WITHPWBUTTON	/* Наличие схемы электронного включения питания */
	static uint_fast8_t gpoweronhold = 1;	/* выдать "1" на выход удержания питания включенным */
#endif /* WITHPWBUTTON */

#if LCDMODE_COLORED
	//static uint_fast8_t gbluebgnd;
	enum { gbluebgnd = 0 };
#else
	enum { gbluebgnd = 0 };
#endif /* LCDMODE_COLORED */

	static uint_fast8_t gshowdbm = 1;	// Отображение уровня сигнала в dBm или S-memter
#if WITHAUTOTUNER

enum
{
 KSCH_TOHIGH = 0,
 KSCH_TOLOW,
 //
 KSCH_COUNT
};


#if FULLSET_7L8C
	/* 7 indictors, 8 capacitors */
	#define CMAX	254    //максимальное значение емкости конденсатора контура
	#define CMIN  0      //минимальное значение емкости конденсатора контура
	#define LMAX	127    //максимальное значение индуктивности катушки контура
	#define LMIN  0      //минимальное значение индуктивности катушки контура
#elif SHORTSET_7L8C
	/* 7 indictors, 8 capacitors */
	#define POSZ_C 8
	const FLASHMEM uint_fast8_t logtable_cap [] =
	{
		0, 1, 2, 3, 4, 5, 6, 7,	/* 0..POSZ_C - 1 */
		POSZ_C * 1, POSZ_C * 2, POSZ_C * 3,  POSZ_C * 4, POSZ_C * 5, POSZ_C * 6, POSZ_C * 7,
		POSZ_C * 8, POSZ_C * 9, POSZ_C * 10, POSZ_C * 11,  POSZ_C * 12, POSZ_C * 13, POSZ_C * 14, POSZ_C * 15,
		POSZ_C * 16, POSZ_C * 17, POSZ_C * 18, POSZ_C * 19,  POSZ_C * 20, POSZ_C * 21, POSZ_C * 22, POSZ_C * 23,
		POSZ_C * 24, POSZ_C * 25, POSZ_C * 26, POSZ_C * 27,  POSZ_C * 28, POSZ_C * 29, POSZ_C * 30, POSZ_C * 31,
	};

	#define POSZ_L 2
	const FLASHMEM uint_fast8_t logtable_ind [] =
	{
		0, 1, /* 1..POSZ_L - 1 */
		POSZ_L * 1, POSZ_L * 2, POSZ_L * 3,  POSZ_L * 4, POSZ_L * 5, POSZ_L * 6, POSZ_L * 7,
		POSZ_L * 8, POSZ_L * 9, POSZ_L * 10, POSZ_L * 11,  POSZ_L * 12, POSZ_L * 13, POSZ_L * 14, POSZ_L * 15,
		POSZ_L * 16, POSZ_L * 17, POSZ_L * 18, POSZ_L * 19,  POSZ_L * 20, POSZ_L * 21, POSZ_L * 22, POSZ_L * 23,
		POSZ_L * 24, POSZ_L * 25, POSZ_L * 26, POSZ_L * 27,  POSZ_L * 28, POSZ_L * 29, POSZ_L * 30, POSZ_L * 31,

		POSZ_L * 32, POSZ_L * 33, POSZ_L * 34, POSZ_L * 35,  POSZ_L * 36, POSZ_L * 37, POSZ_L * 38, POSZ_L * 39,
		POSZ_L * 40, POSZ_L * 41, POSZ_L * 42, POSZ_L * 43,  POSZ_L * 44, POSZ_L * 45, POSZ_L * 46, POSZ_L * 47,
		POSZ_L * 48, POSZ_L * 49, POSZ_L * 50, POSZ_L * 51,  POSZ_L * 52, POSZ_L * 53, POSZ_L * 54, POSZ_L * 55,
		POSZ_L * 56, POSZ_L * 57, POSZ_L * 58, POSZ_L * 59,  POSZ_L * 60, POSZ_L * 61, POSZ_L * 62, POSZ_L * 62,
	};

	#define CMAX (sizeof logtable_cap / sizeof logtable_cap [0] - 1)      //максимальное значение емкости конденсатора контура
	#define CMIN 0        //минимальное значение емкости конденсатора контура
	#define LMAX (sizeof logtable_ind / sizeof logtable_ind [0] - 1)        //максимальное значение индуктивности катушки контура
	#define LMIN 0        //минимальное значение индуктивности катушки контура

#elif SHORTSET8
	/* 8 indictors, 8 capacitors */
	#define POSZ 8
	const FLASHMEM uint_fast8_t logtable_cap [] =
	{
		0, 1, 2, 3, 4, 5, 6, 7,	/* 0..POSZ - 1 */
		POSZ * 1, POSZ * 2, POSZ * 3,  POSZ * 4, POSZ * 5, POSZ * 6, POSZ * 7,
		POSZ * 8, POSZ * 9, POSZ * 10, POSZ * 11,  POSZ * 12, POSZ * 13, POSZ * 14, POSZ * 15,
		POSZ * 16, POSZ * 17, POSZ * 18, POSZ * 19,  POSZ * 20, POSZ * 21, POSZ * 22, POSZ * 23,
		POSZ * 24, POSZ * 25, POSZ * 26, POSZ * 27,  POSZ * 28, POSZ * 29, POSZ * 30, POSZ * 31,
	};
	const FLASHMEM uint_fast8_t logtable_ind [] =
	{
		1, 2, 3, 4, 5, 6, 7,	/* 1..POSZ - 1 */
		POSZ * 1, POSZ * 2, POSZ * 3,  POSZ * 4, POSZ * 5, POSZ * 6, POSZ * 7,
		POSZ * 8, POSZ * 9, POSZ * 10, POSZ * 11,  POSZ * 12, POSZ * 13, POSZ * 14, POSZ * 15,
		POSZ * 16, POSZ * 17, POSZ * 18, POSZ * 19,  POSZ * 20, POSZ * 21, POSZ * 22, POSZ * 23,
		POSZ * 24, POSZ * 25, POSZ * 26, POSZ * 27,  POSZ * 28, POSZ * 29, POSZ * 30, POSZ * 31,
	};

	#define CMAX (sizeof logtable_cap / sizeof logtable_cap [0] - 1)      //максимальное значение емкости конденсатора контура
	#define CMIN 0        //минимальное значение емкости конденсатора контура
	#define LMAX (sizeof logtable_ind / sizeof logtable_ind [0] - 1)        //максимальное значение индуктивности катушки контура
	#define LMIN 0        //минимальное значение индуктивности катушки контура

#elif SHORTSET7
	/* 7 indictors, 7 capacitors */
	#define POSZ 2
	const FLASHMEM uint_fast8_t logtable_cap [] =
	{
		0, 1, /* 0..POSZ - 1 */
		POSZ * 1, POSZ * 2, POSZ * 3,  POSZ * 4, POSZ * 5, POSZ * 6, POSZ * 7,
		POSZ * 8, POSZ * 9, POSZ * 10, POSZ * 11,  POSZ * 12, POSZ * 13, POSZ * 14, POSZ * 15,
		POSZ * 16, POSZ * 17, POSZ * 18, POSZ * 19,  POSZ * 20, POSZ * 21, POSZ * 22, POSZ * 23,
		POSZ * 24, POSZ * 25, POSZ * 26, POSZ * 27,  POSZ * 28, POSZ * 29, POSZ * 30, POSZ * 31,

		POSZ * 32, POSZ * 33, POSZ * 34, POSZ * 35,  POSZ * 36, POSZ * 37, POSZ * 38, POSZ * 39,
		POSZ * 40, POSZ * 41, POSZ * 42, POSZ * 43,  POSZ * 44, POSZ * 45, POSZ * 46, POSZ * 47,
		POSZ * 48, POSZ * 49, POSZ * 50, POSZ * 51,  POSZ * 52, POSZ * 53, POSZ * 54, POSZ * 55,
		POSZ * 56, POSZ * 57, POSZ * 58, POSZ * 59,  POSZ * 60, POSZ * 61, POSZ * 62, POSZ * 62,
	};
	const FLASHMEM uint_fast8_t logtable_ind [] =
	{
		0, 1, /* 1..POSZ - 1 */
		POSZ * 1, POSZ * 2, POSZ * 3,  POSZ * 4, POSZ * 5, POSZ * 6, POSZ * 7,
		POSZ * 8, POSZ * 9, POSZ * 10, POSZ * 11,  POSZ * 12, POSZ * 13, POSZ * 14, POSZ * 15,
		POSZ * 16, POSZ * 17, POSZ * 18, POSZ * 19,  POSZ * 20, POSZ * 21, POSZ * 22, POSZ * 23,
		POSZ * 24, POSZ * 25, POSZ * 26, POSZ * 27,  POSZ * 28, POSZ * 29, POSZ * 30, POSZ * 31,

		POSZ * 32, POSZ * 33, POSZ * 34, POSZ * 35,  POSZ * 36, POSZ * 37, POSZ * 38, POSZ * 39,
		POSZ * 40, POSZ * 41, POSZ * 42, POSZ * 43,  POSZ * 44, POSZ * 45, POSZ * 46, POSZ * 47,
		POSZ * 48, POSZ * 49, POSZ * 50, POSZ * 51,  POSZ * 52, POSZ * 53, POSZ * 54, POSZ * 55,
		POSZ * 56, POSZ * 57, POSZ * 58, POSZ * 59,  POSZ * 60, POSZ * 61, POSZ * 62, POSZ * 62,
	};

	#define CMAX (sizeof logtable_cap / sizeof logtable_cap [0] - 1)      //максимальное значение емкости конденсатора контура
	#define CMIN 0        //минимальное значение емкости конденсатора контура
	#define LMAX (sizeof logtable_ind / sizeof logtable_ind [0] - 1)        //максимальное значение индуктивности катушки контура
	#define LMIN 0        //минимальное значение индуктивности катушки контура

#elif FULLSET7
	/* 7 indictors, 7 capacitors */
	#define CMAX	127    //максимальное значение емкости конденсатора контура
	#define CMIN  0      //минимальное значение емкости конденсатора контура
	#define LMAX	127    //максимальное значение индуктивности катушки контура
	#define LMIN  0      //минимальное значение индуктивности катушки контура
#elif FULLSET8
	/* 8 indictors, 8 capacitors */
	#define CMAX	254    //максимальное значение емкости конденсатора контура
	#define CMIN  0      //минимальное значение емкости конденсатора контура
	#define LMAX	254    //максимальное значение индуктивности катушки контура
	#define LMIN  0      //минимальное значение индуктивности катушки контура
#endif

#endif /* WITHAUTOTUNER */

#if WITHCAT

	static uint_fast8_t catenable = 1;	/* модифицируется через меню. */
	static uint_fast8_t catbaudrate = 3;	/* 3 is a 9600 */ /* модифицируется через меню. - номер скорости при работе по CAT */

	#if WITHCAT_CDC
		#if LCDMODE_DUMMY || ! WITHKEYBOARD
			enum { nopttsig = 1 };		// устройство без органов управления и индикации
			enum { nokeysig = 3 };		// устройство без органов управления и индикации
		#else /* LCDMODE_DUMMY || ! WITHKEYBOARD */
			enum { nopttsig = BOARD_CATSIG_NONE };
			enum { nokeysig = BOARD_CATSIG_NONE };
		#endif /* LCDMODE_DUMMY || ! WITHKEYBOARD */
	#else /* WITHCAT_CDC */
		enum { nopttsig = BOARD_CATSIG_NONE };
		enum { nokeysig = BOARD_CATSIG_NONE };
	#endif /* WITHCAT_CDC */

#if WITHTX
	static uint_fast8_t catsigptt = nopttsig;	/* Выбраный сигнал для перехода на передачу по CAT */
#endif /* WITHTX */
	static uint_fast8_t catsigkey = nokeysig;	/* Выбраный сигнал для манипуляции по CAT */


#else /* WITHCAT */

	enum { catenable = 0 };

#endif /* WITHCAT */

#if WITHIF4DSP
#if defined WITHAFGAINDEFAULT
	static dualctl16_t afgain1 = { WITHAFGAINDEFAULT, WITHAFGAINDEFAULT };
#else
	static dualctl16_t afgain1 = { BOARD_AFGAIN_MAX, BOARD_AFGAIN_MAX };	// Усиление НЧ на максимуме
#endif /* defined WITHAFGAINDEFAULT */
	static dualctl16_t rfgain1 = { BOARD_IFGAIN_MAX, BOARD_IFGAIN_MAX };	// Усиление ПЧ на максимуме
	static uint_fast16_t glineamp = WITHLINEINGAINMAX;	// усиление с LINE IN
	static uint_fast8_t gmikeboost20db;	// предусилитель микрофона
	static uint_fast8_t gmikeagc = 1;	/* Включение программной АРУ перед модулятором */
	static uint_fast8_t gmikeagcgain = 30;	/* Максимальное усидение АРУ микрофона */
	static uint_fast8_t gmikehclip = 20;		/* Ограничитель */

	#if WITHCOMPRESSOR
		static uint_fast8_t gcompressor_attack = 30;
		static uint_fast8_t gcompressor_release = 20;
		static uint_fast8_t gcompressor_hold = 10;
		static uint_fast8_t gcompressor_gain = 6;
		static uint_fast8_t gcompressor_threshold = 20;
	#endif /* WITHCOMPRESSOR */
	#if WITHREVERB
		static uint_fast8_t greverb;		/* ревербератор */
		static uint_fast8_t greverbdelay = 100;		/* ревербератор - задержка (ms) */
		static uint_fast8_t greverbloss = 30;		/* ревербератор - ослабление на возврате */
	#endif /* WITHREVERB */

	#if WITHUSBUAC
		static uint_fast8_t gdatamode;	/* передача звука с USB вместо обычного источника */
		uint_fast8_t hamradio_get_datamode(void) { return gdatamode; }

		#if WITHTX
		static uint_fast8_t gdatavox;	/* автоматический переход на передачу при появлении звука со стороны компьютера */
		#endif /* WITHTX */
		#if WITHUSBHEADSET
			static uint_fast8_t guacplayer = 1;	/* режим прослушивания выхода компьютера в наушниках трансивера - отладочный режим */
		#else /* WITHUSBHEADSET */
			static uint_fast8_t guacplayer;	/* режим прослушивания выхода компьютера в наушниках трансивера - отладочный режим */
		#endif /* WITHUSBHEADSET */
		#if WITHRTS96 || WITHRTS192 || WITHTRANSPARENTIQ
			static uint_fast8_t  gswapiq;		/* Поменять местами I и Q сэмплы в потоке RTS96 */
		#endif /* WITHRTS96 || WITHRTS192 || WITHTRANSPARENTIQ */
	#else /* WITHUSBUAC */
		enum { gdatamode = 0 };	/* передача звука с USB вместо обычного источника */
		enum { guacplayer = 0 };
	#endif /* WITHUSBUAC */
	#if WITHAFCODEC1HAVEPROC
		#define EQUALIZERBASE 12
		static int_fast32_t getequalizerbase(void)
		{
			return - EQUALIZERBASE;
		}
		static uint_fast8_t gmikeequalizer;	// включение обработки сигнала с микрофона (эффекты, эквалайзер, ...)
		static uint_fast8_t gmikeequalizerparams [HARDWARE_CODEC1_NPROCPARAMS] =
		{
			// Эквалайзер 80Hz 230Hz 650Hz 	1.8kHz 5.3kHz
			EQUALIZERBASE, EQUALIZERBASE, EQUALIZERBASE, EQUALIZERBASE, EQUALIZERBASE
		};
	#endif /* WITHAFCODEC1HAVEPROC */
#if WITHAFEQUALIZER
		static uint_fast8_t geqtx;
		static uint_fast8_t geqrx;
		static uint_fast8_t geqtxparams [AF_EQUALIZER_BANDS] =
		{
			AF_EQUALIZER_BASE, AF_EQUALIZER_BASE, AF_EQUALIZER_BASE
		};
		static uint_fast8_t geqrxparams [AF_EQUALIZER_BANDS] =
		{
			AF_EQUALIZER_BASE, AF_EQUALIZER_BASE, AF_EQUALIZER_BASE
		};

		int_fast32_t hamradio_get_af_equalizer_base(void)
		{
			return - AF_EQUALIZER_BASE;
		}

		int_fast32_t hamradio_get_af_equalizer_gain_rx(uint_fast8_t v)
		{
			ASSERT(v < AF_EQUALIZER_BANDS);
			return geqrxparams [v];
		}

		void hamradio_set_af_equalizer_gain_rx(uint_fast8_t index, uint_fast8_t gain)
		{
			ASSERT(index < AF_EQUALIZER_BANDS);
			ASSERT(gain <= AF_EQUALIZER_BASE * 2);
			geqrxparams [index] = gain;
			save_i8(offsetof(struct nvmap, geqrxparams [index]), geqrxparams [index]);
			updateboard(1, 0);
		}

		uint_fast8_t hamradio_get_geqrx(void)
		{
			return geqrx;
		}

		void hamradio_set_geqrx(uint_fast8_t v)
		{
			geqrx = v != 0;
			save_i8(offsetof(struct nvmap, geqrx), geqrx);
			updateboard(1, 0);
		}

	#endif /* WITHAFEQUALIZER */
	static uint_fast8_t gagcoff;
#else /* WITHIF4DSP */
	static const uint_fast8_t gagcoff;
	static const uint_fast8_t gdatamode;	/* передача звука с USB вместо обычного источника */
#endif /* WITHIF4DSP */

#define NRLEVELMAX 60

#if WITHAUTOTUNER
	static uint_fast16_t tunercap;// = (CMAX - CMIN) / 2 + CMIN;
	static uint_fast16_t tunerind;// = (LMAX - LMIN) / 2 + LMIN;
	static uint_fast8_t tunertype;
	static uint_fast8_t tunerwork;	/* начинаем работу с выключенным тюнером */
	static uint_fast8_t tunerdelay = 100;

#endif /* WITHAUTOTUNER */

#if WITHTX
	#if WITHSUBTONES
		// частоты  Continuous Tone-Coded Squelch System or CTCSS с точностью 0.1 герца.
		// https://en.wikipedia.org/wiki/Continuous_Tone-Coded_Squelch_System#List_of_tones
		static const FLASHMEM uint_least16_t gsubtones [] =
		{
			330,	/* 33.0 герц #0 */
			354,	/* 35.4 герц */
			366,	/* 36.6 герц */
			379,	/* 37.9 герц */
			396,	/* 39.6 герц */
			444,	/* 44.4 герц */
			475,	/* 47.5 герц */
			492,	/* 49.2 герц */
			512,	/* 51.2 герц */
			530,	/* 53.0 герц */
			549,	/* 54.9 герц */
			568,	/* 56.8 герц */
			588,	/* 58.8 герц */
			630,	/* 63.0 герц */
			670,	/* 67.0 герц */
			694,	/* 69.4 герц */
			719,	/* 71.9 герц */
			744,	/* 74.4 герц */
			770,	/* 77.0 герц #18 */
			797,	/* 79.7 герц */
			825,	/* 82.5 герц */
			854,	/* 85.4 герц */
			885,	/* 88.5 герц */
			915,	/* 91.5 герц */
			948,	/* 94.8 герц */
			974,	/* 97.4 герц */
			1000,	/* 100.0 герц */
			1035,	/* 103.5 герц */
			1072,	/* 107.2 герц */
			1109,	/* 110.9 герц */
			1148,	/* 114.8 герц */
			1188,	/* 118.8 герц */
			1230,	/* 123.0 герц */
			1273,	/* 127.3 герц */
			1318,	/* 131.8 герц */
			1365,	/* 136.5 герц */
			1413,	/* 141.3 герц */
			1462,	/* 146.2 герц */
			1514,	/* 151.4 герц */
			1567,	/* 156.7 герц */
			1598,	/* 159.8 герц */
			1622,	/* 162.2 герц */
			1655,	/* 165.5 герц */
			1679,	/* 167.9 герц */
			1713,	/* 171.3 герц */
			1738,	/* 173.8 герц */
			1773,	/* 177.3 герц */
			1799,	/* 179.9 герц */
			1835,	/* 183.5 герц */
			1862,	/* 186.2 герц */
			1899,	/* 189.9 герц */
			1928,	/* 192.8 герц */
			1966,	/* 196.6 герц */
			1995,	/* 199.5 герц */
			2035,	/* 203.5 герц */
			2065,	/* 206.5 герц */
			2107,	/* 210.7 герц */
			2181,	/* 218.1 герц */
			2257,	/* 225.7 герц */
			2291,	/* 229.1 герц */
			2336,	/* 233.6 герц */
			2418,	/* 241.8 герц */
			2503,	/* 250.3 герц */
			2541,	/* 254.1 герц */
		};

		static uint_fast8_t gsubtonei = 18;	// частота subtone = 77.0 герц
		static uint_fast8_t gctssenable;	// разрешить формирование subtone
	#endif /* WITHSUBTONES */


	static uint_fast8_t gbandf2adj [NUMLPFADJ]; /* коррекция мощности по ФНЧ передачика */

	/* запись значений по умолчанию для корректировок мощности в завивимости от диапазона ФНЧ УМ */
	static void
	bandf2adjust_initialize(void)
	{
		uint_fast8_t i;

		for (i = 0; i < ARRAY_SIZE(gbandf2adj); ++ i)
		{
			gbandf2adj [i] = 100;
		}
	}

	#if WITHPOWERTRIM
		#if WITHPACLASSA
			static uint_fast8_t gclassapower = WITHPOWERTRIMCLASSA;
			static uint_fast8_t gclassamode;	/* использование режима клвсс А при передаче */
		#else /* WITHPACLASSA */
			static uint_fast8_t gclassapower = WITHPOWERTRIMMAX;
		#endif /* WITHPACLASSA */
		static dualctl8_t gnormalpower = { WITHPOWERTRIMMAX, WITHPOWERTRIMMAX };
		#if WITHLOWPOWEREXTTUNE
			static uint_fast8_t gtunepower = WITHPOWERTRIMATU; /* мощность при работе автоматического согласующего устройства */
		#else /* WITHLOWPOWEREXTTUNE */
			enum { gtunepower = WITHPOWERTRIMMAX }; /* мощность при работе автоматического согласующего устройства */
		#endif /* WITHLOWPOWEREXTTUNE */
	#elif WITHPOWERLPHP
		static uint_fast8_t gpwri = 1;	// индекс нормальной мощности
		#if WITHLOWPOWEREXTTUNE
			static uint_fast8_t gpwratunei = 0; // индекс мощность при работе автоматического согласующего устройства
		#else /* WITHLOWPOWEREXTTUNE */
			enum { gpwratunei = 1 }; // индекс нормальной мощности
		#endif /* WITHLOWPOWEREXTTUNE */
	#endif /* WITHPOWERTRIM, WITHPOWERLPHP */

	#if WITHPABIASTRIM
		#if defined (WITHBBOXPABIAS)
			static uint_fast8_t gpabias = WITHBBOXPABIAS; //WITHPABIASMIN;	/* ток оконечного каскада передатчика */
		#else /* defined (WITHBBOXPABIAS) */
			static uint_fast8_t gpabias = 208; //WITHPABIASMIN;	/* регулировка тока покоя оконечного каскада передатчика */
		#endif /* defined (WITHBBOXPABIAS) */
	#endif /* WITHPABIASTRIM */
	static uint_fast8_t gtxgate = 1;		/* разрешение драйвера и оконечного усилителя */
	#if WITHVOX
		static uint_fast8_t gvoxenable;	/* модифицируется через меню - автоматическое управление передатчиком (от голоса) */
		static uint_fast8_t gvoxlevel = 10;	/* модифицируется через меню - усиление VOX */
		static uint_fast8_t gavoxlevel = 50;	/* модифицируется через меню - усиление anti-VOX */
		static uint_fast8_t voxdelay = 70;	/* модифицируется через меню - задержка отпускания VOX */
	#else /* WITHVOX */
		enum { gvoxenable = 0 };	/* автоматическое управление передатчиком (от голоса) */
	#endif /* WITHVOX */

	#if WITHMUTEALL
		static uint_fast8_t gmuteall;	/* Отключить микрофон всегда. */
	#else /* WITHMUTEALL */
		enum { gmuteall = 0 };
	#endif /* WITHMUTEALL */

	#if ! WITHPACLASSA
		enum { gclassamode = 0 };	/* использование режима клвсс А при передаче */
	#endif /* WITHPACLASSA */

	#if WITHELKEY
		static uint_fast8_t bkinenable = 1;	/* модифицируется через меню - автоматическое управление передатчиком (от телеграфного манипулятора) */
		static uint_fast8_t bkindelay = 40;	/* в десятках mS. модифицируется через меню - задержка отпускания BREAK-IN */
	#endif /* WITHELKEY */

#if TXPATH_BIT_GATE_RX && CTLSTYLE_SW2011ALL
	static const uint_fast8_t pretxdelay = 1;	/* признак того, что требуется снятие питания со второго смесителя */
#else
	static const uint_fast8_t pretxdelay = 0;
#endif

	static uint_fast8_t grgbeep;	/* разрешение (не-0) или запрещение (0) формирования roger beep */
	#if (CTLSTYLE_SW2016MINI)
		static uint_fast8_t rxtxdelay = 45;	/* в единицах mS. модифицируется через меню - задержка перехода прём-передача */
		static uint_fast8_t txrxdelay = 15;	/* в единицах mS. модифицируется через меню - задержка перехода передача-прём */
	#elif (CTLREGMODE_STORCH_V4)
		// modem
		static uint_fast8_t rxtxdelay = 75;	/* в единицах mS. модифицируется через меню - задержка перехода прём-передача */
		static uint_fast8_t txrxdelay = 25;	/* в единицах mS. модифицируется через меню - задержка перехода передача-прём */
	#else
		static uint_fast8_t rxtxdelay = 25;	/* в единицах mS. модифицируется через меню - задержка перехода прём-передача */
		static uint_fast8_t txrxdelay = 25;	/* в единицах mS. модифицируется через меню - задержка перехода передача-прём */
	#endif

#else /* WITHTX */
	static const uint_fast8_t bkinenable = 0;	/* модифицируется через меню - автоматическое управление передатчиком (от телеграфного манипулятора) */
	static const uint_fast8_t gvoxenable = 0;	/* модифицируется через меню - автоматическое управление передатчиком (от голоса) */
	static const uint_fast8_t bkindelay = 80;	/* в десятках mS. модифицируется через меню - задержка отпускания BREAK-IN */
#endif /* WITHTX */

#if WITHELKEY

	static dualctl8_t elkeywpm = { 20, 20 };	/* скорость электронного ключа */
	static uint_fast8_t dashratio = 30;	/* отношение тире к длительности точки - в десятках процентов */
	static uint_fast8_t spaceratio = 10;	/* отношение паузы к длительности точки - в десятках процентов */
	static uint_fast8_t elkeyreverse;

	#if WITHVIBROPLEX
		#if ELKEY328
			static uint_fast8_t elkeymode = 1;		/* режим электронного ключа - 0 - ACS, 1 - electronic key, 2 - straight key, 3 - BUG key */
			static uint_fast8_t elkeyslope = 3;	/* ELKEY328 скорость уменьшения длительности точки и паузы - имитация виброплекса */
			static uint_fast8_t elkeyslopeenable = 1;
		#else
			static uint_fast8_t elkeymode;		/* режим электронного ключа - 0 - ACS, 1 - electronic key, 2 - straight key, 3 - BUG key */
			static uint_fast8_t elkeyslope;		/* скорость уменьшения длительности точки и паузы - имитация виброплекса */
			static uint_fast8_t elkeyslopeenable = 1;
		#endif
	#else
		static uint_fast8_t elkeymode;		/* режим электронного ключа - 0 - ACS, 1 - electronic key, 2 - straight key, 3 - BUG key */
		static uint_fast8_t elkeyslope;		/* скорость уменьшения длительности точки и паузы - имитация виброплекса */
		static uint_fast8_t elkeyslopeenable = 1;

	#endif /* WITHVIBROPLEX */
#else
	//static const uint_fast8_t elkeymode = 2;		/* режим электронного ключа - 0 - ACS, 1 - electronic key, 2 - straight key, 3 - BUG key */
	//static const uint_fast8_t elkeyslope;		/* скорость уменьшения длительности точки и паузы - имитация виброплекса */
	//static const uint_fast8_t elkeyslopeenable;
#endif


static uint_fast8_t  stayfreq;	/* при изменении режимов кнопками - не меняем частоту */

#if defined (DAC1_TYPE)
	#if defined (WITHDAC1VALDEF)
		static uint_fast8_t dac1level = WITHDAC1VALDEF;
	#else /* defined (WITHDAC1VALDEF) */
		static uint_fast8_t dac1level = (WITHDAC1VALMAX + 1 - WITHDAC1VALMIN) / 2 + WITHDAC1VALMIN;	/* модифицируется через меню. */
	#endif /* defined (WITHDAC1VALDEF) */
#endif /* defined (DAC1_TYPE) */


#if LO1FDIV_ADJ
	static uint_fast8_t lo1powmap [2] = { LO1_POWER2, LO1_POWER2 };		/* на сколько делим выходную частоту синтезатора первого гетеродина */
#endif
#if LO2FDIV_ADJ
	static uint_fast8_t lo2powmap [2] = { LO2_POWER2, LO2_POWER2 };		/* на сколько делим выходную частоту синтезатора 2-го гетеродина */
#endif
#if LO3FDIV_ADJ
	static uint_fast8_t lo3powmap [2] = { LO3_POWER2, LO3_POWER2 };		/* на сколько делим выходную частоту синтезатора 3-го гетеродина */
#endif
#if LO4FDIV_ADJ
	static uint_fast8_t lo4powmap [2] = { LO4_POWER2, LO4_POWER2 };		/* на сколько делим выходную частоту синтезатора 4-го гетеродина */
#endif

#if LO1PHASES
	static uint_fast16_t phasesmap [2]; /* приемник [0] и передатчик [1] - коррекция фазы - в nvram phaserx и phasetx */
#endif /* LO1PHASES */

static uint_fast8_t dctxmodecw;	/* при передаче предполагается, что частоты if2 и далее равны 0 - формирование телеграфа на передачу DDS */

#if 1//WITHBARS

	#if WITHINTEGRATEDDSP
		enum { S9FENCE = - 73 };	// -73.01dBm == 50 uV rms == S9
		uint_fast8_t s9level = UINT8_MAX + S9FENCE;					/* уровни калибровки S-метра */
		uint_fast8_t s9delta = (6 * 8);		// 9 баллов - 8 интервалов - по 6 децибел каждый
		uint_fast8_t s9_60_delta = 60;		// 60 dB
	#elif (CTLSTYLE_SW2016MINI)
		uint_fast8_t s9level = 110;			/* уровни калибровки S-метра */
		uint_fast8_t s9delta = 70;
		uint_fast8_t s9_60_delta = 50;
	#elif CTLSTYLE_SW2012CN || CTLSTYLE_SW2013SF || CTLSTYLE_SW2013RDX || CTLSTYLE_SW2012CN5 || CTLSTYLE_SW2015
		uint_fast8_t s9level = 110;			/* уровни калибровки S-метра */
		uint_fast8_t s9delta = 70;
		uint_fast8_t s9_60_delta = 50;
	#elif CTLSTYLE_SW2016 || CTLSTYLE_SW2016VHF || CTLSTYLE_SW2018XVR
		uint_fast8_t s9level = 110;			/* уровни калибровки S-метра */
		uint_fast8_t s9delta = 70;
		uint_fast8_t s9_60_delta = 50;
	#elif (CTLSTYLE_SW2011 || CTLSTYLE_SW2012_MINI)
		uint_fast8_t s9level = 120;			/* уровни калибровки S-метра */
		uint_fast8_t s9delta = 120;
		uint_fast8_t s9_60_delta = 120;
	#elif CTLSTYLE_RA4YBO_V3
		uint_fast8_t s9level = 73;			/* уровни калибровки S-метра */
		uint_fast8_t s9delta = 15;
		uint_fast8_t s9_60_delta = 50;
	#else
		uint_fast8_t s9level = 88;			/* уровни калибровки S-метра */
		uint_fast8_t s9delta = 34;
		uint_fast8_t s9_60_delta = 45;
	#endif

	#if (WITHSWRMTR || WITHSHOWSWRPWR)
		uint_fast16_t minforward = (1U << HARDWARE_ADCBITS) / 32;
		#if WITHSWRCALI
			uint_fast8_t swrcalibr = WITHSWRCALI;	/* калибровочный параметр SWR-метра */
		#else /* WITHSWRCALI */
			uint_fast8_t swrcalibr = 100;	/* калибровочный параметр SWR-метра */
		#endif /* WITHSWRCALI */
	#endif /* (WITHSWRMTR || WITHSHOWSWRPWR) */

	#if WITHPWRMTR || WITHSWRMTR
		#if WITHMAXPWRCALI
			uint_fast8_t maxpwrcali = WITHMAXPWRCALI;	/* калибровочный параметр PWR-метра */
		#else
			uint_fast8_t maxpwrcali = 255;	/* калибровочный параметр PWR-метра */
		#endif
	#else
		uint_fast8_t maxpwrcali = 255;	/* калибровочный параметр PWR-метра */
	#endif /* WITHPWRMTR || WITHSWRMTR */

	#if WITHSWRMTR && ! WITHSHOWSWRPWR
		uint_fast8_t swrmode = 1;
	#elif WITHPWRMTR
		uint_fast8_t swrmode = 0;
	#else
		//static const uint_fast8_t swrmode = 0;
	#endif
#else /* WITHBARS */
	uint_fast8_t swrmode = 0;
#endif /* WITHBARS */

#if WITHVOLTLEVEL && ! WITHREFSENSOR

	// Напряжение fullscale = VREF * 5.3 = 3.3 * 5.3 = 17.5 вольта: сверху 4.3 килоом, синзу 1.0 килоом
	// в схеме датчика делитель: сверху 4.3 килоома, снизу 1 килоом.
	// ADCVREF_CPU - в сотнях милливольт.

	uint_fast8_t voltcalibr100mV = (ADCVREF_CPU * (VOLTLEVEL_UPPER + VOLTLEVEL_LOWER) + VOLTLEVEL_LOWER / 2) / VOLTLEVEL_LOWER;		// Напряжение fullscale - что показать при ADCVREF_CPU вольт на входе АЦП

#endif /* WITHVOLTLEVEL && ! WITHREFSENSOR */
#if (WITHCURRLEVEL || WITHCURRLEVEL2)

	// Корректировка показаний измерителя тока оконечного каскада
	#define IPACALI_RANGE 500
	#define IPACALI_BASE (IPACALI_RANGE / 2)
	static int_fast32_t getipacalibase(void)
	{
		return - IPACALI_BASE;
	}
	static uint_fast16_t gipacali = IPACALI_BASE;
#endif /* (WITHCURRLEVEL || WITHCURRLEVEL2) */
#if WITHDIRECTFREQENER
	static uint_fast8_t editfreqmode;		/* Режим прямого ввода частоты */
	static uint_fast32_t editfreq;		/* значение частоты, которое редактируем */
	static uint_fast8_t blinkpos;		/* позиция редактирования (степень 10) */
	static uint_fast8_t blinkstate = 1;		/* В позиции редактирования показ подчёркивания (0 - пробел) */
#endif /* WITHDIRECTFREQENER */

#if WITHRFSG
	static uint_fast8_t userfsg;		/* включено управление ВЧ сигнал-генератором */
#else /* WITHRFSG */
	enum { userfsg = 0 };
#endif /* WITHRFSG */

static uint_fast8_t gmoderows [2];		/* индексом используется результат функции getbankindex_xxx(tx) */
										/* номер режима работы в маске (номер тройки бит) */
static uint_fast8_t gmodecolmaps [2] [MODEROW_COUNT];	/* индексом 1-й размерности используется результат функции getbankindex_xxx(tx) */
#if WITHSPKMUTE
	static uint_fast8_t gmutespkr;		/*  выключение динамика */
#endif /* WITHSPKMUTE */
										/* маска режимов работы (тройки бит, указывают номер позиции в каждой строке) */
#if CTLSTYLE_RA4YBO
	static uint_fast8_t  gaffilter;		/* включение ФНЧ на приёме в аппарате RA4YBO */
#elif CTLSTYLE_RA4YBO_V1 || CTLSTYLE_RA4YBO_V2 || CTLSTYLE_RA4YBO_V3 || KEYBSTYLE_RA4YBO_AM0
	static uint_fast8_t  guser1;
	static uint_fast8_t  guser2;
	static uint_fast8_t  guser3;
	static uint_fast8_t  guser4;
	static uint_fast8_t  guser5;
	static uint_fast8_t  gaffilter;		/* включение ФНЧ на приёме в аппарате RA4YBO */
#endif /* CTLSTYLE_RA4YBO_V1 || CTLSTYLE_RA4YBO_V2 || CTLSTYLE_RA4YBO_V3 */

#if WITHTX
#if WITHTHERMOLEVEL
	static uint_fast8_t gtempvmax = 55;		/* порог срабатывания защиты по температуре */
#endif /* WITHTHERMOLEVEL */
#if (WITHSWRMTR || WITHSHOWSWRPWR)
#if defined (WITHSWRPROT)
	static uint_fast8_t gswrprot = WITHSWRPROT;
#else /* defined (WITHSWRPROT) */
	static uint_fast8_t gswrprot = 1;	/* защита от превышения КСВ */
#endif /* defined (WITHSWRPROT) */
#if WITHTHERMOLEVEL
	static uint_fast8_t gheatprot = 1;	/* защита от перегрева */
#endif /* WITHTHERMOLEVEL */
#endif /* (WITHSWRMTR || WITHSHOWSWRPWR) */
	static uint_fast8_t tunemode;	/* режим настройки передающего тракта */
	static uint_fast8_t moxmode;	/* передача, включённая кнопкой с клавиатуры */
#if WITHAUTOTUNER
	static uint_fast8_t reqautotune;
#else
	enum { reqautotune = 0 };
#endif /* WITHAUTOTUNER */

	enum { reqautotune2 = 0 };
#else /* WITHTX */

	enum { tunemode = 0, moxmode = 0, reqautotune = 0, reqautotune2 = 0 };

#endif /* WITHTX */

static uint_fast8_t menuset; 	/* номер комплекта функций на кнопках (переключается кнопкой MENU) */
static uint_fast8_t dimmflag;	/* не-0: притушить дисплей. */
static uint_fast8_t sleepflag;	/* не-0: выключить дисплей и звук. */

static uint_fast8_t amenuset(void)
{
	if ((dimmflag || sleepflag || dimmmode))
		return display_getpagesleep();
	return menuset;
}


uint_fast8_t habradio_get_classa(void)
{
#if WITHPACLASSA
	return gclassamode;
#else /* WITHPACLASSA */
	return 0;
#endif /* WITHPACLASSA */
}
// текущее состояние LOCK
uint_fast8_t
hamradio_get_lockvalue(void)
{
	return lockmode;
}

// текущее состояние FAST
uint_fast8_t
hamradio_get_usefastvalue(void)
{
	return gusefast;
}

static int_fast32_t getzerobase(void)
{
	return 0;
}

/* поддержка ABOUT: частота процессора */
static int_fast32_t getcpufreqbase(void)
{
	return CPU_FREQ / 1000000L;
}

static uint_fast16_t gzero;

#define ADCOFFSETMID (512 / 2)
static int_fast32_t getadcoffsbase(void)
{
	return - ADCOFFSETMID;
}


#if defined (REFERENCE_FREQ)

	static const int_fast32_t refbase = REFERENCE_FREQ - OSCSHIFT;

#if defined (REALREFERENCE_FREQ)
	static uint_fast16_t refbias = OSCSHIFT - (REFERENCE_FREQ - REALREFERENCE_FREQ);
#else /* defined (REALREFERENCE_FREQ) */
	static uint_fast16_t refbias = OSCSHIFT;
#endif /* defined (REALREFERENCE_FREQ) */

	int_fast32_t getrefbase(void)
	{
		return refbase;
	}
#endif /* defined (REFERENCE_FREQ) */

#if CTLSTYLE_SW2011ALL
static uint_fast8_t gkeybeep10 = 1850 / 10;	/* озвучка нажатий клавиш - 1850 Гц */
#else /* CTLSTYLE_SW2011ALL */
static uint_fast8_t gkeybeep10 = 880 / 10;	/* озвучка нажатий клавиш - 880 Гц - нота ля второй октавы (A5) (аналогично FT1000) */
#endif /* CTLSTYLE_SW2011ALL */


#if WITHMIC1LEVEL
	static uint_fast16_t gmik1level = (WITHMIKEINGAINMAX - WITHMIKEINGAINMIN) / 2 + WITHMIKEINGAINMIN;
#endif /* WITHMIC1LEVEL */
#if defined(CODEC1_TYPE) && (CODEC1_TYPE == CODEC_TYPE_NAU8822L)
	uint_fast8_t ALCNEN = 0;	// ALC noise gate function control bit
	uint_fast8_t ALCNTH = 0;	// ALC noise gate threshold level
	uint_fast8_t ALCEN = 0;	// only left channel ALC enabled
	uint_fast8_t ALCMXGAIN = 7;	// Set maximum gain limit for PGA volume setting changes under ALC control
	uint_fast8_t ALCMNGAIN = 0;	// Set minimum gain value limit for PGA volume setting changes under ALC control
#endif /* defined(CODEC1_TYPE) && (CODEC1_TYPE == CODEC_TYPE_NAU8822L) */
#if WITHIF4DSP
#if WITHTX
	static uint_fast16_t gdesignscale = 100;		/* используется при калибровке параметров интерполятора */
	#if WITHTXCPATHCALIBRATE
		static uint_fast16_t ggaincwtx = 100;		/* Увеличение усиления при передаче в цифровых режимах 100..300% */
		static uint_fast16_t ggaindigitx = 150;		/* Увеличение усиления при передаче в цифровых режимах 100..300% */
	#elif WITHTXCWREDUCE
		static uint_fast16_t ggaincwtx = 60;		/* Увеличение усиления при передаче в цифровых режимах 100..300% */
		static uint_fast16_t ggaindigitx = 150;		/* Увеличение усиления при передаче в цифровых режимах 100..300% */
	#else /* WITHTXCWREDUCE */
		static uint_fast16_t ggaincwtx = 100;		/* Увеличение усиления при передаче в цифровых режимах 100..300% */
		static uint_fast16_t ggaindigitx = 150;		/* Увеличение усиления при передаче в цифровых режимах 100..300% */
	#endif /* WITHTXCWREDUCE */
	static uint_fast8_t gamdepth = 30;		/* Глубина модуляции в АМ - 0..100% */
	static uint_fast8_t gnfmdeviation = 55;	/* Девиация при передаче в NFM - в сотнях герц */

	/*  Использование амплитуды сигнала с ЦАП передатчика - 0..100% */
	#if defined (WITHDEFDACSCALE)
		static uint_fast8_t gdacscale = WITHDEFDACSCALE;	/* настраивается под прегруз драйвера. */
	#else /* defined (WITHDEFDACSCALE) */
		static uint_fast8_t gdacscale = 50;	/* настраивается под прегруз драйвера. */
	#endif /* defined (WITHDEFDACSCALE) */

#endif /* WITHTX */

	#define FSADCPOWEROFFSET10 700
	static int_fast32_t getfsasdcbase10(void)
	{
		return - FSADCPOWEROFFSET10;
	}

#if WITHWAVPLAYER || WITHSENDWAV
	static uint_fast8_t gloopmsg, gloopsec = 15;
	static uint_fast8_t loopticks;
	static const char * const loopnames [] =
	{
			"none",
			"1.wav",
			"2.wav",
			"3.wav",
			"4.wav",
			"5.wav",
	};
	void playhandler(uint8_t code);
#endif /* WITHWAVPLAYER || WITHSENDWAV */

	static uint_fast8_t gcwedgetime = 5;			/* Время нарастания/спада огибающей телеграфа при передаче - в 1 мс */
	static uint_fast8_t gsubtonelevel = 10;	/* Уровень сигнала CTCSS в процентах - 0%..100% */
#if CTLSTYLE_OLEG4Z_V1
	static uint_fast8_t gsidetonelevel = 0;	/* Уровень сигнала самоконтроля в процентах - 0%..100% */
	static uint_fast8_t gdigigainmax = 120; /* диапазон ручной регулировки цифрового усиления - максимальное значение */
	static uint_fast16_t gfsadcpower10 [2] =
	{
		(+ 180) + FSADCPOWEROFFSET10,	// для соответствия HDSDR мощность, соответствующая full scale от IF ADC
		(+ 190) + FSADCPOWEROFFSET10,	// с конвертором
	};
#elif defined (ADC1_TYPE) && (ADC1_TYPE == ADC_TYPE_AD9246)
	// 14 bit AD9246 + LTC6401-20
	static uint_fast8_t gsidetonelevel = 15;	/* Уровень сигнала самоконтроля в процентах - 0%..100% */
	static uint_fast8_t gdigigainmax = 86;	/* диапазон ручной регулировки цифрового усиления - максимальное значение */
	static uint_fast16_t gfsadcpower10 [2] =
	{
		(-  30) + FSADCPOWEROFFSET10,	// для соответствия HDSDR мощность, соответствующая full scale от IF ADC
		(- 330) + FSADCPOWEROFFSET10,	// с конвертором
	};
#else /* CTLSTYLE_OLEG4Z_V1 */
	// 16 bit LTC2208 + LTC6401-20
	static uint_fast8_t gsidetonelevel = 15;	/* Уровень сигнала самоконтроля в процентах - 0%..100% */
	static uint_fast8_t gdigigainmax = 86;	/* диапазон ручной регулировки цифрового усиления - максимальное значение */
	static uint_fast16_t gfsadcpower10 [2] = 
	{
		(- 30) + FSADCPOWEROFFSET10,	// для соответствия HDSDR мощность, соответствующая full scale от IF ADC
		(- 230) + FSADCPOWEROFFSET10,	// с конвертором
	};
#endif /* CTLSTYLE_OLEG4Z_V1 */
	static uint_fast8_t gmoniflag;		/* разрешение самопрослушивания */

	static uint_fast8_t gvad605 = 180; //UINT8_MAX;	/* напряжение на AD605 (управление усилением тракта ПЧ */
	#if WITHDSPEXTDDC	/* "Воронёнок" с DSP и FPGA */
		static uint_fast8_t gdither;		/* управление зашумлением в LTC2208 */
		#if (ADC1_TYPE == ADC_TYPE_AD9246) || CTLSTYLE_V3D
			static uint_fast8_t gadcrand = 0;		/* управление интерфейсом в LTC2208 */
		#else /* ADC1_TYPE == ADC_TYPE_AD9246 */
			static uint_fast8_t gadcrand = 1;		/* управление интерфейсом в LTC2208 */
		#endif /* ADC1_TYPE == ADC_TYPE_AD9246 */

		static uint_fast8_t gadcfifo = 1;
		static uint_fast16_t gadcoffset = ADCOFFSETMID;
		static uint_fast8_t gdactest;
		#if WITHDACSTRAIGHT
			static uint_fast8_t gdacstraight = 1;	/* Требуется формирование кода для ЦАП в режиме беззнакового кода */
		#else /* WITHDACSTRAIGHT */
			static uint_fast8_t gdacstraight = 0;	/* Требуется формирование кода для ЦАП в режиме знакового кода */
		#endif /* WITHDACSTRAIGHT */
		#if WITHTXINHDISABLE
			static uint_fast8_t gtxinhenable = 0;	/* запрещение реакции на вход tx_inh */
		#else /* WITHTXINHDISABLE */
			static uint_fast8_t gtxinhenable = 1;	/* разрешение реакции на вход tx_inh */
		#endif /* WITHTXINHDISABLE */
	#endif /* WITHDSPEXTDDC */

#endif /* WITHIF4DSP */

#if WITHMODEM
	static const FLASHMEM uint_fast32_t modembr2int100 [] =	// Индекс в этой таблице хранится в gmodemspeed
	{
		3125,			// 31.25 baud		#0
		6250,			// 62.5 baud		#1
		125 * 100uL,	// 125 baud			#2
		150 * 100uL,	// 150 baud			#3
		250 * 100uL,	// 125 baud			#4
		300 * 100uL,	// 300 baud			#5
		500 * 100uL,	// 500 baud			#6
		600 * 100uL,	// 600 baud			#7
		750 * 100uL,	// 750 baud			$8
		1000 * 100uL,	// 1000 (1k) baud	#9
		1200 * 100uL,	// 1200 (1.2k) baud	#10
		2000 * 100uL,	// 2k baud			#11
		2400 * 100uL,	// 2,4k baud		#12
		3600 * 100uL,	// 3,6k baud		#13
		4000 * 100uL,	// 4,0k baud		#14
		4800 * 100uL,	// 4,8k baud		#15
		5000 * 100uL,	// 5,0k baud		#16
		5600 * 100uL,	// 5,6k baud		#17
		6000 * 100uL,	// 6,0k baud		#18
		6400 * 100uL,	// 6,4k baud		#19
		8000 * 100uL,	// 8,0k baud		#20
		9200 * 100uL,	// 9,2k baud		#21
		9600 * 100uL,	// 9,6k baud		#22
	};

	static uint_fast8_t gmodemspeed = 20;	// индекс в таблице скоростей передачи - #5: 300 baud, #7: 600 baud, #9: 1k baud
	static uint_fast8_t gmodemmode;		// применяемая модуляция

	uint_fast32_t hamradio_get_modem_baudrate100(void)
	{
		return modembr2int100 [gmodemspeed];
	}

#endif /* WITHMODEM */

#if WITHAUTOTUNER

// что удалось достичь в результате перебора
typedef struct tunerstate
{
	uint8_t tunercap, tunerind, tunertype;
	uint16_t swr;	// values 0..190: SWR = 1..20
	adcvalholder_t f, r;
} tus_t;

#define TUS_SWRMIN (100)			// 1.0
#define TUS_SWRMAX (TUS_SWRMIN * 9)			// 4.0
#define TUS_SWR1p1 (TUS_SWRMIN * 11 / 10)	// SWR=1.1

static void board_set_tuner_group(void)
{
	//PRINTF(PSTR("tuner: CAP=%-3d, IND=%-3d, TYP=%d\n"), tunercap, tunerind, tunertype);
	// todo: добавить учет включенной антенны
#if SHORTSET7 || SHORTSET8 || SHORTSET_7L8C
	board_set_tuner_C(logtable_cap [tunercap]);
	board_set_tuner_L(logtable_ind [tunerind]);
	//board_set_tuner_C(1U << tunercap);
	//board_set_tuner_L(1U << tunerind);
#else /* SHORTSET7 || SHORTSET8 || SHORTSET_7L8C */
	board_set_tuner_C(tunercap);
	board_set_tuner_L(tunerind);
#endif /* SHORTSET7 || SHORTSET8 || SHORTSET_7L8C */
	board_set_tuner_type(tunertype);
	board_set_tuner_bypass(! tunerwork);
}

// выдача параметров на тюнер
static void updateboard_tuner(void)
{
	//PRINTF(PSTR("updateboard_tuner: CAP=%-3d, IND=%-3d, TYP=%d\n"), tunercap, tunerind, tunertype);
	board_set_tuner_group();
	board_update();		/* вывести забуферированные изменения в регистры */
}

// ожидание требуемого времени после выдачи параметров на тюнер.
static void tuner_waitadc(void)
{
	uint_fast8_t n = (tunerdelay + 4) / 5;
	while (n --)
		local_delay_ms(5);
}

static uint_fast16_t tuner_get_swr0(uint_fast16_t fullscale, adcvalholder_t * pr, adcvalholder_t * pf)
{
	adcvalholder_t r0;
	const adcvalholder_t f0 = board_getswrmeter_unfiltered(& r0, swrcalibr);
	adcvalholder_t r1;
	const adcvalholder_t f1 = board_getswrmeter_unfiltered(& r1, swrcalibr);
	adcvalholder_t r2;
	const adcvalholder_t f2 = board_getswrmeter_unfiltered(& r2, swrcalibr);
	adcvalholder_t r3;
	const adcvalholder_t f3 = board_getswrmeter_unfiltered(& r3, swrcalibr);
	adcvalholder_t r4;
	const adcvalholder_t f4 = board_getswrmeter_unfiltered(& r4, swrcalibr);
	adcvalholder_t r5;
	const adcvalholder_t f5 = board_getswrmeter_unfiltered(& r5, swrcalibr);
	adcvalholder_t r6;
	const adcvalholder_t f6 = board_getswrmeter_unfiltered(& r6, swrcalibr);
	adcvalholder_t r7;
	const adcvalholder_t f7 = board_getswrmeter_unfiltered(& r7, swrcalibr);
	const uint_fast8_t fs = fullscale - TUS_SWRMIN;

	const adcvalholder_t r = r0 + r1 + r2 + r3 + r4 + r5 + r6 + r7;
	const adcvalholder_t f = f0 + f1 + f2 + f3 + f4 + f5 + f6 + f7;
	* pr = r;
	* pf = f;

	//PRINTF("f:%d,%d,%d,%d,%d,%d,%d,%d r:%d,%d,%d,%d,%d,%d,%d,%d\n", f0, f1, f2, f3, f4, f5, f6, f7, r0, r1, r2, r3, r4, r5, r6, r7);
	if (f < minforward)
		return 0;	// SWR=1
	else if (f <= r)
		return fs;		// SWR is infinite

	const uint_fast16_t swr10 = (uint_fast32_t) (f + r) * TUS_SWRMIN / (f - r) - TUS_SWRMIN;
	return swr10 > fs ? fs : swr10;
}

void display2_swrsts22(
	uint_fast8_t x,
	uint_fast8_t y,
	dctx_t * pctx
	)
{
	adcvalholder_t r;
	adcvalholder_t f;
	const uint_fast16_t swr = tuner_get_swr0(TUS_SWRMAX, & r, & f);
	char b [23];

	local_snprintf_P(b, ARRAY_SIZE(b), PSTR("%u.%02u f=%-5u r=%-5u"),
		(unsigned) (swr + TUS_SWRMIN) / 100,
		(unsigned) (swr + TUS_SWRMIN) % 100,
		f,
		r);
	display_at(x, y, b);

}

static void printtunerstate(const char * title, uint_fast16_t swr, adcvalholder_t r, adcvalholder_t f)
{

#if SHORTSET8 || SHORTSET7 || SHORTSET_7L8C
	PRINTF("%s: L=%u(%u),C=%u(%u),ty=%u,fw=%u,ref=%u,swr=%u.%02u\n",
		title,
		(unsigned) logtable_ind [tunerind], (unsigned) tunerind,
		(unsigned) logtable_cap [tunercap], (unsigned) tunercap,
		(unsigned) tunertype,
		(unsigned) f,
		(unsigned) r,
		(unsigned) (swr + TUS_SWRMIN) / 100,
		(unsigned) (swr + TUS_SWRMIN) % 100);
#else /* SHORTSET8 || SHORTSET7 */
	PRINTF("%s: L=%u,C=%u,ty=%u,fw=%u,ref=%u,swr=%u.%02u\n",
		title,
		(unsigned) tunerind, (unsigned) tunercap, (unsigned) tunertype,
		(unsigned) f,
		(unsigned) r,
		(unsigned) (swr + TUS_SWRMIN) / 100,
		(unsigned) (swr + TUS_SWRMIN) % 100);
#endif /* SHORTSET8 || SHORTSET7 */

}

uint_fast16_t tuner_get_swr(const char * title, uint_fast16_t fullscale, adcvalholder_t * pr, adcvalholder_t * pf)
{
	adcvalholder_t r;
	adcvalholder_t f;
	const uint_fast16_t swr = tuner_get_swr0(fullscale, & r, & f);

	* pr = r;
	* pf = f;
	printtunerstate(title, swr, r, f);
	return swr;
}

// Если прервана настройка - возврат не-0
static uint_fast8_t tuneabort(void)
{
	uint_fast8_t kbch, kbready;

	// todo: не работает на дисплеях с off screen composition.
	// счетчик перебора сбрасывается в 0 - и до обновления экрана дело не доходит.
	display2_redrawbarstimed(1, 0, NULL);	/* обновление динамической части отображения - обновление S-метра или SWR-метра и volt-метра. */

	processmessages(& kbch, & kbready, 0, NULL);
	if (kbready != 0)
	{
		switch (kbch)
		{
		case KBD_CODE_ATUBYPASS:
		case KBD_CODE_ATUSTART:
			return 1;
		}
	}
	return 0;
}

// Перебор значений L в поиске минимума SWR
// Если прервана настройка - возврат не-0
static uint_fast8_t scanminLk(tus_t * tus, uint_fast8_t addsteps)
{
	uint_fast8_t bestswrvalid = 0;
	uint_fast8_t a = 1;	/* чтобы не ругался компилятор */

	for (tunerind = LMIN; tunerind <= LMAX; ++ tunerind)
	{
		if (tuneabort())
			return 1;
		updateboard_tuner();
		tuner_waitadc();
		adcvalholder_t r;
		adcvalholder_t f;
		const uint_fast16_t swr = tuner_get_swr("scanminLk", TUS_SWRMAX, & r, & f);

		if ((bestswrvalid == 0) || (tus->swr > swr))
		{
			// Измерений ещё небыло
			tus->swr = swr;
			tus->tunerind = tunerind;
			tus->r = r;
			tus->f = f;
			bestswrvalid = 1;
			a = addsteps;
			PRINTF("scanminLk: best ty=%u, L=%u, C=%u\n", tunertype, tunerind, tunercap);
		}
		else
		{
			if (tus->swr < swr && a -- == 0)
			{
				break;
			}
		}
	}
	tunerind = tus->tunerind;	// лучшее запомненное
	return 0;
}

// Перебор значений C в поиске минимума SWR
// Если прервана настройка - возврат не-0
static uint_fast8_t scanminCk(tus_t * tus, uint_fast8_t addsteps)
{
	uint_fast8_t bestswrvalid = 0;
	uint_fast8_t a = 1;	/* чтобы не ругался компилятор */

	for (tunercap = CMIN; tunercap <= CMAX; ++ tunercap)
	{
		if (tuneabort())
			return 1;
		updateboard_tuner();
		tuner_waitadc();
		adcvalholder_t r;
		adcvalholder_t f;
		const uint_fast16_t swr = tuner_get_swr("scanminCk", TUS_SWRMAX, & r, & f);

		if ((bestswrvalid == 0) || (tus->swr > swr))
		{
			// Измерений ещё небыло
			tus->swr = swr;
			tus->tunercap = tunercap;
			tus->r = r;
			tus->f = f;
			bestswrvalid = 1;
			a = addsteps;
			PRINTF("scanminCk: best ty=%u, L=%u, C=%u\n", tunertype, tunerind, tunercap);
		}
		else
		{
			if (tus->swr < swr && a -- == 0)
			{
				break;
			}
		}
	}
	tunercap = tus->tunercap;	// лучшее запомненное
	return 0;
}

static uint_fast8_t findbestswr(const tus_t * v, uint_fast8_t n)
{
	uint_fast8_t i;
	uint_fast8_t best = 0;
	if (n < 2)
		return best;
	for (i = 1; i < n; ++ i)
	{
		if (v [i].swr < v [best].swr)
			best = i;
	}
	return best;
}

static void savetunerallstate(uint_fast8_t bi, vindex_t b)
{

	save_i8(offsetof(struct nvmap, bands[b].tunercap), tunercap);
	save_i8(offsetof(struct nvmap, bands[b].tunerind), tunerind);
	save_i8(offsetof(struct nvmap, bands[b].tunertype), tunertype);
	save_i8(offsetof(struct nvmap, bands[b].tunerwork), tunerwork);

	vindex_t bn = getfreqband(gfreqs [bi]);		/* определяем по частоте, в каком диапазоне находимся */
	save_i8(offsetof(struct nvmap, bands[bn].tunercap), tunercap);
	save_i8(offsetof(struct nvmap, bands[bn].tunerind), tunerind);
	save_i8(offsetof(struct nvmap, bands[bn].tunertype), tunertype);
	save_i8(offsetof(struct nvmap, bands[bn].tunerwork), tunerwork);

}

/* отсюда не возвращаемся пока не настроится тюнер */
static void auto_tune(void)
{	
	unsigned ndummies;
	const uint_fast8_t tx = 1;
	tus_t statuses [KSCH_COUNT];
	unsigned cshindex;
	const uint_fast8_t bi = getbankindex_tx(tx);
	const vindex_t b = getvfoindex(bi);

#if SHORTSET7 || SHORTSET8 || SHORTSET_7L8C
	const uint_fast8_t addstepsLk = 3;
	const uint_fast8_t addstepsCk = 3;
#else /* SHORTSET7 || SHORTSET8 || SHORTSET_7L8C */
	const uint_fast8_t addstepsLk = 15;
	const uint_fast8_t addstepsCk = 15;
#endif /* SHORTSET7 || SHORTSET8 || SHORTSET_7L8C */

	PRINTF(PSTR("auto_tune start\n"));
	for (ndummies = 5; ndummies --; )
	{
		if (tuneabort())
			goto aborted;
		local_delay_ms(50);
		adcvalholder_t r;
		adcvalholder_t f;
		const uint_fast16_t swr = tuner_get_swr("dummy", TUS_SWRMAX, & r, & f);
		tuner_waitadc();
	}
	// Попытка согласовать двумя схемами
	for (tunertype = 0; tunertype < KSCH_COUNT; ++ tunertype)
	{
		statuses [tunertype].tunertype = tunertype;
		tunerind = LMIN;
		if (tunertype == 0)
		{
			PRINTF("tuner: ty=%u, scan capacitors\n", (unsigned) tunertype);
			if (scanminCk(& statuses [tunertype], addstepsCk) != 0)
				goto aborted;
			PRINTF("scanminCk finish: C=%u\n", tunercap);
			updateboard_tuner();
		}
		else
		{
			statuses [1] = statuses [0];
			statuses [1].tunertype = tunertype;
		}

		// проверка - а может уже нашли подходяшее согласование?
		////if (statuses [tunertype].swr <= TUS_SWR1p1)
		////	goto NoMoreTune;

		PRINTF("tuner: ty=%u, scan inductors\n", (unsigned) tunertype);
		if (scanminLk(& statuses [tunertype], addstepsLk) != 0)
			goto aborted;
		PRINTF("scanminLk finish: L=%u\n", tunerind);
		updateboard_tuner();

		// проверка - а может уже нашли подходяшее согласование?
		////if (statuses [tunertype].swr <= TUS_SWR1p1)
		////	goto NoMoreTune;
	}
	// Выбираем наилучший результат согласования
	cshindex = findbestswr(statuses, sizeof statuses / sizeof statuses [0]);
	PRINTF(PSTR("auto_tune loop done\n"));
	printtunerstate("Selected 1", statuses [cshindex].swr, statuses [cshindex].r, statuses [cshindex].f);
	tunertype = statuses [cshindex].tunertype;
	tunerind = statuses [cshindex].tunerind;
	tunercap = statuses [cshindex].tunercap;
	if (scanminCk(& statuses [cshindex], addstepsCk) != 0)
		goto aborted;
	printtunerstate("Selected 2", statuses [cshindex].swr, statuses [cshindex].r, statuses [cshindex].f);
	// Устанавливаем аппаратуру в состояние при лучшем результате
	tunertype = statuses [cshindex].tunertype;
	tunerind = statuses [cshindex].tunerind;
	tunercap = statuses [cshindex].tunercap;
	updateboard_tuner();
	PRINTF(PSTR("auto_tune stop\n"));
////NoMoreTune:
	savetunerallstate(bi, b);
	return;

aborted:
	tunerwork = 1; //loadvfy8up(offsetof(struct nvmap, bands[b].tunerwork), 0, 1, tunerwork);	// всегда единица (сохранилось в начале настройки)
	tunercap = loadvfy8up(offsetof(struct nvmap, bands[b].tunercap), CMIN, CMAX, tunercap);
	tunerind = loadvfy8up(offsetof(struct nvmap, bands[b].tunerind), LMIN, LMAX, tunerind);
	tunertype = loadvfy8up(offsetof(struct nvmap, bands[b].tunertype), 0, KSCH_COUNT - 1, tunertype);
	updateboard_tuner();
	return;
}
#endif /* WITHAUTOTUNER */

/* получение следующего числа в диапазоне low..high с "заворотом" */
/* используется при переборе режимов кнопками */
uint_fast8_t
//NOINLINEAT
calc_next(uint_fast8_t v, uint_fast8_t low, uint_fast8_t high)
{
	return (v < low || v >= high) ? low : (v + 1);
}

/* получение предыдущего числа в диапазоне low..high с "заворотом" */
/* используется при переборе режимов кнопками */
static uint_fast8_t 
//NOINLINEAT
calc_prev(uint_fast8_t v, uint_fast8_t low, uint_fast8_t high)
{
	return (v <= low || v > high) ? high : (v - 1);
}


/* получение предыдущего или следующего числа в диапазоне low..high с "заворотом" */
/* используется при переборе режимов кнопками */
static uint_fast8_t
//NOINLINEAT
calc_dir(uint_fast8_t reverse, uint_fast8_t v, uint_fast8_t low, uint_fast8_t high)
{
	return reverse ? calc_prev(v, low, high) : calc_next(v, low, high);
}

/* выравнивание после перехода на следующую частоту, кратную указаному шагу */
/* freq - новая частота, step - шаг */
static uint_fast32_t 
NOINLINEAT
nextfreq(uint_fast32_t oldfreq, uint_fast32_t freq, 
							   uint_fast32_t step, uint_fast32_t top)
{
	//const ldiv_t r = ldiv(freq, step);
	//freq -= r.rem;
	freq -= (freq % step);

	if (oldfreq > freq || freq >= top)
		return top - step;

	return freq;
}

/* выравнивание после перехода на предидущую частоту, кратную указаному шагу */
/* freq - новая частота, step - шаг */
static uint_fast32_t 
NOINLINEAT
prevfreq(uint_fast32_t oldfreq, uint_fast32_t freq, 
							   uint_fast32_t step, uint_fast32_t bottom)
{
	const uint_fast32_t rem = (freq % step);
	if (rem != 0)
		freq += (step - rem);
	//const ldiv_t r = ldiv(freq, step);

	//if (r.rem != 0)
	//	freq += (step - r.rem);

	if (oldfreq < freq || freq < bottom)
		return bottom;


	return freq;
}

#if defined(NVRAM_TYPE) && (NVRAM_TYPE != NVRAM_TYPE_NOTHING)

static void fillrelaxedsign(uint8_t * tsign)
{
	ASSERT(sizeof nvramsign == 8);

	memset(tsign, 0xe5, 8);
	tsign [0] = (uint8_t) (sizeof (struct nvmap) >> 24);
	tsign [1] = (uint8_t) (sizeof (struct nvmap) >> 16);
	tsign [2] = (uint8_t) (sizeof (struct nvmap) >> 8);
	tsign [3] = (uint8_t) (sizeof (struct nvmap) >> 0);
	tsign [4] = (uint8_t) (~ sizeof (struct nvmap) >> 24);
	tsign [5] = (uint8_t) (~ sizeof (struct nvmap) >> 16);
	tsign [6] = (uint8_t) (~ sizeof (struct nvmap) >> 8);
	tsign [7] = (uint8_t) (~ sizeof (struct nvmap) >> 0);
}

/* проверка совпадения сигнатуры в энергонезависимой памяти.
   0 - совпадает,
	 не-0 - отличается
	 */
static uint_fast8_t 
//NOINLINEAT
verifynvramsignature(void)
{
#if WITHKEEPNVRAM

	/* ослабить проверку совпадения версий прошивок для стирания NVRAM */
	uint8_t tsign [sizeof nvramsign];
	uint_fast8_t i;

	fillrelaxedsign(tsign);
	for (i = 0; i < (sizeof nvramsign - 1); ++ i)
	{
		const char c = restore_i8(RMT_SIGNATURE_BASE(i));
		if (c != tsign [i])
		{
			return 1;	/* есть отличие */
		}
	}
	return 0;	/* сигнатура совпадает */

#else /* WITHKEEPNVRAM */

	uint_fast8_t i;
	for (i = 0; i < (sizeof nvramsign - 1); ++ i)
	{
		const char c = restore_i8(RMT_SIGNATURE_BASE(i));
		if (c != nvramsign [i])
		{
			return 1;	/* есть отличие */
		}
	}		
	return 0;	/* сигнатура совпадает */

#endif /* WITHKEEPNVRAM */
}

static void 
//NOINLINEAT
initnvramsignature(void)
{
#if WITHKEEPNVRAM
	/* ослабить проверку совпадения версий прошивок для стирания NVRAM */
	uint8_t tsign [sizeof nvramsign];
	uint_fast8_t i;

	fillrelaxedsign(tsign);

	for (i = 0; i < sizeof nvramsign - 1; ++ i)
	{
		save_i8(RMT_SIGNATURE_BASE(i), tsign [i]);
	}
#else /* WITHKEEPNVRAM */
	uint_fast8_t i;

	for (i = 0; i < sizeof nvramsign - 1; ++ i)
	{
		save_i8(RMT_SIGNATURE_BASE(i), nvramsign [i]);
	}
#endif /* WITHKEEPNVRAM */
}

/* проверка совпадения тестовой сигнатуры в энергонезависимой памяти.
   0 - совпадает,
	 не-0 - отличается
	 */
static uint_fast8_t 
//NOINLINEAT
verifynvrampattern(void)
{
	//const uint_fast32_t c32a = restore_i32(RMT_SIGNATURE_BASE(0));
	//const uint_fast32_t c32b = restore_i32(RMT_SIGNATURE_BASE(4));
	//PRINTF(PSTR("verifynvrampattern: c32a=%08lX c32b=%08lX\n"), c32a, c32b);
	uint_fast8_t i;
	for (i = 0; i < (sizeof nvramsign - 1); ++ i)
	{
		const char c = restore_i8(RMT_SIGNATURE_BASE(i));
		//PRINTF(PSTR("verifynvrampattern: pattern[%u]=%02X, mem=%02X\n"), i, (unsigned char) nvrampattern [i], (unsigned char) c);
		if (c != nvrampattern [i])
		{
			return 1;	/* есть отличие */
		}
	}		
	return 0;	/* сигнатура совпадает */
}

static void 
//NOINLINEAT
initnvrampattern(void)
{
	uint_fast8_t i;

	for (i = 0; i < sizeof nvramsign - 1; ++ i)
	{
		save_i8(RMT_SIGNATURE_BASE(i), nvrampattern [i]);
	}
}

#endif /* defined(NVRAM_TYPE) && (NVRAM_TYPE != NVRAM_TYPE_NOTHING) */

/* описатели режимов. код режима - индекс в этом массиве
*/
typedef struct submodeprops_tag
{
	uint_least8_t qflags;		/* флаги подрежима */
	uint_least8_t mode;			/* код семейства режимов */
	uint_least8_t qcatmodecode;		/* Kenwood cat mode code */
	uint_fast16_t qcatwidth;		/* полоса режима, рапортуемая через CAT */
									// The FW command cannot be used in SSB or AM mode (an error tone will sound).

	char qlabel [4];
} submodeprops_t;

/* флаги подрежима */
#define QMODEF_HARDLSB	0x01		// для режимов приема на нижней боковой	 (CWR, LSB)
#define QMODEF_SMARTLSB	0x02	// для режимов приема на нижней боковой	 (CWR, LSB) в зависимости от частоты

// Порядок элементов в этом массиве должен совпадать с элементми enum,
// в котором определён SUBMODE_COUNT
static const FLASHMEM submodeprops_t submodes [SUBMODE_COUNT] =
{
#if WITHMODESETSMART
	/* SUBMODE_SSBSMART */
	{	
		QMODEF_SMARTLSB,
		MODE_SSB, 	/* индекс семейства режимов */
		2,			/* Kenwood cat mode code */
		0,			/* полоса фильтра режима, возвращаемая через CAT */
		"SSB", 
	},
	/* SUBMODE_CWSMART */
	{	
		QMODEF_SMARTLSB,
		MODE_CW, 	/* индекс семейства режимов */
		3,			/* Kenwood cat mode code */
		500,		/* полоса фильтра режима, возвращаемая через CAT */
		"CW ", 
	},
	/* SUBMODE_CWZSMART */
	{	
		QMODEF_SMARTLSB,
		MODE_CWZ, 	/* индекс семейства режимов */
		3,			/* Kenwood cat mode code */
		500,		/* полоса фильтра режима, возвращаемая через CAT */
		"CWZ", 
	},
	/* SUBMODE_DIGISMART */
	{	
		QMODEF_SMARTLSB,
		MODE_DIGI, 	/* индекс семейства режимов */
		9,			/* Kenwood cat mode code */
		0,			/* полоса фильтра режима, возвращаемая через CAT */
		"DIG", 
	},
#else /* WITHMODESETSMART */
	/* SUBMODE_USB */
	{	
		0,
		MODE_SSB, 	/* индекс семейства режимов */
		2,			/* Kenwood cat mode code */
		0,			/* полоса фильтра режима, возвращаемая через CAT */
		"USB", 
	},
	/* SUBMODE_LSB */
	{	
		QMODEF_HARDLSB,
		MODE_SSB, 	/* индекс семейства режимов */
		1,			/* Kenwood cat mode code */
		0,			/* полоса фильтра режима, возвращаемая через CAT */
		"LSB", 
	},
	/* SUBMODE_CW */
	{	
		0,
		MODE_CW, 	/* индекс семейства режимов */
		3,			/* Kenwood cat mode code */
		500,		/* полоса фильтра режима, возвращаемая через CAT */
		"CW ", 
	},
	/* SUBMODE_CWR */
	{	
		QMODEF_HARDLSB,
		MODE_CW, 	/* индекс семейства режимов */
		7,			/* Kenwood cat mode code */
		500,		/* полоса фильтра режима, возвращаемая через CAT */
		"CWR", 
	},
	/* SUBMODE_CWZ */
	{	
		QMODEF_SMARTLSB,
		MODE_CWZ, 	/* индекс семейства режимов */
		UINT8_MAX,	/* Kenwood cat mode code - на этот режим через CAT не переключиться */
		0,			/* полоса фильтра режима, возвращаемая через CAT */
	  	"CWZ", 
	},
	/* SUBMODE_AM */
	{	
		QMODEF_SMARTLSB,
		MODE_AM, 	/* индекс семейства режимов */
		5,			/* Kenwood cat mode code */
		0,			/* полоса фильтра режима, возвращаемая через CAT */
		"AM ",
	},
	#if WITHSAM
	/* SUBMODE_SAM */
	{	
		QMODEF_SMARTLSB,
		MODE_SAM, 	/* индекс семейства режимов */
		UINT8_MAX,	/* Kenwood cat mode code */
		0,			/* полоса фильтра режима, возвращаемая через CAT */
		"SAM",
	},
	#endif /* WITHSAM */
	/* SUBMODE_DRM */
	{	
		QMODEF_SMARTLSB,
		MODE_DRM, 	/* индекс семейства режимов */
		UINT8_MAX,	/* Kenwood cat mode code - на этот режим через CAT не переключиться */
		0,			/* полоса фильтра режима, возвращаемая через CAT */
		"DRM", 
	},
	/* SUBMODE_DGL */
	{	
		QMODEF_HARDLSB,
		MODE_DIGI, 	/* индекс семейства режимов */
		9,			/* Kenwood cat mode code */
		0,			/* полоса фильтра режима, возвращаемая через CAT */
		"DGL", 
	},
	/* SUBMODE_DGU */
	{	
		QMODEF_SMARTLSB,
		MODE_DIGI, 	/* индекс семейства режимов */
		6,			/* Kenwood cat mode code */
		0,			/* полоса фильтра режима, возвращаемая через CAT */
		"DGU", 
	},
#if WITHMODESETFULLNFM || WITHMODESETMIXONLY3NFM || WITHMODESETFULLNFMWFM	// чтобы через CAT не включился этот режтим - если нет - в комментарии.
	/* SUBMODE_NFM */
	{	
		QMODEF_SMARTLSB,
		MODE_NFM, 	/* индекс семейства режимов */
		4,			/* Kenwood cat mode code */
		0,			/* полоса фильтра режима, возвращаемая через CAT */
		"NFM",
	},
#endif /* WITHMODESETFULLNFM || WITHMODESETMIXONLY3NFM || WITHMODESETFULLNFMWFM  */
#if WITHWFM || WITHMODESETFULLNFMWFM
	/* SUBMODE_WFM */
	{	
		QMODEF_SMARTLSB,
		MODE_WFM, 	/* индекс семейства режимов */
		8,			/* Kenwood cat mode code - придуман мною. В описаниях такого нет. */
		0,			/* полоса фильтра режима, возвращаемая через CAT */
		"WFM",
	},
#endif /* WITHWFM || WITHMODESETFULLNFMWFM */
#if WITHMODEM
	/* SUBMODE_PSK */
	{	
		0,
		MODE_MODEM, /* индекс семейства режимов */
		UINT8_MAX,	/* Kenwood cat mode code */
		500,		/* полоса фильтра режима, возвращаемая через CAT */
		"PSK", 
	},
#endif /* WITHMODEM */
	/* SUBMODE_ISB */
	{	
		0,
		MODE_ISB, 	/* индекс семейства режимов */
		UINT8_MAX,	/* Kenwood cat mode code - на этот режим через CAT не переключиться */
		0,			/* полоса фильтра режима, возвращаемая через CAT */
	  	"ISB", 
	},
#if WITHFREEDV
	/* SUBMODE_FDV */
	{	
		0,
		MODE_FREEDV,/* индекс семейства режимов */
		UINT8_MAX,	/* Kenwood cat mode code */
		0,			/* полоса фильтра режима, возвращаемая через CAT */
		"FDV", 
	},
#endif /* WITHFREEDV */
#if WITHRTTY
	/* SUBMODE_RTTY */
	{
		0,
		MODE_RTTY,/* индекс семейства режимов */
		UINT8_MAX,	/* Kenwood cat mode code */
		0,			/* полоса фильтра режима, возвращаемая через CAT */
		"TTY",
	},
#endif /* WITHFREEDV */
#endif /* WITHMODESETSMART */
};

// Возвращает признак работы в LSB данного режима.
static uint_fast8_t
getsubmodelsb(
	uint_fast8_t submode,
	uint_fast8_t forcelsb
	)
{
	const uint_fast8_t flags = submodes [submode].qflags;
#if WITHMODESETSMART
	if ((flags & QMODEF_SMARTLSB) != 0)
		return forcelsb; 
#endif /* WITHMODESETSMART */
	return (flags & QMODEF_HARDLSB) != 0;
}

#if WITHCAT
enum 
{ 
	CATSTATE_HALTED,
	CATSTATE_WAITPARAM,		/* состояние ожидания приёма параметра (кончается по приёму ';' */
#if WITHCATEXT && WITHELKEY
	CATSTATE_WAITMORSE,		/* состояние ожидания приёма символа за KY */
#endif /* WITHCATEXT && WITHELKEY */
	CATSTATE_WAITCOMMAND1,	/* состояние ожидания приёма первого байта команды */
	CATSTATE_WAITCOMMAND2	/* состояние ожидания приёма второго байта команды */
};

enum 
{ 
	CATSTATEO_HALTED,
	CATSTATEO_SENDREADY,
	CATSTATEO_WAITSENDREPLAY	/* обработчик поставил ответ на передачу - пока не закончится */
};

#endif /* WITHCAT */

#if WITHCAT

/* поиск по кенвудовскому номеру */
static uint_fast8_t 
findkenwoodsubmode(uint_fast8_t catmode, uint_fast8_t defsubmode)
{
	uint_fast8_t submode;

	for (submode = 0; submode < (sizeof submodes / sizeof submodes [0]); submode ++)
	{
		const uint_fast8_t v = submodes [submode].qcatmodecode;
		if (v == UINT8_MAX)
			continue;		/* на этот режим через CAT не переключиться */
		if (v == catmode)
			return submode;
	}
	return defsubmode;
}

// Эта команда протокола CAT не возвращает полосу пропускания для SSB режимов.
// TODO: доделать для реальной работы
static uint_fast16_t
getkenwoodfw(
	uint_fast8_t submode,	// current submode
	uint_fast8_t fi)	// filter index
{
	return submodes [submode].qcatwidth;
}

static uint_fast8_t cat_getstateout(void);
static void cat_answer_forming(void);

#endif /* WITHCAT */

#if WITHLCDBACKLIGHT || WITHKBDBACKLIGHT

static uint_fast8_t gdimmtime;	/* количество секунд до гашения индикатора, 0 - не гасим. Регулируется из меню. */
static uint_fast8_t dimmcount;
static uint_fast8_t dimmflagch;	/* не-0: изменилось состояние dimmflag */

#endif /* WITHLCDBACKLIGHT || WITHKBDBACKLIGHT */

#if WITHFANTIMER

#define FANPATIMEMAX	240
static uint_fast8_t gfanpatime = 15;	/* количество секунд до выключения вентилятора после передачи, 0 - не гасим. Регулируется из меню. */
static uint_fast8_t fanpacount = FANPATIMEMAX;
static uint_fast8_t fanpaflag = 1;	/* не-0: выключить ыентилятор. */
static uint_fast8_t fanpaflagch;	/* не-0: изменилось состояние fanpaflag */
	#if WITHFANPWM
		static uint_fast16_t gfanpapwm = 255;
	#endif /* WITHFANPWM */

#endif /* WITHFANTIMER */


#if WITHSLEEPTIMER

static uint_fast8_t gsleeptime;	/* количество минут до выключения, 0 - не выключаем. Регулируется из меню. */
static uint_fast16_t sleepcount;	/* счетчик в секундах */
static uint_fast8_t sleepflagch;	/* не-0: изменилось состояние sleepflag */

#else
	//enum { sleepflag = 0 };

#endif /* WITHSLEEPTIMER */


/* Произошла какая-то активность со стороны пользователя, зажигаем подсветку.
	если было состояние "сна" - возвращаем 1 */
static uint_fast8_t
board_wakeup(void)
{
	uint_fast8_t r = 0;
#if WITHLCDBACKLIGHT || WITHKBDBACKLIGHT
	dimmcount = 0;		/* счётчик времени неактивности */
	if (dimmflag != 0)	
	{
		dimmflag = 0;
		dimmflagch = 1;
		r = 1;
	}
#endif /* WITHLCDBACKLIGHT || WITHKBDBACKLIGHT */
#if WITHSLEEPTIMER
	sleepcount = 0;		/* счётчик времени неактивности */
	if (sleepflag != 0)	
	{
		sleepflag = 0;
		sleepflagch = 1;
		r = 1;
	}
#endif /* WITHSLEEPTIMER */
	return r;
}

/* обработка сообщений от уровня обработчиков прерываний к user-level функциям. */
/* обработка только клавиатурных сообщений - для начальной инициализации, например. */
uint_fast8_t 
//NOINLINEAT
kbd_scan(uint_fast8_t * key)
{
	uint_fast8_t f = 0;
	uint8_t * buff;
	switch (takemsgready_user(& buff))
	{
	case MSGT_EMPTY:
		return 0;

	case MSGT_KEYB:
		* key = buff [0];
		f = 1;
		break;

	default:
		break;
	}
	releasemsgbuffer_user(buff);
	return f;
}

/* получаем PBT offset для текущего режима работы */
/* TODO: сделать зависимым от текущего фильтра */
static int_fast16_t 
getpbt(
	const filter_t * workfilter,
	uint_fast8_t mode,		/* код режима работы */
	uint_fast8_t tx				/* признак передачи */
	)
{	
#if WITHPBT // && (LO3_SIDE != LOCODE_INVALID)
	if (tx || mdt [mode].wpbt == 0)
		return 0;
	return gpbtoffset.value + getpbtbase();	/* из индицируемого значения получить знаковое */

#else /* WITHPBT */
	(void) mode;
	(void) tx;
	return 0;
#endif /* WITHPBT */
}
/* получаем IF SHIFT offset для текущего режима работы */
/* TODO: сделать зависимым от текущего фильтра */
// Увеличение значения параметра смещает слышимую часть спектра в более высокие частоты
static int_fast16_t 
getifshift(
	const filter_t * workfilter,
	uint_fast8_t mode,		/* код режима работы */
	uint_fast8_t tx				/* признак передачи */
	)
{	
#if WITHIFSHIFT
	if (tx || mdt [mode].wifshift == 0)
		return 0;
	return ifshifoffset.value + getifshiftbase();	/* из индицируемого значения получить знаковое */

#else /* WITHIFSHIFT */

	(void) workfilter;
	(void) mode;
	(void) tx;
	return 0;

#endif /* WITHIFSHIFT */
}

static uint_fast32_t 
loadvfy32freq(
	vindex_t b		// band
	)
{
	const vindex_t b0 = (b >= MBANDS_BASE) ? VFOS_BASE : b;
	return loadvfy32(RMT_BFREQ_BASE(b), get_band_bottom(b0), get_band_top(b0), get_band_init(b0));
}

#if 0
static const uint_fast8_t ssb_steps10 [] = 
{
	1,	/* 10 Hz */
	5,	/* 50 Hz */
	10,	/* 100 Hz */
};
static const uint_fast8_t am_steps10 [] = 
{
	10,	/* 100 Hz */
	50,	/* 500 Hz */
	100,/* 1 kHz */
	125,/* 1.25 kHz */
	150,/* 1.5 kHz - for 9 kHz implementation */
	250,/* 2.5 kHz */
};
#endif

/* функция работает с кэшем параметров режима работы */
static const FLASHMEM struct modetempl *
NOINLINEAT
getmodetempl(uint_fast8_t submode)
{
	return & mdt [submodes [submode].mode];
}

#if WITHAMHIGHKBDADJ


/* Изменение верхнего среза полосы пропускания в установленном режиме */
static void 
uif_key_click_amfmbandpassup(void)
{
	const FLASHMEM struct modetempl * const pmodet = getmodetempl(gsubmode);
	const uint_fast8_t bwseti = pmodet->bwsetis [0];	// индекс банка полос пропускания для данного режима на приеме
	const uint_fast8_t pos = bwsetpos [bwseti];
	bwprop_t * const p = bwsetsc [bwseti].prop [pos];
	//if (p->type != BWSET_PAIR)
	//	return;

	switch (p->type)
	{
	case BWSET_SINGLE:
		p->left10_width10 = nextfreq(p->left10_width10, p->left10_width10 + p->limits->granulationleft, p->limits->granulationleft, p->limits->left10_width10_high + 1);
		save_i8(RMT_BWPROPSLEFT_BASE(p->bwpropi), p->left10_width10);	// верхний срез фильтра НЧ в сотнях герц
		updateboard(1, 0);
		break;

	default:
	case BWSET_PAIR:
		p->right100 = nextfreq(p->right100, p->right100 + p->limits->granulationright, p->limits->granulationright, p->limits->right100_high + 1);
		save_i8(RMT_BWPROPSRIGHT_BASE(p->bwpropi), p->right100);	// верхний срез фильтра НЧ в сотнях герц
		updateboard(1, 0);
		break;
	}

}

/* Изменение верхнего среза полосы пропускания в установленном режиме */
static void 
uif_key_click_amfmbandpassdown(void)
{
	const FLASHMEM struct modetempl * const pmodet = getmodetempl(gsubmode);
	const uint_fast8_t bwseti = pmodet->bwsetis [0];	// индекс банка полос пропускания для данного режима на приеме
	const uint_fast8_t pos = bwsetpos [bwseti];
	bwprop_t * const p = bwsetsc [bwseti].prop [pos];
	//if (p->type != BWSET_PAIR)
	//	return;

	switch (p->type)
	{
	case BWSET_SINGLE:
		p->left10_width10 = prevfreq(p->left10_width10, p->left10_width10 - p->limits->granulationleft, p->limits->granulationleft, p->limits->left10_width10_low);
		save_i8(RMT_BWPROPSLEFT_BASE(p->bwpropi), p->left10_width10);	// верхний срез фильтра НЧ в сотнях герц
		updateboard(1, 0);
		break;

	default:
	case BWSET_PAIR:
		p->right100 = prevfreq(p->right100, p->right100 - 1, p->limits->granulationright, p->limits->right100_low);
		save_i8(RMT_BWPROPSRIGHT_BASE(p->bwpropi), p->right100);	// верхний срез фильтра НЧ в сотнях герц
		updateboard(1, 0);
		break;
	}

}

/* текущее значение верхнего среза полосы пропускания в установленном режиме (в десятках герц) */
uint_fast8_t hamradio_get_amfm_highcut10_value(uint_fast8_t * flag)
{
	const FLASHMEM struct modetempl * const pmodet = getmodetempl(gsubmode);
	const uint_fast8_t bwseti = pmodet->bwsetis [0];	// индекс банка полос пропускания для данного режима на приеме
	const uint_fast8_t pos = bwsetpos [bwseti];
	bwprop_t * const p = bwsetsc [bwseti].prop [pos];

	switch (p->type)
	{
	case BWSET_SINGLE:
		* flag = 1;//p->type == BWSET_PAIR;
		return p->left10_width10;
		break;
	default:
	case BWSET_PAIR:
		* flag = 1;//p->type == BWSET_PAIR;
		return p->right100 * 10;
		break;
	}
}
#endif /* WITHAMHIGHKBDADJ */

// проверка, используется ли описатель диапазона с данным кодом в текущей конфигурации.
// Возврат 0 - не используется
static uint_fast8_t
//NOINLINEAT
existingband(
	uint_fast8_t b,	// код диапазона
	uint_fast8_t bandsetbcast
	)
{
	const uint_fast8_t bandset = get_band_bandset(b);

	if (get_band_bottom(b) >= TUNE_TOP || get_band_top(b) < TUNE_BOTTOM)
		return 0;

	switch (bandset)
	{
	default:
	case BANDSETF_ALL:
		return 1;		// используется всегда
	case BANDSETF_HAM:
		return ! bandsetbcast;
	case BANDSETF_HAMWARC:
		return bandsethamwarc && ! bandsetbcast;
	case BANDSETF_BCAST:
		return bandsetbcast;
	case BANDSETF_CB:
		return bandset11m;
#if TUNE_6MBAND
	case BANDSETF_6M:
		return bandset6m && ! bandsetbcast;		// используется или нет - определяется параметром
#endif /* TUNE_6MBAND */
#if TUNE_4MBAND
	case BANDSETF_4M:
		return bandset4m && ! bandsetbcast;		// используется или нет - определяется параметром
#endif /* TUNE_4MBAND */

	// 144 и 430 разрешаются одним пунктом в меню.
#if TUNE_2MBAND
	case BANDSETF_2M:
		return bandset2m && ! bandsetbcast;		// используется или нет - определяется параметром
#endif /* TUNE_2MBAND */
#if TUNE_07MBAND
	case BANDSETF_07M:
		return bandset2m && ! bandsetbcast;		// используется или нет - определяется параметром
#endif /* TUNE_2MBAND */
	}
}

static uint_fast8_t
//NOINLINEAT
existingbandsingle(
	uint_fast8_t b,	// код диапазона
	uint_fast8_t bandsetbcast
	)
{
	if (existingband(b, 0) && existingband(b, 1))
		return bandsetbcast;	// BANDSETF_ALL
	return existingband(b, bandsetbcast);
}

static void 
//NOINLINEAT
verifyband(const vindex_t b)
{
#if DEBUGEXT
	if (b == ((uint_fast8_t) - 1))
	{
		TP();
		display_at(0, 1, "band not found");
		for (;;)
			;
	}
#endif /* DEBUGEXT */
}


#if XVTR_NYQ1
// Частота из лююой зоны найквиста преобразуется в первую
static uint_fast32_t FQMODEL_TUNING_TO_NYQ1(uint_fast32_t f)
{
	uint_fast32_t f1 = (f % DUCDDC_FREQ);
	return f1 >= (DUCDDC_FREQ / 2) ? (DUCDDC_FREQ - f1) : f1;
}
#endif /* XVTR_NYQ1 */

// tuning frequency to band pass frequency adjust
static uint_fast32_t
nyquistadj(uint_fast32_t f)
{
#if 0//XVTR_R820T2
	reuturn R820T_IFFREQ;
#elif 0//XVTR_NYQ1
	return FQMODEL_TUNING_TO_NYQ1(f);
#else /* XVTR_NYQ1 */
	return f;
#endif /* XVTR_NYQ1 */
}

// tuning frequency to PA low pass frequency adjust
static uint_fast32_t
nyquistadj2(uint_fast32_t f)
{
#if 0//XVTR_R820T2
	reuturn R820T_IFFREQ;
#elif XVTR_NYQ1
	return FQMODEL_TUNING_TO_NYQ1(f);
#else /* XVTR_NYQ1 */
	return f;
#endif /* XVTR_NYQ1 */
}

// tuning frequency to external PA (ACC socket) frequency adjust - no correction
static uint_fast32_t
nyquistadj3(uint_fast32_t f)
{
	return f;
}

/* получить номер диапазона, в который попадает отображающаяся частота
  Если не принадлежит ни одному диапазону, возврат ((uint8_t)-1) */
static vindex_t 
getfreqband(const uint_fast32_t freq)
{
	vindex_t i;

	for (i = 0; i < (sizeof bandsmap / sizeof bandsmap [0]); ++ i)
	{
		if (! existingband(i, gbandsetbcast))	// диапазон в данной конфигурации не используется
			continue;
		if (get_band_bottom(i) <= freq && get_band_top(i) > freq)
			return i;
	}
	return ((vindex_t) - 1);
}

/* получить номер диапазона с большей частотой, на который переходить.
  Если нет подходящих, возврат low */
static vindex_t 
getnexthband(const uint_fast32_t freq)
{
	enum { LOW = 0, HIGH = HBANDS_COUNT - 1 };
	vindex_t i;

	for (i = 0; i < HBANDS_COUNT; ++ i)
	{
		if (! existingband(i, gbandsetbcast))	// диапазон в данной конфигурации не используется
			continue;
		if (get_band_top(i) > freq)
			return i;
	}
	return LOW;
}

/* получить номер любительского диспазона, следующего в группе. Если в группе больше нет ни одного диапазона,
 вернуть номер текущего.
 */
static vindex_t 
getnextbandingroup(const vindex_t b, const uint_fast8_t bandgroup)
{
	enum { LOW = 0, HIGH = HBANDS_COUNT - 1 };

	vindex_t i = b;		// начальный диапазон
	do
	{
		i = i == HIGH ? LOW : (i + 1);	// переход к следующему диапазону
		if (! existingband(i, gbandsetbcast))	// диапазон в данной конфигурации не используется
			continue;
		if (bandsmap [i].bandgroup == bandgroup)
			break;			// диапазон той же группы
	} while (i != b);
	return i;
}

/* получить номер диапазона с меньшей частотой, на который переходить.
  Если нет подходящих, возврат high */
static vindex_t 
getprevhband(const uint_fast32_t freq)
{
	vindex_t i;

	for (i = 0; i < HBANDS_COUNT; ++ i)
	{
		if (! existingband(i, gbandsetbcast))	// диапазон в данной конфигурации не используется
			continue;
		if (get_band_bottom(i) > freq)
		{
			i = (i == 0) ? HBANDS_COUNT : i;
			break;
		}
	}
	// возврат только допустимых диапазонов.
	do
		i = calc_prev(i, 0, HBANDS_COUNT - 1);
	while (! existingband(i, gbandsetbcast));
	return i;
}


static vindex_t
getnext_ham_band(
	vindex_t b,		// текущий диапазон
	const uint_fast32_t freq
	)
{
	uint_fast8_t xi;
	vindex_t xsel [XBANDS_COUNT];
	vindex_t xnext [XBANDS_COUNT];
	vindex_t xprev [XBANDS_COUNT];

	for (xi = 0; xi < XBANDS_COUNT; ++ xi)
	{
		const uint_fast32_t f = loadvfy32freq(XBANDS_BASE0 + xi);	// частота в обзорном диапазоне
		xsel [xi] = getfreqband(f);			// не принадлежит ли частота какому-то диапазону
		xnext [xi] = getnexthband(f);		// получить номер диапазона с большей частотой
		xprev [xi] = getprevhband(f);		// получить номер диапазона с меньшей частотой
	}
//	PRINTF("getnext_ham_band: b=%d(%d), xsel[0]=%d, xsel[1]=%d, xnext[0]=%d, xnext[1]=%d, xprev[0]=%d, xprev[1]=%d\n",
//			b, XBANDS_BASE0, xsel[0], xsel[1], xnext[0], xnext[1], xprev[0], xprev[1]);

	do
	{
		// 147M - 120 kHz
		// getnext_ham_band: b=37(36), xsel[0]=36, xsel[1]=37, xnext[0]=0, xnext[1]=0, xprev[0]=35, xprev[1]=35
		// getnext_ham_band: b=35(36), xsel[0]=36, xsel[1]=37, xnext[0]=0, xnext[1]=0, xprev[0]=35, xprev[1]=35
		//
		if (
			b == XBANDS_BASE1 &&
			xsel [0] == XBANDS_BASE0 &&
			xsel [1] == XBANDS_BASE1 &&
			xnext [0] == 0 &&
			xnext [1] == 0 &&
			xprev [0] == (HBANDS_COUNT - 1) &&
			xprev [1] == (HBANDS_COUNT - 1) &&
			1)
		{
			//TP();
			/* обработка ситуацию "из обзорного - в обзорный диапазон",
			если запомненная частота нового обзорного диапазона не попадает на выделенный диапазон */
			b = XBANDS_BASE0;
			continue;
		}
		// 120 kHz -> 225 kHz
		// getnext_ham_band: b=36(36,36), xsel[0]=36, xsel[1]=37, xnext[0]=0, xnext[1]=0, xprev[0]=35, xprev[1]=35
		//
		if (
			b == XBANDS_BASE0 &&
			xsel [0] == XBANDS_BASE0 &&
			xsel [1] == XBANDS_BASE1 &&
			xnext [0] == 0 &&
			xnext [1] == 0 &&
			xprev [0] == (HBANDS_COUNT - 1) &&
			xprev [1] == (HBANDS_COUNT - 1) &&
			1)
		{
			//TP();
			/* обработка ситуацию "из обзорного - в обзорный диапазон",
			если запомненная частота нового обзорного диапазона не попадает на выделенный диапазон */
			b = HBANDS_COUNT - 1;
			do
				b = calc_next(b, 0, HBANDS_COUNT - 1);
			while (! existingband(b, gbandsetbcast));
			continue;
		}
		if (
			b == XBANDS_BASE0 &&
			xprev [0] == xprev [1] &&
			xnext [0] == xnext [1] && 
			xsel [1] >= HBANDS_COUNT)
		{
			//TP();
			/* обработка ситуацию "из обзорного - в выделенный диапазон",
			если запомненная частота нового обзорного диапазона не попадает на выделенный диапазон */
			b = XBANDS_BASE1;
			continue;
		}
		if (b == xprev [0] && xsel [0] >= HBANDS_COUNT)
		{
			//TP();
			// текущая является предшествующей для xfreq [0]
			/* переходим в обзорный диапазон 0 */
			b = XBANDS_BASE0;
			continue;
		}
		if (b == xprev [1] && xsel [1] >= HBANDS_COUNT)
		{
			//TP();
			// текущая является предшествующей для xfreq [1]
			/* переходим в обзорный диапазон 1 */
			b = XBANDS_BASE1;
			continue;
		}
		if (b < HBANDS_COUNT)
		{
			//TP();
			/* текущая частота относится к любительским диапазонам */
			do
				b = calc_next(b, 0, HBANDS_COUNT - 1);
			while (! existingband(b, gbandsetbcast));
			continue;
		}
		if (b == XBANDS_BASE0)
		{
			//TP();
			// текущая частота - обзорный 0
			b = xnext [0];
			continue;
		}
		if (b == XBANDS_BASE1)
		{
			//TP();
			// текущая частота - обзорный 1
			b = xnext [1];
			continue;
		}

		//TP();
		b = getnexthband(freq);
	} while (0);
	//PRINTF("exit b=%d\n", b);
	return b;
}


static vindex_t
getprev_ham_band(
	vindex_t b,		// текущий диапазон
	const uint_fast32_t freq
	)
{
	uint_fast8_t xi;
	vindex_t xsel [XBANDS_COUNT];
	vindex_t xnext [XBANDS_COUNT];
	vindex_t xprev [XBANDS_COUNT];

	for (xi = 0; xi < XBANDS_COUNT; ++ xi)
	{
		const uint_fast32_t f = loadvfy32freq(XBANDS_BASE0 + xi);	// частота в обзорном диапазоне
		xsel [xi] = getfreqband(f);			// не принадлежит ли частота какому-то диапазону
		xnext [xi] = getnexthband(f);		// получить номер диапазона с большей частотой
		xprev [xi] = getprevhband(f);		// получить номер диапазона с меньшей частотой
	}
//	PRINTF("getprev_ham_band: b=%d(%d), xsel[0]=%d, xsel[1]=%d, xnext[0]=%d, xnext[1]=%d, xprev[0]=%d, xprev[1]=%d\n",
//			b, XBANDS_BASE0, xsel[0], xsel[1], xnext[0], xnext[1], xprev[0], xprev[1]);

	do
	{
		// 120 kHz -> 147 MHz
		// getprev_ham_band: b=36(36), xsel[0]=36, xsel[1]=37, xnext[0]=0, xnext[1]=0, xprev[0]=35, xprev[1]=35
		//
		if (
			b == XBANDS_BASE0 &&
			xsel [0] == XBANDS_BASE0 &&
			xsel [1] == XBANDS_BASE1 &&
			xnext [0] == 0 &&
			xnext [1] == 0 &&
			xprev [0] == (HBANDS_COUNT - 1) &&
			xprev [1] == (HBANDS_COUNT - 1) &&
			1)
		{
			//TP();
			/* обработка ситуацию "из обзорного - в обзорный диапазон",
			если запомненная частота нового обзорного диапазона не попадает на выделенный диапазон */
			b = XBANDS_BASE1;
			continue;
		}
		// 147 MHz -> 145 MHz
		// getnext_ham_band: b=36(36,36), xsel[0]=36, xsel[1]=37, xnext[0]=0, xnext[1]=0, xprev[0]=35, xprev[1]=35
		//
		if (
			b == XBANDS_BASE1 &&
			xsel [0] == XBANDS_BASE0 &&
			xsel [1] == XBANDS_BASE1 &&
			xnext [0] == 0 &&
			xnext [1] == 0 &&
			xprev [0] == (HBANDS_COUNT - 1) &&
			xprev [1] == (HBANDS_COUNT - 1) &&
			1)
		{
			//TP();
			/* обработка ситуацию "из обзорного - в выделенный диапазон",
			если запомненная частота нового обзорного диапазона не попадает на выделенный диапазон */
			b = 0;
			do
				b = calc_prev(b, 0, HBANDS_COUNT - 1);
			while (! existingband(b, gbandsetbcast));
			continue;
			continue;
		}
		if (
			b == XBANDS_BASE1 && 
			xprev [0] == xprev [1] && 
			xnext [0] == xnext [1] && 
			xsel [0] >= HBANDS_COUNT
			)
		{
			//TP();
			/* обработка ситуацию "из обзорного - в обзорный диапазон",
			если запомненная частота нового обзорного диапазона не попадает на выделенный диапазон */
			b = XBANDS_BASE0;
			continue;

		}
		if (b == xnext [0] && xsel [0] >= HBANDS_COUNT)
		{
			//TP();
			/* переходим в обзорный диапазон 0 */
			b = XBANDS_BASE0;
			continue;
		}
		if (b == xnext [1] && xsel [1] >= HBANDS_COUNT)
		{
			//TP();
			/* переходим в обзорный диапазон 1 */
			b = XBANDS_BASE1;
			continue;
		}
		if (b < HBANDS_COUNT)
		{
			//TP();
			/* текущая частота относится к любительским диапазонам */
			do
				b = calc_prev(b, 0, HBANDS_COUNT - 1);
			while (! existingband(b, gbandsetbcast));
			continue;
		}
		if (b == (XBANDS_BASE0))
		{
			//TP();
			// текущая частота - обзорный 0
			b = xprev [0];
			continue;
		}
		if (b == (XBANDS_BASE1))
		{
			//TP();
			// текущая частота - обзорный 1
			b = xprev [1];
			continue;
		}

		//TP();
		b = getprevhband(freq);
	} while (0);
	//PRINTF("exit b=%d\n", b);
	return b;
}

static uint_fast8_t
getbankindex_raw(const uint_fast8_t n)
{
	ASSERT(n < 2);
	return (gvfoab + n) % 2;
}

// программирование трактов для двойного приема
static uint_fast8_t
getbankindex_pathi(const uint_fast8_t pathi)
{
	ASSERT(pathi < 2);

#if WITHSPLIT || WITHSPLITEX

	switch (gsplitmode)	/* (vfo/vfoa/vfob/mem) */
	{
	case VFOMODES_VFOINIT:
		return getbankindex_raw(0);

	case VFOMODES_VFOSPLIT:	
		if (gtx == 0)
			return getbankindex_raw(pathi);
		else
			return getbankindex_tx(1);
	default:
		ASSERT(0);
		return 0;
	}

#else /* WITHSPLIT || WITHSPLITEX */
	return getbankindex_raw(0);
#endif /* WITHSPLIT || WITHSPLITEX */
}

static uint_fast8_t
getbankindex_ab(
	const uint_fast8_t ab	// 0: A, 1: B
	)
{
	ASSERT(ab < 2);
#if WITHSPLIT || WITHSPLITEX

	switch (gsplitmode)	/* (vfo/vfoa/vfob/mem) */
	{
	case VFOMODES_VFOINIT:
		return getbankindex_raw(0);

	case VFOMODES_VFOSPLIT:	
		return getbankindex_raw(ab);
	default:
		ASSERT(0);
		return getbankindex_raw(0);
	}

#else /* WITHSPLIT || WITHSPLITEX */
	return getbankindex_raw(0);
#endif /* WITHSPLIT || WITHSPLITEX */
}

static uint_fast8_t
getbankindex_tx(const uint_fast8_t tx)
{
	ASSERT(tx < 2);
#if WITHSPLIT || WITHSPLITEX

	switch (gsplitmode)	/* (vfo/vfoa/vfob/mem) */
	{
	case VFOMODES_VFOINIT:
		return getbankindex_raw(0);

	case VFOMODES_VFOSPLIT:	
		return getbankindex_raw(tx);
	default:
		ASSERT(0);
		return getbankindex_raw(0);
	}

#else /* WITHSPLIT || WITHSPLITEX */
	return getbankindex_raw(0);
#endif /* WITHSPLIT || WITHSPLITEX */
}

// получить bankindex для показа частоты или режима работы тракта на дисплее в полях A (0) и B (1)
static uint_fast8_t
//NOINLINEAT
getbankindex_ab_fordisplay(const uint_fast8_t ab)
{
	ASSERT(ab < 2);
#if WITHSPLIT
	return getbankindex_ab(gtx != ab);
#elif WITHSPLITEX
	return getbankindex_raw(gtx != ab);
#else /* WITHSPLIT */
	return getbankindex_raw(0);
#endif /* WITHSPLIT */
}

#if WITHUSEDUALWATCH

static uint_fast8_t
getactualmainsubrx(void)
{
#if WITHSPLIT || WITHSPLITEX

	switch (gsplitmode)
	{
	default:
		return BOARD_RXMAINSUB_A_A;

	case VFOMODES_VFOSPLIT:
		return mainsubrxmodes [mainsubrxmode].code;
	}
#else /* WITHSPLIT || WITHSPLITEX */

	return BOARD_RXMAINSUB_A_A;

#endif /* WITHSPLIT || WITHSPLITEX */
}

#endif /* WITHUSEDUALWATCH */

// VFO mode
// Через flag возвращается признак активного SPLIT (0/1)
const char * hamradio_get_vfomode3_value(uint_fast8_t * flag)
{
	static const char spl [] = "SPL";
	static const char sp3 [] = "   ";

	switch (gsplitmode)	/* (vfo/vfoa/vfob/mem) */
	{
	default:
	case VFOMODES_VFOINIT:	/* no SPLIT -  Обычная перестройка */
		* flag = 0;
		return sp3;
	case VFOMODES_VFOSPLIT:	
		* flag = 1;
		return spl;
		//return (gvfoab != tx) ? b : a;
	}
}

// VFO mode
const char * hamradio_get_vfomode5_value(uint_fast8_t * flag)
{
	static const char spl [] = "SPLIT";
	static const char sp5 [] = "     ";

	switch (gsplitmode)	/* (vfo/vfoa/vfob/mem) */
	{
	default:
	case VFOMODES_VFOINIT:	/* no SPLIT -  Обычная перестройка */
		* flag = 0;
		return sp5;
	case VFOMODES_VFOSPLIT:	
		* flag = 1;
		return spl;
		//return (gvfoab != tx) ? b : a;
	}
}

// Частота VFO A для маркировки файлов
uint_fast32_t hamradio_get_freq_rx(void)
{
	return gfreqs [getbankindex_tx(0)];
}

// Частота VFO A для отображения на дисплее
uint_fast32_t hamradio_get_freq_a(void)
{
	return gfreqs [getbankindex_ab_fordisplay(0)];		/* VFO A modifications */
}
// Частота VFO A для отображения на дисплее
uint_fast32_t hamradio_get_freq_pathi(uint_fast8_t pathi)
{
	return gfreqs [getbankindex_pathi(pathi)];
}
// SSB/CW/AM/FM/..
const FLASHMEM char * hamradio_get_mode_a_value_P(void)
{
	return submodes [getsubmode(getbankindex_ab_fordisplay(0))].qlabel;	/* VFO A modifications */
}

// SSB/CW/AM/FM/..	
const FLASHMEM char * hamradio_get_mode_b_value_P(void)
{
	return submodes [getsubmode(getbankindex_ab_fordisplay(1))].qlabel;	/* VFO B modifications */
}

// Частота VFO B для отображения на дисплее
uint_fast32_t hamradio_get_freq_b(void)
{
	return gfreqs [getbankindex_ab_fordisplay(1)];	/* VFO B modifications */
}

	enum { withonlybands = 0 };

#if WITHONLYBANDS
	#error Remove WITHONLYBANDS from configuration - not supported
#endif /* WITHONLYBANDS */

/* функция для выполнения условия перестройки только в любительских диапазонах */
static int_fast32_t
tune_top(const uint_fast8_t bi)
{
#if WITHONLYBANDS
	if (withonlybands != 0)
		return tune_top_active [bi];
#endif /* WITHONLYBANDS */
	return TUNE_TOP;
}

/* функция для выполнения условия перестройки только в любительских диапазонах */
static int_fast32_t
tune_bottom(const uint_fast8_t bi)
{
#if WITHONLYBANDS
	if (withonlybands != 0)
		return tune_bottom_active [bi];
#endif /* WITHONLYBANDS */
	return TUNE_BOTTOM;
}

/* скопировать теккущий банк в противоположный перед включением расстройки */
static void 
copybankstate(
	const uint_fast8_t sbi,	// source bank index
	const uint_fast8_t tbi,	// target bank index
	const int_fast32_t deltaf	// рассторйка передатчика для включения способом "auto split"
	)
{
	ASSERT(tbi != sbi);
	gfreqs [tbi] = vfy32up(gfreqs [sbi] + deltaf, tune_bottom(sbi), tune_top(sbi) - 1, gfreqs [sbi]);
	gmoderows [tbi] = gmoderows [sbi];
#if ! WITHONEATTONEAMP
	gpamps [tbi] = gpamps [sbi];
#endif /* ! WITHONEATTONEAMP */
	gatts [tbi] = gatts [sbi];
#if  WITHANTSELECTRX
	grxantennas [tbi] = grxantennas [sbi];
	gantennas [tbi] = gantennas [sbi];
#elif WITHANTSELECT || WITHANTSELECT2
	gantennas [tbi] = gantennas [sbi];
#endif /* WITHANTSELECT || WITHANTSELECTRX */

	memcpy(gmodecolmaps [tbi], gmodecolmaps [sbi], sizeof gmodecolmaps [tbi]);

#if WITHSPECTRUMWF
	gzoomxpow2 [tbi] = gzoomxpow2 [sbi];		/* уменьшение отображаемого участка спектра */
	gtopdbspe [tbi] = gtopdbspe [sbi];			/* нижний предел FFT */
	gbottomdbspe [tbi] = gbottomdbspe [sbi];	/* верхний предел FFT */
	gtopdbwfl [tbi] = gtopdbwfl [sbi];			/* нижний предел FFT waterflow */
	gbottomdbwfl [tbi] = gbottomdbwfl [sbi];	/* верхний предел FFT waterflow */
#endif /* WITHSPECTRUMWF */
}

/* сохранить все частоту настройки в соответствующий диапазон, ячейку памяти или VFO. */
static void
//NOINLINEAT
savebandfreq(const vindex_t b, const uint_fast8_t bi)
{
	//PRINTF(PSTR("savebandfreq: b=%d, bi=%d, freq=%ld\n"), b, bi, (unsigned long) gfreqs [bi]);
	verifyband(b);

	save_i32(RMT_BFREQ_BASE(b), gfreqs [bi]);	/* сохранить в области диапазона частоту */
}

/* сохранить все параметры настройки (кроме частоты) в соответствующий диапазон, ячейку памяти или VFO. */
static void
//NOINLINEAT
savebandstate(const vindex_t b, const uint_fast8_t bi)
{
	//PRINTF(PSTR("savebandstate: b=%d, bi=%d, freq=%ld\n"), b, bi, (unsigned long) gfreqs [bi]);
	verifyband(b);

	save_i8(RMT_MODEROW_BASE(b), gmoderows [bi]);

	uint_fast8_t i;
	for (i = 0; i < MODEROW_COUNT; ++ i)
		save_i8(RMT_MODECOLS_BASE(b, i), gmodecolmaps [bi] [i]);

#if ! WITHONEATTONEAMP
	save_i8(RMT_PAMP_BASE(b), gpamps [bi]);
#endif /* ! WITHONEATTONEAMP */
	save_i8(RMT_ATT_BASE(b), gatts [bi]);
#if WITHANTSELECTRX
	save_i8(RMT_RXANTENNA_BASE(b), grxantennas [bi]);
	save_i8(RMT_ANTENNA_BASE(b), gantennas [bi]);
#elif WITHANTSELECT || WITHANTSELECT2
	save_i8(RMT_ANTENNA_BASE(b), gantennas [bi]);
#endif /* WITHANTSELECT || WITHANTSELECTRX */
#if WITHAUTOTUNER
	save_i8(offsetof(struct nvmap, bands[b].tunerwork), tunerwork);
	save_i8(offsetof(struct nvmap, bands[b].tunercap), tunercap);
	save_i8(offsetof(struct nvmap, bands[b].tunerind), tunerind);
	save_i8(offsetof(struct nvmap, bands[b].tunertype), tunertype);
#endif /* WITHAUTOTUNER */
#if WITHSPECTRUMWF
	save_i8(offsetof(struct nvmap, bands[b].gzoomxpow2), gzoomxpow2 [bi]);	/* уменьшение отображаемого участка спектра */
	save_i8(offsetof(struct nvmap, bands[b].gtopdbspe), gtopdbspe [bi]);	/* нижний предел FFT */
	save_i8(offsetof(struct nvmap, bands[b].gbottomdbspe), gbottomdbspe [bi]);	/* верхний предел FFT */
	save_i8(offsetof(struct nvmap, bands[b].gtopdbwfl), gtopdbwfl [bi]);	/* нижний предел FFT waterflow */
	save_i8(offsetof(struct nvmap, bands[b].gbottomdbwfl), gbottomdbwfl [bi]);	/* верхний предел FFT waterflow */
#endif /* WITHSPECTRUMWF */
}

static void savebandgroup(uint_fast8_t b)
{
#if	WITHDIRECTBANDS
	const uint_fast8_t bandgroup = bandsmap [b].bandgroup;
	if (bandgroup != BANDGROUP_COUNT)
		save_i8(RMT_BANDGROUP(bandgroup), b);
#endif /* WITHDIRECTBANDS */
}

/* выборка из битовой маски, Возможно, значение modecolmap бует откорректировано. */
static uint_fast8_t
NOINLINEAT
getmodecol(
	uint_fast8_t index, 
	uint_fast8_t upper, // moderow
	uint_fast8_t def,
	uint_fast8_t bi)		/* bank index */
{
	uint_fast8_t v = gmodecolmaps [bi] [index];
	if (v > upper)
	{
		gmodecolmaps [bi] [index] = def;
		return def;
	}
	return v;
}

/* внести новое значение в битовую маску */
static void
//NOINLINEAT
putmodecol(
	const uint_fast8_t index, 	// moderow
	const uint_fast8_t v,
	const uint_fast8_t bi		/* bank index */
	)
{
	gmodecolmaps [bi] [index] = v;
}

/* получение режима работы "по умолчанию" для частоты. */
static uint_fast8_t
//NOINLINEAT
getdefaultsubmode(
	uint_fast32_t freq	/* частота (изображение на дисплее) */
	)
{
#if WITHMODESETSMART
	return SUBMODE_SSBSMART;
#else
	return freq < BANDFUSBFREQ ? SUBMODE_LSB : SUBMODE_USB;
#endif
}


/* получение режима работы "по умолчанию" для частоты. */
static uint_fast8_t
//NOINLINEAT
getdefaultbandsubmode(
	uint_fast32_t freq	/* частота (изображение на дисплее) */
	)
{
	const vindex_t b = getfreqband(freq);
	if (b < HBANDS_COUNT)
		return get_band_defsubmode(b);
	return getdefaultsubmode(freq);
}


#if CTLSTYLE_RA4YBO || CTLSTYLE_RA4YBO_V1 || CTLSTYLE_RA4YBO_V2 || CTLSTYLE_RA4YBO_V3

	static uint_fast16_t gtxpower [MODE_COUNT];
	static uint_fast16_t gtxcompr [MODE_COUNT];

#endif /* CTLSTYLE_RA4YBO || CTLSTYLE_RA4YBO_V1 || CTLSTYLE_RA4YBO_V2*/

#if WITHIF4DSP

	static uint_fast8_t gtxaudio [MODE_COUNT];

#endif /* WITHIF4DSP */

#if WITHIF4DSP

// Начальная хагрузка значений из NVRAM
// Загрузка позиции выбора полосы пропускания и значений границ для всех режимов работы
// Сохранение происходит при модификации в обработчика нажатия клавиши BW
static void
bwseti_load(void)
{
	uint_fast8_t bwprop;
	for (bwprop = 0; bwprop < BWPROPI_count; ++ bwprop)
	{
		// Значения границ полос пропускания
		bwprop_t * const p = bwprops [bwprop];

		p->afresponce = loadvfy8up(RMT_BWPROPSAFRESPONCE_BASE(bwprop), AFRESPONCEMIN, AFRESPONCEMAX, p->afresponce);
		switch (p->type)
		{
		case BWSET_SINGLE:
			p->left10_width10 = loadvfy8up(RMT_BWPROPSLEFT_BASE(bwprop), p->limits->left10_width10_low, p->limits->left10_width10_high, p->left10_width10);
			break;
		default:
		case BWSET_PAIR:
			p->left10_width10 = loadvfy8up(RMT_BWPROPSLEFT_BASE(bwprop), p->limits->left10_width10_low, p->limits->left10_width10_high, p->left10_width10);
			p->right100 = loadvfy8up(RMT_BWPROPSRIGHT_BASE(bwprop), p->limits->right100_low, p->limits->right100_high, p->right100);
			break;
		}
	}
	uint_fast8_t bwseti;
	for (bwseti = 0; bwseti < BWSETI_count; ++ bwseti)
	{
		// индекс выбранной полосы
		bwsetpos [bwseti] = loadvfy8up(RMT_BWSETPOS_BASE(bwseti), 0, bwsetsc [bwseti].last, bwsetpos [bwseti]);
	}
}


/* загрузка параметров слухового приема */
static void
agcseti_load(void)
{
	uint_fast8_t agcseti;
	for (agcseti = 0; agcseti < AGCSETI_COUNT; ++ agcseti)
	{
		agcp_t * const p = & gagc [agcseti];
		const struct afsetitempl * const t = & aft [agcseti];
		// параметры АРУ
		p->rate = loadvfy8up(offsetof(struct nvmap, afsets [agcseti].rate), 1, AGC_RATE_FLAT, t->rate);
		p->t0 = loadvfy8up(offsetof(struct nvmap, afsets [agcseti].t0), 0, 250, t->t0);
		p->t1 = loadvfy8up(offsetof(struct nvmap, afsets [agcseti].t1), 10, 250, t->t1);
		p->release10 = loadvfy8up(offsetof(struct nvmap, afsets [agcseti].release10), 1, 100, t->release10);
		p->t4 = loadvfy8up(offsetof(struct nvmap, afsets [agcseti].t4), 10, 250, t->t4);
		p->thung10 = loadvfy8up(offsetof(struct nvmap, afsets [agcseti].thung10), 0, 250, t->thung10);
		p->scale = t->scale;
	}
}

/* чтение из NVRAM параметров профилей обработки сигнала перед модулятором */
static void micproc_load(void)
{
	uint_fast8_t proci;
	uint_fast8_t i;

	for (proci = 0; proci < NMICPROFILES; ++ proci)
	{
		mikproc_t * const p = & micprofiles [proci];

		p->comp = loadvfy8up(offsetof(struct nvmap, gmicprocs [proci].comp), 0, 1, p->comp);
		p->complevel = loadvfy8up(offsetof(struct nvmap, gmicprocs [proci].complevel), 0, 1, p->complevel);
	}

	/* загрузка настроек - профиль аудиообработки для групп режимов работы */
	for (i = 0; i < TXAPROFIG_count; ++ i)
	{
		gtxaprofiles [i] = loadvfy8up(RMT_TXAPROFIGLE_BASE(i), 0, NMICPROFILES - 1, gtxaprofiles [i]);
	}

}

#endif /* WITHIF4DSP */

///////////////////////////
//
// работа со вторым валкодером

// шаг изменения значения параметра
enum
{
	ISTEP1 = 1,
	ISTEP2 = 2,
	ISTEP3 = 3,
	ISTEP5 = 5,
	ISTEP10 = 10,
	ISTEP50 = 50,
	ISTEP100 = 100,
	//ISTEPG,
	//
};

// особые случаи отображения значения параметра
enum
{
	RJ_YES = 128,	/* значение в поле rj, при котором отображаем как Yes/No */
	RJ_ON,			/* значение в поле rj, при котором отображаем как On/Off */
	RJ_CATSPEED,	/* отображение скорости CAT */
	RJ_CATSIG,		/* параметр - управляющие параметры PTT/KEY чкпкз CAT */
	RJ_ELKEYMODE,	/* режим электронного ключа - 0 - ACS, 1 - electronic key, 2 - straight key, 3 - BUG key */
	RJ_POW2,		/* параметр - степень двойки. Отображается результат */
	RJ_ENCRES,		/* параметр - индекс в таблице разрешений валкодера */
	RJ_SUBTONE,		/* параметр - индекс в таблице частот субтонов */
	RJ_TXAUDIO,		/* параметр - источник звука для передачи */
	RJ_MDMSPEED,	/* параметр - скорость модема */
	RJ_MDMMODE,		/* параметр - тип модуляции модема */
	RJ_MONTH,		/* параметр - месяц 1..12 */
	RJ_POWER,		/* отображние мощности HP/LP */
	RJ_SIGNED,		/* отображние знакового числа (меню на втором валкодере) */
	RJ_UNSIGNED,		/* отображние знакового числа (меню на втором валкодере) */
	RJ_SMETER,		/* выбор внешнего вида прибора - стрелочный или градусник */
	RJ_NOTCH,		/* тип NOTCH фильтра - MANUAL/AUTO */
	RJ_CPUTYPE,		/* текст типа процессора */
	RJ_VIEW,		/* стиль отображения спектра и панорамы */
	RJ_COMPILED,		/* текст даты компиляции */
	//
	RJ_notused
};

struct enc2menu
{
	char label [10];
	uint8_t rj;
	uint8_t istep;
	uint16_t bottom, upper;	/* ограничения на редактируемое значение (upper - включая) */

	nvramaddress_t nvrambase;				/* Если MENUNONVRAM - только меняем в памяти */
	nvramaddress_t (* nvramoffs)(nvramaddress_t base);	/* Смещение при доступе к NVRAM. Нужно при работе с настройками специфическрми для диапазона например */
	unsigned (* valoffset)(void);
	uint_fast16_t * pval16;			/* переменная, которую подстраиваем - если она 16 бит. Массив, индексируется по значению от valoffset. */
	uint_fast8_t * pval8;			/* переменная, которую подстраиваем  - если она 8 бит. Массив, индексируется по значению от valoffset. */
	int_fast32_t (* funcoffs)(void);	/* при отображении и использовании добавляется число отсюда */
	void (* adjust)(const FLASHMEM struct enc2menu * mp, int_least16_t nrotate);
};

static const FLASHMEM char catsiglabels [BOARD_CATSIG_count] [9] =
{
	"NONE    ",
	"SER1 DTR",
	"SER1 RTS",
#if WITHUSBHW && WITHUSBCDCACM && WITHUSBCDCACM_N > 1
	"SER2 DTR",
	"SER2 RTS",
#endif /* WITHUSBHW && WITHUSBCDCACM && WITHUSBCDCACM_N > 1 */
};

static unsigned valoffset0(void)
{
	return 0;
}

static unsigned valoffset_bi_a(void)
{
	return getbankindex_ab_fordisplay(0);	/* VFO A modifications */
}


static nvramaddress_t nvramoffs0(nvramaddress_t base)
{
	return base;
}

static nvramaddress_t nvramoffs_band_a(nvramaddress_t base)
{
	const uint_fast8_t bi = getbankindex_ab_fordisplay(0);	/* VFO A modifications */
	const vindex_t b = getvfoindex(bi);

	ASSERT(base != MENUNONVRAM);

	if (base == MENUNONVRAM)
		return MENUNONVRAM;

	//
	// для диапазонов - вычисляем шаг увеличения индекса по массиву хранения в диапазонах
	return base + RMT_BFREQ_BASE(b) - RMT_BFREQ_BASE(0);
}

/* функция для сохранения значения параметра */
static void
enc2savemenuvalue(
	const FLASHMEM struct enc2menu * mp
	)
{
	const nvramaddress_t nvram = mp->nvramoffs(mp->nvrambase);
	const uint_fast16_t * const pv16 = mp->pval16;
	const uint_fast8_t * const pv8 = mp->pval8;
	const unsigned valoffset = mp->valoffset(); //ismenukind(mp, ITEM_ARRAY_BI) ? getbankindex_ab_fordisplay(0) : 0;

	if (nvram == MENUNONVRAM)
		return;

	if (pv16 != NULL)
	{
		save_i16(nvram, pv16 [valoffset]);		/* сохраняем отредактированное значение */
	}
	else if (pv8 != NULL)
	{
		save_i8(nvram, pv8 [valoffset]);		/* сохраняем отредактированное значение */
	}
	else
	{
		ASSERT(0);
	}
}

/* функция для изменения значения параметра */
static 
void
enc2menu_adjust(
	const FLASHMEM struct enc2menu * mp,
	int_least16_t nrotate	/* знаковое число - на сколько повернут валкодер */
	)
{
	const uint_fast16_t step = mp->istep;
	uint_fast16_t * const pv16 = mp->pval16;
	uint_fast8_t * const pv8 = mp->pval8;
	const unsigned valoffset = mp->valoffset();

	/* измиенение параметра */
	if (nrotate < 0)
	{
		// negative change value
		const uint_fast32_t bottom = mp->bottom;
		if (pv16 != NULL)
		{
			pv16 [valoffset] =
				prevfreq(pv16 [valoffset], pv16 [valoffset] - (- nrotate * step), step, bottom);
		}
		else if (pv8 != NULL)
		{
			pv8 [valoffset] =
				prevfreq(pv8 [valoffset], pv8 [valoffset] - (- nrotate * step), step, bottom);
		}
		enc2savemenuvalue(mp);
	}
	else if (nrotate > 0)
	{
		// positive change value
		const uint_fast32_t upper = mp->upper;
		if (pv16 != NULL)
		{
			pv16 [valoffset] =
				nextfreq(pv16 [valoffset], pv16 [valoffset] + (nrotate * step), step, upper + (uint_fast32_t) step);
		}
		else
		{
			pv8 [valoffset] =
				nextfreq(pv8 [valoffset], pv8 [valoffset] + (nrotate * step), step, upper + (uint_fast32_t) step);
		}
		enc2savemenuvalue(mp);
	}
}

#if WITHENCODER2

static const FLASHMEM struct enc2menu enc2menus [] =
{
#if WITHIF4DSP
#if ! WITHPOTAFGAIN
	{
		"VOLUME   ",
		RJ_UNSIGNED,		// rj
		ISTEP1,
		BOARD_AFGAIN_MIN, BOARD_AFGAIN_MAX, 					// Громкость в процентах
		offsetof(struct nvmap, afgain1),
		nvramoffs0,
		valoffset0,
		& afgain1.value,
		NULL,
		getzerobase, /* складывается со смещением и отображается */
		enc2menu_adjust,	/* функция для изменения значения параметра */
	},
#endif /* ! WITHPOTAFGAIN */
#if ! WITHPOTIFGAIN
	{
		"RF GAIN  ",
		RJ_UNSIGNED,		// rj
		ISTEP1,
		BOARD_IFGAIN_MIN, BOARD_IFGAIN_MAX, 					// Усиление ПЧ/ВЧ в процентах
		offsetof(struct nvmap, rfgain1),
		nvramoffs0,
		valoffset0,
		& rfgain1.value,
		NULL,
		getzerobase, /* складывается со смещением и отображается */
		enc2menu_adjust,	/* функция для изменения значения параметра */
	},
#endif /* ! WITHPOTIFGAIN */
#endif /* WITHIF4DSP */
#if WITHELKEY && ! WITHPOTWPM
	{
		"CW SPEED ",
		RJ_UNSIGNED,		// rj
		ISTEP1,
		CWWPMMIN, CWWPMMAX,		// minimal WPM = 10, maximal = 60 (also changed by command KS).
		offsetof(struct nvmap, elkeywpm),
		nvramoffs0,
		valoffset0,
		NULL,
		& elkeywpm.value,
		getzerobase, /* складывается со смещением и отображается */
		enc2menu_adjust,	/* функция для изменения значения параметра */
	},
#endif /* WITHELKEY && ! WITHPOTWPM */
#if WITHTX
#if WITHTXCPATHCALIBRATE
	{
		"TX CALIBR",
		RJ_UNSIGNED,		// rj
		ISTEP1,
		0, 150,		/* используется при калибровке параметров интерполятора */
		offsetof(struct nvmap, gdesignscale),
		nvramoffs0,
		valoffset0,
		& gdesignscale,
		NULL,
		getzerobase, /* складывается со смещением и отображается */
		enc2menu_adjust,	/* функция для изменения значения параметра */
	},
#endif
#if WITHPOWERTRIM && ! WITHPOTPOWER
	{
		"TX POWER ",
		RJ_UNSIGNED,		// rj
		ISTEP1,
		WITHPOWERTRIMMIN, WITHPOWERTRIMMAX,
		offsetof(struct nvmap, gnormalpower),
		nvramoffs0,
		valoffset0,
		NULL,
		& gnormalpower.value,
		getzerobase, /* складывается со смещением и отображается */
		enc2menu_adjust,	/* функция для изменения значения параметра */
	},
#endif /* WITHPOWERTRIM && ! WITHPOTPOWER */
#if WITHSUBTONES
	{
		"CTCSS FRQ", 
		RJ_SUBTONE,		// rj
		ISTEP1,	//  Continuous Tone-Coded Squelch System or CTCSS freq
		0, sizeof gsubtones / sizeof gsubtones [0] - 1, 
		offsetof(struct nvmap, gsubtonei),
		nvramoffs0,
		valoffset0,
		NULL,
		& gsubtonei,
		getzerobase, /* складывается со смещением и отображается */
		enc2menu_adjust,	/* функция для изменения значения параметра */
	},
#endif /* WITHPOWERTRIM */
#if WITHMIC1LEVEL
	{
		"MIKE LEVL",
		RJ_UNSIGNED,
		ISTEP1,		/* подстройка усиления микрофонного усилителя через меню. */
		WITHMIKEINGAINMIN, WITHMIKEINGAINMAX, 
		offsetof(struct nvmap, gmik1level),	/* усиление микрофонного усилителя */
		nvramoffs0,
		valoffset0,
		& gmik1level,
		NULL,
		getzerobase, /* складывается со смещением и отображается */
		enc2menu_adjust,	/* функция для изменения значения параметра */
	},
#endif /* ITHMIC1LEVEL */
#if WITHIF4DSP
	{
		"MIKE CLIP",
		RJ_UNSIGNED,
		ISTEP1,	
		WITHMIKECLIPMIN, WITHMIKECLIPMAX, 		/* Ограничение */
		offsetof(struct nvmap, gmikehclip),
		nvramoffs0,
		valoffset0,
		NULL,
		& gmikehclip,
		getzerobase, /* складывается со смещением и отображается */
		enc2menu_adjust,	/* функция для изменения значения параметра */
	},
#endif /* WITHIF4DSP */
#endif /* WITHTX */
#if WITHNOTCHFREQ && ! WITHPOTNOTCH
	{
		"NOTCH FRQ",
		RJ_UNSIGNED,		// rj
		ISTEP50,
		WITHNOTCHFREQMIN, WITHNOTCHFREQMAX,
		offsetof(struct nvmap, gnotchfreq),	/* центральная частота NOTCH */
		nvramoffs0,
		valoffset0,
		& gnotchfreq.value,
		NULL,
		getzerobase, /* складывается со смещением и отображается */
		enc2menu_adjust,	/* функция для изменения значения параметра */
	},
#endif /* WITHNOTCHFREQ && ! WITHPOTNOTCH */
#if WITHIF4DSP
	{
		"NR LEVEL ", 
		RJ_UNSIGNED,		// rj
		ISTEP1,		/* nr level */
		0, NRLEVELMAX, 
		offsetof(struct nvmap, gnoisereductvl),	/* уровень сигнала болше которого открывается шумодав */
		nvramoffs0,
		valoffset0,
		NULL,
		& gnoisereductvl,
		getzerobase, /* складывается со смещением и отображается */
		enc2menu_adjust,	/* функция для изменения значения параметра */
	},
#if ! WITHPOTNFMSQL
	{
		"SQUELCHFM",
		RJ_UNSIGNED,		// rj
		ISTEP1,		/* squelch level */
		0, SQUELCHMAX, 
		offsetof(struct nvmap, gsquelchNFM),	/* уровень сигнала болше которого открывается шумодав */
		nvramoffs0,
		valoffset0,
		NULL,
		& gsquelchNFM,
		getzerobase, /* складывается со смещением и отображается */
		enc2menu_adjust,	/* функция для изменения значения параметра */
	},
#endif /* ! WITHPOTNFMSQL */
#if WITHSPECTRUMWF
	{
		"BOTTOM DB",
		RJ_UNSIGNED,		// rj
		ISTEP1,		/* spectrum range */
		WITHBOTTOMDBMIN, WITHBOTTOMDBMAX,	/* диапазон отображаемых значений */
		offsetof(struct nvmap, bands [0].gbottomdbspe),
		nvramoffs_band_a,
		valoffset_bi_a,
		NULL,
		& gbottomdbspe [0],
		getzerobase, /* складывается со смещением и отображается */
		enc2menu_adjust,	/* функция для изменения значения параметра */
	},
#if BOARD_FFTZOOM_POW2MAX > 0
	{
		"ZOOM PAN ", 
		RJ_POW2,		// rj
		ISTEP1,		/* spectrum range */
		0, BOARD_FFTZOOM_POW2MAX,	/* масштаб панорамы */
		offsetof(struct nvmap, bands [0].gzoomxpow2),
		nvramoffs_band_a,
		valoffset_bi_a,
		NULL,
		& gzoomxpow2 [0],
		getzerobase, /* складывается со смещением и отображается */
		enc2menu_adjust,	/* функция для изменения значения параметра */
	},
#endif /* BOARD_FFTZOOM_POW2MAX > 0 */
	{
		"VIEW STLE",
		RJ_VIEW,
		ISTEP1,
		0, VIEW_COUNT - 1,
		offsetof(struct nvmap, gviewstyle),
		nvramoffs0,
		valoffset0,
		NULL,
		& gviewstyle,
		getzerobase,
		enc2menu_adjust,
	},
#endif /* WITHSPECTRUMWF */
#endif /* WITHIF4DSP */
#if WITHIFSHIFT && ! WITHPOTIFSHIFT
	// Увеличение значения параметра смещает слышимую часть спектра в более высокие частоты
	{
		"IF SHIFT ",
		RJ_SIGNED,		// rj
		ISTEP50,
		IFSHIFTTMIN, IFSHIFTMAX,			/* -3 kHz..+3 kHz in 50 Hz steps */
		offsetof(struct nvmap, ifshifoffset),
		nvramoffs0,
		valoffset0,
		& ifshifoffset.value,
		NULL,
		getifshiftbase, /* складывается со смещением и отображается */
		enc2menu_adjust,	/* функция для изменения значения параметра */
	},
#endif /* WITHIFSHIFT && ! WITHPOTIFSHIFT */
};

/* получение названия редактируемого параметра */
static 
const FLASHMEM char * 
enc2menu_label_P(
	uint_fast8_t item
	)
{
	const FLASHMEM struct enc2menu * const p = & enc2menus [item];

	return p->label;
}

/* получение значения редактируемого параметра */
static void
enc2menu_value(
	uint_fast8_t item,
	int WDTH,	// ширина поля для отображения (в GUI не используется)
	char * buff,	// буфер для текста значения параметра
	size_t sz		// размер буфера
	)
{
	const FLASHMEM struct enc2menu * const mp = & enc2menus [item];
	long int value;

	if (mp->pval16 != NULL)
	{
		value = mp->funcoffs() + * mp->pval16;
	}
	else if (mp->pval8 != NULL)
	{
		value = mp->funcoffs() + * mp->pval8;
	}
	else
	{
		ASSERT(0);
		value = mp->bottom;	/* чтобы не ругался компилятор */
	}

	switch (mp->rj)
	{
#if WITHSUBTONES && WITHTX
	case RJ_SUBTONE:
		local_snprintf_P(buff, sz, PSTR("%*u.%1u"), WDTH - 2, gsubtones [value] / 10, gsubtones [value] % 10);
		break;
#endif /* WITHSUBTONES && WITHTX */
	case RJ_YES:
		local_snprintf_P(buff, sz, PSTR("%*s"), WDTH, value ? "YES" : "NO");
		break;
	case RJ_CATSIG:
		local_snprintf_P(buff, sz, PSTR("%*s"), WDTH, catsiglabels [value]);
		break;
	case RJ_ON:
		local_snprintf_P(buff, sz, PSTR("%*s"), WDTH, value ? "ON" : "OFF");
		break;
	case RJ_POW2:
		local_snprintf_P(buff, sz, PSTR("%*u"), WDTH, 1U << value);
		break;
	case RJ_VIEW:
		local_snprintf_P(buff, sz, PSTR("%*s"), WDTH, view_types [value]);
		break;
	case RJ_SIGNED:
		local_snprintf_P(buff, sz, PSTR("%-+*ld"), WDTH, (signed long) value);
		break;
	case RJ_UNSIGNED:
	default:
		local_snprintf_P(buff, sz, PSTR("%*lu"), WDTH, (unsigned long) value);
		break;
	}
}

enum
{
	ENC2STATE_INITIALIZE,
	ENC2STATE_SELECTITEM,
	ENC2STATE_EDITITEM,
	//
	ENC2STATE_COUNT
};

#define ENC2POS_COUNT (sizeof enc2menus / sizeof enc2menus [0])

static const FLASHMEM char text_nul9_P [] = "         ";

static uint_fast8_t enc2state = ENC2STATE_INITIALIZE;
static uint_fast8_t enc2pos;	// выбраный пунки меню

#define RMT_ENC2STATE_BASE offsetof(struct nvmap, enc2state)
#define RMT_ENC2POS_BASE offsetof(struct nvmap, enc2pos)

/* нажатие на второй валкодер */
static void
uif_encoder2_press(void)
{
	switch (enc2state)
	{
	case ENC2STATE_INITIALIZE:
		enc2state = ENC2STATE_SELECTITEM;
		break;
	case ENC2STATE_SELECTITEM:
		enc2state = ENC2STATE_EDITITEM;
		break;
	case ENC2STATE_EDITITEM:
		enc2state = ENC2STATE_SELECTITEM;
		break;
	}
	save_i8(RMT_ENC2STATE_BASE, enc2state);
	display_redrawmodestimed(1);
}

/* удержанное нажатие на второй валкодер - выход из режима редактирования */
static void
uif_encoder2_hold(void)
{
	switch (enc2state)
	{
	case ENC2STATE_INITIALIZE:
		enc2state = ENC2STATE_EDITITEM;
		break;
	case ENC2STATE_SELECTITEM:
		enc2state = ENC2STATE_INITIALIZE;
		break;
	case ENC2STATE_EDITITEM:
		enc2state = ENC2STATE_INITIALIZE;
		break;
	}
	save_i8(RMT_ENC2STATE_BASE, enc2state);
	display_redrawmodestimed(1);
}

/* обработка вращения второго валкодера */
static uint_fast8_t
uif_encoder2_rotate(
	int_least16_t nrotate	/* знаковое число - на сколько повернут валкодер */
	)
{
	if (nrotate == 0)
		return 0;

	switch (enc2state)
	{
	case ENC2STATE_SELECTITEM:
		/* выбор параметра для редактирования */
		while (nrotate != 0)
		{
			if (nrotate > 0)
			{
				enc2pos = calc_next(enc2pos, 0, ENC2POS_COUNT - 1);
				-- nrotate;
			}
			else
			{
				enc2pos = calc_prev(enc2pos, 0, ENC2POS_COUNT - 1);
				++ nrotate;
			}
		}
		save_i8(RMT_ENC2POS_BASE, enc2pos);
		return 1;

	case ENC2STATE_EDITITEM:
		if (nrotate != 0)
		{
			const FLASHMEM struct enc2menu * const mp = & enc2menus [enc2pos];
			mp->adjust(mp, nrotate);	// изменение и сохранение значения параметра
			updateboard(1, 0);
			return 1;
		}

	default:
		break;
	}
	return 0;
}

#else /* WITHENCODER2 */

/* заглушка */
static uint_fast8_t
uif_encoder2_rotate(
	int_least16_t nrotate	/* знаковое число - на сколько повернут валкодер */
	)
{
	return 0;
}

#endif /* WITHENCODER2 */


// FUNC item label
void display2_fnlabel9(
	uint_fast8_t x, 
	uint_fast8_t y, 
	dctx_t * pctx
	)
{
#if WITHENCODER2
	const char FLASHMEM * const text = enc2menu_label_P(enc2pos);
	switch (enc2state)
	{
	case ENC2STATE_INITIALIZE:
		display_1fmenu_P(x, y, text_nul9_P);
		break;
	case ENC2STATE_SELECTITEM:
		display_2fmenus_P(x, y, 0, text, text);
		break;
	case ENC2STATE_EDITITEM:
		display_2fmenus_P(x, y, 1, text, text);
		break;
	}
#endif /* WITHENCODER2 */
}

// FUNC item value
void display2_fnvalue9(
	uint_fast8_t x, 
	uint_fast8_t y, 
	dctx_t * pctx
	)
{
#if WITHENCODER2
	enum { WDTH = 9 };	// ширина поля для отображения
	char b [WDTH + 1];	// тут формируется текст для отображения

	enc2menu_value(enc2pos, WDTH, b, ARRAY_SIZE(b));
	switch (enc2state)
	{
	case ENC2STATE_INITIALIZE:
		display_1fmenu_P(x, y, text_nul9_P);
		break;
	case ENC2STATE_SELECTITEM:
		display_2fmenus(x, y, 0, b, b);
		break;
	case ENC2STATE_EDITITEM:
		display_2fmenus(x, y, 1, b, b);
		break;
	}
#endif /* WITHENCODER2 */
}

// split, lock, s-meter display
static void
loadsavedstate(void)
{
	lockmode = loadvfy8up(RMT_LOCKMODE_BASE, 0, 1, lockmode);	/* вытаскиваем признак блокировки валкодера */
#if WITHUSEFAST
	gusefast = loadvfy8up(RMT_USEFAST_BASE, 0, 1, gusefast);	/* переключение в режим крупного шага */
#endif /* WITHUSEFAST */
#if WITHUSEDUALWATCH
	mainsubrxmode = loadvfy8up(RMT_MAINSUBRXMODE_BASE, 0, MAINSUBRXMODE_COUNT - 1, mainsubrxmode);	/* состояние dual watch */
#endif /* WITHUSEDUALWATCH */
#if WITHPOWERLPHP
	gpwri = loadvfy8up(RMT_PWR_BASE, 0, PWRMODE_COUNT - 1, gpwri);
#endif /* WITHPOWERLPHP */
#if WITHNOTCHONOFF
	gnotch = loadvfy8up(RMT_NOTCH_BASE, 0, 1, gnotch);
#elif WITHNOTCHFREQ
	gnotch = loadvfy8up(RMT_NOTCH_BASE, 0, 1, gnotch);
#if WITHENCODER2
	enc2state = loadvfy8up(RMT_ENC2STATE_BASE, ENC2STATE_INITIALIZE, ENC2STATE_COUNT - 1, enc2state);	/* вытаскиваем режим режактирования паарметров вторым валкодером */
	enc2pos = loadvfy8up(RMT_ENC2POS_BASE, 0, ENC2POS_COUNT - 1, enc2pos);	/* вытаскиваем номер параметра для редактирования вторым валкодером */
#endif /* WITHENCODER2 */
#endif /* WITHNOTCHONOFF */
	menuset = loadvfy8up(RMT_MENUSET_BASE, 0, display_getpagesmax(), menuset);		/* вытаскиваем номер субменю, с которым работаем сейчас */
#if WITHSPLIT
	gsplitmode = loadvfy8up(RMT_SPLITMODE_BASE, 0, VFOMODES_COUNT - 1, gsplitmode); /* (vfo/vfoa/vfob/mem) */
	gvfoab = loadvfy8up(RMT_VFOAB_BASE, 0, VFOS_COUNT - 1, gvfoab); /* (vfoa/vfob) */
#elif WITHSPLITEX
	gsplitmode = loadvfy8up(RMT_SPLITMODE_BASE, 0, VFOMODES_COUNT - 1, gsplitmode); /* (vfo/vfoa/vfob/mem) */
	gvfoab = loadvfy8up(RMT_VFOAB_BASE, 0, VFOS_COUNT - 1, gvfoab); /* (vfoa/vfob) */
#endif /* WITHSPLIT */
#if WITHSPKMUTE
	gmutespkr = loadvfy8up(RMT_MUTELOUDSP_BASE, 0, 1, gmutespkr);	/*  выключение динамика */
#endif /* WITHSPKMUTE */
#if CTLSTYLE_RA4YBO
	gaffilter = loadvfy8up(RMT_AFFILTER_BASE, 0, 1, gaffilter);	/* включение ФНЧ на приёме в аппарате RA4YBO */
#elif CTLSTYLE_RA4YBO_V1 || CTLSTYLE_RA4YBO_V2 || KEYBSTYLE_RA4YBO_AM0
	guser1 = loadvfy8up(RMT_USER1_BASE, 0, 1, guser1);
	guser2 = loadvfy8up(RMT_USER2_BASE, 0, 1, guser2);
	guser3 = loadvfy8up(RMT_USER3_BASE, 0, 1, guser3);
	guser4 = loadvfy8up(RMT_USER4_BASE, 0, 1, guser4);
	guser5 = loadvfy8up(RMT_USER5_BASE, 0, 1, guser5);
#elif CTLSTYLE_RA4YBO_V3
	guser1 = loadvfy8up(RMT_USER1_BASE, 0, 1, guser1);
	guser4 = loadvfy8up(RMT_USER4_BASE, 0, 1, guser4);
	guser5 = loadvfy8up(RMT_USER5_BASE, 0, 1, guser5);
#endif /* CTLSTYLE_RA4YBO_V1 || CTLSTYLE_RA4YBO_V2 || CTLSTYLE_RA4YBO_V3 */
	// тюнер запоминается подиапазонно
//#if WITHAUTOTUNER
//	tunerwork = loadvfy8up(offsetof(struct nvmap, tunerwork), 0, 1, tunerwork);
//#endif /* WITHAUTOTUNER */
#if WITHANTSELECT2
	gantmanual = loadvfy8up(RMT_ANTMANUAL_BASE, 0, 1, gantmanual);
#endif /* WITHANTSELECT2 */

#if WITHIF4DSP
	#if WITHUSBUAC && WITHTX
		gdatamode = loadvfy8up(RMT_DATAMODE_BASE, 0, 1, gdatamode);
	#endif /* WITHUSBUAC && WITHTX */
	// Загрузка позиции выбора полосы пропускания и значений границ для всех режимов работы
	// Сохранение происходит при модификации в обработчика нажатия клавиши BW
	bwseti_load();

	agcseti_load();	/* загрузка параметров слухового приема */
	micproc_load();	/* чтение из NVRAM параметров профилей обработки сигнала перед модулятором */

#endif /* WITHIF4DSP */

	/* загрузка параметров, зависящих от режима */
	uint_fast8_t mode;
	for (mode = 0; mode < MODE_COUNT; ++ mode)
	{
	#if CTLSTYLE_RA4YBO || CTLSTYLE_RA4YBO_V1 || CTLSTYLE_RA4YBO_V3
		// компрессия и выходная мощность по режимам работы.
		gtxpower [mode] = loadvfy16up(RMT_TXPOWER_BASE(mode), 0, 255, 255);
		gtxcompr [mode] = loadvfy16up(RMT_TXCOMPR_BASE(mode), 0, 255, 255);
	#endif /* CTLSTYLE_RA4YBO || CTLSTYLE_RA4YBO_V1 || CTLSTYLE_RA4YBO_V3 */
	#if WITHIF4DSP
		// источник звука
		gtxaudio [mode] = loadvfy8up(RMT_TXAUDIO_BASE(mode), 0, BOARD_TXAUDIO_count - 1, mdt [mode].txaudio);
	#endif /* WITHIF4DSP */
		/* включение NR */
	#if WITHIF4DSP
		gnoisereducts [mode] = loadvfy8up(RMT_NR_BASE(mode), 0, 1, gnoisereducts [mode]);
	#endif /* WITHIF4DSP */
	}
}

#if WITHANTSELECT2
/* получить номер антенны в зависимости от частоты */
static uint_fast8_t getdefantenna(uint_fast32_t f)
{
	const uint_fast32_t fsw = hffreqswitch * 1000000uL;
	ASSERT((fsw > TUNE_BOTTOM) && (fsw < TUNE_TOP));
	return f > fsw;
}
#endif /* WITHANTSELECT2 */

/* по диапазону вытащить все параметры (и частоту) нового диапазона */
static void 
//NOINLINEAT
loadnewband(
	vindex_t b,
	uint_fast8_t bi
	)
{
	ASSERT(bi < 2);
	//PRINTF(PSTR("loadnewband: b=%d, bi=%d, freq=%ld\n"), b, bi, (unsigned long) gfreqs [bi]);

	gfreqs [bi] = loadvfy32freq(b);		/* восстанавливаем частоту */
#if WITHONLYBANDS
	const vindex_t hb = getfreqband(gfreqs [bi]);
	tune_bottom_active [bi] = get_band_bottom(hb);
	tune_top_active [bi] = get_band_top(hb);
#endif
#if ! WITHONEATTONEAMP
	gpamps [bi] = loadvfy8up(RMT_PAMP_BASE(b), 0, PAMPMODE_COUNT - 1, DEFPREAMPSTATE);	/* вытаскиваем признак включения предусилителя */
#endif /* ! WITHONEATTONEAMP */
	gatts [bi] = loadvfy8up(RMT_ATT_BASE(b), 0, ATTMODE_COUNT - 1, 0);	/* вытаскиваем признак включения аттенюатора */
#if WITHANTSELECTRX
	grxantennas [bi] = loadvfy8up(RMT_RXANTENNA_BASE(b), 0, RXANTMODE_COUNT - 1, 0);	/* вытаскиваем номер включённой антенны */
	gantennas [bi] = loadvfy8up(RMT_ANTENNA_BASE(b), 0, ANTMODE_COUNT - 1, 0);	/* вытаскиваем номер включённой антенны */
#elif WITHANTSELECT2
	gantennas [bi] = loadvfy8up(RMT_ANTENNA_BASE(b), 0, ANTMODE_COUNT - 1, getdefantenna(gfreqs [bi]));	/* вытаскиваем номер включённой антенны */
#elif WITHANTSELECT
	gantennas [bi] = loadvfy8up(RMT_ANTENNA_BASE(b), 0, ANTMODE_COUNT - 1, 0);	/* вытаскиваем номер включённой антенны */
#endif /* WITHANTSELECT || WITHANTSELECTRX */
	const uint_fast8_t defsubmode = getdefaultbandsubmode(gfreqs [bi]);		/* режим по-умолчанию для частоты - USB или LSB */
	uint_fast8_t defrow;
	const uint_fast8_t  defcol = locatesubmode(defsubmode, & defrow);	/* строка/колонка для SSB . А что делать если не найдено? */

	// прописываем режим работы по умолчанию для данного диапазона
	gmodecolmaps [bi] [defrow] = loadvfy8up(RMT_MODECOLS_BASE(b, defrow), 0, modes [defrow][0] - 1, defcol);

	gmoderows [bi] = loadvfy8up(RMT_MODEROW_BASE(b), 0, MODEROW_COUNT - 1, defrow);

	uint_fast8_t i;
	for (i = 0; i < MODEROW_COUNT; ++ i)
	{
		gmodecolmaps [bi] [i] = loadvfy8up(RMT_MODECOLS_BASE(b, i), 0, 255, 255);	// везде прописывается 255 - потом ещё уточним.
	}
#if WITHAUTOTUNER
	// todo: добавить учет включенной антенны
	tunercap = loadvfy8up(offsetof(struct nvmap, bands[b].tunercap), CMIN, CMAX, tunercap);
	tunerind = loadvfy8up(offsetof(struct nvmap, bands[b].tunerind), LMIN, LMAX, tunerind);
	tunertype = loadvfy8up(offsetof(struct nvmap, bands[b].tunertype), 0, KSCH_COUNT - 1, tunertype);
	tunerwork = loadvfy8up(offsetof(struct nvmap, bands[b].tunerwork), 0, 1, tunerwork);
#endif /* WITHAUTOTUNER */
#if WITHSPECTRUMWF
	gzoomxpow2 [bi] = loadvfy8up(offsetof(struct nvmap, bands[b].gzoomxpow2), 0, BOARD_FFTZOOM_POW2MAX, 0);	/* масштаб панорамы */
	gtopdbspe [bi] = loadvfy8up(offsetof(struct nvmap, bands[b].gtopdbspe), WITHTOPDBMIN, WITHTOPDBMAX, WITHTOPDBDEFAULT);		/* нижний предел FFT */
	gbottomdbspe [bi] = loadvfy8up(offsetof(struct nvmap, bands[b].gbottomdbspe), WITHBOTTOMDBMIN, WITHBOTTOMDBMAX, WITHBOTTOMDBDEFAULT);	/* верхний предел FFT */
	gtopdbwfl [bi] = loadvfy8up(offsetof(struct nvmap, bands[b].gtopdbwfl), WITHTOPDBMIN, WITHTOPDBMAX, WITHTOPDBDEFAULT);		/* нижний предел FFT waterflow */
	gbottomdbwfl [bi] = loadvfy8up(offsetof(struct nvmap, bands[b].gbottomdbwfl), WITHBOTTOMDBMIN, WITHBOTTOMDBMAX, WITHBOTTOMDBDEFAULT);	/* верхний предел FFT waterflow */
#endif /* WITHSPECTRUMWF */
}

/* Получить текущий submode для указанного банка
*/
static uint_fast8_t
//NOINLINEAT
getsubmode(
	uint_fast8_t bi		/* vfo bank index */
   )
{
	ASSERT(bi < 2);
#if ELKEY328
	return SUBMODE_CW;//328
#else /* ELKEY328 */
	const uint_fast8_t moderow = gmoderows [bi];
	const uint_fast8_t modecol = getmodecol(moderow, modes [moderow][0] - 1, 0, bi);	/* выборка из битовой маски, Возможно, значение modecolmap бует откорректировано. */
	return modes [moderow][modecol + 1];	/* выборка из битовой маски */
#endif /* ELKEY328 */
}

/* функция вызывается из updateboard при измененияя параметров приёма
 * Устанавливает "кэшированные" состояния режима работы -
   gsubmode, gstep, gagcmode, gfi, gmode.
*/
static void
//NOINLINEAT
setgsubmode(
	uint_fast8_t submode
   )
{
	/* подправить фильтр, АРУ и шаг перестройки в соответствии с новым режимим работы */
	/* выбор фильтра */
	const FLASHMEM struct modetempl * const pmodet = getmodetempl(submode);
	const uint_fast8_t mode = submodes [submode].mode;
	const uint_fast8_t deffilter = getdefflt(mode, 0);	/* получить индекс фильтра "по умолчанию" для режима */
	
	gsubmode = submode;
	gmode = mode;
	gfi = getsuitablerx(mode, loadvfy8up(RMT_FILTER_BASE(mode), 0, getgfasize() - 1, deffilter));	/* фильтр для режима приёма */
#if ! WITHAGCMODENONE
	gagcmode = loadvfy8up(RMT_AGC_BASE(mode), 0, AGCMODE_COUNT - 1, pmodet->defagcmode);
#endif /* ! WITHAGCMODENONE */
	if (gusefast || gbigstep)
	{
		gstep = pmodet->step10 [1] * 10;
		gencderate = 1;
	}
	else
	{
		gstep = pmodet->step10 [0] * 10;
		gencderate = gstep / STEP_MINIMAL;
	}
}
#if ! WITHAGCMODENONE
#endif /* ! WITHAGCMODENONE */

#if WITHMODEM

void 
modemchangefreq(
	uint_fast32_t f		// частота, которую устанавливаем по команде от CAT
	)
{
	const uint_fast8_t bi = getbankindex_tx(gtx);
	gfreqs [bi] = vfy32up(f, TUNE_BOTTOM, TUNE_TOP - 1, gfreqs [bi]);

#if WITHONLYBANDS
	const vindex_t hb = getfreqband(gfreqs [bi]);
	tune_bottom_active [bi] = get_band_bottom(hb);
	tune_top_active [bi] = get_band_top(hb);
#endif
	//gpamps [bi] = loadvfy8up(RMT_PAMP_BASE(b), 0, PAMPMODE_COUNT - 1, DEFPREAMPSTATE);	/* вытаскиваем признак включения предусилителя */
	//gatts [bi] = loadvfy8up(RMT_ATT_BASE(b), 0, ATTMODE_COUNT - 1, 0);	/* вытаскиваем признак включения аттенюатора */
	//gantennas [bi] = loadvfy8up(RMT_ANTENNA_BASE(b), 0, ANTMODE_COUNT - 1, 0);	/* вытаскиваем код включённой антенны */
	updateboard(1, 1);	/* полная перенастройка (как после смены режима) */
}

void 
modemchangespeed(
	uint_fast32_t speed100		// частота, которую устанавливаем по команде от CAT
	)
{
	uint_fast8_t i;

	for (i = 0; i < sizeof modembr2int100 / sizeof sizeof modembr2int100 [0]; ++ i)
	{
		if (speed100 == modembr2int100 [i])
		{
			gmodemspeed = i;
			updateboard(1, 1);	/* полная перенастройка (как после смены режима) */
			return;
		}
	}
}


void 
modemchangemode(
	uint_fast32_t modemmode		// частота, которую устанавливаем по команде от CAT
	)
{
	if (modemmode < 2)	/* 0: BPSK, 1: QPSK */
	{
		gmodemmode = modemmode;
		updateboard(1, 1);	/* полная перенастройка (как после смены режима) */
	}
}


#endif /* WITHMODEM */

#if WITHCAT
/* функция для cat */
/* по диапазону вытащить все параметры (частота из f) нового диапазона. Старый НЕ записывать - идея в том, что записываются только по ручным операциям. */
static void 
catchangefreq(
	uint_fast32_t f,		// частота, которую устанавливаем по команде от CAT
	uint_fast8_t ab
	)
{
	const uint_fast8_t bi = getbankindex_ab(ab);
	const vindex_t b = getfreqband(f);	/* определяем по частоте, в каком диапазоне находимся */

	gfreqs [bi] = f;
#if WITHONLYBANDS
	tune_bottom_active [bi] = get_band_bottom(b);
	tune_top_active [bi] = get_band_top(b);
#endif
#if ! WITHONEATTONEAMP
	gpamps [bi] = loadvfy8up(RMT_PAMP_BASE(b), 0, PAMPMODE_COUNT - 1, DEFPREAMPSTATE);	/* вытаскиваем признак включения предусилителя */
	if (aistate != 0)
	{
		cat_answer_request(CAT_PA_INDEX);
	}
#endif /* ! WITHONEATTONEAMP */
	gatts [bi] = loadvfy8up(RMT_ATT_BASE(b), 0, ATTMODE_COUNT - 1, 0);	/* вытаскиваем признак включения аттенюатора */
	if (aistate != 0)
	{
		cat_answer_request(CAT_RA_INDEX);
	}
#if WITHANTSELECTRX
	grxantennas [bi] = loadvfy8up(RMT_RXANTENNA_BASE(b), 0, RXANTMODE_COUNT - 1, 0);	/* вытаскиваем номер включённой антенны */
	gantennas [bi] = loadvfy8up(RMT_ANTENNA_BASE(b), 0, ANTMODE_COUNT - 1, 0);	/* вытаскиваем номер включённой антенны */
	if (aistate != 0)
	{
		cat_answer_request(CAT_AN_INDEX);
	}
#elif WITHANTSELECT2
	gantmanual = 1;
	gantennas [bi] = loadvfy8up(RMT_ANTENNA_BASE(b), 0, ANTMODE_COUNT - 1, getdefantenna(f));	/* вытаскиваем номер включённой антенны */
	if (aistate != 0)
	{
		cat_answer_request(CAT_AN_INDEX);
	}
#elif WITHANTSELECT
	gantennas [bi] = loadvfy8up(RMT_ANTENNA_BASE(b), 0, ANTMODE_COUNT - 1, 0);	/* вытаскиваем номер включённой антенны */
	if (aistate != 0)
	{
		cat_answer_request(CAT_AN_INDEX);
	}
#endif /* WITHANTSELECT || WITHANTSELECTRX */
#if WITHAUTOTUNER
	// todo: добавить учет включенной антенны
	tunercap = loadvfy8up(offsetof(struct nvmap, bands[b].tunercap), CMIN, CMAX, tunercap);
	tunerind = loadvfy8up(offsetof(struct nvmap, bands[b].tunerind), LMIN, LMAX, tunerind);
	tunertype = loadvfy8up(offsetof(struct nvmap, bands[b].tunertype), 0, KSCH_COUNT - 1, tunertype);
	tunerwork = loadvfy8up(offsetof(struct nvmap, bands[b].tunerwork), 0, 1, tunerwork);
#endif /* WITHAUTOTUNER */
#if WITHSPECTRUMWF
	gzoomxpow2 [bi] = loadvfy8up(offsetof(struct nvmap, bands[b].gzoomxpow2), 0, BOARD_FFTZOOM_POW2MAX, 0);	/* масштаб панорамы */
	gtopdbspe [bi] = loadvfy8up(offsetof(struct nvmap, bands[b].gtopdbspe), WITHTOPDBMIN, WITHTOPDBMAX, WITHTOPDBDEFAULT);		/* нижний предел FFT */
	gbottomdbspe [bi] = loadvfy8up(offsetof(struct nvmap, bands[b].gbottomdbspe), WITHBOTTOMDBMIN, WITHBOTTOMDBMAX, WITHBOTTOMDBDEFAULT);	/* верхний предел FFT */
	gtopdbwfl [bi] = loadvfy8up(offsetof(struct nvmap, bands[b].gtopdbwfl), WITHTOPDBMIN, WITHTOPDBMAX, WITHTOPDBDEFAULT);		/* нижний предел FFT waterflow */
	gbottomdbwfl [bi] = loadvfy8up(offsetof(struct nvmap, bands[b].gbottomdbwfl), WITHBOTTOMDBMIN, WITHBOTTOMDBMAX, WITHBOTTOMDBDEFAULT);	/* верхний предел FFT waterflow */
#endif /* WITHSPECTRUMWF */
}

static void catchangesplit(
	uint_fast8_t enable,
	int_fast16_t delta
	)
{
	gsplitmode = enable ? VFOMODES_VFOSPLIT : VFOMODES_VFOINIT;
}

#endif /* WITHCAT */

/* расчёты частот ПЧ */

#if 1
/* Смена знака параметра в зависимости от lsbflag */
#define UPPERTOSIGN(lsbflag, value) (((lsbflag) == 0) ? (value) : - (value))
#define UPPERTOSIGN16(lsbflag, value) (((lsbflag) == 0) ? (value) : - (value))
#else
static int_fast32_t UPPERTOSIGN(
	uint_fast8_t lsbflag,
	int_fast32_t v
	)
{
	return lsbflag ? - v : v;
}
static int_fast16_t UPPERTOSIGN16(
	uint_fast8_t lsbflag,
	int_fast16_t v
	)
{
	return lsbflag ? - v : v;
}
#endif
/* 
 * получить число герц - тон CW сигнала (в десятках герц).
 * Для телеграфа отображается частота сигнала при частоте cwpitch, при SSB - при нулевых биениях
 */
static int_least16_t 
//NOINLINEAT
gettone_bymode(
	uint_fast8_t mode		/* код режима работы */
	)
{
	if (mode != MODE_CW)
		return 0;
	return gcwpitch10 * CWPITCHSCALE;
}


/* 
 * получить число герц - тон CW сигнала (в десятках герц).
 * Для телеграфа отображается частота сигнала при частоте cwpitch, при SSB - при нулевых биениях
 */
static int_least16_t 
//NOINLINEAT
gettone_bysubmode(
	uint_fast8_t submode,		/* код режима работы */
	uint_fast8_t forcelsb		/* когда режим работы smart, требуется этот параметр */
	)
{
	const int_least16_t t = gettone_bymode(submodes [submode].mode);
	return UPPERTOSIGN16(getsubmodelsb(submode, forcelsb), t);
}

static uint_fast8_t getforcelsb(uint_fast32_t freq)
{
	return freq < BANDFUSBFREQ;
}

/* После установки нового режима работы кнопками попадаем сюда.
 * В случае необходимости корректируется
 * частота приема (gfreq) для сохранения прежнего тона биений.
 */
static void
gsubmodechange(
	uint_fast8_t newsubmode, 
	uint_fast8_t bi				/* bank index */
	)
{
	uint_fast32_t freq = gfreqs [bi];
	const uint_fast8_t forcelsb = getforcelsb(freq);

	/* коррекция частоты настройки для сохранения постоянного тона при переходе на узкую плолсу */
	const int_fast16_t delta = stayfreq ? 0 : (gettone_bysubmode(newsubmode, forcelsb) - gettone_bysubmode(gsubmode, forcelsb));

	/* Обработка изменения частоты из-за изменения режима.
		контроль изменения частоты убран, так если происходит попытка "выпрыгнуть" из
		любительского диапазона - это имеет смысл, при входе из обзорного в любительский
		ограничение по диапазону (который шире) смысла не имеет.
		*/

	if (delta < 0)
	{
		/* автоматическое изменение "вниз" */
		//const uint_fast32_t lowfreq = bandsmap [b].bottom;
		freq = prevfreq(freq, freq - (- delta), 10UL, TUNE_BOTTOM);
		gfreqs [bi] = freq;

		{
			const vindex_t v = getvfoindex(bi);
			savebandstate(v, bi); // записать все параметры настройки (кроме частоты) в область данных диапазона */
			savebandfreq(v, bi);	/* сохранение частоты в текущем VFO */
		}
	}
	else if (delta > 0)
	{
		/* автоматическое изменение "вверх" */
		//const uint_fast32_t topfreq =  bandsmap [b].top;
		//freq = nextfreq(freq, freq + delta10 * 10UL, 10UL, topfreq);
		freq = nextfreq(freq, freq + delta, 10UL, TUNE_TOP);
		gfreqs [bi] = freq;

		{
			const vindex_t v = getvfoindex(bi);
			savebandstate(v, bi); // записать все параметры настройки (кроме частоты) в область данных диапазона */
			savebandfreq(v, bi);	/* сохранение частоты в текущем VFO */
		}
	}
	else
	{
		{
			const vindex_t v = getvfoindex(bi);
			savebandstate(v, bi); // записать все параметры настройки (кроме частоты) в область данных диапазона */
		}
	}
}


#if defined (RTC1_TYPE)

	static uint_fast16_t grtcyear;
	static uint_fast8_t grtcmonth, grtcday;
	static uint_fast8_t grtchour, grtcminute, grtcsecounds;

	static uint_fast8_t grtcstrobe;
	static uint_fast8_t grtcstrobe_shadow;

	static void getstamprtc(void)
	{
		grtcstrobe = 0;
		grtcstrobe_shadow = 0;
		board_rtc_getdatetime(& grtcyear, & grtcmonth, & grtcday, & grtchour, & grtcminute, & grtcsecounds);
	}

	static void board_setrtcstrobe(uint_fast8_t val)
	{
		if (val != grtcstrobe_shadow && val != 0)
		{
			board_rtc_setdatetime(grtcyear, grtcmonth, grtcday, grtchour, grtcminute, 0);
		}
		grtcstrobe_shadow = val;
	}

#endif /* defined (RTC1_TYPE) */



#define BRSCALE 1200UL

/* скорость 115200 не добавлена из соображений невозможностти точного формирования на atmega
   при частоте генератора 8 МГц
   */
static const FLASHMEM uint_fast8_t catbr2int [] =
{
	1200uL / BRSCALE,	// 1200
	2400uL / BRSCALE,	// 2400
	4800uL / BRSCALE,	// 4800
	9600uL / BRSCALE,	// 9600
	19200uL / BRSCALE,	// 19200
	38400uL / BRSCALE,	// 38400
	57600uL / BRSCALE,	// 57600
//#if CPU_FREQ >= 10000000
	115200uL / BRSCALE,	// 115200
//#endif /* CPUSTYLE_ARM */
};


static int_fast32_t
//NOINLINEAT
getsynthref(
	uint_fast8_t mode		/* код семейства режимов работы */
	)
{
#if defined(REFERENCE_FREQ)
	(void) mode;
	return refbase + refbias;
#else
	(void) mode;
	return 0;
#endif
}
// Возврат кода варианта управления гетеродином конвертора
// LOCODE_UPPER, LOCODE_LOWER or LOCODE_TARGETED
static uint_fast8_t
getsidelo0(
	uint_fast32_t freq
	)
{
#if defined (LO0_SIDE_F)
	/* Управление гетеродином конвертора предопределено в конфигурационном файле */
	/* Возвращаемый код зависит от частоты */
	return LO0_SIDE_F(freq);	// LOCODE_UPPER, LOCODE_LOWER or LOCODE_TARGETED

#elif defined (LO0_SIDE)
	/* Управление гетеродином конвертора предопределено в конфигурационном файле */
	return LO0_SIDE;	// LOCODE_UPPER, LOCODE_LOWER or LOCODE_TARGETED

#else

	#error Undefined LO0_SIDE value LOCODE_XXX

#endif
}

// Возврат кода варианта управления первым гетеродином
// LOCODE_UPPER, LOCODE_LOWER or LOCODE_TARGETED
static uint_fast8_t
getsidelo1(
	uint_fast32_t freq
	)
{
#if defined (LO1_SIDE_F)
	/* Управление первым гетеродином предопределено в конфигурационном файле */
	/* Возвращаемый код зависит от частоты */
	return LO1_SIDE_F(freq);	// LOCODE_UPPER, LOCODE_LOWER or LOCODE_TARGETED

#elif defined (LO1_SIDE)
	/* Управление первым гетеродином предопределено в конфигурационном файле */
	return LO1_SIDE;	// LOCODE_UPPER, LOCODE_LOWER or LOCODE_TARGETED

#else

	#error Undefined LO1_SIDE value LOCODE_XXX

#endif
}

#if WITHFIXEDBFO 	/* переворачивание боковой полосы за счёт изменения частоты первого гетеродина */
	#if defined(LSBONLO4DEFAULT)
		static uint_fast8_t glo4lsb = LSBONLO4DEFAULT;	/* признак LSB на последней ПЧ - модифицируется через меню. */
	#else
		static uint_fast8_t glo4lsb = 0;	/* признак LSB на последней ПЧ - модифицируется через меню. */
	#endif
#endif /* WITHFIXEDBFO */

// Возврат кода варианта управления последнм гетеродином
// LOCODE_UPPER, LOCODE_LOWER or LOCODE_TARGETED
static uint_fast8_t
getsidelo4(void)
{
#if defined (LO4_SIDE)
	/* Управление последгтм гетеродином предопределено в конфигурационном файле */
	return LO4_SIDE;	// LOCODE_UPPER, LOCODE_LOWER or LOCODE_TARGETED

#elif WITHDUALFLTR	/* Переворот боковых за счёт переключения фильтра верхней или нижней боковой полосы */

	return LOCODE_TARGETED;	// LOCODE_UPPER, LOCODE_LOWER or LOCODE_TARGETED

#elif WITHFIXEDBFO	/* переворачивание боковой полосы за счёт изменения частоты первого гетеродина */

	return glo4lsb ? LOCODE_UPPER : LOCODE_LOWER;		/* признак LSB на последней ПЧ */

#else

	#error Undefined LO4_SIDE value LOCODE_XXX and no WITHDUALFLTR or WITHFIXEDBFO

#endif
}

/* получение частоты опорника конвертора */
static int_fast32_t
getlo0(
	   uint_fast32_t lo0hint
	   )
{
	return (int_fast32_t) lo0hint;
}

/* возврат желаемой частоты гетеродина конвертора */
static uint_fast32_t
gethintlo0(
	uint_fast32_t freq,
	uint_fast8_t lo0side	/* формируем гетеродин для указанной боковой полосы */
	)
{
#if XVTR_R820T2
	const uint_fast32_t lo0step = R820T_LOSTEP;
	// дискретность перестройки гетеродина конвертора
	if (lo0side == LOCODE_UPPER)		/* При преобразовании на этом гетеродине происходит инверсия спектра */
		return ((freq + R820T_IFFREQ) + lo0step / 2) / lo0step * lo0step;
	else if (lo0side == LOCODE_LOWER)	/* При преобразовании на этом гетеродине нет инверсии спектра */
		return ((freq - R820T_IFFREQ) + lo0step / 2) / lo0step * lo0step;
	else
		return 0;
#elif 0
	// используется конвертор с фиксировнным гетеродином
	return 116000000uL;
#else
	return 0;
#endif	
}


#if FQMODEL_UW3DI

static uint_fast8_t
getlo2xtal(
	uint_fast8_t mix2lsb,	/* формируем гетеродин для указанной боковой полосы */
	uint_fast8_t hintlo2	/* код пятиисотых килогерц приходит в зависимости от частоты настройки в режиме интерполятора */
	)
{
	uint_fast8_t n;
	if (mix2lsb)
		n = (hintlo2 + IF2FREQTOP / HINTLO2_GRANULARITY);
	else
		n = labs(hintlo2 - IF2FREQBOTOM / HINTLO2_GRANULARITY);
	switch (n)
	{
		case 8000000L / HINTLO2_GRANULARITY:	return 0;	/* 1.8 и 14 мегагерц */
		case 10000000L / HINTLO2_GRANULARITY:	return 1;	/* 3.5 мегагерц */
		case 13500000L / HINTLO2_GRANULARITY:	return 2;	/* 7 мегагерц */
		case 4000000L / HINTLO2_GRANULARITY:	return 3;	/* 10.1 мегагерц */
		case 12000000L / HINTLO2_GRANULARITY:	return 4;	/* 18.68 мегагерц */
		case 15000000L / HINTLO2_GRANULARITY:	return 5;	/* 21 мегагерц */
		case 18500000L / HINTLO2_GRANULARITY:	return 6;	/* 24.89 мегагерц */
		case 22000000L / HINTLO2_GRANULARITY:	return 7;	/* 28.0 мегагерц */
		case 22500000L / HINTLO2_GRANULARITY:	return 8;	/* 28.5 мегагерц */
		case 23000000L / HINTLO2_GRANULARITY:	return 9;	/* 29.0 мегагерц */
		case 23500000L / HINTLO2_GRANULARITY:	return 10;	/* 29.5 мегагерц */
	}
	return 0;
}

#endif	/* FQMODEL_UW3DI */

/* Возврат номера кварца для второго гетеродина */
static uint_fast8_t
gethintlo2(
	uint_fast32_t freq
	)
{
#if FQMODEL_UW3DI
	return freq / HINTLO2_GRANULARITY;
#else
	return 0;
#endif	
}

/* получаем частоту LO2 для текущего режима работы */
static int_fast32_t 
//NOINLINEAT
getlo2(
	const filter_t * workfilter,
	uint_fast8_t mode,		/* код семейства режимов работы */
	uint_fast8_t mix2lsb,	/* формируем гетеродин для указанной боковой полосы */
	uint_fast8_t tx,		/* признак работы в режиме передачи */
	uint_fast8_t hintlo2	/* код пятиисотых килогерц приходит в зависимости от частоты настройки в режиме интерполятора */
	)
{
#if WITHWFM
	if (mode == MODE_WFM)
		return 0;
#endif /* WITHWFM */
#if FQMODEL_10M7_500K && (LO2_SIDE == LOCODE_TARGETED)

	/* RU6BK: 1-st if=10.7 MHz, 2-nd if=500k, fixed BFO */
	const int_fast32_t if2 = 10700000L;
	const int_fast32_t if3 = 500000L;
	if (mix2lsb)
		return if2 + if3;
	else
		return if2 - if3;
	
#elif FQMODEL_UW3DI
	// LO2 формируется набором кварцев или PLL
	// получаем частоту для справки
	if (mix2lsb)
		return (hintlo2 + IF2FREQTOP / HINTLO2_GRANULARITY) * HINTLO2_GRANULARITY;
	else
		return labs(hintlo2 - IF2FREQBOTOM / HINTLO2_GRANULARITY) * HINTLO2_GRANULARITY;

#elif /*defined (PLL2_TYPE) && */ (LO2_SIDE != LOCODE_INVALID)
	// LO2 формируется PLL с зависимостью от режима работы и приёма/передачи
	return (int_fast32_t) (((uint_least64_t) getsynthref(mode) * * getplo2n(workfilter, tx) / * getplo2r(workfilter, tx)) >> LO2_POWER2);

#elif (LO2_SIDE != LOCODE_INVALID)
	// LO2 формируется PLL или прямо берётся с опорного генератора
	(void) tx;
	return (int_fast32_t) (((uint_least64_t) getsynthref(mode) * LO2_PLL_N / LO2_PLL_R) >> LO2_POWER2);

#else
	// LO2 отсутствует
	(void) mode;
	(void) tx;
	return 0;

#endif
}

/* получаем LO3 для текущего режима работы */
static int_fast32_t 
//NOINLINEAT
getlo3(
	uint_fast8_t mode,		/* код семейства режимов работы */
	uint_fast8_t mix3lsb,		/* формируем гетеродин для указанной боковой полосы */
	uint_fast8_t tx		/* признак работы в режиме передачи */
	)
{	
#if (LO3_SIDE != LOCODE_INVALID)
  #if LO3_FREQADJ	/* подстройка частоты гетеродина через меню. */
	return (lo3base + lo3offset) >> LO3_POWER2;
  #else
	return (int_fast32_t) ((uint_least64_t) getsynthref(mode) * LO3_PLL_N / LO3_PLL_R >> LO3_POWER2);
  #endif

#else
	/* смесителя #2A вообще нет в тракте */
	(void) mode;
	(void) mix3lsb;
	return 0;
#endif
}

/* перенастройка формирования гетеродина для указанного режима. */
static void
//NOINLINEAT
update_lo0(
	uint_fast32_t lo0hint,		/* код частоты ковертора (не всегда совпадает с частотой) */
	uint_fast8_t lo0side
	)
{
	const uint_fast8_t enable = lo0side != LOCODE_INVALID;
	board_set_xvrtr(enable);
	synth_lo0_setfreq(getlo0(lo0hint), enable);
#if XVTR_R820T2
	board_set_swaprts(enable);	/* При преобразовании на этом гетеродине происходит инверсия спектра */
#else /* XVTR_R820T2 */
	board_set_swaprts(lo0side == LOCODE_LOWER);	/* При преобразовании на этом гетеродине происходит инверсия спектра */
#endif /* XVTR_R820T2 */
}

/* перенастройка формирования гетеродина для указанного режима. */
static void
//NOINLINEAT
update_lo2(
	uint_fast8_t pathi,		// номер тракта - 0/1: main/sub
	const filter_t * workfilter,
	uint_fast8_t mode,		/* код семейства режимов работы */
	uint_fast8_t lsb,		/* формируем гетеродин для указанной боковой полосы */
	int_fast32_t f,			/* частота, которую хотим получить на выходе DDS */
	uint_fast8_t od,		/* делитель перед подачей на смеситель (1, 2, 4, 8...) */
	uint_fast8_t tx,		/* признак работы в режиме передачи */
	uint_fast8_t hint		/* код пятиисотых килогерц приходит в зависимости от частоты настройки в режиме интерполятора */
	)
{
#if FQMODEL_UW3DI
	board_set_lo2xtal(getlo2xtal(lsb, hint));	// установка номера кварца
#elif defined (PLL2_TYPE) && (LO2_SIDE != LOCODE_INVALID)
	(void) hint;
	prog_pll2_r(getplo2r(workfilter, tx));		/* программирование PLL2 в случае управляемой частоты второго гетеродина */
	prog_pll2_n(getplo2n(workfilter, tx));		/* программирование PLL2 в случае управляемой частоты второго гетеродина */
#elif LO2_DDSGENERATED
	(void) mode;
	(void) hint;
	(void) lsb;
	(void) tx;
	synth_lo2_setfreq(pathi, f, od);		// for IGOR or EW2DZ or FQMODEL_10M7_500K
#else
	/* смесителя #2 вообще нет в тракте */
	(void) mode;
	(void) hint;
	(void) lsb;
	(void) f;
	(void) od;
	(void) tx;
#endif
}


/* перенастройка формирования гетеродина для указанного режима. */
static void
//NOINLINEAT
update_lo3(
	uint_fast8_t pathi,		// номер тракта - 0/1: main/sub
	uint_fast8_t mode,		/* код семейства режимов работы */
	uint_fast8_t lsb,		/* формируем гетеродин для указанной боковой полосы */
	int_fast32_t f,			/* частота, которую хотим получить на выходе DDS */
	uint_fast8_t od,		/* делитель перед подачей на смеситель (1, 2, 4, 8...) */
	uint_fast8_t tx			/* признак работы в режиме передачи */
	)
{
	(void) mode;
	(void) lsb;
	(void) tx;
	synth_lo3_setfreq(pathi, f, od);
}

// return value: LOCODE_UPPER, LOCODE_LOWER or LOCODE_TARGETED
static uint_fast8_t
getsidelo6(
	uint_fast8_t mode,		/* код семейства режимов работы */
	uint_fast8_t tx
	)
{
#if defined (LO6_SIDE)
	/* Управление этим гетеродином предопределено в конфигурационном файле */
	return LO6_SIDE;	// LOCODE_UPPER, LOCODE_LOWER or LOCODE_TARGETED
#elif WITHIF4DSP
	return LOCODE_TARGETED;	/* Этот гетеродин управляется для получения требуемой боковой */
#else /* WITHIF4DSP */
	return LOCODE_LOWER;	/* При преобразовании на этом гетеродине нет инверсии спектра */
#endif /* WITHIF4DSP */
}

#if WITHIF4DSP

// Значение частоты среза для фильтра ПЧ
// при применении алгоритма Уивера.
// Возвращаем полную полосу пропускания тракта.
// INT16_MAX - для обхода фильтра
static  int_fast16_t
getif6bw(
	uint_fast8_t mode,		/* код семейства режимов работы */
	uint_fast8_t tx,
	uint_fast8_t wide		/* способ расчета полосы пропускания */
	)
{
	const uint_fast8_t bwseti = mdt [mode].bwsetis [tx];	// индекс банка полос пропускания для данного режима
	const int_fast16_t bw6 = mdt [mode].bw6s [tx];
	if (bw6 != 0)
		return bw6;

	switch (mode)
	{
#if WITHMODEM
	case MODE_MODEM:
		// для передачи берется из таблицы
		return (uint_fast64_t) modembr2int100 [gmodemspeed] * 28 / 100 / 10;	// bw=symbol rate * 1.4

#endif /* WITHMODEM */

#if WITHFREEDV
	case MODE_FREEDV:
#endif /* WITHFREEDV */
	case MODE_CW:
	case MODE_SSB:
	case MODE_RTTY:
	case MODE_DIGI:
		if (! wide)
		{
			return bwseti_getwidth(bwseti);
		}
		else
		{
			const int_fast16_t lowcut = bwseti_getlow(bwseti);
			const int_fast16_t highcut = bwseti_gethigh(bwseti);
			return (highcut - lowcut); 
		}

	case MODE_ISB:
		{
			const int_fast16_t highcut = bwseti_gethigh(bwseti);
			return 2 * highcut; 
		}

	// большинство режимов по констатне из тассива mdt [mode].
	default:
		if (tx)
			return INT16_MAX;	/* PASSTROUGH */
		return bwseti_gethigh(bwseti) * 2;
	}
	return 0;
}


/* получить левый (низкочастотный) скат полосы пропускания для отображения "шторки" на спектранализаторе */
int_fast16_t
hamradio_getleft_bp(uint_fast8_t pathi)
{
	const uint_fast8_t bi = getbankindex_pathi(pathi);
	const uint_fast32_t freq = gfreqs [bi];
	const uint_fast8_t forcelsb = getforcelsb(freq);
	const uint_fast8_t alsbmode = getsubmodelsb(gsubmode, forcelsb);	// Принимаемая модуляция на нижней боковой
	const FLASHMEM struct modetempl * const pmodet = getmodetempl(gsubmode);
	const uint_fast8_t bwseti = pmodet->bwsetis [gtx];
	const uint_fast8_t mode = submodes [gsubmode].mode;
	const int_fast16_t cwpitch = gcwpitch10 * CWPITCHSCALE;

	switch (mode)
	{
	case MODE_SSB:
	case MODE_DIGI:
		return alsbmode ? - bwseti_gethigh(bwseti) : bwseti_getlow(bwseti);

	case MODE_CW:
		return alsbmode ? - bwseti_gethigh(bwseti) + cwpitch: bwseti_getlow(bwseti) - cwpitch;

	default:
		return - getif6bw(mode, gtx, bwseti_getwide(bwseti)) / 2;	// TODO: учесть возврат INT16_MAX
	}
}

/* получить правый (высокочастотный) скат полосы пропускания для отображения "шторки" на спектранализаторе */
int_fast16_t
hamradio_getright_bp(uint_fast8_t pathi)
{
	const uint_fast8_t bi = getbankindex_pathi(pathi);
	const uint_fast32_t freq = gfreqs [bi];
	const uint_fast8_t forcelsb = getforcelsb(freq);
	const uint_fast8_t alsbmode = getsubmodelsb(gsubmode, forcelsb);	// Принимаемая модуляция на нижней боковой
	const FLASHMEM struct modetempl * const pmodet = getmodetempl(gsubmode);
	const uint_fast8_t bwseti = pmodet->bwsetis [gtx];
	const uint_fast8_t mode = submodes [gsubmode].mode;
	const int_fast16_t cwpitch = gcwpitch10 * CWPITCHSCALE;

	switch (mode)
	{
	case MODE_SSB:
	case MODE_DIGI:
		return alsbmode ? - bwseti_getlow(bwseti) : bwseti_gethigh(bwseti);

	case MODE_CW:
		return alsbmode ? - bwseti_getlow(bwseti) + cwpitch : bwseti_gethigh(bwseti) - cwpitch;

	default:
		return getif6bw(mode, gtx, bwseti_getwide(bwseti)) / 2;	// TODO: учесть возврат INT16_MAX
	}
}

#endif /* WITHIF4DSP */

// Значение частоты для восстановления сигнала
static  int_fast32_t
getlo6(
	uint_fast8_t mode,		/* код семейства режимов работы */
	uint_fast8_t tx,
	uint_fast8_t wide,		/* способ осчёта полосы пропускания */
	int_fast16_t ifshift
	)
{
#if WITHIF4DSP
	const uint_fast8_t bwseti = mdt [mode].bwsetis [tx];	// индекс банка полос пропускания для данного режима
	switch (mode)
	{
	case MODE_ISB:
		return 0;

	case MODE_SSB:
	case MODE_DIGI:
	case MODE_CW:
		if (wide)
		{
			const int_fast16_t lowcut = bwseti_getlow(bwseti);
			const int_fast16_t highcut = bwseti_gethigh(bwseti);
			return (highcut - lowcut) / 2 + lowcut + ifshift; 	// частота центра полосы пропускания
		}
		else
		{
			return tx != 0 ? 0 : gcwpitch10 * CWPITCHSCALE;
		}

	case MODE_DRM:
		return DEFAULT_DRM_PITCH;	/* тон DRM - 12 кГц*/

	case MODE_RTTY:
		return DEFAULT_RTTY_PITCH;	/* тон DIGI modes - 2.125 кГц */

#if WITHMODEM
	case MODE_MODEM:
		return tx ? 0 : 0;		// модем принимает квадратуры
#endif /* WITHMODEM */

#if WITHFREEDV
	case MODE_FREEDV:
		return tx ? 0 : 0;		// модем принимает квадратуры
#endif /* WITHFREEDV */

	default:
		// AM. NFM и остальные - принимаются на baseband
		return 0;
	}
#else /* WITHIF4DSP */
	return 0; //- ifshift;	// В недопустимых режимах значение 0 уже обеспечено.
#endif /* WITHIF4DSP */
}

// Выходные частоты устройства (для SSB - частота подавленной несущей).
// Для классических структутр приёмо-передатчиков передача телеграфа делается разбалансом
// формирователя SSB или иным способом из сигнала гетеродина - это указывается
// возвратом "0" в режиме CW на передаче.
// Для IF DSP и DDC/DUC структур нулевая частота при передаче означает подачу на DUC постоянной составляющей в baseband.

static  int_fast32_t
getif6(
	uint_fast8_t mode,		/* код семейства режимов работы */
	uint_fast8_t tx,
	uint_fast8_t wide		/* способ осчёта полосы пропускания */
	)
{
	switch (mode)
	{
	case MODE_CW:
		return tx != 0 ? 0 : gcwpitch10 * CWPITCHSCALE;

	case MODE_DRM:
		return DEFAULT_DRM_PITCH;	/* тон DRM - 12 кГц*/

	case MODE_RTTY:
		return DEFAULT_RTTY_PITCH;	/* тон RTTY modes - 2.125 кГц */

	default:
		return 0;
	}
}

/*
 * Возврат значения ПЧ для цифровой обработки
 * 1/4 FS (12 kHz) или 0
*/
static  int_fast32_t
getlo5(
	uint_fast8_t mode,		/* код семейства режимов работы */
	uint_fast8_t tx
	)
{
#if WITHIF4DSP
	return dsp_get_ifreq();	/* 1/4 FS (12 kHz) или 0 для DSP */
#else /* WITHIF4DSP */
	return 0;
#endif /* WITHIF4DSP */
}

// Сторона гетеродина, переносящего сигнал ПЧ с 12 кГц на baseband для цифровой обработки
// Или, в классических структурах, перенос с последней ПЧ на звук.
// return value: LOCODE_UPPER, LOCODE_LOWER or LOCODE_TARGETED

static uint_fast8_t
getsidelo5(
	uint_fast8_t mode,		/* код семейства режимов работы */
	uint_fast8_t tx
	)
{
#if defined (LO5_SIDE)
	/* Управление этим гетеродином предопределено в конфигурационном файле */
	return LO5_SIDE;	// LOCODE_UPPER, LOCODE_LOWER or LOCODE_TARGETED
#elif WITHFIXEDBFO || WITHDUALBFO	/* переворачивание боковой полосы за счёт изменения частоты первого гетеродина */
	return LOCODE_LOWER;	/* При преобразовании на этом гетеродине нет инверсии спектра */
#else
	return mdt [mode].lo5side [tx];
#endif /* WITHFIXEDBFO || WITHDUALBFO */
}

static uint_fast8_t
getlo4enable(
	uint_fast8_t mode,
	uint_fast8_t tx
	)
{
#if defined (LO5_SIDE)
	return 1;
#else
	return mdt [mode].lo5side [tx] != LOCODE_INVALID;
#endif
}


static int_fast32_t
getlo4ref(
	const filter_t * workfilter,
	uint_fast8_t mode,			/* код семейства режима работы */
	uint_fast8_t mix4lsb,		/* формируем гетеродин для указанной боковой полосы */
	uint_fast8_t tx				/* для режима передачи - врежиме CW - смещения частоты не требуется. */
	)
{
#if WITHFIXEDBFO || WITHDUALFLTR
	// Выравнивание IF с привязкой к скату фильтра основной селекции
	return getlo4baseflt(workfilter) + lo4offset;
#elif WITHDUALBFO
	// Выравнивание IF с привязкой к скату фильтра основной селекции
	return getlo4baseflt(workfilter) + lo4offsets [mix4lsb];
#elif WITHIF4DSP
	// Выравнивание IF с привязкой к центру фильтра основной селекции
	return getif3filtercenter(workfilter);
#else
	return getif3byedge(workfilter, mode, mix4lsb, tx, gcwpitch10);
#endif
}

// вызывается из user mode
static uint_fast8_t
getactualtune(void)
{
	return tunemode || (catenable && cattunemode) || reqautotune || reqautotune2 || hardware_get_tune();
}

// вызывается из user mode
// Возвращает поизнак необходимости сбросить мощность сейчас (например, запрос от автотюнера)
static uint_fast8_t
getactualdownpower(void)
{
	return reqautotune || reqautotune2 || hardware_get_tune();
}

#if WITHTX

/* возвращаем 0..100 для кода на разъеме ACC */
static uint_fast8_t
getbandf2adjust(uint_fast8_t lpfno)
{
	if (lpfno >= ARRAY_SIZE(gbandf2adj))
		return 100;
	return gbandf2adj [lpfno];
}

/* Возвращает WITHPOWERTRIMMIN..WITHPOWERTRIMMAX */
static uint_fast8_t
getactualpower(void)
{
#if WITHPOWERTRIM
	return getactualdownpower() ? gtunepower : (gclassamode ? gclassapower : gnormalpower.value);

#elif WITHPOWERLPHP
	/* установить выходную мощность передатчика WITHPOWERTRIMMIN..WITHPOWERTRIMMAX */
	return getactualdownpower() ? pwrmodes [gpwratunei].code : pwrmodes [gpwri].code;

#else
	return WITHPOWERTRIMMAX;

#endif /* WITHPOWERLPHP */
}

#endif /* WITHTX */

// вызывается из user mode - признак передачи в режиме данных
static uint_fast8_t
getcattxdata(void)
{
	return catenable && catstatetx && catstatetxdata;
}

// получить submode для тракта, возможно замененный на CWZ в случае формирования сигнала настройки.
static uint_fast8_t
getasubmode(uint_fast8_t pathi)
{
	const uint_fast8_t bi = getbankindex_pathi(pathi);	/* vfo bank index */
	/* преустановка всех параметров приемника или передатчика под новый режим */
	const uint_fast8_t pathsubmode = getsubmode(bi);

#if WITHMODESETSMART
	const uint_fast8_t submode = (getactualtune() == 0) ? pathsubmode : SUBMODE_CWZSMART;	// mode_cwz обеспечивает формирование сигнала самоконтроля при передаче.
#else /* WITHMODESETSMART */
	const uint_fast8_t submode = (getactualtune() == 0) ? pathsubmode : SUBMODE_CWZ;	// mode_cwz обеспечивает формирование сигнала самоконтроля при передаче.
#endif /* WITHMODESETSMART */
	return submode;
}

// get actual mode
// Только показ названия режима (и CAT) используют getsubmode(bi);
// Для установки режимов надо использовать данную функцию.
static uint_fast8_t
getamode(uint_fast8_t pathi)
{
	const uint_fast8_t asubmode = getasubmode(pathi);
	return gdatamode && getactualtune() == 0 ? MODE_DIGI :  submodes [asubmode].mode;
}


/*
 * Установка параметров, влияющих на работу валкодера, цветовой схемой дисплея.
 */
static void
//NOINLINEAT
updateboard2(void)
{
#if WITHENCODER
	encoder_set_resolution(encresols [genc1pulses], genc1dynamic);
#endif /* WITHENCODER */
	display_setbgcolor(gbluebgnd ? COLORMAIN_BLUE : COLORMAIN_BLACK);
}


static uint_fast8_t
//NOINLINEAT
getlsbfull(
	uint_fast8_t lsb,
	const uint_fast8_t * sides,
	uint_fast8_t size
	)
{
	while (size --)
		lsb ^= * sides ++ == LOCODE_UPPER;
	return lsb; 
}

/* Получение признака LSB для LO0..LO6 */
static uint_fast8_t
//NOINLINEAT
getlsbloX(
	uint_fast8_t lsb,
	uint_fast8_t keyindex,	// 0..6 - номер LOx
	const uint_fast8_t * sides,
	uint_fast8_t size
	)
{
	const uint_fast8_t keyside = sides [keyindex];
	if (keyside == LOCODE_TARGETED)
		return getlsbfull(lsb, sides, size);
	return keyside == LOCODE_UPPER;
}

/* получение значение делителя в тракте lo1 перед подачей на смеситель */
static uint_fast8_t getlo1div(
	uint_fast8_t tx
	)
{
#if LO1FDIV_ADJ
	return 1U << lo1powmap [tx];
#elif defined (LO1_POWER2)
	return 1U << LO1_POWER2;
#else
	return 1U;
#endif
}

/* получение значение делителя в тракте lo2 перед подачей на смеситель */
static uint_fast8_t getlo2div(
	uint_fast8_t tx
	)
{
#if LO2FDIV_ADJ
	return 1U << lo2powmap [tx];
#elif defined (LO2_POWER2)
	return 1U << LO2_POWER2;
#else
	return 1U;
#endif
}

/* получение значение делителя в тракте lo3 перед подачей на смеситель */
static uint_fast8_t getlo3div(
	uint_fast8_t tx
	)
{
#if LO3FDIV_ADJ
	return 1U << lo3powmap [tx];
#elif defined (LO3_POWER2)
	return 1U << LO3_POWER2;
#else
	return 1U;
#endif
}

/* получение значение делителя в тракте lo4 перед подачей на смеситель */
static uint_fast8_t getlo4div(
	uint_fast8_t tx
	)
{
#if LO4FDIV_ADJ
	return 1U << lo4powmap [tx];
#elif defined (LO4_POWER2)
	return 1U << LO4_POWER2;
#else
	return 1U;
#endif
}

// speex

#if WITHINTEGRATEDDSP


#ifdef WITHLEAKYLMSANR

#define LEAKYLMSDLINE_SIZE 256 //512 // was 256 //2048   // dline_size
// 1024 funktioniert nicht
typedef struct
{// Automatic noise reduction
	// Variable-leak LMS algorithm
	// taken from (c) Warren Pratts wdsp library 2016
	// GPLv3 licensed
//	#define DLINE_SIZE 256 //512 //2048  // dline_size
	int16_t n_taps; // =     64; //64;                       // taps
	int16_t delay; // =    16; //16;                       // delay
	int dline_size; // = LEAKYLMSDLINE_SIZE;
	//int ANR_buff_size = FFT_length / 2.0;
	int position;// = 0;
	float32_t two_mu;// =   0.0001;   typical: 0.001 to 0.000001  = 1000 to 1 -> div by 1000000     // two_mu --> "gain"
	uint32_t two_mu_int;
	float32_t gamma;// =    0.1;      typical: 1.000 to 0.001  = 1000 to 1 -> div by 1000           // gamma --> "leakage"
	uint32_t gamma_int;
	float32_t lidx;// =     120.0;                      // lidx
	float32_t lidx_min;// = 0.0;                      // lidx_min
	float32_t lidx_max;// = 200.0;                      // lidx_max
	float32_t ngamma;// =   0.001;                      // ngamma
	float32_t den_mult;// = 6.25e-10;                   // den_mult
	float32_t lincr;// =    1.0;                      // lincr
	float32_t ldecr;// =    3.0;                     // ldecr
	//int ANR_mask = ANR_dline_size - 1;
	//int mask;// = DLINE_SIZE - 1;
	int in_idx;// = 0;
	float32_t d [LEAKYLMSDLINE_SIZE];
	float32_t w [LEAKYLMSDLINE_SIZE];
	uint8_t on;// = 0;
	uint8_t notch;// = 0;
} lLMS;

static lLMS leakyLMS;

static void AudioDriver_LeakyLmsNr_Init(void)
{
    /////////////////////// LEAKY LMS noise reduction
    leakyLMS.n_taps =     64; //64;                       // taps
    leakyLMS.delay =    16; //16;                       // delay
    leakyLMS.dline_size = LEAKYLMSDLINE_SIZE;
    //int ANR_buff_size = FFT_length / 2.0;
    leakyLMS.position = 0;
    leakyLMS.two_mu =   0.0001;                     // two_mu --> "gain"
    leakyLMS.two_mu_int = 100;
    leakyLMS.gamma =    0.1;                      // gamma --> "leakage"
    leakyLMS.gamma_int = 100;
    leakyLMS.lidx =     120.0;                      // lidx
    leakyLMS.lidx_min = 0.0;                      // lidx_min
    leakyLMS.lidx_max = 200.0;                      // lidx_max
    leakyLMS.ngamma =   0.001;                      // ngamma
    leakyLMS.den_mult = 6.25e-10;                   // den_mult
    leakyLMS.lincr =    1.0;                      // lincr
    leakyLMS.ldecr =    3.0;                     // ldecr
    //int leakyLMS.mask = leakyLMS.dline_size - 1;
    //leakyLMS.mask = LEAKYLMSDLINE_SIZE - 1;
    leakyLMS.in_idx = 0;
    leakyLMS.on = 0;
    leakyLMS.notch = 0;
    /////////////////////// LEAKY LMS END

}

// Automatic noise reduction
// Variable-leak LMS algorithm
// taken from (c) Warren Pratts wdsp library 2016
// GPLv3 licensed
void AudioDriver_LeakyLmsNr(float32_t * in_buff, float32_t * out_buff, int buff_size, int notch)
{
    int i, j, idx;
    float32_t c0, c1;
    float32_t y, error, sigma, inv_sigp;
    float32_t nel, nev;
	for (i = 0; i < buff_size; i++)
	{
		leakyLMS.d [leakyLMS.in_idx] = in_buff[i];

		y = 0;
		sigma = 0;

		for (j = 0; j < leakyLMS.n_taps; j ++)
		{
			idx = (leakyLMS.in_idx + j + leakyLMS.delay) % leakyLMS.dline_size;
			y += leakyLMS.w [j] * leakyLMS.d [idx];
			sigma += leakyLMS.d [idx] * leakyLMS.d [idx];
		}
		inv_sigp = 1 / (sigma + (float32_t) 1e-10);
		error = leakyLMS.d [leakyLMS.in_idx] - y;

		if (notch)
		{ // automatic notch filter
			out_buff[i] = error;
		}
		else
		{ // noise reduction
			out_buff[i] = y;
		}
//          leakyLMS.out_buff[2 * i + 1] = 0.0;

		if ((nel = error * (1 - leakyLMS.two_mu * sigma * inv_sigp)) < 0)
			nel = - nel;
		if ((nev = leakyLMS.d [leakyLMS.in_idx] - (1 - leakyLMS.two_mu * leakyLMS.ngamma) * y - leakyLMS.two_mu * error * sigma * inv_sigp) < 0)
			nev = - nev;
		if (nev < nel)
		{
			if ((leakyLMS.lidx += leakyLMS.lincr) > leakyLMS.lidx_max)
				leakyLMS.lidx = leakyLMS.lidx_max;
		}
		else
		{
			if ((leakyLMS.lidx -= leakyLMS.ldecr) < leakyLMS.lidx_min)
				leakyLMS.lidx = leakyLMS.lidx_min;
		}
		leakyLMS.ngamma = leakyLMS.gamma * (leakyLMS.lidx * leakyLMS.lidx) * (leakyLMS.lidx * leakyLMS.lidx) * leakyLMS.den_mult;

		c0 = 1 - leakyLMS.two_mu * leakyLMS.ngamma;
		c1 = leakyLMS.two_mu * error * inv_sigp;

		for (j = 0; j < leakyLMS.n_taps; j++)
		{
			idx = (leakyLMS.in_idx + j + leakyLMS.delay) % leakyLMS.dline_size;
			leakyLMS.w [j] = c0 * leakyLMS.w [j] + c1 * leakyLMS.d [idx];
		}
		leakyLMS.in_idx = (leakyLMS.in_idx + leakyLMS.dline_size - 1) % leakyLMS.dline_size;
	}
}

#endif /* WITHLEAKYLMSANR */

#if WITHRTTY

#define TRX_SAMPLERATE ARMI2SRATE

#define IIR_BIQUAD_MAX_SECTIONS 15
#define IIR_BIQUAD_SECTION_ORDER 2

typedef struct iir_filter {
    int sections;
    int sect_ord;
    double a[IIR_BIQUAD_MAX_SECTIONS * (IIR_BIQUAD_SECTION_ORDER + 1)];
    double b[IIR_BIQUAD_MAX_SECTIONS * (IIR_BIQUAD_SECTION_ORDER + 1)];
    double d[(IIR_BIQUAD_MAX_SECTIONS + 1) * IIR_BIQUAD_SECTION_ORDER];
} iir_filter_t;

void biquad_create(iir_filter_t *filter, int sections);
double biquad_update(iir_filter_t *filter, double x);
void iir_freq_resp(const iir_filter_t *filter, double *h, double fs, double f);
void biquad_zero(struct iir_filter *filter);
void biquad_init_lowpass(iir_filter_t *filter, double fs, double f);
void biquad_init_highpass(iir_filter_t *filter, double fs, double f);
void biquad_init_bandpass(iir_filter_t *filter, double fs, double f1, double f2);
void biquad_init_bandstop(iir_filter_t *filter, double fs, double f1, double f2);


typedef struct {
    float re;
    float im;
} complex_s;

typedef struct {
    double re;
    double im;
} complex_d;

static void complex_mul(complex_d *a, complex_d *b);
static void complex_div(complex_d *p, complex_d *q);

static void biquad_init_band(iir_filter_t *filter, double fs, double f1, double f2, int stop);


void biquad_create(iir_filter_t *filter, int sections)
{
	memset(filter, 0x00, sizeof * filter);

	filter->sections = sections;
	filter->sect_ord = IIR_BIQUAD_SECTION_ORDER;

	memset(filter->a, 0x00, sizeof filter->a);
	memset(filter->b, 0x00, sizeof filter->b);
	memset(filter->d, 0x00, sizeof filter->d);
}

void iir_freq_resp(const iir_filter_t *filter, double *h, double fs, double f)
{
    const double *a = filter->a;
    const double *b = filter->b;
    double w = 2.0 * M_PI * f / fs;
    complex_d _z, m, p, q;
    int i, k;

    /* On unit circle, 1/z = ~z (complex conjugate) */

    _z.re = cos(w);
    _z.im = -sin(w);

    m.re = 1.0;
    m.im = 0.0;

    for (i = 0; i < filter->sections; i += 1)
    {
        k = filter->sect_ord;
        p.re = b[k];
        p.im = 0;
        while (k > 0) {
            k -= 1.0;
            complex_mul(&p, &_z);
            p.re += b[k];
        }

        k = filter->sect_ord;
        q.re = a[k];
        q.im = 0;
        while (k > 0) {
            k -= 1.0;
            complex_mul(&q, &_z);
            q.re += a[k];
        }

        complex_div(&p, &q);
        complex_mul(&m, &p);
        a += filter->sect_ord + 1;
        b += filter->sect_ord + 1;
    }

    h[0] = m.re;
    h[1] = m.im;
}

double biquad_update(struct iir_filter *filter, double x)
{
    double *a = filter->a;
    double *b = filter->b;
    double *d = filter->d;
    int stages = filter->sections;
    int k;
    double y;

    for (k = 0; k < stages; k += 1) {
        y = x * b[0];
        y += d[0] * b[1] + d[1] * b[2];
        y -= d[2] * a[1] + d[3] * a[2];
        d[1] = d[0];
        d[0] = x;
        x = y;
        d += 2;
        a += 3;
        b += 3;
    }
    d[1] = d[0];
    d[0] = x;

    return x;
}

void biquad_zero(struct iir_filter *filter)
{
    double *a = filter->a;
    double *b = filter->b;
    int n;
    int i;

    n = filter->sections;
    for (i = 0; i < n; i += 1) {
        b[0] = 0;
        b[1] = 0;
        b[2] = 0;
        a[0] = 0;
        a[1] = 0;
        a[2] = 0;
    }
}

void biquad_init_lowpass(struct iir_filter *filter, double fs, double f)
{
    double *a = filter->a;
    double *b = filter->b;
    double w = 2.0 * M_PI * f / fs;
    double phi, alpha;
    int i, k;
    int n;

    n = filter->sections;
    for (i = 0; i < n; i += 1) {
        k = n - i - 1.0;
        phi = M_PI / (4.0 * n) * (k * 2.0 + 1.0);
        alpha = sin(w) * cos(phi);

        b[0] = (1.0 - cos(w)) / (2.0 * (1.0 + alpha));
        b[1] = (1.0 - cos(w)) / (1.0 + alpha);
        b[2] = (1.0 - cos(w)) / (2.0 * (1.0 + alpha));
        a[0] = 1.0;
        a[1] = -2.0 * cos(w) / (1.0 + alpha);
        a[2] = (1.0 - alpha) / (1.0 + alpha);
        a += 3;
        b += 3;
    }

    for (i = 0; i < (n + 1) * 2; i += 1)
        filter->d[i] = 0;
}

void biquad_init_highpass(struct iir_filter *filter, double fs, double f) {
    double *a = filter->a;
    double *b = filter->b;
    double w = 2.0 * M_PI * f / fs;
    double phi, alpha;
    int n, i, k;

    n = filter->sections;

    for (i = 0; i < n; i += 1) {
        k = n - i - 1;
        phi = M_PI / (4.0 * n) * (k * 2.0 + 1.0);
        alpha = sin(w) * cos(phi);

        b[0] = (1.0 + cos(w)) / (2.0 * (1.0 + alpha));
        b[1] = -(1.0 + cos(w)) / (1.0 + alpha);
        b[2] = (1.0 + cos(w)) / (2.0 * (1.0 + alpha));
        a[1] = -2.0 * cos(w) / (1.0 + alpha);
        a[2] = (1.0 - alpha) / (1.0 + alpha);
        a += 3;
        b += 3;
    }

    for (i = 0; i < (n + 1) * 2; i += 1)
        filter->d[i] = 0;
}

void biquad_init_bandpass(struct iir_filter *filter, double fs, double f1, double f2)
{
	return biquad_init_band(filter, fs, f1, f2, 0);
}

void biquad_init_bandstop(struct iir_filter *filter, double fs, double f1, double f2)
{
	return biquad_init_band(filter, fs, f1, f2, 1);
}

static void complex_square(complex_d *s)
{
    double x, y;

    x = s->re;
    y = s->im;

    s->re = x * x - y * y;
    s->im = 2.0 * x * y;
}

static void complex_sqrt(complex_d *s)
{
    double x, y;
    double r, phi;

    x = s->re;
    y = s->im;
    if (x == 0 && y == 0) {
        return;
    }

    /* Converting to polar */
    phi = atan2(y, x);
    r = sqrt(x * x + y * y);

    /* Square root */
    phi /= 2.0;
    r = sqrt(r);

    /* Back to cartesian */
    s->re = r * cos(phi);
    s->im = r * sin(phi);
}

static void complex_mul(complex_d *a, complex_d *b)
{
    double x, y;

    x = a->re * b->re - a->im * b->im;
    y = a->re * b->im + a->im * b->re;

    a->re = x;
    a->im = y;
}

static void complex_div(complex_d *p, complex_d *q)
{
    double x, y;
    double r, phi;
    complex_d b;

    x = q->re;
    y = q->im;
    if (x == 0 && y == 0) {
        return;
    }

    /* Converting to polar */
    phi = atan2(y, x);
    r = sqrt(x * x + y * y);

    /* Reciprocal */
    phi = -phi;
    r = 1.0 / r;

    /* Back to cartesian */
    b.re = r * cos(phi);
    b.im = r * sin(phi);

    complex_mul(p, &b);
}

/*
 *  Convert from continuous (s) to discrete (z)
 *  using bilinear transform
 *  ts: sample period (T)
 */

static void bilinear_transform(complex_d *z, complex_d *s, double ts)
{
    complex_d p, q;
    double x = s->re;
    double y = s->im;

    x *= ts / 2.0;
    y *= ts / 2.0;
    p.re = 1.0 + x;
    p.im = y;
    q.re = 1.0 - x;
    q.im = -y;
    complex_div(&p, &q);
    *z = p;
}

/*
 *  Compute bandpass or bandstop filter parameters
 */

static void biquad_init_band(struct iir_filter *filter, double fs, double f1, double f2, int stop)
{
    double ts = 1.0 / fs;
    double bw, f;
    double w;
    complex_d p, q;
    complex_d z, s;
    double phi;
    complex_d _z, p_lp, p_bp;
    double k, x, y;
    double wa1, wa2, wa;
    double *a = filter->a;
    double *b = filter->b;
    int n, i;

    f = sqrt(f1 * f2);
    w = 2.0 * M_PI * f / fs;

    /* Map to continuous-time frequencies (pre-warp) */

    wa1 = 2.0 * fs * tan(M_PI * f1 * ts);
    wa2 = 2.0 * fs * tan(M_PI * f2 * ts);

    bw = wa2 - wa1;
    wa = sqrt(wa1 * wa2);

    n = filter->sections;

    for (i = 0; i < n; i += 1) {
        phi = M_PI / 2.0 + M_PI * (2.0 * i + 1.0) / (n * 2.0);
        x = cos(phi);
        y = sin(phi);

        p_lp.re = x * bw / (wa * 2.0);
        p_lp.im = y * bw / (wa * 2.0);

        /*
         *  Map every low-pass pole to a complex conjugate
         *  pair of band-bass poles
         */

        s = p_lp;
        complex_square(&s);
        s.re = 1.0 - s.re;
        s.im = 0.0 - s.im;
        complex_sqrt(&s);
        x = p_lp.re - s.im;
        y = p_lp.im + s.re;
        p_bp.re = x * wa;
        p_bp.im = y * wa;

        /*
         *  Convert every pair from continuous (s)
         *  to discrete (z) using bilinear transform
         */

        bilinear_transform(&z, &p_bp, ts);

        x = z.re;
        y = z.im;

        /*
         *  Find denominator coefficients from
         *  the complex conjugate pair of poles
         */

        a[0] = 1.0;
        a[1] = -2 * x;
        a[2] = x * x + y * y;

        if (stop) {
            /* Band-stop: zeros at Ï‰ and ~Ï‰ */
            s.re = 0;
            s.im = wa;
            bilinear_transform(&z, &s, ts);
            x = z.re;
            y = z.im;

            b[0] = 1.0;
            b[1] = -2.0 * x;
            b[2] = x * x + y * y;
        } else {
            /* Band-pass: zeros at Â±1 */
            b[0] = 1.0;
            b[1] = 0.0;
            b[2] = -1.0;
        }

        /* Scale the parameters to get unity gain in the bassband */

        if (stop) {
            /* Band-stop: unity gain at zero frequency */
            _z.re = 1.0;
            _z.im = 0.0;
        } else {
            /* Band-pass: unity gain at Ï‰ */
            _z.re = cos(w);
            _z.im = -sin(w);
        }

        p.re = b[2];
        p.im = 0;
        complex_mul(&p, &_z);
        p.re += b[1];
        complex_mul(&p, &_z);
        p.re += b[0];

        q.re = a[2];
        q.im = 0;
        complex_mul(&q, &_z);
        q.re += a[1];
        complex_mul(&q, &_z);
        q.re += 1.0;

        complex_div(&p, &q);

        x = p.re;
        y = p.im;
        k = 1.0 / sqrt(x * x + y * y);

        b[0] *= k;
        b[1] *= k;
        b[2] *= k;

        a += filter->sect_ord + 1;
        b += filter->sect_ord + 1;
    }

    for (i = 0; i < (n + 1) * 2; i += 1)
        filter->d[i] = 0;
}

void fill_biquad_coeffs(iir_filter_t *filter, float32_t *coeffs, uint8_t sect_num)
{
	//transpose and save coefficients
	uint16_t ind = 0;
	for(uint8_t sect = 0; sect < sect_num; sect++)
	{
		coeffs[ind + 0] = filter->b[sect * 3 + 0];
		coeffs[ind + 1] = filter->b[sect * 3 + 1];
		coeffs[ind + 2] = filter->b[sect * 3 + 2];
		coeffs[ind + 3] = -filter->a[sect * 3 + 1];
		coeffs[ind + 4] = -filter->a[sect * 3 + 2];
		ind += 5;
	}
}

#define BIQUAD_COEFF_IN_STAGE 5													  // coefficients in manual Notch filter order

#if (defined(LAY_800x480))
#define RTTY_DECODER_STRLEN 66 // length of decoded string
#else
#define RTTY_DECODER_STRLEN 30 // length of decoded string
#endif

#define RTTY_LPF_STAGES 2
#define RTTY_BPF_STAGES 2
#define RTTY_BPF_WIDTH (RTTY_Shift / 4)

#define RTTY_SYMBOL_CODE (0b11011)
#define RTTY_LETTER_CODE (0b11111)

typedef enum {
	RTTY_STATE_WAIT_START,
	RTTY_STATE_BIT,
} rtty_state_t;

typedef enum {
	RTTY_MODE_LETTERS,
	RTTY_MODE_SYMBOLS
} rtty_charSetMode_t;

typedef enum {
    RTTY_STOP_1,
    RTTY_STOP_1_5,
    RTTY_STOP_2
} rtty_stopbits_t;

// Public variables
//extern char RTTY_Decoder_Text[RTTY_DECODER_STRLEN + 1];

// Public methods
extern void RTTYDecoder_Init(void);                   // initialize the CW decoder
extern void RTTYDecoder_Process(const float32_t *bufferIn, unsigned len); // start CW decoder for the data block


//Ported from https://github.com/df8oe/UHSDR/blob/active-devel/mchf-eclipse/drivers/audio/rtty.c

//char RTTY_Decoder_Text[RTTY_DECODER_STRLEN + 1] = {0}; // decoded string

static rtty_state_t RTTY_State = RTTY_STATE_WAIT_START;
static rtty_charSetMode_t RTTY_charSetMode = RTTY_MODE_LETTERS;
static uint16_t RTTY_oneBitSampleCount = 0;
static uint8_t RTTY_byteResult = 0;
static uint16_t RTTY_byteResult_bnum = 0;
static int32_t RTTY_DPLLBitPhase;
static int32_t RTTY_DPLLOldVal;

//lpf
static float32_t RTTY_LPF_Filter_Coeffs[BIQUAD_COEFF_IN_STAGE * RTTY_LPF_STAGES] = {0};
static float32_t RTTY_LPF_Filter_State[2 * RTTY_LPF_STAGES];
static arm_biquad_cascade_df2T_instance_f32 RTTY_LPF_Filter;

//mark
static float32_t RTTY_Mark_Filter_Coeffs[BIQUAD_COEFF_IN_STAGE * RTTY_BPF_STAGES];
static float32_t RTTY_Mark_Filter_State[2 * RTTY_BPF_STAGES];
static arm_biquad_cascade_df2T_instance_f32 RTTY_Mark_Filter;

//space
static float32_t RTTY_Space_Filter_Coeffs[BIQUAD_COEFF_IN_STAGE * RTTY_BPF_STAGES];
static float32_t RTTY_Space_Filter_State[2 * RTTY_BPF_STAGES];
static arm_biquad_cascade_df2T_instance_f32 RTTY_Space_Filter;

static const char RTTY_Letters[] = {
	'\0', 'E', '\n', 'A', ' ', 'S', 'I', 'U',
	'\r', 'D', 'R', 'J', 'N', 'F', 'C', 'K',
	'T', 'Z', 'L', 'W', 'H', 'Y', 'P', 'Q',
	'O', 'B', 'G', ' ', 'M', 'X', 'V', ' '};

static const char RTTY_Symbols[32] = {
	'\0', '3', '\n', '-', ' ', '\a', '8', '7',
	'\r', '$', '4', '\'', ',', '!', ':', '(',
	'5', '"', ')', '2', '#', '6', '0', '1',
	'9', '?', '&', ' ', '.', '/', ';', ' '};

static int RTTYDecoder_waitForStartBit(float32_t sample);
static int RTTYDecoder_getBitDPLL(float32_t sample, int *val_p);
static int RTTYDecoder_demodulator(float32_t sample);
static float32_t RTTYDecoder_decayavg(float32_t average, float32_t input, int weight);

// FSK shift: 170 200 425 850
// FSK tone freq 1275 2125

static float RTTY_Speed = 50; //45.45;
#define	RTTY_Shift 455 //170;
// The standard mark and space tones are 2125 hz and 2295 hz respectively
#define RTTY_FreqMark DEFAULT_RTTY_PITCH		// /* mark тон DIGI modes - 2.125 кГц (1275 2125) */
#define	RTTY_FreqSpace (DEFAULT_RTTY_PITCH + RTTY_Shift)
static int RTTY_StopBits = RTTY_STOP_1;

void RTTYDecoder_Init(void)
{
	iir_filter_t f0;
	//speed
	RTTY_oneBitSampleCount = (uint16_t)roundf((float32_t)TRX_SAMPLERATE / RTTY_Speed);

	//RTTY LPF Filter
	biquad_create(& f0, RTTY_LPF_STAGES);
	biquad_init_lowpass(& f0, TRX_SAMPLERATE, RTTY_Speed * 2);
	fill_biquad_coeffs(& f0, RTTY_LPF_Filter_Coeffs, RTTY_LPF_STAGES);
	arm_biquad_cascade_df2T_init_f32(&RTTY_LPF_Filter, RTTY_LPF_STAGES, RTTY_LPF_Filter_Coeffs, RTTY_LPF_Filter_State);

	//RTTY mark filter
	biquad_create(& f0, RTTY_BPF_STAGES);
	biquad_init_bandpass(& f0, TRX_SAMPLERATE, RTTY_FreqMark - RTTY_BPF_WIDTH / 2, RTTY_FreqMark + RTTY_BPF_WIDTH / 2);
	fill_biquad_coeffs(& f0, RTTY_Mark_Filter_Coeffs, RTTY_BPF_STAGES);
	arm_biquad_cascade_df2T_init_f32(&RTTY_Mark_Filter, RTTY_BPF_STAGES, RTTY_Mark_Filter_Coeffs, RTTY_Mark_Filter_State);

	//RTTY space filter
	biquad_create(& f0, RTTY_BPF_STAGES);
	biquad_init_bandpass(& f0, TRX_SAMPLERATE, RTTY_FreqSpace - RTTY_BPF_WIDTH / 2, RTTY_FreqSpace + RTTY_BPF_WIDTH / 2);
	fill_biquad_coeffs(& f0, RTTY_Space_Filter_Coeffs, RTTY_BPF_STAGES);
	arm_biquad_cascade_df2T_init_f32(&RTTY_Space_Filter, RTTY_BPF_STAGES, RTTY_Space_Filter_Coeffs, RTTY_Space_Filter_State);

	//text
//	sprintf(RTTY_Decoder_Text, " RTTY: -");
//	addSymbols(RTTY_Decoder_Text, RTTY_Decoder_Text, RTTY_DECODER_STRLEN, " ", 1);
//	LCD_UpdateQuery.TextBar = 1;
}

void RTTYDecoder_Process(const float32_t *bufferIn, unsigned len)
{
	for (uint32_t buf_pos = 0; buf_pos < len; buf_pos++)
	{
		switch (RTTY_State)
		{
		case RTTY_STATE_WAIT_START: // not synchronized, need to wait for start bit
			if (RTTYDecoder_waitForStartBit(bufferIn[buf_pos]))
			{
				RTTY_State = RTTY_STATE_BIT;
				RTTY_byteResult_bnum = 1;
				RTTY_byteResult = 0;
			}
			break;
		case RTTY_STATE_BIT:
			// reading 7 more bits
			if (RTTY_byteResult_bnum < 8)
			{
				int bitResult = 0;
				if (RTTYDecoder_getBitDPLL(bufferIn[buf_pos], &bitResult))
				{
					switch (RTTY_byteResult_bnum)
					{
					case 6: // stop bit 1
					case 7: // stop bit 2
						if (bitResult == 0)
						{
							// not in sync
							RTTY_State = RTTY_STATE_WAIT_START;
						}
						if (RTTY_StopBits != RTTY_STOP_2 && RTTY_byteResult_bnum == 6)
						{
							// we pretend to be at the 7th bit after receiving the first stop bit if we have less than 2 stop bits
							// this omits check for 1.5 bit condition but we should be more or less safe here, may cause
							// a little more unaligned receive but without that shortcut we simply cannot receive these configurations
							// so it is worth it
							RTTY_byteResult_bnum = 7;
						}
						break;
					default:
						RTTY_byteResult |= (bitResult ? 1 : 0) << (RTTY_byteResult_bnum - 1);
					}
					RTTY_byteResult_bnum++;
				}
			}
			if (RTTY_byteResult_bnum == 8 && RTTY_State == RTTY_STATE_BIT)
			{
				char charResult;

				switch (RTTY_byteResult)
				{
				case RTTY_LETTER_CODE:
					RTTY_charSetMode = RTTY_MODE_LETTERS;
					// println(" ^L^");
					break;
				case RTTY_SYMBOL_CODE:
					RTTY_charSetMode = RTTY_MODE_SYMBOLS;
					// println(" ^F^");
					break;
				default:
					switch (RTTY_charSetMode)
					{
					case RTTY_MODE_SYMBOLS:
						charResult = RTTY_Symbols[RTTY_byteResult];
						break;
					case RTTY_MODE_LETTERS:
					default:
						charResult = RTTY_Letters[RTTY_byteResult];
						break;
					}
					//RESULT !!!!
					//print(charResult);
					PRINTF("%c", charResult);
//					char str[2] = {0};
//					str[0] = charResult;
//					if (strlen(RTTY_Decoder_Text) >= RTTY_DECODER_STRLEN)
//						shiftTextLeft(RTTY_Decoder_Text, 1);
//					strcat(RTTY_Decoder_Text, str);
//					LCD_UpdateQuery.TextBar = 1;
					break;
				}
				RTTY_State = RTTY_STATE_WAIT_START;
			}
		}
	}
}

// this function returns only 1 when the start bit is successfully received
static int RTTYDecoder_waitForStartBit(float32_t sample)
{
	int retval = 0;
	int bitResult;
	static int16_t wait_for_start_state = 0;
	static int16_t wait_for_half = 0;

	bitResult = RTTYDecoder_demodulator(sample);

	switch (wait_for_start_state)
	{
	case 0:
		// waiting for a falling edge
		if (bitResult != 0)
		{
			wait_for_start_state++;
		}
		break;
	case 1:
		if (bitResult != 1)
		{
			wait_for_start_state++;
		}
		break;
	case 2:
		wait_for_half = RTTY_oneBitSampleCount / 2;
		wait_for_start_state++;
		/* no break */
	case 3:
		wait_for_half--;
		if (wait_for_half == 0)
		{
			retval = (bitResult == 0);
			wait_for_start_state = 0;
		}
		break;
	}
	return retval;
}

// this function returns 1 once at the half of a bit with the bit's value
static int RTTYDecoder_getBitDPLL(float32_t sample, int *val_p)
{
	static int phaseChanged = 0;
	int retval = 0;

	if (RTTY_DPLLBitPhase < RTTY_oneBitSampleCount)
	{
		*val_p = RTTYDecoder_demodulator(sample);

		if (!phaseChanged && *val_p != RTTY_DPLLOldVal)
		{
			if (RTTY_DPLLBitPhase < RTTY_oneBitSampleCount / 2)
			{
				RTTY_DPLLBitPhase += RTTY_oneBitSampleCount / 32; // early
			}
			else
			{
				RTTY_DPLLBitPhase -= RTTY_oneBitSampleCount / 32; // late
			}
			phaseChanged = 1;
		}
		RTTY_DPLLOldVal = *val_p;
		RTTY_DPLLBitPhase++;
	}

	if (RTTY_DPLLBitPhase >= RTTY_oneBitSampleCount)
	{
		RTTY_DPLLBitPhase -= RTTY_oneBitSampleCount;
		retval = 1;
	}

	return retval;
}

// adapted from https://github.com/ukhas/dl-fldigi/blob/master/src/include/misc.h
static float32_t RTTYDecoder_decayavg(float32_t average, float32_t input, int weight)
{
	float32_t retval;
	if (weight <= 1)
	{
		retval = input;
	}
	else
	{
		retval = ((input - average) / (float32_t)weight) + average;
	}
	return retval;
}

// this function returns the bit value of the current sample
static int RTTYDecoder_demodulator(float32_t sample)
{
	float32_t space_mag = 0;
	float32_t mark_mag = 0;
	arm_biquad_cascade_df2T_f32(&RTTY_Space_Filter, &sample, &space_mag, 1);
	arm_biquad_cascade_df2T_f32(&RTTY_Mark_Filter, &sample, &mark_mag, 1);

	float32_t v1 = 0.0;
	// calculating the RMS of the two lines (squaring them)
	space_mag *= space_mag;
	mark_mag *= mark_mag;

	// RTTY decoding with ATC = automatic threshold correction
	float32_t helper = space_mag;
	space_mag = mark_mag;
	mark_mag = helper;
	static float32_t mark_env = 0.0;
	static float32_t space_env = 0.0;
	static float32_t mark_noise = 0.0;
	static float32_t space_noise = 0.0;
	// experiment to implement an ATC (Automatic threshold correction), DD4WH, 2017_08_24
	// everything taken from FlDigi, licensed by GNU GPLv2 or later
	// https://github.com/ukhas/dl-fldigi/blob/master/src/cw_rtty/rtty.cxx
	// calculate envelope of the mark and space signals
	// uses fast attack and slow decay
	mark_env = RTTYDecoder_decayavg(mark_env, mark_mag, (mark_mag > mark_env) ? RTTY_oneBitSampleCount / 4 : RTTY_oneBitSampleCount * 16);
	space_env = RTTYDecoder_decayavg(space_env, space_mag, (space_mag > space_env) ? RTTY_oneBitSampleCount / 4 : RTTY_oneBitSampleCount * 16);
	// calculate the noise on the mark and space signals
	mark_noise = RTTYDecoder_decayavg(mark_noise, mark_mag, (mark_mag < mark_noise) ? RTTY_oneBitSampleCount / 4 : RTTY_oneBitSampleCount * 48);
	space_noise = RTTYDecoder_decayavg(space_noise, space_mag, (space_mag < space_noise) ? RTTY_oneBitSampleCount / 4 : RTTY_oneBitSampleCount * 48);
	// the noise floor is the lower signal of space and mark noise
	float32_t noise_floor = (space_noise < mark_noise) ? space_noise : mark_noise;

	// Linear ATC, section 3 of www.w7ay.net/site/Technical/ATC
	// v1 = space_mag - mark_mag - 0.5 * (space_env - mark_env);

	// Compensating for the noise floor by using clipping
	float32_t mclipped = 0.0, sclipped = 0.0;
	mclipped = mark_mag > mark_env ? mark_env : mark_mag;
	sclipped = space_mag > space_env ? space_env : space_mag;
	if (mclipped < noise_floor)
	{
		mclipped = noise_floor;
	}
	if (sclipped < noise_floor)
	{
		sclipped = noise_floor;
	}

	// Optimal ATC (Section 6 of of www.w7ay.net/site/Technical/ATC)
	v1 = (mclipped - noise_floor) * (mark_env - noise_floor) - (sclipped - noise_floor) * (space_env - noise_floor) - 0.25 * ((mark_env - noise_floor) * (mark_env - noise_floor) - (space_env - noise_floor) * (space_env - noise_floor));
	arm_biquad_cascade_df2T_f32(&RTTY_LPF_Filter, &v1, &v1, 1);

	// RTTY without ATC, which works very well too!
	// inverting line 1
	/*mark_mag *= -1;

	// summing the two lines
	v1 = mark_mag + space_mag;

	// lowpass filtering the summed line
	arm_biquad_cascade_df2T_f32(&RTTY_LPF_Filter, &v1, &v1, 1);*/

	return (v1 > 0) ? 0 : 1;
}

#endif /* WITHRTTY */

#if WITHLMSAUTONOTCH

enum {
	AUTONOTCH_NUMTAPS = 64,
	AUTONOTCH_BUFFER_SIZE = FIRBUFSIZE * 4,
	AUTONOTCH_STATE_ARRAY_SIZE = AUTONOTCH_NUMTAPS + FIRBUFSIZE - 1,
};


#if 1
/* заглушки для проверки компиляции в режиме обработки float_64 по умолчанию.
 *
 */

/**
 * @brief Instance structure for the floating-point normalized LMS filter.
 */
typedef struct
{
        uint16_t numTaps;     /**< number of coefficients in the filter. */
        float64_t *pState;    /**< points to the state variable array. The array is of length numTaps+blockSize-1. */
        float64_t *pCoeffs;   /**< points to the coefficient array. The array is of length numTaps. */
        float64_t mu;         /**< step size that control filter coefficient updates. */
        float64_t energy;     /**< saves previous frame energy. */
        float64_t x0;         /**< saves previous input sample. */
} arm_lms_norm_instance_f64;


/**
 * @brief Instance structure for the floating-point LMS filter.
 */
typedef struct
{
        uint16_t numTaps;    /**< number of coefficients in the filter. */
        float64_t *pState;   /**< points to the state variable array. The array is of length numTaps+blockSize-1. */
        float64_t *pCoeffs;  /**< points to the coefficient array. The array is of length numTaps. */
        float64_t mu;        /**< step size that controls filter coefficient updates. */
} arm_lms_instance_f64;

void arm_lms_norm_init_f64(
        arm_lms_norm_instance_f64 * S,
        uint16_t numTaps,
		float64_t * pCoeffs,
		float64_t * pState,
		float64_t mu,
        uint32_t blockSize)
{
	  /* Assign filter taps */
	  S->numTaps = numTaps;

	  /* Assign coefficient pointer */
	  S->pCoeffs = pCoeffs;

	  /* Clear state buffer and size is always blockSize + numTaps - 1 */
	  memset(pState, 0, (numTaps + (blockSize - 1U)) * sizeof(float64_t));

	  /* Assign state pointer */
	  S->pState = pState;

	  /* Assign Step size value */
	  S->mu = mu;

	  /* Initialise Energy to zero */
	  S->energy = 0;

	  /* Initialise x0 to zero */
	  S->x0 = 0;

}

void arm_lms_norm_f64(
	arm_lms_norm_instance_f64 * S,
	const float64_t * pSrc,
	float64_t * pRef,
	float64_t * pOut,
	float64_t * pErr,
	uint32_t blockSize)
 {
	float64_t *pState = S->pState; /* State pointer */
	float64_t *pCoeffs = S->pCoeffs; /* Coefficient pointer */
	float64_t *pStateCurnt; /* Points to the current sample of the state */
	float64_t *px, *pb; /* Temporary pointers for state and coefficient buffers */
	float64_t mu = S->mu; /* Adaptive factor */
	float64_t acc, e; /* Accumulator, error */
	float64_t w; /* Weight factor */
	uint32_t numTaps = S->numTaps; /* Number of filter coefficients in the filter */
	uint32_t tapCnt, blkCnt; /* Loop counters */
	float64_t energy; /* Energy of the input */
	float64_t x0, in; /* Temporary variable to hold input sample and state */

	/* Initializations of error,  difference, Coefficient update */
	e = 0.0f;
	w = 0.0f;

	energy = S->energy;
	x0 = S->x0;

	/* S->pState points to buffer which contains previous frame (numTaps - 1) samples */
	/* pStateCurnt points to the location where the new input data should be written */
	pStateCurnt = &(S->pState[(numTaps - 1U)]);

	/* initialise loop count */
	blkCnt = blockSize;

	while (blkCnt > 0U) {
		/* Copy the new input sample into the state buffer */
		*pStateCurnt++ = *pSrc;

		/* Initialize pState pointer */
		px = pState;

		/* Initialize coefficient pointer */
		pb = pCoeffs;

		/* Read the sample from input buffer */
		in = *pSrc++;

		/* Update the energy calculation */
		energy -= x0 * x0;
		energy += in * in;

		/* Set the accumulator to zero */
		acc = 0.0f;

#if defined (ARM_MATH_LOOPUNROLL)

		/* Loop unrolling: Compute 4 taps at a time. */
		tapCnt = numTaps >> 2U;

		while (tapCnt > 0U) {
			/* Perform the multiply-accumulate */
			acc += (*px++) * (*pb++);

			acc += (*px++) * (*pb++);

			acc += (*px++) * (*pb++);

			acc += (*px++) * (*pb++);

			/* Decrement loop counter */
			tapCnt--;
		}

		/* Loop unrolling: Compute remaining taps */
		tapCnt = numTaps % 0x4U;

#else

/* Initialize tapCnt with number of samples */
tapCnt = numTaps;

#endif /* #if defined (ARM_MATH_LOOPUNROLL) */

		while (tapCnt > 0U) {
			/* Perform the multiply-accumulate */
			acc += (*px++) * (*pb++);

			/* Decrement the loop counter */
			tapCnt--;
		}

		/* Store the result from accumulator into the destination buffer. */
		*pOut++ = acc;

		/* Compute and store error */
		e = (float64_t) *pRef++ - acc;
		*pErr++ = e;

		/* Calculation of Weighting factor for updating filter coefficients */
		/* epsilon value 0.000000119209289f */
		w = (e * mu) / (energy + 0.000000119209289);

		/* Initialize pState pointer */
		px = pState;

		/* Initialize coefficient pointer */
		pb = pCoeffs;

#if defined (ARM_MATH_LOOPUNROLL)

		/* Loop unrolling: Compute 4 taps at a time. */
		tapCnt = numTaps >> 2U;

		/* Update filter coefficients */
		while (tapCnt > 0U) {
			/* Perform the multiply-accumulate */
			*pb += w * (*px++);
			pb++;

			*pb += w * (*px++);
			pb++;

			*pb += w * (*px++);
			pb++;

			*pb += w * (*px++);
			pb++;

			/* Decrement loop counter */
			tapCnt--;
		}

		/* Loop unrolling: Compute remaining taps */
		tapCnt = numTaps % 0x4U;

#else

/* Initialize tapCnt with number of samples */
tapCnt = numTaps;

#endif /* #if defined (ARM_MATH_LOOPUNROLL) */

		while (tapCnt > 0U) {
			/* Perform the multiply-accumulate */
			*pb += w * (*px++);
			pb++;

			/* Decrement loop counter */
			tapCnt--;
		}

		x0 = *pState;

		/* Advance state pointer by 1 for the next sample */
		pState = pState + 1;

		/* Decrement loop counter */
		blkCnt--;
	}

	/* Save energy and x0 values for the next frame */
	S->energy = energy;
	S->x0 = x0;

	/* Processing is complete.
	 Now copy the last numTaps - 1 samples to the start of the state buffer.
	 This prepares the state buffer for the next function call. */

	/* Points to the start of the pState buffer */
	pStateCurnt = S->pState;

	/* copy data */
#if defined (ARM_MATH_LOOPUNROLL)

	/* Loop unrolling: Compute 4 taps at a time. */
	tapCnt = (numTaps - 1U) >> 2U;

	while (tapCnt > 0U) {
		*pStateCurnt++ = *pState++;
		*pStateCurnt++ = *pState++;
		*pStateCurnt++ = *pState++;
		*pStateCurnt++ = *pState++;

		/* Decrement loop counter */
		tapCnt--;
	}

	/* Loop unrolling: Compute remaining taps */
	tapCnt = (numTaps - 1U) % 0x4U;

#else

/* Initialize tapCnt with number of samples */
tapCnt = (numTaps - 1U);

#endif /* #if defined (ARM_MATH_LOOPUNROLL) */

	while (tapCnt > 0U) {
		*pStateCurnt++ = *pState++;

		/* Decrement loop counter */
		tapCnt--;
	}

}

#endif

typedef struct
{
	ARM_MORPH(arm_lms_norm_instance)	lms2Norm_instance;
	ARM_MORPH(arm_lms_instance)	    lms2_instance;
    FLOAT_t	                lms2StateF32 [AUTONOTCH_STATE_ARRAY_SIZE];
    FLOAT_t	                norm [AUTONOTCH_NUMTAPS];
    FLOAT_t	                ref [AUTONOTCH_BUFFER_SIZE];
    unsigned 					refold;
    unsigned 					refnew;
    FLOAT_t phonefence;
} LMSData_t;

static void hamradio_autonotch_init(LMSData_t * const lmsd)
{
	lmsd->phonefence = 1;
	const FLOAT_t mu = LOG10F(((5 + 1.0f) / 1500.0f) + 1.0f);
	//const float32_t mu = 0.0001f;		// UA3REO value
	ARM_MORPH(arm_lms_norm_init)(& lmsd->lms2Norm_instance, AUTONOTCH_NUMTAPS, lmsd->norm, lmsd->lms2StateF32, mu, FIRBUFSIZE);
	ARM_MORPH(arm_fill)(0, lmsd->ref, AUTONOTCH_BUFFER_SIZE);
	ARM_MORPH(arm_fill)(0, lmsd->norm, AUTONOTCH_NUMTAPS);
	lmsd->refold = 0;
	lmsd->refnew = FIRBUFSIZE;
}

//static volatile int nrestarts;

// pInput - входной буфер FIRBUFSIZE сэмплов
// pOutput - обработаный буфер FIRBUFSIZE сэмплов
static void hamradio_autonotch_process(LMSData_t * const lmsd, FLOAT_t * pInput, FLOAT_t * pOutput)
{
    static FLOAT_t errsig2 [FIRBUFSIZE];	/* unused output */
//	float32_t diag;
//	float32_t diag2;
//
//	arm_mean_f32(lmsd->ref, AUTONOTCH_BUFFER_SIZE, & diag);
//	arm_mean_f32(lmsd->norm, AUTONOTCH_NUMTAPS, & diag2);
//	if (__isnanf(diag) || __isinff(diag) || __isnanf(diag2) || __isinff(diag2))
//	{
//		ARM_MORPH(arm_fill)(0, lmsd->ref, AUTONOTCH_BUFFER_SIZE);
//		ARM_MORPH(arm_fill)(0, lmsd->norm, AUTONOTCH_NUMTAPS);
//		lmsd->refold = 0;
//		lmsd->refnew = FIRBUFSIZE;
//		++ nrestarts;
//	}
    ARM_MORPH(arm_copy)(pInput, & lmsd->ref [lmsd->refnew], FIRBUFSIZE);
    ARM_MORPH(arm_lms_norm)(& lmsd->lms2Norm_instance, pInput, & lmsd->ref [lmsd->refold], errsig2, pOutput, FIRBUFSIZE);
	lmsd->refold += FIRBUFSIZE;
	lmsd->refnew = lmsd->refold + FIRBUFSIZE;
	lmsd->refold %= AUTONOTCH_BUFFER_SIZE;
	lmsd->refnew %= AUTONOTCH_BUFFER_SIZE;

	FLOAT_t diagmin;
	FLOAT_t diagmax;
	uint32_t index;
	ARM_MORPH(arm_min)(pOutput, FIRBUFSIZE, & diagmin, & index);
	ARM_MORPH(arm_max_no_idx)(pOutput, FIRBUFSIZE, & diagmax);
	if (diagmin < - lmsd->phonefence || diagmax > lmsd->phonefence)
	{
		ARM_MORPH(arm_fill)(0, lmsd->ref, AUTONOTCH_BUFFER_SIZE);
		ARM_MORPH(arm_fill)(0, lmsd->norm, AUTONOTCH_NUMTAPS);
		ARM_MORPH(arm_fill)(0, pOutput, FIRBUFSIZE);
		lmsd->refold = 0;
		lmsd->refnew = FIRBUFSIZE;
		//++ nrestarts;
	}
}

#endif /* WITHLMSAUTONOTCH */

#if 1//! WITHSKIPUSERMODE

#if WITHNOSPEEX

	#define NOISE_REDUCTION_BLOCK_SIZE FIRBUFSIZE
	#define NOISE_REDUCTION_TAPS 64
	#define NOISE_REDUCTION_REFERENCE_SIZE (NOISE_REDUCTION_BLOCK_SIZE * 2)
	#define NOISE_REDUCTION_STEP 0.01f

#endif /* WITHNOSPEEX */

typedef struct rxaproc_tag
{
	// FIR audio filter
	FLOAT_t firEQcoeff [Ntap_rx_AUDIO];
	ARM_MORPH(arm_fir_instance) fir_instance;
	FLOAT_t fir_state [FIRBUFSIZE + Ntap_rx_AUDIO - 1];
	FLOAT_t wire1 [FIRBUFSIZE];

#if WITHNOSPEEX
	// NLMS NR
	arm_lms_norm_instance_f32 lms2_Norm_instance;
	float32_t lms2_stateF32 [NOISE_REDUCTION_TAPS + NOISE_REDUCTION_BLOCK_SIZE - 1];
	float32_t lms2_normCoeff_f32 [NOISE_REDUCTION_TAPS];
	float32_t ref [NOISE_REDUCTION_REFERENCE_SIZE];
	float32_t lms2_errsig2 [NOISE_REDUCTION_BLOCK_SIZE];
	uint_fast16_t refold;
	uint_fast16_t refnew;

#else /* WITHNOSPEEX */

//	#include "src/speex/arch.h"
//	#include "src/speex/speex_preprocess.h"

	SpeexPreprocessState * st_handle;

#endif /* WITHNOSPEEX */

#if WITHLMSAUTONOTCH
	// LMS auto notch
	LMSData_t lmsanotch;
#endif /* WITHLMSAUTONOTCH */
} rxaproc_t;

static RAM_D1 rxaproc_t rxaprocs [NTRX];

#endif /* ! WITHSKIPUSERMODE */

#if ! WITHNOSPEEX

	#if FIRBUFSIZE == 64
		#define SPEEXALLOCSIZE (NTRX * 15584)
	#elif FIRBUFSIZE == 128
		#define SPEEXALLOCSIZE (NTRX * 22584)
	#elif FIRBUFSIZE == 256
		#define SPEEXALLOCSIZE (NTRX * 38584)
	#elif FIRBUFSIZE == 512
		#define SPEEXALLOCSIZE (NTRX * 75448)
	#elif FIRBUFSIZE == 1024
		#define SPEEXALLOCSIZE (NTRX * 149176)
	#endif

#endif /* ! WITHNOSPEEX */

#if WITHUSEMALLOC

	#define ROUNDUP64(v) (((v) + 63uL) & ~ 63uL)

	#if ! WITHNOSPEEX
		static RAMHEAP uint8_t speexheap [ROUNDUP64(SPEEXALLOCSIZE)];
	#endif /* ! WITHNOSPEEX */

#endif /* WITHUSEMALLOC */

#if WITHUSEMALLOC

void *speex_alloc(int size)
{
   /* WARNING: this is not equivalent to malloc(). If you want to use malloc()
      or your own allocator, YOU NEED TO CLEAR THE MEMORY ALLOCATED. Otherwise
      you will experience strange bugs */
	void * p = calloc(size, 1);
	ASSERT(p != NULL);
	return p;
}

void speex_free (void *ptr)
{
	free(ptr);
}

#else /* WITHUSEMALLOC */

	#if SPEEXALLOCSIZE

	static int speexallocated = 0;

	static RAM_D2 uint8_t speexheapbuff [SPEEXALLOCSIZE];

	void *speex_alloc (int size)
	{
		size = (size + 0x03) & ~ 0x03;
		ASSERT((speexallocated + size) <= sizeof speexheapbuff / sizeof speexheapbuff [0]);
		if (! ((speexallocated + size) <= sizeof speexheapbuff / sizeof speexheapbuff [0]))
		{
			for (;;)
				;
		}
		void * p = (void *) (speexheapbuff + speexallocated);
		speexallocated += size;
		return p;
	}

	void speex_free (void *ptr)
	{
	}

	#endif /* SPEEXALLOCSIZE */
#endif /* WITHUSEMALLOC */

/* на слабых процессорах второй приемник без NR и автонотч */
static uint_fast8_t ispathprocessing(uint_fast8_t pathi)
{
#if CPUSTYLE_STM32MP1 || CPUSTYLE_XC7Z || CPUSTYLE_XCZU || CPUSTYPE_T113
	return 1;
#else /* CPUSTYLE_STM32MP1 || CPUSTYLE_XC7Z || CPUSTYLE_XCZU || CPUSTYPE_T113 */
	return pathi == 0;
#endif /* CPUSTYLE_STM32MP1 || CPUSTYLE_XC7Z || CPUSTYLE_XCZU || CPUSTYPE_T113 */
}

static void speex_update_rx(void)
{
	uint_fast8_t pathi;

	for (pathi = 0; pathi < NTRX; ++ pathi)
	{
		const uint_fast8_t amode = getamode(pathi);
#if ! WITHNOSPEEX
		spx_int32_t denoise = ispathprocessing(pathi) && gnoisereducts [amode];
		spx_int32_t supress = - (int) gnoisereductvl;
#endif /* ! WITHNOSPEEX */
		rxaproc_t * const nrp = & rxaprocs [pathi];

		// Получение параметров эквалайзера
		FLOAT_t * const dCoefs = nrp->firEQcoeff;
		dsp_recalceq_coeffs(pathi, dCoefs, Ntap_rx_AUDIO);	// calculate 1/2 of coefficients
		fir_expand_symmetric(dCoefs, Ntap_rx_AUDIO);	// Duplicate symmetrical part of coeffs.

#if WITHNOSPEEX
#else /* WITHNOSPEEX */
		SpeexPreprocessState * const st = nrp->st_handle;
		ASSERT(st != NULL);

		//PRINTF("speex_update_rx: amode=%d, pathi=%d, denoise=%d, supress=%d\n", (int) amode, (int) pathi, (int) denoise, (int) supress);
		speex_preprocess_ctl(st, SPEEX_PREPROCESS_SET_DENOISE, & denoise);
		speex_preprocess_ctl(st, SPEEX_PREPROCESS_SET_NOISE_SUPPRESS, & supress);

#endif /* WITHNOSPEEX */
	}
}

static void InitNoiseReduction(void)
{
	uint_fast8_t pathi;
	for (pathi = 0; pathi < NTRX; ++ pathi)
	{
		rxaproc_t * const nrp = & rxaprocs [pathi];

		ARM_MORPH(arm_fir_init)(& nrp->fir_instance, Ntap_rx_AUDIO, nrp->firEQcoeff, nrp->fir_state, FIRBUFSIZE);

#if WITHNOSPEEX

		arm_lms_norm_init_f32(& nrp->lms2_Norm_instance, NOISE_REDUCTION_TAPS, nrp->lms2_normCoeff_f32, nrp->lms2_stateF32, NOISE_REDUCTION_STEP, NOISE_REDUCTION_BLOCK_SIZE);
		ARM_MORPH(arm_fill)(0, nrp->ref, NOISE_REDUCTION_REFERENCE_SIZE);
		ARM_MORPH(arm_fill)(0, nrp->lms2_normCoeff_f32, NOISE_REDUCTION_TAPS);

		nrp->refold = 0;
		nrp->refnew = 0;
#else /* WITHNOSPEEX */

		nrp->st_handle = speex_preprocess_state_init(FIRBUFSIZE, ARMI2SRATE);
		//PRINTF("InitNoiseReduction: pathi=%d\n", (int) pathi);

#endif /* WITHNOSPEEX */

#if WITHLMSAUTONOTCH
		hamradio_autonotch_init(& nrp->lmsanotch);
#endif /* WITHLMSAUTONOTCH */
	}

#ifdef WITHLEAKYLMSANR
    AudioDriver_LeakyLmsNr_Init(); // RX
#endif /* WITHLEAKYLMSANR */

}

#if WITHNOSPEEX

static void processNoiseReduction(rxaproc_t * nrp, const float* bufferIn, float* bufferOut)
{
	arm_copy_f32(bufferIn, & nrp->ref [nrp->refnew], NOISE_REDUCTION_BLOCK_SIZE);
	arm_lms_norm_f32(& nrp->lms2_Norm_instance, bufferIn, & nrp->ref [nrp->refold], bufferOut, nrp->lms2_errsig2, NOISE_REDUCTION_BLOCK_SIZE);

	nrp->refold += NOISE_REDUCTION_BLOCK_SIZE;
	if (nrp->refold >= NOISE_REDUCTION_REFERENCE_SIZE)
		nrp->refold = 0;
	nrp->refnew = nrp->refold + NOISE_REDUCTION_BLOCK_SIZE;
	if (nrp->refnew >= NOISE_REDUCTION_REFERENCE_SIZE)
		nrp->refnew = 0;
}

#endif /* WITHNOSPEEX */

// user-mode processing
// На выходе входной сигнал без изменений
static FLOAT_t * afpnoproc(uint_fast8_t pathi, rxaproc_t * const nrp, FLOAT_t * p)
{
	// FIXME: speex внутри использует целочисленные вычисления
//	static const float32_t ki = 32768;
//	static const float32_t ko = 1. / 32768;
#if WITHNOSPEEX
	// не делать даже коррекцию АЧХ
	return p;
#else /* WITHNOSPEEX */
	// не делать даже коррекцию АЧХ
	#if ! WITHLEAKYLMSANR
//		arm_scale_f32(p, ki, p, FIRBUFSIZE);
//		speex_preprocess_estimate_update(nrp->st_handle, p);
//		arm_scale_f32(p, ko, p, FIRBUFSIZE);
	#endif /* ! WITHLEAKYLMSANR */
	return p;
#endif /* WITHNOSPEEX */
}

static FLOAT_t * afpcw(uint_fast8_t pathi, rxaproc_t * const nrp, FLOAT_t * p)
{
	// FIXME: speex внутри использует целочисленные вычисления
	static const FLOAT_t ki = 32768;
	static const FLOAT_t ko = 1. / 32768;
	const uint_fast8_t amode = getamode(pathi);
	const uint_fast8_t denoise = ispathprocessing(pathi) && gnoisereducts [amode];
	const uint_fast8_t anotch = ispathprocessing(pathi) && gnotch && notchmodes [gnotchtype].code == BOARD_NOTCH_AUTO;
	//////////////////////////////////////////////
	// Filtering
	// Use CMSIS DSP interface

#if WITHNOSPEEX
	if (denoise)
	{
		// Filtering and denoise.
		ARM_MORPH(arm_fir)(& nrp->fir_instance, p, nrp->wire1, FIRBUFSIZE);
		processNoiseReduction(nrp, nrp->wire1, p);	// result copy back
		return p;
	}
	else
	{
		// Filtering only.
		ARM_MORPH(arm_fir)(& nrp->fir_instance, p, nrp->wire1, FIRBUFSIZE);
		return nrp->wire1;
	}
#else /* WITHNOSPEEX */

	// Filtering and denoise.
	BEGIN_STAMP();
	ARM_MORPH(arm_fir)(& nrp->fir_instance, p, nrp->wire1, FIRBUFSIZE);
	END_STAMP();
	if (anotch)
	{
		hamradio_autonotch_process(& nrp->lmsanotch, nrp->wire1, nrp->wire1);
	}
	else
	{
		hamradio_autonotch_process(& nrp->lmsanotch, nrp->wire1, p);	// результат не используем
	}

#if WITHLEAKYLMSANR
	if (pathi == 0)
		AudioDriver_LeakyLmsNr(nrp->wire1, nrp->wire1, FIRBUFSIZE, 0);
#else /* WITHLEAKYLMSANR */
	if (ispathprocessing(pathi))
	{
		ARM_MORPH(arm_scale)(nrp->wire1, ki, nrp->wire1, FIRBUFSIZE);
		speex_preprocess_run(nrp->st_handle, nrp->wire1);
		ARM_MORPH(arm_scale)(nrp->wire1, ko, nrp->wire1, FIRBUFSIZE);
	}
#endif /* WITHLEAKYLMSANR */
	return nrp->wire1;

#endif /* WITHNOSPEEX */

}


// user-mode processing
// На выходе формируется тишина
// прием телетайпа в приемнике A
static FLOAT_t * afprtty(uint_fast8_t pathi, rxaproc_t * const nrp, FLOAT_t * p)
{
#if WITHRTTY
	if (pathi == 0)
	{
		RTTYDecoder_Process(p, FIRBUFSIZE);
	}
#endif /* WITHRTTY */
	//nrp->outsp = p;
	//ARM_MORPH(arm_fill)(0, p, FIRBUFSIZE);
	return afpnoproc(pathi, nrp, p);
}


// user-mode processing
void
audioproc_spool_user(void)
{
	speexel_t * p;
	if (takespeexready_user(& p))
	{
		// обработка и сохранение в savesampleout16stereo_user()
		uint_fast8_t pathi;
		FLOAT_t * outsp [NTRX];
		for (pathi = 0; pathi < NTRX; ++ pathi)
		{
			rxaproc_t * const nrp = & rxaprocs [pathi];
			const uint_fast8_t amode = getamode(pathi);
			// nrp->outsp указывает на результат обработки
			outsp [pathi] = mdt [amode].afproc [gtx] (pathi, nrp, p + pathi * FIRBUFSIZE);
		}
#if WITHAFEQUALIZER
		audio_rx_equalizer(outsp [0], FIRBUFSIZE);
#endif /* WITHAFEQUALIZER */
		//////////////////////////////////////////////
		// Save results
		unsigned i;
		for (i = 0; i < FIRBUFSIZE; ++ i)
		{
	#if WITHUSEDUALWATCH
			deliveryfloat(& speexoutfloat_user, outsp [0] [i], outsp [1] [i]);	// to AUDIO codec
	#else /* WITHUSEDUALWATCH */
			deliveryfloat(& speexoutfloat_user, outsp [0] [i], outsp [0] [i]);	// to AUDIO codec
	#endif /* WITHUSEDUALWATCH */

	}
		// Освобождаем буфер
		releasespeexbuffer_user(p);
	}
}

#else /* WITHINTEGRATEDDSP */

/* поддержка компиляции без Link Time Optimization */
void *speex_alloc (int size)
{
	return NULL;
}

void speex_free (void *ptr)
{
}

FLOAT_t local_pow(FLOAT_t x, FLOAT_t y)
{
	return 0;
}


FLOAT_t local_exp(FLOAT_t x)
{
	return 0;
}

FLOAT_t local_log(FLOAT_t x)
{
	return 0;
}

#endif /* WITHINTEGRATEDDSP */

/* функция вызывается при ожидании опросом разного рода событий связанных с аппаартурой,
 * в том числе наступление следующего кадра.
 * Выводить на экран строго нельзя. Отладочная печать нвозможна.
 * Вызывается в контексте USER.
 * */
void hardware_nonguiyield(void)
{

}

// Печать частоты в формате dddddd.ddd
static void printfreq(int_fast32_t freq)
{
	const ldiv_t v = ldiv(freq, 1000);
	PRINTF(PSTR("%s%ld.%03ld"), (v.quot >= 0 && freq < 0) ? "-" : "", v.quot, freq < 0 ? - v.rem : v.rem);
}

/* Получить частоту lo1 из частоты настройки */
static int_fast32_t synth_if1 [2];

uint_fast32_t synth_freq2lo1(
	uint_fast32_t freq,	/* частота на индикаторе устройства */
	uint_fast8_t pathi		/* номер тракта (main/sub) */
	)
{
	return (int_fast32_t) freq - synth_if1 [pathi];
}

/* Если изменяемый параметр отличается от старого значения - возврат 1 */
static uint_fast8_t
flagne_u8(uint_fast8_t * oldval, uint_fast8_t v)
{
	if (* oldval != v)
	{
		* oldval = v;
		return 1;
	}
	return 0;
}

/* Если изменяемый параметр отличается от старого значения - возврат 1 */
static uint_fast8_t
flagne_u16(uint_fast16_t * oldval, uint_fast16_t v)
{
	if (* oldval != v)
	{
		* oldval = v;
		return 1;
	}
	return 0;
}

/* Если изменяемый параметр отличается от старого значения - возврат 1 */
static uint_fast8_t
flagne_u32(uint_fast32_t * oldval, uint_fast32_t v)
{
	if (* oldval != v)
	{
		* oldval = v;
		return 1;
	}
	return 0;
}

#if WITHCAT

/* формирование запроса на информирование управляющего компьютера при изменении параметра. */
static uint_fast8_t
flagne_u8_cat(dualctl8_t * oldval, uint_fast8_t v, uint_fast8_t catindex)
{
	if (flagne_u8(& oldval->potvalue, v) != 0)
	{
		oldval->value = v;
		cat_answer_request(catindex);
		return 1;
	}
	return 0;
}

/* формирование запроса на информирование управляющего компьютера при изменении параметра. */
static uint_fast8_t
flagne_u16_cat(dualctl16_t * oldval, uint_fast16_t v, uint_fast8_t catindex)
{
	if (flagne_u16(& oldval->potvalue, v) != 0)
	{
		oldval->value = v;
		cat_answer_request(catindex);
		return 1;
	}
	return 0;
}

/* формирование запроса на информирование управляющего компьютера при изменении параметра. */
static uint_fast8_t
flagne_u32_cat(dualctl32_t * oldval, uint_fast32_t v, uint_fast8_t catindex)
{
	if (flagne_u32(& oldval->potvalue, v) != 0)
	{
		oldval->value = v;
		cat_answer_request(catindex);
		return 1;
	}
	return 0;
}

#define FLAGNE_U8_CAT(a,b,c) flagne_u8_cat((a), (b), (c))
#define FLAGNE_U16_CAT(a,b,c) flagne_u16_cat((a), (b), (c))
#define FLAGNE_U32_CAT(a,b,c) flagne_u32_cat((a), (b), (c))

#else /* WITHCAT */

#define FLAGNE_U8_CAT(a,b,c) flagne_u8(& (a)->value, (b))
#define FLAGNE_U16_CAT(a,b,c) flagne_u16(& (a)->value, (b))
#define FLAGNE_U32_CAT(a,b,c) flagne_u32(& (a)->value, (b))

#endif /* WITHCAT */

/*
 параметры:
 tx - не-0: переключение аппаратуры в режим передачи
 Учитывается состояние tunemode - режим настройки передатчика, при этом параметр tx не-ноль.
 */
static void
updateboardZZZ(
	uint_fast8_t full, 
	uint_fast8_t mute,
	const char * file,
	int line
	)
{
	/* параметры, вычисляемые по updateboard(full=1) */
	static const FLASHMEM struct modetempl * pamodetempl;	/* Режим, используемый при передаче */

	/* Параметры, которые могут измениться при перестройке частоты и вызвать необходимость full=1 */
	static uint_fast8_t lo0side = LOCODE_INVALID;
	static uint_fast32_t lo0hint = UINT32_MAX;
	static uint_fast8_t lo1side [2] = { LOCODE_INVALID, LOCODE_INVALID };
	const uint_fast8_t lo2side = LO2_SIDE;
	static uint_fast8_t lo2hint [2];
	const uint_fast8_t lo3side = LO3_SIDE;
	static uint_fast8_t forcelsb [2];
#if WITHDCDCFREQCTL
	static uint_fast32_t bldividerout = UINT32_MAX;
#endif /* WITHDCDCFREQCTL */
#if CTLSTYLE_IGOR
	static uint_fast16_t bandf100khint = UINT16_MAX;
#else /* CTLSTYLE_IGOR */
	static uint_fast8_t bandfhint = UINT8_MAX;
	static uint_fast8_t bandf2hint = UINT8_MAX;
	static uint_fast8_t bandf3hint = UINT8_MAX;	// управление через разъем ACC
#endif /* CTLSTYLE_IGOR */
#if WITHANTSELECT2
	static uint_fast8_t ant2hint = UINT8_MAX;
#endif /* WITHANTSELECT2 */
	uint_fast8_t full2 = full; 

	uint_fast8_t pathi;
	ASSERT(gtx < 2);
	const uint_fast8_t pathn = gtx ? 1 : NTRX;
	/* +++ проверка необходимости полной перенастройки из-за сменившихся условий выбора частот. */
	for (pathi = 0; pathi < pathn; ++ pathi)
	{
		const uint_fast8_t bi = getbankindex_pathi(pathi);
		const int_fast32_t freq = gfreqs [bi];
		/* "Эти параметры для расчёта частот выясняем здесь, так как они (могут) зависить от частоты */
		full2 |= flagne_u8(& lo1side [pathi], getsidelo1(freq));	// LOCODE_UPPER, LOCODE_LOWER or LOCODE_TARGETED
		full2 |= flagne_u8(& lo2hint [pathi], gethintlo2(freq));
		full2 |= flagne_u8(& forcelsb [pathi], getforcelsb(freq));
#if WITHDCDCFREQCTL
		full2 |= flagne_u32(& bldividerout, hardware_dcdc_calcdivider(freq));	// управление частотой dc-dc преобразователя
#endif /* WITHDCDCFREQCTL */
	}
	// параметры, не имеющие специфики для разных приемников
	{
		const uint_fast8_t bi = getbankindex_pathi(0); //getbankindex_tx(gtx);
		const int_fast32_t freq = gfreqs [bi];
	#if CTLSTYLE_IGOR
		full2 |= flagne_u16(& bandf100khint, freq / 100000uL);
	#else /* CTLSTYLE_IGOR */
		full2 |= flagne_u8(& bandfhint, bandf_calc(nyquistadj(freq)));
		full2 |= flagne_u8(& bandf2hint, bandf2_calc(nyquistadj2(freq)));
		full2 |= flagne_u8(& bandf3hint, bandf3_calc(nyquistadj3(freq)));
	#endif /* CTLSTYLE_IGOR */
		full2 |= flagne_u8(& lo0side, getsidelo0(freq));	// LOCODE_UPPER, LOCODE_LOWER or LOCODE_TARGETED
		full2 |= flagne_u32(& lo0hint, gethintlo0(freq, lo0side));
	#if WITHANTSELECT2
		full2 |= flagne_u8(& ant2hint, getdefantenna(freq));
	#endif /* WITHANTSELECT2 */
	}
	/* --- проверка необходимости полной перенастройки из-за сменившихся условий выбора частот. */

//	if (full2 != 0 && full == 0)
//	{
//		PRINTF("full2 added from %s/%d\n", file, line);
//	}
//	if (full != 0)
//	{
//		PRINTF("full passed from %s/%d\n", file, line);
//	}
#if WITHCAT
	if (aistate != 0)
	{
		if (full)
		{
			cat_answer_request(CAT_IF_INDEX);
			cat_answer_request(CAT_MD_INDEX);
			cat_answer_request(CAT_FA_INDEX);	// добавлено для обновления индикатора частоты в ACRP-590 при переходе по диапазонам клавишами на устройстве. И помогло при нажатиях на цифры дисплея.
			cat_answer_request(gtx ? CAT_TX_INDEX : CAT_RX_INDEX);	// ignore main/sub rx selection (0 - main. 1 - sub);
#if WITHIF4DSP
			cat_answer_request(CAT_NR_INDEX);
#endif /* WITHIF4DSP */
		}
		else
		{
			cat_answer_request(CAT_FA_INDEX);
		}
	}
#endif /* WITHCAT */
	
	if (full)
	{
		const uint_fast8_t bi = getbankindex_tx(gtx);	/* vfo bank index */
		/* преустановка всех параметров приемника или передатчика под новый режим */
		setgsubmode(getsubmode(bi));		/* установка gsubmode, gagc, gfi и так далее */
	}

	if (full2)
	{
		/* Полная перенастройка. Изменился режим (или одно из значений hint). */
		if (gtx == 0)
		{
			board_set_tx(0);	/* перед всеми перенастройками выключаем передатчик */
			board_update();		/* вывести забуферированные изменения в регистры */
			// todo: убрать после решения проблем с переходом между приёмом и передачей в FPGA
		}

		const uint_fast8_t rxbi = getbankindex_tx(0);
		const uint_fast8_t txbi = getbankindex_tx(1);
		const uint_fast8_t txsubmode = getsubmode(txbi);		/* код режима, который будет при передаче */
		const uint_fast8_t txmode = submodes [txsubmode].mode;

		for (pathi = 0; pathi < pathn; ++ pathi)
		{
			const uint_fast8_t asubmode = getasubmode(pathi);	// SUBMODE_CWZ/SUBMODE_CWZSMART for tune
			pamodetempl = getmodetempl(asubmode);
			const uint_fast8_t amode = submodes [asubmode].mode;
			const uint_fast8_t alsbmode = getsubmodelsb(asubmode, forcelsb [pathi]);	// Принимаемая модуляция на нижней боковой
			//
			// lo2side - определяется конфигурацией
			// lo3side - определяется конфигурацией
			//const static uint_fast8_t lo2side = LO2_SIDE;
			//const static uint_fast8_t lo3side = LO3_SIDE;
			//const uint_fast8_t lo4side = getsidelo4();	// LOCODE_UPPER, LOCODE_LOWER or LOCODE_TARGETED
			//const uint_fast8_t lo5side = getsidelo5(amode, gtx);	// LOCODE_UPPER, LOCODE_LOWER or LOCODE_TARGETED
			//const uint_fast8_t lo6side = getsidelo6(amode, gtx);	// LOCODE_UPPER, LOCODE_LOWER or LOCODE_TARGETED
			//
			const uint_fast8_t sides [7] = { lo0side, lo1side [pathi], lo2side, lo3side, getsidelo4(), getsidelo5(amode, gtx), getsidelo6(amode, gtx) };
			uint_fast8_t mixXlsbs [sizeof sides / sizeof sides [0]];
			uint_fast8_t i;
			for (i = 0; i < sizeof sides / sizeof sides [0]; ++ i)
			{
				mixXlsbs [i] = getlsbloX(alsbmode, i, sides, sizeof sides / sizeof sides [0]);
			}
			// расчёт частот в тракте
			// 
		#if defined (IF3_MODEL) && (IF3_MODEL == IF3_TYPE_DCRX)
			enum { dc = 1 };	/* работа в режиме прямого преобразования со всегда нулевой частотой LO4 */
		#else
			const uint_fast8_t dc = dctxmodecw && gtx && pamodetempl->txcw;	// на передачу формируем первым гетеродином
		#endif

			const filter_t * workfilter;
			if (gtx != 0)
			{
				workfilter = gettxfilter(asubmode, getsuitabletx(amode, 0));	/* получаем по gfi/gfitx, хранится фильтр, включенный для данного режима */
			}
			else
			{
				workfilter = getrxfilter(asubmode, gfi);	/* получаем по gfi/gfitx, хранится фильтр, включенный для данного режима */
			}
			ASSERT(workfilter != NULL);
	#if WITHIF4DSP
			// Так же, здесь можно решать, какой фильтр ПЧ требуется для данного фильтра dsp.
			// В случае DUC/DDC, используется "заглушка" - IF3_TYPE_BYPASS.
			const uint_fast8_t bwseti = mdt [amode].bwsetis [gtx];
			const uint_fast8_t wide = bwseti_getwide(bwseti);
	#else /* WITHIF4DSP */
			const uint_fast8_t wide = workfilter->widefilter;
	#endif /* WITHIF4DSP */
			//
#if WITHDUMBIF
			{
				const int_fast32_t freqif1 = WITHDUMBIF;
				synth_if1 [pathi] = UPPERTOSIGN(mixXlsbs [1], freqif1);	// Запоминается для последующего преобразования рабочей частоты к частоте гетеродина.
			}
#else /* WITHDUMBIF */
			const int_fast16_t pbt = getpbt(workfilter, amode, gtx);
			const int_fast16_t ifshift = getifshift(workfilter, amode, gtx);	/* положительные значения - повышение тембра (фильтр сдвигается "выше"). */

			const int_fast32_t freqif6 = getif6(amode, gtx, wide); // Positive number: ssb:0, cw=700, drm=12k
			const int_fast32_t freqlo6 = UPPERTOSIGN(! mixXlsbs [6], getlo6(amode, gtx, wide, ifshift));
			const int_fast32_t freqif5 = freqlo6 + UPPERTOSIGN(mixXlsbs [6], freqif6); 	// в режиме CW должно быть 0 для DDC/DUC
			const int_fast32_t freqlo5 = UPPERTOSIGN(mixXlsbs [5], getlo5(amode, gtx));	// 0 or DSP IF freq (12 kHz)
			const int_fast32_t freqif4 = freqlo5 + UPPERTOSIGN(mixXlsbs [5], freqif5);
			const int_fast32_t freqlo4ref = getlo4ref(workfilter, amode, mixXlsbs [4], gtx);

			// Возможно две стратегии переноса частоты - с согласованием частот среза
			// и с согласованием центральных частот полос пропускания.
			// Вариант с согласованием центральных частот - это случай включённого узкого фильтра ПЧ и телеграфного режима.
			// Или, случай WITHIF4DSP.
	#if WITHFIXEDBFO || WITHDUALFLTR
			// Выравнивание IF с привязкой к скату фильтра основной селекции
			// расчёт использует freqif4, freqlo5 == 0
			const int_fast32_t freqlo4 = freqlo4ref - freqlo5;	// если гетеродин один
			const int_fast32_t freqif3 = dc ? freqif4 : (freqlo4 + UPPERTOSIGN(mixXlsbs [4], freqif4));
	#elif WITHDUALBFO
			// Выравнивание IF с привязкой к скату фильтра основной селекции
			// расчёт использует freqif4, freqlo5 == 0
			const int_fast32_t freqlo4 = freqlo4ref - freqlo5;	// гетеродинов два - для разных скатов фильтра
			const int_fast32_t freqif3 = dc ? freqif4 : (freqlo4 + UPPERTOSIGN(mixXlsbs [4], freqif4));
	#elif WITHIF4DSP
			// Выравнивание IF с привязкой к центру фильтра основной селекции
			// расчёт использует freqif4, freqlo5
			const int_fast32_t freqlo4 = freqlo4ref - freqlo5;	// Надо согласовать центр фильтра с частотой переноса на baseband
			const int_fast32_t freqif3 = dc ? freqif4 : (freqlo4 + UPPERTOSIGN(mixXlsbs [4], freqif4));
	#elif 0
			// todo: не работает сохранение картины приёма при переключении SSB->CW, зато работает IF SHIFT
			// Выравнивание IF с привязкой к скату фильтра основной селекции
			// расчёт использует freqif4, freqif5 = - ifshift
			const int_fast32_t freqlo4 = dc ? 0 : (freqlo4ref - UPPERTOSIGN(mixXlsbs [4], freqif4));	// used for programming BFO
			const int_fast32_t freqif3 = dc ? freqif4 : (freqlo4ref - UPPERTOSIGN(mixXlsbs [4], freqif5));

	#elif 0
			// todo: работает сохранение картины приёма при переключении SSB->CW, но IF SHIFT меняет частоту приёма
			// Выравнивание IF с привязкой к скату фильтра основной селекции
			// расчёт использует freqif4, freqif5 = - ifshift
			const int_fast32_t freqlo4 = dc ? 0 : (freqlo4ref - UPPERTOSIGN(mixXlsbs [4], freqif4));	// used for programming BFO
			const int_fast32_t freqif3 = dc ? freqif4 : (freqlo4ref + UPPERTOSIGN(mixXlsbs [4], freqif4) - UPPERTOSIGN(mixXlsbs [4], freqif5));
	#else
			// Убран возврат ifshif в getlo6
			const int_fast32_t freqif3 = dc ? freqif4 : (freqlo4ref - UPPERTOSIGN(mixXlsbs [4], ifshift));
			const int_fast32_t freqlo4 = dc ? 0 : (freqif3 - UPPERTOSIGN(mixXlsbs [4], freqif4));	// used for programming BFO
	#endif

			// Взаимным смещением LO1 и LO2 производится "подрезание" полосы пропускания - PBTs
			const int_fast32_t freqlo3 = getlo3(amode, mixXlsbs [3], gtx) + UPPERTOSIGN16(mixXlsbs [3], pbt);	/* частота для гетеродина, осуществляющего passband tuning = PBT */
			const int_fast32_t freqif2 = freqlo3 + UPPERTOSIGN(mixXlsbs [3], freqif3);
			const int_fast32_t freqlo2 = getlo2(workfilter, amode, mixXlsbs [2], gtx, lo2hint [pathi]);
			const int_fast32_t freqif1 = freqlo2 + UPPERTOSIGN(mixXlsbs [2], freqif2);
			const int_fast32_t freqlo0 = getlo0(lo0hint);
			synth_if1 [pathi] = UPPERTOSIGN(mixXlsbs [1], freqif1) - UPPERTOSIGN(mixXlsbs [0], freqlo0);	// Запоминается для последующего преобразования рабочей частоты к частоте гетеродина.
#endif /* WITHDUMBIF */

	#if 0 && WITHDEBUG
			{
				const uint_fast8_t bi = getbankindex_pathi(pathi);
				const int_fast32_t freq = gfreqs [bi];
				PRINTF(submodes [asubmode].qlabel);
				PRINTF(PSTR(" pathi=%d"), pathi);
				PRINTF(PSTR(" f="));	printfreq(freq);
				PRINTF(PSTR(" lo0="));	printfreq(freqlo0);
				PRINTF(PSTR(" lo1="));	printfreq(synth_freq2lo1(freq, pathi));
				PRINTF(PSTR(" pbt="));	printfreq(pbt);
				PRINTF(PSTR(" ifshift="));	printfreq(ifshift);
				PRINTF(PSTR(" bw="));	PRINTF(workfilter->labelf3);
				PRINTF(PSTR(" dbw="));	PRINTF(hamradio_get_rxbw_value_P());
				PRINTF(PSTR("\n"));
				PRINTF(
					PSTR("mixXlsbs[0]=%d, [1]=%d, [2]=%d, [3]=%d, [4]=%d, [5]=%d, [6]=%d dc=%d tx=%d\n"),
						mixXlsbs [0], mixXlsbs [1], mixXlsbs [2], mixXlsbs [3], mixXlsbs [4], mixXlsbs [5], mixXlsbs [6], dc, gtx
					);
				PRINTF(PSTR(" [lo1="));	printfreq(synth_freq2lo1(freq, pathi));
				PRINTF(PSTR("]if1="));	printfreq(freqif1);
				PRINTF(PSTR(" [lo2="));	printfreq(freqlo2);
				PRINTF(PSTR("]if2="));	printfreq(freqif2);
				PRINTF(PSTR(" [lo3="));	printfreq(freqlo3);
				PRINTF(PSTR("]if3="));	printfreq(freqif3);

				//PRINTF(PSTR("\n"));

				PRINTF(PSTR(" [lo4=%d*"), getlo4enable(amode, gtx));	printfreq(freqlo4);
				PRINTF(PSTR("]if4="));	printfreq(freqif4);
				PRINTF(PSTR(" ["));	printfreq(freqlo5);
				PRINTF(PSTR("]if5="));	printfreq(freqif5);
				PRINTF(PSTR(" ["));	printfreq(freqlo6);
				PRINTF(PSTR("]if6="));	printfreq(freqif6);

				PRINTF(PSTR("\n"));
			}

	#endif /* WITHDEBUG */
		
			board_set_trxpath(pathi);	/* Тракт, к которому относятся все последующие вызовы. При перередаяе используется индекс 0 */
			if (gtx == 0)
			{
			#if WITHNOTCHONOFF
				board_set_notch(gnotch && notchmodes [gnotchtype].code != BOARD_NOTCH_OFF);
				board_set_notchnarrow(gnotch && notchmodes [gnotchtype].code != BOARD_NOTCH_OFF && pamodetempl->nar);
			#elif WITHNOTCHFREQ
				board_set_notch_mode(gnotch == 0 ? BOARD_NOTCH_OFF : notchmodes [gnotchtype].code);
				board_set_notch_width(gnotchwidth.value);
				board_set_notch_freq(gnotchfreq.value);	// TODO: при AUTONOTCH ставить INT16_MAX ?
			#endif /* WITHNOTCHFREQ */
			#if WITHIF4DSP
				const uint_fast8_t agcseti = pamodetempl->agcseti;
				board_set_agcrate(gagc [agcseti].rate);			/* на n децибел изменения входного сигнала 1 дБ выходного. UINT8_MAX - "плоская" АРУ */
				board_set_agc_scale(gagc [agcseti].scale);		/* Для эксперементов по улучшению приема АМ */
				board_set_agc_t0(gagc [agcseti].t0);
				board_set_agc_t1(gagc [agcseti].t1);
				board_set_agc_t2(gagc [agcseti].release10);		// время разряда медленной цепи АРУ
				board_set_agc_t4(gagc [agcseti].t4);			// время разряда быстрой цепи АРУ
				board_set_agc_thung(gagc [agcseti].thung10);	// hold time (hung time) in 0.1 sec
				board_set_squelch(pamodetempl->dspmode [gtx] == DSPCTL_MODE_RX_NFM ? ulmax(gsquelch.value, gsquelchNFM) : gsquelch.value);
				board_set_gainnfmrx(ggainnfmrx10 * 10);	/* дополнительное усиление по НЧ в режиме приёма NFM 100..1000% */
			#endif /* WITHIF4DSP */
			} /* tx == 0 */

		#if WITHIF4DSP
			#if WITHTX && WITHSUBTONES
				board_set_subtonelevel(gctssenable && gtx && getmodetempl(txsubmode)->subtone ? gsubtonelevel : 0);	/* Уровень сигнала CTCSS в процентах - 0%..100% */
			#endif /* WITHTX && WITHSUBTONES */
			board_set_aflowcutrx(bwseti_getlow(bwseti));	/* Нижняя частота среза фильтра НЧ по приему */
			board_set_afhighcutrx(bwseti_gethigh(bwseti));	/* Верхняя частота среза фильтра НЧ по приему */
			board_set_afresponcerx(bwseti_getafresponce(bwseti));	/* изменение тембра звука в приемнике - на Samplerate/2 АЧХ становится на столько децибел  */

			board_set_lo6(freqlo6);	/* иначе, в случае WITHIF4DSP - управление знаком частоты */
			board_set_fullbw6(getif6bw(amode, gtx, wide));	/* Установка частоты среза фильтров ПЧ в алгоритме Уивера - параметр полная полоса пропускания */
			//board_set_fltsofter(gtx ? WITHFILTSOFTMIN : bwseti_getfltsofter(bwseti));	/* Код управления сглаживанием скатов фильтра основной селекции на приёме */
			board_set_dspmode(pamodetempl->dspmode [gtx]);
			#if WITHDSPEXTDDC	/* "Воронёнок" с DSP и FPGA */
				board_set_dactest(gdactest);		/* вместо выхода интерполятора к ЦАП передатчика подключается выход NCO */
				board_set_dacstraight(gdacstraight);	/* Требуется формирование кода для ЦАП в режиме беззнакового кода */
				board_set_tx_inh_enable(gtxinhenable);				/* разрешение реакции на вход tx_inh */
				board_set_tx_bpsk_enable(pamodetempl->dspmode [gtx] == DSPCTL_MODE_TX_BPSK);	/* разрешение прямого формирования модуляции в FPGA */				/* разрешение прямого формирования модуляции в FPGA  */
				board_set_mode_wfm(pamodetempl->dspmode [gtx] == DSPCTL_MODE_RX_WFM);	/* разрешение прямого формирования модуляции в FPGA */				/* разрешение прямого формирования модуляции в FPGA  */
			#endif /* WITHDSPEXTDDC */
		#else /* WITHIF4DSP */
			board_set_if4lsb(mixXlsbs [4]);	/* для прямого преобразования - управление детектором - или выбор фильтра LSB для конфигураций с фиксированным 3-м гетеродином */
			//board_set_detector((mute && ! gtx) ? BOARD_DETECTOR_MUTE : pamodetempl->detector [gtx]);		// режим берётся из gsubmode
			board_set_detector(sleepflag ? BOARD_DETECTOR_MUTE : pamodetempl->detector [gtx]);
		#endif /* WITHIF4DSP */

		board_set_filter(workfilter->code [gtx]);	/* В случае WITHDUALFLTR формирование управляющего слова в зависимости от mixXlsbs [4] происходит при выдаче управляющих кодов в аппаратуру. */
		board_set_nfm(amode == MODE_NFM);

		#if WITHTX
			/* переносить эти параметры под условие перенастройки в режиме приёма не стал, так как меню может быть вызвано и при передаче */
			board_set_txcw(pamodetempl->txcw);	// при передаче будет режим без SSB модулятора
			board_set_vox(gvoxenable && getmodetempl(txsubmode)->vox);	// включение внешних схем VOX
			#if WITHSUBTONES
				// Установка параметров  Continuous Tone-Coded Squelch System or CTCSS
				board_subtone_setfreq(gsubtones [gsubtonei]);	// частота subtone (до десятых долей герца).
				board_subtone_enable_user(gctssenable && gtx && getmodetempl(txsubmode)->subtone);
			#endif /* WITHSUBTONES */
			#if WITHVOX
				vox_enable(gvoxenable && getmodetempl(txsubmode)->vox, voxdelay);		/* разрешение голосового управления переходом на передачу */
				vox_set_levels(gvoxlevel, gavoxlevel);		/* установка параметров vox */
			#endif /* WITHVOX */
			board_set_mikemute(gmuteall || getactualtune() || getmodetempl(txsubmode)->mute);	/* отключить микрофонный усилитель */
			seq_set_txgate_P(pamodetempl->txgfva, pamodetempl->sdtnva);		/* как должен переключаться тракт на передачу */
			board_set_opowerlevel(getactualpower());	/* WITHPOWERTRIMMIN..WITHPOWERTRIMMAX */

		#if WITHPABIASTRIM
			board_set_pabias(gpabias);	/* регулировка тока покоя оконечного каскада передатчика */
		#endif /* WITHPABIASTRIM */
			// установка параметров Speech processor
			//board_speech_set_mode(speechmode);
			//board_speech_set_width(speechwidth);
			seq_set_cw_enable(getmodetempl(txsubmode)->wbkin);	/* разрешение передачи CW */
		#endif /* WITHTX */


		#if CTLREGMODE_RA4YBO

			prog_dac1_a_value(gtxpower [amode]);		// power level
			prog_dac1_b_value(gtx ? gtxcompr [amode] : 0x00);		// compression level

		#elif KEYBSTYLE_RA4YBO_AM0

			board_set_user1(guser1);
			board_set_user2(guser2);
			board_set_user3(guser3);

		#elif CTLREGMODE_RA4YBO_V1

			prog_dac1_b_value(255 - gtxpower [amode]);		// power level
			prog_dac1_a_value(gtx ? gtxcompr [amode] : 0x00);		// compression level
			board_set_user1(guser1);
			board_set_user2(guser2);
			board_set_user3(guser3);
			board_set_user4(guser4);

		#elif CTLREGMODE_RA4YBO_V2

			prog_dac1_b_value(255 - gtxpower [amode]);		// power level
			prog_dac1_a_value(gtx ? gtxcompr [amode] : 0x00);		// compression level
			board_set_user1(guser1);
			board_set_user2(guser2);
			board_set_user3(guser3);
			board_set_user4(guser4);


		#elif CTLREGMODE_RA4YBO_V3

			prog_dac1_b_value(255 - gtxpower [amode]);		// power level
			prog_dac1_a_value(gtx ? gtxcompr [amode] : 0x00);		// compression level
			board_set_user1(guser1);
			board_set_user4(guser4);
			board_set_user5(guser5);

		#elif CTLREGMODE_RA4YBO_V3A

			prog_dac1_b_value(255 - gtxpower [amode]);		// power level
			prog_dac1_a_value(gtx ? gtxcompr [amode] : 0x00);		// compression level
			board_set_user1(guser1);
			board_set_user2(guser2);
			board_set_user3(guser3);
			board_set_user4(guser4);
			board_set_user5(guser5);

		#endif /* CTLREGMODE_RA4YBO_V1 || CTLSTYLE_RA4YBO_V2 */

		/* Этот блок (установка опорной частоты DDS) вызывать до настроек частот */
		#if defined(PLL1_TYPE) && (PLL1_TYPE == PLL_TYPE_SI570)
			synth_lo1_setreference(si570_get_xtall_base() + si570_xtall_offset);
		#else
			synth_lo1_setreference(getsynthref(amode));	// расчет коэфф. для работы синтезаторв
		#endif
			synth_setreference(getsynthref(amode));	// расчет коэфф. для работы синтезаторв

		/* А теперь настраиваем частоты. */
	#if ! WITHDUMBIF
			update_lo2(pathi, workfilter, amode, mixXlsbs [2], freqlo2, getlo2div(gtx), gtx, lo2hint [pathi]);
			update_lo3(pathi, amode, mixXlsbs [3], freqlo3, getlo3div(gtx), gtx);

			synth_lo4_setfreq(pathi, freqlo4, getlo4div(gtx), getlo4enable(amode, gtx));	/* утстановка третьего гетеродина */
	#endif /* ! WITHDUMBIF */

		} // pathi

		// параметры, не имеющие специфики для разных приемников
		update_lo0(lo0hint, lo0side);
		board_set_sleep(sleepflag);

		if (gtx == 0)
		{
			/* при переходе в режим передачи эти параметры не трогаем -
			- возможно, передача будет идти cross-band, но нам не нужно,
			чтобы хлопали реле аттенюатора.
			*/
			/* при переходе в режим передачи эти параметры не трогаем -
			- возможно, передача будет идти cross-band, но нам не нужно,
			чтобы хлопали реле аттенюатора.
			*/
		#if WITHNOATTNOPREAMP
			// без управления аттенюатором и УВЧ
		#elif WITHONEATTONEAMP
			board_set_att(attmodes [gatts [rxbi]].codeatt);
			board_set_preamp(attmodes [gatts [rxbi]].codepre);
		#else /* WITHONEATTONEAMP */
			board_set_att(attmodes [gatts [rxbi]].code);
			board_set_preamp(pampmodes [gpamps [rxbi]].code);
		#endif /* WITHONEATTONEAMP */
		#if ! WITHAGCMODENONE
			board_set_boardagc(gagcoff ? BOARD_AGCCODE_OFF : agcmodes [gagcmode].code);
			board_set_dspagc(gagcoff ? BOARD_AGCCODE_OFF : agcmodes [gagcmode].code);
		#endif /* ! WITHAGCMODENONE */
		#if CTLSTYLE_RA4YBO || CTLSTYLE_RA4YBO_V3
			board_set_affilter(gaffilter);
		#endif /* CTLSTYLE_RA4YBO || CTLSTYLE_RA4YBO_V3 */
		#if WITHDSPEXTDDC	/* "Воронёнок" с DSP и FPGA */
			board_set_dither(gdither);	/* управление зашумлением в LTC2208 */
			board_set_adcrand(gadcrand);	/* управление интерфейсом в LTC2208 */
			board_set_adcfifo(gadcfifo);
			board_set_adcoffset(gadcoffset + getadcoffsbase()); /* смещение для выходного сигнала с АЦП */
		#endif /* WITHDSPEXTDDC */
		} /* (gtx == 0) */
	#if WITHIF4DSP
		speex_update_rx();
	#endif /* WITHIF4DSP */

	#if defined (RTC1_TYPE)
		board_setrtcstrobe(grtcstrobe);
	#endif /* defined (RTC1_TYPE) */
	#if WITHANTSELECTRX
		board_set_rxantenna(rxantmodes [grxantennas [rxbi]].code);
		board_set_antenna(antmodes [gantennas [rxbi]].code);
	#elif WITHANTSELECT2
		board_set_antenna(gantmanual ? antmodes [gantennas [rxbi]].code : ant2hint);
	#elif WITHANTSELECT
		board_set_antenna(antmodes [gantennas [rxbi]].code [gtx]);
	#endif /* WITHANTSELECT || WITHANTSELECTRX */
	#if WITHELKEY
		board_set_wpm(elkeywpm.value);	/* скорость электронного ключа */
		#if WITHVIBROPLEX
			elkey_set_slope(elkeyslopeenable ? elkeyslope : 0);	/* скорость уменьшения длительности точки и паузы - имитация виброплекса */
		#endif /* WITHVIBROPLEX */
		#if ELKEY328
			elkey_set_format(elkeyslopeenable ? 27 : dashratio, spaceratio);	/* соотношение тире к точке (в десятках процентов) */
		#else
			elkey_set_format(dashratio, spaceratio);	/* соотношение тире к точке (в десятках процентов) */
		#endif
			elkey_set_mode(elkeymode, elkeyreverse);	/* режим электронного ключа - 0 - ACS, 1 - electronic key, 2 - straight key, 3 - BUG key */
		#if WITHTX && WITHELKEY
			seq_set_bkin_enable(bkinenable, bkindelay);			/* параметры BREAK-IN */
			/*seq_rgbeep(0); */								/* формирование roger beep */
		#endif /* WITHTX && WITHELKEY */
	#endif /* WITHELKEY */

	#if WITHIF4DSP
		board_set_afgain(sleepflag == 0 ? afgain1.value : BOARD_AFGAIN_MIN);	// Параметр для регулировки уровня на выходе аудио-ЦАП
		board_set_ifgain(sleepflag == 0  ? rfgain1.value : BOARD_IFGAIN_MIN);	// Параметр для регулировки усиления ПЧ

		const uint_fast8_t txaprofile = gtxaprofiles [getmodetempl(txsubmode)->txaprofgp];	// значения 0..NMICPROFILES-1

		#if ! defined (CODEC1_TYPE) && WITHUSBHW && WITHUSBUAC
			/* если конфигурация без автнонмного аудиокодека - все входы модулятора получают звук с USB AUDIO */
			const uint_fast8_t txaudio = BOARD_TXAUDIO_USB;
		#elif WITHBBOX && defined (WITHBBOXMIKESRC)
			const uint_fast8_t txaudio = WITHBBOXMIKESRC;
		#else /* defined (WITHBBOXMIKESRC) */
			const uint_fast8_t txaudio = gtxaudio [txmode];
		#endif /* defined (WITHBBOXMIKESRC) */
#if WITHAFCODEC1HAVELINEINLEVEL
		board_set_lineinput(txaudio == BOARD_TXAUDIO_LINE);
#else /* WITHAFCODEC1HAVELINEINLEVEL */
		board_set_lineinput(0);
#endif /* WITHAFCODEC1HAVELINEINLEVEL */
		board_set_detector(BOARD_DETECTOR_SSB);		/* Всегда смесительный детектор */
		board_set_digigainmax(gdigigainmax);
		board_set_gvad605(gvad605);			/* напряжение на AD605 (управление усилением тракта ПЧ */
		board_set_fsadcpower10((int_fast16_t) gfsadcpower10 [lo0side != LOCODE_INVALID] - (int_fast16_t) FSADCPOWEROFFSET10 + gerflossdb10(lo0side != LOCODE_INVALID, gatts [rxbi], gpamps [rxbi]));	/*	Мощность, соответствующая full scale от IF ADC */
		#if WITHUSEDUALWATCH
			board_set_mainsubrxmode(getactualmainsubrx());		// Левый/правый, A - main RX, B - sub RX
		#endif /* WITHUSEDUALWATCH */
		#if WITHUSBUAC
			board_set_uacplayer((gtx && gdatamode) || guacplayer);	/* режим прослушивания выхода компьютера в наушниках трансивера - отладочный режим */
			#if WITHRTS96 || WITHRTS192 || WITHTRANSPARENTIQ
				board_set_swapiq(gswapiq);	/* Поменять местами I и Q сэмплы в потоке RTS96 */
			#endif /* WITHRTS96 || WITHRTS192 || WITHTRANSPARENTIQ */
			#if WITHTX
				board_set_datavox(gdatavox);	/* автоматический переход на передачу при появлении звука со стороны компьютера */
			#endif /* WITHTX */
		#endif /* WITHUSBUAC */
		board_set_mikeboost20db(gmikeboost20db);	// Включение предусилителя за микрофоном
		board_set_lineamp(glineamp);	/* усиление с линейного входа */
#if WITHUSBUAC
		board_set_txaudio((gdatamode || getcattxdata()) ? BOARD_TXAUDIO_USB : txaudio);	// Альтернативные источники сигнала при передаче
#else /* WITHUSBUAC */
		board_set_txaudio(txaudio);	// Альтернативные источники сигнала при передаче
#endif /* WITHUSBUAC */
		board_set_mikeagc(gmikeagc);	/* Включение программной АРУ перед модулятором */
		board_set_mikeagcgain(gmikeagcgain);	/* Максимальное усидение АРУ микрофона */
		board_set_mikehclip(gmikehclip);	/* Ограничитель */
#if WITHCOMPRESSOR
		board_set_compressor(gcompressor_attack, gcompressor_release, gcompressor_hold, gcompressor_gain, gcompressor_threshold);
#endif /* WITHCOMPRESSOR */
#if WITHREVERB
		board_set_reverb(greverb, greverbdelay, greverbloss);	/* ревербератор */
#endif /* WITHREVERB */
		board_set_cwedgetime(gcwedgetime);	/* Время нарастания/спада огибающей телеграфа при передаче - в 1 мс */
		board_set_sidetonelevel(gsidetonelevel);	/* Уровень сигнала самоконтроля в процентах - 0%..100% */
		board_set_moniflag(gmoniflag);	/* glob_moniflag */
		#if WITHSPECTRUMWF
			const uint8_t bi_main = getbankindex_ab_fordisplay(0);	/* VFO A modifications */
			board_set_topdb(gtxloopback && gtx ? WITHTOPDBMIN : gtopdbspe [bi_main]);		/* верхний предел FFT */
			board_set_bottomdb(gtxloopback && gtx ? WITHBOTTOMDBTX : gbottomdbspe [bi_main]);		/* нижний предел FFT */
			board_set_topdbwf(gtxloopback && gtx ? WITHTOPDBMIN : gtopdbwfl [bi_main]);		/* верхний предел FFT для водопада */
			board_set_bottomdbwf(gtxloopback && gtx ? WITHBOTTOMDBTX : gbottomdbwfl [bi_main]);		/* нижний предел FFT для водопада */
			board_set_zoomxpow2(gzoomxpow2 [bi_main]);	/* уменьшение отображаемого участка спектра */
			board_set_wflevelsep(gwflevelsep);	/* чувствительность водопада регулируется отдельной парой параметров */
			board_set_lvlgridstep(glvlgridstep);	/* Шаг сетки уровней в децибелах */
			board_set_view_style(gviewstyle);			/* стиль отображения спектра и панорамы */
			board_set_view3dss_mark(gview3dss_mark);	/* Для VIEW_3DSS - индикация полосы пропускания на спектре */
			board_set_tx_loopback(gtxloopback && gtx);	/* включение спектроанализатора сигнала передачи */
			board_set_afspeclow(gafspeclow);	// нижняя частота отображения спектроанализатора
			board_set_afspechigh(gafspechigh);	// верхняя частота отображения спектроанализатора
			display2_set_filter_spe(gspecbeta100);	/* beta - парамеры видеофильтра спектра */
			display2_set_filter_wfl(gwflbeta100);	/* beta - парамеры видеофильтра водопада */
		#endif /* WITHSPECTRUMWF */
		board_set_showdbm(gshowdbm);		// Отображение уровня сигнала в dBm или S-memter (в зависимости от настроек)
	#endif /* WITHIF4DSP */
	#if WITHAFEQUALIZER
		board_set_equalizer_rx(geqrx);
		board_set_equalizer_tx(geqtx);
		board_set_equalizer_rx_gains(geqrxparams);
		board_set_equalizer_tx_gains(geqtxparams);
	#endif /* WITHAFEQUALIZER */

	#if WITHTX
		#if defined (CODEC1_TYPE) && WITHAFCODEC1HAVEPROC
			board_set_mikeequal(gmikeequalizer);	// включение обработки сигнала с микрофона (эффекты, эквалайзер, ...)
			board_set_mikeequalparams(gmikeequalizerparams);	// Эквалайзер 80Hz 230Hz 650Hz 	1.8kHz 5.3kHz
		#endif /* defined (CODEC1_TYPE) && WITHAFCODEC1HAVEPROC */
		#if WITHIF4DSP
		{
			const uint_fast8_t asubmode = getasubmode(0);	// SUBMODE_CWZ/SUBMODE_CWZSMART for tune
			const uint_fast8_t amode = submodes [asubmode].mode;
			const uint_fast8_t bwseti = mdt [amode].bwsetis [1];	// индекс банка полос пропускания для данного режима на передаче
			board_set_aflowcuttx(bwseti_getlow(bwseti));	/* Нижняя частота среза фильтра НЧ по передаче */
			board_set_afhighcuttx(bwseti_gethigh(bwseti));	/* Верхняя частота среза фильтра НЧ по передаче */
			board_set_afresponcetx(bwseti_getafresponce(bwseti));	/* коррекция АЧХ НЧ тракта передатчика */
			board_set_nfmdeviation100(gnfmdeviation);	/* Девиация при передаче в NFM - в сотнях герц */
		#if WITHNOTXDACCONTROL
			/* мощность регулируется умножнением выходных значений в потоке к FPGA / IF CODEC */
			// 0..10000
			board_set_dacscale(getbandf2adjust(bandf3hint) * (unsigned long) gdacscale * (unsigned long) (getactualpower() - WITHPOWERTRIMMIN) / (WITHPOWERTRIMMAX - WITHPOWERTRIMMIN));
		#else /* CPUDAC */
			/* мощность регулируется постоянны напряжением на ЦАП */
			// 0..10000
			board_set_dacscale(getbandf2adjust(bandf3hint) * (unsigned long) gdacscale);
		#endif /* CPUDAC */
			board_set_digiscale(ggaindigitx);	/* Увеличение усиления при передаче в цифровых режимах 100..300% */
			board_set_cwscale(ggaincwtx);	/* Увеличение усиления при передаче в CW режимах 50..100% */
			board_set_designscale(gdesignscale);	/* используется при калибровке параметров интерполятора */
			board_set_amdepth(gamdepth);	/* Глубина модуляции в АМ - 0..100% */
			board_rgrbeep_setfreq(1000);	/* roger beep - установка тона */
		}
		#endif /* WITHIF4DSP */
		seq_set_rgbeep(grgbeep);	/* разрешение (не-0) или запрещение (0) формирования roger beep */
		seq_set_rxtxdelay(rxtxdelay, txrxdelay, pretxdelay ? txrxdelay : 0);	/* установить задержку пре переходе на передачу и обратно. */
		board_sidetone_setfreq(gcwpitch10 * CWPITCHSCALE);	// Минимум - 400 герц (определено набором команд CAT Kenwood).
		board_set_classamode(gclassamode);	/* использование режима клвсс А при передаче */
		board_set_txgate(gtxgate);		/* разрешение драйвера и оконечного усилителя */
		#if WITHMIC1LEVEL
			board_set_mik1level(gmik1level);
		#endif /* WITHMIC1LEVEL */
		board_set_autotune(reqautotune);
	#endif /* WITHTX */

	#if CTLSTYLE_IGOR
		board_set_bcdfreq100k(bandf100khint);
	#else /* CTLSTYLE_IGOR */
		board_set_bandf(bandfhint);		/* включение нужного полосового фильтра - возможно переключение УВЧ */
		board_set_bandf2(bandf2hint);	/* включение нужного полосового фильтра (ФНЧ) передатчика */
		board_set_bandf3(bandf3hint);	/* управление через разъем ACC */
	#endif /* CTLSTYLE_IGOR */
		board_keybeep_setfreq(gkeybeep10 * 10);	// Частота озвучивания нажатий клавиш

	#if defined (DEFAULT_LCD_CONTRAST)
		display_set_contrast(gcontrast);
	#endif /* defined (DEFAULT_LCD_CONTRAST) */

	#if WITHFANTIMER
		board_setfanflag(! fanpaflag);	// fanpaflag - сигнал выключения вентилятора
		#if WITHFANPWM
				board_setfanpwm(! fanpaflag ? gfanpapwm : 0);
		#endif /* WITHFANPWM */
	#endif /* WITHFANTIMER */
	#if WITHDCDCFREQCTL
		board_set_bldivider(bldividerout);
	#endif /* WITHDCDCFREQCTL */
	#if WITHLCDBACKLIGHT
		board_set_bglight(dimmflag || sleepflag || dimmmode, gbglight);		/* подсветка дисплея  */
	#endif /* WITHLCDBACKLIGHT */
	#if WITHKBDBACKLIGHT
		board_set_kblight((dimmflag || sleepflag || dimmmode) ? 0 : gkblight);			/* подсвтка клавиатуры */
	#endif /* WITHKBDBACKLIGHT */
	#if WITHPWBUTTON
		board_set_poweron(gpoweronhold);
	#endif /* WITHPWBUTTON */
	#if WITHNBONOFF
		board_set_nfmnbon(lockmode);	/* Включние noise blanker на SW2014FM */
	#endif /* WITHNBONOFF */

	#if WITHSPKMUTE
		board_set_loudspeaker(gmutespkr); /*  выключение динамика */
		board_set_dsploudspeaker(gmutespkr); /*  выключение динамика (управление кодеком) */
	#endif /* WITHSPKMUTE */

	#if WITHAUTOTUNER
		board_set_tuner_group();
	#else /* WITHAUTOTUNER */
		board_set_tuner_bypass(1);
	#endif /* WITHAUTOTUNER */

		/* просто настройки тракта и не относящиеся к приёму-пеердаче. */
	#if WITHCAT
		processcat_enable(catenable);
		cat_set_speed(catbr2int [catbaudrate] * BRSCALE);
	#endif	/* WITHCAT */

	#if WITHMODEM
		board_set_modem_speed100(modembr2int100 [gmodemspeed]);	// скорость передачи (модуляция, не последовательный порт) с точностью 1/100 бод
		board_set_modem_mode(gmodemmode);	// применяемая модуляция
	#endif /* WITHMODEM */

	#if WITHLFM
		synth_lfm_setparams(lfmstart100k * 100000ul, lfmstop100k * 100000ul, lfmspeed1k * 1000ul, getlo1div(gtx));
	#endif /* WITHLFM */

	#if WITHLO1LEVELADJ
		prog_dds1_setlevel(lo1level);
	#endif /* WITHLO1LEVELADJ */
	#if defined (DAC1_TYPE)
		board_set_dac1(dac1level);	/* подстройка частоты опорного генератора */
	#endif /* defined (DAC1_TYPE) */

	#if (WITHSWRMTR || WITHSHOWSWRPWR)
		display2_set_smetertype(gsmetertype);
	#endif /* (WITHSWRMTR || WITHSHOWSWRPWR) */

#if defined(CODEC1_TYPE) && (CODEC1_TYPE == CODEC_TYPE_NAU8822L)
		{
			// nau8822 experements

			// ALC
//			unsigned ALCEN = 0;	// only left channel ALC enabled
//			unsigned ALCMXGAIN = 7;	// Set maximum gain limit for PGA volume setting changes under ALC control
//			unsigned ALCMNGAIN = 0;	// Set minimum gain value limit for PGA volume setting changes under ALC control
			unsigned alcctl1 =
					((ALCEN ? 0x02 : 0x00) << 7) |	// only left channel ALC enabled
					(ALCMXGAIN << 3) |
					(ALCMNGAIN << 0) |
					0;
			nau8822_setreg(NAU8822_ALC_CONTROL_1, alcctl1);

			// Noise gate
//			unsigned ALCNEN = 0;	// ALC noise gate function control bit
//			unsigned ALCNTH = 0;	// ALC noise gate threshold level
			unsigned ngctl1 =
					(1 << 4) |	// reserved
					(ALCNEN << 3) |
					(ALCNTH << 0) |
					0;
			nau8822_setreg(NAU8822_NOISE_GATE, ngctl1);

		}
#endif /* defined(CODEC1_TYPE) && (CODEC1_TYPE == CODEC_TYPE_NAU8822L) */
		board_update();		/* вывести забуферированные изменения в регистры */
	} // full2 != 0
	
#if CTLSTYLE_RA4YBO_AM0
	{
		const int pathi = 0;
		const uint_fast8_t bi = getbankindex_pathi(pathi);
		const int_fast32_t freq = gfreqs [bi];
		const uint_fast32_t lo1 = synth_freq2lo1(freq, pathi);	
		if (gtx)
		{
			synth_lo1_setfreq(pathi, 0, getlo1div(gtx)); /* установка частоты первого гетеродина */
			synth_lo4_setfreq(pathi, getlo0(lo0hint) - freq, getlo1div(gtx), 1);
		}
		else
		{
			synth_lo1_setfreq(pathi, lo1, getlo1div(gtx)); /* установка частоты первого гетеродина */
			synth_lo4_setfreq(pathi, 0, getlo1div(gtx), 0);
		}
	}
#else /* CTLSTYLE_RA4YBO_AM0 */

	if (userfsg)
	{
		const uint_fast8_t bi = getbankindex_tx(gtx);
		const int_fast32_t freq = gfreqs [bi];
		synth_lo1_setfrequ(0, freq, getlo1div(gtx));
		//synth_bfo_setfreq(0);
		//synth_lopbt_setfreq(0);
	}
	else
	{
		for (pathi = 0; pathi < pathn; ++ pathi)
		{
			const uint_fast8_t bi = getbankindex_pathi(pathi);
			const int_fast32_t freq = gfreqs [bi];

			/* частота первого гетеродина может оказхаться отрицательной */
			const uint_fast32_t lo1 = synth_freq2lo1(freq, pathi);	
			synth_lo1_setfreq(pathi, lo1, getlo1div(gtx)); /* установка частоты первого гетеродина */
			synth_rts1_setfreq(pathi, getlo0(lo0hint) - freq);	// Установка центральной частоты панорамного индикатора
		}
	}
#endif /* CTLSTYLE_RA4YBO_AM0 */
	if (full2 != 0 && (mute != 0 || gtx != 0))
	{
		for (pathi = 0; pathi < pathn; ++ pathi)
		{
			//const uint_fast8_t bi = getbankindex_pathi(pathi);
			//const int_fast32_t freq = gfreqs [bi];

		}
		//local_delay_ms(20.0);		/* подождать установки частоты */
	#if ! WITHIF4DSP
		//board_set_detector((mute && ! gtx) ? BOARD_DETECTOR_MUTE : pamodetempl->detector [gtx]);		// режим берётся из gsubmode
		board_set_detector(sleepflag ? BOARD_DETECTOR_MUTE : pamodetempl->detector [gtx]);	// включить звук, если приём
		board_update();		/* вывести забуферированные изменения в регистры */
	#endif /* WITHIF4DSP */
			// todo: убрать после решения проблем с переходом между приёмом и передачей в FPGA
	#if WITHTX
		//board_set_tx(gtx);		/* в конце выдаём сигнал разрешения передачи */
		//board_update();		/* вывести забуферированные изменения в регистры */
	#endif /* WITHTX */
	}

	/* после всех перенастроек включаем передатчик */
#if WITHTX
	board_set_tx(gtx);		/* в конце выдаём сигнал разрешения передачи */
	board_update();		/* вывести забуферированные изменения в регистры */
#endif /* WITHTX */
}

///////////////////////////
// обработчики кнопок клавиатуры

//////////////////////////
#if WITHELKEY && WITHTX

void uif_key_bkintoggle(void)
{
	bkinenable = calc_next(bkinenable, 0, 1);
	save_i8(offsetof(struct nvmap, bkinenable), bkinenable);
	updateboard(1, 0);
}

uint_fast8_t hamradio_get_bkin_value(void)
{
	return bkinenable;
}

#else

uint_fast8_t hamradio_get_bkin_value(void)
{
	return 0;
}

#endif /* WITHELKEY */

#if WITHVOX && WITHTX

static void 
uif_key_voxtoggle(void)
{
	gvoxenable = calc_next(gvoxenable, 0, 1);
	save_i8(offsetof(struct nvmap, gvoxenable), gvoxenable);
	updateboard(1, 0);
}

// текущее состояние VOX
uint_fast8_t hamradio_get_voxvalue(void)
{
	return gvoxenable;
}

#else /* WITHVOX && WITHTX */
// текущее состояние VOX
uint_fast8_t hamradio_get_voxvalue(void)
{
	return 0;
}

#endif /* WITHVOX && WITHTX */


#if WITHANTSELECTRX

// antenna
const FLASHMEM char * hamradio_get_ant5_value_P(void)
{
	static char b [6];
	local_snprintf_P(b, ARRAY_SIZE(b),
			PSTR("%s %s"),
			antmodes [gantennas [getbankindex_tx(gtx)]].label2,
			rxantmodes[grxantennas [getbankindex_tx(gtx)]].label2
	);
	return b;
}


#elif WITHANTSELECT

// antenna
const FLASHMEM char * hamradio_get_ant5_value_P(void)
{
	return antmodes [gantennas [getbankindex_tx(gtx)]].label5;
}

#elif WITHANTSELECT2
// antenna
const FLASHMEM char * hamradio_get_ant5_value_P(void)
{
	const uint_fast8_t bi = getbankindex_tx(gtx);	/* vfo bank index */
	static char b [6];
	local_snprintf_P(b, ARRAY_SIZE(b),
			PSTR("%s %s"),
			antmodes [gantmanual ? gantennas [bi] : getdefantenna(gfreqs [bi])].label2,
			gantmanual ? "MN" : "AU"
	);
	return b;
}
#endif /* WITHANTSELECT || WITHANTSELECTRX */

// включение режима split (возможо, с расстройко от текущей частоты)
static void 
uif_key_spliton(uint_fast8_t holded)
{
#if WITHSPLIT

	const uint_fast8_t srbi = getbankindex_raw(0);
	const uint_fast8_t tgbi = getbankindex_raw(1);
	const vindex_t tgvi = getvfoindex(tgbi);

	copybankstate(srbi, tgbi, holded == 0 ? 0 : getmodetempl(getsubmode(srbi))->autosplitK * 1000L);	/* копируем состояние текущего банка в противоположный */
	gsplitmode = VFOMODES_VFOSPLIT;

	savebandstate(tgvi, tgbi); // записать все параметры настройки (кроме частоты) в область данных VFO */
	savebandfreq(tgvi, tgbi);

	save_i8(RMT_SPLITMODE_BASE, gsplitmode);
	updateboard(1, 1);

#elif WITHSPLITEX

	if (holded != 0)
	{
		const uint_fast8_t srbi = getbankindex_raw(0);
		const uint_fast8_t tgbi = getbankindex_raw(1);
		const vindex_t tgvi = getvfoindex(tgbi);

		copybankstate(srbi, tgbi, getmodetempl(getsubmode(srbi))->autosplitK * 1000L);	/* копируем состояние текущего банка в противоположный */
	
		savebandstate(tgvi, tgbi); // записать все параметры настройки (кроме частоты) в область данных VFO */
		savebandfreq(tgvi, tgbi);
	}
	gsplitmode = VFOMODES_VFOSPLIT;

	save_i8(RMT_SPLITMODE_BASE, gsplitmode);
	updateboard(1, 1);

#else

#endif /* WITHSPLIT */
}

/* копирование в VFO B состояния VFO A */
// for WITHSPLITEX
static void 
uif_key_click_b_from_a(void)
{
#if (WITHSPLIT || WITHSPLITEX)

	if (gsplitmode == VFOMODES_VFOSPLIT)
	{
		const uint_fast8_t sbi = getbankindex_ab(0);	// bank index исходных данных
		const uint_fast8_t tbi = getbankindex_ab(1);	// bank index куда копируются данные
		const vindex_t tgvi = getvfoindex(tbi);		// vfo index куда копируются данные

		copybankstate(sbi, tbi, 0);
		savebandstate(tgvi, tbi); // записать все параметры настройки (кроме частоты) в область данных VFO */
		savebandfreq(tgvi, tbi); // записать частоту в область данных VFO */
		updateboard(1, 1);
	}

#endif /* (WITHSPLIT || WITHSPLITEX) */
}

// вылючение режима split
static void 
uif_key_splitoff(void)
{
#if (WITHSPLIT || WITHSPLITEX)

	gsplitmode = VFOMODES_VFOINIT;
	save_i8(RMT_SPLITMODE_BASE, gsplitmode);

	updateboard(1, 1);

#endif /* (WITHSPLIT || WITHSPLITEX) */
}

/* обмен частотой между VFO */
// for WITHSPLITEX
static void 
uif_key_click_a_ex_b(void)
{
#if (WITHSPLIT || WITHSPLITEX)

	gvfoab = ! gvfoab;	/* меняем текущий VFO на протвоположный */
	save_i8(RMT_VFOAB_BASE, gvfoab);
	updateboard(1, 1);

#endif /* (WITHSPLIT || WITHSPLITEX) */
}

///////////////////////////
// обработчики кнопок клавиатуры
//////////////////////////
/* Переход между режимами фиксированных частот
   или плавной настройки.
  */
//static void 
//uif_key_memmode(uint_fast8_t tx)
//{
//while (repeat --)
//	gsplitmode = calc_next(gsplitmode, 0, VFOMODES_COUNT - 1); /* (vfo/vfoa/vfob/mem) */
//save_i8(RMT_SPLITMODE_BASE, gsplitmode);
//updateboard(1, 0);
//}

///////////////////////////
// обработчики кнопок клавиатуры
//////////////////////////
/* переход по "строке" режимов - удержанное нажатие */
// step to next modecol
static void 
uif_key_hold_modecol(void)
{
	const uint_fast8_t bi = getbankindex_tx(gtx);	/* vfo bank index */
	const uint_fast8_t moderow = gmoderows [bi];	/* строка таблицы запомненных режимов */

	uint_fast8_t modecol = getmodecol(moderow, modes [moderow][0] - 1, 0, bi);	/* выборка из битовой маски. Возможно, значение modecolmap бует откорректировано.  */
	modecol = calc_next(modecol, 0, modes [moderow][0] - 1);
	putmodecol(moderow, modecol, bi);	/* внести новое значение в битовую маску */
	/* переустановка частот всех гетеродинов после смены режимов */
	/* gband должен быть уже известен */
	gsubmodechange(getsubmode(bi), bi); /* если надо - сохранение частоты в текущем VFO */
	updateboard(1, 1);
}


///////////////////////////
// обработчики кнопок клавиатуры
//////////////////////////
/* переход по "столбцу" режимов - быстрое нажатие */
/* switch to next moderow */
static void 
uif_key_click_moderow(void)
{
	const uint_fast8_t bi = getbankindex_tx(gtx);	/* vfo bank index */
	uint_fast8_t defrow = gmoderows [bi];		/* строка таблицы режимов, которую покидаем */
	uint_fast8_t defcol = getmodecol(defrow, modes [defrow][0] - 1, 0, bi);	/* выборка из битовой маски. Возможно, значение modecolmap бует откорректировано.  */
	const uint_fast8_t forcelsb = getforcelsb(gfreqs [bi]);

	gmoderows [bi] = calc_next(gmoderows [bi], 0, MODEROW_COUNT - 1);		/* идём на следующую строку таблицы запомненых режимов */

#if WITHMODESETSMART
	defcol = locatesubmode(SUBMODE_SSBSMART, & defrow);
#else /* WITHMODESETSMART */
	if (gsubmode == SUBMODE_USB)		// если текущий режим USB - ищемм CW
		defcol = locatesubmode(SUBMODE_CW, & defrow);
	else if (gsubmode == SUBMODE_LSB)	// если текущий режим LSB - ищемм CWR
		defcol = locatesubmode(SUBMODE_CWR, & defrow);
	else if (gsubmode == SUBMODE_DGU)	// если текущий режим LSB - ищемм CWR
		defcol = locatesubmode(SUBMODE_USB, & defrow);
	else if (gsubmode == SUBMODE_DGL)	// если текущий режим LSB - ищемм CWR
		defcol = locatesubmode(SUBMODE_LSB, & defrow);
	#if WITHMODESETFULLNFM
	else if (gsubmode == SUBMODE_AM)	// если текущий режим AM - ищемм FM
		defcol = locatesubmode(SUBMODE_NFM, & defrow);
	#endif
	else								// в остальных случаях ищем режим по умолчанию для данного диапазона частот
		defcol = locatesubmode(forcelsb ? SUBMODE_LSB : SUBMODE_USB, & defrow);
#endif /* WITHMODESETSMART */	
	/* если переходим не на строку с найденными режимаим */
	if (defrow != gmoderows [bi])
		defcol = 0;	/* default value (other cases, then switch from usb to cw, from lsb to cwr) */
	/* пытаемся обратиться за битами - они, взоможно, заменяться значением defcol */
	(void) getmodecol(gmoderows [bi], modes [gmoderows [bi]][0] - 1, defcol, bi); /* Возможно, значение modecolmap бует откорректировано. */

	/* переустановка частот всех гетеродинов после смены режимов */
	/* gband должен быть уже известен */
	gsubmodechange(getsubmode(bi), bi); /* если надо - сохранение частоты в текущем VFO */
	updateboard(1, 1);
}

///////////////////////////
// обработчики кнопок клавиатуры
//////////////////////////
/* переход по "столбцу" режимов - быстрое нажатие */
/* switch to next moderow */
static void 
uif_key_click_moderows(uint_fast8_t moderow)
{
	const uint_fast8_t bi = getbankindex_tx(gtx);	/* vfo bank index */
	const uint_fast8_t rowchanged = (gmoderows [bi] != moderow);
	uint_fast8_t defrow = gmoderows [bi] = moderow;		/* строка таблицы режимов, которую покидаем */
	uint_fast8_t defcol = getmodecol(defrow, modes [defrow][0] - 1, 0, bi);	/* выборка из битовой маски. Возможно, значение modecolmap бует откорректировано.  */
	const uint_fast8_t forcelsb = getforcelsb(gfreqs [bi]);

#if WITHMODESETSMART
	defcol = locatesubmode(SUBMODE_SSBSMART, & defrow);
#else /* WITHMODESETSMART */
	if (gsubmode == SUBMODE_USB)		// если текущий режим USB - ищемм CW
		defcol = locatesubmode(SUBMODE_CW, & defrow);
	else if (gsubmode == SUBMODE_LSB)	// если текущий режим LSB - ищемм CWR
		defcol = locatesubmode(SUBMODE_CWR, & defrow);
	else if (gsubmode == SUBMODE_DGU)	// если текущий режим LSB - ищемм CWR
		defcol = locatesubmode(SUBMODE_USB, & defrow);
	else if (gsubmode == SUBMODE_DGL)	// если текущий режим LSB - ищемм CWR
		defcol = locatesubmode(SUBMODE_LSB, & defrow);
	#if WITHMODESETFULLNFM
	else if (gsubmode == SUBMODE_AM)	// если текущий режим AM - ищемм FM
		defcol = locatesubmode(SUBMODE_NFM, & defrow);
	#endif
	else								// в остальных случаях ищем режим по умолчанию для данного диапазона частот
		defcol = locatesubmode(forcelsb ? SUBMODE_LSB : SUBMODE_USB, & defrow);
#endif /* WITHMODESETSMART */	
	/* если переходим не на строку с найденными режимаим */
	if (defrow != gmoderows [bi])
		defcol = 0;	/* default value (other cases, then switch from usb to cw, from lsb to cwr) */
	/* пытаемся обратиться за битами - они, взоможно, заменяться значением defcol */
	(void) getmodecol(gmoderows [bi], modes [gmoderows [bi]][0] - 1, defcol, bi); /* Возможно, значение modecolmap бует откорректировано. */

	/* переустановка частот всех гетеродинов после смены режимов */
	/* gband должен быть уже известен */
	gsubmodechange(getsubmode(bi), bi); /* если надо - сохранение частоты в текущем VFO */
	updateboard(1, 1);
}

///////////////////////////
// обработчики кнопок клавиатуры
//////////////////////////
/* переход по "строке" режимов - удержанное нажатие */
// step to next modecol
static void 
uif_key_hold_modecols(uint_fast8_t moderow)
{
	const uint_fast8_t bi = getbankindex_tx(gtx);	/* vfo bank index */
	if (gmoderows [bi] != moderow)	/* строка таблицы запомненных режимов */
	{
		uif_key_click_moderows(moderow);
		return;
	}

	uint_fast8_t modecol = getmodecol(moderow, modes [moderow][0] - 1, 0, bi);	/* выборка из битовой маски. Возможно, значение modecolmap бует откорректировано.  */
	modecol = calc_next(modecol, 0, modes [moderow][0] - 1);
	putmodecol(moderow, modecol, bi);	/* внести новое значение в битовую маску */
	/* переустановка частот всех гетеродинов после смены режимов */
	/* gband должен быть уже известен */
	gsubmodechange(getsubmode(bi), bi); /* если надо - сохранение частоты в текущем VFO */
	updateboard(1, 1);
}

///////////////////////////
// обработчики кнопок клавиатуры
//////////////////////////

// короткое нажатие кнопки BAND UP
/* переход на следующий (с большей частотой) диапазон */
static void 
uif_key_click_bandup(void)
{
	const uint_fast8_t bi = getbankindex_tx(gtx);	/* vfo bank index */
	const vindex_t vi = getvfoindex(bi);
	const vindex_t b = getfreqband(gfreqs [bi]);	/* определяем по частоте, в каком диапазоне находимся */
	verifyband(b);
	savebandstate(b, bi); // записать все параметры настройки (кроме частоты) в область данных диапазона */
	savebandfreq(b, bi);
	const vindex_t bn = getnext_ham_band(b, gfreqs [bi]);
	loadnewband(bn, bi);	/* загрузка всех параметров (и частоты) нового режима */
	savebandgroup(bn);
	savebandfreq(vi, bi);	/* сохранение частоты в текущем VFO */
	savebandstate(vi, bi); // записать все параметры настройки (кроме частоты)  в текущем VFO */
	updateboard(1, 1);
}
///////////////////////////
// обработчики кнопок клавиатуры
//////////////////////////
// короткое нажатие кнопки BAND DOWN
/* переход на предыдущий (с меньшей частотой) диапазон */
static void 
uif_key_click_banddown(void)
{
	const uint_fast8_t bi = getbankindex_tx(gtx);	/* vfo bank index */
	const vindex_t vi = getvfoindex(bi);
	const vindex_t b = getfreqband(gfreqs [bi]);	/* определяем по частоте, в каком диапазоне находимся */
	verifyband(b);
	savebandstate(b, bi); // записать все параметры настройки (кроме частоты) в область данных диапазона */
	savebandfreq(b, bi);
	const uint_fast8_t bn = getprev_ham_band(b, gfreqs [bi]);
	loadnewband(bn, bi);	/* загрузка всех параметров (и частоты) нового режима */
	savebandgroup(bn);
	savebandfreq(vi, bi);	/* сохранение частоты в текущем VFO */
	savebandstate(vi, bi); // записать все параметры настройки (кроме частоты)  в текущем VFO */
	updateboard(1, 1);
}


/* переход на диапазон, содержащий указанную частоту */
static void 
uif_key_click_banddjump(uint_fast32_t f)
{
#if	WITHDIRECTBANDS
	const uint_fast8_t bi = getbankindex_tx(gtx);	/* vfo bank index */
	const vindex_t vi = getvfoindex(bi);
	const vindex_t b = getfreqband(gfreqs [bi]);	/* определяем по частоте, в каком диапазоне находимся */
	vindex_t bn = getfreqband(f);

	const uint_fast8_t bandgroup = bandsmap [bn].bandgroup;
	verifyband(b);
	verifyband(bn);
	savebandstate(b, bi); // записать все параметры настройки (кроме частоты) в область данных диапазона */
	savebandfreq(b, bi);
	// 
	//
	if (bandgroup != BANDGROUP_COUNT)
	{
		// новый поддиапазон является частью группы
		bn = loadvfy8up(RMT_BANDGROUP(bandgroup), 0, HBANDS_COUNT - 1, bn);
		verifyband(bn);
		if (bandgroup == bandsmap [b].bandgroup)
		{
			// переключение в диапазон той же группы - переход в пределах группы
			bn = getnextbandingroup(bn, bandgroup);
			verifyband(bn);
			save_i8(RMT_BANDGROUP(bandgroup), bn);
		}
	}
	loadnewband(bn, bi);	/* загрузка всех параметров (и частоты) нового режима */
	savebandfreq(vi, bi);	/* сохранение частоты в текущем VFO */
	savebandstate(vi, bi); // записать все параметры настройки (кроме частоты)  в текущем VFO */
	updateboard(1, 1);
#endif /* WITHDIRECTBANDS */
}

/* AGC mode switch
	 - вызывает сохранение состояния режима */
static void 
uif_key_click_agcmode(void)
{
	gagcmode = calc_next(gagcmode, 0, AGCMODE_COUNT - 1);
	save_i8(RMT_AGC_BASE(submodes [gsubmode].mode), gagcmode);
	updateboard(1, 0);
}

#if WITHANTSELECTRX

/* Antenna switch
	  */
static void
uif_key_next_antenna(void)
{
	const uint_fast8_t bi = getbankindex_tx(gtx);	/* vfo bank index */
	const vindex_t vi = getvfoindex(bi);

	verifyband(vi);

	gantennas [bi] = calc_next(gantennas [bi], 0, ANTMODE_COUNT - 1);
	savebandstate(vi, bi);	// запись всех режимов в область памяти диапазона
	updateboard(1, 0);
}

/* Antenna switch
	  */
static void
uif_key_next_rxantenna(void)
{
	const uint_fast8_t bi = getbankindex_tx(gtx);	/* vfo bank index */
	const vindex_t vi = getvfoindex(bi);

	verifyband(vi);

	grxantennas [bi] = calc_next(grxantennas [bi], 0, RXANTMODE_COUNT - 1);
	savebandstate(vi, bi);	// запись всех режимов в область памяти диапазона
	updateboard(1, 0);
}

#elif WITHANTSELECT2

/* Antenna switch
	  */
static void
uif_key_next_antenna(void)
{
	const uint_fast8_t bi = getbankindex_tx(gtx);	/* vfo bank index */
	const vindex_t vi = getvfoindex(bi);

	verifyband(vi);

	gantennas [bi] = calc_next(gantennas [bi], 0, ANTMODE_COUNT - 1);
	savebandstate(vi, bi);	// запись всех режимов в область памяти диапазона
	updateboard(1, 0);
}

/* ручной/автоматический выбор антенны */
static void
uif_key_next_autoantmode(void)
{
	gantmanual = calc_next(gantmanual, 0, 1);
	save_i8(RMT_ANTMANUAL_BASE, gantmanual);
	updateboard(1, 0);
}

#elif WITHANTSELECT

/* Antenna switch
	  */
static void 
uif_key_next_antenna(void)
{
	const uint_fast8_t bi = getbankindex_tx(gtx);	/* vfo bank index */
	const vindex_t vi = getvfoindex(bi);

	verifyband(vi);

	gantennas [bi] = calc_next(gantennas [bi], 0, ANTMODE_COUNT - 1);
	savebandstate(vi, bi);	// запись всех режимов в область памяти диапазона
	updateboard(1, 0);
}

#endif /* WITHANTSELECT || WITHANTSELECTRX */

#if ! WITHONEATTONEAMP
/* переключение режима предусилителя  */
static void 
uif_key_click_pamp(void)
{
	const uint_fast8_t bi = getbankindex_tx(gtx);	/* vfo bank index */
	const vindex_t vi = getvfoindex(bi);

	verifyband(vi);

	gpamps [bi] = calc_next(gpamps [bi], 0, PAMPMODE_COUNT - 1);
	savebandstate(vi, bi);	// запись всех режимов в область памяти диапазона
	updateboard(1, 0);
}
#endif /* ! WITHONEATTONEAMP */


/* переключение режима аттенюатора  */
static void 
uif_key_click_attenuator(void)
{
	const uint_fast8_t bi = getbankindex_tx(gtx);	/* vfo bank index */
	const vindex_t vi = getvfoindex(bi);

	verifyband(vi);

	gatts [bi] = calc_next(gatts [bi], 0, ATTMODE_COUNT - 1);
	savebandstate(vi, bi);	// запись всех режимов в область памяти диапазона
	updateboard(1, 0);
}

#if WITHPOWERLPHP
/* переключение режима мощности  */
static void 
uif_key_click_pwr(void)
{
	gpwri = calc_next(gpwri, 0, PWRMODE_COUNT - 1);
	save_i8(RMT_PWR_BASE, gpwri);

	updateboard(1, 0);
}
#endif /* WITHPOWERLPHP */

#if WITHNOTCHONOFF || WITHNOTCHFREQ
/* включение/выключение NOTCH  */
static void 
uif_key_click_notch(void)
{
	gnotch = calc_next(gnotch, 0, 1);
	save_i8(RMT_NOTCH_BASE, gnotch);

	updateboard(1, 0);
}

#endif /* WITHNOTCHONOFF || WITHNOTCHFREQ */

#if ELKEY328

static void 
uif_key_click_vibroplex(void)
{
	elkeyslopeenable = calc_next(elkeyslopeenable, 0, 1);
	updateboard(1, 0);
}

#endif /* ELKEY328 */

/* блокировка енкодера */
static void 
uif_key_lockencoder(void)
{
	lockmode = calc_next(lockmode, 0, 1);
	save_i8(RMT_LOCKMODE_BASE, lockmode);
	updateboard(1, 0);
}

#if WITHBCBANDS
/* переход в режим переключения по вещательным диапазонам */
static void
uif_key_genham(void)
{
	gbandsetbcast = calc_next(gbandsetbcast, 0, 1);
	save_i8(offsetof(struct nvmap, gbandsetbcast), gbandsetbcast);
	updateboard(1, 0);
}

uint_fast8_t hamradio_get_genham_value(void)
{
	return gbandsetbcast;
}

#endif /* WITHBCBANDS */

#if WITHUSEFAST
/* переключение в режим крупного шага */
static void 
uif_key_usefast(void)
{
	gusefast = calc_next(gusefast, 0, 1);
	save_i8(RMT_USEFAST_BASE, gusefast);
	updateboard(1, 0);
}
#endif /* WITHUSEFAST */

#if WITHSPKMUTE

/* Включение-выключение динамика */
static void 
uif_key_loudsp(void)
{
	gmutespkr = calc_next(gmutespkr, 0, 1);
	save_i8(RMT_MUTELOUDSP_BASE, gmutespkr);
	updateboard(1, 0);
}

uint_fast8_t hamradio_get_spkon_value(void)
{
	return ! gmutespkr;
}

#endif /* WITHSPKMUTE */

#if CTLSTYLE_RA4YBO

/* включение ФНЧ на приёме в аппарате RA4YBO */
static void 
uif_key_affilter(void)
{
	gaffilter = calc_next(gaffilter, 0, 1);
	save_i8(RMT_AFFILTER_BASE, gaffilter);
	updateboard(1, 0);
}

///////////////////////////
// обработчики кнопок клавиатуры
//////////////////////////

#elif CTLSTYLE_RA4YBO_V1 || CTLSTYLE_RA4YBO_V2 || CTLSTYLE_RA4YBO_V3 || KEYBSTYLE_RA4YBO_AM0

static void 
uif_key_user1(void)
{
	guser1 = calc_next(guser1, 0, 1);
	save_i8(RMT_USER1_BASE, guser1);
	updateboard(1, 0);
}

static void 
uif_key_user2(void)
{
	guser2 = calc_next(guser2, 0, 1);
	save_i8(RMT_USER2_BASE, guser2);
	updateboard(1, 0);
}
static void 
uif_key_user3(void)
{
	guser3 = calc_next(guser3, 0, 1);
	save_i8(RMT_USER3_BASE, guser3);
	updateboard(1, 0);
}

static void 
uif_key_user4(void)
{
	guser4 = calc_next(guser4, 0, 1);
	save_i8(RMT_USER4_BASE, guser4);
	updateboard(1, 0);
}
static void 
uif_key_user5(void)
{
	guser5 = calc_next(guser5, 0, 1);
	save_i8(RMT_USER5_BASE, guser5);
	updateboard(1, 0);
}

/* включение ФНЧ на приёме в аппарате RA4YBO */
static void 
uif_key_affilter(void)
{
	gaffilter = calc_next(gaffilter, 0, 1);
	save_i8(RMT_AFFILTER_BASE, gaffilter);
	updateboard(1, 0);
}

#endif /* CTLSTYLE_RA4YBO_V1 || CTLSTYLE_RA4YBO_V2 */
///////////////////////////
// обработчики кнопок клавиатуры
//////////////////////////
#if WITHIF4DSP
/* Переключение полос пропускания в CW приёмника
	 - не вызывает сохранение состояния диапазона */

static void 
uif_key_changebw(void)
{
	const uint_fast8_t bwseti = mdt [gmode].bwsetis [0];	// индекс банка полос пропускания для данного режима на приеме

	bwsetpos [bwseti] = calc_next(bwsetpos [bwseti], 0, bwsetsc [bwseti].last);
	save_i8(RMT_BWSETPOS_BASE(bwseti), bwsetpos [bwseti]);	/* только здесь сохраняем новый фильтр для режима */
	updateboard(1, 1);
}

/* Переключение шумоподавления
	 - не вызывает сохранение состояния диапазона */

static void 
uif_key_changenr(void)
{
	gnoisereducts [gmode] = calc_next(gnoisereducts [gmode], 0, 1);
	save_i8(RMT_NR_BASE(gmode), gnoisereducts [gmode]);
	updateboard(1, 1);
}

#if WITHUSBUAC

/* переключение источника звука с USB или обычного для данного режима */
static void 
uif_key_click_datamode(void)
{
	gdatamode = calc_next(gdatamode, 0, 1);
	save_i8(RMT_DATAMODE_BASE, gdatamode);
	updateboard(1, 0);
}

#endif /* WITHUSBUAC */


#else /* WITHIF4DSP */
/* Переключение фильтров приёмника
	 - не вызывает сохранение состояния диапазона */
static void 
uif_key_changefilter(void)
{
	gfi = getsuitablerx(gmode, calc_next(gfi, 0, getgfasize() - 1));
	save_i8(RMT_FILTER_BASE(gmode), gfi);	/* только здесь сохраняем новый фильтр для режима */
	updateboard(1, 1);
}

#endif /* WITHIF4DSP */
///////////////////////////
// обработчики кнопок клавиатуры
//////////////////////////
#if WITHTX
/* включение режима настройки */
static void 
uif_key_tuneoff(void)
{
	if (getactualtune() || moxmode)
	{
		moxmode = 0;
		tunemode = 0;		/* не важно, по какой причине переходил на передачу - выход из режима при настройке */
#if WITHCAT
		cattunemode = 0;
#endif /* WITHCAT */
	}
	else
	{
		moxmode = calc_next(moxmode, 0, 1);
	}
	updateboard(1, 1);
}

///////////////////////////
// обработчики кнопок клавиатуры
//////////////////////////
/* включение режима настройки */

static void 
uif_key_tune(void)
{
	tunemode = calc_next(tunemode, 0, 1);
	updateboard(1, 1);
}

#endif /* WITHTX */

#if WITHKEYBOARD
#endif /* WITHKEYBOARD */


#if WITHAUTOTUNER

static void 
uif_key_bypasstoggle(void)
{
	const uint_fast8_t tx = 1;
	const uint_fast8_t bi = getbankindex_tx(tx);
	const vindex_t b = getvfoindex(bi);

	tunerwork = calc_next(tunerwork, 0, 1);	// переключаем в противоположное состояние

	savetunerallstate(bi, b);

	if (tunerwork == 0)
		reqautotune = 0;	// сброс идущей настройки
	updateboard(1, 0);
}

static void 
uif_key_atunerstart(void)
{
	const uint_fast8_t tx = 1;
	const uint_fast8_t bi = getbankindex_tx(tx);
	const vindex_t b = getvfoindex(bi);

	reqautotune = 1;
	// отработка перехода в режим передачи делается в основном цикле
	tunerwork = 1;
	save_i8(offsetof(struct nvmap, bands[b].tunerwork), 1);
	updateboard(1, 0);
}

uint_fast8_t
hamradio_get_bypvalue(void)
{
	return ! tunerwork;
}

uint_fast8_t
hamradio_get_atuvalue(void)
{
	return reqautotune;
}
#endif /* WITHAUTOTUNER */

#if WITHNOTCHONOFF || WITHNOTCHFREQ

uint_fast8_t hamradio_get_notchvalue(int_fast32_t * p)
{
#if WITHNOTCHFREQ
	* p = gnotchfreq.value;
#else /* WITHNOTCHFREQ */
	* p = 0;
#endif /* WITHNOTCHFREQ */
	return gnotch && notchmodes [gnotchtype].code != BOARD_NOTCH_OFF;
}

const FLASHMEM char * hamradio_get_notchtype5_P(void)
{
	return notchmodes [gnotchtype].label;
}


#endif /* WITHNOTCHONOFF || WITHNOTCHFREQ  */

#if WITHINTEGRATEDDSP
// NR ON/OFF
uint_fast8_t hamradio_get_nrvalue(int_fast32_t * p)
{
	* p = gnoisereductvl;
	return gnoisereducts [gmode] != 0;
}
#endif /* WITHINTEGRATEDDSP */

// текущее состояние TUNE
uint_fast8_t hamradio_get_tunemodevalue(void)
{
	return getactualtune();
}

#if WITHVOLTLEVEL

// Вольты в десятых долях
uint_fast8_t hamradio_get_volt_value(void)
{
#if WITHTDIRECTDATA

	return (sadcvalholder_t) board_getadc_filtered_truevalue(VOLTMRRIX) / 100;	// миливольты в 0.1 вольта

#elif WITHTARGETVREF

	unsigned Vref_mV = board_getadc_unfiltered_truevalue(VREFMVMRRIX); //WITHTARGETVREF;//ADCVREF_CPU * 100;
	if (Vref_mV == 0)
		Vref_mV = WITHTARGETVREF;
	const unsigned voltcalibr_mV = (Vref_mV * (VOLTLEVEL_UPPER + VOLTLEVEL_LOWER) + VOLTLEVEL_LOWER / 2) / VOLTLEVEL_LOWER;		// Напряжение fullscale - что показать при ADCVREF_CPU вольт на входе АЦП
	const uint_fast16_t mv = board_getadc_filtered_u16(VOLTMRRIX, 0, voltcalibr_mV);
	//PRINTF(PSTR("hamradio_get_volt_value: ref=%u, VrefmV=%u, v=%u, out=%u\n"), ref, Vref_mV, mv, (mv + 50) / 100);
	return (mv + 50) / 100;	// Приводим к десятым долям вольта

#elif WITHREFSENSOR
	// Измерение опрного напряжения
	const uint_fast8_t vrefi = VREFIX;
	const adcvalholder_t ref = board_getadc_unfiltered_truevalue(vrefi);	// текущее значение данного АЦП
	if (ref != 0)
	{
		//const unsigned Vref_mV = ADCVREF_CPU * 100;
		const unsigned Vref_mV = (uint_fast32_t) board_getadc_fsval(vrefi) * WITHREFSENSORVAL / ref;
		const unsigned voltcalibr_mV = (Vref_mV * (VOLTLEVEL_UPPER + VOLTLEVEL_LOWER) + VOLTLEVEL_LOWER / 2) / VOLTLEVEL_LOWER;		// Напряжение fullscale - что показать при ADCVREF_CPU вольт на входе АЦП
		const uint_fast16_t mv = board_getadc_filtered_u16(VOLTMRRIX, 0, voltcalibr_mV);
		//PRINTF(PSTR("hamradio_get_volt_value: ref=%u, VrefmV=%u, v=%u, out=%u\n"), ref, Vref_mV, mv, (mv + 50) / 100);
		return (mv + 50) / 100;	// Приводим к десятым долям вольта
	}
	else
	{
		//PRINTF(PSTR("hamradio_get_volt_value: ref=%u\n"), ref);
		return UINT8_MAX;
	}

#elif CTLSTYLE_SW2011ALL

	//PRINTF(PSTR("hamradio_get_volt_value: VOLTMRRIX=%u, voltcalibr100mV=%u\n"), board_getadc_unfiltered_truevalue(VOLTMRRIX), voltcalibr100mV);
	return board_getadc_unfiltered_u8(VOLTSOURCE, 0, voltcalibr100mV);

#else /* WITHREFSENSOR */

	// TODO: разобраться почему это не работает на SW20xx
	//PRINTF(PSTR("hamradio_get_volt_value: VOLTMRRIX=%u, voltcalibr100mV=%u\n"), board_getadc_unfiltered_truevalue(VOLTMRRIX), voltcalibr100mV);
	return board_getadc_filtered_u8(VOLTMRRIX, 0, voltcalibr100mV);

#endif /* WITHREFSENSOR */
}

#endif /* WITHVOLTLEVEL */

#if WITHTHERMOLEVEL

// Градусы в десятых долях
// Read from thermo sensor ST LM235Z (2 kOhm to +12)
int_fast16_t hamradio_get_temperature_value(void)
{
	const int_fast16_t thermo_offset = THERMOSENSOR_OFFSET;

	// XTHERMOIX - данные с АЦП напрямую
#if WITHTDIRECTDATA

	return (sadcvalholder_t) board_getadc_filtered_truevalue(XTHERMOMRRIX);	// 0.1 градуса

#elif WITHTARGETVREF

	unsigned Vref_mV = board_getadc_unfiltered_truevalue(VREFMVMRRIX); //WITHTARGETVREF;//ADCVREF_CPU * 100;
	if (Vref_mV == 0)
		Vref_mV = WITHTARGETVREF;
	const int_fast32_t mv = (int32_t) board_getadc_filtered_u32(XTHERMOMRRIX, 0, (uint_fast64_t) Vref_mV * (THERMOSENSOR_UPPER + THERMOSENSOR_LOWER) / THERMOSENSOR_LOWER);
	return (mv + thermo_offset) / THERMOSENSOR_DENOM;	// Приводим к десятым долям градуса

#elif WITHREFSENSOR
	// Измерение опрного напряжения
	const uint_fast8_t vrefi = VREFIX;
	const adcvalholder_t ref = board_getadc_unfiltered_truevalue(vrefi);	// текущее значение данного АЦП
	if (ref != 0)
	{
		const unsigned Vref_mV = (uint_fast32_t) board_getadc_fsval(vrefi) * WITHREFSENSORVAL / ref;
		const int_fast32_t mv = (int32_t) board_getadc_filtered_u32(XTHERMOMRRIX, 0, (uint_fast64_t) Vref_mV * (THERMOSENSOR_UPPER + THERMOSENSOR_LOWER) / THERMOSENSOR_LOWER);
		return (mv + thermo_offset) / THERMOSENSOR_DENOM;	// Приводим к десятым долям градуса
	}
	else
	{
		PRINTF(PSTR("hamradio_get_temperature_value: ref=%u\n"), ref);
		return 999;
	}

#else /* WITHREFSENSOR */

	const unsigned Vref_mV = ADCVREF_CPU * 100;
	const int_fast32_t mv = (int32_t) board_getadc_filtered_u32(XTHERMOMRRIX, 0, (uint_fast64_t) Vref_mV * (THERMOSENSOR_UPPER + THERMOSENSOR_LOWER) / THERMOSENSOR_LOWER);
	return (mv + thermo_offset) / THERMOSENSOR_DENOM;	// Приводим к десятым долям градуса

#endif /* WITHREFSENSOR */
}

#endif /* WITHTHERMOLEVEL */

#if (WITHCURRLEVEL || WITHCURRLEVEL2)

// Ток в десятках милиампер (может быть отрицательным)
// PA current sense - ACS712ELCTR-05B-T chip
// PA current sense - ACS712ELCTR-30B-T chip
int_fast16_t hamradio_get_pacurrent_value(void)
{
	// Чувствительность датчиков:
	// x05B - 0.185 V/A
	// x20A - 0.100 V/A
	// x30A - 0.066 V/A

#if WITHCURRLEVEL_ACS712_30A
	// x30A - 0.066 V/A
	enum {
		sens = 66,			// millivolts / ampher
		scale = 100			// результат - в сотых долях ампера
	};
#elif WITHCURRLEVEL_ACS712_20A
	//  x20A - 0.100 V/A
	enum {
		sens = 100,			// millivolts / ampher
		scale = 100			// результат - в сотых долях ампера
	};
#else /* WITHCURRLEVEL_ACS712_30A */
	// x05B - 0.185 V/A
	enum {
		sens = 185,			// millivolts / ampher
		scale = 100			// результат - в сотых долях ампера
	};
#endif /* CTLSTYLE_RA4YBO_V3 */

#if WITHCURRLEVEL

	const uint_fast8_t adci = PASENSEMRRIX;

#elif WITHCURRLEVEL2

#endif

#if WITHTDIRECTDATA

#elif WITHTARGETVREF

	unsigned Vref_mV = board_getadc_unfiltered_truevalue(VREFMVMRRIX); //WITHTARGETVREF;//ADCVREF_CPU * 100;
	if (Vref_mV == 0)
		Vref_mV = WITHTARGETVREF;

#elif WITHREFSENSOR
	// Измерение опрного напряжения
	const uint_fast8_t vrefi = VREFIX;
	const adcvalholder_t ref = board_getadc_unfiltered_truevalue(vrefi);	// текущее значение данного АЦП
	if (ref == 0)
		return 0;
	//const unsigned Vref_mV = ADCVREF_CPU * 100;
	const unsigned Vref_mV = (uint_fast32_t) board_getadc_fsval(vrefi) * WITHREFSENSORVAL / ref;
#else /* WITHREFSENSOR */
	// опорное напряжение известно из конфигурации процессора.
	const unsigned Vref_mV = ADCVREF_CPU * 100;
#endif /* WITHREFSENSOR */


#if WITHTDIRECTDATA

	int curr10 = (sadcvalholder_t) board_getadc_filtered_truevalue(PASENSEMRRIX) / 10;	// милиамперы в десятки милиампер

#elif WITHCURRLEVEL2

	const adcvalholder_t midp = board_getadc_unfiltered_truevalue(PAREFERIX2);
	const adcvalholder_t sense = board_getadc_unfiltered_truevalue(PASENSEIX2);

	const long curr10 = ((long) midp - (long) sense) * (int_fast64_t) Vref_mV * scale / ((long) sens * board_getadc_fsval(PAREFERIX2));

#else /* WITHCURRLEVEL2 */

	const long vsense = board_getadc_filtered_u32(PASENSEMRRIX, 0, (uint_fast32_t) Vref_mV * scale);

#if CTLSTYLE_RAVENDSP_V5
	const long midpoint = (gtx ? 2472uL : 2442uL) * scale; // tx=247200, rx=244200
#else
	static const long midpoint = 2516uL * scale;
#endif

	int curr10 = ((long) midpoint - (long) vsense + sens / 2) / sens;

#endif /* WITHCURRLEVEL2 */

	return curr10 + (gipacali + getipacalibase());
}

#endif /* (WITHCURRLEVEL || WITHCURRLEVEL2) */

uint_fast8_t hamradio_get_tx(void)
{
	return gtx;
}

// RX bandwidth
#if WITHIF4DSP

const FLASHMEM char * hamradio_get_rxbw_value_P(void)
{
	const uint_fast8_t bwseti = mdt [gmode].bwsetis [gtx];	// индекс банка полос пропускания для данного режима
	return bwsetsc [bwseti].labels [bwsetpos[bwseti]];
}

#else /* WITHIF4DSP */

const FLASHMEM char * hamradio_get_rxbw_value_P(void)
{
#if WITHFIXEDBFO
	return PSTR("");
#else /* WITHFIXEDBFO */
	return getrxfilter(gsubmode, gfi)->labelf3;
#endif /* WITHFIXEDBFO */
}

#endif /* WITHIF4DSP */

// RX preamplifier
const FLASHMEM char * hamradio_get_pre_value_P(void)
{
#if ! WITHONEATTONEAMP
	return pampmodes [gpamps [getbankindex_tx(0)]].label;
#else /* ! WITHONEATTONEAMP */
	return PSTR("   ");
#endif /* ! WITHONEATTONEAMP */
}

// RX attenuator (or att/pre).
const FLASHMEM char * hamradio_get_att_value_P(void)
{
	return attmodes [gatts [getbankindex_tx(0)]].label;
}

// RX agc time - 3 символа
const FLASHMEM char * hamradio_get_agc3_value_P(void)
{
#if ! WITHAGCMODENONE
	return agcmodes [gagcmode].label3;
#else /* ! WITHAGCMODENONE */
	return PSTR("   ");
#endif /* ! WITHAGCMODENONE */
}

// RX agc time - 4 символа
const FLASHMEM char * hamradio_get_agc4_value_P(void)
{
#if ! WITHAGCMODENONE
	return agcmodes [gagcmode].label4;
#else /* ! WITHAGCMODENONE */
	return PSTR("    ");
#endif /* ! WITHAGCMODENONE */
}

#if WITHPOWERLPHP
// HP/LP
const FLASHMEM char * hamradio_get_hplp_value_P(void)
{
	return pwrmodes [gpwri].label;
}
#endif /* WITHPOWERLPHP */


///////////////////////////
// обработчики кнопок клавиатуры
//////////////////////////
#if WITHUSEDUALWATCH

/* переключение основного/дополнительного приёмника */
static void 
uif_key_mainsubrx(void)
{
	mainsubrxmode = calc_next(mainsubrxmode, 0, MAINSUBRXMODE_COUNT - 1);
	save_i8(RMT_MAINSUBRXMODE_BASE, mainsubrxmode);	// Левый/правый, A - main RX, B - sub RX
	updateboard(1, 0);
}

// текущее состояние DUAL WATCH
const FLASHMEM char * hamradio_get_mainsubrxmode3_value_P(void)
{
	return mainsubrxmodes [mainsubrxmode].label;
}


#endif /* WITHUSEDUALWATCH */

///////////////////////////
// обработчики кнопок клавиатуры
//////////////////////////
/* переключение шага
	 - не вызывает сохранение состояния диапазона */
//static void 
//uif_key_changestep(uint_fast8_t tx)
//{
//while (repeat --)
//	lockmode = calc_next(lockmode, 0, 1);
//save_i8(RMT_LOCKMODE_BASE, lockmode);
//}

///////////////////////////
// обработчики кнопок клавиатуры
//////////////////////////
/* заглушка для клавиш, которые ничего не делают.
*/
//static void 
//uif_key_dummy(void)
//{
//}

/* Template - Xxxx */

/*
static void 
uif_key_click_xxxx(void)
{

}
*/
///////////////////////////
// были обработчики кнопок клавиатуры
///////////////////////////

// ****************
// NMEA parser
// dummy function
#if WITHNMEA && 0

enum nmea_states
{
	NMEAST_INITIALIZED,
	NMEAST_OPENED,	// встретился символ '$'
	NMEAST_CHSHI,	// прём старшего символа контрольной суммы
	NMEAST_CHSLO,	// приём младшего символа контрольной суммы
	//
	NMEAST_COUNTSTATES

};


typedef struct timeholder
{
	uint_fast8_t ms;
	uint_fast8_t secounds;
	uint_fast8_t minutes;
	uint_fast8_t hours;
	uint_fast8_t valid;
} timeholder_t;

// исправить время на следующую секунду
void time_next(
	timeholder_t * t
	)
{
	const uint_fast8_t a = 1;
	if ((t->secounds += a) >= 60)
	{
		t->secounds -= 60;
		if ((t->minutes += 1) >= 60)
		{
			t->minutes -= 60;
			if ((t->hours += 1) >= 24)
			{
				t->hours -= 24;
			}
		}
	}
}
// исправить время на предидущую секунду
void time_prev(
	timeholder_t * t
	)
{
	const uint_fast8_t a = 1;
	if ((t->secounds -= a) >= 60)
	{
		t->secounds += 60;
		if ((t->minutes -= 1) >= 60)
		{
			t->minutes += 60;
			if ((t->hours -= 1) >= 24)
			{
				t->hours += 24;
			}
		}
	}
}

static uint_fast8_t nmea_state = NMEAST_INITIALIZED;
static uint_fast8_t nmea_checksum;
static uint_fast8_t nmea_chsval;
static uint_fast8_t nmea_param;		// номер принимаемого параметра в строке
static uint_fast8_t nmea_chars;		// количество символов, помещённых в буфер

#define NMEA_PARAMS 20
#define NMEA_CHARS 12	// really need 11
static char nmea_buff [NMEA_PARAMS] [NMEA_CHARS];
static volatile timeholder_t nmea_time;
static timeholder_t th;
static volatile uint_fast8_t secoundticks;

static unsigned char hex2int(uint_fast8_t c)
{
	if (local_isdigit(c))
		return c - '0';
	if (isupper(c))
		return c - 'A' + 10;
	if (islower(c))
		return c - 'a' + 10;
	return 0;
}

void nmea_parsrchar(uint_fast8_t c)
{

	switch (nmea_state)
	{
	case NMEAST_INITIALIZED:
		if (c == '$')
		{
			nmea_checksum = '*';
			nmea_state = NMEAST_OPENED;
			nmea_param = 0;		// номер принимаемого параметра в строке
			nmea_chars = 0;		// количество символов, помещённых в буфер
		}
		break;

	case NMEAST_OPENED:
		nmea_checksum ^= c;
		if (c == ',')
		{
			// закрываем буфер параметра, переходим к следующему параметру
			nmea_buff [nmea_param][nmea_chars] = '\0';
			nmea_param += 1;
			nmea_chars = 0;
		}
		else if (c == '*')
		{
			// закрываем буфер параметра, переходим к следующему параметру
			nmea_buff [nmea_param][nmea_chars] = '\0';
			nmea_param += 1;
			// переходим к приёму контрольной суммы
			nmea_state = NMEAST_CHSHI;
		}
		else if (nmea_param < NMEA_PARAMS && nmea_chars < (NMEA_CHARS - 1))
		{
			nmea_buff [nmea_param][nmea_chars] = c;
			nmea_chars += 1;
			//stat_l1 = stat_l1 > nmea_chars ? stat_l1 : nmea_chars;
		}
		else
			nmea_state = NMEAST_INITIALIZED;	// при ошибках формата строки
		break;

	case NMEAST_CHSHI:
		nmea_chsval = hex2int(c) * 16;
		nmea_state = NMEAST_CHSLO;
		break;

	case NMEAST_CHSLO:
		nmea_state = NMEAST_INITIALIZED;
		if (nmea_checksum == (nmea_chsval + hex2int(c)))
		{
			if (nmea_param > 2 && 
				strcmp(nmea_buff [0], "GPRMC") == 0 && 
				strcmp(nmea_buff [2], "A") == 0 &&
				1)
			{
				// разбор времени
				const char * const s = nmea_buff [1];	// начало буфера, где лежит строка времени.формата 044709.00 или 044709.000
				nmea_time.hours = (s [0] - '0') * 10 + (s [1] - '0');
				nmea_time.minutes = (s [2] - '0') * 10 + (s [3] - '0');
				nmea_time.secounds = (s [4] - '0') * 10 + (s [5] - '0');
				nmea_time.ms = 0; //strtoul(s + 7, NULL, 10);
				nmea_time.valid = 1;
				time_next(& nmea_time);	// какое время надо будет поставить для установки в следующий PPS
			}
		}
		break;
	}
}


#if WITHLFM
	static uint_fast16_t lfmtoffset = 0;
	static uint_fast16_t lfmtinterval = 30;
	static uint_fast8_t lfmmode = 1;
	static uint_fast16_t lfmstart100k = 80;
	static uint_fast16_t lfmstop100k = 180;
	static uint_fast16_t lfmspeed1k = 500;

// Используются параметры
// lfmtoffset - Секунды от начала часа до запуска
// lfmtinterval - Интервал в секундах между запусками в пределах часа
// возврат не-0 в случае подходящего времени для запуска.
static uint_fast8_t 
islfmstart(unsigned now)
{
	unsigned s;
	for (s = lfmtoffset; s < 60 * 60; s += lfmtinterval)
	{
		if (s == now)
			return 1;
	}
	return 0;
}

#endif /* WITHLFM */

static timeholder_t th;
// Обработчик вызывается при приходе очередного импульса PPS
void 
RAMFUNC_NONILINE 
spool_nmeapps(void)
{
	th = nmea_time;
#if WITHLFM
	//lfm_run();
	//return;
	//memcpy(& th, & nmea_time, sizeof th);
	//th = nmea_time;
	//secoundticks = 0;	// следующее обновление через секунду
	if (lfmmode != 0 && nmea_time.valid && islfmstart(nmea_time.minutes * 60 + nmea_time.secounds))
	{
		lfm_run();
	}
#endif /* WITHLFM */
}

/* вызывается из обработчика прерываний */
// произошла потеря символа (символов) при получении данных с CAT компорта
void nmea_rxoverflow(void)
{

}

// dummy function
/* вызывается из обработчика прерываний */
void nmea_sendchar(void * ctx)
{

}
#endif /* WITHNMEA */

// S-METER, SWR-METER, POWER-METER
/* отображение S-метра или SWR-метра на приёме или передаче */
// Функция вызывается из display2.c
void
display2_bars(
	uint_fast8_t x,
	uint_fast8_t y,
	dctx_t * pctx
	)
{
#if WITHBARS
	if (userfsg)
	{
	}
	else if (gtx)
	{
		display2_bars_tx(x, y, pctx);
	}
	else
	{
		display2_bars_rx(x, y, pctx);
	}
#endif /* WITHBARS */
}

static volatile uint_fast8_t counterupdatedfreqs;
static volatile uint_fast8_t counterupdatedmodes;
static volatile uint_fast16_t counterupdatedvoltage; // счетчик для обновления вольтажа АКБ

static volatile uint_fast8_t counterupdatebars;
static volatile uint_fast8_t counterupdatewpm;

/* 
	отсчёт времени по запрещению обновления дисплея при вращении валкодера.
	обновлению s-метра
	обновлению вольтметра
	обновлению режимов работы
	Вызывается из обработчика таймерного прерывания
*/
static void
display_spool(void * ctx)
{
	#if 0 //WITHNMEA
	// таймер обновления часов/минут/секунд
	{
		enum { n = NTICKS(1000) };
		const uint_fast8_t t = secoundticks + 1;
		if (t >= n)
		{
			time_next(& th);
			secoundticks = 0;
		}
		else
		{
			secoundticks = t;
		}
	}
	#endif /* WITHNMEA */

#if 0
	// таймер обновления режимов работы
	{
		const uint_fast8_t t = counterupdatedmodes;
		if (t != 0)
			counterupdatedmodes = t - 1;
	}
#endif

	// таймер обновления частоты
	{
		const uint_fast8_t t = counterupdatedfreqs;
		if (t != 0)
			counterupdatedfreqs = t - 1;
	}
	//отсчёт времени для обновления индикатора АКБ
	{
		const uint_fast16_t t = counterupdatedvoltage;
		if (t != 0)
			counterupdatedvoltage = t - 1;

	}

	// таймер обновления индикатора SWR или мощности
	{
		const uint_fast8_t t = counterupdatebars;
		if (t != 0)
			counterupdatebars = t - 1;
	}

	// отсчёт времени для считывания потенциометров с панели управления
	{
		const uint_fast8_t t = counterupdatewpm;
		if (t != 0)
			counterupdatewpm = t - 1;
	}
}

// Проверка разрешения обновления скорости передачи в телеграфе.
// а так же других потенциометров - громкости, усиления ПЧ
static uint_fast8_t
display_refreshenabled_wpm(void)
{
	return getstablev8(& counterupdatewpm) == 0;		/* таймер дошёл до нуля - можно обновлять. */
}

// подтверждение выполненного обновления скорости передачи в телеграфе.
// а так же других потенциометров - громкости, усиления ПЧ
static void
display_refreshperformed_wpm(void)
{
	const uint_fast8_t n = NTICKS(100);	// 100 ms - обновление с частотой 10 герц

	system_disableIRQ();
	counterupdatewpm = n;
	system_enableIRQ();
}

// Проверка разрешения обновления дисплея (индикация напряжения/тока).
static uint_fast8_t
display_refreshenabled_voltage(void)
{
	return getstablev16(& counterupdatedvoltage) == 0;		/* таймер дошёл до нуля - можно обновлять. */
}

// подтверждение выполненного обновления дисплея (напряжение питания).
static void
display_refreshperformed_voltage(void)
{
	const uint_fast16_t n = NTICKS(500);	/* 1/2 секунды */

	system_disableIRQ();
	counterupdatedvoltage = n;
	system_enableIRQ();
}


// Проверка разрешения обновления дисплея (индикация частоты).
static uint_fast8_t
display_refreshenabled_freqs(void)
{
	return getstablev8(& counterupdatedfreqs) == 0;		/* таймер дошёл до нуля - можно обновлять. */
}

// подтверждение выполненного обновления дисплея (индикация частоты).
static void
display_refreshperformed_freqs(void)
{
	const uint_fast8_t n = NTICKS(1000 / gdisplayfreqsfps);	// 50 ms - обновление с частотой 20 герц

	system_disableIRQ();
	counterupdatedfreqs = n;
	system_enableIRQ();
}

// Проверка разрешения обновления дисплея (индикация режимов, приём/передача).
static uint_fast8_t
display_refreshenabled_modes(void)
{
	return 1;
	//return getstablev8(& counterupdatedmodes) == 0;		/* таймер дошёл до нуля - можно обновлять. */
}

// подтверждение выполненного обновления дисплея (индикация режимов, приём/передача).
static void
display_refreshperformed_modes(void)
{
	return;	// TODO: пока этот таймер не работает
	const uint_fast8_t n = NTICKS(1000 / displaymodesfps);	// 50 ms - обновление с частотой 20 герц

	system_disableIRQ();
	counterupdatedmodes = n;
	system_enableIRQ();
}


dctx_t * display2_getcontext(void)
{
#if WITHDIRECTFREQENER
	static editfreq2_t ef;
	static dctx_t ctx;

	ef.freq = editfreq;
	ef.blinkpos = blinkpos;
	ef.blinkstate = blinkstate;

	ctx.type = DCTX_FREQ;
	ctx.pv = & ef;
	return editfreqmode ? & ctx : NULL;
#else /*  WITHDIRECTFREQENER */
	return NULL;
#endif /* WITHDIRECTFREQENER */
}

/* отображение частоты (частот) настройки */
static void
display_freqpair(void)
{
#if LCDMODE_LTDC == 0
#if WITHDIRECTFREQENER

	if (editfreqmode)
	{
		display2_dispfreq_a2(editfreq, blinkpos + 1, blinkstate, amenuset());
	}
	else
	{
		display2_dispfreq_ab(amenuset());	/* отображение всех индикаторов частоты */
	}

#else /* WITHDIRECTFREQENER */

	display2_dispfreq_ab(amenuset());		/* отображение всех индикаторов частоты */

#endif /* WITHDIRECTFREQENER */
#endif /* LCDMODE_LTDC == 0 */
}

// Проверка разрешения обновления дисплея (индикация SWR/S-метр).
static uint_fast8_t
display_refresenabled_bars(void)
{
	return getstablev8(& counterupdatebars) == 0;		/* таймер дошёл до нуля - можно обновлять. */
}
// подтверждение выполненного обновления дисплея (индикация SWR/S-метр).
static void
display_refreshperformed_bars(void)
{
	const uint_fast8_t n = NTICKS(1000 / gdisplaybarsfps);	// 50 ms - обновление с частотой 20 герц

	system_disableIRQ();
	counterupdatebars = n;
	system_enableIRQ();
}

/* обновление динамической части отображения - S-метра или SWR-метра и volt-метра. */
static void 
//NOINLINEAT
display2_redrawbarstimed(
	uint_fast8_t immed,	// Безусловная перерисовка изображения
	uint_fast8_t extra,		/* находимся в режиме отображения настроек */
	const FLASHMEM struct menudef * mp
	)
{
	if (immed || display_refresenabled_bars())
	{
		/* быстро меняющиеся значения с частым опорсом */
		main_speed_diagnostics();
		/* +++ переписываем значения из возможно внешних АЦП в кеш значений */
	#if WITHSWRMTR
		board_adc_store_data(PWRMRRIX, board_getadc_unfiltered_truevalue(PWRI));
		board_adc_store_data(FWDMRRIX, board_getadc_unfiltered_truevalue(FWD));
		board_adc_store_data(REFMRRIX, board_getadc_unfiltered_truevalue(REF));
	#elif WITHPWRMTR
		board_adc_store_data(PWRMRRIX, board_getadc_unfiltered_truevalue(PWRI));
	#endif /* WITHSWRMTR || WITHPWRMTR */
	#if WITHCURRLEVEL2
		board_adc_store_data(PASENSEMRRIX2, board_getadc_unfiltered_truevalue(PASENSEIX2));
		board_adc_store_data(PAREFERMRRIX2, board_getadc_unfiltered_truevalue(PAREFERIX2));
	#elif WITHCURRLEVEL
		board_adc_store_data(PASENSEMRRIX, board_getadc_unfiltered_truevalue(PASENSEIX));
	#endif /* WITHCURRLEVEL */
		/* --- переписываем значения из возможно внешних АЦП в кеш значений */

		/* отрисовка элементов, общих для всех режимов отображения */
		/* отрисовка элементов, специфических для данного режима отображения */
		display2_barmeters_subset(amenuset(), extra);
		// подтверждение отрисовки
		display_refreshperformed_bars();
	}

	if (immed || display_refreshenabled_voltage())
	{
		looptests();		// Периодически вызывается в главном цикле - тесты
#if WITHAUTOTUNER
		//if (gtx && ! reqautotune)
		{
			adcvalholder_t r;
			adcvalholder_t f;
			const uint_fast16_t swr = tuner_get_swr("main_loop", TUS_SWRMAX, & r, & f);

		}
#endif /* WITHAUTOTUNER */
		/* медленно меняющиеся значения с редким опорсом */
		/* +++ переписываем значения из возможно внешних АЦП в кеш значений */
	#if WITHTHERMOLEVEL
		// ST LM235Z test values:
		// 2.98 V @ 25C
		// 2.98 / 5.7 = 0.5223V at ADC input
		// 0.5223V * 4095 / 3.3 = 648.75
		// test value = 649, expected temperature approx 25C

		board_adc_store_data(XTHERMOMRRIX, board_getadc_unfiltered_truevalue(XTHERMOIX));
	#endif /* WITHTHERMOLEVEL */
	#if WITHVOLTLEVEL
		board_adc_store_data(VOLTMRRIX, board_getadc_unfiltered_truevalue(VOLTSOURCE));
	#endif /* WITHVOLTLEVEL */
		/* --- переписываем значения из возможно внешних АЦП в кеш значений */

		display2_volts(amenuset(), extra);
		if (extra)
		{
			display2_menu(mp, 0);	// на дисплее без offscreen composition обновляем только индикацию значения
		}
		display_refreshperformed_voltage();
	}
}

// обновимть изображение частоты на дисплее
static void 
//NOINLINEAT
display_redrawfreqstimed(
	uint_fast8_t immed	// Безусловная перерисовка изображения
	)
{
	if (immed || display_refreshenabled_freqs())
	{
		display_freqpair();	/* обновление показания частоты */
		display_refreshperformed_freqs();
	}
}

// обновимть изображение режимов работы на дисплее
static void 
//NOINLINEAT
display_redrawmodestimed(
	uint_fast8_t immed	// Безусловная перерисовка изображения
	)
{
	if (immed || display_refreshenabled_modes())
	{
		/* отрисовка элементов, общих для всех режимов отображения */
		/* отрисовка элементов, специфических для данного режима отображения */
		display2_mode_subset(amenuset());
		// подтверждение отрисовки
		display_refreshperformed_modes();
	}
}

// Обновление дисплея - всё, включая частоту
static void
//NOINLINEAT
display_redrawfreqmodesbarsnow(
	uint_fast8_t extra,		/* находимся в режиме отображения настроек */
	const FLASHMEM struct menudef * mp
	)
{
	if (extra == 0)
	{
		display_redrawfreqstimed(1);	/* безусловное обновление показания частоты */
		display_redrawmodestimed(1);
		display2_redrawbarstimed(1, extra, mp);	/* обновление динамической части отображения - обновление S-метра или SWR-метра и volt-метра. */
	}
	else
	{
		display2_redrawbarstimed(1, extra, mp);	/* обновление динамической части отображения - обновление S-метра или SWR-метра и volt-метра. */
	}
}

static void 
directctlupdate(
	uint_fast8_t inmenu,
	const FLASHMEM struct menudef * mp
	)
{
	uint_fast8_t changed = 0;
	uint_fast8_t changedtx = 0;
#if WITHTX
	changedtx |= flagne_u8(& gtx, seq_get_txstate());	// текущее состояние прием или передача
#endif /* WITHTX */

	if (display_refreshenabled_wpm())
	{
		// +++ получение состояния органов управления */
	#if WITHPOTPOWER
		{
			static adcvalholder_t powerstate;
			changed |= FLAGNE_U8_CAT(& gnormalpower, board_getpot_filtered_u8(POTPOWER, WITHPOWERTRIMMIN, WITHPOWERTRIMMAX, & powerstate), CAT_PC_INDEX);	// регулировка мощности
		}
	#endif /* WITHPOTPOWER */
	#if WITHPOTWPM
		{
			static adcvalholder_t wpmstate;
			changed |= FLAGNE_U8_CAT(& elkeywpm, board_getpot_filtered_u8(POTWPM, CWWPMMIN, CWWPMMAX, & wpmstate), CAT_KS_INDEX);
		}
	#endif /* WITHPOTWPM */
	#if WITHPOTNFMSQL
		{
			static adcvalholder_t sqlstate;
			changed |= flagne_u8(& gsquelchNFM, board_getpot_filtered_u8(POTNFMSQL, 0, SQUELCHMAX, & sqlstate));
		}
	#endif /* WITHPOTNFMSQL */
	#if WITHPOTIFGAIN
		{
			static adcvalholder_t ifgainstate;
			changed |= FLAGNE_U16_CAT(& rfgain1, board_getpot_filtered_u16(POTIFGAIN, BOARD_IFGAIN_MIN, BOARD_IFGAIN_MAX, & ifgainstate), CAT_RG_INDEX);	// Параметр для регулировки усиления ПЧ
		}
	#endif /* WITHPOTIFGAIN */
	#if WITHPOTAFGAIN
		{
			static adcvalholder_t afgainstate;
			changed |= FLAGNE_U16_CAT(& afgain1, board_getpot_filtered_u16(POTAFGAIN, BOARD_AFGAIN_MIN, BOARD_AFGAIN_MAX, & afgainstate), CAT_AG_INDEX);	// Параметр для регулировки уровня на выходе аудио-ЦАП
		}
	#endif /* WITHPOTAFGAIN */
	#if WITHPBT && WITHPOTPBT
		{
			/* установка gpbtoffset PBTMIN, PBTMAX, midscale = PBTHALF */
			static adcvalholder_t pbtstate;
			changed |= flagne_u16(& gpbtoffset, board_getpot_filtered_u16(POTPBT, PBTMIN, PBTMAX, & pbtstate) / 10 * 10);
		}
	#endif /* WITHPBT && WITHPOTPBT */
	#if WITHIFSHIFT && WITHPOTIFSHIFT
		{
			/* установка gifshftoffset IFSHIFTTMIN, IFSHIFTMAX, midscale = IFSHIFTHALF */
			static adcvalholder_t ifshiftstate;
			changed |= flagne_u16(& ifshifoffset.value, board_getpot_filtered_u16(POTIFSHIFT, IFSHIFTTMIN, IFSHIFTMAX, & ifshiftstate) / 10 * 10);
		}
	#endif /* WITHIFSHIFT && WITHPOTIFSHIFT */
	#if WITHPOTNOTCH && WITHNOTCHFREQ
		{
			static adcvalholder_t notchstate;
			changed |= flagne_u16(& gnotchfreq.value, board_getpot_filtered_u16(POTNOTCH, WITHNOTCHFREQMIN, WITHNOTCHFREQMAX, & notchstate) / 50 * 50);	// регулировка частоты NOTCH фильтра
		}
	#endif /* WITHPOTNOTCH && WITHNOTCHFREQ */

	#if CTLSTYLE_RA4YBO_V3
		{
			changed |= flagne_u8(& guser2, kbd_get_ishold(KIF_USER2));
			changed |= flagne_u8(& guser3, kbd_get_ishold(KIF_USER3));
		}
	#endif /* CTLSTYLE_RA4YBO_V3 */
		// --- конец получения состояния органов управления */

		// подтверждаем, что обновление выполнено
		display_refreshperformed_wpm();
	}
	if (changedtx != 0)
	{
		updateboard(1, 1);	/* полная перенастройка (как после смены режима) */
		seq_ask_txstate(gtx);
		display_redrawfreqmodesbarsnow(inmenu, mp);	// Обновление дисплея - всё, включая частоту
	}
	else if (changed != 0)
		updateboard(1, 0);	/* полная перенастройка (как после смены режима) */
}

// *************************
// CAT sequence parser

// omnirig default settings: PTT control: RTS, CW control: DTR
// HXCat use same default settings


#if WITHCAT

static uint_fast8_t morsefill;	/* индекс буфера, заполняемого в данный момент. Противоположгый передаётся. */

static uint_fast8_t inpmorselength [2];
static uint_fast8_t sendmorsepos [2];

#if WITHCATEXT && WITHELKEY
	static void cat_set_kyanswer(uint_fast8_t force);
	static uint_fast8_t cathasparamerror;
	static unsigned char morsestring [2][25];
#endif /* WITHCATEXT && WITHELKEY */

static uint_fast8_t catstatein = CATSTATE_HALTED;

static volatile uint_fast8_t catstateout = CATSTATEO_HALTED;
static volatile const char * catsendptr;
static volatile uint_fast8_t catsendcount;

//A communication error occurred, such as an overrun or framing error during a serial data transmission.
//static const char processingcmd [2] = "E;";	// ответ на команду которая ещё выполняется

static uint_fast8_t 
cat_getstateout(void)
{
	return getstablev8(& catstateout);
}

/* вызывается из обработчика прерываний */
void cat2_sendchar(void * ctx)
{
	switch (catstateout)
	{
	case CATSTATEO_HALTED:
		//HARDWARE_CAT_ENABLETX(0);
		break;

	case CATSTATEO_WAITSENDREPLAY:
		if (catsendcount --)
			HARDWARE_CAT_TX(ctx, * catsendptr ++);
		else
		{
			HARDWARE_CAT_ENABLETX(0);
			catstateout = CATSTATEO_SENDREADY;
		}
		break;

	case CATSTATEO_SENDREADY:
		// обработка ситуации после USB DISCONNECT
		HARDWARE_CAT_ENABLETX(0);
		break;

	default:
		TP();
		// not need, but for safety.
		//HARDWARE_CAT_ENABLETX(0);
		break;
	}
}

// Вызов из user-mode программы
void 
//NOINLINEAT
static cat_answervariable(const char * p, uint_fast8_t len)
{
	//PRINTF(PSTR("cat_answervariable: '%*.*s'"), len, len, p);
	system_disableIRQ();
	if (catstateout != CATSTATEO_SENDREADY)
	{
		// Сейчас ещё передается сообщение - новое игнорируем.
		// Добавлено для поддержки отладки при работающем CAT
		system_enableIRQ();
		return;
	}
	if ((catsendcount = len) != 0)
	{
		catsendptr = p;
		catstateout = CATSTATEO_WAITSENDREPLAY;
		HARDWARE_CAT_ENABLETX(1);
	}
	else
	{
		//catstateout = CATSTATEO_SENDREADY;
		HARDWARE_CAT_ENABLETX(0);
	}
	system_enableIRQ();
}


// *************************

//#define CAT_ASKBUFF_SIZE (43 + 28)
#define CAT_ASKBUFF_SIZE (43)

static char cat_ask_buffer [CAT_ASKBUFF_SIZE];

static void 
//NOINLINEAT
cat_answer(uint_fast8_t len)
{
	cat_answervariable(cat_ask_buffer, len);
}

#if WITHELKEY

/* переключение на следующий буфер, если можно */
static void
morseswitchnext(void)
{
	const uint_fast8_t ms = ! morsefill;	// ms: morse sent
	if (sendmorsepos [ms] == inpmorselength [ms])
	{
		// закончили передавать
		if (sendmorsepos [morsefill] < inpmorselength [morsefill])
		{
			// заполнен приёмный буфер
			sendmorsepos [ms] = inpmorselength [ms] = 0;
			morsefill = ! morsefill;
		}
	}

}

#endif /* WITHELKEY */

static uint_fast8_t 
//NOINLINEAT
ascii_toupper(uint_fast8_t c)
{
	return toupper((unsigned char) c);
}

#if WITHCATEXT && WITHELKEY

/* todo: переделать на обновление параметра KY в ответе, если KY запрошен. */
static void 
//NOINLINEAT
cat_set_kyanswer(uint_fast8_t force)
{
	// проверка что все буферы заполнены
	//const uint_fast8_t f = (sendmorsepos [morsefill] != inpmorselength [morsefill]);
	const uint_fast8_t f = sendmorsepos [morsefill] < inpmorselength [morsefill];
	// Это была команда KY; (без параметров) - запрос состояния
	// KEYER BUFFER STATUS: 0 - BUFFER SPACE AVALIABLE, 1 - BUFFER FULL
	if (force)
		cat_answer_map [CAT_KY_INDEX] = 1;
	cat_answerparam_map [CAT_KY_INDEX] = '0' + f;
}

/* вызывается из обработчика прерываний */
/* Получить следующий символ для передачи (только верхний регистр) */
static char cat_getnextcw(void)
{
	morseswitchnext();	/* переключение на следующий буфер, если можно */
	cat_set_kyanswer(0);	// KEYER BUFFER STATUS: 0 - BUFFER SPACE AVALIABLE, 1 - BUFFER FULL
	const uint_fast8_t ms = ! morsefill;	// ms: morse sent
	while (sendmorsepos [ms] < inpmorselength [ms])
	{
		const char c = morsestring [ms] [sendmorsepos [ms] ++];
		if (c == ' ' && (sendmorsepos [ms] < inpmorselength [ms]) && morsestring [ms] [sendmorsepos [ms]] == ' ')
			continue;
		return c;
	}
	return '\0';
}

#endif /* WITHCATEXT && WITHELKEY */

/* вызывается из обработчика прерываний */
// произошла потеря символа (символов) при получении данных с CAT компорта
void cat2_rxoverflow(void)
{

}

/* вызывается из обработчика прерываний */
// произошёл разрыв связи при работе по USB CDC
void cat2_disconnect(void)
{
	uint_fast8_t i;
	for (i = 0; i < (sizeof cat_answer_map / sizeof cat_answer_map [0]); ++ i)
		cat_answer_map [i] = 0;

#if WITHTX
	sendmorsepos  [0] = 
	inpmorselength [0]= 
	sendmorsepos  [1] = 
	inpmorselength [1] = 0;
#endif /* WITHTX */

	//aistate = 0; /* Power-up state of AI mode = 0 (TS-590). */
	cattunemode = catstatetx = 0;
	catstatein = CATSTATE_WAITCOMMAND1;
	catstateout = CATSTATEO_SENDREADY;
}


/* вызывается из обработчика прерываний */
void cat2_parsechar(uint_fast8_t c)
{
	static uint_fast8_t catcommand1;
	static uint_fast8_t catcommand2;
	static uint_fast8_t cathasparam;
	static uint_fast8_t catp [CATPCOUNTSIZE];
	static uint_fast8_t catpcount;

   // PRINTF(PSTR("c=%02x, catstatein=%d, c1=%02X, c2=%02X\n"), c, catstatein, catcommand1, catcommand2);
	switch (catstatein)
	{
	case CATSTATE_HALTED:
		break;

	case CATSTATE_WAITCOMMAND1:
		if ((catcommand1 = ascii_toupper(c)) != ';')
			catstatein = CATSTATE_WAITCOMMAND2;
		break;
	
	case CATSTATE_WAITCOMMAND2:
		if ((catcommand2 = ascii_toupper(c)) == ';')
		{
			cat_answer_map [CAT_BADCOMMAND_INDEX] = 1;	// второй символ не буква, а ';' - преждевременный конец команды
			catstatein = CATSTATE_WAITCOMMAND1;
		}
#if WITHCATEXT && WITHELKEY
		else if (catcommand1 == 'K' && catcommand2 == 'Y')
		{
			catstatein = CATSTATE_WAITMORSE;
			cathasparam = 0;	// строка была - 1, иначе - 0
			catpcount = 0;
			cathasparamerror = 0;
		}
#endif	/* WITHCATEXT && WITHELKEY */
		else
		{
			catstatein = CATSTATE_WAITPARAM;
			cathasparam = 0;
			//cathasparamerror = 0;
			catpcount = 0;
		}
		break;

	case CATSTATE_WAITPARAM:
		//if (c >= '0' && c <= '9')
		if (local_isdigit((unsigned char) c))
		{
			cathasparam = 1;
			if (catpcount < (sizeof catp / sizeof catp [0]))
				catp [catpcount ++] = c;
		}
		else if (c == ';')
		{
			//catstatein = CATSTATE_READYCOMMAND;		// команда готова для интерпретации параметров

			uint8_t * buff;
			if (takemsgbufferfree_low(& buff) != 0)
			{
				uint_fast8_t i;
				// check MSGBUFFERSIZE8 valie
				buff [0] = catcommand1;
				buff [1] = catcommand2;
				buff [2] = cathasparam;
				
				buff [8] = catpcount;
				for (i = 0; i < catpcount; ++ i)
					buff [9 + i] = catp [i];

				placesemsgbuffer_low(MSGT_CAT, buff);
			}
			catstatein = CATSTATE_WAITCOMMAND1;	/* в user-mode нечего делать - ответ не формируем  */
		}
		else
		{
			;// остальные символы игнорируются //
		}
		break;

#if WITHCATEXT && WITHELKEY
	case CATSTATE_WAITMORSE:
		if (c == '\0')	// такой симвоь недопустим
		{
			cathasparamerror = 1;
		}
		else if (c != ';')
		{
			cathasparam = 1;	// признак наличия символов для передачи
			if (catpcount == 0 && c == '0')
			{
				sendmorsepos [0] = inpmorselength [0] = 0;	// очистить буфер передачи морзе.
				sendmorsepos [1] = inpmorselength [1] = 0;	// очистить буфер передачи морзе.
				cat_set_kyanswer(0);
			}
			
			if (sendmorsepos [morsefill] < inpmorselength [morsefill])
			{
				// Данные ещё не переданы - не портим буфер. Будет передана ошибка.
				cathasparamerror = 1;
			}
			else if (catpcount < (sizeof morsestring [morsefill] / sizeof morsestring [morsefill][0]))
			{
				/* запоминаем очередной символ для передачи */
				morsestring [morsefill] [catpcount ++] = ascii_toupper(c);
			}
			else
			{
				// слишком длинная строка для передачи
				cathasparamerror = 1;
			}
		}
		else
		{
			// Пришёл символ завершения команды.
			// Были ли ошибки?
			if (cathasparamerror != 0)
			{
				// были символы за кодом команды KY, но они небыли приняты из-за ошибок.
				// ошибка - это незакончившаяся ранее передача буфера.
				// Ещё не обработали старые данные для передачи
				cat_answer_map [CAT_BADCOMMAND_INDEX] = 1;
			}
			else if (cathasparam != 0)
			{
				/* более одного символа (включая пробелы в конце) - первый игнорируется */
				if (catpcount > 1)
				{
					/* устанавливаем индексы для передачи */
					inpmorselength  [morsefill] = catpcount;
					sendmorsepos  [morsefill] = 1;	/* Первый символ в буфере - пробел */

					morseswitchnext();	/* переключение на следующий буфер, если можно */
					cat_set_kyanswer(0);	// KEYER BUFFER STATUS: 0 - BUFFER SPACE AVALIABLE, 1 - BUFFER FULL
				}
			}
			else
			{
				cat_set_kyanswer(1);	// KEYER BUFFER STATUS: 0 - BUFFER SPACE AVALIABLE, 1 - BUFFER FULL
			}
			catstatein = CATSTATE_WAITCOMMAND1;	/* в user-mode нечего делать - ответ не формируем  */
		}
		break;
#endif /* WITHCATEXT && WITHELKEY */
	}
}

static void idanswer(uint_fast8_t arg)
{
	static const FLASHMEM char fmt_1 [] =
		"ID"			// 2 characters - status information code
		"%03d"		// P1 3 characters - model
		";";				// 1 char - line terminator
	// answer mode
	const uint_fast8_t len = local_snprintf_P(cat_ask_buffer, CAT_ASKBUFF_SIZE, fmt_1,
		(int) 21	// 021 - code of TS-590S model
		);
	cat_answer(len);
}

static void fvanswer(uint_fast8_t arg)
{
	static const FLASHMEM char fmt_0 [] =
		"FV"			// 2 characters - status information code
		"1.00"			// P1 4 characters - For example, for firmware version ..00, it reads “FV..00;.
		";";			// 1 char - line terminator

	// answer mode
	const uint_fast8_t len = local_snprintf_P(cat_ask_buffer, CAT_ASKBUFF_SIZE, fmt_0
		);
	cat_answer(len);
}

static void daanswer(uint_fast8_t arg)
{
	static const FLASHMEM char fmt_1 [] =
		"DA"			// 2 characters - status information code
		"%1d"			// P1 1 characters -  0: DATA mode OFF, 1: DATA mode ON, 
		";";			// 1 char - line terminator

	// answer mode
	const uint_fast8_t len = local_snprintf_P(cat_ask_buffer, CAT_ASKBUFF_SIZE, fmt_1,
		(int) 0	// 0/1: data mode on/off
		);
	cat_answer(len);
}

static void faanswer(uint_fast8_t arg)
{
	const uint_fast8_t bi = getbankindex_ab(0);	/* vfo A bank index */
	static const FLASHMEM char fmt_1 [] =
		"FA"				// 2 characters - status information code
		"%011ld"			// P1 11 characters - freq
		";";				// 1 char - line terminator
	// answer VFO A frequency
	const uint_fast8_t len = local_snprintf_P(cat_ask_buffer, CAT_ASKBUFF_SIZE, fmt_1,
		(signed long) gfreqs [bi]
		);
	cat_answer(len);
}

static void fbanswer(uint_fast8_t arg)
{
	const uint_fast8_t bi = getbankindex_ab(1);	/* vfo B bank index */
	static const FLASHMEM char fmt_1 [] =
		"FB"				// 2 characters - status information code
		"%011ld"			// P1 11 characters - freq
		";";				// 1 char - line terminator

	// answer VFO B frequency
	const uint_fast8_t len = local_snprintf_P(cat_ask_buffer, CAT_ASKBUFF_SIZE, fmt_1,
		(signed long) gfreqs [bi]
		);
	cat_answer(len);
}

static void spanswer(uint_fast8_t arg)
{
	//const uint_fast8_t bi = getbankindex_ab(1);	/* vfo B bank index */
	static const FLASHMEM char fmt_1 [] =
		"SP"				// 2 characters - status information code
		"%01d"			// P1 1 characters - 0: VFO A, 1: VFO B, 2: Memory Channel
		";";				// 1 char - line terminator

	// answer VFO B frequency
	const uint_fast8_t len = local_snprintf_P(cat_ask_buffer, CAT_ASKBUFF_SIZE, fmt_1,
		(int) (gsplitmode != VFOMODES_VFOINIT)
		);
	cat_answer(len);
}



static void franswer(uint_fast8_t arg)
{
	//const uint_fast8_t bi = getbankindex_ab(1);	/* vfo B bank index */
	static const FLASHMEM char fmt_1 [] =
		"FR"				// 2 characters - status information code
		"%01d"			// P1 1 characters - 0: VFO A, 1: VFO B, 2: Memory Channel
		";";				// 1 char - line terminator

	// answer VFO B frequency
	const uint_fast8_t len = local_snprintf_P(cat_ask_buffer, CAT_ASKBUFF_SIZE, fmt_1,
		(int) gvfosplit [0]	// At index 0: RX VFO A or B, at index 1: TX VFO A or B
		);
	cat_answer(len);
}

static void ftanswer(uint_fast8_t arg)
{
	//const uint_fast8_t bi = getbankindex_ab(1);	/* vfo B bank index */
	static const FLASHMEM char fmt_1 [] =
		"FT"				// 2 characters - status information code
		"%01d"			// P1 1 characters - 0: VFO A, 1: VFO B, 2: Memory Channel
		";";				// 1 char - line terminator

	// answer VFO B frequency
	const uint_fast8_t len = local_snprintf_P(cat_ask_buffer, CAT_ASKBUFF_SIZE, fmt_1,
		(int) gvfosplit [1]	// At index 0: RX VFO A or B, at index 1: TX VFO A or B
		);
	cat_answer(len);
}

static void mdanswer(uint_fast8_t arg)
{
	const uint_fast8_t bi = getbankindex_ab(0);	/* vfo B bank index */
	static const FLASHMEM char fmt_1 [] =
		"MD"				// 2 characters - status information code
		"%1d"				// 1 char - mode
		";";				// 1 char - line terminator

	// answer mode
	const uint_fast8_t len = local_snprintf_P(cat_ask_buffer, CAT_ASKBUFF_SIZE, fmt_1,
		(int) submodes [getsubmode(bi)].qcatmodecode
		);
	cat_answer(len);
}

static void fwanswer(uint_fast8_t arg)
{
	static const FLASHMEM char fmt_1 [] =
		"FW"			// 2 characters - status information code
		"%04d"			// P42 4 characters - DSP filtering bandwidth.
		";";				// 1 char - line terminator

	// answer mode
	const uint_fast8_t len = local_snprintf_P(cat_ask_buffer, CAT_ASKBUFF_SIZE, fmt_1,
		(int) getkenwoodfw(gsubmode, gfi) // полоса пропускания в герцах или код полосы пропускания
		);
	cat_answer(len);
}

#if WITHIF4DSP//CTLSTYLE_V1D || CTLSTYLE_OLEG4Z_V1 || 1
static void zyanswer(uint_fast8_t arg)
{
	//ZYmSLL
	static const FLASHMEM char fmt_3 [] =
		"ZY"			// 2 characters - status information code
		"%1d"			// 1 char - mode
		"%1d"			// state
		"%02d"			// level
		";";			// 1 char - line terminator

	const uint_fast8_t submode = findkenwoodsubmode(arg, SUBMODE_USB);	/* поиск по кенвудовскому номеру */
	const uint_fast8_t mode = submodes [submode].mode;

	// answer mode
	const uint_fast8_t len = local_snprintf_P(cat_ask_buffer, CAT_ASKBUFF_SIZE, fmt_3,
		(int) arg,
		(int) gnoisereducts [mode],
		(int) gnoisereductvl
		);
	cat_answer(len);
}

static void zzanswer(uint_fast8_t arg)
{
	//ZZmLLLLUUUUSSSS
	static const FLASHMEM char fmt_4 [] =
		"ZZ"			// 2 characters - status information code
		"%1d"			// 1 char - mode
		"%04d"			// right
		"%04d"			// left
		"%03d"			// slope
		";";			// 1 char - line terminator

	const uint_fast8_t submode = findkenwoodsubmode(arg, SUBMODE_USB);	/* поиск по кенвудовскому номеру */
	const FLASHMEM struct modetempl * const pmodet = getmodetempl(submode);
	const uint_fast8_t bwseti = pmodet->bwsetis [0];	// индекс банка полос пропускания для данного режима на приеме
	const uint_fast8_t pos = bwsetpos [bwseti];
	const bwprop_t * const p = bwsetsc [bwseti].prop [pos];

	// answer mode
	const uint_fast8_t len = local_snprintf_P(cat_ask_buffer, CAT_ASKBUFF_SIZE, fmt_4,
		(int) arg,
		(int) p->left10_width10,
		(int) p->right100,
		(int) p->afresponce
		);
	cat_answer(len);
}
#endif /* CTLSTYLE_V1D || CTLSTYLE_OLEG4Z_V1 */

#if WITHIF4DSP

static void aganswer(uint_fast8_t arg)
{
	// AF gain
	static const FLASHMEM char fmt_2 [] =
		"AG"			// 2 characters - status information code
		"%1d"			// P1 always 0
		"%03d"			// P2 0..255 Squelch level
		";";				// 1 char - line terminator

	// answer mode
	const uint_fast8_t len = local_snprintf_P(cat_ask_buffer, CAT_ASKBUFF_SIZE, fmt_2,
		(int) arg,
		(int) afgain1.value
		);
	cat_answer(len);
}

static void rganswer(uint_fast8_t arg)
{
	// RF (IF) gain
	static const FLASHMEM char fmt_1 [] =
		"RG"			// 2 characters - status information code
		"%03d"			// P1 0..255 RF Gain status
		";";				// 1 char - line terminator

	// answer mode
	const uint_fast8_t len = local_snprintf_P(cat_ask_buffer, CAT_ASKBUFF_SIZE, fmt_1,
		(int) rfgain1.value
		);
	cat_answer(len);
}

static void sqanswer(uint_fast8_t arg)
{
	static const FLASHMEM char fmt_2 [] =
		"SQ"			// 2 characters - status information code
		"%1d"			// P1 always 0
		"%03d"			// P2 0..255 Squelch level
		";";				// 1 char - line terminator

	// answer mode
	const uint_fast8_t len = local_snprintf_P(cat_ask_buffer, CAT_ASKBUFF_SIZE, fmt_2,
		(int) arg,
		(int) gsquelch.value
		);
	cat_answer(len);
}

static void nranswer(uint_fast8_t arg)
{
	static const FLASHMEM char fmt_1 [] =
		"NR"			// 2 characters - status information code
		"%1d"			// P1 NR0: nr off, NR1: nr1 on NR2: nr2 on
		";";			// 1 char - line terminator

	// answer mode
	const uint_fast8_t len = local_snprintf_P(cat_ask_buffer, CAT_ASKBUFF_SIZE, fmt_1,
		(int) (gnoisereducts [gmode] != 0 ? 2 : 0)
		);
	cat_answer(len);
}
#endif /* WITHIF4DSP */

#if WITHPOWERTRIM
static void pcanswer(uint_fast8_t arg)
{
	static const FLASHMEM char fmt_1 [] =
		"PC"			// 2 characters - status information code
		"%03d"			// P1 005..100: SSB/CW/FM/FSK, 25: AM
		";";				// 1 char - line terminator

	// answer mode
	// Нормирование значений WITHPOWERTRIMMIN..WITHPOWERTRIMMAX к диапазону Kenwook CAT
	const uint_fast8_t len = local_snprintf_P(cat_ask_buffer, CAT_ASKBUFF_SIZE, fmt_1,
		(int) ((gnormalpower.value - WITHPOWERTRIMMIN) * 95 / (WITHPOWERTRIMMAX - WITHPOWERTRIMMIN) + 5)
		);
	cat_answer(len);
}

#endif /* WITHPOWERTRIM */

#if WITHCATEXT && WITHELKEY

static void kyanswer(uint_fast8_t arg)
{
	static const FLASHMEM char fmt_1 [] =
		"KY"			// 2 characters - status information code
		"%c"			// P44 1 character - KEYER BUFFER STATUS: 0 - BUFFER SPACE AVALIABLE, 1 - BUFFER FULL
		";";				// 1 char - line terminator

	// answer mode
	const uint_fast8_t len = local_snprintf_P(cat_ask_buffer, CAT_ASKBUFF_SIZE, fmt_1,
		(char) arg
		);
	cat_answer(len);
}

static void ksanswer(uint_fast8_t arg)
{
	static const FLASHMEM char fmt_1 [] =
		"KS"			// 2 characters - status information code
		"%03d"			// P42 3 characters - keyer speed in WPM
		";";				// 1 char - line terminator

	// keyer speed information
	const uint_fast8_t len = local_snprintf_P(cat_ask_buffer, CAT_ASKBUFF_SIZE, fmt_1,
		(int) elkeywpm.value
		);
	cat_answer(len);
}
#endif /* WITHCATEXT && WITHELKEY */

static void txanswer(uint_fast8_t arg)
{
	static const FLASHMEM char fmt_1 [] =
		"TX"			// 2 characters - status information code
		"%01d"			// P1 1 character - TX reason
		";";			// 1 char - line terminator

	// answer mode
	const uint_fast8_t len = local_snprintf_P(cat_ask_buffer, CAT_ASKBUFF_SIZE, fmt_1,
		(int) (getactualtune() ? 2 : 0)
		);
	cat_answer(len);
}

static void rxanswer(uint_fast8_t arg)
{
	static const FLASHMEM char fmt_0 [] =
		"RX"			// 2 characters - status information code
		";";			// 1 char - line terminator

	// answer mode
	const uint_fast8_t len = local_snprintf_P(cat_ask_buffer, CAT_ASKBUFF_SIZE, fmt_0
		);
	cat_answer(len);
}

static void aianswer(uint_fast8_t arg)
{
	static const FLASHMEM char fmt_1 [] =
		"AI"			// 2 characters - status information code
		"%1d"			// 1 char - mode
		";";			// 1 char - line terminator

	// answer mode
	const uint_fast8_t len = local_snprintf_P(cat_ask_buffer, CAT_ASKBUFF_SIZE, fmt_1,
		(int) (aistate ? 2 : 0)
		);
	cat_answer(len);
}


#if WITHCATEXT

static void psanswer(uint_fast8_t arg)
{
	static const FLASHMEM char fmt_1 [] =
		"PS"			// 2 characters - status information code
		"%01d"			// P1 1 characters - POWER ON/OFF
		";";			// 1 char - line terminator

	// answer mode
	const uint_fast8_t len = local_snprintf_P(cat_ask_buffer, CAT_ASKBUFF_SIZE, fmt_1,
		(int) 1
		);
	cat_answer(len);
}

// used s9level, s9delta, s9_60_delta values for calculate s-meter points
static uint_fast8_t 
scaletopointssmeter(
	uint_fast8_t v,
	uint_fast8_t minval, uint_fast8_t maxval
	)
{
#if WITHBARS
	const int s0level = s9level - s9delta;
	const int s9_60level = s9level + s9_60_delta;

	if (v < s0level)
		v = s0level;
	if (v > s9_60level)
		v = s9_60level;
	
	return ((v - s0level) * 30) / (s9delta + s9_60_delta);
#else
	return 0;
#endif
}

static void smanswer(uint_fast8_t arg)
{
	// s-meter state answer
	// код SM9 введен для получения "сырого" уровня.
	static const FLASHMEM char fmt0_1 [] =
		"SM"			// 2 characters - status information code
		"0"				// 1 char - Always 0
		"%04d"				// 4 chars - s-meter points (0000..0030)
		";";				// 1 char - line terminator
	// s-meter state answer
	// код SM9 введен для получения "сырого" уровня.
	static const FLASHMEM char fmt9_1 [] =
		"SM"			// 2 characters - status information code
		"9"				// 1 char - Always 9
		"%+d"				// level in dBm
		";";				// 1 char - line terminator

	uint_fast8_t tracemax;
	uint_fast8_t v = board_getsmeter(& tracemax, 0, UINT8_MAX, arg == 9);

	switch (arg)
	{
	default:
	case 0:
		{
			// answer mode
			const uint_fast8_t len = local_snprintf_P(cat_ask_buffer, CAT_ASKBUFF_SIZE, fmt0_1,
				(int) scaletopointssmeter(v, 0, UINT8_MAX)
				);
			cat_answer(len);
		}
		break;

	case 9:
		{
			// answer mode
			int level = ((int) v - (int) UINT8_MAX);
			if (level < - 170)
				level = - 170;
			else if (level > 20)
				level = 20;
			const uint_fast8_t len = local_snprintf_P(cat_ask_buffer, CAT_ASKBUFF_SIZE, fmt9_1,
				(int) level
				);
			cat_answer(len);
		}
		break;
	}

}

static void raanswer(uint_fast8_t arg)
{
	// RX attenuator
	static const FLASHMEM char fmt_1 [] =
		"RA"				// 2 characters - status information code
		"%02d"				// 2 chars - 00: ATT OFF 01: ATT ON
		"00"				// 2 chars - 00: Always 00
		";";				// 1 char - line terminator

	// answer mode
	const uint_fast8_t len = local_snprintf_P(cat_ask_buffer, CAT_ASKBUFF_SIZE, fmt_1,
		(int) gatts [getbankindex_tx(0)]

		);
	cat_answer(len);
}

static void paanswer(uint_fast8_t arg)
{
#if ! WITHONEATTONEAMP
	uint_fast8_t v = (gpamps [getbankindex_tx(gtx)] != 0);
#else /* ! WITHONEATTONEAMP */
	enum { v = 0 };
#endif /* ! WITHONEATTONEAMP */

	// RX preamplifier
	static const FLASHMEM char fmt_1 [] =
		"PA"				// 2 characters - status information code
		"%01d"				// 1 chars - 0: Pre-amp OFF, 1: : Pre-amp ON
		"0"					// 1 chars - Always 0
		";";				// 1 char - line terminator

	// answer mode
	const uint_fast8_t len = local_snprintf_P(cat_ask_buffer, CAT_ASKBUFF_SIZE, fmt_1,
		(int) v

		);
	cat_answer(len);
}

#if WITHANTSELECT || WITHANTSELECTRX || WITHANTSELECT2

static void ananswer(uint_fast8_t arg)
{
	//const uint_fast8_t pathi = 0;	// A or B path 
	// Antenna information answer
	static const FLASHMEM char fmt_1 [] =
		"AN"				// 2 characters - status information code
		"%01d"				// 1 chars - 0: ant 1, 1: : ant 2
		"0"					// 1 chars - 0: RX ANT is not used
		"1"					// 1 chars - 1: : Drive Out ON
		";";				// 1 char - line terminator

	// answer mode
	const uint_fast8_t len = local_snprintf_P(cat_ask_buffer, CAT_ASKBUFF_SIZE, fmt_1,
		(int) ((gantennas [getbankindex_tx(0)] != 0) + 1)

		);
	cat_answer(len);
}

#endif /* WITHANTSELECT || WITHANTSELECTRX */

#if WITHTX && (WITHSWRMTR || WITHSHOWSWRPWR)

static uint_fast8_t kenwoodswrmeter(void)
{
	//const uint_fast8_t pathi = 0;	// A or B path 

	//enum { FS = SWRMIN * 15 / 10 };	// swr=1.0..4.0
	adcvalholder_t r;
	const adcvalholder_t f = board_getswrmeter(& r, swrcalibr);
	//const uint_fast16_t fullscale = FS - SWRMIN;
	uint_fast16_t swr10;		// рассчитанное  значение
	if (f < minforward)
		swr10 = 0;	// SWR=1
	else if (f <= r)
		swr10 = 30;		// SWR is infinite
	else
		swr10 = (f + r) * SWRMIN / (f - r) - SWRMIN;
	// v = 10..40 for swr 1..4
	// swr10 = 0..30 for swr 1..4
	return swr10;	// tested with ARCP950. 0: SWR=1.0, 5: SWR=1.3, 10: SWR=1.8, 15: SWR=3.0
}

// SWR
static void rm1answer(uint_fast8_t arg)
{
	//const uint_fast8_t pathi = 0;	// A or B path 
	// SWR-meter report
	static const FLASHMEM char fmt_1 [] =
		"RM"			// 2 characters - status information code
		"1"				// 1 char - 1 - SWR
		"%04d"				// 4 chars - 0000 ~ 0030: Meter value in dots
		";";				// 1 char - line terminator

	// answer mode
	const uint_fast8_t len = local_snprintf_P(cat_ask_buffer, CAT_ASKBUFF_SIZE, fmt_1,
		(int) kenwoodswrmeter()
		);
	cat_answer(len);
}

// COMP
static void rm2answer(uint_fast8_t arg)
{
	//const uint_fast8_t pathi = 0;	// A or B path 
	// COMP report
	static const FLASHMEM char fmt_1 [] =
		"RM"			// 2 characters - status information code
		"2"				// 1 char - 2 - COMP
		"%04d"				// 4 chars - 0000 ~ 0030: Meter value in dots
		";";				// 1 char - line terminator

	// answer mode
	const uint_fast8_t len = local_snprintf_P(cat_ask_buffer, CAT_ASKBUFF_SIZE, fmt_1,
		(int) 0
		);
	cat_answer(len);
}

// ALC
static void rm3answer(uint_fast8_t arg)
{
	//const uint_fast8_t pathi = 0;	// A or B path 
	// COMP report
	static const FLASHMEM char fmt_1 [] =
		"RM"			// 2 characters - status information code
		"3"				// 1 char - 3 - ALC
		"%04d"				// 4 chars - 0000 ~ 0030: Meter value in dots
		";";				// 1 char - line terminator

	// answer mode
	const uint_fast8_t len = local_snprintf_P(cat_ask_buffer, CAT_ASKBUFF_SIZE, fmt_1,
		(int) 0
		);
	cat_answer(len);
}

#endif /* WITHTX && (WITHSWRMTR || WITHSHOWSWRPWR) */
#endif /* WITHCATEXT */

static uint_fast8_t
adjust8(uint_fast8_t v, uint_fast8_t minimal, uint_fast8_t maximal)
{
	if (v > maximal)
		return maximal;
	if (v < minimal)
		return minimal;
	return v;
}

// в трансивере допустимый диапазон изменения значений CW PITCH больше чем поддерживается протоколом CAT KENWOOD
// поэтому перед отдачей приводится к допустимому.
static void ptanswer(uint_fast8_t arg)
{
	static const FLASHMEM char fmt_1 [] =
		"PT"			// 2 characters - status information code
		"%02d"			// P1 2 characters - pitch code
		";";				// 1 char - line terminator

	// answer mode
	const uint_fast8_t len = local_snprintf_P(cat_ask_buffer, CAT_ASKBUFF_SIZE, fmt_1,
		(int) ((adjust8(gcwpitch10, 40, 190) - 40) / 5)
		);

	cat_answer(len);
}
// information request
// Reads status of the transceiver.
static void ifanswer(uint_fast8_t arg)
{
	static const FLASHMEM char fmt_5 [] =
		"IF"			// 2 characters - status information code
		"%011ld"		// P1 11 characters - freq
		"     "			// P2 5 chars - unused
		"+0000"			// P3 5 chars - rit/xit freq
		"0"				// P4 1 char - rit off/on
		"0"				// P5 1 char - xit off/on
		" "				// P6 1 char - not used (hundreds of channel number)
		"00"			// P7 2 char - memory/channel
		"%1d"			// P8 1 char - tx/rx
		"%1d"			// P9 1 char - mode
		"%1d"			// P10 1 char - FR/FT function
		"0"				// P11 1 char - scan off/on
		"%1d"			// P12 1 char - split off/on
		"0"				// P13 1 char - tone off/on
		"00"			// P14 2 char - tone number
		"0"				// 1 char - unused
		";";				// 1 char - line terminator

	const uint_fast8_t len = local_snprintf_P(cat_ask_buffer, CAT_ASKBUFF_SIZE, fmt_5,
		(signed long) gfreqs [getbankindex_tx(gtx)],
		(int) gtx,
		(int) submodes [gsubmode].qcatmodecode,	// P9
		(int) 0,	// P10: FR/FT state
		(int) (gsplitmode == VFOMODES_VFOSPLIT) // P12
		);
	cat_answer(len);
}

static void badcommandanswer(uint_fast8_t arg)
{
	static const char badcommand [] = "?;";	// ответ на команду с неправильным синтаксисом

	cat_answervariable(badcommand, 2);	// после ответа - ждём новую.
}

#if WITHTX

// Вызывается из user mode программы
// сбрасываем запрос на передачу от команд CAT
static void 
cat_reset_ptt(void)	
{
	system_disableIRQ();
	cattunemode = catstatetx = 0;
	system_enableIRQ();
}

static uint_fast8_t
cat_get_signal(uint_fast8_t selector)
{
	switch (selector)
	{
	case BOARD_CATSIG_SER1_RTS: return HARDWARE_CAT_GET_RTS();
	case BOARD_CATSIG_SER1_DTR: return HARDWARE_CAT_GET_DTR();
#if WITHUSBHW && WITHUSBCDCACM && WITHUSBCDCACM_N > 1
	case BOARD_CATSIG_SER2_RTS: return HARDWARE_CAT2_GET_RTS();
	case BOARD_CATSIG_SER2_DTR: return HARDWARE_CAT2_GET_DTR();
#endif /* WITHUSBHW && WITHUSBCDCACM && WITHUSBCDCACM_N > 1 */
	default: return 0;
	}
}

// Вызывается из пользовательской программы
// Получить запрос перехода на передачу от команд CAT или порта управления.
static uint_fast8_t 
cat_get_ptt(void)	
{
	if (catprocenable != 0)
	{
		system_disableIRQ();
		const uint_fast8_t r = cat_get_signal(catsigptt);
		system_enableIRQ();

		return (catstatetx != 0) || r;	// catstatetx - это по текстовым командам
	}
	return 0;
}

#endif /* WITHTX */


// Вызывается из обработчика перерываний
// Получить нажате ключа от порта управления, вызывается из обработчика перерываний
uint_fast8_t cat_get_keydown(void)
{
#if WITHELKEY
	if (catprocenable != 0)
	{
		system_disableIRQ();
		const uint_fast8_t r = cat_get_signal(catsigkey);
		system_enableIRQ();

		return r;
	}
#endif /* WITHELKEY */
	return 0;

}


static void 
NOINLINEAT
cat_answer_request(uint_fast8_t catindex)
{
	if (CAT_MAX_INDEX == catindex)
		return;
	//PRINTF(PSTR("cat_answer_request: catindex=%u\n"), catindex);
	cat_answer_map [catindex] = 1;
}

static void 
processcat_initialize(void)
{
	//catprocenable = catstatetxdata = catstatetx = 0;
	//catstatein = CATSTATE_HALTED;
	//catstateout = CATSTATEO_HALTED;
	//morsefill = 0;	/* индекс буфера, заполняемого в данный момент. Противоположный передаётся. */
	//sendmorsepos [0] = sendmorsepos [1] = inpmorselength [0] = inpmorselength [1] = 0;
}

/* эта операция вызывается неоднократно - не должна мешать работе при уже разрешённом CAT */
static void processcat_enable(uint_fast8_t enable)
{
	if (catprocenable == enable)
		return;
	catprocenable = enable;
	if (! catprocenable)
	{
		system_disableIRQ();
		HARDWARE_CAT_ENABLERX(0);
		HARDWARE_CAT_ENABLETX(0);
		catstatein = CATSTATE_HALTED;
		catstateout = CATSTATEO_HALTED;
		system_enableIRQ();
	}
	else
	{
		uint_fast8_t i;
		for (i = 0; i < (sizeof cat_answer_map / sizeof cat_answer_map [0]); ++ i)
			cat_answer_map [i] = 0;

#if WITHTX
		sendmorsepos  [0] = 
		inpmorselength [0]= 
		sendmorsepos  [1] = 
		inpmorselength [1] = 0;
#endif /* WITHTX */

		aistate = 0; /* Power-up state of AI mode = 0 (TS-590). */
		system_disableIRQ();
		catstatetxdata = 0;
		cattunemode = catstatetx = 0;
		HARDWARE_CAT_ENABLERX(1);
		catstatein = CATSTATE_WAITCOMMAND1;
		catstateout = CATSTATEO_SENDREADY;
		system_enableIRQ();
	}
}

/* вызывается из USER-MODE программы для отправки ответов на CAT-запросы */

typedef void (* canapfn)(uint_fast8_t arg);

static const canapfn catanswers [CAT_MAX_INDEX] =
{
#if WITHCATEXT
#if WITHELKEY
	kyanswer,	// обрабатыаем первой для ускорения реакции,
	ksanswer,
#endif /* WITHELKEY */
	psanswer,
	smanswer,
	raanswer,
	paanswer,
#if WITHANTSELECT || WITHANTSELECTRX || WITHANTSELECT2
	ananswer,
#endif /* WITHANTSELECT || WITHANTSELECTRX || WITHANTSELECT2 */
#if WITHTX && (WITHSWRMTR || WITHSHOWSWRPWR)
	rm1answer,
	rm2answer,
	rm3answer,
#endif /* WITHTX && (WITHSWRMTR || WITHSHOWSWRPWR) */
#endif /* WITHCATEXT */
	idanswer,				
	fvanswer,
	daanswer,
	faanswer,
	fbanswer,
	spanswer,
	franswer,
	ftanswer,
	mdanswer,
	txanswer,
	rxanswer,
	aianswer,
	ptanswer,
	ifanswer,
	fwanswer,
#if WITHIF4DSP
	zyanswer,
	zzanswer,
#endif /* WITHIF4DSP */
#if WITHIF4DSP
	rganswer,
	aganswer,
	sqanswer,
#endif /* WITHIF4DSP */
#if WITHPOWERTRIM
	pcanswer,
#endif /* WITHPOWERTRIM */
#if WITHIF4DSP
	nranswer,
#endif /* WITHIF4DSP */
	badcommandanswer,
};

static void 
cat_answer_forming(void)
{
	static uint_fast8_t ilast;
	uint_fast8_t original;
	original = ilast;
	for (;;)
	{
		const uint_fast8_t i = ilast;
		ilast = calc_next(i, 0, (sizeof cat_answer_map / sizeof cat_answer_map [0]) - 1);
		system_disableIRQ();
		if (cat_answer_map [i] != 0)
		{
			const uint_fast8_t answerparam = cat_answerparam_map [i];
			cat_answer_map [i] = 0;
			system_enableIRQ();
			(* catanswers [i])(answerparam);
			return;
		}
		system_enableIRQ();
		if (ilast == original)
			break;
	}
}

static uint_fast32_t 
catscanint(
	const uint8_t * p,	// массив символов
	uint_fast8_t width	// количество символов
	)
{
	uint_fast32_t v = 0;
	while (width -- && local_isdigit((unsigned char) * p))
	{
		v = v * 10 + * p ++ - '0';
	}
	return v;
}

/* возврат ненуля - была какая-либо команда
	требуется обновление дисплея */
static uint_fast8_t
//NOINLINEAT
processcatmsg(
	uint_fast8_t catcommand1,
	uint_fast8_t catcommand2,
	uint_fast8_t cathasparam,
	uint_fast8_t catpcount,	// количество символов за кодом команды
	const uint8_t * catp	// массив символов
	)
{
	//PRINTF(PSTR("processcatmsg: c1=%02X, c2=%02X, chp=%d, cp=%lu\n"), catcommand1, catcommand2, cathasparam, catparam);
	#define match2(ch1, ch2) (catcommand1 == (ch1) && catcommand2 == (ch2))
	uint_fast8_t rc = 0;
	const uint_fast32_t catparam = catscanint(catp, catpcount);
	
#if WITHCAT
	if (match2('I', 'D'))
	{
		if (cathasparam != 0)
		{
			cat_answer_request(CAT_BADCOMMAND_INDEX);
		}
		else
		{
			cat_answer_request(CAT_ID_INDEX);
		}
	}
	else if (match2('F', 'V'))
	{
		if (cathasparam != 0)
		{
			cat_answer_request(CAT_BADCOMMAND_INDEX);
		}
		else
		{
			cat_answer_request(CAT_FV_INDEX);
		}
	}
	else if (match2('D', 'A'))
	{
		if (cathasparam != 0)
		{
			switch (catparam)
			{
			case 0:
				// DATA mode OFF
				break;
			case 1:
				// DATA mode ON
				break;
			default:
				cat_answer_request(CAT_BADCOMMAND_INDEX);
				break;
			};
		}
		else
		{
			cat_answer_request(CAT_DA_INDEX);
		}
	}
	// parse receieved command
	//
	else if (match2('A', 'I'))
	{
		if (cathasparam != 0)
		{
			switch (catparam)
			{
			case 0:
				aistate = 0;
				break;
			case 2:
				aistate = 1;
				cat_answer_request(CAT_IF_INDEX);	/* следующим ответом пойдёт состоние */
				cat_answer_request(CAT_MD_INDEX);	/* следующим ответом пойдёт состоние */
				break;
			default:
				cat_answer_request(CAT_BADCOMMAND_INDEX);
				break;
			}
		}
		else
		{
			cat_answer_request(CAT_AI_INDEX);
		}
	}
	else if (match2('F', 'A'))
	{
		if (cathasparam != 0)
		{
			const uint_fast8_t bi = getbankindex_ab(0);	/* VFO A bank index */
			const uint_fast32_t v = catparam;
			catchangefreq(vfy32up(v, TUNE_BOTTOM, TUNE_TOP - 1, gfreqs [bi]), gtx);
			updateboard(1, 1);	/* полная перенастройка (как после смены режима) */
			rc = 1;
		}
		else
		{
			cat_answer_request(CAT_FA_INDEX);
		}
	}
	else if (match2('F', 'B'))
	{
		if (cathasparam != 0)
		{
			const uint_fast8_t bi = getbankindex_ab(1);	/* VFO B bank index */
			const uint_fast32_t v = catparam;
			catchangefreq(vfy32up(v, TUNE_BOTTOM, TUNE_TOP - 1, gfreqs [bi]), gtx);
			updateboard(1, 1);	/* полная перенастройка (как после смены режима) */
			rc = 1;
		}
		else
		{
			cat_answer_request(CAT_FB_INDEX);
		}
	}
	else if (match2('P', 'T'))
	{
		if (cathasparam != 0)
		{
			const uint_fast32_t v = catparam;
			// Минимальный тон телеграфа - 400 герц.
			gcwpitch10 = vfy32up(v, 0, 14, 6) * 5 + 40; 
			updateboard(1, 1);	/* полная перенастройка (как после смены режима) */
			rc = 1;
		}
		else
		{
			cat_answer_request(CAT_PT_INDEX);
		}
	}
	else if (match2('M', 'D'))
	{
		if (cathasparam != 0)
		{
			const uint_fast32_t v = catparam;

			const uint_fast8_t bi = getbankindex_tx(gtx);	/* vfo bank index */
			const uint_fast8_t defsubmode = findkenwoodsubmode(v, gsubmode);	/* поиск по кенвудовскому номеру */
			//defsubmode = getdefaultbandsubmode(gfreqs [bi]);		/* режим по-умолчанию для частоты - USB или LSB */
			// todo: не очень хорошо, если locatesubmode не находит режима, она обнуляет row.
			const uint_fast8_t defcol = locatesubmode(defsubmode, & gmoderows [bi]);	/* строка/колонка для SSB. Что делать, если не нашли? */
			putmodecol(gmoderows [bi], defcol, bi);	/* внести новое значение в битовую маску */
			updateboard(1, 1);	/* полная перенастройка (как после смены режима) */

			rc = 1;
		}
		else
		{
			cat_answer_request(CAT_MD_INDEX);
		}
	}
	else if (match2('I', 'F'))
	{
		if (cathasparam)
		{
		}
		else
		{
			cat_answer_request(CAT_IF_INDEX);
		}
	}
#if WITHIF4DSP
	else if (match2('R', 'L'))
	{
		cat_answer_request(CAT_BADCOMMAND_INDEX);
	}
	else if (match2('N', 'R'))
	{
		if (cathasparam != 0)
		{
			const uint_fast32_t p1 = vfy32up(catparam, 0, 2, 0) != 0;	// RN0; NR1; NR2;
			if (gnoisereducts [gmode] != p1)
			{
				gnoisereducts [gmode] = p1;
				updateboard(1, 1);	/* полная перенастройка (как после смены режима) */
				rc = 1;
			}
			rc = 1;
		}
		else
		{
			// NR;
			cat_answer_request(CAT_NR_INDEX);	// nranswer()
		}
	}
#endif /* WITHIF4DSP */
#if WITHSPLITEX
	else if (match2('S', 'P'))
	{
		if (cathasparam != 0)
		{
			if (catpcount == 1)
			{
				catchangesplit(catp [0] == '1', 0);
			}
			else if (catpcount == 3)
			{
				const int_fast8_t sign = 0 - (catp [1] == '1');	// P2: 0: plus direction
				catchangesplit(catp [0] == '1', sign * (catp [2] - '0'));
			}
			else
			{
				cat_answer_request(CAT_BADCOMMAND_INDEX);
			}
			//const uint_fast8_t bi = getbankindex_ab(1);	/* VFO B bank index */
			//const uint_fast32_t v = catparam;
			//catchangefreq(vfy32up(v, TUNE_BOTTOM, TUNE_TOP - 1, gfreqs [bi]), gtx);
			//updateboard(1, 1);	/* полная перенастройка (как после смены режима) */
			rc = 1;
		}
		else
		{
			cat_answer_request(CAT_SP_INDEX);	// spanswer()
		}
	}
	else if (match2('F', 'R'))
	{
		if (cathasparam != 0)
		{
			// gvfosplit: At index 0: RX VFO A or B, at index 1: TX VFO A or B
			switch (catparam)
			{
			case 0:
				// Set VFO A to simplex state
				gvfosplit [0] = 0;
				break;
			case 1:
				// Set VFO B to simplex state
				gvfosplit [0] = 1;
				break;
			case 2:
				// Set VFO B to Memory Channel
				break;
			default:
				cat_answer_request(CAT_BADCOMMAND_INDEX);
				break;
			}

			//const uint_fast8_t bi = getbankindex_ab(1);	/* VFO B bank index */
			//const uint_fast32_t v = catparam;
			//catchangefreq(vfy32up(v, TUNE_BOTTOM, TUNE_TOP - 1, gfreqs [bi]), gtx);
			//updateboard(1, 1);	/* полная перенастройка (как после смены режима) */
			rc = 1;
		}
		else
		{
			cat_answer_request(CAT_FR_INDEX);
		}
	}
	else if (match2('F', 'T'))
	{
		if (cathasparam != 0)
		{
			// gvfosplit: At index 0: RX VFO A or B, at index 1: TX VFO A or B
			switch (catparam)
			{
			case 0:
				// Set VFO A to split state
				gvfosplit [1] = 0;
				break;
			case 1:
				// Set VFO B to split state
				gvfosplit [1] = 1;
				break;
			default:
				cat_answer_request(CAT_BADCOMMAND_INDEX);
				break;
			}

			//const uint_fast8_t bi = getbankindex_ab(1);	/* VFO B bank index */
			//const uint_fast32_t v = catparam;
			//catchangefreq(vfy32up(v, TUNE_BOTTOM, TUNE_TOP - 1, gfreqs [bi]), gtx);
			//updateboard(1, 1);	/* полная перенастройка (как после смены режима) */
			rc = 1;
		}
		else
		{
			cat_answer_request(CAT_FT_INDEX);
		}
	}
#endif /* WITHSPLITEX */
#if WITHCATEXT
#if WITHIF4DSP
	else if (match2('N', 'T'))
	{
		// Sets and reads the Notch Filter status.
		cat_answer_request(CAT_BADCOMMAND_INDEX);
	}
	else if (match2('B', 'P'))
	{
		// Adjusts the Notch Frequency of the Manual Notch Filter.
		cat_answer_request(CAT_BADCOMMAND_INDEX);
	}
	else if (match2('S', 'Q'))
	{
		// Squelch level set/report
		if (cathasparam != 0)
		{
			if (catpcount == 4)
			{
				//const uint_fast32_t p1 = vfy32up(catscanint(catp + 0, 1), 0, 0, 0);
				const uint_fast32_t p2 = vfy32up(catscanint(catp + 1, 3), 0, SQUELCHMAX, 0);
				if (flagne_u8(& gsquelch.value, p2))
				{
					updateboard(1, 1);	/* полная перенастройка (как после смены режима) */
					rc = 1;
				}
			}
			else if (catpcount == 1)
			{
				const uint_fast32_t p1 = vfy32up(catscanint(catp + 0, 1), 0, 0, 0);
				cat_answerparam_map [CAT_SQ_INDEX] = p1;
				cat_answer_request(CAT_SQ_INDEX);	// sqanswer
				rc = 1;
			}
			else
			{
				cat_answer_request(CAT_BADCOMMAND_INDEX);
			}
		}
		else
		{
			cat_answer_request(CAT_BADCOMMAND_INDEX);
		}
	}
	else if (match2('A', 'G'))
	{
		// AF gain level set/report
		if (cathasparam != 0)
		{
			if (catpcount == 4)
			{
				//const uint_fast32_t p1 = vfy32up(catscanint(catp + 0, 1), 0, 0, 0);
				const uint_fast32_t p2 = vfy32up(catscanint(catp + 1, 3), BOARD_AFGAIN_MIN, BOARD_AFGAIN_MAX, BOARD_AFGAIN_MAX);
				if (flagne_u16(& afgain1.value, p2))
				{
					updateboard(1, 1);	/* полная перенастройка (как после смены режима) */
					rc = 1;
				}
			}
			else if (catpcount == 1)
			{
				const uint_fast32_t p1 = vfy32up(catscanint(catp + 0, 1), 0, 0, 0);
				cat_answerparam_map [CAT_AG_INDEX] = p1;
				cat_answer_request(CAT_AG_INDEX);	// aganswer
				//rc = 1;
			}
			else
			{
				cat_answer_request(CAT_BADCOMMAND_INDEX);
			}
		}
		else
		{
			cat_answer_request(CAT_BADCOMMAND_INDEX);
		}
	}
	else if (match2('R', 'G'))
	{
		// RF gain level set/report
		if (cathasparam != 0)
		{
			const uint_fast32_t p2 = vfy32up(catparam, BOARD_IFGAIN_MIN, BOARD_IFGAIN_MAX, BOARD_IFGAIN_MAX);
			if (flagne_u16(& rfgain1.value, p2))
			{
				updateboard(1, 1);	/* полная перенастройка (как после смены режима) */
				rc = 1;
			}
		}
		else
		{
			cat_answer_request(CAT_RG_INDEX);	// rganswer
		}
	}
#if WITHPOWERTRIM
	else if (match2('P', 'C'))
	{
		// Sets and reads the output power
		if (cathasparam != 0)
		{
			const uint_fast32_t p2 = vfy32up(catparam, 5, 100, 100);
			// Нормирование Значений Kenwook CAT к диапазону WITHPOWERTRIMMIN..WITHPOWERTRIMMAX
			if (flagne_u16(& gnormalpower.value, (p2 - 5) * (WITHPOWERTRIMMAX - WITHPOWERTRIMMIN) / 95 + WITHPOWERTRIMMIN))
			{
				updateboard(1, 1);	/* полная перенастройка (как после смены режима) */
				rc = 1;
			}
		}
		else
		{
			cat_answer_request(CAT_PC_INDEX);	// pcanswer
		}
	}
#endif /* WITHPOWERTRIM */
#endif /* WITHIF4DSP */
	else if (match2('R', 'A'))
	{
		// Attenuator status set/query
		if (cathasparam /* && (catparam == 0 || catparam == 1) */)
		{
			gatts [getbankindex_tx(gtx)] = vfy32up(catparam, 0, ATTMODE_COUNT - 1, 0);	/* 0..1 */;	// one step or fully attenuated
			updateboard(1, 0);	/* полная перенастройка (как после смены режима) */
			cat_answer_request(CAT_RA_INDEX);
			rc = 1;
		}
		else
		{
			// read attenuator state
			cat_answer_request(CAT_RA_INDEX);
		}
	}
	else if (match2('P', 'A'))
	{
		// Preamplifier status set/query
		if (cathasparam /* && (catparam == 0 || catparam == 1) */)
		{
#if ! WITHONEATTONEAMP
			gpamps [getbankindex_tx(gtx)] = vfy32up(catparam, 0, PAMPMODE_COUNT - 1, 0);	/* 0..1 */;	// one step or fully attenuated
			updateboard(1, 0);	/* полная перенастройка (как после смены режима) */
#endif /* ! WITHONEATTONEAMP */
			cat_answer_request(CAT_PA_INDEX);
			rc = 1;
		}
		else
		{
			// read preamplifier state
			cat_answer_request(CAT_PA_INDEX);
		}
	}
#if WITHANTSELECT || WITHANTSELECTRX
	else if (match2('A', 'N'))
	{
		// in differencies from documentation, ant1=1, ant2=2
		// antenna selection set/query
		if (cathasparam)
		{
			const uint_fast8_t p1 = catparam / 100;
			if (p1 != 9)
			{
				gantennas [getbankindex_tx(gtx)] = vfy32up(p1, 1, ANTMODE_COUNT, 1) - 1;	/* 0..1 */;	// one step or fully attenuated
				updateboard(1, 0);	/* полная перенастройка (как после смены режима) */
			}
			cat_answer_request(CAT_AN_INDEX);
			rc = 1;
		}
		else
		{
			// read antenna selection
			cat_answer_request(CAT_AN_INDEX);
		}
	}
#endif /* WITHANTSELECT || WITHANTSELECTRX */
	else if (match2('P', 'S'))
	{
		if (cathasparam)
		{
			// set param
			cat_answer_request(CAT_PS_INDEX);
		}
		else
		{
			// read state
			cat_answer_request(CAT_PS_INDEX);
		}
	}
	else if (match2('S', 'M'))
	{
		// Get S-neter information
		if (cathasparam && catparam == 0)
		{
			// parameter a
			cat_answerparam_map [CAT_SM_INDEX] = 0;
			cat_answer_request(CAT_SM_INDEX);
		}
		else if (cathasparam && catparam == 9)
		{
			// parameter a
			cat_answerparam_map [CAT_SM_INDEX] = 9;
			cat_answer_request(CAT_SM_INDEX);
		}
		else
		{
			cat_answer_request(CAT_BADCOMMAND_INDEX);
		}
	}
	else if (match2('U', 'P'))
	{
		if (cathasparam)
		{
			const int steps = vfy32up(catparam, 0, 99, 1);	/* 00 .. 99 */
			encoder_pushback(steps, genc1div);
		}
	}
	else if (match2('D', 'N'))
	{
		if (cathasparam)
		{
			const int steps = vfy32up(catparam, 0, 99, 1);	/* 00 .. 99 */
			encoder_pushback(0 - steps, genc1div);
		}
	}
#if 0
	else if (match2('E', 'X'))
	{
		if (cathasparam)
		{
			switch (cathasparam)
			{
			case 58:
				// EX0058xxx; управление SPLIT
				break;
			}
		}
	}
#endif
#endif /* WITHCATEXT */
#if WITHTX
	else if (match2('T', 'X'))
	{
		if (cathasparam != 0)
		{
			const uint_fast32_t v = catparam;	/* 0 - send, 1 - data send, 2 - TX TUNE */
			switch (v)
			{
			case 0:
				catstatetx = 1;		/* эта переменная сбрасывается и читается и из прерываний */
				break;
			case 1:
				catstatetx = 1;		/* эта переменная сбрасывается и читается и из прерываний */
				catstatetxdata = 1;
				break;
			case 2:
				cattunemode = 1;
				break;
			}

			if (aistate != 0)
				cat_answer_request(CAT_TX_INDEX);	// ignore main/sub rx selection (0 - main. 1 - sub);
		}
		else
		{
			catstatetx = 1;		/* эта переменная сбрасывается и читается и из прерываний */
			catstatetxdata = 0;
			cattunemode = 0;
			if (aistate != 0)
				cat_answer_request(CAT_TX_INDEX);
		}
	}
	else if (match2('R', 'X'))
	{
		if (cathasparam != 0)
		{
			cattunemode = 0;
			catstatetx = 0;		/* эта переменная сбрасывается и читается и из прерываний */
			catstatetxdata = 0;
			if (aistate != 0)
				cat_answer_request(CAT_RX_INDEX);	// POSSIBLE: ignore main/sub rx selection (0 - main. 1 - sub);
		}
		else
		{
			cattunemode = 0;
			catstatetx = 0;		/* эта переменная сбрасывается и читается и из прерываний */
			catstatetxdata = 0;
			if (aistate != 0)
				cat_answer_request(CAT_RX_INDEX);
		}
	}
#if WITHTX && (WITHSWRMTR || WITHSHOWSWRPWR) && WITHCATEXT
	else if (match2('R', 'M'))
	{
		// Get SWR, COMP or ALC- meter information
		if (cathasparam != 0)
		{
			switch (catparam)
			{
			case 1:	// SWR
			case 2: // COMP
			case 3: // ALC
				rmstate = catparam;
				break;
			default:
				cat_answer_request(CAT_BADCOMMAND_INDEX);
				break;
			}
		}
		else
		{
			switch (rmstate)
			{
			default:
			case 1:
				cat_answer_request(CAT_RM1_INDEX);	// SWR
				break;
			case 2:
				cat_answer_request(CAT_RM2_INDEX);	// COMP
				break;
			case 3:
				cat_answer_request(CAT_RM3_INDEX);	// ALC
				break;
			}
		}
	}
#endif /*  WITHTX && WITHSWRMTR && WITHCATEXT */
#endif /*  WITHTX */
	else if (match2('F', 'W'))
	{
		// filter width
		if (cathasparam != 0)
		{
			// Ширина полосы пропускания фильтра на приёме в герцах
			const uint_fast32_t width = vfy32up(catparam, 0, 9999, 3100); 
			const uint_fast8_t i = findfilter(gmode, gfi, width);	/* поиск фильтра, допустимого для данного режима */
			gfi = getsuitablerx(gmode, i); /* при переключении через CAT сохранения в NVRAM не производится */
			updateboard(1, 1);	/* полная перенастройка (как после смены режима) */
		}
		else
		{
			cat_answer_request(CAT_FW_INDEX);
		}
	}
#if WITHCATEXT && WITHELKEY
	else if (match2('K', 'S'))
	{
		// keyer speed
		if (cathasparam != 0)
		{
			// Скорость передачи от 10 до 60 WPM (в TS-590 от 4 до 60).
			const uint_fast32_t p1 = vfy32up(catparam, CWWPMMIN, CWWPMMAX, 20); 
			if (flagne_u8(& elkeywpm.value, p1))
			{
				updateboard(1, 1);	/* полная перенастройка (как после смены режима) */
			}
		}
		else
		{
			cat_answer_request(CAT_KS_INDEX);
		}
	}
#endif	/* WITHCATEXT */
#if WITHIF4DSP//CTLSTYLE_V1D || CTLSTYLE_OLEG4Z_V1 || 1
	else if (match2('Z', 'Y'))
	{
		if (cathasparam != 0)
		{
			if (catpcount == 4)
			{
				// Format: ZYmSLL
				const uint_fast32_t v = catp [0] - '0';
				const uint_fast8_t submode = findkenwoodsubmode(v, UINT8_MAX);	/* поиск по кенвудовскому номеру */
				if (submode == UINT8_MAX)
				{
					cat_answer_request(CAT_BADCOMMAND_INDEX);
				}
				else
				{
					const uint_fast8_t mode = submodes [submode].mode;
					gnoisereducts [mode] = vfy32up(catscanint(catp + 1, 1), 0, 1, 0);
					gnoisereductvl = vfy32up(catscanint(catp + 2, 2), 0, NRLEVELMAX, gnoisereductvl);
					updateboard(1, 1);	/* полная перенастройка (как после смены режима) */
					rc = 1;
				}
			}
			else if (catpcount == 1)
			{
				/* report about NR state for specified submode and global level */
				const uint_fast32_t v = catp [0] - '0';
				cat_answerparam_map [CAT_ZY_INDEX] = v;
				cat_answer_request(CAT_ZY_INDEX);	// zyanswer
			}
			else
			{
				cat_answer_request(CAT_BADCOMMAND_INDEX);
			}
			//const uint_fast8_t bi = getbankindex_ab(1);	/* VFO B bank index */
			//const uint_fast32_t v = catparam;
			//catchangefreq(vfy32up(v, TUNE_BOTTOM, TUNE_TOP - 1, gfreqs [bi]), gtx);
			//updateboard(1, 1);	/* полная перенастройка (как после смены режима) */
		}
		else
		{
			cat_answer_request(CAT_BADCOMMAND_INDEX);
		}
	}
	else if (match2('Z', 'Z'))
	{
		if (cathasparam != 0)
		{
			if (catpcount == 12)
			{
				// Format: ZZmLLLLUUUUSSS
				const uint_fast32_t v = catp [0] - '0';
				const uint_fast8_t submode = findkenwoodsubmode(v, UINT8_MAX);	/* поиск по кенвудовскому номеру */
				if (submode == UINT8_MAX)
				{
					cat_answer_request(CAT_BADCOMMAND_INDEX);
				}
				else
				{
					const FLASHMEM struct modetempl * const pmodet = getmodetempl(submode);
					//ZZmLLLLUUUUSSSS
					const uint_fast8_t bwseti = pmodet->bwsetis [0];	// индекс банка полос пропускания для данного режима на приеме
					const uint_fast8_t pos = bwsetpos [bwseti];
					bwprop_t * const p = bwsetsc [bwseti].prop [pos];
					p->left10_width10 = vfy32up(catscanint(catp + 1, 4), p->limits->left10_width10_low,p->limits->left10_width10_high, p->left10_width10);
					p->right100 = vfy32up(catscanint(catp + 5, 4), p->limits->right100_low, p->limits->right100_high, p->right100);
					if (p->type == BWSET_PAIR)
						p->afresponce = vfy32up(catscanint(catp + 9, 3), AFRESPONCEMIN, AFRESPONCEMAX, p->afresponce);
					updateboard(1, 1);	/* полная перенастройка (как после смены режима) */
					rc = 1;
				}
			}
			else if (catpcount == 1)
			{
				const uint_fast32_t v = catp [0] - '0';
				cat_answerparam_map [CAT_ZZ_INDEX] = v;
				cat_answer_request(CAT_ZZ_INDEX);	// zzanswer
			}
			else
			{
				cat_answer_request(CAT_BADCOMMAND_INDEX);
			}
			//const uint_fast8_t bi = getbankindex_ab(1);	/* VFO B bank index */
			//const uint_fast32_t v = catparam;
			//catchangefreq(vfy32up(v, TUNE_BOTTOM, TUNE_TOP - 1, gfreqs [bi]), gtx);
			//updateboard(1, 1);	/* полная перенастройка (как после смены режима) */
		}
		else
		{
			cat_answer_request(CAT_BADCOMMAND_INDEX);
		}
	}
#endif /* CTLSTYLE_V1D || CTLSTYLE_OLEG4Z_V1 */
	else
	{
		// нераспознанная команда - ожидание следующей.
#if 0
		// печать информации о принятой команде
		uint_fast16_t y;
		uint_fast16_t x;
		x = display_wrdata_begin(0, 1, & y);
		x = display_wrdata_fast(x, y, catcommand1 & 0x7f);
		x = display_wrdata_fast(x, y, catcommand2 & 0x7f);
		display_wrdata_end();

		if (cathasparam)
			display_menu_digit(catparam, 7, 0, 0);
		else
		{
			display_wrdata_begin();
			display_wrdata_fast(';');
			display_wrdata_end();
		}
		//local_delay_ms(500);
#endif
		cat_answer_request(CAT_BADCOMMAND_INDEX);
	}

#endif // WITHCAT
	return rc;
	#undef match2
}

#endif	/* WITHCAT */


#if WITHELKEY


#if WITHBEACON

static uint_fast8_t beacon_state;
static uint_fast8_t beacon_pause;
static uint_fast8_t beacon_index;
static char beacon_text [] = "V";

enum
{
	BEACONST_INITIALIZE,
	BEACON_PAUSE,
	BEACON_TEXT,
	//
	BEACON_COUNT
};

static void beacon_initialize(void)
{
	beacon_state = BEACONST_INITIALIZE;
}

static void beacon_states(void)
{
	switch (beacon_state)
	{
	case BEACONST_INITIALIZE:
		break;
	case BEACON_PAUSE:
		break;
	case BEACON_TEXT:
		break;
	}
}

// вызывается из user mode
uint_fast8_t beacon_get_ptt(void)
{
	return beacon_text [beacon_index] != '\0';
}


#endif /* WITHBEACON */

/* вызывается из обработчика прерываний */
/* Получить следующий символ для передачи (только верхний регистр) */
static char beacon_getnextcw(void)
{
#if WITHBEACON
	char c = beacon_text [beacon_index];

	if (c == '\0')
		beacon_index = 0;
	else
		++ beacon_index;

	return c;
#else /* WITHBEACON */
	return '\0';
#endif /* WITHBEACON */
}

// Получение символа для передачи (только верхний регистр)
uint_fast8_t elkey_getnextcw(void)
{
#if WITHCAT && WITHCATEXT
	const uint_fast8_t chcat = cat_getnextcw();
	const uint_fast8_t ch = (chcat != '\0') ? chcat : beacon_getnextcw();
	return ch;
#else
	return beacon_getnextcw();
#endif /* WITHCAT && WITHCATEXT*/
}

#endif /* WITHELKEY */

static void dpc_1stimer(void * arg)
{
#if WITHWAVPLAYER || WITHSENDWAV
		if (gloopmsg > 0)
		{
			if (++ loopticks >= gloopsec)
			{
				loopticks = 0;
				playhandler(gloopmsg);
			}
		}
#endif /* WITHWAVPLAYER || WITHSENDWAV */
#if WITHLCDBACKLIGHT || WITHKBDBACKLIGHT
		if (gdimmtime == 0)
		{
			// Функция выключена
			if (dimmflag != 0)
			{
				dimmflag = 0;
				dimmflagch = 1;		// запрос на обновление состояния аппаратуры из user mode программы
			}
			dimmcount = 0;
		}
		else if (dimmflag == 0)		// ещё не выключили
		{
			if (++ dimmcount >= gdimmtime)
			{
				dimmflag = 1;
				dimmflagch = 1;		// запрос на обновление состояния аппаратуры из user mode программы
			}
		}
#endif /* WITHLCDBACKLIGHT || WITHKBDBACKLIGHT */
#if WITHFANTIMER
		if (gtx != 0 || gfanpatime == 0)
		{
			if (fanpaflag != 0)	
			{
				fanpaflag = 0;
				fanpaflagch = 1;
			}
			fanpacount = 0;		/* счётчик времени неактивности */
		}
		else if (fanpaflag == 0)		// ещё не выключили
		{
			if (++ fanpacount >= gfanpatime)
			{
				fanpaflag = 1;
				fanpaflagch = 1;		// запрос на обновление состояния аппаратуры из user mode программы
			}
		}
#endif /* WITHFANTIMER */
#if WITHSLEEPTIMER
		if (gsleeptime == 0)
		{
			// Функция выключена
			if (sleepflag != 0)
			{
				sleepflag = 0;
				sleepflagch = 1;		// запрос на обновление состояния аппаратуры из user mode программы
			}
			sleepcount = 0;
		}
		else if (sleepflag == 0)		// ещё не выключили
		{
			if (++ sleepcount >= gsleeptime * 60)
			{
				sleepflag = 1;
				sleepflagch = 1;		// запрос на обновление состояния аппаратуры из user mode программы
			}
		}
#endif /* WITHSLEEPTIMER */

#if WITHLWIP
//	sys_check_timeouts();
#endif /* WITHLWIP */

#if 0 && CPUSTYLE_XC7Z || CPUSTYLE_XCZU
	hamradio_set_freq(hamradio_get_freq_rx() + 1);
#endif /* CPUSTYLE_XC7Z || CPUSTYLE_XCZU */

#if WITHCPUTEMPERATURE && 0
	uint8_t c = GET_CPU_TEMPERATURE();
	PRINTF(PSTR("CPU temp: %dC\n"), c);
#endif
}

static void
poke_uintptr(volatile uint8_t * p, uintptr_t v)
{
	p [0] = (v >> 24) & 0xFF;
	p [1] = (v >> 16) & 0xFF;
	p [2] = (v >> 8) & 0xFF;
	p [3] = (v >> 0) & 0xFF;
}

static uintptr_t
peek_uintptr(volatile const uint8_t * p)
{
	uintptr_t v = 0;

	v = (v << 8) + * p ++;
	v = (v << 8) + * p ++;
	v = (v << 8) + * p ++;
	v = (v << 8) + * p ++;

	return v;
}


void dpclock_initialize(dpclock_t * lp)
{
	SPINLOCK_INITIALIZE(& lp->lock);
	lp->flag = 0;
}
/*
void dpclock_enter(dpclock_t * lp)
{
	global_disableIRQ();
	SPIN_LOCK(& lp->lock);

	SPIN_UNLOCK(& lp->lock);
	global_enableIRQ();
}
*/
void dpclock_exit(dpclock_t * lp)
{
	global_disableIRQ();
	SPIN_LOCK(& lp->lock);
	lp->flag = 0;
	SPIN_UNLOCK(& lp->lock);
	global_enableIRQ();
}

// возврат не-0 если уже занято
uint_fast8_t dpclock_traylock(dpclock_t * lp)
{
	uint_fast8_t v;

	global_disableIRQ();
	SPIN_LOCK(& lp->lock);
	v = lp->flag;
	lp->flag = 1;
	SPIN_UNLOCK(& lp->lock);
	global_enableIRQ();

	return v;
}


/* обработка сообщений от уровня обработчиков прерываний к user-level функциям. */
static void
//NOINLINEAT
processmessages(
	uint_fast8_t * kbch,
	uint_fast8_t * kbready,
	uint_fast8_t inmenu,
	const FLASHMEM struct menudef * mp
	)
{
	if (hardware_getshutdown())	// признак провала питания
	{
		display_uninitialize();	// выключаем дисплей
		gtx = 0;
		updateboard(1, 1);	// переходим на приём
		for (;;)				// вешаемся...
			;
	}


	uint8_t * buff;

	* kbready = 0;
	* kbch = KBD_CODE_MAX;

	switch (takemsgready_user(& buff))
	{
	case MSGT_EMPTY:
#if WITHINTEGRATEDDSP
		audioproc_spool_user();
#endif /* WITHINTEGRATEDDSP */
#if WITHUSEAUDIOREC
		sdcardbgprocess();
#endif /* WITHUSEAUDIOREC */
#if WITHUSBHW
		board_usbh_polling();     // usb device polling
#endif /* WITHUSBHW */
#if WITHWAVPLAYER || WITHSENDWAV
		spoolplayfile();
#endif /* WITHWAVPLAYER || WITHSENDWAV */
#if WITHLWIP
		/* LWIP */
		//usb_polling();     // usb device polling
		//network_spool();
		//stmr();            // call software timers
#endif /* WITHLWIP */
		display2_bgprocess();			/* выполнение шагов state machine отображения дисплея */
		directctlupdate(inmenu, mp);		/* управление скоростью передачи (и другими параметрами) через потенциометр */
#if WITHLCDBACKLIGHT || WITHKBDBACKLIGHT
		// обработать запрос на обновление состояния аппаратуры из user mode программы
		if (dimmflagch != 0)
		{
			dimmflagch = 0;
			display2_bgreset();
			display_redrawfreqmodesbarsnow(inmenu, mp);			/* Обновление дисплея - всё, включая частоту */
			updateboard(1, 0);
		}
#endif /* WITHLCDBACKLIGHT || WITHKBDBACKLIGHT */
#if WITHFANTIMER
		// обработать запрос на обновление состояния аппаратуры из user mode программы
		if (fanpaflagch != 0)
		{
			fanpaflagch = 0;
			updateboard(1, 0);
		}
#endif /* WITHFANTIMER */
#if WITHSLEEPTIMER
		// обработать запрос на обновление состояния аппаратуры из user mode программы
		if (sleepflagch != 0)
		{
			sleepflagch = 0;
			display2_bgreset();
			display_redrawfreqmodesbarsnow(0, NULL);			/* Обновление дисплея - всё, включая частоту */
			updateboard(1, 0);
		}
#endif /* WITHSLEEPTIMER */
#if WITHCAT
		if (cat_getstateout() == CATSTATEO_SENDREADY)
		{
			cat_answer_forming();
		}
#endif /* WITHCAT */
		return;

	case MSGT_CAT:
		board_wakeup();
#if WITHCAT
		{
			// check MSGBUFFERSIZE8 valie
			// 12 bytes as parameter
			//PRINTF(PSTR("processmessages: MSGT_CAT\n"));
			if (processcatmsg(buff [0], buff [1], buff [2], buff [8], buff + 9))
				display_redrawfreqmodesbarsnow(inmenu, mp);			/* Обновление дисплея - всё, включая частоту */
		}
#endif /* WITHCAT */
		break;

	case MSGT_KEYB:
		//PRINTF(PSTR("processmessages: MSGT_KEYB\n"));
		board_wakeup();
		//if (board_wakeup() == 0)
		{
			// первый символ, нажатый в состоянии "сна" или "притушенного дисплея" игнорируется
			* kbch = buff [0];
			* kbready = 1;
		}
		break;

	case MSGT_DPC:
		// Выполнение отложенного вызова user-mode функций
		{
			uintptr_t func;
			void * arg1;
			void * arg2;
			void * arg3;
			dpclock_t * lp;

			ASSERT(MSGBUFFERSIZE8 >= 21);

			func = (uintptr_t) peek_uintptr(buff + 1);
			arg1 = (void *) peek_uintptr(buff + 5);
			arg2 = (void *) peek_uintptr(buff + 9);
			arg3 = (void *) peek_uintptr(buff + 13);
			lp = (dpclock_t *) peek_uintptr(buff + 17);

			dpclock_exit(lp);	// освобождаем перед вызовом - чтобы была возможность самого себя повторно запросить
			switch (buff [0])
			{
			case 1:
				((udpcfn_t) func)(arg1);
				break;
			case 2:
				((udpcfn2_t) func)(arg1, arg2);
				break;
			case 3:
				((udpcfn3_t) func)(arg1, arg2, arg3);
				break;
			}
		}
		break;

	default:
		break;
	}
	releasemsgbuffer_user(buff);
}

// Запрос отложенного вызова user-mode функций
uint_fast8_t board_dpc(dpclock_t * lp, udpcfn_t func, void * arg)
{
	// предотвращение повторного включения в очередь того же запроса
	if (dpclock_traylock(lp))
		return 0;
	uint8_t * buff;
	ASSERT(MSGBUFFERSIZE8 >= 21);
	if (takemsgbufferfree_low(& buff) != 0)
	{
		buff [0] = 1;
		poke_uintptr(buff + 1, (uintptr_t) func);
		poke_uintptr(buff + 5, (uintptr_t) arg);
		poke_uintptr(buff + 17, (uintptr_t) lp);
		placesemsgbuffer_low(MSGT_DPC, buff);
		return 1;
	}
	dpclock_exit(lp);	// освобождаем в случае невозможности получить буфер
	return 0;
}

// Запрос отложенного вызова user-mode функций
uint_fast8_t board_dpc2(dpclock_t * lp, udpcfn2_t func, void * arg1, void * arg2)
{
	// предотвращение повторного включения в очередь того же запроса
	if (dpclock_traylock(lp))
		return 0;
	uint8_t * buff;
	ASSERT(MSGBUFFERSIZE8 >= 21);
	if (takemsgbufferfree_low(& buff) != 0)
	{
		buff [0] = 2;
		poke_uintptr(buff + 1, (uintptr_t) func);
		poke_uintptr(buff + 5, (uintptr_t) arg1);
		poke_uintptr(buff + 9, (uintptr_t) arg2);
		poke_uintptr(buff + 17, (uintptr_t) lp);
		placesemsgbuffer_low(MSGT_DPC, buff);
		return 1;
	}
	dpclock_exit(lp);	// освобождаем в случае невозможности получить буфер
	return 0;
}

// Запрос отложенного вызова user-mode функций
uint_fast8_t board_dpc3(dpclock_t * lp, udpcfn3_t func, void * arg1, void * arg2, void * arg3)
{
	// предотвращение повторного включения в очередь того же запроса
	if (dpclock_traylock(lp))
		return 0;
	uint8_t * buff;
	ASSERT(MSGBUFFERSIZE8 >= 21);
	if (takemsgbufferfree_low(& buff) != 0)
	{
		buff [0] = 3;
		poke_uintptr(buff + 1, (uintptr_t) func);
		poke_uintptr(buff + 5, (uintptr_t) arg1);
		poke_uintptr(buff + 9, (uintptr_t) arg2);
		poke_uintptr(buff + 13, (uintptr_t) arg3);
		poke_uintptr(buff + 17, (uintptr_t) lp);
		placesemsgbuffer_low(MSGT_DPC, buff);
		return 1;
	}
	dpclock_exit(lp);	// освобождаем в случае невозможности получить буфер
	return 0;
}

static dpclock_t dpc_1slock;
/* Вызывается из обработчика прерываний раз в секунду */
static void spool_secound(void * ctx)
{
	(void) ctx;	// приходит NULL

	board_dpc(& dpc_1slock, dpc_1stimer, NULL);	// при работе тестов никто не прокачивает очередь DPC
	//VERIFY(board_dpc(& dpc_1slock, dpc_1stimer, NULL));
}

#if WITHTX

uint_fast16_t get_swr(uint_fast16_t swr_fullscale)
{
	uint_fast16_t swr10; 		// swr10 = 0..30 for swr 1..4
	adcvalholder_t forward, reflected;

	forward = board_getswrmeter_unfiltered(& reflected, swrcalibr);

								// рассчитанное  значение
	if (forward < minforward)
		swr10 = 0;				// SWR=1
	else if (forward <= reflected)
		swr10 = swr_fullscale;		// SWR is infinite
	else
		swr10 = (forward + reflected) * SWRMIN / (forward - reflected) - SWRMIN;
	return swr10;
}
#else
uint_fast16_t get_swr(uint_fast16_t swr_fullscale)
{
	return 0;
}
#endif /* WITHTX */

uint_fast8_t hamradio_get_txdisable(void)
{
#if defined (HARDWARE_GET_TXDISABLE)
	if (HARDWARE_GET_TXDISABLE())
		return 1;
#endif /* defined (HARDWARE_GET_TXDISABLE) */
#if WITHTHERMOLEVEL
	if (gheatprot != 0 && hamradio_get_temperature_value() >= gtempvmax * 10)// Градусы в десятых долях
		return 1;
#endif /* WITHTHERMOLEVEL */
#if (WITHSWRMTR || WITHSHOWSWRPWR)
	if (gswrprot != 0 && getactualdownpower() == 0 && get_swr(40) >= 20)	// SWR >= 3.0
		return 1;
#endif /* (WITHSWRMTR || WITHSHOWSWRPWR) */
	return 0;
}

/* Установка сиквенсору запроса на передачу.	*/
static void
//NOINLINEAT
processtxrequest(void)
{
#if WITHTX
	uint_fast8_t txreq = 0;
	uint_fast8_t tunreq = 0;
	if (moxmode || hardware_get_ptt())	// тангента, педаль
	{
#if WITHCAT	
		cat_reset_ptt();	// снять программный запрос на передачу - "залипший" запрос.
#endif	/* WITHCAT */
		txreq = 1;
	}
#if WITHSENDWAV
	if (isplayfile())
	{
		txreq = 1;
	}
#endif /* WITHSENDWAV */
#if WITHBEACON	
	if (beacon_get_ptt())
	{
		txreq = 1;
	}
#endif	/* WITHCAT */
#if WITHCAT	
	if (cat_get_ptt())
	{
		txreq = 1;
	}
#endif	/* WITHCAT */
#if WITHMODEM
	if (modem_get_ptt())
	{
		txreq = 1;
	}
#endif	/* WITHMODEM */
	if (moxmode)
	{
		txreq = 1;
	}
	if (getactualtune())
	{
		tunreq = 1;
	}

	const uint_fast8_t error = hamradio_get_txdisable();
	if (error)
	{
#if WITHCAT
		cat_reset_ptt();	// снять программный запрос на передачу - "залипший" запрос.
#endif	/* WITHCAT */
		moxmode = 0;
		tunemode = 0;		/* не важно, по какой причине переходил на передачу - выход из режима при настройке */
#if WITHAUTOTUNER
		reqautotune = 0;
#endif /* WITHAUTOTUNER */
	}

	seq_txrequest(! error && tunreq, ! error && (tunreq || txreq));
#endif /* WITHTX */
}

static uint_fast32_t ipow10(uint_fast8_t v)
{
	uint_fast32_t r = 1;
	while (v --)
		r *= 10;
	return r;
}

// При редактировании настроек - показ цифровых значений параметров.
// Или диагностическое сообщение при запуске
static void 
//NOINLINEAT
display_menu_digit(
	uint_fast8_t x, 
	uint_fast8_t y, 
	int_fast32_t value,
	uint_fast8_t width,		// WSIGNFLAG can be added for display '+; or '-'
	uint_fast8_t comma,
	uint_fast8_t rj
	)
{
	uint_fast8_t lowhalf = HALFCOUNT_SMALL - 1;

	colmain_setcolors(MNUVALCOLOR, BGCOLOR);
	do
	{
		display2_menu_value(x, y + lowhalf, value, width, comma, rj, lowhalf);
	} while (lowhalf --);
}

// При редактировании настроек - показ строковых (из FLASHMEM) значений параметров.
// Или диагностическое сообщение при запуске

static void 
display_menu_string_P(
	uint_fast8_t x, 
	uint_fast8_t y, 
	const FLASHMEM  char * text,
	uint_fast8_t width,
	uint_fast8_t filled		// сколько символов сейчас в text
	)
{
	if (width > filled)
	{
		const size_t fill = width - filled;
		char notext [fill + 1];
		memset(notext, ' ', fill);
		notext [fill] = '\0';

		colmain_setcolors(MNUVALCOLOR, BGCOLOR);
		display_at(x + 0, y, notext);
		display_at_P(x + fill, y, text);
	}
	else
	{
		colmain_setcolors(MNUVALCOLOR, BGCOLOR);
		display_at_P(x + 0, y, text);
	}
}

#if WITHMENU

// WSIGNFLAG

#define ITEM_VALUE	(0x01u << 0)	/* пункт меню для редактирования параметра */
#define ITEM_GROUP	(0x01u << 1)	/* пункт меню без изменяемого значения - связан с подменю */

#define ITEM_FILTERU	(0x01u << 2)	/* пункт меню для подстройки частот фильтра ПЧ (высокочастотный скат) */
#define ITEM_FILTERL	(0x01u << 3)	/* пункт меню для подстройки частот фильтра ПЧ (низкочастотный скат) */

#define ITEM_NOINITNVRAM	(0x01u << 4)	/* значение этого пункта не используется при начальной инициализации NVRAM */

#define ITEM_ARRAY_BI	(0x01u << 5)	/* указатель на переменную ссылается на массив. Индекс по bank index от getbankindex_ab_fordisplay(0) */

#if CPUSTYLE_ATMEGA
	#define QLABEL(s) (s)
	#define QLABEL2(s1, s2) (s1)
#else /* CPUSTYLE_ATMEGA */
	#define QLABEL(s) (s), (s)
	#define QLABEL2(s1, s2) (s1), (s2)
#endif /* CPUSTYLE_ATMEGA */

struct menudef
{
#if CPUSTYLE_ATMEGA
	char qlabel [LABELW + 1];		/* текст - название пункта меню */
#else /* CPUSTYLE_ATMEGA */
	char qlabel [LABELW + 1];		/* текст - название пункта меню */
	const char * label;
#endif /* CPUSTYLE_ATMEGA */
	uint8_t qwidth, qcomma, qrj;
	uint8_t qistep;
	uint8_t qspecial;	/* признак к какому меню относится */
	uint16_t qbottom, qupper;	/* ограничения на редактируемое значение (upper - включая) */

	nvramaddress_t qnvram;				/* Если MENUNONVRAM - только меняем в памяти */
	nvramaddress_t (* qnvramoffs)(nvramaddress_t base);	/* Смещение при доступе к NVRAM. Нужно при работе с настройками специфическрми для диапазона например */

	uint_fast16_t * qpval16;			/* переменная, которую подстраиваем - если она 16 бит */
	uint_fast8_t * qpval8;			/* переменная, которую подстраиваем  - если она 8 бит*/
	int_fast32_t (* funcoffs)(void);	/* при отображении и использовании добавляется число отсюда */
};

#include "menu.h"

/* входит ли данный пункт меню в группу разрешенных для показа */
static uint_fast8_t
ismenukind(
	const FLASHMEM struct menudef * mp,
	uint_fast8_t itemmask
	)
{
	return (mp->qspecial & itemmask) != 0;
}

/* пункт меню для подстройки частот фильтра ПЧ (высокочастотный скат) */
static uint_fast8_t
ismenufilterusb(
	const FLASHMEM struct menudef * mp
	)
{
	return ismenukind(mp, ITEM_FILTERU);
}

/* пункт меню для подстройки частот фильтра ПЧ (низкочастотный скат) */
static uint_fast8_t
ismenufilterlsb(
	const FLASHMEM struct menudef * mp
	)
{
	return ismenukind(mp, ITEM_FILTERL);
}

#define MENUROW_COUNT (sizeof menutable / sizeof menutable [0])

/* Загрузка значений из NVRAM в переменные программы.
   Значением по умолчанию является то, на которое
   переменная инициализированна при запуске программы.
   Не восстанавливаем "массивы"
   see also loadsavedstate().
   */
static void 
//NOINLINEAT
loadsettings(void)
{
	uint_fast8_t i;

	for (i = 0; i < MENUROW_COUNT; ++ i)
	{
		const FLASHMEM struct menudef * const mp = & menutable [i];
		if (ismenukind(mp, ITEM_VALUE) && ! ismenukind(mp, ITEM_NOINITNVRAM) && ! ismenukind(mp, ITEM_ARRAY_BI))
		{
			const nvramaddress_t nvram = mp->qnvramoffs(mp->qnvram);
			const uint_fast16_t bottom = mp->qbottom;
			const uint_fast16_t upper = mp->qupper;
			uint_fast16_t * const pv16 =  mp->qpval16;
			uint_fast8_t * const pv8 = mp->qpval8;
			const unsigned valoffset = ismenukind(mp, ITEM_ARRAY_BI) ? getbankindex_ab_fordisplay(0) : 0;

			if (nvram == MENUNONVRAM)
				continue;
			if (pv16 != NULL)
			{
				pv16 [valoffset] = loadvfy16up(nvram, bottom, upper, pv16 [valoffset]);
			}
			else if (pv8 != NULL)
			{
				pv8 [valoffset] = loadvfy8up(nvram, bottom, upper, pv8 [valoffset]);
			}
		}
	}
}

/* Сохранить параметр после редактирования */
static void 
//NOINLINEAT
savemenuvalue(
	const FLASHMEM struct menudef * mp
	)
{
	if (ismenukind(mp, ITEM_VALUE))
	{
		const nvramaddress_t nvram = mp->qnvramoffs(mp->qnvram);
		const uint_fast16_t * const pv16 = mp->qpval16;
		const uint_fast8_t * const pv8 = mp->qpval8;
		const unsigned valoffset = ismenukind(mp, ITEM_ARRAY_BI) ? getbankindex_ab_fordisplay(0) : 0;

		if (nvram == MENUNONVRAM)
			return;
		if (pv16 != NULL)
		{
			save_i16(nvram, pv16 [valoffset]);		/* сохраняем отредактированное значение */
		}
		else if (pv8 != NULL)
		{
			save_i8(nvram, pv8 [valoffset]);		/* сохраняем отредактированное значение */
		}
	}
}



/* Загрузка в NVRAM значениями по умолчанию.
   Значением по умолчанию является то, на которое
   переменная инициализированна при запуске программы.
   */
static void 
defaultsettings(void)
{
	uint_fast8_t i;

	for (i = 0; i < MENUROW_COUNT; ++ i)
	{
		const FLASHMEM struct menudef * const mp = & menutable [i];
		if (! ismenukind(mp, ITEM_NOINITNVRAM))
		{
			savemenuvalue(mp);
		}
	}

#if WITHSPECTRUMWF
	const uint_fast8_t bi = 0;
	for (i = 0; i < HBANDS_COUNT; ++ i)
	{
			save_i8(offsetof(struct nvmap, bands[i].gzoomxpow2), gzoomxpow2 [bi]);	/* уменьшение отображаемого участка спектра */
			save_i8(offsetof(struct nvmap, bands[i].gtopdbspe), gtopdbspe [bi]);	/* нижний предел FFT */
			save_i8(offsetof(struct nvmap, bands[i].gbottomdbspe), gbottomdbspe [bi]);	/* верхний предел FFT */
			save_i8(offsetof(struct nvmap, bands[i].gtopdbwfl), gtopdbwfl [bi]);	/* нижний предел FFT waterflow */
			save_i8(offsetof(struct nvmap, bands[i].gbottomdbwfl), gbottomdbwfl [bi]);	/* верхний предел FFT waterflow */
	}
#endif /* WITHSPECTRUMWF */
}

//+++ menu support

#if ! WITHFLATMENU
// Вызывается из display2.c
// Отображение многострочного меню для больших экранов (группы)
void display2_multilinemenu_block_groups(uint_fast8_t x, uint_fast8_t y, dctx_t * pctx)
{
	if (pctx == NULL || pctx->type != DCTX_MENU)
		return;
	const FLASHMEM struct menudef * const mp = pctx->pv;
	const uint_fast16_t index = (int) (mp - menutable);
	uint_fast16_t y_position_groups = y;
	uint_fast16_t index_groups = 0;
	uint_fast16_t selected_group_left_margin; // первый элемент группы
	uint_fast16_t el;
	multimenuwnd_t window;

	display2_getmultimenu(& window);

	//ищем границы текущей группы параметров
	uint_fast16_t selected_group_finder = index;
	while (selected_group_finder > 0 && ! ismenukind(& menutable [selected_group_finder], ITEM_GROUP))
		selected_group_finder --;
	selected_group_left_margin = selected_group_finder;

	// предварительно расчитываем скролл
	uint_fast16_t selected_group_index = 0;
	uint_fast16_t selected_params_index = 0;
	for (el = 0; el < MENUROW_COUNT; el ++)
	{
		const FLASHMEM struct menudef * const mv = & menutable [el];
		if (ismenukind(mv, ITEM_GROUP))
		{
			index_groups ++;
			if (el == selected_group_left_margin)
				selected_group_index = index_groups - 1;
		}
	}
	index_groups = 0;
	const uint_fast16_t menu_block_scroll_offset_groups = window.multilinemenu_max_rows * (selected_group_index / window.multilinemenu_max_rows);

	// выводим на экран блок с параметрами
	for (el = 0; el < MENUROW_COUNT; el ++)
	{
		const FLASHMEM struct menudef * const mv = & menutable [el];
		if (ismenukind(mv, ITEM_GROUP))
		{
			index_groups ++;
			if (index_groups <= menu_block_scroll_offset_groups)
				continue; //пропускаем пункты для скролла
			if ((index_groups - menu_block_scroll_offset_groups) > window.multilinemenu_max_rows)
				continue;
			if (el == selected_group_left_margin)
			{
				//подсвечиваем выбранный элемент
				colmain_setcolors(MENUSELCOLOR, BGCOLOR);
				display_at_P(x - 1, y_position_groups, PSTR(">"));
			}
			else
			{
				//снять отметку
				colmain_setcolors(MENUSELCOLOR, BGCOLOR);
				display_at_P(x - 1, y_position_groups, PSTR(" "));
			}

			dctx_t dctx;
			dctx.type = DCTX_MENU;
			dctx.pv = mv;
			display2_menu_group(x, y_position_groups, & dctx); // название группы

			y_position_groups += window.ystep;
		}
	}

	//стираем ненужные имена групп, оставшиеся от предыдущей страницы
	char nolabel [1 + LABELW + 1];
	memset(nolabel, ' ', sizeof nolabel - 1);
	nolabel [sizeof nolabel - 1] = '\0';

	colmain_setcolors(COLORMAIN_WHITE, BGCOLOR);
	for (;
			index_groups - menu_block_scroll_offset_groups < window.multilinemenu_max_rows;
			++ index_groups, y_position_groups += window.ystep)
	{
		display_at(x - 1, y_position_groups, nolabel);
	}
}

// Отображение многострочного меню для больших экранов (параметры)
void display2_multilinemenu_block_params(uint_fast8_t x, uint_fast8_t y, dctx_t * pctx)
{
	if (pctx == NULL || pctx->type != DCTX_MENU)
		return;
	const FLASHMEM struct menudef * const mp = pctx->pv;
	const uint_fast16_t index = (int) (mp - menutable);
	uint_fast16_t y_position_params = y;
	uint_fast16_t index_params = 0;
	uint_fast16_t selected_group_left_margin; // первый элемент группы
	uint_fast16_t selected_group_right_margin; // последний элемент группы
	uint_fast16_t el;
	multimenuwnd_t window;

	display2_getmultimenu(& window);

	// ищем границы текущей группы параметров
	uint_fast16_t selected_group_finder = index;
	while (selected_group_finder > 0 && ! ismenukind(& menutable [selected_group_finder], ITEM_GROUP))
		selected_group_finder --;
	selected_group_left_margin = selected_group_finder;
	selected_group_finder ++;
	while (selected_group_finder < MENUROW_COUNT && ! ismenukind(& menutable [selected_group_finder], ITEM_GROUP))
		selected_group_finder ++;
	selected_group_right_margin = selected_group_finder - 1;	// последний элмент в списке параметров данной группы

	// предварительно расчитываем скролл
	uint_fast16_t selected_params_index = 0;
	for (el = 0; el < MENUROW_COUNT; el ++)
	{
		const FLASHMEM struct menudef * const mv = & menutable [el];
		if (ismenukind(mv, ITEM_VALUE))
		{
			if (el < selected_group_left_margin || el > selected_group_right_margin)
				continue;
			index_params ++;
			if (el == index)
				selected_params_index = index_params - 1;
		}
	}
	index_params = 0;
	const uint_fast16_t menu_block_scroll_offset_params = window.multilinemenu_max_rows * (selected_params_index / window.multilinemenu_max_rows);

	// выводим на экран блок с параметрами
	for (el = 0; el < MENUROW_COUNT; el ++)
	{
		const FLASHMEM struct menudef * const mv = & menutable [el];
		if (ismenukind(mv, ITEM_VALUE))
		{
			if (el < selected_group_left_margin)
				continue;
			if (el > selected_group_right_margin)
				continue;
			index_params ++;
			if (index_params <= menu_block_scroll_offset_params)
				continue; //пропускаем пункты для скролла
			if ((index_params - menu_block_scroll_offset_params) > window.multilinemenu_max_rows)
				continue;
			if (el == index)
			{
				//подсвечиваем выбранный элемент
				colmain_setcolors(MENUSELCOLOR, BGCOLOR);
				display_at_P(x - 1, y_position_params, PSTR(">"));
			}
			else
			{
				//снять подсветку
				colmain_setcolors(MENUSELCOLOR, BGCOLOR);
				display_at_P(x - 1, y_position_params, PSTR(" "));
			}
			dctx_t dctx;
			dctx.type = DCTX_MENU;
			dctx.pv = mv;
			display2_menu_lblng(x, y_position_params, & dctx); // название редактируемого параметра
			y_position_params += window.ystep;
		}
	}

	//стираем ненужные имена параметров, оставшиеся от предыдущей страницы
	char nolabel [1 + LABELW + 1];
	memset(nolabel, ' ', sizeof nolabel - 1);
	nolabel [sizeof nolabel - 1] = '\0';

	colmain_setcolors(COLORMAIN_WHITE, BGCOLOR);
	for (;
			index_params - menu_block_scroll_offset_params < window.multilinemenu_max_rows;
			++ index_params, y_position_params += window.ystep)
	{
		display_at(x - 1, y_position_params, nolabel);
	}
}

// Отображение многострочного меню для больших экранов (значения)
void display2_multilinemenu_block_vals(uint_fast8_t x, uint_fast8_t y, dctx_t * pctx)
{
	if (pctx == NULL || pctx->type != DCTX_MENU)
		return;
	const FLASHMEM struct menudef * const mp = pctx->pv;
	const uint_fast16_t index = (int) (mp - menutable);
	uint_fast16_t y_position_params = y;
	uint_fast16_t index_params = 0;
	uint_fast16_t selected_group_left_margin; // первый элемент группы
	uint_fast16_t selected_group_right_margin; // последний элемент группы
	uint_fast16_t el;
	multimenuwnd_t window;

	display2_getmultimenu(& window);

	//ищем границы текущей группы параметров
	uint_fast16_t selected_group_finder = index;
	while (selected_group_finder > 0 && ! ismenukind(& menutable [selected_group_finder], ITEM_GROUP))
		selected_group_finder --;
	selected_group_left_margin = selected_group_finder;
	selected_group_finder ++;
	while (selected_group_finder < MENUROW_COUNT && ! ismenukind(& menutable [selected_group_finder], ITEM_GROUP))
		selected_group_finder ++;
	selected_group_right_margin = selected_group_finder - 1;	// последний элмент в списке параметров данной группы

	// предварительно расчитываем скролл
	uint_fast16_t selected_params_index = 0;
	for (el = 0; el < MENUROW_COUNT; el ++)
	{
		const FLASHMEM struct menudef * const mv = & menutable [el];
		if (ismenukind(mv, ITEM_VALUE))
		{
			if (el < selected_group_left_margin || el > selected_group_right_margin)
				continue;
			index_params ++;
			if (el == index)
				selected_params_index = index_params - 1;
		}
	}
	index_params = 0;
	const uint_fast16_t menu_block_scroll_offset_params = window.multilinemenu_max_rows * (selected_params_index / window.multilinemenu_max_rows);

	// выводим на экран блок с параметрами
	for (el = 0; el < MENUROW_COUNT; el ++)
	{
		const FLASHMEM struct menudef * const mv = & menutable [el];
		if (ismenukind(mv, ITEM_VALUE))
		{
			if (el < selected_group_left_margin)
				continue;
			if (el > selected_group_right_margin)
				continue;
			index_params ++;
			if (index_params <= menu_block_scroll_offset_params)
				continue; //пропускаем пункты для скролла
			if ((index_params - menu_block_scroll_offset_params) > window.multilinemenu_max_rows)
				continue;
            dctx_t dctx;
            dctx.type = DCTX_MENU;
            dctx.pv = mv;
            display2_menu_valxx(x, y_position_params, & dctx); // значение параметра
			y_position_params += window.ystep;
		}
	}

	/* параметры полей вывода значений в меню */
	const uint_fast8_t VALUEW = window.valuew;

	//стираем ненужные значения параметров, оставшиеся от предыдущей страницы
	char nolabel [VALUEW + 1];
	memset(nolabel, ' ', VALUEW);
	nolabel [VALUEW] = '\0';

	colmain_setcolors(COLORMAIN_WHITE, BGCOLOR);
	for (;
			index_params - menu_block_scroll_offset_params < window.multilinemenu_max_rows;
			++ index_params, y_position_params += window.ystep)
	{
		//display_menu_string_P(x, y_position_params, nolabel, VALUEW, VALUEW);
		display_at(x, y_position_params, nolabel);
	}
}
#endif /* ! WITHFLATMENU */

// Вызывается из display2.c
// код редактируемого параметра
void display2_menu_lblc3(
	uint_fast8_t x, 
	uint_fast8_t y, 
	dctx_t * pctx
	)
{
	if (pctx == NULL || pctx->type != DCTX_MENU)
		return;
	const FLASHMEM struct menudef * const mp = pctx->pv;
	char buff [4];
	const uint_fast8_t index = (int) (mp - menutable);
	if (ismenukind(mp, ITEM_GROUP))
	{
		colmain_setcolors(MENUCOLOR, BGCOLOR);
		display_at_P(x, y, PSTR("---"));
		return;
	}

	local_snprintf_P(buff, sizeof buff / sizeof buff [0], index >= 100 ? PSTR("%03d") : PSTR("F%02d"), index);

	colmain_setcolors(MENUCOLOR, BGCOLOR);
	display_at(x + 0, y, buff);
}

// Вызывается из display2.c
// название редактируемого параметра
// если группа - ничего не отображаем
void display2_menu_lblng(
	uint_fast8_t x, 
	uint_fast8_t y, 
	dctx_t * pctx
	)
{
	if (pctx == NULL || pctx->type != DCTX_MENU)
		return;
	const FLASHMEM struct menudef * const mp = pctx->pv;
	if (ismenukind(mp, ITEM_VALUE) == 0)
		return;
	colmain_setcolors(MENUCOLOR, BGCOLOR);
	display_at_P(x, y, mp->qlabel);
}

// Вызывается из display2.c
// название редактируемого параметра или группы
void display2_menu_lblst(
	uint_fast8_t x, 
	uint_fast8_t y, 
	dctx_t * pctx
	)
{
	if (pctx == NULL || pctx->type != DCTX_MENU)
		return;
	const FLASHMEM struct menudef * const mp = pctx->pv;
	colmain_setcolors(MENUCOLOR, BGCOLOR);
	display_at_P(x, y, mp->qlabel);
}

// Вызывается из display2.c
// группа, в которой находится редактируемый параметр
void display2_menu_group(
	uint_fast8_t x, 
	uint_fast8_t y, 
	dctx_t * pctx
	)
{
	if (pctx == NULL || pctx->type != DCTX_MENU)
		return;
	const FLASHMEM struct menudef * mp = pctx->pv;

	while (ismenukind(mp, ITEM_GROUP) == 0)
		-- mp;
	colmain_setcolors(MENUGROUPCOLOR, BGCOLOR);
	display_at_P(x, y, mp->qlabel);
}


// Вызывается из display2.c
// значение параметра
void display2_menu_valxx(
	uint_fast8_t x, 
	uint_fast8_t y, 
	dctx_t * pctx
	)
{
	if (pctx == NULL || pctx->type != DCTX_MENU)
		return;
	const FLASHMEM struct menudef * const mp = pctx->pv;
	if (ismenukind(mp, ITEM_VALUE) == 0)
		return;
	multimenuwnd_t window;

	display2_getmultimenu(& window);

	/* параметры полей вывода значений в меню */
	const uint_fast8_t VALUEW = window.valuew;

	int_fast32_t value;
	const uint_fast8_t rj = mp->qrj;
	uint_fast8_t width = mp->qwidth;
	uint_fast8_t comma = mp->qcomma;
	const uint_fast16_t * const pv16 = mp->qpval16;
	const uint_fast8_t * const pv8 = mp->qpval8;
	const unsigned valoffset = ismenukind(mp, ITEM_ARRAY_BI) ? getbankindex_ab_fordisplay(0) : 0;

	// получение значения для отображения
	if (ismenufilterlsb(mp))
	{
		const filter_t * const filter = CONTAINING_RECORD(pv16, filter_t, low_or_center);
		value = getlo4baseflt(filter) + pv16 [valoffset];
	}
	else if (ismenufilterusb(mp))
	{
		const filter_t * const filter = CONTAINING_RECORD(pv16, filter_t, high);
		value = getlo4baseflt(filter) + pv16 [valoffset];
	}
	else if (pv16 != NULL)
	{
		const int_fast32_t offs = mp->funcoffs();
		value = offs + pv16 [valoffset];
	}
	else if (pv8 != NULL)
	{
		const int_fast32_t offs = mp->funcoffs();
		value = offs + pv8 [valoffset];
	}
	else
	{
		value = mp->qbottom;	/* чтобы не ругался компилятор */
	}

	// отображение параметра, отличающиеся от цифрового
	switch (rj)
	{
#if WITHTX && WITHIF4DSP
	case RJ_TXAUDIO:
		{
			static const FLASHMEM char msg [] [6] = 
			{
 				"MIKE ",	// BOARD_TXAUDIO_MIKE
#if WITHAFCODEC1HAVELINEINLEVEL	/* кодек имеет управление усилением с линейного входа */
 				"LINE ",	// BOARD_TXAUDIO_LINE
#endif /* WITHAFCODEC1HAVELINEINLEVEL */
#if WITHUSBUACOUT
				"USB  ",	// BOARD_TXAUDIO_USB
#endif /* WITHUSBUACOUT */
				"2TONE",	// BOARD_TXAUDIO_2TONE
				"NOISE",	// BOARD_TXAUDIO_NOISE
				"1TONE",	// BOARD_TXAUDIO_1TONE
				"MUTE ",	// BOARD_TXAUDIO_MUTE
			};

			width = VALUEW;
			display_menu_string_P(x, y, msg [value], width, comma);
		}
		break;
#endif /* WITHTX && WITHIF4DSP */

#if WITHMODEM

	case RJ_MDMSPEED:
		width = VALUEW;
		display_menu_digit(x, y, modembr2int100 [value], width, comma, 0);
		break;

	case RJ_MDMMODE:
		{
			static const FLASHMEM char msg [] [5] = 
			{
 				"BPSK",
				"QPSK",
			};

			width = VALUEW;
			comma = 4;
			display_menu_string_P(x, y, msg [value], width, comma);
		}
		break;

#endif /* WITHMODEM */

#if defined (RTC1_TYPE)
	case RJ_MONTH:
		{
			static const FLASHMEM char months [13] [4] = 
			{
				"JAN",
				"FEB",
				"MAR",
				"APR",
				"MAY",
				"JUN",
				"JUL",
				"AUG",
				"SEP",
				"OCT",
				"NOV",
				"DEC",
			};

			width = VALUEW;
			comma = 3;
			display_menu_string_P(x, y, months [value - mp->qbottom], width, comma);
		}
		break;
#endif /* defined (RTC1_TYPE) */

	case RJ_YES:
		{
			static const FLASHMEM char msg_yes [] = "YES";
			static const FLASHMEM char msg_no  [] = " NO";

			width = VALUEW;
			comma = 3;
			display_menu_string_P(x, y, value ? msg_yes : msg_no, width, comma);
		}
		break;

	case RJ_SMETER:
		{
			static const FLASHMEM char msg_dial [] = "DIAL";
			static const FLASHMEM char msg_bars [] = "BARS";

			width = VALUEW;
			comma = 4;
			display_menu_string_P(x, y, value ? msg_dial : msg_bars, width, comma);
		}
		break;

#if WITHNOTCHFREQ || WITHNOTCHONOFF
	case RJ_NOTCH:
		{
			width = VALUEW;
			comma = 4;
			switch (notchmodes [value].code)
			{
			default:
			case BOARD_NOTCH_OFF:
				display_menu_string_P(x, y, PSTR("OFF "), width, comma);
				break;
			case BOARD_NOTCH_MANUAL:
				display_menu_string_P(x, y, PSTR("FREQ"), width, comma);
				break;
			case BOARD_NOTCH_AUTO:
				display_menu_string_P(x, y, PSTR("AUTO"), width, comma);
				break;
			}
		}
		break;
#endif /* WITHNOTCHFREQ || WITHNOTCHONOFF */

	case RJ_ON:
		{
			static const FLASHMEM char msg_on  [] = " ON";
			static const FLASHMEM char msg_off [] = "OFF";

			width = VALUEW;
			comma = 3;
			display_menu_string_P(x, y, value ? msg_on : msg_off, width, comma);
		}
		break;

	case RJ_ENCRES:
		width = comma ? VALUEW - 1 : VALUEW;
		display_menu_digit(x, y, encresols [value] * ENCRESSCALE, width, comma, 0);
		break;

#if WITHCAT
	case RJ_CATSPEED:
		width = comma ? VALUEW - 1 : VALUEW;
		display_menu_digit(x, y, catbr2int [value] * BRSCALE, width, comma, 0);
		break;

	case RJ_CATSIG:
	{
		comma = 8;
		width = VALUEW;
		display_menu_string_P(x, y, catsiglabels [value], width, comma);
	}
	break;
#endif /* WITHCAT */

#if WITHSUBTONES && WITHTX
	case RJ_SUBTONE:
		width = comma ? VALUEW - 1 : VALUEW;
		display_menu_digit(x, y, gsubtones [value], width, comma, 0);
		break;
#endif /* WITHSUBTONES && WITHTX */

	case RJ_POW2:
		width = comma ? VALUEW - 1 : VALUEW;
		display_menu_digit(x, y, 1UL << value, width, comma, 0);
		break;

#if WITHELKEY
	case RJ_ELKEYMODE:
		{
			/* режим электронного ключа - 0 - ACS, 1 - electronic key, 2 - straight key, 3 - BUG key */
			static const FLASHMEM char msg [][4] = 
			{ 
				"ACS", 	// 
				"ELE", 
				"OFF",
				"BUG",
			};

			width = VALUEW;
			comma = 3;
			display_menu_string_P(x, y, msg [value], width, comma);
		}
		break;
#endif /* WITHELKEY */

#if WITHPOWERLPHP
	case RJ_POWER:	/* отображние мощности HP/LP */
		width = VALUEW;
		comma = 2;
			display_menu_string_P(x, y, pwrmodes [value].label, width, comma);
		break;
#endif /* WITHPOWERLPHP */

	case RJ_CPUTYPE:
		{
			const FLASHMEM char * msg;
#if CPUSTYLE_STM32MP1
			RCC->MP_APB5ENSETR = RCC_MP_APB5ENSETR_BSECEN;
			(void) RCC->MP_APB5ENSETR;
			RCC->MP_APB5LPENSETR = RCC_MP_APB5LPENSETR_BSECLPEN;
			(void) RCC->MP_APB5LPENSETR;

			const unsigned rpn = ((* (volatile uint32_t *) RPN_BASE) & RPN_ID_Msk) >> RPN_ID_Pos;
			switch (rpn)
			{
			case 0x24: 	msg = PSTR("STM32MP153Cx"); break;
			case 0x25: 	msg = PSTR("STM32MP153Ax"); break;
			case 0xA4: 	msg = PSTR("STM32MP153Fx"); break;
			case 0xA5: 	msg = PSTR("STM32MP153Dx"); break;
			case 0x00: 	msg = PSTR("STM32MP157Cx"); break;
			case 0x01: 	msg = PSTR("STM32MP157Ax"); break;
			case 0x80: 	msg = PSTR("STM32MP157Fx"); break;
			case 0x81:	msg = PSTR("STM32MP157Dx"); break;
			default: 	msg = PSTR("STM32MP15xxx"); break;
			}
#elif CPUSTYLE_XC7Z
			msg = PSTR("ZYNQ 7020");
#elif CPUSTYLE_XCZU
			msg = PSTR("ZYNQ USCALE");
#elif CPUSTYLE_R7S721
			msg = PSTR("RENESAS");
#elif CPUSTYPE_T113
			msg = PSTR("Allw T128-S3");
#else
			msg = PSTR("CPUxxx");
#endif
			width = VALUEW;
			comma = strlen_P(msg);
			display_menu_string_P(x, y, msg, width, comma);
		}
		break;

	case RJ_COMPILED:
		{
			static const FLASHMEM char msg [] =
					__DATE__
					//" " __TIME__
					;
			const FLASHMEM char * const p = msg + strlen_P(msg) - ulmin(VALUEW, strlen_P(msg));	// сколько может поместиться в поле отображения
			width = VALUEW;
			comma = strlen_P(p);
			display_menu_string_P(x, y, p, width, comma);
		}
		break;

	case RJ_VIEW:
		{
			/* стиль отображения спектра и панорамы */
			width = VALUEW;
			display_menu_string_P(x, y, view_types [value], width, comma);
		}
		break;

	default:
		if (width & WSIGNFLAG)
			width = (VALUEW - 1) | WSIGNFLAG;
		else
			width = VALUEW;

		if (comma)
			width = width - 1;

		display_menu_digit(x, y, value, width, comma, rj);
		break;

	}
}

// --- menu support

static uint_fast16_t menulooklast(uint_fast16_t menupos)
{
	const FLASHMEM struct menudef * mp;
	do
	{
		mp = & menutable [++ menupos];
	} while (menupos < MENUROW_COUNT && (mp->qspecial & ITEM_VALUE) != 0);
	return menupos - 1;
}

/* работа с параметрами настройки. */
static void 
modifysettings(
	uint_fast16_t firstitem, uint_fast16_t lastitem,	/* диапазон от какого и до какого пункта меню работает */
	uint_fast8_t itemmask,		/* по какому типу пунктов меню проходим */
	nvramaddress_t posnvram,	/* где сохранена текущая позиция */
	uint_fast8_t exitkey,		/* дополнительная клавиша, по которой происходит выход из меню на уровень выше (или KBD_CODE_MAX) */
	uint_fast8_t byname			/* был выполнен прямой вход в меню */
	)
{
	uint_fast16_t menupos = loadvfy8up(posnvram, firstitem, lastitem, firstitem);	/* начальное значение позиции */
	const FLASHMEM struct menudef * mp = & menutable [menupos];
	multimenuwnd_t window;

	display2_getmultimenu(& window);
	/* функция для сохранения работы варианта без групп */
	while (! ismenukind(mp, itemmask))
	{
		/* проход по определённому типу элементов (itemmask) */
		menupos = calc_next(menupos, firstitem, lastitem);
		mp = & menutable [menupos];
	}
#if WITHDEBUG
	PRINTF(PSTR("menu: ")); PRINTF(mp->qlabel); PRINTF(PSTR("\n"));
#endif /* WITHDEBUG */
	display2_redrawbarstimed(1, 1, mp);
	encoder_clear();

	for (;;)
	{
		int nrotate;
		uint_fast8_t kbch, kbready;

		processmodem();
		processmessages(& kbch, & kbready, 1, mp);
		processtxrequest();	/* Установка сиквенсору запроса на передачу.	*/

#if WITHKEYBOARD

#if WITHENCODER2
		if (kbready == 0)
		{
			uint_fast8_t js;
			const int_least16_t nr2 = getRotateHiRes2(& js, genc2div);  // перемещение по меню также с помощью 2го энкодера

			if (nr2 > 0)
			{
				kbch = KBD_CODE_MENU_DOWN;
				kbready = 1;
			}
			else if (nr2 < 0)
			{
				kbch = KBD_CODE_MENU_UP;
				kbready = 1;
			}
		}
#endif /* WITHENCODER2 */

		if (kbready != 0)
		{

			switch (kbch)
			{
			default:
				if (kbch != exitkey)
					continue;
				/* в случчае несовпадения - прожолжаем работать. */
			case KBD_CODE_DISPMODE:
			case KBD_ENC2_PRESS:
				/* выход из меню */
				if (posnvram != MENUNONVRAM)
					save_i8(posnvram, menupos);	/* сохраняем номер пункта меню, с которым работаем */
				encoder_clear();	// сбросить информацию о повороте
				return;

#if ! WITHFLATMENU
			case KBD_CODE_MENU:
			case KBD_ENC2_HOLD:
				if (ismenukind(mp, ITEM_GROUP))
				{
					/* вход в подменю */
					const uint_fast16_t first = menupos + 1;	/* следующий за текущим пунктом */
					const uint_fast16_t last = menulooklast(first);

					if (ismenukind(& menutable [first], ITEM_VALUE))
					{
					#if defined (RTC1_TYPE)
						getstamprtc();
					#endif /* defined (RTC1_TYPE) */
						modifysettings(first, last, ITEM_VALUE, mp->qnvram, exitkey, byname);
						display2_redrawbarstimed(1, 1, mp);		/* обновление динамической части отображения - обновление S-метра или SWR-метра и volt-метра. */
					}
				}
				continue;	// требуется обновление индикатора
#endif /* ! WITHFLATMENU */

			case KBD_CODE_LOCK:
				savemenuvalue(mp);		/* сохраняем отредактированное значение */
				uif_key_lockencoder();
				display2_redrawbarstimed(1, 1, mp);		/* обновление динамической части отображения - обновление S-метра или SWR-метра и volt-метра. */
				continue;	// требуется обновление индикатора

#if WITHTX
			case KBD_CODE_MOX:
				savemenuvalue(mp);		/* сохраняем отредактированное значение */
				/* выключить режим настройки или приём/передача */
				uif_key_tuneoff();
				display2_redrawbarstimed(1, 1, mp);		/* обновление динамической части отображения - обновление S-метра или SWR-метра и volt-метра. */
				continue;	// требуется обновление индикатора

			case KBD_CODE_TXTUNE:
				savemenuvalue(mp);		/* сохраняем отредактированное значение */
				/* выключить режим настройки или приём/передача */
				uif_key_tune();
				display2_redrawbarstimed(1, 1, mp);		/* обновление динамической части отображения - обновление S-метра или SWR-метра и volt-метра. */
				continue;	// требуется обновление индикатора

	#if WITHAUTOTUNER
			case KBD_CODE_ATUSTART:
				savemenuvalue(mp);		/* сохраняем отредактированное значение */
				uif_key_atunerstart();
				display2_redrawbarstimed(1, 1, mp);		/* обновление динамической части отображения - обновление S-метра или SWR-метра и volt-метра. */
				continue;	// требуется обновление индикатора

			case KBD_CODE_ATUBYPASS:
				savemenuvalue(mp);		/* сохраняем отредактированное значение */
				uif_key_bypasstoggle();
				display2_redrawbarstimed(1, 1, mp);		/* обновление динамической части отображения - обновление S-метра или SWR-метра и volt-метра. */
				continue;	// требуется обновление индикатора
	#endif /* WITHAUTOTUNER */

#endif /* WITHTX */

			case KBD_CODE_BAND_DOWN:
#if WITHENCODER2
				savemenuvalue(mp);		/* сохраняем отредактированное значение */
				/* переход на следующий (с большей частотой) диапазон или на шаг general coverage */
				uif_key_click_banddown();
				display2_redrawbarstimed(1, 1, mp);		/* обновление динамической части отображения - обновление S-метра или SWR-метра и volt-метра. */
				continue;	// требуется обновление индикатора
#endif /* WITHENCODER2 */

			case KBD_CODE_MENU_DOWN:
				/* переход на предыдущий пункт меню */
				savemenuvalue(mp);		/* сохраняем отредактированное значение */
				do
				{
					/* проход по определённому типу элементов (itemmask) */
					menupos = calc_dir(! window.reverse, menupos, firstitem, lastitem);
					mp = & menutable [menupos];
				}
				while (! ismenukind(mp, itemmask));
				goto menuswitch;

			case KBD_CODE_BAND_UP:
#if WITHENCODER2
				savemenuvalue(mp);		/* сохраняем отредактированное значение */
				/* переход на следующий (с большей частотой) диапазон или на шаг general coverage */
				uif_key_click_bandup();
				display2_redrawbarstimed(1, 1, mp);		/* обновление динамической части отображения - обновление S-метра или SWR-метра и volt-метра. */
				continue;	// требуется обновление индикатора
#endif /* WITHENCODER2 */

			case KBD_CODE_MENU_UP:
				/* переход на следующий пункт меню */
				savemenuvalue(mp);		/* сохраняем отредактированное значение */
				do
				{
					/* если спецпункты запрещены - ищем обычный */
					menupos = calc_dir(window.reverse, menupos, firstitem, lastitem);
					mp = & menutable [menupos];
				}
				while (! ismenukind(mp, itemmask));

			menuswitch:
#if (NVRAM_TYPE != NVRAM_TYPE_CPUEEPROM)
				if (posnvram != MENUNONVRAM)
					save_i8(posnvram, menupos);	/* сохраняем номер пункта меню, с которым работаем */
#endif /* (NVRAM_TYPE != NVRAM_TYPE_CPUEEPROM) */

#if WITHDEBUG
				PRINTF(PSTR("menu: ")); PRINTF(mp->qlabel); PRINTF(PSTR("\n"));
#endif /* WITHDEBUG */

				display2_redrawbarstimed(1, 1, mp);		/* обновление динамической части отображения - обновление S-метра или SWR-метра и volt-метра. */
				break;
			}
		}
#endif /* WITHKEYBOARD */

#if WITHENCODER
		/* редактирование значения с помощью поворота валкодера. */
		nrotate = getRotateLoRes(genc1div);
		if (lockmode != 0)
			nrotate = 0;	// ignore encoder

		if (nrotate != 0 && ismenukind(mp, ITEM_VALUE))
		{
			/* редактирование паратметра */
			const uint_fast16_t step = mp->qistep;
			uint_fast16_t * const pv16 = mp->qpval16;
			uint_fast8_t * const pv8 = mp->qpval8;

			if (nrotate < 0)
			{
				// negative change value
				const uint_fast32_t bottom = mp->qbottom;
				if (pv16 != NULL)
				{
					* pv16 =
						prevfreq(* pv16, * pv16 - (- nrotate * step), step, bottom);
				}
				else
				{
					* pv8 =
						prevfreq(* pv8, * pv8 - (- nrotate * step), step, bottom);
				}
			}
			else
			{
				// positive change value
				const uint_fast32_t upper = mp->qupper;
				if (pv16 != NULL)
				{
					* pv16 =
						nextfreq(* pv16, * pv16 + (nrotate * step), step, upper + (uint_fast32_t) step);
				}
				else
				{
					* pv8 =
						nextfreq(* pv8, * pv8 + (nrotate * step), step, upper + (uint_fast32_t) step);
				}
			}
			/* обновление отображения пункта */
			board_wakeup();
			updateboard(1, 0);
			display2_redrawbarstimed(1, 1, mp);		/* немедленное обновление динамической части отображения - обновление S-метра или SWR-метра и volt-метра. */

#if (NVRAM_TYPE != NVRAM_TYPE_CPUEEPROM)
			savemenuvalue(mp);		/* сохраняем отредактированное значение */
#endif
		}
		else
		{
			display2_redrawbarstimed(0, 1, mp);		/* обновление динамической части отображения - обновление S-метра или SWR-метра и volt-метра. */
		}
#endif /* WITHENCODER */
	}
}

// Вызов конкретного пункта меню на редактирование
static void 
uif_key_click_menubyname(const char * name, uint_fast8_t exitkey)
{
#if WITHAUTOTUNER
	if (reqautotune != 0)
		return;
#endif /* WITHAUTOTUNER */

	uint_fast16_t menupos;

	for (menupos = 0; menupos < MENUROW_COUNT; ++ menupos)
	{
		const FLASHMEM struct menudef * const mp = & menutable [menupos];
		if ((mp->qspecial & ITEM_VALUE) == 0)
			continue;
	#if CPUSTYLE_ATMEGA || CPUSTYLE_ATXMEGA
		// Сравнение строки в SRAM и FLASH
		const int r = strcmp_P(name, mp->qlabel);
	#else /* CPUSTYLE_ATMEGA */
		const int r = strcmp(name, mp->qlabel);
	#endif /* CPUSTYLE_ATMEGA */
		if (r == 0)
			break;
	}
	if (menupos >= MENUROW_COUNT)
	{
		// Не нашли такой пункт
		return;
	}

	display2_bgreset();

	modifysettings(menupos, menupos, ITEM_VALUE, MENUNONVRAM, exitkey, 1);

	updateboard(1, 0);
	updateboard2();			/* настройки валкодера и цветовой схемы дисплея. */
	display2_bgreset();		/* возможно уже с новой цветовой схемой */
}

#if 0

static const struct menudef notchPopUp [] =
{
#if WITHNOTCHFREQ
	{
		QLABEL("NOTCH   "), 0, 0, 0, 0,
		ITEM_GROUP,
		0, 0,
		offsetof(struct nvmap, ggrpnotch),
		nvramoffs0,
		NULL,
		NULL,
		NULL,
	},
	{
		QLABEL("NOTCH   "), 8, 3, RJ_NOTCH,	ISTEP1,		/* управление режимом NOTCH */
		ITEM_VALUE,
		0, NOTCHMODE_COUNT - 1,
		RMT_NOTCHTYPE_BASE,							/* управление режимом NOTCH */
		nvramoffs0,
		NULL,
		& gnotchtype,
		getzerobase, /* складывается со смещением и отображается */
	},
	#if ! WITHPOTNOTCH
	{
		QLABEL("NTCH FRQ"), 7, 2, 1,	ISTEP50,		/* управление частотой NOTCH. */
		ITEM_VALUE,
		WITHNOTCHFREQMIN, WITHNOTCHFREQMAX,
		offsetof(struct nvmap, gnotchfreq),	/* центральная частота NOTCH */
		nvramoffs0,
		& gnotchfreq.value,
		NULL,
		getzerobase, /* складывается со смещением и отображается */
	},
	{
		QLABEL("NTCH WDT"), 7, 0, 0,	ISTEP50,		/* полоса режекции NOTCH. */
		ITEM_VALUE,
		WITHNOTCHWIDTHMIN, WITHNOTCHWIDTHMAX,
		offsetof(struct nvmap, gnotchwidth),	/* полоса режекции NOTCH */
		nvramoffs0,
		& gnotchwidth.value,
		NULL,
		getzerobase, /* складывается со смещением и отображается */
	},
	#endif /* ! WITHPOTNOTCH */
#elif WITHNOTCHONOFF
#if ! WITHFLATMENU
	{
		QLABEL("NOTCH   "), 0, 0, 0, 0,
		ITEM_GROUP,
		0, 0,
		offsetof(struct nvmap, ggrpnotch),
		nvramoffs0,
		NULL,
		NULL,
		NULL,
	},
#endif /* ! WITHFLATMENU */
	{
		QLABEL("NOTCH   "), 8, 3, RJ_ON,	ISTEP1,		/* управление режимом NOTCH */
		ITEM_VALUE,
		0, NOTCHMODE_COUNT - 1,
		RMT_NOTCH_BASE,							/* управление режимом NOTCH */
		nvramoffs0,
		NULL,
		& gnotch,
		getzerobase, /* складывается со смещением и отображается */
	},
#endif /* WITHNOTCHFREQ */
};

static const FLASHMEM struct menudef * thisPopUp = notchPopUp;
static size_t sizePopUp = ARRAY_SIZE(notchPopUp);
#else
static const FLASHMEM struct menudef * thisPopUp = NULL;
static size_t sizePopUp = 0;
#endif

// всплывающее меню
void display2_popup(
	uint_fast8_t xcell,
	uint_fast8_t ycell,
	dctx_t * pctx
	)
{

	if (thisPopUp == NULL)
		return;
	multimenuwnd_t mw;
	unsigned i;

	display2_getmultimenu(& mw);
	const uint_fast16_t x = GRID2X(xcell);
	const uint_fast16_t y = GRID2Y(ycell);
	const uint_fast16_t w = GRID2X(LABELW);
	const uint_fast16_t h = GRID2Y(mw.ystep) * sizePopUp;

	//display_fillrect(x, y, w, h, COLORMAIN_DARKGREEN);	// Фон

	for (i = 0; i < sizePopUp; ++ i)
	{
		const FLASHMEM struct menudef * const mp = thisPopUp + i;

		//menu
	}
}

#else // WITHMENU

static void 
uif_key_click_menubyname(const char * name, uint_fast8_t exitkey)
{
}

#endif // WITHMENU

#if MULTIVFO

static void dispvfocode(
	uint_fast8_t vco,
	uint_fast8_t top
	)
{
	char label [16];

	local_snprintf_P(label, sizeof label / sizeof label [0],
		top ?
			PSTR("VFO #%d HIGH") :
			PSTR("VFO #%d LOW "),
		vco
		);


	uint_fast32_t freq = getvcoranges(vco, top);
	synth_lo1_setfreq(0, freq, getlo1div(gtx));

	display_at(0, 1, label);
	display_menu_digit(0, 0, freq, 9, 3, 0);

}

/* Работа в режиме настройки VCO */
static void vfoallignment(void)
{
	uint_fast8_t vfo = 0;
	uint_fast8_t top = 0;
	dispvfocode(vfo, top);
	for (;;)
	{	
		uint_fast8_t kbch, kbready;

		processmessages(& kbch, & kbready, 1, mp);
		//display2_redrawbarstimed(0, 1, mp);	/* обновление динамической части отображения - обновление S-метра или SWR-метра и volt-метра. */

		if (kbready != 0)
		{
			switch (kbch)
			{
			case KBD_CODE_MENU:
			case KBD_CODE_DISPMODE:
				/* выход из режима настройки */
				return;
			case KBD_CODE_MODE:	/* та же клавиша, что и переключение режимов */
				top = ! top;	// переключение верхний или нижний край диапазона
				goto menuswitch;


			case KBD_CODE_BAND_DOWN:
			case KBD_CODE_MENU_DOWN:
				/* переход на предыдущий пункт меню */
				vfo = calc_prev(vfo, 0, HYBRID_NVFOS - 1);
				goto menuswitch;

			case KBD_CODE_BAND_UP:
			case KBD_CODE_MENU_UP:
				/* переход на следующий пункт меню */
				vfo = calc_next(vfo, 0, HYBRID_NVFOS - 1);

			menuswitch:
				dispvfocode(vfo, top);
				break;
			}
		}
	}
	return;
}
#endif // MULTIVFO

#if WITHKEYBOARD
/* возврат ненуля - было какое-либо нажатие,
	требуется обновление дисплея */
static uint_fast8_t
process_key_menuset0(uint_fast8_t kbch)
{
	switch (kbch)
	{

#if WITHSPLIT

	case KBD_CODE_SPLIT:
		/* Переключение VFO
			 - не вызывает сохранение состояния диапазона */
		if (gsplitmode == VFOMODES_VFOINIT)
			uif_key_spliton(0);		// включение SPLIT без смещения
		else
			uif_key_click_a_ex_b();	// Обмен VFO
		return 1;	// требуется обновление индикатора

	case KBD_CODE_SPLIT_HOLDED:
		/* Выход из режима переключение VFO - возврат к простой настройке
			 - не вызывает сохранение состояния диапазона */
		if (gsplitmode == VFOMODES_VFOINIT)
			uif_key_spliton(1);
		else
			uif_key_splitoff();
		return 1;	// требуется обновление индикатора

#elif WITHSPLITEX

	case KBD_CODE_SPLIT:
		/* Переключение VFO
			 - не вызывает сохранение состояния диапазона */
		if (gsplitmode == VFOMODES_VFOINIT)
			uif_key_spliton(0);
		else
			uif_key_splitoff();
		return 1;	// требуется обновление индикатора

	case KBD_CODE_SPLIT_HOLDED:
		/* Переключение VFO
			 - не вызывает сохранение состояния диапазона */
		uif_key_spliton(1);
		return 1;	// требуется обновление индикатора

#endif /* WITHSPLIT */

	case KBD_CODE_A_EX_B:
		uif_key_click_a_ex_b();
		return 1;	// требуется обновление индикатора

	case KBD_CODE_A_EQ_B:
		uif_key_click_b_from_a();
		return 1;	// требуется обновление индикатора

	case KBD_CODE_BAND_UP:
		/* переход на следующий (с большей частотой) диапазон или на шаг general coverage */
		uif_key_click_bandup();
		return 1;	// требуется обновление индикатора

	case KBD_CODE_BAND_DOWN:
		/* переход на предыдущий (с меньшей частотой) диапазон или на шаг general coverage */
		uif_key_click_banddown();
		return 1;	// требуется обновление индикатора

#if WITHDIRECTBANDS

	case KBD_CODE_BAND_1M8: 
		uif_key_click_banddjump(1800000L);
		return 1;	// требуется обновление индикатора
	case KBD_CODE_BAND_3M5: 
		uif_key_click_banddjump(3500000L);
		return 1;	// требуется обновление индикатора
	case KBD_CODE_BAND_5M3:
		uif_key_click_banddjump(5351500L);
		return 1;	// требуется обновление индикатора
	case KBD_CODE_BAND_7M0: 
		uif_key_click_banddjump(7000000L);
		return 1;	// требуется обновление индикатора
	case KBD_CODE_BAND_10M1: 
		uif_key_click_banddjump(10100000L);
		return 1;	// требуется обновление индикатора
	case KBD_CODE_BAND_14M0: 
		uif_key_click_banddjump(14000000L);
		return 1;	// требуется обновление индикатора
	case KBD_CODE_BAND_18M0: 
		uif_key_click_banddjump(18068000L);
		return 1;	// требуется обновление индикатора
	case KBD_CODE_BAND_21M0: 
		uif_key_click_banddjump(21000000L);
		return 1;	// требуется обновление индикатора
	case KBD_CODE_BAND_24M9: 
		uif_key_click_banddjump(24890000L);
		return 1;	// требуется обновление индикатора
	case KBD_CODE_BAND_28M0:
		uif_key_click_banddjump(28000000L);
		return 1;	// требуется обновление индикатора
	case KBD_CODE_BAND_50M0:
		uif_key_click_banddjump(50100000L);
		return 1;	// требуется обновление индикатора
#endif /* WITHDIRECTBANDS */

	default:
		return 0;	// не требуется обновления индикатора
	}
}
#endif /* WITHKEYBOARD */


static int_fast32_t
getpower10(uint_fast8_t pos)
{
	int_fast32_t v = 1;
	while (pos --)
		v *= 10;

	return v;
}

/* проверка введённой вручную частоты на допустимость. */
static uint_fast8_t
freqvalid(
	int_fast32_t freq,
	uint_fast8_t tx
	)
{
	return (freq >= TUNE_BOTTOM && freq < TUNE_TOP);	/* частота внутри допустимого диапазона */
}

#if WITHPWBUTTON
static void
uif_pwbutton_press(void)
{
	gpoweronhold = 0;
	updateboard(1, 0);
}
#endif /* WITHPWBUTTON */

#if WITHKEYBOARD

/* возврат ненуля - было какое-либо нажатие, клавиша уже обработана
	требуется обновление дисплея */
static uint_fast8_t
process_key_menuset_common(uint_fast8_t kbch)
{
#if WITHAUTOTUNER
	//const vindex_t b = getvfoindex(bi);
#endif /* WITHAUTOTUNER */
	switch (kbch)
	{
	case KBD_CODE_BW:
#if WITHIF4DSP
		/* Переключение фильтров
			 - не вызывает сохранение состояния диапазона */
		uif_key_changebw();
#else /* WITHIF4DSP */
		/* Переключение фильтров
			 - не вызывает сохранение состояния диапазона */
		uif_key_changefilter();
#endif /* WITHIF4DSP */
		return 1;	/* клавиша уже обработана */

#if WITHIF4DSP
	case KBD_CODE_NR:
		/* Переключение режтима шумоподавления
			 - не вызывает сохранение состояния диапазона */
		uif_key_changenr();
		return 1;	/* клавиша уже обработана */
#endif /* WITHIF4DSP */

#if ! WITHAGCMODENONE
	case KBD_CODE_AGC:
		/* AGC mode switch
			 - не вызывает сохранение состояния диапазона */
		uif_key_click_agcmode();
		return 1;	/* клавиша уже обработана */
#endif /* ! WITHAGCMODENONE */

#if WITHANTSELECTRX
	case KBD_CODE_ANTENNA_HOLDED:
		/* Antenna switch */
		uif_key_next_antenna();
		return 1;	/* клавиша уже обработана */

	case KBD_CODE_ANTENNA:
		/* RX Antenna switch */
		uif_key_next_rxantenna();
		return 1;	/* клавиша уже обработана */

#elif WITHANTSELECT2
	case KBD_CODE_ANTENNA_HOLDED:
		/* ручной/автоматический выбор антенны */
		uif_key_next_autoantmode();
		return 1;	/* клавиша уже обработана */

	case KBD_CODE_ANTENNA:
		/* Antenna switch */
		uif_key_next_antenna();
		return 1;	/* клавиша уже обработана */

#elif WITHANTSELECT
	case KBD_CODE_ANTENNA:
		/* Antenna switch
			 - не вызывает сохранение состояния диапазона */
		uif_key_next_antenna();
		return 1;	/* клавиша уже обработана */
#endif /* WITHANTSELECT || WITHANTSELECTRX */

#if WITHUSEAUDIOREC
	case KBD_CODE_RECORDTOGGLE:	// SD CARD audio recording
		sdcardtoggle();
		return 1;	/* клавиша уже обработана */
	case KBD_CODE_RECORD_HOLDED:
		sdcardformat();
		return 1;	/* клавиша уже обработана */
#endif /* WITHUSEAUDIOREC */

#if WITHPWBUTTON
	case KBD_CODE_POWEROFF:
		uif_pwbutton_press();
		return 1;
#endif /* WITHPWBUTTON */

#if WITHENCODER2
	case KBD_ENC2_PRESS:
		uif_encoder2_press();
		return 1;
	case KBD_ENC2_HOLD:
		uif_encoder2_hold();
		return 1;
#endif /* WITHENCODER2 */

#if WITHTX

	case KBD_CODE_MOX:
		/* выключить режим настройки или приём/передача */
		uif_key_tuneoff();
		return 1;	/* клавиша уже обработана */

	case KBD_CODE_TXTUNE:
		/* включить режим настройки */
		uif_key_tune();
		return 1;	/* клавиша уже обработана */

#if WITHAUTOTUNER
	case KBD_CODE_ATUSTART:
		uif_key_atunerstart();
		return 1;	/* клавиша уже обработана */

	case KBD_CODE_ATUBYPASS:
		uif_key_bypasstoggle();
		return 1;	/* клавиша уже обработана */
#endif /* WITHAUTOTUNER */

#if WITHVOX
	case KBD_CODE_VOXTOGGLE:
		uif_key_voxtoggle();
		return 1;	/* клавиша уже обработана */
#endif /* WITHVOX */
#if WITHELKEY
	case KBD_CODE_BKIN:
		uif_key_bkintoggle();
		return 1;
#endif /* WITHELKEY */

#if WITHIF4DSP && WITHUSBUAC && WITHDATAMODE
	case KBD_CODE_DATATOGGLE:
		uif_key_click_datamode();
		return 1;	/* клавиша уже обработана */
#endif /* WITHIF4DSP && WITHUSBUAC && WITHDATAMODE */

#endif /* WITHTX */

#if WITHUSEDUALWATCH
	case KBD_CODE_DWATCHHOLD:
		return 1;	/* клавиша уже обработана */

	case KBD_CODE_DWATCHTOGGLE:
		uif_key_mainsubrx();
		return 1;	/* клавиша уже обработана */

#endif /* WITHUSEDUALWATCH */

#if WITHAMHIGHKBDADJ
	case KBD_CODE_AMBANDPASSUP:
		uif_key_click_amfmbandpassup();
		return 1;	/* клавиша уже обработана */
	case KBD_CODE_AMBANDPASSDOWN:
		uif_key_click_amfmbandpassdown();
		return 1;	/* клавиша уже обработана */
#endif /* WITHAMHIGHKBDADJ */

#if WITHSPKMUTE
	case KBD_CODE_LDSPTGL:
		uif_key_loudsp();
		return 1;	/* клавиша уже обработана */
#endif /* WITHSPKMUTE */

#if CTLSTYLE_RA4YBO || CTLSTYLE_RA4YBO_V3
	case KBD_CODE_AFNARROW:
		/*  - не вызывает сохранение состояния диапазона */
		uif_key_affilter();
		return 1;	/* клавиша уже обработана */

#endif /* CTLSTYLE_RA4YBO || CTLSTYLE_RA4YBO_V3 */

#if CTLSTYLE_RA4YBO_V1 || CTLSTYLE_RA4YBO_V2 || CTLSTYLE_RA4YBO_V3
	case KBD_CODE_USER1:
		uif_key_user1();
		return 1;	/* клавиша уже обработана */

#if 0
	case KBD_CODE_USER2: // rec
		uif_key_user2();
		return 1;	/* клавиша уже обработана */

	case KBD_CODE_USER3:	// play
		uif_key_user3();
		return 1;	/* клавиша уже обработана */
#endif

	case KBD_CODE_USER4:
		uif_key_user4();
		return 1;	/* клавиша уже обработана */

	case KBD_CODE_USER5:
		uif_key_user5();
		return 1;	/* клавиша уже обработана */

#endif /* CTLSTYLE_RA4YBO_V1 || CTLSTYLE_RA4YBO_V2*/

#if KEYBSTYLE_RA4YBO_AM0

	case KBD_CODE_USER1:
		uif_key_user1();
		return 1;	/* клавиша уже обработана */
	case KBD_CODE_USER2:
		uif_key_user2();
		return 1;	/* клавиша уже обработана */
	case KBD_CODE_USER3:
		uif_key_user3();
		return 1;	/* клавиша уже обработана */
#endif /* KEYBSTYLE_RA4YBO_AM0 */

#if WITHELKEY

	case KBD_CODE_CWSPEEDDOWN:
			if (elkeywpm.value > CWWPMMIN)
			{
				elkeywpm.value -= 1;
				save_i8(offsetof(struct nvmap, elkeywpm), elkeywpm.value);
				updateboard(1, 0);
			}
		return 1;	/* клавиша уже обработана */

	case KBD_CODE_CWSPEEDUP:
			if (elkeywpm.value < CWWPMMAX)
			{
				elkeywpm.value += 1;
				save_i8(offsetof(struct nvmap, elkeywpm), elkeywpm.value);
				updateboard(1, 0);
			}
		return 1;	/* клавиша уже обработана */

#endif /* WITHELKEY */

#if ELKEY328
	case KBD_CODE_VIBROCTL:
		/* переключение режима аттенюатора  */
		uif_key_click_vibroplex();
		return 1;	// требуется обновление индикатора

#endif /* ELKEY328 */

	case KBD_CODE_ATT:
		/* переключение режима аттенюатора  */
		uif_key_click_attenuator();
		return 1;	/* клавиша уже обработана */

#if ! WITHONEATTONEAMP
	case KBD_CODE_PAMP:
		/* переключение режима предусилителя  */
		uif_key_click_pamp();
		return 1;	/* клавиша уже обработана */
#endif /* ! WITHONEATTONEAMP */

	case KBD_CODE_MODE:
		/* переход по "столбцу" режимов - быстрое нажатие */
		/* switch to next moderow */
		uif_key_click_moderow();
		return 1;	/* клавиша уже обработана */

	case KBD_CODE_MODEMOD:
		/* переход по "строке" режимов - удержанное нажатие */
		// step to next modecol
		uif_key_hold_modecol();
		return 1;	/* клавиша уже обработана */

#if WITHDIRECTBANDS

	case KBD_CODE_MODE_0:
		/* переход по "столбцу" режимов - быстрое нажатие */
		/* switch to next moderow */
		uif_key_click_moderows(0);
		return 1;	/* клавиша уже обработана */

	case KBD_CODE_MODE_1:
		/* переход по "столбцу" режимов - быстрое нажатие */
		/* switch to next moderow */
		uif_key_click_moderows(1);
		return 1;	/* клавиша уже обработана */

	case KBD_CODE_MODE_2:
		/* переход по "столбцу" режимов - быстрое нажатие */
		/* switch to next moderow */
		uif_key_click_moderows(2);
		return 1;	/* клавиша уже обработана */

	case KBD_CODE_MODE_3:
		/* переход по "столбцу" режимов - быстрое нажатие */
		/* switch to next moderow */
		uif_key_click_moderows(3);
		return 1;	/* клавиша уже обработана */

	case KBD_CODE_MODEMOD_0:
		/* переход по "строке" режимов - удержанное нажатие */
		// step to next modecol
		uif_key_hold_modecols(0);
		return 1;	/* клавиша уже обработана */

	case KBD_CODE_MODEMOD_1:
		/* переход по "строке" режимов - удержанное нажатие */
		// step to next modecol
		uif_key_hold_modecols(1);
		return 1;	/* клавиша уже обработана */

	case KBD_CODE_MODEMOD_2:
		/* переход по "строке" режимов - удержанное нажатие */
		// step to next modecol
		uif_key_hold_modecols(2);
		return 1;	/* клавиша уже обработана */

	case KBD_CODE_MODEMOD_3:
		/* переход по "строке" режимов - удержанное нажатие */
		// step to next modecol
		uif_key_hold_modecols(3);
		return 1;	/* клавиша уже обработана */

#endif /* WITHDIRECTBANDS */

	case KBD_CODE_LOCK:
		/* блокировка валкодера
			 - не вызывает сохранение состояния диапазона */
		uif_key_lockencoder();
		return 1;	/* клавиша уже обработана */

	case KBD_CODE_LOCK_HOLDED:
#if WITHLCDBACKLIGHTOFF
		{
			dimmmode = calc_next(dimmmode, 0, 1);
			display2_bgreset();
			display_redrawfreqmodesbarsnow(0, NULL);			/* Обновление дисплея - всё, включая частоту */
			updateboard(1, 0);
		}
#endif /* WITHLCDBACKLIGHTOFF */
		return 1;	/* клавиша уже обработана */

#if WITHBCBANDS
	case KBD_CODE_GEN_HAM:
		uif_key_genham();
		return 1;	/* клавиша уже обработана */
#endif /* WITHBCBANDS */


#if WITHUSEFAST
	case KBD_CODE_USEFAST:
		uif_key_usefast();
		return 1;	/* клавиша уже обработана */
#endif /* WITHUSEFAST */
#if WITHPOWERLPHP
	case KBD_CODE_PWRTOGGLE:
		/* переключение режима мощности (для sw2012sf) */
		uif_key_click_pwr();
		return 1;	/* клавиша уже обработана */
#endif /* WITHPOWERLPHP */

#if WITHNOTCHONOFF
	case KBD_CODE_NOTCHTOGGLE:
		/* переключение NOTCH фильтра */
		uif_key_click_notch();
		return 1;	/* клавиша уже обработана */
#endif /* WITHNOTCHONOFF */

#if WITHNOTCHFREQ
	case KBD_CODE_NOTCHTOGGLE:
		/* переключение NOTCH фильтра */
		uif_key_click_notch();
		return 1;	/* клавиша уже обработана */

	case KBD_CODE_NOTCHFREQ:
		uif_key_click_menubyname("NTCH FRQ", KBD_CODE_NOTCHTOGGLE);
		return 1;
#endif /* WITHNOTCHFREQ */


#if WITHAUTOTUNER && KEYB_UA3DKC
	case KBD_CODE_TUNERTYPE:	// переключение типа согласующего устройства
		tunertype = calc_next(tunertype, 0, KSCH_COUNT - 1);
		save_i8(offsetof(struct nvmap, bands[b].tunertype), tunertype);
		updateboard_tuner();
		return 1;	// требуется обновление индикатора

	case KBD_CODE_CAP_UP:	// увеличение емкости
		// todo: добавить учет включенной антенны
		tunercap = calc_next(tunercap, CMIN, CMAX);
		save_i8(offsetof(struct nvmap, bands[b].tunercap), tunercap);
		updateboard_tuner();
		return 1;	// требуется обновление индикатора

	case KBD_CODE_CAP_DOWN:	// уменьшение емкости
		// todo: добавить учет включенной антенны
		tunercap = calc_prev(tunercap, CMIN, CMAX);
		save_i8(offsetof(struct nvmap, bands[b].tunercap), tunercap);
		updateboard_tuner();
		return 1;	// требуется обновление индикатора

	case KBD_CODE_IND_UP:	// увеличение индуктивности
		// todo: добавить учет включенной антенны
		tunerind = calc_next(tunerind, LMIN, LMAX);
		save_i8(offsetof(struct nvmap, bands[b].tunerind), tunerind);
		updateboard_tuner();
		return 1;	// требуется обновление индикатора

	case KBD_CODE_IND_DOWN:	// уменьшение индуктивности
		// todo: добавить учет включенной антенны
		tunerind = calc_prev(tunerind, LMIN, LMAX);
		save_i8(offsetof(struct nvmap, bands[b].tunerind), tunerind);
		updateboard_tuner();
		return 1;	// требуется обновление индикатора
#endif /* WITHAUTOTUNER && KEYB_UA3DKC */

	default:
		return 0;	/* клавиша не обработана */
	}
}

#if WITHWAVPLAYER || WITHSENDWAV
void playhandler(uint8_t code)
{
	if (code >= 1 && code < (sizeof loopnames / sizeof loopnames [0]))
		playwavfile(loopnames [code]);
	else
		playwavstop();

}
#endif /* WITHWAVPLAYER || WITHSENDWAV */

/* возврат ненуля - было какое-либо нажатие,
	требуется обновление дисплея и состояния аппаратуры */
static uint_fast8_t
processkeyboard(uint_fast8_t kbch)
{
	const uint_fast8_t exitkey = getexitkey();	/* эта клавиша совмещена с menu - дополнительный код для выхода. */

#if WITHDIRECTFREQENER
	uint_fast8_t rj;
	uint_fast8_t fullwidth = display_getfreqformat(& rj);
	const int DISPLAY_LEFTBLINKPOS = fullwidth - 1;

	if (editfreqmode != 0)
	{
		const uint_fast8_t c = kbd_getnumpad(kbch);
		if (kbch == KBD_CODE_ENTERFREQDONE)
		{
			editfreqmode = 0;
			return 1;
		}
		if (c == '#' && blinkpos < DISPLAY_LEFTBLINKPOS)
		{
			blinkpos += 1;	/* перемещаемся на одну позицию левее */
			updateboard(1, 0);
			return 1;
		}
		if (c >= '0' && c <= '9')
		{
			const int_fast32_t m = getpower10(blinkpos + rj);
			const int_fast32_t m10 = m * 10;
			editfreq = editfreq / m10 * m10 + (c - '0') * m;
			if (blinkpos != 0)
				-- blinkpos;	/* перемещаемся на одну позицию правее */
			else if (freqvalid(editfreq, gtx))
			{
				const uint_fast8_t bi = getbankindex_tx(gtx);
				vindex_t vi = getvfoindex(bi);
				gfreqs [bi] = editfreq;
				editfreqmode = 0;
				savebandfreq(vi, bi);		/* сохранение частоты в текущем VFO */
				updateboard(1, 0);
			}
			else
			{
				/* опять к начальному состоянию */
				blinkpos = DISPLAY_LEFTBLINKPOS;		/* позиция курсора */
				editfreqmode = 1;
				editfreq = gfreqs [getbankindex_tx(gtx)];
			}
			return 1;
		}
	}
	else if (kbch == KBD_CODE_ENTERFREQ)
	{
		blinkpos = DISPLAY_LEFTBLINKPOS;		/* позиция курсора */
		editfreqmode = 1;
		editfreq = gfreqs [getbankindex_tx(gtx)];
		return 1;
	}
#endif /* WITHDIRECTFREQENER */

	switch (kbch)
	{
	case KBD_CODE_MENU:
		/* Вход в меню
			 - не вызывает сохранение состояния диапазона */
#if WITHMENU
	#if WITHAUTOTUNER
		if (reqautotune != 0)
			return 1;
	#endif /* WITHAUTOTUNER */
		display2_bgreset();
	#if defined (RTC1_TYPE)
		getstamprtc();
	#endif /* defined (RTC1_TYPE) */
	#if WITHFLATMENU
		modifysettings(0, MENUROW_COUNT - 1, ITEM_VALUE, RMT_GROUP_BASE, exitkey, 0);	/* выбор группы параметров для редактирования */
	#else /* WITHFLATMENU */
		modifysettings(0, MENUROW_COUNT - 1, ITEM_GROUP, RMT_GROUP_BASE, exitkey, 0);	/* выбор группы параметров для редактирования */
	#endif /* WITHFLATMENU */
		updateboard(1, 0);
		updateboard2();			/* настройки валкодера и цветовой схемы дисплея. */
		display2_bgreset();		/* возможно уже с новой цветовой схемой */
		return 1;	// требуется обновление индикатора
#else
		return 0;
#endif //WITHMENU

	case KBD_CODE_DISPMODE:
		if (display_getpagesmax() != 0)
		{
			/* Альтернативные функции кнопок - "Fn"
				 - не вызывает сохранение состояния диапазона */
			menuset = calc_next(menuset, 0, display_getpagesmax());
			save_i8(RMT_MENUSET_BASE, menuset);
			display2_bgreset();
			return 1;	// требуется обновление индикатора
		}
		return 0;	// не требуется обновление индикатора

#if WITHMENU
#if WITHIF4DSP
	case KBD_CODE_MENU_CWSPEED:
		uif_key_click_menubyname("CW SPEED", KBD_CODE_MENU_CWSPEED);
		return 1;	/* клавиша уже обработана */

	case KBD_CODE_IFSHIFT:
		uif_key_click_menubyname("IF SHIFT", KBD_CODE_IFSHIFT);
		return 1;	/* клавиша уже обработана */

#elif KEYBSTYLE_SW2013SF_US2IT || KEYBSTYLE_SW2012CN5_UY5UM
	case KBD_CODE_MENU_CWSPEED:
		uif_key_click_menubyname("CW SPEED", KBD_CODE_MENU_CWSPEED);
		return 1;	/* клавиша уже обработана */

	case KBD_CODE_IFSHIFT:
		uif_key_click_menubyname("IF SHIFT", KBD_CODE_IFSHIFT);
		return 1;	/* клавиша уже обработана */

#elif KEYB_RA4YBO_V1
	case KBD_CODE_IFSHIFT:
		uif_key_click_menubyname("IF SHIFT", KBD_CODE_IFSHIFT);
		return 1;	/* клавиша уже обработана */

#elif KEYB_RA4YBO_V3
	case KBD_CODE_IFSHIFT:
		uif_key_click_menubyname("IF SHIFT", KBD_CODE_IFSHIFT);
		return 1;	/* клавиша уже обработана */

#endif /* KEYBSTYLE_SW2013SF_US2IT || WITHIF4DSP */
#endif /* WITHMENU */

	default:
		break;
	}

#if WITHWAVPLAYER || WITHSENDWAV
	switch (kbch)
	{
	case KBD_CODE_PLAYFILE1:
		playhandler(1);
		return 1;	/* клавиша уже обработана */
	case KBD_CODE_PLAYFILE2:
		playhandler(2);
		return 1;	/* клавиша уже обработана */
	case KBD_CODE_PLAYFILE3:
		playhandler(3);
		return 1;	/* клавиша уже обработана */
	case KBD_CODE_PLAYFILE4:
		playhandler(4);
		return 1;	/* клавиша уже обработана */
	case KBD_CODE_PLAYFILE5:
		playhandler(5);
		return 1;	/* клавиша уже обработана */
	case KBD_CODE_PLAYSTOP:
		playwavstop();
		return 1;	/* клавиша уже обработана */
#if ! WITHPOTAFGAIN
	case KBD_CODE_PLAYLOUD:	// громче
		if (afgain1.value != BOARD_AFGAIN_MAX)
		{
			afgain1.value = calc_next(afgain1.value, BOARD_AFGAIN_MIN, BOARD_AFGAIN_MAX);
			save_i8(offsetof(struct nvmap, afgain1), afgain1.value);
			updateboard(1, 0);
		}
		return 1;
	case KBD_CODE_PLAYQUITE:	// тише
		if (afgain1.value != BOARD_AFGAIN_MIN)
		{
			afgain1.value = calc_prev(afgain1.value, BOARD_AFGAIN_MIN, BOARD_AFGAIN_MAX);
			save_i8(offsetof(struct nvmap, afgain1), afgain1.value);
			updateboard(1, 0);
		}
		return 1;
#endif /* ! WITHPOTAFGAIN */
	}
#endif /* WITHWAVPLAYER */

	uint_fast8_t processed = 0;
#if 1
	processed = process_key_menuset0(kbch);
#else
	switch (menuset)
	{
	default:
	case DISPMODE_MAIN:
		processed = process_key_menuset0(kbch);
		break;
#if DSTYLE_SWITCHMODES2		// по кнопке управления переключается набор отображаемой информации
	case DISPMODE_ALT:
		processed = process_key_menuset0(kbch);
		break;
#endif /* DSTYLE_SWITCHMODES2 */
	}
#endif
	if (processed != 0)
		return 1;
	if (process_key_menuset_common(kbch))
			return 1;	/* клавиша уже обработана */
	return 0;	// не требуется обновления индикатора
}

#else /* WITHKEYBOARD */

static uint_fast8_t
processkeyboard(uint_fast8_t kbch)
{
	return 0;
}

#endif /* WITHKEYBOARD */

/* вызывается при запрещённых прерываниях. */
static void 
lowinitialize(void)
{
	board_beep_initialize();
	//hardware_cw_diagnostics_noirq(1, 0, 1);	// 'K'
#if WITHDEBUG

	//HARDWARE_DEBUG_INITIALIZE();
	//HARDWARE_DEBUG_SET_SPEED(DEBUGSPEED);


	dbg_puts_impl_P(PSTR("Version " __DATE__ " " __TIME__ " 1 debug session starts.\n"));
	// выдача повторяющегося символа для тестирования скорости передачи, если ошибочная инициализация
	//for (;;)
	//	hardware_putchar(0xff);
	// тестирование приёма и передачи символов
	for (;0;)
	{
		char c;
		if (dbg_getchar(& c))
		{
			if (c == 0x1b)
				break;
			dbg_putchar(c);
		}
	}
#endif /* WITHDEBUG */

#if WITHCAT

	HARDWARE_CAT_INITIALIZE();

#endif /* WITHCAT */


#if WITHNMEA && WITHAUTOTUNER_UA1CEI

	HARDWARE_NMEA_INITIALIZE();
	HARDWARE_NMEA_SET_SPEED(250000L);
	HARDWARE_NMEA_ENABLERX(1);

#elif WITHNMEA

	HARDWARE_NMEA_INITIALIZE();
	HARDWARE_NMEA_SET_SPEED(115200L);
	HARDWARE_NMEA_ENABLERX(1);

#endif /* WITHNMEA */

#if WITHMODEM

	HARDWARE_MODEM_INITIALIZE();
	HARDWARE_MODEM_SET_SPEED(19200L);
	HARDWARE_MODEM_ENABLERX(1);

#endif /* WITHMODEM */

	//hardware_cw_diagnostics_noirq(1, 0, 0);	// 'D'
	// Инициализация таймера и списка регистрирумых обработчиков
	hardware_timer_initialize(TICKS_FREQUENCY);

	board_initialize();		/* инициализация чипселектов и SPI, I2C, загрузка FPGA */
	cpu_initdone();			/* секция init (в которой лежит образ для загрузки в FPGA) больше не нужна */
	display_hardware_initialize();
#if WITHWATCHDOG
	watchdog_initialize();	/* разрешение сторожевого таймера в устройстве */
#endif /* WITHWATCHDOG */
	static ticker_t displayticker;
	static ticker_t ticker_1S;

	ticker_initialize(& displayticker, 1, display_spool, NULL);	// вызывается с частотой TICKS_FREQUENCY (например, 200 Гц) с запрещенными прерываниями.
	ticker_add(& displayticker);
	ticker_initialize(& ticker_1S, NTICKS(1000), spool_secound, NULL);	// вызывается с частотой TICKS_FREQUENCY (например, 200 Гц) с запрещенными прерываниями.
	ticker_add(& ticker_1S);

	buffers_initialize();	// инициализация системы буферов - в том числе очереди сообщений

#if WITHUSBHW
	board_usb_initialize();		// USB device and host support
#endif /* WITHUSBHW */
#if WITHGPUHW
	board_gpu_initialize();		// GPU controller
#endif /* WITHGPUHW */
#if WITHENCODER
	hardware_encoder_initialize();	//  todo: разобраться - вызов перенесен сюда из board_initialize - иначе не собирается под Cortex-A9.
#endif /* WITHENCODER */

#if WITHLFM
	hardware_lfm_timer_initialize();
	hardware_lfm_setupdatefreq(20);
#endif /* WITHLFM */
#if WITHENCODER
	encoder_initialize();
#endif /* WITHENCODER */
#if WITHELKEY
	elkey_initialize();
#endif /* WITHELKEY */
	seq_initialize();
	vox_initialize();		/* подготовка работы задержек переключения приём-передача */
#if WITHSDHCHW
	hardware_sdhost_initialize();	/* если есть аппаратный контрлллер SD CARD */
#endif /* WITHSDHCHW */

#if WITHCAT
	processcat_initialize();
#endif
#if WITHKEYBOARD
	kbd_initialize();
#endif /* WITHKEYBOARD */

#if WITHDEBUG
	dbg_puts_impl_P(PSTR("Most of hardware initialized.\n"));
#endif

#if 0
	{
		const spitarget_t cs = targetfpga1;
		PRINTF(PSTR("targetfpga1=%04lX\n"), (unsigned long) cs);
		dbg_puts_impl_P(PSTR("SPI send test started.\n"));
		// Тестирование скорости передачи по SPI. На SCK должна быть частота SPISPEED
		for (;;)
		{
			static uint_fast8_t v = 0x00;
			v = ~ v;
			spi_select(cs, SPIC_MODE3);
			spi_progval8_p1(cs, v);
			spi_progval8_p2(cs, v);
			spi_progval8_p2(cs, v);
			spi_progval8_p2(cs, v);
			spi_progval8_p2(cs, v);
			spi_progval8_p2(cs, v);
			spi_progval8_p2(cs, v);
			spi_progval8_p2(cs, v);
			spi_progval8_p2(cs, v);
			spi_progval8_p2(cs, v);
			spi_progval8_p2(cs, v);
			spi_progval8_p2(cs, v);
			spi_complete(cs);
			spi_unselect(cs);
		}
	}
#endif
	//for (;;) ;
	//hardware_cw_diagnostics_noirq(1, 1, 0);	// 'S'
	//board_testsound_enable(0);	// Выключить 1 кГц на самоконтроле
}

static uint_fast8_t
keyboard_test(void)
{
	uint_fast8_t n;

	// 1 secound total
	for (n = 0; n < 100; ++ n)
	{
		if (kbd_is_tready() != 0)
			return 1;
		local_delay_ms(10);
	}
	return 0;
}

/* вызывается при разрешённых прерываниях. */
static void initialize2(void)
{
	uint_fast8_t mclearnvram;


	//hardware_cw_diagnostics(0, 1, 0);	// 'D'

	PRINTF(PSTR("initialize2() started.\n"));

	display_reset();
	display_initialize();

	display2_initialize();
	display2_bgreset();

	if (keyboard_test() == 0)
	{
		static const FLASHMEM char msg  [] = "KBD fault";

		board_set_bglight(0, WITHLCDBACKLIGHTMAX);	// включить подсветку
		board_update();

		display_at_P(0, 0, msg);
		display_flush();

		PRINTF(PSTR("KBD fault\n"));
		for (;;)
			;
	}
	PRINTF(PSTR("KBD ok\n"));

#if defined(NVRAM_TYPE) && (NVRAM_TYPE != NVRAM_TYPE_NOTHING)

	//PRINTF(PSTR("initialize2(): NVRAM initialization started.\n"));

	mclearnvram = kbd_get_ishold(KIF_ERASE) != 0;
	//extmenu = kbd_get_ishold(KIF_EXTMENU);
	
	// проверить работу - потом закомментарит.
	if (sizeof (struct nvmap) > (NVRAM_END + 1))
	{
		// в случае отсутствия превышения размера этот кусок и переменная не комптилируются
		static const FLASHMEM char msg  [] = "TOO LARGE nvmap";
		void wrong_NVRAM_END(void);

		board_set_bglight(0, WITHLCDBACKLIGHTMAX);	// включить подсветку
		board_update();

		display_menu_digit(0, 0, sizeof (struct nvmap), 9, 0, 0);
		display_at_P(0, 1, msg);
		display_flush();

		wrong_NVRAM_END();
		//hardware_cw_diagnostics(0, 0, 0);	// 'S'
		for (;;)
			;
	}
	else
	{
		// отладочная печать размера памяти, требуемого для хранения конфигурации
		/*
		static const FLASHMEM char msg  [] = "nvmap size";

		display_menu_digit(sizeof (struct nvmap), 9, 0, 0);
		display_at_P(0, 0, msg);


		unsigned i;
		for (i = 0; i < 50; ++ i)
		{
			local_delay_ms(20);
		}
		*/
	}

	nvram_initialize();	// write enable all nvram memory regions

#endif /* defined(NVRAM_TYPE) && (NVRAM_TYPE != NVRAM_TYPE_NOTHING) */

	//PRINTF(PSTR("initialize2(): NVRAM initialization passed.\n"));

#if HARDWARE_IGNORENONVRAM

#elif NVRAM_TYPE == NVRAM_TYPE_FM25XXXX

	//PRINTF(PSTR("initialize2(): NVRAM autodetection start.\n"));

	uint_fast8_t ab = 0;
	const uint_fast8_t ABMAX = 2;
	// проверка сигнатуры привсех возможных ab
	for (ab = 0; ab < ABMAX; ++ ab)
	{
		nvram_set_abytes(ab);
		if (verifynvramsignature() == 0)
			break;
	}
	if (ab >= ABMAX)
	{
		mclearnvram = 2;	/* ни при одном ab сигнатура не совпала */
	}

	if (mclearnvram)
	{
		/* есть запрос на стирание памяти - получаем подтверждение от оператора. */
		if (mclearnvram == 1)
		{
			uint_fast8_t kbch;

			board_set_bglight(0, WITHLCDBACKLIGHTMAX);	// включить подсветку
			board_update();

			display_at_P(0, 0, PSTR("ERASE: Press SPL"));
			display_flush();

			for (;;)
			{
				while (kbd_scan(& kbch) == 0)
					local_delay_ms(20);	// FIXME: разобраться почему не работает без
				PRINTF("kbch=0x%02X (%u)\n", (unsigned) kbch, (unsigned) kbch);
				if (kbch == KBD_CODE_SPLIT || kbch == KBD_CODE_ERASECONFIG)
					break;
			}
			display2_bgreset();
		}
		/* есть запрос на стирание памяти - опять перебираем все ab */
		for (ab = 0; ab < ABMAX; ++ ab)
		{
			nvram_set_abytes(ab);
			/* стирание всей памяти */
			uint_least16_t i;
			for (i = 0; i < sizeof (struct nvmap); ++ i)
				save_i8(i, 0xFF);

			initnvrampattern();

			if (verifynvrampattern() == 0)
				break;						// прочиталось!
		}
		if (ab >= ABMAX)
		{
			// в случае неправильно работающего NVRAM зависаем
			PRINTF(PSTR("initialize2(): NVRAM initialization: wrong NVRAM pattern in any address sizes.\n"));

			board_set_bglight(0, WITHLCDBACKLIGHTMAX);	// включить подсветку
			board_update();

			display_menu_digit(0, 0, NVRAM_END + 1, 9, 0, 0);
			display_at_P(0, 1, PSTR("NVRAM fault"));
			display_flush();

			PRINTF(PSTR("NVRAM fault1\n"));
			for (;;)
				;
		}
#if WITHMENU
		defaultsettings();		/* загрузка в nvram установок по умолчанию */
#endif //WITHMENU
		initnvramsignature();
		//extmenu = 1;	/* сразу включаем инженерный режим - без перезагрузки доступны все пункты */
	}

#else /* NVRAM_TYPE == NVRAM_TYPE_FM25XXXX */

	//PRINTF(PSTR("initialize2(): NVRAM(BKPSRAM/CPU EEPROM/SPI MEMORY) initialization: verify NVRAM signature.\n"));

	if (verifynvramsignature())
		mclearnvram = 2;

	//PRINTF(PSTR("initialize2(): NVRAM initialization: work on NVRAM signature, mclearnvram=%d\n"), mclearnvram);

	if (mclearnvram != 0)
	{
		if (mclearnvram == 1)
		{
			uint_fast8_t kbch;

			board_set_bglight(0, WITHLCDBACKLIGHTMAX);	// включить подсветку
			board_update();

			display_at_P(0, 0, PSTR("ERASE: Press SPL"));
			display_flush();

			for (;;)
			{
				while (kbd_scan(& kbch) == 0)
					local_delay_ms(20);	// FIXME: разобраться почему не работает без
				PRINTF("kbch=0x%02X (%u)\n", (unsigned) kbch, (unsigned) kbch);
				if (kbch == KBD_CODE_SPLIT || kbch == KBD_CODE_ERASECONFIG)
					break;
			}
			display2_bgreset();
		}
		
		//PRINTF(PSTR("initialize2(): NVRAM initialization: erase NVRAM.\n"));
		/* стирание всей памяти */
		uint_least16_t i;
		for (i = 0; i < sizeof (struct nvmap); ++ i)
			save_i8(i, 0xFF);

		//PRINTF(PSTR("initialize2(): NVRAM initialization: write NVRAM pattern.\n"));
		initnvrampattern();
		//PRINTF(PSTR("initialize2(): NVRAM initialization: verify NVRAM pattern.\n"));

		if (verifynvrampattern())
		{
			PRINTF(PSTR("initialize2(): NVRAM initialization: wrong NVRAM pattern.\n"));
			// проверяем только что записанную сигнатуру
			// в случае неправильно работающего NVRAM зависаем

			board_set_bglight(0, WITHLCDBACKLIGHTMAX);	// включить подсветку
			board_update();

			display_menu_digit(0, 1, NVRAM_END + 1, 9, 0, 0);
			display_at_P(0, 1, PSTR("NVRAM fault"));
			display_flush();

			for (;;)
				;
		}

#if WITHMENU
		defaultsettings();		/* загрузка в nvram установок по умолчанию */
#endif //WITHMENU

		//PRINTF(PSTR("initialize2(): NVRAM initialization: write NVRAM signature.\n"));
		initnvramsignature();
		//extmenu = 1;	/* сразу включаем инженерный режим - без перезагрузки доступны все пункты */
	}
	//blinkmain();
#endif /* NVRAM_TYPE == NVRAM_TYPE_FM25XXXX */

	(void) mclearnvram;

#if WITHDEBUG
	dbg_puts_impl_P(PSTR("initialize2() finished.\n"));
#endif
}

static uint_fast8_t usbactivated;

/* вызывается при разрешённых прерываниях. */
static void 
hamradio_initialize(void)
{
	/* NVRAM уже можно пользоваться */
#if WITHMENU && ! HARDWARE_IGNORENONVRAM
	loadsettings();		/* загрузка всех установок из nvram. Не восстанавливаем "массивы" */
#endif /* WITHMENU && ! HARDWARE_IGNORENONVRAM */
	//extmenu = extmenu || alignmode;
	loadsavedstate();	// split, lock, s-meter display, see also loadsettings().
	loadnewband(getvfoindex(1), 1);	/* загрузка последнего сохраненного состояния - всегда VFO или MEMxx */
	loadnewband(getvfoindex(0), 0);	/* загрузка последнего сохраненного состояния - всегда VFO или MEMxx */

	synthcalc_init();
	bandf_calc_initialize();
	bandf2_calc_initialize();
	bandf3_calc_initialize();
#if CTLSTYLE_SW2011ALL
	board_set_bandfonhpf(bandf_calc(nyquistadj(14000000L)));	/* в SW20xx частота (диапазон), с которого включается ФВЧ на входе приёмника */
	board_set_bandfonuhf(bandf_calc(nyquistadj(85000000L)));
#endif /* CTLSTYLE_SW2011ALL */
#if CTLREGMODE_RA4YBO_V1 || CTLREGMODE_RA4YBO_V2 || CTLREGMODE_RA4YBO_V3 || CTLREGMODE_RA4YBO_V3A
	board_set_bandfonuhf(bandf_calc(nyquistadj(111000000L)));
#endif /* CTLREGMODE_RA4YBO_V1 || CTLREGMODE_RA4YBO_V2 || CTLREGMODE_RA4YBO_V3 || CTLREGMODE_RA4YBO_V3A */
#if XVTR_R820T2
	//board_set_bandfxvrtr(bandf_calc(R820T_IFFREQ))	// Этот диапазон подставляется как ПЧ для трансвертора
#endif /* XVTR_R820T2 */
	board_init_chips();	// программирование всех микросхем синтезатора.

#if WITHUSESDCARD
	sdcardhw_initialize();
#endif /* WITHUSESDCARD */
#if WITHUSERAMDISK
	{
		ALIGNX_BEGIN BYTE work [FF_MAX_SS] ALIGNX_END;
		FRESULT rc;
		PRINTF(PSTR("ramdisk: start formatting\n"));
		rc = f_mkfs("0:", NULL, work, sizeof (work));
		if (rc != FR_OK)
		{
			PRINTF(PSTR("ramdisk: f_mkfs failure\n"));
		}
		else
		{
			PRINTF(PSTR("ramdisk: f_mkfs okay\n"));
		}
	}
#endif
#if WITHUSEAUDIOREC
	sdcardinitialize();			// перевод state machine в начальное состояние
#endif /* WITHUSEAUDIOREC */

#if WITHMODEM
	modem_initialze();
#endif /* WITHMODEM */

#if WITHINTEGRATEDDSP	/* в программу включена инициализация и запуск DSP части. */
	dsp_initialize();		// цифровая обработка подготавливается
	InitNoiseReduction();
#if WITHRTTY
	RTTYDecoder_Init();
#endif /* WITHRTTY */

#endif /* WITHINTEGRATEDDSP */

#if WITHUSBHW

	#if WITHISBOOTLOADER && defined (BOARD_IS_USERBOOT)
		if (BOARD_IS_USERBOOT())
		{
			board_usb_activate();		// USB device and host start
			usbactivated = 1;
		}

	#else /* WITHISBOOTLOADER && defined (BOARD_IS_USERBOOT) */
		board_usb_activate();		// USB device and host start
		usbactivated = 1;

	#endif /* WITHISBOOTLOADER && defined (BOARD_IS_USERBOOT) */

#endif /* WITHUSBHW */

	// TODO: у аудио кодека и IF кодека могут быть раные требования
#if defined(CODEC1_TYPE)
	/* требуется ли подача тактирования для инициадизации кодека */
	const int reqclk = board_getaudiocodecif()->clocksneed();
#else /* defined(CODEC1_TYPE) */
	const int reqclk = 1;
#endif /* defined(CODEC1_TYPE) */

	if (reqclk != 0)
	{
		hardware_channels_enable();	// SAI, I2S и подключенная на них периферия
		board_set_i2s_enable(1);	// Разрешить FPGA формирование тактовой частоты для кодеков и тактирование I2S
		board_update();
	}

	board_init_chips2();	// программирование кодеков при подающейся тактовой частоте

	if (reqclk == 0)
	{
		hardware_channels_enable();	// SAI, I2S и подключенная на них периферия
		board_set_i2s_enable(1);	// Разрешить FPGA формирование тактовой частоты для кодеков и тактирование I2S
		board_update();
	}

#if WITHSPISLAVE
	hardware_spi_slave_enable(SPIC_MODE3);
#endif /* WITHSPISLAVE */
}

#if WITHSPISLAVE
// основной цикл программы при работе в режиме управляемого извне DSP
static void
dspcontrol_mainloop(void)
{
	PRINTF(PSTR("dspcontrol_mainloop started.\n"));

	board_update();
#if 0
	// Тест производительности.
	// при запрещённых прерываниях смотрим выхолную частоту на выводе процессора
	// и сравниваем с тем, что стало при разрешённых прерываниях.
	system_disableIRQ();
	for (;;)
	{
		local_delay_ms(50);
		arm_hardware_pioa_outputs(1, 1);
		local_delay_ms(50);
		arm_hardware_pioa_outputs(1, 0);
	}
#endif

	for (;;)
	{
		board_update();	// Все произошедшие изменения (обработка прерываний по приёму управляющих блоков от HOST-контроллера воспринимаются здесь.
		__WFI();
	}
}
#endif /* WITHSPISLAVE */

#if CTLSTYLE_V1H

static void df(
	uint_fast32_t f
	)
{
	char s [21];
	local_snprintf_P(s, 21, PSTR("%10lu.%1lu"), f / 1000, (f % 10) / 100);
	display_at(0, 0, s);
}

static void
hamradio_mainloop_vh1(void)
{
	const uint_fast8_t tx = 0;
	uint_fast32_t dfreq = 435000000UL;
	synth_lo1_setfrequ(0, dfreq, getlo1div(tx));
	df(dfreq);
	for (;;)
	{
		int nrotate = 0;
		int jumpsize = 1;
		int gstep = 12500;

		uint_fast8_t key;
		const uint_fast8_t repeat = kbd_scan(& key);
		if (repeat != 0)
		{
			switch (key)
			{
			case KBD_CODE_4:
				jumpsize = 20;
				nrotate = -1;
				break;
			case KBD_CODE_5:
				nrotate = -1;
				break;
			case KBD_CODE_6:
				nrotate = +1;
				break;
			case KBD_CODE_7:
				jumpsize = 20;
				nrotate = +1;
				break;
			}
		}

		if (nrotate < 0)
		{
			/* вращали "вниз" */
			//const uint_fast32_t lowfreq = bandsmap [b].bottom;
			dfreq = prevfreq(dfreq, dfreq - ((uint_fast32_t) gstep * jumpsize * - nrotate), gstep, RFSGTUNE_BOTTOM);

			//gfreqs [bi] = prevfreq(gfreqs [bi], gfreqs [bi] - (jumpsize * - nrotate), gstep, TUNE_BOTTOM);

			df(dfreq);
			synth_lo1_setfrequ(0, dfreq, getlo1div(tx));
			continue;
		}
		if (nrotate > 0)
		{
			/* вращали "вверх" */
			//const uint_fast32_t topfreq = bandsmap [b].top;
			dfreq = nextfreq(dfreq, dfreq + ((uint_fast32_t) gstep * jumpsize * nrotate), gstep, RFSGTUNE_TOP);

			//gfreqs [bi] = nextfreq(gfreqs [bi], gfreqs [bi] + (jumpsize * nrotate), gstep, TUNE_TOP);

			df(dfreq);
			synth_lo1_setfrequ(0, dfreq, getlo1div(tx));
			continue;
		}
	}
}
#endif /* CTLSTYLE_V1H */

// инициализация машины состояний тюнера
static void hamradio_tune_initialize(void)
{
}

// работа в машине состояний тюнера
// возврат STTE_OK для перехода на следующее состояние
// возврат STTE_BUSY - продолжаем тут
static STTE_t hamradio_tune_step(void)
{
#if WITHAUTOTUNER
	processtxrequest();	/* Установка сиквенсору запроса на передачу.	*/
	display2_redrawbarstimed(0, 0, NULL);		/* обновление динамической части отображения - обновление S-метра или SWR-метра и volt-метра. */
	updateboard(1, 0);
	auto_tune();
	reqautotune = 0;
	updateboard(1, 0);
#endif /* WITHAUTOTUNER */
	return STTE_OK;
}

// инициализация машины состояний меню
static void hamradio_menu_initialize(void)
{
}

// работа в машине состояний меню
static STTE_t hamradio_menu_step(void)
{
	return STTE_OK;
}

enum
{
	//STHTL_IDLE,
	STHRL_RXTX,
	STHRL_RXTX_FQCHANGED,
	STHRL_TUNE,
	STHRL_MENU,
	//
	STHRL_count
};

static uint_fast8_t sthrl = STHRL_RXTX;

// инициализация главной машины состояний
static void hamradio_main_initialize(void)
{
	// начальная инициализация
	
	seq_purge();

#if FQMODEL_GEN500
	gfreqs [getbankindex_raw(0)] = 434085900UL;
	gfreqs [getbankindex_raw(1] = 434085900UL;
#endif /* FQMODEL_GEN500 */

#if WITHUSEAUDIOREC
	sdcardbgprocess();
#endif /* WITHUSEAUDIOREC */
#if WITHUSBHW
	board_usbh_polling();     // usb device polling
#endif /* WITHUSBHW */
	directctlupdate(0, NULL);		/* управление скоростью передачи (и другими параметрами) через потенциометр */
	updateboard(1, 1);	/* полная перенастройка (как после смены режима) - режим приема */
	updateboard2();			/* настройки валкодера и цветовой схемы дисплея. */
	display2_bgreset();

#if 0 && MODEL_MAXLAB
	// тестирование алгоритма для MAXLAB
	{
		unsigned n;
		uint_fast32_t f;

		f = 5500000;
		for (n = 0; n < 1000; n += 50)
			synth_lo1_setfreq(f + n. getlo1div(tx));
		f = 5999000;
		for (n = 0; n < 1000; n += 50)
			synth_lo1_setfreq(f + n, getlo1div(tx));
		//for (;;)
		//	;
	}
#endif

#if FQMODEL_GEN500
	{
		const spitarget_t target = targetpll2;		/* addressing to chip */
		
		void prog_cmx992_print(spitarget_t target), prog_cmx992_initialize(spitarget_t target);

		//prog_cmx992_initialize(target);
		for (;;)
			prog_cmx992_print(target);

		display_redrawfreqmodesbarsnow(0);	// Обновление дисплея - всё, включая частоту

		//prog_pll1_init();
		synth_lo1_setfreq((434085900UL - 10700000UL) / 3. getlo1div(gtx));
		for (;;)
			;
	}
#endif /* FQMODEL_GEN500 */


	encoder_clear();

	/* начальное отображение */
	display_redrawfreqmodesbarsnow(0, NULL);	// Обновление дисплея - всё, включая частоту

}

// работа в главной машине состояний
static STTE_t
hamradio_main_step(void)
{
	//void r820t_spool(void);
	//r820t_spool();
	uint_fast8_t kbch, kbready;

	processmessages(& kbch, & kbready, 0, NULL);

	switch (sthrl)
	{
	case STHRL_MENU:
		if (hamradio_menu_step() == STTE_OK)
			sthrl = STHRL_RXTX;
		break;

	case STHRL_TUNE:
		if (hamradio_tune_step() == STTE_OK)
			sthrl = STHRL_RXTX;
		break;

	case STHRL_RXTX_FQCHANGED:
			/* валкодер перестал вращаться - если было изменение частоты - ообновляем отображение */
			if (display_refreshenabled_freqs())
			{
				processtxrequest();	/* Установка сиквенсору запроса на передачу.	*/
				#if 1//WITHSPLIT
					const uint_fast8_t bi_main = getbankindex_ab_fordisplay(0);		/* состояние выбора банков может измениться */
					const uint_fast8_t bi_sub = getbankindex_ab_fordisplay(1);		/* состояние выбора банков может измениться */
				#elif WITHSPLITEX
					const uint_fast8_t bi_main = getbankindex_ab(0);		/* состояние выбора банков может измениться */
					const uint_fast8_t bi_sub = getbankindex_ab(1);		/* состояние выбора банков может измениться */
				#endif /* WITHSPLIT, WITHSPLITEX */
			/* в случае внутренней памяти микроконтроллера - частоту не запоминать (очень мал ресурс). */

	#if (NVRAM_TYPE != NVRAM_TYPE_CPUEEPROM)
				savebandfreq(getvfoindex(bi_main), bi_main);		/* сохранение частоты в текущем VFO */
				savebandfreq(getvfoindex(bi_sub), bi_sub);		/* сохранение частоты в текущем VFO */
	#endif
				sthrl = STHRL_RXTX;

#if ! LCDMODE_LTDC
				display_freqpair();
				display_refreshperformed_freqs();
#endif /* ! LCDMODE_LTDC */
				board_wakeup();
				break;
			}
			// проваливаемся дальше

	case STHRL_RXTX:
		// работа с пользователем
		{
			processtxrequest();	/* Установка сиквенсору запроса на передачу.	*/
			#if 1//WITHSPLIT
				const uint_fast8_t bi_main = getbankindex_ab_fordisplay(0);		/* состояние выбора банков может измениться */
				const uint_fast8_t bi_sub = getbankindex_ab_fordisplay(1);		/* состояние выбора банков может измениться */
			#elif WITHSPLITEX
				const uint_fast8_t bi_main = getbankindex_ab(0);		/* состояние выбора банков может измениться */
				const uint_fast8_t bi_sub = getbankindex_ab(1);		/* состояние выбора банков может измениться */
			#endif /* WITHSPLIT, WITHSPLITEX */
			#if WITHAUTOTUNER
			if (reqautotune != 0)
			{
				sthrl = STHRL_TUNE;
			}
			#endif /* WITHAUTOTUNER */

			display2_redrawbarstimed(0, 0, NULL);		/* обновление динамической части отображения - обновление S-метра или SWR-метра и volt-метра. */

	#if WITHLFM && defined (LO1MODE_DIRECT)
			if (lfmmode && spool_lfm_enable)
			{
				testlfm();
			}
	#endif
			if (alignmode)
			{
	#if MULTIVFO
				// вход в режим настройки ГУНов первого гетеродина
				display2_bgreset();
				vfoallignment();
				display2_bgreset();
				display_redrawfreqmodesbarsnow(0);			/* Обновление дисплея - всё, включая частоту */
				updateboard(1, 1);	/* полная перенастройка (как после смены режима) */
	#endif // MULTIVFO
				alignmode = 0;	// в nvram осталась не-0
			}
	#if WITHUSEAUDIOREC
			if (recmode)
			{
				recmode = 0;
				sdcardrecord();
			}
	#endif /* WITHUSEAUDIOREC */

			uint_fast8_t jumpsize;
			uint_fast8_t jumpsize2;
			int_least16_t nrotate;
			int_least16_t nrotate2;

			/* переход по частоте - шаг берется из gstep */
			#if WITHBBOX && defined (WITHBBOXFREQ)
				nrotate = 0;	// ignore encoder
				nrotate2 = 0;	// ignore encoder
			#else
				nrotate = getRotateHiRes(& jumpsize, genc1div * gencderate);
				nrotate2 = getRotateHiRes2(& jumpsize2, genc2div);
			#endif

			if (uif_encoder2_rotate(nrotate2))
			{
				nrotate2 = 0;
				display_redrawfreqmodesbarsnow(0, NULL);			/* Обновление дисплея - всё, включая частоту */
			}
	#if WITHDEBUG
			{
				/* здесь можно добавить обработку каких-либо команд с debug порта */
				char c;
				if (dbg_getchar(& c))
				{
					switch (c)
					{
					case 0x00:
						break;
					default:
						PRINTF("key=%02X\n", (unsigned char) c);
						break;
		#if WITHUSBHOST_HIGHSPEEDULPI
					case 'u':
						PRINTF("hkey:\n");
						ulpi_chip_debug();
						break;
		#endif /* WITHUSBHOST_HIGHSPEEDULPI */
		#if WITHWAVPLAYER || WITHSENDWAV
					case 'p':
						PRINTF(PSTR("Play test file\n"));
						playwavfile("1.wav");
						break;
		#endif /* WITHWAVPLAYER */
					}
				}
			}
	#endif /* WITHDEBUG */
	#if WITHKEYBOARD
			if (kbready != 0 && processkeyboard(kbch))
			{
				/* обновление индикатора без сохранения состояния диапазона */
				encoder_clear();				/* при возможном уменьшении шага исключение случайного накопления */
				display_redrawfreqmodesbarsnow(0, NULL);			/* Обновление дисплея - всё, включая частоту */
			} // end keyboard processing
	#endif /* WITHKEYBOARD */

			if (processmodem())
			{
				/* обновление индикатора без сохранения состояния диапазона */
				display_redrawfreqmodesbarsnow(0, NULL);			/* Обновление дисплея - всё, включая частоту */
			} // end keyboard processing

			//auto int marker;
			//PRINTF(PSTR("M0:@%p %02x %08lx!\n"), & marker, INTC.ICCRPR, __get_CPSR());

		
			if (lockmode == 0)
			{
				uint_fast8_t freqchanged = 0;

				/* Обработка накопленного количества импульсов от валкодера */
				if (nrotate < 0)
				{
					/* Валкодер A: вращали "вниз" */
					//const uint_fast32_t lowfreq = bandsmap [b].bottom;
					gfreqs [bi_main] = prevfreq(gfreqs [bi_main], gfreqs [bi_main] - ((uint_fast32_t) gstep * jumpsize * - nrotate), gstep, tune_bottom(bi_main));
					//gfreqs [bi_main] = prevfreq(gfreqs [bi_main], gfreqs [bi_main] - (jumpsize * - nrotate), gstep, TUNE_BOTTOM);
					freqchanged = 1;
				}
				else if (nrotate > 0)
				{
					/* Валкодер A: вращали "вверх" */
					//const uint_fast32_t topfreq = bandsmap [b].top;
					gfreqs [bi_main] = nextfreq(gfreqs [bi_main], gfreqs [bi_main] + ((uint_fast32_t) gstep * jumpsize * nrotate), gstep, tune_top(bi_main));
					//gfreqs [bi_main] = nextfreq(gfreqs [bi_main], gfreqs [bi_main] + (jumpsize * nrotate), gstep, TUNE_TOP);
					freqchanged = 1;
				}
				if (nrotate2 < 0)
				{
					/* Валкодер B: вращали "вниз" */
					//const uint_fast32_t lowfreq = bandsmap [b].bottom;
					gfreqs [bi_sub] = prevfreq(gfreqs [bi_sub], gfreqs [bi_sub] - ((uint_fast32_t) gstep * jumpsize2 * - nrotate2), gstep, tune_bottom(bi_sub));
					//gfreqs [bi_sub] = prevfreq(gfreqs [bi_sub], gfreqs [bi_sub] - (jumpsize2 * - nrotate2), gstep, TUNE_BOTTOM);
					freqchanged = 1;
				}
				else if (nrotate2 > 0)
				{
					/* Валкодер B: вращали "вверх" */
					//const uint_fast32_t topfreq = bandsmap [b].top;
					gfreqs [bi_sub] = nextfreq(gfreqs [bi_sub], gfreqs [bi_sub] + ((uint_fast32_t) gstep * jumpsize2 * nrotate2), gstep, tune_top(bi_sub));
					//gfreqs [bi_sub] = nextfreq(gfreqs [bi_sub], gfreqs [bi_sub] + (jumpsize2 * nrotate2), gstep, TUNE_TOP);
					freqchanged = 1;
				}
				if (freqchanged != 0)
				{
					// Ограничение по скорости обновления дисплея уже заложено в него
					sthrl = STHRL_RXTX_FQCHANGED;
					updateboard(0, 0);	/* частичная перенастройка - без смены режима работы */
				}
			}

#if 0 && (CPUSTYLE_XC7Z || CPUSTYLE_XCZU)		// тестовая прокрутка частоты
			hamradio_set_freq(hamradio_get_freq_rx() + 1);
#endif
		}
		break;

	default:
		break;
	}
	return STTE_OK;
}

#if WITHSPKMUTE
uint_fast8_t hamradio_get_gmutespkr(void)
{
	return gmutespkr;
}

void hamradio_set_gmutespkr(uint_fast8_t v)
{
	gmutespkr = v != 0;
	save_i8(RMT_MUTELOUDSP_BASE, gmutespkr);
	updateboard(1, 0);
}
#endif /* WITHSPKMUTE */

#if WITHIF4DSP

uint_fast16_t hamradio_get_afgain(void)
{
	return afgain1.value;
}

#if ! WITHPOTAFGAIN
void hamradio_set_afgain(uint_fast16_t v)
{
	ASSERT(v >= BOARD_AFGAIN_MIN);
	ASSERT(v <= BOARD_AFGAIN_MAX);

	afgain1.value = v;
	save_i16(offsetof(struct nvmap, afgain1), afgain1.value);
	updateboard(1, 0);
}

#endif /* ! WITHPOTAFGAIN */

#endif /* WITHIF4DSP */

#if WITHTX

void hamradio_set_tune(uint_fast8_t v)
{
	tunemode = v != 0;
	updateboard(1, 1);
}

#if WITHPOWERTRIM

#if WITHLOWPOWEREXTTUNE

void hamradio_set_tx_tune_power(uint_fast8_t v)
{
	ASSERT(v >= WITHPOWERTRIMMIN);
	ASSERT(v <= WITHPOWERTRIMMAX);
	gtunepower = v;
	save_i8(offsetof(struct nvmap, gtunepower), gtunepower);
	updateboard(1, 0);
}

uint_fast8_t hamradio_get_tx_tune_power(void)
{
	return gtunepower;
}

#endif /* WITHLOWPOWEREXTTUNE */

void hamradio_set_tx_power(uint_fast8_t v)
{
	ASSERT(v >= WITHPOWERTRIMMIN);
	ASSERT(v <= WITHPOWERTRIMMAX);
	gnormalpower.value = v;
	save_i8(offsetof(struct nvmap, gnormalpower), gnormalpower.value = v);
	updateboard(1, 0);
}

uint_fast8_t hamradio_get_tx_power(void)
{
	return gnormalpower.value;
}

void hamradio_get_tx_power_limits(uint_fast8_t * min, uint_fast8_t * max)
{
	* min = WITHPOWERTRIMMIN;
	* max = WITHPOWERTRIMMAX;
}

#else /* WITHPOWERTRIM */

void hamradio_set_tx_power(uint_fast8_t v)
{
}

uint_fast8_t hamradio_get_tx_power(void)
{
	return 0;
}

#endif /* WITHPOWERTRIM */

#endif /* WITHTX */

uint_fast8_t hamradio_verify_freq_bands(uint_fast32_t freq, uint_fast32_t * bottom, uint_fast32_t * top)
{
	for (uint_fast8_t i = 0; i < HBANDS_COUNT; i++)
	{
		if (freq >= get_band_bottom(i) && freq <= get_band_top(i) && get_band_bandset(i) == BANDSETF_HAM)
		{
			* bottom = get_band_bottom(i);
			* top = get_band_top(i);
			return 1;					// частота внутри любительского диапазона
		}
	}
	* bottom = 0;
	* top = 0;
	return 0; 							// частота вне любительских диапазонов
}

#if WITHVOX

void hamradio_set_gvoxenable(uint_fast8_t v)
{
	gvoxenable = v != 0;
	save_i8(offsetof(struct nvmap, gvoxenable), gvoxenable);
	updateboard(1, 0);
}

uint_fast8_t hamradio_get_gvoxenable(void)
{
	return gvoxenable;
}

void hamradio_get_vox_delay_limits(uint_fast8_t * min, uint_fast8_t * max)
{
	* min = WITHVOXDELAYMIN;
	* max = WITHVOXDELAYMAX;
}

uint_fast8_t hamradio_get_vox_delay(void)
{
	return voxdelay;
}

void hamradio_set_vox_delay(uint_fast8_t v)
{
	ASSERT(v >= WITHVOXDELAYMIN);
	ASSERT(v <= WITHVOXDELAYMAX);
	voxdelay = v;
	save_i8(offsetof(struct nvmap, voxdelay), voxdelay);
	updateboard(1, 0);
}

void hamradio_get_vox_level_limits(uint_fast8_t * min, uint_fast8_t * max)
{
	* min = WITHVOXLEVELMIN;
	* max = WITHVOXLEVELMAX;
}

uint_fast8_t hamradio_get_vox_level(void)
{
	return gvoxlevel;
}

void hamradio_set_vox_level(uint_fast8_t v)
{
	ASSERT(v >= WITHVOXLEVELMIN);
	ASSERT(v <= WITHVOXLEVELMAX);
	gvoxlevel = v;
	save_i8(offsetof(struct nvmap, gvoxlevel), gvoxlevel);
	updateboard(1, 0);
}

void hamradio_get_antivox_delay_limits(uint_fast8_t * min, uint_fast8_t * max)
{
	* min = WITHAVOXLEVELMIN;
	* max = WITHAVOXLEVELMAX;
}

uint_fast8_t hamradio_get_antivox_level(void)
{
	return gavoxlevel;
}

void hamradio_set_antivox_level(uint_fast8_t v)
{
	ASSERT(v >= WITHAVOXLEVELMIN);
	ASSERT(v <= WITHAVOXLEVELMAX);
	gavoxlevel = v;
	save_i8(offsetof(struct nvmap, gavoxlevel), gavoxlevel);
	updateboard(1, 0);
}

#endif /* WITHVOX */

#if WITHIF4DSP

#if WITHREVERB

void hamradio_get_reverb_delay_limits(uint_fast8_t * min, uint_fast8_t * max)
{
	* min = WITHREVERBDELAYMIN;
	* max = WITHREVERBDELAYMAX;
}

void hamradio_get_reverb_loss_limits(uint_fast8_t * min, uint_fast8_t * max)
{
	* min = WITHREVERBLOSSMIN;
	* max = WITHREVERBLOSSMAX;
}

uint_fast8_t hamradio_get_reverb_delay(void)
{
	return greverbdelay;
}

uint_fast8_t hamradio_get_reverb_loss(void)
{
	return greverbloss;
}

void hamradio_set_reverb_delay(uint_fast8_t v)
{
	ASSERT(v >= WITHREVERBDELAYMIN);
	ASSERT(v <= WITHREVERBDELAYMAX);
	greverbdelay = v;
	save_i8(offsetof(struct nvmap, greverbdelay), greverbdelay);
	updateboard(1, 0);
}

void hamradio_set_reverb_loss(uint_fast8_t v)
{
	ASSERT(v >= WITHREVERBLOSSMIN);
	ASSERT(v <= WITHREVERBLOSSMAX);
	greverbloss = v;
	save_i8(offsetof(struct nvmap, greverbloss), greverbloss);
	updateboard(1, 0);
}

void hamradio_set_greverb(uint_fast8_t v)
{
	greverb = v != 0;
	save_i8(offsetof(struct nvmap, greverb), greverb);
	updateboard(1, 0);
}

uint_fast8_t hamradio_get_greverb(void)
{
	return greverb;
}

#endif /* WITHREVERB */

void hamradio_set_gmoniflag(uint_fast8_t v)
{
	gmoniflag = v != 0;
	save_i8(offsetof(struct nvmap, gmoniflag), gmoniflag);
	updateboard(1, 0);
}

uint_fast8_t hamradio_get_gmoniflag(void)
{
	return gmoniflag;
}

#endif /* WITHIF4DSP */

#if WITHAFCODEC1HAVEPROC

void hamradio_get_mic_clip_limits(uint_fast8_t * min, uint_fast8_t * max)
{
	* min = WITHMIKECLIPMIN;
	* max = WITHMIKECLIPMAX;
}

uint_fast8_t hamradio_get_gmikehclip(void)
{
	return gmikehclip;
}

void hamradio_set_gmikehclip(uint_fast8_t v)
{
	ASSERT(v >= WITHMIKECLIPMIN);
	ASSERT(v <= WITHMIKECLIPMAX);
	gmikehclip = v;
	save_i8(offsetof(struct nvmap, gmikehclip), gmikehclip);
	updateboard(1, 0);
}

void hamradio_get_mic_level_limits(uint_fast8_t * min, uint_fast8_t * max)
{
	* min = WITHMIKEINGAINMIN;
	* max = WITHMIKEINGAINMAX;
}

uint_fast8_t hamradio_get_mik1level(void)
{
	return gmik1level;
}

void hamradio_set_mik1level(uint_fast8_t v)
{
	ASSERT(v >= WITHMIKEINGAINMIN);
	ASSERT(v <= WITHMIKEINGAINMAX);
	gmik1level = v;
	save_i8(offsetof(struct nvmap, gmik1level), gmik1level);
	updateboard(1, 0);
}

uint_fast8_t hamradio_get_gmikeagc(void)
{
	return gmikeagc;
}

void hamradio_set_gmikeagc(uint_fast8_t v)
{
	gmikeagc = v != 0;
	save_i8(offsetof(struct nvmap, gmikeagc), gmikeagc);
	updateboard(1, 0);
}

void hamradio_get_mic_agc_limits(uint_fast8_t * min, uint_fast8_t * max)
{
	* min = WITHMIKEAGCMIN;
	* max = WITHMIKEAGCMAX;
}

uint_fast8_t hamradio_get_gmikeagcgain(void)
{
	return gmikeagcgain;
}

void hamradio_set_gmikeagcgain(uint_fast8_t v)
{
	ASSERT(v >= WITHMIKEINGAINMIN);
	ASSERT(v <= WITHMIKEINGAINMAX);
	gmikeagcgain = v;
	save_i8(offsetof(struct nvmap, gmikeagcgain), gmikeagcgain);
	updateboard(1, 0);
}

uint_fast8_t hamradio_get_gmikeboost20db(void)
{
	return gmikeboost20db;
}

void hamradio_set_gmikeboost20db(uint_fast8_t v)
{
	gmikeboost20db = v != 0;
	save_i8(offsetof(struct nvmap, gmikeboost20db), gmikeboost20db);
	updateboard(1, 0);
}

uint_fast8_t hamradio_get_gmikeequalizer(void)
{
	return gmikeequalizer;
}

void hamradio_set_gmikeequalizer(uint_fast8_t v)
{
	gmikeequalizer = v != 0;
	save_i8(offsetof(struct nvmap, gmikeequalizer), gmikeequalizer);
	updateboard(1, 0);
}

uint_fast8_t hamradio_get_gmikeequalizerparams(uint_fast8_t i)
{
	ASSERT(i < HARDWARE_CODEC1_NPROCPARAMS);
	return gmikeequalizerparams [i];
}

void hamradio_set_gmikeequalizerparams(uint_fast8_t i, uint_fast8_t v)
{
	ASSERT(i < HARDWARE_CODEC1_NPROCPARAMS);
	ASSERT(v <= EQUALIZERBASE * 2);
	gmikeequalizerparams [i] = v;
	//save_i8(offsetof(struct nvmap, gmoniflagxxx), gmoniflagxxx);
	updateboard(1, 0);
}

int_fast32_t hamradio_getequalizerbase(void)
{
	return getequalizerbase();
}
#endif /* WITHAFCODEC1HAVEPROC */

int_fast16_t hamradio_if_shift(int_fast8_t step)
{
#if WITHIFSHIFT

	if (step != 0)
	{
		int_fast16_t val = ifshifoffset.value + step * ISTEP50;

		if (val < IFSHIFTTMIN)
			val = IFSHIFTTMIN;

		if (val > IFSHIFTMAX)
			val = IFSHIFTMAX;

		ifshifoffset.value = val;
		updateboard(1, 0);
	}
	return ifshifoffset.value + getifshiftbase();	// Добавить учет признака наличия сдвига

#else /* WITHIFSHIFT */

	return 0;

#endif /* WITHIFSHIFT */
}

#if WITHELKEY

uint_fast8_t hamradio_get_cw_wpm(void)
{
	return elkeywpm.value;
}
#endif /* WITHELKEY */

void hamradio_set_lockmode(uint_fast8_t lock)
{
	lockmode = lock != 0;
	save_i8(RMT_LOCKMODE_BASE, lockmode);
	updateboard(1, 0);
}

uint_fast8_t hamradio_set_freq(uint_fast32_t freq)
{
	if (freqvalid(freq, gtx))
	{
		const uint_fast8_t bi = getbankindex_tx(gtx);
		gfreqs [bi] = freq;
		sthrl = STHRL_RXTX_FQCHANGED;
		updateboard(0, 0);
		return 1;
	}
	return 0;
}

#if WITHNOTCHFREQ

uint_fast8_t hamradio_get_gnotch(void)
{
	return gnotch;
}

void hamradio_set_gnotch(uint_fast8_t v)
{
	gnotch = v != 0;
	updateboard(1, 0);
}

uint_fast8_t hamradio_get_gnotchtype(void)
{
	return notchmodes [gnotchtype].code;
}

void hamradio_set_gnotchtype(uint_fast8_t v)
{
	gnotchtype = v;
	updateboard(1, 0);
}

uint_fast16_t hamradio_notch_freq(int_fast8_t step)
{
	if (step != 0)
	{
		uint_fast16_t val = gnotchfreq.value + step * ISTEP50;

		if (val < WITHNOTCHFREQMIN)
			val = WITHNOTCHFREQMIN;

		if (val > WITHNOTCHFREQMAX)
			val = WITHNOTCHFREQMAX;

		gnotchfreq.value = val;
		updateboard(1, 0);
	}
	return gnotchfreq.value;
}

uint_fast16_t hamradio_notch_width(int_fast8_t step)
{
	if (step != 0)
	{
		uint_fast16_t val = gnotchwidth.value + step * ISTEP50;

		if (val < WITHNOTCHWIDTHMIN)
			val = WITHNOTCHWIDTHMIN;

		if (val > WITHNOTCHWIDTHMAX)
			val = WITHNOTCHWIDTHMAX;

		gnotchwidth.value = val;
		updateboard(1, 0);
	}
	return gnotchwidth.value;
}

#endif /* WITHNOTCHFREQ */

#if (WITHSWRMTR || WITHSHOWSWRPWR)
uint_fast8_t hamradio_get_gsmetertype(void)
{
	return gsmetertype;
}
#endif /* (WITHSWRMTR || WITHSHOWSWRPWR) */

#if WITHSPECTRUMWF && WITHMENU
const char * hamradio_change_view_style(uint_fast8_t v)
{
	uint_fast16_t menupos;
	const char * name = "VIEW STL";

	for (menupos = 0; menupos < MENUROW_COUNT; ++ menupos)
	{
		const FLASHMEM struct menudef * const mp = & menutable [menupos];
		if ((mp->qspecial & ITEM_VALUE) == 0)
			continue;

		if (! strcmp(name, mp->qlabel))
			break;
	}

	const FLASHMEM struct menudef * const mp = & menutable [menupos];

	if (v)
	{
		uint_fast8_t * const pv8 = mp->qpval8;
		* pv8 = (* pv8 + 1) % (mp->qupper + 1);
		gviewstyle = * pv8;
		updateboard(1, 0);
	}

#if (NVRAM_TYPE != NVRAM_TYPE_CPUEEPROM)
		savemenuvalue(mp);		/* сохраняем отредактированное значение */
#endif

	dctx_t dctx;
	dctx.type = DCTX_MENU;
	dctx.pv = mp;
	display2_menu_valxx(0, 0, & dctx);

	return menuw;
}

uint_fast8_t hamradio_get_gzoomxpow2(void)
{
	const uint_fast8_t bi = getbankindex_ab_fordisplay(0);		/* VFO A modifications */
	return gzoomxpow2 [bi];
}

void hamradio_set_gzoomxpow2(uint_fast8_t v)
{
	const uint_fast8_t bi = getbankindex_ab_fordisplay(0);	/* VFO A modifications */
	ASSERT(v <= BOARD_FFTZOOM_POW2MAX);
	gzoomxpow2 [bi] = v;
	// сохранение зависит от текущего диапазона
	save_i8(nvramoffs_band_a(offsetof(struct nvmap, bands [0].gzoomxpow2)), gzoomxpow2 [bi]);
	updateboard(1, 0);
}

uint_fast8_t hamradio_get_gwflevelsep(void)
{
	return gwflevelsep;
}

void hamradio_set_gwflevelsep(uint_fast8_t v)
{
	gwflevelsep = v != 0;
	save_i8(offsetof(struct nvmap, gwflevelsep), gwflevelsep);
	updateboard(1, 0);
}

uint_fast8_t hamradio_gtopdbsp(int_fast8_t v)
{
	const uint_fast8_t bi = getbankindex_ab_fordisplay(0);	/* VFO A modifications */
	if (v > 0)
		gtopdbspe [bi] = calc_next(gtopdbspe [bi], WITHTOPDBMIN, WITHTOPDBMAX);
	else if (v < 0)
		gtopdbspe [bi] = calc_prev(gtopdbspe [bi], WITHTOPDBMIN, WITHTOPDBMAX);

	if (v != 0)
	{
		save_i8(nvramoffs_band_a(offsetof(struct nvmap, bands [0].gtopdbspe)), gtopdbspe [bi]);
		updateboard(1, 0);
	}

	return gtopdbspe [bi];
}

uint_fast8_t hamradio_gbottomdbsp(int_fast8_t v)
{
	const uint_fast8_t bi = getbankindex_ab_fordisplay(0);	/* VFO A modifications */
	if (v > 0)
		gbottomdbspe [bi] = calc_next(gbottomdbspe [bi], WITHBOTTOMDBMIN, WITHBOTTOMDBMAX);
	else if (v < 0)
		gbottomdbspe [bi] = calc_prev(gbottomdbspe [bi], WITHBOTTOMDBMIN, WITHBOTTOMDBMAX);

	if (v != 0)
	{
		save_i8(nvramoffs_band_a(offsetof(struct nvmap, bands [0].gbottomdbspe)), gbottomdbspe [bi]);
		updateboard(1, 0);
	}

	return gbottomdbspe [bi];
}

uint_fast8_t hamradio_gtopdbwf(int_fast8_t v)
{
	const uint_fast8_t bi = getbankindex_ab_fordisplay(0);	/* VFO A modifications */
	if (v > 0)
		gtopdbwfl [bi] = calc_next(gtopdbwfl [bi], WITHTOPDBMIN, WITHTOPDBMAX);
	else if (v < 0)
		gtopdbwfl [bi] = calc_prev(gtopdbwfl [bi], WITHTOPDBMIN, WITHTOPDBMAX);

	if (v != 0)
	{
		save_i8(nvramoffs_band_a(offsetof(struct nvmap, bands [0].gtopdbwfl)), gtopdbwfl [bi]);
		updateboard(1, 0);
	}

	return gtopdbwfl [bi];
}

uint_fast8_t hamradio_gbottomdbwf(int_fast8_t v)
{
	const uint_fast8_t bi = getbankindex_ab_fordisplay(0);	/* VFO A modifications */
	if (v > 0)
		gbottomdbwfl [bi] = calc_next(gbottomdbwfl [bi], WITHBOTTOMDBMIN, WITHBOTTOMDBMAX);
	else if (v < 0)
		gbottomdbwfl [bi] = calc_prev(gbottomdbwfl [bi], WITHBOTTOMDBMIN, WITHBOTTOMDBMAX);

	if (v != 0)
	{
		save_i8(nvramoffs_band_a(offsetof(struct nvmap, bands [0].gbottomdbwfl)), gbottomdbwfl [bi]);
		updateboard(1, 0);
	}

	return gbottomdbwfl [bi];
}

#endif /* WITHSPECTRUMWF && WITHMENU */

uint_fast8_t hamradio_get_att_db(void)
{
	const uint_fast8_t bi = getbankindex_ab_fordisplay(0);	/* VFO A modifications */
	return attmodes [gatts [bi]].atten10 / 10;
}

uint_fast8_t hamradio_get_att_dbs(uint_fast8_t * values, uint_fast8_t limit)
{
	const uint_fast8_t bi = getbankindex_ab_fordisplay(0);	/* vfo A bank index */
	for (uint_fast8_t i = 0; i < ATTMODE_COUNT; i ++)
	{
		if ( i > limit)
			break;

		values [i] = attmodes [i].atten10;
	}

	return ATTMODE_COUNT;
}

void hamradio_set_att_db(uint_fast8_t db)
{
	const uint_fast8_t bi = getbankindex_ab_fordisplay(0);	/* VFO A modifications */
	const vindex_t vi = getvfoindex(bi);

	verifyband(vi);

	gatts [bi] = db;
	savebandstate(vi, bi);	// запись всех режимов в область памяти диапазона
	updateboard(1, 0);
}

const char * hamradio_get_att_value(void)
{
	const uint_fast8_t bi = getbankindex_ab_fordisplay(0);	/* VFO A modifications */
	return attmodes [gatts [bi]].label;
}

const char * hamradio_get_preamp_value(void)
{
	const uint_fast8_t bi = getbankindex_ab_fordisplay(0);	/* VFO A modifications */
#if ! WITHONEATTONEAMP
	return pampmodes [gpamps [bi]].label;
#else
	return attmodes [gatts [bi]].label;
#endif
}

#if WITHUSEDUALWATCH
uint_fast8_t hamradio_split_toggle(void)
{
	if (gsplitmode == VFOMODES_VFOINIT)
		uif_key_spliton(0);
	else
		uif_key_splitoff();

	return gsplitmode != 0;
}

void hamradio_split_mode_toggle(void)
{
	uif_key_mainsubrx();
}
#endif /* WITHUSEDUALWATCH */

// основной цикл программы при работе в режиме любительского премника
static void
hamradio_mainloop(void)
{
	hamradio_main_initialize();
	for (;;)
	{
		hamradio_main_step();
	}
}

#if 0

static volatile uint16_t f255, f255cnt, f255period;

void beacon_255(void)
{
	if (++ f255cnt == f255period)
	{
		f255cnt = 0;
		f255 = 1;
	}
}

static const FLASHMEM char textbeacon [] =
	"00"
	"1011001101010100110010110100101011001010110101010011010011001100"
	"1010110011010101010101001101001011010010110101001011001011001100"
	"1010110011010011001011010010110100110100101100101100101101001101"
	"01010011001100101101001011001010101101010011010"
	"00"
	;

static unsigned textbeaconindex = 0;

static void ff256cycle(unsigned long f, unsigned period)
{
	f255period = period;
	f255cnt = 0;

	synth_lo1_setfreq(f, 1);
	for (;;)
	{
		while (f255 == 0)
			;
		f255 = 0;
		if (textbeacon [textbeaconindex] == '\0')
		{
			textbeaconindex = 0;
			return;
		}
		prog_dds1_setlevel(100 * (textbeacon [textbeaconindex ++] != '0'));
	}

}

struct band
{
	unsigned long freq;
	unsigned period;
};

static void
hamradio_mainloop_beacon(void)
{
	uint8_t ifreq;
	static const struct band ffs [] =
	{
		{ 3548500L + 50,	256, },	
		{ 7039500L + 105, 	256, },	//14063710L,
		{ 10136500L + 140, 	256, },	//14063710L,
		{ 14063500L + 210, 	256, },	//14063710L,
		{ 18106500L + 270, 	256, },	//14063710L,
		{ 21075500L + 315, 	256, },	//14063710L,
		{ 24926500L + 390, 	256, },	
		{ 28071500L + 420, 	256, },	
	};

	hardware_timer_initialize(1000);
	updateboard(1, 1);	/* полная перенастройка (как после смены режима) - режим приема */
	for (ifreq = 0; ; )
	{
		ff256cycle(ffs [ifreq].freq, ffs [ifreq].period);
		if (++ ifreq >= (sizeof ffs / sizeof ffs [0]))
			ifreq = 0;
	}
}

#endif

#if 0 && WITHDEBUG

static void local_gets(char * buff, size_t len)
{
	size_t pos = 0;

	for (;;)
	{
		char c;
		if (dbg_getchar(& c))
		{
			if (c == '\r')
			{
				dbg_putchar('\n');
				buff [pos] = '\0';
				return;
			}
			if ((pos + 1) >= len)
				continue;
			if (isprint((unsigned char) c))
			{
				buff [pos ++] = c;
				dbg_putchar(c);
				continue;
			}
			if (pos != 0 && c == '\b')
			{
				PRINTF(PSTR("\b \b"));
				-- pos;
				continue;
			}
		}
	}
}

static void siggen_mainloop(void)
{

	PRINTF(PSTR("RF Signal generator\n"));
	uint_fast8_t tx = 0;
	// signal-generator tests
	board_set_attvalue(0);
	updateboard(1, 0);
	for (;;)
	{
		PRINTF(PSTR("Enter tx=%d, command (a#/g/n):\n"), tx);
		char buff [132];
		local_gets(buff, sizeof buff / sizeof buff [0]);
		char * cp = buff;
		while (isspace((unsigned char) * cp))
			++ cp;
		switch (tolower((unsigned char) * cp))
		{
		case 'a':
			// set att value
			++ cp;
			unsigned long value = strtoul(cp, NULL, 10);
			PRINTF(PSTR("RFSG ATT value: %lu\n"), value);
			if (value < 63)
			{
				board_set_attvalue(value);
				updateboard(1, 0);
			}
			break;
		case 'g':
			// generaton on
			PRINTF(PSTR("RFSG output ON\n"));
			tx = 1;
			updateboard(1, 0);
			break;
		case 'n':
			// generator off
			PRINTF(PSTR("RFSG output OFF\n"));
			tx = 0;
			updateboard(1, 0);
			break;
		}
	}
}
#endif

#if WITHISBOOTLOADER

struct stm32_header {
	uint32_t magic_number;
	uint8_t image_signature[64];
	uint32_t image_checksum;
	uint8_t  header_version[4];
	uint32_t image_length;
	uint32_t image_entry_point;
	uint32_t reserved1;
	uint32_t load_address;
	uint32_t reserved2;
	uint32_t version_number;
	uint32_t option_flags;
	uint32_t ecdsa_algorithm;
	uint8_t ecdsa_public_key[64];
	uint8_t padding[83];
	uint8_t binary_type;
} ATTRPACKED;

#define HEADER_MAGIC	0x324d5453  //	__be32_to_cpu(0x53544D32)

static uint_fast8_t bootloader_get_start(
		uintptr_t apparea,	/* целевой адрес для загрузки образа - здесь лежит заголовок файла */
		uintptr_t * ip)
{
	volatile struct stm32_header * const hdr = (volatile struct stm32_header *) apparea;
	uint_fast32_t checksum = hdr->image_checksum;
	uint_fast32_t length = hdr->image_length;
	const uint8_t * p = (const uint8_t *) hdr->load_address;
	if (hdr->magic_number != HEADER_MAGIC)
		return 1;
	* ip = hdr->image_entry_point;
	while (length --)
		checksum -= * p ++;
	return checksum != 0;	// возврат 0 если контрольная сумма совпала
}

static uint_fast8_t bootloader_copyapp(
		uint_fast32_t appoffset,	/* смещение заголовка приожения в накопителе */
		uintptr_t * ip
		)
{
	enum { HEADERSIZE = 256 };
	static uint8_t tmpbuff [HEADERSIZE];
	volatile struct stm32_header * const hdr = (volatile struct stm32_header *) tmpbuff;

	bootloader_readimage(appoffset, tmpbuff, HEADERSIZE);
	//printhex(appoffset, tmpbuff, HEADERSIZE);
	if (hdr->magic_number != HEADER_MAGIC)
		return 1;
	* ip = hdr->image_entry_point;
	PRINTF("bootloader_copyapp: ip=%08lX (addr=%08lX, len=%08lX)\n", (unsigned long) * ip, hdr->load_address, hdr->image_length);
	bootloader_readimage(appoffset + HEADERSIZE, (void *) hdr->load_address, hdr->image_length);
	PRINTF("bootloader_copyapp done.\n");
	return 0;
}

// Сюда попадаем из USB DFU клвсса при приходе команды
// DFU_Detach после USBD_Stop
static void
bootloader_launch_app(uintptr_t ip)
{
	__disable_irq();
#if WITHUSBHW
		board_usb_deinitialize();
#endif /* WITHUSBHW */
	arm_hardware_flush_all();

#if (__L2C_PRESENT == 1)
	L2C_Disable();
#endif


#if (__GIC_PRESENT == 1)
	// keep enabled foe CPU1 start
	//GIC_DisableInterface();
	//GIC_DisableDistributor();

	// Disable all IRQs
	{
		// Get ITLinesNumber
		const unsigned n = ((GIC_DistributorInfo() & 0x1f) + 1) * 32;
		unsigned i;
		// 32 - skip SGI handlers (keep enabled for CPU1 start).
		for (i = 32; i < n; ++ i)
			IRQ_Disable(i);
	}
#endif

#if (__CORTEX_A != 0)

	MMU_Disable();
	MMU_InvalidateTLB();
	__ISB();
	__DSB();
	(* (void (*)(void)) ip)();

#endif

	for (;;)
		;
}

/* Вызов заказан вызывется из обработчика USB прерываний EP0 */
void bootloader_deffereddetach(void * arg)
{
#if defined (USBD_DFU_RAM_LOADER)
	uintptr_t ip;
	if (bootloader_get_start(USBD_DFU_RAM_LOADER, & ip) == 0)
	{
		PRINTF("bootloader_deffereddetach: ip=%08lX\n", (unsigned long) ip);
		/* Perform an Attach-Detach operation on USB bus */
#if WITHUSBHW
		if (usbactivated)
			board_usb_deactivate();
#endif /* WITHUSBHW */
		bootloader_launch_app(ip);
	}
	else
	{
		PRINTF("bootloader_deffereddetach: Header is not loaded to %08lX.\n", (unsigned long) USBD_DFU_RAM_LOADER);
	}
#endif /* defined (USBD_DFU_RAM_LOADER) */
}

#if CPUSTYLE_XC7Z || CPUSTYLE_XCZU	// мигалка

static unsigned volatile tmpressed;
static unsigned volatile pressflag;

static unsigned refreshtimer;
static unsigned volatile refreshevent;

static unsigned s1timer;
static unsigned volatile s1event;

static unsigned s01timer;
static unsigned volatile s01event;
enum { REFRESHPERIODmS = 500 };

static unsigned tcpiptimer;
static unsigned volatile tcpipevent;

static int getrefresh(void)
{
	unsigned v;

	system_disableIRQ();
	v = refreshevent;
	refreshevent = 0;
	system_enableIRQ();

	return v;
}


static void
tsc_spool(void * ctx)
{
	{
		unsigned t = tmpressed;
		if (t != 0)
			tmpressed = t - 1;
	}
	// обновление крана
	{
		unsigned t = refreshtimer;
		if (++ t >= NTICKS(REFRESHPERIODmS))
		{
			refreshevent = 1;
			refreshtimer = 0;
		}
		else
		{
			refreshtimer = t;
		}
	}
	// обработка LWIP
	{
		unsigned t = tcpiptimer;
		if (++ t >= NTICKS(250))
		{
			tcpipevent = 1;
			tcpiptimer = 0;
		}
		else
		{
			tcpiptimer = t;
		}
	}
//	{
//		unsigned t = s1timer;
//		if (++ t >= NTICKS(1000))
//		{
//			++ abstime;
//			s1event = 1;
//			s1timer = 0;
//		}
//		else
//		{
//			s1timer = t;
//		}
//	}
}

#endif /* CPUSTYLE_XC7Z || CPUSTYLE_XCZU */

#if WITHISBOOTLOADERFATFS

static void bootloader_fatfs_mainloop(void)
{
	static const char IMAGENAME [] = WITHISBOOTLOADERIMAGE;
	static FATFSALIGN_BEGIN BYTE header [sizeof (struct stm32_header)] FATFSALIGN_END;
	static RAMNOINIT_D1 FATFS Fatfs;		/* File system object  - нельзя располагать в Cortex-M4 CCM */
	static RAMNOINIT_D1 FIL Fil;			/* Описатель открытого файла - нельзя располагать в Cortex-M4 CCM */
	FRESULT rc;
	UINT br = 0;		//  количество считанных байтов
	struct stm32_header * const hdr = (struct stm32_header *) & header;

	board_set_bglight(1, gbglight);	// выключить подсветку
	board_update();
	PRINTF("bootloader_fatfs_mainloop start: '%s'\n", IMAGENAME);

	static BYTE targetdrv = 0;
	DSTATUS st = disk_initialize (targetdrv);				/* Physical drive nmuber (0..) */
	if (st != RES_OK)
	{
		PRINTF("disk_initialize code=%02X\n", st);
		PRINTF(" STA_NOINIT = %d\n", STA_NOINIT);
		PRINTF(" STA_NODISK = %d\n", STA_NODISK);
		PRINTF(" STA_PROTECT = %d\n", STA_PROTECT);
		for (;;)
			;
	}
	f_mount(& Fatfs, "", 0);		/* Register volume work area (never fails) */
	// чтение файла
	rc = f_open(& Fil, IMAGENAME, FA_READ);
	if (rc != FR_OK)
	{
		PRINTF("Can not open file '%s'\n", IMAGENAME);
		PRINTF("Failed with rc=%u.\n", rc);
		for (;;)
			;
	}
	rc = f_read(& Fil, header, sizeof header, & br);	/* Read a chunk of file */
	if (rc != FR_OK || br != sizeof (header))
	{
		PRINTF("Can not read header of file '%s'\n", IMAGENAME);
		PRINTF("Failed with rc=%u.\n", rc);
		for (;;)
			;
	}

	uint_fast32_t length = hdr->image_length;
	const uint8_t * p = (const uint8_t *) hdr->load_address;
	if (hdr->magic_number != HEADER_MAGIC)
	{
		PRINTF("Wrong header of file '%s'\n", IMAGENAME);
		for (;;)
			;
	}
	rc = f_read(& Fil, (BYTE *) hdr->load_address, hdr->image_length, & br);	/* Read a chunk of file */
	if (rc != FR_OK || br != hdr->image_length)
	{
		PRINTF("Can not read body of file '%s', rc=%d, hdr->image_length=%08lX, br=%08lX\n", IMAGENAME, (int) rc, (unsigned long) hdr->image_length, (unsigned long) br);
		PRINTF("Failed with rc=%u.\n", rc);
		for (;;)
			;
	}
	uint_fast32_t checksum = hdr->image_checksum;
	while (length --)
		checksum -= * p ++;
	if (checksum != 0)
	{
		PRINTF("Wrong body checksum of file '%s'\n", IMAGENAME);
		for (;;)
			;
	}
	rc = f_close(& Fil);
	if (rc != FR_OK)
	{
		PRINTF("Can not close file '%s'\n", IMAGENAME);
		PRINTF("Failed with rc=%u.\n", rc);
		for (;;)
			;
	}

#if BOOTLOADER_RAMSIZE
	uintptr_t ip;
	if (bootloader_get_start((uintptr_t) header, & ip) != 0)	/* проверка сигнатуры и получение стартового адреса */
	{
		PRINTF("bootloader_fatfs_mainloop start: can not load '%s'\n", IMAGENAME);
		for (;;)
			;
	}
#else
	ASSERT(0);
	for (;;)
		;
#endif /* BOOTLOADER_RAMSIZE */
#if WITHUSBHW
	board_usb_deactivate();
#endif /* WITHUSBHW */
#if BOOTLOADER_RAMSIZE
	PRINTF("bootloader_fatfs_mainloop start: run '%s' at %08lX\n", IMAGENAME, ip);
#if WITHDEBUG
	local_delay_ms(100);
#endif /* WITHDEBUG */
	bootloader_launch_app(ip);
#endif /* BOOTLOADER_RAMSIZE */
}

#else /* WITHISBOOTLOADERFATFS */

static void bootloader_mainloop(void)
{
	PRINTF("bootloader_mainloop:\n");
	board_set_bglight(1, gbglight);	// выключить подсветку
	board_update();

#if BOOTLOADER_RAMSIZE && defined (BOARD_IS_USERBOOT)

	if (BOARD_IS_USERBOOT() == 0)
	{
		/* Нет запроса на вход в режим загрузчика - грузим с QSPI FLASH */
		do
		{
			uintptr_t ip;
			if (bootloader_copyapp(BOOTLOADER_SELFSIZE, & ip) != 0)	/* копирование исполняемого образа (если есть) в требуемое место */
			{
				PRINTF("bootloader_mainloop: No application image\n");
				break;
			}
	#if WITHUSBHW
			if (usbactivated)
				board_usb_deactivate();
	#endif /* WITHUSBHW */
			PRINTF("bootloader_mainloop: ip=%08lX\n", (unsigned long) ip);
			bootloader_launch_app(ip);

		} while (0);
	}
#endif /* BOOTLOADER_RAMSIZE && defined (BOARD_IS_USERBOOT) */

	PRINTF("bootloader_mainloop: loop\n");
	/* Обеспечение работы USB DFU */
	for (;;)
	{
		uint_fast8_t kbch, kbready;
		processmessages(& kbch, & kbready, 0, NULL);
	}
}
#endif /* WITHISBOOTLOADERFATFS */

#endif /* WITHISBOOTLOADER */

/* Главная функция программы */
int 
//__attribute__ ((used))
main(void)
{
#if CPUSTYLE_ARM
	sysinit_gpio_initialize();
#endif /* CPUSTYLE_ARM */
#if WITHDEBUG && (! CPUSTYLE_ARM /* || WITHISBOOTLOADER */)

	HARDWARE_DEBUG_INITIALIZE();
	HARDWARE_DEBUG_SET_SPEED(DEBUGSPEED);

#endif /* WITHDEBUG && ! CPUSTYLE_ARM */
	lowtests();		/* функции тестирования, работающие до инициализации периферии */

	global_disableIRQ();
	cpu_initialize();		// в случае ARM - инициализация прерываний и контроллеров, AVR - запрет JTAG
	lowinitialize();	/* вызывается при запрещённых прерываниях. */
	global_enableIRQ();
	cpump_runuser();	/* остальным ядрам разрешаем выполнять прерывания */
	midtests();
	// Инициализируем то что не получается иниитить в описании перменных.
#if WITHTX
	/* запись значений по умолчанию для корректировок мощности в завивимости от диапазона ФНЧ УМ */
	bandf2adjust_initialize();
#endif /* WITHTX */
	initialize2();	/* вызывается при разрешённых прерываниях. */
#if WITHLWIP
	//network_initialize();
#endif /* WITHLWIP */
	hamradio_initialize();
	hightests();		/* подпрограммы для тестирования аппаратуры */

#if WITHISBOOTLOADER && WITHISBOOTLOADERFATFS
	bootloader_fatfs_mainloop();
#elif WITHISBOOTLOADER
	bootloader_mainloop();
#elif 0
	siggen_mainloop();
#elif 0
	hamradio_mainloop_beacon();
#elif WITHSPISLAVE
	dspcontrol_mainloop();
#elif CTLSTYLE_V1H
	hamradio_mainloop_vh1();
#else /* WITHSPISLAVE */
	hamradio_mainloop();
#endif /* WITHSPISLAVE */
	return 0;
}
