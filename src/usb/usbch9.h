/* $Id$ */
/*
*   usbch9.h
*
*   Def's for USB device communications stack - "Standard Request" layer,
*   conforming to USB 2.0 specification, Chapter 9.
*
*   Adapted from Philips PDIUSBD12 firmware library (CHAP_9.H)
*   MJB Nov.2005
*/


#ifndef __CHAP_9_H__
#define __CHAP_9_H__

#include "hardware.h"

#define UNICODE_ENGLISH     (0x0409)    /* US_English (Ref: USB_LANGIDs.pdf) */


#if CPUSTYLE_R7S721
	//  Renesas parameters
	#define HARDWARE_USBD_PIPE_ISOC_OUT	1	// ISOC OUT Аудиоданные от компьютера в TRX - D0FIFOB0
	#define HARDWARE_USBD_PIPE_ISOC_IN	2	// ISOC IN Аудиоданные в компьютер из TRX - D0FIFOB1

#if WITHUSBCDCACMINTSHARING
	#define HARDWARE_USBD_PIPE_CDC_INTSHARED	6	// CDC ACM shared interrupt endpoint
#else /* WITHUSBCDCACMINTSHARING */
	#define HARDWARE_USBD_PIPE_CDC_INT	6	//
	#define HARDWARE_USBD_PIPE_CDC_INTb	7	//
#endif /* WITHUSBCDCACMINTSHARING */

	#define HARDWARE_USBD_PIPE_CDC_OUT	3	// CDC OUT Данные ком-порта от компьютера в TRX
	#define HARDWARE_USBD_PIPE_CDC_IN	4	// CDC IN Данные ком-порта в компьютер из TRX
	#define HARDWARE_USBD_PIPE_CDC_OUTb	14	// CDC OUT - без передачи данных
	#define HARDWARE_USBD_PIPE_CDC_INb	15	// CDC IN - без передачи данных
	#define HARDWARE_USBD_PIPE_RNDIS_OUT	12	// RNDIS OUT Данные RNDIS от компьютера в TRX
	#define HARDWARE_USBD_PIPE_RNDIS_IN		13	// RNDIS IN Данные RNDIS в компьютер из TRX
	#define HARDWARE_USBD_PIPE_RNDIS_INT	8	//

	/* совпадает с RNDIS */
	#define HARDWARE_USBD_PIPE_CDCEEM_OUT	12	// CDC EEM OUT Данные ком-порта от компьютера в TRX
	#define HARDWARE_USBD_PIPE_CDCEEM_IN	13	// CDC EEM IN Данные ком-порта в компьютер из TRX

#endif /* CPUSTYLE_R7S721 */

// STM32F429:
//	valid EPs: dcp, 0x01/0x81, 0x02/0x82, 0x03/0x83 
// STM32F446, STM32F746:
//	valid EPs: dcp, 0x01/0x81, 0x02/0x82, 0x03/0x83, 0x04/0x84, 0x05/0x85

// IN and INT Endpoints allocation
enum
{
	ep0inxxx = 0x80,

#if WITHUSBRNDIS
	USBD_EP_RNDIS_INT,
	USBD_EP_RNDIS_IN,
#endif /* WITHUSBRNDIS */

#if WITHUSBUACIN
	USBD_EP_AUDIO_IN,	// ISOC IN Аудиоданные в компьютер из TRX
	#if WITHUSBUACIN2
		USBD_EP_RTS_IN,	// ISOC IN Аудиоданные в компьютер из TRX
	#endif
#endif /* WITHUSBUACIN */

#if WITHUSBCDCACM

	#if WITHUSBCDCACMINTSHARING
		/* Использование общей notification endpoint на всех CDC ACM устрйоствах */
		USBD_EP_CDCACM_IN,		// CDC IN Данные ком-порта в компьютер из TRX
		USBD_EP_CDCACM_INlast = USBD_EP_CDCACM_IN + WITHUSBCDCACM_N - 1,
		USBD_EP_CDCACM_INTSHARED,	// Shared EP: CDC INT События ком-порта в компьютер из TRX

	#else /* WITHUSBCDCACMINTSHARING */

		USBD_EP_CDCACM_IN,		// CDC IN Данные ком-порта в компьютер из TRX
		USBD_EP_CDCACM_INT,		// CDC IN Данные ком-порта в компьютер из TRX
		USBD_EP_CDCACM_INlast = USBD_EP_CDCACM_IN + WITHUSBCDCACM_N * 2 - 1,

