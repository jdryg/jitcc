#include <jlib/macros.h>
#include <jlib/os.h>
#include <jlib/memory.h>
#include <jlib/string.h>
#include <jlib/dbg.h>
#include <jlib/allocator.h>
#include <jlib/image.h>
#include <jlib/error.h>
#include <jlib/memory_tracer.h>
#include <jlib/logger.h>
#include <stdbool.h>

#if JX_PLATFORM_WINDOWS
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <Windowsx.h> // GET_X_LPARAM, GET_Y_LPARAM
#include <Shlobj.h>
#include <VersionHelpers.h>

#define JOS_CONFIG_DEBUG 0
#if JOS_CONFIG_DEBUG
#define JOS_TRACE JX_TRACE
#else
#define JOS_TRACE(...)
#endif

static jx_os_module_t* _jx_os_moduleOpen(jx_file_base_dir baseDir, const char* path);
static void _jx_os_moduleClose(jx_os_module_t* mod);
static void* _jx_os_moduleGetSymbolAddr(jx_os_module_t* mod, const char* symbolName);
static jx_os_window_t* _jx_os_windowOpen(const jx_os_window_desc_t* desc);
static void _jx_os_windowClose(jx_os_window_t* win);
static void _jx_os_windowSetTitle(jx_os_window_t* win, const char* title);
static void _jx_os_windowSetCursor(jx_os_window_t* win, jx_os_cursor_type cursor);
static bool _jx_os_windowSetIcon(jx_os_window_t* win, const jx_os_icon_desc_t* iconDesc);
static void _jx_os_windowGetResolution(jx_os_window_t* win, uint16_t* res);
static uint32_t _jx_os_windowGetFlags(jx_os_window_t* win);
static void* _jx_os_windowGetNativeHandle(jx_os_window_t* win);
static bool _jx_os_windowClipboardGetString(jx_os_window_t* win, char** str, uint32_t* len, jx_allocator_i* allocator);
static bool _jx_os_windowClipboardSetString(jx_os_window_t* win, const char* str, uint32_t len);
static jx_os_frame_tick_result _jx_os_frameTick(void);
static jx_os_timer_t* _jx_os_timerCreate();
static void _jx_os_timerDestroy(jx_os_timer_t* timer);
static bool _jx_os_timerSleep(jx_os_timer_t* timer, int64_t duration_us);
static int64_t _jx_os_timeNow(void);
static int64_t _jx_os_timeDiff(int64_t end, int64_t start);
static int64_t _jx_os_timeSince(int64_t start);
static int64_t _jx_os_timeLapTime(int64_t* timer);
static double _jx_os_timeConvertTo(int64_t delta, jx_os_time_units units);
static uint64_t _jx_os_timestampNow(void);
static uint64_t _jx_os_timestampNow_precise(void);
static int64_t _jx_os_timestampDiff(uint64_t end, uint64_t start);
static int64_t _jx_os_timestampSince(uint64_t start);
static double _jx_os_timestampConvertTo(int64_t delta, jx_os_time_units units);
static uint32_t _jx_os_timestampToString(uint64_t ts, char* buffer, uint32_t max);
static int32_t _jx_os_consoleOpen(void);
static void _jx_os_consoleClose(bool waitForUserInput);
static int32_t _jx_os_consolePuts(const char* str, uint32_t len);
static jx_os_mutex_t* _jx_os_mutexCreate(void);
static void _jx_os_mutexDestroy(jx_os_mutex_t* mutex);
static void _jx_os_mutexLock(jx_os_mutex_t* mutex);
static bool _jx_os_mutexTryLock(jx_os_mutex_t* mutex);
static void _jx_os_mutexUnlock(jx_os_mutex_t* mutex);
static jx_os_semaphore_t* _jx_os_semaphoreCreate(void);
static void _jx_os_semaphoreDestroy(jx_os_semaphore_t* semaphore);
static void _jx_os_semaphoreSignal(jx_os_semaphore_t* semaphore, uint32_t count);
static bool _jx_os_semaphoreWait(jx_os_semaphore_t* semaphore, uint32_t msecs);
static jx_os_event_t* _jx_os_eventCreate(bool manualReset, bool initialState, const char* name);
static void _jx_os_eventDestroy(jx_os_event_t* ev);
static bool _jx_os_eventSet(jx_os_event_t* ev);
static bool _jx_os_eventReset(jx_os_event_t* ev);
static bool _jx_os_eventWait(jx_os_event_t* ev, uint32_t msecs);
static uint32_t _jx_os_threadGetID(void);
static jx_os_thread_t* _jx_os_threadCreate(josThreadFunc func, void* userData, uint32_t stackSize, const char* name);
static void _jx_os_threadDestroy(jx_os_thread_t* thread);
static void _jx_os_threadShutdown(jx_os_thread_t* thread);
static bool _jx_os_threadIsRunning(jx_os_thread_t* thread);
static int32_t _jx_os_threadGetExitCode(jx_os_thread_t* thread);
static uint32_t _jx_os_getNumHardwareThreads(void);
static jx_os_file_t* _jx_os_fileOpenRead(jx_file_base_dir baseDir, const char* relPath);
static jx_os_file_t* _jx_os_fileOpenWrite(jx_file_base_dir baseDir, const char* relPath);
static void _jx_os_fileClose(jx_os_file_t* f);
static uint32_t _jx_os_fileRead(jx_os_file_t* f, void* buffer, uint32_t len);
static uint32_t _jx_os_fileWrite(jx_os_file_t* f, const void* buffer, uint32_t len);
static uint64_t _jx_os_fileGetSize(jx_os_file_t* f);
static void _jx_os_fileSeek(jx_os_file_t* f, int64_t offset, jx_file_seek_origin origin);
static uint64_t _jx_os_fileTell(jx_os_file_t* f);
static void _jx_os_fileFlush(jx_os_file_t* f);
static int32_t _jx_os_fileGetTime(jx_os_file_t* f, jx_file_time_type type, jx_os_file_time_t* time);
static int32_t _jx_os_fsSetBaseDir(jx_file_base_dir whichDir, jx_file_base_dir baseDir, const char* relPath);
static int32_t _jx_os_fsGetBaseDir(jx_file_base_dir whichDir, char* absPath, uint32_t max);
static int32_t _jx_os_fsRemoveFile(jx_file_base_dir baseDir, const char* relPath);
static int32_t _jx_os_fsCopyFile(jx_file_base_dir srcBaseDir, const char* srcRelPath, jx_file_base_dir dstBaseDir, const char* dstRelPath);
static int32_t _jx_os_fsMoveFile(jx_file_base_dir srcBaseDir, const char* srcRelPath, jx_file_base_dir dstBaseDir, const char* dstRelPath);
static int32_t _jx_os_fsCreateDirectory(jx_file_base_dir baseDir, const char* relPath);
static int32_t _jx_os_fsRemoveEmptyDirectory(jx_file_base_dir baseDir, const char* relPath);
static int32_t _jx_os_fsEnumFilesAndFolders(jx_file_base_dir baseDir, const char* pattern, josEnumFilesAndFoldersCallback callback, void* userData);
static bool _jx_os_fsFileExists(jx_file_base_dir baseDir, const char* relPath);
static uint32_t _jx_os_vmemGetPageSize(void);
static void* _jx_os_vmemAlloc(void* desiredAddr, size_t sz, uint32_t protectFlags);
static void _jx_os_vmemFree(void* addr, size_t sz);
static bool _jx_os_vmemProtect(void* addr, size_t sz, uint32_t protectFlags);

jx_os_api* os_api = &(jx_os_api){
	.moduleOpen = _jx_os_moduleOpen,
	.moduleClose = _jx_os_moduleClose,
	.moduleGetSymbolAddr = _jx_os_moduleGetSymbolAddr,
	.windowOpen = _jx_os_windowOpen,
	.windowClose = _jx_os_windowClose,
	.windowSetCursor = _jx_os_windowSetCursor,
	.windowSetTitle = _jx_os_windowSetTitle,
	.windowSetIcon = _jx_os_windowSetIcon,
	.windowGetResolution = _jx_os_windowGetResolution,
	.windowGetFlags = _jx_os_windowGetFlags,
	.windowGetNativeHandle = _jx_os_windowGetNativeHandle,
	.windowClipboardGetString = _jx_os_windowClipboardGetString,
	.windowClipboardSetString = _jx_os_windowClipboardSetString,
	.frameTick = _jx_os_frameTick,
	.timerCreate = _jx_os_timerCreate,
	.timerDestroy = _jx_os_timerDestroy,
	.timerSleep = _jx_os_timerSleep,
	.timeNow = _jx_os_timeNow,
	.timeDiff = _jx_os_timeDiff,
	.timeSince = _jx_os_timeSince,
	.timeLapTime = _jx_os_timeLapTime,
	.timeConvertTo = _jx_os_timeConvertTo,
	.timestampNow = _jx_os_timestampNow,
	.timestampDiff = _jx_os_timestampDiff,
	.timestampSince = _jx_os_timestampSince,
	.timestampConvertTo = _jx_os_timeConvertTo,
	.timestampToString = _jx_os_timestampToString,
	.consoleOpen = _jx_os_consoleOpen,
	.consoleClose = _jx_os_consoleClose,
	.consolePuts = _jx_os_consolePuts,
	.mutexCreate = _jx_os_mutexCreate,
	.mutexDestroy = _jx_os_mutexDestroy,
	.mutexLock = _jx_os_mutexLock,
	.mutexTryLock = _jx_os_mutexTryLock,
	.mutexUnlock = _jx_os_mutexUnlock,
	.semaphoreCreate = _jx_os_semaphoreCreate,
	.semaphoreDestroy = _jx_os_semaphoreDestroy,
	.semaphoreSignal = _jx_os_semaphoreSignal,
	.semaphoreWait = _jx_os_semaphoreWait,
	.eventCreate = _jx_os_eventCreate,
	.eventDestroy = _jx_os_eventDestroy,
	.eventSet = _jx_os_eventSet,
	.eventReset = _jx_os_eventReset,
	.eventWait = _jx_os_eventWait,
	.threadGetID = _jx_os_threadGetID,
	.threadCreate = _jx_os_threadCreate,
	.threadDestroy = _jx_os_threadDestroy,
	.threadShutdown = _jx_os_threadShutdown,
	.threadIsRunning = _jx_os_threadIsRunning,
	.threadGetExitCode = _jx_os_threadGetExitCode,
	.getNumHardwareThreads = _jx_os_getNumHardwareThreads,
	.fileOpenRead = _jx_os_fileOpenRead,
	.fileOpenWrite = _jx_os_fileOpenWrite,
	.fileClose = _jx_os_fileClose,
	.fileRead = _jx_os_fileRead,
	.fileWrite = _jx_os_fileWrite,
	.fileGetSize = _jx_os_fileGetSize,
	.fileSeek = _jx_os_fileSeek,
	.fileTell = _jx_os_fileTell,
	.fileFlush = _jx_os_fileFlush,
	.fileGetTime = _jx_os_fileGetTime,
	.fsSetBaseDir = _jx_os_fsSetBaseDir,
	.fsGetBaseDir = _jx_os_fsGetBaseDir,
	.fsRemoveFile = _jx_os_fsRemoveFile,
	.fsCopyFile = _jx_os_fsCopyFile,
	.fsMoveFile = _jx_os_fsMoveFile,
	.fsCreateDirectory = _jx_os_fsCreateDirectory,
	.fsRemoveEmptyDirectory = _jx_os_fsRemoveEmptyDirectory,
	.fsEnumFilesAndFolders = _jx_os_fsEnumFilesAndFolders,
	.fsFileExists = _jx_os_fsFileExists,
	.vmemGetPageSize = _jx_os_vmemGetPageSize,
	.vmemAlloc = _jx_os_vmemAlloc,
	.vmemFree = _jx_os_vmemFree,
	.vmemProtect = _jx_os_vmemProtect,
};

typedef void (*pfnGetSystemTimePreciseAsFileTime)(LPFILETIME lpSystemTimeAsFileTime);

typedef struct jx_os_win32
{
	jx_allocator_i* m_Allocator;
	struct jx_os_window_t* m_MainWindow;
	pfnGetSystemTimePreciseAsFileTime GetSystemTimePreciseAsFileTime;
	jx_os_virtual_key m_ScanToKey[512];
	HCURSOR m_Cursors[JX_OS_NUM_CURSORS];
	int64_t m_TimerFreq;
	char m_InstallDir[512];
	char m_TempDir[512];
	char m_UserDataDir[512];
	char m_UserAppDataDir[512];
} jx_os_win32;

