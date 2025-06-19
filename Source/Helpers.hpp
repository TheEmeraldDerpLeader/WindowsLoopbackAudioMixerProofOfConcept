#pragma once

#include <Windows.h>


struct ErrorHandler
{
public:
	HRESULT err = 0;

	ErrorHandler() = default;

	ErrorHandler& operator=(const HRESULT errH);
};

/* Simple error checker
LPVOID lpMsgBuf;
DWORD dw = GetLastError(); 

if (FormatMessage(
FORMAT_MESSAGE_ALLOCATE_BUFFER | 
FORMAT_MESSAGE_FROM_SYSTEM |
FORMAT_MESSAGE_IGNORE_INSERTS,
NULL,
dw,
MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
(LPTSTR) &lpMsgBuf,
0, NULL) == 0) {
MessageBox(NULL, TEXT("FormatMessage failed"), TEXT("Error"), MB_OK);
}

MessageBox(NULL, (LPCTSTR)lpMsgBuf, TEXT("Error"), MB_OK);

LocalFree(lpMsgBuf);
*/ 