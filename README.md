# EarlyBirdInjection — APC 早期注入技术

一个基于 Windows x64/x86 的 APC（Asynchronous Procedure Call）早期注入技术演示项目，使用 C 语言编写，在 Visual
Studio 2022 (v143) 下编译。

## 概述

**APC 早期注入** 是一种代码注入技术，其

1. 以 **挂起状态** 创建一个合法进程（如 `notepad.exe` 等）
2. 获取 `kernel32.dll` 中 `LoadLibraryA` 的函数地址
3. 在目标进程地址空间中分配内存，写入 DL
4. 向目标进程的主线程投递 APC（异步过程调用）
5. 恢复主线程执行 → 线程进入 APC 分发器 → 执行 `LoadLibraryA(dllPath)` → DLL 被加载

与传统的 `CreateRemoteThread` + `LoadLibrary` 注入方式不同，APC 早期注入**不创建新线程**，而是复用目标进程的主
线程，在进程入口点执行之前完成注入，隐蔽性更高。

## 项目结构

```
EarlyBirdInjection/
├── EarlyBirdInjection.c              # 主源码 — 完整的 APC 早期注入实现
├── EarlyBirdInjection.sln            # Visual Studio 解决方案文件
├── EarlyBirdInjection.vcxproj        #
├── EarlyBirdInjection.vcxproj.filters
├── EarlyBirdInjection.vcxproj.user
└── README.md                         # 本文件
```

## 编译环境

| 项目 | 值 |
|------|-----|
| 编译器 | MSVC v143 (Visual Studio 2022) |
| 目标平台 | x86 / x64 |
| Windows SDK | 10.0 |
| 字符集 | Unicode |
| 配置 | Release / Debug |
| 依赖库 | 仅 Windows API（无额外依赖） |

## 使用方法

```shell
EarlyBirdInjection.exe <target.exe> <dll_path>
```

| 参数 | 说明 |
|------|------|
| `target.exe` | 以挂起状态创建的目标进程路径（如 `notepad.exe`） |
| `dll_path` | 要注入的 DLL 文件的**绝对

### 示例

```shell
EarlyBirdInjection.exe C:\Windows\System32\notepad.exe C:\Users\Public\mydll.dll
```

该命令将：
- 挂起启动 `notepad.exe`
- 在 notepad 进程中分配内存并写入 `mydll.dll` 的路径
- 向 notepad 主线程投递 APC，使 `LoadLibraryA("mydll.dll")` 在进程入口点之前执行
- 恢复主线程，DLL 被加载到 notepad 进程空间

## 实现细节

### 核心流程

```
Read parameters
    │
    ▼
CreateProcess (CREATE_SUSPENDED)
    │
    ▼
GetModuleHandleA("kernel32.dll")
    │
    ▼
GetProcAddress("LoadLibraryA")
    │
    ▼
VirtualAllocEx → 分配远程内存
    │
    ▼
WriteProcessMemory → 写入 DLL 路径
    │
    ▼
QueueUserAPC → 投递 APC (LoadLibraryA, dllPath)
    │
    ▼
ResumeThread → 恢复主线程
    │
    ▼
WaitForSingleObject (3000ms timeout)
    │
    ▼
CloseHandle → 清理
```

### 关键技术点

| 步骤 | API | 说明 |
|------|-----|------|
| 创建挂起进程 | `CreateProcessA(..., CREATE_SUSPENDED, ...)` | 进程创建后主线程暂停，尚未执行入口点 |
| 获取 LoadLibraryA | `GetModuleHandleA` + `GetProcAddress` | 获取 kernel32 中 LoadLibraryA 的函数指针 |
| 分配远程内存 | `VirtualAllocEx` | 在目标进程空间中分配 `PAGE_READWRITE` 内存 |
| 写入 DLL 路径 | `WriteProcessMemory` | 区 |
| 投递 APC | `QueueUserAPC` | 将 `LoadLibraryA(dllPath)` 加入主线程 APC 队列 |
| 恢复线程 | `ResumeThread` | 线程恢复 → 进入 `KiUserApcDispatcher` → 执行 APC |
| 等待 | `WaitForSingleObject` | 3 秒超时等待目标进程结束 |

### 什么是 APC？

APC（Asynchronous Procedure Call，异步过程调用）是 Windows 提供的一种机制，允许在特定线程的上下文中异步执行代码
。

当一个线程使用 `CREATE_SUSPENDED` 标志创建时，其主线程在恢复后首先经过 `KiUserApcDispatcher`（内核 APC 分发器）
，系统会在此处检查用户 APC 队列。如果队列中有待执行的 APC，会在执行线程的任何用户代码之前优先处理——这提供了在进
程入口点（`main`/`WinMain`）执行前注入 DLL 的独特时机窗口。

### 执行流程时序

```
时间 ───────────────────────────────────

注入器进程                          目标进程 (notepad.exe)
    │                                    │
    │  CreateProcess(SUSPENDED)           │  [进程创建，主线程挂起]
    │────────────────────────────────────►│
    │
    │  VirtualAllocEx + WriteProcessMemory│
    │────────────────────────────────────►│  [远程缓冲区写入 dllPath]
    │
    │  QueueUserAPC(LoadLibraryA, dllPath)│
    │────────────────────────────────────►│  [APC 加入主线程队列]
    │
    │  ResumeThread                       │
    │────────────────────────────────────►│  [主线程恢复]
    │                                     │
    │                                     │  KiUserApcDispatcher
    │                                     │      │
    │                                     │      ▼
    │                                     │  LoadLibraryA(dllPath)
    │
    │                                     │      ▼
    │                                   CH)
    │                                     │      │
    │                                     │      ▼
    │                                     │  main/WinMain (入口点)
    │                                     │
    │  WaitForSingleObject                │
    │◄──────────────────────────────────
    │                                     │
    │  CloseHandle                        │
    │
```

## 注意事项

- ⚠️ **免责声明：** 本项目仅供**学习研究**使用。APC 注入技术常被恶意软件用于代码注入和隐蔽执行。请勿将其用于任
何非法或未经授权的用途。

- **架构匹配：** 注入器的位数必须与目标进程匹配（32 位 → 32 位，64 位 → 64 位）。架构不匹配会导致注入失败（错误
码 998）。

- **DLL 路径：** 必须使用**绝对路径**。因为目标进程的内存空间中不存在注入器的当前工作目录上下文，相对路径无法解
析。

- **管理员权限：** 注入某些系统进程（如 `svchost.exe`、`lsass.exe` 等）可能需要管理员权限。

- **DLL 入口点：** 在 `DLL_PROCESS_ATTACH` 中执行的代码应尽量轻量，避免过多的初始化逻辑或等待操作，因为在 APC
上下文中执行存在一定限制（如不能安全调用 `LoadLibrary` / `FreeLibrary`）。

- **清理机制：** 程序在目标进程退出或 3 秒超时后会自动 `VirtualFreeEx` 释放远程内存，并 `CloseHandle` 关闭所有
句柄。

## 参考

- [Windows APC 机制 (Microsoft Learn)](https://learn.microsoft.com/en-us/windows/win32/sync/asynchronous-proced
ure-calls)
- [QueueUserAPC 文档](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsap
i-queueuserapc)
- [CreateProcess 文档](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsa
pi-createprocessa)
- [我的博客](http://sayble.xyz)
