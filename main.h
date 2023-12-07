#include <stdint.h>

struct RegRef8{
	 int idx;
	 char loOrHiReg;
	 uint8_t* ptr;
};

struct RegRef16{
	 int idx;
	 char loOrHiReg;
	 uint16_t* ptr;
};

struct RegRef32{
	 int idx;
	 uint32_t* ptr;
};

