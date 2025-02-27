/* $Id$ */
//
// Проект HF Dream Receiver (КВ приёмник мечты)
// автор Гена Завидовский mgs2001@mail.ru
// UA1ARN
//

#include "hardware.h"	/* зависящие от процессора функции работы с портами */
#include "keyboard.h"

#include <string.h>
#include <math.h>

#include "board.h"

#include "formats.h"	// for debug prints
#include "spi.h"


#if defined(STM32F401xC)


#elif CPUSTYLE_STM32F4XX

#elif CPUSTYLE_STM32F7XX

	#define BOARD_USART1_FREQ (stm32f7xx_get_usart1_freq())
	#define BOARD_USART2_FREQ 	(stm32f7xx_get_apb1_freq())

#elif CPUSTYLE_STM32H7XX

	// See Table 8. Register boundary addresses
	#define BOARD_USART1_FREQ 	(stm32h7xx_get_usart1_6_freq())
	#define BOARD_USART2_FREQ 	(stm32h7xx_get_usart2_to_8_freq())
	#define BOARD_USART3_FREQ 	(stm32h7xx_get_usart2_to_8_freq())

#elif CPUSTYLE_STM32MP1

	//#define BOARD_USART1_FREQ 	(stm32mp1_uart1_get_freq())
	//#define BOARD_USART2_FREQ 	(stm32mp1_uart2_4_get_freq())
	//#define BOARD_USART3_FREQ 	(stm32mp1_uart3_5_get_freq())
	//#define BOARD_UART4_FREQ 	(stm32mp1_uart2_4_get_freq())
	//#define BOARD_UART5_FREQ 	(stm32mp1_uart3_5_get_freq())
	//#define BOARD_USART6_FREQ 	(stm32mp1_usart6_get_freq())
	//#define BOARD_UART7_FREQ 	(stm32mp1_uart7_8_get_freq())
	//#define BOARD_UART8_FREQ 	(stm32mp1_uart7_8_get_freq())

#endif

// Set interrupt vector wrapper
static void serial_set_handler(uint_fast16_t int_id, void (* handler)(void))
{
#if WITHNMEAOVERREALTIME
		arm_hardware_set_handler_overrealtime(int_id, handler);
#else /* WITHNMEAOVERREALTIME */
		arm_hardware_set_handler_system(int_id, handler);
#endif /* WITHNMEAOVERREALTIME */
}


#if WITHNMEA

// Очереди символов для обмена с согласующим устройством
enum { qSZ = 512 };
static uint8_t queue [qSZ];
static volatile unsigned qp, qg;

// Передать символ в host
static uint_fast8_t	qput(uint_fast8_t c)
{
	unsigned qpt = qp;
	const unsigned next = (qpt + 1) % qSZ;
	if (next != qg)
	{
		queue [qpt] = c;
		qp = next;
		HARDWARE_NMEA_ENABLETX(1);
		return 1;
	}
	return 0;
}

// Получить символ в host
static uint_fast8_t qget(uint_fast8_t * pc)
{
	if (qp != qg)
	{
		* pc = queue [qg];
		qg = (qg + 1) % qSZ;
		return 1;
	}
	return 0;
}

// получить состояние очереди передачи
static uint_fast8_t qempty(void)
{
	return qp == qg;
}

// Передать массив символов
static void qputs(const char * s, int n)
{
	while (n --)
		qput(* s ++);
}


/* вызывается из обработчика прерываний */
// компорт готов передавать
void nmea_sendchar(void * ctx)
{
	uint_fast8_t c;
	if (qget(& c))
	{
		HARDWARE_NMEA_TX(ctx, c);
		if (qempty())
			HARDWARE_NMEA_ENABLETX(0);
	}
	else
	{
		HARDWARE_NMEA_ENABLETX(0);
	}
}

int nmea_putc(int c)
{
#if WITHNMEAOVERREALTIME
	global_disableIRQ();
	qput(c);
	global_enableIRQ();
#else /* WITHNMEAOVERREALTIME */
    system_disableIRQ();
    qput(c);
	system_enableIRQ();
#endif /* WITHNMEAOVERREALTIME */
	return c;
}


void nmea_format(const char * format, ...)
{
	char b [256];
	int n, i;
	va_list	ap;
	va_start(ap, format);

	n = vsnprintf(b, sizeof b / sizeof b [0], format, ap);

	for (i = 0; i < n; ++ i)
		nmea_putc(b [i]);

	va_end(ap);
}

/* вызывается из обработчика прерываний */
// произошла потеря символа (символов) при получении данных с CAT компорта
void nmea_rxoverflow(void)
{
}
/* вызывается из обработчика прерываний */
void nmea_disconnect(void)
{

}

#endif /* WITHNMEA */

#if WITHUART1HW 

#if CPUSTYLE_ATSAM3S || CPUSTYLE_ATSAM4S

	void RAMFUNC_NONILINE USART0_Handler(void) 
	{
		const uint_fast32_t csr = USART0->US_CSR;

		if (csr & US_CSR_RXRDY)
			HARDWARE_UART1_ONRXCHAR(USART0->US_RHR);
		if (csr & US_CSR_TXRDY)
			HARDWARE_UART1_ONTXCHAR(USART0);
	}

#elif CPUSTYLE_STM32F1XX || CPUSTYLE_STM32F4XX

	void RAMFUNC_NONILINE USART1_IRQHandler(void)
	{
		const uint_fast32_t sr = USART1->SR;

		if (sr & (USART_SR_RXNE | USART_SR_ORE | USART_SR_FE | USART_SR_NE))
			HARDWARE_UART1_ONRXCHAR(USART1->DR);
		if (sr & (USART_SR_ORE | USART_SR_FE | USART_SR_NE))
			HARDWARE_UART1_ONOVERFLOW();
		if (sr & USART_SR_TXE)
			HARDWARE_UART1_ONTXCHAR(USART1);
	}

#elif CPUSTYLE_STM32F30X || CPUSTYLE_STM32F0XX || CPUSTYLE_STM32L0XX || CPUSTYLE_STM32H7XX || CPUSTYLE_STM32F7XX || CPUSTYLE_STM32MP1

	void RAMFUNC_NONILINE USART1_IRQHandler(void)
	{
		const uint_fast32_t isr = USART1->ISR;

		if (isr & USART_ISR_RXNE)
			HARDWARE_UART1_ONRXCHAR(USART1->RDR);
		if (isr & USART_ISR_ORE)
		{
			USART1->ICR = USART_ICR_ORECF;
			HARDWARE_UART1_ONOVERFLOW();
		}
		if (isr & USART_ISR_FE)
			USART1->ICR = USART_ICR_FECF;
		if (isr & USART_ISR_TXE)
			HARDWARE_UART1_ONTXCHAR(USART1);
	}

#elif CPUSTYLE_AT91SAM7S

	#if HARDWARE_ARM_USEUSART0
		static RAMFUNC_NONILINE void AT91F_US0Handler(void) 
		{
			const uint_fast32_t csr = AT91C_BASE_US0->US_CSR;

			if (csr & AT91C_US_RXRDY)
				HARDWARE_UART1_ONRXCHAR(AT91C_BASE_US0->US_RHR);
			if (csr & AT91C_US_TXRDY)
				HARDWARE_UART1_ONTXCHAR(AT91C_BASE_US0);
		}
	#elif HARDWARE_ARM_USEUSART1
		static RAMFUNC_NONILINE void AT91F_US1Handler(void) 
		{
			const uint_fast32_t csr = AT91C_BASE_US1->US_CSR;

			if (csr & AT91C_US_RXRDY)
				HARDWARE_UART1_ONRXCHAR(AT91C_BASE_US1->US_RHR);
			if (csr & AT91C_US_TXRDY)
				HARDWARE_UART1_ONTXCHAR(AT91C_BASE_US1);
		}
	#else	/* HARDWARE_ARM_USExxx */
		#error Wrong HARDWARE_ARM_USExxx value
	#endif		/* HARDWARE_ARM_USEUSART0 */

#elif CPUSTYLE_ATMEGA328

	ISR(USART_RX_vect)
	{
		HARDWARE_UART1_ONRXCHAR(UDR0);
	}

	ISR(USART_TX_vect)
	{
		HARDWARE_UART1_ONTXCHAR(NULL);
	}

#elif CPUSTYLE_ATMEGA_XXX4

	ISR(USART0_RX_vect)
	{
		HARDWARE_UART1_ONRXCHAR(UDR0);
	}

	ISR(USART0_TX_vect)
	{
		HARDWARE_UART1_ONTXCHAR(NULL);
	}

#elif CPUSTYLE_ATMEGA32

	ISR(USART_RXC_vect)
	{
		HARDWARE_UART1_ONRXCHAR(UDR);
	}

	ISR(USART_TXC_vect)
	{
		HARDWARE_UART1_ONTXCHAR(NULL);
	}

#elif CPUSTYLE_ATMEGA128

	ISR(USART0_RX_vect)
	{
		HARDWARE_UART1_ONRXCHAR(UDR0);
	}

	ISR(USART0_TX_vect)
	{
		HARDWARE_UART1_ONTXCHAR(NULL);
	}

#elif CPUSTYLE_ATXMEGAXXXA4
	
	ISR(USARTE0_RXC_vect)
	{
		HARDWARE_UART1_ONRXCHAR(USARTE0.DATA);
	}

	ISR(USARTE0_DRE_vect)
	{
		HARDWARE_UART1_ONTXCHAR(& USARTE0);
	}

#elif CPUSTYLE_R7S721

	// Приём символа он последовательного порта
	static RAMFUNC_NONILINE void SCIFRXI0_IRQHandler(void)
	{
		(void) SCIF0.SCFSR;						// Перед сбросом бита RDF должно произойти его чтение в ненулевом состоянии
		SCIF0.SCFSR = (uint16_t) ~ SCIF0_SCFSR_RDF;	// RDF=0 читать незачем (в примерах странное - сбрасывабтся и другие биты)
		uint_fast8_t n = (SCIF0.SCFDR & SCIF0_SCFDR_R) >> SCIF0_SCFDR_R_SHIFT;
		while (n --)
			HARDWARE_UART1_ONRXCHAR(SCIF0.SCFRDR & SCIF0_SCFRDR_D);
	}

	// Передача символа в последовательный порт
	static RAMFUNC_NONILINE void SCIFTXI0_IRQHandler(void)
	{
		HARDWARE_UART1_ONTXCHAR(& SCIF0);
	}

#elif CPUSTYLE_XC7Z || CPUSTYLE_XCZU || CPUSTYLE_XCZU


#elif CPUSTYPE_T113

	static RAMFUNC_NONILINE void UART0_IRQHandler(void)
	{
	#if WITHUART1HW
		const uint_fast32_t ier = UART0->DLH_IER;
		const uint_fast32_t usr = UART0->UART_USR;
		if ((UART0->UART_USR & (0x1uL << 3)) == 0)	// RX FIFO Not Empty

		if (ier & (0x01uL << 0))	// ERBFI Enable Received Data Available Interrupt
		{
			if (usr & (0x1uL << 3))	// RX FIFO Not Empty
				HARDWARE_UART1_ONRXCHAR(UART0->DATA);
		}
		if (ier & (0x01uL << 1))	// ETBEI Enable Transmit Holding Register Empty Interrupt
		{
			if (usr & (0x1uL << 1))	// TX FIFO Not Full
				HARDWARE_UART1_ONTXCHAR(UART0);
		}
	#endif /* WITHUART1HW */
	}

	static RAMFUNC_NONILINE void UART1_IRQHandler(void)
	{
	#if WITHUART2HW
		TP();
	#endif /* WITHUART2HW */
	}

	static RAMFUNC_NONILINE void UART2_IRQHandler(void)
	{
	#if WITHUART3HW
		TP();
	#endif /* WITHUART3HW */
	}

	static RAMFUNC_NONILINE void UART3_IRQHandler(void)
	{
	#if WITHUART4HW
		TP();
	#endif /* WITHUART4HW */
	}

	static RAMFUNC_NONILINE void UART4_IRQHandler(void)
	{
	#if WITHUART5HW
		TP();
	#endif /* WITHUART5HW */
	}

	static RAMFUNC_NONILINE void UART5_IRQHandler(void)
	{
	#if WITHUART6HW
		TP();
	#endif /* WITHUART6HW */
	}

#else

	#error Undefined CPUSTYLE_XXX

#endif	/* CPUSTYLE_ATMEGA_XXX4 */
	

/* вызывается из обработчика прерываний или при запрещённых прерываниях. */
/*
		system_disableIRQ();
		system_enableIRQ();
*/
/* Разрешение/запрещение прерывания по передаче символа */
void hardware_uart1_enabletx(uint_fast8_t state)
{
#if CPUSTYLE_ATSAM3S || CPUSTYLE_ATSAM4S

	#if HARDWARE_ARM_USEUSART0

		if (state)
			USART0->US_IER = US_IER_TXRDY;
		else
			USART0->US_IDR = US_IDR_TXRDY;

	#elif HARDWARE_ARM_USEUSART1

		if (state)
			USART1->US_IER = US_IER_TXRDY;
		else
			USART1->US_IDR = US_IDR_TXRDY;

	#elif HARDWARE_ARM_USEUART0

		if (state)
			UART0->UART_IER = UART_IER_TXRDY;
		else
			UART0->UART_IDR = UART_IDR_TXRDY;

	#elif HARDWARE_ARM_USEUART1

		if (state)
			UART1->UART_IER = UART_IER_TXRDY;
		else
			UART1->UART_IDR = UART_IDR_TXRDY;

	#else	/* HARDWARE_ARM_USExxx */
		#error Wrong HARDWARE_ARM_USExxx value
	#endif /* HARDWARE_ARM_USEUSART0 */

#elif CPUSTYLE_STM32F || CPUSTYLE_STM32MP1

	if (state)
		USART1->CR1 |= USART_CR1_TXEIE;
	else
		USART1->CR1 &= ~ USART_CR1_TXEIE;

#elif CPUSTYLE_AT91SAM7S

	#if HARDWARE_ARM_USEUSART0

	if (state)
		AT91C_BASE_US0->US_IER = AT91C_US_TXRDY;
	else
		AT91C_BASE_US0->US_IDR = AT91C_US_TXRDY;

	#elif HARDWARE_ARM_USEUSART1

	if (state)
		AT91C_BASE_US1->US_IER = AT91C_US_TXRDY;
	else
		AT91C_BASE_US1->US_IDR = AT91C_US_TXRDY;

	#else	/* HARDWARE_ARM_USExxx */
		#error Wrong HARDWARE_ARM_USExxx value
	#endif /* HARDWARE_ARM_USEUSART0 */

#elif CPUSTYLE_ATMEGA_XXX4

	/* Used USART 0 */
	if (state)
	{
		UCSR0B |= (1U << TXCIE0);
		HARDWARE_UART1_ONTXCHAR(NULL);	// initiate 1-st character sending
	}
	else
	{
		UCSR0B &= ~ (1U << TXCIE0);
	}

#elif CPUSTYLE_ATMEGA32

	if (state)
	{
		UCSRB |= (1U << TXCIE);
		HARDWARE_UART1_ONTXCHAR(NULL);	// initiate 1-st character sending
	}
	else
	{
		UCSRB &= ~ (1U << TXCIE);
	}

#elif CPUSTYLE_ATMEGA128

	/* Used USART 0 */
	if (state)
	{
		UCSR0B |= (1U << TXCIE0);
		HARDWARE_UART1_ONTXCHAR(NULL);	// initiate 1-st character sending
	}
	else
	{
		UCSR0B &= ~ (1U << TXCIE0);
	}

#elif CPUSTYLE_ATXMEGAXXXA4

	if (state)
		USARTE0.CTRLA = (USARTE0.CTRLA & ~ USART_DREINTLVL_gm) | USART_DREINTLVL_LO_gc;
	else
		USARTE0.CTRLA = (USARTE0.CTRLA & ~ USART_DREINTLVL_gm) | USART_DREINTLVL_OFF_gc;

#elif CPUSTYPE_TMS320F2833X

	if (state)
		SCIACTL2 |= (1U << 0);	// TX INT ENA
	else
		SCIACTL2 &= ~ (1U << 0); // TX INT ENA

#elif CPUSTYLE_R7S721

	if (state)
		SCIF0.SCSCR |= SCIF0_SCSCR_TIE;	// TIE Transmit Interrupt Enable
	else
		SCIF0.SCSCR &= ~ SCIF0_SCSCR_TIE;	// TIE Transmit Interrupt Enable

#elif CPUSTYLE_XC7Z || CPUSTYLE_XCZU


//	if (state)
//		 UART0->CR |= SCIF0_SCSCR_TIE;	// TIE Transmit Interrupt Enable
//	else
//		 UART0->CR &= ~ SCIF0_SCSCR_TIE;	// TIE Transmit Interrupt Enable
//

#elif CPUSTYPE_T113

	if (state)
		 UART0->DLH_IER |= (0x01uL << 1);	// ETBEI Enable Transmit Holding Register Empty Interrupt
	else
		 UART0->DLH_IER &= ~ (0x01uL << 1);	// ETBEI Enable Transmit Holding Register Empty Interrupt


#else
	#error Undefined CPUSTYLE_XXX
#endif
}

/* вызывается из обработчика прерываний или при запрещённых прерываниях. */
/* Разрешение/запрещение прерываний про приёму символа */
void hardware_uart1_enablerx(uint_fast8_t state)
{
#if CPUSTYLE_ATSAM3S || CPUSTYLE_ATSAM4S

	#if HARDWARE_ARM_USEUSART0

		if (state)
			USART0->US_IER = US_IER_RXRDY;
		else
			USART0->US_IDR = US_IDR_RXRDY;

	#elif HARDWARE_ARM_USEUSART1

		if (state)
			USART1->US_IER = US_IER_RXRDY;
		else
			USART1->US_IDR = US_IDR_RXRDY;

	#elif HARDWARE_ARM_USEUART0

		if (state)
			UART0->UART_IER = UART_IER_RXRDY;
		else
			UART0->UART_IDR = UART_IDR_RXRDY;

	#elif HARDWARE_ARM_USEUART1

		if (state)
			UART1->UART_IER = UART_IER_RXRDY;
		else
			UART1->UART_IDR = UART_IDR_RXRDY;

	#else	/* HARDWARE_ARM_USExxx */
		#error Wrong HARDWARE_ARM_USExxx value
	#endif /* HARDWARE_ARM_USEUSART0 */

#elif CPUSTYLE_AT91SAM7S

	#if HARDWARE_ARM_USEUSART0

	if (state)
		AT91C_BASE_US0->US_IER = AT91C_US_RXRDY;
	else
		AT91C_BASE_US0->US_IDR = AT91C_US_RXRDY;

	#elif HARDWARE_ARM_USEUSART1

	if (state)
		AT91C_BASE_US1->US_IER = AT91C_US_RXRDY;
	else
		AT91C_BASE_US1->US_IDR = AT91C_US_RXRDY;

	#else	/* HARDWARE_ARM_USExxx */
		#error Wrong HARDWARE_ARM_USExxx value
	#endif /* HARDWARE_ARM_USEUSART0 */


#elif CPUSTYLE_ATMEGA_XXX4
	/* Used USART 0 */
	if (state)
		UCSR0B |= (1U << RXCIE0);
	else
		UCSR0B &= ~ (1U << RXCIE0);

#elif CPUSTYLE_ATMEGA32

	if (state)
		UCSRB |= (1U << RXCIE);
	else
		UCSRB &= ~ (1U << RXCIE);

#elif CPUSTYLE_ATMEGA128

	/* Used USART 0 */
	if (state)
		UCSR0B |= (1U << RXCIE0);
	else
		UCSR0B &= ~ (1U << RXCIE0);

#elif CPUSTYLE_ATXMEGAXXXA4

	if (state)
		USARTE0.CTRLA = (USARTE0.CTRLA & ~ USART_RXCINTLVL_gm) | USART_RXCINTLVL_LO_gc;
	else
		USARTE0.CTRLA = (USARTE0.CTRLA & ~ USART_RXCINTLVL_gm) | USART_RXCINTLVL_OFF_gc;

#elif CPUSTYLE_STM32F || CPUSTYLE_STM32MP1

	if (state)
		USART1->CR1 |= USART_CR1_RXNEIE;
	else
		USART1->CR1 &= ~ USART_CR1_RXNEIE;

#elif CPUSTYPE_TMS320F2833X

	if (state)
		SCIACTL2 |= (1U << 1);	// RX/BK INT ENA
	else
		SCIACTL2 &= ~ (1U << 1); // RX/BK INT ENA

#elif CPUSTYLE_R7S721

	if (state)
		SCIF0.SCSCR |= SCIF0_SCSCR_RIE;	// RIE Receive Interrupt Enable
	else
		SCIF0.SCSCR &= ~ SCIF0_SCSCR_RIE;	// RIE Receive Interrupt Enable

#elif CPUSTYLE_XC7Z || CPUSTYLE_XCZU


//	if (state)
//		 UART0->CR |= SCIF0_SCSCR_RIE;	// RIE Receive Interrupt Enable
//	else
//		 UART0->CR &= ~ SCIF0_SCSCR_RIE;	// RIE Receive Interrupt Enable
//

#elif CPUSTYPE_T113

	if (state)
		 UART0->DLH_IER |= (0x01uL << 0);	// ERBFI Enable Received Data Available Interrupt
	else
		 UART0->DLH_IER &= ~ (0x01uL << 0);	// ERBFI Enable Received Data Available Interrupt

#else
	#error Undefined CPUSTYLE_XXX

#endif
}


/* передача символа из обработчика прерывания готовности передатчика */
void hardware_uart1_tx(void * ctx, uint_fast8_t c)
{
#if CPUSTYLE_ATSAM3S || CPUSTYLE_ATSAM4S

	#if HARDWARE_ARM_USEUSART0
		USART0->US_THR = c;
	#elif HARDWARE_ARM_USEUART0
		UART0->UART_THR = c;
	#else	/* HARDWARE_ARM_USExxx */
		#error Wrong HARDWARE_ARM_USExxx value
	#endif

#elif CPUSTYLE_AT91SAM7S

	#if HARDWARE_ARM_USEUSART0
		AT91C_BASE_US0->US_THR = c;
	#else	/* HARDWARE_ARM_USExxx */
		#error Wrong HARDWARE_ARM_USExxx value
	#endif

#elif CPUSTYLE_ATMEGA128
	UDR0 = c;

#elif CPUSTYLE_ATMEGA_XXX4

	UDR0 = c;

#elif CPUSTYLE_ATMEGA32

	UDR = c;

#elif CPUSTYLE_ATXMEGAXXXA4

	USARTE0.DATA = c;

#elif CPUSTYLE_STM32F1XX || CPUSTYLE_STM32F4XX

	USART1->DR = c;

#elif CPUSTYLE_STM32F30X || CPUSTYLE_STM32F0XX || CPUSTYLE_STM32L0XX || CPUSTYLE_STM32H7XX || CPUSTYLE_STM32F7XX || CPUSTYLE_STM32MP1

	USART1->TDR = c;

#elif CPUSTYPE_TMS320F2833X

	SCIATXBUF = c;

#elif CPUSTYLE_R7S721

	(void) SCIF0.SCFSR;			// Перед сбросом бита TDFE должно произойти его чтение в ненулевом состоянии
	SCIF0.SCFTDR = c;
	SCIF0.SCFSR = (uint16_t) ~ (1U << SCIF0_SCFSR_TDFE_SHIFT);	// TDFE=0 читать незачем (в примерах странное)

#elif CPUSTYLE_STM32MP1
	#warning Insert code for CPUSTYLE_STM32MP1

#elif CPUSTYLE_XC7Z || CPUSTYLE_XCZU

	UART0->FIFO = c;

#elif CPUSTYPE_T113

	UART0->DATA = c;

#else
	#error Undefined CPUSTYLE_XXX
#endif
}



/* приём символа, если готов порт */
uint_fast8_t 
hardware_uart1_getchar(char * cp)
{
#if CPUSTYLE_ATSAM3S || CPUSTYLE_ATSAM4S

	#if HARDWARE_ARM_USEUSART0
		if ((USART0->US_CSR & US_CSR_RXRDY) == 0)
			return 0;
		* cp = USART0->US_RHR;
	#elif HARDWARE_ARM_USEUSART1
		if ((USART1->US_CSR & US_CSR_RXRDY) == 0)
			return 0;
		* cp = USART1->US_RHR;
	#elif HARDWARE_ARM_USEUART0
		if ((UART0->UART_SR & UART_SR_RXRDY) == 0)
			return 0;
		* cp = UART0->UART_RHR;
	#elif HARDWARE_ARM_USEUART1
		if ((UART1->UART_SR & UART_SR_RXRDY) == 0)
			return 0;
		* cp = UART1->UART_RHR;
	#else	/* HARDWARE_ARM_USExxx */
		#error Wrong HARDWARE_ARM_USExxx value
	#endif

#elif CPUSTYLE_AT91SAM7S

	#if HARDWARE_ARM_USEUSART0
		if ((AT91C_BASE_US0->US_CSR & AT91C_US_RXRDY) == 0)
			return 0;
		* cp = AT91C_BASE_US0->US_RHR;
	#elif HARDWARE_ARM_USEUSART1
		if ((AT91C_BASE_US1->US_CSR & AT91C_US_RXRDY) == 0)
			return 0;
		* cp = AT91C_BASE_US1->US_RHR;
	#else	/* HARDWARE_ARM_USExxx */
		#error Wrong HARDWARE_ARM_USExxx value
	#endif

#elif CPUSTYLE_ATXMEGAXXXA4

	if ((USARTE0.STATUS & (1 << USART_RXCIF_bp)) == 0)
			return 0;
	* cp = USARTE0.DATA;

#elif CPUSTYLE_ATMEGA128

	if ((UCSR0A & (1 << RXC0)) == 0)
			return 0;
	* cp = UDR0;

#elif CPUSTYLE_ATMEGA_XXX4

	if ((UCSR0A & (1 << RXC0)) == 0)
			return 0;
	* cp = UDR0;

#elif CPUSTYLE_ATMEGA32

	if ((UCSRA & (1 << RXC)) == 0)
			return 0;
	* cp = UDR;

#elif CPUSTYLE_STM32F1XX || CPUSTYLE_STM32F4XX

	if ((USART1->SR & (USART_SR_RXNE | USART_SR_ORE | USART_SR_FE | USART_SR_NE)) == 0)
		return 0;
	* cp = USART1->DR;

#elif CPUSTYLE_STM32F30X || CPUSTYLE_STM32F0XX || CPUSTYLE_STM32L0XX || CPUSTYLE_STM32H7XX || CPUSTYLE_STM32F7XX || CPUSTYLE_STM32MP1
	const uint_fast32_t isr = USART1->ISR;
	if (isr & USART_ISR_ORE)
		USART1->ICR = USART_ICR_ORECF;
	if (isr & USART_ISR_FE)
		USART1->ICR = USART_ICR_FECF;
	if ((isr & USART_ISR_RXNE) == 0)
		return 0;
	* cp = USART1->RDR;

#elif CPUSTYPE_TMS320F2833X

	if ((SCIARXST & (1U << 6)) == 0)	// Wait for RXRDY bit
		return 0;
	* cp = SCIARXBUF;

#elif CPUSTYLE_R7S721

	if ((SCIF0.SCFSR & (1U << 1)) == 0)	// RDF
		return 0;
	* cp = SCIF0.SCFRDR;
	SCIF0.SCFSR = (uint16_t) ~ (1U << 1);	// RDF=0 читать незачем (в примерах странное)

#elif CPUSTYLE_XC7Z || CPUSTYLE_XCZU

	if ((UART0->SR & XUARTPS_SR_RXEMPTY) != 0)
		return 0;
	* cp = UART0->FIFO;

#elif CPUSTYPE_T113

	if ((UART0->UART_USR & (0x1uL << 3)) == 0)	// RX FIFO Not Empty
		return 0;
	* cp = UART0->DATA;

#else
	#error Undefined CPUSTYLE_XXX
#endif
	return 1;
}

/* передача символа если готов порт */
uint_fast8_t 
hardware_uart1_putchar(uint_fast8_t c)
{
#if CPUSTYLE_ATSAM3S || CPUSTYLE_ATSAM4S

	#if HARDWARE_ARM_USEUSART0
		if ((USART0->US_CSR & US_CSR_TXRDY) == 0)
			return 0;
		USART0->US_THR = c;
	#elif HARDWARE_ARM_USEUSART1
		if ((USART1->US_CSR & US_CSR_TXRDY) == 0)
			return 0;
		USART1->US_THR = c;
	#elif HARDWARE_ARM_USEUART0
		if ((UART0->UART_SR & UART_SR_TXRDY) == 0)
			return 0;
		UART0->UART_THR = c;
	#elif HARDWARE_ARM_USEUART1
		if ((UART1->UART_SR & UART_SR_TXRDY) == 0)
			return 0;
		UART1->UART_THR = c;
	#else	/* HARDWARE_ARM_USExxx */
		#error Wrong HARDWARE_ARM_USExxx value
	#endif

#elif CPUSTYLE_AT91SAM7S

	#if HARDWARE_ARM_USEUSART0
		if ((AT91C_BASE_US0->US_CSR & AT91C_US_TXRDY) == 0)
			return 0;
		AT91C_BASE_US0->US_THR = c;
	#elif HARDWARE_ARM_USEUSART1
		if ((AT91C_BASE_US1->US_CSR & AT91C_US_TXRDY) == 0)
			return 0;
		AT91C_BASE_US1->US_THR = c;
	#else	/* HARDWARE_ARM_USExxx */
		#error Wrong HARDWARE_ARM_USExxx value
	#endif

#elif CPUSTYLE_ATXMEGAXXXA4

	if ((USARTE0.STATUS & USART_DREIF_bm) == 0)
		return 0;
	USARTE0.DATA = c;

#elif CPUSTYLE_ATMEGA_XXX4

	if ((UCSR0A & (1 << UDRE0)) == 0)
		return 0;
	UDR0 = c;

#elif CPUSTYLE_ATMEGA128

	if ((UCSR0A & (1 << UDRE0)) == 0)
		return 0;
	UDR0 = c;

#elif CPUSTYLE_ATMEGA32

	if ((UCSRA & (1 << UDRE)) == 0)
		return 0;
	UDR = c;

#elif CPUSTYLE_STM32F1XX || CPUSTYLE_STM32F4XX

	if ((USART1->SR & USART_SR_TXE) == 0)
		return 0;
	USART1->DR = c;

#elif CPUSTYLE_STM32F30X || CPUSTYLE_STM32F0XX || CPUSTYLE_STM32L0XX || CPUSTYLE_STM32H7XX || CPUSTYLE_STM32F7XX || CPUSTYLE_STM32MP1

	if ((USART1->ISR & USART_ISR_TXE) == 0)
		return 0;
	USART1->TDR = c;

#elif CPUSTYPE_TMS320F2833X

	if ((SCIACTL2 & (1U << 7)) == 0)	// wait for TXRDY bit
		return 0;
	SCIATXBUF = c;

#elif CPUSTYLE_R7S721

	if ((SCIF0.SCFSR & (1U << SCIF0_SCFSR_TDFE_SHIFT)) == 0)	// Перед сбросом бита TDFE должно произойти его чтение в ненулевом состоянии
		return 0;
	SCIF0.SCFTDR = c;
	SCIF0.SCFSR = (uint16_t) ~ (1U << SCIF0_SCFSR_TDFE_SHIFT);	// TDFE=0 читать незачем (в примерах странное)

#elif CPUSTYLE_XC7Z || CPUSTYLE_XCZU

	if ((UART0->SR & XUARTPS_SR_TNFUL) != 0)
		return 0;
	UART0->FIFO = c;

#elif CPUSTYPE_T113

	if ((UART0->UART_USR & (0x1uL << 1)) == 0)	// TX FIFO Not Full
		return 0;
	UART0->DATA = c;

#else
	#error Undefined CPUSTYLE_XXX
#endif
	return 1;
}

