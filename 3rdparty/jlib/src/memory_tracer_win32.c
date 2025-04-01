#include <jlib/memory_tracer.h>
#include <jlib/allocator.h>
#include <jlib/string.h>
#include <jlib/dbg.h>
#include <jlib/memory.h>

#if JX_PLATFORM_WINDOWS
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <dbghelp.h>

#define HPROCESS GetCurrentProcess()

typedef BOOL    (__stdcall* pfnSymInitialize)(HANDLE hProcess, PCSTR UserSearchPath, BOOL fInvadeProcess);
typedef BOOL    (__stdcall* pfnSymCleanup)(HANDLE hProcess);
typedef DWORD   (__stdcall* pfnSymSetOptions)(DWORD SymOptions);
typedef BOOL    (__stdcall* pfnSymGetLineFromAddr64)(HANDLE hProcess, DWORD64 dwAddr, PDWORD pdwDisplacement, PIMAGEHLP_LINE64 Line);
typedef DWORD64 (__stdcall* pfnSymLoadModuleEx)(HANDLE hProcess, HANDLE hFile, PCSTR ImageName, PCSTR ModuleName, DWORD64 BaseOfDll, DWORD DllSize, PMODLOAD_DATA Data, DWORD Flags);

static jx_allocator_handle_t _jmemtracer_createAllocator(const char* name);
static void _jmemtracer_destroyAllocator(jx_allocator_handle_t allocatorHandle);
static void _jmemtracer_onRealloc(jx_allocator_handle_t allocatorHandle, void* ptr, void* newPtr, size_t newSize, const char* file, uint32_t line);
static void _jmemtracer_onModuleLoaded(char* modulePath, uint64_t baseAddr);

jx_memory_tracer_api* memory_tracer_api = &(jx_memory_tracer_api){
	.createAllocator = _jmemtracer_createAllocator,
	.destroyAllocator = _jmemtracer_destroyAllocator,
	.onRealloc = _jmemtracer_onRealloc,
	.onModuleLoaded = _jmemtracer_onModuleLoaded,
};

typedef struct _jx_allocation_info
{
	struct _jx_allocation_info* m_Next;
	struct _jx_allocation_info* m_Prev;
	const void* m_Ptr;
	const char* m_Filename;
	uint32_t m_Line;
	size_t m_Size;
	uint64_t m_StackFrameAddr[JX_CONFIG_MEMTRACER_MAX_STACK_FRAMES];
} _jx_allocation_info;

typedef struct _jx_allocator_info
{
	char m_Name[64];
	size_t m_TotalAllocatedMemory;
	_jx_allocation_info* m_Allocations;
	uint32_t m_TotalAllocations;
	uint32_t m_ActiveAllocations;
	bool m_IsAlive;
} _jx_allocator_info;

typedef struct jmemory_tracer
{
	jx_allocator_i* m_Allocator;
	jx_allocator_i* m_AllocInfoPool;
	_jx_allocator_info* m_Allocators;
	uint32_t m_NumAllocators;

	CRITICAL_SECTION m_CS;
	HMODULE m_hDbgHelpDll;
	pfnSymCleanup SymCleanup;
	pfnSymGetLineFromAddr64 SymGetLineFromAddr64;
	pfnSymInitialize SymInitialize;
	pfnSymSetOptions SymSetOptions;
	pfnSymLoadModuleEx SymLoadModuleEx;
} jmemory_tracer;

static jmemory_tracer* s_MemTracer = &(jmemory_tracer){ 0 };

static _jx_allocation_info* _jmemtracer_findAllocation(_jx_allocator_info* ai, const void* ptr);