static jx_os_win32 s_OSContext = { 0 };

static bool _jx_os_win32_getInstallFolder(char* path_utf8, uint32_t max);
static bool _jx_os_win32_getTempFolder(char* path_utf8, uint32_t max);
static bool _jx_os_win32_getUserDataFolder(char* path_utf8, uint32_t max);
static bool _jx_os_win32_getUserAppDataFolder(char* path_utf8, uint32_t max);
static const char* _jx_os_getBaseDirPathUTF8(jx_file_base_dir baseDir);
static void _jx_os_win32_initKeycodes(void);
static void _jx_os_win32_initCursors(void);

bool jx_os_initAPI(void)
{
	s_OSContext.m_Allocator = allocator_api->createAllocator("os");
	if (!s_OSContext.m_Allocator) {
		return false;
	}

	if (!_jx_os_win32_getInstallFolder(s_OSContext.m_InstallDir, JX_COUNTOF(s_OSContext.m_InstallDir))) {
		return false;
	}

	if (!_jx_os_win32_getTempFolder(s_OSContext.m_TempDir, JX_COUNTOF(s_OSContext.m_TempDir))) {
		return false;
	}

	if (!_jx_os_win32_getUserDataFolder(s_OSContext.m_UserDataDir, JX_COUNTOF(s_OSContext.m_UserDataDir))) {
		return false;
	}

	if (!_jx_os_win32_getUserAppDataFolder(s_OSContext.m_UserAppDataDir, JX_COUNTOF(s_OSContext.m_UserAppDataDir))) {
		return false;
	}

	_jx_os_win32_initKeycodes();
	_jx_os_win32_initCursors();

	{
		LARGE_INTEGER freq;
		QueryPerformanceFrequency(&freq);
		s_OSContext.m_TimerFreq = (int64_t)freq.QuadPart;
	}

	{
		HMODULE kernel32 = LoadLibraryA("kernel32.dll");
		if (kernel32) {
			s_OSContext.GetSystemTimePreciseAsFileTime = (pfnGetSystemTimePreciseAsFileTime)GetProcAddress(kernel32, "GetSystemTimePreciseAsFileTime");
			FreeLibrary(kernel32);
		}

		const bool hasPreciseTimestamps = s_OSContext.GetSystemTimePreciseAsFileTime != NULL;
		if (hasPreciseTimestamps) {
			os_api->timestampNow = _jx_os_timestampNow_precise;
		}
	}

	return true;
}

void jx_os_shutdownAPI(void)
{
	if (s_OSContext.m_Allocator) {
		allocator_api->destroyAllocator(s_OSContext.m_Allocator);
		s_OSContext.m_Allocator = NULL;
	}
}

void jx_os_logInfo(jx_logger_i* logger)
{
	// OS Version
	{
		bool success = false;

		{
			HMODULE ntdll = LoadLibraryW(L"ntdll.dll");
			if (ntdll != NULL) {
				typedef DWORD(WINAPI* pfnRtlGetVersion)(PRTL_OSVERSIONINFOW lpVersionInformation);

				pfnRtlGetVersion rtlGetVersion = (pfnRtlGetVersion)GetProcAddress(ntdll, "RtlGetVersion");
				if (rtlGetVersion != NULL) {
					OSVERSIONINFOEXW osVersion;
					jx_memset(&osVersion, 0, sizeof(OSVERSIONINFOEXW));
					osVersion.dwOSVersionInfoSize = sizeof(OSVERSIONINFOEXW);

					rtlGetVersion((PRTL_OSVERSIONINFOW)&osVersion);

					JX_LOG_DEBUG(logger, "os", "Windows x64 %u.%u.%u\n", osVersion.dwMajorVersion, osVersion.dwMinorVersion, osVersion.dwBuildNumber);

					success = true;
				}
				FreeLibrary(ntdll);
			}
		}

#if 0
		// NOTE: Always returns 6.2.xxxx (Windows 8)
		if (!success) {
			HMODULE kernel = LoadLibraryW(L"kernel32.dll");
			if (kernel != NULL) {
				typedef BOOL(WINAPI* pfnGetVersionExW)(LPOSVERSIONINFOW lpVersionInformation);

				pfnGetVersionExW getVersionExW = (pfnGetVersionExW)GetProcAddress(kernel, "GetVersionExW");
				if (getVersionExW != NULL && getVersionExW((OSVERSIONINFO*)&osVersion)) {
					success = true;
				}
				FreeLibrary(kernel);
			}
		}
#endif

		if (!success) {
			// Get the ProductVersion string from kernel32.dll
			// https://web.archive.org/web/20160611121336/https://msdn.microsoft.com/en-us/library/windows/desktop/ms724429.aspx
			// To obtain the full version number for the operating system, call the GetFileVersionInfo function on 
			// one of the system DLLs, such as Kernel32.dll, then call VerQueryValue to obtain the 
			// \\StringFileInfo\\<lang><codepage>\\ProductVersion subblock of the file version information.
			HMODULE version = LoadLibraryW(L"version.dll");
			if (version != NULL) {
				typedef DWORD(WINAPI* pfnGetFileVersionInfoSizeA)(LPCSTR lptstrFilename, LPDWORD lpdwHandle);
				typedef BOOL(WINAPI* pfnGetFileVersionInfoA)(LPCSTR lptstrFilename, DWORD dwHandle, DWORD dwLen, LPVOID lpData);
				typedef BOOL(WINAPI* pfnVerQueryValueA)(LPCVOID pBlock, LPCSTR lpSubBlock, LPVOID* lplpBuffer, PUINT puLen);
				typedef struct LANGANDCODEPAGE
				{
					WORD wLanguage;
					WORD wCodePage;
				} LANGANDCODEPAGE;

				pfnGetFileVersionInfoSizeA getFileVersionInfoSizeA = (pfnGetFileVersionInfoSizeA)GetProcAddress(version, "GetFileVersionInfoSizeA");
				pfnGetFileVersionInfoA getFileVersionInfoA = (pfnGetFileVersionInfoA)GetProcAddress(version, "GetFileVersionInfoA");
				pfnVerQueryValueA verQueryValueA = (pfnVerQueryValueA)GetProcAddress(version, "VerQueryValueA");

				if (getFileVersionInfoA == NULL || getFileVersionInfoSizeA == NULL || verQueryValueA == NULL) {
					goto version_end;
				}

				const uint32_t verInfoSz = getFileVersionInfoSizeA("kernel32.dll", NULL);
				if (!verInfoSz) {
					goto version_end;
				}
				
				uint8_t* verInfoBuffer = JX_ALLOC(s_OSContext.m_Allocator, verInfoSz);
				if (!verInfoBuffer) {
					goto version_end;
				}
				
				if (!getFileVersionInfoA("kernel32.dll", 0, verInfoSz, verInfoBuffer)) {
					JX_FREE(s_OSContext.m_Allocator, verInfoBuffer);
					goto version_end;
				}
				
				LANGANDCODEPAGE* lpTranslate = NULL;
				UINT cbTranslate = 0;
				verQueryValueA(verInfoBuffer, "\\VarFileInfo\\Translation", (LPVOID*)&lpTranslate, &cbTranslate);
				if (lpTranslate == NULL || cbTranslate < sizeof(LANGANDCODEPAGE)) {
					JX_FREE(s_OSContext.m_Allocator, verInfoBuffer);
					goto version_end;
				}
				
				char str[256];
				jx_snprintf(str, JX_COUNTOF(str), "\\StringFileInfo\\%04x%04x\\ProductVersion", lpTranslate->wLanguage, lpTranslate->wCodePage);
						
				void* lpBuffer = NULL;
				UINT dwBytes = 0;
				verQueryValueA(verInfoBuffer, str, &lpBuffer, &dwBytes);
				if (lpBuffer != NULL && dwBytes != 0) {
					JX_LOG_DEBUG(logger, "os", "Windows x64 %s\n", (const char*)lpBuffer);
					success = true;
				}

				JX_FREE(s_OSContext.m_Allocator, verInfoBuffer);

			version_end:
				FreeLibrary(version);
			}
		}

		if (!success) {
			// NOTE: This isn't accurate. On Win 10 reports Win 8 or greater.
			// Does it require a manifest? If yes, what's the point?
			if (IsWindows10OrGreater()) {
				JX_LOG_DEBUG(logger, "os", "Windows 10 or greater\n");
			} else if (IsWindows8Point1OrGreater()) {
				JX_LOG_DEBUG(logger, "os", "Windows 8.1 or greater\n");
			} else if (IsWindows8OrGreater()) {
				JX_LOG_DEBUG(logger, "os", "Windows 8 or greater\n");
			} else if (IsWindows7SP1OrGreater()) {
				JX_LOG_DEBUG(logger, "os", "Windows 7 SP1 or greater\n");
			} else if (IsWindows7OrGreater()) {
				JX_LOG_DEBUG(logger, "os", "Windows 7 or greater\n");
			} else if (IsWindowsVistaSP2OrGreater()) {
				JX_LOG_DEBUG(logger, "os", "Windows Vista SP2 or greater\n");
			} else if (IsWindowsVistaSP1OrGreater()) {
				JX_LOG_DEBUG(logger, "os", "Windows Vista SP1 or greater\n");
			} else if (IsWindowsVistaOrGreater()) {
				JX_LOG_DEBUG(logger, "os", "Windows Vista or greater\n");
			} else if (IsWindowsXPSP3OrGreater()) {
				JX_LOG_DEBUG(logger, "os", "Windows XP SP3 or greater\n");
			} else if (IsWindowsXPSP2OrGreater()) {
				JX_LOG_DEBUG(logger, "os", "Windows XP SP2 or greater\n");
			} else if (IsWindowsXPSP1OrGreater()) {
				JX_LOG_DEBUG(logger, "os", "Windows XP SP1 or greater\n");
			} else if (IsWindowsXPOrGreater()) {
				JX_LOG_DEBUG(logger, "os", "Windows XP or greater\n");
			} else {
				JX_LOG_DEBUG(logger, "os", "Windows Unknown\n");
			}
		}
	}

	// Presice system timer
	{
		JX_LOG_DEBUG(logger, "os", "Has precise timestamps: %s\n", s_OSContext.GetSystemTimePreciseAsFileTime != NULL ? "Yes" : "No");
	}
}

//////////////////////////////////////////////////////////////////////////
// Init/common functions
//
static bool _jx_os_win32_getInstallFolder(char* path_utf8, uint32_t max)
{
	wchar_t exePathW[1024];
	if (!GetModuleFileNameW(NULL, &exePathW[0], JX_COUNTOF(exePathW))) {
		return false;
	}

	char exePathA[1024];
	if (!jx_utf8from_utf16(exePathA, JX_COUNTOF(exePathA), exePathW, UINT32_MAX)) {
		return false;
	}

	char* lastSlash = (char*)jx_strrchr(exePathA, '\\');
	if (!lastSlash) {
		return false;
	}
	*(lastSlash + 1) = '\0';

	const uint32_t len = (uint32_t)(lastSlash + 1 - exePathA);
	const uint32_t copyLen = max - 1 < len ? max - 1 : len;
	jx_memcpy(path_utf8, exePathA, copyLen);
	path_utf8[copyLen] = '\0';

	return true;
}

static bool _jx_os_win32_getTempFolder(char* path_utf8, uint32_t max)
{
	wchar_t pathW[1024];
	if (!GetTempPathW(JX_COUNTOF(pathW), pathW)) {
		return false;
	}

	if (!jx_utf8from_utf16(path_utf8, max, pathW, UINT32_MAX)) {
		return false;
	}

	return true;
}

static bool _jx_os_win32_getUserDataFolder(char* path_utf8, uint32_t max)
{
	uint32_t pathLen = 0;
	wchar_t* folder = NULL;
	if (FAILED(SHGetKnownFolderPath(&FOLDERID_Documents, 0, NULL, &folder))) {
		goto error;
	}

	pathLen = jx_utf8from_utf16(path_utf8, max, folder, UINT32_MAX);
	if (pathLen == 0) {
		goto error;
	}

	// Make sure the path ends with a forward slash
	if (path_utf8[pathLen - 1] != '\\') {
		if (pathLen == max) {
			goto error;
		}

		path_utf8[pathLen] = '\\';
		path_utf8[pathLen + 1] = '\0';
	}

	CoTaskMemFree(folder);
	return true;

error:
	CoTaskMemFree(folder);
	return false;
}

