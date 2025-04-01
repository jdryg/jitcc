#ifndef JX_OS_H
#define JX_OS_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct jx_allocator_i jx_allocator_i;

// Opaque type for dynamically linked libraries.
typedef struct jx_os_module_t jx_os_module_t;

// Opaque type for windows
typedef struct jx_os_window_t jx_os_window_t;

// Opaque type for timers
typedef struct jx_os_timer_t jx_os_timer_t;

// Opaque type for mutexes
typedef struct jx_os_mutex_t jx_os_mutex_t;

// Opaque type for semaphores
typedef struct jx_os_semaphore_t jx_os_semaphore_t;

// Opaque type for events
typedef struct jx_os_event_t jx_os_event_t;

// Opaque type for threads
typedef struct jx_os_thread_t jx_os_thread_t;

// Opaque type for files
typedef struct jx_os_file_t jx_os_file_t;

typedef int32_t (*josThreadFunc)(jx_os_thread_t* thread, void* userData);

typedef enum jx_file_base_dir
{
	JX_FILE_BASE_DIR_ABSOLUTE_PATH = 0,
	JX_FILE_BASE_DIR_INSTALL,
	JX_FILE_BASE_DIR_USERDATA,
	JX_FILE_BASE_DIR_USERAPPDATA,
	JX_FILE_BASE_DIR_TEMP
} jx_file_base_dir;

typedef enum jx_file_seek_origin
{
	JX_FILE_SEEK_ORIGIN_BEGIN = 0,
	JX_FILE_SEEK_ORIGIN_CURRENT,
	JX_FILE_SEEK_ORIGIN_END
} jx_file_seek_origin;

typedef enum jx_file_time_type
{
	JX_FILE_TIME_TYPE_CREATION,
	JX_FILE_TIME_TYPE_LAST_ACCESS,
	JX_FILE_TIME_TYPE_LAST_WRITE
} jx_file_time_type;

typedef enum jx_os_cursor_type
{
	JX_CURSOR_TYPE_ARROW = 0,
	JX_CURSOR_TYPE_HAND,
	JX_CURSOR_TYPE_I_BAR,
	JX_CURSOR_TYPE_MOVE_XY,
	JX_CURSOR_TYPE_RESIZE_X,
	JX_CURSOR_TYPE_RESIZE_Y,
	
	JX_OS_NUM_CURSORS,

	JX_CURSOR_TYPE_DEFAULT = JX_CURSOR_TYPE_ARROW,
} jx_os_cursor_type;

typedef enum jx_os_window_event_type
{
	JX_WINDOW_EVENT_TYPE_INVALID = 0,
	JX_WINDOW_EVENT_TYPE_MOUSE_BUTTON_DOWN,
	JX_WINDOW_EVENT_TYPE_MOUSE_BUTTON_UP,
	JX_WINDOW_EVENT_TYPE_MOUSE_MOVE,
	JX_WINDOW_EVENT_TYPE_MOUSE_ENTER,
	JX_WINDOW_EVENT_TYPE_MOUSE_LEAVE,
	JX_WINDOW_EVENT_TYPE_MOUSE_SCROLL,
	JX_WINDOW_EVENT_TYPE_KEY_DOWN,
	JX_WINDOW_EVENT_TYPE_KEY_UP,
	JX_WINDOW_EVENT_TYPE_CHAR,
	JX_WINDOW_EVENT_TYPE_MINIMIZED,
	JX_WINDOW_EVENT_TYPE_RESTORED,
	JX_WINDOW_EVENT_TYPE_RESIZED
} jx_os_window_event_type;

typedef enum jx_os_mouse_button
{
	JX_MOUSE_BUTTON_LEFT = 0,
	JX_MOUSE_BUTTON_MIDDLE,
	JX_MOUSE_BUTTON_RIGHT,

	JX_OS_NUM_MOUSE_BUTTONS
} jx_os_mouse_button;

