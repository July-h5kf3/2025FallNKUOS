## 练习1：完成读文件操作的实现(需要编码)

### 1.实验任务说明

首先了解打开文件的处理流程，然后参考本实验后续的文件读写操作的过程分析，填写在 kern/fs/sfs/sfs_inode.c中 的sfs_io_nolock()函数，实现读文件中数据的代码。

### 2.读文件整体执行流程回顾

首先我们从用户程序执行read函数开始，向下逐层进行分析。当我们在用户程序调用下面这个函数时：

```
read(fd, buf, len);
```

主要的调用链应该是这个样子的：

```
用户态:
  read(fd, user_buf, len)
    ↓
syscall 边界:
  sys_read(arg[])                 // 只是拆参数
    ↓
内核通用文件接口层:
  sysfile_read(fd, user_buf, len) // 分块读 + copy_to_user
    ↓
文件抽象层（按 fd 找 file）:
  file_read(fd, kbuf, alen, &alen_out)
    ↓
VFS inode 多态分发:
  vop_read(file->node, iob)
    ↓
SFS 文件系统层:
  sfs_read(node, iob)             // wrapper
    ↓
  sfs_io(node, iob, write=0)      // 加锁版
    ↓
  sfs_io_nolock(...)              // 真正读文件内容（按 block 拆）
    ↓
块映射 + 块读:
  sfs_bmap_load_nolock(...)       // logical block -> disk block
  sfs_rbuf / sfs_rblock           // 读部分块 / 读整块(多块)
    ↓
设备层:
  dop_io -> disk0_io -> ide_read_secs  // 真的从磁盘读扇区
```

我们从最开始的read进行说明，一路建立函数调用的链条，直到进入我们的`sfs_io_nolock()`函数：

- **从用户态函数到系统调用：`read` / `sys_read`：**

  先进入通用文件访问接口层的处理流程，即进一步调用如下用户态函数：`read->sys_read->syscall`，从而引起系统调用进入到内核态。到了内核态以后，通过中断处理例程，会调用到`sys_read`内核函数，并进一步调用`sysfile_read`内核函数，进入到文件系统抽象层处理流程，完成进一步读文件的操作。

  ```c++
  //用户态调用read函数，例如read(fd, data, len)
  //即为从文件标识符为fd的文件中读取len个字节的数据于data中
  int read(int fd, void *base, size_t len) {
      //这里的sys_read实际是用户态的函数，其中存在syscall汇编指令，通过一系列流程进入内核态
      return sys_read(fd, base, len);
  }
  
  
  //这是内核态的sys_read函数：只是拆参数，然后进入内核文件接口层sysfile_read函数
  static int sys_read(uint64_t arg[]) {
      int fd = (int)arg[0];
      void *base = (void *)arg[1];
      size_t len = (size_t)arg[2];
      return sysfile_read(fd, base, len);
  }
  ```

