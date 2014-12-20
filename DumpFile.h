/***************************************
* @file     DumpFile.h
* @brief    一个宏命令，就可以程序崩溃时生成dump文件
* @details  在主程序初始化时加入 DeclareDumpFile();
* @author   phata, wqvbjhc@gmail.com
* @date     2014-2-10
* @mod      2014-05-14 phata 添加控制台项目缺少的头文件
            2014-05-15 phata 更改生成dmp文件命名方法("日期_时间.dmp"改为"exe全名(日期_时间).dmp")
****************************************/
#pragma once
#include <windows.h>
#include <time.h>
#include <tchar.h>
#include <Dbghelp.h>
#include <iostream>
#include <vector>
#pragma comment(lib, "Dbghelp.lib")
using namespace std;

namespace NSDumpFile
{
void CreateDumpFile(LPCTSTR lpstrDumpFilePathName, EXCEPTION_POINTERS *pException)
{
    // 创建Dump文件
    HANDLE hDumpFile = CreateFile(lpstrDumpFilePathName, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

    // Dump信息
    MINIDUMP_EXCEPTION_INFORMATION dumpInfo;
    dumpInfo.ExceptionPointers = pException;
    dumpInfo.ThreadId = GetCurrentThreadId();
    dumpInfo.ClientPointers = TRUE;

    // 写入Dump文件内容
    MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hDumpFile, MiniDumpNormal, &dumpInfo, NULL, NULL);
    CloseHandle(hDumpFile);
}


LPTOP_LEVEL_EXCEPTION_FILTER WINAPI MyDummySetUnhandledExceptionFilter(LPTOP_LEVEL_EXCEPTION_FILTER lpTopLevelExceptionFilter)
{
    return NULL;
}


BOOL PreventSetUnhandledExceptionFilter()
{
    HMODULE hKernel32 = LoadLibrary(_T("kernel32.dll"));
    if (hKernel32 ==   NULL)
        return FALSE;


    void *pOrgEntry = GetProcAddress(hKernel32, "SetUnhandledExceptionFilter");
    if(pOrgEntry == NULL)
        return FALSE;


    unsigned char newJump[ 100 ];
    DWORD dwOrgEntryAddr = (DWORD) pOrgEntry;
    dwOrgEntryAddr += 5; // add 5 for 5 op-codes for jmp far


    void *pNewFunc = &MyDummySetUnhandledExceptionFilter;
    DWORD dwNewEntryAddr = (DWORD) pNewFunc;
    DWORD dwRelativeAddr = dwNewEntryAddr -  dwOrgEntryAddr;


    newJump[ 0 ] = 0xE9;  // JMP absolute
    memcpy(&newJump[ 1 ], &dwRelativeAddr, sizeof(pNewFunc));
    SIZE_T bytesWritten;
    BOOL bRet = WriteProcessMemory(GetCurrentProcess(),    pOrgEntry, newJump, sizeof(pNewFunc) + 1, &bytesWritten);
    return bRet;
}

LONG WINAPI UnhandledExceptionFilterEx(struct _EXCEPTION_POINTERS *pException)
{
    TCHAR directory [MAX_PATH+1] = {0};
    GetModuleFileName(NULL, directory, MAX_PATH);
    time_t t = time(0);
    TCHAR tmp[64];
    memset(tmp,0,sizeof(tmp));
    _tcsftime( tmp, sizeof(tmp), _T("(%Y%m%d_%H%M%S).dmp"),localtime(&t));
    _tcscat(directory,tmp);
    CreateDumpFile(directory, pException);

    // TODO: MiniDumpWriteDump
    FatalAppExit(-1,  _T("Fatal Error"));
    return EXCEPTION_CONTINUE_SEARCH;
}


void RunCrashHandler()
{
    SetUnhandledExceptionFilter(UnhandledExceptionFilterEx);
    PreventSetUnhandledExceptionFilter(); //Release版不会生成，具体原因不知
}
};
#define DeclareDumpFile() NSDumpFile::RunCrashHandler();