typedef enum jx_os_virtual_key
{
	JX_VKEY_INVALID = 0,

	JX_VKEY_SPACE = 32,
	JX_VKEY_APOSTROPHE = 39,
	JX_VKEY_COMMA = 44,
	JX_VKEY_MINUS = 45,
	JX_VKEY_PERIOD = 46,
	JX_VKEY_SLASH = 47,
	JX_VKEY_0 = 48,
	JX_VKEY_1 = 49,
	JX_VKEY_2 = 50,
	JX_VKEY_3 = 51,
	JX_VKEY_4 = 52,
	JX_VKEY_5 = 53,
	JX_VKEY_6 = 54,
	JX_VKEY_7 = 55,
	JX_VKEY_8 = 56,
	JX_VKEY_9 = 57,
	JX_VKEY_SEMICOLON = 59,
	JX_VKEY_EQUAL = 61,
	JX_VKEY_A = 65,
	JX_VKEY_B = 66,
	JX_VKEY_C = 67,
	JX_VKEY_D = 68,
	JX_VKEY_E = 69,
	JX_VKEY_F = 70,
	JX_VKEY_G = 71,
	JX_VKEY_H = 72,
	JX_VKEY_I = 73,
	JX_VKEY_J = 74,
	JX_VKEY_K = 75,
	JX_VKEY_L = 76,
	JX_VKEY_M = 77,
	JX_VKEY_N = 78,
	JX_VKEY_O = 79,
	JX_VKEY_P = 80,
	JX_VKEY_Q = 81,
	JX_VKEY_R = 82,
	JX_VKEY_S = 83,
	JX_VKEY_T = 84,
	JX_VKEY_U = 85,
	JX_VKEY_V = 86,
	JX_VKEY_W = 87,
	JX_VKEY_X = 88,
	JX_VKEY_Y = 89,
	JX_VKEY_Z = 90,

	JX_VKEY_LEFT_BRACKET = 91,
	JX_VKEY_BACKSLASH = 92,
	JX_VKEY_RIGHT_BRACKET = 93,
	JX_VKEY_GRAVE_ACCENT = 96,

	JX_VKEY_ESCAPE = 256,
	JX_VKEY_ENTER = 257,
	JX_VKEY_TAB = 258,
	JX_VKEY_BACKSPACE = 259,
	JX_VKEY_INSERT = 260,
	JX_VKEY_DELETE = 261,
	JX_VKEY_RIGHT = 262,
	JX_VKEY_LEFT = 263,
	JX_VKEY_DOWN = 264,
	JX_VKEY_UP = 265,
	JX_VKEY_PAGE_UP = 266,
	JX_VKEY_PAGE_DOWN = 267,
	JX_VKEY_HOME = 268,
	JX_VKEY_END = 269,

	JX_VKEY_F1 = 290,
	JX_VKEY_F2 = 291,
	JX_VKEY_F3 = 292,
	JX_VKEY_F4 = 293,
	JX_VKEY_F5 = 294,
	JX_VKEY_F6 = 295,
	JX_VKEY_F7 = 296,
	JX_VKEY_F8 = 297,
	JX_VKEY_F9 = 298,
	JX_VKEY_F10 = 299,
	JX_VKEY_F11 = 300,
	JX_VKEY_F12 = 301,

	JX_VKEY_KP_0 = 320,
	JX_VKEY_KP_1 = 321,
	JX_VKEY_KP_2 = 322,
	JX_VKEY_KP_3 = 323,
	JX_VKEY_KP_4 = 324,
	JX_VKEY_KP_5 = 325,
	JX_VKEY_KP_6 = 326,
	JX_VKEY_KP_7 = 327,
	JX_VKEY_KP_8 = 328,
	JX_VKEY_KP_9 = 329,
	JX_VKEY_KP_DECIMAL = 330,
	JX_VKEY_KP_DIVIDE = 331,
	JX_VKEY_KP_MULTIPLY = 332,
	JX_VKEY_KP_SUBTRACT = 333,
	JX_VKEY_KP_ADD = 334,
	JX_VKEY_KP_ENTER = 335,
	JX_VKEY_KP_EQUEL = 336,
	JX_VKEY_LEFT_SHIFT = 340,
	JX_VKEY_LEFT_CONTROL = 341,
	JX_VKEY_LEFT_ALT = 342,
	JX_VKEY_LEFT_SUPER = 343,
	JX_VKEY_RIGHT_SHIFT = 344,
	JX_VKEY_RIGHT_CONTROL = 345,
	JX_VKEY_RIGHT_ALT = 346,
	JX_VKEY_RIGHT_SUPER = 347,

	JOS_NUM_VIRTUAL_KEYS,
} jx_os_virtual_key;