- **`sysfile_read(fd, user_base, len)` 做了什么？**

  这是内核读文件的总控调度者，它的逻辑非常典型：循环分块读 + 拷贝回用户空间。核心结构是：

  - 首先进行参数合法性检查，读取的字节数len是否为零，文件标识符fd是否合法

    ```c++
    if (len == 0) {
        return 0;
    }
    if (!file_testfd(fd, 1, 0)) {
        return -E_INVAL;
    }
    ```

  - 分配一个内核缓冲区 `buffer = kmalloc(IOBUF_SIZE)`，后面调用函数`file_read`实质上是从文件标识符为fd的文件中读取一定数目的字节于内核buffer中：

    ```c++
    void *buffer;
    if ((buffer = kmalloc(IOBUF_SIZE)) == NULL) {
        return -E_NO_MEM;
    }
    ```

  - 然后进入`while (len != 0)` 的循环：

    ```c++
    int ret = 0;
    size_t copied = 0, alen;
    while (len != 0) 
    {
        //先检查剩余部分大小len，若其小于4096字节(IOBUF_SIZE)，则只读取剩余部分的大小
        //这个逻辑相当于：alen = min(IOBUF_SIZE, len)
        if ((alen = IOBUF_SIZE) > len) 
        {
            alen = len;
        }
        
        //从文件读到内核buffer中
        ret = file_read(fd, buffer, alen, &alen);
        if (alen != 0) {
            lock_mm(mm);
            {
                //执行函数copy_to_user，从内核buffer拷回到用户base中
                //由于copy_to_user访问的是用户地址空间，需要防止并发/地址空间变化造成问题
                //所以需要进行加锁和解锁操作
                if (copy_to_user(mm, base, buffer, alen)) {
                    assert(len >= alen);
                    //然后进行更新操作
                    base += alen, len -= alen, copied += alen;
                }
                else if (ret == 0) {
                    ret = -E_INVAL;
                }
            }
            unlock_mm(mm);
        }
        if (ret != 0 || alen == 0) {
            goto out;
        }
    }
    ```

    首先，进入每次循环中，先检查剩余部分大小，若其小于4096字节，则只读取剩余部分的大小，获取实际上要读取的字节数`alen = min(IOBUF_SIZE, len)`；然后执行函数`file_read(fd, buffer, alen, &alen)`，真正从文件读到内核buffer中；接着执行函数`copy_to_user(mm, user_base, buffer, alen)` ，从内核buffer拷回到用户base中；然后更新 `user_base += alen`, `len -= alen`，`copied += alen`。

    在循环结束后，我们就像用户态空间中写入了len个字节的数据，每个循环最多只会读取一页的数据进入到内核态的buffer中，然后从内核buffer拷贝回用户态的base中，这样的好处是内存压力更小，也容易处理边界/失败回滚。

  - 释放 `kfree(buffer)`
  - 返回：如果 `copied != 0` 返回copied，否则返回错误码ret

- **`file_read(fd, base, len, &copied)` 做了什么？**

  这个函数是读文件的核心函数。函数有4个参数，`fd`是文件描述符，`base`是缓存的基地址，`len`是要读取的长度，`copied_store`存放实际读取的长度。

  函数首先调用`fd2file`函数找到对应的`file`结构，并检查是否可读：

  需要注意的是：对于每个进程而言，当它打开某个文件时都会创建一个file结构体，但是其中inode类型的指针node指向的都是相同的文件inode

  ```c++
  int ret;
  struct file *file;
  *copied_store = 0;
  
  //执行完该函数后，如果fd为合法的文件描述符，那么file为file->fd = fd的那个file结构
  //相当于是从该进程的进程控制块proc_struct中的files_struct.fd_array中根据fd来寻找打开的file
  if ((ret = fd2file(fd, &file)) != 0) {
      return ret;
  }
  
  //如果不可读直接返回错误
  if (!file->readable) {
      return -E_INVAL;
  }
  ```

  调用`filemap_acquire`函数使这个文件的计数加1，也就是给这个 `file` 做引用/加锁，保证并发安全调用`vop_read`函数将文件内容读到`iob`中。调整文件指针偏移量`pos`的值，使其向后移动实际读到的字节数`iobuf_used(iob)`。最后调用`filemap_release`函数使打开这个文件的计数减1，若打开计数为0，则释放`file`：

  ```c++
  fd_array_acquire(file);
  
  struct iobuf __iob, *iob = iobuf_init(&__iob, base, len, file->pos);
  //这个iob可以告诉我们已经在读到了多少数据，从buffer+copied继续拷贝数据
  
  ret = vop_read(file->node, iob);
  size_t copied = iobuf_used(iob);
  if (file->status == FD_OPENED) {
      //file_read的上层函数sysfile_read执行循环，每次循环结束后file->pos+=alen
      //我们通过file->pos知道我们该继续从文件的哪个位置继续读数据
      file->pos += copied;
  }
  *copied_store = copied;
  
  fd_array_release(file);
  ```

  简单来说就是`file_read(fd, base, len, &copied)` 做的事是：把一次从fd文件读取len字节的请求，封装成一个`iobuf`，交给VFS/文件系统去“推进”，最后通过`iobuf`的变化得知： 实际读了多少字节，以及文件偏移推进到哪里`base+copied`。`iobuf`的作用在于告诉我们 “我要从文件的哪个位置，向哪块内存，读多少数据，现在还剩多少没读”

