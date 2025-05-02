typedef char int8_t;
typedef unsigned char uint8_t;
typedef short int16_t;
typedef unsigned short uint16_t;
typedef int int32_t;
typedef unsigned int uint32_t;
typedef long long int int64_t;
typedef unsigned long long int uint64_t;

int8_t   test_addi8(int8_t a, int8_t b)      { return a + b; }
int16_t  test_addi16(int16_t a, int16_t b)   { return a + b; }
int32_t  test_addi32(int32_t a, int32_t b)   { return a + b; }
int64_t  test_addi64(int64_t a, int64_t b)   { return a + b; }
uint8_t  test_addu8(uint8_t a, uint8_t b)    { return a + b; }
uint16_t test_addu16(uint16_t a, uint16_t b) { return a + b; }
uint32_t test_addu32(uint32_t a, uint32_t b) { return a + b; }
uint64_t test_addu64(uint64_t a, uint64_t b) { return a + b; }
float    test_addf32(float a, float b)       { return a + b; }
double   test_addf64(double a, double b)     { return a + b; }

int8_t   test_subi8(int8_t a, int8_t b)      { return a - b; }
int16_t  test_subi16(int16_t a, int16_t b)   { return a - b; }
int32_t  test_subi32(int32_t a, int32_t b)   { return a - b; }
int64_t  test_subi64(int64_t a, int64_t b)   { return a - b; }
uint8_t  test_subu8(uint8_t a, uint8_t b)    { return a - b; }
uint16_t test_subu16(uint16_t a, uint16_t b) { return a - b; }
uint32_t test_subu32(uint32_t a, uint32_t b) { return a - b; }
uint64_t test_subu64(uint64_t a, uint64_t b) { return a - b; }
float    test_subf32(float a, float b)       { return a - b; }
double   test_subf64(double a, double b)     { return a - b; }

int8_t   test_muli8(int8_t a, int8_t b)      { return a * b; }
int16_t  test_muli16(int16_t a, int16_t b)   { return a * b; }
int32_t  test_muli32(int32_t a, int32_t b)   { return a * b; }
int64_t  test_muli64(int64_t a, int64_t b)   { return a * b; }
uint8_t  test_mulu8(uint8_t a, uint8_t b)    { return a * b; }
uint16_t test_mulu16(uint16_t a, uint16_t b) { return a * b; }
uint32_t test_mulu32(uint32_t a, uint32_t b) { return a * b; }
uint64_t test_mulu64(uint64_t a, uint64_t b) { return a * b; }
float    test_mulf32(float a, float b)       { return a * b; }
double   test_mulf64(double a, double b)     { return a * b; }

int8_t   test_divi8(int8_t a, int8_t b)      { return a / b; }
int16_t  test_divi16(int16_t a, int16_t b)   { return a / b; }
int32_t  test_divi32(int32_t a, int32_t b)   { return a / b; }
int64_t  test_divi64(int64_t a, int64_t b)   { return a / b; }
uint8_t  test_divu8(uint8_t a, uint8_t b)    { return a / b; }
uint16_t test_divu16(uint16_t a, uint16_t b) { return a / b; }
uint32_t test_divu32(uint32_t a, uint32_t b) { return a / b; }
uint64_t test_divu64(uint64_t a, uint64_t b) { return a / b; }
float    test_divf32(float a, float b)       { return a / b; }
double   test_divf64(double a, double b)     { return a / b; }

int8_t   test_modi8(int8_t a, int8_t b)      { return a % b; }
int16_t  test_modi16(int16_t a, int16_t b)   { return a % b; }
int32_t  test_modi32(int32_t a, int32_t b)   { return a % b; }
int64_t  test_modi64(int64_t a, int64_t b)   { return a % b; }
uint8_t  test_modu8(uint8_t a, uint8_t b)    { return a % b; }
uint16_t test_modu16(uint16_t a, uint16_t b) { return a % b; }
uint32_t test_modu32(uint32_t a, uint32_t b) { return a % b; }
uint64_t test_modu64(uint64_t a, uint64_t b) { return a % b; }

int8_t   test_andi8(int8_t a, int8_t b)      { return a & b; }
int16_t  test_andi16(int16_t a, int16_t b)   { return a & b; }
int32_t  test_andi32(int32_t a, int32_t b)   { return a & b; }
int64_t  test_andi64(int64_t a, int64_t b)   { return a & b; }
uint8_t  test_andu8(uint8_t a, uint8_t b)    { return a & b; }
uint16_t test_andu16(uint16_t a, uint16_t b) { return a & b; }
uint32_t test_andu32(uint32_t a, uint32_t b) { return a & b; }
uint64_t test_andu64(uint64_t a, uint64_t b) { return a & b; }

int8_t   test_ori8(int8_t a, int8_t b)       { return a | b; }
int16_t  test_ori16(int16_t a, int16_t b)    { return a | b; }
int32_t  test_ori32(int32_t a, int32_t b)    { return a | b; }
int64_t  test_ori64(int64_t a, int64_t b)    { return a | b; }
uint8_t  test_oru8(uint8_t a, uint8_t b)     { return a | b; }
uint16_t test_oru16(uint16_t a, uint16_t b)  { return a | b; }
uint32_t test_oru32(uint32_t a, uint32_t b)  { return a | b; }
uint64_t test_oru64(uint64_t a, uint64_t b)  { return a | b; }

int8_t   test_xori8(int8_t a, int8_t b)      { return a ^ b; }
int16_t  test_xori16(int16_t a, int16_t b)   { return a ^ b; }
int32_t  test_xori32(int32_t a, int32_t b)   { return a ^ b; }
int64_t  test_xori64(int64_t a, int64_t b)   { return a ^ b; }
uint8_t  test_xoru8(uint8_t a, uint8_t b)    { return a ^ b; }
uint16_t test_xoru16(uint16_t a, uint16_t b) { return a ^ b; }
uint32_t test_xoru32(uint32_t a, uint32_t b) { return a ^ b; }
uint64_t test_xoru64(uint64_t a, uint64_t b) { return a ^ b; }

uint64_t test_sar_const(uint64_t a)          { return a >> 4; };
uint64_t test_sal_const(uint64_t a)          { return a << 4; };
int64_t  test_shr_const(int64_t a)           { return a >> 4; };
int64_t  test_shl_const(int64_t a)           { return a << 4; };

uint64_t test_sar(uint64_t a, uint8_t shift) { return a >> shift; };
uint64_t test_sal(uint64_t a, uint8_t shift) { return a << shift; };
int64_t  test_shr(int64_t a, uint8_t shift)  { return a >> shift; };
int64_t  test_shl(int64_t a, uint8_t shift)  { return a << shift; };
