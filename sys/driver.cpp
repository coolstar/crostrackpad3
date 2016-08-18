#include "internal.h"
#include "driver.h"
#include "device.h"
#include "ntstrsafe.h"
#include "hiddevice.h"	
#include "input.h"

void TrackpadRawInput(PDEVICE_CONTEXT pDevice, struct csgesture_softc *sc, struct cyapa_regs *regs, int tickinc);
void SetDefaultSettings(struct csgesture_softc *sc);
void CyapaTimerFunc(_In_ WDFTIMER hTimer);

#define MAX_FINGERS 15

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
			CyapaPrint(
                TRACE_LEVEL_ERROR, 
                TRACE_FLAG_WDFLOADING,
                "Error creating WDFDEVICE - %!STATUS!", 
                status);

            goto exit;
        }

        pDevice = GetDeviceContext(fxDevice);
        NT_ASSERT(pDevice != nullptr);

		SetDefaultSettings(&pDevice->sc);

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
			CyapaPrint(
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
			CyapaPrint(DEBUG_LEVEL_ERROR, DBG_PNP,
				"WdfIoQueueCreate failed 0x%x\n", status);

            goto exit;
        }
    }

	WDF_IO_QUEUE_CONFIG           queueConfig;

	WDF_IO_QUEUE_CONFIG_INIT(&queueConfig, WdfIoQueueDispatchManual);

	queueConfig.PowerManaged = WdfFalse;

	status = WdfIoQueueCreate(pDevice->FxDevice,
		&queueConfig,
		WDF_NO_OBJECT_ATTRIBUTES,
		&pDevice->ReportQueue
		);

	if (!NT_SUCCESS(status))
	{
		CyapaPrint(DEBUG_LEVEL_ERROR, DBG_PNP, "Queue 2!\n");
		CyapaPrint(DEBUG_LEVEL_ERROR, DBG_PNP,
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
		&pDevice->Interrupt);

	if (!NT_SUCCESS(status))
	{
		CyapaPrint(DEBUG_LEVEL_ERROR, DBG_PNP,
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
		CyapaPrint(DEBUG_LEVEL_ERROR, DBG_PNP, "(%!FUNC!) WdfTimerCreate failed status:%!STATUS!\n", status);
		return status;
	}

	CyapaPrint(DEBUG_LEVEL_ERROR, DBG_PNP,
		"Success! 0x%x\n", status);

	pDevice->DeviceMode = DEVICE_MODE_MOUSE;

exit:

    FuncExit(TRACE_FLAG_WDFLOADING);

    return status;
}


BOOLEAN OnInterruptIsr(
	WDFINTERRUPT Interrupt,
	ULONG MessageID){
	UNREFERENCED_PARAMETER(MessageID);

	WDFDEVICE Device = WdfInterruptGetDevice(Interrupt);
	PDEVICE_CONTEXT pDevice = GetDeviceContext(Device);

	if (!pDevice->ConnectInterrupt)
		return true;

	CyapaPrint(DEBUG_LEVEL_INFO, DBG_IOCTL, "Interrupt!\n");

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

	csgesture_softc sc = pDevice->sc;
	TrackpadRawInput(pDevice, &sc, &regs, 1);
	pDevice->sc = sc;
	return;
}

static int distancesq(int delta_x, int delta_y){
	return (delta_x * delta_x) + (delta_y*delta_y);
}

_CYAPA_RELATIVE_MOUSE_REPORT lastreport;

static void update_relative_mouse(PDEVICE_CONTEXT pDevice, BYTE button,
	BYTE x, BYTE y, BYTE wheelPosition, BYTE wheelHPosition){
	_CYAPA_RELATIVE_MOUSE_REPORT report;
	report.ReportID = REPORTID_RELATIVE_MOUSE;
	report.Button = button;
	report.XValue = x;
	report.YValue = y;
	report.WheelPosition = wheelPosition;
	report.HWheelPosition = wheelHPosition;
	if (report.Button == lastreport.Button &&
		report.XValue == lastreport.XValue &&
		report.YValue == lastreport.YValue &&
		report.WheelPosition == lastreport.WheelPosition &&
		report.HWheelPosition == lastreport.HWheelPosition)
		return;
	lastreport = report;

	size_t bytesWritten;
	CyapaProcessVendorReport(pDevice, &report, sizeof(report), &bytesWritten);
}

static void update_keyboard(PDEVICE_CONTEXT pDevice, BYTE shiftKeys, BYTE keyCodes[KBD_KEY_CODES]){
	_CYAPA_KEYBOARD_REPORT report;
	report.ReportID = REPORTID_KEYBOARD;
	report.ShiftKeyFlags = shiftKeys;
	for (int i = 0; i < KBD_KEY_CODES; i++){
		report.KeyCodes[i] = keyCodes[i];
	}

	size_t bytesWritten;
	CyapaProcessVendorReport(pDevice, &report, sizeof(report), &bytesWritten);
}

static void stop_scroll(PDEVICE_CONTEXT pDevice) {
	_CYAPA_SCROLL_REPORT report;
	report.ReportID = REPORTID_SCROLL;
	report.Flag = 1;
	report.Touch1XValue = 65535;
	report.Touch1YValue = 65535;
	report.Touch2XValue = 65535;
	report.Touch2YValue = 65535;

	size_t bytesWritten;
	CyapaProcessVendorReport(pDevice, &report, sizeof(report), &bytesWritten);
}

USHORT filterNegative(int val) {
	if (val > 0)
		return val;
	return 65535;
}

bool ProcessMove(PDEVICE_CONTEXT pDevice, csgesture_softc *sc, int abovethreshold, int iToUse[3]) {
	if (abovethreshold == 1 || sc->panningActive) {
		int i = iToUse[0];
		if (!sc->panningActive && sc->tick[i] < 5)
			return false;

		stop_scroll(pDevice);

		if (sc->panningActive && i == -1)
			i = sc->idForPanning;

		int delta_x = sc->x[i] - sc->lastx[i];
		int delta_y = sc->y[i] - sc->lasty[i];

		if (abs(delta_x) > 75 || abs(delta_y) > 75) {
			delta_x = 0;
			delta_y = 0;
		}

		for (int j = 0;j < MAX_FINGERS;j++) {
			if (j != i) {
				if (sc->blacklistedids[j] != 1) {
					if (sc->y[j] > sc->y[i]) {
						if (sc->truetick[j] > sc->truetick[i] + 15) {
							sc->blacklistedids[j] = 1;
						}
					}
				}
			}
		}

		sc->dx = delta_x;
		sc->dy = delta_y;

		sc->dx *= sc->settings.pointerMultiplier;
		sc->dx /= 10;

		sc->dy *= sc->settings.pointerMultiplier;
		sc->dy /= 10;

		sc->panningActive = true;
		sc->idForPanning = i;
		return true;
	}
	return false;
}

bool ProcessScroll(PDEVICE_CONTEXT pDevice, csgesture_softc *sc, int abovethreshold, int iToUse[3]) {
	if (!sc->settings.scrollEnabled)
		return false;

	sc->scrollx = 0;
	sc->scrolly = 0;
	if (abovethreshold == 2 || sc->scrollingActive) {
		int i1 = iToUse[0];
		int i2 = iToUse[1];

		if (!sc->scrollingActive && !sc->scrollInertiaActive) {
			if (sc->truetick[i1] < 4 && sc->truetick[i2] < 4)
				return false; 
		}

		if (sc->scrollingActive){
			if (i1 == -1) {
				if (i2 != sc->idsForScrolling[0])
					i1 = sc->idsForScrolling[0];
				else
					i1 = sc->idsForScrolling[1];
			}
			if (i2 == -1) {
				if (i1 != sc->idsForScrolling[0])
					i2 = sc->idsForScrolling[0];
				else
					i2 = sc->idsForScrolling[1];
			}
		}

		/*int delta_x1 = sc->x[i1] - sc->lastx[i1];
		int delta_y1 = sc->y[i1] - sc->lasty[i1];

		int delta_x2 = sc->x[i2] - sc->lastx[i2];
		int delta_y2 = sc->y[i2] - sc->lasty[i2];

		if ((abs(delta_y1) + abs(delta_y2)) > (abs(delta_x1) + abs(delta_x2))) {
			int avgy = (delta_y1 + delta_y2) / 2;
			sc->scrolly = avgy;
		}
		else {
			int avgx = (delta_x1 + delta_x2) / 2;
			sc->scrollx = avgx;
		}
		if (abs(sc->scrollx) > 100)
			sc->scrollx = 0;
		if (abs(sc->scrolly) > 100)
			sc->scrolly = 0;
		if (sc->scrolly > 8)
			sc->scrolly = sc->scrolly / 8;
		else if (sc->scrolly > 5)
			sc->scrolly = 1;
		else if (sc->scrolly < -8)
			sc->scrolly = sc->scrolly / 8;
		else if (sc->scrolly < -5)
			sc->scrolly = -1;
		else
			sc->scrolly = 0;

		if (sc->scrollx > 8) {
			sc->scrollx = sc->scrollx / 8;
			sc->scrollx = -sc->scrollx;
		}
		else if (sc->scrollx > 5)
			sc->scrollx = -1;
		else if (sc->scrollx < -8) {
			sc->scrollx = sc->scrollx / 8;
			sc->scrollx = -sc->scrollx;
		}
		else if (sc->scrollx < -5)
			sc->scrollx = 1;
		else
			sc->scrollx = 0;*/

		_CYAPA_SCROLL_REPORT report;
		report.ReportID = REPORTID_SCROLL;
		report.Flag = 0;
		report.Touch1XValue = filterNegative(sc->x[i1]);
		report.Touch1YValue = filterNegative(sc->y[i1]);
		report.Touch2XValue = filterNegative(sc->x[i2]);
		report.Touch2YValue = filterNegative(sc->y[i2]);

		size_t bytesWritten;
		CyapaProcessVendorReport(pDevice, &report, sizeof(report), &bytesWritten);

		int fngrcount = 0;
		int totfingers = 0;
		for (int i = 0; i < MAX_FINGERS; i++) {
			if (sc->x[i] != -1) {
				totfingers++;
				if (i == i1 || i == i2)
					fngrcount++;
			}
		}

		if (fngrcount == 2)
			sc->ticksSinceScrolling = 0;
		else
			sc->ticksSinceScrolling++;
		if (fngrcount == 2 || sc->ticksSinceScrolling <= 5) {
			sc->scrollingActive = true;
			if (abovethreshold == 2){
				sc->idsForScrolling[0] = iToUse[0];
				sc->idsForScrolling[1] = iToUse[1];
			}
		}
		else {
			sc->scrollingActive = false;
			sc->idsForScrolling[0] = -1;
			sc->idsForScrolling[1] = -1;
		}
		return true;
	}
	return false;
}

bool ProcessThreeFingerSwipe(PDEVICE_CONTEXT pDevice, csgesture_softc *sc, int abovethreshold, int iToUse[3]) {
	if (sc->alttabswitchershowing) {
		BYTE shiftKeys = KBD_LALT_BIT;
		BYTE keyCodes[KBD_KEY_CODES] = { 0, 0, 0, 0, 0, 0 };
		update_keyboard(pDevice, shiftKeys, keyCodes);
	}
	if (abovethreshold == 3 || abovethreshold == 4) {
		stop_scroll(pDevice);

		int i1 = iToUse[0];
		int delta_x1 = sc->x[i1] - sc->lastx[i1];
		int delta_y1 = sc->y[i1] - sc->lasty[i1];

		int i2 = iToUse[1];
		int delta_x2 = sc->x[i2] - sc->lastx[i2];
		int delta_y2 = sc->y[i2] - sc->lasty[i2];

		int i3 = iToUse[2];
		int delta_x3 = sc->x[i3] - sc->lastx[i3];
		int delta_y3 = sc->y[i3] - sc->lasty[i3];

		int avgx = (delta_x1 + delta_x2 + delta_x3) / 3;
		int avgy = (delta_y1 + delta_y2 + delta_y3) / 3;

		sc->multitaskingx += avgx;
		sc->multitaskingy += avgy;
		sc->multitaskinggesturetick++;

		if (sc->multitaskinggesturetick > 5 && !sc->multitaskingdone) {
			if ((abs(delta_y1) + abs(delta_y2) + abs(delta_y3)) > (abs(delta_x1) + abs(delta_x2) + abs(delta_x3))) {
				if (abs(sc->multitaskingy) > 15) {
					if (sc->multitaskingy < 0) {
						if (sc->alttabswitchershowing) {
							for (int i = 0; i < 3; i++) {
								sc->idsforalttab[i] = iToUse[i];
							}

							BYTE shiftKeys = KBD_LALT_BIT;
							BYTE keyCodes[KBD_KEY_CODES] = { 0, 0, 0, 0, 0, 0 };
							keyCodes[0] = 0x52; //Alt + Up
							update_keyboard(pDevice, shiftKeys, keyCodes);
							keyCodes[0] = 0x0;
							update_keyboard(pDevice, shiftKeys, keyCodes);
							sc->multitaskingx = 0;
							sc->multitaskingy = 0;
							sc->multitaskingdone = true;
						} 
						else if (abovethreshold == 3 && sc->settings.threeFingerSwipeUpGesture == SwipeUpGestureTaskView ||
							abovethreshold == 4 && sc->settings.fourFingerSwipeUpGesture == SwipeUpGestureTaskView) {
							if (abs(sc->multitaskingy) > 50) {
								BYTE shiftKeys = KBD_LGUI_BIT;
								BYTE keyCodes[KBD_KEY_CODES] = { 0, 0, 0, 0, 0, 0 };
								keyCodes[0] = 0x2B; //Windows Key + Tab
								update_keyboard(pDevice, shiftKeys, keyCodes);
								shiftKeys = 0;
								keyCodes[0] = 0x0;
								update_keyboard(pDevice, shiftKeys, keyCodes);
								sc->multitaskingx = 0;
								sc->multitaskingy = 0;
								sc->multitaskingdone = true;
							}
						}
					}
					else {
						if (sc->alttabswitchershowing) {
							for (int i = 0; i < 3; i++) {
								sc->idsforalttab[i] = iToUse[i];
							}

							BYTE shiftKeys = KBD_LALT_BIT;
							BYTE keyCodes[KBD_KEY_CODES] = { 0, 0, 0, 0, 0, 0 };
							keyCodes[0] = 0x51; //Alt + Down
							update_keyboard(pDevice, shiftKeys, keyCodes);
							keyCodes[0] = 0x0;
							update_keyboard(pDevice, shiftKeys, keyCodes);
							sc->multitaskingx = 0;
							sc->multitaskingy = 0;
							sc->multitaskingdone = true;
						}
						else if (abovethreshold == 3 && sc->settings.threeFingerSwipeDownGesture == SwipeDownGestureShowDesktop ||
							abovethreshold == 4 && sc->settings.fourFingerSwipeDownGesture == SwipeDownGestureShowDesktop) {
							if (abs(sc->multitaskingy) > 50) {
								BYTE shiftKeys = KBD_LGUI_BIT;
								BYTE keyCodes[KBD_KEY_CODES] = { 0, 0, 0, 0, 0, 0 };
								keyCodes[0] = 0x07;  //Windows Key + D
								update_keyboard(pDevice, shiftKeys, keyCodes);
								shiftKeys = 0;
								keyCodes[0] = 0x0;
								update_keyboard(pDevice, shiftKeys, keyCodes);
								sc->multitaskingx = 0;
								sc->multitaskingy = 0;
								sc->multitaskingdone = true;
							}
						}
					}
				}
			}
			else {
				if (abs(sc->multitaskingx) > 15) {
					if (sc->multitaskingx > 0) {
						if ((abovethreshold == 3 && sc->settings.threeFingerSwipeLeftRightGesture == SwipeGestureSwitchWorkspace ||
							abovethreshold == 4 && sc->settings.fourFingerSwipeLeftRightGesture == SwipeGestureSwitchWorkspace) &&
							!sc->alttabswitchershowing) {
							if (abs(sc->multitaskingx) > 50) {
								BYTE shiftKeys = KBD_LGUI_BIT | KBD_LCONTROL_BIT;
								BYTE keyCodes[KBD_KEY_CODES] = { 0, 0, 0, 0, 0, 0 };
								keyCodes[0] = 0x50; //Ctrl + Windows Key + Left
								update_keyboard(pDevice, shiftKeys, keyCodes);
								shiftKeys = 0;
								keyCodes[0] = 0x0;
								update_keyboard(pDevice, shiftKeys, keyCodes);
								sc->multitaskingx = 0;
								sc->multitaskingy = 0;
								sc->multitaskingdone = true;
							}
						}
						else if (abovethreshold == 3 && sc->settings.threeFingerSwipeLeftRightGesture == SwipeGestureAltTabSwitcher ||
							abovethreshold == 4 && sc->settings.fourFingerSwipeLeftRightGesture == SwipeGestureAltTabSwitcher ||
							sc->alttabswitchershowing) {
							for (int i = 0; i < 3; i++) {
								sc->idsforalttab[i] = iToUse[i];
							}

							if (!sc->alttabswitchershowing) {
								BYTE shiftKeys = KBD_LALT_BIT;
								BYTE keyCodes[KBD_KEY_CODES] = { 0, 0, 0, 0, 0, 0 };
								keyCodes[0] = 0x2B; //Alt + Tab
								update_keyboard(pDevice, shiftKeys, keyCodes);
								shiftKeys = KBD_LALT_BIT;
								keyCodes[0] = 0x0;
								update_keyboard(pDevice, shiftKeys, keyCodes);
								sc->multitaskingx = 0;
								sc->multitaskingy = 0;
								sc->multitaskingdone = true;
								sc->alttabswitchershowing = true;
							}
							else {
								BYTE shiftKeys = KBD_LALT_BIT;
								BYTE keyCodes[KBD_KEY_CODES] = { 0, 0, 0, 0, 0, 0 };
								keyCodes[0] = 0x4F; //Alt + Right
								update_keyboard(pDevice, shiftKeys, keyCodes);
								keyCodes[0] = 0x0;
								update_keyboard(pDevice, shiftKeys, keyCodes);
								sc->multitaskingx = 0;
								sc->multitaskingy = 0;
								sc->multitaskingdone = true;
							}
						}
					}
					else {
						if ((abovethreshold == 3 && sc->settings.threeFingerSwipeLeftRightGesture == SwipeGestureSwitchWorkspace ||
							abovethreshold == 4 && sc->settings.fourFingerSwipeLeftRightGesture == SwipeGestureSwitchWorkspace) &&
							!sc->alttabswitchershowing) {
							if (abs(sc->multitaskingx) > 50) {
								BYTE shiftKeys = KBD_LGUI_BIT | KBD_LCONTROL_BIT;
								BYTE keyCodes[KBD_KEY_CODES] = { 0, 0, 0, 0, 0, 0 };
								keyCodes[0] = 0x4F; //Ctrl + Windows Key + Right
								update_keyboard(pDevice, shiftKeys, keyCodes);
								shiftKeys = 0;
								keyCodes[0] = 0x0;
								update_keyboard(pDevice, shiftKeys, keyCodes);
								sc->multitaskingx = 0;
								sc->multitaskingy = 0;
								sc->multitaskingdone = true;
							}
						}
						else if (abovethreshold == 3 && sc->settings.threeFingerSwipeLeftRightGesture == SwipeGestureAltTabSwitcher ||
							abovethreshold == 4 && sc->settings.fourFingerSwipeLeftRightGesture == SwipeGestureAltTabSwitcher ||
							sc->alttabswitchershowing) {
							for (int i = 0; i < 3; i++) {
								sc->idsforalttab[i] = iToUse[i];
							}

							if (!sc->alttabswitchershowing) {
								BYTE shiftKeys = KBD_LALT_BIT | KBD_LSHIFT_BIT;
								BYTE keyCodes[KBD_KEY_CODES] = { 0, 0, 0, 0, 0, 0 };
								keyCodes[0] = 0x2B; //Alt + Shift + Tab
								update_keyboard(pDevice, shiftKeys, keyCodes);
								shiftKeys = KBD_LALT_BIT;
								keyCodes[0] = 0x0;
								update_keyboard(pDevice, shiftKeys, keyCodes);
								sc->multitaskingx = 0;
								sc->multitaskingy = 0;
								sc->multitaskingdone = true;
								sc->alttabswitchershowing = true;
							}
							else {
								BYTE shiftKeys = KBD_LALT_BIT;
								BYTE keyCodes[KBD_KEY_CODES] = { 0, 0, 0, 0, 0, 0 };
								keyCodes[0] = 0x50; //Alt + Left
								update_keyboard(pDevice, shiftKeys, keyCodes);
								keyCodes[0] = 0x0;
								update_keyboard(pDevice, shiftKeys, keyCodes);
								sc->multitaskingx = 0;
								sc->multitaskingy = 0;
								sc->multitaskingdone = true;
							}
						}
					}
				}
			}
		}
		else if (sc->multitaskinggesturetick > 25) {
			sc->multitaskingx = 0;
			sc->multitaskingy = 0;
			sc->multitaskinggesturetick = 0;
			sc->multitaskingdone = false;
		}
		return true;
	}
	else {
		if (sc->alttabswitchershowing) {
			bool foundTouch = false;
			for (int i = 0; i < MAX_FINGERS; i++) {
				if (foundTouch)
					break;
				if (sc->x[i] == -1)
					continue;
				for (int j = 0; j < 3; j++) {
					if (i = sc->idsforalttab[j]) {
						foundTouch = true;
						break;
					}
				}
			}
			if (!foundTouch) {
				BYTE shiftKeys = 0;
				BYTE keyCodes[KBD_KEY_CODES] = { 0, 0, 0, 0, 0, 0 };
				keyCodes[0] = 0x0;
				update_keyboard(pDevice, shiftKeys, keyCodes);
				sc->alttabswitchershowing = false;
				for (int i = 0; i < 3; i++) {
					sc->idsforalttab[i] = -1;
				}
			}
		}
		sc->multitaskingx = 0;
		sc->multitaskingy = 0;
		sc->multitaskinggesturetick = 0;
		sc->multitaskingdone = false;
		return false;
	}
}

void TapToClickOrDrag(PDEVICE_CONTEXT pDevice, csgesture_softc *sc, int button) {
	if (!sc->settings.tapToClickEnabled)
		return;
	if (sc->scrollInertiaActive)
		return;

	sc->tickssinceclick++;
	if (sc->mouseDownDueToTap && sc->idForMouseDown == -1) {
		if (sc->tickssinceclick > 10) {
			sc->mouseDownDueToTap = false;
			sc->mousedown = false;
			sc->buttonmask = 0;
			//Tap Drag Timed out
		}
		return;
	}
	if (sc->mousedown) {
		sc->tickssinceclick = 0;
		return;
	}
	if (button == 0)
		return;

	for (int i = 0; i < MAX_FINGERS; i++) {
		if (sc->truetick[i] < 10 && sc->truetick[i] > 0)
			button++;
	}

	int buttonmask = 0;

	switch (button) {
	case 1:
		if (!sc->settings.swapLeftRightFingers)
			buttonmask = MOUSE_BUTTON_1;
		else
			buttonmask = MOUSE_BUTTON_2;
		break;
	case 2:
		if (sc->settings.multiFingerTap) {
			if (!sc->settings.swapLeftRightFingers)
				buttonmask = MOUSE_BUTTON_2;
			else
				buttonmask = MOUSE_BUTTON_1;
		}
		break;
	case 3:
		if (sc->settings.multiFingerTap) {
			if (sc->settings.threeFingerTapAction == ThreeFingerTapActionWheelClick)
				buttonmask = MOUSE_BUTTON_3;
			else if (sc->settings.threeFingerTapAction == ThreeFingerTapActionCortana) {
				buttonmask = 0;

				BYTE shiftKeys = KBD_LGUI_BIT;
				BYTE keyCodes[KBD_KEY_CODES] = { 0, 0, 0, 0, 0, 0 };
				keyCodes[0] = 0x06; //Windows Key + C for Cortana
				update_keyboard(pDevice, shiftKeys, keyCodes);
				shiftKeys = 0;
				keyCodes[0] = 0x0;
				update_keyboard(pDevice, shiftKeys, keyCodes);
			}
		}
		break;
	case 4:
		if (sc->settings.fourFingerTapEnabled) {
			buttonmask = 0;

			BYTE shiftKeys = KBD_LGUI_BIT;
			BYTE keyCodes[KBD_KEY_CODES] = { 0, 0, 0, 0, 0, 0 };
			keyCodes[0] = 0x04; //Windows Key + A for Action Center
			update_keyboard(pDevice, shiftKeys, keyCodes);
			shiftKeys = 0;
			keyCodes[0] = 0x0;
			update_keyboard(pDevice, shiftKeys, keyCodes);
		}
		break;
	}
	if (buttonmask != 0 && sc->tickssinceclick > 10 && sc->ticksincelastrelease == 0) {
		sc->idForMouseDown = -1;
		sc->mouseDownDueToTap = true;
		sc->buttonmask = buttonmask;
		sc->mousebutton = button;
		sc->mousedown = true;
		sc->tickssinceclick = 0;
	}
}

void ClearTapDrag(PDEVICE_CONTEXT pDevice, csgesture_softc *sc, int i) {
	if (i == sc->idForMouseDown && sc->mouseDownDueToTap == true) {
		if (sc->tick[i] < 10) {
			//Double Tap
			update_relative_mouse(pDevice, 0, 0, 0, 0, 0);
			update_relative_mouse(pDevice, sc->buttonmask, 0, 0, 0, 0);
		}
		sc->mouseDownDueToTap = false;
		sc->mousedown = false;
		sc->buttonmask = 0;
		sc->idForMouseDown = -1;
		//Clear Tap Drag
	}
}

void ProcessGesture(PDEVICE_CONTEXT pDevice, csgesture_softc *sc) {
#pragma mark reset inputs
	sc->dx = 0;
	sc->dy = 0;

#pragma mark process touch thresholds
	int avgx[MAX_FINGERS];
	int avgy[MAX_FINGERS];

	int abovethreshold = 0;
	int recentlyadded = 0;
	int lastrecentlyadded = -1;
	int iToUse[3] = { -1,-1,-1 };
	int a = 0;

	int nfingers = 0;
	for (int i = 0;i < MAX_FINGERS;i++) {
		if (sc->x[i] != -1)
			nfingers++;
	}

	for (int i = 0;i < MAX_FINGERS;i++) {
		if (sc->truetick[i] < 30 && sc->truetick[i] != 0) {
			recentlyadded++;
			lastrecentlyadded = i;
		}
		if (sc->tick[i] == 0)
			continue;
		if (sc->blacklistedids[i] == 1)
			continue;
		avgx[i] = sc->flextotalx[i] / sc->tick[i];
		avgy[i] = sc->flextotaly[i] / sc->tick[i];
		if (distancesq(avgx[i], avgy[i]) > 2) {
			abovethreshold++;
			iToUse[a] = i;
			a++;
		}
	}

#pragma mark process different gestures
	bool handled = false;
	if (!handled)
		handled = ProcessThreeFingerSwipe(pDevice, sc, abovethreshold, iToUse);
	if (!handled)
		handled = ProcessScroll(pDevice, sc, abovethreshold, iToUse);
	if (!handled)
		handled = ProcessMove(pDevice, sc, abovethreshold, iToUse);

#pragma mark process clickpad press state
	int buttonmask = 0;

	sc->mousebutton = recentlyadded;

	if (sc->settings.rightClickBottomRight) {
		if (sc->mousebutton == 1 && lastrecentlyadded != -1) {
			if (sc->x[lastrecentlyadded] > sc->resx / 2 && sc->y[lastrecentlyadded] > (sc->resy - 60))
				sc->mousebutton = 2;
		}
	}

	if (sc->mousebutton == 0)
		sc->mousebutton = abovethreshold;

	if (sc->mousebutton == 0) {
		if (sc->panningActive)
			sc->mousebutton = 1;
		else
			sc->mousebutton = nfingers;
		if (sc->mousebutton == 0 && sc->settings.clickWithNoFingers)
			sc->mousebutton = 1;
	}
	if (sc->mousebutton > 3)
		sc->mousebutton = 3;

	if (!sc->mouseDownDueToTap) {
		if (sc->buttondown && !sc->mousedown) {
			sc->mousedown = true;
			sc->tickssinceclick = 0;

			switch (sc->mousebutton) {
			case 1:
				if (!sc->settings.swapLeftRightFingers)
					buttonmask = MOUSE_BUTTON_1;
				else
					buttonmask = MOUSE_BUTTON_2;
				break;
			case 2:
				if (sc->settings.multiFingerClick) {
					if (!sc->settings.swapLeftRightFingers)
						buttonmask = MOUSE_BUTTON_2;
					else
						buttonmask = MOUSE_BUTTON_1;
				}
				break;
			case 3:
				if (sc->settings.multiFingerClick) {
					buttonmask = MOUSE_BUTTON_3;
				}
				break;
			}
			sc->buttonmask = buttonmask;
		}
		else if (sc->mousedown && !sc->buttondown) {
			sc->mousedown = false;
			sc->mousebutton = 0;
			sc->buttonmask = 0;
		}
	}

#pragma mark shift to last
	int releasedfingers = 0;

	for (int i = 0;i < MAX_FINGERS;i++) {
		if (sc->x[i] != -1) {
			if (sc->lastx[i] == -1) {
				if (sc->ticksincelastrelease < 10 && sc->mouseDownDueToTap && sc->idForMouseDown == -1) {
					if (sc->settings.tapDragEnabled)
						sc->idForMouseDown = i; //Associate Tap Drag
				}
			}
			sc->truetick[i]++;
			if (sc->tick[i] < 10) {
				if (sc->lastx[i] != -1) {
					sc->totalx[i] += abs(sc->x[i] - sc->lastx[i]);
					sc->totaly[i] += abs(sc->y[i] - sc->lasty[i]);
					sc->totalp[i] += sc->p[i];

					sc->flextotalx[i] = sc->totalx[i];
					sc->flextotaly[i] = sc->totaly[i];

					int j = sc->tick[i];
					sc->xhistory[i][j] = abs(sc->x[i] - sc->lastx[i]);
					sc->yhistory[i][j] = abs(sc->y[i] - sc->lasty[i]);
				}
				sc->tick[i]++;
			}
			else if (sc->lastx[i] != -1) {
				int absx = abs(sc->x[i] - sc->lastx[i]);
				int absy = abs(sc->y[i] - sc->lasty[i]);

				int newtotalx = sc->flextotalx[i] - sc->xhistory[i][0] + absx;
				int newtotaly = sc->flextotaly[i] - sc->yhistory[i][0] + absy;

				sc->totalx[i] += absx;
				sc->totaly[i] += absy;

				sc->flextotalx[i] -= sc->xhistory[i][0];
				sc->flextotaly[i] -= sc->yhistory[i][0];
				for (int j = 1;j < 10;j++) {
					sc->xhistory[i][j - 1] = sc->xhistory[i][j];
					sc->yhistory[i][j - 1] = sc->yhistory[i][j];
				}
				sc->flextotalx[i] += absx;
				sc->flextotaly[i] += absy;

				int j = 9;
				sc->xhistory[i][j] = absx;
				sc->yhistory[i][j] = absy;
			}
		}
		if (sc->x[i] == -1) {
			ClearTapDrag(pDevice, sc, i);
			if (sc->lastx[i] != -1)
				sc->ticksincelastrelease = -1;
			for (int j = 0;j < 10;j++) {
				sc->xhistory[i][j] = 0;
				sc->yhistory[i][j] = 0;
			}
			if (sc->tick[i] < 10 && sc->tick[i] != 0) {
				int avgp = sc->totalp[i] / sc->tick[i];
				if (avgp > 7)
					releasedfingers++;
			}
			sc->totalx[i] = 0;
			sc->totaly[i] = 0;
			sc->totalp[i] = 0;
			sc->tick[i] = 0;
			sc->truetick[i] = 0;

			sc->blacklistedids[i] = 0;

			if (sc->idForPanning == i) {
				sc->panningActive = false;
				sc->idForPanning = -1;
			}
		}
		sc->lastx[i] = sc->x[i];
		sc->lasty[i] = sc->y[i];
		sc->lastp[i] = sc->p[i];
	}
	sc->ticksincelastrelease++;

#pragma mark process tap to click
	TapToClickOrDrag(pDevice, sc, releasedfingers);

#pragma mark send to system
	update_relative_mouse(pDevice, sc->buttonmask, sc->dx, sc->dy, sc->scrolly, sc->scrollx);
}

void TrackpadRawInput(PDEVICE_CONTEXT pDevice, struct csgesture_softc *sc, struct cyapa_regs *regs, int tickinc){
	int nfingers;
	int afingers;	/* actual fingers after culling */
	int i;

	if ((regs->stat & CYAPA_STAT_RUNNING) == 0) {
		regs->fngr = 0;
	}

	nfingers = CYAPA_FNGR_NUMFINGERS(regs->fngr);

	for (int i = 0;i < 15;i++) {
		sc->x[i] = -1;
		sc->y[i] = -1;
		sc->p[i] = -1;
	}
	for (int i = 0;i < nfingers;i++) {
		int a = regs->touch[i].id;
		int x = CYAPA_TOUCH_X(regs, i);
		int y = CYAPA_TOUCH_Y(regs, i);
		int p = CYAPA_TOUCH_P(regs, i);
		sc->x[a] = x;
		sc->y[a] = y;
		sc->p[a] = p;
	}

	sc->buttondown = (regs->fngr & CYAPA_FNGR_LEFT);

	ProcessGesture(pDevice, sc);
}

void SetDefaultSettings(struct csgesture_softc *sc) {
	sc->settings.pointerMultiplier = 10; //done

	//click settings
	sc->settings.swapLeftRightFingers = false;
	sc->settings.clickWithNoFingers = true;
	sc->settings.multiFingerClick = true;
	sc->settings.rightClickBottomRight = false;

	//tap settings
	sc->settings.tapToClickEnabled = true;
	sc->settings.multiFingerTap = true;
	sc->settings.tapDragEnabled = true;

	sc->settings.threeFingerTapAction = ThreeFingerTapActionCortana;

	sc->settings.fourFingerTapEnabled = true;

	//scroll settings
	sc->settings.scrollEnabled = true;

	//three finger gestures
	sc->settings.threeFingerSwipeUpGesture = SwipeUpGestureTaskView;
	sc->settings.threeFingerSwipeDownGesture = SwipeDownGestureShowDesktop;
	sc->settings.threeFingerSwipeLeftRightGesture = SwipeGestureAltTabSwitcher;

	//four finger gestures
	sc->settings.fourFingerSwipeUpGesture = SwipeUpGestureTaskView;
	sc->settings.fourFingerSwipeDownGesture = SwipeDownGestureShowDesktop;
	sc->settings.fourFingerSwipeLeftRightGesture = SwipeGestureSwitchWorkspace;
}

void ProcessInfo(PDEVICE_CONTEXT pDevice, struct csgesture_softc *sc, int infoValue) {
	_CYAPA_INFO_REPORT report;
	report.ReportID = REPORTID_SETTINGS;
	for (int i = 0; i < 60; i++)
		report.Value[i] = 0x00;
	switch (infoValue) {
	case 0: //driver version
		strcpy((char *)report.Value, "3.0 (8/18/2016)");
		break;
	case 1: //product name
		strcpy((char *)report.Value, sc->product_id);
		break;
	case 2: //firmware version
		strcpy((char *)report.Value, sc->firmware_version);
		break;
	}

	size_t bytesWritten;
	CyapaProcessVendorReport(pDevice, &report, sizeof(report), &bytesWritten);
}

void ProcessSetting(PDEVICE_CONTEXT pDevice, struct csgesture_softc *sc, int settingRegister, int settingValue) {
	switch (settingRegister) {
	case 0:
		sc->settings.pointerMultiplier = settingValue;
		break;
	case 1:
		sc->settings.swapLeftRightFingers = settingValue;
		break;
	case 2:
		sc->settings.clickWithNoFingers = settingValue;
		break;
	case 3:
		sc->settings.multiFingerClick = settingValue;
		break;
	case 4:
		sc->settings.rightClickBottomRight = settingValue;
		break;
	case 5:
		sc->settings.tapToClickEnabled = settingValue;
		break;
	case 6:
		sc->settings.multiFingerTap = settingValue;
		break;
	case 7:
		sc->settings.tapDragEnabled = settingValue;
		break;
	case 8:
		sc->settings.threeFingerTapAction = (ThreeFingerTapAction)settingValue;
		break;
	case 9:
		sc->settings.fourFingerTapEnabled = settingValue;
		break;
	case 10:
		sc->settings.scrollEnabled = settingValue;
		break;
	case 11:
		sc->settings.threeFingerSwipeUpGesture = (SwipeUpGesture)settingValue;
		break;
	case 12:
		sc->settings.threeFingerSwipeDownGesture = (SwipeDownGesture)settingValue;
		break;
	case 13:
		sc->settings.threeFingerSwipeLeftRightGesture = (SwipeGesture)settingValue;
		break;
	case 14:
		sc->settings.fourFingerSwipeUpGesture = (SwipeUpGesture)settingValue;
		break;
	case 15:
		sc->settings.fourFingerSwipeDownGesture = (SwipeDownGesture)settingValue;
		break;
	case 16:
		sc->settings.fourFingerSwipeLeftRightGesture = (SwipeGesture)settingValue;
		break;
	case 255: //255 is for driver info
		ProcessInfo(pDevice, sc, settingValue);
		break;
	}
}