void hardware_uart1_initialize(uint_fast8_t debug)
{
#if CPUSTYLE_ATSAM3S || CPUSTYLE_ATSAM4S

	#if HARDWARE_ARM_USEUSART0
		// enable the clock of USART0
		PMC->PMC_PCER0 = 1UL << ID_USART0;

		HARDWARE_UART1_INITIALIZE();	/* Присоединить периферию к выводам */
		
		// reset the UART
		USART0->US_CR = US_CR_RSTRX | US_CR_RSTTX | US_CR_RXDIS | US_CR_TXDIS;
		// set serial line mode
		USART0->US_MR = 
			US_MR_CHMODE_NORMAL |// Normal Mode
			US_MR_USCLKS_MCK |   // Clock = MCK
			US_MR_CHRL_8_BIT |
			US_MR_PAR_NO |
			US_MR_NBSTOP_1_BIT;                   

		USART0->US_IDR = (US_IDR_RXRDY | US_IDR_TXRDY);

		if (debug == 0)
		{
			serial_set_handler(USART0_IRQn, & USART0_IRQHandler);
		}

		USART0->US_CR = US_CR_RXEN | US_CR_TXEN;	// разрешаем приёмник и передатчик.

	#elif HARDWARE_ARM_USEUSART1
		// enable the clock of USART1
		PMC->PMC_PCER0 = 1UL << ID_USART1;

		HARDWARE_UART1_INITIALIZE();	/* Присоединить периферию к выводам */

		// reset the UART
		USART1->US_CR = US_CR_RSTRX | US_CR_RSTTX | US_CR_RXDIS | US_CR_TXDIS;
		// set serial line mode
		USART1->US_MR = 
			US_MR_CHMODE_NORMAL |// Normal Mode
			US_MR_USCLKS_MCK |   // Clock = MCK
			US_MR_CHRL_8_BIT |
			US_MR_PAR_NO |
			US_MR_NBSTOP_1_BIT;                   
									
		USART1->US_IDR = (US_IDR_RXRDY | US_IDR_TXRDY);

		if (debug == 0)
		{
			serial_set_handler(USART1_IRQn, & USART1_IRQHandler);
		}

		USART1->US_CR = US_CR_RXEN | US_CR_TXEN;	// разрешаем приёмник и передатчик.

	#elif HARDWARE_ARM_USEUART0
		// enable the clock of UART0
		PMC->PMC_PCER0 = 1UL << ID_UART0;

		HARDWARE_UART1_INITIALIZE();	/* Присоединить периферию к выводам */
		
		// reset the UART
		UART0->UART_CR = UART_CR_RSTRX | UART_CR_RSTTX | UART_CR_RXDIS | UART_CR_TXDIS;
		// set serial line mode
		UART0->UART_MR = 
			UART_MR_CHMODE_NORMAL |// Normal Mode
			//UART_MR_USCLKS_MCK |   // Clock = MCK
			//UART_MR_CHRL_8_BIT |
			UART_MR_PAR_NO |
			//UART_MR_NBSTOP_1_BIT |
			0;
									
		UART0->UART_IDR = (UART_IDR_RXRDY | UART_IDR_TXRDY);

		if (debug == 0)
		{
			serial_set_handler(UART0_IRQn, & UART0_IRQHandler);
		}

		UART0->UART_CR = UART_CR_RXEN | UART_CR_TXEN;	// разрешаем приёмник и передатчик.

	#elif HARDWARE_ARM_USEUART1
		// enable the clock of UART1
		PMC->PMC_PCER0 = 1UL << ID_UART1;

		HARDWARE_UART1_INITIALIZE();	/* Присоединить периферию к выводам */

		// reset the UART
		UART1->UART_CR = UART_CR_RSTRX | UART_CR_RSTTX | UART_CR_RXDIS | UART_CR_TXDIS;
		// set serial line mode
		UART1->UART_MR = 
			UART_MR_CHMODE_NORMAL |// Normal Mode
			//UART_MR_USCLKS_MCK |   // Clock = MCK
			//UART_MR_CHRL_8_BIT |
			UART_MR_PAR_NO |
			//UART_MR_NBSTOP_1_BIT |
			0;
									
		UART1->UART_IDR = (UART_IDR_RXRDY | UART_IDR_TXRDY);

		if (debug == 0)
		{
			serial_set_handler(UART1_IRQn, & UART1_IRQHandler);
		}

		UART1->UART_CR = UART_CR_RXEN | UART_CR_TXEN;	// разрешаем приёмник и передатчик.

	#else	/* HARDWARE_ARM_USExxx */
		#error Wrong HARDWARE_ARM_USExxx value
	#endif /* HARDWARE_ARM_USEUSART0 */


#elif CPUSTYLE_STM32F1XX

	RCC->APB2ENR |= RCC_APB2ENR_USART1EN; // Включение тактирования USART1.
	(void) RCC->APB2ENR;

	USART1->CR1 |= (USART_CR1_RE | USART_CR1_TE); // Transmitter Enable & Receiver Enables


	RCC->APB2ENR |= RCC_APB2ENR_AFIOEN;     //включить тактирование альтернативных функций
	(void) RCC->APB2ENR;

	HARDWARE_UART1_INITIALIZE();	/* Присоединить периферию к выводам */

	if (debug == 0)
	{
		serial_set_handler(USART1_IRQn, & USART1_IRQHandler);
	}

	USART1->CR1 |= USART_CR1_UE; // Включение USART1.

#elif CPUSTYLE_STM32F30X || CPUSTYLE_STM32F4XX || CPUSTYLE_STM32F0XX || CPUSTYLE_STM32L0XX || CPUSTYLE_STM32F7XX || CPUSTYLE_STM32H7XX

	RCC->APB2ENR |= RCC_APB2ENR_USART1EN; // Включение тактирования USART1.
	(void) RCC->APB2ENR;

	USART1->CR1 = 0;

#if (__CORTEX_M == 0)
#if WITHUARTFIFO
	USART1->CR1 |= USART_CR1_FIFOEN_Msk;
#else /* WITHUARTFIFO */
	USART1->CR1 &= ~ USART_CR1_FIFOEN_Msk;
#endif /* WITHUARTFIFO */
#endif

	USART1->CR1 |= (USART_CR1_RE | USART_CR1_TE); // Transmitter Enable & Receiver Enables

	HARDWARE_UART1_INITIALIZE();	/* Присоединить периферию к выводам */

	if (debug == 0)
	{
		serial_set_handler(USART1_IRQn, & USART1_IRQHandler);
	}

	USART1->CR1 |= USART_CR1_UE; // Включение USART1.

#elif CPUSTYLE_AT91SAM7S

	#if HARDWARE_ARM_USEUSART0
		// enable the clock of USART0
		AT91C_BASE_PMC->PMC_PCER = 1UL << AT91C_ID_US0;

		HARDWARE_UART1_INITIALIZE();	/* Присоединить периферию к выводам */

		// disable I/O pullup
		// reset the UART
		AT91C_BASE_US0->US_CR = AT91C_US_RSTRX | AT91C_US_RSTTX | AT91C_US_RXDIS | AT91C_US_TXDIS;
		// set serial line mode
		AT91C_BASE_US0->US_MR = 
							AT91C_US_OVER |
							AT91C_US_USMODE_NORMAL |// Normal Mode
						   AT91C_US_CLKS_CLOCK |   // Clock = MCK
						   AT91C_US_CHRL_8_BITS |
						   AT91C_US_PAR_NONE |
						   AT91C_US_NBSTOP_1_BIT;                   
		AT91C_BASE_US0->US_IDR = (AT91C_US_RXRDY | AT91C_US_TXRDY);

		if (debug == 0)
		{
			serial_set_handler(AT91C_ID_US0, AT91F_US0Handler);
		}

		AT91C_BASE_US0->US_CR = AT91C_US_RXEN  | AT91C_US_TXEN;	// разрешаем приёмник и передатчик.

	#elif HARDWARE_ARM_USEUSART1
		// enable the clock of USART1
		AT91C_BASE_PMC->PMC_PCER = 1UL << AT91C_ID_US1;

		HARDWARE_UART1_INITIALIZE();	/* Присоединить периферию к выводам */

		// enable uart pins on PIO
		AT91C_BASE_PIOA->PIO_ASR = AT91C_PA21_RXD1 | AT91C_PA22_TXD1; // assigns the 2 I/O lines to peripheral A function
		AT91C_BASE_PIOA->PIO_PDR = AT91C_PA21_RXD1 | AT91C_PA22_TXD1;	// enable peripheral control of PA21,PA22 (RXD1 and TXD1)
		// reset the UART
		AT91C_BASE_US1->US_CR = AT91C_US_RSTRX | AT91C_US_RSTTX | AT91C_US_RXDIS | AT91C_US_TXDIS;
		// set serial line mode
		AT91C_BASE_US1->US_MR = 
							AT91C_US_OVER |
							AT91C_US_USMODE_NORMAL |// Normal Mode
						   AT91C_US_CLKS_CLOCK |   // Clock = MCK
						   AT91C_US_CHRL_8_BITS |
						   AT91C_US_PAR_NONE |
						   AT91C_US_NBSTOP_1_BIT;          
					            
		AT91C_BASE_US1->US_IDR = (AT91C_US_RXRDY | AT91C_US_TXRDY);

		if (debug == 0)
		{
			serial_set_handler(AT91C_ID_US1, AT91F_US1Handler);
		}

		AT91C_BASE_US1->US_CR = AT91C_US_RXEN | AT91C_US_TXEN;	// разрешаем приёмник и передатчик.

	#else	/* HARDWARE_ARM_USExxx */

		#error Wrong HARDWARE_ARM_USExxx value

	#endif /* HARDWARE_ARM_USEUSART0 */

#elif CPUSTYLE_ATMEGA_XXX4

	// USART initialization
	UCSR0B = (1U << RXEN0) | (1U << TXEN0) /* | (1U << UCSZ02) */;
	UCSR0C = (1U << UCSZ01) | (1U << UCSZ00);	// asynchronious mode, 8 bit.
	// enable pull-up registers for RXD and TXD pins: then rx or tx disabled, these pins disconnected fron UART
	PORTD |= ((1U << PD0) | (1U << PD1));
#if defined (DDRD0) && defined (DDRD1)
	DDRD &= ~ ((1U << DDRD0) | (1U << DDRD1));
#else
	DDRD &= ~ ((1U << DDD0) | (1U << DDD1));
#endif

#elif CPUSTYLE_ATMEGA128

	// USART initialization
	UCSR0B = (1U << RXEN0) | (1U << TXEN0) /* | (1U << UCSZ02) */;
	UCSR0C = (1U << UCSZ01) | (1U << UCSZ00);	// asynchronious mode, 8 bit.
	// enable pull-up registers for RXD and TXD pins: then rx or tx disabled, these pins disconnected fron UART
	//PORTE |= ((1U << PE0) | (1U << PE1));

#elif CPUSTYLE_ATMEGA

	// USART initialization
	UCSRB = (1U << RXEN) | (1U << TXEN) /* | (1U << UCSZ2) */;
	UCSRC = (1U << URSEL) | (1U << UCSZ1) | (1U << UCSZ0);	// asynchronious mode, 8 bit

	// enable pull-up registers for RXD and TXD pins: then rx or tx disabled, these pins disconnected fron UART
	PORTD |= ((1U << PD0) | (1U << PD1));

#if defined (DDRD0) && defined (DDRD1)
	DDRD &= ~ ((1U << DDRD0) | (1U << DDRD1));
#else
	DDRD &= ~ ((1U << DDD0) | (1U << DDD1));
#endif

#elif CPUSTYLE_ATXMEGAXXXA4

	PORTE.DIRSET = PIN3_bm; // PE3 (TXD0) as output
	PORTE.DIRCLR = PIN2_bm; // PE2 (RXD0) as input
	PORTE_PIN2CTRL = (PORTE_PIN2CTRL & ~ PORT_OPC_gm) | PORT_OPC_PULLUP_gc;							// pin is pulled high

	USARTE0.CTRLC = USART_CMODE_ASYNCHRONOUS_gc | USART_PMODE_DISABLED_gc | USART_CHSIZE_8BIT_gc;
	USARTE0.CTRLB = USART_RXEN_bm | USART_TXEN_bm;

#elif CPUSTYPE_TMS320F2833X

	// Enable SCI-A clock
	PCLKCR0 |= (1U << 10);	// SCIAENCLK

	//SCIACTL1 &= ~ (1U << 5);	// SW RESET on
	SCIACTL1 |= (1U << 5);	// SW RESET off

	SCIACCR =
			(7U << 0) |	// Data length = 8 bit
			(0U << 5) | // Parity enable
			(0U << 6) | // Evan/Odd parity
			(0U << 7);	// 0 - one stop bit, 1 - to stop bits


	tms320_hardware_piob_periph(
			(1UL << (35 % 32)) |	// SCITXDA
			(1UL << (36 % 32)),	// SCIRXDA
			1	// mux = 1
			);

	SCIACTL1 |= (1U << 0) |	// RX enable
				(1U << 1);	// TX enable

#elif CPUSTYLE_R7S721

    /* ---- Supply clock to the SCIF(channel 0) ---- */
	CPG.STBCR4 &= ~ CPG_STBCR4_BIT_MSTP47;	// Module Stop 47 SCIF0
	(void) CPG.STBCR4;			/* Dummy read */

	SCIF0.SCSCR = 0x0000;	/* SCIF transmitting and receiving operations stop, internal clock */

	SCIF0.SCSCR = (SCIF0.SCSCR & ~ 0x03) |	
		0x00 |						// internal clock
		0;

	/* ---- Serial status register(SCFSR2) setting ---- */
	/* ER,BRK,DR bit clear */
	(void) SCIF0.SCFSR;						// Перед сбросом бита xxx должно произойти его чтение в ненулевом состоянии
	SCIF0.SCFSR = ~ 0x0091;	// 0xFF6E;

	/* ---- Line status register (SCLSR2) setting ---- */
	/* ORER bit clear */
	//SCIF0.SCLSR.BIT.ORER  = 0;
	SCIF0.SCLSR &= ~ 0x0001;

	SCIF0.SCSMR = 
		0x00 |	/* 8-N-1 format */
		0;

	/* ---- FIFO control register (SCFCR2) setting ---- */
	/*  RTS output active trigger        :Initial value	*/
	/*  Receive FIFO data trigger        :1-data		*/
	/*  Transmit FIFO data trigger       :0-data		*/
	/*  Modem control enable             :Disabled		*/
	/*  Receive FIFO data register reset :Disabled		*/
	/*  Loop-back test                   :Disabled 		*/
	SCIF0.SCFCR = 0x0030;

	/* ---- Serial port register (SCSPTR2) setting ---- */
	/* Serial port  break output(SPB2IO)  1: Enabled */
	/* Serial port break data(SPB2DT)  1: High-level */
	//SCIF0.SCSPTR |= 0x0003;

	if (debug == 0)
	{
	   serial_set_handler(SCIFRXI0_IRQn, SCIFRXI0_IRQHandler);
	   serial_set_handler(SCIFTXI0_IRQn, SCIFTXI0_IRQHandler);
	}
	HARDWARE_UART1_INITIALIZE();	/* Присоединить периферию к выводам */

	SCIF0.SCSCR |= 0x0030;	// TE RE - SCIF0 transmitting and receiving operations are enabled */

#elif CPUSTYLE_STM32MP1

	RCC->MP_APB5ENSETR = RCC_MP_APB5ENSETR_USART1EN; // Включение тактирования USART1.
	(void) RCC->MP_APB5ENSETR;
	RCC->MP_APB5LPENSETR = RCC_MP_APB5LPENSETR_USART1LPEN; // Включение тактирования USART1.
	(void) RCC->MP_APB5LPENSETR;
	RCC->APB5RSTSETR = RCC_APB5RSTSETR_USART1RST; // Установить сброс USART1.
	(void) RCC->APB5RSTSETR;
	RCC->APB5RSTCLRR = RCC_APB5RSTCLRR_USART1RST; // Снять брос USART1.
	(void) RCC->APB5RSTCLRR;

	USART1->CR1 = 0;

#if WITHUARTFIFO
	USART1->CR1 |= USART_CR1_FIFOEN_Msk;
#else /* WITHUARTFIFO */
	USART1->CR1 &= ~ USART_CR1_FIFOEN_Msk;
#endif /* WITHUARTFIFO */

	USART1->CR1 |= (USART_CR1_RE | USART_CR1_TE); // Transmitter Enable & Receiver Enables

	HARDWARE_UART1_INITIALIZE();	/* Присоединить периферию к выводам */

	if (debug == 0)
	{
	   serial_set_handler(USART1_IRQn, USART1_IRQHandler);
	}

	USART1->CR1 |= USART_CR1_UE; // Включение USART1.

#elif CPUSTYLE_XC7Z || CPUSTYLE_XCZU

	SCLR->SLCR_UNLOCK = 0x0000DF0DU;
	SCLR->APER_CLK_CTRL |= (0x01uL << 20);	// APER_CLK_CTRL.UART0_CPU_1XCLKACT
	//EMIT_MASKWRITE(0XF8000154, 0x00003F33U ,0x00001002U),	// UART_CLK_CTRL
	SCLR->UART_CLK_CTRL = (SCLR->UART_CLK_CTRL & ~ (0x00003F30U)) |
			((uint_fast32_t) SCLR_UART_CLK_CTRL_DIVISOR_VALUE << 8) | // DIVISOR
			(0x00uL << 4) |	// SRCSEL - 0x: IO PLL
			(0x01) |	// CLKACT0 - UART 0 reference clock active
			0;

	uint32_t r; // Temporary value variable
	r = UART0->CR;
	r &= ~(XUARTPS_CR_TX_EN | XUARTPS_CR_RX_EN); // Clear Tx & Rx Enable
	r |= XUARTPS_CR_RX_DIS | XUARTPS_CR_TX_DIS; // Tx & Rx Disable
	UART0->CR = r;

	UART0->MR = 0;
	UART0->MR &= ~XUARTPS_MR_CLKSEL; // Clear "Input clock selection" - 0: clock source is uart_ref_clk
	UART0->MR |= XUARTPS_MR_CHARLEN_8_BIT; 	// Set "8 bits data"
	UART0->MR |= XUARTPS_MR_PARITY_NONE; 	// Set "No parity mode"
	UART0->MR |= XUARTPS_MR_STOPMODE_1_BIT; // Set "1 stop bit"
	UART0->MR |= XUARTPS_MR_CHMODE_NORM; 	// Set "Normal mode"

	UART0->CR |= (XUARTPS_CR_TXRST | XUARTPS_CR_RXRST); // TX & RX logic reset

	r = UART0->CR;
	r |= XUARTPS_CR_RX_EN | XUARTPS_CR_TX_EN; // Set TX & RX enabled
	r &= ~(XUARTPS_CR_RX_DIS | XUARTPS_CR_TX_DIS); // Clear TX & RX disabled
	UART0->CR = r;

	HARDWARE_UART1_INITIALIZE();	/* Присоединить периферию к выводам */

#elif CPUSTYPE_T113
	const unsigned ix = 0;

	/* Open the clock gate for uart0 */
	CCU->UART_BGR_REG |= (0x01uL << (ix + 0));

	/* Deassert uart0 reset */
	CCU->UART_BGR_REG |= (0x01uL << (ix + 16));

	/* Config uart0 to 115200-8-1-0 */
	uint32_t divisor = allwnrt113_get_usart_freq() / ((DEBUGSPEED) * 16);

	UART0->DLH_IER = 0;
	UART0->IIR_FCR = 0xf7;
	UART0->UART_MCR = 0x00;

	UART0->UART_LCR |= (1 << 7);	// Divisor Latch Access Bit
	UART0->DATA = divisor & 0xff;
	UART0->DLH_IER = (divisor >> 8) & 0xff;
	UART0->UART_LCR &= ~ (1 << 7);	// Divisor Latch Access Bit
	//
	UART0->UART_LCR &= ~ 0x1f;
	UART0->UART_LCR |= (0x3 << 0) | (0 << 2) | (0x0 << 3);	//DAT_LEN_8_BITS ONE_STOP_BIT NO_PARITY

	HARDWARE_UART1_INITIALIZE();

	if (debug == 0)
	{
	   serial_set_handler(UART0_IRQn, UART0_IRQHandler);
	}

#else

	#error Undefined CPUSTYLE_XXX

#endif

}

#endif /* WITHUART1HW */

#if WITHUART2HW 

#if CPUSTYLE_ATSAM3S || CPUSTYLE_ATSAM4S

	void RAMFUNC_NONILINE USART1_Handler(void) 
	{
		const uint_fast32_t csr = USART1->US_CSR;

		if (csr & US_CSR_RXRDY)
			HARDWARE_UART2_ONRXCHAR(USART1->US_RHR);
		if (csr & US_CSR_TXRDY)
			HARDWARE_UART2_ONTXCHAR(USART1);
	}

#elif CPUSTYLE_STM32F1XX || CPUSTYLE_STM32F4XX

	void RAMFUNC_NONILINE USART2_IRQHandler(void)
	{
		const uint_fast32_t sr = USART2->SR;

		if (sr & (USART_SR_RXNE | USART_SR_ORE | USART_SR_FE | USART_SR_NE))
			HARDWARE_UART2_ONRXCHAR(USART2->DR);
		if (sr & (USART_SR_ORE | USART_SR_FE | USART_SR_NE))
			HARDWARE_UART2_ONOVERFLOW();
		if (sr & USART_SR_TXE)
			HARDWARE_UART2_ONTXCHAR(USART2);
	}

#elif CPUSTYLE_STM32H7XX || CPUSTYLE_STM32MP1

	void RAMFUNC_NONILINE USART2_IRQHandler(void)
	{
		const uint_fast32_t isr = USART2->ISR;

		if (isr & USART_ISR_RXNE_RXFNE)
			HARDWARE_UART2_ONRXCHAR(USART2->RDR);
		if (isr & USART_ISR_ORE)
		{
			USART2->ICR = USART_ICR_ORECF;
			HARDWARE_UART2_ONOVERFLOW();
		}
		if (isr & USART_ISR_FE)
			USART2->ICR = USART_ICR_FECF;
		if (isr & USART_ISR_TXE_TXFNF)
			HARDWARE_UART2_ONTXCHAR(USART2);
	}

#elif CPUSTYLE_STM32F30X || CPUSTYLE_STM32F0XX || CPUSTYLE_STM32L0XX || CPUSTYLE_STM32F7XX

	void RAMFUNC_NONILINE USART2_IRQHandler(void)
	{
		const uint_fast32_t isr = USART2->ISR;

		if (isr & USART_ISR_RXNE)
			HARDWARE_UART2_ONRXCHAR(USART2->RDR);
		if (isr & USART_ISR_ORE)
		{
			USART2->ICR = USART_ICR_ORECF;
			HARDWARE_UART2_ONOVERFLOW();
		}
		if (isr & USART_ISR_FE)
			USART2->ICR = USART_ICR_FECF;
		if (isr & USART_ISR_TXE)
			HARDWARE_UART2_ONTXCHAR(USART2);
	}

#elif CPUSTYLE_AT91SAM7S

	#if HARDWARE_ARM_USEUSART0
		static RAMFUNC_NONILINE void AT91F_US0Handler(void) 
		{
			const uint_fast32_t csr = AT91C_BASE_US0->US_CSR;

			if (csr & AT91C_US_RXRDY)
				HARDWARE_UART2_ONRXCHAR(AT91C_BASE_US0->US_RHR);
			if (csr & AT91C_US_TXRDY)
				HARDWARE_UART2_ONTXCHAR(AT91C_BASE_US0);
		}
	#elif HARDWARE_ARM_USEUSART1
		static RAMFUNC_NONILINE void AT91F_US1Handler(void) 
		{
			const uint_fast32_t csr = AT91C_BASE_US1->US_CSR;

			if (csr & AT91C_US_RXRDY)
				HARDWARE_UART2_ONRXCHAR(AT91C_BASE_US1->US_RHR);
			if (csr & AT91C_US_TXRDY)
				HARDWARE_UART2_ONTXCHAR(AT91C_BASE_US1);
		}
	#else	/* HARDWARE_ARM_USExxx */
		#error Wrong HARDWARE_ARM_USExxx value
	#endif		/* HARDWARE_ARM_USEUSART0 */

#elif CPUSTYLE_ATMEGA328

	ISR(USART_RX_vect)
	{
		HARDWARE_UART2_ONRXCHAR(UDR0);
	}

	ISR(USART_TX_vect)
	{
		HARDWARE_UART2_ONTXCHAR(NULL);
	}

#elif CPUSTYLE_ATMEGA_XXX4

	ISR(USART1_RX_vect)
	{
		HARDWARE_UART2_ONRXCHAR(UDR0);
	}

	ISR(USART1_TX_vect)
	{
		HARDWARE_UART2_ONTXCHAR(NULL);
	}

#elif CPUSTYLE_ATMEGA32

	ISR(USART_RXC_vect)
	{
		HARDWARE_UART2_ONRXCHAR(UDR);
	}

	ISR(USART_TXC_vect)
	{
		HARDWARE_UART2_ONTXCHAR(NULL);
	}

#elif CPUSTYLE_ATMEGA128

	ISR(USART1_RX_vect)
	{
		HARDWARE_UART2_ONRXCHAR(UDR1);
	}

	ISR(USART1_TX_vect)
	{
		HARDWARE_UART2_ONTXCHAR(NULL);
	}

#elif CPUSTYLE_ATXMEGAXXXA4
	
	ISR(USARTE1_RXC_vect)
	{
		HARDWARE_UART2_ONRXCHAR(USARTE1.DATA);
	}

	ISR(USARTE1_DRE_vect)
	{
		HARDWARE_UART2_ONTXCHAR(& USARTE1);
	}

#elif CPUSTYLE_R7S721

	// Приём символа он последовательного порта
	static void SCIFRXI3_IRQHandler(void)
	{
		(void) SCIF3.SCFSR;						// Перед сбросом бита RDF должно произойти его чтение в ненулевом состоянии
		SCIF3.SCFSR = (uint16_t) ~ SCIF3_SCFSR_RDF;	// RDF=0 читать незачем (в примерах странное - сбрасывабтся и другие биты)
		uint_fast8_t n = (SCIF3.SCFDR & SCIF3_SCFDR_R) >> SCIF3_SCFDR_R_SHIFT;
		while (n --)
			HARDWARE_UART2_ONRXCHAR(SCIF3.SCFRDR & SCIF3_SCFRDR_D);
	}

	// Передача символа в последовательный порт
	static void SCIFTXI3_IRQHandler(void)
	{
		HARDWARE_UART2_ONTXCHAR(& SCIF3);
	}

#elif CPUSTYLE_XC7Z || CPUSTYLE_XCZU



#else

	#error Undefined CPUSTYLE_XXX

#endif	/* CPUSTYLE_ATMEGA_XXX4 */
	

/* вызывается из обработчика прерываний или при запрещённых прерываниях. */
/*
		system_disableIRQ();
		system_enableIRQ();
*/
/* Разрешение/запрещение прерывания по передаче символа */
void hardware_uart2_enabletx(uint_fast8_t state)
{
#if CPUSTYLE_ATSAM3S || CPUSTYLE_ATSAM4S

	#if HARDWARE_ARM_USEUSART0

		if (state)
			USART0->US_IER = US_IER_TXRDY;
		else
			USART0->US_IDR = US_IDR_TXRDY;

	#elif HARDWARE_ARM_USEUSART1

		if (state)
			USART1->US_IER = US_IER_TXRDY;
		else
			USART1->US_IDR = US_IDR_TXRDY;

	#elif HARDWARE_ARM_USEUART0

		if (state)
			UART0->UART_IER = UART_IER_TXRDY;
		else
			UART0->UART_IDR = UART_IDR_TXRDY;

	#elif HARDWARE_ARM_USEUART1

		if (state)
			UART1->UART_IER = UART_IER_TXRDY;
		else
			UART1->UART_IDR = UART_IDR_TXRDY;

	#else	/* HARDWARE_ARM_USExxx */
		#error Wrong HARDWARE_ARM_USExxx value
	#endif /* HARDWARE_ARM_USEUSART0 */

#elif CPUSTYLE_AT91SAM7S

	#if HARDWARE_ARM_USEUSART0

	if (state)
		AT91C_BASE_US0->US_IER = AT91C_US_TXRDY;
	else
		AT91C_BASE_US0->US_IDR = AT91C_US_TXRDY;

	#elif HARDWARE_ARM_USEUSART1

	if (state)
		AT91C_BASE_US1->US_IER = AT91C_US_TXRDY;
	else
		AT91C_BASE_US1->US_IDR = AT91C_US_TXRDY;

	#else	/* HARDWARE_ARM_USExxx */
		#error Wrong HARDWARE_ARM_USExxx value
	#endif /* HARDWARE_ARM_USEUSART0 */

#elif CPUSTYLE_STM32F || CPUSTYLE_STM32MP1

	if (state)
		USART2->CR1 |= USART_CR1_TXEIE;
	else
		USART2->CR1 &= ~ USART_CR1_TXEIE;

#elif CPUSTYLE_ATMEGA_XXX4

	/* Used USART 1 */
	if (state)
	{
		UCSR1B |= (1U << TXCIE1);
		HARDWARE_UART2_ONTXCHAR(NULL);	// initiate 1-st character sending
	}
	else
	{
		UCSR1B &= ~ (1U << TXCIE1);
	}

#elif CPUSTYLE_ATMEGA32

	#error WITHUART2HW with CPUSTYLE_ATMEGA32 not supported
	if (state)
	{
		UCSRB |= (1U << TXCIE);
		HARDWARE_UART2_ONTXCHAR(NULL);	// initiate 1-st character sending
	}
	else
	{
		UCSRB &= ~ (1U << TXCIE);
	}

#elif CPUSTYLE_ATMEGA128

	/* Used USART 1 */
	if (state)
	{
		UCSR1B |= (1U << TXCIE1);
		HARDWARE_UART2_ONTXCHAR(NULL);	// initiate 1-st character sending
	}
	else
	{
		UCSR1B &= ~ (1U << TXCIE1);
	}

#elif CPUSTYLE_ATXMEGAXXXA4

	if (state)
		USARTE1.CTRLA = (USARTE1.CTRLA & ~ USART_DREINTLVL_gm) | USART_DREINTLVL_LO_gc;
	else
		USARTE1.CTRLA = (USARTE1.CTRLA & ~ USART_DREINTLVL_gm) | USART_DREINTLVL_OFF_gc;

#elif CPUSTYPE_TMS320F2833X

	if (state)
		SCIBCTL2 |= (1U << 0);	// TX INT ENA
	else
		SCIBCTL2 &= ~ (1U << 0); // TX INT ENA

#elif CPUSTYLE_R7S721

	if (state)
		SCIF3.SCSCR |= (1U << 7);	// TIE Transmit Interrupt Enable
	else
		SCIF3.SCSCR &= ~ (1U << 7);	// TIE Transmit Interrupt Enable

#elif CPUSTYLE_XC7Z || CPUSTYLE_XCZU


#else
	#error Undefined CPUSTYLE_XXX
#endif
}

