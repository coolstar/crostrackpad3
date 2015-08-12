/*++

Copyright (c) Microsoft Corporation.  All rights reserved.

Module Name: 

    driver.cpp

Abstract:

    This module contains the WDF driver initialization 
    functions for the peripheral driver.

Environment:

    kernel-mode only

Revision History:

--*/

#include "internal.h"
#include "driver.h"
#include "device.h"
#include "ntstrsafe.h"
#include "hiddevice.h"
#include "input.h"

static ULONG VMultiDebugLevel = 100;
static ULONG VMultiDebugCatagories = DBG_INIT || DBG_PNP || DBG_IOCTL;

void TrackpadRawInput(PDEVICE_CONTEXT pDevice, struct cyapa_softc *sc, struct cyapa_regs *regs, int tickinc);
void CyapaTimerFunc(_In_ WDFTIMER hTimer);

//#include "driver.tmh"

NTSTATUS
#pragma prefast(suppress:__WARNING_DRIVER_FUNCTION_TYPE, "thanks, i know this already")
DriverEntry(
    _In_ PDRIVER_OBJECT  DriverObject,
    _In_ PUNICODE_STRING RegistryPath
    )
{
    /*WDF_DRIVER_CONFIG_INIT(&driverConfig, OnDeviceAdd);*/
	NTSTATUS               status = STATUS_SUCCESS;
	WDF_DRIVER_CONFIG      driverConfig;
	WDF_OBJECT_ATTRIBUTES  driverAttributes;
	WDFDRIVER fxDriver;

	WPP_INIT_TRACING(DriverObject, RegistryPath);

	FuncEntry(TRACE_FLAG_WDFLOADING);

	WDF_DRIVER_CONFIG_INIT(&driverConfig, OnDeviceAdd);
	driverConfig.DriverPoolTag = SPBT_POOL_TAG;

	WDF_OBJECT_ATTRIBUTES_INIT(&driverAttributes);
	driverAttributes.EvtCleanupCallback = OnDriverCleanup;

	//
	// Create a framework driver object to represent our driver.
	//

	status = WdfDriverCreate(DriverObject,
		RegistryPath,
		&driverAttributes,
		&driverConfig,
		&fxDriver
		);

	if (!NT_SUCCESS(status))
	{
		Trace(
			TRACE_LEVEL_ERROR,
			TRACE_FLAG_WDFLOADING,
			"Error creating WDF driver object - %!STATUS!",
			status);
		goto exit;
	}

	Trace(
		TRACE_LEVEL_VERBOSE,
		TRACE_FLAG_WDFLOADING,
		"Created WDF driver object");

exit:

	FuncExit(TRACE_FLAG_WDFLOADING);

	return status;
}

VOID
OnDriverCleanup(
    _In_ WDFOBJECT Object
    )
{
    FuncEntry(TRACE_FLAG_WDFLOADING);

    UNREFERENCED_PARAMETER(Object);

    WPP_CLEANUP(nullptr);

    FuncExit(TRACE_FLAG_WDFLOADING);
}

NTSTATUS
OnDeviceAdd(
    _In_    WDFDRIVER       FxDriver,
    _Inout_ PWDFDEVICE_INIT FxDeviceInit
    )