#define JX_KEY_MODIFIER_SHIFT_Pos    0
#define JX_KEY_MODIFIER_SHIFT_Msk    (0x01u << JX_KEY_MODIFIER_SHIFT_Pos)
#define JX_KEY_MODIFIER_SHIFT(val)   (((val) << JX_KEY_MODIFIER_SHIFT_Pos) & JX_KEY_MODIFIER_SHIFT_Msk)
#define JX_KEY_MODIFIER_CONTROL_Pos  1
#define JX_KEY_MODIFIER_CONTROL_Msk  (0x01u << JX_KEY_MODIFIER_CONTROL_Pos)
#define JX_KEY_MODIFIER_CONTROL(val) (((val) << JX_KEY_MODIFIER_CONTROL_Pos) & JX_KEY_MODIFIER_CONTROL_Msk)
#define JX_KEY_MODIFIER_ALT_Pos      2
#define JX_KEY_MODIFIER_ALT_Msk      (0x01u << JX_KEY_MODIFIER_ALT_Pos)
#define JX_KEY_MODIFIER_ALT(val)     (((val) << JX_KEY_MODIFIER_ALT_Pos) & JX_KEY_MODIFIER_ALT_Msk)
#define JX_KEY_MODIFIER_SUPER_Pos    3
#define JX_KEY_MODIFIER_SUPER_Msk    (0x01u << JX_KEY_MODIFIER_SUPER_Pos)
#define JX_KEY_MODIFIER_SUPER(val)   (((val) << JX_KEY_MODIFIER_SUPER_Pos) & JX_KEY_MODIFIER_SUPER_Msk)

typedef struct jx_os_window_event_t
{
	jx_os_window_event_type m_Type;
	jx_os_mouse_button m_MouseButton;
	jx_os_virtual_key m_VirtualKey;
	uint32_t m_KeyModifiers;
	uint32_t m_CharCode;
	float m_MousePos[2]; // { x, y }
	float m_Scroll[2];   // { horizontal, vertical }
} jx_os_window_event_t;

typedef void (*josWinEventCallback)(jx_os_window_t* win, const jx_os_window_event_t* ev, void* userData);

#define JX_WINDOW_FLAGS_FULLSCREEN_Pos   0
#define JX_WINDOW_FLAGS_FULLSCREEN_Msk   (0x01u << JX_WINDOW_FLAGS_FULLSCREEN_Pos)
#define JX_WINDOW_FLAGS_FULLSCREEN(val)  (((val) << JX_WINDOW_FLAGS_FULLSCREEN_Pos) & JX_WINDOW_FLAGS_FULLSCREEN_Msk)
#define JX_WINDOW_FLAGS_VSYNC_Pos        1
#define JX_WINDOW_FLAGS_VSYNC_Msk        (0x01u << JX_WINDOW_FLAGS_VSYNC_Pos)
#define JX_WINDOW_FLAGS_VSYNC(val)       (((val) << JX_WINDOW_FLAGS_VSYNC_Pos) & JX_WINDOW_FLAGS_VSYNC_Msk)
#define JX_WINDOW_FLAGS_CENTER_Pos       2
#define JX_WINDOW_FLAGS_CENTER_Msk       (0x01u << JX_WINDOW_FLAGS_CENTER_Pos)
#define JX_WINDOW_FLAGS_CENTER(val)      (((val) << JX_WINDOW_FLAGS_CENTER_Pos) & JX_WINDOW_FLAGS_CENTER_Msk)
#define JX_WINDOW_FLAGS_NONE             0

typedef struct jx_os_window_desc_t
{
	const char* m_Title;
	josWinEventCallback m_EventCb;
	void* m_EventCbUserData;
	uint32_t m_Flags;
	uint16_t m_Width;
	uint16_t m_Height;
} jx_os_window_desc_t;

typedef enum jx_os_time_units
{
	JX_TIME_UNITS_SEC = 0,
	JX_TIME_UNITS_MS,
	JX_TIME_UNITS_US,
	JX_TIME_UNITS_NS
} jx_os_time_units;

typedef enum jx_os_frame_tick_result
{
	JX_FRAME_TICK_CONTINUE = 0,
	JX_FRAME_TICK_QUIT
} jx_os_frame_tick_result;

typedef enum jx_os_icon_format
{
	JX_ICON_FORMAT_L1 = 0, // Bitmap, 8 pixels/byte
	JX_ICON_FORMAT_L1_A1,  // Bitmap, 8 pixels/byte followed by a binary mask (8 pixels/byte)
	JX_ICON_FORMAT_RGBA8,  // True color, 4 bytes/pixel
} jx_os_icon_format;