/* вызывается из обработчика прерываний или при запрещённых прерываниях. */
/* Разрешение/запрещение прерываний про приёму символа */
void hardware_uart2_enablerx(uint_fast8_t state)
{
#if CPUSTYLE_ATSAM3S || CPUSTYLE_ATSAM4S

	#if HARDWARE_ARM_USEUSART0

		if (state)
			USART0->US_IER = US_IER_RXRDY;
		else
			USART0->US_IDR = US_IDR_RXRDY;

	#elif HARDWARE_ARM_USEUSART1

		if (state)
			USART1->US_IER = US_IER_RXRDY;
		else
			USART1->US_IDR = US_IDR_RXRDY;

	#elif HARDWARE_ARM_USEUART0

		if (state)
			UART0->UART_IER = UART_IER_RXRDY;
		else
			UART0->UART_IDR = UART_IDR_RXRDY;

	#elif HARDWARE_ARM_USEUART1

		if (state)
			UART1->UART_IER = UART_IER_RXRDY;
		else
			UART1->UART_IDR = UART_IDR_RXRDY;

	#else	/* HARDWARE_ARM_USExxx */
		#error Wrong HARDWARE_ARM_USExxx value
	#endif /* HARDWARE_ARM_USEUSART0 */

#elif CPUSTYLE_AT91SAM7S

	#if HARDWARE_ARM_USEUSART1

	if (state)
		AT91C_BASE_US1->US_IER = AT91C_US_RXRDY;
	else
		AT91C_BASE_US1->US_IDR = AT91C_US_RXRDY;

	#else	/* HARDWARE_ARM_USExxx */
		#error Wrong HARDWARE_ARM_USExxx value
	#endif /* HARDWARE_ARM_USEUSART0 */


#elif CPUSTYLE_ATMEGA_XXX4
	/* Used USART 1 */
	if (state)
		UCSR1B |= (1U << RXCIE1);
	else
		UCSR1B &= ~ (1U << RXCIE1);

#elif CPUSTYLE_ATMEGA32

	#error WITHUART2HW with CPUSTYLE_ATMEGA32 not supported
	if (state)
		UCSRB |= (1U << RXCIE);
	else
		UCSRB &= ~ (1U << RXCIE);

#elif CPUSTYLE_ATMEGA128

	/* Used USART 0 */
	if (state)
		UCSR1B |= (1U << RXCIE1);
	else
		UCSR1B &= ~ (1U << RXCIE1);

#elif CPUSTYLE_ATXMEGAXXXA4

	if (state)
		USARTE1.CTRLA = (USARTE1.CTRLA & ~ USART_RXCINTLVL_gm) | USART_RXCINTLVL_LO_gc;
	else
		USARTE1.CTRLA = (USARTE1.CTRLA & ~ USART_RXCINTLVL_gm) | USART_RXCINTLVL_OFF_gc;

#elif CPUSTYLE_STM32F || CPUSTYLE_STM32MP1

	if (state)
		USART2->CR1 |= USART_CR1_RXNEIE;
	else
		USART2->CR1 &= ~ USART_CR1_RXNEIE;

#elif CPUSTYPE_TMS320F2833X

	if (state)
		SCIBCTL2 |= (1U << 1);	// RX/BK INT ENA
	else
		SCIBCTL2 &= ~ (1U << 1); // RX/BK INT ENA

#elif CPUSTYLE_R7S721

	if (state)
		SCIF3.SCSCR |= (1U << 6);	// RIE Receive Interrupt Enable
	else
		SCIF3.SCSCR &= ~ (1U << 6);	// RIE Receive Interrupt Enable

#elif CPUSTYLE_XC7Z || CPUSTYLE_XCZU


#else
	#error Undefined CPUSTYLE_XXX

#endif
}


/* передача символа из обработчика прерывания готовности передатчика */
void hardware_uart2_tx(void * ctx, uint_fast8_t c)
{
#if CPUSTYLE_ATSAM3S || CPUSTYLE_ATSAM4S

	#if HARDWARE_ARM_USEUSART1
		USART1->US_THR = c;
	#elif HARDWARE_ARM_USEUART1
		UART1->UART_THR = c;
	#else	/* HARDWARE_ARM_USExxx */
		#error Wrong HARDWARE_ARM_USExxx value
	#endif

#elif CPUSTYLE_AT91SAM7S

	#if HARDWARE_ARM_USEUSART1
		AT91C_BASE_US1->US_THR = c;
	#else	/* HARDWARE_ARM_USExxx */
		#error Wrong HARDWARE_ARM_USExxx value
	#endif

#elif CPUSTYLE_ATMEGA128
	UDR1 = c;

#elif CPUSTYLE_ATMEGA_XXX4

	UDR1 = c;

#elif CPUSTYLE_ATMEGA32

	#error WITHUART2HW with CPUSTYLE_ATMEGA32 not supported
	UDR = c;

#elif CPUSTYLE_ATXMEGAXXXA4

	USARTE1.DATA = c;

#elif CPUSTYLE_STM32F1XX || CPUSTYLE_STM32F4XX

	USART2->DR = c;

#elif CPUSTYLE_STM32F30X || CPUSTYLE_STM32F0XX || CPUSTYLE_STM32L0XX || CPUSTYLE_STM32H7XX || CPUSTYLE_STM32F7XX || CPUSTYLE_STM32MP1

	USART2->TDR = c;

#elif CPUSTYPE_TMS320F2833X

	SCIBTXBUF = c;

#elif CPUSTYLE_R7S721

	(void) SCIF3.SCFSR;			// Перед сбросом бита TDFE должно произойти его чтение в ненулевом состоянии
	SCIF3.SCFTDR = c;
	SCIF3.SCFSR = (uint16_t) ~ (1U << SCIF3_SCFSR_TDFE_SHIFT);	// TDFE=0 читать незачем (в примерах странное)

#elif CPUSTYLE_XC7Z || CPUSTYLE_XCZU

	UART1->FIFO = c;

#else
	#error Undefined CPUSTYLE_XXX
#endif
}



/* приём символа, если готов порт */
uint_fast8_t 
hardware_uart2_getchar(char * cp)
{
#if CPUSTYLE_ATSAM3S || CPUSTYLE_ATSAM4S

	#if HARDWARE_ARM_USEUSART0
		if ((USART0->US_CSR & US_CSR_RXRDY) == 0)
			return 0;
		* cp = USART0->US_RHR;
	#elif HARDWARE_ARM_USEUSART1
		if ((USART1->US_CSR & US_CSR_RXRDY) == 0)
			return 0;
		* cp = USART1->US_RHR;
	#elif HARDWARE_ARM_USEUART0
		if ((UART0->UART_SR & UART_SR_RXRDY) == 0)
			return 0;
		* cp = UART0->UART_RHR;
	#elif HARDWARE_ARM_USEUART1
		if ((UART1->UART_SR & UART_SR_RXRDY) == 0)
			return 0;
		* cp = UART1->UART_RHR;
	#else	/* HARDWARE_ARM_USExxx */
		#error Wrong HARDWARE_ARM_USExxx value
	#endif

#elif CPUSTYLE_AT91SAM7S

	#if HARDWARE_ARM_USEUSART1
		if ((AT91C_BASE_US1->US_CSR & AT91C_US_RXRDY) == 0)
			return 0;
		* cp = AT91C_BASE_US1->US_RHR;
	#else	/* HARDWARE_ARM_USExxx */
		#error Wrong HARDWARE_ARM_USExxx value
	#endif

#elif CPUSTYLE_ATXMEGAXXXA4

	if ((USARTE1.STATUS & (1 << USART_RXCIF_bp)) == 0)
			return 0;
	* cp = USARTE1.DATA;

#elif CPUSTYLE_ATMEGA128

	if ((UCSR1A & (1 << RXC1)) == 0)
			return 0;
	* cp = UDR1;

#elif CPUSTYLE_ATMEGA_XXX4

	if ((UCSR1A & (1 << RXC1)) == 0)
			return 0;
	* cp = UDR1;

#elif CPUSTYLE_ATMEGA32

	#error WITHUART2HW with CPUSTYLE_ATMEGA32 not supported

	if ((UCSRA & (1 << RXC)) == 0)
			return 0;
	* cp = UDR;

#elif CPUSTYLE_STM32F1XX || CPUSTYLE_STM32F4XX

	if ((USART2->SR & (USART_SR_RXNE | USART_SR_ORE | USART_SR_FE | USART_SR_NE)) == 0)
		return 0;
	* cp = USART2->DR;

#elif CPUSTYLE_STM32H7XX || CPUSTYLE_STM32MP1

	const uint_fast32_t isr = USART2->ISR;
	if (isr & USART_ISR_ORE)
		USART2->ICR = USART_ICR_ORECF;
	if (isr & USART_ISR_FE)
		USART2->ICR = USART_ICR_FECF;
	if ((isr & USART_ISR_RXNE_RXFNE) == 0)
		return 0;
	* cp = USART2->RDR;

#elif CPUSTYLE_STM32F30X || CPUSTYLE_STM32F0XX || CPUSTYLE_STM32L0XX || CPUSTYLE_STM32F7XX

	const uint_fast32_t isr = USART2->ISR;
	if (isr & USART_ISR_ORE)
		USART2->ICR = USART_ICR_ORECF;
	if (isr & USART_ISR_FE)
		USART2->ICR = USART_ICR_FECF;
	if ((isr & USART_ISR_RXNE) == 0)
		return 0;
	* cp = USART2->RDR;

#elif CPUSTYPE_TMS320F2833X

	if ((SCIBRXST & (1U << 6)) == 0)	// Wait for RXRDY bit
		return 0;
	* cp = SCIBRXBUF;

#elif CPUSTYLE_R7S721

	if ((SCIF3.SCFSR & (1U << 1)) == 0)	// RDF
		return 0;
	* cp = SCIF3.SCFRDR;
	SCIF3.SCFSR = (uint16_t) ~ (1U << 1);	// RDF=0 читать незачем (в примерах странное)

#elif CPUSTYLE_XC7Z || CPUSTYLE_XCZU

	if ((UART1->SR & XUARTPS_SR_RXEMPTY) != 0)
		return 0;
	* cp = UART1->FIFO;

#else
	#error Undefined CPUSTYLE_XXX
#endif
	return 1;
}

/* передача символа если готов порт */
uint_fast8_t 
hardware_uart2_putchar(uint_fast8_t c)
{
#if CPUSTYLE_ATSAM3S || CPUSTYLE_ATSAM4S

	#if HARDWARE_ARM_USEUSART0
		if ((USART0->US_CSR & US_CSR_TXRDY) == 0)
			return 0;
		USART0->US_THR = c;
	#elif HARDWARE_ARM_USEUSART1
		if ((USART1->US_CSR & US_CSR_TXRDY) == 0)
			return 0;
		USART1->US_THR = c;
	#elif HARDWARE_ARM_USEUART0
		if ((UART0->UART_SR & UART_SR_TXRDY) == 0)
			return 0;
		UART0->UART_THR = c;
	#elif HARDWARE_ARM_USEUART1
		if ((UART1->UART_SR & UART_SR_TXRDY) == 0)
			return 0;
		UART1->UART_THR = c;
	#else	/* HARDWARE_ARM_USExxx */
		#error Wrong HARDWARE_ARM_USExxx value
	#endif

#elif CPUSTYLE_AT91SAM7S

	#if HARDWARE_ARM_USEUSART0
		if ((AT91C_BASE_US0->US_CSR & AT91C_US_TXRDY) == 0)
			return 0;
		AT91C_BASE_US0->US_THR = c;
	#elif HARDWARE_ARM_USEUSART1
		if ((AT91C_BASE_US1->US_CSR & AT91C_US_TXRDY) == 0)
			return 0;
		AT91C_BASE_US1->US_THR = c;
	#else	/* HARDWARE_ARM_USExxx */
		#error Wrong HARDWARE_ARM_USExxx value
	#endif

#elif CPUSTYLE_ATXMEGAXXXA4

	if ((USARTE0.STATUS & USART_DREIF_bm) == 0)
		return 0;
	USARTE0.DATA = c;

#elif CPUSTYLE_ATMEGA_XXX4

	if ((UCSR0A & (1 << UDRE0)) == 0)
		return 0;
	UDR0 = c;

#elif CPUSTYLE_ATMEGA128

	if ((UCSR0A & (1 << UDRE0)) == 0)
		return 0;
	UDR0 = c;

#elif CPUSTYLE_ATMEGA32

	#error WITHUART2HW with CPUSTYLE_ATMEGA32 not supported

	if ((UCSRA & (1 << UDRE)) == 0)
		return 0;
	UDR = c;

#elif CPUSTYLE_STM32F1XX || CPUSTYLE_STM32F4XX

	if ((USART2->SR & USART_SR_TXE) == 0)
		return 0;
	USART2->DR = c;

#elif CPUSTYLE_STM32H7XX || CPUSTYLE_STM32MP1

	if ((USART2->ISR & USART_ISR_TXE_TXFNF) == 0)
		return 0;
	USART2->TDR = c;

#elif CPUSTYLE_STM32F30X || CPUSTYLE_STM32F0XX || CPUSTYLE_STM32L0XX || CPUSTYLE_STM32F7XX

	if ((USART2->ISR & USART_ISR_TXE) == 0)
		return 0;
	USART2->TDR = c;

#elif CPUSTYPE_TMS320F2833X

	if ((SCIBCTL2 & (1U << 7)) == 0)	// wait for TXRDY bit
		return 0;
	SCIBTXBUF = c;

#elif CPUSTYLE_R7S721

	if ((SCIF3.SCFSR & (1U << SCIF3_SCFSR_TDFE_SHIFT)) == 0)	// Перед сбросом бита TDFE должно произойти его чтение в ненулевом состоянии
		return 0;
	SCIF3.SCFTDR = c;
	SCIF3.SCFSR = (uint16_t) ~ (1U << SCIF3_SCFSR_TDFE_SHIFT);	// TDFE=0 читать незачем (в примерах странное)

#elif CPUSTYLE_XC7Z || CPUSTYLE_XCZU

	if ((UART1->SR & XUARTPS_SR_TNFUL) != 0)
		return 0;
	UART1->FIFO = c;

#else
	#error Undefined CPUSTYLE_XXX
#endif
	return 1;
}

void hardware_uart2_initialize(uint_fast8_t debug)
{
#if CPUSTYLE_ATSAM3S || CPUSTYLE_ATSAM4S

	#if HARDWARE_ARM_USEUSART0
		// enable the clock of USART0
		PMC->PMC_PCER0 = 1UL << ID_USART0;

		HARDWARE_UART2_INITIALIZE();	/* Присоединить периферию к выводам */
		
		// reset the UART
		USART0->US_CR = US_CR_RSTRX | US_CR_RSTTX | US_CR_RXDIS | US_CR_TXDIS;
		// set serial line mode
		USART0->US_MR = 
			US_MR_CHMODE_NORMAL |// Normal Mode
			US_MR_USCLKS_MCK |   // Clock = MCK
			US_MR_CHRL_8_BIT |
			US_MR_PAR_NO |
			US_MR_NBSTOP_1_BIT;                   
									
		USART0->US_IDR = (US_IDR_RXRDY | US_IDR_TXRDY);

		if (debug == 0)
		{
			serial_set_handler(USART0_IRQn, & USART0_IRQHandler);
		}

		USART0->US_CR = US_CR_RXEN | US_CR_TXEN;	// разрешаем приёмник и передатчик.

	#elif HARDWARE_ARM_USEUSART1
		// enable the clock of USART1
		PMC->PMC_PCER0 = 1UL << ID_USART1;

		HARDWARE_UART2_INITIALIZE();	/* Присоединить периферию к выводам */

		// reset the UART
		USART1->US_CR = US_CR_RSTRX | US_CR_RSTTX | US_CR_RXDIS | US_CR_TXDIS;
		// set serial line mode
		USART1->US_MR = 
			US_MR_CHMODE_NORMAL |// Normal Mode
			US_MR_USCLKS_MCK |   // Clock = MCK
			US_MR_CHRL_8_BIT |
			US_MR_PAR_NO |
			US_MR_NBSTOP_1_BIT;                   
									
		USART1->US_IDR = (US_IDR_RXRDY | US_IDR_TXRDY);

		if (debug == 0)
		{
			serial_set_handler(USART1_IRQn, & USART1_IRQHandler);
		}

		USART1->US_CR = US_CR_RXEN | US_CR_TXEN;	// разрешаем приёмник и передатчик.

	#elif HARDWARE_ARM_USEUART0

		// enable the clock of UART0
		PMC->PMC_PCER0 = 1UL << ID_UART0;

		HARDWARE_UART2_INITIALIZE();	/* Присоединить периферию к выводам */
		
		// reset the UART
		UART0->UART_CR = UART_CR_RSTRX | UART_CR_RSTTX | UART_CR_RXDIS | UART_CR_TXDIS;
		// set serial line mode
		UART0->UART_MR = 
			UART_MR_CHMODE_NORMAL |// Normal Mode
			//UART_MR_USCLKS_MCK |   // Clock = MCK
			//UART_MR_CHRL_8_BIT |
			UART_MR_PAR_NO |
			//UART_MR_NBSTOP_1_BIT |
			0;
									
		UART0->UART_IDR = (UART_IDR_RXRDY | UART_IDR_TXRDY);

		if (debug == 0)
		{
			serial_set_handler(UART0_IRQn, & UART0_IRQHandler);
		}

		UART0->UART_CR = UART_CR_RXEN | UART_CR_TXEN;	// разрешаем приёмник и передатчик.

	#elif HARDWARE_ARM_USEUART1

		// enable the clock of UART1
		PMC->PMC_PCER0 = 1UL << ID_UART1;

		HARDWARE_UART2_INITIALIZE();	/* Присоединить периферию к выводам */

		// reset the UART
		UART1->UART_CR = UART_CR_RSTRX | UART_CR_RSTTX | UART_CR_RXDIS | UART_CR_TXDIS;
		// set serial line mode
		UART1->UART_MR = 
			UART_MR_CHMODE_NORMAL |// Normal Mode
			//UART_MR_USCLKS_MCK |   // Clock = MCK
			//UART_MR_CHRL_8_BIT |
			UART_MR_PAR_NO |
			//UART_MR_NBSTOP_1_BIT |
			0;
									
		UART1->UART_IDR = (UART_IDR_RXRDY | UART_IDR_TXRDY);

		if (debug == 0)
		{
			serial_set_handler(UART1_IRQn, & UART1_IRQHandler);
		}

		UART1->UART_CR = UART_CR_RXEN | UART_CR_TXEN;	// разрешаем приёмник и передатчик.

	#else	/* HARDWARE_ARM_USExxx */
		#error Wrong HARDWARE_ARM_USExxx value
	#endif /* HARDWARE_ARM_USEUSART0 */


#elif CPUSTYLE_STM32F1XX

	RCC->APB1ENR |= RCC_APB1ENR_USART2EN; // Включение тактирования USART2.
	(void) RCC->APB1ENR;

	USART2->CR1 |= (USART_CR1_RE | USART_CR1_TE); // Transmitter Enable & Receiver Enables


	RCC->APB2ENR |= RCC_APB2ENR_AFIOEN;     //включить тактирование альтернативных функций
	(void) RCC->APB2ENR;

	HARDWARE_UART2_INITIALIZE();	/* Присоединить периферию к выводам */

	if (debug == 0)
	{
		serial_set_handler(USART2_IRQn, & USART2_IRQHandler);
	}

	USART2->CR1 |= USART_CR1_UE; // Включение USART2.

#elif CPUSTYLE_STM32H7XX

	RCC->APB1LENR |= RCC_APB1LENR_USART2EN; // Включение тактирования USART2.
	(void) RCC->APB1LENR;

	USART2->CR1 = 0;

#if WITHUARTFIFO
	USART2->CR1 |= USART_CR1_FIFOEN_Msk;
#else /* WITHUARTFIFO */
	USART2->CR1 &= ~ USART_CR1_FIFOEN_Msk;
#endif /* WITHUARTFIFO */

	USART2->CR1 |= (USART_CR1_RE | USART_CR1_TE); // Transmitter Enable & Receiver Enables

	HARDWARE_UART2_INITIALIZE();	/* Присоединить периферию к выводам */

	if (debug == 0)
	{
		serial_set_handler(USART2_IRQn, & USART2_IRQHandler);
	}

	USART2->CR1 |= USART_CR1_UE; // Включение USART2.

#elif CPUSTYLE_STM32F30X || CPUSTYLE_STM32F4XX || CPUSTYLE_STM32F0XX || CPUSTYLE_STM32L0XX || CPUSTYLE_STM32F7XX

	RCC->APB1ENR |= RCC_APB1ENR_USART2EN; // Включение тактирования USART2.
	(void) RCC->APB1ENR;

	USART2->CR1 |= (USART_CR1_RE | USART_CR1_TE); // Transmitter Enable & Receiver Enables

	HARDWARE_UART2_INITIALIZE();	/* Присоединить периферию к выводам */

	if (debug == 0)
	{
		serial_set_handler(USART2_IRQn, & USART2_IRQHandler);
	}

	USART2->CR1 |= USART_CR1_UE; // Включение USART2.

#elif CPUSTYLE_AT91SAM7S

	#if HARDWARE_ARM_USEUSART0
		// enable the clock of USART0
		AT91C_BASE_PMC->PMC_PCER = 1UL << AT91C_ID_US0;

		HARDWARE_UART2_INITIALIZE();	/* Присоединить периферию к выводам */

		// disable I/O pullup
		// reset the UART
		AT91C_BASE_US0->US_CR = AT91C_US_RSTRX | AT91C_US_RSTTX | AT91C_US_RXDIS | AT91C_US_TXDIS;
		// set serial line mode
		AT91C_BASE_US0->US_MR = 
							AT91C_US_OVER |
							AT91C_US_USMODE_NORMAL |// Normal Mode
						   AT91C_US_CLKS_CLOCK |   // Clock = MCK
						   AT91C_US_CHRL_8_BITS |
						   AT91C_US_PAR_NONE |
						   AT91C_US_NBSTOP_1_BIT;                   
		AT91C_BASE_US0->US_IDR = (AT91C_US_RXRDY | AT91C_US_TXRDY);

		{
			enum { irqID = AT91C_ID_US0 };
			// programming interrupts from ADC
			AT91C_BASE_AIC->AIC_IDCR = (1UL << irqID);
			AT91C_BASE_AIC->AIC_SVR [irqID] = (AT91_REG) AT91F_US0Handler;
			AT91C_BASE_AIC->AIC_SMR [irqID] = 
				(AT91C_AIC_SRCTYPE & AT91C_AIC_SRCTYPE_INT_HIGH_LEVEL) |
				(AT91C_AIC_PRIOR & AT91C_AIC_PRIOR_LOWEST);
			AT91C_BASE_AIC->AIC_ICCR = (1UL << irqID);		// clear pending interrupt
			AT91C_BASE_AIC->AIC_IECR = (1UL << irqID);	// enable inerrupt

		}

		AT91C_BASE_US0->US_CR = AT91C_US_RXEN  | AT91C_US_TXEN;	// разрешаем приёмник и передатчик.

	#elif HARDWARE_ARM_USEUSART1

		// enable the clock of USART2
		AT91C_BASE_PMC->PMC_PCER = 1UL << AT91C_ID_US1;

		HARDWARE_UART2_INITIALIZE();	/* Присоединить периферию к выводам */

		// enable uart pins on PIO
		AT91C_BASE_PIOA->PIO_ASR = AT91C_PA21_RXD1 | AT91C_PA22_TXD1; // assigns the 2 I/O lines to peripheral A function
		AT91C_BASE_PIOA->PIO_PDR = AT91C_PA21_RXD1 | AT91C_PA22_TXD1;	// enable peripheral control of PA21,PA22 (RXD1 and TXD1)
		// reset the UART
		AT91C_BASE_US1->US_CR = AT91C_US_RSTRX | AT91C_US_RSTTX | AT91C_US_RXDIS | AT91C_US_TXDIS;
		// set serial line mode
		AT91C_BASE_US1->US_MR = 
							AT91C_US_OVER |
							AT91C_US_USMODE_NORMAL |// Normal Mode
						   AT91C_US_CLKS_CLOCK |   // Clock = MCK
						   AT91C_US_CHRL_8_BITS |
						   AT91C_US_PAR_NONE |
						   AT91C_US_NBSTOP_1_BIT;          
					            
		AT91C_BASE_US1->US_IDR = (AT91C_US_RXRDY | AT91C_US_TXRDY);

		if (debug == 0)
		{
			serial_set_handler(AT91C_ID_US1, & AT91F_US1Handler);
		}

		AT91C_BASE_US1->US_CR = AT91C_US_RXEN | AT91C_US_TXEN;	// разрешаем приёмник и передатчик.
	#else	/* HARDWARE_ARM_USExxx */
		#error Wrong HARDWARE_ARM_USExxx value
	#endif /* HARDWARE_ARM_USEUSART0 */

#elif CPUSTYLE_ATMEGA_XXX4

	// USART initialization
	UCSR1B = (1U << RXEN1) | (1U << TXEN1) /* | (1U << UCSZ12) */;
	UCSR1C = (1U << UCSZ11) | (1U << UCSZ10);	// asynchronious mode, 8 bit.
	// enable pull-up registers for RXD and TXD pins: then rx or tx disabled, these pins disconnected fron UART
	PORTD |= ((1U << PD2) | (1U << PD3));
#if defined (DDRD2) && defined (DDRD3)
	DDRD &= ~ ((1U << DDRD2) | (1U << DDRD3));
#else
	DDRD &= ~ ((1U << DDD2) | (1U << DDD3));
#endif

#elif CPUSTYLE_ATMEGA128

	// USART initialization
	UCSR1B = (1U << RXEN1) | (1U << TXEN1) /* | (1U << UCSZ12) */;
	UCSR1C = (1U << UCSZ11) | (1U << UCSZ10);	// asynchronious mode, 8 bit.
	// enable pull-up registers for RXD and TXD pins: then rx or tx disabled, these pins disconnected fron UART
	//PORTE |= ((1U << PE0) | (1U << PE1));

#elif CPUSTYLE_ATXMEGAXXXA4

xxxx!;
	PORTE.DIRSET = PIN3_bm; // PE3 (TXD0) as output
	PORTE.DIRCLR = PIN2_bm; // PE2 (RXD0) as input
	PORTE_PIN2CTRL = (PORTE_PIN2CTRL & ~ PORT_OPC_gm) | PORT_OPC_PULLUP_gc;							// pin is pulled high

	USARTE1.CTRLC = USART_CMODE_ASYNCHRONOUS_gc | USART_PMODE_DISABLED_gc | USART_CHSIZE_8BIT_gc;
	USARTE1.CTRLB = USART_RXEN_bm | USART_TXEN_bm;


#elif CPUSTYLE_ATMEGA32

	#error WITHUART2HW with CPUSTYLE_ATMEGA not supported

#elif CPUSTYPE_TMS320F2833X
xxxx!;
	// Enable SCI-B clock
	PCLKCR0 |= (1U << 10);	// SCIBENCLK

	//SCIBCTL1 &= ~ (1U << 5);	// SW RESET on
	SCIBCTL1 |= (1U << 5);	// SW RESET off

	SCIBCCR =
			(7U << 0) |	// Data length = 8 bit
			(0U << 5) | // Parity enable
			(0U << 6) | // Evan/Odd parity
			(0U << 7);	// 0 - one stop bit, 1 - to stop bits


	tms320_hardware_piob_periph(
			(1UL << (35 % 32)) |	// SCITXDA
			(1UL << (36 % 32)),	// SCIRXDA
			1	// mux = 1
			);

	SCIBCTL1 |= (1U << 0) |	// RX enable
				(1U << 1);	// TX enable

#elif CPUSTYLE_R7S721

    /* ---- Supply clock to the SCIF(channel 1) ---- */
	CPG.STBCR4 &= ~ CPG_STBCR4_BIT_MSTP44;	// Module Stop 44 - SCIF3
	(void) CPG.STBCR4;			/* Dummy read */

	SCIF3.SCSCR = 0x0000;	/* SCIF transmitting and receiving operations stop, internal clock */

	SCIF3.SCSCR = (SCIF3.SCSCR & ~ 0x03) |	
		0x00 |						// internal clock
		0;

	/* ---- Serial status register(SCFSR2) setting ---- */
	/* ER,BRK,DR bit clear */
	(void) SCIF3.SCFSR;						// Перед сбросом бита xxx должно произойти его чтение в ненулевом состоянии
	SCIF3.SCFSR = ~ 0x0091;	// 0xFF6E;

	/* ---- Line status register (SCLSR2) setting ---- */
	/* ORER bit clear */
	//SCIF3.SCLSR.BIT.ORER  = 0;
	SCIF3.SCLSR &= ~ 0x0001;

	SCIF3.SCSMR = 
		0x00 |	/* 8-N-1 format */
		0;

	/* ---- FIFO control register (SCFCR2) setting ---- */
	/*  RTS output active trigger        :Initial value	*/
	/*  Receive FIFO data trigger        :1-data		*/
	/*  Transmit FIFO data trigger       :0-data		*/
	/*  Modem control enable             :Disabled		*/
	/*  Receive FIFO data register reset :Disabled		*/
	/*  Loop-back test                   :Disabled 		*/
	SCIF3.SCFCR = 0x0030;

	/* ---- Serial port register (SCSPTR2) setting ---- */
	/* Serial port  break output(SPB2IO)  1: Enabled */
	/* Serial port break data(SPB2DT)  1: High-level */
	//SCIF3.SCSPTR |= 0x0003;

	if (debug == 0)
	{
	   serial_set_handler(SCIFRXI3_IRQn, SCIFRXI3_IRQHandler);
	   serial_set_handler(SCIFTXI3_IRQn, SCIFTXI3_IRQHandler);
	}
	HARDWARE_UART2_INITIALIZE();	/* Присоединить периферию к выводам */

	SCIF3.SCSCR |= 0x0030;	// TE RE - SCIF3 transmitting and receiving operations are enabled */

#elif CPUSTYLE_STM32MP1

	RCC->MP_APB1ENSETR = RCC_MP_APB1ENSETR_USART2EN; // Включение тактирования USART2.
	(void) RCC->MP_APB1ENSETR;
	RCC->MP_APB1LPENSETR = RCC_MP_APB1LPENSETR_USART2LPEN; // Включение тактирования USART2.
	(void) RCC->MP_APB1LPENSETR;
	RCC->APB1RSTSETR = RCC_APB1RSTSETR_USART2RST; // Установить сброс USART2.
	(void) RCC->APB1RSTSETR;
	RCC->APB1RSTCLRR = RCC_APB1RSTCLRR_USART2RST; // Снять брос USART2.
	(void) RCC->APB1RSTCLRR;

	USART2->CR1 = 0;

#if WITHUARTFIFO
	USART2->CR1 |= USART_CR1_FIFOEN_Msk;
#else /* WITHUARTFIFO */
	USART2->CR1 &= ~ USART_CR1_FIFOEN_Msk;
#endif /* WITHUARTFIFO */

	USART2->CR1 |= (USART_CR1_RE | USART_CR1_TE); // Transmitter Enable & Receiver Enables

	HARDWARE_UART2_INITIALIZE();	/* Присоединить периферию к выводам */

	if (debug == 0)
	{
		serial_set_handler(USART2_IRQn, USART2_IRQHandler);
	}

	USART2->CR1 |= USART_CR1_UE; // Включение USART1.

#elif CPUSTYLE_XC7Z || CPUSTYLE_XCZU

	SCLR->SLCR_UNLOCK = 0x0000DF0DU;
	SCLR->APER_CLK_CTRL |= (0x01uL << 21);	// APER_CLK_CTRL.UART1_CPU_1XCLKACT
	//EMIT_MASKWRITE(0XF8000154, 0x00003F33U ,0x00001002U),	// UART_CLK_CTRL
	SCLR->UART_CLK_CTRL = (SCLR->UART_CLK_CTRL & ~ (0x00003F30U)) |
			((uint_fast32_t) SCLR_UART_CLK_CTRL_DIVISOR_VALUE << 8) | // DIVISOR
			(0x00uL << 4) |	// SRCSEL - 0x: IO PLL
			(0x02) |	// CLKACT1 - UART 1 reference clock active
			0;

	uint32_t r; // Temporary value variable
	r = UART1->CR;
	r &= ~(XUARTPS_CR_TX_EN | XUARTPS_CR_RX_EN); // Clear Tx & Rx Enable
	r |= XUARTPS_CR_RX_DIS | XUARTPS_CR_TX_DIS; // Tx & Rx Disable
	UART1->CR = r;

	UART1->MR = 0;
	UART1->MR &= ~XUARTPS_MR_CLKSEL; // Clear "Input clock selection" - 0: clock source is uart_ref_clk
	UART1->MR |= XUARTPS_MR_CHARLEN_8_BIT; 	// Set "8 bits data"
	UART1->MR |= XUARTPS_MR_PARITY_NONE; 	// Set "No parity mode"
	UART1->MR |= XUARTPS_MR_STOPMODE_1_BIT; // Set "1 stop bit"
	UART1->MR |= XUARTPS_MR_CHMODE_NORM; 	// Set "Normal mode"

	UART1->CR |= (XUARTPS_CR_TXRST | XUARTPS_CR_RXRST); // TX & RX logic reset

	r = UART1->CR;
	r |= XUARTPS_CR_RX_EN | XUARTPS_CR_TX_EN; // Set TX & RX enabled
	r &= ~(XUARTPS_CR_RX_DIS | XUARTPS_CR_TX_DIS); // Clear TX & RX disabled
	UART1->CR = r;

	HARDWARE_UART2_INITIALIZE();

#else
	#error Undefined CPUSTYLE_XXX
#endif

}