bool jx_memtracer_init(jx_allocator_i* allocator)
{
	s_MemTracer->m_Allocator = allocator;

	s_MemTracer->m_AllocInfoPool = allocator_api->createPoolAllocator(sizeof(_jx_allocation_info), 2048, allocator);
	if (!s_MemTracer->m_AllocInfoPool) {
		return false;
	}

	InitializeCriticalSection(&s_MemTracer->m_CS);

	// Initialize dbghelp.dll
	{
		// Don't use the OS API because it might not have been initialized yet.
		// Since we know that the code below works only on Win32, there is no 
		// need for an OS abstraction.
		char filename[256];
		GetModuleFileNameA(NULL, &filename[0], JX_COUNTOF(filename));
		char* lastSlash = (char*)jx_strrchr(filename, '\\');
		*lastSlash = '\0';

		char dbgHelpFilename[256];
		jx_snprintf(dbgHelpFilename, JX_COUNTOF(dbgHelpFilename), "%s\\dbghelp.dll", filename);
		HMODULE dbghelp = LoadLibraryA(dbgHelpFilename);
		if (dbghelp) {
			pfnSymInitialize symInitialize = (pfnSymInitialize)GetProcAddress(dbghelp, "SymInitialize");
			pfnSymCleanup symCleanup = (pfnSymCleanup)GetProcAddress(dbghelp, "SymCleanup");
			pfnSymSetOptions symSetOptions = (pfnSymSetOptions)GetProcAddress(dbghelp, "SymSetOptions");
			pfnSymGetLineFromAddr64 symGetLineFromAddr64 = (pfnSymGetLineFromAddr64)GetProcAddress(dbghelp, "SymGetLineFromAddr64");
			pfnSymLoadModuleEx symLoadModuleEx = (pfnSymLoadModuleEx)GetProcAddress(dbghelp, "SymLoadModuleEx");

			const bool success = true
				&& symInitialize != NULL
				&& symCleanup != NULL
				&& symSetOptions != NULL
				&& symGetLineFromAddr64 != NULL
				&& symLoadModuleEx != NULL
				;

			if (success) {
				symSetOptions(SYMOPT_UNDNAME | SYMOPT_LOAD_ANYTHING | SYMOPT_LOAD_LINES);

				BOOL status = symInitialize(HPROCESS, NULL, TRUE);
				if (!status) {
					JX_CHECK(false, "dbghelp.dll: SymInitialize(0x%08X) failed. Error: %u", HPROCESS, GetLastError());
					FreeLibrary(dbghelp);
				} else {
					s_MemTracer->m_hDbgHelpDll = dbghelp;
					s_MemTracer->SymCleanup = symCleanup;
					s_MemTracer->SymGetLineFromAddr64 = symGetLineFromAddr64;
					s_MemTracer->SymInitialize = symInitialize;
					s_MemTracer->SymSetOptions = symSetOptions;
					s_MemTracer->SymLoadModuleEx = symLoadModuleEx;
				}
			} else {
				JX_CHECK(false, "dbghelp.dll: Failed to load at least one required function.\n", 0);
				FreeLibrary(dbghelp);
			}
		}
	}

	return true;
}

void jx_memtracer_shutdown(void)
{
	const uint32_t numAllocators = s_MemTracer->m_NumAllocators;
	for (uint32_t i = 0; i < numAllocators; ++i) {
		_jx_allocator_info* ai = &s_MemTracer->m_Allocators[i];
		if (!ai->m_IsAlive) {
			continue;
		}

		JX_WARN(false, "Allocator \"%s\" is still alive", ai->m_Name);

		_jmemtracer_destroyAllocator((jx_allocator_handle_t) { i });
	}

	DeleteCriticalSection(&s_MemTracer->m_CS);

	if (s_MemTracer->m_hDbgHelpDll) {
		s_MemTracer->SymCleanup(HPROCESS);

		FreeLibrary(s_MemTracer->m_hDbgHelpDll);

		s_MemTracer->m_hDbgHelpDll = NULL;
		s_MemTracer->SymCleanup = NULL;
		s_MemTracer->SymGetLineFromAddr64 = NULL;
		s_MemTracer->SymInitialize = NULL;
		s_MemTracer->SymSetOptions = NULL;
	}

	if (s_MemTracer->m_AllocInfoPool) {
		allocator_api->destroyPoolAllocator(s_MemTracer->m_AllocInfoPool);
		s_MemTracer->m_AllocInfoPool = NULL;
	}

	s_MemTracer->m_Allocator = NULL;
}