typedef struct jx_os_icon_desc_t
{
	const uint8_t* m_Data;
	jx_os_icon_format m_Format;
	uint16_t m_Width;
	uint16_t m_Height;
} jx_os_icon_desc_t;

typedef struct jx_os_file_time_t
{
	uint16_t m_Year;
	uint16_t m_Month;
	uint16_t m_Day;
	uint16_t m_Hour;
	uint16_t m_Minute;
	uint16_t m_Second;
	uint16_t m_Millisecond;
} jx_os_file_time_t;

typedef void (*josEnumFilesAndFoldersCallback)(const char* relPath, bool isFile, void* userData);

typedef struct jx_os_api
{
	jx_os_module_t* (*moduleOpen)(jx_file_base_dir baseDir, const char* path_utf8);
	void            (*moduleClose)(jx_os_module_t* mod);
	void*           (*moduleGetSymbolAddr)(jx_os_module_t* mod, const char* symbolName);

	jx_os_window_t* (*windowOpen)(const jx_os_window_desc_t* desc);
	void            (*windowClose)(jx_os_window_t* win);
	void            (*windowSetTitle)(jx_os_window_t* win, const char* title);
	void            (*windowSetCursor)(jx_os_window_t* win, jx_os_cursor_type cursor);
	bool            (*windowSetIcon)(jx_os_window_t* win, const jx_os_icon_desc_t* iconDesc);
	void            (*windowGetResolution)(jx_os_window_t* win, uint16_t* res);
	uint32_t        (*windowGetFlags)(jx_os_window_t* win);
	void*           (*windowGetNativeHandle)(jx_os_window_t* win);
	bool            (*windowClipboardGetString)(jx_os_window_t* win, char** str, uint32_t* len, jx_allocator_i* allocator);
	bool            (*windowClipboardSetString)(jx_os_window_t* win, const char* str, uint32_t len);

	jx_os_frame_tick_result (*frameTick)(void);

	// Waitable timers
	jx_os_timer_t*  (*timerCreate)();
	void            (*timerDestroy)(jx_os_timer_t* timer);
	bool            (*timerSleep)(jx_os_timer_t* timer, int64_t duration_us);

	// High-resolution timer for time interval measurements
	int64_t         (*timeNow)(void);
	int64_t         (*timeDiff)(int64_t end, int64_t start);
	int64_t         (*timeSince)(int64_t start);
	int64_t         (*timeLapTime)(int64_t* timer);
	double          (*timeConvertTo)(int64_t delta, jx_os_time_units units);

	// Timestamps for high-resolution time-of-day measurements
	uint64_t        (*timestampNow)(void);
	int64_t         (*timestampDiff)(uint64_t end, uint64_t start);
	int64_t         (*timestampSince)(uint64_t start);
	double          (*timestampConvertTo)(int64_t delta, jx_os_time_units units);
	uint32_t        (*timestampToString)(uint64_t ts, char* buffer, uint32_t max);

	int32_t         (*consoleOpen)(void);
	void            (*consoleClose)(bool waitForUserInput);

	jx_os_mutex_t*  (*mutexCreate)(void);
	void            (*mutexDestroy)(jx_os_mutex_t* mutex);
	void            (*mutexLock)(jx_os_mutex_t* mutex);
	bool            (*mutexTryLock)(jx_os_mutex_t* mutex);
	void            (*mutexUnlock)(jx_os_mutex_t* mutex);

	jx_os_semaphore_t* (*semaphoreCreate)(void);
	void               (*semaphoreDestroy)(jx_os_semaphore_t* semaphore);
	void               (*semaphoreSignal)(jx_os_semaphore_t* semaphore, uint32_t count);
	bool               (*semaphoreWait)(jx_os_semaphore_t* semaphore, uint32_t msecs);

	jx_os_event_t*  (*eventCreate)(bool manualReset, bool initialState, const char* name);
	void            (*eventDestroy)(jx_os_event_t* ev);
	bool            (*eventSet)(jx_os_event_t* ev);
	bool            (*eventReset)(jx_os_event_t* ev);
	bool            (*eventWait)(jx_os_event_t* ev, uint32_t msecs);

	uint32_t        (*threadGetID)(void);
	jx_os_thread_t* (*threadCreate)(josThreadFunc func, void* userData, uint32_t stackSize, const char* name);
	void            (*threadDestroy)(jx_os_thread_t* thread);
	void            (*threadShutdown)(jx_os_thread_t* thread);
	bool            (*threadIsRunning)(jx_os_thread_t* thread);
	int32_t         (*threadGetExitCode)(jx_os_thread_t* thread);
	uint32_t        (*getNumHardwareThreads)(void);

	jx_os_file_t*   (*fileOpenRead)(jx_file_base_dir baseDir, const char* relPath);
	jx_os_file_t*   (*fileOpenWrite)(jx_file_base_dir baseDir, const char* relPath);
	void            (*fileClose)(jx_os_file_t* f);
	uint32_t        (*fileRead)(jx_os_file_t* f, void* buffer, uint32_t len);
	uint32_t        (*fileWrite)(jx_os_file_t* f, const void* buffer, uint32_t len);
	uint64_t        (*fileGetSize)(jx_os_file_t* f);
	void            (*fileSeek)(jx_os_file_t* f, int64_t offset, jx_file_seek_origin origin);
	uint64_t        (*fileTell)(jx_os_file_t* f);
	void            (*fileFlush)(jx_os_file_t* f);
	int32_t         (*fileGetTime)(jx_os_file_t* f, jx_file_time_type type, jx_os_file_time_t* time);

	int32_t         (*fsSetBaseDir)(jx_file_base_dir whichDir, jx_file_base_dir baseDir, const char* relPath);
	int32_t         (*fsGetBaseDir)(jx_file_base_dir whichDir, char* absPath, uint32_t max);
	int32_t         (*fsRemoveFile)(jx_file_base_dir baseDir, const char* relPath);
	int32_t         (*fsCopyFile)(jx_file_base_dir srcBaseDir, const char* srcRelPath, jx_file_base_dir dstBaseDir, const char* dstRelPath);
	int32_t         (*fsMoveFile)(jx_file_base_dir srcBaseDir, const char* srcRelPath, jx_file_base_dir dstBaseDir, const char* dstRelPath);
	int32_t         (*fsCreateDirectory)(jx_file_base_dir baseDir, const char* relPath);
	int32_t         (*fsRemoveEmptyDirectory)(jx_file_base_dir baseDir, const char* relPath);
	int32_t         (*fsEnumFilesAndFolders)(jx_file_base_dir baseDir, const char* pattern, josEnumFilesAndFoldersCallback callback, void* userData);
} jx_os_api;