	#endif /* WITHUSBCDCACMINTSHARING */
#endif /* WITHUSBCDCACM */

#if WITHUSBCDCEEM
	USBD_EP_CDCEEM_IN,	// CDC IN Данные ком-порта в компьютер из TRX
#endif /* WITHUSBCDCEEM */

#if WITHUSBCDCECM
	USBD_EP_CDCECM_INT,	// CDC INT События ком-порта в компьютер из TRX
	USBD_EP_CDCECM_IN,	// CDC IN Данные ком-порта в компьютер из TRX
#endif /* WITHUSBCDCECM */

#if WITHUSBHID
	//USBD_EP_HIDMOUSE_INT,	// HID INT События манипулятора в компьютер из TRX
#endif /* WITHUSBHID */

#if WITHUSBDFU
		/* no endpoints need */
#endif /* WITHUSBDFU */
	//
	epincount
};

// OUT Endpoints allocation
enum
{
	ep0outxxx = 0x00,
	epoutbase = (epincount - 1) & 0x7F,

#if WITHUSBRNDIS
	USBD_EP_RNDIS_OUT,
#endif /* WITHUSBRNDIS */

#if WITHUSBUACOUT
	USBD_EP_AUDIO_OUT,	// ISOC OUT Аудиоданные от компьютера в TRX
#endif /* WITHUSBUACOUT */

#if WITHUSBCDCACM
	USBD_EP_CDCACM_OUT,	// CDC OUT Данные ком-порта от компьютера в TRX
	USBD_EP_CDCACM_OUTlast = USBD_EP_CDCACM_OUT + WITHUSBCDCACM_N - 1,
#endif /* WITHUSBCDCACM */

#if WITHUSBCDCEEM
	USBD_EP_CDCEEM_OUT,	// CDC OUT Данные ком-порта от компьютера в TRX
#endif /* WITHUSBCDCEEM */

#if WITHUSBCDCECM
	USBD_EP_CDCECM_OUT,	// CDC OUT Данные ком-порта от компьютера в TRX
#endif /* WITHUSBCDCECM */

#if WITHUSBHID
#endif /* WITHUSBHID */

#if WITHUSBDFU
		/* no endpoints need */
#endif /* WITHUSBDFU */
	//
	epoutcount
};

#if WITHUSBCDCACM
	#define VIRTUAL_COM_PORT_INT_SIZE 			10
	#if 0 && WITHUSBDEV_HSDESC
		#define VIRTUAL_COM_PORT_IN_DATA_SIZE			USB_OTG_HS_MAX_PACKET_SIZE
		#define VIRTUAL_COM_PORT_OUT_DATA_SIZE			USB_OTG_HS_MAX_PACKET_SIZE
	#else /* WITHUSBDEV_HSDESC */
		#define VIRTUAL_COM_PORT_IN_DATA_SIZE			USB_OTG_FS_MAX_PACKET_SIZE
		#define VIRTUAL_COM_PORT_OUT_DATA_SIZE			USB_OTG_FS_MAX_PACKET_SIZE
	#endif /* WITHUSBDEV_HSDESC */
#endif /* WITHUSBCDCACM */

#if WITHUSBHID
	#define HIDMOUSE_INT_DATA_SIZE 4
#endif /* WITHUSBHID */

#if WITHUSBCDCEEM
	#if WITHUSBDEV_HSDESC
		#define USBD_CDCEEM_BUFSIZE			USB_OTG_HS_MAX_PACKET_SIZE
	#else /* WITHUSBDEV_HSDESC */
		#define USBD_CDCEEM_BUFSIZE			USB_OTG_FS_MAX_PACKET_SIZE
	#endif /* WITHUSBDEV_HSDESC */
#endif /* WITHUSBCDCEEM */

#if WITHUSBCDCECM
	#define USBD_CDCECM_INT_SIZE	8	// interrupt data
	#if WITHUSBDEV_HSDESC
		#define USBD_CDCECM_IN_BUFSIZE			USB_OTG_HS_MAX_PACKET_SIZE
		#define USBD_CDCECM_OUT_BUFSIZE			USB_OTG_HS_MAX_PACKET_SIZE
	#else /* WITHUSBDEV_HSDESC */
		#define USBD_CDCECM_IN_BUFSIZE			USB_OTG_FS_MAX_PACKET_SIZE
		#define USBD_CDCECM_OUT_BUFSIZE			USB_OTG_FS_MAX_PACKET_SIZE
	#endif /* WITHUSBDEV_HSDESC */