static bool _jx_os_win32_getUserAppDataFolder(char* path_utf8, uint32_t max)
{
	uint32_t pathLen = 0;
	wchar_t* folder = NULL;
	if (FAILED(SHGetKnownFolderPath(&FOLDERID_LocalAppData, 0, NULL, &folder))) {
		goto error;
	}

	pathLen = jx_utf8from_utf16(path_utf8, max, folder, UINT32_MAX);
	if (pathLen == 0) {
		goto error;
	}

	// Make sure the path ends with a forward slash
	if (path_utf8[pathLen - 1] != '\\') {
		if (pathLen == max) {
			goto error;
		}

		path_utf8[pathLen] = '\\';
		path_utf8[pathLen + 1] = '\0';
	}

	CoTaskMemFree(folder);
	return true;

error:
	CoTaskMemFree(folder);
	return false;
}

static const char* _jx_os_getBaseDirPathUTF8(jx_file_base_dir baseDir)
{
	switch (baseDir) {
	case JX_FILE_BASE_DIR_ABSOLUTE_PATH:
		return "";
	case JX_FILE_BASE_DIR_INSTALL:
		return s_OSContext.m_InstallDir;
	case JX_FILE_BASE_DIR_TEMP:
		return s_OSContext.m_TempDir;
	case JX_FILE_BASE_DIR_USERDATA:
		return s_OSContext.m_UserDataDir;
	case JX_FILE_BASE_DIR_USERAPPDATA:
		return s_OSContext.m_UserAppDataDir;
	default:
		break;
	}

	return NULL;
}

static void _jx_os_win32_initKeycodes(void)
{
	// Taken from sokol_app.h
	// http://www.ee.bgu.ac.il/~microlab/MicroLab/Labs/ScanCodes.htm (not exactly the same values)
	jx_os_virtual_key* scanToKey = &s_OSContext.m_ScanToKey[0];
	scanToKey[0x001] = JX_VKEY_ESCAPE;
	scanToKey[0x002] = JX_VKEY_1;
	scanToKey[0x003] = JX_VKEY_2;
	scanToKey[0x004] = JX_VKEY_3;
	scanToKey[0x005] = JX_VKEY_4;
	scanToKey[0x006] = JX_VKEY_5;
	scanToKey[0x007] = JX_VKEY_6;
	scanToKey[0x008] = JX_VKEY_7;
	scanToKey[0x009] = JX_VKEY_8;
	scanToKey[0x00A] = JX_VKEY_9;
	scanToKey[0x00B] = JX_VKEY_0;
	scanToKey[0x00C] = JX_VKEY_MINUS;
	scanToKey[0x00D] = JX_VKEY_EQUAL;
	scanToKey[0x00E] = JX_VKEY_BACKSPACE;
	scanToKey[0x00F] = JX_VKEY_TAB;
	scanToKey[0x010] = JX_VKEY_Q;
	scanToKey[0x011] = JX_VKEY_W;
	scanToKey[0x012] = JX_VKEY_E;
	scanToKey[0x013] = JX_VKEY_R;
	scanToKey[0x014] = JX_VKEY_T;
	scanToKey[0x015] = JX_VKEY_Y;
	scanToKey[0x016] = JX_VKEY_U;
	scanToKey[0x017] = JX_VKEY_I;
	scanToKey[0x018] = JX_VKEY_O;
	scanToKey[0x019] = JX_VKEY_P;
	scanToKey[0x01A] = JX_VKEY_LEFT_BRACKET;
	scanToKey[0x01B] = JX_VKEY_RIGHT_BRACKET;
	scanToKey[0x01C] = JX_VKEY_ENTER;
	scanToKey[0x01D] = JX_VKEY_LEFT_CONTROL;
	scanToKey[0x01E] = JX_VKEY_A;
	scanToKey[0x01F] = JX_VKEY_S;
	scanToKey[0x020] = JX_VKEY_D;
	scanToKey[0x021] = JX_VKEY_F;
	scanToKey[0x022] = JX_VKEY_G;
	scanToKey[0x023] = JX_VKEY_H;
	scanToKey[0x024] = JX_VKEY_J;
	scanToKey[0x025] = JX_VKEY_K;
	scanToKey[0x026] = JX_VKEY_L;
	scanToKey[0x027] = JX_VKEY_SEMICOLON;
	scanToKey[0x028] = JX_VKEY_APOSTROPHE;
	scanToKey[0x029] = JX_VKEY_GRAVE_ACCENT;
	scanToKey[0x02A] = JX_VKEY_LEFT_SHIFT;
	scanToKey[0x02B] = JX_VKEY_BACKSLASH;
	scanToKey[0x02C] = JX_VKEY_Z;
	scanToKey[0x02D] = JX_VKEY_X;
	scanToKey[0x02E] = JX_VKEY_C;
	scanToKey[0x02F] = JX_VKEY_V;
	scanToKey[0x030] = JX_VKEY_B;
	scanToKey[0x031] = JX_VKEY_N;
	scanToKey[0x032] = JX_VKEY_M;
	scanToKey[0x033] = JX_VKEY_COMMA;
	scanToKey[0x034] = JX_VKEY_PERIOD;
	scanToKey[0x035] = JX_VKEY_SLASH;
	scanToKey[0x036] = JX_VKEY_RIGHT_SHIFT;
	scanToKey[0x037] = JX_VKEY_KP_MULTIPLY;
	scanToKey[0x038] = JX_VKEY_LEFT_ALT;
	scanToKey[0x039] = JX_VKEY_SPACE;
//	scanToKey[0x03A] = JX_VKEY_CAPS_LOCK;
	scanToKey[0x03B] = JX_VKEY_F1;
	scanToKey[0x03C] = JX_VKEY_F2;
	scanToKey[0x03D] = JX_VKEY_F3;
	scanToKey[0x03E] = JX_VKEY_F4;
	scanToKey[0x03F] = JX_VKEY_F5;
	scanToKey[0x040] = JX_VKEY_F6;
	scanToKey[0x041] = JX_VKEY_F7;
	scanToKey[0x042] = JX_VKEY_F8;
	scanToKey[0x043] = JX_VKEY_F9;
	scanToKey[0x044] = JX_VKEY_F10;
//	scanToKey[0x045] = JX_VKEY_PAUSE;
//	scanToKey[0x046] = JX_VKEY_SCROLL_LOCK;
	scanToKey[0x047] = JX_VKEY_KP_7;
	scanToKey[0x048] = JX_VKEY_KP_8;
	scanToKey[0x049] = JX_VKEY_KP_9;
	scanToKey[0x04A] = JX_VKEY_KP_SUBTRACT;
	scanToKey[0x04B] = JX_VKEY_KP_4;
	scanToKey[0x04C] = JX_VKEY_KP_5;
	scanToKey[0x04D] = JX_VKEY_KP_6;
	scanToKey[0x04E] = JX_VKEY_KP_ADD;
	scanToKey[0x04F] = JX_VKEY_KP_1;
	scanToKey[0x050] = JX_VKEY_KP_2;
	scanToKey[0x051] = JX_VKEY_KP_3;
	scanToKey[0x052] = JX_VKEY_KP_0;
	scanToKey[0x053] = JX_VKEY_KP_DECIMAL;

//	scanToKey[0x056] = JX_VKEY_WORLD_2;
	scanToKey[0x057] = JX_VKEY_F11;
	scanToKey[0x058] = JX_VKEY_F12;

//	scanToKey[0x064] = JX_VKEY_F13;
//	scanToKey[0x065] = JX_VKEY_F14;
//	scanToKey[0x066] = JX_VKEY_F15;
//	scanToKey[0x067] = JX_VKEY_F16;
//	scanToKey[0x068] = JX_VKEY_F17;
//	scanToKey[0x069] = JX_VKEY_F18;
//	scanToKey[0x06A] = JX_VKEY_F19;
//	scanToKey[0x06B] = JX_VKEY_F20;
//	scanToKey[0x06C] = JX_VKEY_F21;
//	scanToKey[0x06D] = JX_VKEY_F22;
//	scanToKey[0x06E] = JX_VKEY_F23;

//	scanToKey[0x076] = JX_VKEY_F24;

	scanToKey[0x11C] = JX_VKEY_KP_ENTER;
	scanToKey[0x11D] = JX_VKEY_RIGHT_CONTROL;

	scanToKey[0x135] = JX_VKEY_KP_DIVIDE;

//	scanToKey[0x137] = JX_VKEY_PRINT_SCREEN;
	scanToKey[0x138] = JX_VKEY_RIGHT_ALT;

//	scanToKey[0x145] = JX_VKEY_NUM_LOCK;
//	scanToKey[0x146] = JX_VKEY_PAUSE;
	scanToKey[0x147] = JX_VKEY_HOME;
	scanToKey[0x148] = JX_VKEY_UP;
	scanToKey[0x149] = JX_VKEY_PAGE_UP;

	scanToKey[0x14B] = JX_VKEY_LEFT;

	scanToKey[0x14D] = JX_VKEY_RIGHT;

	scanToKey[0x14F] = JX_VKEY_END;

	scanToKey[0x150] = JX_VKEY_DOWN;
	scanToKey[0x151] = JX_VKEY_PAGE_DOWN;
	scanToKey[0x152] = JX_VKEY_INSERT;
	scanToKey[0x153] = JX_VKEY_DELETE;

	scanToKey[0x15B] = JX_VKEY_LEFT_SUPER;
	scanToKey[0x15C] = JX_VKEY_RIGHT_SUPER;
//	scanToKey[0x15D] = JX_VKEY_MENU;
}

static void _jx_os_win32_initCursors(void)
{
	HCURSOR* cursors = &s_OSContext.m_Cursors[0];
	cursors[JX_CURSOR_TYPE_ARROW] = LoadCursor(NULL, IDC_ARROW);
	cursors[JX_CURSOR_TYPE_HAND] = LoadCursor(NULL, IDC_HAND);
	cursors[JX_CURSOR_TYPE_I_BAR] = LoadCursor(NULL, IDC_IBEAM);
	cursors[JX_CURSOR_TYPE_MOVE_XY] = LoadCursor(NULL, IDC_SIZE);
	cursors[JX_CURSOR_TYPE_RESIZE_X] = LoadCursor(NULL, IDC_SIZEWE);
	cursors[JX_CURSOR_TYPE_RESIZE_Y] = LoadCursor(NULL, IDC_SIZENS);
}

//////////////////////////////////////////////////////////////////////////
// Module
// 
static jx_os_module_t* _jx_os_moduleOpen(jx_file_base_dir baseDir, const char* path_utf8)
{
	const char* baseDirPath = _jx_os_getBaseDirPathUTF8(baseDir);
	if (!baseDirPath) {
		return NULL;
	}

	char absPath[1024];
	jx_snprintf(absPath, JX_COUNTOF(absPath), "%s%s", baseDirPath, path_utf8);

	wchar_t absPathW[1024];
	if (!jx_utf8to_utf16((uint16_t*)absPathW, JX_COUNTOF(absPathW), absPath, SIZE_MAX, NULL)) {
		return NULL;
	}

	jx_os_module_t* module = (jx_os_module_t*)LoadLibraryW(absPathW);
#if JX_CONFIG_TRACE_ALLOCATIONS
	if (module) {
		memory_tracer_api->onModuleLoaded(absPath, (uint64_t)module);
	}
#endif
	return module;
}

static void _jx_os_moduleClose(jx_os_module_t* mod)
{
	FreeLibrary((HMODULE)mod);
}

static void* _jx_os_moduleGetSymbolAddr(jx_os_module_t* mod, const char* symbolName)
{
	return (void*)GetProcAddress((HMODULE)mod, symbolName);
}

//////////////////////////////////////////////////////////////////////////
// Window
//
typedef enum jx_os_win32_window_flags
{
	JWIN32_WINDOW_FLAGS_MOUSE_TRACKED = 1u << 0,
	JWIN32_WINDOW_FLAGS_MINIMIZED = 1u << 1,
	JWIN32_WINDOW_FLAGS_READY = 1u << 2,
} jx_os_win32_window_flags;