#endif /* WITHUART2HW */

#if WITHUART4HW

#if CPUSTYLE_ATSAM3S || CPUSTYLE_ATSAM4S

	void RAMFUNC_NONILINE USART4_Handler(void)
	{
		const uint_fast32_t csr = USART4->US_CSR;

		if (csr & US_CSR_RXRDY)
			HARDWARE_UART4_ONRXCHAR(USART1->US_RHR);
		if (csr & US_CSR_TXRDY)
			HARDWARE_UART4_ONTXCHAR(USART1);
	}

#elif CPUSTYLE_STM32F1XX || CPUSTYLE_STM32F4XX

	void RAMFUNC_NONILINE UART4_IRQHandler(void)
	{
		const uint_fast32_t sr = UART4->SR;

		if (sr & (USART_SR_RXNE | USART_SR_ORE | USART_SR_FE | USART_SR_NE))
			HARDWARE_UART4_ONRXCHAR(UART4->DR);
		if (sr & (USART_SR_ORE | USART_SR_FE | USART_SR_NE))
			HARDWARE_UART4_ONOVERFLOW();
		if (sr & USART_SR_TXE)
			HARDWARE_UART4_ONTXCHAR(UART4);
	}

#elif CPUSTYLE_STM32H7XX || CPUSTYLE_STM32MP1

	void RAMFUNC_NONILINE UART4_IRQHandler(void)
	{
		const uint_fast32_t isr = UART4->ISR;
		const uint_fast32_t cr1 = UART4->CR1;

		if (cr1 & USART_CR1_RXNEIE)
		{
			if (isr & USART_ISR_RXNE_RXFNE)
				HARDWARE_UART4_ONRXCHAR(UART4->RDR);
			if (isr & USART_ISR_ORE)
			{
				UART4->ICR = USART_ICR_ORECF;
				HARDWARE_UART4_ONOVERFLOW();
			}
			if (isr & USART_ISR_FE)
				UART4->ICR = USART_ICR_FECF;
		}
		if (cr1 & USART_CR1_TXEIE)
		{
			if (isr & USART_ISR_TXE_TXFNF)
				HARDWARE_UART4_ONTXCHAR(UART4);
		}
	}

#elif CPUSTYLE_STM32F30X || CPUSTYLE_STM32F0XX || CPUSTYLE_STM32L0XX || CPUSTYLE_STM32F7XX

	void RAMFUNC_NONILINE UART4_IRQHandler(void)
	{
		const uint_fast32_t isr = UART4->ISR;

		if (isr & USART_ISR_RXNE)
			HARDWARE_UART4_ONRXCHAR(UART4->RDR);
		if (isr & USART_ISR_ORE)
		{
			UART4->ICR = USART_ICR_ORECF;
			HARDWARE_UART4_ONOVERFLOW();
		}
		if (isr & USART_ISR_FE)
			UART4->ICR = USART_ICR_FECF;
		if (isr & USART_ISR_TXE)
			HARDWARE_UART4_ONTXCHAR(UART4);
	}

#elif CPUSTYLE_AT91SAM7S

	#if HARDWARE_ARM_USEUSART0
		static RAMFUNC_NONILINE void AT91F_US0Handler(void)
		{
			const uint_fast32_t csr = AT91C_BASE_US0->US_CSR;

			if (csr & AT91C_US_RXRDY)
				HARDWARE_UART4_ONRXCHAR(AT91C_BASE_US0->US_RHR);
			if (csr & AT91C_US_TXRDY)
				HARDWARE_UART4_ONTXCHAR(AT91C_BASE_US0);
		}
	#elif HARDWARE_ARM_USEUSART1
		static RAMFUNC_NONILINE void AT91F_US1Handler(void)
		{
			const uint_fast32_t csr = AT91C_BASE_US1->US_CSR;

			if (csr & AT91C_US_RXRDY)
				HARDWARE_UART4_ONRXCHAR(AT91C_BASE_US1->US_RHR);
			if (csr & AT91C_US_TXRDY)
				HARDWARE_UART4_ONTXCHAR(AT91C_BASE_US1);
		}
	#else	/* HARDWARE_ARM_USExxx */
		#error Wrong HARDWARE_ARM_USExxx value
	#endif		/* HARDWARE_ARM_USEUSART0 */

#elif CPUSTYLE_ATMEGA328

	ISR(USART_RX_vect)
	{
		HARDWARE_UART4_ONRXCHAR(UDR0);
	}

	ISR(USART_TX_vect)
	{
		HARDWARE_UART4_ONTXCHAR(NULL);
	}

#elif CPUSTYLE_ATMEGA_XXX4

	ISR(USART1_RX_vect)
	{
		HARDWARE_UART4_ONRXCHAR(UDR0);
	}

	ISR(USART1_TX_vect)
	{
		HARDWARE_UART4_ONTXCHAR(NULL);
	}

#elif CPUSTYLE_ATMEGA32

	ISR(USART_RXC_vect)
	{
		HARDWARE_UART4_ONRXCHAR(UDR);
	}

	ISR(USART_TXC_vect)
	{
		HARDWARE_UART4_ONTXCHAR(NULL);
	}

#elif CPUSTYLE_ATMEGA128

	ISR(USART1_RX_vect)
	{
		HARDWARE_UART4_ONRXCHAR(UDR1);
	}

	ISR(USART1_TX_vect)
	{
		HARDWARE_UART4_ONTXCHAR(NULL);
	}

#elif CPUSTYLE_ATXMEGAXXXA4

	ISR(USARTE1_RXC_vect)
	{
		HARDWARE_UART4_ONRXCHAR(USARTE1.DATA);
	}

	ISR(USARTE1_DRE_vect)
	{
		HARDWARE_UART4_ONTXCHAR(& USARTE1);
	}

#elif CPUSTYLE_R7S721

	// Приём символа он последовательного порта
	static void SCIFRXI3_IRQHandler(void)
	{
		(void) SCIF3.SCFSR;						// Перед сбросом бита RDF должно произойти его чтение в ненулевом состоянии
		SCIF3.SCFSR = (uint16_t) ~ SCIF3_SCFSR_RDF;	// RDF=0 читать незачем (в примерах странное - сбрасывабтся и другие биты)
		uint_fast8_t n = (SCIF3.SCFDR & SCIF3_SCFDR_R) >> SCIF3_SCFDR_R_SHIFT;
		while (n --)
			HARDWARE_UART4_ONRXCHAR(SCIF3.SCFRDR & SCIF3_SCFRDR_D);
	}

	// Передача символа в последовательный порт
	static void SCIFTXI3_IRQHandler(void)
	{
		HARDWARE_UART4_ONTXCHAR(& SCIF3);
	}

#else

	#error Undefined CPUSTYLE_XXX

#endif	/* CPUSTYLE_ATMEGA_XXX4 */


/* вызывается из обработчика прерываний или при запрещённых прерываниях. */
/*
		system_disableIRQ();
		system_enableIRQ();
*/
/* Разрешение/запрещение прерывания по передаче символа */
void hardware_uart4_enabletx(uint_fast8_t state)
{
#if CPUSTYLE_ATSAM3S || CPUSTYLE_ATSAM4S

	#if HARDWARE_ARM_USEUSART0

		if (state)
			USART0->US_IER = US_IER_TXRDY;
		else
			USART0->US_IDR = US_IDR_TXRDY;

	#elif HARDWARE_ARM_USEUSART1

		if (state)
			USART1->US_IER = US_IER_TXRDY;
		else
			USART1->US_IDR = US_IDR_TXRDY;

	#elif HARDWARE_ARM_USEUART0

		if (state)
			UART0->UART_IER = UART_IER_TXRDY;
		else
			UART0->UART_IDR = UART_IDR_TXRDY;

	#elif HARDWARE_ARM_USEUART1

		if (state)
			UART1->UART_IER = UART_IER_TXRDY;
		else
			UART1->UART_IDR = UART_IDR_TXRDY;

	#else	/* HARDWARE_ARM_USExxx */
		#error Wrong HARDWARE_ARM_USExxx value
	#endif /* HARDWARE_ARM_USEUSART0 */

#elif CPUSTYLE_AT91SAM7S

	#if HARDWARE_ARM_USEUSART0

	if (state)
		AT91C_BASE_US0->US_IER = AT91C_US_TXRDY;
	else
		AT91C_BASE_US0->US_IDR = AT91C_US_TXRDY;

	#elif HARDWARE_ARM_USEUSART1

	if (state)
		AT91C_BASE_US1->US_IER = AT91C_US_TXRDY;
	else
		AT91C_BASE_US1->US_IDR = AT91C_US_TXRDY;

	#else	/* HARDWARE_ARM_USExxx */
		#error Wrong HARDWARE_ARM_USExxx value
	#endif /* HARDWARE_ARM_USEUSART0 */

#elif CPUSTYLE_STM32F || CPUSTYLE_STM32MP1

	if (state)
		UART4->CR1 |= USART_CR1_TXEIE;
	else
		UART4->CR1 &= ~ USART_CR1_TXEIE;

#elif CPUSTYLE_ATMEGA_XXX4

	/* Used USART 1 */
	if (state)
	{
		UCSR1B |= (1U << TXCIE1);
		HARDWARE_UART4_ONTXCHAR(NULL);	// initiate 1-st character sending
	}
	else
	{
		UCSR1B &= ~ (1U << TXCIE1);
	}

#elif CPUSTYLE_ATMEGA32

	#error WITHUART2HW with CPUSTYLE_ATMEGA32 not supported
	if (state)
	{
		UCSRB |= (1U << TXCIE);
		HARDWARE_UART4_ONTXCHAR(NULL);	// initiate 1-st character sending
	}
	else
	{
		UCSRB &= ~ (1U << TXCIE);
	}

#elif CPUSTYLE_ATMEGA128

	/* Used USART 1 */
	if (state)
	{
		UCSR1B |= (1U << TXCIE1);
		HARDWARE_UART4_ONTXCHAR(NULL);	// initiate 1-st character sending
	}
	else
	{
		UCSR1B &= ~ (1U << TXCIE1);
	}

#elif CPUSTYLE_ATXMEGAXXXA4

	if (state)
		USARTE1.CTRLA = (USARTE1.CTRLA & ~ USART_DREINTLVL_gm) | USART_DREINTLVL_LO_gc;
	else
		USARTE1.CTRLA = (USARTE1.CTRLA & ~ USART_DREINTLVL_gm) | USART_DREINTLVL_OFF_gc;

#elif CPUSTYPE_TMS320F2833X

	if (state)
		SCIBCTL2 |= (1U << 0);	// TX INT ENA
	else
		SCIBCTL2 &= ~ (1U << 0); // TX INT ENA

#elif CPUSTYLE_R7S721

	if (state)
		SCIF3.SCSCR |= (1U << 7);	// TIE Transmit Interrupt Enable
	else
		SCIF3.SCSCR &= ~ (1U << 7);	// TIE Transmit Interrupt Enable

#else
	#error Undefined CPUSTYLE_XXX
#endif
}

/* вызывается из обработчика прерываний или при запрещённых прерываниях. */
/* Разрешение/запрещение прерываний про приёму символа */
void hardware_uart4_enablerx(uint_fast8_t state)
{
#if CPUSTYLE_ATSAM3S || CPUSTYLE_ATSAM4S

	#if HARDWARE_ARM_USEUSART0

		if (state)
			USART0->US_IER = US_IER_RXRDY;
		else
			USART0->US_IDR = US_IDR_RXRDY;

	#elif HARDWARE_ARM_USEUSART1

		if (state)
			USART1->US_IER = US_IER_RXRDY;
		else
			USART1->US_IDR = US_IDR_RXRDY;

	#elif HARDWARE_ARM_USEUART0

		if (state)
			UART0->UART_IER = UART_IER_RXRDY;
		else
			UART0->UART_IDR = UART_IDR_RXRDY;

	#elif HARDWARE_ARM_USEUART1

		if (state)
			UART1->UART_IER = UART_IER_RXRDY;
		else
			UART1->UART_IDR = UART_IDR_RXRDY;

	#else	/* HARDWARE_ARM_USExxx */
		#error Wrong HARDWARE_ARM_USExxx value
	#endif /* HARDWARE_ARM_USEUSART0 */

#elif CPUSTYLE_AT91SAM7S

	#if HARDWARE_ARM_USEUSART1

	if (state)
		AT91C_BASE_US1->US_IER = AT91C_US_RXRDY;
	else
		AT91C_BASE_US1->US_IDR = AT91C_US_RXRDY;

	#else	/* HARDWARE_ARM_USExxx */
		#error Wrong HARDWARE_ARM_USExxx value
	#endif /* HARDWARE_ARM_USEUSART0 */


#elif CPUSTYLE_ATMEGA_XXX4
	/* Used USART 1 */
	if (state)
		UCSR1B |= (1U << RXCIE1);
	else
		UCSR1B &= ~ (1U << RXCIE1);

#elif CPUSTYLE_ATMEGA32

	#error WITHUART2HW with CPUSTYLE_ATMEGA32 not supported
	if (state)
		UCSRB |= (1U << RXCIE);
	else
		UCSRB &= ~ (1U << RXCIE);

#elif CPUSTYLE_ATMEGA128

	/* Used USART 0 */
	if (state)
		UCSR1B |= (1U << RXCIE1);
	else
		UCSR1B &= ~ (1U << RXCIE1);

#elif CPUSTYLE_ATXMEGAXXXA4

	if (state)
		USARTE1.CTRLA = (USARTE1.CTRLA & ~ USART_RXCINTLVL_gm) | USART_RXCINTLVL_LO_gc;
	else
		USARTE1.CTRLA = (USARTE1.CTRLA & ~ USART_RXCINTLVL_gm) | USART_RXCINTLVL_OFF_gc;

#elif CPUSTYLE_STM32F || CPUSTYLE_STM32MP1

	if (state)
		UART4->CR1 |= USART_CR1_RXNEIE;
	else
		UART4->CR1 &= ~ USART_CR1_RXNEIE;

#elif CPUSTYPE_TMS320F2833X

	if (state)
		SCIBCTL2 |= (1U << 1);	// RX/BK INT ENA
	else
		SCIBCTL2 &= ~ (1U << 1); // RX/BK INT ENA

#elif CPUSTYLE_R7S721

	if (state)
		SCIF3.SCSCR |= (1U << 6);	// RIE Receive Interrupt Enable
	else
		SCIF3.SCSCR &= ~ (1U << 6);	// RIE Receive Interrupt Enable

#else
	#error Undefined CPUSTYLE_XXX

#endif
}


/* передача символа из обработчика прерывания готовности передатчика */
void hardware_uart4_tx(void * ctx, uint_fast8_t c)
{
#if CPUSTYLE_ATSAM3S || CPUSTYLE_ATSAM4S

	#if HARDWARE_ARM_USEUSART1
		USART1->US_THR = c;
	#elif HARDWARE_ARM_USEUART1
		UART1->UART_THR = c;
	#else	/* HARDWARE_ARM_USExxx */
		#error Wrong HARDWARE_ARM_USExxx value
	#endif

#elif CPUSTYLE_AT91SAM7S

	#if HARDWARE_ARM_USEUSART1
		AT91C_BASE_US1->US_THR = c;
	#else	/* HARDWARE_ARM_USExxx */
		#error Wrong HARDWARE_ARM_USExxx value
	#endif

#elif CPUSTYLE_ATMEGA128
	UDR1 = c;

#elif CPUSTYLE_ATMEGA_XXX4

	UDR1 = c;

#elif CPUSTYLE_ATMEGA32

	#error WITHUART2HW with CPUSTYLE_ATMEGA32 not supported
	UDR = c;

#elif CPUSTYLE_ATXMEGAXXXA4

	USARTE1.DATA = c;

#elif CPUSTYLE_STM32F1XX || CPUSTYLE_STM32F4XX

	UART4->DR = c;

#elif CPUSTYLE_STM32F30X || CPUSTYLE_STM32F0XX || CPUSTYLE_STM32L0XX || CPUSTYLE_STM32H7XX || CPUSTYLE_STM32F7XX || CPUSTYLE_STM32MP1

	UART4->TDR = c;

#elif CPUSTYPE_TMS320F2833X

	SCIBTXBUF = c;

#elif CPUSTYLE_R7S721

	(void) SCIF3.SCFSR;			// Перед сбросом бита TDFE должно произойти его чтение в ненулевом состоянии
	SCIF3.SCFTDR = c;
	SCIF3.SCFSR = (uint16_t) ~ (1U << SCIF3_SCFSR_TDFE_SHIFT);	// TDFE=0 читать незачем (в примерах странное)

#else
	#error Undefined CPUSTYLE_XXX
#endif
}



/* приём символа, если готов порт */
uint_fast8_t
hardware_uart4_getchar(char * cp)
{
#if CPUSTYLE_ATSAM3S || CPUSTYLE_ATSAM4S

	#if HARDWARE_ARM_USEUSART0
		if ((USART0->US_CSR & US_CSR_RXRDY) == 0)
			return 0;
		* cp = USART0->US_RHR;
	#elif HARDWARE_ARM_USEUSART1
		if ((USART1->US_CSR & US_CSR_RXRDY) == 0)
			return 0;
		* cp = USART1->US_RHR;
	#elif HARDWARE_ARM_USEUART0
		if ((UART0->UART_SR & UART_SR_RXRDY) == 0)
			return 0;
		* cp = UART0->UART_RHR;
	#elif HARDWARE_ARM_USEUART1
		if ((UART1->UART_SR & UART_SR_RXRDY) == 0)
			return 0;
		* cp = UART1->UART_RHR;
	#else	/* HARDWARE_ARM_USExxx */
		#error Wrong HARDWARE_ARM_USExxx value
	#endif

#elif CPUSTYLE_AT91SAM7S

	#if HARDWARE_ARM_USEUSART1
		if ((AT91C_BASE_US1->US_CSR & AT91C_US_RXRDY) == 0)
			return 0;
		* cp = AT91C_BASE_US1->US_RHR;
	#else	/* HARDWARE_ARM_USExxx */
		#error Wrong HARDWARE_ARM_USExxx value
	#endif

#elif CPUSTYLE_ATXMEGAXXXA4

	if ((USARTE1.STATUS & (1 << USART_RXCIF_bp)) == 0)
			return 0;
	* cp = USARTE1.DATA;

#elif CPUSTYLE_ATMEGA128

	if ((UCSR1A & (1 << RXC1)) == 0)
			return 0;
	* cp = UDR1;

#elif CPUSTYLE_ATMEGA_XXX4

	if ((UCSR1A & (1 << RXC1)) == 0)
			return 0;
	* cp = UDR1;

#elif CPUSTYLE_ATMEGA32

	#error WITHUART2HW with CPUSTYLE_ATMEGA32 not supported

	if ((UCSRA & (1 << RXC)) == 0)
			return 0;
	* cp = UDR;

#elif CPUSTYLE_STM32F1XX || CPUSTYLE_STM32F4XX

	if ((UART4->SR & (USART_SR_RXNE | USART_SR_ORE | USART_SR_FE | USART_SR_NE)) == 0)
		return 0;
	* cp = UART4->DR;

#elif CPUSTYLE_STM32H7XX || CPUSTYLE_STM32MP1

	const uint_fast32_t isr = UART4->ISR;
	if (isr & USART_ISR_ORE)
		UART4->ICR = USART_ICR_ORECF;
	if (isr & USART_ISR_FE)
		UART4->ICR = USART_ICR_FECF;
	if ((isr & USART_ISR_RXNE_RXFNE) == 0)
		return 0;
	* cp = UART4->RDR;

#elif CPUSTYLE_STM32F30X || CPUSTYLE_STM32F0XX || CPUSTYLE_STM32L0XX || CPUSTYLE_STM32F7XX

	const uint_fast32_t isr = UART4->ISR;
	if (isr & USART_ISR_ORE)
		UART4->ICR = USART_ICR_ORECF;
	if (isr & USART_ISR_FE)
		UART4->ICR = USART_ICR_FECF;
	if ((isr & USART_ISR_RXNE) == 0)
		return 0;
	* cp = UART4->RDR;

#elif CPUSTYPE_TMS320F2833X

	if ((SCIBRXST & (1U << 6)) == 0)	// Wait for RXRDY bit
		return 0;
	* cp = SCIBRXBUF;

#elif CPUSTYLE_R7S721

	if ((SCIF3.SCFSR & (1U << 1)) == 0)	// RDF
		return 0;
	* cp = SCIF3.SCFRDR;
	SCIF3.SCFSR = (uint16_t) ~ (1U << 1);	// RDF=0 читать незачем (в примерах странное)

#else
	#error Undefined CPUSTYLE_XXX
#endif
	return 1;
}

/* передача символа если готов порт */
uint_fast8_t
hardware_uart4_putchar(uint_fast8_t c)
{
#if CPUSTYLE_ATSAM3S || CPUSTYLE_ATSAM4S

	#if HARDWARE_ARM_USEUSART0
		if ((USART0->US_CSR & US_CSR_TXRDY) == 0)
			return 0;
		USART0->US_THR = c;
	#elif HARDWARE_ARM_USEUSART1
		if ((USART1->US_CSR & US_CSR_TXRDY) == 0)
			return 0;
		USART1->US_THR = c;
	#elif HARDWARE_ARM_USEUART0
		if ((UART0->UART_SR & UART_SR_TXRDY) == 0)
			return 0;
		UART0->UART_THR = c;
	#elif HARDWARE_ARM_USEUART1
		if ((UART1->UART_SR & UART_SR_TXRDY) == 0)
			return 0;
		UART1->UART_THR = c;
	#else	/* HARDWARE_ARM_USExxx */
		#error Wrong HARDWARE_ARM_USExxx value
	#endif

#elif CPUSTYLE_AT91SAM7S

	#if HARDWARE_ARM_USEUSART0
		if ((AT91C_BASE_US0->US_CSR & AT91C_US_TXRDY) == 0)
			return 0;
		AT91C_BASE_US0->US_THR = c;
	#elif HARDWARE_ARM_USEUSART1
		if ((AT91C_BASE_US1->US_CSR & AT91C_US_TXRDY) == 0)
			return 0;
		AT91C_BASE_US1->US_THR = c;
	#else	/* HARDWARE_ARM_USExxx */
		#error Wrong HARDWARE_ARM_USExxx value
	#endif

#elif CPUSTYLE_ATXMEGAXXXA4

	if ((USARTE0.STATUS & USART_DREIF_bm) == 0)
		return 0;
	USARTE0.DATA = c;

#elif CPUSTYLE_ATMEGA_XXX4

	if ((UCSR0A & (1 << UDRE0)) == 0)
		return 0;
	UDR0 = c;

#elif CPUSTYLE_ATMEGA128

	if ((UCSR0A & (1 << UDRE0)) == 0)
		return 0;
	UDR0 = c;

#elif CPUSTYLE_ATMEGA32

	#error WITHUART2HW with CPUSTYLE_ATMEGA32 not supported

	if ((UCSRA & (1 << UDRE)) == 0)
		return 0;
	UDR = c;

#elif CPUSTYLE_STM32F1XX || CPUSTYLE_STM32F4XX

	if ((UART4->SR & USART_SR_TXE) == 0)
		return 0;
	UART4->DR = c;

#elif CPUSTYLE_STM32H7XX || CPUSTYLE_STM32MP1

	if ((UART4->ISR & USART_ISR_TXE_TXFNF) == 0)
		return 0;
	UART4->TDR = c;

#elif CPUSTYLE_STM32F30X || CPUSTYLE_STM32F0XX || CPUSTYLE_STM32L0XX || CPUSTYLE_STM32F7XX

	if ((UART4->ISR & USART_ISR_TXE) == 0)
		return 0;
	UART4->TDR = c;

#elif CPUSTYPE_TMS320F2833X

	if ((SCIBCTL2 & (1U << 7)) == 0)	// wait for TXRDY bit
		return 0;
	SCIBTXBUF = c;

#elif CPUSTYLE_R7S721

	if ((SCIF3.SCFSR & (1U << SCIF3_SCFSR_TDFE_SHIFT)) == 0)	// Перед сбросом бита TDFE должно произойти его чтение в ненулевом состоянии
		return 0;
	SCIF3.SCFTDR = c;
	SCIF3.SCFSR = (uint16_t) ~ (1U << SCIF3_SCFSR_TDFE_SHIFT);	// TDFE=0 читать незачем (в примерах странное)

#else
	#error Undefined CPUSTYLE_XXX
#endif
	return 1;
}