#endif /* WITHUSBCDCECM */

#if WITHUSBRNDIS
	#define USBD_RNDIS_INT_SIZE	8	// interrupt data
	#if WITHUSBDEV_HSDESC
		#define USBD_RNDIS_IN_BUFSIZE			USB_OTG_HS_MAX_PACKET_SIZE
		#define USBD_RNDIS_OUT_BUFSIZE			USB_OTG_HS_MAX_PACKET_SIZE
	#else /* WITHUSBDEV_HSDESC */
		#define USBD_RNDIS_IN_BUFSIZE			USB_OTG_FS_MAX_PACKET_SIZE	// bulk data
		#define USBD_RNDIS_OUT_BUFSIZE			USB_OTG_FS_MAX_PACKET_SIZE	// bulk data
	#endif /* WITHUSBDEV_HSDESC */
#endif /* WITHUSBRNDIS */


//#elif CPUSTYLE_STM32F4XX || CPUSTYLE_STM32F7XX
//#else
//	#error Unsupported USB hardware 
//#endif

// Descriptor IDs
// Идентификаторы могут быть произвольными числами (кроме нуля)
// Применена автоматическая нумерация терминалов в каждой цепочке.
enum
{
	TERMINAL_ID_UNDEFINED = 0,

	TERMINAL_UACIN48 = 16,	// fill_UAC2_IN48_function, fill_UAC1_IN48_function
	TERMINAL_UACOUT48 = 32,	// fill_UACOUT48_function
	TERMINAL_UACIN48_UACINRTS = 48,	// fill_UACIN48_function or fill_UACIN48_INRTS_function
	TERMINAL_UACINRTS = 64,	// fill_UACINRTS_function

	TERMINAL_ID_SELECTOR_6 = 80,	// Input selector - вынесено из-за неудобства использования: значение проверяется

	TERMINAL_ID_FU1_IN = 96,
	TERMINAL_ID_FU1_OUT = 112,
	TERMINAL_ID_CLKSOURCE = 128,

	TERMINAL_ID_FU2_IN = 144,
	TERMINAL_ID_FU2_OUT = 160,


	TERMINAL_ID_CLKMULTIPLIER_UACINOUT = 176,	// shared in/our clock source
	TERMINAL_ID_CLKMULTIPLIER_UACIN48_UACINRTS,
	TERMINAL_ID_CLKMULTIPLIER_UACINRTS,
	TERMINAL_ID_CLKMULTIPLIER_UACIN48,
	TERMINAL_ID_CLKMULTIPLIER_UACOUT48,

	TERMINAL_ID_count_unused
};

#define MAX_TERMINALS_IN_INTERFACE 4

#define TERMINAL_ID_SELECTOR_6_INPUTS 2

#define WITHPLAINDESCROPTOR	1		/* не используется множество конфигураций */

#if WITHPLAINDESCROPTOR

#if WITHUSBUAC
	#if WITHUSBUACIN2
		//#define INTERFACE_UAC_count 2	/* количество интерфейсов в одном UAC */
	#else /* WITHUSBUACIN2 */
		//#define INTERFACE_UAC_count 3	/* количество интерфейсов в одном UAC */
	#endif /* WITHUSBUACIN2 */
#endif /* WITHUSBUAC */

