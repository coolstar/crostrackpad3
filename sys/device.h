#ifndef _DEVICE_H_
#define _DEVICE_H_

//
// WDF event callbacks.
//

EVT_WDF_DEVICE_PREPARE_HARDWARE      OnPrepareHardware;
EVT_WDF_DEVICE_RELEASE_HARDWARE      OnReleaseHardware;
EVT_WDF_DEVICE_D0_ENTRY              OnD0Entry;
EVT_WDF_DEVICE_D0_EXIT               OnD0Exit;

EVT_WDF_FILE_CLEANUP                 OnFileCleanup;

EVT_WDF_IO_QUEUE_IO_DEFAULT          OnTopLevelIoDefault;
EVT_WDF_IO_QUEUE_IO_READ             OnIoRead;
EVT_WDF_IO_QUEUE_IO_WRITE            OnIoWrite;
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL   OnIoDeviceControl;

EVT_WDF_INTERRUPT_ISR                OnInterruptIsr;
EVT_WDF_TIMER OnPollTimerFunc;

void ProcessSetting(PDEVICE_CONTEXT pDevice, struct csgesture_softc *sc, int settingRegister, int settingValue);

#endif