extern jx_os_api* os_api;

static jx_os_module_t* jx_os_moduleOpen(jx_file_base_dir baseDir, const char* path_utf8);
static void jx_os_moduleClose(jx_os_module_t* mod);
static void* jx_os_moduleGetSymbolAddr(jx_os_module_t* mod, const char* symbolName);

static jx_os_window_t* jx_os_windowOpen(const jx_os_window_desc_t* desc);
static void jx_os_windowClose(jx_os_window_t* win);
static void jx_os_windowSetTitle(jx_os_window_t* win, const char* title);
static void jx_os_windowSetCursor(jx_os_window_t* win, jx_os_cursor_type cursor);
static bool jx_os_windowSetIcon(jx_os_window_t* win, const jx_os_icon_desc_t* iconDesc);
static void jx_os_windowGetResolution(jx_os_window_t* win, uint16_t* res);
static uint32_t jx_os_windowGetFlags(jx_os_window_t* win);
static void* jx_os_windowGetNativeHandle(jx_os_window_t* win);

static jx_os_frame_tick_result jx_os_frameTick(void);

static jx_os_timer_t* jx_os_timerCreate();
static void jx_os_timerDestroy(jx_os_timer_t* timer);
static bool jx_os_timerSleep(jx_os_timer_t* timer, int64_t duration_us);

static int64_t jx_os_timeNow(void);
static int64_t jx_os_timeDiff(int64_t end, int64_t start);
static int64_t jx_os_timeSince(int64_t start);
static int64_t jx_os_timeLapTime(int64_t* timer);
static double jx_os_timeConvertTo(int64_t delta, jx_os_time_units units);

static uint64_t jx_os_timestampNow(void);
static int64_t jx_os_timestampDiff(uint64_t end, uint64_t start);
static int64_t jx_os_timestampSince(uint64_t start);
static double jx_os_timestampConvertTo(int64_t delta, jx_os_time_units units);
static uint32_t jx_os_timestampToString(uint64_t ts, char* buffer, uint32_t max);

static int32_t jx_os_consoleOpen(void);
static void jx_os_consoleClose(bool waitForUserInput);