/* Последовательность функций в данном enum должна соответствовать порядку использования в fill_Configuration_main_group */
enum interfaces_tag
{
#if WITHUSBDFU && WITHMOVEDFU
	INTERFACE_DFU_CONTROL,		/* DFU control Interface */
#endif /* WITHUSBDFU */

#if WITHUSBCDCACM
	// функция виртуального последовательного порта
	INTERFACE_CDC_base,
	INTERFACE_CDC_CONTROL_3a = INTERFACE_CDC_base,	/* CDC ACM control Interface */
	INTERFACE_CDC_DATA_4a,		/* CDC ACM data Interface */
	INTERFACE_CDC_last = INTERFACE_CDC_base + WITHUSBCDCACM_N * 2 - 1,
#endif /* WITHUSBCDCACM */

#if WITHUSBUAC
	#if WITHUSBUACIN && WITHUSBUACOUT && WITHUSBUACINOUTRENESAS
		// функция звукового устройства
		/* совмещённое усройство ввода/вывода (и спектр измененем параметров устройства) */
		INTERFACE_AUDIO_CONTROL_MIKE,		/* AUDIO receiever out control interface */
		INTERFACE_AUDIO_MIKE,		/* USB Microphone Standard AS Interface Descriptor (Alt. Set. 0) (CODE == 3)*/ //zero-bandwidth interface
		#define INTERFACE_AUDIO_CONTROL_SPK 2222
		//#define INTERFACE_AUDIO_CONTROL_RTS 2223
		INTERFACE_AUDIO_SPK,			/* USB Speaker Standard AS Interface Descriptor - Audio Streaming Zero Bandwith */
	#elif WITHUSBUACIN && WITHUSBUACOUT && WITHUSBUACINOUT
		// версия с совмещённым устройством IN/OUT
		INTERFACE_AUDIO_CONTROL_MIKE,		/* AUDIO receiever out / transmitter input control interface */
		INTERFACE_AUDIO_MIKE,		/* USB Microphone Standard AS Interface Descriptor (Alt. Set. 0) (CODE == 3)*/ //zero-bandwidth interface
		INTERFACE_AUDIO_SPK,			/* USB Speaker Standard AS Interface Descriptor - Audio Streaming Zero Bandwith */
		#define INTERFACE_AUDIO_CONTROL_SPK 2222
		#if WITHRTS96 || WITHRTS192
			INTERFACE_AUDIO_CONTROL_RTS,		/* AUDIO spectrum control interface */
			INTERFACE_AUDIO_RTS,		/* USB spectrum Standard AS Interface Descriptor (Alt. Set. 0) (CODE == 3)*/ //zero-bandwidth interface
		#endif /* WITHRTS96 || WITHRTS192 */
	#else /* WITHUSBUACIN && WITHUSBUACOUT && WITHUSBUACINOUT */
		#if WITHUSBUACIN2
			INTERFACE_AUDIO_CONTROL_MIKE,		/* AUDIO receiever out control interface */
			INTERFACE_AUDIO_MIKE,		/* USB receiver output  Standard AS Interface Descriptor (Alt. Set. 0) (CODE == 3)*/ //zero-bandwidth interface
			INTERFACE_AUDIO_CONTROL_RTS,		/* AUDIO spectrum control interface */
			INTERFACE_AUDIO_RTS,		/* USB spectrum Standard AS Interface Descriptor (Alt. Set. 0) (CODE == 3)*/ //zero-bandwidth interface
		#elif WITHUSBUACIN
			INTERFACE_AUDIO_CONTROL_MIKE,		/* AUDIO receiever out control interface */
			INTERFACE_AUDIO_MIKE,		/* USB Microphone Standard AS Interface Descriptor (Alt. Set. 0) (CODE == 3)*/ //zero-bandwidth interface
		#endif
		#if WITHUSBUACOUT
			INTERFACE_AUDIO_CONTROL_SPK,		/* AUDIO transmitter input control interface */
			INTERFACE_AUDIO_SPK,			/* USB Speaker Standard AS Interface Descriptor - Audio Streaming Zero Bandwith */
		#endif /* WITHUSBUACOUT */
	#endif /* WITHUSBUACIN && WITHUSBUACOUT && WITHUSBUACINOUT */
#endif /* WITHUSBUAC */

#if WITHUSBRNDIS
	// функция сетевой платы
	INTERFACE_RNDIS_CONTROL,	/* RNDIS control Interface */
	INTERFACE_RNDIS_DATA,		/* RNDIS data Interface */
#endif /* WITHUSBRNDIS */

#if WITHUSBCDCEEM
	// функция сетевой платы
	INTERFACE_CDCEEM_DATA,	/* CDC ECM/CDC EEM data Interface */
#endif /* WITHUSBCDCEEM */

#if WITHUSBCDCECM
	// функция сетевой платы
	INTERFACE_CDCECM_CONTROL,	/* CDC ECM control Interface */
	INTERFACE_CDCECM_DATA,	/* CDC ECM/CDC EEM data Interface */
#endif /* WITHUSBCDCECM */

#if WITHUSBHID
	// функция HID
	INTERFACE_HID_CONTROL,	/* HID control Interface */
#endif /* WITHUSBHID */

#if WITHUSBDFU && ! WITHMOVEDFU
	// функция DFU
	INTERFACE_DFU_CONTROL,		/* DFU control Interface */
#endif /* WITHUSBDFU */
	// 
	INTERFACE_count				/* Значение для configuration descriptor */
};