/*++
 
  Routine Description:

    This routine creates the device object for an SPB 
    controller and the device's child objects.

  Arguments:

    FxDriver - the WDF driver object handle
    FxDeviceInit - information about the PDO that we are loading on

  Return Value:

    Status

--*/
{
    FuncEntry(TRACE_FLAG_WDFLOADING);

    PDEVICE_CONTEXT pDevice;
	WDFDEVICE fxDevice;
	WDF_INTERRUPT_CONFIG interruptConfig;
    NTSTATUS status;
    
    UNREFERENCED_PARAMETER(FxDriver);

	//
	// Tell framework this is a filter driver. Filter drivers by default are  
	// not power policy owners. This works well for this driver because
	// HIDclass driver is the power policy owner for HID minidrivers.
	//
	WdfFdoInitSetFilter(FxDeviceInit);

    //
    // Setup PNP/Power callbacks.
    //

    {
        WDF_PNPPOWER_EVENT_CALLBACKS pnpCallbacks;
        WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnpCallbacks);

        pnpCallbacks.EvtDevicePrepareHardware = OnPrepareHardware;
        pnpCallbacks.EvtDeviceReleaseHardware = OnReleaseHardware;
        pnpCallbacks.EvtDeviceD0Entry = OnD0Entry;
        pnpCallbacks.EvtDeviceD0Exit = OnD0Exit;

        WdfDeviceInitSetPnpPowerEventCallbacks(FxDeviceInit, &pnpCallbacks);
    }
	
    //
    // Set request attributes.
    //

    {
        WDF_OBJECT_ATTRIBUTES attributes;
        WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(
            &attributes,
            REQUEST_CONTEXT);

        WdfDeviceInitSetRequestAttributes(FxDeviceInit, &attributes);
    }

    //
    // Create the device.
    //

    {
        WDF_OBJECT_ATTRIBUTES deviceAttributes;
        WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&deviceAttributes, DEVICE_CONTEXT);

        status = WdfDeviceCreate(
            &FxDeviceInit, 
            &deviceAttributes,
            &fxDevice);

        if (!NT_SUCCESS(status))
        {
			VMultiPrint(
                TRACE_LEVEL_ERROR, 
                TRACE_FLAG_WDFLOADING,
                "Error creating WDFDEVICE - %!STATUS!", 
                status);

            goto exit;
        }

        pDevice = GetDeviceContext(fxDevice);
        NT_ASSERT(pDevice != nullptr);

        pDevice->FxDevice = fxDevice;
    }

    //
    // Ensure device is disable-able
    //
    
    {
        WDF_DEVICE_STATE deviceState;
        WDF_DEVICE_STATE_INIT(&deviceState);
        
        deviceState.NotDisableable = WdfFalse;
        WdfDeviceSetDeviceState(pDevice->FxDevice, &deviceState);
    }

    //
    // Create queues to handle IO
    //

    {
        WDF_IO_QUEUE_CONFIG queueConfig;
        WDFQUEUE queue;

        //
        // Top-level queue
        //

        WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(
            &queueConfig, 
            WdfIoQueueDispatchParallel);

        queueConfig.EvtIoDefault = OnTopLevelIoDefault;
        queueConfig.PowerManaged = WdfFalse;

        status = WdfIoQueueCreate(
            pDevice->FxDevice,
            &queueConfig,
            WDF_NO_OBJECT_ATTRIBUTES,
            &queue
            );

        if (!NT_SUCCESS(status))
        {
			VMultiPrint(
                TRACE_LEVEL_ERROR, 
                TRACE_FLAG_WDFLOADING,
                "Error creating top-level IO queue - %!STATUS!", 
                status);

            goto exit;
        }

        //
        // Sequential SPB queue
        //

        WDF_IO_QUEUE_CONFIG_INIT(
            &queueConfig, 
			WdfIoQueueDispatchSequential);

		queueConfig.EvtIoInternalDeviceControl = OnIoDeviceControl;
        queueConfig.PowerManaged = WdfFalse;

        status = WdfIoQueueCreate(
            fxDevice,
            &queueConfig,
            WDF_NO_OBJECT_ATTRIBUTES,
			&pDevice->SpbQueue
            );

        if (!NT_SUCCESS(status))
        {
			VMultiPrint(DEBUG_LEVEL_ERROR, DBG_PNP,
				"WdfIoQueueCreate failed 0x%x\n", status);

            goto exit;
        }
    }

	WDF_IO_QUEUE_CONFIG           queueConfig;
	WDFQUEUE                      queue;
	PVMULTI_CONTEXT               devContext;

	devContext = VMultiGetDeviceContext(pDevice->FxDevice);

	WDF_IO_QUEUE_CONFIG_INIT(&queueConfig, WdfIoQueueDispatchManual);

	queueConfig.PowerManaged = WdfFalse;

	status = WdfIoQueueCreate(pDevice->FxDevice,
		&queueConfig,
		WDF_NO_OBJECT_ATTRIBUTES,
		&devContext->ReportQueue
		);

	if (!NT_SUCCESS(status))
	{
		VMultiPrint(DEBUG_LEVEL_ERROR, DBG_PNP, "Queue 2!\n");
		VMultiPrint(DEBUG_LEVEL_ERROR, DBG_PNP,
			"WdfIoQueueCreate failed 0x%x\n", status);

		return status;
	}

	//
	// Create an interrupt object for hardware notifications
	//
	WDF_INTERRUPT_CONFIG_INIT(
		&interruptConfig,
		OnInterruptIsr,
		NULL);
	interruptConfig.PassiveHandling = TRUE;

	status = WdfInterruptCreate(
		fxDevice,
		&interruptConfig,
		WDF_NO_OBJECT_ATTRIBUTES,
		&devContext->Interrupt);

	if (!NT_SUCCESS(status))
	{
		VMultiPrint(DEBUG_LEVEL_ERROR, DBG_PNP,
			"Error creating WDF interrupt object - %!STATUS!",
			status);

		goto exit;
	}

	WDF_TIMER_CONFIG              timerConfig;
	WDFTIMER                      hTimer;
	WDF_OBJECT_ATTRIBUTES         attributes;

	WDF_TIMER_CONFIG_INIT_PERIODIC(&timerConfig, CyapaTimerFunc, 10);

	WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
	attributes.ParentObject = fxDevice;
	status = WdfTimerCreate(&timerConfig, &attributes, &hTimer);
	pDevice->Timer = hTimer;
	if (!NT_SUCCESS(status))
	{
		VMultiPrint(DEBUG_LEVEL_ERROR, DBG_PNP, "(%!FUNC!) WdfTimerCreate failed status:%!STATUS!\n", status);
		return status;
	}

	VMultiPrint(DEBUG_LEVEL_ERROR, DBG_PNP,
		"Success! 0x%x\n", status);

	devContext->DeviceMode = DEVICE_MODE_MOUSE;