typedef struct jx_os_window_t
{
	HWND m_Handle;
	jx_os_window_desc_t m_Desc;
	uint32_t m_Flags;
	jx_os_cursor_type m_Cursor;
	float m_MousePos[2];
	uint16_t m_Size[2];
} jx_os_window_t;

static LRESULT CALLBACK _jx_os_win32WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

static jx_os_window_t* _jx_os_windowOpen(const jx_os_window_desc_t* desc)
{
	jx_allocator_i* allocator = s_OSContext.m_Allocator;

	jx_os_window_t* win = (jx_os_window_t*)JX_ALLOC(allocator, sizeof(jx_os_window_t));
	if (!win) {
		return NULL;
	}

	jx_memset(win, 0, sizeof(jx_os_window_t));
	jx_memcpy(&win->m_Desc, desc, sizeof(jx_os_window_desc_t));

	RECT desktopRect;
	GetClientRect(GetDesktopWindow(), &desktopRect);
	const uint32_t desktopWidth = (uint32_t)(desktopRect.right - desktopRect.left);
	const uint32_t desktopHeight = (uint32_t)(desktopRect.bottom - desktopRect.top);

	const bool fullscreen = (desc->m_Flags & JX_WINDOW_FLAGS_FULLSCREEN_Msk) != 0;
	const bool centerToDesktop = (desc->m_Flags & JX_WINDOW_FLAGS_CENTER_Msk) != 0;
	const uint32_t requestedWinWidth = desc->m_Width;
	const uint32_t requestedWinHeight = desc->m_Height;
	const uint32_t winWidth = (fullscreen || requestedWinWidth > desktopWidth) ? desktopWidth : requestedWinWidth;
	const uint32_t winHeight = (fullscreen || requestedWinHeight > desktopHeight) ? desktopHeight : requestedWinHeight;
	const char* title = desc->m_Title;

	HINSTANCE instance = (HINSTANCE)GetModuleHandleW(NULL);

	HICON icon = LoadIcon(NULL, IDI_APPLICATION);

	WNDCLASSEXW wnd;
	jx_memset(&wnd, 0, sizeof(WNDCLASSEXW));
	wnd.cbSize = sizeof(WNDCLASSEXW);
	wnd.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
	wnd.lpfnWndProc = _jx_os_win32WndProc;
	wnd.hInstance = instance;
	wnd.hIcon = icon;
	wnd.hIconSm = icon;
	wnd.lpszClassName = L"jlib_window";
	wnd.hCursor = NULL;
	RegisterClassExW(&wnd);

	const DWORD winExStyle = WS_EX_APPWINDOW | WS_EX_WINDOWEDGE;
	const DWORD winStyle = fullscreen
		? (WS_POPUP | WS_SYSMENU | WS_VISIBLE)
		: (WS_CLIPCHILDREN | WS_CLIPSIBLINGS | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SIZEBOX)
		;

	RECT rect;
	if (centerToDesktop) {
		rect.left = (desktopWidth - winWidth) / 2;
		rect.top = (desktopHeight - winHeight) / 2;
	} else {
		rect.left = 0;
		rect.top = 0;
	}
	rect.right = rect.left + winWidth;
	rect.bottom = rect.top + winHeight;

	AdjustWindowRectEx(&rect, winStyle, false, winExStyle);
	if (rect.left < 0) {
		rect.right += rect.left;
		rect.left = 0;
	}
	if (rect.top < 0) {
		rect.bottom += rect.top;
		rect.top = 0;
	}

	wchar_t titleW[256];
	jx_utf8to_utf16(titleW, JX_COUNTOF(titleW), title, UINT32_MAX, NULL);
	win->m_Handle = CreateWindowExW(
		winExStyle
		, L"jlib_window"
		, titleW
		, winStyle
		, rect.left
		, rect.top
		, (rect.right - rect.left)
		, (rect.bottom - rect.top)
		, NULL
		, NULL
		, instance
		, 0
	);
	if (!win->m_Handle) {
		JX_FREE(allocator, win);
		return NULL;
	}

	SetWindowLongPtrW(win->m_Handle, GWLP_USERDATA, (LONG_PTR)win);
	ShowWindow(win->m_Handle, SW_SHOWNORMAL);

	win->m_Cursor = JX_OS_NUM_CURSORS;
	_jx_os_windowSetCursor(win, JX_CURSOR_TYPE_DEFAULT);

	win->m_Flags |= JWIN32_WINDOW_FLAGS_READY;

	RECT winRect;
	GetClientRect(win->m_Handle, &winRect);
	win->m_Size[0] = (uint16_t)(winRect.right - winRect.left);
	win->m_Size[1] = (uint16_t)(winRect.bottom - winRect.top);

	if (s_OSContext.m_MainWindow == NULL) {
		s_OSContext.m_MainWindow = win;
	}

	return win;
}

static void _jx_os_windowClose(jx_os_window_t* win)
{
	if ((win->m_Flags & JWIN32_WINDOW_FLAGS_READY) != 0) {
		DestroyWindow(win->m_Handle);
		win->m_Flags &= ~JWIN32_WINDOW_FLAGS_READY;
	}
}

static void _jx_os_windowSetTitle(jx_os_window_t* win, const char* title)
{
	wchar_t titleW[512];
	jx_utf8to_utf16(titleW, JX_COUNTOF(titleW), title, UINT32_MAX, NULL);
	SetWindowTextW(win->m_Handle, titleW);
}

static void _jx_os_windowSetCursor(jx_os_window_t* win, jx_os_cursor_type cursor)
{
	win->m_Cursor = cursor;
}

static bool _jx_os_windowSetIcon(jx_os_window_t* win, const jx_os_icon_desc_t* iconDesc)
{
	if (!iconDesc->m_Data) {
		return false;
	}

	const uint16_t iconWidth = iconDesc->m_Width;
	const uint16_t iconHeight = iconDesc->m_Height;
	const uint8_t* iconData = iconDesc->m_Data;
	const jx_os_icon_format iconFormat = iconDesc->m_Format;

	BITMAPV5HEADER bi;
	jx_memset(&bi, 0, sizeof(BITMAPV5HEADER));
	bi.bV5Size = sizeof(BITMAPV5HEADER);
	bi.bV5Width = (LONG)iconWidth;
	bi.bV5Height = -(LONG)iconHeight;
	bi.bV5Planes = 1;
	bi.bV5BitCount = 32;
	bi.bV5Compression = BI_BITFIELDS;
	bi.bV5RedMask   = 0x00FF0000;
	bi.bV5GreenMask = 0x0000FF00;
	bi.bV5BlueMask  = 0x000000FF;
	bi.bV5AlphaMask = 0xFF000000;

	uint8_t* rgba = NULL;
	HDC dc = GetDC(NULL);
	HBITMAP colorBitmap = CreateDIBSection(dc, (BITMAPINFO*)&bi, DIB_RGB_COLORS, (void**)&rgba, NULL, (DWORD)0);
	ReleaseDC(NULL, dc);
	if (!colorBitmap) {
		return false;
	}

	HBITMAP mask = CreateBitmap((int)iconWidth, (int)iconHeight, 1, 1, NULL);
	if (!mask) {
		DeleteObject(colorBitmap);
		return false;
	}

	if (iconFormat == JX_ICON_FORMAT_L1) {
		jx_img_convert_L1_to_RGBA8(rgba, iconData, iconWidth, iconHeight);
	} else if (iconFormat == JX_ICON_FORMAT_L1_A1) {
		jx_img_convert_L1_A1_to_RGBA8(rgba, iconData, iconWidth, iconHeight);
	} else if (iconFormat == JX_ICON_FORMAT_RGBA8) {
		jx_img_convert_BGRA8_to_RGBA8(rgba, iconData, iconWidth, iconHeight);
	} else {
		// Unknown icon type
		JX_CHECK(false, "os: Unkown icon format %d.", iconFormat);
		DeleteObject(colorBitmap);
		return false;
	}

	ICONINFO ii;
	jx_memset(&ii, 0, sizeof(ICONINFO));
	ii.fIcon = true;
	ii.xHotspot = 0;
	ii.yHotspot = 0;
	ii.hbmMask = mask;
	ii.hbmColor = colorBitmap;

	HICON handle = CreateIconIndirect(&ii);

	DeleteObject(colorBitmap);
	DeleteObject(mask);

	if (!handle) {
		return false;
	}

	SendMessageW(win->m_Handle, WM_SETICON, ICON_SMALL, (LPARAM)handle);
	SendMessageW(win->m_Handle, WM_SETICON, ICON_BIG, (LPARAM)handle);

	return true;
}

static void _jx_os_windowGetResolution(jx_os_window_t* win, uint16_t* res)
{
	res[0] = win->m_Size[0];
	res[1] = win->m_Size[1];
}

static uint32_t _jx_os_windowGetFlags(jx_os_window_t* win)
{
	return win->m_Desc.m_Flags;
}

static void* _jx_os_windowGetNativeHandle(jx_os_window_t* win)
{
	return win->m_Handle;
}

static bool _jx_os_windowClipboardGetString(jx_os_window_t* win, char** str, uint32_t* len, jx_allocator_i* allocator)
{
	if (!IsClipboardFormatAvailable(CF_TEXT)) {
		return false;
	}

	if (!OpenClipboard(win->m_Handle)) {
		return false;
	}

	HGLOBAL handle = GetClipboardData(CF_TEXT);
	if (handle != NULL) {
		const char* src = GlobalLock(handle);
		if (src != NULL) {
			const uint32_t srcLen = jx_strlen(src);

			*str = (char*)JX_ALLOC(allocator, srcLen + 1);
			if (!*str) {
				GlobalUnlock(handle);
				CloseClipboard();
				return false;
			}

			jx_memcpy(*str, src, srcLen);
			(*str)[srcLen] = '\0';

			if (len) {
				*len = srcLen;
			}
			
			GlobalUnlock(handle);
		}
	}
	CloseClipboard();

	return true;
}

static bool _jx_os_windowClipboardSetString(jx_os_window_t* win, const char* str, uint32_t len)
{
	if (!OpenClipboard(win->m_Handle)) {
		return false;
	}

	EmptyClipboard();

	if (len == 0) {
		CloseClipboard();
		return true;
	}

	HGLOBAL copyHandle = GlobalAlloc(GMEM_MOVEABLE, (len + 1) * sizeof(char));
	if (copyHandle == NULL) {
		CloseClipboard();
		return false;
	}

	char* copyStr = (char*)GlobalLock(copyHandle);
	jx_memcpy(copyStr, str, len);
	copyStr[len] = '\0';
	GlobalUnlock(copyHandle);

	SetClipboardData(CF_TEXT, copyHandle);

	CloseClipboard();

	return true;
}

static jx_os_frame_tick_result _jx_os_frameTick()
{
	jx_os_frame_tick_result res = JX_FRAME_TICK_CONTINUE;

	MSG msg;
	while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
		if (msg.message == WM_QUIT) {
			if (s_OSContext.m_MainWindow == NULL) {
				res = JX_FRAME_TICK_QUIT;
			}
		} else {
			TranslateMessage(&msg);
			DispatchMessageW(&msg);
		}
	}

	return res;
}

static void _jx_os_windowSendEvent(jx_os_window_t* win, const jx_os_window_event_t* ev)
{
	if (win->m_Desc.m_EventCb) {
		win->m_Desc.m_EventCb(win, ev, win->m_Desc.m_EventCbUserData);
	}
}

static uint32_t _jx_os_getKeyModifiers()
{
	return 0
		| JX_KEY_MODIFIER_SHIFT(GetKeyState(VK_SHIFT) >> 31)
		| JX_KEY_MODIFIER_CONTROL(GetKeyState(VK_CONTROL) >> 31)
		| JX_KEY_MODIFIER_ALT(GetKeyState(VK_MENU) >> 31)
		| JX_KEY_MODIFIER_SUPER((GetKeyState(VK_LWIN) | GetKeyState(VK_RWIN)) >> 31)
		;
}