//////////////////////////////////////////////////////////////////////////
// Internal
//
static jx_allocator_handle_t _jmemtracer_createAllocator(const char* name)
{
	jmemory_tracer* ctx = s_MemTracer;

	// Check if an allocator with the same name already exists.
	const uint32_t numAllocators = (uint32_t)ctx->m_NumAllocators;
	for (uint32_t i = 0; i < numAllocators; ++i) {
		_jx_allocator_info* ai = &ctx->m_Allocators[i];
		if (!jx_strcmp(ai->m_Name, name)) {
			JX_CHECK(!ai->m_IsAlive, "Allocator \"%s\" is already initialized and still alive.", name);
			ai->m_IsAlive = true;
			return (jx_allocator_handle_t) { i };
		}
	}

	ctx->m_Allocators = (_jx_allocator_info*)JX_REALLOC(ctx->m_Allocator, ctx->m_Allocators, sizeof(_jx_allocator_info) * (ctx->m_NumAllocators + 1));

	_jx_allocator_info* ai = &ctx->m_Allocators[ctx->m_NumAllocators++];
	jx_memset(ai, 0, sizeof(_jx_allocator_info));
	jx_snprintf(ai->m_Name, JX_COUNTOF(ai->m_Name), "%s", name);
	ai->m_IsAlive = true;

	return (jx_allocator_handle_t) { ctx->m_NumAllocators - 1 };
}

static void _jmemtracer_destroyAllocator(jx_allocator_handle_t allocatorHandle)
{
	jmemory_tracer* ctx = s_MemTracer;

	EnterCriticalSection(&ctx->m_CS);

	JX_CHECK(allocatorHandle.idx < ctx->m_NumAllocators, "Invalid allocator handle", 0);

	_jx_allocator_info* ai = &ctx->m_Allocators[allocatorHandle.idx];
	JX_WARN(ai->m_IsAlive, "Allocator \"%s\" is already destroyed", ai->m_Name);

	jx_dbg_printf("*------------------------------------------------------*\n");
	jx_dbg_printf("| Allocator            | %-30s|\n", ai->m_Name);
	jx_dbg_printf("| # Active allocations | %-30u|\n", ai->m_ActiveAllocations);
	jx_dbg_printf("| Allocated memory     | %$$$-30u|\n", ai->m_TotalAllocatedMemory);
	jx_dbg_printf("*------------------------------------------------------*\n");

	_jx_allocation_info* alloc = ai->m_Allocations;
	if (alloc) {
		jx_dbg_printf("*------------------------------------------------------*\n");
		jx_dbg_printf("|                Active allocations                    |\n");
		while (alloc) {
			_jx_allocation_info* nextAlloc = alloc->m_Next;

			// Dump allocation info.
			jx_dbg_printf("*------------------------------------------------------*\n");
			jx_dbg_printf("%s(%d): %$$$u \n", alloc->m_Filename, alloc->m_Line, alloc->m_Size);

			if (ctx->m_hDbgHelpDll) {
				jx_dbg_printf("- Stack trace:\n");

				for (uint32_t iFrame = 0; iFrame < JX_CONFIG_MEMTRACER_MAX_STACK_FRAMES; ++iFrame) {
					IMAGEHLP_LINE64 line;
					line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);

					DWORD offset_ln = 0;
					if (ctx->SymGetLineFromAddr64(HPROCESS, alloc->m_StackFrameAddr[iFrame], &offset_ln, &line)) {
						jx_dbg_printf("%s(%d)\n", line.FileName, line.LineNumber);
					} else {
						const DWORD err = GetLastError();
						if (err == ERROR_MOD_NOT_FOUND) {
							jx_dbg_printf("(null)(0): Module not found\n");
						} else if (err == ERROR_INVALID_ADDRESS) {
							break;
						} else {
							jx_dbg_printf("dbghelp.dll: SymGetLineFromAddr64() failed. Error: %u\n", err);
						}
					}
				}
			}

			JX_FREE(ctx->m_AllocInfoPool, alloc);

			alloc = nextAlloc;
		}

		jx_dbg_printf("*------------------------------------------------------*\n");
	}

	ai->m_Allocations = NULL;
	ai->m_ActiveAllocations = 0;
	ai->m_TotalAllocatedMemory = 0;
	ai->m_TotalAllocations = 0;
	ai->m_IsAlive = false;

	LeaveCriticalSection(&ctx->m_CS);
}

