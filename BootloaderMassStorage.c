/*
             LUFA Library
     Copyright (C) Dean Camera, 2021.

  dean [at] fourwalledcubicle [dot] com
           www.lufa-lib.org
*/

/*
  Copyright 2021  Dean Camera (dean [at] fourwalledcubicle [dot] com)

  Permission to use, copy, modify, distribute, and sell this
  software and its documentation for any purpose is hereby granted
  without fee, provided that the above copyright notice appear in
  all copies and that both that the copyright notice and this
  permission notice and warranty disclaimer appear in supporting
  documentation, and that the name of the author not be used in
  advertising or publicity pertaining to distribution of the
  software without specific, written prior permission.

  The author disclaims all warranties with regard to this
  software, including all implied warranties of merchantability
  and fitness.  In no event shall the author be liable for any
  special, indirect or consequential damages or any damages
  whatsoever resulting from loss of use, data or profits, whether
  in an action of contract, negligence or other tortious action,
  arising out of or in connection with the use or performance of
  this software.
*/

/** \file
 *
 *  Main source file for the Mass Storage class bootloader. This file contains the complete bootloader logic.
 */

#define  INCLUDE_FROM_BOOTLOADER_MASSSTORAGE_C
#include "BootloaderMassStorage.h"
#include "dwenguino_lcd.h"
#include <util/delay.h>
#include <avr/interrupt.h>

/** LUFA Mass Storage Class driver interface configuration and state information. This structure is
 *  passed to all Mass Storage Class driver functions, so that multiple instances of the same class
 *  within a device can be differentiated from one another.
 */
USB_ClassInfo_MS_Device_t Disk_MS_Interface =
	{
		.Config =
			{
				.InterfaceNumber           = INTERFACE_ID_MassStorage,
				.DataINEndpoint            =
					{
						.Address           = MASS_STORAGE_IN_EPADDR,
						.Size              = MASS_STORAGE_IO_EPSIZE,
						.Banks             = 1,
					},
				.DataOUTEndpoint           =
					{
						.Address           = MASS_STORAGE_OUT_EPADDR,
						.Size              = MASS_STORAGE_IO_EPSIZE,
						.Banks             = 1,
					},
				.TotalLUNs                 = 1,
			},
	};

/** Flag to indicate if the bootloader should be running, or should exit and allow the application code to run
 *  via a soft reset. When cleared, the bootloader will abort, the USB interface will shut down and the application
 *  started via a forced watchdog reset.
 */
bool RunBootloader = true;

/** Magic lock for forced application start. If the HWBE fuse is programmed and BOOTRST is unprogrammed, the bootloader
 *  will start if the /HWB line of the AVR is held low and the system is reset. However, if the /HWB line is still held
 *  low when the application attempts to start via a watchdog reset, the bootloader will re-start. If set to the value
 *  \ref MAGIC_BOOT_KEY the special init function \ref Application_Jump_Check() will force the application to start.
 */
uint16_t MagicBootKey ATTR_NO_INIT;

/** Indicates if the bootloader is allowed to exit immediately if \ref RunBootloader is \c false. During shutdown all
 *  pending commands must be processed before jumping to the user-application, thus this tracks the main program loop
 *  iterations since a SCSI command from the host was received.
 */
static uint8_t TicksSinceLastCommand = 0;


/** Special startup routine to check if the bootloader was started via a watchdog reset, and if the magic application
 *  start key has been loaded into \ref MagicBootKey. If the bootloader started via the watchdog and the key is valid,
 *  this will force the user application to start via a software jump.
 */
void Application_Jump_Check(void)
{
	bool JumpToApplication = false;

	// Setup boot selection on the Dwenguino board

	/* Enable pull-up on the south button so we can use it to select the mode */
	DDRE &= ~(1<<PE5); // Set south button as input
	PORTE |= (1 << PE5);
	Delay_MS(10);

	/* Start application by default, only if south pin pressed on reset or no program is loaded you enter the bootloader */
	JumpToApplication = (PINE & (1 << PE5));

	DDRA = 0xff;
	PORTA = 0x00;
	/* Disable pull-up after the check has completed */
	PORTE &= ~(1 << PE5);
	

	/* Don't run the user application if the reset vector is blank (no app loaded) */
	bool ApplicationValid = (pgm_read_word_near(0) != 0xFFFF);
	
	/* If a request has been made to jump to the user application, honor it */
	if (JumpToApplication && ApplicationValid)
	{
		PrepareForApplication();

		/* Turn off the watchdog */
		MCUSR &= ~(1 << WDRF);
		wdt_disable();

		/* Clear the boot key and jump to the user application */
		MagicBootKey = 0;
		
		// cppcheck-suppress constStatement
		((void (*)(void))0x0000)();
	}
}


