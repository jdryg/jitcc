#ifndef JX_OS_H
#error "Must be included from jx/os.h"
#endif

#include <jlib/allocator.h> // JX_ALLOC in jx_os_fsReadFile

#ifdef __cplusplus
extern "C" {
#endif

static inline jx_os_module_t* jx_os_moduleOpen(jx_file_base_dir baseDir, const char* path)
{
	return os_api->moduleOpen(baseDir, path);
}

static inline void jx_os_moduleClose(jx_os_module_t* mod)
{
	os_api->moduleClose(mod);
}

static inline void* jx_os_moduleGetSymbolAddr(jx_os_module_t* mod, const char* symbolName)
{
	return os_api->moduleGetSymbolAddr(mod, symbolName);
}

static inline jx_os_window_t* jx_os_windowOpen(const jx_os_window_desc_t* desc)
{
	return os_api->windowOpen(desc);
}

static inline void jx_os_windowClose(jx_os_window_t* win)
{
	os_api->windowClose(win);
}

static inline void jx_os_windowSetTitle(jx_os_window_t* win, const char* title)
{
	os_api->windowSetTitle(win, title);
}

static inline void jx_os_windowSetCursor(jx_os_window_t* win, jx_os_cursor_type cursor)
{
	os_api->windowSetCursor(win, cursor);
}

static inline bool jx_os_windowSetIcon(jx_os_window_t* win, const jx_os_icon_desc_t* iconDesc)
{
	return os_api->windowSetIcon(win, iconDesc);
}

static inline jx_os_frame_tick_result jx_os_frameTick(void)
{
	return os_api->frameTick();
}

static inline void jx_os_windowGetResolution(jx_os_window_t* win, uint16_t* res)
{
	os_api->windowGetResolution(win, res);
}

static inline uint32_t jx_os_windowGetFlags(jx_os_window_t* win)
{
	return os_api->windowGetFlags(win);
}

static inline void* jx_os_windowGetNativeHandle(jx_os_window_t* win)
{
	return os_api->windowGetNativeHandle(win);
}

static inline jx_os_timer_t* jx_os_timerCreate()
{
	return os_api->timerCreate();
}

static inline void jx_os_timerDestroy(jx_os_timer_t* timer)
{
	os_api->timerDestroy(timer);
}

static inline bool jx_os_timerSleep(jx_os_timer_t* timer, int64_t duration_us)
{
	return os_api->timerSleep(timer, duration_us);
}

static inline int64_t jx_os_timeNow(void)
{
	return os_api->timeNow();
}

static inline int64_t jx_os_timeDiff(int64_t end, int64_t start)
{
	return os_api->timeDiff(end, start);
}

static inline int64_t jx_os_timeSince(int64_t start)
{
	return os_api->timeSince(start);
}

static inline int64_t jx_os_timeLapTime(int64_t* timer)
{
	return os_api->timeLapTime(timer);
}

static inline double jx_os_timeConvertTo(int64_t delta, jx_os_time_units units)
{
	return os_api->timeConvertTo(delta, units);
}

static inline uint64_t jx_os_timestampNow(void)
{
	return os_api->timestampNow();
}

static inline int64_t jx_os_timestampDiff(uint64_t end, uint64_t start)
{
	return os_api->timestampDiff(end, start);
}

static inline int64_t jx_os_timestampSince(uint64_t start)
{
	return os_api->timestampSince(start);
}

static inline double jx_os_timestampConvertTo(int64_t delta, jx_os_time_units units)
{
	return os_api->timestampConvertTo(delta, units);
}

static inline uint32_t jx_os_timestampToString(uint64_t ts, char* buffer, uint32_t max)
{
	return os_api->timestampToString(ts, buffer, max);
}

static inline int32_t jx_os_consoleOpen(void)
{
	return os_api->consoleOpen();
}

static inline void jx_os_consoleClose(bool waitForUserInput)
{
	os_api->consoleClose(waitForUserInput);
}

static int32_t jx_os_consolePuts(const char* str, uint32_t len)
{
	return os_api->consolePuts(str, len);
}

static inline jx_os_mutex_t* jx_os_mutexCreate(void)
{
	return os_api->mutexCreate();
}

static inline void jx_os_mutexDestroy(jx_os_mutex_t* mutex)
{
	os_api->mutexDestroy(mutex);
}

static inline void jx_os_mutexLock(jx_os_mutex_t* mutex)
{
	os_api->mutexLock(mutex);
}

static inline bool jx_os_mutexTryLock(jx_os_mutex_t* mutex)
{
	return os_api->mutexTryLock(mutex);
}

static inline void jx_os_mutexUnlock(jx_os_mutex_t* mutex)
{
	os_api->mutexUnlock(mutex);
}

static inline jx_os_semaphore_t* jx_os_semaphoreCreate(void)
{
	return os_api->semaphoreCreate();
}

static inline void jx_os_semaphoreDestroy(jx_os_semaphore_t* semaphore)
{
	os_api->semaphoreDestroy(semaphore);
}

static inline void jx_os_semaphoreSignal(jx_os_semaphore_t* semaphore, uint32_t count)
{
	os_api->semaphoreSignal(semaphore, count);
}

static inline bool jx_os_semaphoreWait(jx_os_semaphore_t* semaphore, uint32_t msecs)
{
	return os_api->semaphoreWait(semaphore, msecs);
}

static inline jx_os_event_t* jx_os_eventCreate(bool manualReset, bool initialState, const char* name)
{
	return os_api->eventCreate(manualReset, initialState, name);
}

static inline void jx_os_eventDestroy(jx_os_event_t* ev)
{
	os_api->eventDestroy(ev);
}

static inline bool jx_os_eventSet(jx_os_event_t* ev)
{
	return os_api->eventSet(ev);
}

static inline bool jx_os_eventReset(jx_os_event_t* ev)
{
	return os_api->eventReset(ev);
}

static inline bool jx_os_eventWait(jx_os_event_t* ev, uint32_t msecs)
{
	return os_api->eventWait(ev, msecs);
}

static inline uint32_t jx_os_threadGetID(void)
{
	return os_api->threadGetID();
}

static inline jx_os_thread_t* jx_os_threadCreate(josThreadFunc func, void* userData, uint32_t stackSize, const char* name)
{
	return os_api->threadCreate(func, userData, stackSize, name);
}

static inline void jx_os_threadDestroy(jx_os_thread_t* thread)
{
	os_api->threadDestroy(thread);
}

static inline void jx_os_threadShutdown(jx_os_thread_t* thread)
{
	os_api->threadShutdown(thread);
}

static inline bool jx_os_threadIsRunning(jx_os_thread_t* thread)
{
	return os_api->threadIsRunning(thread);
}

static inline int32_t jx_os_threadGetExitCode(jx_os_thread_t* thread)
{
	return os_api->threadGetExitCode(thread);
}

static inline uint32_t jx_os_getNumHardwareThreads(void)
{
	return os_api->getNumHardwareThreads();
}

static inline jx_os_file_t* jx_os_fileOpenRead(jx_file_base_dir baseDir, const char* relPath)
{
	return os_api->fileOpenRead(baseDir, relPath);
}

static inline jx_os_file_t* jx_os_fileOpenWrite(jx_file_base_dir baseDir, const char* relPath)
{
	return os_api->fileOpenWrite(baseDir, relPath);
}

static inline void jx_os_fileClose(jx_os_file_t* f)
{
	os_api->fileClose(f);
}

static inline uint32_t jx_os_fileRead(jx_os_file_t* f, void* buffer, uint32_t len)
{
	return os_api->fileRead(f, buffer, len);
}

static inline uint32_t jx_os_fileWrite(jx_os_file_t* f, const void* buffer, uint32_t len)
{
	return os_api->fileWrite(f, buffer, len);
}

static inline uint64_t jx_os_fileGetSize(jx_os_file_t* f)
{
	return os_api->fileGetSize(f);
}

static inline void jx_os_fileSeek(jx_os_file_t* f, int64_t offset, jx_file_seek_origin origin)
{
	os_api->fileSeek(f, offset, origin);
}

static inline uint64_t jx_os_fileTell(jx_os_file_t* f)
{
	return os_api->fileTell(f);
}

static inline void jx_os_fileFlush(jx_os_file_t* f)
{
	os_api->fileFlush(f);
}

static int32_t jx_os_fileGetTime(jx_os_file_t* f, jx_file_time_type type, jx_os_file_time_t* time)
{
	return os_api->fileGetTime(f, type, time);
}

static inline int32_t jx_os_fsSetBaseDir(jx_file_base_dir whichDir, jx_file_base_dir baseDir, const char* relPath)
{
	return os_api->fsSetBaseDir(whichDir, baseDir, relPath);
}

static inline int32_t jx_os_fsGetBaseDir(jx_file_base_dir whichDir, char* absPath, uint32_t max)
{
	return os_api->fsGetBaseDir(whichDir, absPath, max);
}

static inline int32_t jx_os_fsRemoveFile(jx_file_base_dir baseDir, const char* relPath)
{
	return os_api->fsRemoveFile(baseDir, relPath);
}

static inline int32_t jx_os_fsCopyFile(jx_file_base_dir srcBaseDir, const char* srcRelPath, jx_file_base_dir dstBaseDir, const char* dstRelPath)
{
	return os_api->fsCopyFile(srcBaseDir, srcRelPath, dstBaseDir, dstRelPath);
}

static inline int32_t jx_os_fsMoveFile(jx_file_base_dir srcBaseDir, const char* srcRelPath, jx_file_base_dir dstBaseDir, const char* dstRelPath)
{
	return os_api->fsMoveFile(srcBaseDir, srcRelPath, dstBaseDir, dstRelPath);
}

static inline int32_t jx_os_fsCreateDirectory(jx_file_base_dir baseDir, const char* relPath)
{
	return os_api->fsCreateDirectory(baseDir, relPath);
}

static inline int32_t jx_os_fsRemoveEmptyDirectory(jx_file_base_dir baseDir, const char* relPath)
{
	return os_api->fsRemoveEmptyDirectory(baseDir, relPath);
}

static int32_t jx_os_fsEnumFilesAndFolders(jx_file_base_dir baseDir, const char* pattern, josEnumFilesAndFoldersCallback callback, void* userData)
{
	return os_api->fsEnumFilesAndFolders(baseDir, pattern, callback, userData);
}

static void* jx_os_fsReadFile(jx_file_base_dir baseDir, const char* relPath, jx_allocator_i* allocator, bool nullTerminate, uint64_t* sz)
{
	jx_os_file_t* f = os_api->fileOpenRead(baseDir, relPath);
	if (!f) {
		return NULL;
	}

	const uint64_t fileSize = os_api->fileGetSize(f);
	uint8_t* buffer = (uint8_t*)JX_ALLOC(allocator, fileSize + (nullTerminate ? 1 : 0));
	if (!buffer) {
		os_api->fileClose(f);
		return NULL;
	}

	os_api->fileRead(f, buffer, (uint32_t)fileSize); // TODO: Read files larger than 4GB
	os_api->fileClose(f);

	if (nullTerminate) {
		buffer[fileSize] = 0;
	}

	if (sz) {
		*sz = fileSize;
	}

	return buffer;
}

static inline uint32_t jx_os_vmemGetPageSize(void)
{
	return os_api->vmemGetPageSize();
}

static inline void* jx_os_vmemAlloc(void* desiredAddr, size_t sz, uint32_t protectFlags)
{
	return os_api->vmemAlloc(desiredAddr, sz, protectFlags);
}

static inline void jx_os_vmemFree(void* addr, size_t sz)
{
	os_api->vmemFree(addr, sz);
}

static inline bool jx_os_vmemProtect(void* addr, size_t sz, uint32_t protectFlags)
{
	return os_api->vmemProtect(addr, sz, protectFlags);
}

#ifdef __cplusplus
}
#endif
