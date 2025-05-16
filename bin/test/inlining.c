//static inline int add_i32(int a, int b)
//{
//	return a + b;
//}
//
//static inline int sub_i32(int a, int b)
//{
//	return a - b;
//}
//
//int main(void)
//{
//	return sub_i32(add_i32(2, 3), 5);
//}

int printf(const char* str, ...);

// Leaf function (no calls to other functions)
static inline int leaf_add(int a, int b)
{
    return a + b;
}

// Direct recursive function
static inline int factorial(int n)
{
    if (n <= 1) return 1;
    return n * factorial(n - 1);  // Direct recursion
}

// Indirect recursive functions (mutual recursion)
static inline int is_even(int n);
static inline int is_odd(int n);

static inline int is_even(int n)
{
    if (n == 0) {
        return 1;
    }

    return is_odd(n - 1);  // Indirect recursion
}

static inline int is_odd(int n)
{
    if (n == 0) {
        return 0;
    }

    return is_even(n - 1);  // Indirect recursion
}

int main()
{
    int a = 5, b = 7;

    // Test leaf function
    int sum = leaf_add(a, b);
    printf("Leaf add: %d + %d = %d\n", a, b, sum);

    // Test direct recursion
    int fact = factorial(5);
    printf("Factorial of 5: %d\n", fact);

    // Test indirect recursion
    int num = 6;
    if (is_even(num)) {
        printf("%d is even\n", num);
    } else {
        printf("%d is odd\n", num);
    }

    return 0;
}