exit:

    FuncExit(TRACE_FLAG_WDFLOADING);

    return status;
}


BOOLEAN OnInterruptIsr(
	WDFINTERRUPT Interrupt,
	ULONG MessageID){
	WDFDEVICE Device = WdfInterruptGetDevice(Interrupt);
	PDEVICE_CONTEXT pDevice = GetDeviceContext(Device);

	if (!pDevice->ConnectInterrupt)
		return true;

	struct cyapa_regs regs;
	SpbReadDataSynchronously(&pDevice->I2CContext, 0, &regs, sizeof(regs));
	pDevice->lastregs = regs;
	pDevice->RegsSet = true;
	return true;
}

void CyapaTimerFunc(_In_ WDFTIMER hTimer){
	WDFDEVICE Device = (WDFDEVICE)WdfTimerGetParentObject(hTimer);
	PDEVICE_CONTEXT pDevice = GetDeviceContext(Device);

	if (!pDevice->ConnectInterrupt)
		return;
	if (!pDevice->RegsSet)
		return;

	struct cyapa_regs regs = pDevice->lastregs;

	cyapa_softc sc = pDevice->sc;
	TrackpadRawInput(pDevice, &sc, &regs, 1);
	pDevice->sc = sc;
	return;
}

int distancesq(int delta_x, int delta_y){
	return (delta_x * delta_x) + (delta_y*delta_y);
}

void vmulti_update_relative_mouse(PDEVICE_CONTEXT pDevice, BYTE button,
	BYTE x, BYTE y, BYTE wheelPosition, BYTE wheelHPosition){
#ifdef DEBUG
	VMultiPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL, "Move Mouse. Buttons: %d X: %d Y: %d!\n", button, x, y);
#endif

	_VMULTI_RELATIVE_MOUSE_REPORT report;
	report.ReportID = REPORTID_RELATIVE_MOUSE;
	report.Button = button;
	report.XValue = x;
	report.YValue = y;
	report.WheelPosition = wheelPosition;
	report.HWheelPosition = wheelHPosition;
	size_t bytesWritten;
	VMultiProcessVendorReport(pDevice, &report, sizeof(report), &bytesWritten);
#ifdef DEBUG
	VMultiPrint(DEBUG_LEVEL_INFO, DBG_IOCTL, "Bytes Written: %d\n", bytesWritten);
#endif
}

void vmulti_update_keyboard(PDEVICE_CONTEXT pDevice, BYTE shiftKeys, BYTE keyCodes[KBD_KEY_CODES]){
#ifdef DEBUG
	VMultiPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL, "Update keyboard. ShiftKeys: %d Codes: %d %d %d %d %d %d!\n", shiftKeys, keyCodes[0], keyCodes[1], keyCodes[2], keyCodes[3], keyCodes[4], keyCodes[5]);