- **`vop_read` → `sfs_read`：多态分发怎么发生？**

  在创建一个文件的时候，我们就会根据文件的类型设置`in_ops`，此时就是将`in_ops` =&sfs_inode_ops。这样当我们调用vop_read时，会根据该文件file中node指向的inode结构体中in_type确定绑定的是什么。于是上面对vop_read函数的调用会自动进入sfs_read函数的逻辑中：

  ```c++
  struct inode {
      union {                                 
          //包含不同文件系统特定inode信息的union成员变量
          struct device __device_info;          //设备文件系统内存inode信息
          struct sfs_inode __sfs_inode_info;    //SFS文件系统内存inode信息
      } in_info;
      enum {
          inode_type_device_info = 0x1234,
          inode_type_sfs_inode_info,
      } in_type;                          //此inode所属文件系统类型
      atomic_t ref_count;                 //此inode的引用计数
      atomic_t open_count;                //打开此inode对应文件的个数
      struct fs *in_fs;                   //抽象的文件系统，包含访问文件系统的函数指针
      const struct inode_ops *in_ops;     //抽象的inode操作，包含访问inode的函数指针
  }
  ```

- 从`sfs_read(node, iob)` 到`sfs_io(node, iob, write=0)`：

  ```c++
  static int sfs_read(struct inode *node, struct iobuf *iob) {
      //0表示读，1表示写
      return sfs_io(node, iob, 0);
  }
  ```

  由于inode中的`sfs_inode __sfs_inode_info`包含有sfs_node的信息，我们可以通过这个文件的inode找到其`sys_inode`，然后将其上锁避免有其他进程在该进程读取的时候对其进行修改，然后执行：

  ```c++
  lock_sin(sin);
  {
      size_t alen = iob->io_resid;
      //调用函数sfs_io_nolock
      ret = sfs_io_nolock(sfs, sin, iob->io_base, iob->io_offset, &alen, write);
      if (alen != 0) {
          iobuf_skip(iob, alen);
      }
  }
  unlock_sin(sin);
  ```

通过这一系列的函数调用，我们终于进入到了函数`sfs_io_nolock()`，接下来我们对该函数进行实现

### 3.`sfs_io_nolock()` 函数

函数原型如下：

```c++
static int
sfs_io_nolock(struct sfs_fs *sfs,
              struct sfs_inode *sin,
              void *buf,
              off_t offset,
              size_t *alenp,
              bool write)
```

各个参数的含义为：

| 参数     | 含义                                |
| -------- | ----------------------------------- |
| `sfs`    | SFS文件系统实例                     |
| `sin`    | 当前文件的SFS inode（内存态）       |
| `buf`    | 内核缓冲区地址                      |
| `offset` | 文件读起始偏移                      |
| `alenp`  | 请求长度指针，返回实际读长度        |
| `write`  | 是否为写操作（本实验中为0，表示读） |

我们知道，在SFS中，文件内容以4KB的block为单位存储，因此读文件必须根据 `offset` 计算起始block号和 block内偏移，然后需要将 `[offset, offset+len)` 区间拆分为三部分：

- **首块的非对齐部分**
- **中间的整块部分**
- **尾块的非对齐部分**

然后我们对每一部分调用合适的block读接口，并累计实际读取字节数并返回。也就是该函数的实际功能在于：将从`sin`这个`sys_inode`代表的文件中，以`文件开头+offset`为起始地址读取len个字节的数据到buf中这件事情，变成：读取磁盘的若干个数据块，并且处理好偏移、边界、块对齐问题。

