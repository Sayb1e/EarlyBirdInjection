#include<windows.h>
#include<stdio.h>

int main(int argc, char* argv[]) {
	const char* ProcessPath = argv[1];
	const char* dllPath = argv[2];

	printf("[*] Target Process: %s\n",ProcessPath);
	printf("[*] DLL Path: %s\n",dllPath);

	// 1. 创建挂起进程
	STARTUPINFOA hello = {
		sizeof(hello)
	};
	PROCESS_INFORMATION p = { 0 };

	if (!CreateProcessA(
		NULL,
		(LPSTR)ProcessPath,
		NULL,
		NULL,
		FALSE,
		CREATE_SUSPENDED,
		NULL,
		NULL,
		&hello,
		&p))
	{
		printf("[!] CreateProcess failed,error: %lu\n", GetLastError());
		return -1;
	}

	printf("[*] Suspended process failed created, PID: %lu\n", p.dwProcessId);
	printf("[*] Main thread ID: %lu\n", p.dwThreadId);

	// 2. 获取LoadLibraryA地址
	HMODULE hKernel32 = GetModuleHandleA("kernel32.dll");
	if (!hKernel32) {
		printf("[!] GetModuleHandleA failed\n");
		TerminateProcess(p.hProcess, 1);
		CloseHandle(p.hThread);
		CloseHandle(p.hProcess);
		return -1;
	}
	PAPCFUNC pfnLoadLibraryA = (PAPCFUNC)GetProcAddress(hKernel32, "LoadLibraryA");
	if (!pfnLoadLibraryA) {
		printf("[!] GetProAddress failed\n");
		TerminateProcess(p.hProcess, 1);
		CloseHandle(p.hThread);
		CloseHandle(p.hProcess);
		return -1;
	}
	printf("[*] LoadLibraryA at: %p\n", pfnLoadLibraryA);

	// 3. 在目标进程分配内存，写入dll路径
	SIZE_T pathLen = strlen(dllPath) + 1;
	LPVOID pRemoteBuf = VirtualAllocEx(p.hProcess, NULL, pathLen, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	if (!pRemoteBuf) {
		printf("[!] Virtual failed, error: %lu\n", GetLastError());
		// VirtualFreeEx(p.hProcess, pRemoteBuf, 0, MEM_RELEASE);
		TerminateProcess(p.hProcess, 1);
		CloseHandle(p.hThread);
		CloseHandle(p.hProcess);
		return -1;
	}
	printf("[+] Remote buffer at: %p\n",pRemoteBuf);

	if (!WriteProcessMemory(p.hProcess, pRemoteBuf, dllPath, pathLen, NULL)) {
		printf("[!] WritePorcessMemory failed, error: %lu\n", GetLastError());
		VirtualFreeEx(p.hProcess, pRemoteBuf, 0, MEM_RELEASE);
		TerminateProcess(p.hProcess, 1);
		CloseHandle(p.hThread);
		CloseHandle(p.hProcess);
		return -1;
	}

	// 4. 投递APC到挂起的主线程
	if (!QueueUserAPC(pfnLoadLibraryA, p.hThread, (ULONG_PTR)pRemoteBuf)) {
		printf("[!] QueueUserAPC failed, error: %lu\n", GetLastError());
		VirtualFreeEx(p.hProcess, pRemoteBuf, 0, MEM_RELEASE);
		CloseHandle(p.hThread);
		CloseHandle(p.hProcess);
		return -1;
	}
	printf("[+] APC queued to main thread\n");

	// 5. 恢复线程 -> APC被立即执行
	printf("[*] Resuming thread...\n");
	DWORD preCount = ResumeThread(p.hThread);
	if (preCount == (DWORD)-1) {
		printf("[!] RemuseThread failed, error: %lu\n", GetLastError());
	}
	else {
		printf("[!] Thread resumed (previous suspend count: %lu)\n", preCount);
	}

	// 6. 等待进程
	printf("[*] Waiting for dll to be loaded...\n");
	DWORD waitresult = WaitForSingleObject(p.hProcess, 3000);
	if (waitresult == WAIT_OBJECT_0) {
		printf("[+] Target process exited\n");
	}
	else if (waitresult == WAIT_TIMEOUT) {
		printf("[*] Target process still running (dll loaded during init)\n");
	}

	// 7. 清理
	if (waitresult == WAIT_OBJECT_0) {
		VirtualFreeEx(p.hProcess, pRemoteBuf, 0, MEM_RELEASE);
	}
	CloseHandle(p.hThread);
	CloseHandle(p.hProcess);
	printf("[*] Done.\n");
	return 0;
}