void hardware_uart4_initialize(uint_fast8_t debug)
{
#if CPUSTYLE_ATSAM3S || CPUSTYLE_ATSAM4S

	#if HARDWARE_ARM_USEUSART0
		// enable the clock of USART0
		PMC->PMC_PCER0 = 1UL << ID_USART0;

		HARDWARE_UART4_INITIALIZE();	/* Присоединить периферию к выводам */

		// reset the UART
		USART0->US_CR = US_CR_RSTRX | US_CR_RSTTX | US_CR_RXDIS | US_CR_TXDIS;
		// set serial line mode
		USART0->US_MR =
			US_MR_CHMODE_NORMAL |// Normal Mode
			US_MR_USCLKS_MCK |   // Clock = MCK
			US_MR_CHRL_8_BIT |
			US_MR_PAR_NO |
			US_MR_NBSTOP_1_BIT;

		USART0->US_IDR = (US_IDR_RXRDY | US_IDR_TXRDY);

		if (debug == 0)
		{
			serial_set_handler(USART0_IRQn, & USART0_IRQHandler);
		}

		USART0->US_CR = US_CR_RXEN | US_CR_TXEN;	// разрешаем приёмник и передатчик.

	#elif HARDWARE_ARM_USEUSART1
		// enable the clock of USART1
		PMC->PMC_PCER0 = 1UL << ID_USART1;

		HARDWARE_UART4_INITIALIZE();	/* Присоединить периферию к выводам */

		// reset the UART
		USART1->US_CR = US_CR_RSTRX | US_CR_RSTTX | US_CR_RXDIS | US_CR_TXDIS;
		// set serial line mode
		USART1->US_MR =
			US_MR_CHMODE_NORMAL |// Normal Mode
			US_MR_USCLKS_MCK |   // Clock = MCK
			US_MR_CHRL_8_BIT |
			US_MR_PAR_NO |
			US_MR_NBSTOP_1_BIT;

		USART1->US_IDR = (US_IDR_RXRDY | US_IDR_TXRDY);

		if (debug == 0)
		{
			serial_set_handler(USART1_IRQn, & USART1_IRQHandler);
		}

		USART1->US_CR = US_CR_RXEN | US_CR_TXEN;	// разрешаем приёмник и передатчик.

	#elif HARDWARE_ARM_USEUART0

		// enable the clock of UART0
		PMC->PMC_PCER0 = 1UL << ID_UART0;

		HARDWARE_UART4_INITIALIZE();	/* Присоединить периферию к выводам */

		// reset the UART
		UART0->UART_CR = UART_CR_RSTRX | UART_CR_RSTTX | UART_CR_RXDIS | UART_CR_TXDIS;
		// set serial line mode
		UART0->UART_MR =
			UART_MR_CHMODE_NORMAL |// Normal Mode
			//UART_MR_USCLKS_MCK |   // Clock = MCK
			//UART_MR_CHRL_8_BIT |
			UART_MR_PAR_NO |
			//UART_MR_NBSTOP_1_BIT |
			0;

		UART0->UART_IDR = (UART_IDR_RXRDY | UART_IDR_TXRDY);

		if (debug == 0)
		{
			serial_set_handler(UART0_IRQn, & UART0_IRQHandler);
		}

		UART0->UART_CR = UART_CR_RXEN | UART_CR_TXEN;	// разрешаем приёмник и передатчик.

	#elif HARDWARE_ARM_USEUART1

		// enable the clock of UART1
		PMC->PMC_PCER0 = 1UL << ID_UART1;

		HARDWARE_UART4_INITIALIZE();	/* Присоединить периферию к выводам */

		// reset the UART
		UART1->UART_CR = UART_CR_RSTRX | UART_CR_RSTTX | UART_CR_RXDIS | UART_CR_TXDIS;
		// set serial line mode
		UART1->UART_MR =
			UART_MR_CHMODE_NORMAL |// Normal Mode
			//UART_MR_USCLKS_MCK |   // Clock = MCK
			//UART_MR_CHRL_8_BIT |
			UART_MR_PAR_NO |
			//UART_MR_NBSTOP_1_BIT |
			0;

		UART1->UART_IDR = (UART_IDR_RXRDY | UART_IDR_TXRDY);

		if (debug == 0)
		{
			serial_set_handler(UART1_IRQn, & UART1_IRQHandler);
		}

		UART1->UART_CR = UART_CR_RXEN | UART_CR_TXEN;	// разрешаем приёмник и передатчик.

	#else	/* HARDWARE_ARM_USExxx */
		#error Wrong HARDWARE_ARM_USExxx value
	#endif /* HARDWARE_ARM_USEUSART0 */


#elif CPUSTYLE_STM32F1XX

	RCC->APB1ENR |= RCC_APB1ENR_UART4EN; // Включение тактирования UART4.
	(void) RCC->APB1ENR;

	UART4->CR1 |= (USART_CR1_RE | USART_CR1_TE); // Transmitter Enable & Receiver Enables


	RCC->APB2ENR |= RCC_APB2ENR_AFIOEN;     //включить тактирование альтернативных функций
	(void) RCC->APB2ENR;

	HARDWARE_UART4_INITIALIZE();	/* Присоединить периферию к выводам */

	if (debug == 0)
	{
		serial_set_handler(UART4_IRQn, & UART4_IRQHandler);
	}

	UART4->CR1 |= USART_CR1_UE; // Включение UART4.

#elif CPUSTYLE_STM32H7XX

	RCC->APB1LENR |= RCC_APB1LENR_UART4EN; // Включение тактирования UART4.
	(void) RCC->APB1LENR;

	UART4->CR1 = 0;

#if WITHUARTFIFO
	UART4->CR1 |= USART_CR1_FIFOEN_Msk;
#else /* WITHUARTFIFO */
	UART4->CR1 &= ~ USART_CR1_FIFOEN_Msk;
#endif /* WITHUARTFIFO */

	UART4->CR1 |= (USART_CR1_RE | USART_CR1_TE); // Transmitter Enable & Receiver Enables

	HARDWARE_UART4_INITIALIZE();	/* Присоединить периферию к выводам */

	if (debug == 0)
	{
		serial_set_handler(UART4_IRQn, & UART4_IRQHandler);
	}

	UART4->CR1 |= USART_CR1_UE; // Включение UART4.

#elif CPUSTYLE_STM32F30X || CPUSTYLE_STM32F4XX || CPUSTYLE_STM32F0XX || CPUSTYLE_STM32L0XX || CPUSTYLE_STM32F7XX

	RCC->APB1ENR |= RCC_APB1ENR_UART4EN; // Включение тактирования UART4.
	(void) RCC->APB1ENR;

	UART4->CR1 |= (USART_CR1_RE | USART_CR1_TE); // Transmitter Enable & Receiver Enables

	HARDWARE_UART4_INITIALIZE();	/* Присоединить периферию к выводам */

	if (debug == 0)
	{
		serial_set_handler(UART4_IRQn, & UART4_IRQHandler);
	}

	UART4->CR1 |= USART_CR1_UE; // Включение UART4.

#elif CPUSTYLE_AT91SAM7S

	#if HARDWARE_ARM_USEUSART0
		// enable the clock of USART0
		AT91C_BASE_PMC->PMC_PCER = 1UL << AT91C_ID_US0;

		HARDWARE_UART4_INITIALIZE();	/* Присоединить периферию к выводам */

		// disable I/O pullup
		// reset the UART
		AT91C_BASE_US0->US_CR = AT91C_US_RSTRX | AT91C_US_RSTTX | AT91C_US_RXDIS | AT91C_US_TXDIS;
		// set serial line mode
		AT91C_BASE_US0->US_MR =
							AT91C_US_OVER |
							AT91C_US_USMODE_NORMAL |// Normal Mode
						   AT91C_US_CLKS_CLOCK |   // Clock = MCK
						   AT91C_US_CHRL_8_BITS |
						   AT91C_US_PAR_NONE |
						   AT91C_US_NBSTOP_1_BIT;
		AT91C_BASE_US0->US_IDR = (AT91C_US_RXRDY | AT91C_US_TXRDY);

		{
			enum { irqID = AT91C_ID_US0 };
			// programming interrupts from ADC
			AT91C_BASE_AIC->AIC_IDCR = (1UL << irqID);
			AT91C_BASE_AIC->AIC_SVR [irqID] = (AT91_REG) AT91F_US0Handler;
			AT91C_BASE_AIC->AIC_SMR [irqID] =
				(AT91C_AIC_SRCTYPE & AT91C_AIC_SRCTYPE_INT_HIGH_LEVEL) |
				(AT91C_AIC_PRIOR & AT91C_AIC_PRIOR_LOWEST);
			AT91C_BASE_AIC->AIC_ICCR = (1UL << irqID);		// clear pending interrupt
			AT91C_BASE_AIC->AIC_IECR = (1UL << irqID);	// enable inerrupt

		}

		AT91C_BASE_US0->US_CR = AT91C_US_RXEN  | AT91C_US_TXEN;	// разрешаем приёмник и передатчик.

	#elif HARDWARE_ARM_USEUSART1

		// enable the clock of UART4
		AT91C_BASE_PMC->PMC_PCER = 1UL << AT91C_ID_US1;

		HARDWARE_UART4_INITIALIZE();	/* Присоединить периферию к выводам */

		// enable uart pins on PIO
		AT91C_BASE_PIOA->PIO_ASR = AT91C_PA21_RXD1 | AT91C_PA22_TXD1; // assigns the 2 I/O lines to peripheral A function
		AT91C_BASE_PIOA->PIO_PDR = AT91C_PA21_RXD1 | AT91C_PA22_TXD1;	// enable peripheral control of PA21,PA22 (RXD1 and TXD1)
		// reset the UART
		AT91C_BASE_US1->US_CR = AT91C_US_RSTRX | AT91C_US_RSTTX | AT91C_US_RXDIS | AT91C_US_TXDIS;
		// set serial line mode
		AT91C_BASE_US1->US_MR =
							AT91C_US_OVER |
							AT91C_US_USMODE_NORMAL |// Normal Mode
						   AT91C_US_CLKS_CLOCK |   // Clock = MCK
						   AT91C_US_CHRL_8_BITS |
						   AT91C_US_PAR_NONE |
						   AT91C_US_NBSTOP_1_BIT;

		AT91C_BASE_US1->US_IDR = (AT91C_US_RXRDY | AT91C_US_TXRDY);

		if (debug == 0)
		{
			serial_set_handler(AT91C_ID_US1, & AT91F_US1Handler);
		}

		AT91C_BASE_US1->US_CR = AT91C_US_RXEN | AT91C_US_TXEN;	// разрешаем приёмник и передатчик.
	#else	/* HARDWARE_ARM_USExxx */
		#error Wrong HARDWARE_ARM_USExxx value
	#endif /* HARDWARE_ARM_USEUSART0 */

#elif CPUSTYLE_ATMEGA_XXX4

	// USART initialization
	UCSR1B = (1U << RXEN1) | (1U << TXEN1) /* | (1U << UCSZ12) */;
	UCSR1C = (1U << UCSZ11) | (1U << UCSZ10);	// asynchronious mode, 8 bit.
	// enable pull-up registers for RXD and TXD pins: then rx or tx disabled, these pins disconnected fron UART
	PORTD |= ((1U << PD2) | (1U << PD3));
#if defined (DDRD2) && defined (DDRD3)
	DDRD &= ~ ((1U << DDRD2) | (1U << DDRD3));
#else
	DDRD &= ~ ((1U << DDD2) | (1U << DDD3));
#endif

#elif CPUSTYLE_ATMEGA128

	// USART initialization
	UCSR1B = (1U << RXEN1) | (1U << TXEN1) /* | (1U << UCSZ12) */;
	UCSR1C = (1U << UCSZ11) | (1U << UCSZ10);	// asynchronious mode, 8 bit.
	// enable pull-up registers for RXD and TXD pins: then rx or tx disabled, these pins disconnected fron UART
	//PORTE |= ((1U << PE0) | (1U << PE1));

#elif CPUSTYLE_ATXMEGAXXXA4

xxxx!;
	PORTE.DIRSET = PIN3_bm; // PE3 (TXD0) as output
	PORTE.DIRCLR = PIN2_bm; // PE2 (RXD0) as input
	PORTE_PIN2CTRL = (PORTE_PIN2CTRL & ~ PORT_OPC_gm) | PORT_OPC_PULLUP_gc;							// pin is pulled high

	USARTE1.CTRLC = USART_CMODE_ASYNCHRONOUS_gc | USART_PMODE_DISABLED_gc | USART_CHSIZE_8BIT_gc;
	USARTE1.CTRLB = USART_RXEN_bm | USART_TXEN_bm;


#elif CPUSTYLE_ATMEGA32

	#error WITHUART2HW with CPUSTYLE_ATMEGA not supported

#elif CPUSTYPE_TMS320F2833X
xxxx!;
	// Enable SCI-B clock
	PCLKCR0 |= (1U << 10);	// SCIBENCLK

	//SCIBCTL1 &= ~ (1U << 5);	// SW RESET on
	SCIBCTL1 |= (1U << 5);	// SW RESET off

	SCIBCCR =
			(7U << 0) |	// Data length = 8 bit
			(0U << 5) | // Parity enable
			(0U << 6) | // Evan/Odd parity
			(0U << 7);	// 0 - one stop bit, 1 - to stop bits


	tms320_hardware_piob_periph(
			(1UL << (35 % 32)) |	// SCITXDA
			(1UL << (36 % 32)),	// SCIRXDA
			1	// mux = 1
			);

	SCIBCTL1 |= (1U << 0) |	// RX enable
				(1U << 1);	// TX enable

#elif CPUSTYLE_R7S721

    /* ---- Supply clock to the SCIF(channel 1) ---- */
	CPG.STBCR4 &= ~ CPG_STBCR4_BIT_MSTP44;	// Module Stop 44 - SCIF3
	(void) CPG.STBCR4;			/* Dummy read */

	SCIF3.SCSCR = 0x0000;	/* SCIF transmitting and receiving operations stop, internal clock */

	SCIF3.SCSCR = (SCIF3.SCSCR & ~ 0x03) |
		0x00 |						// internal clock
		0;

	/* ---- Serial status register(SCFSR2) setting ---- */
	/* ER,BRK,DR bit clear */
	(void) SCIF3.SCFSR;						// Перед сбросом бита xxx должно произойти его чтение в ненулевом состоянии
	SCIF3.SCFSR = ~ 0x0091;	// 0xFF6E;

	/* ---- Line status register (SCLSR2) setting ---- */
	/* ORER bit clear */
	//SCIF3.SCLSR.BIT.ORER  = 0;
	SCIF3.SCLSR &= ~ 0x0001;

	SCIF3.SCSMR =
		0x00 |	/* 8-N-1 format */
		0;

	/* ---- FIFO control register (SCFCR2) setting ---- */
	/*  RTS output active trigger        :Initial value	*/
	/*  Receive FIFO data trigger        :1-data		*/
	/*  Transmit FIFO data trigger       :0-data		*/
	/*  Modem control enable             :Disabled		*/
	/*  Receive FIFO data register reset :Disabled		*/
	/*  Loop-back test                   :Disabled 		*/
	SCIF3.SCFCR = 0x0030;

	/* ---- Serial port register (SCSPTR2) setting ---- */
	/* Serial port  break output(SPB2IO)  1: Enabled */
	/* Serial port break data(SPB2DT)  1: High-level */
	//SCIF3.SCSPTR |= 0x0003;

	if (debug == 0)
	{
	   serial_set_handler(SCIFRXI3_IRQn, SCIFRXI3_IRQHandler);
	   serial_set_handler(SCIFTXI3_IRQn, SCIFTXI3_IRQHandler);
	}
	HARDWARE_UART4_INITIALIZE();	/* Присоединить периферию к выводам */

	SCIF3.SCSCR |= 0x0030;	// TE RE - SCIF3 transmitting and receiving operations are enabled */

#elif CPUSTYLE_STM32MP1

	RCC->MP_APB1ENSETR = RCC_MP_APB1ENSETR_UART4EN; // Включение тактирования UART4.
	(void) RCC->MP_APB1ENSETR;
	RCC->MP_APB1LPENSETR = RCC_MP_APB1LPENSETR_UART4LPEN; // Включение тактирования UART4.
	(void) RCC->MP_APB1LPENSETR;
	RCC->APB1RSTSETR = RCC_APB1RSTSETR_UART4RST; // Установить сброс UART4.
	(void) RCC->APB1RSTSETR;
	RCC->APB1RSTCLRR = RCC_APB1RSTCLRR_UART4RST; // Снять брос UART4.
	(void) RCC->APB1RSTCLRR;

	UART4->CR1 = 0;

#if WITHUARTFIFO
	UART4->CR1 |= USART_CR1_FIFOEN_Msk;
#else /* WITHUARTFIFO */
	UART4->CR1 &= ~ USART_CR1_FIFOEN_Msk;
#endif /* WITHUARTFIFO */

	UART4->CR1 |= (USART_CR1_RE | USART_CR1_TE); // Transmitter Enable & Receiver Enables

	HARDWARE_UART4_INITIALIZE();	/* Присоединить периферию к выводам */

	if (debug == 0)
	{
		serial_set_handler(UART4_IRQn, UART4_IRQHandler);
	}

	UART4->CR1 |= USART_CR1_UE; // Включение USART1.

#else
	#error Undefined CPUSTYLE_XXX
#endif

}

#endif /* WITHUART4HW */

#if WITHUART7HW

#if CPUSTYLE_ATSAM3S || CPUSTYLE_ATSAM4S

	void RAMFUNC_NONILINE USART7_Handler(void)
	{
		const uint_fast32_t csr = USART7->US_CSR;

		if (csr & US_CSR_RXRDY)
			HARDWARE_UART7_ONRXCHAR(USART7->US_RHR);
		if (csr & US_CSR_TXRDY)
			HARDWARE_UART7_ONTXCHAR(USART7);
	}

#elif CPUSTYLE_STM32F1XX || CPUSTYLE_STM32F4XX

	void RAMFUNC_NONILINE UART7_IRQHandler(void)
	{
		const uint_fast32_t sr = UART7->SR;

		if (sr & (USART_SR_RXNE | USART_SR_ORE | USART_SR_FE | USART_SR_NE))
			HARDWARE_UART7_ONRXCHAR(UART7->DR);
		if (sr & (USART_SR_ORE | USART_SR_FE | USART_SR_NE))
			HARDWARE_UART7_ONOVERFLOW();
		if (sr & USART_SR_TXE)
			HARDWARE_UART7_ONTXCHAR(UART7);
	}

#elif CPUSTYLE_STM32H7XX || CPUSTYLE_STM32MP1

	void RAMFUNC_NONILINE UART7_IRQHandler(void)
	{
		const uint_fast32_t isr = UART7->ISR;
		const uint_fast32_t cr1 = UART7->CR1;

		if (cr1 & USART_CR1_RXNEIE)
		{
			if (isr & USART_ISR_RXNE_RXFNE)
				HARDWARE_UART7_ONRXCHAR(UART7->RDR);
			if (isr & USART_ISR_ORE)
			{
				UART7->ICR = USART_ICR_ORECF;
				HARDWARE_UART7_ONOVERFLOW();
			}
			if (isr & USART_ISR_FE)
				UART7->ICR = USART_ICR_FECF;
		}
		if (cr1 & USART_CR1_TXEIE)
		{
			if (isr & USART_ISR_TXE_TXFNF)
				HARDWARE_UART7_ONTXCHAR(UART7);
		}
	}

#elif CPUSTYLE_STM32F30X || CPUSTYLE_STM32F0XX || CPUSTYLE_STM32L0XX || CPUSTYLE_STM32F7XX

	void RAMFUNC_NONILINE UART7_IRQHandler(void)
	{
		const uint_fast32_t isr = UART7->ISR;

		if (isr & USART_ISR_RXNE)
			HARDWARE_UART7_ONRXCHAR(UART7->RDR);
		if (isr & USART_ISR_ORE)
		{
			UART7->ICR = USART_ICR_ORECF;
			HARDWARE_UART7_ONOVERFLOW();
		}
		if (isr & USART_ISR_FE)
			UART7->ICR = USART_ICR_FECF;
		if (isr & USART_ISR_TXE)
			HARDWARE_UART7_ONTXCHAR(UART7);
	}

#elif CPUSTYLE_R7S721

	// Приём символа он последовательного порта
	static void SCIFRXI7_IRQHandler(void)
	{
		(void) SCIF7.SCFSR;						// Перед сбросом бита RDF должно произойти его чтение в ненулевом состоянии
		SCIF7.SCFSR = (uint16_t) ~ SCIF7_SCFSR_RDF;	// RDF=0 читать незачем (в примерах странное - сбрасывабтся и другие биты)
		uint_fast8_t n = (SCIF7.SCFDR & SCIF7_SCFDR_R) >> SCIF7_SCFDR_R_SHIFT;
		while (n --)
			HARDWARE_UART7_ONRXCHAR(SCIF7.SCFRDR & SCIF7_SCFRDR_D);
	}

	// Передача символа в последовательный порт
	static void SCIFTXI7_IRQHandler(void)
	{
		HARDWARE_UART7_ONTXCHAR(& SCIF7);
	}

#else

	#error Undefined CPUSTYLE_XXX

#endif	/* CPUSTYLE_ATMEGA_XXX4 */


/* вызывается из обработчика прерываний или при запрещённых прерываниях. */
/*
		system_disableIRQ();
		system_enableIRQ();
*/
/* Разрешение/запрещение прерывания по передаче символа */
void hardware_uart7_enabletx(uint_fast8_t state)
{
#if CPUSTYLE_ATSAM3S || CPUSTYLE_ATSAM4S

	#if HARDWARE_ARM_USEUSART0

		if (state)
			USART0->US_IER = US_IER_TXRDY;
		else
			USART0->US_IDR = US_IDR_TXRDY;

	#elif HARDWARE_ARM_USEUSART1

		if (state)
			USART1->US_IER = US_IER_TXRDY;
		else
			USART1->US_IDR = US_IDR_TXRDY;

	#elif HARDWARE_ARM_USEUART0

		if (state)
			UART0->UART_IER = UART_IER_TXRDY;
		else
			UART0->UART_IDR = UART_IDR_TXRDY;

	#elif HARDWARE_ARM_USEUART1

		if (state)
			UART1->UART_IER = UART_IER_TXRDY;
		else
			UART1->UART_IDR = UART_IDR_TXRDY;

	#else	/* HARDWARE_ARM_USExxx */
		#error Wrong HARDWARE_ARM_USExxx value
	#endif /* HARDWARE_ARM_USEUSART0 */

#elif CPUSTYLE_AT91SAM7S

	#if HARDWARE_ARM_USEUSART0

	if (state)
		AT91C_BASE_US0->US_IER = AT91C_US_TXRDY;
	else
		AT91C_BASE_US0->US_IDR = AT91C_US_TXRDY;

	#elif HARDWARE_ARM_USEUSART1

	if (state)
		AT91C_BASE_US1->US_IER = AT91C_US_TXRDY;
	else
		AT91C_BASE_US1->US_IDR = AT91C_US_TXRDY;

	#else	/* HARDWARE_ARM_USExxx */
		#error Wrong HARDWARE_ARM_USExxx value
	#endif /* HARDWARE_ARM_USEUSART0 */

#elif CPUSTYLE_STM32F || CPUSTYLE_STM32MP1

	if (state)
		UART7->CR1 |= USART_CR1_TXEIE;
	else
		UART7->CR1 &= ~ USART_CR1_TXEIE;

#elif CPUSTYLE_ATMEGA_XXX4

	/* Used USART 1 */
	if (state)
	{
		UCSR1B |= (1U << TXCIE1);
		HARDWARE_UART4_ONTXCHAR(NULL);	// initiate 1-st character sending
	}
	else
	{
		UCSR1B &= ~ (1U << TXCIE1);
	}

#elif CPUSTYLE_ATMEGA32

	#error WITHUART2HW with CPUSTYLE_ATMEGA32 not supported
	if (state)
	{
		UCSRB |= (1U << TXCIE);
		HARDWARE_UART4_ONTXCHAR(NULL);	// initiate 1-st character sending
	}
	else
	{
		UCSRB &= ~ (1U << TXCIE);
	}

#elif CPUSTYLE_ATMEGA128

	/* Used USART 1 */
	if (state)
	{
		UCSR1B |= (1U << TXCIE1);
		HARDWARE_UART4_ONTXCHAR(NULL);	// initiate 1-st character sending
	}
	else
	{
		UCSR1B &= ~ (1U << TXCIE1);
	}

#elif CPUSTYLE_ATXMEGAXXXA4

	if (state)
		USARTE1.CTRLA = (USARTE1.CTRLA & ~ USART_DREINTLVL_gm) | USART_DREINTLVL_LO_gc;
	else
		USARTE1.CTRLA = (USARTE1.CTRLA & ~ USART_DREINTLVL_gm) | USART_DREINTLVL_OFF_gc;

#elif CPUSTYPE_TMS320F2833X

	if (state)
		SCIBCTL2 |= (1U << 0);	// TX INT ENA
	else
		SCIBCTL2 &= ~ (1U << 0); // TX INT ENA

#elif CPUSTYLE_R7S721

	if (state)
		SCIF3.SCSCR |= (1U << 7);	// TIE Transmit Interrupt Enable
	else
		SCIF3.SCSCR &= ~ (1U << 7);	// TIE Transmit Interrupt Enable

#else
	#error Undefined CPUSTYLE_XXX
#endif
}

/* вызывается из обработчика прерываний или при запрещённых прерываниях. */
/* Разрешение/запрещение прерываний про приёму символа */
void hardware_uart7_enablerx(uint_fast8_t state)
{
#if CPUSTYLE_ATSAM3S || CPUSTYLE_ATSAM4S

	#if HARDWARE_ARM_USEUSART0

		if (state)
			USART0->US_IER = US_IER_RXRDY;
		else
			USART0->US_IDR = US_IDR_RXRDY;

	#elif HARDWARE_ARM_USEUSART1

		if (state)
			USART1->US_IER = US_IER_RXRDY;
		else
			USART1->US_IDR = US_IDR_RXRDY;

	#elif HARDWARE_ARM_USEUART0

		if (state)
			UART0->UART_IER = UART_IER_RXRDY;
		else
			UART0->UART_IDR = UART_IDR_RXRDY;

	#elif HARDWARE_ARM_USEUART1

		if (state)
			UART1->UART_IER = UART_IER_RXRDY;
		else
			UART1->UART_IDR = UART_IDR_RXRDY;

	#else	/* HARDWARE_ARM_USExxx */
		#error Wrong HARDWARE_ARM_USExxx value
	#endif /* HARDWARE_ARM_USEUSART0 */

#elif CPUSTYLE_AT91SAM7S

	#if HARDWARE_ARM_USEUSART1

	if (state)
		AT91C_BASE_US1->US_IER = AT91C_US_RXRDY;
	else
		AT91C_BASE_US1->US_IDR = AT91C_US_RXRDY;

	#else	/* HARDWARE_ARM_USExxx */
		#error Wrong HARDWARE_ARM_USExxx value
	#endif /* HARDWARE_ARM_USEUSART0 */


#elif CPUSTYLE_ATMEGA_XXX4
	/* Used USART 1 */
	if (state)
		UCSR1B |= (1U << RXCIE1);
	else
		UCSR1B &= ~ (1U << RXCIE1);

#elif CPUSTYLE_ATMEGA32

	#error WITHUART2HW with CPUSTYLE_ATMEGA32 not supported
	if (state)
		UCSRB |= (1U << RXCIE);
	else
		UCSRB &= ~ (1U << RXCIE);

#elif CPUSTYLE_ATMEGA128

	/* Used USART 0 */
	if (state)
		UCSR1B |= (1U << RXCIE1);
	else
		UCSR1B &= ~ (1U << RXCIE1);

#elif CPUSTYLE_ATXMEGAXXXA4

	if (state)
		USARTE1.CTRLA = (USARTE1.CTRLA & ~ USART_RXCINTLVL_gm) | USART_RXCINTLVL_LO_gc;
	else
		USARTE1.CTRLA = (USARTE1.CTRLA & ~ USART_RXCINTLVL_gm) | USART_RXCINTLVL_OFF_gc;

#elif CPUSTYLE_STM32F || CPUSTYLE_STM32MP1

	if (state)
		UART7->CR1 |= USART_CR1_RXNEIE;
	else
		UART7->CR1 &= ~ USART_CR1_RXNEIE;

#elif CPUSTYPE_TMS320F2833X

	if (state)
		SCIBCTL2 |= (1U << 1);	// RX/BK INT ENA
	else
		SCIBCTL2 &= ~ (1U << 1); // RX/BK INT ENA

#elif CPUSTYLE_R7S721

	if (state)
		SCIF3.SCSCR |= (1U << 6);	// RIE Receive Interrupt Enable
	else
		SCIF3.SCSCR &= ~ (1U << 6);	// RIE Receive Interrupt Enable

#else
	#error Undefined CPUSTYLE_XXX

#endif
}


/* передача символа из обработчика прерывания готовности передатчика */
void hardware_uart7_tx(void * ctx, uint_fast8_t c)
{
#if CPUSTYLE_ATSAM3S || CPUSTYLE_ATSAM4S

	#if HARDWARE_ARM_USEUSART1
		USART1->US_THR = c;
	#elif HARDWARE_ARM_USEUART1
		UART1->UART_THR = c;
	#else	/* HARDWARE_ARM_USExxx */
		#error Wrong HARDWARE_ARM_USExxx value
	#endif

#elif CPUSTYLE_AT91SAM7S

	#if HARDWARE_ARM_USEUSART1
		AT91C_BASE_US1->US_THR = c;
	#else	/* HARDWARE_ARM_USExxx */
		#error Wrong HARDWARE_ARM_USExxx value
	#endif

#elif CPUSTYLE_ATMEGA128
	UDR1 = c;

#elif CPUSTYLE_ATMEGA_XXX4

	UDR1 = c;

#elif CPUSTYLE_ATMEGA32

	#error WITHUART2HW with CPUSTYLE_ATMEGA32 not supported
	UDR = c;

#elif CPUSTYLE_ATXMEGAXXXA4

	USARTE1.DATA = c;

#elif CPUSTYLE_STM32F1XX || CPUSTYLE_STM32F4XX

	UART4->DR = c;

#elif CPUSTYLE_STM32F30X || CPUSTYLE_STM32F0XX || CPUSTYLE_STM32L0XX || CPUSTYLE_STM32H7XX || CPUSTYLE_STM32F7XX || CPUSTYLE_STM32MP1

	UART7->TDR = c;

#elif CPUSTYPE_TMS320F2833X

	SCIBTXBUF = c;

#elif CPUSTYLE_R7S721

	(void) SCIF3.SCFSR;			// Перед сбросом бита TDFE должно произойти его чтение в ненулевом состоянии
	SCIF3.SCFTDR = c;
	SCIF3.SCFSR = (uint16_t) ~ (1U << SCIF3_SCFSR_TDFE_SHIFT);	// TDFE=0 читать незачем (в примерах странное)

#else
	#error Undefined CPUSTYLE_XXX
#endif
}



/* приём символа, если готов порт */
uint_fast8_t
hardware_uart7_getchar(char * cp)
{
#if CPUSTYLE_ATSAM3S || CPUSTYLE_ATSAM4S

	#if HARDWARE_ARM_USEUSART0
		if ((USART0->US_CSR & US_CSR_RXRDY) == 0)
			return 0;
		* cp = USART0->US_RHR;
	#elif HARDWARE_ARM_USEUSART1
		if ((USART1->US_CSR & US_CSR_RXRDY) == 0)
			return 0;
		* cp = USART1->US_RHR;
	#elif HARDWARE_ARM_USEUART0
		if ((UART0->UART_SR & UART_SR_RXRDY) == 0)
			return 0;
		* cp = UART0->UART_RHR;
	#elif HARDWARE_ARM_USEUART1
		if ((UART1->UART_SR & UART_SR_RXRDY) == 0)
			return 0;
		* cp = UART1->UART_RHR;
	#else	/* HARDWARE_ARM_USExxx */
		#error Wrong HARDWARE_ARM_USExxx value
	#endif

#elif CPUSTYLE_AT91SAM7S

	#if HARDWARE_ARM_USEUSART1
		if ((AT91C_BASE_US1->US_CSR & AT91C_US_RXRDY) == 0)
			return 0;
		* cp = AT91C_BASE_US1->US_RHR;
	#else	/* HARDWARE_ARM_USExxx */
		#error Wrong HARDWARE_ARM_USExxx value
	#endif

#elif CPUSTYLE_ATXMEGAXXXA4

	if ((USARTE1.STATUS & (1 << USART_RXCIF_bp)) == 0)
			return 0;
	* cp = USARTE1.DATA;

#elif CPUSTYLE_ATMEGA128

	if ((UCSR1A & (1 << RXC1)) == 0)
			return 0;
	* cp = UDR1;

#elif CPUSTYLE_ATMEGA_XXX4

	if ((UCSR1A & (1 << RXC1)) == 0)
			return 0;
	* cp = UDR1;

#elif CPUSTYLE_ATMEGA32

	#error WITHUART2HW with CPUSTYLE_ATMEGA32 not supported

	if ((UCSRA & (1 << RXC)) == 0)
			return 0;
	* cp = UDR;

#elif CPUSTYLE_STM32F1XX || CPUSTYLE_STM32F4XX

	if ((UART7->SR & (USART_SR_RXNE | USART_SR_ORE | USART_SR_FE | USART_SR_NE)) == 0)
		return 0;
	* cp = UART7->DR;

#elif CPUSTYLE_STM32H7XX || CPUSTYLE_STM32MP1

	const uint_fast32_t isr = UART7->ISR;
	if (isr & USART_ISR_ORE)
		UART7->ICR = USART_ICR_ORECF;
	if (isr & USART_ISR_FE)
		UART7->ICR = USART_ICR_FECF;
	if ((isr & USART_ISR_RXNE_RXFNE) == 0)
		return 0;
	* cp = UART7->RDR;

#elif CPUSTYLE_STM32F30X || CPUSTYLE_STM32F0XX || CPUSTYLE_STM32L0XX || CPUSTYLE_STM32F7XX

	const uint_fast32_t isr = UART7->ISR;
	if (isr & USART_ISR_ORE)
		UART7->ICR = USART_ICR_ORECF;
	if (isr & USART_ISR_FE)
		UART7->ICR = USART_ICR_FECF;
	if ((isr & USART_ISR_RXNE) == 0)
		return 0;
	* cp = UART7->RDR;

#elif CPUSTYPE_TMS320F2833X

	if ((SCIBRXST & (1U << 6)) == 0)	// Wait for RXRDY bit
		return 0;
	* cp = SCIBRXBUF;

#elif CPUSTYLE_R7S721

	if ((SCIF3.SCFSR & (1U << 1)) == 0)	// RDF
		return 0;
	* cp = SCIF3.SCFRDR;
	SCIF3.SCFSR = (uint16_t) ~ (1U << 1);	// RDF=0 читать незачем (в примерах странное)

#else
	#error Undefined CPUSTYLE_XXX
#endif
	return 1;
}

/* передача символа если готов порт */
uint_fast8_t
hardware_uart7_putchar(uint_fast8_t c)
{
#if CPUSTYLE_ATSAM3S || CPUSTYLE_ATSAM4S

	#if HARDWARE_ARM_USEUSART0
		if ((USART0->US_CSR & US_CSR_TXRDY) == 0)
			return 0;
		USART0->US_THR = c;
	#elif HARDWARE_ARM_USEUSART1
		if ((USART1->US_CSR & US_CSR_TXRDY) == 0)
			return 0;
		USART1->US_THR = c;
	#elif HARDWARE_ARM_USEUART0
		if ((UART0->UART_SR & UART_SR_TXRDY) == 0)
			return 0;
		UART0->UART_THR = c;
	#elif HARDWARE_ARM_USEUART1
		if ((UART1->UART_SR & UART_SR_TXRDY) == 0)
			return 0;
		UART1->UART_THR = c;
	#else	/* HARDWARE_ARM_USExxx */
		#error Wrong HARDWARE_ARM_USExxx value
	#endif

#elif CPUSTYLE_AT91SAM7S

	#if HARDWARE_ARM_USEUSART0
		if ((AT91C_BASE_US0->US_CSR & AT91C_US_TXRDY) == 0)
			return 0;
		AT91C_BASE_US0->US_THR = c;
	#elif HARDWARE_ARM_USEUSART1
		if ((AT91C_BASE_US1->US_CSR & AT91C_US_TXRDY) == 0)
			return 0;
		AT91C_BASE_US1->US_THR = c;
	#else	/* HARDWARE_ARM_USExxx */
		#error Wrong HARDWARE_ARM_USExxx value
	#endif

#elif CPUSTYLE_ATXMEGAXXXA4

	if ((USARTE0.STATUS & USART_DREIF_bm) == 0)
		return 0;
	USARTE0.DATA = c;

#elif CPUSTYLE_ATMEGA_XXX4

	if ((UCSR0A & (1 << UDRE0)) == 0)
		return 0;
	UDR0 = c;

#elif CPUSTYLE_ATMEGA128

	if ((UCSR0A & (1 << UDRE0)) == 0)
		return 0;
	UDR0 = c;

#elif CPUSTYLE_ATMEGA32

	#error WITHUART2HW with CPUSTYLE_ATMEGA32 not supported

	if ((UCSRA & (1 << UDRE)) == 0)
		return 0;
	UDR = c;

#elif CPUSTYLE_STM32F1XX || CPUSTYLE_STM32F4XX

	if ((UART4->SR & USART_SR_TXE) == 0)
		return 0;
	UART4->DR = c;

#elif CPUSTYLE_STM32H7XX || CPUSTYLE_STM32MP1

	if ((UART7->ISR & USART_ISR_TXE_TXFNF) == 0)
		return 0;
	UART7->TDR = c;

#elif CPUSTYLE_STM32F30X || CPUSTYLE_STM32F0XX || CPUSTYLE_STM32L0XX || CPUSTYLE_STM32F7XX

	if ((UART4->ISR & USART_ISR_TXE) == 0)
		return 0;
	UART4->TDR = c;

#elif CPUSTYPE_TMS320F2833X

	if ((SCIBCTL2 & (1U << 7)) == 0)	// wait for TXRDY bit
		return 0;
	SCIBTXBUF = c;

#elif CPUSTYLE_R7S721

	if ((SCIF3.SCFSR & (1U << SCIF3_SCFSR_TDFE_SHIFT)) == 0)	// Перед сбросом бита TDFE должно произойти его чтение в ненулевом состоянии
		return 0;
	SCIF3.SCFTDR = c;
	SCIF3.SCFSR = (uint16_t) ~ (1U << SCIF3_SCFSR_TDFE_SHIFT);	// TDFE=0 читать незачем (в примерах странное)

#else
	#error Undefined CPUSTYLE_XXX
#endif
	return 1;
}