具体代码实现如下：

(1)参数合法性与读边界裁剪

```c++
off_t endpos = offset + *alenp;
*alenp = 0;

//防止负偏移、越界访问
if (offset < 0 || offset >= SFS_MAX_FILE_SIZE || offset > endpos) {
    return -E_INVAL;
}
if (offset == endpos) {
    //说明*alenp为空，属于空读
    return 0;
}
//限制读操作不超过文件系统最大文件大小
if (endpos > SFS_MAX_FILE_SIZE) {
    endpos = SFS_MAX_FILE_SIZE;
}
```

对于读操作，还需保证不越过文件末尾（EOF）：

```c++
if (!write) {
    if (offset >= din->size) {
        return 0;
    }
    if (endpos > din->size) {
        endpos = din->size;
    }
}
```

(2)根据读写类型选择操作函数

```c++
if (write) {
    sfs_buf_op = sfs_wbuf;
    sfs_block_op = sfs_wblock;
}
else {
    //如果是写操作：
    //其中，buf_op用于处理块内部分数据（非对齐），block_op用于处理整块或连续块
    sfs_buf_op = sfs_rbuf;
    sfs_block_op = sfs_rblock;
}
```

(3)计算起始block与块内偏移

```c++
uint32_t blkno = offset / SFS_BLKSIZE; //通过offset获取起始块的块号
uint32_t nblks = endpos / SFS_BLKSIZE - blkno; //计算要读几个完整块(包括起始块)
blkoff = offset % SFS_BLKSIZE;//从起始块的多少字节开始进行读操作
```

(4)处理首块的非对齐部分

```c++
if (blkoff != 0) {
    //如果完整块为0说明只用读取起始块中的部分数据，否则就需要读取endpos - offset字节数据
    size = (nblks != 0) ? (SFS_BLKSIZE - blkoff) : (endpos - offset);
    //将起始块中非对齐部分的字节读取到buffer中
    sfs_bmap_load_nolock(sfs, sin, blkno, &ino);
    sfs_buf_op(sfs, buffer, size, ino, blkoff);
    alen += size;
    buffer += size;
    blkno++;
    nblks--;
}
```

(5)处理中间的整块数据

```c++
while (nblks != 0) {
    //每次读一个完整block
    sfs_bmap_load_nolock(sfs, sin, blkno, &ino);
    sfs_block_op(sfs, buffer, ino, 1);
    blkno++;
    nblks--;
    alen += SFS_BLKSIZE;
    buffer += SFS_BLKSIZE;
}
```

(6)处理尾块的非对齐部分

```c++
size = endpos % SFS_BLKSIZE;
if (size != 0) {
    sfs_bmap_load_nolock(sfs, sin, blkno, &ino);
    sfs_buf_op(sfs, buffer, size, ino, 0);
    alen += size;
}
```

(4)返回实际读长度并更新inode状态

```c++
//这个其实是为写操作准备的，但是我们需要预留出来：
*alenp = alen;
if (offset + alen > sin->din->size) {
    sin->din->size = offset + alen;
    sin->dirty = 1;
}
return ret;
```



## 练习2: 完成基于文件系统的执行程序机制的实现（需要编码）

### 1. 实验目标（练习2）

​       这一部分我们要在 `ucore/ucore-rv` 的内核中实现**基于文件系统的执行程序机制**：使得用户态终端能够根据输入的程序名，从 `SimpleFS`（`sfs`）中读取 `ELF` 可执行文件，将其装载到进程的用户虚拟地址空间，并正确设置用户栈与参数，从而成功执行用户程序（如 `hello`、`exit` 等）。

完成这一部分后，我们通过`shell`执行外部程序时应当遵循以下流程：

