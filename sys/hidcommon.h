#if !defined(_CYAPA_COMMON_H_)
#define _CYAPA_COMMON_H_

//
//These are the device attributes returned by vmulti in response
// to IOCTL_HID_GET_DEVICE_ATTRIBUTES.
//

#define CYAPA_PID              0x0004
#define CYAPA_VID              0x04B4
#define CYAPA_VERSION          0x0003

//
// These are the report ids
//

#define REPORTID_FEATURE        0x02
#define REPORTID_RELATIVE_MOUSE 0x04
#define REPORTID_TOUCHPAD       0x05
#define REPORTID_SCROLL			0x06
#define REPORTID_KEYBOARD       0x07
#define REPORTID_SCROLLCTRL		0x08

//
// Keyboard specific report infomation
//

#define KBD_LCONTROL_BIT     1
#define KBD_LGUI_BIT         8

#define KBD_KEY_CODES        6

#pragma pack(1)
typedef struct _CYAPA_KEYBOARD_REPORT
{

	BYTE      ReportID;

	// Left Control, Left Shift, Left Alt, Left GUI
	// Right Control, Right Shift, Right Alt, Right GUI
	BYTE      ShiftKeyFlags;

	BYTE      Reserved;

	// See http://www.usb.org/developers/devclass_docs/Hut1_11.pdf
	// for a list of key codes
	BYTE      KeyCodes[KBD_KEY_CODES];

} CyapaKeyboardReport;

#pragma pack()

//
// Mouse specific report information
//

#define MOUSE_BUTTON_1     0x01
#define MOUSE_BUTTON_2     0x02
#define MOUSE_BUTTON_3     0x04

#define MIN_WHEEL_POS   -127
#define MAX_WHEEL_POS    127

//
// Relative mouse specific report information
//

#define RELATIVE_MOUSE_MIN_COORDINATE   -127
#define RELATIVE_MOUSE_MAX_COORDINATE   127

#pragma pack(1)
typedef struct _CYAPA_RELATIVE_MOUSE_REPORT
{

	BYTE        ReportID;

	BYTE        Button;

	BYTE        XValue;

	BYTE        YValue;

	BYTE        WheelPosition;

	BYTE		HWheelPosition;

} CyapaRelativeMouseReport;
#pragma pack()

//
// Scroll specific report information
//
#pragma pack(1)
typedef struct _CYAPA_SCROLL_REPORT
{

	BYTE        ReportID;

	BYTE		Flag;

	USHORT        Touch1XValue;

	USHORT        Touch1YValue;

	USHORT        Touch2XValue;

	USHORT        Touch2YValue;

} CyapaScrollReport;
#pragma pack()

#pragma pack(1)
typedef struct _CYAPA_SCROLL_CONTROL_REPORT
{

	BYTE        ReportID;

	BYTE		Flag;

} CyapaScrollControlReport;
#pragma pack()

//
// Feature report infomation
//

#define DEVICE_MODE_MOUSE        0x00

#pragma pack(1)
typedef struct _CYAPA_FEATURE_REPORT
{

	BYTE      ReportID;

	BYTE      DeviceMode;

	BYTE      DeviceIdentifier;

} CyapaFeatureReport;
#pragma pack()

#endif
