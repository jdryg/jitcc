typedef struct vec4i32
{
	int x, y, z, w;
} vec4i32;

typedef struct vec4i16
{
	short x, y, z, w;
} vec4i16;

typedef struct vec2x4i32
{
	vec4i32 a;
	vec4i32 b;
} vec2x4i32;

typedef struct context_t
{
	int arr[10];
	int idx;
} context_t;

int printf(const char* format, ...);

//int passthrough(int x)
//{
//	return x;
//}
//
//int addi(int a, int b)
//{
//	return a + b;
//}
//
//int maddi(int a, int b, int c)
//{
//	int x = a * b;
//	return x + c;
//}
//
//int sumiv(int* arr, int n)
//{
//	int sum = 0;
//	for (int i = 0; i < n; i++) {
//		sum += arr[i];
//	}
//	return sum;
//}
//
//int postinc(int x)
//{
//	return x++;
//}
//
//int preinc(int x)
//{
//	return ++x;
//}
//
//int factorial(int n)
//{
//	int result = 1;
//
//	if (n != 0) {
//		for (int c = 1; c <= n; ++c) {
//			result *= c;
//		}
//	}
//
//	return result;
//}
//
//int test_if(int a, int b)
//{
//	if (!(a > 5)) {
//		return ~(b + 1);
//	}
//	return -((b & 5) | 8);
//}
//
//int test_shifts(int a)
//{
//	return (a >> 3) << 8;
//}
//
//int test_logical_and(int a, int b)
//{
//	if (a > 0 && b > 0) {
//		return 1;
//	}
//	return 0;
//}
//
//int test_logical_or(int a, int b)
//{
//	if (a > 0 || b > 0) {
//		return 1;
//	}
//	return 0;
//}
//
//int test_conditional(int a, int b, int c)
//{
//	return a >= 0 ? b : c;
//}
//
//int test_if_else(int a, int b)
//{
//	if (a > 5) {
//		return b + 1;
//	} else if (a < -5) {
//		return b - 1;
//	}
//
//	return 0;
//}
//
//int vec4i32_getX(vec4i32 a)
//{
//	return a.x;
//}
//
//short vec4i16_getX(vec4i16 a)
//{
//	return a.x;
//}
//
//vec4i32 vec4i32_init(void)
//{
//	return (vec4i32){ 
//		.x = 1, 
//		.y = 2, 
//		.z = 3, 
//		.w = 4 
//	};
//}
//
//vec4i16 vec4i16_init(void)
//{
//	return (vec4i16){
//		.x = 1,
//		.y = 2,
////		.z = 3,
//		.w = 4
//	};
//}
//
//int test_complex_struct(vec2x4i32* ptr)
//{
//	return ptr->a.x + ptr->b.x;
//}
//
//int test_addr(int* arr, int n)
//{
//	int* ptr = &arr[n];
//	return *ptr;
//}

//int test_common_expr(int a, int b, int c, int d)
//{
//	int x = ((a + b) * c) + ((a + b) / d);
//	a = 5;
//	return x + (a + b);
//}

//int test_shortcircuit(int* ptr)
//{
//	if ((ptr && ptr[5]) || (ptr[0] != ptr[1])) {
//		return 5;
//	}
//
//	return 0;
//}
//
//int test_do(int n)
//{
//	int a = 0;
//
//	do {
//		a += 10;
//		if (a > 50) {
//			break;
//		}
//	} while (n-- > 0);
//
//	return a;
//}
//
//int test_goto_lbl(int a, int b)
//{
//	int x;
//
//	if (a == 0) {
//		goto err;
//	}
//
//	return b;
//err:
//	x = a + b;
//	return x;
//}
//
//int test_switch(int* arr, int x)
//{
//	int y = 0;
//	switch (x) {
//	case 0:
//		y = 1;
//		break;
//	case 1:
//		y = 10;
//		break;
//	case 10:
//		y = 100;
//		break;
//	default:
//		break;
//	}
//
//	return arr[y];
//}
//
//static inline int add(int a, int b)
//{
//	return a + b;
//}
//
//static inline int mul(int a, int b)
//{
//	return a * b;
//}
//
//int madd(int a, int b, int c)
//{
//	return add(mul(a, b), c);
//}
//
//int test_funcptr(int (*f)(int a, int b))
//{
//	return f(1, 2);
//}
//
//static int g_GlobalCounter = 0;
//
//int count(void)
//{
//	return ++g_GlobalCounter;
//}
//
//void test__func__(void)
//{
//	printf("Function: %s\n", __func__);
//}
//
//int main(int argc, char** argv)
//{
//	printf("Hello world!\n");
//
//	return 0;
//}