1. `shell` 调用 `fork()` 创建子进程，进入内核 `do_fork` 完成子进程结构建立；
2. 子进程调用 `execve(path, argc, argv)`，进入内核 `do_execve`；
3. `do_execve` 通过 `sysfile_open(argv[0], O_RDONLY)` 打开可执行文件获得文件描述符 `fd`，并释放旧地址空间；
4. 调用 `load_icode(fd, argc, kargv)`：从文件系统读取 `ELF`，建立新的 `mm/pgdir`，映射并装载各段，构造用户栈并写入参数；
5. 设置 `trapframe`（`epc=ELF entry`，`sp=stacktop`，`a0/a1=argc/argv`），内核返回用户态后从新程序入口开始执行。

### 2. 代码实现

#### 2.1 do_fork 支持 exec 前置条件（文件表继承）

​       在这次实验中，我们要保证`fork`出来的子进程必须继承父进程的 `filesp`/文件描述符表，否则 `exec` 阶段无法通过 `sysfile_` 访问可执行文件，`shell` 的 `I/O` 也可能异常。

​       因此在 `do_fork` 中，除完成实验`4`的基本进程创建流程（`alloc_proc`、`setup_kstack`、`copy_mm`、`copy_thread`）以及实验`5`的父子关系维护（`proc->parent=current`、`set_links`）外，还要需要额外保证子进程拥有正确的文件系统上下文，所以我们就在 `copy_mm` 之后增加 `copy_files(clone_flags, proc)`，使子进程继承父进程的文件描述符表 `filesp`。

```c
if (copy_files(clone_flags, proc) != 0)
    { // for LAB8
        goto bad_fork_cleanup_mm;
    }
bad_fork_cleanup_mm:
    if (proc->mm != NULL && mm_count_dec(proc->mm) == 0) {
        exit_mmap(proc->mm);
        put_pgdir(proc->mm);
        mm_destroy(proc->mm);
    }
```

该步骤保证子进程在随后执行 `do_execve/load_icode` 时，能够通过 `sysfile_open/seek/read` 正常访问 `sfs` 中的 `ELF` 文件并完成装载，同时也维持了 `shell` 的标准输入输出等文件状态的一致性。

#### 2.2 do_execve：从文件系统打开 ELF 并替换旧地址空间

- **参数安全拷贝（避免用户指针失效）**

  因为`do_execve` 接收的 `name` 和 `argv` 均为用户态指针，为了避免 `exec` 过程中释放旧 `mm` 后用户指针失效，所以我们要先在加锁条件下使用 `copy_string` 将进程名复制到内核缓冲区 `local_name`，并通过 `copy_kargv` 将参数字符串复制到内核，形成内核可用的 `kargv[]`，完成之后再释放 `mm` 锁进入后续流程。

  ```c
  struct mm_struct *mm = current->mm;
  lock_mm(mm);
  if (name == NULL) {
      snprintf(local_name, sizeof(local_name), "<null> %d", current->pid);
  } else {
      if (!copy_string(mm, local_name, name, sizeof(local_name))) {
          unlock_mm(mm);
          return -E_INVAL;
      }
  }
  if ((ret = copy_kargv(mm, argc, kargv, argv)) != 0) {
      unlock_mm(mm);
      return ret;
  }
  path = argv[0];    // 这里用用户态指针
  unlock_mm(mm);
  ```

- **路径选择**

  另外我们的路径选择中 `path = argv[0]` 使用的是**用户态指针**，这是因为 `sysfile_open` 对其第一个参数会进行用户指针合法性检查，并在内核中执行从用户空间拷贝路径的过程，具体的调用顺序为：`copy_kargv -> path=argv[0] -> unlock_mm -> files_closeall -> sysfile_open(path, ...)`，其中 `sysfile_open` 发生在旧 `mm` 释放之前，因此 `argv[0]` 仍指向有效的用户地址空间，不会出现悬空指针，但是如果改用 `kargv[0]`（内核地址）反而会绕过/触发用户指针检查失败，导致 `open` 无法成功。

  ```c
  files_closeall(current->filesp);
  int fd;
  if ((ret = fd = sysfile_open(path, O_RDONLY)) < 0) {
      goto execve_exit;
  }
  ```

