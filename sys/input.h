#include "stdint.h"

#define MOUSEEVENTF_MOVE 0x0001
#define MOUSEEVENTF_WHEEL 0x0800
#define MOUSEEVENTF_HWHEEL 0x01000

struct MOUSEINPUT {
	BYTE dx;
	BYTE dy;
	int32_t dwFlags;
	int32_t mouseData;
};

struct INPUT {
	MOUSEINPUT mi;
};