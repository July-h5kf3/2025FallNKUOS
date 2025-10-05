<center>
    <h1>2025秋操作系统课程仓库
</center>

### 课程实验目标

搭建Ucore操作系统

### 课程资源

课程网站:http://oslab.mobisys.cc/

课程实验指导书:http://oslab.mobisys.cc/lab2025/_book/index.html

#### 课程镜像仓库(课程需要的基本环境已经配好了)

[![Docker Hub](https://img.shields.io/badge/DockerHub-lorn1%2Fpc-blue)](https://hub.docker.com/repository/docker/lorn1/os-wsl/general)

##### 使用方法
```bash
docker pull lorn1/pc:wsl
docker run -it lorn1/pc:wsl /bin/bash
```

### lab-01：比麻雀更小的麻雀（最小可执行内核）

相对于上百万行的现代操作系统(linux, windows), 几千行的ucore是一只"麻雀"。但这只麻雀依然是一只胖麻雀，我们一眼看不过来几千行的代码。所以，我们要再做简化，先用好刀法，片掉麻雀的血肉, 搞出一个"麻雀骨架"，看得通透，再像组装哪吒一样，把血肉安回去，变成一个活生生的麻雀。这就是我们的ucore step-by-step tutorial的思路。

lab1是后面实验的预备，我们构建一个最小的可执行内核（”麻雀骨架“），它能够进行格式化的输出，然后进入死循环。

### lab-02:物理内存和页表

实验二主要涉及操作系统的物理内存管理。操作系统为了使用内存，还需高效地管理内存资源。在实验二中会了解并且自己动手完成一个简单的物理内存管理系统。

### lab-03:断,都可以断

在完成了物理内存管理和页表机制之后，内核已经具备了最基本的内存支持。接下来，我们需要让操作系统能够与外部世界进行交互，并能够对硬件事件作出响应。因此在实验三中，我们将在最小可执行内核的基础上，加入对中断与异常的支持，并通过时钟中断来验证中断处理系统的正确性。