#endif

	_VMULTI_KEYBOARD_REPORT report;
	report.ReportID = REPORTID_KEYBOARD;
	report.ShiftKeyFlags = shiftKeys;
	for (int i = 0; i < KBD_KEY_CODES; i++){
		report.KeyCodes[i] = keyCodes[i];
	}

	size_t bytesWritten;
	VMultiProcessVendorReport(pDevice, &report, sizeof(report), &bytesWritten);
#ifdef DEBUG
	VMultiPrint(DEBUG_LEVEL_INFO, DBG_IOCTL, "Bytes Written: %d\n", bytesWritten);
#endif
}

void MySendInput(PDEVICE_CONTEXT pDevice, int count, INPUT* pinput, cyapa_softc *softc, size_t size){
	INPUT input = *pinput;
	BYTE button = 0, x = 0, y = 0, wheelPosition = 0, wheelHPosition = 0;
	if (softc->mousedown){
		if (softc->mousebutton == 0)
			button = MOUSE_BUTTON_1;
		else if (softc->mousebutton == 1)
			button = MOUSE_BUTTON_2;
		else if (softc->mousebutton == 2)
			button = MOUSE_BUTTON_3;
	}

	if (input.mi.dwFlags == MOUSEEVENTF_MOVE){
		x = input.mi.dx;
		y = input.mi.dy;
		//wheelPosition = pDevice->wheelPosition;
	}
	else if (input.mi.dwFlags == MOUSEEVENTF_WHEEL){
		wheelPosition = (input.mi.mouseData / 7);
		//VMultiPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL, "Wheel not supported!\n");
	}
	else if (input.mi.dwFlags == MOUSEEVENTF_HWHEEL){
		wheelHPosition = (input.mi.mouseData / 7);
		//VMultiPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL, "Horizontal Wheel not supported!\n");
	}
	vmulti_update_relative_mouse(pDevice, button, x, y, wheelPosition, wheelHPosition);
}