void hardware_uart7_initialize(uint_fast8_t debug)
{
#if CPUSTYLE_ATSAM3S || CPUSTYLE_ATSAM4S

	#if HARDWARE_ARM_USEUSART0
		// enable the clock of USART0
		PMC->PMC_PCER0 = 1UL << ID_USART0;

		HARDWARE_UART4_INITIALIZE();	/* Присоединить периферию к выводам */

		// reset the UART
		USART0->US_CR = US_CR_RSTRX | US_CR_RSTTX | US_CR_RXDIS | US_CR_TXDIS;
		// set serial line mode
		USART0->US_MR =
			US_MR_CHMODE_NORMAL |// Normal Mode
			US_MR_USCLKS_MCK |   // Clock = MCK
			US_MR_CHRL_8_BIT |
			US_MR_PAR_NO |
			US_MR_NBSTOP_1_BIT;

		USART0->US_IDR = (US_IDR_RXRDY | US_IDR_TXRDY);

		if (debug == 0)
		{
			serial_set_handler(USART0_IRQn, & USART0_IRQHandler);
		}

		USART0->US_CR = US_CR_RXEN | US_CR_TXEN;	// разрешаем приёмник и передатчик.

	#elif HARDWARE_ARM_USEUSART1
		// enable the clock of USART1
		PMC->PMC_PCER0 = 1UL << ID_USART1;

		HARDWARE_UART4_INITIALIZE();	/* Присоединить периферию к выводам */

		// reset the UART
		USART1->US_CR = US_CR_RSTRX | US_CR_RSTTX | US_CR_RXDIS | US_CR_TXDIS;
		// set serial line mode
		USART1->US_MR =
			US_MR_CHMODE_NORMAL |// Normal Mode
			US_MR_USCLKS_MCK |   // Clock = MCK
			US_MR_CHRL_8_BIT |
			US_MR_PAR_NO |
			US_MR_NBSTOP_1_BIT;

		USART1->US_IDR = (US_IDR_RXRDY | US_IDR_TXRDY);

		if (debug == 0)
		{
			serial_set_handler(USART1_IRQn, & USART1_IRQHandler);
		}

		USART1->US_CR = US_CR_RXEN | US_CR_TXEN;	// разрешаем приёмник и передатчик.

	#elif HARDWARE_ARM_USEUART0

		// enable the clock of UART0
		PMC->PMC_PCER0 = 1UL << ID_UART0;

		HARDWARE_UART4_INITIALIZE();	/* Присоединить периферию к выводам */

		// reset the UART
		UART0->UART_CR = UART_CR_RSTRX | UART_CR_RSTTX | UART_CR_RXDIS | UART_CR_TXDIS;
		// set serial line mode
		UART0->UART_MR =
			UART_MR_CHMODE_NORMAL |// Normal Mode
			//UART_MR_USCLKS_MCK |   // Clock = MCK
			//UART_MR_CHRL_8_BIT |
			UART_MR_PAR_NO |
			//UART_MR_NBSTOP_1_BIT |
			0;

		UART0->UART_IDR = (UART_IDR_RXRDY | UART_IDR_TXRDY);

		if (debug == 0)
		{
			serial_set_handler(UART0_IRQn, & UART0_IRQHandler);
		}

		UART0->UART_CR = UART_CR_RXEN | UART_CR_TXEN;	// разрешаем приёмник и передатчик.

	#elif HARDWARE_ARM_USEUART1

		// enable the clock of UART1
		PMC->PMC_PCER0 = 1UL << ID_UART7;

		HARDWARE_UART7_INITIALIZE();	/* Присоединить периферию к выводам */

		// reset the UART
		UART7->UART_CR = UART_CR_RSTRX | UART_CR_RSTTX | UART_CR_RXDIS | UART_CR_TXDIS;
		// set serial line mode
		UART7->UART_MR =
			UART_MR_CHMODE_NORMAL |// Normal Mode
			//UART_MR_USCLKS_MCK |   // Clock = MCK
			//UART_MR_CHRL_8_BIT |
			UART_MR_PAR_NO |
			//UART_MR_NBSTOP_1_BIT |
			0;

		UART7->UART_IDR = (UART_IDR_RXRDY | UART_IDR_TXRDY);

		if (debug == 0)
		{
			serial_set_handler(UART7_IRQn, & UART7_IRQHandler);
		}

		UART7->UART_CR = UART_CR_RXEN | UART_CR_TXEN;	// разрешаем приёмник и передатчик.

	#else	/* HARDWARE_ARM_USExxx */
		#error Wrong HARDWARE_ARM_USExxx value
	#endif /* HARDWARE_ARM_USEUSART0 */


#elif CPUSTYLE_STM32F1XX

	RCC->APB1ENR |= RCC_APB1ENR_UART4EN; // Включение тактирования UART4.
	(void) RCC->APB1ENR;

	UART7->CR1 |= (USART_CR1_RE | USART_CR1_TE); // Transmitter Enable & Receiver Enables


	RCC->APB2ENR |= RCC_APB2ENR_AFIOEN;     //включить тактирование альтернативных функций
	(void) RCC->APB2ENR;

	HARDWARE_UART7_INITIALIZE();	/* Присоединить периферию к выводам */

	if (debug == 0)
	{
		serial_set_handler(UART7_IRQn, & UART7_IRQHandler);
	}

	UART7->CR1 |= USART_CR1_UE; // Включение UART4.

#elif CPUSTYLE_STM32H7XX

	RCC->APB1LENR |= RCC_APB1LENR_UART7EN; // Включение тактирования UART4.
	(void) RCC->APB1LENR;

	UART7->CR1 = 0;

#if WITHUARTFIFO
	UART7->CR1 |= USART_CR1_FIFOEN_Msk;
#else /* WITHUARTFIFO */
	UART7->CR1 &= ~ USART_CR1_FIFOEN_Msk;
#endif /* WITHUARTFIFO */

	UART7->CR1 |= (USART_CR1_RE | USART_CR1_TE); // Transmitter Enable & Receiver Enables

	HARDWARE_UART7_INITIALIZE();	/* Присоединить периферию к выводам */

	if (debug == 0)
	{
		serial_set_handler(UART7_IRQn, & UART7_IRQHandler);
	}

	UART7->CR1 |= USART_CR1_UE; // Включение UART4.

#elif CPUSTYLE_STM32F30X || CPUSTYLE_STM32F4XX || CPUSTYLE_STM32F0XX || CPUSTYLE_STM32L0XX || CPUSTYLE_STM32F7XX

	RCC->APB1ENR |= RCC_APB1ENR_UART7EN; // Включение тактирования UART7.
	(void) RCC->APB1ENR;

	UART7->CR1 |= (USART_CR1_RE | USART_CR1_TE); // Transmitter Enable & Receiver Enables

	HARDWARE_UART7_INITIALIZE();	/* Присоединить периферию к выводам */

	if (debug == 0)
	{
		serial_set_handler(UART7_IRQn, & UART7_IRQHandler);
	}

	UART7->CR1 |= USART_CR1_UE; // Включение UART4.

#elif CPUSTYLE_AT91SAM7S

	#if HARDWARE_ARM_USEUSART0
		// enable the clock of USART0
		AT91C_BASE_PMC->PMC_PCER = 1UL << AT91C_ID_US0;

		HARDWARE_UART7_INITIALIZE();	/* Присоединить периферию к выводам */

		// disable I/O pullup
		// reset the UART
		AT91C_BASE_US0->US_CR = AT91C_US_RSTRX | AT91C_US_RSTTX | AT91C_US_RXDIS | AT91C_US_TXDIS;
		// set serial line mode
		AT91C_BASE_US0->US_MR =
							AT91C_US_OVER |
							AT91C_US_USMODE_NORMAL |// Normal Mode
						   AT91C_US_CLKS_CLOCK |   // Clock = MCK
						   AT91C_US_CHRL_8_BITS |
						   AT91C_US_PAR_NONE |
						   AT91C_US_NBSTOP_1_BIT;
		AT91C_BASE_US0->US_IDR = (AT91C_US_RXRDY | AT91C_US_TXRDY);

		{
			enum { irqID = AT91C_ID_US0 };
			// programming interrupts from ADC
			AT91C_BASE_AIC->AIC_IDCR = (1UL << irqID);
			AT91C_BASE_AIC->AIC_SVR [irqID] = (AT91_REG) AT91F_US0Handler;
			AT91C_BASE_AIC->AIC_SMR [irqID] =
				(AT91C_AIC_SRCTYPE & AT91C_AIC_SRCTYPE_INT_HIGH_LEVEL) |
				(AT91C_AIC_PRIOR & AT91C_AIC_PRIOR_LOWEST);
			AT91C_BASE_AIC->AIC_ICCR = (1UL << irqID);		// clear pending interrupt
			AT91C_BASE_AIC->AIC_IECR = (1UL << irqID);	// enable inerrupt

		}

		AT91C_BASE_US0->US_CR = AT91C_US_RXEN  | AT91C_US_TXEN;	// разрешаем приёмник и передатчик.

	#elif HARDWARE_ARM_USEUSART1

		// enable the clock of UART4
		AT91C_BASE_PMC->PMC_PCER = 1UL << AT91C_ID_US1;

		HARDWARE_UART7_INITIALIZE();	/* Присоединить периферию к выводам */

		// enable uart pins on PIO
		AT91C_BASE_PIOA->PIO_ASR = AT91C_PA21_RXD1 | AT91C_PA22_TXD1; // assigns the 2 I/O lines to peripheral A function
		AT91C_BASE_PIOA->PIO_PDR = AT91C_PA21_RXD1 | AT91C_PA22_TXD1;	// enable peripheral control of PA21,PA22 (RXD1 and TXD1)
		// reset the UART
		AT91C_BASE_US1->US_CR = AT91C_US_RSTRX | AT91C_US_RSTTX | AT91C_US_RXDIS | AT91C_US_TXDIS;
		// set serial line mode
		AT91C_BASE_US1->US_MR =
							AT91C_US_OVER |
							AT91C_US_USMODE_NORMAL |// Normal Mode
						   AT91C_US_CLKS_CLOCK |   // Clock = MCK
						   AT91C_US_CHRL_8_BITS |
						   AT91C_US_PAR_NONE |
						   AT91C_US_NBSTOP_1_BIT;

		AT91C_BASE_US1->US_IDR = (AT91C_US_RXRDY | AT91C_US_TXRDY);

		if (debug == 0)
		{
			serial_set_handler(AT91C_ID_US1, & AT91F_US1Handler);
		}

		AT91C_BASE_US1->US_CR = AT91C_US_RXEN | AT91C_US_TXEN;	// разрешаем приёмник и передатчик.
	#else	/* HARDWARE_ARM_USExxx */
		#error Wrong HARDWARE_ARM_USExxx value
	#endif /* HARDWARE_ARM_USEUSART0 */

#elif CPUSTYLE_ATMEGA_XXX4

	// USART initialization
	UCSR1B = (1U << RXEN1) | (1U << TXEN1) /* | (1U << UCSZ12) */;
	UCSR1C = (1U << UCSZ11) | (1U << UCSZ10);	// asynchronious mode, 8 bit.
	// enable pull-up registers for RXD and TXD pins: then rx or tx disabled, these pins disconnected fron UART
	PORTD |= ((1U << PD2) | (1U << PD3));
#if defined (DDRD2) && defined (DDRD3)
	DDRD &= ~ ((1U << DDRD2) | (1U << DDRD3));
#else
	DDRD &= ~ ((1U << DDD2) | (1U << DDD3));
#endif

#elif CPUSTYLE_ATMEGA128

	// USART initialization
	UCSR1B = (1U << RXEN1) | (1U << TXEN1) /* | (1U << UCSZ12) */;
	UCSR1C = (1U << UCSZ11) | (1U << UCSZ10);	// asynchronious mode, 8 bit.
	// enable pull-up registers for RXD and TXD pins: then rx or tx disabled, these pins disconnected fron UART
	//PORTE |= ((1U << PE0) | (1U << PE1));

#elif CPUSTYLE_ATXMEGAXXXA4

xxxx!;
	PORTE.DIRSET = PIN3_bm; // PE3 (TXD0) as output
	PORTE.DIRCLR = PIN2_bm; // PE2 (RXD0) as input
	PORTE_PIN2CTRL = (PORTE_PIN2CTRL & ~ PORT_OPC_gm) | PORT_OPC_PULLUP_gc;							// pin is pulled high

	USARTE1.CTRLC = USART_CMODE_ASYNCHRONOUS_gc | USART_PMODE_DISABLED_gc | USART_CHSIZE_8BIT_gc;
	USARTE1.CTRLB = USART_RXEN_bm | USART_TXEN_bm;


#elif CPUSTYLE_ATMEGA32

	#error WITHUART7HW with CPUSTYLE_ATMEGA not supported

#elif CPUSTYPE_TMS320F2833X
xxxx!;
	// Enable SCI-B clock
	PCLKCR0 |= (1U << 10);	// SCIBENCLK

	//SCIBCTL1 &= ~ (1U << 5);	// SW RESET on
	SCIBCTL1 |= (1U << 5);	// SW RESET off

	SCIBCCR =
			(7U << 0) |	// Data length = 8 bit
			(0U << 5) | // Parity enable
			(0U << 6) | // Evan/Odd parity
			(0U << 7);	// 0 - one stop bit, 1 - to stop bits


	tms320_hardware_piob_periph(
			(1UL << (35 % 32)) |	// SCITXDA
			(1UL << (36 % 32)),	// SCIRXDA
			1	// mux = 1
			);

	SCIBCTL1 |= (1U << 0) |	// RX enable
				(1U << 1);	// TX enable

#elif CPUSTYLE_R7S721

    /* ---- Supply clock to the SCIF(channel 1) ---- */
	CPG.STBCR4 &= ~ CPG_STBCR4_BIT_MSTP44;	// Module Stop 44 - SCIF3
	(void) CPG.STBCR4;			/* Dummy read */

	SCIF3.SCSCR = 0x0000;	/* SCIF transmitting and receiving operations stop, internal clock */

	SCIF3.SCSCR = (SCIF3.SCSCR & ~ 0x03) |
		0x00 |						// internal clock
		0;

	/* ---- Serial status register(SCFSR2) setting ---- */
	/* ER,BRK,DR bit clear */
	(void) SCIF3.SCFSR;						// Перед сбросом бита xxx должно произойти его чтение в ненулевом состоянии
	SCIF3.SCFSR = ~ 0x0091;	// 0xFF6E;

	/* ---- Line status register (SCLSR2) setting ---- */
	/* ORER bit clear */
	//SCIF3.SCLSR.BIT.ORER  = 0;
	SCIF3.SCLSR &= ~ 0x0001;

	SCIF3.SCSMR =
		0x00 |	/* 8-N-1 format */
		0;

	/* ---- FIFO control register (SCFCR2) setting ---- */
	/*  RTS output active trigger        :Initial value	*/
	/*  Receive FIFO data trigger        :1-data		*/
	/*  Transmit FIFO data trigger       :0-data		*/
	/*  Modem control enable             :Disabled		*/
	/*  Receive FIFO data register reset :Disabled		*/
	/*  Loop-back test                   :Disabled 		*/
	SCIF3.SCFCR = 0x0030;

	/* ---- Serial port register (SCSPTR2) setting ---- */
	/* Serial port  break output(SPB2IO)  1: Enabled */
	/* Serial port break data(SPB2DT)  1: High-level */
	//SCIF3.SCSPTR |= 0x0003;

	if (debug == 0)
	{
	   serial_set_handler(SCIFRXI3_IRQn, SCIFRXI3_IRQHandler);
	   serial_set_handler(SCIFTXI3_IRQn, SCIFTXI3_IRQHandler);
	}
	HARDWARE_UART7_INITIALIZE();	/* Присоединить периферию к выводам */

	SCIF3.SCSCR |= 0x0030;	// TE RE - SCIF3 transmitting and receiving operations are enabled */

#elif CPUSTYLE_STM32MP1

	RCC->MP_APB1ENSETR = RCC_MP_APB1ENSETR_UART7EN; // Включение тактирования UART7.
	(void) RCC->MP_APB1ENSETR;
	RCC->MP_APB1LPENSETR = RCC_MP_APB1LPENSETR_UART7LPEN; // Включение тактирования UART7.
	(void) RCC->MP_APB1LPENSETR;
	RCC->APB1RSTSETR = RCC_APB1RSTSETR_UART7RST; // Установить сброс UART7.
	(void) RCC->APB1RSTSETR;
	RCC->APB1RSTCLRR = RCC_APB1RSTCLRR_UART7RST; // Снять брос UART7.
	(void) RCC->APB1RSTCLRR;

	UART7->CR1 = 0;

#if WITHUARTFIFO
	UART7->CR1 |= USART_CR1_FIFOEN_Msk;
#else /* WITHUARTFIFO */
	UART7->CR1 &= ~ USART_CR1_FIFOEN_Msk;
#endif /* WITHUARTFIFO */

	UART7->CR1 |= (USART_CR1_RE | USART_CR1_TE); // Transmitter Enable & Receiver Enables

	HARDWARE_UART7_INITIALIZE();	/* Присоединить периферию к выводам */

	if (debug == 0)
	{
		serial_set_handler(UART7_IRQn, UART7_IRQHandler);
	}

	UART7->CR1 |= USART_CR1_UE; // Включение USART1.

#else
	#error Undefined CPUSTYLE_XXX
#endif

}

#endif /* WITHUART7HW */

#if WITHUART5HW

#if CPUSTYLE_ATSAM3S || CPUSTYLE_ATSAM4S

	void RAMFUNC_NONILINE USART5_Handler(void)
	{
		const uint_fast32_t csr = USART5->US_CSR;

		if (csr & US_CSR_RXRDY)
			HARDWARE_UART5_ONRXCHAR(USART5->US_RHR);
		if (csr & US_CSR_TXRDY)
			HARDWARE_UART5_ONTXCHAR(USART5);
	}

#elif CPUSTYLE_STM32F1XX || CPUSTYLE_STM32F4XX

	void RAMFUNC_NONILINE UART5_IRQHandler(void)
	{
		const uint_fast32_t sr = UART5->SR;

		if (sr & (USART_SR_RXNE | USART_SR_ORE | USART_SR_FE | USART_SR_NE))
			HARDWARE_UART5_ONRXCHAR(UART5->DR);
		if (sr & (USART_SR_ORE | USART_SR_FE | USART_SR_NE))
			HARDWARE_UART5_ONOVERFLOW();
		if (sr & USART_SR_TXE)
			HARDWARE_UART5_ONTXCHAR(UART5);
	}

#elif CPUSTYLE_STM32H7XX || CPUSTYLE_STM32MP1

	void RAMFUNC_NONILINE UART5_IRQHandler(void)
	{
		const uint_fast32_t isr = UART5->ISR;
		const uint_fast32_t cr1 = UART5->CR1;

		if (cr1 & USART_CR1_RXNEIE)
		{
			if (isr & USART_ISR_RXNE_RXFNE)
				HARDWARE_UART5_ONRXCHAR(UART5->RDR);
			if (isr & USART_ISR_ORE)
			{
				UART5->ICR = USART_ICR_ORECF;
				HARDWARE_UART5_ONOVERFLOW();
			}
			if (isr & USART_ISR_FE)
				UART5->ICR = USART_ICR_FECF;
		}
		if (cr1 & USART_CR1_TXEIE)
		{
			if (isr & USART_ISR_TXE_TXFNF)
				HARDWARE_UART5_ONTXCHAR(UART5);
		}
	}

#elif CPUSTYLE_STM32F30X || CPUSTYLE_STM32F0XX || CPUSTYLE_STM32L0XX || CPUSTYLE_STM32F7XX

	void RAMFUNC_NONILINE UART5_IRQHandler(void)
	{
		const uint_fast32_t isr = UART5->ISR;

		if (isr & USART_ISR_RXNE)
			HARDWARE_UART5_ONRXCHAR(UART5->RDR);
		if (isr & USART_ISR_ORE)
		{
			UART5->ICR = USART_ICR_ORECF;
			HARDWARE_UART5_ONOVERFLOW();
		}
		if (isr & USART_ISR_FE)
			UART5->ICR = USART_ICR_FECF;
		if (isr & USART_ISR_TXE)
			HARDWARE_UART5_ONTXCHAR(UART5);
	}

#elif CPUSTYLE_AT91SAM7S

	#if HARDWARE_ARM_USEUSART0
		static RAMFUNC_NONILINE void AT91F_US0Handler(void)
		{
			const uint_fast32_t csr = AT91C_BASE_US0->US_CSR;

			if (csr & AT91C_US_RXRDY)
				HARDWARE_UART5_ONRXCHAR(AT91C_BASE_US0->US_RHR);
			if (csr & AT91C_US_TXRDY)
				HARDWARE_UART5_ONTXCHAR(AT91C_BASE_US0);
		}
	#elif HARDWARE_ARM_USEUSART1
		static RAMFUNC_NONILINE void AT91F_US1Handler(void)
		{
			const uint_fast32_t csr = AT91C_BASE_US1->US_CSR;

			if (csr & AT91C_US_RXRDY)
				HARDWARE_UART5_ONRXCHAR(AT91C_BASE_US1->US_RHR);
			if (csr & AT91C_US_TXRDY)
				HARDWARE_UART5_ONTXCHAR(AT91C_BASE_US1);
		}
	#else	/* HARDWARE_ARM_USExxx */
		#error Wrong HARDWARE_ARM_USExxx value
	#endif		/* HARDWARE_ARM_USEUSART0 */

#elif CPUSTYLE_ATMEGA328

	ISR(USART_RX_vect)
	{
		HARDWARE_UART5_ONRXCHAR(UDR0);
	}

	ISR(USART_TX_vect)
	{
		HARDWARE_UART5_ONTXCHAR(NULL);
	}

#elif CPUSTYLE_ATMEGA_XXX4

	ISR(USART1_RX_vect)
	{
		HARDWARE_UART5_ONRXCHAR(UDR0);
	}

	ISR(USART1_TX_vect)
	{
		HARDWARE_UART5_ONTXCHAR(NULL);
	}

#elif CPUSTYLE_ATMEGA32

	ISR(USART_RXC_vect)
	{
		HARDWARE_UART5_ONRXCHAR(UDR);
	}

	ISR(USART_TXC_vect)
	{
		HARDWARE_UART5_ONTXCHAR(NULL);
	}

#elif CPUSTYLE_ATMEGA128

	ISR(USART1_RX_vect)
	{
		HARDWARE_UART5_ONRXCHAR(UDR1);
	}

	ISR(USART1_TX_vect)
	{
		HARDWARE_UART5_ONTXCHAR(NULL);
	}

#elif CPUSTYLE_ATXMEGAXXXA4

	ISR(USARTE1_RXC_vect)
	{
		HARDWARE_UART5_ONRXCHAR(USARTE1.DATA);
	}

	ISR(USARTE1_DRE_vect)
	{
		HARDWARE_UART5_ONTXCHAR(& USARTE1);
	}

#elif CPUSTYLE_R7S721

	// Приём символа он последовательного порта
	static void SCIFRXI3_IRQHandler(void)
	{
		(void) SCIF3.SCFSR;						// Перед сбросом бита RDF должно произойти его чтение в ненулевом состоянии
		SCIF3.SCFSR = (uint16_t) ~ SCIF3_SCFSR_RDF;	// RDF=0 читать незачем (в примерах странное - сбрасывабтся и другие биты)
		uint_fast8_t n = (SCIF3.SCFDR & SCIF3_SCFDR_R) >> SCIF3_SCFDR_R_SHIFT;
		while (n --)
			HARDWARE_UART5_ONRXCHAR(SCIF3.SCFRDR & SCIF3_SCFRDR_D);
	}

	// Передача символа в последовательный порт
	static void SCIFTXI3_IRQHandler(void)
	{
		HARDWARE_UART5_ONTXCHAR(& SCIF3);
	}

#else

	#error Undefined CPUSTYLE_XXX

#endif	/* CPUSTYLE_ATMEGA_XXX4 */


/* вызывается из обработчика прерываний или при запрещённых прерываниях. */
/*
		system_disableIRQ();
		system_enableIRQ();
*/
/* Разрешение/запрещение прерывания по передаче символа */
void hardware_uart5_enabletx(uint_fast8_t state)
{
#if CPUSTYLE_ATSAM3S || CPUSTYLE_ATSAM4S

	#if HARDWARE_ARM_USEUSART0

		if (state)
			USART0->US_IER = US_IER_TXRDY;
		else
			USART0->US_IDR = US_IDR_TXRDY;

	#elif HARDWARE_ARM_USEUSART1

		if (state)
			USART1->US_IER = US_IER_TXRDY;
		else
			USART1->US_IDR = US_IDR_TXRDY;

	#elif HARDWARE_ARM_USEUART0

		if (state)
			UART0->UART_IER = UART_IER_TXRDY;
		else
			UART0->UART_IDR = UART_IDR_TXRDY;

	#elif HARDWARE_ARM_USEUART1

		if (state)
			UART1->UART_IER = UART_IER_TXRDY;
		else
			UART1->UART_IDR = UART_IDR_TXRDY;

	#else	/* HARDWARE_ARM_USExxx */
		#error Wrong HARDWARE_ARM_USExxx value
	#endif /* HARDWARE_ARM_USEUSART0 */

#elif CPUSTYLE_AT91SAM7S

	#if HARDWARE_ARM_USEUSART0

	if (state)
		AT91C_BASE_US0->US_IER = AT91C_US_TXRDY;
	else
		AT91C_BASE_US0->US_IDR = AT91C_US_TXRDY;

	#elif HARDWARE_ARM_USEUSART1

	if (state)
		AT91C_BASE_US1->US_IER = AT91C_US_TXRDY;
	else
		AT91C_BASE_US1->US_IDR = AT91C_US_TXRDY;

	#else	/* HARDWARE_ARM_USExxx */
		#error Wrong HARDWARE_ARM_USExxx value
	#endif /* HARDWARE_ARM_USEUSART0 */

#elif CPUSTYLE_STM32F || CPUSTYLE_STM32MP1

	if (state)
		UART5->CR1 |= USART_CR1_TXEIE;
	else
		UART5->CR1 &= ~ USART_CR1_TXEIE;

#elif CPUSTYLE_ATMEGA_XXX4

	/* Used USART 1 */
	if (state)
	{
		UCSR1B |= (1U << TXCIE1);
		HARDWARE_UART5_ONTXCHAR(NULL);	// initiate 1-st character sending
	}
	else
	{
		UCSR1B &= ~ (1U << TXCIE1);
	}

#elif CPUSTYLE_ATMEGA32

	#error WITHUART5HW with CPUSTYLE_ATMEGA32 not supported
	if (state)
	{
		UCSRB |= (1U << TXCIE);
		HARDWARE_UART5_ONTXCHAR(NULL);	// initiate 1-st character sending
	}
	else
	{
		UCSRB &= ~ (1U << TXCIE);
	}

#elif CPUSTYLE_ATMEGA128

	/* Used USART 1 */
	if (state)
	{
		UCSR1B |= (1U << TXCIE1);
		HARDWARE_UART5_ONTXCHAR(NULL);	// initiate 1-st character sending
	}
	else
	{
		UCSR1B &= ~ (1U << TXCIE1);
	}

#elif CPUSTYLE_ATXMEGAXXXA4

	if (state)
		USARTE1.CTRLA = (USARTE1.CTRLA & ~ USART_DREINTLVL_gm) | USART_DREINTLVL_LO_gc;
	else
		USARTE1.CTRLA = (USARTE1.CTRLA & ~ USART_DREINTLVL_gm) | USART_DREINTLVL_OFF_gc;

#elif CPUSTYPE_TMS320F2833X

	if (state)
		SCIBCTL2 |= (1U << 0);	// TX INT ENA
	else
		SCIBCTL2 &= ~ (1U << 0); // TX INT ENA

#elif CPUSTYLE_R7S721

	if (state)
		SCIF3.SCSCR |= (1U << 7);	// TIE Transmit Interrupt Enable
	else
		SCIF3.SCSCR &= ~ (1U << 7);	// TIE Transmit Interrupt Enable

#else
	#error Undefined CPUSTYLE_XXX
#endif
}

/* вызывается из обработчика прерываний или при запрещённых прерываниях. */
/* Разрешение/запрещение прерываний про приёму символа */
void hardware_uart5_enablerx(uint_fast8_t state)
{
#if CPUSTYLE_ATSAM3S || CPUSTYLE_ATSAM4S

	#if HARDWARE_ARM_USEUSART0

		if (state)
			USART0->US_IER = US_IER_RXRDY;
		else
			USART0->US_IDR = US_IDR_RXRDY;

	#elif HARDWARE_ARM_USEUSART1

		if (state)
			USART1->US_IER = US_IER_RXRDY;
		else
			USART1->US_IDR = US_IDR_RXRDY;

	#elif HARDWARE_ARM_USEUART0

		if (state)
			UART0->UART_IER = UART_IER_RXRDY;
		else
			UART0->UART_IDR = UART_IDR_RXRDY;

	#elif HARDWARE_ARM_USEUART1

		if (state)
			UART1->UART_IER = UART_IER_RXRDY;
		else
			UART1->UART_IDR = UART_IDR_RXRDY;

	#else	/* HARDWARE_ARM_USExxx */
		#error Wrong HARDWARE_ARM_USExxx value
	#endif /* HARDWARE_ARM_USEUSART0 */

#elif CPUSTYLE_AT91SAM7S

	#if HARDWARE_ARM_USEUSART1

	if (state)
		AT91C_BASE_US1->US_IER = AT91C_US_RXRDY;
	else
		AT91C_BASE_US1->US_IDR = AT91C_US_RXRDY;

	#else	/* HARDWARE_ARM_USExxx */
		#error Wrong HARDWARE_ARM_USExxx value
	#endif /* HARDWARE_ARM_USEUSART0 */


#elif CPUSTYLE_ATMEGA_XXX4
	/* Used USART 1 */
	if (state)
		UCSR1B |= (1U << RXCIE1);
	else
		UCSR1B &= ~ (1U << RXCIE1);

#elif CPUSTYLE_ATMEGA32

	#error WITHUART5HW with CPUSTYLE_ATMEGA32 not supported
	if (state)
		UCSRB |= (1U << RXCIE);
	else
		UCSRB &= ~ (1U << RXCIE);

#elif CPUSTYLE_ATMEGA128

	/* Used USART 0 */
	if (state)
		UCSR1B |= (1U << RXCIE1);
	else
		UCSR1B &= ~ (1U << RXCIE1);

#elif CPUSTYLE_ATXMEGAXXXA4

	if (state)
		USARTE1.CTRLA = (USARTE1.CTRLA & ~ USART_RXCINTLVL_gm) | USART_RXCINTLVL_LO_gc;
	else
		USARTE1.CTRLA = (USARTE1.CTRLA & ~ USART_RXCINTLVL_gm) | USART_RXCINTLVL_OFF_gc;

#elif CPUSTYLE_STM32F || CPUSTYLE_STM32MP1

	if (state)
		UART5->CR1 |= USART_CR1_RXNEIE;
	else
		UART5->CR1 &= ~ USART_CR1_RXNEIE;

#elif CPUSTYPE_TMS320F2833X

	if (state)
		SCIBCTL2 |= (1U << 1);	// RX/BK INT ENA
	else
		SCIBCTL2 &= ~ (1U << 1); // RX/BK INT ENA

#elif CPUSTYLE_R7S721

	if (state)
		SCIF3.SCSCR |= (1U << 6);	// RIE Receive Interrupt Enable
	else
		SCIF3.SCSCR &= ~ (1U << 6);	// RIE Receive Interrupt Enable

#else
	#error Undefined CPUSTYLE_XXX

#endif
}


/* передача символа из обработчика прерывания готовности передатчика */
void hardware_uart5_tx(void * ctx, uint_fast8_t c)
{
#if CPUSTYLE_ATSAM3S || CPUSTYLE_ATSAM4S

	#if HARDWARE_ARM_USEUSART1
		USART1->US_THR = c;
	#elif HARDWARE_ARM_USEUART1
		UART1->UART_THR = c;
	#else	/* HARDWARE_ARM_USExxx */
		#error Wrong HARDWARE_ARM_USExxx value
	#endif

#elif CPUSTYLE_AT91SAM7S

	#if HARDWARE_ARM_USEUSART1
		AT91C_BASE_US1->US_THR = c;
	#else	/* HARDWARE_ARM_USExxx */
		#error Wrong HARDWARE_ARM_USExxx value
	#endif

#elif CPUSTYLE_ATMEGA128
	UDR1 = c;

#elif CPUSTYLE_ATMEGA_XXX4

	UDR1 = c;

#elif CPUSTYLE_ATMEGA32

	#error WITHUART5HW with CPUSTYLE_ATMEGA32 not supported
	UDR = c;

#elif CPUSTYLE_ATXMEGAXXXA4

	USARTE1.DATA = c;

#elif CPUSTYLE_STM32F1XX || CPUSTYLE_STM32F4XX

	UART5->DR = c;

#elif CPUSTYLE_STM32F30X || CPUSTYLE_STM32F0XX || CPUSTYLE_STM32L0XX || CPUSTYLE_STM32H7XX || CPUSTYLE_STM32F7XX || CPUSTYLE_STM32MP1

	UART5->TDR = c;

#elif CPUSTYPE_TMS320F2833X

	SCIBTXBUF = c;

#elif CPUSTYLE_R7S721

	(void) SCIF3.SCFSR;			// Перед сбросом бита TDFE должно произойти его чтение в ненулевом состоянии
	SCIF3.SCFTDR = c;
	SCIF3.SCFSR = (uint16_t) ~ (1U << SCIF3_SCFSR_TDFE_SHIFT);	// TDFE=0 читать незачем (в примерах странное)

#else
	#error Undefined CPUSTYLE_XXX
#endif
}