- **打开 ELF 文件 + 释放旧 mm 的正确顺序**

  在`do_execve`中，我们首先调用 `sysfile_open(path, O_RDONLY)` 在 `sfs` 中打开可执行文件获得 `fd`，之后若进程原本存在旧的 `mm`，则先切换到内核页表 `boot_pgdir`（`lsatp(boot_pgdir_pa)`）并刷新 `TLB`，随后在引用计数归零时依次执行 `exit_mmap/put_pgdir/mm_destroy` 释放旧地址空间，并将 `current->mm` 置空，这样可以避免在仍使用旧用户页表时释放映射导致访问异常。

  ```c
  if (mm != NULL) {
      lsatp(boot_pgdir_pa);
      flush_tlb();
  
      if (mm_count_dec(mm) == 0) {
          exit_mmap(mm);
          put_pgdir(mm);
          mm_destroy(mm);
      }
      current->mm = NULL;
  }
  ```

- **调用 load_icode 完成装载**

  完成文件打开与旧地址空间清理后，`do_execve` 将调用 `load_icode(fd, argc, kargv)` 装载新程序，成功后关闭 `fd`，释放 `kargv`，并用 `set_proc_name` 更新进程名；

  ```c
  ret = -E_NO_MEM;
  if ((ret = load_icode(fd, argc, kargv)) != 0) {
      sysfile_close(fd);
      goto execve_exit;
  }
  sysfile_close(fd);
  
  put_kargv(argc, kargv);
  set_proc_name(current, local_name);
  return 0;
  ```

  若装载失败则调用 `do_exit(ret)` 退出当前进程。

  ```c
  execve_exit:
      put_kargv(argc, kargv);
      do_exit(ret);
      panic("already exit: %e.\n", ret);
  ```

#### 2.3 load_icode：通过 fd 读取 ELF，建立新用户态环境（核心）

- **创建 mm 与 pgdir**

  首先我们进行`load_icode` 的前置条件是 `current->mm == NULL`，函数会首先调用 `mm_create()` 创建新的 `mm_struct`，随后调用 `setup_pgdir(mm)` 建立新的页表 `pgdir`，使该页表包含内核映射，同时用户态部分为空，为后续段映射准备。

  ```c
  if (current->mm != NULL) {
      panic("load_icode: current->mm must be empty.\n");
  }
  
  struct mm_struct *mm;
  if ((mm = mm_create()) == NULL) {
      goto bad_mm;
  }
  
  if (setup_pgdir(mm) != 0) {
      goto bad_pgdir_cleanup_mm;
  }
  ```

- **读取 ELF Header 与 Program Header（来自文件系统）**

  这里和实验`5`从内存指针读取 `ELF` 不同，而是通过文件描述符 `fd` 从 `sfs` 读取 `ELF` 内容：先使用 `load_icode_read(fd, &elf, sizeof(elf), 0)` 读取 `ELF header`，并根据 `elf.e_phoff/elf.e_phnum` 分配 `phdrs` 并读取 `program header` 表，最后还要检查 `elf.e_magic` 确认 `ELF` 的合法性。

  ```c
  struct elfhdr elf;
  if ((ret = load_icode_read(fd, &elf, sizeof(elf), 0)) != 0) {
      goto bad_elf_cleanup_pgdir;
  }
  
  if ((phdrs = kmalloc(sizeof(struct proghdr) * elf.e_phnum)) == NULL) {
      ret = -E_NO_MEM;
      goto bad_elf_cleanup_pgdir;
  }
  
  if ((ret = load_icode_read(fd, phdrs,
          sizeof(struct proghdr) * elf.e_phnum, elf.e_phoff)) != 0) {
      goto bad_free_phdrs;
  }
  
  if (elf.e_magic != ELF_MAGIC) {
      ret = -E_INVAL_ELF;
      goto bad_free_phdrs;
  }
  ```