void TrackpadRawInput(PDEVICE_CONTEXT pDevice, struct cyapa_softc *sc, struct cyapa_regs *regs, int tickinc){
	int nfingers;
	int afingers;	/* actual fingers after culling */
	int i;

	nfingers = CYAPA_FNGR_NUMFINGERS(regs->fngr);
	afingers = nfingers;

	//	system("cls");
#ifdef DEBUG
	VMultiPrint(DEBUG_LEVEL_INFO,DBG_IOCTL,"stat %02x\tTracking ID: %d\tbuttons %c%c%c\tnfngrs=%d\n",
		regs->stat,
		regs->touch->id,
		((regs->fngr & CYAPA_FNGR_LEFT) ? 'L' : '-'),
		((regs->fngr & CYAPA_FNGR_MIDDLE) ? 'M' : '-'),
		((regs->fngr & CYAPA_FNGR_RIGHT) ? 'R' : '-'),
		nfingers
		);
#endif

	for (i = 0; i < nfingers; ++i) {
#ifdef DEBUG
		VMultiPrint(DEBUG_LEVEL_INFO,DBG_IOCTL," [x=%04d y=%04d p=%d]\n",
			CYAPA_TOUCH_X(regs, i),
			CYAPA_TOUCH_Y(regs, i),
			CYAPA_TOUCH_P(regs, i));
		if (CYAPA_TOUCH_Y(regs, i) > 400){
			if (CYAPA_TOUCH_X(regs, i) < 400){
				VMultiPrint(DEBUG_LEVEL_INFO, DBG_IOCTL, "Simulate left hardware button! %d\n", CYAPA_TOUCH_Y(regs, i));
			}
			else {
				VMultiPrint(DEBUG_LEVEL_INFO, DBG_IOCTL, "Simulate right hardware button! %d\n", CYAPA_TOUCH_Y(regs, i));
			}
		}
#endif
		if (CYAPA_TOUCH_P(regs, i) < cyapa_minpressure)
			--afingers;
	}

	if (regs->touch->id != sc->lastid){
		sc->x = 0;
		sc->y = 0;
		sc->lastid = regs->touch->id;
	}

	int x = sc->x;
	int y = sc->y;

	bool overrideDeltas = false;

	if (afingers > 0){
#ifdef DEBUG
		VMultiPrint(DEBUG_LEVEL_INFO,DBG_IOCTL,"Tick inc.\n");
#endif
		sc->tick += tickinc;
		x = CYAPA_TOUCH_X(regs, 0);
		y = CYAPA_TOUCH_Y(regs, 0);
		if (afingers > 1){
			int x1 = CYAPA_TOUCH_X(regs, 0);
			int y1 = CYAPA_TOUCH_Y(regs, 0);
			int x2 = CYAPA_TOUCH_X(regs, 1);
			int y2 = CYAPA_TOUCH_Y(regs, 1);

			int d1 = distancesq(x1 - sc->x, y1 - sc->y);
			int d2 = distancesq(x1 - sc->x, y1 - sc->y);
			if (d2 < d1 || (y > 400 && y2 < 400)){
				x = x2;
				y = y2;
			}
#ifdef DEBUG
			VMultiPrint(DEBUG_LEVEL_INFO,DBG_IOCTL,"%d %d\t%d %d\t%d %d\n", x, y, x1, y1, x2, y2);
#endif
		}
		else {
			if (sc->mousedown && y > 400){
				overrideDeltas = true;
				sc->x = 0;
				sc->y = 0;
			}
		}
		if ((overrideDeltas != true) && (sc->x == 0 && sc->y == 0)){
			sc->x = x;
			sc->y = y;
		}
	}
	else {
		if (sc->tick < 10 && sc->tick != 0){
			INPUT input;
			if (sc->lastnfingers == 1){
				sc->mousebutton = 0;
			}
			else if (sc->lastnfingers == 2){
				sc->mousebutton = 1;
			}
			else if (sc->lastnfingers == 3){
				sc->mousebutton = 2;
			}
			else if (sc->lastnfingers == 4){
				sc->mousebutton = 3;
			}
			input.mi.dx = 0;
			input.mi.dy = 0;
			input.mi.mouseData = 0;
			sc->mousedown = true;
			MySendInput(pDevice, 1, &input, sc, sizeof(INPUT));
			sc->tickssincelastclick = 0;
#ifdef DEBUG
			VMultiPrint(DEBUG_LEVEL_INFO,DBG_IOCTL,"Tap to Click!\n");
#endif
		}
		sc->tick = 0;
		sc->hasmoved = false;
		sc->mousedownfromtap = false;
		sc->tickssincelastclick+=tickinc;
#ifdef DEBUG
		VMultiPrint(DEBUG_LEVEL_INFO,DBG_IOCTL,"Move Reset!\n");
#endif
	}

	int delta_x = x - sc->x, delta_y = y - sc->y;
	if (abs(delta_x) + abs(delta_y) > 10 && !sc->hasmoved){
		sc->hasmoved = true;
		if (sc->tickssincelastclick < 10 && sc->tickssincelastclick >= 0){
			INPUT input;
			input.mi.dx = 0;
			input.mi.dy = 0;
			input.mi.mouseData = 0;
			MySendInput(pDevice, 1, &input, sc, sizeof(INPUT));
			sc->mousebutton = 0;
			sc->mousedown = true;
			sc->mousedownfromtap = true;
			sc->tickssincelastclick = 0;
#ifdef DEBUG
			VMultiPrint(DEBUG_LEVEL_INFO,DBG_IOCTL,"Move from tap!\n");
#endif
		}
#ifdef DEBUG
		VMultiPrint(DEBUG_LEVEL_INFO,DBG_IOCTL,"Has moved!\n");
#endif
	}
	if (overrideDeltas){
		delta_x = 0;
		delta_y = 0;
	}

	sc->lastnfingers = nfingers;

	if (CYAPA_TOUCH_P(regs, 0) < 20)
		sc->tick -= tickinc;
	if (CYAPA_TOUCH_P(regs, 0) < 10)
		sc->tick = 0;
	else if (sc->hasmoved)
		sc->tick = 0;
	if (sc->tick < 0)
		sc->tick = 0;

	INPUT input;
	if (afingers < 2 || sc->mousedown){
		input.mi.dx = delta_x;
		input.mi.dy = delta_y;
		input.mi.dwFlags = MOUSEEVENTF_MOVE;
		if (delta_x != 0 && delta_y != 0)
			MySendInput(pDevice, 1, &input, sc, sizeof(input));
	}
	else if (afingers == 2){
		if (abs(delta_x) > abs(delta_y)){
			input.mi.dwFlags = MOUSEEVENTF_HWHEEL;
			input.mi.mouseData = -delta_x;
			MySendInput(pDevice, 1, &input, sc, sizeof(input));
		}
		else if (abs(delta_y) > abs(delta_x)){
			input.mi.dwFlags = MOUSEEVENTF_WHEEL;
			input.mi.mouseData = delta_y;
			MySendInput(pDevice, 1, &input, sc, sizeof(input));
		}
	}
	else if (afingers == 3){
		if (sc->hasmoved){
			sc->multitaskingx += delta_x;
			sc->multitaskingy += delta_y;
			if (sc->multitaskinggesturetick > 5 && !sc->multitaskingdone){
				if (abs(sc->multitaskingx) > abs(sc->multitaskingy)){
					BYTE shiftKeys = KBD_LGUI_BIT | KBD_LCONTROL_BIT;
					BYTE keyCodes[KBD_KEY_CODES] = { 0, 0, 0, 0, 0, 0 };
					if (sc->multitaskingx > 0)
						keyCodes[0] = 0x50;
					else
						keyCodes[0] = 0x4F;
					vmulti_update_keyboard(pDevice, shiftKeys, keyCodes);
					shiftKeys = 0;
					keyCodes[0] = 0x0;
					vmulti_update_keyboard(pDevice, shiftKeys, keyCodes);
				}
				else if (abs(sc->multitaskingy) > abs(sc->multitaskingx)){
					BYTE shiftKeys = KBD_LGUI_BIT;
					BYTE keyCodes[KBD_KEY_CODES] = { 0, 0, 0, 0, 0, 0 };
					if (sc->multitaskingy < 0)
						keyCodes[0] = 0x2B;
					else
						keyCodes[0] = 0x07;
					vmulti_update_keyboard(pDevice, shiftKeys, keyCodes);
					shiftKeys = 0;
					keyCodes[0] = 0x0;
					vmulti_update_keyboard(pDevice, shiftKeys, keyCodes);
				}
				sc->multitaskingdone = true;
				sc->multitaskinggesturetick = -1;
			}
			else if (sc->multitaskingdone){
				if (sc->multitaskinggesturetick > 25){
					sc->multitaskinggesturetick = -1;
					sc->multitaskingx = 0;
					sc->multitaskingy = 0;
					sc->multitaskingdone = false;
				}
			}
#ifdef DEBUG
			VMultiPrint(DEBUG_LEVEL_INFO,DBG_IOCTL,"Multitasking Gestures!\n");
#endif
			sc->multitaskinggesturetick++;
		}
	}

	if (afingers != 3){
		sc->multitaskinggesturetick = 0;
		sc->multitaskingx = 0;
		sc->multitaskingy = 0;
		sc->multitaskingdone = false;
	}


	if (afingers > 0){
		if ((regs->fngr & CYAPA_FNGR_LEFT) != 0 && sc->mousedown == false){
			sc->mousedown = true;

			if (afingers == 1){
				if (sc->y < 400 || sc->x < 400){
					sc->mousebutton = 0;
				}
				else {
					sc->mousebutton = 1;
				}
			}
			else if (afingers == 2){
				sc->mousebutton = 1;
			}
			else if (afingers == 3){
				sc->mousebutton = 2;
			}
			else if (afingers == 4){
				sc->mousebutton = 3;
			}

			input.mi.dx = 0;
			input.mi.dy = 0;
			input.mi.mouseData = 0;
			MySendInput(pDevice, 1, &input, sc, sizeof(INPUT));
		}
		if (!overrideDeltas){
			sc->x = x;
			sc->y = y;
		}
	}
	else {
		if (!overrideDeltas){
			sc->x = 0;
			sc->y = 0;
		}
	}

	if ((regs->fngr & CYAPA_FNGR_LEFT) == 0 && sc->mousedown == true && sc->mousedownfromtap != true){
		sc->mousedown = false;

		if (sc->mousebutton == 3){
			BYTE shiftKeys = KBD_LGUI_BIT;
			BYTE keyCodes[KBD_KEY_CODES] = { 0x04, 0, 0, 0, 0, 0 };
			vmulti_update_keyboard(pDevice, shiftKeys, keyCodes);
			shiftKeys = 0;
			keyCodes[0] = 0x0;
			vmulti_update_keyboard(pDevice, shiftKeys, keyCodes);

		}
		sc->mousebutton = 0;
		input.mi.dx = 0;
		input.mi.dy = 0;
		input.mi.mouseData = 0;
		MySendInput(pDevice, 1, &input, sc, sizeof(INPUT));
	}
}