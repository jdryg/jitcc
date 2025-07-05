#ifndef _WINDOWS_
#define _WINDOWS_

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN 1
#endif

#ifndef WINVER
#define WINVER 0x0502
#endif

#include <_jitcc.h>

#ifndef _INC_WINDOWS
#define _INC_WINDOWS

#define NOWINBASEINTERLOCK
#include <stdarg.h>
#include <windef.h>
#include <winbase.h>
#include <wingdi.h>
#include <winuser.h>
#include <wincon.h>
#include <winver.h>
#include <winreg.h>

// misc
int WINAPI WideCharToMultiByte(UINT CodePage, DWORD dwFlags, LPCWSTR lpWideCharStr, int cchWideChar, LPSTR lpMultiByteStr, int cbMultiByte, LPCSTR lpDefaultChar, LPBOOL lpUsedDefaultChar);
int WINAPI MultiByteToWideChar(UINT CodePage, DWORD dwFlags, LPCSTR lpMultiByteStr, int cbMultiByte, LPWSTR lpWideCharStr, int cchWideChar);

/* Code Pages: */
#define CP_INSTALLED                0x00000001
#define CP_SUPPORTED                0x00000002
#define CP_ACP                      0
#define CP_OEMCP                    1
#define CP_MACCP                    2
#define CP_THREAD_ACP               3
#define CP_SYMBOL                   42
#define CP_UTF7                     65000
#define CP_UTF8                     65001

void __debugbreak();

#endif // _INC_WINDOWS
#endif // _WINDOWS_
