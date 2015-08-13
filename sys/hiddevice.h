#if !defined(_VMULTI_H_)
#define _VMULTI_H_

#pragma warning(disable:4200)  // suppress nameless struct/union warning
#pragma warning(disable:4201)  // suppress nameless struct/union warning
#pragma warning(disable:4214)  // suppress bit field types other than int warning
#include <initguid.h>
#include <wdm.h>

#pragma warning(default:4200)
#pragma warning(default:4201)
#pragma warning(default:4214)
#include <wdf.h>

#pragma warning(disable:4201)  // suppress nameless struct/union warning
#pragma warning(disable:4214)  // suppress bit field types other than int warning
#include <hidport.h>

#include "hidcommon.h"

//
// String definitions
//

#define DRIVERNAME                 "crostrackpad.sys: "

#define CYAPA_POOL_TAG            (ULONG)'PAYC'
#define CYAPA_HARDWARE_IDS        L"ACPI\\CYAP0000\0\0"
#define CYAPA_HARDWARE_IDS_LENGTH sizeof(CYAPA_HARDWARE_IDS)

#define NTDEVICE_NAME_STRING       L"\\Device\\CYAP0000"
#define SYMBOLIC_NAME_STRING       L"\\DosDevices\\CYAP0000"

//
// This is the default report descriptor for the Hid device provided
// by the mini driver in response to IOCTL_HID_GET_REPORT_DESCRIPTOR.
// 

typedef UCHAR HID_REPORT_DESCRIPTOR, *PHID_REPORT_DESCRIPTOR;

#ifdef DEFINEDESCRIPTOR
HID_REPORT_DESCRIPTOR DefaultReportDescriptor[] = {
	//
	// Relative mouse report starts here
	//
	0x05, 0x01,                         // USAGE_PAGE (Generic Desktop)
	0x09, 0x02,                         // USAGE (Mouse)
	0xa1, 0x01,                         // COLLECTION (Application)
	0x85, REPORTID_RELATIVE_MOUSE,      //   REPORT_ID (Mouse)
	0x09, 0x01,                         //   USAGE (Pointer)
	0xa1, 0x00,                         //   COLLECTION (Physical)
	0x05, 0x09,                         //     USAGE_PAGE (Button)
	0x19, 0x01,                         //     USAGE_MINIMUM (Button 1)
	0x29, 0x05,                         //     USAGE_MAXIMUM (Button 5)
	0x15, 0x00,                         //     LOGICAL_MINIMUM (0)
	0x25, 0x01,                         //     LOGICAL_MAXIMUM (1)
	0x75, 0x01,                         //     REPORT_SIZE (1)
	0x95, 0x05,                         //     REPORT_COUNT (5)
	0x81, 0x02,                         //     INPUT (Data,Var,Abs)
	0x95, 0x03,                         //     REPORT_COUNT (3)
	0x81, 0x03,                         //     INPUT (Cnst,Var,Abs)
	0x05, 0x01,                         //     USAGE_PAGE (Generic Desktop)
	0x09, 0x30,                         //     USAGE (X)
	0x09, 0x31,                         //     USAGE (Y)
	0x15, 0x81,                         //     Logical Minimum (-127)
	0x25, 0x7F,                         //     Logical Maximum (127)
	0x75, 0x08,                         //     REPORT_SIZE (8)
	0x95, 0x02,                         //     REPORT_COUNT (2)
	0x81, 0x06,                         //     INPUT (Data,Var,Rel)
	0x05, 0x01,                         //     Usage Page (Generic Desktop)
	0x09, 0x38,                         //     Usage (Wheel)
	0x15, 0x81,                         //     Logical Minimum (-127)
	0x25, 0x7F,                         //     Logical Maximum (127)
	0x75, 0x08,                         //     Report Size (8)
	0x95, 0x01,                         //     Report Count (1)
	0x81, 0x06,                         //     Input (Data, Variable, Relative)
	// ------------------------------  Horizontal wheel
	0x05, 0x0c,                         //     USAGE_PAGE (Consumer Devices)
	0x0a, 0x38, 0x02,                   //     USAGE (AC Pan)
	0x15, 0x81,                         //     LOGICAL_MINIMUM (-127)
	0x25, 0x7f,                         //     LOGICAL_MAXIMUM (127)
	0x75, 0x08,                         //     REPORT_SIZE (8)
	0x95, 0x01,                         //     Report Count (1)
	0x81, 0x06,                         //     Input (Data, Variable, Relative)
	0xc0,                               //   END_COLLECTION
	0xc0,                               // END_COLLECTION

	//
	// Keyboard report starts here
	//    
	0x05, 0x01,                         // USAGE_PAGE (Generic Desktop)
	0x09, 0x06,                         // USAGE (Keyboard)
	0xa1, 0x01,                         // COLLECTION (Application)
	0x85, REPORTID_KEYBOARD,            //   REPORT_ID (Keyboard)    
	0x05, 0x07,                         //   USAGE_PAGE (Keyboard)
	0x19, 0xe0,                         //   USAGE_MINIMUM (Keyboard LeftControl)
	0x29, 0xe7,                         //   USAGE_MAXIMUM (Keyboard Right GUI)
	0x15, 0x00,                         //   LOGICAL_MINIMUM (0)
	0x25, 0x01,                         //   LOGICAL_MAXIMUM (1)
	0x75, 0x01,                         //   REPORT_SIZE (1)
	0x95, 0x08,                         //   REPORT_COUNT (8)
	0x81, 0x02,                         //   INPUT (Data,Var,Abs)
	0x95, 0x01,                         //   REPORT_COUNT (1)
	0x75, 0x08,                         //   REPORT_SIZE (8)
	0x81, 0x03,                         //   INPUT (Cnst,Var,Abs)
	0x95, 0x05,                         //   REPORT_COUNT (5)
	0x75, 0x01,                         //   REPORT_SIZE (1)
	0x05, 0x08,                         //   USAGE_PAGE (LEDs)
	0x19, 0x01,                         //   USAGE_MINIMUM (Num Lock)
	0x29, 0x05,                         //   USAGE_MAXIMUM (Kana)
	0x91, 0x02,                         //   OUTPUT (Data,Var,Abs)
	0x95, 0x01,                         //   REPORT_COUNT (1)
	0x75, 0x03,                         //   REPORT_SIZE (3)
	0x91, 0x03,                         //   OUTPUT (Cnst,Var,Abs)
	0x95, 0x06,                         //   REPORT_COUNT (6)
	0x75, 0x08,                         //   REPORT_SIZE (8)
	0x15, 0x00,                         //   LOGICAL_MINIMUM (0)
	0x25, 0x65,                         //   LOGICAL_MAXIMUM (101)
	0x05, 0x07,                         //   USAGE_PAGE (Keyboard)
	0x19, 0x00,                         //   USAGE_MINIMUM (Reserved (no event indicated))
	0x29, 0x65,                         //   USAGE_MAXIMUM (Keyboard Application)
	0x81, 0x00,                         //   INPUT (Data,Ary,Abs)
	0xc0,                               // END_COLLECTION
};