/** Main program entry point. This routine configures the hardware required by the application, then
 *  enters a loop to run the application tasks in sequence.
 */
int main(void)
{

	initLCD();
	backlightOn();
	printStringToLCD("DWENGUINO", 0, 3);
	printStringToLCD("Zet bestand over", 1, 0);
	
	SetupHardware();

	GlobalInterruptEnable();
	

	while (RunBootloader || TicksSinceLastCommand++ < 0xFF)
	{
		MS_Device_USBTask(&Disk_MS_Interface);
		USB_USBTask();
	}
	/* Wait a short time to end all USB transactions and then disconnect */
	clearLCD();
	printStringToLCD("Bezig....", 0, 3);
	_delay_ms(1000);

	PrepareForApplication();
	
	/* Disconnect from the host - USB interface will be reset later along with the AVR */
	USB_Detach();

	/* Unlock the forced application start mode of the bootloader if it is restarted */
	MagicBootKey = MAGIC_BOOT_KEY;

	/* Enable the watchdog and force a timeout to reset the AVR */
	wdt_enable(WDTO_250MS);

	for (;;);
}
/**
 * Resets board hardware used by bootloader
 */
static void PrepareForApplication(void){
	PORTA = 0;
	clearLCD();
	backlightOff();
}

/** Configures the board hardware and chip peripherals for the demo's functionality. */
static void SetupHardware(void)
{
	/* Disable watchdog if enabled by bootloader/fuses */
	MCUSR &= ~(1 << WDRF);
	wdt_disable();

	/* Disable clock division */
	clock_prescale_set(clock_div_1);

	/* Relocate the interrupt vector table to the bootloader section */
	unsigned char temp;
    /* Get MCUCR */
    temp = MCUCR;
    /* Enable change of Interrupt Vectors */
    MCUCR = temp | (1<<IVCE);
    /* Move interrupts to Boot Flash section */
    MCUCR = temp | (1<<IVSEL);

	/* Hardware Initialization */
	USB_Init();   	
}

/** Event handler for the USB_Connect event. This indicates that the device is enumerating via the status LEDs. */
void EVENT_USB_Device_Connect(void)
{
	/* Indicate USB enumerating */
}

/** Event handler for the USB_Disconnect event. This indicates that the device is no longer connected to a host via
 *  the status LEDs and stops the Mass Storage management task.
 */
void EVENT_USB_Device_Disconnect(void)
{
	// TODO: possibly use this function to go into application mode.
}

/** Event handler for the library USB Configuration Changed event. */
void EVENT_USB_Device_ConfigurationChanged(void)
{
	bool ConfigSuccess = true;

	/* Setup Mass Storage Data Endpoints */
	ConfigSuccess &= MS_Device_ConfigureEndpoints(&Disk_MS_Interface);
}

/** Event handler for the library USB Control Request reception event. */
void EVENT_USB_Device_ControlRequest(void)
{
	MS_Device_ProcessControlRequest(&Disk_MS_Interface);
}

/** Mass Storage class driver callback function the reception of SCSI commands from the host, which must be processed.
 *
 *  \param[in] MSInterfaceInfo  Pointer to the Mass Storage class interface configuration structure being referenced
 */
bool CALLBACK_MS_Device_SCSICommandReceived(USB_ClassInfo_MS_Device_t* const MSInterfaceInfo)
{
	bool CommandSuccess;

	CommandSuccess = SCSI_DecodeSCSICommand(MSInterfaceInfo);

	/* Signal that a command was processed, must not exit bootloader yet */
	TicksSinceLastCommand = 0;

	return CommandSuccess;
}