#define INTERFACE_CDCACM_count 2	/* количество интерфейсов в одном CDC - control & data */
#define INTERFACE_CDCEEM_count 1	/* количество интерфейсов в одном CDC EEM */
#define INTERFACE_CDCECM_count 2	/* количество интерфейсов в одном CDC ECM */
#define INTERFACE_HID_count 1	/* количество интерфейсов в одном HID */
#define INTERFACE_RNDIS_count 2	/* количество интерфейсов в одном RNDIS */
#define INTERFACE_DFU_count 1	/* количество интерфейсов в одном DFU */

//#define INTERFACE_UAC_count (INTERFACE_AUDIO_last - INTERFACE_AUDIO_CONTROL_SPK)



#if WITHUSBCDCACMINTSHARING

	#define USBD_CDCACM_EP(base, offset) ((base) + (offset))
	#define USBD_CDCACM_OFFSET_BY_EP(ep, base) ((ep) - (base))

#else /* WITHUSBCDCACMINTSHARING */

	#define USBD_CDCACM_EP(base, offset) ((base) + (offset) * 2)
	#define USBD_CDCACM_OFFSET_BY_EP(ep, base) (((ep) - (base)) / 2)

#endif /* WITHUSBCDCACMINTSHARING */
#define USBD_CDCACM_IFC(base, offset) ((base) + (offset) * INTERFACE_CDCACM_count)

