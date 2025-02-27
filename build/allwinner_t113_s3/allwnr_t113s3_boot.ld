/* $Id$ */
/* Allwinner t113-s3 layout */
/* Linker script to configure memory regions. */

MEMORY
{
  RAM (rwx)	: ORIGIN = 0x00020100, LENGTH = 0x000FF00		/*  */
  RAM2 (rwx): ORIGIN = 0x00030000, LENGTH = 16k		/*  */
  TTB (rwx) : ORIGIN = 0x00034000, LENGTH = 16k		/* last 16 kB SYSRAM for TTB */
  DDR3 (rwx) : ORIGIN = 0x40000000, LENGTH = 128M	/*  */
}

/* Library configurations */
/* GROUP(libgcc.a libc.a libm.a libnosys.a) */

/* Linker script to place sections and symbol values. Should be used together
 * with other linker script that defines memory regions FLASH and RAM.
 * It references following symbols, which must be defined in code:
 *   Reset_Handler : Entry of reset handler
 *
 * It defines following symbols, which code can use without definition:
 *   __exidx_start
 *   __exidx_end
 *   __etext
 *   __data_start__
 *   __preinit_array_start
 *   __preinit_array_end
 *   __init_array_start
 *   __init_array_end
 *   __fini_array_start
 *   __fini_array_end
 *   __data_end__
 *   __bss_start__
 *   __bss_end__
 *   __end__
 *   end
 *   __HeapLimit
 *   __StackLimit
 *   __StackTop
 *   __stack
 */
	 ENTRY(Reset_Handler)

SECTIONS
{
	.text :
	{
		. = ALIGN(32);
		KEEP(*(.vectors))
		__Vectors_End = .;
		__Vectors_Size = __Vectors_End - __Vectors;
		
		. = ALIGN(32);
		KEEP(*(.startup0))
		KEEP(*(.init))
		KEEP(*(.initfunc*))
		KEEP(*(.initdata*))
		KEEP(*(.fini))

		KEEP(*(.text*))
		KEEP(*(.itcm*))
		. = ALIGN(4);
	} > RAM

	.ARM.extab :
	{
		*(.ARM.extab* .gnu.linkonce.armextab.*)
	} > RAM

	__exidx_start = .;
	.ARM.exidx :
	{
		*(.ARM.exidx* .gnu.linkonce.armexidx.*)
	} > RAM
	__exidx_end = .;

	/* To copy multiple ROM to RAM sections,
	 * uncomment .copy.table section and,
	 * define __STARTUP_COPY_MULTIPLE in startup_ARMCMx.S */
	.copy.table :
	{
		. = ALIGN(4);
		__copy_table_start__ = .;
	/*
		LONG (__etext)
		LONG (__data_start__)
		LONG (__data_end__ - __data_start__)
		LONG (__etext2)
		LONG (__data2_start__)
		LONG (__data2_end__ - __data2_start__)
	*/
		__copy_table_end__ = .;
	} > RAM

	/* To clear multiple BSS sections,
	 * uncomment .zero.table section and,
	 * define __STARTUP_CLEAR_BSS_MULTIPLE in startup_ARMCMx.S */
	.zero.table :
	{
		. = ALIGN(4);
		__zero_table_start__ = .;
		LONG (__bss_start__)
		LONG (__bss_end__ - __bss_start__)
	/*
		LONG (__bss2_start__)
		LONG (__bss2_end__ - __bss2_start__)
	*/
		__zero_table_end__ = .;
	} > RAM

	__etext = .;

	.data : /*AT (__etext)*/
	{
		__data_start__ = .;
		. = ALIGN(32);
		KEEP(*(.dtcm*))

		. = ALIGN(32);
		/* .ctors */
		*crtbegin.o(.ctors)
		*crtbegin?.o(.ctors)
		*(EXCLUDE_FILE(*crtend?.o *crtend.o) .ctors)
		*(SORT(.ctors.*))
		*(.ctors)

		/* .dtors */
 		*crtbegin.o(.dtors)
 		*crtbegin?.o(.dtors)
 		*(EXCLUDE_FILE(*crtend?.o *crtend.o) .dtors)
 		*(SORT(.dtors.*))
 		*(.dtors)

		*(.rodata*)

		KEEP(*(.eh_frame*))

		KEEP(*(.data*))
	   . = ALIGN(4);	/* one line added by ua1arn */

		. = ALIGN(4);
		/* preinit data */
		PROVIDE_HIDDEN (__preinit_array_start = .);
		KEEP(*(.preinit_array))
		PROVIDE_HIDDEN (__preinit_array_end = .);

		. = ALIGN(4);
		/* init data */
		PROVIDE_HIDDEN (__init_array_start = .);
		KEEP(*(SORT(.init_array.*)))
		KEEP(*(.init_array))
		PROVIDE_HIDDEN (__init_array_end = .);


		. = ALIGN(4);
		/* finit data */
		PROVIDE_HIDDEN (__fini_array_start = .);
		KEEP(*(SORT(.fini_array.*)))
		KEEP(*(.fini_array))
		PROVIDE_HIDDEN (__fini_array_end = .);

		KEEP(*(.jcr*))
		. = ALIGN(32);
		/* All data end */
		__data_end__ = .;

	} > RAM

	.bss (NOLOAD):
	{
		__bss_start__ = .;
		. = ALIGN(32);
		KEEP(*(.bss*))
		KEEP(*(COMMON))
		. = ALIGN(32);
		__bss_end__ = .;
	} > RAM2

	.noinit (NOLOAD):
	{
		. = ALIGN(32);
		KEEP(*(.noinit*))
		. = ALIGN(32);
	} > RAM2

	.vram (NOLOAD):
	{
		. = ALIGN(64);
		KEEP(*(.framebuff*))
		. = ALIGN(64);
	} > DDR3

	.heap (NOLOAD):
	{
		__HeapBase = .;
		__end__ = .;
		end = __end__;
		KEEP(*(.heap*))
		__HeapLimit = .;
	} > RAM2

	/* .stack_dummy section doesn't contains any symbols. It is only
	 * used for linker to calculate size of stack sections, and assign
	 * values to stack symbols later */
	.stack_dummy (NOLOAD):
	{
		KEEP(*(.stack*))
	} > RAM

	/* Set stack top to end of RAM, and stack limit move down by
	 * size of stack_dummy section */
	__StackTop = ORIGIN(RAM) + LENGTH(RAM);
	__StackLimit = __StackTop - SIZEOF(.stack_dummy);
	PROVIDE(__stack = __StackTop);

	PROVIDE(__TTB_BASE = ORIGIN(TTB));
	/* PROVIDE(__ttb_end__ = ORIGIN(TTB) + LENGTH(TTB)); */

	/* Check if data + heap + stack exceeds RAM limit */
	/*ASSERT(__StackLimit >= __HeapLimit, "region RAM overflowed with stack")*/
}