static jx_os_mutex_t* jx_os_mutexCreate(void);
static void jx_os_mutexDestroy(jx_os_mutex_t* mutex);
static void jx_os_mutexLock(jx_os_mutex_t* mutex);
static bool jx_os_mutexTryLock(jx_os_mutex_t* mutex);
static void jx_os_mutexUnlock(jx_os_mutex_t* mutex);

static jx_os_semaphore_t* jx_os_semaphoreCreate(void);
static void jx_os_semaphoreDestroy(jx_os_semaphore_t* semaphore);
static void jx_os_semaphoreSignal(jx_os_semaphore_t* semaphore, uint32_t count);
static bool jx_os_semaphoreWait(jx_os_semaphore_t* semaphore, uint32_t msecs);

static jx_os_event_t* jx_os_eventCreate(bool manualReset, bool initialState, const char* name);
static void jx_os_eventDestroy(jx_os_event_t* ev);
static bool jx_os_eventSet(jx_os_event_t* ev);
static bool jx_os_eventReset(jx_os_event_t* ev);
static bool jx_os_eventWait(jx_os_event_t* ev, uint32_t msecs);

static uint32_t jx_os_threadGetID(void);
static jx_os_thread_t* jx_os_threadCreate(josThreadFunc func, void* userData, uint32_t stackSize, const char* name);
static void jx_os_threadDestroy(jx_os_thread_t* thread);
static void jx_os_threadShutdown(jx_os_thread_t* thread);
static bool jx_os_threadIsRunning(jx_os_thread_t* thread);
static int32_t jx_os_threadGetExitCode(jx_os_thread_t* thread);
static uint32_t jx_os_getNumHardwareThreads(void);

static jx_os_file_t* jx_os_fileOpenRead(jx_file_base_dir baseDir, const char* relPath);
static jx_os_file_t* jx_os_fileOpenWrite(jx_file_base_dir baseDir, const char* relPath);
static void jx_os_fileClose(jx_os_file_t* f);
static uint32_t jx_os_fileRead(jx_os_file_t* f, void* buffer, uint32_t len);
static uint32_t jx_os_fileWrite(jx_os_file_t* f, const void* buffer, uint32_t len);
static uint64_t jx_os_fileGetSize(jx_os_file_t* f);
static void jx_os_fileSeek(jx_os_file_t* f, int64_t offset, jx_file_seek_origin origin);
static uint64_t jx_os_fileTell(jx_os_file_t* f);
static int32_t jx_os_fileGetTime(jx_os_file_t* f, jx_file_time_type type, jx_os_file_time_t* time);

static int32_t jx_os_fsSetBaseDir(jx_file_base_dir whichDir, jx_file_base_dir baseDir, const char* relPath);
static int32_t jx_os_fsGetBaseDir(jx_file_base_dir whichDir, char* absPath, uint32_t max);
static int32_t jx_os_fsRemoveFile(jx_file_base_dir baseDir, const char* relPath);
static int32_t jx_os_fsCopyFile(jx_file_base_dir srcBaseDir, const char* srcRelPath, jx_file_base_dir dstBaseDir, const char* dstRelPath);
static int32_t jx_os_fsMoveFile(jx_file_base_dir srcBaseDir, const char* srcRelPath, jx_file_base_dir dstBaseDir, const char* dstRelPath);
static int32_t jx_os_fsCreateDirectory(jx_file_base_dir baseDir, const char* relPath);
static int32_t jx_os_fsRemoveEmptyDirectory(jx_file_base_dir baseDir, const char* relPath);
static int32_t jx_os_fsEnumFilesAndFolders(jx_file_base_dir baseDir, const char* pattern, josEnumFilesAndFoldersCallback callback, void* userData);
static void* jx_os_fsReadFile(jx_file_base_dir baseDir, const char* relPath, jx_allocator_i* allocator, bool nullTerminate, uint64_t* sz);

#ifdef __cplusplus
}

struct jx_os_mutex_scope_t
{
	jx_os_mutex_t* m_Mutex;

	jx_os_mutex_scope_t(jx_os_mutex_t* mutex) : m_Mutex(mutex)
	{
		jx_os_mutexLock(m_Mutex);
	}

	~jx_os_mutex_scope_t()
	{
		jx_os_mutexUnlock(m_Mutex);
	}
};
#endif // __cplusplus

#include "inline/os.inl"

#endif // JX_OS_H
