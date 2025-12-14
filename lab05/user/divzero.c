#include <stdio.h>
#include <ulib.h>

int zero;

// 通过内联汇编强制生成 DIV 指令，避免编译器把 1/zero 当成未定义行为而常量折叠
static inline int hardware_divide(int dividend, int divisor)
{
    int result;
    asm volatile("divw %0, %1, %2" : "=r"(result) : "r"(dividend), "r"(divisor));
    return result;
}

int
main(void)
{
    cprintf("value is %d.\n", hardware_divide(1, zero));
    panic("FAIL: T.T\n");
}
