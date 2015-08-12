typedef signed char       int8_t;
typedef signed short      int16_t;
typedef signed int        int32_t;
typedef unsigned char     uint8_t;
typedef unsigned short    uint16_t;
typedef unsigned int      uint32_t;

#ifndef ABS32
#define ABS32
	inline int32_t abs(int32_t num){
		if (num < 0){
			return num * -1;
		}
		return num;
	}
#endif