//typedef struct api_t
//{
//	int (*printf)(const char* fmt, ...);
//} api_t;
//
////int factorial(int n)
////{
////	if (n == 0) {
////		return 1;
////	}
////
////	int result = 1;
////	for (int c = 2; c <= n; ++c) {
////		result *= c;
////	}
////	
////	return result;
////}
//
//int n_choose_r(int n, int k)
//{
//	return factorial(n) / (factorial(k) * factorial(n - k));
//}
//
//int plugin_main(const api_t* api)
//{
//	api->printf("n_choose_r(5, 2): %d\n", n_choose_r(5, 2));
//	return 0;
//}
//
//vec2x4i32 vec2x4i32_init(void)
//{
//	return (vec2x4i32) { .a.x = 1 };
//}

//static const vec4i32 g_Vec4i32_zero = { 0, 0, 0, 0 };
//const int g_Arr[5] = { 1, 2, 3, 4, 5 };
//
//int test_memzero_arr(void)
//{ 
//	int arr[5] = { 1, 1, 2 };
//	return arr[2];
//}
// 
//int test_arr_in_struct(void)
//{
//	context_t ctx = { 0 };
//	return ctx.arr[ctx.idx];
//}
//
//int* test_getArrElemPtr(context_t* ctx, int idx)
//{
//	return &ctx->arr[idx];
//}
//
//int test_2darr(int i, int j)
//{
//	int arr[2][2] = { { 1, 2 }, { 3, 4 } };
//	return arr[i][j];
//}

////vec4i32 vec4i32_init(int x, int y, int z, int w)
////{
////    return (vec4i32){ .x = x, .y = y, .z = z, .w = w };
////}
//
//vec4i32 vec4i32_add(vec4i32 a, vec4i32 b)
//{
//    return (vec4i32){
//        .x = a.x + b.x,
//        .y = a.y + b.y,
//        .z = a.z + b.z,
//        .w = a.w + b.w
//    };
//}
//
//int vec4i32_dot(vec4i32 a, vec4i32 b)
//{
//    return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
//}
//
//int vec4i32_ab_dot_bc(vec4i32 a, vec4i32 b, vec4i32 c)
//{
//    return vec4i32_dot(vec4i32_add(a, b), vec4i32_add(b, c));
//}
//
//typedef union generic_value_t
//{
//    short i16;
//    char i8;
//    int i32;
//    float f32;
//    double f64;
//    unsigned long long i64;
//} generic_value_t;
//
//generic_value_t makeChar(char ch)
//{
//    return (generic_value_t){ .i8 = ch };
//}
//
//generic_value_t makeInt(int i)
//{
//    return (generic_value_t){ .i32 = i };
//}
//
//int test_union(void)
//{
//    generic_value_t gv = makeChar(48);
//    return gv.i32;
//}

//int test_multiarg(int a, int b, int c, int d, int e, int f, int g)
//{
//	int x = a * b;
//	int y = c + d;
//	x *= y;
//	y = (e / f) * g;
//	return y - x;
//}

int div_sub(int a, int b)
{
	return (a / b) - b;
}