- **映射并装载 PT_LOAD 段：TEXT/DATA 拷贝 + BSS 补零**

  然后开始遍历所有的 `program header`，对 `p_type==PT_LOAD` 的段执行装载，首先根据 `p_flags` 计算 `VMA` 权限（`VM_READ/VM_WRITE/VM_EXEC`）并转换为 `RISC-V` 的页表权限位（`PTE_R/W/X`），调用 `mm_map(mm, p_va, p_memsz, vm_flags, NULL)` 建立覆盖整个 `p_memsz` 的 `VMA`。

  ```c
  vm_flags = 0, perm = PTE_U | PTE_V;
  if (ph->p_flags & ELF_PF_X) vm_flags |= VM_EXEC;
  if (ph->p_flags & ELF_PF_W) vm_flags |= VM_WRITE;
  if (ph->p_flags & ELF_PF_R) vm_flags |= VM_READ;
  
  if (vm_flags & VM_READ)  perm |= PTE_R;
  if (vm_flags & VM_WRITE) perm |= (PTE_W | PTE_R);
  if (vm_flags & VM_EXEC)  perm |= PTE_X;
  if ((ret = mm_map(mm, ph->p_va, ph->p_memsz, vm_flags, NULL)) != 0) {
      goto bad_cleanup_mmap;
  }
  ```

  对于文件中实际存在的部分 `[p_va, p_va+p_filesz)`，按页调用 `pgdir_alloc_page(mm->pgdir, la, perm)` 分配并映射物理页，再使用 `load_icode_read(fd, page2kva(page)+off, size, bin_offset)` 从文件偏移读取段内容写入页中，从而实现 `TEXT/DATA` 的装载。

  ```c
  uintptr_t start = ph->p_va;
  uintptr_t la = ROUNDDOWN(start, PGSIZE);
  uintptr_t end = ph->p_va + ph->p_filesz;
  off_t bin_offset = ph->p_offset;
  
  while (start < end) {
      if ((page = pgdir_alloc_page(mm->pgdir, la, perm)) == NULL) {
          goto bad_cleanup_mmap;
      }
      size_t off = start - la;
      size_t size = PGSIZE - off;
      if (end < la + PGSIZE) size = end - start;
  
      if ((ret = load_icode_read(fd, page2kva(page) + off, size, bin_offset)) != 0) {
          goto bad_cleanup_mmap;
      }
      start += size;
      bin_offset += size;
      la += PGSIZE;
  }
  ```

  对于内存中需要但文件中不存在的部分（`BSS`，即 `p_memsz > p_filesz`），对剩余区域按页补零：若与最后一页重叠则对页内剩余部分 `memset` 清零，否则分配新页后清零，保证 `BSS` 段初值为 `0`。

  ```c
  end = ph->p_va + ph->p_memsz;
  
  if (start < la) {
      if (start == end) continue;
  
      size_t off = start + PGSIZE - la;
      size_t size = PGSIZE - off;
      if (end < la) size -= la - end;
  
      memset(page2kva(page) + off, 0, size);
      start += size;
  }
  
  while (start < end) {
      if ((page = pgdir_alloc_page(mm->pgdir, la, perm)) == NULL) {
          goto bad_cleanup_mmap;
      }
      size_t off = start - la;
      size_t size = PGSIZE - off;
      la += PGSIZE;
      if (end < la) size -= la - end;
  
      memset(page2kva(page) + off, 0, size);
      start += size;
  }
  ```