static LRESULT CALLBACK _jx_os_win32WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	jx_os_window_t* win = (jx_os_window_t*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);
	if (win == NULL || (win->m_Flags & JWIN32_WINDOW_FLAGS_READY) == 0) {
		return DefWindowProcW(hWnd, uMsg, wParam, lParam);
	}

	switch (uMsg) {
	case WM_CLOSE: {
		JOS_TRACE("os: WM_CLOSE on %08X", hWnd);
		DestroyWindow(win->m_Handle);
		win->m_Flags &= ~JWIN32_WINDOW_FLAGS_READY;
		return 0;
	} break;
	case WM_DESTROY: {
		JOS_TRACE("os: WM_DESTROY on %08X", hWnd);
		if (win == s_OSContext.m_MainWindow) {
			s_OSContext.m_MainWindow = NULL;
		}
		JX_FREE(s_OSContext.m_Allocator, win);

		if (s_OSContext.m_MainWindow == NULL) {
			PostQuitMessage(0);
		}
		return 0;
	} break;
	case WM_SYSCOMMAND: {
		JOS_TRACE("os: WM_SYSCOMMAND on %08X", hWnd);
		switch (wParam & 0xFFF0) {
		case SC_SCREENSAVE:
		case SC_MONITORPOWER:
			if ((win->m_Desc.m_Flags & JX_WINDOW_FLAGS_FULLSCREEN_Msk) != 0) {
				// Disable screen saver and blanking in fullscreen mode
				return 0;
			}
			break;
		case SC_KEYMENU:
			// User trying to access menu via ALT
			return 0;
		}
	} break;
	case WM_ERASEBKGND: {
		JOS_TRACE("os: WM_ERASEBKGND on %08X", hWnd);
		return 1;
	} break;
	case WM_SIZE: {
		JOS_TRACE("os: WM_SIZE on %08X", hWnd);
		const bool newMinimized = wParam == SIZE_MINIMIZED;
		const bool curMinimized = (win->m_Flags & JWIN32_WINDOW_FLAGS_MINIMIZED) != 0;
		if (newMinimized != curMinimized) {
			if (newMinimized) {
				win->m_Flags |= JWIN32_WINDOW_FLAGS_MINIMIZED;
				_jx_os_windowSendEvent(win, &(jx_os_window_event_t) {
					.m_Type = JX_WINDOW_EVENT_TYPE_MINIMIZED
				});
			} else {
				win->m_Flags &= ~JWIN32_WINDOW_FLAGS_MINIMIZED;
				_jx_os_windowSendEvent(win, &(jx_os_window_event_t) {
					.m_Type = JX_WINDOW_EVENT_TYPE_RESTORED
				});
			}
		}

		RECT rect;
		bool success = GetClientRect(win->m_Handle, &rect);
		if (success) {
			win->m_Size[0] = (uint16_t)(rect.right - rect.left);
			win->m_Size[1] = (uint16_t)(rect.bottom - rect.top);
		} else {
			win->m_Size[0] = 1;
			win->m_Size[1] = 1;
		}

		// TODO: Inform renderer backend for the new size...

		_jx_os_windowSendEvent(win, &(jx_os_window_event_t) {
			.m_Type = JX_WINDOW_EVENT_TYPE_RESIZED
		});
	} break;
	case WM_LBUTTONDOWN: {
		JOS_TRACE("os: WM_LBUTTONDOWN on %08X", hWnd);
		_jx_os_windowSendEvent(win, &(jx_os_window_event_t){
			.m_Type = JX_WINDOW_EVENT_TYPE_MOUSE_BUTTON_DOWN,
			.m_MouseButton = JX_MOUSE_BUTTON_LEFT,
			.m_MousePos = {
				win->m_MousePos[0],
				win->m_MousePos[1]
			},
			.m_KeyModifiers = _jx_os_getKeyModifiers()
		});
	} break;
	case WM_RBUTTONDOWN: {
		JOS_TRACE("os: WM_RBUTTONDOWN on %08X", hWnd);
		_jx_os_windowSendEvent(win, &(jx_os_window_event_t){
			.m_Type = JX_WINDOW_EVENT_TYPE_MOUSE_BUTTON_DOWN,
			.m_MouseButton = JX_MOUSE_BUTTON_RIGHT,
			.m_MousePos = {
				win->m_MousePos[0],
				win->m_MousePos[1]
			},
			.m_KeyModifiers = _jx_os_getKeyModifiers()
		});
	} break;
	case WM_MBUTTONDOWN: {
		JOS_TRACE("os: WM_MBUTTONDOWN on %08X", hWnd);
		_jx_os_windowSendEvent(win, &(jx_os_window_event_t){
			.m_Type = JX_WINDOW_EVENT_TYPE_MOUSE_BUTTON_DOWN,
			.m_MouseButton = JX_MOUSE_BUTTON_MIDDLE,
			.m_MousePos = {
				win->m_MousePos[0],
				win->m_MousePos[1]
			},
			.m_KeyModifiers = _jx_os_getKeyModifiers()
		});
	} break;
	case WM_LBUTTONUP: {
		JOS_TRACE("os: WM_LBUTTONUP on %08X", hWnd);
		_jx_os_windowSendEvent(win, &(jx_os_window_event_t){
			.m_Type = JX_WINDOW_EVENT_TYPE_MOUSE_BUTTON_UP,
			.m_MouseButton = JX_MOUSE_BUTTON_LEFT,
			.m_MousePos = {
				win->m_MousePos[0],
				win->m_MousePos[1]
			},
			.m_KeyModifiers = _jx_os_getKeyModifiers()
		});
	} break;
	case WM_RBUTTONUP: {
		JOS_TRACE("os: WM_RBUTTONUP on %08X", hWnd);
		_jx_os_windowSendEvent(win, &(jx_os_window_event_t){
			.m_Type = JX_WINDOW_EVENT_TYPE_MOUSE_BUTTON_UP,
			.m_MouseButton = JX_MOUSE_BUTTON_RIGHT,
			.m_MousePos = {
				win->m_MousePos[0],
				win->m_MousePos[1]
			},
			.m_KeyModifiers = _jx_os_getKeyModifiers()
		});
	} break;
	case WM_MBUTTONUP: {
		JOS_TRACE("os: WM_MBUTTONUP on %08X", hWnd);
		_jx_os_windowSendEvent(win, &(jx_os_window_event_t){
			.m_Type = JX_WINDOW_EVENT_TYPE_MOUSE_BUTTON_UP,
			.m_MouseButton = JX_MOUSE_BUTTON_MIDDLE,
			.m_MousePos = {
				win->m_MousePos[0],
				win->m_MousePos[1]
			},
			.m_KeyModifiers = _jx_os_getKeyModifiers()
		});
	} break;
	case WM_MOUSEMOVE: {
		JOS_TRACE("os: WM_MOUSEMOVE on %08X", hWnd);
		win->m_MousePos[0] = (float)GET_X_LPARAM(lParam);
		win->m_MousePos[1] = (float)GET_Y_LPARAM(lParam);

		const bool mouseTracked = (win->m_Flags & JWIN32_WINDOW_FLAGS_MOUSE_TRACKED) != 0;
		if (!mouseTracked) {
			win->m_Flags |= JWIN32_WINDOW_FLAGS_MOUSE_TRACKED;

			TRACKMOUSEEVENT tme;
			jx_memset(&tme, 0, sizeof(TRACKMOUSEEVENT));
			tme.cbSize = sizeof(TRACKMOUSEEVENT);
			tme.dwFlags = TME_LEAVE;
			tme.hwndTrack = win->m_Handle;
			TrackMouseEvent(&tme);

			_jx_os_windowSendEvent(win, &(jx_os_window_event_t){
				.m_Type = JX_WINDOW_EVENT_TYPE_MOUSE_ENTER,
				.m_MouseButton = JX_OS_NUM_MOUSE_BUTTONS,
				.m_MousePos = {
					win->m_MousePos[0],
					win->m_MousePos[1]
				},
				.m_KeyModifiers = _jx_os_getKeyModifiers()
			});
		}

		_jx_os_windowSendEvent(win, &(jx_os_window_event_t){
			.m_Type = JX_WINDOW_EVENT_TYPE_MOUSE_MOVE,
			.m_MouseButton = JX_OS_NUM_MOUSE_BUTTONS,
			.m_MousePos = {
				win->m_MousePos[0],
				win->m_MousePos[1]
			},
			.m_KeyModifiers = _jx_os_getKeyModifiers()
		});
	} break;
	case WM_MOUSELEAVE: {
		JOS_TRACE("os: WM_MOUSELEAVE on %08X", hWnd);
		win->m_Flags &= ~JWIN32_WINDOW_FLAGS_MOUSE_TRACKED;
		_jx_os_windowSendEvent(win, &(jx_os_window_event_t){
			.m_Type = JX_WINDOW_EVENT_TYPE_MOUSE_LEAVE,
			.m_MouseButton = JX_OS_NUM_MOUSE_BUTTONS,
			.m_MousePos = {
				win->m_MousePos[0],
				win->m_MousePos[1]
			},
			.m_KeyModifiers = _jx_os_getKeyModifiers()
		});
	} break;
	case WM_MOUSEWHEEL: {
		JOS_TRACE("os: WM_MOUSEWHEEL on %08X", hWnd);
		_jx_os_windowSendEvent(win, &(jx_os_window_event_t) {
			.m_Type = JX_WINDOW_EVENT_TYPE_MOUSE_SCROLL,
			.m_Scroll = {
				0.0f,
				(float)((SHORT)HIWORD(wParam)) / 30.0f
			}
		});
	} break;
	case WM_MOUSEHWHEEL: {
		JOS_TRACE("os: WM_MOUSEHWHEEL on %08X", hWnd);
		_jx_os_windowSendEvent(win, &(jx_os_window_event_t) {
			.m_Type = JX_WINDOW_EVENT_TYPE_MOUSE_SCROLL,
			.m_Scroll = {
				-(float)((SHORT)HIWORD(wParam)) / 30.0f, 
				0.0f
			}
		});
	} break;
	case WM_CHAR:
	case WM_SYSCHAR:
	case WM_UNICHAR: {
		JOS_TRACE("os: WM_CHAR on %08X", hWnd);
		if ((wParam >= 32 && wParam <= 126) || wParam >= 160) {
			_jx_os_windowSendEvent(win, &(jx_os_window_event_t){
				.m_Type = JX_WINDOW_EVENT_TYPE_CHAR,
				.m_CharCode = (uint32_t)wParam,
				.m_KeyModifiers = _jx_os_getKeyModifiers()
			});
		}
	} break;
	case WM_KEYDOWN:
	case WM_SYSKEYDOWN: {
		JOS_TRACE("os: WM_KEYDOWN on %08X", hWnd);
		const uint32_t scanCode = (uint32_t)((lParam & 0x01FF0000) >> 16);
		if (scanCode < JX_COUNTOF(s_OSContext.m_ScanToKey)) {
			const jx_os_virtual_key vk = s_OSContext.m_ScanToKey[scanCode];
			if (vk != JX_VKEY_INVALID) {
				_jx_os_windowSendEvent(win, &(jx_os_window_event_t){
					.m_Type = JX_WINDOW_EVENT_TYPE_KEY_DOWN,
					.m_VirtualKey = vk,
					.m_KeyModifiers = _jx_os_getKeyModifiers()
				});
			}
		}
	} break;
	case WM_KEYUP:
	case WM_SYSKEYUP: {
		JOS_TRACE("os: WM_KEYUP on %08X", hWnd);
		const uint32_t scanCode = (uint32_t)((lParam & 0x01FF0000) >> 16);
		if (scanCode < JX_COUNTOF(s_OSContext.m_ScanToKey)) {
			const jx_os_virtual_key vk = s_OSContext.m_ScanToKey[scanCode];
			if (vk != JX_VKEY_INVALID) {
				_jx_os_windowSendEvent(win, &(jx_os_window_event_t){
					.m_Type = JX_WINDOW_EVENT_TYPE_KEY_UP,
					.m_VirtualKey = vk,
					.m_KeyModifiers = _jx_os_getKeyModifiers()
				});
			}
		}
	} break;
	case WM_GETICON: {
		JOS_TRACE("os: WM_GETICON on %08X", hWnd);
	} break;
	case WM_GETMINMAXINFO: {
		JOS_TRACE("os: WM_GETMINMAXINFO on %08X", hWnd);
	} break;
	case WM_SETCURSOR: {
		JOS_TRACE("os: WM_SETCURSOR on %08X, lParam: %08X", hWnd, lParam);
		SetCursor(s_OSContext.m_Cursors[win->m_Cursor]);
	} break;
	case WM_PAINT: {
		JOS_TRACE("os: WM_PAINT on %08X", hWnd);
	} break;
	case WM_CONTEXTMENU: {
		JOS_TRACE("os: WM_CONTEXTMENU on %08X", hWnd);
	} break;
	case WM_WINDOWPOSCHANGING: {
		JOS_TRACE("os: WM_WINDOWPOSCHANGING on %08X", hWnd);
	} break;
	case WM_WINDOWPOSCHANGED: {
		JOS_TRACE("os: WM_WINDOWPOSCHANGED on %08X", hWnd);
	} break;
	case WM_ACTIVATE: {
		JOS_TRACE("os: WM_ACTIVATE on %08X", hWnd);
	} break;
	case WM_ACTIVATEAPP: {
		JOS_TRACE("os: WM_ACTIVATEAPP on %08X", hWnd);
	} break;
	case WM_KILLFOCUS: {
		JOS_TRACE("os: WM_KILLFOCUS on %08X", hWnd);
	} break;
	default:
		JOS_TRACE("os: 0x%04X on %08X", uMsg, hWnd);
		break;
	}

	return DefWindowProcW(hWnd, uMsg, wParam, lParam);
}