static void _jmemtracer_onRealloc(jx_allocator_handle_t allocatorHandle, void* ptr, void* newPtr, size_t newSize, const char* file, uint32_t line)
{
	jmemory_tracer* ctx = s_MemTracer;

	JX_CHECK(allocatorHandle.idx < ctx->m_NumAllocators, "Invalid allocator handle", 0);

	EnterCriticalSection(&ctx->m_CS);

	_jx_allocator_info* ai = &ctx->m_Allocators[allocatorHandle.idx];
	JX_CHECK(ai->m_IsAlive, "Allocator \"%s\" has been destroyed", ai->m_Name);

	if (ptr) {
		if (!newSize) {
			// free
			JX_CHECK(newPtr == NULL, "Size/ptr suggests this is a free() but new pointer is not NULL", 0);

			_jx_allocation_info* alloc = _jmemtracer_findAllocation(ai, ptr);
			JX_CHECK(alloc != NULL, "Allocation not found", 0);
			JX_CHECK(alloc->m_Ptr == ptr, "Allocation pointer mismatch", 0);

			if (alloc->m_Prev) {
				alloc->m_Prev->m_Next = alloc->m_Next;
			}
			if (alloc->m_Next) {
				alloc->m_Next->m_Prev = alloc->m_Prev;
			}
			if (ai->m_Allocations == alloc) {
				ai->m_Allocations = alloc->m_Next;
			}

			JX_CHECK(ai->m_ActiveAllocations > 0, "Allocation counter undeflow", 0);
			ai->m_ActiveAllocations--;

			JX_CHECK(ai->m_TotalAllocatedMemory >= alloc->m_Size, "Allocated memory underflow", 0);
			ai->m_TotalAllocatedMemory -= alloc->m_Size;

			JX_FREE(ctx->m_AllocInfoPool, alloc);
		} else {
			// realloc
			JX_CHECK(newPtr != NULL, "Size/ptr suggests this is a realloc() but new pointer is NULL", 0);

			_jx_allocation_info* alloc = _jmemtracer_findAllocation(ai, ptr);
			JX_CHECK(alloc != NULL, "Allocation not found", 0);
			JX_CHECK(alloc->m_Ptr == ptr, "Allocation pointer mismatch", 0);

			alloc->m_Filename = file;
			alloc->m_Line = line;
			alloc->m_Ptr = newPtr;

			JX_CHECK(ai->m_TotalAllocatedMemory >= alloc->m_Size, "Allocated memory underflow", 0);
			ai->m_TotalAllocatedMemory -= alloc->m_Size;
			ai->m_TotalAllocatedMemory += newSize;

			alloc->m_Size = newSize;
		}
	} else {
		// alloc
		if (newPtr == NULL) {
			JX_CHECK(newSize == 0, "Size/ptr suggests this is an alloc() but new pointer is NULL.", 0);
		} else {
			JX_CHECK(newSize != 0, "Size/ptr suggests this is an alloc() but new size is 0.", 0);

			_jx_allocation_info* alloc = (_jx_allocation_info*)JX_ALLOC(ctx->m_AllocInfoPool, sizeof(_jx_allocation_info));
			alloc->m_Filename = file;
			alloc->m_Line = line;

			// Skip 2 stack frames.
			// 1 is the call to onRealloc
			// 1 is the tracing allocator which called onRealloc
			RtlCaptureStackBackTrace(2, JX_CONFIG_MEMTRACER_MAX_STACK_FRAMES, (PVOID*)&alloc->m_StackFrameAddr[0], NULL);

			alloc->m_Next = ai->m_Allocations;
			alloc->m_Prev = NULL;
			alloc->m_Ptr = newPtr;
			alloc->m_Size = newSize;

			if (ai->m_Allocations) {
				ai->m_Allocations->m_Prev = alloc;
			}
			ai->m_Allocations = alloc;
			ai->m_ActiveAllocations++;
			ai->m_TotalAllocations++;
			ai->m_TotalAllocatedMemory += newSize;
		}
	}

	LeaveCriticalSection(&ctx->m_CS);
}

static void _jmemtracer_onModuleLoaded(char* modulePath, uint64_t baseAddr)
{
	if (s_MemTracer->m_hDbgHelpDll) {
		if (!s_MemTracer->SymLoadModuleEx(HPROCESS, NULL, modulePath, NULL, (DWORD64)baseAddr, 0, NULL, 0)) {
			JX_TRACE("dbghelp.dll: SymLoadModuleEx(\"%s\") failed. Error: %u", modulePath, GetLastError());
		}
	}
}

static _jx_allocation_info* _jmemtracer_findAllocation(_jx_allocator_info* ai, const void* ptr)
{
	JX_CHECK(ptr != NULL, "Trying to find an invalid allocation", 0);

	_jx_allocation_info* alloc = ai->m_Allocations;
	while (alloc) {
		if (alloc->m_Ptr == ptr) {
			return alloc;
		}

		alloc = alloc->m_Next;
	}

	return NULL;
}
#endif // JX_PLATFORM_WINDOWS