- **建立用户栈并压入参数**

  装载完各段后，调用 `mm_map(mm, USTACKTOP-USTACKSIZE, USTACKSIZE, VM_READ|VM_WRITE|VM_STACK, NULL)` 建立栈区 `VMA`，并预分配若干栈页以保证可写；随后从高地址向低地址依次将每个参数字符串复制到用户栈中，并记录其用户虚拟地址；最后再将 `argv[]` 指针数组本体拷贝到用户栈，形成用户态可直接使用的 `argc/argv` 布局。

  ```c
  vm_flags = VM_READ | VM_WRITE | VM_STACK;
  if ((ret = mm_map(mm, USTACKTOP - USTACKSIZE, USTACKSIZE, vm_flags, NULL)) != 0) {
      goto bad_cleanup_mmap;
  }
  
  assert(pgdir_alloc_page(mm->pgdir, USTACKTOP - PGSIZE, PTE_USER) != NULL);
  assert(pgdir_alloc_page(mm->pgdir, USTACKTOP - 2*PGSIZE, PTE_USER) != NULL);
  assert(pgdir_alloc_page(mm->pgdir, USTACKTOP - 3*PGSIZE, PTE_USER) != NULL);
  assert(pgdir_alloc_page(mm->pgdir, USTACKTOP - 4*PGSIZE, PTE_USER) != NULL);
  
  assert(*get_pte(mm->pgdir, USTACKTOP - PGSIZE, 0) & PTE_W);
  ```

- **切换到新页表 + 设置 trapframe，返回用户态执行**

  栈与段映射完成后，将 `mm` 挂到 `current->mm`，并通过 `lsatp(PADDR(mm->pgdir))` 切换页表并刷新 `TLB`，最后设置 `trapframe`：

  ```c
  mm_count_inc(mm);
  current->mm = mm;
  current->pgdir = PADDR(mm->pgdir);
  
  lsatp(PADDR(mm->pgdir));
  flush_tlb();
  
  struct trapframe *tf = current->tf;
  uintptr_t sstatus = tf->status;
  memset(tf, 0, sizeof(struct trapframe));
  
  uintptr_t argv_user = stacktop;
  stacktop = ROUNDDOWN(stacktop, 16);
  
  tf->gpr.sp = stacktop;
  tf->gpr.a0 = argc;
  tf->gpr.a1 = argv_user;
  tf->epc = elf.e_entry;
  
  tf->status = sstatus & ~(SSTATUS_SPP | SSTATUS_SIE);
  tf->status |= SSTATUS_SPIE;
  tf->status |= SSTATUS_SUM;
  ```

- **异常清理**

  若在装载过程中任一步失败，按资源创建的逆序释放：包括 `exit_mmap/put_pgdir/mm_destroy` 以及 `phdrs/uargv` 的释放；若已经将页表切换到新 `pgdir`，则先切回 `boot_pgdir` 再释放用户映射，避免在无效页表上继续执行。

### 3. 结果验证

首先我们执行`make qemu`，结果如下，可以看到成功进入`shell`：
![image-20260104165042875](img/1.png)

然后我们可以在终端输入几个程序进行测试：

- **执行 hello**

  我们先输入`hello`测试是否可以输出预期字符串，结果如下，说明 `sysfile_open + load_icode_read` 能从 `sfs` 读取 `ELF` 并成功执行：

  ![image-20260104165209345](img/2.png)

- **执行 exit**

  接着输入`exit`，结果如下，可以看到子进程退出，`shell` 返回提示符，说明 `fork/exec/wait` 协同工作正常：

  ![image-20260104165357157](img/3.png)

- **执行不存在的程序**

  另外我们还可以测试当输入`user`目录下不存在的程序名时，结果如何：

  ![image-20260104165512582](img/5.png)

  可以看到`exec` 打开失败后输出错误提示：`no such file`，子进程退出，`shel`l 仍可继续接受命令，说明错误路径处理正确且不会破坏父进程。

以上测试结果说明我们已经成功实现了一个可以通过文件系统和用户程序交互的终端，最后我们测试`make grade`结果如下，得分为`100/100`，本次实验圆满结束！

![image-20260104165908020](img/4.png)