/* приём символа, если готов порт */
uint_fast8_t
hardware_uart5_getchar(char * cp)
{
#if CPUSTYLE_ATSAM3S || CPUSTYLE_ATSAM4S

	#if HARDWARE_ARM_USEUSART0
		if ((USART0->US_CSR & US_CSR_RXRDY) == 0)
			return 0;
		* cp = USART0->US_RHR;
	#elif HARDWARE_ARM_USEUSART1
		if ((USART1->US_CSR & US_CSR_RXRDY) == 0)
			return 0;
		* cp = USART1->US_RHR;
	#elif HARDWARE_ARM_USEUART0
		if ((UART0->UART_SR & UART_SR_RXRDY) == 0)
			return 0;
		* cp = UART0->UART_RHR;
	#elif HARDWARE_ARM_USEUART1
		if ((UART1->UART_SR & UART_SR_RXRDY) == 0)
			return 0;
		* cp = UART1->UART_RHR;
	#else	/* HARDWARE_ARM_USExxx */
		#error Wrong HARDWARE_ARM_USExxx value
	#endif

#elif CPUSTYLE_AT91SAM7S

	#if HARDWARE_ARM_USEUSART1
		if ((AT91C_BASE_US1->US_CSR & AT91C_US_RXRDY) == 0)
			return 0;
		* cp = AT91C_BASE_US1->US_RHR;
	#else	/* HARDWARE_ARM_USExxx */
		#error Wrong HARDWARE_ARM_USExxx value
	#endif

#elif CPUSTYLE_ATXMEGAXXXA4

	if ((USARTE1.STATUS & (1 << USART_RXCIF_bp)) == 0)
			return 0;
	* cp = USARTE1.DATA;

#elif CPUSTYLE_ATMEGA128

	if ((UCSR1A & (1 << RXC1)) == 0)
			return 0;
	* cp = UDR1;

#elif CPUSTYLE_ATMEGA_XXX4

	if ((UCSR1A & (1 << RXC1)) == 0)
			return 0;
	* cp = UDR1;

#elif CPUSTYLE_ATMEGA32

	#error WITHUART5HW with CPUSTYLE_ATMEGA32 not supported

	if ((UCSRA & (1 << RXC)) == 0)
			return 0;
	* cp = UDR;

#elif CPUSTYLE_STM32F1XX || CPUSTYLE_STM32F4XX

	if ((UART5->SR & (USART_SR_RXNE | USART_SR_ORE | USART_SR_FE | USART_SR_NE)) == 0)
		return 0;
	* cp = UART5->DR;

#elif CPUSTYLE_STM32H7XX || CPUSTYLE_STM32MP1

	const uint_fast32_t isr = UART5->ISR;
	if (isr & USART_ISR_ORE)
		UART5->ICR = USART_ICR_ORECF;
	if (isr & USART_ISR_FE)
		UART5->ICR = USART_ICR_FECF;
	if ((isr & USART_ISR_RXNE_RXFNE) == 0)
		return 0;
	* cp = UART5->RDR;

#elif CPUSTYLE_STM32F30X || CPUSTYLE_STM32F0XX || CPUSTYLE_STM32L0XX || CPUSTYLE_STM32F7XX

	const uint_fast32_t isr = UART5->ISR;
	if (isr & USART_ISR_ORE)
		UART5->ICR = USART_ICR_ORECF;
	if (isr & USART_ISR_FE)
		UART5->ICR = USART_ICR_FECF;
	if ((isr & USART_ISR_RXNE) == 0)
		return 0;
	* cp = UART5->RDR;

#elif CPUSTYPE_TMS320F2833X

	if ((SCIBRXST & (1U << 6)) == 0)	// Wait for RXRDY bit
		return 0;
	* cp = SCIBRXBUF;

#elif CPUSTYLE_R7S721

	if ((SCIF3.SCFSR & (1U << 1)) == 0)	// RDF
		return 0;
	* cp = SCIF3.SCFRDR;
	SCIF3.SCFSR = (uint16_t) ~ (1U << 1);	// RDF=0 читать незачем (в примерах странное)

#else
	#error Undefined CPUSTYLE_XXX
#endif
	return 1;
}

/* передача символа если готов порт */
uint_fast8_t
hardware_uart5_putchar(uint_fast8_t c)
{
#if CPUSTYLE_ATSAM3S || CPUSTYLE_ATSAM4S

	#if HARDWARE_ARM_USEUSART0
		if ((USART0->US_CSR & US_CSR_TXRDY) == 0)
			return 0;
		USART0->US_THR = c;
	#elif HARDWARE_ARM_USEUSART1
		if ((USART1->US_CSR & US_CSR_TXRDY) == 0)
			return 0;
		USART1->US_THR = c;
	#elif HARDWARE_ARM_USEUART0
		if ((UART0->UART_SR & UART_SR_TXRDY) == 0)
			return 0;
		UART0->UART_THR = c;
	#elif HARDWARE_ARM_USEUART1
		if ((UART1->UART_SR & UART_SR_TXRDY) == 0)
			return 0;
		UART1->UART_THR = c;
	#else	/* HARDWARE_ARM_USExxx */
		#error Wrong HARDWARE_ARM_USExxx value
	#endif

#elif CPUSTYLE_AT91SAM7S

	#if HARDWARE_ARM_USEUSART0
		if ((AT91C_BASE_US0->US_CSR & AT91C_US_TXRDY) == 0)
			return 0;
		AT91C_BASE_US0->US_THR = c;
	#elif HARDWARE_ARM_USEUSART1
		if ((AT91C_BASE_US1->US_CSR & AT91C_US_TXRDY) == 0)
			return 0;
		AT91C_BASE_US1->US_THR = c;
	#else	/* HARDWARE_ARM_USExxx */
		#error Wrong HARDWARE_ARM_USExxx value
	#endif

#elif CPUSTYLE_ATXMEGAXXXA4

	if ((USARTE0.STATUS & USART_DREIF_bm) == 0)
		return 0;
	USARTE0.DATA = c;

#elif CPUSTYLE_ATMEGA_XXX4

	if ((UCSR0A & (1 << UDRE0)) == 0)
		return 0;
	UDR0 = c;

#elif CPUSTYLE_ATMEGA128

	if ((UCSR0A & (1 << UDRE0)) == 0)
		return 0;
	UDR0 = c;

#elif CPUSTYLE_ATMEGA32

	#error WITHUART5HW with CPUSTYLE_ATMEGA32 not supported

	if ((UCSRA & (1 << UDRE)) == 0)
		return 0;
	UDR = c;

#elif CPUSTYLE_STM32F1XX || CPUSTYLE_STM32F4XX

	if ((UART5->SR & USART_SR_TXE) == 0)
		return 0;
	UART5->DR = c;

#elif CPUSTYLE_STM32H7XX || CPUSTYLE_STM32MP1

	if ((UART5->ISR & USART_ISR_TXE_TXFNF) == 0)
		return 0;
	UART5->TDR = c;

#elif CPUSTYLE_STM32F30X || CPUSTYLE_STM32F0XX || CPUSTYLE_STM32L0XX || CPUSTYLE_STM32F7XX

	if ((UART5->ISR & USART_ISR_TXE) == 0)
		return 0;
	UART5->TDR = c;

#elif CPUSTYPE_TMS320F2833X

	if ((SCIBCTL2 & (1U << 7)) == 0)	// wait for TXRDY bit
		return 0;
	SCIBTXBUF = c;

#elif CPUSTYLE_R7S721

	if ((SCIF3.SCFSR & (1U << SCIF3_SCFSR_TDFE_SHIFT)) == 0)	// Перед сбросом бита TDFE должно произойти его чтение в ненулевом состоянии
		return 0;
	SCIF3.SCFTDR = c;
	SCIF3.SCFSR = (uint16_t) ~ (1U << SCIF3_SCFSR_TDFE_SHIFT);	// TDFE=0 читать незачем (в примерах странное)

#else
	#error Undefined CPUSTYLE_XXX
#endif
	return 1;
}

void hardware_uart5_initialize(uint_fast8_t debug)
{
#if CPUSTYLE_ATSAM3S || CPUSTYLE_ATSAM4S

	#if HARDWARE_ARM_USEUSART0
		// enable the clock of USART0
		PMC->PMC_PCER0 = 1UL << ID_USART0;

		HARDWARE_UART5_INITIALIZE();	/* Присоединить периферию к выводам */

		// reset the UART
		USART0->US_CR = US_CR_RSTRX | US_CR_RSTTX | US_CR_RXDIS | US_CR_TXDIS;
		// set serial line mode
		USART0->US_MR =
			US_MR_CHMODE_NORMAL |// Normal Mode
			US_MR_USCLKS_MCK |   // Clock = MCK
			US_MR_CHRL_8_BIT |
			US_MR_PAR_NO |
			US_MR_NBSTOP_1_BIT;

		USART0->US_IDR = (US_IDR_RXRDY | US_IDR_TXRDY);

		if (debug == 0)
		{
			serial_set_handler(USART0_IRQn, & USART0_IRQHandler);
		}

		USART0->US_CR = US_CR_RXEN | US_CR_TXEN;	// разрешаем приёмник и передатчик.

	#elif HARDWARE_ARM_USEUSART1
		// enable the clock of USART1
		PMC->PMC_PCER0 = 1UL << ID_USART1;

		HARDWARE_UART5_INITIALIZE();	/* Присоединить периферию к выводам */

		// reset the UART
		USART1->US_CR = US_CR_RSTRX | US_CR_RSTTX | US_CR_RXDIS | US_CR_TXDIS;
		// set serial line mode
		USART1->US_MR =
			US_MR_CHMODE_NORMAL |// Normal Mode
			US_MR_USCLKS_MCK |   // Clock = MCK
			US_MR_CHRL_8_BIT |
			US_MR_PAR_NO |
			US_MR_NBSTOP_1_BIT;

		USART1->US_IDR = (US_IDR_RXRDY | US_IDR_TXRDY);

		if (debug == 0)
		{
			serial_set_handler(USART1_IRQn, & USART1_IRQHandler);
		}

		USART1->US_CR = US_CR_RXEN | US_CR_TXEN;	// разрешаем приёмник и передатчик.

	#elif HARDWARE_ARM_USEUART0

		// enable the clock of UART0
		PMC->PMC_PCER0 = 1UL << ID_UART0;

		HARDWARE_UART5_INITIALIZE();	/* Присоединить периферию к выводам */

		// reset the UART
		UART0->UART_CR = UART_CR_RSTRX | UART_CR_RSTTX | UART_CR_RXDIS | UART_CR_TXDIS;
		// set serial line mode
		UART0->UART_MR =
			UART_MR_CHMODE_NORMAL |// Normal Mode
			//UART_MR_USCLKS_MCK |   // Clock = MCK
			//UART_MR_CHRL_8_BIT |
			UART_MR_PAR_NO |
			//UART_MR_NBSTOP_1_BIT |
			0;

		UART0->UART_IDR = (UART_IDR_RXRDY | UART_IDR_TXRDY);

		if (debug == 0)
		{
			serial_set_handler(UART0_IRQn, & UART0_IRQHandler);
		}

		UART0->UART_CR = UART_CR_RXEN | UART_CR_TXEN;	// разрешаем приёмник и передатчик.

	#elif HARDWARE_ARM_USEUART1

		// enable the clock of UART1
		PMC->PMC_PCER0 = 1UL << ID_UART5;

		HARDWARE_UART5_INITIALIZE();	/* Присоединить периферию к выводам */

		// reset the UART
		UART5->UART_CR = UART_CR_RSTRX | UART_CR_RSTTX | UART_CR_RXDIS | UART_CR_TXDIS;
		// set serial line mode
		UART5->UART_MR =
			UART_MR_CHMODE_NORMAL |// Normal Mode
			//UART_MR_USCLKS_MCK |   // Clock = MCK
			//UART_MR_CHRL_8_BIT |
			UART_MR_PAR_NO |
			//UART_MR_NBSTOP_1_BIT |
			0;

		UART5->UART_IDR = (UART_IDR_RXRDY | UART_IDR_TXRDY);

		if (debug == 0)
		{
			serial_set_handler(UART5_IRQn, & UART5_IRQHandler);
		}

		UART5->UART_CR = UART_CR_RXEN | UART_CR_TXEN;	// разрешаем приёмник и передатчик.

	#else	/* HARDWARE_ARM_USExxx */
		#error Wrong HARDWARE_ARM_USExxx value
	#endif /* HARDWARE_ARM_USEUSART0 */


#elif CPUSTYLE_STM32F1XX

	RCC->APB1ENR |= RCC_APB1ENR_UART5EN; // Включение тактирования UART5.
	(void) RCC->APB1ENR;

	UART5->CR1 |= (USART_CR1_RE | USART_CR1_TE); // Transmitter Enable & Receiver Enables


	RCC->APB2ENR |= RCC_APB2ENR_AFIOEN;     //включить тактирование альтернативных функций
	(void) RCC->APB2ENR;

	HARDWARE_UART5_INITIALIZE();	/* Присоединить периферию к выводам */

	if (debug == 0)
	{
		serial_set_handler(UART5_IRQn, & UART5_IRQHandler);
	}

	UART5->CR1 |= USART_CR1_UE; // Включение UART5.

#elif CPUSTYLE_STM32H7XX

	RCC->APB1LENR |= RCC_APB1LENR_UART5EN; // Включение тактирования UART5.
	(void) RCC->APB1LENR;

	UART5->CR1 = 0;

#if WITHUARTFIFO
	UART5->CR1 |= USART_CR1_FIFOEN_Msk;
#else /* WITHUARTFIFO */
	UART5->CR1 &= ~ USART_CR1_FIFOEN_Msk;
#endif /* WITHUARTFIFO */

	UART5->CR1 |= (USART_CR1_RE | USART_CR1_TE); // Transmitter Enable & Receiver Enables

	HARDWARE_UART5_INITIALIZE();	/* Присоединить периферию к выводам */

	if (debug == 0)
	{
		serial_set_handler(UART5_IRQn, & UART5_IRQHandler);
	}

	UART5->CR1 |= USART_CR1_UE; // Включение UART5.

#elif CPUSTYLE_STM32F30X || CPUSTYLE_STM32F4XX || CPUSTYLE_STM32F0XX || CPUSTYLE_STM32L0XX || CPUSTYLE_STM32F7XX

	RCC->APB1ENR |= RCC_APB1ENR_UART5EN; // Включение тактирования UART5.
	(void) RCC->APB1ENR;

	UART5->CR1 |= (USART_CR1_RE | USART_CR1_TE); // Transmitter Enable & Receiver Enables

	HARDWARE_UART5_INITIALIZE();	/* Присоединить периферию к выводам */

	if (debug == 0)
	{
		serial_set_handler(UART5_IRQn, & UART5_IRQHandler);
	}

	UART5->CR1 |= USART_CR1_UE; // Включение UART5.

#elif CPUSTYLE_AT91SAM7S

	#if HARDWARE_ARM_USEUSART0
		// enable the clock of USART0
		AT91C_BASE_PMC->PMC_PCER = 1UL << AT91C_ID_US0;

		HARDWARE_UART5_INITIALIZE();	/* Присоединить периферию к выводам */

		// disable I/O pullup
		// reset the UART
		AT91C_BASE_US0->US_CR = AT91C_US_RSTRX | AT91C_US_RSTTX | AT91C_US_RXDIS | AT91C_US_TXDIS;
		// set serial line mode
		AT91C_BASE_US0->US_MR =
							AT91C_US_OVER |
							AT91C_US_USMODE_NORMAL |// Normal Mode
						   AT91C_US_CLKS_CLOCK |   // Clock = MCK
						   AT91C_US_CHRL_8_BITS |
						   AT91C_US_PAR_NONE |
						   AT91C_US_NBSTOP_1_BIT;
		AT91C_BASE_US0->US_IDR = (AT91C_US_RXRDY | AT91C_US_TXRDY);

		{
			enum { irqID = AT91C_ID_US0 };
			// programming interrupts from ADC
			AT91C_BASE_AIC->AIC_IDCR = (1UL << irqID);
			AT91C_BASE_AIC->AIC_SVR [irqID] = (AT91_REG) AT91F_US0Handler;
			AT91C_BASE_AIC->AIC_SMR [irqID] =
				(AT91C_AIC_SRCTYPE & AT91C_AIC_SRCTYPE_INT_HIGH_LEVEL) |
				(AT91C_AIC_PRIOR & AT91C_AIC_PRIOR_LOWEST);
			AT91C_BASE_AIC->AIC_ICCR = (1UL << irqID);		// clear pending interrupt
			AT91C_BASE_AIC->AIC_IECR = (1UL << irqID);	// enable inerrupt

		}

		AT91C_BASE_US0->US_CR = AT91C_US_RXEN  | AT91C_US_TXEN;	// разрешаем приёмник и передатчик.

	#elif HARDWARE_ARM_USEUSART1

		// enable the clock of UART5
		AT91C_BASE_PMC->PMC_PCER = 1UL << AT91C_ID_US1;

		HARDWARE_UART5_INITIALIZE();	/* Присоединить периферию к выводам */

		// enable uart pins on PIO
		AT91C_BASE_PIOA->PIO_ASR = AT91C_PA21_RXD1 | AT91C_PA22_TXD1; // assigns the 2 I/O lines to peripheral A function
		AT91C_BASE_PIOA->PIO_PDR = AT91C_PA21_RXD1 | AT91C_PA22_TXD1;	// enable peripheral control of PA21,PA22 (RXD1 and TXD1)
		// reset the UART
		AT91C_BASE_US1->US_CR = AT91C_US_RSTRX | AT91C_US_RSTTX | AT91C_US_RXDIS | AT91C_US_TXDIS;
		// set serial line mode
		AT91C_BASE_US1->US_MR =
							AT91C_US_OVER |
							AT91C_US_USMODE_NORMAL |// Normal Mode
						   AT91C_US_CLKS_CLOCK |   // Clock = MCK
						   AT91C_US_CHRL_8_BITS |
						   AT91C_US_PAR_NONE |
						   AT91C_US_NBSTOP_1_BIT;

		AT91C_BASE_US1->US_IDR = (AT91C_US_RXRDY | AT91C_US_TXRDY);

		if (debug == 0)
		{
			serial_set_handler(AT91C_ID_US1, & AT91F_US1Handler);
		}

		AT91C_BASE_US1->US_CR = AT91C_US_RXEN | AT91C_US_TXEN;	// разрешаем приёмник и передатчик.
	#else	/* HARDWARE_ARM_USExxx */
		#error Wrong HARDWARE_ARM_USExxx value
	#endif /* HARDWARE_ARM_USEUSART0 */

#elif CPUSTYLE_ATMEGA_XXX4

	// USART initialization
	UCSR1B = (1U << RXEN1) | (1U << TXEN1) /* | (1U << UCSZ12) */;
	UCSR1C = (1U << UCSZ11) | (1U << UCSZ10);	// asynchronious mode, 8 bit.
	// enable pull-up registers for RXD and TXD pins: then rx or tx disabled, these pins disconnected fron UART
	PORTD |= ((1U << PD2) | (1U << PD3));
#if defined (DDRD2) && defined (DDRD3)
	DDRD &= ~ ((1U << DDRD2) | (1U << DDRD3));
#else
	DDRD &= ~ ((1U << DDD2) | (1U << DDD3));
#endif

#elif CPUSTYLE_ATMEGA128

	// USART initialization
	UCSR1B = (1U << RXEN1) | (1U << TXEN1) /* | (1U << UCSZ12) */;
	UCSR1C = (1U << UCSZ11) | (1U << UCSZ10);	// asynchronious mode, 8 bit.
	// enable pull-up registers for RXD and TXD pins: then rx or tx disabled, these pins disconnected fron UART
	//PORTE |= ((1U << PE0) | (1U << PE1));

#elif CPUSTYLE_ATXMEGAXXXA4

xxxx!;
	PORTE.DIRSET = PIN3_bm; // PE3 (TXD0) as output
	PORTE.DIRCLR = PIN2_bm; // PE2 (RXD0) as input
	PORTE_PIN2CTRL = (PORTE_PIN2CTRL & ~ PORT_OPC_gm) | PORT_OPC_PULLUP_gc;							// pin is pulled high

	USARTE1.CTRLC = USART_CMODE_ASYNCHRONOUS_gc | USART_PMODE_DISABLED_gc | USART_CHSIZE_8BIT_gc;
	USARTE1.CTRLB = USART_RXEN_bm | USART_TXEN_bm;


#elif CPUSTYLE_ATMEGA32

	#error WITHUART5HW with CPUSTYLE_ATMEGA not supported

#elif CPUSTYPE_TMS320F2833X
xxxx!;
	// Enable SCI-B clock
	PCLKCR0 |= (1U << 10);	// SCIBENCLK

	//SCIBCTL1 &= ~ (1U << 5);	// SW RESET on
	SCIBCTL1 |= (1U << 5);	// SW RESET off

	SCIBCCR =
			(7U << 0) |	// Data length = 8 bit
			(0U << 5) | // Parity enable
			(0U << 6) | // Evan/Odd parity
			(0U << 7);	// 0 - one stop bit, 1 - to stop bits


	tms320_hardware_piob_periph(
			(1UL << (35 % 32)) |	// SCITXDA
			(1UL << (36 % 32)),	// SCIRXDA
			1	// mux = 1
			);

	SCIBCTL1 |= (1U << 0) |	// RX enable
				(1U << 1);	// TX enable

#elif CPUSTYLE_R7S721

    /* ---- Supply clock to the SCIF(channel 1) ---- */
	CPG.STBCR4 &= ~ CPG_STBCR4_BIT_MSTP44;	// Module Stop 44 - SCIF3
	(void) CPG.STBCR4;			/* Dummy read */

	SCIF3.SCSCR = 0x0000;	/* SCIF transmitting and receiving operations stop, internal clock */

	SCIF3.SCSCR = (SCIF3.SCSCR & ~ 0x03) |
		0x00 |						// internal clock
		0;

	/* ---- Serial status register(SCFSR2) setting ---- */
	/* ER,BRK,DR bit clear */
	(void) SCIF3.SCFSR;						// Перед сбросом бита xxx должно произойти его чтение в ненулевом состоянии
	SCIF3.SCFSR = ~ 0x0091;	// 0xFF6E;

	/* ---- Line status register (SCLSR2) setting ---- */
	/* ORER bit clear */
	//SCIF3.SCLSR.BIT.ORER  = 0;
	SCIF3.SCLSR &= ~ 0x0001;

	SCIF3.SCSMR =
		0x00 |	/* 8-N-1 format */
		0;

	/* ---- FIFO control register (SCFCR2) setting ---- */
	/*  RTS output active trigger        :Initial value	*/
	/*  Receive FIFO data trigger        :1-data		*/
	/*  Transmit FIFO data trigger       :0-data		*/
	/*  Modem control enable             :Disabled		*/
	/*  Receive FIFO data register reset :Disabled		*/
	/*  Loop-back test                   :Disabled 		*/
	SCIF3.SCFCR = 0x0030;

	/* ---- Serial port register (SCSPTR2) setting ---- */
	/* Serial port  break output(SPB2IO)  1: Enabled */
	/* Serial port break data(SPB2DT)  1: High-level */
	//SCIF3.SCSPTR |= 0x0003;

	if (debug == 0)
	{
	   serial_set_handler(SCIFRXI3_IRQn, SCIFRXI3_IRQHandler);
	   serial_set_handler(SCIFTXI3_IRQn, SCIFTXI3_IRQHandler);
	}
	HARDWARE_UART5_INITIALIZE();	/* Присоединить периферию к выводам */

	SCIF3.SCSCR |= 0x0030;	// TE RE - SCIF3 transmitting and receiving operations are enabled */

#elif CPUSTYLE_STM32MP1

	RCC->MP_APB1ENSETR = RCC_MP_APB1ENSETR_UART5EN; // Включение тактирования UART5.
	(void) RCC->MP_APB1ENSETR;
	RCC->MP_APB1LPENSETR = RCC_MP_APB1LPENSETR_UART5LPEN; // Включение тактирования UART5.
	(void) RCC->MP_APB1LPENSETR;
	RCC->APB1RSTSETR = RCC_APB1RSTSETR_UART5RST; // Установить сброс UART5.
	(void) RCC->APB1RSTSETR;
	RCC->APB1RSTCLRR = RCC_APB1RSTCLRR_UART5RST; // Снять брос UART5.
	(void) RCC->APB1RSTCLRR;

	UART5->CR1 = 0;

#if WITHUARTFIFO
	UART5->CR1 |= USART_CR1_FIFOEN_Msk;
#else /* WITHUARTFIFO */
	UART5->CR1 &= ~ USART_CR1_FIFOEN_Msk;
#endif /* WITHUARTFIFO */

	UART5->CR1 |= (USART_CR1_RE | USART_CR1_TE); // Transmitter Enable & Receiver Enables

	HARDWARE_UART5_INITIALIZE();	/* Присоединить периферию к выводам */

	if (debug == 0)
	{
		serial_set_handler(UART5_IRQn, UART5_IRQHandler);
	}

	UART5->CR1 |= USART_CR1_UE; // Включение USART1.

#else
	#error Undefined CPUSTYLE_XXX
#endif

}

#endif /* WITHUART5HW */

#if WITHUART1HW

void
hardware_uart1_set_speed(uint_fast32_t baudrate)
{
#if CPUSTYLE_ATSAM3S || CPUSTYLE_ATSAM4S

	#if HARDWARE_ARM_USEUSART0
		// Использование автоматического расчёта предделителя
		unsigned value;
		const uint_fast8_t prei = calcdivider(calcdivround2(CPU_FREQ, baudrate), ATSAM3S_USART_BRGR_WIDTH, ATSAM3S_USART_BRGR_TAPS, & value, 0);
		USART0->US_BRGR = value;
		if (prei == 0)
		{
			USART0->US_MR |= US_MR_OVER;
		}
		else
		{
			USART0->US_MR &= ~ US_MR_OVER;
		}
	#elif HARDWARE_ARM_USEUSART1
		// Использование автоматического расчёта предделителя
		unsigned value;
		const uint_fast8_t prei = calcdivider(calcdivround2(CPU_FREQ, baudrate), ATSAM3S_USART_BRGR_WIDTH, ATSAM3S_USART_BRGR_TAPS, & value, 0);
		USART1->US_BRGR = value;
		if (prei == 0)
		{
			USART1->US_MR |= US_MR_OVER;
		}
		else
		{
			USART1->US_MR &= ~ US_MR_OVER;
		}

	#elif HARDWARE_ARM_USEUART0
		// Использование автоматического расчёта предделителя
		unsigned value;
		calcdivider(calcdivround2(CPU_FREQ, baudrate), ATSAM3S_UART_BRGR_WIDTH, ATSAM3S_UART_BRGR_TAPS, & value, 0);
		UART0->UART_BRGR = value;
	#elif HARDWARE_ARM_USEUART1
		// Использование автоматического расчёта предделителя
		unsigned value;
		calcdivider(calcdivround2(CPU_FREQ, baudrate), ATSAM3S_UART_BRGR_WIDTH, ATSAM3S_UART_BRGR_TAPS, & value, 0);
		UART1->UART_BRGR = value;
	#else	/* HARDWARE_ARM_USExxx */
		#error Wrong HARDWARE_ARM_USExxx value
	#endif

#elif CPUSTYLE_AT91SAM7S

	// Использование автоматического расчёта предделителя
	unsigned value;
	const uint_fast8_t prei = calcdivider(calcdivround2(CPU_FREQ, baudrate), AT91SAM7_USART_BRGR_WIDTH, AT91SAM7_USART_BRGR_TAPS, & value, 0);

	#if HARDWARE_ARM_USEUSART0
		AT91C_BASE_US0->US_BRGR = value;
		if (prei == 0)
		{
			AT91C_BASE_US0->US_MR |= AT91C_US_OVER;
		}
		else
		{
			AT91C_BASE_US0->US_MR &= ~ AT91C_US_OVER;
		}
	#elif HARDWARE_ARM_USEUSART1
		AT91C_BASE_US1->US_BRGR = value;
		if (prei == 0)
		{
			AT91C_BASE_US1->US_MR |= AT91C_US_OVER;
		}
		else
		{
			AT91C_BASE_US1->US_MR &= ~ AT91C_US_OVER;
		}
	#else	/* HARDWARE_ARM_USExxx */
		#error Wrong HARDWARE_ARM_USExxx value
	#endif

#elif CPUSTYLE_ATMEGA_XXX4

	// Использование автоматического расчёта предделителя
	unsigned value;
	const uint_fast8_t prei = calcdivider(calcdivround2(CPU_FREQ, baudrate), ATMEGA_UBR_WIDTH, ATMEGA_UBR_TAPS, & value, 1);

	if (prei == 0)
		UCSR0A |= (1U << U2X0);
	else
		UCSR0A &= ~ (1U << U2X0);

	UBRR0 = value;	/* Значение получено уже уменьшенное на 1 */


#elif CPUSTYLE_ATMEGA128

	// Использование автоматического расчёта предделителя
	unsigned value;
	const uint_fast8_t prei = calcdivider(calcdivround2(CPU_FREQ, baudrate), ATMEGA_UBR_WIDTH, ATMEGA_UBR_TAPS, & value, 1);

	if (prei == 0)
		UCSR0A |= (1U << U2X0);
	else
		UCSR0A &= ~ (1U << U2X0);

	UBRR0H = (value >> 8) & 0xff;	/* Значение получено уже уменьшенное на 1 */
	UBRR0L = value & 0xff;

#elif CPUSTYLE_ATMEGA

	// Использование автоматического расчёта предделителя
	unsigned value;
	const uint_fast8_t prei = calcdivider(calcdivround2(CPU_FREQ, baudrate), ATMEGA_UBR_WIDTH, ATMEGA_UBR_TAPS, & value, 1);

	if (prei == 0)
		UCSRA |= (1U << U2X);
	else
		UCSRA &= ~ (1U << U2X);

	UBRRH = (value >> 8) & 0xff;	/* Значение получено уже уменьшенное на 1 */
	UBRRL = value & 0xff;

#elif CPUSTYLE_ATXMEGAXXXA4

	// Использование автоматического расчёта предделителя
	unsigned value;
	const uint_fast8_t prei = calcdivider(calcdivround2(CPU_FREQ, baudrate), ATXMEGA_UBR_WIDTH, ATXMEGA_UBR_TAPS, & value, 1);
	if (prei == 0)
		USARTE0.CTRLB |= USART_CLK2X_bm;
	else
		USARTE0.CTRLB &= ~USART_CLK2X_bm;
	// todo: проверить требование к порядку обращения к портам
	USARTE0.BAUDCTRLA = (value & 0xff);	/* Значение получено уже уменьшенное на 1 */
	USARTE0.BAUDCTRLB = (ATXMEGA_UBR_BSEL << 4) | ((value >> 8) & 0x0f);

#elif CPUSTYLE_STM32MP1

	// usart1
	USART1->BRR = calcdivround2(BOARD_USART1_FREQ, baudrate);		// младшие 4 бита - это дробная часть.

#elif CPUSTYLE_STM32F

	// uart1 on apb2 up to 72/36 MHz

	USART1->BRR = calcdivround2(BOARD_USART1_FREQ, baudrate);		// младшие 4 бита - это дробная часть.

#elif CPUSTYPE_TMS320F2833X

	const unsigned long lspclk = CPU_FREQ / 4;
	const unsigned long brr = (lspclk / 8) / baudrate;	// @ CPU_FREQ = 100 MHz, 9600 can not be programmed

	SCIAHBAUD = (brr - 1) >> 8;		// write 8 bits, not 16
	SCIALBAUD = (brr - 1) >> 0;

#elif CPUSTYLE_R7S721

	// Использование автоматического расчёта предделителя
	unsigned value;
	const uint_fast8_t prei = calcdivider(calcdivround_p1clock(baudrate), R7S721_SCIF_SCBRR_WIDTH, R7S721_SCIF_SCBRR_TAPS, & value, 1);

	SCIF0.SCSMR = (SCIF0.SCSMR & ~ 0x03) |
		scemr_scsmr [prei].scsmr |	// prescaler: 0: /1, 1: /4, 2: /16, 3: /64
		0;
	SCIF0.SCEMR = (SCIF0.SCEMR & ~ (0x80 | 0x01)) |
		0 * 0x80 |						// BGDM
		scemr_scsmr [prei].scemr |	// ABCS = 8/16 clocks per bit
		0;
	SCIF0.SCBRR = value;	/* Bit rate register */

#elif CPUSTYLE_XC7Z || CPUSTYLE_XCZU

	  uint32_t r; // Temporary value variable
	  r = UART0->CR;
	  r &= ~(XUARTPS_CR_TX_EN | XUARTPS_CR_RX_EN); // Clear Tx & Rx Enable
	  r |= XUARTPS_CR_RX_DIS | XUARTPS_CR_TX_DIS; // Tx & Rx Disable
	  UART0->CR = r;
	  const unsigned long sel_clk = xc7z_get_uart_freq();
	  const unsigned long bdiv = 8;
	  // baud_rate = sel_clk / (CD * (BDIV + 1) (ref: UG585 - TRM - Ch. 19 UART)
	  UART0->BAUDDIV = bdiv - 1; // ("BDIV")
	  UART0->BAUDGEN = calcdivround2(sel_clk, baudrate * bdiv); // ("CD")
	  // Baud Rate = 100Mhz / (124 * (6 + 1)) = 115200 bps
	  UART0->CR |= (XUARTPS_CR_TXRST | XUARTPS_CR_RXRST); // TX & RX logic reset

	  r = UART0->CR;
	  r |= XUARTPS_CR_RX_EN | XUARTPS_CR_TX_EN; // Set TX & RX enabled
	  r &= ~(XUARTPS_CR_RX_DIS | XUARTPS_CR_TX_DIS); // Clear TX & RX disabled
	  UART0->CR = r;

#elif CPUSTYPE_T113

	unsigned divisor = calcdivround2(BOARD_USART_FREQ, baudrate * 16);

	UART0->UART_LCR |= (1 << 7);
	UART0->DATA = divisor & 0xff;
	UART0->DLH_IER = (divisor >> 8) & 0xff;
	UART0->UART_LCR &= ~ (1 << 7);
#else
	#error Undefined CPUSTYLE_XXX
#endif

}