//
// This is the default HID descriptor returned by the mini driver
// in response to IOCTL_HID_GET_DEVICE_DESCRIPTOR. The size
// of report descriptor is currently the size of DefaultReportDescriptor.
//

CONST HID_DESCRIPTOR DefaultHidDescriptor = {
	0x09,   // length of HID descriptor
	0x21,   // descriptor type == HID  0x21
	0x0100, // hid spec release
	0x00,   // country code == Not Specified
	0x01,   // number of HID class descriptors
	{ 0x22,   // descriptor type 
	sizeof(DefaultReportDescriptor) }  // total length of report descriptor
};
#endif

#define VMULTI_CONTEXT DEVICE_CONTEXT
#define PVMULTI_CONTEXT PDEVICE_CONTEXT
#define VMultiGetDeviceContext GetDeviceContext

//
// Function definitions
//

DRIVER_INITIALIZE DriverEntry;

EVT_WDF_DRIVER_UNLOAD VMultiDriverUnload;

EVT_WDF_DRIVER_DEVICE_ADD VMultiEvtDeviceAdd;

EVT_WDFDEVICE_WDM_IRP_PREPROCESS VMultiEvtWdmPreprocessMnQueryId;

NTSTATUS
VMultiGetHidDescriptor(
IN WDFDEVICE Device,
IN WDFREQUEST Request
);

NTSTATUS
VMultiGetReportDescriptor(
IN WDFDEVICE Device,
IN WDFREQUEST Request
);

NTSTATUS
VMultiGetDeviceAttributes(
IN WDFREQUEST Request
);

NTSTATUS
VMultiGetString(
IN WDFREQUEST Request
);

NTSTATUS
VMultiWriteReport(
IN PVMULTI_CONTEXT DevContext,
IN WDFREQUEST Request
);

NTSTATUS
VMultiProcessVendorReport(
IN PVMULTI_CONTEXT DevContext,
IN PVOID ReportBuffer,
IN ULONG ReportBufferLen,
OUT size_t* BytesWritten
);

NTSTATUS
VMultiReadReport(
IN PVMULTI_CONTEXT DevContext,
IN WDFREQUEST Request,
OUT BOOLEAN* CompleteRequest
);

NTSTATUS
VMultiSetFeature(
IN PVMULTI_CONTEXT DevContext,
IN WDFREQUEST Request,
OUT BOOLEAN* CompleteRequest
);

NTSTATUS
VMultiGetFeature(
IN PVMULTI_CONTEXT DevContext,
IN WDFREQUEST Request,
OUT BOOLEAN* CompleteRequest
);

PCHAR
DbgHidInternalIoctlString(
IN ULONG        IoControlCode
);

//
// Helper macros
//

#define DEBUG_LEVEL_ERROR   1
#define DEBUG_LEVEL_INFO    2
#define DEBUG_LEVEL_VERBOSE 3

#define DBG_INIT  1
#define DBG_PNP   2
#define DBG_IOCTL 4

#if 0
#define CyapaPrintPrint(dbglevel, dbgcatagory, fmt, ...) {          \
    if (CyapaPrintDebugLevel >= dbglevel &&                         \
        (CyapaPrintDebugCatagories && dbgcatagory))                 \
	    {                                                           \
        DbgPrint(DRIVERNAME);                                   \
        DbgPrint(fmt, __VA_ARGS__);                             \
	    }                                                           \
}
#else
#define CyapaPrint(dbglevel, fmt, ...) {                       \
}
#endif

#endif