#else /* WITHPLAINDESCROPTOR */

	#if WITHUSBUAC
		#if WITHUSBUACIN2
			//#define INTERFACE_UAC_count 2	/* количество интерфейсов в одном UAC */
		#else /* WITHUSBUACIN2 */
			//#define INTERFACE_UAC_count 3	/* количество интерфейсов в одном UAC */
		#endif /* WITHUSBUACIN2 */
	#endif /* WITHUSBUAC */

	#if WITHUSBRNDIS
		enum
		{

			INTERFACE_RNDIS_CONTROL,	/* RNDIS control Interface */
			INTERFACE_RNDIS_DATA,		/* RNDIS data Interface */
		};
	#endif /* WITHUSBRNDIS */
	#if WITHUSBUAC
		enum
		{
		#if WITHUSBUACIN && WITHUSBUACOUT && WITHUSBUACINOUTRENESAS
			// функция звукового устройства
			/* совмещённое усройство ввода/вывода (и спектр измененем параметров устройства) */
			INTERFACE_AUDIO_CONTROL_MIKE,		/* AUDIO receiever out control interface */
			INTERFACE_AUDIO_MIKE,		/* USB Microphone Standard AS Interface Descriptor (Alt. Set. 0) (CODE == 3)*/ //zero-bandwidth interface
			#define INTERFACE_AUDIO_CONTROL_SPK 2222
			//#define INTERFACE_AUDIO_CONTROL_RTS 2223
			INTERFACE_AUDIO_SPK,			/* USB Speaker Standard AS Interface Descriptor - Audio Streaming Zero Bandwith */
		#elif WITHUSBUACIN && WITHUSBUACOUT && WITHUSBUACINOUT
			// версия с совмещённым устройством IN/OUT
			INTERFACE_AUDIO_CONTROL_MIKE,		/* AUDIO receiever out / transmitter input control interface */
			INTERFACE_AUDIO_MIKE,		/* USB Microphone Standard AS Interface Descriptor (Alt. Set. 0) (CODE == 3)*/ //zero-bandwidth interface
			INTERFACE_AUDIO_SPK,			/* USB Speaker Standard AS Interface Descriptor - Audio Streaming Zero Bandwith */
			#define INTERFACE_AUDIO_CONTROL_SPK 2222
			#if WITHRTS96 || WITHRTS192
				INTERFACE_AUDIO_CONTROL_RTS,		/* AUDIO spectrum control interface */
				INTERFACE_AUDIO_RTS,		/* USB spectrum Standard AS Interface Descriptor (Alt. Set. 0) (CODE == 3)*/ //zero-bandwidth interface
			#endif /* WITHRTS96 || WITHRTS192 */
		#else /* WITHUSBUACIN && WITHUSBUACOUT && WITHUSBUACINOUT */
			#if WITHUSBUACIN2
				INTERFACE_AUDIO_CONTROL_MIKE,		/* AUDIO receiever out control interface */
				INTERFACE_AUDIO_MIKE,		/* USB receiver output  Standard AS Interface Descriptor (Alt. Set. 0) (CODE == 3)*/ //zero-bandwidth interface
				INTERFACE_AUDIO_CONTROL_RTS,		/* AUDIO spectrum control interface */
				INTERFACE_AUDIO_RTS,		/* USB spectrum Standard AS Interface Descriptor (Alt. Set. 0) (CODE == 3)*/ //zero-bandwidth interface
			#elif WITHUSBUACIN
				INTERFACE_AUDIO_CONTROL_MIKE,		/* AUDIO receiever out control interface */
				INTERFACE_AUDIO_MIKE,		/* USB Microphone Standard AS Interface Descriptor (Alt. Set. 0) (CODE == 3)*/ //zero-bandwidth interface
			#endif
			#if WITHUSBUACOUT
				INTERFACE_AUDIO_CONTROL_SPK,		/* AUDIO transmitter input control interface */
				INTERFACE_AUDIO_SPK,			/* USB Speaker Standard AS Interface Descriptor - Audio Streaming Zero Bandwith */
			#endif /* WITHUSBUACOUT */
		#endif /* WITHUSBUACIN && WITHUSBUACOUT && WITHUSBUACINOUT */
		};
	#endif /* WITHUSBUAC */
	#if WITHUSBCDCACM
		enum
		{

			INTERFACE_CDC_base,
			INTERFACE_CDC_CONTROL_3a = INTERFACE_CDC_base,	/* CDC ACM control Interface */
			INTERFACE_CDC_DATA_4a,		/* CDC ACM data Interface */
			INTERFACE_CDC_last = INTERFACE_CDC_base + WITHUSBCDCACM_N * 2 - 1,
		};
	#endif /* WITHUSBCDCACM */
	#if WITHUSBCDCEEM
		enum
		{
			INTERFACE_CDCEEM_DATA		/* CDC ECM/CDC EEM data Interface */
		};
	#endif /* WITHUSBCDCEEM */
	#if WITHUSBCDCECM
		enum
		{
			INTERFACE_CDCECM_CONTROL,	/* CDC ECM control Interface */
			INTERFACE_CDCECM_DATA		/* CDC ECM/CDC EEM data Interface */
		};
	#endif /* WITHUSBCDCECM */
	#if WITHUSBHID
		enum
		{
			INTERFACE_HID_CONTROL	/* HID control Interface */
			//
		};
	#endif /* WITHUSBHID */

	#if WITHUSBDFU
		enum
		{
			INTERFACE_DFU_CONTROL		/* DFU control Interface */
			//
		};
	#endif /* WITHUSBDFU */

	#define INTERFACE_CDCACM_count 2	/* количество интерфейсов в одном CDC - control & data */
	#define INTERFACE_CDCEEM_count 1	/* количество интерфейсов в одном CDC EEM */
	#define INTERFACE_CDCECM_count 2	/* количество интерфейсов в одном CDC ECM */
	#define INTERFACE_HID_count 1	/* количество интерфейсов в одном HID */
	#define INTERFACE_RNDIS_count 2	/* количество интерфейсов в одном RNDIS */
	#define INTERFACE_DFU_count 1	/* количество интерфейсов в одном DFU */

	enum
	{
		UNUSED_cfgidx = 0,
		// sequence of IDs should be same as used in usbd_descriptors_initialize
		CDCECM_cfgidx = 1,
		RNDIS_cfgidx = 2,
		//
		UNUSED2_cfgidx
	};

	#define INTERFACE_count 4 //(MAX(INTERFACE_RNDIS_count, INTERFACE_CDCECM_count))

#endif /* WITHPLAINDESCROPTOR */


enum
{
	UACINALT_NONE = 0,
	UACINALT_AUDIO48,
#if ! WITHUSBUACIN2
	#if WITHRTS96
		UACINALT_RTS96,
	#endif /* WITHRTS96 */
	#if WITHRTS192
		UACINALT_RTS192,
	#endif /* WITHRTS192 */
#endif /* ! WITHUSBUACIN2 */
	//
	UACINALT_top
};

enum
{
	UACINRTSALT_NONE = 0,
#if WITHUSBUACIN2
	#if WITHRTS96
		UACINRTSALT_RTS96,
	#endif /* WITHRTS96 */
	#if WITHRTS192
		UACINRTSALT_RTS192,
	#endif /* WITHRTS192 */
#endif /* WITHUSBUACIN2 */
	//
	UACINRTSALT_top
};

enum
{
	UACOUTALT_NONE = 0,
	UACOUTALT_AUDIO48,

	UACOUTALTALT_top
};


#endif  /* __CHAP_9_H__ */