#endif /* WITHUART1HW */


#if WITHUART2HW

void
hardware_uart2_set_speed(uint_fast32_t baudrate)
{
#if CPUSTYLE_ATSAM3S || CPUSTYLE_ATSAM4S

	#if HARDWARE_ARM_USEUSART0
		// Использование автоматического расчёта предделителя
		unsigned value;
		const uint_fast8_t prei = calcdivider(calcdivround2(CPU_FREQ, baudrate), ATSAM3S_USART_BRGR_WIDTH, ATSAM3S_USART_BRGR_TAPS, & value, 0);
		USART0->US_BRGR = value;
		if (prei == 0)
		{
			USART0->US_MR |= US_MR_OVER;
		}
		else
		{
			USART0->US_MR &= ~ US_MR_OVER;
		}
	#elif HARDWARE_ARM_USEUSART1
		// Использование автоматического расчёта предделителя
		unsigned value;
		const uint_fast8_t prei = calcdivider(calcdivround2(CPU_FREQ, baudrate), ATSAM3S_USART_BRGR_WIDTH, ATSAM3S_USART_BRGR_TAPS, & value, 0);
		USART1->US_BRGR = value;
		if (prei == 0)
		{
			USART1->US_MR |= US_MR_OVER;
		}
		else
		{
			USART1->US_MR &= ~ US_MR_OVER;
		}

	#elif HARDWARE_ARM_USEUART0
		// Использование автоматического расчёта предделителя
		unsigned value;
		calcdivider(calcdivround2(CPU_FREQ, baudrate), ATSAM3S_UART_BRGR_WIDTH, ATSAM3S_UART_BRGR_TAPS, & value, 0);
		UART0->UART_BRGR = value;
	#elif HARDWARE_ARM_USEUART1
		// Использование автоматического расчёта предделителя
		unsigned value;
		calcdivider(calcdivround2(CPU_FREQ, baudrate), ATSAM3S_UART_BRGR_WIDTH, ATSAM3S_UART_BRGR_TAPS, & value, 0);
		UART1->UART_BRGR = value;
	#else	/* HARDWARE_ARM_USExxx */
		#error Wrong HARDWARE_ARM_USExxx value
	#endif

#elif CPUSTYLE_AT91SAM7S

	// Использование автоматического расчёта предделителя
	unsigned value;
	const uint_fast8_t prei = calcdivider(calcdivround2(CPU_FREQ, baudrate), AT91SAM7_USART_BRGR_WIDTH, AT91SAM7_USART_BRGR_TAPS, & value, 0);

	#if HARDWARE_ARM_USEUSART0
		AT91C_BASE_US0->US_BRGR = value;
		if (prei == 0)
		{
			AT91C_BASE_US0->US_MR |= AT91C_US_OVER;
		}
		else
		{
			AT91C_BASE_US0->US_MR &= ~ AT91C_US_OVER;
		}
	#elif HARDWARE_ARM_USEUSART1
		AT91C_BASE_US1->US_BRGR = value;
		if (prei == 0)
		{
			AT91C_BASE_US1->US_MR |= AT91C_US_OVER;
		}
		else
		{
			AT91C_BASE_US1->US_MR &= ~ AT91C_US_OVER;
		}
	#else	/* HARDWARE_ARM_USExxx */
		#error Wrong HARDWARE_ARM_USExxx value
	#endif

#elif CPUSTYLE_ATMEGA_XXX4

	// Использование автоматического расчёта предделителя
	unsigned value;
	const uint_fast8_t prei = calcdivider(calcdivround2(CPU_FREQ, baudrate), ATMEGA_UBR_WIDTH, ATMEGA_UBR_TAPS, & value, 1);

	if (prei == 0)
		UCSR1A |= (1U << U2X1);
	else
		UCSR1A &= ~ (1U << U2X1);

	UBRR1 = value;	/* Значение получено уже уменьшенное на 1 */


#elif CPUSTYLE_ATMEGA128

	// Использование автоматического расчёта предделителя
	unsigned value;
	const uint_fast8_t prei = calcdivider(calcdivround2(CPU_FREQ, baudrate), ATMEGA_UBR_WIDTH, ATMEGA_UBR_TAPS, & value, 1);

	if (prei == 0)
		UCSR1A |= (1U << U2X1);
	else
		UCSR1A &= ~ (1U << U2X1);

	UBRR1H = (value >> 8) & 0xff;	/* Значение получено уже уменьшенное на 1 */
	UBRR1L = value & 0xff;

#elif CPUSTYLE_ATMEGA

	#error WITHUART2HW not supported with CPUSTYLE_ATMEGA

#elif CPUSTYLE_ATXMEGAXXXA4

	// Использование автоматического расчёта предделителя
	unsigned value;
	const uint_fast8_t prei = calcdivider(calcdivround2(CPU_FREQ, baudrate), ATXMEGA_UBR_WIDTH, ATXMEGA_UBR_TAPS, & value, 1);
	if (prei == 0)
		USARTE1.CTRLB |= USART_CLK2X_bm;
	else
		USARTE1.CTRLB &= ~USART_CLK2X_bm;
	// todo: проверить требование к порядку обращения к портам
	USARTE1.BAUDCTRLA = (value & 0xff);	/* Значение получено уже уменьшенное на 1 */
	USARTE1.BAUDCTRLB = (ATXMEGA_UBR_BSEL << 4) | ((value >> 8) & 0x0f);

#elif CPUSTYLE_STM32MP1

	// uart2
	USART2->BRR = calcdivround2(stm32mp1_uart2_4_get_freq(), baudrate);		// младшие 4 бита - это дробная часть.

#elif CPUSTYLE_STM32F

	// uart2 on apb1

	USART2->BRR = calcdivround2(BOARD_USART2_FREQ, baudrate);		// младшие 4 бита - это дробная часть.

#elif CPUSTYPE_TMS320F2833X

	const unsigned long lspclk = CPU_FREQ / 4;
	const unsigned long brr = (lspclk / 8) / baudrate;	// @ CPU_FREQ = 100 MHz, 9600 can not be programmed

	SCIBHBAUD = (brr - 1) >> 8;		// write 8 bits, not 16
	SCIBLBAUD = (brr - 1) >> 0;

#elif CPUSTYLE_R7S721

	// Использование автоматического расчёта предделителя
	unsigned value;
	const uint_fast8_t prei = calcdivider(calcdivround_p1clock(baudrate), R7S721_SCIF_SCBRR_WIDTH, R7S721_SCIF_SCBRR_TAPS, & value, 1);

	SCIF3.SCSMR = (SCIF3.SCSMR & ~ 0x03) |
		scemr_scsmr [prei].scsmr |	// prescaler: 0: /1, 1: /4, 2: /16, 3: /64
		0;
	SCIF3.SCEMR = (SCIF3.SCEMR & ~ (0x80 | 0x01)) |
		0 * 0x80 |						// BGDM
		scemr_scsmr [prei].scemr |	// ABCS = 8/16 clocks per bit
		0;
	SCIF3.SCBRR = value;	/* Bit rate register */

#elif CPUSTYLE_XC7Z || CPUSTYLE_XCZU

	  uint32_t r; // Temporary value variable
	  r = UART1->CR;
	  r &= ~(XUARTPS_CR_TX_EN | XUARTPS_CR_RX_EN); // Clear Tx & Rx Enable
	  r |= XUARTPS_CR_RX_DIS | XUARTPS_CR_TX_DIS; // Tx & Rx Disable
	  UART1->CR = r;
	  const unsigned long sel_clk = xc7z_get_uart_freq();
	  const unsigned long bdiv = 8;
	  // baud_rate = sel_clk / (CD * (BDIV + 1) (ref: UG585 - TRM - Ch. 19 UART)
	  UART1->BAUDDIV = bdiv - 1; // ("BDIV")
	  UART1->BAUDGEN = calcdivround2(sel_clk, baudrate * bdiv); // ("CD")
	  // Baud Rate = 100Mhz / (124 * (6 + 1)) = 115200 bps
	  UART1->CR |= (XUARTPS_CR_TXRST | XUARTPS_CR_RXRST); // TX & RX logic reset

	  r = UART1->CR;
	  r |= XUARTPS_CR_RX_EN | XUARTPS_CR_TX_EN; // Set TX & RX enabled
	  r &= ~(XUARTPS_CR_RX_DIS | XUARTPS_CR_TX_DIS); // Clear TX & RX disabled
	  UART1->CR = r;


#else
	#warning Undefined CPUSTYLE_XXX
#endif

}

#endif /* WITHUART2HW */


#if WITHUART4HW

void
hardware_uart4_set_speed(uint_fast32_t baudrate)
{
#if CPUSTYLE_ATSAM3S || CPUSTYLE_ATSAM4S

	#if HARDWARE_ARM_USEUSART0
		// Использование автоматического расчёта предделителя
		unsigned value;
		const uint_fast8_t prei = calcdivider(calcdivround2(CPU_FREQ, baudrate), ATSAM3S_USART_BRGR_WIDTH, ATSAM3S_USART_BRGR_TAPS, & value, 0);
		USART0->US_BRGR = value;
		if (prei == 0)
		{
			USART0->US_MR |= US_MR_OVER;
		}
		else
		{
			USART0->US_MR &= ~ US_MR_OVER;
		}
	#elif HARDWARE_ARM_USEUSART1
		// Использование автоматического расчёта предделителя
		unsigned value;
		const uint_fast8_t prei = calcdivider(calcdivround2(CPU_FREQ, baudrate), ATSAM3S_USART_BRGR_WIDTH, ATSAM3S_USART_BRGR_TAPS, & value, 0);
		USART1->US_BRGR = value;
		if (prei == 0)
		{
			USART1->US_MR |= US_MR_OVER;
		}
		else
		{
			USART1->US_MR &= ~ US_MR_OVER;
		}

	#elif HARDWARE_ARM_USEUART0
		// Использование автоматического расчёта предделителя
		unsigned value;
		calcdivider(calcdivround2(CPU_FREQ, baudrate), ATSAM3S_UART_BRGR_WIDTH, ATSAM3S_UART_BRGR_TAPS, & value, 0);
		UART0->UART_BRGR = value;
	#elif HARDWARE_ARM_USEUART1
		// Использование автоматического расчёта предделителя
		unsigned value;
		calcdivider(calcdivround2(CPU_FREQ, baudrate), ATSAM3S_UART_BRGR_WIDTH, ATSAM3S_UART_BRGR_TAPS, & value, 0);
		UART1->UART_BRGR = value;
	#else	/* HARDWARE_ARM_USExxx */
		#error Wrong HARDWARE_ARM_USExxx value
	#endif

#elif CPUSTYLE_AT91SAM7S

	// Использование автоматического расчёта предделителя
	unsigned value;
	const uint_fast8_t prei = calcdivider(calcdivround2(CPU_FREQ, baudrate), AT91SAM7_USART_BRGR_WIDTH, AT91SAM7_USART_BRGR_TAPS, & value, 0);

	#if HARDWARE_ARM_USEUSART0
		AT91C_BASE_US0->US_BRGR = value;
		if (prei == 0)
		{
			AT91C_BASE_US0->US_MR |= AT91C_US_OVER;
		}
		else
		{
			AT91C_BASE_US0->US_MR &= ~ AT91C_US_OVER;
		}
	#elif HARDWARE_ARM_USEUSART1
		AT91C_BASE_US1->US_BRGR = value;
		if (prei == 0)
		{
			AT91C_BASE_US1->US_MR |= AT91C_US_OVER;
		}
		else
		{
			AT91C_BASE_US1->US_MR &= ~ AT91C_US_OVER;
		}
	#else	/* HARDWARE_ARM_USExxx */
		#error Wrong HARDWARE_ARM_USExxx value
	#endif

#elif CPUSTYLE_ATMEGA_XXX4

	// Использование автоматического расчёта предделителя
	unsigned value;
	const uint_fast8_t prei = calcdivider(calcdivround2(CPU_FREQ, baudrate), ATMEGA_UBR_WIDTH, ATMEGA_UBR_TAPS, & value, 1);

	if (prei == 0)
		UCSR1A |= (1U << U2X1);
	else
		UCSR1A &= ~ (1U << U2X1);

	UBRR1 = value;	/* Значение получено уже уменьшенное на 1 */


#elif CPUSTYLE_ATMEGA128

	// Использование автоматического расчёта предделителя
	unsigned value;
	const uint_fast8_t prei = calcdivider(calcdivround2(CPU_FREQ, baudrate), ATMEGA_UBR_WIDTH, ATMEGA_UBR_TAPS, & value, 1);

	if (prei == 0)
		UCSR1A |= (1U << U2X1);
	else
		UCSR1A &= ~ (1U << U2X1);

	UBRR1H = (value >> 8) & 0xff;	/* Значение получено уже уменьшенное на 1 */
	UBRR1L = value & 0xff;

#elif CPUSTYLE_ATMEGA

	#error WITHUART2HW not supported with CPUSTYLE_ATMEGA

#elif CPUSTYLE_ATXMEGAXXXA4

	// Использование автоматического расчёта предделителя
	unsigned value;
	const uint_fast8_t prei = calcdivider(calcdivround2(CPU_FREQ, baudrate), ATXMEGA_UBR_WIDTH, ATXMEGA_UBR_TAPS, & value, 1);
	if (prei == 0)
		USARTE1.CTRLB |= USART_CLK2X_bm;
	else
		USARTE1.CTRLB &= ~USART_CLK2X_bm;
	// todo: проверить требование к порядку обращения к портам
	USARTE1.BAUDCTRLA = (value & 0xff);	/* Значение получено уже уменьшенное на 1 */
	USARTE1.BAUDCTRLB = (ATXMEGA_UBR_BSEL << 4) | ((value >> 8) & 0x0f);

#elif CPUSTYLE_STM32MP1

	// uart4
	UART4->BRR = calcdivround2(stm32mp1_uart2_4_get_freq(), baudrate);		// младшие 4 бита - это дробная часть.

#elif CPUSTYLE_STM32F

	// uart4 on apb1

	UART4->BRR = calcdivround2(BOARD_UART4_FREQ, baudrate);		// младшие 4 бита - это дробная часть.

#elif CPUSTYPE_TMS320F2833X

	const unsigned long lspclk = CPU_FREQ / 4;
	const unsigned long brr = (lspclk / 8) / baudrate;	// @ CPU_FREQ = 100 MHz, 9600 can not be programmed

	SCIBHBAUD = (brr - 1) >> 8;		// write 8 bits, not 16
	SCIBLBAUD = (brr - 1) >> 0;

#elif CPUSTYLE_R7S721

	// Использование автоматического расчёта предделителя
	unsigned value;
	const uint_fast8_t prei = calcdivider(calcdivround_p1clock(baudrate), R7S721_SCIF_SCBRR_WIDTH, R7S721_SCIF_SCBRR_TAPS, & value, 1);

	SCIF3.SCSMR = (SCIF3.SCSMR & ~ 0x03) |
		scemr_scsmr [prei].scsmr |	// prescaler: 0: /1, 1: /4, 2: /16, 3: /64
		0;
	SCIF3.SCEMR = (SCIF3.SCEMR & ~ (0x80 | 0x01)) |
		0 * 0x80 |						// BGDM
		scemr_scsmr [prei].scemr |	// ABCS = 8/16 clocks per bit
		0;
	SCIF3.SCBRR = value;	/* Bit rate register */

#else
	#warning Undefined CPUSTYLE_XXX
#endif

}

#endif /* WITHUART4HW */


#if WITHUART5HW

void
hardware_uart5_set_speed(uint_fast32_t baudrate)
{
#if CPUSTYLE_ATSAM3S || CPUSTYLE_ATSAM4S

	#if HARDWARE_ARM_USEUSART0
		// Использование автоматического расчёта предделителя
		unsigned value;
		const uint_fast8_t prei = calcdivider(calcdivround2(CPU_FREQ, baudrate), ATSAM3S_USART_BRGR_WIDTH, ATSAM3S_USART_BRGR_TAPS, & value, 0);
		USART0->US_BRGR = value;
		if (prei == 0)
		{
			USART0->US_MR |= US_MR_OVER;
		}
		else
		{
			USART0->US_MR &= ~ US_MR_OVER;
		}
	#elif HARDWARE_ARM_USEUSART5
		// Использование автоматического расчёта предделителя
		unsigned value;
		const uint_fast8_t prei = calcdivider(calcdivround2(CPU_FREQ, baudrate), ATSAM3S_USART_BRGR_WIDTH, ATSAM3S_USART_BRGR_TAPS, & value, 0);
		USART5->US_BRGR = value;
		if (prei == 0)
		{
			USART5->US_MR |= US_MR_OVER;
		}
		else
		{
			USART5->US_MR &= ~ US_MR_OVER;
		}

	#elif HARDWARE_ARM_USEUART0
		// Использование автоматического расчёта предделителя
		unsigned value;
		calcdivider(calcdivround2(CPU_FREQ, baudrate), ATSAM3S_UART_BRGR_WIDTH, ATSAM3S_UART_BRGR_TAPS, & value, 0);
		UART0->UART_BRGR = value;
	#elif HARDWARE_ARM_USEUART1
		// Использование автоматического расчёта предделителя
		unsigned value;
		calcdivider(calcdivround2(CPU_FREQ, baudrate), ATSAM3S_UART_BRGR_WIDTH, ATSAM3S_UART_BRGR_TAPS, & value, 0);
		UART1->UART_BRGR = value;
	#else	/* HARDWARE_ARM_USExxx */
		#error Wrong HARDWARE_ARM_USExxx value
	#endif

#elif CPUSTYLE_AT91SAM7S

	// Использование автоматического расчёта предделителя
	unsigned value;
	const uint_fast8_t prei = calcdivider(calcdivround2(CPU_FREQ, baudrate), AT91SAM7_USART_BRGR_WIDTH, AT91SAM7_USART_BRGR_TAPS, & value, 0);

	#if HARDWARE_ARM_USEUSART0
		AT91C_BASE_US0->US_BRGR = value;
		if (prei == 0)
		{
			AT91C_BASE_US0->US_MR |= AT91C_US_OVER;
		}
		else
		{
			AT91C_BASE_US0->US_MR &= ~ AT91C_US_OVER;
		}
	#elif HARDWARE_ARM_USEUSART5
		AT91C_BASE_US1->US_BRGR = value;
		if (prei == 0)
		{
			AT91C_BASE_US1->US_MR |= AT91C_US_OVER;
		}
		else
		{
			AT91C_BASE_US1->US_MR &= ~ AT91C_US_OVER;
		}
	#else	/* HARDWARE_ARM_USExxx */
		#error Wrong HARDWARE_ARM_USExxx value
	#endif

#elif CPUSTYLE_ATMEGA_XXX4

	// Использование автоматического расчёта предделителя
	unsigned value;
	const uint_fast8_t prei = calcdivider(calcdivround2(CPU_FREQ, baudrate), ATMEGA_UBR_WIDTH, ATMEGA_UBR_TAPS, & value, 1);

	if (prei == 0)
		UCSR1A |= (1U << U2X1);
	else
		UCSR1A &= ~ (1U << U2X1);

	UBRR1 = value;	/* Значение получено уже уменьшенное на 1 */


#elif CPUSTYLE_ATMEGA128

	// Использование автоматического расчёта предделителя
	unsigned value;
	const uint_fast8_t prei = calcdivider(calcdivround2(CPU_FREQ, baudrate), ATMEGA_UBR_WIDTH, ATMEGA_UBR_TAPS, & value, 1);

	if (prei == 0)
		UCSR1A |= (1U << U2X1);
	else
		UCSR1A &= ~ (1U << U2X1);

	UBRR1H = (value >> 8) & 0xff;	/* Значение получено уже уменьшенное на 1 */
	UBRR1L = value & 0xff;

#elif CPUSTYLE_ATMEGA

	#error WITHUART5HW not supported with CPUSTYLE_ATMEGA

#elif CPUSTYLE_ATXMEGAXXXA4

	// Использование автоматического расчёта предделителя
	unsigned value;
	const uint_fast8_t prei = calcdivider(calcdivround2(CPU_FREQ, baudrate), ATXMEGA_UBR_WIDTH, ATXMEGA_UBR_TAPS, & value, 1);
	if (prei == 0)
		USARTE1.CTRLB |= USART_CLK2X_bm;
	else
		USARTE1.CTRLB &= ~USART_CLK2X_bm;
	// todo: проверить требование к порядку обращения к портам
	USARTE1.BAUDCTRLA = (value & 0xff);	/* Значение получено уже уменьшенное на 1 */
	USARTE1.BAUDCTRLB = (ATXMEGA_UBR_BSEL << 4) | ((value >> 8) & 0x0f);

#elif CPUSTYLE_STM32MP1

	// uart5
	UART5->BRR = calcdivround2(BOARD_UART5_FREQ, baudrate);		// младшие 4 бита - это дробная часть.

#elif CPUSTYLE_STM32F

	// uart5 on apb1

	USART5->BRR = calcdivround2(BOARD_UART5_FREQ, baudrate);		// младшие 4 бита - это дробная часть.

#elif CPUSTYPE_TMS320F2833X

	const unsigned long lspclk = CPU_FREQ / 4;
	const unsigned long brr = (lspclk / 8) / baudrate;	// @ CPU_FREQ = 100 MHz, 9600 can not be programmed

	SCIBHBAUD = (brr - 1) >> 8;		// write 8 bits, not 16
	SCIBLBAUD = (brr - 1) >> 0;

#elif CPUSTYLE_R7S721

	// Использование автоматического расчёта предделителя
	unsigned value;
	const uint_fast8_t prei = calcdivider(calcdivround_p1clock(baudrate), R7S721_SCIF_SCBRR_WIDTH, R7S721_SCIF_SCBRR_TAPS, & value, 1);

	SCIF4.SCSMR = (SCIF4.SCSMR & ~ 0x03) |
		scemr_scsmr [prei].scsmr |	// prescaler: 0: /1, 1: /4, 2: /16, 3: /64
		0;
	SCIF4.SCEMR = (SCIF4.SCEMR & ~ (0x80 | 0x01)) |
		0 * 0x80 |						// BGDM
		scemr_scsmr [prei].scemr |	// ABCS = 8/16 clocks per bit
		0;
	SCIF4.SCBRR = value;	/* Bit rate register */

#else
	#warning Undefined CPUSTYLE_XXX
#endif

}

#endif /* WITHUART5HW */

#if WITHUART7HW

void
hardware_uart7_set_speed(uint_fast32_t baudrate)
{
#if CPUSTYLE_ATSAM3S || CPUSTYLE_ATSAM4S

	#if HARDWARE_ARM_USEUSART0
		// Использование автоматического расчёта предделителя
		unsigned value;
		const uint_fast8_t prei = calcdivider(calcdivround2(CPU_FREQ, baudrate), ATSAM3S_USART_BRGR_WIDTH, ATSAM3S_USART_BRGR_TAPS, & value, 0);
		USART0->US_BRGR = value;
		if (prei == 0)
		{
			USART0->US_MR |= US_MR_OVER;
		}
		else
		{
			USART0->US_MR &= ~ US_MR_OVER;
		}
	#elif HARDWARE_ARM_USEUSART1
		// Использование автоматического расчёта предделителя
		unsigned value;
		const uint_fast8_t prei = calcdivider(calcdivround2(CPU_FREQ, baudrate), ATSAM3S_USART_BRGR_WIDTH, ATSAM3S_USART_BRGR_TAPS, & value, 0);
		USART1->US_BRGR = value;
		if (prei == 0)
		{
			USART1->US_MR |= US_MR_OVER;
		}
		else
		{
			USART1->US_MR &= ~ US_MR_OVER;
		}

	#elif HARDWARE_ARM_USEUART0
		// Использование автоматического расчёта предделителя
		unsigned value;
		calcdivider(calcdivround2(CPU_FREQ, baudrate), ATSAM3S_UART_BRGR_WIDTH, ATSAM3S_UART_BRGR_TAPS, & value, 0);
		UART0->UART_BRGR = value;
	#elif HARDWARE_ARM_USEUART1
		// Использование автоматического расчёта предделителя
		unsigned value;
		calcdivider(calcdivround2(CPU_FREQ, baudrate), ATSAM3S_UART_BRGR_WIDTH, ATSAM3S_UART_BRGR_TAPS, & value, 0);
		UART1->UART_BRGR = value;
	#else	/* HARDWARE_ARM_USExxx */
		#error Wrong HARDWARE_ARM_USExxx value
	#endif

#elif CPUSTYLE_AT91SAM7S

	// Использование автоматического расчёта предделителя
	unsigned value;
	const uint_fast8_t prei = calcdivider(calcdivround2(CPU_FREQ, baudrate), AT91SAM7_USART_BRGR_WIDTH, AT91SAM7_USART_BRGR_TAPS, & value, 0);

	#if HARDWARE_ARM_USEUSART0
		AT91C_BASE_US0->US_BRGR = value;
		if (prei == 0)
		{
			AT91C_BASE_US0->US_MR |= AT91C_US_OVER;
		}
		else
		{
			AT91C_BASE_US0->US_MR &= ~ AT91C_US_OVER;
		}
	#elif HARDWARE_ARM_USEUSART1
		AT91C_BASE_US1->US_BRGR = value;
		if (prei == 0)
		{
			AT91C_BASE_US1->US_MR |= AT91C_US_OVER;
		}
		else
		{
			AT91C_BASE_US1->US_MR &= ~ AT91C_US_OVER;
		}
	#else	/* HARDWARE_ARM_USExxx */
		#error Wrong HARDWARE_ARM_USExxx value
	#endif

#elif CPUSTYLE_ATMEGA_XXX4

	// Использование автоматического расчёта предделителя
	unsigned value;
	const uint_fast8_t prei = calcdivider(calcdivround2(CPU_FREQ, baudrate), ATMEGA_UBR_WIDTH, ATMEGA_UBR_TAPS, & value, 1);

	if (prei == 0)
		UCSR1A |= (1U << U2X1);
	else
		UCSR1A &= ~ (1U << U2X1);

	UBRR1 = value;	/* Значение получено уже уменьшенное на 1 */


#elif CPUSTYLE_ATMEGA128

	// Использование автоматического расчёта предделителя
	unsigned value;
	const uint_fast8_t prei = calcdivider(calcdivround2(CPU_FREQ, baudrate), ATMEGA_UBR_WIDTH, ATMEGA_UBR_TAPS, & value, 1);

	if (prei == 0)
		UCSR1A |= (1U << U2X1);
	else
		UCSR1A &= ~ (1U << U2X1);

	UBRR1H = (value >> 8) & 0xff;	/* Значение получено уже уменьшенное на 1 */
	UBRR1L = value & 0xff;

#elif CPUSTYLE_ATMEGA

	#error WITHUART2HW not supported with CPUSTYLE_ATMEGA

#elif CPUSTYLE_ATXMEGAXXXA4

	// Использование автоматического расчёта предделителя
	unsigned value;
	const uint_fast8_t prei = calcdivider(calcdivround2(CPU_FREQ, baudrate), ATXMEGA_UBR_WIDTH, ATXMEGA_UBR_TAPS, & value, 1);
	if (prei == 0)
		USARTE1.CTRLB |= USART_CLK2X_bm;
	else
		USARTE1.CTRLB &= ~USART_CLK2X_bm;
	// todo: проверить требование к порядку обращения к портам
	USARTE1.BAUDCTRLA = (value & 0xff);	/* Значение получено уже уменьшенное на 1 */
	USARTE1.BAUDCTRLB = (ATXMEGA_UBR_BSEL << 4) | ((value >> 8) & 0x0f);

#elif CPUSTYLE_STM32MP1

	// uart7
	UART7->BRR = calcdivround2(BOARD_UART7_FREQ, baudrate);		// младшие 4 бита - это дробная часть.

#elif CPUSTYLE_STM32F

	// uart7 on apb1

	USART7->BRR = calcdivround2(BOARD_UART7_FREQ, baudrate);		// младшие 4 бита - это дробная часть.

#elif CPUSTYPE_TMS320F2833X

	const unsigned long lspclk = CPU_FREQ / 4;
	const unsigned long brr = (lspclk / 8) / baudrate;	// @ CPU_FREQ = 100 MHz, 9600 can not be programmed

	SCIBHBAUD = (brr - 1) >> 8;		// write 8 bits, not 16
	SCIBLBAUD = (brr - 1) >> 0;

#elif CPUSTYLE_R7S721

	// Использование автоматического расчёта предделителя
	unsigned value;
	const uint_fast8_t prei = calcdivider(calcdivround_p1clock(baudrate), R7S721_SCIF_SCBRR_WIDTH, R7S721_SCIF_SCBRR_TAPS, & value, 1);

	SCIF6.SCSMR = (SCIF6.SCSMR & ~ 0x03) |
		scemr_scsmr [prei].scsmr |	// prescaler: 0: /1, 1: /4, 2: /16, 3: /64
		0;
	SCIF6.SCEMR = (SCIF6.SCEMR & ~ (0x80 | 0x01)) |
		0 * 0x80 |						// BGDM
		scemr_scsmr [prei].scemr |	// ABCS = 8/16 clocks per bit
		0;
	SCIF6.SCBRR = value;	/* Bit rate register */

#else
	#warning Undefined CPUSTYLE_XXX
#endif

}

#endif /* WITHUART7HW */

