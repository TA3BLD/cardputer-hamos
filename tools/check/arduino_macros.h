#define PI 3.1415926535897932384626433832795
#define HALF_PI 1.5707963267948966192313216916398
#define TWO_PI 6.283185307179586476925286766559
#define DEG_TO_RAD 0.017453292519943295769236907684886
#define RAD_TO_DEG 57.295779513082320876798154814105
#define EULER 2.718281828459045235360287471352
#define SERIAL 0x0
#define DISPLAY 0x1
#define LSBFIRST 0
#define MSBFIRST 1
#define CHANGE 0x03
#define FALLING 0x02
#define RISING 0x01
#define INPUT 0x01
#define PULLUP 0x04
#define INPUT_PULLUP 0x05
#define PULLDOWN 0x08
#define OPEN_DRAIN 0x10
#define ANALOG 0xC0
#define DEFAULT 1
#define EXTERNAL 0
#define DEC 10
#define HEX 16
#define OCT 8
#define BIN 2
#define SHIFT 0x80
#define KEY_LEFT_CTRL 0x80
#define KEY_LEFT_SHIFT 0x81
#define KEY_LEFT_ALT 0x82
#define KEY_FN 0xff
#define KEY_OPT 0x00
#define KEY_BACKSPACE 0x2a
#define KEY_TAB 0x2b
#define KEY_ENTER 0x28
#define BIT(nr) (1UL << (nr))
#define ESP_OK 0
#define ESP_FAIL -1
#define sq(x) ((x)*(x))
#define radians(deg) ((deg)*DEG_TO_RAD)
#define degrees(rad) ((rad)*RAD_TO_DEG)
#define constrain(amt,low,high) ((amt)<(low)?(low):((amt)>(high)?(high):(amt)))
#define bit(b) (1UL << (b))
#define lowByte(w) ((uint8_t) ((w) & 0xff))
#define highByte(w) ((uint8_t) ((w) >> 8))
#define word(...) makeWord(__VA_ARGS__)
#define interrupts() sei()
#define noInterrupts() cli()
#define bitRead(value, bit) (((value) >> (bit)) & 0x01)
#define bitSet(value, bit) ((value) |= (1UL << (bit)))
#define bitClear(value, bit) ((value) &= ~(1UL << (bit)))
#define bitToggle(value, bit) ((value) ^= (1UL << (bit)))
#define bitWrite(value, bit, bitvalue) ((bitvalue) ? bitSet(value, bit) : bitClear(value, bit))
#define _min(a,b) ((a)<(b)?(a):(b))
#define _max(a,b) ((a)>(b)?(a):(b))
#define _abs(x) ((x)>0?(x):-(x))
#define _round(x) ((x)>=0?(long)((x)+0.5):(long)((x)-0.5))
#define F(x) (x)
#define PSTR(s) (s)
#define PROGMEM
#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))
#define setbit(a,i) ((a)[(i)/8] |= 1<<((i)%8))
#define clrbit(a,i) ((a)[(i)/8] &= ~(1<<((i)%8)))
#define isset(a,i) ((a)[(i)/8] & (1<<((i)%8)))
#define isclr(a,i) (((a)[(i)/8] & (1<<((i)%8))) == 0)
#define howmany(x, y) (((x)+((y)-1))/(y))
#define roundup(x, y) ((((x)+((y)-1))/(y))*(y))
#define MHZ (1000000)
#define NOP() asm volatile ("nop")
#define digitalPinToInterrupt(p) (p)
#define FILE_READ "r"
#define B0 0
#define B1 1
#define B00 0
#define B01 1
#define B10 2
#define B11 3
#define ONLOW 0x04
#define ONHIGH 0x05
#define FUNCTION_1 0x00
#define ESP_LOGE(tag, ...)
#define ESP_LOGI(tag, ...)