//////////////////////////////////////////////////////////////////////////
// Timers
//
static jx_os_timer_t* _jx_os_timerCreate()
{
	HANDLE timer = CreateWaitableTimerExW(NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
	return (jx_os_timer_t*)timer;
}

static void _jx_os_timerDestroy(jx_os_timer_t* timer)
{
	if (timer) {
		CloseHandle((HANDLE)timer);
	}
}

static bool _jx_os_timerSleep(jx_os_timer_t* timer, int64_t duration_us)
{
	LARGE_INTEGER liDueTime;
	
	// Convert from microseconds to 100 of ns, and negative for relative time.
	liDueTime.QuadPart = -(duration_us * 10);

	if (!SetWaitableTimer((HANDLE)timer, &liDueTime, 0, NULL, NULL, 0)) {
		return false;
	}

	return WaitForSingleObject((HANDLE)timer, INFINITE) == WAIT_OBJECT_0;
}

//////////////////////////////////////////////////////////////////////////
// Time
//
static int64_t _jx_os_timeNow(void)
{
	LARGE_INTEGER counter;
	QueryPerformanceCounter(&counter);
	return counter.QuadPart;
}

static int64_t _jx_os_timeDiff(int64_t end, int64_t start)
{
	return end - start;
}

static int64_t _jx_os_timeSince(int64_t start)
{
	return _jx_os_timeNow() - start;
}

static int64_t _jx_os_timeLapTime(int64_t* timer)
{
	const int64_t now = _jx_os_timeNow();
	const int64_t dt = *timer == 0
		? 0
		: _jx_os_timeDiff(now, *timer)
		;
	*timer = now;
	return dt;
}

static double _jx_os_timeConvertTo(int64_t delta, jx_os_time_units units)
{
	static const double kTimeConversionFactor[] = {
		1.0,          // JX_TIME_UNITS_SEC
		1000.0,       // JX_TIME_UNITS_MS
		1000000.0,    // JX_TIME_UNITS_US
		1000000000.0, // JX_TIME_UNITS_NS
	};
	return (double)delta * (kTimeConversionFactor[units] / (double)s_OSContext.m_TimerFreq);
}

//////////////////////////////////////////////////////////////////////////
// Timestamp
//
static uint64_t _jx_os_timestampNow(void)
{
	FILETIME fileTime;
	GetSystemTimeAsFileTime(&fileTime);
	return (((uint64_t)fileTime.dwHighDateTime) << 32) | ((uint64_t)fileTime.dwLowDateTime);
}

static uint64_t _jx_os_timestampNow_precise(void)
{
	FILETIME fileTime;
	s_OSContext.GetSystemTimePreciseAsFileTime(&fileTime);
	return (((uint64_t)fileTime.dwHighDateTime) << 32) | ((uint64_t)fileTime.dwLowDateTime);
}

static int64_t _jx_os_timestampDiff(uint64_t end, uint64_t start)
{
	return end - start;
}

static int64_t _jx_os_timestampSince(uint64_t start)
{
	return os_api->timestampNow() - start;
}

static double _jx_os_timestampConvertTo(int64_t delta, jx_os_time_units units)
{
	static const double kTimeConversionFactor[] = {
		10000000.0, // JX_TIME_UNITS_SEC
		10000.0,    // JX_TIME_UNITS_MS
		10.0,       // JX_TIME_UNITS_US
		0.001,      // JX_TIME_UNITS_NS
	};
	return (double)delta / kTimeConversionFactor[units];
}

static uint32_t _jx_os_timestampToString(uint64_t ts, char* buffer, uint32_t max)
{
	FILETIME fileTime;
	fileTime.dwLowDateTime = (uint32_t)((ts & 0x00000000FFFFFFFFull) >> 0);
	fileTime.dwHighDateTime = (uint32_t)((ts & 0xFFFFFFFF00000000ull) >> 32);

	SYSTEMTIME systemTime;
	FileTimeToSystemTime(&fileTime, &systemTime);
	
	return jx_snprintf(buffer, max, "%04u-%02u-%02u %02u:%02u:%02u.%03u "
		, systemTime.wYear
		, systemTime.wMonth
		, systemTime.wDay
		, systemTime.wHour
		, systemTime.wMinute
		, systemTime.wSecond
		, systemTime.wMilliseconds);
}

//////////////////////////////////////////////////////////////////////////
// Console
//
#include <stdio.h>
#include <conio.h>

static int32_t _jx_os_consoleOpen(void)
{
	if (!AttachConsole(ATTACH_PARENT_PROCESS)) {
		if (!AllocConsole()) {
			return JX_ERROR_OPEN_CONSOLE;
		}
	}
	
	(void)freopen("CON", "w", stdout);
	(void)freopen("CON", "w", stderr);

	SetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
	SetConsoleMode(GetStdHandle(STD_ERROR_HANDLE), ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING);

	return JX_ERROR_NONE;
}

static void _jx_os_consoleClose(bool waitForUserInput)
{
	if (waitForUserInput) {
		WriteConsoleA(GetStdHandle(STD_OUTPUT_HANDLE), "\nPress any key to close console...\n", 35, NULL, NULL);

		HANDLE stdinHandle = GetStdHandle(STD_INPUT_HANDLE);
		DWORD prevMode = 0;
		GetConsoleMode(stdinHandle, &prevMode);
		SetConsoleMode(stdinHandle, prevMode & ~ENABLE_LINE_INPUT);

		uint8_t ch;
		DWORD numCharsRead;
		ReadConsoleA(GetStdHandle(STD_INPUT_HANDLE), &ch, 1, &numCharsRead, NULL);
	}

	FreeConsole();
}

static int32_t _jx_os_consolePuts(const char* str, uint32_t len)
{
	len = len == UINT32_MAX
		? jx_strlen(str)
		: len
		;

	uint32_t charsWritten;
	if (!WriteConsoleA(GetStdHandle(STD_OUTPUT_HANDLE), str, len, &charsWritten, NULL)) {
		return -1;
	}

	return (int32_t)charsWritten;
}

//////////////////////////////////////////////////////////////////////////
// Mutex
//
static jx_os_mutex_t* _jx_os_mutexCreate(void)
{
	CRITICAL_SECTION* cs = (CRITICAL_SECTION*)JX_ALLOC(s_OSContext.m_Allocator, sizeof(CRITICAL_SECTION));
	InitializeCriticalSection(cs);
	return (jx_os_mutex_t*)cs;
}

static void _jx_os_mutexDestroy(jx_os_mutex_t* mutex)
{
	DeleteCriticalSection((CRITICAL_SECTION*)mutex);
	JX_FREE(s_OSContext.m_Allocator, mutex);
}

static void _jx_os_mutexLock(jx_os_mutex_t* mutex)
{
	EnterCriticalSection((CRITICAL_SECTION*)mutex);
}

static bool _jx_os_mutexTryLock(jx_os_mutex_t* mutex)
{
	return TryEnterCriticalSection((CRITICAL_SECTION*)mutex) != 0;
}

static void _jx_os_mutexUnlock(jx_os_mutex_t* mutex)
{
	LeaveCriticalSection((CRITICAL_SECTION*)mutex);
}

//////////////////////////////////////////////////////////////////////////
// Semaphores
//
static jx_os_semaphore_t* _jx_os_semaphoreCreate(void)
{
	HANDLE sem = CreateSemaphoreA(NULL, 0, LONG_MAX, NULL);
	return (jx_os_semaphore_t*)sem;
}

static void _jx_os_semaphoreDestroy(jx_os_semaphore_t* semaphore)
{
	CloseHandle((HANDLE)semaphore);
}

static void _jx_os_semaphoreSignal(jx_os_semaphore_t* semaphore, uint32_t count)
{
	ReleaseSemaphore((HANDLE)semaphore, count, NULL);
}

static bool _jx_os_semaphoreWait(jx_os_semaphore_t* semaphore, uint32_t msecs)
{
	return WaitForSingleObject((HANDLE)semaphore, msecs) == WAIT_OBJECT_0;
}

//////////////////////////////////////////////////////////////////////////
// Events
// 
static jx_os_event_t* _jx_os_eventCreate(bool manualReset, bool initialState, const char* name)
{
	HANDLE ev = CreateEventA(NULL, manualReset, initialState, name);
	return (jx_os_event_t*)ev;
}

static void _jx_os_eventDestroy(jx_os_event_t* ev)
{
	CloseHandle((HANDLE)ev);
}

static bool _jx_os_eventSet(jx_os_event_t* ev)
{
	return SetEvent((HANDLE)ev) != FALSE;
}

static bool _jx_os_eventReset(jx_os_event_t* ev)
{
	return ResetEvent((HANDLE)ev) != FALSE;
}

static bool _jx_os_eventWait(jx_os_event_t* ev, uint32_t msecs)
{
	return WaitForSingleObject((HANDLE)ev, msecs) == WAIT_OBJECT_0;
}

//////////////////////////////////////////////////////////////////////////
// Thread
//
static uint32_t _jx_os_threadGetID(void)
{
	return (uint32_t)GetCurrentThreadId();
}

typedef struct jx_os_thread_t
{
	josThreadFunc m_Func;
	void* m_UserData;
	jx_os_semaphore_t* m_Semaphore;
	HANDLE m_Handle;
	uint32_t m_StackSize;
	uint32_t m_ThreadID;
	int32_t m_ExitCode;
	bool m_IsRunning;
} jx_os_thread_t;

static uint32_t _jx_os_threadFunc(void* threadData)
{
	jx_os_thread_t* thread = (jx_os_thread_t*)threadData;
	thread->m_ThreadID = (uint32_t)GetCurrentThreadId();
	_jx_os_semaphoreSignal(thread->m_Semaphore, 1);
	return thread->m_Func(thread, thread->m_UserData);
}

static jx_os_thread_t* _jx_os_threadCreate(josThreadFunc func, void* userData, uint32_t stackSize, const char* name)
{
	JX_UNUSED(name); // TODO: 

	jx_os_thread_t* thread = (jx_os_thread_t*)JX_ALLOC(s_OSContext.m_Allocator, sizeof(jx_os_thread_t));
	if (!thread) {
		return NULL;
	}

	jx_memset(thread, 0, sizeof(jx_os_thread_t));
	thread->m_Func = func;
	thread->m_UserData = userData;
	thread->m_StackSize = stackSize;
	thread->m_Semaphore = _jx_os_semaphoreCreate();
	if (!thread->m_Semaphore) {
		_jx_os_threadDestroy(thread);
		return NULL;
	}
	thread->m_Handle = CreateThread(NULL, stackSize, (LPTHREAD_START_ROUTINE)_jx_os_threadFunc, thread, 0, NULL);
	if (thread->m_Handle == NULL) {
		_jx_os_threadDestroy(thread);
		return NULL;
	}
	
	thread->m_IsRunning = true;
	_jx_os_semaphoreWait(thread->m_Semaphore, UINT32_MAX);

#if 0 // TODO: 
	if (name != NULL) {
		setThreadName(name);
	}
#endif

	return thread;
}

static void _jx_os_threadDestroy(jx_os_thread_t* thread)
{
	if (thread->m_IsRunning) {
		_jx_os_threadShutdown(thread);
	}

	if (thread->m_Semaphore) {
		_jx_os_semaphoreDestroy(thread->m_Semaphore);
		thread->m_Semaphore = NULL;
	}

	JX_FREE(s_OSContext.m_Allocator, thread);
}

static void _jx_os_threadShutdown(jx_os_thread_t* thread)
{
	WaitForSingleObject(thread->m_Handle, INFINITE);
	GetExitCodeThread(thread->m_Handle, (DWORD*)&thread->m_ExitCode);
	CloseHandle(thread->m_Handle);
	thread->m_Handle = NULL;
	thread->m_IsRunning = false;
}

static bool _jx_os_threadIsRunning(jx_os_thread_t* thread)
{
	return thread->m_IsRunning;
}

static int32_t _jx_os_threadGetExitCode(jx_os_thread_t* thread)
{
	return thread->m_ExitCode;
}

static uint32_t _jx_os_getNumHardwareThreads(void)
{
	SYSTEM_INFO sysinfo;
	GetSystemInfo(&sysinfo);
	return sysinfo.dwNumberOfProcessors;
}

//////////////////////////////////////////////////////////////////////////
// File
//
#define JX_FILE_FLAGS_ACCESS_Pos   0
#define JX_FILE_FLAGS_ACCESS_Msk   (0x03u << JX_FILE_FLAGS_ACCESS_Pos)
#define JX_FILE_FLAGS_ACCESS_READ  ((0x01u << JX_FILE_FLAGS_ACCESS_Pos) & JX_FILE_FLAGS_ACCESS_Msk)
#define JX_FILE_FLAGS_ACCESS_WRITE ((0x02u << JX_FILE_FLAGS_ACCESS_Pos) & JX_FILE_FLAGS_ACCESS_Msk)
#define JX_FILE_FLAGS_BINARY_Pos   2
#define JX_FILE_FLAGS_BINARY_Msk   (0x01u << JX_FILE_FLAGS_BINARY_Pos)

typedef struct jx_os_file_t
{
	HANDLE m_Handle;
	uint32_t m_Flags;
	JX_PAD(4);
} jx_os_file_t;

static jx_os_file_t* _jx_os_fileOpenRead(jx_file_base_dir baseDir, const char* relPath)
{
	const char* baseDirPath = _jx_os_getBaseDirPathUTF8(baseDir);
	if (!baseDirPath) {
		return NULL;
	}

	char absPath[1024];
	jx_snprintf(absPath, JX_COUNTOF(absPath), "%s%s", baseDirPath, relPath);

	wchar_t absPathW[1024];
	if (!jx_utf8to_utf16((uint16_t*)absPathW, JX_COUNTOF(absPathW), absPath, UINT32_MAX, NULL)) {
		return NULL;
	}

	HANDLE handle = CreateFileW(absPathW, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (handle == INVALID_HANDLE_VALUE) {
		return NULL;
	}

	jx_os_file_t* file = (jx_os_file_t*)JX_ALLOC(s_OSContext.m_Allocator, sizeof(jx_os_file_t));
	file->m_Handle = handle;
	file->m_Flags = JX_FILE_FLAGS_ACCESS_READ | JX_FILE_FLAGS_BINARY_Msk;

	return file;
}

static jx_os_file_t* _jx_os_fileOpenWrite(jx_file_base_dir baseDir, const char* relPath)
{
	const char* baseDirPath = _jx_os_getBaseDirPathUTF8(baseDir);
	if (!baseDirPath) {
		return NULL;
	}

	char absPath[1024];
	jx_snprintf(absPath, JX_COUNTOF(absPath), "%s%s", baseDirPath, relPath);

	wchar_t absPathW[1024];
	if (!jx_utf8to_utf16((uint16_t*)absPathW, JX_COUNTOF(absPathW), absPath, UINT32_MAX, NULL)) {
		return NULL;
	}

	HANDLE handle = CreateFileW(absPathW, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (handle == INVALID_HANDLE_VALUE) {
		return NULL;
	}

	jx_os_file_t* file = (jx_os_file_t*)JX_ALLOC(s_OSContext.m_Allocator, sizeof(jx_os_file_t));
	file->m_Handle = handle;
	file->m_Flags = JX_FILE_FLAGS_ACCESS_WRITE | JX_FILE_FLAGS_BINARY_Msk;

	return file;
}

static void _jx_os_fileClose(jx_os_file_t* f)
{
	if (!f) {
		JX_CHECK(false, "Invalid file", 0);
		return;
	}

	CloseHandle(f->m_Handle);
	JX_FREE(s_OSContext.m_Allocator, f);
}

static uint32_t _jx_os_fileRead(jx_os_file_t* f, void* buffer, uint32_t len)
{
	if (!f || f->m_Handle == INVALID_HANDLE_VALUE) {
		JX_CHECK(false, "Invalid file", 0);
		return 0;
	}

	JX_CHECK((f->m_Flags & JX_FILE_FLAGS_ACCESS_Msk) == JX_FILE_FLAGS_ACCESS_READ, "Trying to read from a file opened for writing", 0);

	DWORD numBytesRead = 0;
	if (!ReadFile(f->m_Handle, buffer, len, &numBytesRead, NULL)) {
		return 0;
	}

	return numBytesRead;
}

static uint32_t _jx_os_fileWrite(jx_os_file_t* f, const void* buffer, uint32_t len)
{
	if (!f || f->m_Handle == INVALID_HANDLE_VALUE) {
		JX_CHECK(false, "Invalid file", 0);
		return 0;
	}

	JX_CHECK((f->m_Flags & JX_FILE_FLAGS_ACCESS_Msk) == JX_FILE_FLAGS_ACCESS_WRITE, "Trying to write to a file opened for reading", 0);

	DWORD numBytesWritten = 0;
	if (!WriteFile(f->m_Handle, buffer, len, &numBytesWritten, NULL)) {
		return 0;
	}

	return numBytesWritten;
}

static uint64_t _jx_os_fileGetSize(jx_os_file_t* f)
{
	if (!f || f->m_Handle == INVALID_HANDLE_VALUE) {
		JX_CHECK(false, "Invalid file", 0);
		return 0ull;
	}

	JX_CHECK((f->m_Flags & JX_FILE_FLAGS_ACCESS_Msk) == JX_FILE_FLAGS_ACCESS_READ, "Cannot get the size of a file opened for writing", 0);

	LARGE_INTEGER fileSize;
	if (!GetFileSizeEx(f->m_Handle, &fileSize)) {
		JX_CHECK(false, "GetFileSizeEx() failed", 0);
		return 0;
	}

	return (uint64_t)fileSize.QuadPart;
}

static void _jx_os_fileSeek(jx_os_file_t* f, int64_t offset, jx_file_seek_origin origin)
{
	if (!f || f->m_Handle == INVALID_HANDLE_VALUE) {
		JX_CHECK(false, "Invalid file", 0);
		return;
	}

	LONG offsetLow = (LONG)((offset & 0x00000000FFFFFFFFull) >> 0);
	LONG offsetHigh = (LONG)((offset & 0xFFFFFFFF00000000ull) >> 32);
	DWORD moveMethod = origin == JX_FILE_SEEK_ORIGIN_BEGIN
		? FILE_BEGIN 
		: (origin == JX_FILE_SEEK_ORIGIN_CURRENT ? FILE_CURRENT : FILE_END)
		;
	const DWORD result = SetFilePointer(f->m_Handle, offsetLow, &offsetHigh, moveMethod);
	JX_CHECK(result != INVALID_SET_FILE_POINTER, "SetFilePointer failed", 0);
	JX_UNUSED(result); // for release mode
}

static uint64_t _jx_os_fileTell(jx_os_file_t* f)
{
	if (!f || f->m_Handle == INVALID_HANDLE_VALUE) {
		JX_CHECK(false, "Invalid file", 0);
		return 0ull;
	}

	LONG distanceHigh = 0;
	DWORD offsetLow = SetFilePointer(f->m_Handle, 0, &distanceHigh, FILE_CURRENT);
	JX_CHECK(offsetLow != INVALID_SET_FILE_POINTER, "SetFilePointer failed", 0);

	return ((uint64_t)offsetLow | ((uint64_t)distanceHigh << 32));
}

static void _jx_os_fileFlush(jx_os_file_t* f)
{
	if (!f || f->m_Handle == INVALID_HANDLE_VALUE) {
		JX_CHECK(false, "Invalid file", 0);
		return;
	}

	FlushFileBuffers(f->m_Handle);
}

static int32_t _jx_os_fileGetTime(jx_os_file_t* f, jx_file_time_type type, jx_os_file_time_t* t)
{
	if (!f || f->m_Handle == INVALID_HANDLE_VALUE) {
		JX_CHECK(false, "Invalid file", 0);
		return JX_ERROR_INVALID_ARGUMENT;
	}

	FILETIME creationTime, lastAccessTime, lastWriteTime;
	BOOL res = GetFileTime(f->m_Handle, &creationTime, &lastAccessTime, &lastWriteTime);
	if (!res) {
		return JX_ERROR_FILE_READ;
	}

	FILETIME* srcTime = NULL;
	switch (type) {
	case JX_FILE_TIME_TYPE_CREATION:
		srcTime = &creationTime;
		break;
	case JX_FILE_TIME_TYPE_LAST_ACCESS:
		srcTime = &lastAccessTime;
		break;
	case JX_FILE_TIME_TYPE_LAST_WRITE:
		srcTime = &lastWriteTime;
		break;
	default:
		JX_CHECK(false, "Unknown FileTimeType");
		break;
	}

	if (!srcTime) {
		return JX_ERROR_INVALID_ARGUMENT;
	}

	SYSTEMTIME systemTime;
	FileTimeToSystemTime(srcTime, &systemTime);
	t->m_Year = systemTime.wYear;
	t->m_Month = systemTime.wMonth;
	t->m_Day = systemTime.wDay;
	t->m_Hour = systemTime.wHour;
	t->m_Minute = systemTime.wMinute;
	t->m_Second = systemTime.wSecond;
	t->m_Millisecond = systemTime.wMilliseconds;

	return JX_ERROR_NONE;
}

//////////////////////////////////////////////////////////////////////////
// File system
//
static int32_t _jx_os_fsSetBaseDir(jx_file_base_dir whichDir, jx_file_base_dir baseDir, const char* relPath)
{
	const char* baseDirPath = _jx_os_getBaseDirPathUTF8(baseDir);
	if (!baseDirPath) {
		return JX_ERROR_INVALID_ARGUMENT;
	}

	char absPath[512];
	const uint32_t absPathLen = jx_snprintf(absPath, JX_COUNTOF(absPath), "%s%s", baseDirPath, relPath);
	
	// Make sure path ends with a slash.
	if (absPath[absPathLen - 1] == '/') {
		absPath[absPathLen - 1] = '\\';
	} else if (absPath[absPathLen - 1] != '\\') {
		absPath[absPathLen] = '\\';
		absPath[absPathLen + 1] = '\0';
	}

	switch (whichDir) {
	case JX_FILE_BASE_DIR_ABSOLUTE_PATH:
	case JX_FILE_BASE_DIR_INSTALL:
		return JX_ERROR_INVALID_ARGUMENT;
	case JX_FILE_BASE_DIR_USERDATA:
		JX_SYS_LOG_DEBUG("os", "Setting User Data directory to \"%s\".\n", absPath);
		_jx_os_fsCreateDirectory(JX_FILE_BASE_DIR_ABSOLUTE_PATH, absPath);
		jx_snprintf(s_OSContext.m_UserDataDir, JX_COUNTOF(s_OSContext.m_UserDataDir), "%s", absPath);
		break;
	case JX_FILE_BASE_DIR_USERAPPDATA:
		JX_SYS_LOG_DEBUG("os", "Setting User App Data directory to \"%s\".\n", absPath);
		_jx_os_fsCreateDirectory(JX_FILE_BASE_DIR_ABSOLUTE_PATH, absPath);
		jx_snprintf(s_OSContext.m_UserAppDataDir, JX_COUNTOF(s_OSContext.m_UserAppDataDir), "%s", absPath);
		break;
	case JX_FILE_BASE_DIR_TEMP:
		JX_SYS_LOG_DEBUG("os", "Setting Temp directory to \"%s\".\n", absPath);
		_jx_os_fsCreateDirectory(JX_FILE_BASE_DIR_ABSOLUTE_PATH, absPath);
		jx_snprintf(s_OSContext.m_TempDir, JX_COUNTOF(s_OSContext.m_TempDir), "%s", absPath);
		break;
	default:
		JX_CHECK(false, "Unknown base dir");
		return JX_ERROR_INVALID_ARGUMENT;
	}

	return JX_ERROR_NONE;
}

static int32_t _jx_os_fsGetBaseDir(jx_file_base_dir whichDir, char* absPath, uint32_t max)
{
	const char* baseDirPath = _jx_os_getBaseDirPathUTF8(whichDir);
	if (!baseDirPath) {
		return JX_ERROR_INVALID_ARGUMENT;
	}

	jx_snprintf(absPath, max, "%s", baseDirPath);

	return JX_ERROR_NONE;
}

static int32_t _jx_os_fsRemoveFile(jx_file_base_dir baseDir, const char* relPath)
{
	const char* baseDirPath = _jx_os_getBaseDirPathUTF8(baseDir);
	if (!baseDirPath) {
		return JX_ERROR_INVALID_ARGUMENT;
	}

	char absPath[1024];
	jx_snprintf(absPath, JX_COUNTOF(absPath), "%s%s", baseDirPath, relPath);

	wchar_t absPathW[1024];
	if (!jx_utf8to_utf16((uint16_t*)absPathW, JX_COUNTOF(absPathW), absPath, UINT32_MAX, NULL)) {
		return JX_ERROR_INVALID_ARGUMENT;
	}

	return DeleteFileW(absPathW) != 0
		? JX_ERROR_NONE
		: JX_ERROR_OPERATION_FAILED
		;
}

static int32_t _jx_os_fsCopyFile(jx_file_base_dir srcBaseDir, const char* srcRelPath, jx_file_base_dir dstBaseDir, const char* dstRelPath)
{
	wchar_t existingFilenameW[512];
	wchar_t newFilenameW[512];

	{
		const char* baseDirPath = _jx_os_getBaseDirPathUTF8(srcBaseDir);
		if (!baseDirPath) {
			return JX_ERROR_INVALID_ARGUMENT;
		}

		char absPath[1024];
		jx_snprintf(absPath, JX_COUNTOF(absPath), "%s%s", baseDirPath, srcRelPath);

		if (!jx_utf8to_utf16((uint16_t*)existingFilenameW, JX_COUNTOF(existingFilenameW), absPath, UINT32_MAX, NULL)) {
			return JX_ERROR_INVALID_ARGUMENT;
		}
	}

	{
		const char* baseDirPath = _jx_os_getBaseDirPathUTF8(dstBaseDir);
		if (!baseDirPath) {
			return JX_ERROR_INVALID_ARGUMENT;
		}

		char absPath[1024];
		jx_snprintf(absPath, JX_COUNTOF(absPath), "%s%s", baseDirPath, dstRelPath);

		if (!jx_utf8to_utf16((uint16_t*)newFilenameW, JX_COUNTOF(newFilenameW), absPath, UINT32_MAX, NULL)) {
			return JX_ERROR_INVALID_ARGUMENT;
		}
	}

	return CopyFileW(existingFilenameW, newFilenameW, FALSE) != 0
		? JX_ERROR_NONE
		: JX_ERROR_OPERATION_FAILED
		;
}

static int32_t _jx_os_fsMoveFile(jx_file_base_dir srcBaseDir, const char* srcRelPath, jx_file_base_dir dstBaseDir, const char* dstRelPath)
{
	wchar_t existingFilenameW[512];
	wchar_t newFilenameW[512];

	{
		const char* baseDirPath = _jx_os_getBaseDirPathUTF8(srcBaseDir);
		if (!baseDirPath) {
			return JX_ERROR_INVALID_ARGUMENT;
		}

		char absPath[1024];
		jx_snprintf(absPath, JX_COUNTOF(absPath), "%s%s", baseDirPath, srcRelPath);

		if (!jx_utf8to_utf16((uint16_t*)existingFilenameW, JX_COUNTOF(existingFilenameW), absPath, UINT32_MAX, NULL)) {
			return JX_ERROR_INVALID_ARGUMENT;
		}
	}

	{
		const char* baseDirPath = _jx_os_getBaseDirPathUTF8(dstBaseDir);
		if (!baseDirPath) {
			return JX_ERROR_INVALID_ARGUMENT;
		}

		char absPath[1024];
		jx_snprintf(absPath, JX_COUNTOF(absPath), "%s%s", baseDirPath, dstRelPath);

		if (!jx_utf8to_utf16((uint16_t*)newFilenameW, JX_COUNTOF(newFilenameW), absPath, UINT32_MAX, NULL)) {
			return JX_ERROR_INVALID_ARGUMENT;
		}
	}

	return MoveFileW(existingFilenameW, newFilenameW) != 0
		? JX_ERROR_NONE
		: JX_ERROR_OPERATION_FAILED
		;
}

static bool _jx_os_createDirectory_internal(const char* path)
{
	if (!path) {
		return false;
	}

	wchar_t pathW[1024];
	if (!jx_utf8to_utf16((uint16_t*)pathW, JX_COUNTOF(pathW), path, UINT32_MAX, NULL)) {
		return false;
	}

	bool success = true;

	const DWORD fileAttributes = GetFileAttributesW(pathW);
	if (fileAttributes == INVALID_FILE_ATTRIBUTES) {
		success = CreateDirectoryW(pathW, NULL) != 0;
	} else {
		// Make sure this is a directory.
		success = false
			|| ((fileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
			|| ((fileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
			;
	}

	return success;
}

static int32_t _jx_os_fsCreateDirectory(jx_file_base_dir baseDir, const char* relPath)
{
	const char* baseDirPath = _jx_os_getBaseDirPathUTF8(baseDir);
	if (!baseDirPath) {
		return JX_ERROR_INVALID_ARGUMENT;
	}

	char absPath[1024];
	jx_snprintf(absPath, JX_COUNTOF(absPath), "%s%s", baseDirPath, relPath);

	char partialPath[1024];

	const char* slash = jx_strchr(absPath, '\\');
	while (slash) {
		const uint32_t partialPathLen = (uint32_t)(slash - absPath);
		const uint32_t copyLen = partialPathLen < (JX_COUNTOF(partialPath) - 1)
			? partialPathLen
			: (JX_COUNTOF(partialPath) - 1)
			;
		jx_memcpy(partialPath, absPath, copyLen);
		partialPath[copyLen] = '\0';

		if (!_jx_os_createDirectory_internal(partialPath)) {
			return JX_ERROR_OPERATION_FAILED;
		}

		slash = jx_strchr(slash + 1, '\\');
	}

	return _jx_os_createDirectory_internal(absPath);
}

static int32_t _jx_os_fsRemoveEmptyDirectory(jx_file_base_dir baseDir, const char* relPath)
{
	if (baseDir != JX_FILE_BASE_DIR_USERDATA && baseDir != JX_FILE_BASE_DIR_USERAPPDATA) {
		JX_CHECK(false, "Can only create subfolders inside user data folder");
		return false;
	}

	const char* baseDirPath = _jx_os_getBaseDirPathUTF8(baseDir);
	if (!baseDirPath) {
		return JX_ERROR_INVALID_ARGUMENT;
	}

	char absPath[1024];
	jx_snprintf(absPath, JX_COUNTOF(absPath), "%s%s", baseDirPath, relPath);

	wchar_t utf16RelPath[512];
	jx_utf8to_utf16((uint16_t*)&utf16RelPath[0], JX_COUNTOF(utf16RelPath), absPath, UINT32_MAX, NULL);

	RemoveDirectoryW(utf16RelPath);

	return true;
}

static int32_t _jx_os_fsEnumFilesAndFolders(jx_file_base_dir baseDir, const char* pattern, josEnumFilesAndFoldersCallback callback, void* userData)
{
	const char* baseDirPath = _jx_os_getBaseDirPathUTF8(baseDir);
	if (!baseDirPath) {
		return JX_ERROR_INVALID_ARGUMENT;
	}

	char absPath[1024];
	jx_snprintf(absPath, JX_COUNTOF(absPath), "%s%s", baseDirPath, pattern);

	wchar_t utf16RelPath[512];
	jx_utf8to_utf16((uint16_t*)&utf16RelPath[0], JX_COUNTOF(utf16RelPath), absPath, UINT32_MAX, NULL);

	WIN32_FIND_DATAW ffd;
	HANDLE hFind = FindFirstFileW(utf16RelPath, &ffd);
	if (hFind == INVALID_HANDLE_VALUE) {
		return JX_ERROR_FILE_NOT_FOUND;
	}

	char utf8Filename[1024];

	do {
		if (ffd.cFileName[0] == L'.') {
			if (ffd.cFileName[1] == L'\0' || (ffd.cFileName[1] == L'.' && ffd.cFileName[2] == L'\0')) {
				continue;
			}
		}

		jx_utf8from_utf16(&utf8Filename[0], JX_COUNTOF(utf8Filename), (uint16_t*)ffd.cFileName, UINT32_MAX);

		callback(utf8Filename, !(ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY), userData);
	} while (FindNextFileW(hFind, &ffd) != 0);

	const DWORD dwError = GetLastError();

	FindClose(hFind);

	return dwError == ERROR_NO_MORE_FILES
		? JX_ERROR_NONE
		: JX_ERROR_OPERATION_FAILED
		;
}

static bool _jx_os_fsFileExists(jx_file_base_dir baseDir, const char* relPath)
{
	const char* baseDirPath = _jx_os_getBaseDirPathUTF8(baseDir);
	if (!baseDirPath) {
		return JX_ERROR_INVALID_ARGUMENT;
	}

	char absPath[1024];
	jx_snprintf(absPath, JX_COUNTOF(absPath), "%s%s", baseDirPath, relPath);

	wchar_t absPathW[1024];
	if (!jx_utf8to_utf16((uint16_t*)absPathW, JX_COUNTOF(absPathW), absPath, UINT32_MAX, NULL)) {
		return JX_ERROR_INVALID_ARGUMENT;
	}

	const DWORD attrs = GetFileAttributesW(absPathW);
	if (attrs == INVALID_FILE_ATTRIBUTES) {
		return false;
	}

	return !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

static uint32_t _vmemProtectToWin32(uint32_t protectFlags)
{
	uint32_t win32Protect = 0;
	if ((protectFlags & JX_VMEM_PROTECT_EXEC_Msk) != 0) {
		if ((protectFlags & JX_VMEM_PROTECT_WRITE_Msk) != 0) {
			win32Protect = PAGE_EXECUTE_READWRITE;
		} else if ((protectFlags & JX_VMEM_PROTECT_READ_Msk) != 0) {
			win32Protect = PAGE_EXECUTE_READ;
		} else {
			win32Protect = PAGE_EXECUTE;
		}
	} else {
		if ((protectFlags & JX_VMEM_PROTECT_WRITE_Msk) != 0) {
			win32Protect = PAGE_READWRITE;
		} else if ((protectFlags & JX_VMEM_PROTECT_READ_Msk) != 0) {
			win32Protect = PAGE_READONLY;
		} else {
			win32Protect = PAGE_NOACCESS;
		}
	}

	return win32Protect;
}

static uint32_t _jx_os_vmemGetPageSize(void)
{
	SYSTEM_INFO sysInf;
	GetSystemInfo(&sysInf);
	return sysInf.dwPageSize;
}

static void* _jx_os_vmemAlloc(void* desiredAddr, size_t sz, uint32_t protectFlags)
{
	const uint32_t win32Protect = _vmemProtectToWin32(protectFlags);
	return VirtualAlloc(desiredAddr, sz, MEM_RESERVE | MEM_COMMIT, win32Protect);
}

static void _jx_os_vmemFree(void* addr, size_t sz)
{
	JX_UNUSED(sz);
	VirtualFree(addr, 0, MEM_RELEASE);
}

static bool _jx_os_vmemProtect(void* addr, size_t sz, uint32_t protectFlags)
{
	const uint32_t win32Protect = _vmemProtectToWin32(protectFlags);
	uint32_t oldProtect = 0;
	return VirtualProtect(addr, sz, win32Protect, &oldProtect) != 0;
}

#endif // JX_PLATFORM_WINDOWS
