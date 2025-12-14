<center>
<h1>OS-lab05实验报告</h1>
</center>

## 练习一:加载应用程序并执行(需要编码)
下面从ucore启动到执行第一个应用程序的过程展开分析。

如lab04中操作一样，当我们启动ucore后，会自动创建`idle`内核进程和`init_main`内核进程.而`init_main`线程会在进程内创建`user_main`内核进程。而后者会使用宏`KERNEL_EXECVE`把内核镜像的用户程序作为参数，通过软中断触发`SYS_exec`系统调用，实现内核主动执行用户程序。

这里定义了多个宏:
```cpp
#define __KERNEL_EXECVE(name, binary, size) ({                          \
            cprintf("kernel_execve: pid = %d, name = \"%s\".\n",        \
                    current->pid, name);                                \
            kernel_execve(name, binary, (size_t)(size));                \
        })

#define KERNEL_EXECVE(x) ({                                             \
            extern unsigned char _binary_obj___user_##x##_out_start[],  \
                _binary_obj___user_##x##_out_size[];                    \
            __KERNEL_EXECVE(#x, _binary_obj___user_##x##_out_start,     \
                            _binary_obj___user_##x##_out_size);         \
        })

#define __KERNEL_EXECVE2(x, xstart, xsize) ({                           \
            extern unsigned char xstart[], xsize[];                     \
            __KERNEL_EXECVE(#x, xstart, (size_t)xsize);                 \
        })

#define KERNEL_EXECVE2(x, xstart, xsize)        __KERNEL_EXECVE2(x, xstart, xsize)
```

其中`_binary_obj___user_##x##_out_start`和`_binary_obj___user_##x##_out_size`都是编译的时候自动生成的符号。这里`##x##`是按照 C 语言宏的语法，会直接把 x 的变量名代替进去。

所以这里(`user_main`进程)在做的事情就是调用并执行函数:`kernel_execve("exit", _binary_obj___user_exit_out_start,_binary_obj___user_exit_out_size)`.这里实际上就是加载了存储在这个位置的程序`exit`并在`user_main`进程中开始执行。这时`user_main`就从内核进程变成了用户进程。

回到上文，我们在`kernel_execve`中通过软中断(`: "i"(SYS_exec), "m"(name), "m"(len), "m"(binary), "m"(size)`)触发了`SYS_exec`系统调用,此时会转到函数`do_execve`中。