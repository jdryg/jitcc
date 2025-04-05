// Heavily based on chibicc (https://github.com/rui314/chibicc)
// Copyright (c) 2019 Rui Ueyama
//
// - Refactored to be a single file, removed the preprocessor and added
//   checks to (almost) all allocated objects which might end up NULL.
// - No malloc/calloc etc. All allocations are performed through 
//   custom allocators (a linear allocator per context).
// - No global state. All tokenizer and parser state are contained 
//   in structs which must be allocated (either on the heap or the stack).
// - No exit() calls on errors. 
// - No CRT (uses my custom replacement functions)
// - String (token, label) interning
// - Converted fat AST node struct into hierarchy of node types
// - Added specific token types for all keywords and punctuators (replaced
//   strcmp calls with simple equalities)
// - Removed support for VLAs
// - Unique labels using IDs instead of strings
// - Removed (most? all?) GCC extensions
// - Removed long double support (the parser recognizes long double but it 
//   uses double as type).
// - Removed atomic operations (TODO: reintroduce later?)
// - Generate IR
// - VM
// - TODO: x86_64 asm/codegen for Windows ABI
// - TODO: recover from previous error and continue parsing
// - TODO: Type interning (?)
//
#include "jcc.h"
#include <jlib/allocator.h>
#include <jlib/array.h>
#include <jlib/hashmap.h>
#include <jlib/logger.h>
#include <jlib/math.h>
#include <jlib/memory.h>
#include <jlib/os.h>
#include <jlib/string.h>

#define JCC_SOURCE_LOCATION_CUR() &(jx_cc_source_loc_t){ .m_Filename = __FILE__, .m_LineNum = __LINE__ }
#define JCC_SOURCE_LOCATION_MAKE(file, line) &(jx_cc_source_loc_t){ .m_Filename = (file), .m_LineNum = (line) }

static jx_cc_type_t* kType_void    = &(jx_cc_type_t){ .m_Kind = JCC_TYPE_VOID,    .m_Size = 1,  .m_Alignment = 1 };
static jx_cc_type_t* kType_bool    = &(jx_cc_type_t){ .m_Kind = JCC_TYPE_BOOL,    .m_Size = 1,  .m_Alignment = 1 };
static jx_cc_type_t* kType_char    = &(jx_cc_type_t){ .m_Kind = JCC_TYPE_CHAR,    .m_Size = 1,  .m_Alignment = 1 };
static jx_cc_type_t* kType_short   = &(jx_cc_type_t){ .m_Kind = JCC_TYPE_SHORT,   .m_Size = 2,  .m_Alignment = 2 };
static jx_cc_type_t* kType_int     = &(jx_cc_type_t){ .m_Kind = JCC_TYPE_INT,     .m_Size = 4,  .m_Alignment = 4 };
static jx_cc_type_t* kType_long    = &(jx_cc_type_t){ .m_Kind = JCC_TYPE_LONG,    .m_Size = 8,  .m_Alignment = 8 };
static jx_cc_type_t* kType_uchar   = &(jx_cc_type_t){ .m_Kind = JCC_TYPE_CHAR,    .m_Size = 1,  .m_Alignment = 1, .m_Flags = JCC_TYPE_FLAGS_IS_UNSIGNED_Msk };
static jx_cc_type_t* kType_ushort  = &(jx_cc_type_t){ .m_Kind = JCC_TYPE_SHORT,   .m_Size = 2,  .m_Alignment = 2, .m_Flags = JCC_TYPE_FLAGS_IS_UNSIGNED_Msk };
static jx_cc_type_t* kType_uint    = &(jx_cc_type_t){ .m_Kind = JCC_TYPE_INT,     .m_Size = 4,  .m_Alignment = 4, .m_Flags = JCC_TYPE_FLAGS_IS_UNSIGNED_Msk };
static jx_cc_type_t* kType_ulong   = &(jx_cc_type_t){ .m_Kind = JCC_TYPE_LONG,    .m_Size = 8,  .m_Alignment = 8, .m_Flags = JCC_TYPE_FLAGS_IS_UNSIGNED_Msk };
static jx_cc_type_t* kType_float   = &(jx_cc_type_t){ .m_Kind = JCC_TYPE_FLOAT,   .m_Size = 4,  .m_Alignment = 4 };
static jx_cc_type_t* kType_double  = &(jx_cc_type_t){ .m_Kind = JCC_TYPE_DOUBLE,  .m_Size = 8,  .m_Alignment = 8 };

typedef struct jcc_scope_t jcc_scope_t;
typedef struct jcc_initializer_t jcc_initializer_t;
typedef struct jcc_init_desg_t jcc_init_desg_t;

typedef struct jcc_known_token_t
{
	const char* m_Name;
	uint32_t m_Len;
	jx_cc_token_kind m_Kind;
} jcc_known_token_t;

#define JCC_DEFINE_KNOWN_TOKEN(name, kind) { .m_Name = (name), .m_Len = sizeof(name) - 1, .m_Kind = (kind) }

static const jcc_known_token_t kKeywords[] = {
	JCC_DEFINE_KNOWN_TOKEN("return", JCC_TOKEN_RETURN),
	JCC_DEFINE_KNOWN_TOKEN("if", JCC_TOKEN_IF),
	JCC_DEFINE_KNOWN_TOKEN("else", JCC_TOKEN_ELSE),
	JCC_DEFINE_KNOWN_TOKEN("for", JCC_TOKEN_FOR),
	JCC_DEFINE_KNOWN_TOKEN("while", JCC_TOKEN_WHILE),
	JCC_DEFINE_KNOWN_TOKEN("int", JCC_TOKEN_INT),
	JCC_DEFINE_KNOWN_TOKEN("sizeof", JCC_TOKEN_SIZEOF),
	JCC_DEFINE_KNOWN_TOKEN("char", JCC_TOKEN_CHAR),
	JCC_DEFINE_KNOWN_TOKEN("struct", JCC_TOKEN_STRUCT),
	JCC_DEFINE_KNOWN_TOKEN("union", JCC_TOKEN_UNION),
	JCC_DEFINE_KNOWN_TOKEN("short", JCC_TOKEN_SHORT),
	JCC_DEFINE_KNOWN_TOKEN("long", JCC_TOKEN_LONG),
	JCC_DEFINE_KNOWN_TOKEN("void", JCC_TOKEN_VOID),
	JCC_DEFINE_KNOWN_TOKEN("typedef", JCC_TOKEN_TYPEDEF),
	JCC_DEFINE_KNOWN_TOKEN("_Bool", JCC_TOKEN_BOOL),
	JCC_DEFINE_KNOWN_TOKEN("enum", JCC_TOKEN_ENUM),
	JCC_DEFINE_KNOWN_TOKEN("static", JCC_TOKEN_STATIC),
	JCC_DEFINE_KNOWN_TOKEN("goto", JCC_TOKEN_GOTO),
	JCC_DEFINE_KNOWN_TOKEN("break", JCC_TOKEN_BREAK),
	JCC_DEFINE_KNOWN_TOKEN("continue", JCC_TOKEN_CONTINUE),
	JCC_DEFINE_KNOWN_TOKEN("switch", JCC_TOKEN_SWITCH),
	JCC_DEFINE_KNOWN_TOKEN("case", JCC_TOKEN_CASE),
	JCC_DEFINE_KNOWN_TOKEN("default", JCC_TOKEN_DEFAULT),
	JCC_DEFINE_KNOWN_TOKEN("extern", JCC_TOKEN_EXTERN),
	JCC_DEFINE_KNOWN_TOKEN("_Alignof", JCC_TOKEN_ALIGNOF),
	JCC_DEFINE_KNOWN_TOKEN("_Alignas", JCC_TOKEN_ALIGNAS),
	JCC_DEFINE_KNOWN_TOKEN("do", JCC_TOKEN_DO),
	JCC_DEFINE_KNOWN_TOKEN("signed", JCC_TOKEN_SIGNED),
	JCC_DEFINE_KNOWN_TOKEN("unsigned", JCC_TOKEN_UNSIGNED),
	JCC_DEFINE_KNOWN_TOKEN("const", JCC_TOKEN_CONST),
	JCC_DEFINE_KNOWN_TOKEN("volatile", JCC_TOKEN_VOLATILE),
	JCC_DEFINE_KNOWN_TOKEN("auto", JCC_TOKEN_AUTO),
	JCC_DEFINE_KNOWN_TOKEN("register", JCC_TOKEN_REGISTER),
	JCC_DEFINE_KNOWN_TOKEN("restrict", JCC_TOKEN_RESTRICT),
	JCC_DEFINE_KNOWN_TOKEN("__restrict", JCC_TOKEN_RESTRICT),
	JCC_DEFINE_KNOWN_TOKEN("__restrict__", JCC_TOKEN_RESTRICT),
	JCC_DEFINE_KNOWN_TOKEN("_Noreturn", JCC_TOKEN_NORETURN),
	JCC_DEFINE_KNOWN_TOKEN("float", JCC_TOKEN_FLOAT),
	JCC_DEFINE_KNOWN_TOKEN("double", JCC_TOKEN_DOUBLE),
	JCC_DEFINE_KNOWN_TOKEN("typeof", JCC_TOKEN_TYPEOF),
	JCC_DEFINE_KNOWN_TOKEN("asm", JCC_TOKEN_ASM),
	JCC_DEFINE_KNOWN_TOKEN("_Thread_local", JCC_TOKEN_THREAD_LOCAL),
	JCC_DEFINE_KNOWN_TOKEN("__thread", JCC_TOKEN_THREAD_LOCAL),
	JCC_DEFINE_KNOWN_TOKEN("_Atomic", JCC_TOKEN_ATOMIC),
	JCC_DEFINE_KNOWN_TOKEN("__attribute__", JCC_TOKEN_ATTRIBUTE),
	JCC_DEFINE_KNOWN_TOKEN("inline", JCC_TOKEN_INLINE),
	JCC_DEFINE_KNOWN_TOKEN("_Generic", JCC_TOKEN_GENERIC),
	JCC_DEFINE_KNOWN_TOKEN("packed", JCC_TOKEN_PACKED),
	JCC_DEFINE_KNOWN_TOKEN("aligned", JCC_TOKEN_ALIGNED),
};

static const jcc_known_token_t kPunctuators[] = {
	JCC_DEFINE_KNOWN_TOKEN("<<=", JCC_TOKEN_LSHIFT_ASSIGN),
	JCC_DEFINE_KNOWN_TOKEN(">>=", JCC_TOKEN_RSHIFT_ASSIGN),
	JCC_DEFINE_KNOWN_TOKEN("...", JCC_TOKEN_ELLIPSIS),
	JCC_DEFINE_KNOWN_TOKEN("==", JCC_TOKEN_EQUAL),
	JCC_DEFINE_KNOWN_TOKEN("!=", JCC_TOKEN_NOT_EQUAL),
	JCC_DEFINE_KNOWN_TOKEN("<=", JCC_TOKEN_LESS_EQUAL),
	JCC_DEFINE_KNOWN_TOKEN(">=", JCC_TOKEN_GREATER_EQUAL),
	JCC_DEFINE_KNOWN_TOKEN("->", JCC_TOKEN_PTR),
	JCC_DEFINE_KNOWN_TOKEN("+=", JCC_TOKEN_ADD_ASSIGN),
	JCC_DEFINE_KNOWN_TOKEN("-=", JCC_TOKEN_SUB_ASSIGN),
	JCC_DEFINE_KNOWN_TOKEN("*=", JCC_TOKEN_MUL_ASSIGN),
	JCC_DEFINE_KNOWN_TOKEN("/=", JCC_TOKEN_DIV_ASSIGN),
	JCC_DEFINE_KNOWN_TOKEN("++", JCC_TOKEN_INC),
	JCC_DEFINE_KNOWN_TOKEN("--", JCC_TOKEN_DEC),
	JCC_DEFINE_KNOWN_TOKEN("%=", JCC_TOKEN_MOD_ASSIGN),
	JCC_DEFINE_KNOWN_TOKEN("&=", JCC_TOKEN_AND_ASSIGN),
	JCC_DEFINE_KNOWN_TOKEN("|=", JCC_TOKEN_OR_ASSIGN),
	JCC_DEFINE_KNOWN_TOKEN("^=", JCC_TOKEN_XOR_ASSIGN),
	JCC_DEFINE_KNOWN_TOKEN("&&", JCC_TOKEN_LOGICAL_AND),
	JCC_DEFINE_KNOWN_TOKEN("||", JCC_TOKEN_LOGICAL_OR),
	JCC_DEFINE_KNOWN_TOKEN("<<", JCC_TOKEN_LSHIFT),
	JCC_DEFINE_KNOWN_TOKEN(">>", JCC_TOKEN_RSHIFT),
	JCC_DEFINE_KNOWN_TOKEN("##", JCC_TOKEN_HASHASH),
	JCC_DEFINE_KNOWN_TOKEN("!", JCC_TOKEN_LOGICAL_NOT),
	JCC_DEFINE_KNOWN_TOKEN("\"", JCC_TOKEN_DOUBLE_QUOTE),
	JCC_DEFINE_KNOWN_TOKEN("#", JCC_TOKEN_HASH),
	JCC_DEFINE_KNOWN_TOKEN("$", JCC_TOKEN_DOLLAR),
	JCC_DEFINE_KNOWN_TOKEN("%", JCC_TOKEN_MOD),
	JCC_DEFINE_KNOWN_TOKEN("&", JCC_TOKEN_AND),
	JCC_DEFINE_KNOWN_TOKEN("'", JCC_TOKEN_SINGLE_QUOTE),
	JCC_DEFINE_KNOWN_TOKEN("(", JCC_TOKEN_OPEN_PAREN),
	JCC_DEFINE_KNOWN_TOKEN(")", JCC_TOKEN_CLOSE_PAREN),
	JCC_DEFINE_KNOWN_TOKEN("*", JCC_TOKEN_MUL),
	JCC_DEFINE_KNOWN_TOKEN("+", JCC_TOKEN_ADD),
	JCC_DEFINE_KNOWN_TOKEN(",", JCC_TOKEN_COMMA),
	JCC_DEFINE_KNOWN_TOKEN("-", JCC_TOKEN_SUB),
	JCC_DEFINE_KNOWN_TOKEN(".", JCC_TOKEN_DOT),
	JCC_DEFINE_KNOWN_TOKEN("/", JCC_TOKEN_DIV),
	JCC_DEFINE_KNOWN_TOKEN(":", JCC_TOKEN_COLON),
	JCC_DEFINE_KNOWN_TOKEN(";", JCC_TOKEN_SEMICOLON),
	JCC_DEFINE_KNOWN_TOKEN("<", JCC_TOKEN_LESS),
	JCC_DEFINE_KNOWN_TOKEN("=", JCC_TOKEN_ASSIGN),
	JCC_DEFINE_KNOWN_TOKEN(">", JCC_TOKEN_GREATER),
	JCC_DEFINE_KNOWN_TOKEN("?", JCC_TOKEN_QUESTIONMARK),
	JCC_DEFINE_KNOWN_TOKEN("@", JCC_TOKEN_AT),
	JCC_DEFINE_KNOWN_TOKEN("[", JCC_TOKEN_OPEN_BRACKET),
	JCC_DEFINE_KNOWN_TOKEN("\\", JCC_TOKEN_BACKWORD_SLASH),
	JCC_DEFINE_KNOWN_TOKEN("]", JCC_TOKEN_CLOSE_BRACKET),
	JCC_DEFINE_KNOWN_TOKEN("^", JCC_TOKEN_XOR),
	JCC_DEFINE_KNOWN_TOKEN("_", JCC_TOKEN_UNDERSCORE),
	JCC_DEFINE_KNOWN_TOKEN("`", JCC_TOKEN_GRAVE_ACCENT),
	JCC_DEFINE_KNOWN_TOKEN("{", JCC_TOKEN_OPEN_CURLY_BRACKET),
	JCC_DEFINE_KNOWN_TOKEN("|", JCC_TOKEN_OR),
	JCC_DEFINE_KNOWN_TOKEN("}", JCC_TOKEN_CLOSE_CURLY_BRACKET),
	JCC_DEFINE_KNOWN_TOKEN("~", JCC_TOKEN_NOT),
};

static bool jcc_isIdentifierCodepoint1(uint32_t c);
static bool jcc_isIdentifierCodepoint2(uint32_t c);

// Scope for local variables, global variables, typedefs
// or enum constants
typedef struct jcc_var_scope_t 
{
	jx_cc_object_t* m_Var;
	jx_cc_type_t* m_Typedef;
	jx_cc_type_t* m_Enum;
	int32_t m_EnumValue;
} jcc_var_scope_t;

typedef struct jcc_scope_entry_t
{
	const char* m_Key;
	uint32_t m_KeyLen;
	void* m_Value;
} jcc_scope_entry_t;

// Represents a block scope.
typedef struct jcc_scope_t 
{
	jcc_scope_t* m_Next;

	// C has two block scopes; one is for variables/typedefs and
	// the other is for struct/union/enum tags.
	jx_hashmap_t* m_Vars;
	jx_hashmap_t* m_Tags;
} jcc_scope_t;

// Variable attributes such as typedef or extern.
#define JCC_VAR_ATTR_IS_TYPEDEF_Pos 0
#define JCC_VAR_ATTR_IS_TYPEDEF_Msk (1u << JCC_VAR_ATTR_IS_TYPEDEF_Pos)
#define JCC_VAR_ATTR_IS_STATIC_Pos  1
#define JCC_VAR_ATTR_IS_STATIC_Msk  (1u << JCC_VAR_ATTR_IS_STATIC_Pos)
#define JCC_VAR_ATTR_IS_EXTERN_Pos  2
#define JCC_VAR_ATTR_IS_EXTERN_Msk  (1u << JCC_VAR_ATTR_IS_EXTERN_Pos)
#define JCC_VAR_ATTR_IS_INLINE_Pos  3
#define JCC_VAR_ATTR_IS_INLINE_Msk  (1u << JCC_VAR_ATTR_IS_INLINE_Pos)
#define JCC_VAR_ATTR_IS_TLS_Pos     4
#define JCC_VAR_ATTR_IS_TLS_Msk     (1u << JCC_VAR_ATTR_IS_TLS_Pos)

typedef struct jcc_var_attr_t 
{
	uint32_t m_Align;
	uint32_t m_Flags;
} jcc_var_attr_t;

// This struct represents a variable initializer. Since initializers
// can be nested (e.g. `int x[2][2] = {{1, 2}, {3, 4}}`), this struct
// is a tree data structure.
typedef struct jcc_initializer_t 
{
	jx_cc_type_t* m_Type;
	bool m_IsFlexible;

	// If it's not an aggregate type and has an initializer,
	// `expr` has an initialization expression.
	jx_cc_ast_expr_t* m_Expr;

	// If it's an initializer for an aggregate type (e.g. array or struct),
	// `children` has initializers for its children.
	jcc_initializer_t** m_Children;

	// Only one member can be initialized for a union.
	// `mem` is used to clarify which member is initialized.
	jx_cc_struct_member_t* m_Members;
} jcc_initializer_t;

// For local variable initializer.
typedef struct jcc_init_desg_t 
{
	jcc_init_desg_t* m_Next;
	int m_ID;
	jx_cc_struct_member_t* m_Member;
	jx_cc_object_t* m_Var;
} jcc_init_desg_t;

typedef struct jcc_translation_unit_t
{
	// All local variable instances created during parsing are
	// accumulated to this list.
	jx_cc_object_t* m_LocalsHead;
	jx_cc_object_t* m_LocalsTail;

	// Likewise, global variables are accumulated to this list.
	jx_cc_object_t* m_GlobalsHead;
	jx_cc_object_t* m_GlobalsTail;

	jcc_scope_t* m_Scope;

	// Points to the function object the parser is currently parsing.
	jx_cc_object_t* m_CurFunction;

	// Lists of all goto statements and labels in the curent function.
	jx_cc_ast_stmt_goto_t* m_CurFuncGotos;
	jx_cc_ast_stmt_label_t* m_CurFuncLabels;

	// Current "goto" and "continue" jump targets.
	jx_cc_label_t m_CurFuncBreakLabel;
	jx_cc_label_t m_CurFuncContinueLabel;

	// Points to a node representing a switch if we are parsing
	// a switch statement. Otherwise, NULL.
	jx_cc_ast_stmt_switch_t* m_CurFuncSwitch;

	// Tokenizer
	const char* m_CurFilename;
	uint32_t m_CurLineNumber;
	bool m_AtBeginOfLine;      // True if the current position is at the beginning of a line
	bool m_HasSpace;           // True if the current position follows a space character

	uint32_t m_NextLabelID;
	uint32_t m_NextLocalVarID;
	uint32_t m_NextGlobalVarID;
} jcc_translation_unit_t;

typedef struct jx_cc_context_t
{
	jx_allocator_i* m_Allocator;
	jx_allocator_i* m_LinearAllocator;
	jx_string_table_t* m_StringTable;
	jx_cc_translation_unit_t** m_TranslationUnitsArr;
	jx_logger_i* m_Logger;
} jx_cc_context_t;

jx_cc_token_t* jcc_tokenizeString(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, char* source, uint64_t sourceLen);
static bool jcc_convertPreprocessorNumber(jx_cc_token_t* tok);
static bool jcc_parse(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_token_t* tok);

static bool jcc_tokIs(jx_cc_token_t* tok, jx_cc_token_kind kind);
static bool jcc_tokExpect(jx_cc_token_t** tok, jx_cc_token_kind kind);
static bool jcc_tokExpectStr(jx_cc_token_t** tok, const char* op);

static bool jx_cc_typeIsInteger(const jx_cc_type_t* ty);
static bool jx_cc_typeIsNumeric(const jx_cc_type_t* ty);
static bool jcc_typeIsCompatible(const jx_cc_type_t* t1, const jx_cc_type_t* t2);
static bool jcc_typeIsSame(const jx_cc_type_t* t1, const jx_cc_type_t* t2);

static jcc_initializer_t* jcc_allocInitializer(jx_cc_context_t* ctx, jx_cc_type_t* ty, bool is_flexible);
static jx_cc_object_t* jcc_tuVarAlloc(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, const char* name, jx_cc_type_t* ty);

static bool jcc_tuIsTypename(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_token_t* tok);
static bool jcc_tuEnterScope(jx_cc_context_t* ctx, jcc_translation_unit_t* tu);
static void jcc_tuLeaveScope(jx_cc_context_t* ctx, jcc_translation_unit_t* tu);
static jx_cc_object_t* jcc_tuVarAllocLocal(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, const char* name, jx_cc_type_t* ty);
static jx_cc_object_t* jcc_tuVarAllocAnonLocal(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_type_t* ty);

static int64_t jcc_parseConstExpression(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_token_t** tokenListPtr, bool* err);
static jx_cc_type_t* jcc_parseDeclarationSpecifiers(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_token_t** tokenListPtr, jcc_var_attr_t* attr);
static jx_cc_type_t* jcc_parseTypename(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_token_t** tokenListPtr);
static jx_cc_type_t* jcc_parseEnum(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_token_t** tokenListPtr);
static jx_cc_type_t* jcc_parseTypeof(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_token_t** tokenListPtr);
static jx_cc_type_t* jcc_parseTypeSuffix(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_token_t** tokenListPtr, jx_cc_type_t* ty);
static jx_cc_type_t* jcc_parseDeclarator(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_token_t** tokenListPtr, jx_cc_type_t* ty);
static jx_cc_ast_stmt_t* jcc_parseDeclaration(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_token_t** tokenListPtr, jx_cc_type_t* basety, jcc_var_attr_t* attr);
static bool jcc_parseArrayInitializer2(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_token_t** tokenListPtr, jcc_initializer_t* init, int i);
static bool jcc_parseStructInitializer2(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_token_t** tokenListPtr, jcc_initializer_t* init, jx_cc_struct_member_t* mem);
static bool jcc_parseInitializer2(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_token_t** tokenListPtr, jcc_initializer_t* init);
static jcc_initializer_t* jcc_parseInitializer(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_token_t** tokenListPtr, jx_cc_type_t* ty, jx_cc_type_t** new_ty);
static jx_cc_ast_expr_t* jcc_parseLocalVarInitializer(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_token_t** tokenListPtr, jx_cc_object_t* var);
static bool jcc_parseGlobalVarInitializer(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_token_t** tokenListPtr, jx_cc_object_t* var);
static jx_cc_ast_stmt_t* jcc_parseCompoundStatement(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_token_t** tokenListPtr);
static jx_cc_ast_stmt_t* jcc_parseStatement(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_token_t** tokenListPtr);
static jx_cc_ast_stmt_t* jcc_parseExpressionStatement(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_token_t** tokenListPtr);
static jx_cc_ast_expr_t* jcc_parseExpression(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_token_t** tokenListPtr);
static int64_t jcc_astEvalConstExpression(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_ast_expr_t* node, bool* err);
static int64_t jcc_astEvalConstExpression2(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_ast_expr_t* node, char*** label, bool* err);
static double jcc_astEvalConstExpressionDouble(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_ast_expr_t* node, bool* err);
static int64_t jcc_astEvalRValue(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_ast_expr_t* node, char*** label, bool* err);
static bool jcc_astIsConstExpression(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_ast_expr_t* node);
static jx_cc_ast_expr_t* jcc_parseAssignment(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_token_t** tokenListPtr);
static jx_cc_ast_expr_t* jcc_parseLogicalOr(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_token_t** tokenListPtr);
static jx_cc_ast_expr_t* jcc_parseConditional(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_token_t** tokenListPtr);
static jx_cc_ast_expr_t* jcc_parseLogicalAnd(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_token_t** tokenListPtr);
static jx_cc_ast_expr_t* jcc_parseBitwiseOr(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_token_t** tokenListPtr);
static jx_cc_ast_expr_t* jcc_parseBitwiseXor(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_token_t** tokenListPtr);
static jx_cc_ast_expr_t* jcc_parseBitwiseAnd(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_token_t** tokenListPtr);
static jx_cc_ast_expr_t* jcc_parseEquality(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_token_t** tokenListPtr);
static jx_cc_ast_expr_t* jcc_parseRelational(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_token_t** tokenListPtr);
static jx_cc_ast_expr_t* jcc_parseShift(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_token_t** tokenListPtr);
static jx_cc_ast_expr_t* jcc_parseAdd(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_token_t** tokenListPtr);
static jx_cc_ast_expr_t* jcc_parseMul(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_token_t** tokenListPtr);
static jx_cc_ast_expr_t* jcc_parseCast(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_token_t** tokenListPtr);
static jx_cc_struct_member_t* jcc_getStructMember(jx_cc_type_t* ty, jx_cc_token_t* tok);
static jx_cc_type_t* jcc_parseStructDeclaration(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_token_t** tokenListPtr);
static jx_cc_type_t* jcc_parseUnionDeclaration(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_token_t** tokenListPtr);
static jx_cc_ast_expr_t* jcc_parsePostfix(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_token_t** tokenListPtr);
static jx_cc_ast_expr_t* jcc_parseFuncCall(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_token_t** tokenListPtr, jx_cc_ast_expr_t* node);
static jx_cc_ast_expr_t* jcc_parseUnary(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_token_t** tokenListPtr);
static jx_cc_ast_expr_t* jcc_parsePrimaryExpression(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_token_t** tokenListPtr);
static bool jcc_parseTypedef(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_token_t** tokenListPtr, jx_cc_type_t* basety);
static bool jcc_isFunction(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_token_t* tok);
static bool jcc_parseFunction(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_token_t** tokenListPtr, jx_cc_type_t* basety, jcc_var_attr_t* attr);
static bool jcc_parseGlobalVariable(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_token_t** tokenListPtr, jx_cc_type_t* basety, jcc_var_attr_t* attr);
static jx_cc_type_t* jcc_typeAlloc(jx_cc_context_t* ctx, jx_cc_type_kind kind, int size, uint32_t align);
static jx_cc_type_t* jcc_typeCopy(jx_cc_context_t* ctx, const jx_cc_type_t* ty);
static jx_cc_type_t* jcc_typeAllocPointerTo(jx_cc_context_t* ctx, jx_cc_type_t* base);
static jx_cc_type_t* jcc_typeAllocFunc(jx_cc_context_t* ctx, jx_cc_type_t* return_ty);
static jx_cc_type_t* jcc_typeAllocArrayOf(jx_cc_context_t* ctx, jx_cc_type_t* base, int size);
static jx_cc_type_t* jcc_typeAllocEnum(jx_cc_context_t* ctx);
static jx_cc_type_t* jcc_typeAllocStruct(jx_cc_context_t* ctx);
static bool jcc_astAddType(jx_cc_context_t* ctx, jx_cc_ast_node_t* node);

static void jcc_logError(jx_cc_context_t* ctx, const jx_cc_source_loc_t* loc, const char* fmt, ...);

jx_cc_context_t* jx_cc_createContext(jx_allocator_i* allocator, jx_logger_i* logger)
{
	jx_cc_context_t* ctx = (jx_cc_context_t*)JX_ALLOC(allocator, sizeof(jx_cc_context_t));
	if (!ctx) {
		return NULL;
	}

	jx_memset(ctx, 0, sizeof(jx_cc_context_t));
	ctx->m_Allocator = allocator;
	ctx->m_Logger = logger;

	ctx->m_LinearAllocator = allocator_api->createLinearAllocator(1u << 20, allocator);
	if (!ctx->m_LinearAllocator) {
		jx_cc_destroyContext(ctx);
		return NULL;
	}

	ctx->m_StringTable = jx_strtable_create(allocator);
	if (!ctx->m_StringTable) {
		jx_cc_destroyContext(ctx);
		return NULL;
	}

	ctx->m_TranslationUnitsArr = (jx_cc_translation_unit_t**)jx_array_create(allocator);
	if (!ctx->m_TranslationUnitsArr) {
		jx_cc_destroyContext(ctx);
		return NULL;
	}

	jx_strtable_insert(ctx->m_StringTable, "void", UINT32_MAX);
	jx_strtable_insert(ctx->m_StringTable, "bool", UINT32_MAX);
	jx_strtable_insert(ctx->m_StringTable, "char", UINT32_MAX);
	jx_strtable_insert(ctx->m_StringTable, "short", UINT32_MAX);
	jx_strtable_insert(ctx->m_StringTable, "int", UINT32_MAX);
	jx_strtable_insert(ctx->m_StringTable, "long", UINT32_MAX);
	jx_strtable_insert(ctx->m_StringTable, "signed", UINT32_MAX);
	jx_strtable_insert(ctx->m_StringTable, "unsigned", UINT32_MAX);
	jx_strtable_insert(ctx->m_StringTable, "struct", UINT32_MAX);
	jx_strtable_insert(ctx->m_StringTable, "union", UINT32_MAX);

	return ctx;
}

void jx_cc_destroyContext(jx_cc_context_t* ctx)
{
	jx_allocator_i* allocator = ctx->m_Allocator;

	const uint32_t numTranslationUnits = (uint32_t)jx_array_sizeu(ctx->m_TranslationUnitsArr);
	for (uint32_t iTU = 0; iTU < numTranslationUnits; ++iTU) {
		jx_cc_translation_unit_t* tu = ctx->m_TranslationUnitsArr[iTU];

		jx_cc_object_t* global = tu->m_Globals;
		while (global) {
			if (global->m_FuncRefsArr) {
				jx_array_free(global->m_FuncRefsArr);
				global->m_FuncRefsArr = NULL;
			}

			global = global->m_Next;
		}

		JX_FREE(ctx->m_Allocator, tu);
	}
	jx_array_free(ctx->m_TranslationUnitsArr);
	ctx->m_TranslationUnitsArr = NULL;

	if (ctx->m_StringTable) {
		jx_strtable_destroy(ctx->m_StringTable);
		ctx->m_StringTable = NULL;
	}

	if (ctx->m_LinearAllocator) {
		allocator_api->destroyLinearAllocator(ctx->m_LinearAllocator);
		ctx->m_LinearAllocator = NULL;
	}

	JX_FREE(allocator, ctx);
}

jx_cc_translation_unit_t* jx_cc_compileFile(jx_cc_context_t* ctx, jx_file_base_dir baseDir, const char* filename)
{
	jx_cc_translation_unit_t* unit = (jx_cc_translation_unit_t*)JX_ALLOC(ctx->m_Allocator, sizeof(jx_cc_translation_unit_t));
	if (!unit) {
		return NULL;
	}

	jx_memset(unit, 0, sizeof(jx_cc_translation_unit_t));

	jcc_translation_unit_t* tu = &(jcc_translation_unit_t){ 0 };

	uint64_t sourceLen = 0ull;
	char* source = (char*)jx_os_fsReadFile(baseDir, filename, ctx->m_Allocator, true, &sourceLen);
	if (!source) {
		jcc_logError(ctx, JCC_SOURCE_LOCATION_CUR(), "Internal Error: Failed to open file \"%s\".", filename);
		JX_FREE(ctx->m_Allocator, unit);
		return NULL;
	}

	tu->m_CurFilename = filename;

	if (!jcc_tuEnterScope(ctx, tu)) {
		jcc_logError(ctx, JCC_SOURCE_LOCATION_CUR(), "Internal Error: Memory allocation failed.");
		goto end;
	}

	jx_cc_token_t* tok = jcc_tokenizeString(ctx, tu, source, sourceLen);

	JX_FREE(ctx->m_Allocator, source);

	if (!tok) {
		jcc_tuLeaveScope(ctx, tu);
		goto end;
	}

	// Convert preprocessor numbers
	for (jx_cc_token_t* t = tok; t->m_Kind != JCC_TOKEN_EOF; t = t->m_Next) {
		if (t->m_Kind == JCC_TOKEN_PREPROC_NUMBER) {
			if (!jcc_convertPreprocessorNumber(t)) {
				jcc_logError(ctx, JCC_SOURCE_LOCATION_CUR(), "Internal Error: Failed to convert preprocessor number.");
				jcc_tuLeaveScope(ctx, tu);
				goto end;
			}
		}
	}

	bool res = jcc_parse(ctx, tu, tok);
	if (!res) {
		jcc_tuLeaveScope(ctx, tu);
		goto end;
	}

	jcc_tuLeaveScope(ctx, tu);

end:
	unit->m_Globals = tu->m_GlobalsHead;

	jx_array_push_back(ctx->m_TranslationUnitsArr, unit);

	return unit;
}

// Round up `n` to the nearest multiple of `align`. For instance,
// jcc_alignTo(5, 8) returns 8 and jcc_alignTo(11, 8) returns 16.
static int jcc_alignTo(int n, int align)
{
	return ((n + align - 1) / align) * align;
}

static int jcc_alignDown(int n, int align) 
{
	return jcc_alignTo(n - align + 1, align);
}

static uint64_t jcc_scopeEntryHashCallback(const void* item, uint64_t seed0, uint64_t seed1, void* udata)
{
	JX_UNUSED(udata);
	const jcc_scope_entry_t* node = (const jcc_scope_entry_t*)item;
	return jx_hashFNV1a_cstr(node->m_Key, node->m_KeyLen, seed0, seed1);
}

static int32_t jcc_scopeEntryCompareCallback(const void* a, const void* b, void* udata)
{
	JX_UNUSED(udata);
	const jcc_scope_entry_t* nodeA = (const jcc_scope_entry_t*)a;
	const jcc_scope_entry_t* nodeB = (const jcc_scope_entry_t*)b;
	return str_api->strcmp(nodeA->m_Key, nodeA->m_KeyLen, nodeB->m_Key, nodeB->m_KeyLen);
}

static bool jcc_tuEnterScope(jx_cc_context_t* ctx, jcc_translation_unit_t* tu)
{
	jcc_scope_t* sc = (jcc_scope_t*)JX_ALLOC(ctx->m_Allocator, sizeof(jcc_scope_t));
	if (!sc) {
		return false;
	}
	
	jx_memset(sc, 0, sizeof(jcc_scope_t));
	sc->m_Next = tu->m_Scope;
	tu->m_Scope = sc;

	sc->m_Vars = jx_hashmapCreate(ctx->m_Allocator, sizeof(jcc_scope_entry_t), 64, 0, 0, jcc_scopeEntryHashCallback, jcc_scopeEntryCompareCallback, NULL, ctx);
	sc->m_Tags = jx_hashmapCreate(ctx->m_Allocator, sizeof(jcc_scope_entry_t), 64, 0, 0, jcc_scopeEntryHashCallback, jcc_scopeEntryCompareCallback, NULL, ctx);

	if (!sc->m_Vars || !sc->m_Tags) {
		jcc_tuLeaveScope(ctx, tu);
		return false;
	}

	return true;
}

static void jcc_tuLeaveScope(jx_cc_context_t* ctx, jcc_translation_unit_t* tu)
{
	jcc_scope_t* sc = tu->m_Scope;
	tu->m_Scope = sc->m_Next;

	if (sc->m_Vars) {
		jx_hashmapDestroy(sc->m_Vars);
		sc->m_Vars = NULL;
	}

	if (sc->m_Tags) {
		jx_hashmapDestroy(sc->m_Tags);
		sc->m_Tags = NULL;
	}

	JX_FREE(ctx->m_Allocator, sc);
}

// Find a variable by name.
static jcc_var_scope_t* jcc_tuFindVariable(jcc_translation_unit_t* tu, jx_cc_token_t* tok)
{
	jcc_scope_entry_t* key = &(jcc_scope_entry_t) { 
		.m_Key = tok->m_String,
		.m_KeyLen = tok->m_Length
	};
	for (jcc_scope_t* sc = tu->m_Scope; sc; sc = sc->m_Next) {
		jcc_scope_entry_t* entry = jx_hashmapGet(sc->m_Vars, key);
		if (entry) {
			return (jcc_var_scope_t*)entry->m_Value;
		}
	}
	
	return NULL;
}

// Find a struct/union by name
static jx_cc_type_t* jcc_tuFindTag(jcc_translation_unit_t* tu, jx_cc_token_t* tok)
{
	const jcc_scope_entry_t* key = &(jcc_scope_entry_t) { 
		.m_Key = tok->m_String,
		.m_KeyLen = tok->m_Length
	};

	for (jcc_scope_t* sc = tu->m_Scope; sc; sc = sc->m_Next) {
		jcc_scope_entry_t* entry = jx_hashmapGet(sc->m_Tags, key);
		if (entry) {
			return (jx_cc_type_t*)entry->m_Value;
		}
	}

	return NULL;
}

static jx_cc_ast_node_t* jcc_astAllocNode(jx_cc_context_t* ctx, jx_cc_ast_node_kind kind, jx_cc_token_t* tok, size_t sz)
{
	JX_CHECK(sz >= sizeof(jx_cc_ast_node_t), "AST node size must be at least sizeof(jx_cc_ast_node_t)");

	jx_cc_ast_node_t* node = (jx_cc_ast_node_t*)JX_ALLOC(ctx->m_LinearAllocator, sz);
	if (!node) {
		return NULL;
	}

	jx_memset(node, 0, sz);
	node->m_Kind = kind;
	node->m_Token = tok;

	return node;
}

static jx_cc_ast_expr_t* jcc_astAllocExpr(jx_cc_context_t* ctx, jx_cc_ast_node_kind kind, jx_cc_token_t* tok, jx_cc_type_t* ty, size_t sz)
{
	JX_CHECK(sz >= sizeof(jx_cc_ast_expr_t), "AST expression node size must be at least sizeof(jx_cc_ast_expr_t)");

	jx_cc_ast_expr_t* node = (jx_cc_ast_expr_t*)jcc_astAllocNode(ctx, kind, tok, sz);
	if (!node) {
		return NULL;
	}

	node->m_Type = ty;

	return node;
}

static jx_cc_ast_stmt_t* jcc_astAllocStmt(jx_cc_context_t* ctx, jx_cc_ast_node_kind kind, jx_cc_token_t* tok, size_t sz)
{
	JX_CHECK(sz >= sizeof(jx_cc_ast_stmt_t), "AST statement node size must be at least sizeof(jx_cc_ast_stmt_t)");

	jx_cc_ast_stmt_t* node = (jx_cc_ast_stmt_t*)jcc_astAllocNode(ctx, kind, tok, sz);
	if (!node) {
		return NULL;
	}

	// TODO: Initialize any jx_cc_ast_stmt_t members here

	return node;
}

static jx_cc_ast_expr_t* jcc_astAllocExprNull(jx_cc_context_t* ctx, jx_cc_token_t* tok)
{
	return jcc_astAllocExpr(ctx, JCC_NODE_EXPR_NULL, tok, kType_void, sizeof(jx_cc_ast_expr_t));
}

static jx_cc_ast_expr_t* jcc_astAllocExprBinary(jx_cc_context_t* ctx, jx_cc_ast_node_kind kind, jx_cc_ast_expr_t* lhs, jx_cc_ast_expr_t* rhs, jx_cc_token_t* tok)
{
	if (!lhs || !rhs) {
		return NULL;
	}

	jx_cc_ast_expr_binary_t* node = (jx_cc_ast_expr_binary_t*)jcc_astAllocExpr(ctx, kind, tok, NULL, sizeof(jx_cc_ast_expr_binary_t));
	if (!node) {
		return NULL;
	}

	node->m_ExprLHS = lhs;
	node->m_ExprRHS = rhs;

	return &node->super;
}

static jx_cc_ast_expr_t* jcc_astAllocExprComma(jx_cc_context_t* ctx, jx_cc_ast_expr_t* lhs, jx_cc_ast_expr_t* rhs, jx_cc_token_t* tok)
{
	// Check if only one of lhs, rhs are valid expressions (not NULL and not JCC_NODE_EXPR_NULL) and return that one.
	if (!lhs || !rhs) {
		return NULL;
	} else if (lhs->super.m_Kind == JCC_NODE_EXPR_NULL) {
		return rhs;
	} else if (rhs->super.m_Kind == JCC_NODE_EXPR_NULL) {
		return lhs;
	}

	jx_cc_ast_expr_binary_t* node = (jx_cc_ast_expr_binary_t*)jcc_astAllocExpr(ctx, JCC_NODE_EXPR_COMMA, tok, NULL, sizeof(jx_cc_ast_expr_binary_t));
	if (!node) {
		return NULL;
	}

	node->m_ExprLHS = lhs;
	node->m_ExprRHS = rhs;

	return &node->super;
}

static jx_cc_ast_expr_t* jcc_astAllocExprUnary(jx_cc_context_t* ctx, jx_cc_ast_node_kind kind, jx_cc_ast_expr_t* expr, jx_cc_token_t* tok)
{
	if (!expr) {
		return NULL;
	}

	jx_cc_ast_expr_unary_t* node = (jx_cc_ast_expr_unary_t*)jcc_astAllocExpr(ctx, kind, tok, NULL, sizeof(jx_cc_ast_expr_unary_t));
	if (!node) {
		return NULL;
	}

	node->m_Expr = expr;
	
	return &node->super;
}

static jx_cc_ast_expr_t* jcc_astAllocExprIConst(jx_cc_context_t* ctx, int64_t val, jx_cc_type_t* type, jx_cc_token_t* tok)
{
	jx_cc_ast_expr_iconst_t* node = (jx_cc_ast_expr_iconst_t*)jcc_astAllocExpr(ctx, JCC_NODE_NUMBER, tok, NULL, sizeof(jx_cc_ast_expr_iconst_t));
	if (!node) {
		return NULL;
	}

	node->m_Value = val;
	
	return &node->super;
}

static jx_cc_ast_expr_t* jcc_astAllocExprIConst_long(jx_cc_context_t* ctx, int64_t val, jx_cc_token_t* tok)
{
	return jcc_astAllocExprIConst(ctx, val, kType_long, tok);
}

static jx_cc_ast_expr_t* jcc_astAllocExprIConst_ulong(jx_cc_context_t* ctx, uint64_t val, jx_cc_token_t* tok)
{
	return jcc_astAllocExprIConst(ctx, (int64_t)val, kType_ulong, tok);
}

static jx_cc_ast_expr_t* jcc_astAllocExprFConst(jx_cc_context_t* ctx, double val, jx_cc_type_t* type, jx_cc_token_t* tok)
{
	jx_cc_ast_expr_fconst_t* node = (jx_cc_ast_expr_fconst_t*)jcc_astAllocExpr(ctx, JCC_NODE_NUMBER, tok, type, sizeof(jx_cc_ast_expr_fconst_t));
	if (!node) {
		return NULL;
	}

	node->m_Value = val;

	return &node->super;
}

static jx_cc_ast_expr_t* jcc_astAllocExprVar(jx_cc_context_t* ctx, jx_cc_object_t* var, jx_cc_token_t* tok)
{
	if (!var) {
		return NULL;
	}

	jx_cc_ast_expr_variable_t* node = (jx_cc_ast_expr_variable_t*)jcc_astAllocExpr(ctx, JCC_NODE_VARIABLE, tok, var->m_Type, sizeof(jx_cc_ast_expr_variable_t));
	if (!node) {
		return NULL;
	}

	node->m_Var = var;
	
	return &node->super;
}

static jx_cc_ast_expr_t* jcc_astAllocExprCast(jx_cc_context_t* ctx, jx_cc_ast_expr_t* expr, jx_cc_type_t* ty)
{
	if (!expr || !ty) {
		return NULL;
	}

	if (!jcc_astAddType(ctx, &expr->super)) {
		return NULL;
	}

	if (jcc_typeIsSame(ty, expr->m_Type)) {
		return expr;
	}

	jx_cc_type_t* tyCopy = jcc_typeCopy(ctx, ty);
	if (!tyCopy) {
		return NULL;
	}
	
	jx_cc_ast_expr_unary_t* node = (jx_cc_ast_expr_unary_t*)jcc_astAllocExpr(ctx, JCC_NODE_EXPR_CAST, expr->super.m_Token, tyCopy, sizeof(jx_cc_ast_expr_unary_t));
	if (!node) {
		return NULL;
	}

	node->m_Expr = expr;

	return &node->super;
}

static jx_cc_ast_expr_t* jcc_astAllocExprMember(jx_cc_context_t* ctx, jx_cc_ast_expr_t* expr, jx_cc_struct_member_t* member, jx_cc_token_t* tok)
{
	if (!expr || !member) {
		return NULL;
	}

	jx_cc_ast_expr_member_t* node = (jx_cc_ast_expr_member_t*)jcc_astAllocExpr(ctx, JCC_NODE_EXPR_MEMBER, tok, NULL, sizeof(jx_cc_ast_expr_member_t));
	if (!node) {
		return NULL;
	}

	node->m_Expr = expr;
	node->m_Member = member;

	return &node->super;
}

static jx_cc_ast_expr_t* jcc_astAllocExprMemZero(jx_cc_context_t* ctx, jx_cc_object_t* var, jx_cc_token_t* tok)
{
	if (!var) {
		return NULL;
	}

	jx_cc_ast_expr_variable_t* node = (jx_cc_ast_expr_variable_t*)jcc_astAllocExpr(ctx, JCC_NODE_EXPR_MEMZERO, tok, NULL, sizeof(jx_cc_ast_expr_variable_t));
	if (!node) {
		return NULL;
	}

	node->m_Var = var;

	return &node->super;
}

static jx_cc_ast_expr_t* jcc_astAllocExprCond(jx_cc_context_t* ctx, jx_cc_ast_expr_t* condExpr, jx_cc_ast_expr_t* thenExpr, jx_cc_ast_expr_t* elseExpr, jx_cc_token_t* tok)
{
	if (!condExpr || !thenExpr || !elseExpr) {
		return NULL;
	}

	jx_cc_ast_expr_cond_t* node = (jx_cc_ast_expr_cond_t*)jcc_astAllocExpr(ctx, JCC_NODE_EXPR_CONDITIONAL, tok, NULL, sizeof(jx_cc_ast_expr_cond_t));
	if (!node) {
		return NULL;
	}

	node->m_CondExpr = condExpr;
	node->m_ThenExpr = thenExpr;
	node->m_ElseExpr = elseExpr;

	return &node->super;
}

static jx_cc_ast_expr_t* jcc_astAllocExprFuncCall(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_ast_expr_t* funcExpr, jx_cc_ast_expr_t** args, uint32_t numArgs, jx_cc_type_t* ty, jx_cc_token_t* tok)
{
	if (!funcExpr || !ty) {
		return NULL;
	}

	jx_cc_type_t* retType = ty->m_FuncRetType;
	if (!retType) {
		return NULL;
	}

	jx_cc_ast_expr_funccall_t* node = (jx_cc_ast_expr_funccall_t*)jcc_astAllocExpr(ctx, JCC_NODE_EXPR_FUNC_CALL, tok, retType, sizeof(jx_cc_ast_expr_funccall_t));
	if (!node) {
		return NULL;
	}

	node->m_FuncExpr = funcExpr;
	node->m_FuncType = ty;

	if (numArgs != 0) {
		node->m_Args = (jx_cc_ast_expr_t**)JX_ALLOC(ctx->m_LinearAllocator, sizeof(jx_cc_ast_expr_t*) * numArgs);
		if (!node->m_Args) {
			return NULL;
		}
		jx_memcpy(node->m_Args, args, sizeof(jx_cc_ast_expr_t*) * numArgs);
	}
	node->m_NumArgs = numArgs;

	return &node->super;
}

static jx_cc_ast_expr_t* jcc_astAllocExprGetElementPtr(jx_cc_context_t* ctx, jx_cc_type_t* baseType, jx_cc_ast_expr_t* ptr, jx_cc_ast_expr_t* idx, jx_cc_token_t* tok)
{
	if (!ptr || !idx) {
		return NULL;
	}

	jx_cc_ast_expr_get_element_ptr_t* node = (jx_cc_ast_expr_get_element_ptr_t*)jcc_astAllocExpr(ctx
		, JCC_NODE_EXPR_GET_ELEMENT_PTR
		, tok
		, jcc_typeAllocPointerTo(ctx, baseType)
		, sizeof(jx_cc_ast_expr_get_element_ptr_t)
	);
	if (!node) {
		return NULL;
	}

	node->m_ExprPtr = ptr;
	node->m_ExprIndex = idx;

	return &node->super;
}

// In C, `+` operator is overloaded to perform the pointer arithmetic.
// If p is a pointer, p+n adds not n but sizeof(*p)*n to the value of p,
// so that p+n points to the location n elements (not bytes) ahead of p.
// In other words, we need to scale an integer value before adding to a
// pointer value. This function takes care of the scaling.
static jx_cc_ast_expr_t* jcc_astAllocExprAdd(jx_cc_context_t* ctx, jx_cc_ast_expr_t* lhs, jx_cc_ast_expr_t* rhs, jx_cc_token_t* tok)
{
	if (!lhs || !rhs) {
		return NULL;
	}

	if (!jcc_astAddType(ctx, &lhs->super)) {
		return NULL;
	}
	if (!jcc_astAddType(ctx, &rhs->super)) {
		return NULL;
	}

	jx_cc_ast_expr_t* node = NULL;

	const bool lhsIsNumeric = jx_cc_typeIsNumeric(lhs->m_Type);
	const bool rhsIsNumeric = jx_cc_typeIsNumeric(rhs->m_Type);
	const bool lhsIsPtr = lhs->m_Type->m_BaseType;
	const bool rhsIsPtr = rhs->m_Type->m_BaseType;

	if (lhsIsNumeric && rhsIsNumeric) {
		// num + num
		node = jcc_astAllocExprBinary(ctx, JCC_NODE_EXPR_ADD, lhs, rhs, tok);
	} else if (lhsIsPtr && rhsIsPtr) {
		return NULL; // ERROR: invalid operands // TODO: Requires the translation unit to report errors.
	} else {
		// Canonicalize `num + ptr` to `ptr + num`.
		if (!lhsIsPtr && rhsIsPtr) {
			jx_cc_ast_expr_t* tmp = lhs;
			lhs = rhs;
			rhs = tmp;
		}

		node = jcc_astAllocExprGetElementPtr(ctx, lhs->m_Type->m_BaseType, lhs, rhs, tok);
	}

	return node;
}

// Like `+`, `-` is overloaded for the pointer type.
static jx_cc_ast_expr_t* jcc_astAllocExprSub(jx_cc_context_t* ctx, jx_cc_ast_expr_t* lhs, jx_cc_ast_expr_t* rhs, jx_cc_token_t* tok)
{
	if (!lhs || !rhs) {
		return NULL;
	}

	if (!jcc_astAddType(ctx, &lhs->super)) {
		return NULL;
	}
	if (!jcc_astAddType(ctx, &rhs->super)) {
		return NULL;
	}

	const bool lhsIsNumeric = jx_cc_typeIsNumeric(lhs->m_Type);
	const bool rhsIsNumeric = jx_cc_typeIsNumeric(rhs->m_Type);
	const bool lhsIsPointer = lhs->m_Type->m_BaseType != NULL;
	const bool rhsIsPointer = rhs->m_Type->m_BaseType != NULL;

	jx_cc_ast_expr_t* node = NULL;
	if (lhsIsNumeric && rhsIsNumeric) {
		// num - num
		node = jcc_astAllocExprBinary(ctx, JCC_NODE_EXPR_SUB, lhs, rhs, tok);
	} if (lhsIsPointer && rhsIsNumeric) {
		jx_cc_ast_expr_t* rhsNeg = jcc_astAllocExprUnary(ctx, JCC_NODE_EXPR_NEG, rhs, tok);
		node = jcc_astAllocExprGetElementPtr(ctx, lhs->m_Type->m_BaseType, lhs, rhsNeg, tok);
	} else if (lhsIsPointer && rhsIsPointer) {
		// ptr - ptr, which returns how many elements are between the two.
		if (!jcc_typeIsSame(lhs->m_Type, rhs->m_Type)) {
			return NULL;
		}

		jx_cc_ast_expr_t* sub = jcc_astAllocExprBinary(ctx
			, JCC_NODE_EXPR_SUB
			, jcc_astAllocExprCast(ctx, lhs, kType_long)
			, jcc_astAllocExprCast(ctx, rhs, kType_long)
			, tok
		);
		if (!sub) {
			return NULL;
		}

		sub->m_Type = kType_long;

		node = jcc_astAllocExprBinary(ctx
			, JCC_NODE_EXPR_DIV
			, sub
			, jcc_astAllocExprIConst(ctx, lhs->m_Type->m_BaseType->m_Size, kType_long, tok)
			, tok
		);
	}

	if (!node) {
		jcc_logError(ctx, &tok->m_Loc, "Invalid operands.");
		return NULL; // ERROR: "invalid operands"
	}

	return node; 
}

static jx_cc_ast_expr_t* jcc_astAllocExprCompoundAssign(jx_cc_context_t* ctx, jx_cc_ast_expr_t* lhs, jx_cc_ast_expr_t* rhs, jx_cc_ast_node_kind op, jx_cc_token_t* tok)
{
	if (!lhs || !rhs) {
		return NULL;
	}

	jx_cc_ast_expr_compound_assign_t* node = (jx_cc_ast_expr_compound_assign_t*)jcc_astAllocExpr(ctx, JCC_NODE_EXPR_COMPOUND_ASSIGN, tok, lhs->m_Type, sizeof(jx_cc_ast_expr_compound_assign_t));
	if (!node) {
		return NULL;
	}

	node->m_ExprLHS = lhs;
	node->m_ExprRHS = rhs;
	node->m_Op = op;

	return &node->super;
}

static jx_cc_ast_stmt_t* jcc_astAllocStmtAsm(jx_cc_context_t* ctx, const char* asmCodeStr, uint32_t specs, jx_cc_token_t* tok)
{
	if (!asmCodeStr) {
		return NULL;
	}

	jx_cc_ast_stmt_asm_t* node = (jx_cc_ast_stmt_asm_t*)jcc_astAllocStmt(ctx, JCC_NODE_STMT_ASM, tok, sizeof(jx_cc_ast_stmt_asm_t));
	if (!node) {
		return NULL;
	}

	node->m_AsmCodeStr = asmCodeStr;
	node->m_Specifiers = specs;

	return &node->super;
}

// expr can be NULL
static jx_cc_ast_stmt_t* jcc_astAllocStmtReturn(jx_cc_context_t* ctx, jx_cc_ast_expr_t* expr, jx_cc_token_t* tok)
{
	jx_cc_ast_stmt_expr_t* node = (jx_cc_ast_stmt_expr_t*)jcc_astAllocStmt(ctx, JCC_NODE_STMT_RETURN, tok, sizeof(jx_cc_ast_stmt_expr_t));
	if (!node) {
		return NULL;
	}

	node->m_Expr = expr;

	return &node->super;
}

// else statement can be NULL
static jx_cc_ast_stmt_t* jcc_astAllocStmtIf(jx_cc_context_t* ctx, jx_cc_ast_expr_t* condExpr, jx_cc_ast_stmt_t* thenStmt, jx_cc_ast_stmt_t* elseStmt, jx_cc_label_t thenLbl, jx_cc_label_t elseLbl, jx_cc_label_t endLbl, jx_cc_token_t* tok)
{
	if (!condExpr || !thenStmt) {
		return NULL;
	}

	jx_cc_ast_stmt_if_t* node = (jx_cc_ast_stmt_if_t*)jcc_astAllocStmt(ctx, JCC_NODE_STMT_IF, tok, sizeof(jx_cc_ast_stmt_if_t));
	if (!node) {
		return NULL;
	}

	node->m_CondExpr = condExpr;
	node->m_ThenStmt = thenStmt;
	node->m_ElseStmt = elseStmt;
	node->m_ThenLbl = thenLbl;
	node->m_ElseLbl = elseLbl;
	node->m_EndLbl = endLbl;

	return &node->super;
}

static jx_cc_ast_stmt_t* jcc_astAllocStmtBlock(jx_cc_context_t* ctx, jx_cc_ast_node_t** children, uint32_t numChildren, jx_cc_token_t* tok)
{
	jx_cc_ast_stmt_block_t* block = (jx_cc_ast_stmt_block_t*)jcc_astAllocStmt(ctx, JCC_NODE_STMT_BLOCK, tok, sizeof(jx_cc_ast_stmt_block_t));
	if (!block) {
		return NULL;
	}

	if (children != NULL && numChildren != 0) {
		block->m_Children = (jx_cc_ast_stmt_t**)JX_ALLOC(ctx->m_LinearAllocator, sizeof(jx_cc_ast_node_t*) * numChildren);
		if (!block->m_Children) {
			return NULL;
		}

		jx_memcpy(block->m_Children, children, sizeof(jx_cc_ast_node_t*) * numChildren);
	}
	block->m_NumChildren = numChildren;

	return &block->super;
}

static jx_cc_ast_stmt_t* jcc_astAllocStmtSwitch(jx_cc_context_t* ctx, jx_cc_ast_expr_t* condExpr, jx_cc_label_t brkLbl, jx_cc_token_t* tok)
{
	if (!condExpr) {
		return NULL;
	}

	jx_cc_ast_stmt_switch_t* node = (jx_cc_ast_stmt_switch_t*)jcc_astAllocStmt(ctx, JCC_NODE_STMT_SWITCH, tok, sizeof(jx_cc_ast_stmt_switch_t));
	if (!node) {
		return NULL;
	}

	node->m_CondExpr = condExpr;
	node->m_BreakLbl = brkLbl;
	
	return &node->super;
}

static jx_cc_ast_stmt_t* jcc_astAllocStmtCase(jx_cc_context_t* ctx, jx_cc_label_t lbl, int64_t begin, int64_t end, jx_cc_ast_stmt_t* body, jx_cc_token_t* tok)
{
	if (!body) {
		return NULL;
	}

	jx_cc_ast_stmt_case_t* caseNode = (jx_cc_ast_stmt_case_t*)jcc_astAllocStmt(ctx, JCC_NODE_STMT_CASE, tok, sizeof(jx_cc_ast_stmt_case_t));
	if (!caseNode) {
		return NULL;
	}

	caseNode->m_BodyStmt = body;
	caseNode->m_Lbl = lbl;
	caseNode->m_Range[0] = begin;
	caseNode->m_Range[1] = end;

	return &caseNode->super;
}

static jx_cc_ast_stmt_t* jcc_astAllocStmtFor(jx_cc_context_t* ctx, jx_cc_label_t brkLbl, jx_cc_label_t continueLbl, jx_cc_label_t bodyLbl, jx_cc_ast_stmt_t* initStmt, jx_cc_ast_expr_t* condExpr, jx_cc_ast_expr_t* incExpr, jx_cc_ast_stmt_t* bodyStmt, jx_cc_token_t* tok)
{
	if (!bodyStmt) {
		return NULL;
	}

	jx_cc_ast_stmt_for_t* node = (jx_cc_ast_stmt_for_t*)jcc_astAllocStmt(ctx, JCC_NODE_STMT_FOR, tok, sizeof(jx_cc_ast_stmt_for_t));
	if (!node) {
		return NULL;
	}

	node->m_BreakLbl = brkLbl;
	node->m_ContinueLbl = continueLbl;
	node->m_BodyLbl = bodyLbl;
	node->m_InitStmt = initStmt;
	node->m_CondExpr = condExpr;
	node->m_IncExpr = incExpr;
	node->m_BodyStmt = bodyStmt;

	return &node->super;
}

static jx_cc_ast_stmt_t* jcc_astAllocStmtDo(jx_cc_context_t* ctx, jx_cc_label_t brkLbl, jx_cc_label_t continueLbl, jx_cc_label_t bodyLbl, jx_cc_ast_expr_t* condExpr, jx_cc_ast_stmt_t* bodyStmt, jx_cc_token_t* tok)
{
	if (!bodyStmt || !condExpr) {
		return NULL;
	}

	jx_cc_ast_stmt_do_t* node = (jx_cc_ast_stmt_do_t*)jcc_astAllocStmt(ctx, JCC_NODE_STMT_DO, tok, sizeof(jx_cc_ast_stmt_do_t));
	if (!node) {
		return NULL;
	}

	node->m_BreakLbl = brkLbl;
	node->m_ContinueLbl = continueLbl;
	node->m_BodyLbl = bodyLbl;
	node->m_CondExpr = condExpr;
	node->m_BodyStmt = bodyStmt;

	return &node->super;
}

static jx_cc_ast_stmt_t* jcc_astAllocStmtGoto(jx_cc_context_t* ctx, const char* lbl, jx_cc_token_t* tok)
{
	if (!lbl) {
		return NULL;
	}

	jx_cc_ast_stmt_goto_t* node = (jx_cc_ast_stmt_goto_t*)jcc_astAllocStmt(ctx, JCC_NODE_STMT_GOTO, tok, sizeof(jx_cc_ast_stmt_goto_t));
	if (!node) {
		return NULL;
	}

	node->m_Label = jx_strtable_insert(ctx->m_StringTable, lbl, UINT32_MAX);

	return &node->super;
}

static jx_cc_ast_stmt_t* jcc_astAllocStmtBreakContinue(jx_cc_context_t* ctx, jx_cc_label_t lbl, jx_cc_token_t* tok)
{
	jx_cc_ast_stmt_goto_t* node = (jx_cc_ast_stmt_goto_t*)jcc_astAllocStmt(ctx, JCC_NODE_STMT_GOTO, tok, sizeof(jx_cc_ast_stmt_goto_t));
	if (!node) {
		return NULL;
	}

	node->m_UniqueLabel = lbl;

	return &node->super;
}

static jx_cc_ast_stmt_t* jcc_astAllocStmtLabel(jx_cc_context_t* ctx, const char* lbl, jx_cc_label_t uniqueLbl, jx_cc_ast_stmt_t* stmt, jx_cc_token_t* tok)
{
	if (!lbl || !stmt) {
		return NULL;
	}

	jx_cc_ast_stmt_label_t* node = (jx_cc_ast_stmt_label_t*)jcc_astAllocStmt(ctx, JCC_NODE_STMT_LABEL, tok, sizeof(jx_cc_ast_stmt_label_t));
	if (!node) {
		return NULL;
	}

	node->m_Label = jx_strtable_insert(ctx->m_StringTable, lbl, UINT32_MAX);
	node->m_UniqueLabel = uniqueLbl;
	node->m_Stmt = stmt;
	
	return &node->super;
}

static jx_cc_ast_stmt_t* jcc_astAllocStmtExpr(jx_cc_context_t* ctx, jx_cc_ast_expr_t* expr, jx_cc_token_t* tok)
{
	if (!expr) {
		return NULL;
	}

	jx_cc_ast_stmt_expr_t* exprStmt = (jx_cc_ast_stmt_expr_t*)jcc_astAllocStmt(ctx, JCC_NODE_STMT_EXPR, tok, sizeof(jx_cc_ast_stmt_expr_t));
	if (!exprStmt) {
		return NULL;
	}

	exprStmt->m_Expr = expr;

	return &exprStmt->super;
}

static bool jcc_astNodeIsExprBinary(jx_cc_ast_expr_t* expr)
{
	jx_cc_ast_node_kind kind = expr->super.m_Kind;
	const bool isBinary = false
		|| kind == JCC_NODE_EXPR_ADD
		|| kind == JCC_NODE_EXPR_SUB
		|| kind == JCC_NODE_EXPR_MUL
		|| kind == JCC_NODE_EXPR_DIV
		|| kind == JCC_NODE_EXPR_MOD
		|| kind == JCC_NODE_EXPR_BITWISE_AND
		|| kind == JCC_NODE_EXPR_BITWISE_OR
		|| kind == JCC_NODE_EXPR_BITWISE_XOR
		|| kind == JCC_NODE_EXPR_LSHIFT
		|| kind == JCC_NODE_EXPR_RSHIFT
		|| kind == JCC_NODE_EXPR_EQUAL
		|| kind == JCC_NODE_EXPR_NOT_EQUAL
		|| kind == JCC_NODE_EXPR_LESS_THAN
		|| kind == JCC_NODE_EXPR_LESS_EQUAL
		|| kind == JCC_NODE_EXPR_ASSIGN
		|| kind == JCC_NODE_EXPR_COMMA
		|| kind == JCC_NODE_EXPR_LOGICAL_AND
		|| kind == JCC_NODE_EXPR_LOGICAL_OR
		;
	return isBinary;
}

static jcc_var_scope_t* jcc_tuPushVarScope(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, const char* name)
{
	jcc_var_scope_t* sc = (jcc_var_scope_t*)JX_ALLOC(ctx->m_LinearAllocator, sizeof(jcc_var_scope_t));
	if (!sc) {
		return NULL;
	}

	jx_memset(sc, 0, sizeof(jcc_var_scope_t));

	jx_hashmapSet(tu->m_Scope->m_Vars, &(jcc_scope_entry_t){ .m_Key = name, .m_KeyLen = jx_strlen(name), .m_Value = sc});
	
	return sc;
}

static jcc_initializer_t* jcc_allocInitializer(jx_cc_context_t* ctx, jx_cc_type_t* ty, bool is_flexible)
{
	if (!ty) {
		return NULL;
	}

	jcc_initializer_t* init = (jcc_initializer_t*)JX_ALLOC(ctx->m_LinearAllocator, sizeof(jcc_initializer_t));
	if (!init) {
		return NULL;
	}

	jx_memset(init, 0, sizeof(jcc_initializer_t));

	init->m_Type = ty;
	
	if (ty->m_Kind == JCC_TYPE_ARRAY) {
		if (is_flexible && ty->m_Size < 0) {
			init->m_IsFlexible = true;
		} else {
			init->m_Children = (jcc_initializer_t**)JX_ALLOC(ctx->m_LinearAllocator, sizeof(jcc_initializer_t*) * ty->m_ArrayLen);
			if (!init->m_Children) {
				return NULL;
			}

			jx_memset(init->m_Children, 0, sizeof(jcc_initializer_t*) * ty->m_ArrayLen);

			for (int i = 0; i < ty->m_ArrayLen; i++) {
				jcc_initializer_t* childInit = jcc_allocInitializer(ctx, ty->m_BaseType, false);
				if (!childInit) {
					return NULL;
				}

				init->m_Children[i] = childInit;
			}
		}
	} else if (ty->m_Kind == JCC_TYPE_STRUCT || ty->m_Kind == JCC_TYPE_UNION) {
		// Count the number of struct members.
		uint32_t numMembers = 0;
		for (jx_cc_struct_member_t* mem = ty->m_StructMembers; mem; mem = mem->m_Next) {
			++numMembers;
		}
		
		init->m_Children = (jcc_initializer_t**)JX_ALLOC(ctx->m_LinearAllocator, sizeof(jcc_initializer_t*) * numMembers);
		if (!init->m_Children) {
			return NULL;
		}

		jx_memset(init->m_Children, 0, sizeof(jcc_initializer_t*) * numMembers);
		
		for (jx_cc_struct_member_t* mem = ty->m_StructMembers; mem; mem = mem->m_Next) {
			if (is_flexible && (ty->m_Flags & JCC_TYPE_FLAGS_IS_FLEXIBLE_Msk) != 0 && !mem->m_Next) {
				jcc_initializer_t* child = (jcc_initializer_t*)JX_ALLOC(ctx->m_LinearAllocator, sizeof(jcc_initializer_t));
				if (!child) {
					return NULL;
				}

				jx_memset(child, 0, sizeof(jcc_initializer_t));
				child->m_Type = mem->m_Type;
				child->m_IsFlexible = true;
				
				init->m_Children[mem->m_ID] = child;
			} else {
				jcc_initializer_t* childInit = jcc_allocInitializer(ctx, mem->m_Type, false);
				if (!childInit) {
					return NULL;
				}

				init->m_Children[mem->m_ID] = childInit;
			}
		}
	}

	return init;
}

static jx_cc_label_t jcc_tuGenerateUniqueLabel(jx_cc_context_t* ctx, jcc_translation_unit_t* tu)
{
	JX_UNUSED(ctx);
	return (jx_cc_label_t) { .m_ID = ++tu->m_NextLabelID };
}

static jx_cc_object_t* jcc_tuVarAlloc(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, const char* name, jx_cc_type_t* ty)
{
	if (!name || !ty) {
		return NULL;
	}

	jx_cc_object_t* var = (jx_cc_object_t*)JX_ALLOC(ctx->m_LinearAllocator, sizeof(jx_cc_object_t));
	if (!var) {
		return NULL;
	}

	jx_memset(var, 0, sizeof(jx_cc_object_t));

	var->m_Name = jx_strtable_insert(ctx->m_StringTable, name, UINT32_MAX);
	var->m_Type = ty;
	var->m_Alignment = ty->m_Alignment;
	
	jcc_var_scope_t* scope = jcc_tuPushVarScope(ctx, tu, var->m_Name);
	if (!scope) {
		return NULL;
	}
	scope->m_Var = var;

	return var;
}

static jx_cc_object_t* jcc_tuVarAllocLocal(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, const char* name, jx_cc_type_t* ty)
{
	if (!name || !ty) {
		return NULL;
	}

	jx_cc_object_t* var = jcc_tuVarAlloc(ctx, tu, name, ty);
	if (!var) {
		return NULL;
	}

	var->m_Flags |= JCC_OBJECT_FLAGS_IS_LOCAL_Msk;
	
	if (!tu->m_LocalsHead) {
		JX_CHECK(!tu->m_LocalsTail, "Locals linked list in invalid state");
		tu->m_LocalsHead = var;
		tu->m_LocalsTail = var;
	} else {
		JX_CHECK(tu->m_LocalsTail, "Globals linked list in invalid state");
		tu->m_LocalsTail->m_Next = var;
		tu->m_LocalsTail = var;
	}
	
	return var;
}

static jx_cc_object_t* jcc_tuVarAllocAnonLocal(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_type_t* ty)
{
	char name[256];
	jx_snprintf(name, JX_COUNTOF(name), "$lvar_%u", ++tu->m_NextLocalVarID);
	return jcc_tuVarAllocLocal(ctx, tu, name, ty);
}

static jx_cc_object_t* jcc_tuVarAllocGlobal(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, const char* name, jx_cc_type_t* ty)
{
	if (!name || !ty) {
		return NULL;
	}

	jx_cc_object_t* var = jcc_tuVarAlloc(ctx, tu, name, ty);
	if (!var) {
		return NULL;
	}

	var->m_Flags |= (JCC_OBJECT_FLAGS_IS_STATIC_Msk | JCC_OBJECT_FLAGS_IS_DEFINITION_Msk);
	
	if (!tu->m_GlobalsHead) {
		JX_CHECK(!tu->m_GlobalsTail, "Globals linked list in invalid state");
		tu->m_GlobalsHead = var;
		tu->m_GlobalsTail = var;
	} else {
		JX_CHECK(tu->m_GlobalsTail, "Globals linked list in invalid state");
		tu->m_GlobalsTail->m_Next = var;
		tu->m_GlobalsTail = var;
	}
	
	return var;
}

static jx_cc_object_t* jcc_tuVarAllocAnonGlobal(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_type_t* ty)
{
	char name[256];
	jx_snprintf(name, JX_COUNTOF(name), "$gvar_%u", ++tu->m_NextGlobalVarID);
	return jcc_tuVarAllocGlobal(ctx, tu, name, ty);
}

static jx_cc_object_t* jcc_tuVarAllocStringLiteral(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, const char* p, jx_cc_type_t* ty)
{
	if (!p || !ty) {
		return NULL;
	}

	jx_cc_object_t* var = jcc_tuVarAllocAnonGlobal(ctx, tu, ty);
	if (!var) {
		return NULL;
	}

	var->m_GlobalInitData = p;
	
	return var;
}

// If tok is an identifier returns a copy of the identifier,
// otherwise it returns NULL.
static const char* jcc_tokGetIdentifier(jx_cc_context_t* ctx, jx_cc_token_t* tok)
{
	return jcc_tokIs(tok, JCC_TOKEN_IDENTIFIER)
		? tok->m_String
		: NULL
		;
}

// If tok is an identifier check if there is a type already
// defined with the same name and return it. Otherwise,
// NULL is returned.
static jx_cc_type_t* jcc_tuFindTypeDefinition(jcc_translation_unit_t* tu, jx_cc_token_t* tok)
{
	if (!jcc_tokIs(tok, JCC_TOKEN_IDENTIFIER)) {
		return NULL;
	}
	
	jcc_var_scope_t* sc = jcc_tuFindVariable(tu, tok);
	if (!sc) {
		return NULL;
	}
	
	return sc->m_Typedef;
}

// Add type ty into current scope with the name specified by the token tok.
static bool jcc_tuScopeAddTag(jcc_translation_unit_t* tu, jx_cc_token_t* tok, jx_cc_type_t* ty)
{
	jx_hashmapSet(tu->m_Scope->m_Tags, &(jcc_scope_entry_t){.m_Key = tok->m_String, .m_KeyLen = tok->m_Length, .m_Value = ty});
	return true; 
}

// declspec = ("void" | "_Bool" | "char" | "short" | "int" | "long"
//             | "typedef" | "static" | "extern" | "inline"
//             | "_Thread_local" | "__thread"
//             | "signed" | "unsigned"
//             | struct-decl | union-decl | typedef-name
//             | enum-specifier | typeof-specifier
//             | "const" | "volatile" | "auto" | "register" | "restrict"
//             | "__restrict" | "__restrict__" | "_Noreturn")+
//
// The order of typenames in a type-specifier doesn't matter. For
// example, `int long static` means the same as `static long int`.
// That can also be written as `static long` because you can omit
// `int` if `long` or `short` are specified. However, something like
// `char int` is not a valid type specifier. We have to accept only a
// limited combinations of the typenames.
//
// In this function, we count the number of occurrences of each typename
// while keeping the "current" type object that the typenames up
// until that point represent. When we reach a non-typename token,
// we returns the current type object.
static jx_cc_type_t* jcc_parseDeclarationSpecifiers(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_token_t** tokenListPtr, jcc_var_attr_t* attr)
{
	jx_cc_token_t* tok = *tokenListPtr;

	// We use a single integer as counters for all typenames.
	// For example, bits 0 and 1 represents how many times we saw the
	// keyword "void" so far. With this, we can use a switch statement
	// as you can see below.
	enum {
		VOID     = 1 << 0,
		BOOL     = 1 << 2,
		CHAR     = 1 << 4,
		SHORT    = 1 << 6,
		INT      = 1 << 8,
		LONG     = 1 << 10,
		FLOAT    = 1 << 12,
		DOUBLE   = 1 << 14,
		OTHER    = 1 << 16,
		SIGNED   = 1 << 17,
		UNSIGNED = 1 << 18,
	};
	
	jx_cc_type_t* ty = kType_int;
	int counter = 0;
	bool is_atomic = false;
	
	while (jcc_tuIsTypename(ctx, tu, tok)) {
		// Handle storage class specifiers.
		const bool isStorageClassSpec = false
			|| jcc_tokIs(tok, JCC_TOKEN_TYPEDEF)
			|| jcc_tokIs(tok, JCC_TOKEN_STATIC)
			|| jcc_tokIs(tok, JCC_TOKEN_EXTERN)
			|| jcc_tokIs(tok, JCC_TOKEN_INLINE)
			|| jcc_tokIs(tok, JCC_TOKEN_THREAD_LOCAL)
			;
		if (isStorageClassSpec) {
			if (!attr) {
				jcc_logError(ctx, &tok->m_Loc, "Storage class specifier is not allowed in this context");
				return NULL;
			}
			
			if (jcc_tokExpect(&tok, JCC_TOKEN_TYPEDEF)) {
				attr->m_Flags |= JCC_VAR_ATTR_IS_TYPEDEF_Msk;
			} else if (jcc_tokExpect(&tok, JCC_TOKEN_STATIC)) {
				attr->m_Flags |= JCC_VAR_ATTR_IS_STATIC_Msk;
			} else if (jcc_tokExpect(&tok, JCC_TOKEN_EXTERN)) {
				attr->m_Flags |= JCC_VAR_ATTR_IS_EXTERN_Msk;
			} else if (jcc_tokExpect(&tok, JCC_TOKEN_INLINE)) {
				attr->m_Flags |= JCC_VAR_ATTR_IS_INLINE_Msk;
			} else if (jcc_tokExpect(&tok, JCC_TOKEN_THREAD_LOCAL)) {
				attr->m_Flags |= JCC_VAR_ATTR_IS_TLS_Msk;
			} else {
				jcc_logError(ctx, JCC_SOURCE_LOCATION_CUR(), "Internal Error: Unreachable code path reached!");
				JX_CHECK(false, "Should not land here!");
				return NULL;
			}
			
			if ((attr->m_Flags & JCC_VAR_ATTR_IS_TYPEDEF_Msk) != 0 && (attr->m_Flags & (JCC_VAR_ATTR_IS_STATIC_Msk | JCC_VAR_ATTR_IS_EXTERN_Msk | JCC_VAR_ATTR_IS_INLINE_Msk | JCC_VAR_ATTR_IS_TLS_Msk)) != 0) {
				jcc_logError(ctx, &tok->m_Loc, "typedef may not be used together with static, extern, inline, __thread or _Thread_local");
				return NULL;
			}
			
			continue;
		}

		// These keywords are recognized but ignored.
		const bool ignoreKeyword = false
			|| jcc_tokExpect(&tok, JCC_TOKEN_CONST)
			|| jcc_tokExpect(&tok, JCC_TOKEN_VOLATILE)
			|| jcc_tokExpect(&tok, JCC_TOKEN_AUTO)
			|| jcc_tokExpect(&tok, JCC_TOKEN_REGISTER)
			|| jcc_tokExpect(&tok, JCC_TOKEN_RESTRICT)
			|| jcc_tokExpect(&tok, JCC_TOKEN_NORETURN)
			;
		if (ignoreKeyword) {
			continue;
		}

		if (jcc_tokExpect(&tok, JCC_TOKEN_ATOMIC)) {
			if (jcc_tokExpect(&tok , JCC_TOKEN_OPEN_PAREN)) {
				ty = jcc_parseTypename(ctx, tu, &tok);
				if (!ty) {
					jcc_logError(ctx, &tok->m_Loc, "Expected typename after '('");
					return NULL;
				}

				if (!jcc_tokExpect(&tok, JCC_TOKEN_CLOSE_PAREN)) {
					jcc_logError(ctx, &tok->m_Loc, "Expected ')' after typename");
					return NULL;
				}
			}
		
			is_atomic = true;
			continue;
		}
		
		if (jcc_tokExpect(&tok, JCC_TOKEN_ALIGNAS)) {
			if (!attr) {
				jcc_logError(ctx, &tok->m_Loc, "_Alignas is not allowed in this context");
				return NULL;
			}
			
			if (!jcc_tokExpect(&tok, JCC_TOKEN_OPEN_PAREN)) {
				jcc_logError(ctx, &tok->m_Loc, "Expected '(' after _Alignas");
				return NULL;
			}
			
			if (jcc_tuIsTypename(ctx, tu, tok)) {
				jx_cc_type_t* alignType = jcc_parseTypename(ctx, tu, &tok);
				if (!alignType) {
					jcc_logError(ctx, &tok->m_Loc, "Expected typename after '('");
					return NULL;
				}

				attr->m_Align = alignType->m_Alignment;
			} else {
				bool err = false;
				attr->m_Align = (uint32_t)jcc_parseConstExpression(ctx, tu, &tok, &err);
				if (err) {
					jcc_logError(ctx, &tok->m_Loc, "Expected constant expression after '('");
					return NULL;
				}
			}
		
			if (!jcc_tokExpect(&tok, JCC_TOKEN_CLOSE_PAREN)) {
				jcc_logError(ctx, &tok->m_Loc, "Expected ')'");
				return NULL;
			}

			continue;
		}
		
		// Handle user-defined types.
		jx_cc_type_t* ty2 = jcc_tuFindTypeDefinition(tu, tok);
		if (jcc_tokIs(tok, JCC_TOKEN_STRUCT) || jcc_tokIs(tok, JCC_TOKEN_UNION) || jcc_tokIs(tok, JCC_TOKEN_ENUM) || jcc_tokIs(tok, JCC_TOKEN_TYPEOF) || ty2) {
			if (counter) {
				break;
			}
			
			if (jcc_tokExpect(&tok, JCC_TOKEN_STRUCT)) {
				ty = jcc_parseStructDeclaration(ctx, tu, &tok);
			} else if (jcc_tokExpect(&tok, JCC_TOKEN_UNION)) {
				ty = jcc_parseUnionDeclaration(ctx, tu, &tok);
			} else if (jcc_tokExpect(&tok, JCC_TOKEN_ENUM)) {
				ty = jcc_parseEnum(ctx, tu, &tok);
			} else if (jcc_tokExpect(&tok, JCC_TOKEN_TYPEOF)) {
				ty = jcc_parseTypeof(ctx, tu, &tok);
			} else {
				ty = ty2;
				tok = tok->m_Next;
			}
			
			counter += OTHER;
			continue;
		}
		
		// Handle built-in types.
		if (jcc_tokExpect(&tok, JCC_TOKEN_VOID)) {
			counter += VOID;
		} else if (jcc_tokExpect(&tok, JCC_TOKEN_BOOL)) {
			counter += BOOL;
		} else if (jcc_tokExpect(&tok, JCC_TOKEN_CHAR)) {
			counter += CHAR;
		} else if (jcc_tokExpect(&tok, JCC_TOKEN_SHORT)) {
			counter += SHORT;
		} else if (jcc_tokExpect(&tok, JCC_TOKEN_INT)) {
			counter += INT;
		} else if (jcc_tokExpect(&tok, JCC_TOKEN_LONG)) {
			counter += LONG;
		} else if (jcc_tokExpect(&tok, JCC_TOKEN_FLOAT)) {
			counter += FLOAT;
		} else if (jcc_tokExpect(&tok, JCC_TOKEN_DOUBLE)) {
			counter += DOUBLE;
		} else if (jcc_tokExpect(&tok, JCC_TOKEN_SIGNED)) {
			counter |= SIGNED;
		} else if (jcc_tokExpect(&tok, JCC_TOKEN_UNSIGNED)) {
			counter |= UNSIGNED;
		} else {
			jcc_logError(ctx, &tok->m_Loc, "Unexpected token");
			return NULL;
		}
		
		switch (counter) {
		case VOID:
			ty = kType_void;
			break;
		case BOOL:
			ty = kType_bool;
			break;
		case CHAR:
		case SIGNED + CHAR:
			ty = kType_char;
			break;
		case UNSIGNED + CHAR:
			ty = kType_uchar;
			break;
		case SHORT:
		case SHORT + INT:
		case SIGNED + SHORT:
		case SIGNED + SHORT + INT:
			ty = kType_short;
			break;
		case UNSIGNED + SHORT:
		case UNSIGNED + SHORT + INT:
			ty = kType_ushort;
			break;
		case INT:
		case SIGNED:
		case SIGNED + INT:
			ty = kType_int;
			break;
		case UNSIGNED:
		case UNSIGNED + INT:
			ty = kType_uint;
			break;
		case LONG:
		case LONG + INT:
		case LONG + LONG:
		case LONG + LONG + INT:
		case SIGNED + LONG:
		case SIGNED + LONG + INT:
		case SIGNED + LONG + LONG:
		case SIGNED + LONG + LONG + INT:
			ty = kType_long;
			break;
		case UNSIGNED + LONG:
		case UNSIGNED + LONG + INT:
		case UNSIGNED + LONG + LONG:
		case UNSIGNED + LONG + LONG + INT:
			ty = kType_ulong;
			break;
		case FLOAT:
			ty = kType_float;
			break;
		case DOUBLE:
		case LONG + DOUBLE:
			ty = kType_double;
			break;
		default:
			jcc_logError(ctx, &tok->m_Loc, "Invalid type");
			return NULL;
		}
	}
	
	// TODO: jcc_typeAllocAtomic
	if (is_atomic) {
		ty = jcc_typeCopy(ctx, ty);
		if (!ty) {
			jcc_logError(ctx, JCC_SOURCE_LOCATION_CUR(), "Internal Error: Memory allocation failed.");
			return NULL;
		}

		ty->m_Flags |= JCC_TYPE_FLAGS_IS_ATOMIC_Msk;
	}
	
	*tokenListPtr = tok;

	return ty;
}

// func-params = ("void" | param ("," param)* ("," "...")?)? ")"
// param       = declspec declarator
static jx_cc_type_t* jcc_parseFunctionParameters(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_token_t** tokenListPtr, jx_cc_type_t* ty)
{
	jx_cc_token_t* tok = *tokenListPtr;

	if (jcc_tokIs(tok, JCC_TOKEN_VOID) && jcc_tokIs(tok->m_Next, JCC_TOKEN_CLOSE_PAREN)) {
		tok = tok->m_Next->m_Next;
		ty = jcc_typeAllocFunc(ctx, ty);
		if (!ty) {
			return NULL;
		}
	} else {
		jx_cc_type_t head = { 0 };
		jx_cc_type_t* cur = &head;
		bool is_variadic = false;

		while (!jcc_tokExpect(&tok, JCC_TOKEN_CLOSE_PAREN)) {
			if (cur != &head) {
				if (!jcc_tokExpect(&tok, JCC_TOKEN_COMMA)) {
					jcc_logError(ctx, &tok->m_Loc, "Expected ',' after parameter declaration");
					return NULL;
				}
			}

			if (jcc_tokExpect(&tok, JCC_TOKEN_ELLIPSIS)) {
				is_variadic = true;

				if (!jcc_tokExpect(&tok, JCC_TOKEN_CLOSE_PAREN)) {
					jcc_logError(ctx, &tok->m_Loc, "Expected ')' after '...'");
					return NULL;
				}

				break;
			}

			jx_cc_type_t* paramType = jcc_parseDeclarationSpecifiers(ctx, tu, &tok, NULL);
			if (!paramType) {
				jcc_logError(ctx, &tok->m_Loc, "Expected type");
				return NULL;
			}

			jx_cc_type_t* ty2 = jcc_typeCopy(ctx, paramType);
			ty2 = jcc_parseDeclarator(ctx, tu, &tok, ty2);
			if (!ty2) {
				jcc_logError(ctx, &tok->m_Loc, "Expected declarator after type");
				return NULL;
			}

			jx_cc_token_t* name = ty2->m_DeclName;

			if (ty2->m_Kind == JCC_TYPE_ARRAY) {
				// "array of T" is converted to "pointer to T" only in the parameter
				// context. For example, *argv[] is converted to **argv by this.
				ty2 = jcc_typeAllocPointerTo(ctx, ty2->m_BaseType);
				if (!ty2) {
					jcc_logError(ctx, JCC_SOURCE_LOCATION_CUR(), "Internal Error: Memory allocation failed.");
					return NULL;
				}

				ty2->m_DeclName = name;
			} else if (ty2->m_Kind == JCC_TYPE_FUNC) {
				// Likewise, a function is converted to a pointer to a function
				// only in the parameter context.
				ty2 = jcc_typeAllocPointerTo(ctx, ty2);
				if (!ty2) {
					jcc_logError(ctx, JCC_SOURCE_LOCATION_CUR(), "Internal Error: Memory allocation failed.");
					return NULL;
				}

				ty2->m_DeclName = name;
			}

			cur->m_Next = ty2;
			cur = cur->m_Next;
		}

		if (cur == &head) {
			is_variadic = true;
		}

		ty = jcc_typeAllocFunc(ctx, ty);
		if (!ty) {
			jcc_logError(ctx, JCC_SOURCE_LOCATION_CUR(), "Internal Error: Memory allocation failed.");
			return NULL;
		}

		ty->m_FuncParams = head.m_Next;
		ty->m_Flags |= is_variadic ? JCC_TYPE_FLAGS_IS_VARIADIC_Msk : 0;
	}

	*tokenListPtr = tok;

	return ty;
}

// array-dimensions = ("static" | "restrict")* const-expr? "]" type-suffix
static jx_cc_type_t* jcc_parseArrayDimensions(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_token_t** tokenListPtr, jx_cc_type_t* ty)
{
	jx_cc_token_t* tok = *tokenListPtr;

	while (jcc_tokIs(tok, JCC_TOKEN_STATIC) || jcc_tokIs(tok, JCC_TOKEN_RESTRICT)) {
		tok = tok->m_Next;
	}
	
	if (jcc_tokExpect(&tok, JCC_TOKEN_CLOSE_BRACKET)) {
		ty = jcc_parseTypeSuffix(ctx, tu, &tok, ty);
		if (!ty) {
			jcc_logError(ctx, &tok->m_Loc, "Expected type suffix");
			return NULL;
		}

		ty = jcc_typeAllocArrayOf(ctx, ty, -1);
		if (!ty) {
			return NULL;
		}
	} else {
		jx_cc_ast_expr_t* expr = jcc_parseConditional(ctx, tu, &tok);
		if (!expr) {
			jcc_logError(ctx, &tok->m_Loc, "Expected expression");
			return NULL;
		}

		if (!jcc_tokExpect(&tok, JCC_TOKEN_CLOSE_BRACKET)) {
			jcc_logError(ctx, &tok->m_Loc, "Expected ']' after expression");
			return NULL;
		}

		ty = jcc_parseTypeSuffix(ctx, tu, &tok, ty);
		if (!ty) {
			jcc_logError(ctx, &tok->m_Loc, "Expected type suffix");
			return NULL;
		}

		if (!jcc_astIsConstExpression(ctx, tu, expr)) {
			jcc_logError(ctx, &expr->super.m_Token->m_Loc, "Expected constant expression. Variable-length arrays are not supported.");
		} else {
			bool err = false;
			const int64_t arrayLen = jcc_astEvalConstExpression(ctx, tu, expr, &err);
			if (err) {
				jcc_logError(ctx, &tok->m_Loc, "Failed to evaluate constant expression");
				return NULL;
			}

			ty = jcc_typeAllocArrayOf(ctx, ty, (int)arrayLen);
			if (!ty) {
				return NULL;
			}
		}
	}

	*tokenListPtr = tok;

	return ty;
}

// type-suffix = "(" func-params
//             | "[" array-dimensions
//             | ε
static jx_cc_type_t* jcc_parseTypeSuffix(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_token_t** tokenListPtr, jx_cc_type_t* ty)
{
	jx_cc_token_t* tok = *tokenListPtr;

	if (jcc_tokExpect(&tok, JCC_TOKEN_OPEN_PAREN)) {
		ty = jcc_parseFunctionParameters(ctx, tu, &tok, ty);
		if (!ty) {
			jcc_logError(ctx, &tok->m_Loc, "Failed to parse function parameter list");
			return NULL;
		}
	} else if (jcc_tokExpect(&tok, JCC_TOKEN_OPEN_BRACKET)) {
		ty = jcc_parseArrayDimensions(ctx, tu, &tok, ty);
		if (!ty) {
			jcc_logError(ctx, &tok->m_Loc, "Failed to parse array dimensions");
			return NULL;
		}
	}

	*tokenListPtr = tok;

	return ty;
}

// pointers = ("*" ("const" | "volatile" | "restrict")*)*
static jx_cc_type_t* jcc_parsePointers(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_token_t** tokenListPtr, jx_cc_type_t* ty)
{
	jx_cc_token_t* tok = *tokenListPtr;

	while (jcc_tokExpect(&tok, JCC_TOKEN_MUL)) {
		ty = jcc_typeAllocPointerTo(ctx, ty);
		if (!ty) {
			jcc_logError(ctx, JCC_SOURCE_LOCATION_CUR(), "Internal Error: Memory allocation failed.");
			return NULL;
		}
		
		while (jcc_tokIs(tok, JCC_TOKEN_CONST) || jcc_tokIs(tok, JCC_TOKEN_VOLATILE) || jcc_tokIs(tok, JCC_TOKEN_RESTRICT)) {
			tok = tok->m_Next;
		}
	}
	
	*tokenListPtr = tok;

	return ty;
}

// declarator = pointers ("(" ident ")" | "(" declarator ")" | ident) type-suffix
static jx_cc_type_t* jcc_parseDeclarator(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_token_t** tokenListPtr, jx_cc_type_t* ty)
{
	jx_cc_token_t* tok = *tokenListPtr;

	ty = jcc_parsePointers(ctx, tu, &tok, ty);
	if (!ty) {
		jcc_logError(ctx, &tok->m_Loc, "Failed to parse pointers");
		return NULL;
	}
	
	if (jcc_tokExpect(&tok, JCC_TOKEN_OPEN_PAREN)) {
		jx_cc_token_t* start = tok;

		jx_cc_type_t dummy = { 0 };
		jcc_parseDeclarator(ctx, tu, &tok, &dummy);

		if (!jcc_tokExpect(&tok, JCC_TOKEN_CLOSE_PAREN)) {
			jcc_logError(ctx, &tok->m_Loc, "Expected ')' after declarator");
			return NULL;
		}

		ty = jcc_parseTypeSuffix(ctx, tu, &tok, ty);
		if (!ty) {
			jcc_logError(ctx, &tok->m_Loc, "Expected type suffix after ')'");
			return NULL;
		}

		ty = jcc_parseDeclarator(ctx, tu, &start, ty);
		if (!ty) {
			jcc_logError(ctx, &tok->m_Loc, "Expected declarator after type suffix");
			return NULL;
		}
	} else {
		jx_cc_token_t* name = NULL;
		jx_cc_token_t* name_pos = tok;

		if (jcc_tokIs(tok, JCC_TOKEN_IDENTIFIER)) {
			name = tok;
			tok = tok->m_Next;
		}

		ty = jcc_parseTypeSuffix(ctx, tu, &tok, ty);
		if (!ty) {
			jcc_logError(ctx, &tok->m_Loc, "Expected type suffix");
			return NULL;
		}

		ty->m_DeclName = name;
	}

	*tokenListPtr = tok;

	return ty;
}

// abstract-declarator = pointers ("(" abstract-declarator ")")? type-suffix
static jx_cc_type_t* jcc_parseAbstractDeclarator(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_token_t** tokenListPtr, jx_cc_type_t* ty)
{
	jx_cc_token_t* tok = *tokenListPtr;

	ty = jcc_parsePointers(ctx, tu, &tok, ty);
	if (!ty) {
		jcc_logError(ctx, &tok->m_Loc, "Failed to parse pointers");
		return NULL;
	}
	
	if (jcc_tokExpect(&tok, JCC_TOKEN_OPEN_PAREN)) {
		jx_cc_token_t* start = tok;

		jx_cc_type_t dummy = { 0 };
		jcc_parseAbstractDeclarator(ctx, tu, &tok, &dummy);

		if (!jcc_tokExpect(&tok, JCC_TOKEN_CLOSE_PAREN)) {
			jcc_logError(ctx, &tok->m_Loc, "Expected ')' after declarator");
			return NULL;
		}

		ty = jcc_parseTypeSuffix(ctx, tu, &tok, ty);
		if (!ty) {
			jcc_logError(ctx, &tok->m_Loc, "Expected type suffix");
			return NULL;
		}

		ty = jcc_parseAbstractDeclarator(ctx, tu, &start, ty);
		if (!ty) {
			jcc_logError(ctx, &tok->m_Loc, "Failed to parse abstract declarator");
			return NULL;
		}
	} else {
		ty = jcc_parseTypeSuffix(ctx, tu, &tok, ty);
		if (!ty) {
			jcc_logError(ctx, &tok->m_Loc, "Expected type suffix");
			return NULL;
		}
	}

	*tokenListPtr = tok;

	return ty;
}

// type-name = declspec abstract-declarator
static jx_cc_type_t* jcc_parseTypename(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_token_t** tokenListPtr)
{
	jx_cc_token_t* tok = *tokenListPtr;

	jx_cc_type_t* ty = jcc_parseDeclarationSpecifiers(ctx, tu, &tok, NULL);
	if (!ty) {
		jcc_logError(ctx, &tok->m_Loc, "Failed to parse declaration specifiers");
		return NULL;
	}

	ty = jcc_parseAbstractDeclarator(ctx, tu, &tok, ty);
	if (!ty) {
		jcc_logError(ctx, &tok->m_Loc, "Failed to parse abstract declarator");
		return NULL;
	}

	*tokenListPtr = tok;

	return ty;
}

static bool jcc_tokIsEndOfList(jx_cc_token_t* tok) 
{
	return false
		|| jcc_tokIs(tok, JCC_TOKEN_CLOSE_CURLY_BRACKET) 
		|| (jcc_tokIs(tok, JCC_TOKEN_COMMA) && jcc_tokIs(tok->m_Next, JCC_TOKEN_CLOSE_CURLY_BRACKET))
		;
}

static bool jcc_tokConsumeEndOfList(jx_cc_token_t** tokenListPtr) 
{
	jx_cc_token_t* tok = *tokenListPtr;

	if (jcc_tokIs(tok, JCC_TOKEN_CLOSE_CURLY_BRACKET)) {
		*tokenListPtr = tok->m_Next;
		return true;
	}
	
	if (jcc_tokIs(tok, JCC_TOKEN_COMMA) && jcc_tokIs(tok->m_Next, JCC_TOKEN_CLOSE_CURLY_BRACKET)) {
		*tokenListPtr = tok->m_Next->m_Next;
		return true;
	}
	
	return false;
}

// enum-specifier = ident? "{" enum-list? "}"
//                | ident ("{" enum-list? "}")?
//
// enum-list      = ident ("=" num)? ("," ident ("=" num)?)* ","?
static jx_cc_type_t* jcc_parseEnum(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_token_t** tokenListPtr)
{
	jx_cc_token_t* tok = *tokenListPtr;

	jx_cc_type_t* ty = jcc_typeAllocEnum(ctx);
	if (!ty) {
		jcc_logError(ctx, JCC_SOURCE_LOCATION_CUR(), "Internal Error: Memory allocation failed.");
		return NULL;
	}
	
	// Read a struct tag.
	jx_cc_token_t* tag = NULL;
	if (jcc_tokIs(tok, JCC_TOKEN_IDENTIFIER)) {
		tag = tok;
		tok = tok->m_Next;
	}
	
	if (tag && !jcc_tokIs(tok, JCC_TOKEN_OPEN_CURLY_BRACKET)) {
		jx_cc_type_t* ty = jcc_tuFindTag(tu, tag);
		if (!ty) {
			jcc_logError(ctx, &tok->m_Loc, "Unknown enum tag '%s'", tag->m_String);
			return NULL;
		}
		
		if (ty->m_Kind != JCC_TYPE_ENUM) {
			jcc_logError(ctx, &tok->m_Loc, "'%s' is not an enum tag", tag->m_String);
			return NULL;
		}
	} else {
		if (!jcc_tokExpect(&tok, JCC_TOKEN_OPEN_CURLY_BRACKET)) {
			jcc_logError(ctx, &tok->m_Loc, "Expected '{'");
			return NULL;
		}

		// Read an enum-list.
		bool first = true;
		int32_t val = 0;
		while (!jcc_tokConsumeEndOfList(&tok)) {
			if (!first) {
				if (!jcc_tokExpect(&tok, JCC_TOKEN_COMMA)) {
					jcc_logError(ctx, &tok->m_Loc, "Expected ','");
					return NULL;
				}
			}
			first = false;

			const char* name = jcc_tokGetIdentifier(ctx, tok);
			if (!name) {
				jcc_logError(ctx, &tok->m_Loc, "Expected identifier");
				return NULL;
			}

			tok = tok->m_Next;

			if (jcc_tokExpect(&tok, JCC_TOKEN_ASSIGN)) {
				bool err = false;
				val = (int32_t)jcc_parseConstExpression(ctx, tu, &tok, &err);
				if (err) {
					jcc_logError(ctx, &tok->m_Loc, "Failed to parse constant expression");
					return NULL;
				}
			}

			jcc_var_scope_t* sc = jcc_tuPushVarScope(ctx, tu, name);
			if (!sc) {
				jcc_logError(ctx, JCC_SOURCE_LOCATION_CUR(), "Internal Error: Failed to push variable to scope.");
				return NULL;
			}

			sc->m_Enum = ty;
			sc->m_EnumValue = val++;
		}
	}

	*tokenListPtr = tok;
	
	if (tag) {
		jcc_tuScopeAddTag(tu, tag, ty);
	}
	
	return ty;
}

// typeof-specifier = "(" (expr | typename) ")"
static jx_cc_type_t* jcc_parseTypeof(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_token_t** tokenListPtr)
{
	jx_cc_token_t* tok = *tokenListPtr;
	if (!jcc_tokExpect(&tok, JCC_TOKEN_OPEN_PAREN)) {
		jcc_logError(ctx, &tok->m_Loc, "Expected '('");
		return NULL;
	}
	
	jx_cc_type_t* ty = NULL;
	if (jcc_tuIsTypename(ctx, tu, tok)) {
		ty = jcc_parseTypename(ctx, tu, &tok);
		if (!ty) {
			jcc_logError(ctx, &tok->m_Loc, "Failed to parse typename");
			return NULL;
		}
	} else {
		jx_cc_ast_expr_t* node = jcc_parseExpression(ctx, tu, &tok);
		if (!node) {
			jcc_logError(ctx, &tok->m_Loc, "Expected expression");
			return NULL;
		}

		if (!jcc_astAddType(ctx, &node->super)) {
			return NULL;
		}

		ty = node->m_Type;
	}

	if (!jcc_tokExpect(&tok, JCC_TOKEN_CLOSE_PAREN)) {
		jcc_logError(ctx, &tok->m_Loc, "Expected ')'");
		return NULL;
	}

	*tokenListPtr = tok;

	return ty;
}

// declaration = declspec (declarator ("=" expr)? ("," declarator ("=" expr)?)*)? ";"
static jx_cc_ast_stmt_t* jcc_parseDeclaration(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_token_t** tokenListPtr, jx_cc_type_t* basety, jcc_var_attr_t* attr)
{
	jx_cc_token_t* tok = *tokenListPtr;

	jx_cc_ast_node_t** childArr = (jx_cc_ast_node_t**)jx_array_create(ctx->m_Allocator);
	if (!childArr) {
		return NULL;
	}

	bool first = true;
	while (!jcc_tokIs(tok, JCC_TOKEN_SEMICOLON)) {
		if (!first) {
			if (!jcc_tokExpect(&tok, JCC_TOKEN_COMMA)) {
				jcc_logError(ctx, &tok->m_Loc, "Expected ','");
				goto error;
			}
		}
		first = false;

		jx_cc_token_t* baseTypeName = basety->m_DeclName;

		jx_cc_type_t* ty = jcc_parseDeclarator(ctx, tu, &tok, basety);
		if (!ty) {
			jcc_logError(ctx, &tok->m_Loc, "Failed to parse declarator");
			goto error;
		}

		if (ty->m_Kind == JCC_TYPE_VOID) {
			jcc_logError(ctx, &tok->m_Loc, "Variable declared void");
			goto error;
		}
		
		if (!ty->m_DeclName) {
			jcc_logError(ctx, &tok->m_Loc, "Variable name omitted");
			goto error;
		}

		jx_cc_token_t* declName = ty->m_DeclName;
		ty->m_DeclName = baseTypeName;

		if (attr && (attr->m_Flags & JCC_VAR_ATTR_IS_STATIC_Msk) != 0) {
			// static local variable
			jx_cc_object_t* var = jcc_tuVarAllocAnonGlobal(ctx, tu, ty);
			if (!var) {
				goto error;
			}

			const char* name = jcc_tokGetIdentifier(ctx, declName);
			if (!name) {
				goto error;
			}

			jcc_var_scope_t* scope = jcc_tuPushVarScope(ctx, tu, name);
			if (!scope) {
				goto error;
			}

			scope->m_Var = var;

			if (jcc_tokExpect(&tok, JCC_TOKEN_ASSIGN)) {
				if (!jcc_parseGlobalVarInitializer(ctx, tu, &tok, var)) {
					jcc_logError(ctx, &tok->m_Loc, "Failed to parse initializer");
					goto error;
				}
			}
		} else {
			jx_cc_object_t* var = jcc_tuVarAllocLocal(ctx, tu, jcc_tokGetIdentifier(ctx, declName), ty);
			if (!var) {
				goto error;
			}

			if (attr && attr->m_Align) {
				var->m_Alignment = attr->m_Align;
			}

			if (jcc_tokExpect(&tok, JCC_TOKEN_ASSIGN)) {
				jx_cc_ast_expr_t* expr = jcc_parseLocalVarInitializer(ctx, tu, &tok, var);
				if (!expr) {
					jcc_logError(ctx, &tok->m_Loc, "Failed to parse variable initializer");
					goto error;
				}

				jx_cc_ast_stmt_t* child = jcc_astAllocStmtExpr(ctx, expr, tok);
				if (!child) {
					goto error;
				}
				
				jx_array_push_back(childArr, &child->super);
			}

			if (var->m_Type->m_Size < 0) {
				jcc_logError(ctx, &tok->m_Loc, "Variable has incomplete type");
				goto error;
			}

			if (var->m_Type->m_Kind == JCC_TYPE_VOID) {
				jcc_logError(ctx, &tok->m_Loc, "Variable declared void");
				goto error;
			}
		}
	}
	
	tok = tok->m_Next; // Skip semicolon

	jx_cc_ast_stmt_t* block = jcc_astAllocStmtBlock(ctx, childArr, jx_array_sizeu(childArr), *tokenListPtr);

	jx_array_free(childArr);

	*tokenListPtr = tok;

	return block;

error:
	jx_array_free(childArr);
	return NULL;
}

static jx_cc_token_t* jcc_skipExcessElement(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_token_t* tok)
{
	if (jcc_tokExpect(&tok, JCC_TOKEN_OPEN_CURLY_BRACKET)) {
		tok = jcc_skipExcessElement(ctx, tu, tok);
		if (!jcc_tokExpect(&tok, JCC_TOKEN_CLOSE_CURLY_BRACKET)) {
			jcc_logError(ctx, &tok->m_Loc, "Expected '}'");
			return NULL;
		}

		return tok;
	}
	
	jcc_parseAssignment(ctx, tu, &tok);

	return tok;
}

// string-initializer = string-literal
static bool jcc_parseStringInitializer(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_token_t** tokenListPtr, jcc_initializer_t* init)
{
	jx_cc_token_t* tok = *tokenListPtr;

	if (init->m_IsFlexible) {
		jx_cc_type_t* arrayType = jcc_typeAllocArrayOf(ctx, init->m_Type->m_BaseType, tok->m_Type->m_ArrayLen);
		if (!arrayType) {
			jcc_logError(ctx, JCC_SOURCE_LOCATION_CUR(), "Internal Error: Memory allocation failed.");
			return false;
		}

		jcc_initializer_t* arrayInit = jcc_allocInitializer(ctx, arrayType, false);
		if (!arrayInit) {
			jcc_logError(ctx, JCC_SOURCE_LOCATION_CUR(), "Internal Error: Memory allocation failed.");
			return false;
		}

		jx_memcpy(init, arrayInit, sizeof(jcc_initializer_t));
	}
	
	int len = jx_min_i32(init->m_Type->m_ArrayLen, tok->m_Type->m_ArrayLen);
	
	switch (init->m_Type->m_BaseType->m_Size) {
	case 1: {
		const char* str = tok->m_Val_string;
		for (int i = 0; i < len; i++) {
			jx_cc_ast_expr_t* constExpr = jcc_astAllocExprIConst(ctx, (int64_t)str[i], kType_int, tok);
			if (!constExpr) {
				jcc_logError(ctx, JCC_SOURCE_LOCATION_CUR(), "Internal Error: Memory allocation failed.");
				return false;
			}
		
			init->m_Children[i]->m_Expr = constExpr;
		}
	} break;
	case 2: {
		const uint16_t* str = (const uint16_t*)tok->m_Val_string;
		for (int i = 0; i < len; i++) {
			jx_cc_ast_expr_t* constExpr = jcc_astAllocExprIConst(ctx, (int64_t)str[i], kType_int, tok);
			if (!constExpr) {
				jcc_logError(ctx, JCC_SOURCE_LOCATION_CUR(), "Internal Error: Memory allocation failed.");
				return false;
			}

			init->m_Children[i]->m_Expr = constExpr;
		}
	} break;
	case 4: {
		const uint32_t* str = (const uint32_t*)tok->m_Val_string;
		for (int i = 0; i < len; i++) {
			jx_cc_ast_expr_t* constExpr = jcc_astAllocExprIConst(ctx, (int64_t)str[i], kType_int, tok);
			if (!constExpr) {
				jcc_logError(ctx, JCC_SOURCE_LOCATION_CUR(), "Internal Error: Memory allocation failed.");
				return false;
			}

			init->m_Children[i]->m_Expr = constExpr;
		}
	} break;
	default:
		jcc_logError(ctx, &tok->m_Loc, "Invalid initializer base type size");
		return false;
	}
	
	tok = tok->m_Next; // Skip ?

	*tokenListPtr = tok;

	return true;
}

// array-designator = "[" const-expr "]"
//
// C99 added the designated initializer to the language, which allows
// programmers to move the "cursor" of an initializer to any element.
// The syntax looks like this:
//
//   int x[10] = { 1, 2, [5]=3, 4, 5, 6, 7 };
//
// `[5]` moves the cursor to the 5th element, so the 5th element of x
// is set to 3. Initialization then continues forward in order, so
// 6th, 7th, 8th and 9th elements are initialized with 4, 5, 6 and 7,
// respectively. Unspecified elements (in this case, 3rd and 4th
// elements) are initialized with zero.
//
// Nesting is allowed, so the following initializer is valid:
//
//   int x[5][10] = { [5][8]=1, 2, 3 };
//
// It sets x[5][8], x[5][9] and x[6][0] to 1, 2 and 3, respectively.
//
// Use `.fieldname` to move the cursor for a struct initializer. E.g.
//
//   struct { int a, b, c; } x = { .c=5 };
//
// The above initializer sets x.c to 5.
static bool jcc_parseArrayDesignator(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_token_t** tokenListPtr, jx_cc_type_t* ty, int* begin, int* end)
{
	jx_cc_token_t* tok = *tokenListPtr;

	if (!jcc_tokExpect(&tok, JCC_TOKEN_OPEN_BRACKET)) {
		jcc_logError(ctx, &tok->m_Loc, "Expected '['");
		return false;
	}

	bool err = false;
	*begin = (int)jcc_parseConstExpression(ctx, tu, &tok, &err);
	if (err) {
		jcc_logError(ctx, &tok->m_Loc, "Failed to parse constant expression");
		return false;
	}
	if (*begin >= ty->m_ArrayLen) {
		jcc_logError(ctx, &tok->m_Loc, "Array designator index exceeds array bounds");
		return false;
	}
	
	if (jcc_tokExpect(&tok, JCC_TOKEN_ELLIPSIS)) {
		*end = (int)jcc_parseConstExpression(ctx, tu, &tok, &err);
		if (err) {
			jcc_logError(ctx, &tok->m_Loc, "Failed to parse constant expression");
			return false;
		}
		if (*end >= ty->m_ArrayLen) {
			jcc_logError(ctx, &tok->m_Loc, "Array designator index exceeds array bounds");
			return false;
		}
		
		if (*end < *begin) {
			jcc_logError(ctx, &tok->m_Loc, "Array designator range [%d, %d] is empty", *begin, *end);
			return false;
		}
	} else {
		*end = *begin;
	}

	if (!jcc_tokExpect(&tok, JCC_TOKEN_CLOSE_BRACKET)) {
		jcc_logError(ctx, &tok->m_Loc, "Expected ']'");
		return false;
	}
	
	*tokenListPtr = tok;

	return true;
}

// struct-designator = "." ident
static jx_cc_struct_member_t* jcc_parseStructDesignator(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_token_t** tokenListPtr, jx_cc_type_t* ty) 
{
	jx_cc_token_t* tok = *tokenListPtr;
	jx_cc_token_t* start = tok;

	if (!jcc_tokExpect(&tok, JCC_TOKEN_DOT)) {
		jcc_logError(ctx, &tok->m_Loc, "Expected '.'");
		return NULL;
	}

	if (tok->m_Kind != JCC_TOKEN_IDENTIFIER) {
		jcc_logError(ctx, &tok->m_Loc, "Expected a field designator");
		return NULL;
	}
	
	jx_cc_struct_member_t* member = NULL;
	for (jx_cc_struct_member_t* mem = ty->m_StructMembers; mem; mem = mem->m_Next) {
		if (mem->m_Type->m_Kind == JCC_TYPE_STRUCT && !mem->m_Name) {
			// Anonymous struct member
			if (jcc_getStructMember(mem->m_Type, tok)) {
				tok = start;
				member = mem;
				break;
			}
		} else if (mem->m_Name->m_String == tok->m_String) {
			// Regular struct member
			tok = tok->m_Next;
			member = mem;
			break;
		}
	}
	
	if (!member) {
		jcc_logError(ctx, &tok->m_Loc, "Struct has no such member");
		return NULL;
	}

	*tokenListPtr = tok;

	return member;
}

// designation = ("[" const-expr "]" | "." ident)* "="? initializer
static bool jcc_parseDesignation(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_token_t** tokenListPtr, jcc_initializer_t* init)
{
	jx_cc_token_t* tok = *tokenListPtr;

	if (jcc_tokIs(tok, JCC_TOKEN_OPEN_BRACKET)) {
		if (init->m_Type->m_Kind != JCC_TYPE_ARRAY) {
			jcc_logError(ctx, &tok->m_Loc, "Array index in non-array initializer");
			return false;
		}
		
		int begin, end;
		if (!jcc_parseArrayDesignator(ctx, tu, &tok, init->m_Type, &begin, &end)) {
			jcc_logError(ctx, &tok->m_Loc, "Invalid array designator");
			return false;
		}
		
		jx_cc_token_t* designationTok = tok;
		for (int i = begin; i <= end; i++) {
			tok = designationTok;
			if (!jcc_parseDesignation(ctx, tu, &tok, init->m_Children[i])) {
				jcc_logError(ctx, &tok->m_Loc, "Failed to parse designation");
				return false;
			}
		}
		
		if (!jcc_parseArrayInitializer2(ctx, tu, &tok, init, begin + 1)) {
			return false;
		}
	} else if (jcc_tokIs(tok, JCC_TOKEN_DOT)) {
		if (init->m_Type->m_Kind == JCC_TYPE_STRUCT) {
			jx_cc_struct_member_t* mem = jcc_parseStructDesignator(ctx, tu, &tok, init->m_Type);
			if (!mem) {
				jcc_logError(ctx, &tok->m_Loc, "Expected struct designator");
				return false;
			}

			if (!jcc_parseDesignation(ctx, tu, &tok, init->m_Children[mem->m_ID])) {
				jcc_logError(ctx, &tok->m_Loc, "Failed to parse designation");
				return false;
			}

			init->m_Expr = NULL;
			
			if (!jcc_parseStructInitializer2(ctx, tu, &tok, init, mem->m_Next)) {
				jcc_logError(ctx, &tok->m_Loc, "Failed to parse initializer");
				return false;
			}
		} else if (init->m_Type->m_Kind == JCC_TYPE_UNION) {
			jx_cc_struct_member_t* mem = jcc_parseStructDesignator(ctx, tu, &tok, init->m_Type);
			if (!mem) {
				jcc_logError(ctx, &tok->m_Loc, "Expected union designator");
				return false;
			}

			init->m_Members = mem;

			if (!jcc_parseDesignation(ctx, tu, &tok, init->m_Children[mem->m_ID])) {
				jcc_logError(ctx, &tok->m_Loc, "Failed to parse designation");
				return false;
			}
		} else {
			jcc_logError(ctx, &tok->m_Loc, "Field name not in struct or union initializer");
			return false;
		}
	} else {
		jcc_tokExpect(&tok, JCC_TOKEN_ASSIGN); // optional?

		if (!jcc_parseInitializer2(ctx, tu, &tok, init)) {
			return false;
		}
	}

	*tokenListPtr = tok;

	return true;
}

// An array length can be omitted if an array has an initializer
// (e.g. `int x[] = {1,2,3}`). If it's omitted, count the number
// of initializer elements.
// TODO: Return bool and pass result in ptr
static int64_t jcc_countArrayInitElements(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_token_t* tok, jx_cc_type_t* ty, bool* err)
{
	jcc_initializer_t* dummy = jcc_allocInitializer(ctx, ty->m_BaseType, true);
	if (!dummy) {
		*err = true;
		jcc_logError(ctx, JCC_SOURCE_LOCATION_CUR(), "Internal Error: Memory allocation failed.");
		return 0;
	}
	
	int64_t idx = 0;
	int64_t maxVal = 0;
	
	bool first = true;
	while (!jcc_tokConsumeEndOfList(&tok)) {
		if (!first) {
			if (!jcc_tokExpect(&tok, JCC_TOKEN_COMMA)) {
				jcc_logError(ctx, &tok->m_Loc, "Expected ','");
				*err = true;
				return 0;
			}
		}
		first = false;
		
		if (jcc_tokExpect(&tok, JCC_TOKEN_OPEN_BRACKET)) {
			idx = jcc_parseConstExpression(ctx, tu, &tok, err);
			if (*err) {
				jcc_logError(ctx, &tok->m_Loc, "Failed to parse constant expression");
				return 0;
			}

			if (jcc_tokIs(tok, JCC_TOKEN_ELLIPSIS)) {
				idx = jcc_parseConstExpression(ctx, tu, &tok, err);
				if (*err) {
					jcc_logError(ctx, &tok->m_Loc, "Failed to parse constant expression");
					return true;
				}
			}

			if (!jcc_tokExpect(&tok, JCC_TOKEN_CLOSE_BRACKET)) {
				jcc_logError(ctx, &tok->m_Loc, "Expected ']'");
				*err = true;
				return 0;
			}

			jcc_parseDesignation(ctx, tu, &tok, dummy);
		} else {
			if (!jcc_parseInitializer2(ctx, tu, &tok, dummy)) {
				jcc_logError(ctx, &tok->m_Loc, "Failed to parse initializer.");
				return 0; // ERROR
			}
		}
		
		idx++;
		maxVal = jx_max_i64(maxVal, idx);
	}

	return maxVal;
}

// array-initializer1 = "{" initializer ("," initializer)* ","? "}"
static bool jcc_parseArrayInitializer1(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_token_t** tokenListPtr, jcc_initializer_t* init)
{
	jx_cc_token_t* tok = *tokenListPtr;

	if (!jcc_tokExpect(&tok, JCC_TOKEN_OPEN_CURLY_BRACKET)) {
		jcc_logError(ctx, &tok->m_Loc, "Expected '{'");
		return false;
	}
	
	if (init->m_IsFlexible) {
		bool err = false;
		const int64_t len = jcc_countArrayInitElements(ctx, tu, tok, init->m_Type, &err);
		if (err) {
			jcc_logError(ctx, &tok->m_Loc, "Failed to calculate array size");
			return false;
		}

		jcc_initializer_t* arrayInit = jcc_allocInitializer(ctx, jcc_typeAllocArrayOf(ctx, init->m_Type->m_BaseType, (int)len), false);
		if (!arrayInit) {
			jcc_logError(ctx, JCC_SOURCE_LOCATION_CUR(), "Internal Error: Memory allocation failed.");
			return false;
		}

		jx_memcpy(init, arrayInit, sizeof(jcc_initializer_t));
	}
	
	if (init->m_IsFlexible) {
		bool err = false;
		const int64_t len = jcc_countArrayInitElements(ctx, tu, tok, init->m_Type, &err);
		if (err) {
			jcc_logError(ctx, &tok->m_Loc, "Failed to calculate array size");
			return false;
		}

		jcc_initializer_t* arrayInit = jcc_allocInitializer(ctx, jcc_typeAllocArrayOf(ctx, init->m_Type->m_BaseType, (int)len), false);
		if (!arrayInit) {
			jcc_logError(ctx, JCC_SOURCE_LOCATION_CUR(), "Internal Error: Memory allocation failed.");
			return false;
		}

		jx_memcpy(init, arrayInit, sizeof(jcc_initializer_t));
	}
	
	bool first = true;
	for (int idx = 0; !jcc_tokConsumeEndOfList(&tok); idx++) {
		if (!first) {
			if (!jcc_tokExpect(&tok, JCC_TOKEN_COMMA)) {
				jcc_logError(ctx, &tok->m_Loc, "Expected ','");
				return false;
			}
		}
		first = false;
		
		if (jcc_tokIs(tok, JCC_TOKEN_OPEN_BRACKET)) {
			int begin, end;
			if (!jcc_parseArrayDesignator(ctx, tu, &tok, init->m_Type, &begin, &end)) {
				jcc_logError(ctx, &tok->m_Loc, "Failed to parse array designator");
				return false;
			}
			
			jx_cc_token_t* designationToken = tok;
			for (int j = begin; j <= end; j++) {
				tok = designationToken;
				if (!jcc_parseDesignation(ctx, tu, &tok, init->m_Children[j])) {
					jcc_logError(ctx, &tok->m_Loc, "Failed to parse designation");
					return false;
				}
			}
			
			idx = end;
		} else {
			if (idx < init->m_Type->m_ArrayLen) {
				if (!jcc_parseInitializer2(ctx, tu, &tok, init->m_Children[idx])) {
					return false;
				}
			} else {
				tok = jcc_skipExcessElement(ctx, tu, tok);
				if (!tok) {
					return false; // ERROR: ?
				}
			}
		}
	}

	*tokenListPtr = tok;

	return true;
}

// array-initializer2 = initializer ("," initializer)*
static bool jcc_parseArrayInitializer2(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_token_t** tokenListPtr, jcc_initializer_t* init, int i)
{
	jx_cc_token_t* tok = *tokenListPtr;

	if (init->m_IsFlexible) {
		bool err = false;
		const int64_t len = jcc_countArrayInitElements(ctx, tu, tok, init->m_Type, &err);
		if (err) {
			jcc_logError(ctx, &tok->m_Loc, "Failed to calculate array size");
			return false;
		}

		jcc_initializer_t* arrayInit = jcc_allocInitializer(ctx, jcc_typeAllocArrayOf(ctx, init->m_Type->m_BaseType, (int)len), false);
		if (!arrayInit) {
			jcc_logError(ctx, JCC_SOURCE_LOCATION_CUR(), "Internal Error: Memory allocation failed.");
			return false;
		}

		jx_memcpy(init, arrayInit, sizeof(jcc_initializer_t));
	}
	
	for (; i < init->m_Type->m_ArrayLen && !jcc_tokIsEndOfList(tok); i++) {
		jx_cc_token_t* start = tok;
		if (i > 0) {
			if (!jcc_tokExpect(&tok, JCC_TOKEN_COMMA)) {
				jcc_logError(ctx, &tok->m_Loc, "Expected ','");
				return false;
			}
		}
		
		if (jcc_tokIs(tok, JCC_TOKEN_OPEN_BRACKET) || jcc_tokIs(tok, JCC_TOKEN_DOT)) {
			tok = start;
			break;
		}
		
		if (!jcc_parseInitializer2(ctx, tu, &tok, init->m_Children[i])) {
			return false;
		}
	}
	
	*tokenListPtr = tok;

	return true;
}

// struct-initializer1 = "{" initializer ("," initializer)* ","? "}"
static bool jcc_parseStructInitializer1(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_token_t** tokenListPtr, jcc_initializer_t* init)
{
	jx_cc_token_t* tok = *tokenListPtr;

	if (!jcc_tokExpect(&tok, JCC_TOKEN_OPEN_CURLY_BRACKET)) {
		jcc_logError(ctx, &tok->m_Loc, "Expected '{'");
		return false;
	}
	
	jx_cc_struct_member_t* mem = init->m_Type->m_StructMembers;

	bool first = true;	
	while (!jcc_tokConsumeEndOfList(&tok)) {
		if (!first) {
			if (!jcc_tokExpect(&tok, JCC_TOKEN_COMMA)) {
				jcc_logError(ctx, &tok->m_Loc, "Expected ','");
				return false;
			}
		}
		first = false;
		
		if (jcc_tokIs(tok, JCC_TOKEN_DOT)) {
			mem = jcc_parseStructDesignator(ctx, tu, &tok, init->m_Type);
			if (!mem) {
				jcc_logError(ctx, &tok->m_Loc, "Member not found");
				return false;
			}

			if (!jcc_parseDesignation(ctx, tu, &tok, init->m_Children[mem->m_ID])) {
				jcc_logError(ctx, &tok->m_Loc, "Failed to parse designation.");
				return false;
			}

			mem = mem->m_Next;
		} else {
			if (mem) {
				if (!jcc_parseInitializer2(ctx, tu, &tok, init->m_Children[mem->m_ID])) {
					jcc_logError(ctx, &tok->m_Loc, "Failed to parse initializer");
					return false;
				}

				mem = mem->m_Next;
			} else {
				tok = jcc_skipExcessElement(ctx, tu, tok);
				if (!tok) {
					jcc_logError(ctx, JCC_SOURCE_LOCATION_CUR(), "Syntax error.");
					return false;
				}
			}
		}
	}

	*tokenListPtr = tok;

	return true;
}

// struct-initializer2 = initializer ("," initializer)*
static bool jcc_parseStructInitializer2(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_token_t** tokenListPtr, jcc_initializer_t* init, jx_cc_struct_member_t* mem)
{
	jx_cc_token_t* tok = *tokenListPtr;

	bool first = true;	
	for (; mem && !jcc_tokIsEndOfList(tok); mem = mem->m_Next) {
		jx_cc_token_t* start = tok;
		
		if (!first) {
			if (!jcc_tokExpect(&tok, JCC_TOKEN_COMMA)) {
				jcc_logError(ctx, &tok->m_Loc, "Expected ','");
				return false;
			}
		}
		first = false;
		
		if (jcc_tokIs(tok, JCC_TOKEN_OPEN_BRACKET) || jcc_tokIs(tok, JCC_TOKEN_DOT)) {
			tok = start;
			break;
		}
		
		if (!jcc_parseInitializer2(ctx, tu, &tok, init->m_Children[mem->m_ID])) {
			jcc_logError(ctx, &tok->m_Loc, "Failed to parse initializer.");
			return false;
		}
	}
	
	*tokenListPtr = tok;

	return true;
}

static bool jcc_parseUnionInitializer(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_token_t** tokenListPtr, jcc_initializer_t* init)
{
	jx_cc_token_t* tok = *tokenListPtr;

	// Unlike structs, union initializers take only one initializer,
	// and that initializes the first union member by default.
	// You can initialize other member using a designated initializer.
	if (jcc_tokIs(tok, JCC_TOKEN_OPEN_CURLY_BRACKET) && jcc_tokIs(tok->m_Next, JCC_TOKEN_DOT)) {
		tok = tok->m_Next;

		jx_cc_struct_member_t* mem = jcc_parseStructDesignator(ctx, tu, &tok, init->m_Type);
		if (!mem) {
			jcc_logError(ctx, &tok->m_Loc, "Expected struct designator");
			return false;
		}

		init->m_Members = mem;

		if (!jcc_parseDesignation(ctx, tu, &tok, init->m_Children[mem->m_ID])) {
			jcc_logError(ctx, &tok->m_Loc, "Failed to parse designation.");
			return false;
		}

		if (!jcc_tokExpect(&tok, JCC_TOKEN_CLOSE_CURLY_BRACKET)) {
			jcc_logError(ctx, &tok->m_Loc, "Expected '}'");
			return false;
		}
	} else {
		init->m_Members = init->m_Type->m_StructMembers;

		if (jcc_tokExpect(&tok, JCC_TOKEN_OPEN_CURLY_BRACKET)) {
			if (!jcc_parseInitializer2(ctx, tu, &tok, init->m_Children[0])) {
				jcc_logError(ctx, &tok->m_Loc, "Failed to parse initializer.");
				return false;
			}

			jcc_tokExpect(&tok, JCC_TOKEN_COMMA); // optional

			if (!jcc_tokExpect(&tok, JCC_TOKEN_CLOSE_CURLY_BRACKET)) {
				jcc_logError(ctx, &tok->m_Loc, "Expected '}'");
				return false;
			}
		} else {
			if (!jcc_parseInitializer2(ctx, tu, &tok, init->m_Children[0])) {
				jcc_logError(ctx, &tok->m_Loc, "Failed to parse initializer.");
				return false;
			}
		}
	}

	*tokenListPtr = tok;

	return true;
}

// initializer = string-initializer | array-initializer
//             | struct-initializer | union-initializer
//             | assign
static bool jcc_parseInitializer2(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_token_t** tokenListPtr, jcc_initializer_t* init)
{
	jx_cc_token_t* tok = *tokenListPtr;

	if (init->m_Type->m_Kind == JCC_TYPE_ARRAY) {
		if (tok->m_Kind == JCC_TOKEN_STRING_LITERAL) {
			if (!jcc_parseStringInitializer(ctx, tu, &tok, init)) {
				jcc_logError(ctx, &tok->m_Loc, "Expected string initializer");
				return false;
			}
		} else {
			if (jcc_tokIs(tok, JCC_TOKEN_OPEN_CURLY_BRACKET)) {
				if (!jcc_parseArrayInitializer1(ctx, tu, &tok, init)) {
					jcc_logError(ctx, &tok->m_Loc, "Expected array initializer");
					return false;
				}
			} else {
				if (!jcc_parseArrayInitializer2(ctx, tu, &tok, init, 0)) {
					jcc_logError(ctx, &tok->m_Loc, "Expected array initializer");
					return false;
				}
			}
		}
	} else if (init->m_Type->m_Kind == JCC_TYPE_STRUCT) {
		if (jcc_tokIs(tok, JCC_TOKEN_OPEN_CURLY_BRACKET)) {
			if (!jcc_parseStructInitializer1(ctx, tu, &tok, init)) {
				jcc_logError(ctx, &tok->m_Loc, "Expected struct initializer");
				return false;
			}
		} else {
			jx_cc_token_t* start = tok;

			// A struct can be initialized with another struct. E.g.
			// `struct T x = y;` where y is a variable of type `struct T`.
			// Handle that case first.
			jx_cc_ast_expr_t* expr = jcc_parseAssignment(ctx, tu, &tok);
			if (!expr) {
				jcc_logError(ctx, &tok->m_Loc, "Expected expression");
				return false;
			}

			if (!jcc_astAddType(ctx, &expr->super)) {
				jcc_logError(ctx, JCC_SOURCE_LOCATION_CUR(), "Internal Error: Failed to add type to expression node.");
				return false;
			}

			if (expr->m_Type->m_Kind == JCC_TYPE_STRUCT) {
				init->m_Expr = expr;
			} else {
				if (!init->m_Type->m_StructMembers) {
					jcc_logError(ctx, &tok->m_Loc, "Initializer for empty aggregate requires explicit braces.");
					return false;
				}

				if (!jcc_parseStructInitializer2(ctx, tu, &tok, init, init->m_Type->m_StructMembers)) {
					jcc_logError(ctx, &tok->m_Loc, "Failed to parse struct initializer");
					return false;
				}
			}
		}
	} else if (init->m_Type->m_Kind == JCC_TYPE_UNION) {
		if (jcc_tokIs(tok, JCC_TOKEN_OPEN_CURLY_BRACKET)) {
			if (!jcc_parseUnionInitializer(ctx, tu, &tok, init)) {
				jcc_logError(ctx, &tok->m_Loc, "Expected union initializer");
				return false;
			}
		} else {
			jx_cc_token_t* start = tok;
			jx_cc_ast_expr_t* expr = jcc_parseAssignment(ctx, tu, &tok);
			if (!expr) {
				jcc_logError(ctx, &tok->m_Loc, "Expected expression");
				return false;
			}

			if (!jcc_astAddType(ctx, &expr->super)) {
				jcc_logError(ctx, JCC_SOURCE_LOCATION_CUR(), "Internal Error: Failed to add type to expression node.");
				return false;
			}

			if (expr->m_Type->m_Kind == JCC_TYPE_UNION) {
				init->m_Expr = expr;
			} else {
				if (!init->m_Type->m_StructMembers) {
					jcc_logError(ctx, &tok->m_Loc, "Initializer for empty aggregate requires explicit braces.");
					return false;
				}

				init->m_Members = init->m_Type->m_StructMembers;
				tok = start;
				jcc_parseInitializer2(ctx, tu, &tok, init->m_Children[0]);
			}
		}
	} else {
		if (jcc_tokExpect(&tok, JCC_TOKEN_OPEN_CURLY_BRACKET)) {
			// An initializer for a scalar variable can be surrounded by
			// braces. E.g. `int x = {3};`. Handle that case.
			if (!jcc_parseInitializer2(ctx, tu, &tok, init)) {
				jcc_logError(ctx, &tok->m_Loc, "Failed to parse initializer");
				return false;
			}

			if (!jcc_tokExpect(&tok, JCC_TOKEN_CLOSE_CURLY_BRACKET)) {
				jcc_logError(ctx, &tok->m_Loc, "Expected '}'");
				return false;
			}
		} else {
			init->m_Expr = jcc_parseAssignment(ctx, tu, &tok);
			if (!init->m_Expr) {
				jcc_logError(ctx, &tok->m_Loc, "Failed to parse expression");
				return false;
			}
		}
	}

	*tokenListPtr = tok;

	return true;
}

static jx_cc_type_t* jcc_copyStructType(jx_cc_context_t* ctx, const jx_cc_type_t* ty)
{
	jx_cc_type_t* copy = jcc_typeCopy(ctx, ty);
	
	jx_cc_struct_member_t head = { 0 };
	jx_cc_struct_member_t* cur = &head;

	for (jx_cc_struct_member_t* mem = copy->m_StructMembers; mem; mem = mem->m_Next) {
		jx_cc_struct_member_t* m = (jx_cc_struct_member_t*)JX_ALLOC(ctx->m_LinearAllocator, sizeof(jx_cc_struct_member_t));
		if (!m) {
			jcc_logError(ctx, JCC_SOURCE_LOCATION_CUR(), "Internal Error: Memory allocation failed.");
			return NULL;
		}

		jx_memcpy(m, mem, sizeof(jx_cc_struct_member_t));
		
		cur->m_Next = m;
		cur = cur->m_Next;
	}
	
	copy->m_StructMembers = head.m_Next;

	return copy;
}

static jcc_initializer_t* jcc_parseInitializer(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_token_t** tokenListPtr, jx_cc_type_t* ty, jx_cc_type_t** new_ty)
{
	jx_cc_token_t* tok = *tokenListPtr;

	jcc_initializer_t* init = jcc_allocInitializer(ctx, ty, true);
	if (!init) {
		jcc_logError(ctx, JCC_SOURCE_LOCATION_CUR(), "Internal Error: Memory allocation failed.");
		return NULL;
	}

	if (!jcc_parseInitializer2(ctx, tu, &tok, init)) {
		jcc_logError(ctx, &tok->m_Loc, "Failed to parse initializer.");
		return NULL;
	}
	
	if ((ty->m_Kind == JCC_TYPE_STRUCT || ty->m_Kind == JCC_TYPE_UNION) && (ty->m_Flags & JCC_TYPE_FLAGS_IS_FLEXIBLE_Msk) != 0) {
		jx_cc_type_t* copy = jcc_copyStructType(ctx, ty);
		if (!copy) {
			jcc_logError(ctx, JCC_SOURCE_LOCATION_CUR(), "Internal Error: Memory allocation failed.");
			return NULL;
		}
		
		jx_cc_struct_member_t* mem = copy->m_StructMembers;
		while (mem->m_Next) {
			mem = mem->m_Next;
		}
		mem->m_Type = init->m_Children[mem->m_ID]->m_Type;
		copy->m_Size += mem->m_Type->m_Size;
		
		jx_memcpy(*new_ty, copy, sizeof(jx_cc_type_t));
	} else {
		jx_memcpy(*new_ty, init->m_Type, sizeof(jx_cc_type_t));
	}

	*tokenListPtr = tok;

	return init;
}

static jx_cc_ast_expr_t* jcc_astInitDesgExpr(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jcc_init_desg_t* desg, jx_cc_token_t* tok)
{
	jx_cc_ast_expr_t* expr = NULL;
	if (desg->m_Var) {
		expr = jcc_astAllocExprVar(ctx
			, desg->m_Var
			, tok
		);
	} else if (desg->m_Member) {
		expr = jcc_astAllocExprMember(ctx
			, jcc_astInitDesgExpr(ctx, tu, desg->m_Next, tok)
			, desg->m_Member
			, tok
		);
	} else {
		expr = jcc_astAllocExprUnary(ctx
			, JCC_NODE_EXPR_DEREF
			, jcc_astAllocExprAdd(ctx
				, jcc_astInitDesgExpr(ctx, tu, desg->m_Next, tok)
				, jcc_astAllocExprIConst(ctx, desg->m_ID, kType_int, tok)
				, tok)
			, tok
		);
	}

	return expr;
}

static jx_cc_ast_expr_t* jcc_astCreateLocalVarInitializer(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jcc_initializer_t* init, jx_cc_type_t* ty, jcc_init_desg_t* desg, jx_cc_token_t* tok) 
{
	jx_cc_ast_expr_t* node = NULL;

	if (ty->m_Kind == JCC_TYPE_ARRAY) {
		node = jcc_astAllocExprNull(ctx, tok);
		for (int i = 0; i < ty->m_ArrayLen && node != NULL; i++) {
			jcc_init_desg_t desg2 = { 
				.m_Next = desg, 
				.m_ID = i,
				.m_Member = NULL,
				.m_Var = NULL
			};
		
			node = jcc_astAllocExprComma(ctx
				, node
				, jcc_astCreateLocalVarInitializer(ctx, tu
					, init->m_Children[i]
					, ty->m_BaseType
					, &desg2
					, tok)
				, tok
			);
		}
	} else {
		if (init->m_Expr) {
			node = jcc_astAllocExprBinary(ctx
				, JCC_NODE_EXPR_ASSIGN
				, jcc_astInitDesgExpr(ctx, tu, desg, tok)
				, init->m_Expr
				, tok
			);
		} else {
			if (ty->m_Kind == JCC_TYPE_STRUCT) {
				node = jcc_astAllocExprNull(ctx, tok);
				for (jx_cc_struct_member_t* mem = ty->m_StructMembers; mem != NULL && node != NULL; mem = mem->m_Next) {
					jcc_init_desg_t desg2 = {
						.m_Next = desg,
						.m_ID = 0,
						.m_Member = mem,
						.m_Var = NULL
					};

					node = jcc_astAllocExprComma(ctx
						, node
						, jcc_astCreateLocalVarInitializer(ctx, tu
							, init->m_Children[mem->m_ID]
							, mem->m_Type
							, &desg2
							, tok)
						, tok
					);
				}
			} else if (ty->m_Kind == JCC_TYPE_UNION) {
				if (!init->m_Members) {
					node = jcc_astAllocExprNull(ctx, tok);
				} else {
					jcc_init_desg_t desg2 = {
						.m_Next = desg,
						.m_ID = 0,
						.m_Member = init->m_Members,
						.m_Var = NULL
					};

					node = jcc_astCreateLocalVarInitializer(ctx, tu, init->m_Children[init->m_Members->m_ID], init->m_Members->m_Type, &desg2, tok);
				}
			} else {
				node = jcc_astAllocExprNull(ctx, tok);
			}
		}
	}

	return node;
}

// A variable definition with an initializer is a shorthand notation
// for a variable definition followed by assignments. This function
// generates assignment expressions for an initializer. For example,
// `int x[2][2] = {{6, 7}, {8, 9}}` is converted to the following
// expressions:
//
//   x[0][0] = 6;
//   x[0][1] = 7;
//   x[1][0] = 8;
//   x[1][1] = 9;
static jx_cc_ast_expr_t* jcc_parseLocalVarInitializer(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_token_t** tokenListPtr, jx_cc_object_t* var)
{
	jx_cc_token_t* start = *tokenListPtr;
	jx_cc_token_t* tok = *tokenListPtr;

	jcc_initializer_t* init = jcc_parseInitializer(ctx, tu, &tok, var->m_Type, &var->m_Type);
	if (!init) {
		return NULL;
	}

	jcc_init_desg_t desg = { 
		.m_Next = NULL, 
		.m_ID = 0, 
		.m_Member = NULL, 
		.m_Var = var 
	};

	jx_cc_ast_expr_t* node = NULL;
	if (jx_cc_typeIsNumeric(var->m_Type) || var->m_Type->m_Kind == JCC_TYPE_PTR) {
		node = jcc_astCreateLocalVarInitializer(ctx, tu, init, var->m_Type, &desg, start);
	} else {
		// If a partial initializer list is given, the standard requires
		// that unspecified elements are set to 0. Here, we simply
		// zero-initialize the entire memory region of a variable before
		// initializing it with user-supplied values.
		node = jcc_astAllocExprComma(ctx
			, jcc_astAllocExprMemZero(ctx, var, start)
			, jcc_astCreateLocalVarInitializer(ctx, tu, init, var->m_Type, &desg, start)
			, start
		);
	}
	if (!node) {
		return NULL;
	}

	*tokenListPtr = tok;

	return node;
}

static uint64_t jcc_readBuf(char* buf, int sz) 
{
	if (sz == 1) {
		return *buf;
	} else if (sz == 2) {
		return *(uint16_t*)buf;
	} else if (sz == 4) {
		return *(uint32_t*)buf;
	} else if (sz == 8) {
		return *(uint64_t*)buf;
	}

	JX_CHECK(false, "Unreachable code");

	return 0;
}

static void jcc_writeBuf(char* buf, uint64_t val, int sz) 
{
	if (sz == 1) {
		*buf = (char)val;
	} else if (sz == 2) {
		*(uint16_t*)buf = (uint16_t)val;
	} else if (sz == 4) {
		*(uint32_t*)buf = (uint32_t)val;
	} else if (sz == 8) {
		*(uint64_t*)buf = val;
	} else {
		JX_CHECK(false, "Unreachable code");
	}
}

static jx_cc_relocation_t* jcc_writeGlobalVarData(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_relocation_t* cur, jcc_initializer_t* init, jx_cc_type_t* ty, char* buf, int offset) 
{
	if (ty->m_Kind == JCC_TYPE_ARRAY) {
		int sz = ty->m_BaseType->m_Size;
		for (int i = 0; i < ty->m_ArrayLen; i++) {
			cur = jcc_writeGlobalVarData(ctx, tu, cur, init->m_Children[i], ty->m_BaseType, buf, offset + sz * i);
		}
	} else if (ty->m_Kind == JCC_TYPE_STRUCT) {
		for (jx_cc_struct_member_t* mem = ty->m_StructMembers; mem; mem = mem->m_Next) {
			if (mem->m_IsBitfield) {
				jx_cc_ast_expr_t* expr = init->m_Children[mem->m_ID]->m_Expr;
				if (!expr) {
					break;
				}
				
				char* loc = buf + offset + mem->m_Offset;
				uint64_t oldval = jcc_readBuf(loc, mem->m_Type->m_Size);

				bool err = false;
				uint64_t newval = (uint64_t)jcc_astEvalConstExpression(ctx, tu, expr, &err);
				if (err) {
					return NULL;
				}

				uint64_t mask = (1L << mem->m_BitWidth) - 1;
				uint64_t combined = oldval | ((newval & mask) << mem->m_BitOffset);
				jcc_writeBuf(loc, combined, mem->m_Type->m_Size);
			} else {
				cur = jcc_writeGlobalVarData(ctx, tu, cur, init->m_Children[mem->m_ID], mem->m_Type, buf, offset + mem->m_Offset);
			}
		}
	} else if (ty->m_Kind == JCC_TYPE_UNION) {
		if (init->m_Members) {
			cur = jcc_writeGlobalVarData(ctx, tu, cur, init->m_Children[init->m_Members[0].m_ID], init->m_Members[0].m_Type, buf, offset);
		}
	} else {
		if (!init->m_Expr) {
			return cur;
		}
	
		if (ty->m_Kind == JCC_TYPE_FLOAT) {
			bool err = false;
			*(float*)(buf + offset) = (float)jcc_astEvalConstExpressionDouble(ctx, tu, init->m_Expr, &err);
			if (err) {
				return NULL;
			}
		} else if (ty->m_Kind == JCC_TYPE_DOUBLE) {
			bool err = false;
			*(double*)(buf + offset) = jcc_astEvalConstExpressionDouble(ctx, tu, init->m_Expr, &err);
			if (err) {
				return NULL;
			}
		} else {
			char** label = NULL;
			bool err = false;
			uint64_t val = (uint64_t)jcc_astEvalConstExpression2(ctx, tu, init->m_Expr, &label, &err);
			if (err) {
				return NULL;
			}

			if (!label) {
				jcc_writeBuf(buf + offset, val, ty->m_Size);
			} else {
				jx_cc_relocation_t* rel = (jx_cc_relocation_t*)JX_ALLOC(ctx->m_LinearAllocator, sizeof(jx_cc_relocation_t));
				if (!rel) {
					return NULL;
				}

				jx_memset(rel, 0, sizeof(jx_cc_relocation_t));
				rel->m_Offset = offset;
				rel->m_Label = label;
				rel->m_Addend = (int64_t)val;

				cur->m_Next = rel;
				cur = rel;
			}
		}
	}

	return cur;
}

// Initializers for global variables are evaluated at compile-time and
// embedded to .data section. This function serializes jcc_initializer_t
// objects to a flat byte array. It is a compile error if an
// initializer list contains a non-constant expression.
static bool jcc_parseGlobalVarInitializer(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_token_t** tokenListPtr, jx_cc_object_t* var)
{
	jx_cc_token_t* tok = *tokenListPtr;

	jcc_initializer_t* init = jcc_parseInitializer(ctx, tu, &tok, var->m_Type, &var->m_Type);
	if (!init) {
		jcc_logError(ctx, &tok->m_Loc, "Failed to parse global var initializer.");
		return false;
	}
	
	jx_cc_relocation_t head = { 0 };
	char* buf = (char*)JX_ALLOC(ctx->m_LinearAllocator, var->m_Type->m_Size);
	if (!buf) {
		jcc_logError(ctx, JCC_SOURCE_LOCATION_CUR(), "Internal Error: Memory allocation failed.");
		return false;
	}

	jx_memset(buf, 0, var->m_Type->m_Size);

	jcc_writeGlobalVarData(ctx, tu, &head, init, var->m_Type, buf, 0);

	var->m_GlobalInitData = buf;
	var->m_GlobalRelocations = head.m_Next;

	*tokenListPtr = tok;

	return true;
}

// Returns true if a given token represents a type.
static bool jcc_tuIsTypename(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_token_t* tok)
{
	const bool isTypename = false
		|| jcc_tokIs(tok, JCC_TOKEN_VOID)
		|| jcc_tokIs(tok, JCC_TOKEN_BOOL)
		|| jcc_tokIs(tok, JCC_TOKEN_CHAR)
		|| jcc_tokIs(tok, JCC_TOKEN_SHORT)
		|| jcc_tokIs(tok, JCC_TOKEN_INT)
		|| jcc_tokIs(tok, JCC_TOKEN_LONG)
		|| jcc_tokIs(tok, JCC_TOKEN_STRUCT)
		|| jcc_tokIs(tok, JCC_TOKEN_UNION)
		|| jcc_tokIs(tok, JCC_TOKEN_TYPEDEF)
		|| jcc_tokIs(tok, JCC_TOKEN_ENUM)
		|| jcc_tokIs(tok, JCC_TOKEN_STATIC)
		|| jcc_tokIs(tok, JCC_TOKEN_EXTERN)
		|| jcc_tokIs(tok, JCC_TOKEN_ALIGNAS)
		|| jcc_tokIs(tok, JCC_TOKEN_SIGNED)
		|| jcc_tokIs(tok, JCC_TOKEN_UNSIGNED)
		|| jcc_tokIs(tok, JCC_TOKEN_CONST)
		|| jcc_tokIs(tok, JCC_TOKEN_VOLATILE)
		|| jcc_tokIs(tok, JCC_TOKEN_AUTO)
		|| jcc_tokIs(tok, JCC_TOKEN_REGISTER)
		|| jcc_tokIs(tok, JCC_TOKEN_RESTRICT)
		|| jcc_tokIs(tok, JCC_TOKEN_NORETURN)
		|| jcc_tokIs(tok, JCC_TOKEN_FLOAT)
		|| jcc_tokIs(tok, JCC_TOKEN_DOUBLE)
		|| jcc_tokIs(tok, JCC_TOKEN_TYPEOF)
		|| jcc_tokIs(tok, JCC_TOKEN_INLINE)
		|| jcc_tokIs(tok, JCC_TOKEN_THREAD_LOCAL)
		|| jcc_tokIs(tok, JCC_TOKEN_ATOMIC)
		;

	return isTypename
		|| jcc_tuFindTypeDefinition(tu, tok)
		;
}

// asm-stmt = "asm" ("volatile" | "inline")* "(" string-literal ")"
static jx_cc_ast_stmt_t* jcc_parseAsmStatement(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_token_t** tokenListPtr)
{
	jx_cc_token_t* tok = *tokenListPtr;

	// Skip volatile/inline
	while (jcc_tokIs(tok, JCC_TOKEN_VOLATILE) || jcc_tokIs(tok, JCC_TOKEN_INLINE)) {
		tok = tok->m_Next;
	}
	
	if (!jcc_tokExpect(&tok, JCC_TOKEN_OPEN_PAREN)) {
		jcc_logError(ctx, &tok->m_Loc, "Expected '(' after 'asm'");
		return NULL;
	}
	
	if (tok->m_Kind != JCC_TOKEN_STRING_LITERAL || tok->m_Type->m_BaseType->m_Kind != JCC_TYPE_CHAR) {
		jcc_logError(ctx, &tok->m_Loc, "Expected string literal after '('");
		return NULL;
	}
	
	const jx_cc_token_t* asmStrTok = tok;
	tok = tok->m_Next;

	if (!jcc_tokExpect(&tok, JCC_TOKEN_CLOSE_PAREN)) {
		jcc_logError(ctx, &tok->m_Loc, "Expected ')' after string literal");
		return NULL;
	}

	jx_cc_ast_stmt_t* node = jcc_astAllocStmtAsm(ctx, asmStrTok->m_Val_string, 0, *tokenListPtr);
	if (!node) {
		jcc_logError(ctx, JCC_SOURCE_LOCATION_CUR(), "Internal Error: Memory allocation failed.");
		return NULL;
	}

	*tokenListPtr = tok;

	return node;
}

// Helper function which parses the body statement of the switch 
// and manages the switch/break stack of the current function.
static bool jcc_parseSwitchStatement(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_ast_stmt_t* stmt, jx_cc_token_t** tokenListPtr)
{
	JX_CHECK(stmt->super.m_Kind == JCC_NODE_STMT_SWITCH, "Only switch nodes are allowed");
	
	jx_cc_ast_stmt_switch_t* switchNode = (jx_cc_ast_stmt_switch_t*)stmt;
	jx_cc_token_t* tok = *tokenListPtr;

	jx_cc_ast_stmt_switch_t* curSwitchNode = tu->m_CurFuncSwitch;
	jx_cc_label_t curBrkLabel = tu->m_CurFuncBreakLabel;

	tu->m_CurFuncSwitch = switchNode;
	tu->m_CurFuncBreakLabel = switchNode->m_BreakLbl;

	switchNode->m_BodyStmt = jcc_parseStatement(ctx, tu, &tok);

	tu->m_CurFuncSwitch = curSwitchNode;
	tu->m_CurFuncBreakLabel = curBrkLabel;

	if (!switchNode->m_BodyStmt) {
		return false;
	}

	*tokenListPtr = tok;

	return true;
}

static void jcc_tuSwitchAddCase(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_ast_stmt_t* stmt, bool isDefault)
{
	JX_CHECK(stmt && stmt->super.m_Kind == JCC_NODE_STMT_CASE, "Only case nodes are allowed");
	JX_CHECK(tu->m_CurFuncSwitch != NULL, "Must be called only when parsing switch statements");

	jx_cc_ast_stmt_case_t* caseNode = (jx_cc_ast_stmt_case_t*)stmt;

	if (isDefault) {
		tu->m_CurFuncSwitch->m_DefaultCase = caseNode;
	} else {
		caseNode->m_NextCase = tu->m_CurFuncSwitch->m_CaseListHead;
		tu->m_CurFuncSwitch->m_CaseListHead = caseNode;
	}
}

static void jcc_tuFuncAddGoto(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_ast_stmt_t* stmt)
{
	JX_CHECK(stmt->super.m_Kind == JCC_NODE_STMT_GOTO, "Only goto nodes are allowed");

	jx_cc_ast_stmt_goto_t* gotoNode = (jx_cc_ast_stmt_goto_t*)stmt;

	gotoNode->m_NextGoto = tu->m_CurFuncGotos;
	tu->m_CurFuncGotos = gotoNode;
}

static void jcc_tuFuncAddLabel(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_ast_stmt_t* stmt)
{
	JX_CHECK(stmt->super.m_Kind == JCC_NODE_STMT_LABEL, "Only label nodes are allowed");

	jx_cc_ast_stmt_label_t* labelNode = (jx_cc_ast_stmt_label_t*)stmt;

	labelNode->m_NextLabel = tu->m_CurFuncLabels;
	tu->m_CurFuncLabels = labelNode;
}

// stmt = "return" expr? ";"
//      | "if" "(" expr ")" stmt ("else" stmt)?
//      | "switch" "(" expr ")" stmt
//      | "case" const-expr ("..." const-expr)? ":" stmt
//      | "default" ":" stmt
//      | "for" "(" expr-stmt expr? ";" expr? ")" stmt
//      | "while" "(" expr ")" stmt
//      | "do" stmt "while" "(" expr ")" ";"
//      | "asm" asm-stmt
//      | "goto" (ident | "*" expr) ";"
//      | "break" ";"
//      | "continue" ";"
//      | ident ":" stmt
//      | "{" compound-stmt
//      | expr-stmt
static jx_cc_ast_stmt_t* jcc_parseStatement(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_token_t** tokenListPtr)
{
	jx_cc_token_t* tok = *tokenListPtr;

	jx_cc_ast_stmt_t* node = NULL;

	if (jcc_tokExpect(&tok, JCC_TOKEN_RETURN)) {
		jx_cc_ast_expr_t* expr = NULL;
		if (!jcc_tokExpect(&tok, JCC_TOKEN_SEMICOLON)) {
			expr = jcc_parseExpression(ctx, tu, &tok);
			if (!expr) {
				jcc_logError(ctx, &tok->m_Loc, "Expected expression");
				return NULL;
			}

			if (!jcc_tokExpect(&tok, JCC_TOKEN_SEMICOLON)) {
				jcc_logError(ctx, &tok->m_Loc, "Expected ';'");
				return NULL;
			}

			if (!jcc_astAddType(ctx, &expr->super)) {
				jcc_logError(ctx, JCC_SOURCE_LOCATION_CUR(), "Internal Error: Memory allocation failed.");
				return NULL;
			}

			jx_cc_type_t* ty = tu->m_CurFunction->m_Type->m_FuncRetType;
			if (ty->m_Kind != JCC_TYPE_STRUCT && ty->m_Kind != JCC_TYPE_UNION) {
				expr = jcc_astAllocExprCast(ctx, expr, tu->m_CurFunction->m_Type->m_FuncRetType);
				if (!expr) {
					jcc_logError(ctx, JCC_SOURCE_LOCATION_CUR(), "Internal Error: Memory allocation failed.");
					return NULL;
				}
			}
		}

		node = jcc_astAllocStmtReturn(ctx, expr, *tokenListPtr);
		if (!node) {
			jcc_logError(ctx, JCC_SOURCE_LOCATION_CUR(), "Internal Error: Memory allocation failed.");
			return NULL;
		}
	} else if (jcc_tokExpect(&tok, JCC_TOKEN_IF)) {
		if (!jcc_tokExpect(&tok, JCC_TOKEN_OPEN_PAREN)) {
			jcc_logError(ctx, &tok->m_Loc, "Expected '(' after 'if'");
			return NULL;
		}

		jx_cc_ast_expr_t* condExpr = jcc_parseExpression(ctx, tu, &tok);
		if (!condExpr) {
			jcc_logError(ctx, &tok->m_Loc, "Expected expression after '('");
			return NULL;
		}

		if (!jcc_tokExpect(&tok, JCC_TOKEN_CLOSE_PAREN)) {
			jcc_logError(ctx, &tok->m_Loc, "Expected ')' after expression");
			return NULL;
		}

		jx_cc_ast_stmt_t* thenStmt = jcc_parseStatement(ctx, tu, &tok);
		if (!thenStmt) {
			jcc_logError(ctx, &tok->m_Loc, "Expected statement after 'if'");
			return NULL;
		}

		jx_cc_ast_stmt_t* elseStmt = NULL;
		if (jcc_tokExpect(&tok, JCC_TOKEN_ELSE)) {
			elseStmt = jcc_parseStatement(ctx, tu, &tok);
			if (!elseStmt) {
				jcc_logError(ctx, &tok->m_Loc, "Expected statement after 'else'");
				return NULL;
			}
		}

		jx_cc_label_t thenLbl = jcc_tuGenerateUniqueLabel(ctx, tu);
		jx_cc_label_t elseLbl = jcc_tuGenerateUniqueLabel(ctx, tu);
		jx_cc_label_t endLbl = jcc_tuGenerateUniqueLabel(ctx, tu);

		node = jcc_astAllocStmtIf(ctx, condExpr, thenStmt, elseStmt, thenLbl, elseLbl, endLbl, *tokenListPtr);
		if (!node) {
			jcc_logError(ctx, JCC_SOURCE_LOCATION_CUR(), "Internal Error: Memory allocation failed.");
			return NULL;
		}
	} else if (jcc_tokExpect(&tok, JCC_TOKEN_SWITCH)) {
		if (!jcc_tokExpect(&tok, JCC_TOKEN_OPEN_PAREN)) {
			jcc_logError(ctx, &tok->m_Loc, "Expected '(' after 'switch'");
			return NULL;
		}

		jx_cc_ast_expr_t* condExpr = jcc_parseExpression(ctx, tu, &tok);
		if (!condExpr) {
			jcc_logError(ctx, &tok->m_Loc, "Expected expression after '('");
			return NULL;
		}

		if (!jcc_tokExpect(&tok, JCC_TOKEN_CLOSE_PAREN)) {
			jcc_logError(ctx, &tok->m_Loc, "Expected ')' after expression");
			return NULL;
		}
		
		node = jcc_astAllocStmtSwitch(ctx, condExpr, jcc_tuGenerateUniqueLabel(ctx, tu), *tokenListPtr);
		if (!node) {
			jcc_logError(ctx, JCC_SOURCE_LOCATION_CUR(), "Internal Error: Memory allocation failed.");
			return NULL;
		}
		
		if (!jcc_parseSwitchStatement(ctx, tu, node, &tok)) {
			jcc_logError(ctx, &tok->m_Loc, "Expected statement after 'switch'");
			return NULL;
		}
	} else if (jcc_tokExpect(&tok, JCC_TOKEN_CASE)) {
		if (!tu->m_CurFuncSwitch) {
			jcc_logError(ctx, &tok->m_Loc, "Stray case statement");
			return NULL;
		}

		bool err = false;
		const int64_t begin = jcc_parseConstExpression(ctx, tu, &tok, &err);
		if (err) {
			jcc_logError(ctx, &tok->m_Loc, "Failed to parse constant expression");
			return NULL;
		}

#if 1
		const int64_t end = begin;
#else // GCC extension
		int64_t end;
		if (jcc_tokIs(tok, JCC_TOKEN_ELLIPSIS)) {
			// [GNU] Case ranges, e.g. "case 1 ... 5:"
			end = (int)jcc_parseConstExpression(ctx, tu, &tok, tok->next);
			if (end < begin) {
				jcc_errorTokOut(tok, "empty case range specified");
			}
		} else {
			end = begin;
		}
#endif

		if (!jcc_tokExpect(&tok, JCC_TOKEN_COLON)) {
			jcc_logError(ctx, &tok->m_Loc, "Expected ':' after constant expression");
			return NULL;
		}

		jx_cc_label_t caseLbl = jcc_tuGenerateUniqueLabel(ctx, tu);

		jx_cc_ast_stmt_t* caseStmt = jcc_parseStatement(ctx, tu, &tok);
		if (!caseStmt) {
			jcc_logError(ctx, &tok->m_Loc, "Expected statement");
			return NULL;
		}

		node = jcc_astAllocStmtCase(ctx, caseLbl, begin, end, caseStmt, *tokenListPtr);
		if (!node) {
			jcc_logError(ctx, JCC_SOURCE_LOCATION_CUR(), "Internal Error: Memory allocation failed.");
			return NULL;
		}

		// Add case statement to current switch
		jcc_tuSwitchAddCase(ctx, tu, node, false);
	} else if (jcc_tokExpect(&tok, JCC_TOKEN_DEFAULT)) {
		if (!tu->m_CurFuncSwitch) {
			jcc_logError(ctx, &tok->m_Loc, "Stray default statement");
			return NULL;
		}
		
		if (!jcc_tokExpect(&tok, JCC_TOKEN_COLON)) {
			jcc_logError(ctx, &tok->m_Loc, "Expected ':'");
			return NULL;
		}

		jx_cc_label_t lbl = jcc_tuGenerateUniqueLabel(ctx, tu);

		jx_cc_ast_stmt_t* caseStmt = jcc_parseStatement(ctx, tu, &tok);
		if (!caseStmt) {
			jcc_logError(ctx, &tok->m_Loc, "Expected statement");
			return NULL;
		}

		node = jcc_astAllocStmtCase(ctx, lbl, INT64_MAX, INT64_MAX, caseStmt, *tokenListPtr);
		if (!node) {
			jcc_logError(ctx, JCC_SOURCE_LOCATION_CUR(), "Internal Error: Memory allocation failed.");
			return NULL;
		}

		jcc_tuSwitchAddCase(ctx, tu, node, true);
	} else if (jcc_tokExpect(&tok, JCC_TOKEN_FOR)) {
		if (!jcc_tokExpect(&tok, JCC_TOKEN_OPEN_PAREN)) {
			jcc_logError(ctx, &tok->m_Loc, "Expected '(' after 'for'");
			return NULL;
		}

		jx_cc_label_t newBrkLbl = jcc_tuGenerateUniqueLabel(ctx, tu);
		jx_cc_label_t newContLbl = jcc_tuGenerateUniqueLabel(ctx, tu);
		jx_cc_label_t bodyLbl = jcc_tuGenerateUniqueLabel(ctx, tu);
		jx_cc_ast_stmt_t* initStmt = NULL;
		jx_cc_ast_expr_t* condExpr = NULL;
		jx_cc_ast_expr_t* incExpr = NULL;
		jx_cc_ast_stmt_t* bodyStmt = NULL;

		if (!jcc_tuEnterScope(ctx, tu)) {
			jcc_logError(ctx, JCC_SOURCE_LOCATION_CUR(), "Internal Error: Memory allocation failed.");
			return NULL;
		}
		{
			if (jcc_tuIsTypename(ctx, tu, tok)) {
				jx_cc_type_t* basety = jcc_parseDeclarationSpecifiers(ctx, tu, &tok, NULL);
				if (!basety) {
					jcc_logError(ctx, &tok->m_Loc, "Failed to parse declaration specifiers.");
					return NULL;
				}

				initStmt = jcc_parseDeclaration(ctx, tu, &tok, basety, NULL);
			} else {
				initStmt = jcc_parseExpressionStatement(ctx, tu, &tok);
			}

			if (!initStmt) {
				jcc_logError(ctx, &tok->m_Loc, "Failed to parse for loop initialization statement.");
				return NULL;
			}

			if (!jcc_tokIs(tok, JCC_TOKEN_SEMICOLON)) {
				condExpr = jcc_parseExpression(ctx, tu, &tok);
				if (!condExpr) {
					jcc_logError(ctx, &tok->m_Loc, "Expected for loop conditional expression");
					return NULL;
				}
			}

			if (!jcc_tokExpect(&tok, JCC_TOKEN_SEMICOLON)) {
				jcc_logError(ctx, &tok->m_Loc, "Expected ';' after for loop terminator expression");
				return NULL;
			}

			if (!jcc_tokIs(tok, JCC_TOKEN_CLOSE_PAREN)) {
				incExpr = jcc_parseExpression(ctx, tu, &tok);
				if (!incExpr) {
					jcc_logError(ctx, &tok->m_Loc, "Expected for loop iteration expression");
					return NULL;
				}
			}

			if (!jcc_tokExpect(&tok, JCC_TOKEN_CLOSE_PAREN)) {
				jcc_logError(ctx, &tok->m_Loc, "Expected ')'");
				return NULL;
			}

			jx_cc_label_t curBrkLbl = tu->m_CurFuncBreakLabel;
			jx_cc_label_t curContLbl = tu->m_CurFuncContinueLabel;
			tu->m_CurFuncBreakLabel = newBrkLbl;
			tu->m_CurFuncContinueLabel = newContLbl;

			bodyStmt = jcc_parseStatement(ctx, tu, &tok);
			if (!bodyStmt) {
				jcc_logError(ctx, &tok->m_Loc, "Expected statement");
				return NULL;
			}

			tu->m_CurFuncBreakLabel = curBrkLbl;
			tu->m_CurFuncContinueLabel = curContLbl;
		}
		jcc_tuLeaveScope(ctx, tu);

		node = jcc_astAllocStmtFor(ctx, newBrkLbl, newContLbl, bodyLbl, initStmt, condExpr, incExpr, bodyStmt, *tokenListPtr);
		if (!node) {
			jcc_logError(ctx, JCC_SOURCE_LOCATION_CUR(), "Internal Error: Memory allocation failed.");
			return NULL;
		}
	} else if (jcc_tokExpect(&tok, JCC_TOKEN_WHILE)) {
		if (!jcc_tokExpect(&tok, JCC_TOKEN_OPEN_PAREN)) {
			jcc_logError(ctx, &tok->m_Loc, "Expected '(' after 'while'");
			return NULL;
		}
		
		jx_cc_ast_expr_t* condExpr = jcc_parseExpression(ctx, tu, &tok);
		if (!condExpr) {
			jcc_logError(ctx, &tok->m_Loc, "Expected expression after '('");
			return NULL;
		}

		if (!jcc_tokExpect(&tok, JCC_TOKEN_CLOSE_PAREN)) {
			jcc_logError(ctx, &tok->m_Loc, "Expected ')' after expression");
			return NULL;
		}
		
		jx_cc_label_t newBrkLbl = jcc_tuGenerateUniqueLabel(ctx, tu);
		jx_cc_label_t newContLbl = jcc_tuGenerateUniqueLabel(ctx, tu);
		jx_cc_label_t bodyLbl = jcc_tuGenerateUniqueLabel(ctx, tu);

		jx_cc_label_t curBrkLbl = tu->m_CurFuncBreakLabel;
		jx_cc_label_t curContLbl = tu->m_CurFuncContinueLabel;
		tu->m_CurFuncBreakLabel = newBrkLbl;
		tu->m_CurFuncContinueLabel = newContLbl;
		
		jx_cc_ast_stmt_t* bodyStmt = jcc_parseStatement(ctx, tu, &tok);
		if (!bodyStmt) {
			jcc_logError(ctx, &tok->m_Loc, "Expected statement");
			return NULL;
		}
		
		tu->m_CurFuncBreakLabel = curBrkLbl;
		tu->m_CurFuncContinueLabel = curContLbl;

		node = jcc_astAllocStmtFor(ctx, newBrkLbl, newContLbl, bodyLbl, NULL, condExpr, NULL, bodyStmt, *tokenListPtr);
		if (!node) {
			jcc_logError(ctx, JCC_SOURCE_LOCATION_CUR(), "Internal Error: Memory allocation failed.");
			return NULL;
		}
	} else if (jcc_tokExpect(&tok, JCC_TOKEN_DO)) {
		jx_cc_label_t newBrkLbl = jcc_tuGenerateUniqueLabel(ctx, tu);
		jx_cc_label_t newContLbl = jcc_tuGenerateUniqueLabel(ctx, tu);
		jx_cc_label_t bodyLbl = jcc_tuGenerateUniqueLabel(ctx, tu);

		jx_cc_label_t curBrkLbl = tu->m_CurFuncBreakLabel;
		jx_cc_label_t curContLbl = tu->m_CurFuncContinueLabel;
		tu->m_CurFuncBreakLabel = newBrkLbl;
		tu->m_CurFuncContinueLabel = newContLbl;
		
		jx_cc_ast_stmt_t* bodyStmt = jcc_parseStatement(ctx, tu, &tok);
		if (!bodyStmt) {
			jcc_logError(ctx, &tok->m_Loc, "Expected statement after 'do'");
			return NULL;
		}
		
		tu->m_CurFuncBreakLabel = curBrkLbl;
		tu->m_CurFuncContinueLabel = curContLbl;
		
		if (!jcc_tokExpect(&tok, JCC_TOKEN_WHILE)) {
			jcc_logError(ctx, &tok->m_Loc, "Expected 'while' after loop body");
			return NULL;
		}

		if (!jcc_tokExpect(&tok, JCC_TOKEN_OPEN_PAREN)) {
			jcc_logError(ctx, &tok->m_Loc, "Expected '(' after 'while'");
			return NULL;
		}

		jx_cc_ast_expr_t* condExpr = jcc_parseExpression(ctx, tu, &tok);
		if (!condExpr) {
			jcc_logError(ctx, &tok->m_Loc, "Expected expression after '('");
			return NULL;
		}

		if (!jcc_tokExpect(&tok, JCC_TOKEN_CLOSE_PAREN)) {
			jcc_logError(ctx, &tok->m_Loc, "Expected ')' after expression");
			return NULL;
		}

		if (!jcc_tokExpect(&tok, JCC_TOKEN_SEMICOLON)) {
			jcc_logError(ctx, &tok->m_Loc, "Expected ';' after ')'");
			return NULL;
		}

		node = jcc_astAllocStmtDo(ctx, newBrkLbl, newContLbl, bodyLbl, condExpr, bodyStmt, *tokenListPtr);
		if (!node) {
			jcc_logError(ctx, JCC_SOURCE_LOCATION_CUR(), "Internal Error: Memory allocation failed.");
			return NULL;
		}
	} else if (jcc_tokExpect(&tok, JCC_TOKEN_ASM)) {
		node = jcc_parseAsmStatement(ctx, tu, &tok);
		if (!node) {
			jcc_logError(ctx, &tok->m_Loc, "Failed to parse asm statement");
			return NULL;
		}
	} else if (jcc_tokExpect(&tok, JCC_TOKEN_GOTO)) {
		if (!jcc_tokIs(tok, JCC_TOKEN_IDENTIFIER)) {
			jcc_logError(ctx, &tok->m_Loc, "Expected identifier after 'goto'");
			return NULL;
		}

		const char* lbl = jcc_tokGetIdentifier(ctx, tok);
		tok = tok->m_Next;

		if (!jcc_tokExpect(&tok, JCC_TOKEN_SEMICOLON)) {
			jcc_logError(ctx, &tok->m_Loc, "Expected ';' after identifier");
			return NULL;
		}

		node = jcc_astAllocStmtGoto(ctx, lbl, *tokenListPtr);
		if (!node) {
			jcc_logError(ctx, JCC_SOURCE_LOCATION_CUR(), "Internal Error: Memory allocation failed.");
			return NULL;
		}

		jcc_tuFuncAddGoto(ctx, tu, node);
	} else if (jcc_tokExpect(&tok, JCC_TOKEN_BREAK)) {
		if (tu->m_CurFuncBreakLabel.m_ID == 0) {
			jcc_logError(ctx, &tok->m_Loc, "Stray break statement");
			return NULL;
		}

		if (!jcc_tokExpect(&tok, JCC_TOKEN_SEMICOLON)) {
			jcc_logError(ctx, &tok->m_Loc, "Expected ';' after 'break'");
			return NULL;
		}

		node = jcc_astAllocStmtBreakContinue(ctx, tu->m_CurFuncBreakLabel, *tokenListPtr);
		if (!node) {
			jcc_logError(ctx, JCC_SOURCE_LOCATION_CUR(), "Internal Error: Memory allocation failed.");
			return NULL;
		}
	} else if (jcc_tokExpect(&tok, JCC_TOKEN_CONTINUE)) {
		if (tu->m_CurFuncContinueLabel.m_ID == 0) {
			jcc_logError(ctx, &tok->m_Loc, "Stray continue statement.");
			return NULL;
		}

		if (!jcc_tokExpect(&tok, JCC_TOKEN_SEMICOLON)) {
			jcc_logError(ctx, &tok->m_Loc, "Expected ';' after 'continue'");
			return NULL;
		}

		node = jcc_astAllocStmtBreakContinue(ctx, tu->m_CurFuncContinueLabel, *tokenListPtr);
		if (!node) {
			jcc_logError(ctx, JCC_SOURCE_LOCATION_CUR(), "Internal Error: Memory allocation failed.");
			return NULL;
		}
	} else if (tok->m_Kind == JCC_TOKEN_IDENTIFIER && jcc_tokIs(tok->m_Next, JCC_TOKEN_COLON)) {
		const char* lbl = jcc_tokGetIdentifier(ctx, tok);
		jx_cc_label_t uniqueLbl = jcc_tuGenerateUniqueLabel(ctx, tu);

		tok = tok->m_Next->m_Next; // Skip identifier + ':'

		jx_cc_ast_stmt_t* stmt = jcc_parseStatement(ctx, tu, &tok);
		if (!stmt) {
			jcc_logError(ctx, &tok->m_Loc, "Expected statement after ':'");
			return NULL;
		}

		node = jcc_astAllocStmtLabel(ctx, lbl, uniqueLbl, stmt, tok);
		if (!node) {
			jcc_logError(ctx, JCC_SOURCE_LOCATION_CUR(), "Internal Error: Memory allocation failed.");
			return NULL;
		}

		jcc_tuFuncAddLabel(ctx, tu, node);
	} else if (jcc_tokExpect(&tok, JCC_TOKEN_OPEN_CURLY_BRACKET)) {
		node = jcc_parseCompoundStatement(ctx, tu, &tok);
		if (!node) {
			jcc_logError(ctx, &tok->m_Loc, "Failed to parse compound statement");
			return NULL;
		}
	} else {
		node = jcc_parseExpressionStatement(ctx, tu, &tok);
		if (!node) {
			jcc_logError(ctx, &tok->m_Loc, "Failed to parse expression statement");
			return NULL;
		}
	}
	
	JX_CHECK(node != NULL, "Expected valid node at this point.");

	*tokenListPtr = tok;

	return node;
}

// compound-stmt = (typedef | declaration | stmt)* "}"
static jx_cc_ast_stmt_t* jcc_parseCompoundStatement(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_token_t** tokenListPtr)
{
	jx_cc_token_t* tok = *tokenListPtr;

	jx_cc_ast_node_t** childArr = (jx_cc_ast_node_t**)jx_array_create(ctx->m_Allocator);
	if (!childArr) {
		jcc_logError(ctx, JCC_SOURCE_LOCATION_CUR(), "Internal Error: Memory allocation failed.");
		return NULL;
	}

	if (!jcc_tuEnterScope(ctx, tu)) {
		jcc_logError(ctx, JCC_SOURCE_LOCATION_CUR(), "Internal Error: Memory allocation failed.");
		jx_array_free(childArr);
		return NULL;
	}
	{
		while (!jcc_tokExpect(&tok, JCC_TOKEN_CLOSE_CURLY_BRACKET)) {
			jx_cc_ast_stmt_t* child = NULL;

			if (jcc_tuIsTypename(ctx, tu, tok) && !jcc_tokIs(tok->m_Next, JCC_TOKEN_COLON)) {
				jcc_var_attr_t attr = { 0 };
				jx_cc_type_t* basety = jcc_parseDeclarationSpecifiers(ctx, tu, &tok, &attr);
				if (!basety) {
					jcc_logError(ctx, &tok->m_Loc, "Expected declaration specifiers");
					jx_array_free(childArr);
					return NULL;
				}

				if ((attr.m_Flags & JCC_VAR_ATTR_IS_TYPEDEF_Msk) != 0) {
					if (!jcc_parseTypedef(ctx, tu, &tok, basety)) {
						jcc_logError(ctx, &tok->m_Loc, "Failed to parse typedef");
						jx_array_free(childArr);
						return NULL;
					}

					continue;
				}

				if (jcc_isFunction(ctx, tu, tok)) {
					if (!jcc_parseFunction(ctx, tu, &tok, basety, &attr)) {
						jcc_logError(ctx, &tok->m_Loc, "Failed to parse function");
						jx_array_free(childArr);
						return NULL;
					}

					continue;
				}

				if ((attr.m_Flags & JCC_VAR_ATTR_IS_EXTERN_Msk) != 0) {
					if (!jcc_parseGlobalVariable(ctx, tu, &tok, basety, &attr)) {
						jcc_logError(ctx, &tok->m_Loc, "Failed to parse global variable");
						jx_array_free(childArr);
						return NULL;
					}

					continue;
				}

				child = jcc_parseDeclaration(ctx, tu, &tok, basety, &attr);
				if (!child) {
					jcc_logError(ctx, &tok->m_Loc, "Expected declaration");
					jx_array_free(childArr);
					return NULL;
				}
			} else {
				child = jcc_parseStatement(ctx, tu, &tok);
				if (!child) {
					jcc_logError(ctx, &tok->m_Loc, "Expected statement");
					jx_array_free(childArr);
					return NULL;
				}
			}

			if (!child) {
				jx_array_free(childArr);
				return NULL;
			}

			jx_array_push_back(childArr, &child->super);

			if (!jcc_astAddType(ctx, &child->super)) {
				jcc_logError(ctx, JCC_SOURCE_LOCATION_CUR(), "Internal Error: Failed to add type to node.");
				jx_array_free(childArr);
				return NULL;
			}
		}
	}
	jcc_tuLeaveScope(ctx, tu);

	jx_cc_ast_stmt_t* block = jcc_astAllocStmtBlock(ctx, childArr, jx_array_sizeu(childArr), tok);

	jx_array_free(childArr);

	*tokenListPtr = tok;
	
	return block;
}

// expr-stmt = expr? ";"
static jx_cc_ast_stmt_t* jcc_parseExpressionStatement(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_token_t** tokenListPtr)
{
	jx_cc_token_t* tok = *tokenListPtr;

	jx_cc_ast_stmt_t* node = NULL;

	if (jcc_tokExpect(&tok, JCC_TOKEN_SEMICOLON)) {
		node = jcc_astAllocStmtBlock(ctx, NULL, 0, tok);
	} else {
		jx_cc_ast_expr_t* expr = jcc_parseExpression(ctx, tu, &tok);
		if (!expr) {
			jcc_logError(ctx, &tok->m_Loc, "Failed to parse expression");
			return NULL;
		}

		if (!jcc_tokExpect(&tok, JCC_TOKEN_SEMICOLON)) {
			jcc_logError(ctx, &tok->m_Loc, "Expected ';'");
			return NULL;
		}

		node = jcc_astAllocStmtExpr(ctx, expr, tok);
	}

	if (!node) {
		return NULL;
	}

	*tokenListPtr = tok;
	
	return node;
}

// expr = assign ("," expr)?
static jx_cc_ast_expr_t* jcc_parseExpression(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_token_t** tokenListPtr)
{
	jx_cc_token_t* tok = *tokenListPtr;

	jx_cc_ast_expr_t* node = jcc_parseAssignment(ctx, tu, &tok);

	if (jcc_tokExpect(&tok, JCC_TOKEN_COMMA)) {
		jx_cc_ast_expr_t* expr = jcc_parseExpression(ctx, tu, &tok);

		node = jcc_astAllocExprComma(ctx, node, expr, tok);
	}

	if (!node) {
		return NULL;
	}

	*tokenListPtr = tok;
	
	return node;
}

static int64_t jcc_astEvalConstExpression(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_ast_expr_t* node, bool* err)
{
	return jcc_astEvalConstExpression2(ctx, tu, node, NULL, err);
}

// Evaluate a given node as a constant expression.
//
// A constant expression is either just a number or ptr + n where ptr
// is a pointer to a global variable and n is a postiive/negative
// number. The latter form is accepted only as an initialization
// expression for a global variable.
static int64_t jcc_astEvalConstExpression2(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_ast_expr_t* node, char*** label, bool* err)
{
	if (!jcc_astAddType(ctx, &node->super)) {
		jcc_logError(ctx, JCC_SOURCE_LOCATION_CUR(), "Internal Error: Failed to add type to expression node.");
		*err = true;
		return INT64_MAX;
	}
	
	if (jx_cc_typeIsFloat(node->m_Type)) {
		return (int64_t)jcc_astEvalConstExpressionDouble(ctx, tu, node, err);
	}
	
	switch (node->super.m_Kind) {
	case JCC_NODE_EXPR_ADD: {
		jx_cc_ast_expr_binary_t* binaryNode = (jx_cc_ast_expr_binary_t*)node;
		return jcc_astEvalConstExpression2(ctx, tu, binaryNode->m_ExprLHS, label, err) + jcc_astEvalConstExpression(ctx, tu, binaryNode->m_ExprRHS, err);
	} break;
	case JCC_NODE_EXPR_SUB: {
		jx_cc_ast_expr_binary_t* binaryNode = (jx_cc_ast_expr_binary_t*)node;
		return jcc_astEvalConstExpression2(ctx, tu, binaryNode->m_ExprLHS, label, err) - jcc_astEvalConstExpression(ctx, tu, binaryNode->m_ExprRHS, err);
	} break;
	case JCC_NODE_EXPR_MUL: {
		jx_cc_ast_expr_binary_t* binaryNode = (jx_cc_ast_expr_binary_t*)node;
		return jcc_astEvalConstExpression(ctx, tu, binaryNode->m_ExprLHS, err) * jcc_astEvalConstExpression(ctx, tu, binaryNode->m_ExprRHS, err);
	} break;
	case JCC_NODE_EXPR_DIV: {
		jx_cc_ast_expr_binary_t* binaryNode = (jx_cc_ast_expr_binary_t*)node;

		if ((node->m_Type->m_Flags & JCC_TYPE_FLAGS_IS_UNSIGNED_Msk) != 0) {
			return (uint64_t)jcc_astEvalConstExpression(ctx, tu, binaryNode->m_ExprLHS, err) / jcc_astEvalConstExpression(ctx, tu, binaryNode->m_ExprRHS, err);
		}

		return jcc_astEvalConstExpression(ctx, tu, binaryNode->m_ExprLHS, err) / jcc_astEvalConstExpression(ctx, tu, binaryNode->m_ExprRHS, err);
	} break;
	case JCC_NODE_EXPR_NEG: {
		jx_cc_ast_expr_unary_t* unaryNode = (jx_cc_ast_expr_unary_t*)node;
		return -jcc_astEvalConstExpression(ctx, tu, unaryNode->m_Expr, err);
	} break;
	case JCC_NODE_EXPR_MOD: {
		jx_cc_ast_expr_binary_t* binaryNode = (jx_cc_ast_expr_binary_t*)node;

		if ((node->m_Type->m_Flags & JCC_TYPE_FLAGS_IS_UNSIGNED_Msk) != 0) {
			return (uint64_t)jcc_astEvalConstExpression(ctx, tu, binaryNode->m_ExprLHS, err) % jcc_astEvalConstExpression(ctx, tu, binaryNode->m_ExprRHS, err);
		}
		return jcc_astEvalConstExpression(ctx, tu, binaryNode->m_ExprLHS, err) % jcc_astEvalConstExpression(ctx, tu, binaryNode->m_ExprRHS, err);
	} break;
	case JCC_NODE_EXPR_BITWISE_AND: {
		jx_cc_ast_expr_binary_t* binaryNode = (jx_cc_ast_expr_binary_t*)node;
		return jcc_astEvalConstExpression(ctx, tu, binaryNode->m_ExprLHS, err) & jcc_astEvalConstExpression(ctx, tu, binaryNode->m_ExprRHS, err);
	} break;
	case JCC_NODE_EXPR_BITWISE_OR: {
		jx_cc_ast_expr_binary_t* binaryNode = (jx_cc_ast_expr_binary_t*)node;
		return jcc_astEvalConstExpression(ctx, tu, binaryNode->m_ExprLHS, err) | jcc_astEvalConstExpression(ctx, tu, binaryNode->m_ExprRHS, err);
	} break;
	case JCC_NODE_EXPR_BITWISE_XOR: {
		jx_cc_ast_expr_binary_t* binaryNode = (jx_cc_ast_expr_binary_t*)node;
		return jcc_astEvalConstExpression(ctx, tu, binaryNode->m_ExprLHS, err) ^ jcc_astEvalConstExpression(ctx, tu, binaryNode->m_ExprRHS, err);
	} break;
	case JCC_NODE_EXPR_LSHIFT: {
		jx_cc_ast_expr_binary_t* binaryNode = (jx_cc_ast_expr_binary_t*)node;
		return jcc_astEvalConstExpression(ctx, tu, binaryNode->m_ExprLHS, err) << jcc_astEvalConstExpression(ctx, tu, binaryNode->m_ExprRHS, err);
	} break;
	case JCC_NODE_EXPR_RSHIFT: {
		jx_cc_ast_expr_binary_t* binaryNode = (jx_cc_ast_expr_binary_t*)node;

		if ((node->m_Type->m_Flags & JCC_TYPE_FLAGS_IS_UNSIGNED_Msk) != 0 && node->m_Type->m_Size == 8) {
			return (uint64_t)jcc_astEvalConstExpression(ctx, tu, binaryNode->m_ExprLHS, err) >> jcc_astEvalConstExpression(ctx, tu, binaryNode->m_ExprRHS, err);
		}
		return jcc_astEvalConstExpression(ctx, tu, binaryNode->m_ExprLHS, err) >> jcc_astEvalConstExpression(ctx, tu, binaryNode->m_ExprRHS, err);
	} break;
	case JCC_NODE_EXPR_EQUAL: {
		jx_cc_ast_expr_binary_t* binaryNode = (jx_cc_ast_expr_binary_t*)node;
		return jcc_astEvalConstExpression(ctx, tu, binaryNode->m_ExprLHS, err) == jcc_astEvalConstExpression(ctx, tu, binaryNode->m_ExprRHS, err);
	} break;
	case JCC_NODE_EXPR_NOT_EQUAL: {
		jx_cc_ast_expr_binary_t* binaryNode = (jx_cc_ast_expr_binary_t*)node;
		return jcc_astEvalConstExpression(ctx, tu, binaryNode->m_ExprLHS, err) != jcc_astEvalConstExpression(ctx, tu, binaryNode->m_ExprRHS, err);
	} break;
	case JCC_NODE_EXPR_LESS_THAN: {
		jx_cc_ast_expr_binary_t* binaryNode = (jx_cc_ast_expr_binary_t*)node;

		if ((binaryNode->m_ExprLHS->m_Type->m_Flags & JCC_TYPE_FLAGS_IS_UNSIGNED_Msk) != 0) {
			return (uint64_t)jcc_astEvalConstExpression(ctx, tu, binaryNode->m_ExprLHS, err) < (uint64_t)jcc_astEvalConstExpression(ctx, tu, binaryNode->m_ExprRHS, err);
		}
		return jcc_astEvalConstExpression(ctx, tu, binaryNode->m_ExprLHS, err) < jcc_astEvalConstExpression(ctx, tu, binaryNode->m_ExprRHS, err);
	} break;
	case JCC_NODE_EXPR_LESS_EQUAL: {
		jx_cc_ast_expr_binary_t* binaryNode = (jx_cc_ast_expr_binary_t*)node;

		if ((binaryNode->m_ExprLHS->m_Type->m_Flags & JCC_TYPE_FLAGS_IS_UNSIGNED_Msk) != 0) {
			return (uint64_t)jcc_astEvalConstExpression(ctx, tu, binaryNode->m_ExprLHS, err) <= (uint64_t)jcc_astEvalConstExpression(ctx, tu, binaryNode->m_ExprRHS, err);
		}
		return jcc_astEvalConstExpression(ctx, tu, binaryNode->m_ExprLHS, err) <= jcc_astEvalConstExpression(ctx, tu, binaryNode->m_ExprRHS, err);
	} break;
	case JCC_NODE_EXPR_CONDITIONAL: {
		jx_cc_ast_expr_cond_t* condNode = (jx_cc_ast_expr_cond_t*)node;

		return jcc_astEvalConstExpression(ctx, tu, condNode->m_CondExpr, err)
			? jcc_astEvalConstExpression2(ctx, tu, condNode->m_ThenExpr, label, err)
			: jcc_astEvalConstExpression2(ctx, tu, condNode->m_ElseExpr, label, err);
	} break;
	case JCC_NODE_EXPR_COMMA: {
		jx_cc_ast_expr_binary_t* binaryNode = (jx_cc_ast_expr_binary_t*)node;
		return jcc_astEvalConstExpression2(ctx, tu, binaryNode->m_ExprRHS, label, err);
	} break;
	case JCC_NODE_EXPR_NOT: {
		jx_cc_ast_expr_unary_t* unaryNode = (jx_cc_ast_expr_unary_t*)node;
		return !jcc_astEvalConstExpression(ctx, tu, unaryNode->m_Expr, err);
	} break;
	case JCC_NODE_EXPR_BITWISE_NOT: {
		jx_cc_ast_expr_unary_t* unaryNode = (jx_cc_ast_expr_unary_t*)node;
		return ~jcc_astEvalConstExpression(ctx, tu, unaryNode->m_Expr, err);
	} break;
	case JCC_NODE_EXPR_LOGICAL_AND: {
		jx_cc_ast_expr_binary_t* binaryNode = (jx_cc_ast_expr_binary_t*)node;
		return jcc_astEvalConstExpression(ctx, tu, binaryNode->m_ExprLHS, err) && jcc_astEvalConstExpression(ctx, tu, binaryNode->m_ExprRHS, err);
	} break;
	case JCC_NODE_EXPR_LOGICAL_OR: {
		jx_cc_ast_expr_binary_t* binaryNode = (jx_cc_ast_expr_binary_t*)node;
		return jcc_astEvalConstExpression(ctx, tu, binaryNode->m_ExprLHS, err) || jcc_astEvalConstExpression(ctx, tu, binaryNode->m_ExprRHS, err);
	} break;
	case JCC_NODE_EXPR_CAST: {
		jx_cc_ast_expr_unary_t* unaryNode = (jx_cc_ast_expr_unary_t*)node;
		int64_t val = jcc_astEvalConstExpression2(ctx, tu, unaryNode->m_Expr, label, err);
		if (jx_cc_typeIsInteger(node->m_Type)) {
			switch (node->m_Type->m_Size) {
			case 1: return (node->m_Type->m_Flags & JCC_TYPE_FLAGS_IS_UNSIGNED_Msk) != 0 ? (uint8_t)val : (int8_t)val;
			case 2: return (node->m_Type->m_Flags & JCC_TYPE_FLAGS_IS_UNSIGNED_Msk) != 0 ? (uint16_t)val : (int16_t)val;
			case 4: return (node->m_Type->m_Flags & JCC_TYPE_FLAGS_IS_UNSIGNED_Msk) != 0 ? (uint32_t)val : (int32_t)val;
			}
		}
		return val;
	} break;
	case JCC_NODE_EXPR_ADDR: {
		jx_cc_ast_expr_unary_t* unaryNode = (jx_cc_ast_expr_unary_t*)node;
		return jcc_astEvalRValue(ctx, tu, unaryNode->m_Expr, label, err);
	} break;
	case JCC_NODE_EXPR_MEMBER: {
		if (!label) {
			jcc_logError(ctx, &node->super.m_Token->m_Loc, "Not a compile-time constant");
			*err = true;
			return 0;
		}

		if (node->m_Type->m_Kind != JCC_TYPE_ARRAY) {
			jcc_logError(ctx, &node->super.m_Token->m_Loc, "Invalid initializer");
			*err = true;
			return 0;
		}

		jx_cc_ast_expr_member_t* memberNode = (jx_cc_ast_expr_member_t*)node;
		return jcc_astEvalRValue(ctx, tu, memberNode->m_Expr, label, err) + memberNode->m_Member->m_Offset;
	} break;
	case JCC_NODE_VARIABLE:
		if (!label) {
			jcc_logError(ctx, &node->super.m_Token->m_Loc, "Not a compile-time constant");
			*err = true;
			return 0;
		}

		jx_cc_ast_expr_variable_t* varNode = (jx_cc_ast_expr_variable_t*)node;
		jx_cc_object_t* var = varNode->m_Var;
		if (var->m_Type->m_Kind != JCC_TYPE_ARRAY && var->m_Type->m_Kind != JCC_TYPE_FUNC) {
			jcc_logError(ctx, &node->super.m_Token->m_Loc, "Invalid initializer");
			*err = true;
			return 0;
		}

		*label = &var->m_Name;

		return 0;
	case JCC_NODE_NUMBER:
		return ((jx_cc_ast_expr_iconst_t*)node)->m_Value;
	}

	jcc_logError(ctx, &node->super.m_Token->m_Loc, "Not a compile-time constant");
	*err = true;

	return 0;
}

static int64_t jcc_astEvalRValue(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_ast_expr_t* node, char*** label, bool* err)
{
	switch (node->super.m_Kind) {
	case JCC_NODE_VARIABLE: {
		jx_cc_ast_expr_variable_t* varNode = (jx_cc_ast_expr_variable_t*)node;

		if ((varNode->m_Var->m_Flags & JCC_OBJECT_FLAGS_IS_LOCAL_Msk) != 0) {
			*err = true;
			jcc_logError(ctx, &node->super.m_Token->m_Loc, "Not a compile-time constant");
			return 0;
		}

		*label = &varNode->m_Var->m_Name;

		return 0;
	} break;
	case JCC_NODE_EXPR_DEREF: {
		jx_cc_ast_expr_unary_t* unaryNode = (jx_cc_ast_expr_unary_t*)node;
		return jcc_astEvalConstExpression2(ctx, tu, unaryNode->m_Expr, label, err);
	} break;
	case JCC_NODE_EXPR_MEMBER: {
		jx_cc_ast_expr_member_t* memberNode = (jx_cc_ast_expr_member_t*)node;
		return jcc_astEvalRValue(ctx, tu, memberNode->m_Expr, label, err) + memberNode->m_Member->m_Offset;
	} break;
	}
	
	*err = true;
	jcc_logError(ctx, &node->super.m_Token->m_Loc, "Invalid initializer");
	return 0;
}

static bool jcc_astIsConstExpression(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_ast_expr_t* node)
{
	if (!jcc_astAddType(ctx, &node->super)) {
		jcc_logError(ctx, &node->super.m_Token->m_Loc, "Failed to add type to expression node.");
		return false;
	}
	
	switch (node->super.m_Kind) {
	case JCC_NODE_EXPR_ADD:
	case JCC_NODE_EXPR_SUB:
	case JCC_NODE_EXPR_MUL:
	case JCC_NODE_EXPR_DIV:
	case JCC_NODE_EXPR_BITWISE_AND:
	case JCC_NODE_EXPR_BITWISE_OR:
	case JCC_NODE_EXPR_BITWISE_XOR:
	case JCC_NODE_EXPR_LSHIFT:
	case JCC_NODE_EXPR_RSHIFT:
	case JCC_NODE_EXPR_EQUAL:
	case JCC_NODE_EXPR_NOT_EQUAL:
	case JCC_NODE_EXPR_LESS_THAN:
	case JCC_NODE_EXPR_LESS_EQUAL:
	case JCC_NODE_EXPR_LOGICAL_AND:
	case JCC_NODE_EXPR_LOGICAL_OR: {
		jx_cc_ast_expr_binary_t* binaryNode = (jx_cc_ast_expr_binary_t*)node;
		return true
			&& jcc_astIsConstExpression(ctx, tu, binaryNode->m_ExprLHS)
			&& jcc_astIsConstExpression(ctx, tu, binaryNode->m_ExprRHS)
			;
	} break;
	case JCC_NODE_EXPR_CONDITIONAL: {
		jx_cc_ast_expr_cond_t* condNode = (jx_cc_ast_expr_cond_t*)node;
		if (!jcc_astIsConstExpression(ctx, tu, condNode->m_CondExpr)) {
			return false;
		}

		bool err = false;
		const int64_t condVal = jcc_astEvalConstExpression(ctx, tu, condNode->m_CondExpr, &err);
		if (err) {
			return false;
		}

		return jcc_astIsConstExpression(ctx, tu, condVal ? condNode->m_ThenExpr : condNode->m_ElseExpr);
	} break;
	case JCC_NODE_EXPR_COMMA: {
		jx_cc_ast_expr_binary_t* binaryNode = (jx_cc_ast_expr_binary_t*)node;
		return jcc_astIsConstExpression(ctx, tu, binaryNode->m_ExprRHS);
	} break;
	case JCC_NODE_EXPR_NEG:
	case JCC_NODE_EXPR_NOT:
	case JCC_NODE_EXPR_BITWISE_NOT:
	case JCC_NODE_EXPR_CAST: {
		jx_cc_ast_expr_unary_t* unaryNode = (jx_cc_ast_expr_unary_t*)node;
		return jcc_astIsConstExpression(ctx, tu, unaryNode->m_Expr);
	} break;
	case JCC_NODE_NUMBER:
		return true;
	}
	
	return false;
}

static int64_t jcc_parseConstExpression(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_token_t** tokenListPtr, bool* err)
{
	jx_cc_token_t* tok = *tokenListPtr;

	jx_cc_ast_expr_t* node = jcc_parseConditional(ctx, tu, &tok);
	if (!node) {
		jcc_logError(ctx, &tok->m_Loc, "Failed to parse constant expression.");
		*err = true;
		return 0;
	}

	const int64_t val = jcc_astEvalConstExpression(ctx, tu, node, err);
	if (*err) {
		jcc_logError(ctx, &tok->m_Loc, "Failed to evaluate constant expression.");
		return 0;
	}

	*tokenListPtr = tok;

	return val;
}

static double jcc_astEvalConstExpressionDouble(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_ast_expr_t* node, bool* err)
{
	if (!jcc_astAddType(ctx, &node->super)) {
		jcc_logError(ctx, &node->super.m_Token->m_Loc, "Internal Error: Failed to add type to expression node.");
		*err = true;
		return 0.0;
	}

	if (jx_cc_typeIsInteger(node->m_Type)) {
		if ((node->m_Type->m_Flags & JCC_TYPE_FLAGS_IS_UNSIGNED_Msk) != 0) {
			return (uint32_t)jcc_astEvalConstExpression(ctx, tu, node, err);
		}
		
		return (double)jcc_astEvalConstExpression(ctx, tu, node, err);
	}
	
	switch (node->super.m_Kind) {
	case JCC_NODE_EXPR_ADD: {
		jx_cc_ast_expr_binary_t* binaryNode = (jx_cc_ast_expr_binary_t*)node;
		return jcc_astEvalConstExpressionDouble(ctx, tu, binaryNode->m_ExprLHS, err) + jcc_astEvalConstExpressionDouble(ctx, tu, binaryNode->m_ExprRHS, err);
	} break;
	case JCC_NODE_EXPR_SUB: {
		jx_cc_ast_expr_binary_t* binaryNode = (jx_cc_ast_expr_binary_t*)node;
		return jcc_astEvalConstExpressionDouble(ctx, tu, binaryNode->m_ExprLHS, err) - jcc_astEvalConstExpressionDouble(ctx, tu, binaryNode->m_ExprRHS, err);
	} break;
	case JCC_NODE_EXPR_MUL: {
		jx_cc_ast_expr_binary_t* binaryNode = (jx_cc_ast_expr_binary_t*)node;
		return jcc_astEvalConstExpressionDouble(ctx, tu, binaryNode->m_ExprLHS, err) * jcc_astEvalConstExpressionDouble(ctx, tu, binaryNode->m_ExprRHS, err);
	} break;
	case JCC_NODE_EXPR_DIV: {
		jx_cc_ast_expr_binary_t* binaryNode = (jx_cc_ast_expr_binary_t*)node;
		return jcc_astEvalConstExpressionDouble(ctx, tu, binaryNode->m_ExprLHS, err) / jcc_astEvalConstExpressionDouble(ctx, tu, binaryNode->m_ExprRHS, err);
	} break;
	case JCC_NODE_EXPR_NEG: {
		jx_cc_ast_expr_unary_t* unaryNode = (jx_cc_ast_expr_unary_t*)node;
		return -jcc_astEvalConstExpressionDouble(ctx, tu, unaryNode->m_Expr, err);
	} break;
	case JCC_NODE_EXPR_CONDITIONAL: {
		jx_cc_ast_expr_cond_t* condNode = (jx_cc_ast_expr_cond_t*)node;

		return jcc_astEvalConstExpressionDouble(ctx, tu, condNode->m_CondExpr, err)
			? jcc_astEvalConstExpressionDouble(ctx, tu, condNode->m_ThenExpr, err)
			: jcc_astEvalConstExpressionDouble(ctx, tu, condNode->m_ElseExpr, err)
			;
	} break;
	case JCC_NODE_EXPR_COMMA: {
		jx_cc_ast_expr_binary_t* binaryNode = (jx_cc_ast_expr_binary_t*)node;
		return jcc_astEvalConstExpressionDouble(ctx, tu, binaryNode->m_ExprRHS, err);
	} break;
	case JCC_NODE_EXPR_CAST: {
		jx_cc_ast_expr_unary_t* unaryNode = (jx_cc_ast_expr_unary_t*)node;

		if (jx_cc_typeIsFloat(unaryNode->m_Expr->m_Type)) {
			return jcc_astEvalConstExpressionDouble(ctx, tu, unaryNode->m_Expr, err);
		}

		return (double)jcc_astEvalConstExpression(ctx, tu, unaryNode->m_Expr, err);
	} break;
	case JCC_NODE_NUMBER: {
		jx_cc_ast_expr_fconst_t* constNode = (jx_cc_ast_expr_fconst_t*)node;
		return constNode->m_Value;
	} break;
	}
	
	*err = true;
	jcc_logError(ctx, &node->super.m_Token->m_Loc, "Not a compile-time constant");
	return 0.0;
}

static jx_cc_ast_expr_t* jcc_astConvertToAssign(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_ast_expr_t* expr)
{
	jx_cc_ast_expr_t* node = NULL;

	if (jcc_astNodeIsExprBinary(expr)) {
		jx_cc_ast_expr_binary_t* binary = (jx_cc_ast_expr_binary_t*)expr;
		if (!binary) {
			return NULL;
		}

		if (!jcc_astAddType(ctx, &binary->m_ExprLHS->super)) {
			jcc_logError(ctx, &expr->super.m_Token->m_Loc, "Internal Error: Failed to add type to expression node.");
			return NULL;
		}
		if (!jcc_astAddType(ctx, &binary->m_ExprRHS->super)) {
			jcc_logError(ctx, &expr->super.m_Token->m_Loc, "Internal Error: Failed to add type to expression node.");
			return NULL;
		}

		node = jcc_astAllocExprCompoundAssign(ctx, binary->m_ExprLHS, binary->m_ExprRHS, expr->super.m_Kind, expr->super.m_Token);
	} else if (expr->super.m_Kind == JCC_NODE_EXPR_GET_ELEMENT_PTR) {
		jx_cc_ast_expr_get_element_ptr_t* gep = (jx_cc_ast_expr_get_element_ptr_t*)expr;
		node = jcc_astAllocExprBinary(ctx, JCC_NODE_EXPR_ASSIGN, gep->m_ExprPtr, expr, expr->super.m_Token);
	} else {
		jcc_logError(ctx, &expr->super.m_Token->m_Loc, "Cannot convert expression to assignment.");
	}

	return node;
}

// assign    = conditional (assign-op assign)?
// assign-op = "=" | "+=" | "-=" | "*=" | "/=" | "%=" | "&=" | "|=" | "^="
//           | "<<=" | ">>="
static jx_cc_ast_expr_t* jcc_parseAssignment(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_token_t** tokenListPtr)
{
	jx_cc_token_t* tok = *tokenListPtr;

	jx_cc_ast_expr_t* node = jcc_parseConditional(ctx, tu, &tok);
	if (!node) {
		return NULL;
	}
	
	if (jcc_tokExpect(&tok, JCC_TOKEN_ASSIGN)) {
		node = jcc_astAllocExprBinary(ctx, JCC_NODE_EXPR_ASSIGN, node, jcc_parseAssignment(ctx, tu, &tok), tok);
	} else if (jcc_tokExpect(&tok, JCC_TOKEN_ADD_ASSIGN)) {
		node = jcc_astConvertToAssign(ctx, tu, jcc_astAllocExprAdd(ctx, node, jcc_parseAssignment(ctx, tu, &tok), tok));
	} else if (jcc_tokExpect(&tok, JCC_TOKEN_SUB_ASSIGN)) {
		node = jcc_astConvertToAssign(ctx, tu, jcc_astAllocExprSub(ctx, node, jcc_parseAssignment(ctx, tu, &tok), tok));
	} else if (jcc_tokExpect(&tok, JCC_TOKEN_MUL_ASSIGN)) {
		node = jcc_astConvertToAssign(ctx, tu, jcc_astAllocExprBinary(ctx, JCC_NODE_EXPR_MUL, node, jcc_parseAssignment(ctx, tu, &tok), tok));
	} else if (jcc_tokExpect(&tok, JCC_TOKEN_DIV_ASSIGN)) {
		node = jcc_astConvertToAssign(ctx, tu, jcc_astAllocExprBinary(ctx, JCC_NODE_EXPR_DIV, node, jcc_parseAssignment(ctx, tu, &tok), tok));
	} else if (jcc_tokExpect(&tok, JCC_TOKEN_MOD_ASSIGN)) {
		node = jcc_astConvertToAssign(ctx, tu, jcc_astAllocExprBinary(ctx, JCC_NODE_EXPR_MOD, node, jcc_parseAssignment(ctx, tu, &tok), tok));
	} else if (jcc_tokExpect(&tok, JCC_TOKEN_AND_ASSIGN)) {
		node = jcc_astConvertToAssign(ctx, tu, jcc_astAllocExprBinary(ctx, JCC_NODE_EXPR_BITWISE_AND, node, jcc_parseAssignment(ctx, tu, &tok), tok));
	} else if (jcc_tokExpect(&tok, JCC_TOKEN_OR_ASSIGN)) {
		node = jcc_astConvertToAssign(ctx, tu, jcc_astAllocExprBinary(ctx, JCC_NODE_EXPR_BITWISE_OR, node, jcc_parseAssignment(ctx, tu, &tok), tok));
	} else if (jcc_tokExpect(&tok, JCC_TOKEN_XOR_ASSIGN)) {
		node = jcc_astConvertToAssign(ctx, tu, jcc_astAllocExprBinary(ctx, JCC_NODE_EXPR_BITWISE_XOR, node, jcc_parseAssignment(ctx, tu, &tok), tok));
	} else if (jcc_tokExpect(&tok, JCC_TOKEN_LSHIFT_ASSIGN)) {
		node = jcc_astConvertToAssign(ctx, tu, jcc_astAllocExprBinary(ctx, JCC_NODE_EXPR_LSHIFT, node, jcc_parseAssignment(ctx, tu, &tok), tok));
	} else if (jcc_tokExpect(&tok, JCC_TOKEN_RSHIFT_ASSIGN)) {
		node = jcc_astConvertToAssign(ctx, tu, jcc_astAllocExprBinary(ctx, JCC_NODE_EXPR_RSHIFT, node, jcc_parseAssignment(ctx, tu, &tok), tok));
	}
	
	if (!node) {
		return NULL;
	}

	*tokenListPtr = tok;

	return node;
}

// conditional = logor ("?" expr? ":" conditional)?
static jx_cc_ast_expr_t* jcc_parseConditional(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_token_t** tokenListPtr)
{
	jx_cc_token_t* tok = *tokenListPtr;

	jx_cc_ast_expr_t* node = jcc_parseLogicalOr(ctx, tu, &tok);
	
	if (jcc_tokExpect(&tok, JCC_TOKEN_QUESTIONMARK)) {
#if 0 // GCC extension
		if (jcc_tokIs(tok, JCC_TOKEN_COLON)) {
			// [GNU] Compile `a ?: b` as `tmp = a, tmp ? tmp : b`.
			jcc_astAddType(ctx, cond);
			jx_cc_object_t* var = jcc_tuVarAllocLocal(ctx, tu, "", cond->ty);
			jx_cc_ast_node_t* lhs = jcc_astAllocExprBinary(ctx, JCC_NODE_EXPR_ASSIGN, jcc_astAllocExprVar(ctx, var, tok), cond, tok);
			jx_cc_ast_node_t* rhs = jcc_astAllocNode(ctx, JCC_NODE_EXPR_CONDITIONAL, tok);
			rhs->cond = jcc_astAllocExprVar(ctx, var, tok);
			rhs->then = jcc_astAllocExprVar(ctx, var, tok);
			rhs->els = jcc_parseConditional(ctx, tu, rest, tok->next);
			return jcc_astAllocExprBinary(ctx, JCC_NODE_EXPR_COMMA, lhs, rhs, tok);
		}
#endif

		jx_cc_token_t* conditionalTok = tok;
		jx_cc_ast_expr_t* thenExpr = jcc_parseExpression(ctx, tu, &tok);
		if (!thenExpr) {
			jcc_logError(ctx, &tok->m_Loc, "Expected expression after '?'");
			return NULL;
		}

		if (!jcc_tokExpect(&tok, JCC_TOKEN_COLON)) {
			jcc_logError(ctx, &tok->m_Loc, "Expected ':' after expression");
			return NULL;
		}

		jx_cc_ast_expr_t* elseExpr = jcc_parseConditional(ctx, tu, &tok);
		if (!elseExpr) {
			jcc_logError(ctx, &tok->m_Loc, "Expected expression after ':'");
			return NULL;
		}

		node = jcc_astAllocExprCond(ctx, node, thenExpr, elseExpr, conditionalTok);
	}

	if (!node) {
		return NULL;
	}

	*tokenListPtr = tok;
	
	return node;
}

// logor = logand ("||" logand)*
static jx_cc_ast_expr_t* jcc_parseLogicalOr(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_token_t** tokenListPtr)
{
	jx_cc_token_t* tok = *tokenListPtr;

	jx_cc_ast_expr_t* node = jcc_parseLogicalAnd(ctx, tu, &tok);

	jx_cc_token_t* start = tok;
	while (node != NULL && jcc_tokExpect(&tok, JCC_TOKEN_LOGICAL_OR)) {
		jx_cc_ast_expr_t* otherNode = jcc_parseLogicalAnd(ctx, tu, &tok);
		node = jcc_astAllocExprBinary(ctx, JCC_NODE_EXPR_LOGICAL_OR, node, otherNode, start);
		start = tok;
	}

	if (!node) {
		return NULL;
	}
	
	*tokenListPtr = tok;
	
	return node;
}

// logand = bitor ("&&" bitor)*
static jx_cc_ast_expr_t* jcc_parseLogicalAnd(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_token_t** tokenListPtr)
{
	jx_cc_token_t* tok = *tokenListPtr;

	jx_cc_ast_expr_t* node = jcc_parseBitwiseOr(ctx, tu, &tok);

	jx_cc_token_t* start = tok;
	while (node != NULL && jcc_tokExpect(&tok, JCC_TOKEN_LOGICAL_AND)) {
		jx_cc_ast_expr_t* otherNode = jcc_parseBitwiseOr(ctx, tu, &tok);
		node = jcc_astAllocExprBinary(ctx, JCC_NODE_EXPR_LOGICAL_AND, node, otherNode, start);
		start = tok;
	}

	if (!node) {
		return NULL;
	}
	
	*tokenListPtr = tok;
	
	return node;
}

// bitor = bitxor ("|" bitxor)*
static jx_cc_ast_expr_t* jcc_parseBitwiseOr(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_token_t** tokenListPtr)
{
	jx_cc_token_t* tok = *tokenListPtr;

	jx_cc_ast_expr_t* node = jcc_parseBitwiseXor(ctx, tu, &tok);

	jx_cc_token_t* start = tok;
	while (node != NULL && jcc_tokExpect(&tok, JCC_TOKEN_OR)) {
		jx_cc_ast_expr_t* otherNode = jcc_parseBitwiseXor(ctx, tu, &tok);
		node = jcc_astAllocExprBinary(ctx, JCC_NODE_EXPR_BITWISE_OR, node, otherNode, start);
		start = tok;
	}

	if (!node) {
		return NULL;
	}

	*tokenListPtr = tok;
	
	return node;
}

// bitxor = bitand ("^" bitand)*
static jx_cc_ast_expr_t* jcc_parseBitwiseXor(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_token_t** tokenListPtr)
{
	jx_cc_token_t* tok = *tokenListPtr;

	jx_cc_ast_expr_t* node = jcc_parseBitwiseAnd(ctx, tu, &tok);

	jx_cc_token_t* start = tok;
	while (node != NULL && jcc_tokExpect(&tok, JCC_TOKEN_XOR)) {
		jx_cc_ast_expr_t* otherNode = jcc_parseBitwiseAnd(ctx, tu, &tok);
		node = jcc_astAllocExprBinary(ctx, JCC_NODE_EXPR_BITWISE_XOR, node, otherNode, start);
		start = tok;
	}

	if (!node) {
		return NULL;
	}

	*tokenListPtr = tok;
	
	return node;
}

// bitand = equality ("&" equality)*
static jx_cc_ast_expr_t* jcc_parseBitwiseAnd(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_token_t** tokenListPtr)
{
	jx_cc_token_t* tok = *tokenListPtr;

	jx_cc_ast_expr_t* node = jcc_parseEquality(ctx, tu, &tok);
	
	jx_cc_token_t* start = tok;
	while (node != NULL && jcc_tokExpect(&tok, JCC_TOKEN_AND)) {
		jx_cc_ast_expr_t* otherNode = jcc_parseEquality(ctx, tu, &tok);
		node = jcc_astAllocExprBinary(ctx, JCC_NODE_EXPR_BITWISE_AND, node, otherNode, start);
		start = tok;
	}

	if (!node) {
		return NULL;
	}

	*tokenListPtr = tok;
	
	return node;
}

// equality = relational ("==" relational | "!=" relational)*
static jx_cc_ast_expr_t* jcc_parseEquality(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_token_t** tokenListPtr)
{
	jx_cc_token_t* tok = *tokenListPtr;

	jx_cc_ast_expr_t* node = jcc_parseRelational(ctx, tu, &tok);
	
	while (node != NULL) {
		jx_cc_token_t* start = tok;
		
		if (jcc_tokExpect(&tok, JCC_TOKEN_EQUAL)) {
			jx_cc_ast_expr_t* otherNode = jcc_parseRelational(ctx, tu, &tok);
			node = jcc_astAllocExprBinary(ctx, JCC_NODE_EXPR_EQUAL, node, otherNode, start);
		} else if (jcc_tokExpect(&tok, JCC_TOKEN_NOT_EQUAL)) {
			jx_cc_ast_expr_t* otherNode = jcc_parseRelational(ctx, tu, &tok);
			node = jcc_astAllocExprBinary(ctx, JCC_NODE_EXPR_NOT_EQUAL, node, otherNode, start);
		} else {
			break;
		}
	}

	if (!node) {
		return NULL;
	}

	*tokenListPtr = tok;

	return node;
}

// relational = shift ("<" shift | "<=" shift | ">" shift | ">=" shift)*
static jx_cc_ast_expr_t* jcc_parseRelational(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_token_t** tokenListPtr)
{
	jx_cc_token_t* tok = *tokenListPtr;

	jx_cc_ast_expr_t* node = jcc_parseShift(ctx, tu, &tok);
	
	while (node != NULL) {
		jx_cc_token_t* start = tok;
		
		if (jcc_tokExpect(&tok, JCC_TOKEN_LESS)) {
			jx_cc_ast_expr_t* otherNode = jcc_parseShift(ctx, tu, &tok);
			node = jcc_astAllocExprBinary(ctx, JCC_NODE_EXPR_LESS_THAN, node, otherNode, start);
		} else if (jcc_tokExpect(&tok, JCC_TOKEN_LESS_EQUAL)) {
			jx_cc_ast_expr_t* otherNode = jcc_parseShift(ctx, tu, &tok);
			node = jcc_astAllocExprBinary(ctx, JCC_NODE_EXPR_LESS_EQUAL, node, otherNode, start);
		} else if (jcc_tokExpect(&tok, JCC_TOKEN_GREATER)) {
			jx_cc_ast_expr_t* otherNode = jcc_parseShift(ctx, tu, &tok);
			node = jcc_astAllocExprBinary(ctx, JCC_NODE_EXPR_LESS_THAN, otherNode, node, start);
		} else if (jcc_tokExpect(&tok, JCC_TOKEN_GREATER_EQUAL)) {
			jx_cc_ast_expr_t* otherNode = jcc_parseShift(ctx, tu, &tok);
			node = jcc_astAllocExprBinary(ctx, JCC_NODE_EXPR_LESS_EQUAL, otherNode, node, start);
		} else {
			break;
		}
	}

	if (!node) {
		return NULL;
	}

	*tokenListPtr = tok;

	return node;
}

// shift = add ("<<" add | ">>" add)*
static jx_cc_ast_expr_t* jcc_parseShift(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_token_t** tokenListPtr)
{
	jx_cc_token_t* tok = *tokenListPtr;

	jx_cc_ast_expr_t* node = jcc_parseAdd(ctx, tu, &tok);
	
	while (node != NULL) {
		jx_cc_token_t* start = tok;
		
		if (jcc_tokExpect(&tok, JCC_TOKEN_LSHIFT)) {
			jx_cc_ast_expr_t* otherNode = jcc_parseAdd(ctx, tu, &tok);
			node = jcc_astAllocExprBinary(ctx, JCC_NODE_EXPR_LSHIFT, node, otherNode, start);
		} else if (jcc_tokExpect(&tok, JCC_TOKEN_RSHIFT)) {
			jx_cc_ast_expr_t* otherNode = jcc_parseAdd(ctx, tu, &tok);
			node = jcc_astAllocExprBinary(ctx, JCC_NODE_EXPR_RSHIFT, node, otherNode, start);
		} else {
			break;
		}
	}

	if (!node) {
		return NULL;
	}

	*tokenListPtr = tok;

	return node;
}

// add = mul ("+" mul | "-" mul)*
static jx_cc_ast_expr_t* jcc_parseAdd(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_token_t** tokenListPtr)
{
	jx_cc_token_t* tok = *tokenListPtr;

	jx_cc_ast_expr_t* node = jcc_parseMul(ctx, tu, &tok);

	// This is normally a infinite loop unless an allocation failed or a syntax error occured inside it.
	while (node) {
		jx_cc_token_t* start = tok;
		if (jcc_tokExpect(&tok, JCC_TOKEN_ADD)) {
			node = jcc_astAllocExprAdd(ctx, node, jcc_parseMul(ctx, tu, &tok), start);
		} else if (jcc_tokExpect(&tok, JCC_TOKEN_SUB)) {
			node = jcc_astAllocExprSub(ctx, node, jcc_parseMul(ctx, tu, &tok), start);
		} else {
			break;
		}
	}

	if (!node) {
		return NULL;
	}
		
	*tokenListPtr = tok;
	
	return node;
}

// mul = cast ("*" cast | "/" cast | "%" cast)*
static jx_cc_ast_expr_t* jcc_parseMul(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_token_t** tokenListPtr)
{
	jx_cc_token_t* tok = *tokenListPtr;

	jx_cc_ast_expr_t* node = jcc_parseCast(ctx, tu, &tok);
	
	// This is normally a infinite loop unless an allocation failed or a syntax error occured inside it.
	while (node) {
		jx_cc_token_t* start = tok;

		if (jcc_tokExpect(&tok, JCC_TOKEN_MUL)) {
			node = jcc_astAllocExprBinary(ctx, JCC_NODE_EXPR_MUL, node, jcc_parseCast(ctx, tu, &tok), start);
		} else if (jcc_tokExpect(&tok, JCC_TOKEN_DIV)) {
			node = jcc_astAllocExprBinary(ctx, JCC_NODE_EXPR_DIV, node, jcc_parseCast(ctx, tu, &tok), start);
		} else if (jcc_tokExpect(&tok, JCC_TOKEN_MOD)) {
			node = jcc_astAllocExprBinary(ctx, JCC_NODE_EXPR_MOD, node, jcc_parseCast(ctx, tu, &tok), start);
		} else {
			break;
		}
	}

	if (!node) {
		return NULL;
	}
		
	*tokenListPtr = tok;
		
	return node;
}

// cast = "(" type-name ")" cast | unary
static jx_cc_ast_expr_t* jcc_parseCast(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_token_t** tokenListPtr)
{
	jx_cc_token_t* tok = *tokenListPtr;

	jx_cc_ast_expr_t* node = NULL;

	if (jcc_tokIs(tok, JCC_TOKEN_OPEN_PAREN) && jcc_tuIsTypename(ctx, tu, tok->m_Next)) {
		jx_cc_token_t* start = tok;

		tok = tok->m_Next;

		jx_cc_type_t* ty = jcc_parseTypename(ctx, tu, &tok);
		if (!ty) {
			return NULL;
		}

		if (!jcc_tokExpect(&tok, JCC_TOKEN_CLOSE_PAREN)) {
			return NULL;
		}

		// compound literal
		if (jcc_tokIs(tok, JCC_TOKEN_OPEN_CURLY_BRACKET)) {
			tok = start;

			node = jcc_parseUnary(ctx, tu, &tok);
			if (!node) {
				return NULL;
			}
		} else {
			// type cast
			jx_cc_ast_expr_t* castNode = jcc_parseCast(ctx, tu, &tok);
			if (!castNode) {
				return NULL;
			}

			node = jcc_astAllocExprCast(ctx, castNode, ty);
			if (!node) {
				return NULL;
			}

			node->super.m_Token = start;
		}
	} else {
		node = jcc_parseUnary(ctx, tu, &tok);
	}

	if (!node) {
		return NULL;
	}

	*tokenListPtr = tok;

	return node;
}

// unary = ("+" | "-" | "*" | "&" | "!" | "~") cast
//       | ("++" | "--") unary
//       | "&&" ident
//       | postfix
static jx_cc_ast_expr_t* jcc_parseUnary(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_token_t** tokenListPtr)
{
	jx_cc_token_t* tok = *tokenListPtr;

	jx_cc_ast_expr_t* node = NULL;

	if (jcc_tokExpect(&tok, JCC_TOKEN_ADD)) {
		node = jcc_parseCast(ctx, tu, &tok);
	} else if (jcc_tokExpect(&tok, JCC_TOKEN_SUB)) {
		node = jcc_astAllocExprUnary(ctx, JCC_NODE_EXPR_NEG, jcc_parseCast(ctx, tu, &tok), tok);
	} else if (jcc_tokExpect(&tok, JCC_TOKEN_AND)) {
		jx_cc_ast_expr_t* lhs = jcc_parseCast(ctx, tu, &tok);
		if (!lhs) {
			return NULL;
		}

		if (!jcc_astAddType(ctx, &lhs->super)) {
			return NULL;
		}

		if (lhs->super.m_Kind == JCC_NODE_EXPR_MEMBER && ((jx_cc_ast_expr_member_t*)lhs)->m_Member->m_IsBitfield) {
			return NULL; // ERROR: cannot take address of bitfield
		}
		
		node = jcc_astAllocExprUnary(ctx, JCC_NODE_EXPR_ADDR, lhs, tok);
	} else if (jcc_tokExpect(&tok, JCC_TOKEN_MUL)) {
		// [https://www.sigbus.info/n1570#6.5.3.2p4] This is an oddity
		// in the C spec, but dereferencing a function shouldn't do
		// anything. If foo is a function, `*foo`, `**foo` or `*****foo`
		// are all equivalent to just `foo`.
		node = jcc_parseCast(ctx, tu, &tok);
		if (!node) {
			return NULL;
		}

		if (!jcc_astAddType(ctx, &node->super)) {
			return NULL;
		}
		
		if (node->m_Type->m_Kind != JCC_TYPE_FUNC) {
			node = jcc_astAllocExprUnary(ctx, JCC_NODE_EXPR_DEREF, node, tok);
		}
	} else if (jcc_tokExpect(&tok, JCC_TOKEN_LOGICAL_NOT)) {
		node = jcc_astAllocExprUnary(ctx, JCC_NODE_EXPR_NOT, jcc_parseCast(ctx, tu, &tok), tok);
	} else if (jcc_tokExpect(&tok, JCC_TOKEN_NOT)) {
		node = jcc_astAllocExprUnary(ctx, JCC_NODE_EXPR_BITWISE_NOT, jcc_parseCast(ctx, tu, &tok), tok);
	} else if (jcc_tokExpect(&tok, JCC_TOKEN_INC)) {
		// Read ++i as i+=1
		// TODO: Constant type should be the same as the unary expression's type?
		node = jcc_astConvertToAssign(ctx, tu, jcc_astAllocExprAdd(ctx, jcc_parseUnary(ctx, tu, &tok), jcc_astAllocExprIConst(ctx, 1, kType_int, tok), tok));
	} else if (jcc_tokExpect(&tok, JCC_TOKEN_DEC)) {
		// Read --i as i-=1
		// TODO: Constant type should be the same as the unary expression's type?
		node = jcc_astConvertToAssign(ctx, tu, jcc_astAllocExprSub(ctx, jcc_parseUnary(ctx, tu, &tok), jcc_astAllocExprIConst(ctx, 1, kType_int, tok), tok));
	} else {
		node = jcc_parsePostfix(ctx, tu, &tok);
	}

	if (!node) {
		return NULL;
	}

	*tokenListPtr = tok;

	return node;
}

// struct-members = (declspec declarator (","  declarator)* ";")*
static bool jcc_parseStructMembers(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_token_t** tokenListPtr, jx_cc_type_t* ty)
{
	jx_cc_token_t* tok = *tokenListPtr;

	jx_cc_struct_member_t head = { 0 };
	jx_cc_struct_member_t* cur = &head;
	int idx = 0;
	
	while (!jcc_tokExpect(&tok, JCC_TOKEN_CLOSE_CURLY_BRACKET)) {
		jcc_var_attr_t attr = { 0 };
		jx_cc_type_t* basety = jcc_parseDeclarationSpecifiers(ctx, tu, &tok, &attr);
		if (!basety) {
			return false;
		}
		
		if ((basety->m_Kind == JCC_TYPE_STRUCT || basety->m_Kind == JCC_TYPE_UNION) && jcc_tokExpect(&tok, JCC_TOKEN_SEMICOLON)) {
			// Anonymous struct member
			jx_cc_struct_member_t* mem = (jx_cc_struct_member_t*)JX_ALLOC(ctx->m_LinearAllocator, sizeof(jx_cc_struct_member_t));
			if (!mem) {
				return false;
			}

			jx_memset(mem, 0, sizeof(jx_cc_struct_member_t));
			mem->m_Type = basety;
			mem->m_ID = idx++;
			mem->m_Alignment = attr.m_Align
				? attr.m_Align
				: mem->m_Type->m_Alignment
				;
			cur->m_Next = mem;
			cur = cur->m_Next;
		} else {
			// Regular struct members
			bool first = true;
			while (!jcc_tokExpect(&tok, JCC_TOKEN_SEMICOLON)) {
				if (!first) {
					if (!jcc_tokExpect(&tok, JCC_TOKEN_COMMA)) {
						return false;
					}
				}
				first = false;

				jx_cc_struct_member_t* mem = (jx_cc_struct_member_t*)JX_ALLOC(ctx->m_LinearAllocator, sizeof(jx_cc_struct_member_t));
				if (!mem) {
					return false;
				}

				jx_cc_token_t* baseTypeName = basety->m_DeclName;

				jx_memset(mem, 0, sizeof(jx_cc_struct_member_t));
				mem->m_Type = jcc_parseDeclarator(ctx, tu, &tok, basety);
				if (!mem->m_Type) {
					return false;
				}
				mem->m_Name = mem->m_Type->m_DeclName;
				mem->m_ID = idx++;
				mem->m_Alignment = attr.m_Align
					? attr.m_Align
					: mem->m_Type->m_Alignment
					;
				mem->m_Type->m_DeclName = baseTypeName;

				if (jcc_tokExpect(&tok, JCC_TOKEN_COLON)) {
					mem->m_IsBitfield = true;

					bool err = false;
					mem->m_BitWidth = (uint32_t)jcc_parseConstExpression(ctx, tu, &tok, &err);
					if (err) {
						return false;
					}
				}

				cur->m_Next = mem;
				cur = cur->m_Next;
			}
		}
	}

	ty->m_StructMembers = head.m_Next;

	// If the last element is an array of incomplete type, it's
	// called a "flexible array member". It should behave as if
	// if were a zero-sized array.
	if (cur != &head && cur->m_Type->m_Kind == JCC_TYPE_ARRAY && cur->m_Type->m_ArrayLen < 0) {
		cur->m_Type = jcc_typeAllocArrayOf(ctx, cur->m_Type->m_BaseType, 0);
		if (!cur->m_Type) {
			return false;
		}

		ty->m_Flags |= JCC_TYPE_FLAGS_IS_FLEXIBLE_Msk;
	}

	*tokenListPtr = tok;

	return true;
}

// attribute = ("__attribute__" "(" "(" "packed" ")" ")")*
static bool jcc_parseAttributes(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_token_t** tokenListPtr, jx_cc_type_t* ty)
{
	jx_cc_token_t* tok = *tokenListPtr;

	while (jcc_tokExpect(&tok, JCC_TOKEN_ATTRIBUTE)) {
		if (!jcc_tokExpect(&tok, JCC_TOKEN_OPEN_PAREN)) {
			return false;
		}
		if (!jcc_tokExpect(&tok, JCC_TOKEN_OPEN_PAREN)) {
			return false;
		}
		
		bool first = true;
		while (!jcc_tokExpect(&tok, JCC_TOKEN_CLOSE_PAREN)) {
			if (!first) {
				if (!jcc_tokExpect(&tok, JCC_TOKEN_COMMA)) {
					return false;
				}
			}
			first = false;
			
			if (jcc_tokExpect(&tok, JCC_TOKEN_PACKED)) {
				ty->m_Flags |= JCC_TYPE_FLAGS_IS_PACKED_Msk;
			} else if (jcc_tokExpect(&tok, JCC_TOKEN_ALIGNED)) {
				if (!jcc_tokExpect(&tok, JCC_TOKEN_OPEN_PAREN)) {
					return false;
				}

				bool err = false;
				ty->m_Alignment = (uint32_t)jcc_parseConstExpression(ctx, tu, &tok, &err);
				if (err) {
					return false;
				}

				if (!jcc_tokExpect(&tok, JCC_TOKEN_CLOSE_PAREN)) {
					return false;
				}
			} else {
				return false; // ERROR: unknown attribute
			}
		}
		
		if (!jcc_tokExpect(&tok, JCC_TOKEN_CLOSE_PAREN)) {
			return false;
		}
	}

	*tokenListPtr = tok;
	
	return true;
}

// struct-union-decl = attribute? ident? ("{" struct-members)?
static jx_cc_type_t* jcc_parseStructUnionDeclaration(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_token_t** tokenListPtr)
{
	jx_cc_token_t* tok = *tokenListPtr;

	jx_cc_type_t* ty = jcc_typeAllocStruct(ctx);
	if (!ty) {
		return NULL;
	}

	if (!jcc_parseAttributes(ctx, tu, &tok, ty)) {
		return NULL;
	}
	
	// Read a tag.
	jx_cc_token_t* tag = NULL;
	if (tok->m_Kind == JCC_TOKEN_IDENTIFIER) {
		tag = tok;
		tok = tok->m_Next;
	}
	
	if (tag && !jcc_tokIs(tok, JCC_TOKEN_OPEN_CURLY_BRACKET)) {
		jx_cc_type_t* ty2 = jcc_tuFindTag(tu, tag);
		if (ty2) {
			*tokenListPtr = tok;
			return ty2;
		}
		
		ty->m_Size = -1;
		jcc_tuScopeAddTag(tu, tag, ty);
	} else {
		if (!jcc_tokExpect(&tok, JCC_TOKEN_OPEN_CURLY_BRACKET)) {
			return NULL;
		}

		// Construct a struct object.
		if (!jcc_parseStructMembers(ctx, tu, &tok, ty)) {
			return NULL;
		}

		if (!jcc_parseAttributes(ctx, tu, &tok, ty)) {
			return NULL;
		}

		if (tag) {
			// If this is a redefinition, overwrite a previous type.
			// Otherwise, register the struct type.
			jcc_scope_entry_t* entry = jx_hashmapGet(tu->m_Scope->m_Tags, &(jcc_scope_entry_t){.m_Key = tag->m_String, .m_KeyLen = tag->m_Length});
			if (entry) {
				jx_memcpy(entry->m_Value, ty, sizeof(jx_cc_type_t));
				*tokenListPtr = tok;
				return (jx_cc_type_t*)entry->m_Value;
			}

			jcc_tuScopeAddTag(tu, tag, ty);

			ty->m_DeclName = tag;
		}
	}

	*tokenListPtr = tok;
	
	return ty;
}

// struct-decl = struct-union-decl
static jx_cc_type_t* jcc_parseStructDeclaration(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_token_t** tokenListPtr)
{
	jx_cc_token_t* tok = *tokenListPtr;

	jx_cc_type_t* ty = jcc_parseStructUnionDeclaration(ctx, tu, &tok);
	if (!ty) {
		return NULL;
	}

	ty->m_Kind = JCC_TYPE_STRUCT;
	
	if (ty->m_Size >= 0) {
		// Assign offsets within the struct to members.
		int bits = 0;
		for (jx_cc_struct_member_t* mem = ty->m_StructMembers; mem; mem = mem->m_Next) {
			if (mem->m_IsBitfield && mem->m_BitWidth == 0) {
				// Zero-width anonymous bitfield has a special meaning.
				// It affects only alignment.
				bits = jcc_alignTo(bits, mem->m_Type->m_Size * 8);
			} else if (mem->m_IsBitfield) {
				const int sz = mem->m_Type->m_Size;
				if (bits / (sz * 8) != (bits + mem->m_BitWidth - 1) / (sz * 8)) {
					bits = jcc_alignTo(bits, sz * 8);
				}

				mem->m_Offset = jcc_alignDown(bits / 8, sz);
				mem->m_BitOffset = bits % (sz * 8);
				bits += mem->m_BitWidth;
			} else {
				if ((ty->m_Flags & JCC_TYPE_FLAGS_IS_PACKED_Msk) == 0) {
					bits = jcc_alignTo(bits, mem->m_Alignment * 8);
				}

				mem->m_Offset = bits / 8;
				bits += mem->m_Type->m_Size * 8;
			}

			if ((ty->m_Flags & JCC_TYPE_FLAGS_IS_PACKED_Msk) == 0 && ty->m_Alignment < mem->m_Alignment) {
				ty->m_Alignment = mem->m_Alignment;
			}
		}

		ty->m_Size = jcc_alignTo(bits, ty->m_Alignment * 8) / 8;
	}

	*tokenListPtr = tok;

	return ty;
}

// union-decl = struct-union-decl
static jx_cc_type_t* jcc_parseUnionDeclaration(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_token_t** tokenListPtr)
{
	jx_cc_token_t* tok = *tokenListPtr;

	jx_cc_type_t* ty = jcc_parseStructUnionDeclaration(ctx, tu, &tok);
	if (!ty) {
		return NULL;
	}

	ty->m_Kind = JCC_TYPE_UNION;
	
	if (ty->m_Size >= 0) {
		// If union, we don't have to assign offsets because they
		// are already initialized to zero. We need to compute the
		// alignment and the size though.
		for (jx_cc_struct_member_t* mem = ty->m_StructMembers; mem; mem = mem->m_Next) {
			if (ty->m_Alignment < mem->m_Alignment) {
				ty->m_Alignment = mem->m_Alignment;
			}

			if (ty->m_Size < mem->m_Type->m_Size) {
				ty->m_Size = mem->m_Type->m_Size;
			}
		}

		ty->m_Size = jcc_alignTo(ty->m_Size, ty->m_Alignment);
	}

	*tokenListPtr = tok;

	return ty;
}

// Find a struct member by name.
static jx_cc_struct_member_t* jcc_getStructMember(jx_cc_type_t* ty, jx_cc_token_t* tok) 
{
	for (jx_cc_struct_member_t* mem = ty->m_StructMembers; mem; mem = mem->m_Next) {
		// Anonymous struct member
		if ((mem->m_Type->m_Kind == JCC_TYPE_STRUCT || mem->m_Type->m_Kind == JCC_TYPE_UNION) && !mem->m_Name) {
			if (jcc_getStructMember(mem->m_Type, tok)) {
				return mem;
			}
			
			continue;
		}

		// Regular struct member
		if (mem->m_Name->m_String == tok->m_String) {
			return mem;
		}
	}
	
	return NULL;
}

// Create a node representing a struct member access, such as foo.bar
// where foo is a struct and bar is a member name.
//
// C has a feature called "anonymous struct" which allows a struct to
// have another unnamed struct as a member like this:
//
//   struct { struct { int a; }; int b; } x;
//
// The members of an anonymous struct belong to the outer struct's
// member namespace. Therefore, in the above example, you can access
// member "a" of the anonymous struct as "x.a".
//
// This function takes care of anonymous structs.
static jx_cc_ast_expr_t* jcc_astAllocStructMemberNode(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_ast_expr_t* node, jx_cc_token_t* tok)
{
	if (!node) {
		return NULL;
	}

	if (!jcc_astAddType(ctx, &node->super)) {
		return NULL;
	}

	if (node->m_Type->m_Kind != JCC_TYPE_STRUCT && node->m_Type->m_Kind != JCC_TYPE_UNION) {
		return NULL; // ERROR: not a struct nor a union
	}
	
	jx_cc_type_t* ty = node->m_Type;
	for (;;) {
		jx_cc_struct_member_t* mem = jcc_getStructMember(ty, tok);
		if (!mem) {
			return NULL; // ERROR: no such member
		}

		node = jcc_astAllocExprMember(ctx, node, mem, tok);
		if (!node) {
			return NULL;
		}

		if (mem->m_Name) {
			break;
		}

		ty = mem->m_Type;
	}

	return node;
}

// Convert A++ to `(typeof A)((A += 1) - 1)`
static jx_cc_ast_expr_t* jcc_astAllocIncDecNode(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_ast_expr_t* node, jx_cc_token_t* tok, int addend)
{
	if (!node) {
		return NULL;
	}

	if (!jcc_astAddType(ctx, &node->super)) {
		return NULL;
	}

	return jcc_astAllocExprCast(ctx
		, jcc_astAllocExprAdd(ctx
			, jcc_astConvertToAssign(ctx, tu
				, jcc_astAllocExprAdd(ctx
					, node
					, jcc_astAllocExprIConst(ctx, addend, kType_int, tok) // TODO: Constant type same as node's type?
					, tok
				))
			, jcc_astAllocExprIConst(ctx, -addend, kType_int, tok) // TODO: Constant type same as node's type?
			, tok)
		, node->m_Type)
		;
}

// postfix = "(" type-name ")" "{" initializer-list "}"
//         = ident "(" func-args ")" postfix-tail*
//         | primary postfix-tail*
//
// postfix-tail = "[" expr "]"
//              | "(" func-args ")"
//              | "." ident
//              | "->" ident
//              | "++"
//              | "--"
static jx_cc_ast_expr_t* jcc_parsePostfix(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_token_t** tokenListPtr)
{
	jx_cc_token_t* tok = *tokenListPtr;

	jx_cc_ast_expr_t* node = NULL;

	if (jcc_tokIs(tok, JCC_TOKEN_OPEN_PAREN) && jcc_tuIsTypename(ctx, tu, tok->m_Next)) {
		// Compound literal
		jx_cc_token_t* start = tok;

		tok = tok->m_Next; // Skip '('
		
		jx_cc_type_t* ty = jcc_parseTypename(ctx, tu, &tok);
		if (!ty) {
			return NULL;
		}

		if (!jcc_tokExpect(&tok, JCC_TOKEN_CLOSE_PAREN)) {
			return NULL;
		}
		
		if (!tu->m_Scope->m_Next) {
			jx_cc_object_t* var = jcc_tuVarAllocAnonGlobal(ctx, tu, ty);
			if (!var) {
				return NULL;
			}

			if (!jcc_parseGlobalVarInitializer(ctx, tu, &tok, var)) {
				return NULL;
			}

			node = jcc_astAllocExprVar(ctx, var, start);
		} else {
			jx_cc_object_t* var = jcc_tuVarAllocAnonLocal(ctx, tu, ty);
			if (!var) {
				return NULL;
			}

			jx_cc_ast_expr_t* lhs = jcc_parseLocalVarInitializer(ctx, tu, &tok, var);
			if (!lhs) {
				return NULL;
			}

			jx_cc_ast_expr_t* rhs = jcc_astAllocExprVar(ctx, var, tok);
			if (!rhs) {
				return NULL;
			}

			node = jcc_astAllocExprComma(ctx, lhs, rhs, start);
		}
	} else {
		node = jcc_parsePrimaryExpression(ctx, tu, &tok);
		while (node) {
			jx_cc_token_t* start = tok;

			if (jcc_tokExpect(&tok, JCC_TOKEN_OPEN_PAREN)) {
				node = jcc_parseFuncCall(ctx, tu, &tok, node);
			} else if (jcc_tokExpect(&tok, JCC_TOKEN_OPEN_BRACKET)) {
				// x[y] is short for *(x+y)
				jx_cc_ast_expr_t* idx = jcc_parseExpression(ctx, tu, &tok);
				if (!idx) {
					return NULL;
				}

				if (!jcc_tokExpect(&tok, JCC_TOKEN_CLOSE_BRACKET)) {
					return NULL;
				}

				if (!jcc_astAddType(ctx, &node->super)) {
					return NULL;
				}

				JX_CHECK(node->m_Type->m_Kind == JCC_TYPE_PTR || node->m_Type->m_Kind == JCC_TYPE_ARRAY, "Node not a pointer!");
				node = jcc_astAllocExprUnary(ctx
					, JCC_NODE_EXPR_DEREF
					, jcc_astAllocExprGetElementPtr(ctx, node->m_Type->m_BaseType, node, idx, start), start
				);
			} else if (jcc_tokExpect(&tok, JCC_TOKEN_DOT)) {
				node = jcc_astAllocStructMemberNode(ctx, tu, node, tok);
				tok = tok->m_Next;
			} else if (jcc_tokExpect(&tok, JCC_TOKEN_PTR)) {
				// x->y is short for (*x).y
				node = jcc_astAllocExprUnary(ctx, JCC_NODE_EXPR_DEREF, node, start);
				if (!node) {
					return NULL;
				}
				node = jcc_astAllocStructMemberNode(ctx, tu, node, tok);

				tok = tok->m_Next;
			} else if (jcc_tokExpect(&tok, JCC_TOKEN_INC)) {
				node = jcc_astAllocIncDecNode(ctx, tu, node, start, 1);
			} else if (jcc_tokExpect(&tok, JCC_TOKEN_DEC)) {
				node = jcc_astAllocIncDecNode(ctx, tu, node, start, -1);
			} else {
				break;
			}
		}
	}

	if (!node) {
		return NULL;
	}
	
	*tokenListPtr = tok;

	return node;
}

// funccall = (assign ("," assign)*)? ")"
static jx_cc_ast_expr_t* jcc_parseFuncCall(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_token_t** tokenListPtr, jx_cc_ast_expr_t* fn)
{
	jx_cc_token_t* tok = *tokenListPtr;

	if (!fn) {
		return NULL;
	}

	if (!jcc_astAddType(ctx, &fn->super)) {
		return NULL;
	}
	
	if (fn->m_Type->m_Kind != JCC_TYPE_FUNC && (fn->m_Type->m_Kind != JCC_TYPE_PTR || fn->m_Type->m_BaseType->m_Kind != JCC_TYPE_FUNC)) {
		return NULL; // ERROR: not a function
	}
	
	jx_cc_type_t* ty = (fn->m_Type->m_Kind == JCC_TYPE_FUNC)
		? fn->m_Type
		: fn->m_Type->m_BaseType
		;
	jx_cc_type_t* param_ty = ty->m_FuncParams;
	
	jx_cc_ast_expr_t** argsArr = (jx_cc_ast_expr_t**)jx_array_create(ctx->m_Allocator);
	if (!argsArr) {
		return NULL;
	}
	
	bool first = true;
	while (!jcc_tokExpect(&tok, JCC_TOKEN_CLOSE_PAREN)) {
		if (!first) {
			if (!jcc_tokExpect(&tok, JCC_TOKEN_COMMA)) {
				jx_array_free(argsArr);
				return NULL;
			}
		}
		first = false;
		
		jx_cc_ast_expr_t* arg = jcc_parseAssignment(ctx, tu, &tok);
		if (!arg) {
			jx_array_free(argsArr);
			return NULL;
		}

		if (!jcc_astAddType(ctx, &arg->super)) {
			jx_array_free(argsArr);
			return NULL;
		}
		
		if (!param_ty && (ty->m_Flags & JCC_TYPE_FLAGS_IS_VARIADIC_Msk) == 0) {
			jx_array_free(argsArr);
			return NULL; // ERROR: too many arguments
		}
		
		if (param_ty) {
			if (param_ty->m_Kind != JCC_TYPE_STRUCT && param_ty->m_Kind != JCC_TYPE_UNION) {
				// TODO: Check if cast can be performed? E.g. passing a float to a const char* argument should fail!
				arg = jcc_astAllocExprCast(ctx, arg, param_ty);
				if (!arg) {
					jx_array_free(argsArr);
					return NULL;
				}
			}
			
			param_ty = param_ty->m_Next;
		} else if (arg->m_Type->m_Kind == JCC_TYPE_FLOAT) {
			// If parameter type is omitted (e.g. in "..."), float
			// arguments are promoted to double.
			arg = jcc_astAllocExprCast(ctx, arg, kType_double);
			if (!arg) {
				jx_array_free(argsArr);
				return NULL;
			}
		}

		jx_array_push_back(argsArr, arg);
	}
	
	if (param_ty) {
		jx_array_free(argsArr);
		return NULL; // ERROR: too few arguments
	}

	jx_cc_ast_expr_t* node = jcc_astAllocExprFuncCall(ctx, tu, fn, argsArr, jx_array_sizeu(argsArr), ty, tok);
	if (!node) {
		jx_array_free(argsArr);
		return NULL;
	}

	jx_array_free(argsArr);
	
	*tokenListPtr = tok;

	return node;
}

// generic-selection = "(" assign "," generic-assoc ("," generic-assoc)* ")"
//
// generic-assoc = type-name ":" assign
//               | "default" ":" assign
static jx_cc_ast_expr_t* jcc_parseGenericSelection(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_token_t** tokenListPtr)
{
	jx_cc_token_t* tok = *tokenListPtr;

	jx_cc_token_t* start = tok;

	if (!jcc_tokExpect(&tok, JCC_TOKEN_OPEN_PAREN)) {
		return NULL;
	}
	
	jx_cc_ast_expr_t* ctrl = jcc_parseAssignment(ctx, tu, &tok);
	if (!ctrl) {
		return NULL;
	}

	if (!jcc_astAddType(ctx, &ctrl->super)) {
		return NULL;
	}
	
	jx_cc_type_t* t1 = ctrl->m_Type;
	if (t1->m_Kind == JCC_TYPE_FUNC) {
		t1 = jcc_typeAllocPointerTo(ctx, t1);
		if (!t1) {
			return NULL;
		}
	} else if (t1->m_Kind == JCC_TYPE_ARRAY) {
		t1 = jcc_typeAllocPointerTo(ctx, t1->m_BaseType);
		if (!t1) {
			return NULL;
		}
	}
	
	jx_cc_ast_expr_t* ret = NULL;
	while (!jcc_tokExpect(&tok, JCC_TOKEN_CLOSE_PAREN)) {
		if (!jcc_tokExpect(&tok, JCC_TOKEN_COMMA)) {
			return NULL;
		}
		
		if (jcc_tokExpect(&tok, JCC_TOKEN_DEFAULT)) {
			if (!jcc_tokExpect(&tok, JCC_TOKEN_COLON)) {
				return NULL;
			}
			
			jx_cc_ast_expr_t* node = jcc_parseAssignment(ctx, tu, &tok);
			if (!node) {
				return NULL;
			}

			if (!ret) {
				ret = node;
			}
		} else {
			jx_cc_type_t* t2 = jcc_parseTypename(ctx, tu, &tok);
			if (!t2) {
				return NULL;
			}

			if (!jcc_tokExpect(&tok, JCC_TOKEN_COLON)) {
				return NULL;
			}

			jx_cc_ast_expr_t* node = jcc_parseAssignment(ctx, tu, &tok);
			if (!node) {
				return NULL;
			}

			if (jcc_typeIsCompatible(t1, t2)) {
				ret = node;
			}
		}
	}
	
	if (!ret) {
		return NULL; // ERROR: controlling expression type not compatible with any generic association type
	}

	*tokenListPtr = tok;

	return ret;
}

// primary = "(" "{" stmt+ "}" ")"
//         | "(" expr ")"
//         | "sizeof" "(" type-name ")"
//         | "sizeof" unary
//         | "_Alignof" "(" type-name ")"
//         | "_Alignof" unary
//         | "_Generic" generic-selection
//         | "__builtin_types_compatible_p" "(" type-name, type-name, ")"
//         | "__builtin_reg_class" "(" type-name ")"
//         | ident
//         | str
//         | num
static jx_cc_ast_expr_t* jcc_parsePrimaryExpression(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_token_t** tokenListPtr)
{
	jx_cc_token_t* tok = *tokenListPtr;

	jx_cc_token_t* start = tok;
	
#if 0 // GCC extension
	if (jcc_tokIs(tok, JCC_TOKEN_OPEN_PAREN) && jcc_tokIs(tok->next, JCC_TOKEN_OPEN_CURLY_BRACKET)) {
		// This is a GNU statement expresssion.
		jx_cc_ast_node_t* node = jcc_astAllocNode(ctx, JCC_NODE_STMT_EXPR, tok);
		node->body = jcc_parseCompoundStatement(ctx, tu, &tok, tok->next->next)->body;
		*rest = jcc_tokSkipIf(tok, JCC_TOKEN_CLOSE_PAREN);
		return node;
	}
#endif
	
	jx_cc_ast_expr_t* node = NULL;
	if (jcc_tokExpect(&tok, JCC_TOKEN_OPEN_PAREN)) {
		node = jcc_parseExpression(ctx, tu, &tok);
		if (!node) {
			return NULL;
		}

		if (!jcc_tokExpect(&tok, JCC_TOKEN_CLOSE_PAREN)) {
			return NULL;
		}
	} else if (jcc_tokExpect(&tok, JCC_TOKEN_SIZEOF)) {
		if (jcc_tokIs(tok, JCC_TOKEN_OPEN_PAREN) && jcc_tuIsTypename(ctx, tu, tok->m_Next)) {
			tok = tok->m_Next;

			jx_cc_type_t* ty = jcc_parseTypename(ctx, tu, &tok);
			if (!ty) {
				return NULL;
			}

			if (!jcc_tokExpect(&tok, JCC_TOKEN_CLOSE_PAREN)) {
				return NULL;
			}

			node = jcc_astAllocExprIConst_ulong(ctx, ty->m_Size, start);
		} else {
			node = jcc_parseUnary(ctx, tu, &tok);
			if (!node) {
				return NULL;
			}

			if (!jcc_astAddType(ctx, &node->super)) {
				return NULL;
			}

			node = jcc_astAllocExprIConst_ulong(ctx, node->m_Type->m_Size, tok);
		}
	} else if (jcc_tokExpect(&tok, JCC_TOKEN_ALIGNOF)) {
		if (jcc_tokIs(tok, JCC_TOKEN_OPEN_PAREN) && jcc_tuIsTypename(ctx, tu, tok->m_Next)) {
			tok = tok->m_Next;

			jx_cc_type_t* ty = jcc_parseTypename(ctx, tu, &tok);
			if (!ty) {
				return NULL;
			}

			if (!jcc_tokExpect(&tok, JCC_TOKEN_CLOSE_PAREN)) {
				return NULL;
			}

			node = jcc_astAllocExprIConst_ulong(ctx, ty->m_Alignment, tok);
		} else {
			node = jcc_parseUnary(ctx, tu, &tok);
			if (!node) {
				return NULL;
			}

			if (!jcc_astAddType(ctx, &node->super)) {
				return NULL;
			}

			node = jcc_astAllocExprIConst_ulong(ctx, node->m_Type->m_Alignment, tok);
		}
	} else if (jcc_tokExpect(&tok, JCC_TOKEN_GENERIC)) {
		node = jcc_parseGenericSelection(ctx, tu, &tok);
#if 0
	} else if (jcc_tokExpectStr(&tok, "__builtin_types_compatible_p")) {
		if (!jcc_tokExpect(&tok, JCC_TOKEN_OPEN_PAREN)) {
			return NULL;
		}

		jx_cc_type_t* t1 = jcc_parseTypename(ctx, tu, &tok);
		if (!t1) {
			return NULL;
		}

		if (!jcc_tokExpect(&tok, JCC_TOKEN_COMMA)) {
			return NULL;
		}

		jx_cc_type_t* t2 = jcc_parseTypename(ctx, tu, &tok);
		if (!t2) {
			return NULL;
		}

		if (!jcc_tokExpect(&tok, JCC_TOKEN_CLOSE_PAREN)) {
			return NULL;
		}

		node = jcc_astAllocExprIConst(ctx, jcc_typeIsCompatible(t1, t2), kType_int, start);
#endif
#if 0
	} else if (jcc_tokExpectStr(&tok, "__builtin_reg_class")) {
		if (!jcc_tokExpect(&tok, JCC_TOKEN_OPEN_PAREN)) {
			return NULL;
		}
		
		jx_cc_type_t* ty = jcc_parseTypename(ctx, tu, &tok);
		if (!ty) {
			return NULL;
		}

		if (!jcc_tokExpect(&tok, JCC_TOKEN_CLOSE_PAREN)) {
			return NULL;
		}

		if (jx_cc_typeIsInteger(ty) || ty->m_Kind == JCC_TYPE_PTR) {
			node = jcc_astAllocExprIConst(ctx, 0, kType_int, start);
		} else if (jx_cc_typeIsFloat(ty)) {
			node = jcc_astAllocExprIConst(ctx, 1, kType_int, start);
		} else {
			node = jcc_astAllocExprIConst(ctx, 2, kType_int, start);
		}
#endif
#if 0
	} else if (jcc_tokExpectStr(&tok, "__builtin_compare_and_swap")) {
		node = jcc_astAllocNode(ctx, JCC_NODE_STMT_CAS, tok);
		if (!node) {
			return NULL;
		}

		if (!jcc_tokExpect(&tok, JCC_TOKEN_OPEN_PAREN)) {
			return NULL;
		}
		
		node->cas_addr = jcc_parseAssignment(ctx, tu, &tok);
		if (!node->cas_addr) {
			return NULL;
		}
		
		if (!jcc_tokExpect(&tok, JCC_TOKEN_COMMA)) {
			return NULL;
		}

		node->cas_old = jcc_parseAssignment(ctx, tu, &tok);
		if (!node->cas_old) {
			return NULL;
		}

		if (!jcc_tokExpect(&tok, JCC_TOKEN_COMMA)) {
			return NULL;
		}

		node->cas_new = jcc_parseAssignment(ctx, tu, &tok);
		if (!node->cas_new) {
			return NULL;
		}

		if (!jcc_tokExpect(&tok, JCC_TOKEN_CLOSE_PAREN)) {
			return NULL;
		}
#endif
#if 0
	} else if (jcc_tokExpectStr(&tok, "__builtin_atomic_exchange")) {
		node = jcc_astAllocNode(ctx, JCC_NODE_STMT_EXCH, tok);
		if (!node) {
			return NULL;
		}

		if (!jcc_tokExpect(&tok, JCC_TOKEN_OPEN_PAREN)) {
			return NULL;
		}

		node->lhs = jcc_parseAssignment(ctx, tu, &tok);
		if (!node->lhs) {
			return NULL;
		}

		if (!jcc_tokExpect(&tok, JCC_TOKEN_COMMA)) {
			return NULL;
		}

		node->rhs = jcc_parseAssignment(ctx, tu, &tok);
		if (!node->rhs) {
			return NULL;
		}

		if (!jcc_tokExpect(&tok, JCC_TOKEN_CLOSE_PAREN)) {
			return NULL;
		}
#endif
	} else if (jcc_tokIs(tok, JCC_TOKEN_IDENTIFIER)) {
		// Variable or enum constant
		jcc_var_scope_t* sc = jcc_tuFindVariable(tu, tok);
		tok = tok->m_Next;

		// For "static inline" function
		if (sc && sc->m_Var && (sc->m_Var->m_Flags & JCC_OBJECT_FLAGS_IS_FUNCTION_Msk) != 0) {
			if (tu->m_CurFunction) {
				jx_array_push_back(tu->m_CurFunction->m_FuncRefsArr, sc->m_Var->m_Name);
			} else {
				sc->m_Var->m_Flags |= JCC_OBJECT_FLAGS_IS_ROOT_Msk;
			}
		}
		
		if (sc) {
			if (sc->m_Var) {
				node = jcc_astAllocExprVar(ctx, sc->m_Var, tok);
			} else if (sc->m_Enum) {
				node = jcc_astAllocExprIConst(ctx, sc->m_EnumValue, kType_int, tok);
			}
		}
		
		if (!node) {
			if (jcc_tokIs(tok, JCC_TOKEN_OPEN_PAREN)) {
				jcc_logError(ctx, &tok->m_Loc, "Implicit declaration of a function");
			} else {
				jcc_logError(ctx, &tok->m_Loc, "Undefined variable");
			}
			return NULL;
		}
	} else if (jcc_tokIs(tok, JCC_TOKEN_STRING_LITERAL)) {
		jx_cc_object_t* var = jcc_tuVarAllocStringLiteral(ctx, tu, tok->m_Val_string, tok->m_Type);
		if (!var) {
			return NULL;
		}
		tok = tok->m_Next;

		node = jcc_astAllocExprVar(ctx, var, tok);
	} else if (jcc_tokIs(tok, JCC_TOKEN_NUMBER)) {
		if (jx_cc_typeIsFloat(tok->m_Type)) {
			node = jcc_astAllocExprFConst(ctx, tok->m_Val_float, tok->m_Type, tok);
		} else {
			node = jcc_astAllocExprIConst(ctx, tok->m_Val_int, tok->m_Type, tok);
		}
		
		if (!node) {
			return NULL;
		}

		tok = tok->m_Next;
	}

	if (!node) {
		return NULL; // ERROR: expected an expression
	}

	*tokenListPtr = tok;

	return node;
}

static bool jcc_parseTypedef(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_token_t** tokenListPtr, jx_cc_type_t* basety)
{
	jx_cc_token_t* tok = *tokenListPtr;

	bool first = true;	
	while (!jcc_tokExpect(&tok, JCC_TOKEN_SEMICOLON)) {
		if (!first) {
			if (!jcc_tokExpect(&tok, JCC_TOKEN_COMMA)) {
				jcc_logError(ctx, &tok->m_Loc, "Expected ','");
				return false;
			}
		}
		first = false;
		
		jx_cc_type_t* ty = jcc_parseDeclarator(ctx, tu, &tok, basety);
		if (!ty) {
			jcc_logError(ctx, &tok->m_Loc, "Expected declarator");
			return false;
		}

		if (!ty->m_DeclName) {
			jcc_logError(ctx, &tok->m_Loc, "typedef name omitted");
			return false;
		}
		
		jcc_var_scope_t* scope = jcc_tuPushVarScope(ctx, tu, jcc_tokGetIdentifier(ctx, ty->m_DeclName));
		if (!scope) {
			jcc_logError(ctx, JCC_SOURCE_LOCATION_CUR(), "Internal Error: Memory allocation failed.");
			return false;
		}

		scope->m_Typedef = ty;
	}

	*tokenListPtr = tok;
	
	return true;
}

static bool jcc_createParamLocalVars(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_type_t* param)
{
	while (param) {
		if (!param->m_DeclName) {
			jcc_logError(ctx, JCC_SOURCE_LOCATION_CUR(), "Parameter name omitted.");
			return false;
		}

		jx_cc_object_t* localVar = jcc_tuVarAllocLocal(ctx, tu, jcc_tokGetIdentifier(ctx, param->m_DeclName), param);
		if (!localVar) {
			return false;
		}

		param = param->m_Next;
	}

	return true;
}

// This function matches gotos or labels-as-values with labels.
//
// We cannot resolve gotos as we parse a function because gotos
// can refer a label that appears later in the function.
// So, we need to do this after we parse the entire function.
static bool jcc_resolveGotoLabels(jcc_translation_unit_t* tu)
{
	bool res = true;
	for (jx_cc_ast_stmt_goto_t* x = tu->m_CurFuncGotos; x; x = x->m_NextGoto) {
		for (jx_cc_ast_stmt_label_t* y = tu->m_CurFuncLabels; y; y = y->m_NextLabel) {
			if (x->m_Label == y->m_Label) {
				x->m_UniqueLabel = y->m_UniqueLabel;
				break;
			} else {
				JX_CHECK(jx_strcmp(x->m_Label, y->m_Label), "Label not interned?!?!?");
			}
		}
		
		if (x->m_UniqueLabel.m_ID == 0) {
			res = false; // ERROR: use of undeclared label
			break;
		}
	}
	
	tu->m_CurFuncGotos = NULL;
	tu->m_CurFuncLabels = NULL;

	return res;
}

static jx_cc_object_t* jcc_tuFindFunction(jcc_translation_unit_t* tu, const char* name)
{
	jcc_scope_t* sc = tu->m_Scope;
	while (sc->m_Next) {
		sc = sc->m_Next;
	}
	
	jcc_scope_entry_t* entry = jx_hashmapGet(sc->m_Vars, &(jcc_scope_entry_t){.m_Key = name, .m_KeyLen = jx_strlen(name)});
	if (entry) {
		jcc_var_scope_t* sc2 = (jcc_var_scope_t*)entry->m_Value;
		if (sc2 && sc2->m_Var && (sc2->m_Var->m_Flags & JCC_OBJECT_FLAGS_IS_FUNCTION_Msk) != 0) {
			return sc2->m_Var;
		}
	}
	
	return NULL;
}

static void jcc_tuFunctionMarkLive(jcc_translation_unit_t* tu, jx_cc_object_t* var)
{
	if ((var->m_Flags & JCC_OBJECT_FLAGS_IS_FUNCTION_Msk) == 0 || (var->m_Flags & JCC_OBJECT_FLAGS_IS_LIVE_Msk) != 0) {
		return;
	}
	var->m_Flags |= JCC_OBJECT_FLAGS_IS_LIVE_Msk;
	
	const uint32_t numRefs = jx_array_sizeu(var->m_FuncRefsArr);
	for (uint32_t i = 0; i < numRefs; i++) {
		jx_cc_object_t* fn = jcc_tuFindFunction(tu, var->m_FuncRefsArr[i]);
		if (fn) {
			jcc_tuFunctionMarkLive(tu, fn);
		}
	}
}

static bool jcc_parseFunction(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_token_t** tokenListPtr, jx_cc_type_t* basety, jcc_var_attr_t* attr)
{
	jx_cc_token_t* tok = *tokenListPtr;

	jx_cc_type_t* ty = jcc_parseDeclarator(ctx, tu, &tok, basety);
	if (!ty) {
		jcc_logError(ctx, &tok->m_Loc, "Expected declarator");
		return false;
	}

	if (!ty->m_DeclName) {
		jcc_logError(ctx, &tok->m_Loc, "Function name omitted");
		return false;
	}
	
	const char* name_str = jcc_tokGetIdentifier(ctx, ty->m_DeclName);
	if (!name_str) {
		jcc_logError(ctx, JCC_SOURCE_LOCATION_CUR(), "Internal Error: Memory allocation failed.");
		return false;
	}
	
	jx_cc_object_t* fn = jcc_tuFindFunction(tu, name_str);
	if (fn) {
		// Redeclaration
		if ((fn->m_Flags && JCC_OBJECT_FLAGS_IS_FUNCTION_Msk) == 0) {
			jcc_logError(ctx, &tok->m_Loc, "Redeclared as different kind of symbol");
			return false;
		}
		
		if ((fn->m_Flags & JCC_OBJECT_FLAGS_IS_DEFINITION_Msk) != 0 && jcc_tokIs(tok, JCC_TOKEN_OPEN_CURLY_BRACKET)) {
			jcc_logError(ctx, &tok->m_Loc, "Redefinition of %s", name_str);
			return false;
		}
		
		if ((fn->m_Flags & JCC_OBJECT_FLAGS_IS_STATIC_Msk) == 0 && (attr->m_Flags & JCC_VAR_ATTR_IS_STATIC_Msk) != 0) {
			jcc_logError(ctx, &tok->m_Loc, "Static declaration follows a non-static declaration");
			return false;
		}
		
		const bool isDefinition = false
			|| (fn->m_Flags & JCC_OBJECT_FLAGS_IS_DEFINITION_Msk) != 0
			|| jcc_tokIs(tok, JCC_TOKEN_OPEN_CURLY_BRACKET)
			;
		
		fn->m_Flags |= isDefinition ? JCC_OBJECT_FLAGS_IS_DEFINITION_Msk : 0;
	} else {
		fn = jcc_tuVarAllocGlobal(ctx, tu, name_str, ty);
		if (!fn) {
			jcc_logError(ctx, JCC_SOURCE_LOCATION_CUR(), "Internal Error: Memory allocation failed.");
			return false;
		}

		// Remove is_static and is_definition flags set by alloc global.
		fn->m_Flags &= ~(JCC_OBJECT_FLAGS_IS_DEFINITION_Msk | JCC_OBJECT_FLAGS_IS_STATIC_Msk);

		fn->m_Flags |= JCC_OBJECT_FLAGS_IS_FUNCTION_Msk;
		fn->m_Flags |= jcc_tokIs(tok, JCC_TOKEN_OPEN_CURLY_BRACKET) ? JCC_OBJECT_FLAGS_IS_DEFINITION_Msk : 0;
		fn->m_Flags |= (false
			|| (attr->m_Flags & JCC_VAR_ATTR_IS_STATIC_Msk) != 0
			|| (attr->m_Flags & (JCC_VAR_ATTR_IS_INLINE_Msk | JCC_VAR_ATTR_IS_EXTERN_Msk)) == JCC_VAR_ATTR_IS_INLINE_Msk) ? JCC_OBJECT_FLAGS_IS_STATIC_Msk : 0
			;
		fn->m_Flags |= (attr->m_Flags & JCC_VAR_ATTR_IS_INLINE_Msk) != 0 ? JCC_OBJECT_FLAGS_IS_INLINE_Msk : 0;

		fn->m_FuncRefsArr = (char**)jx_array_create(ctx->m_Allocator);
		if (!fn->m_FuncRefsArr) {
			jcc_logError(ctx, JCC_SOURCE_LOCATION_CUR(), "Internal Error: Memory allocation failed.");
			return false;
		}
	}
	
	fn->m_Flags |= !((fn->m_Flags & JCC_OBJECT_FLAGS_IS_STATIC_Msk) != 0 && (fn->m_Flags & JCC_OBJECT_FLAGS_IS_INLINE_Msk) != 0)
		? JCC_OBJECT_FLAGS_IS_ROOT_Msk
		: 0
		;
	
	if (!jcc_tokExpect(&tok, JCC_TOKEN_SEMICOLON)) {
		tu->m_CurFunction = fn;

		jx_cc_object_t* funcPtr = tu->m_GlobalsTail;

		if (!jcc_tuEnterScope(ctx, tu)) {
			jcc_logError(ctx, JCC_SOURCE_LOCATION_CUR(), "Internal Error: Memory allocation failed.");
			return false;
		}
		{
			tu->m_LocalsHead = NULL;
			tu->m_LocalsTail = NULL;

			// A buffer for a struct/union return value is passed as the hidden first parameter.
			jx_cc_type_t* rty = ty->m_FuncRetType;
			if ((rty->m_Kind == JCC_TYPE_STRUCT || rty->m_Kind == JCC_TYPE_UNION) && rty->m_Size > JCC_CONFIG_MAX_RETVAL_SIZE) {
				if (!jcc_tuVarAllocLocal(ctx, tu, "$__retbuf__", jcc_typeAllocPointerTo(ctx, rty))) {
					jcc_logError(ctx, JCC_SOURCE_LOCATION_CUR(), "Internal Error: Memory allocation failed.");
					jcc_tuLeaveScope(ctx, tu);
					return false;
				}
			}

			if (!jcc_createParamLocalVars(ctx, tu, ty->m_FuncParams)) {
				jcc_logError(ctx, JCC_SOURCE_LOCATION_CUR(), "Internal Error: Memory allocation failed.");
				jcc_tuLeaveScope(ctx, tu);
				return false;
			}

			fn->m_FuncParams = tu->m_LocalsHead;
			
			tu->m_LocalsHead = NULL;
			tu->m_LocalsTail = NULL;

			if ((ty->m_Flags & JCC_TYPE_FLAGS_IS_VARIADIC_Msk) != 0) {
				fn->m_FuncVarArgArea = jcc_tuVarAllocLocal(ctx, tu, "__va_area__", jcc_typeAllocArrayOf(ctx, kType_char, 136)); // TODO: Magic number!
				if (!fn->m_FuncVarArgArea) {
					jcc_logError(ctx, JCC_SOURCE_LOCATION_CUR(), "Internal Error: Memory allocation failed.");
					jcc_tuLeaveScope(ctx, tu);
					return false;
				}
			}

			if (!jcc_tokExpect(&tok, JCC_TOKEN_OPEN_CURLY_BRACKET)) {
				jcc_logError(ctx, &tok->m_Loc, "Expected '{'");
				jcc_tuLeaveScope(ctx, tu);
				return false;
			}

			// __func__ local variable
			{
				jx_cc_type_t* funcVarType = jcc_typeAllocArrayOf(ctx, kType_char, (int)jx_strlen(fn->m_Name) + 1);
				if (!funcVarType) {
					jcc_logError(ctx, JCC_SOURCE_LOCATION_CUR(), "Internal Error: Memory allocation failed.");
					jcc_tuLeaveScope(ctx, tu);
					return false;
				}

				// [https://www.sigbus.info/n1570#6.4.2.2p1] "__func__" is
				// automatically defined as a local variable containing the
				// current function name.
				{
					jcc_var_scope_t* funcVar = jcc_tuPushVarScope(ctx, tu, jx_strtable_insert(ctx->m_StringTable, "__func__", UINT32_MAX));
					if (!funcVar) {
						jcc_logError(ctx, JCC_SOURCE_LOCATION_CUR(), "Internal Error: Memory allocation failed.");
						jcc_tuLeaveScope(ctx, tu);
						return false;
					}

					funcVar->m_Var = jcc_tuVarAllocStringLiteral(ctx, tu, fn->m_Name, funcVarType);
					if (!funcVar->m_Var) {
						jcc_logError(ctx, JCC_SOURCE_LOCATION_CUR(), "Internal Error: Memory allocation failed.");
						jcc_tuLeaveScope(ctx, tu);
						return false;
					}
				}
			}

			fn->m_FuncBody = jcc_parseCompoundStatement(ctx, tu, &tok);
			if (!fn->m_FuncBody) {
				jcc_logError(ctx, &tok->m_Loc, "Expected compound statement");
				jcc_tuLeaveScope(ctx, tu);
				return false;
			}

			fn->m_FuncLocals = tu->m_LocalsHead;
		}
		jcc_tuLeaveScope(ctx, tu);

		jcc_resolveGotoLabels(tu);

		// Rearrange globals so the function appears last in the list.
		// The function might have created additional globals (e.g. static variables or strings),
		// and they must appear before the function definition to simplify code generation.
		{
			if (funcPtr != tu->m_GlobalsTail) {
				bool found = false;

				if (funcPtr == tu->m_GlobalsHead) {
					tu->m_GlobalsHead = tu->m_GlobalsHead->m_Next;

					tu->m_GlobalsTail->m_Next = funcPtr;
					tu->m_GlobalsTail = funcPtr;
					funcPtr->m_Next = NULL;

					found = true;
				} else {
					jx_cc_object_t* ptr = tu->m_GlobalsHead;
					while (ptr) {
						if (ptr->m_Next == funcPtr) {
							ptr->m_Next = funcPtr->m_Next;

							tu->m_GlobalsTail->m_Next = funcPtr;
							tu->m_GlobalsTail = funcPtr;
							funcPtr->m_Next = NULL;

							found = true;
							break;
						}

						ptr = ptr->m_Next;
					}
				}

				JX_CHECK(found, "Previous global tail not found!");
			}
		}
	}

	*tokenListPtr = tok;

	return true;
}

static bool jcc_parseGlobalVariable(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_token_t** tokenListPtr, jx_cc_type_t* basety, jcc_var_attr_t* attr)
{
	jx_cc_token_t* tok = *tokenListPtr;

	bool first = true;	
	while (!jcc_tokExpect(&tok, JCC_TOKEN_SEMICOLON)) {
		if (!first) {
			if (!jcc_tokExpect(&tok, JCC_TOKEN_COMMA)) {
				return false;
			}
		}
		first = false;
		
		jx_cc_token_t* typeName = basety->m_DeclName;

		jx_cc_type_t* ty = jcc_parseDeclarator(ctx, tu, &tok, basety);
		if (!ty) {
			return false;
		}

		if (!ty->m_DeclName) {
			return false; // ERROR: variable name omitted
		}

		jx_cc_token_t* varName = ty->m_DeclName;
		ty->m_DeclName = typeName;
		
		jx_cc_object_t* var = jcc_tuVarAllocGlobal(ctx, tu, jcc_tokGetIdentifier(ctx, varName), ty);
		if (!var) {
			return false;
		}

		var->m_Flags |= (attr->m_Flags & JCC_VAR_ATTR_IS_EXTERN_Msk) == 0 ? JCC_OBJECT_FLAGS_IS_DEFINITION_Msk : 0;
		var->m_Flags |= (attr->m_Flags & JCC_VAR_ATTR_IS_STATIC_Msk) != 0 ? JCC_OBJECT_FLAGS_IS_STATIC_Msk : 0;
		var->m_Flags |= (attr->m_Flags & JCC_VAR_ATTR_IS_TLS_Msk) != 0 ? JCC_OBJECT_FLAGS_IS_TLS_Msk : 0;
		if (attr->m_Align) {
			var->m_Alignment = attr->m_Align;
		}
		
		if (jcc_tokExpect(&tok, JCC_TOKEN_ASSIGN)) {
			if (!jcc_parseGlobalVarInitializer(ctx, tu, &tok, var)) {
				return false;
			}
		} else if ((attr->m_Flags & (JCC_VAR_ATTR_IS_EXTERN_Msk | JCC_VAR_ATTR_IS_TLS_Msk)) == 0) {
			var->m_Flags |= JCC_OBJECT_FLAGS_IS_TENTATIVE_Msk;
		}
	}

	*tokenListPtr = tok;
	
	return true;
}

// Lookahead tokens and returns true if a given token is a start
// of a function definition or declaration.
static bool jcc_isFunction(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_token_t* tok)
{
	if (jcc_tokIs(tok, JCC_TOKEN_SEMICOLON)) {
		return false;
	}
	
	jx_cc_type_t dummy = { 0 };
	jx_cc_type_t* ty = jcc_parseDeclarator(ctx, tu, &tok, &dummy);
	return ty && ty->m_Kind == JCC_TYPE_FUNC;
}

// Remove redundant tentative definitions.
static void jcc_scanGlobals(jcc_translation_unit_t* tu)
{
	jx_cc_object_t head;
	jx_cc_object_t* cur = &head;
	
	for (jx_cc_object_t* var = tu->m_GlobalsHead; var; var = var->m_Next) {
		if ((var->m_Flags & JCC_OBJECT_FLAGS_IS_TENTATIVE_Msk) == 0) {
			cur->m_Next = var;
			cur = cur->m_Next;
		} else {
			// Find another definition of the same identifier.
			jx_cc_object_t* var2 = tu->m_GlobalsHead;
			for (; var2; var2 = var2->m_Next) {
				if (var != var2 && (var2->m_Flags & JCC_OBJECT_FLAGS_IS_DEFINITION_Msk) != 0 && var->m_Name == var2->m_Name) {
					break;
				}
			}

			// If there's another definition, the tentative definition
			// is redundant
			if (!var2) {
				cur->m_Next = var;
				cur = cur->m_Next;
			}
		}
	}
	
	cur->m_Next = NULL;
	tu->m_GlobalsHead = head.m_Next;
	if (tu->m_GlobalsHead) {
		tu->m_GlobalsTail = tu->m_GlobalsHead;
		while (tu->m_GlobalsTail->m_Next) {
			tu->m_GlobalsTail = tu->m_GlobalsTail->m_Next;
		}
	}
}

// program = (typedef | function-definition | global-variable)*
static bool jcc_parse(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_token_t* tok)
{
	while (tok->m_Kind != JCC_TOKEN_EOF) {
		jcc_var_attr_t attr = { 0 };
		jx_cc_type_t* basety = jcc_parseDeclarationSpecifiers(ctx, tu, &tok, &attr);
		if (!basety) {
			jcc_logError(ctx, &tok->m_Loc, "Expected declaration specifiers");
			return false;
		}
		
		if ((attr.m_Flags & JCC_VAR_ATTR_IS_TYPEDEF_Msk) != 0) {
			// Typedef
			if (!jcc_parseTypedef(ctx, tu, &tok, basety)) {
				jcc_logError(ctx, &tok->m_Loc, "Failed to parse typedef");
				return false;
			}
		} else if (jcc_isFunction(ctx, tu, tok)) {
			// Function
			if (!jcc_parseFunction(ctx, tu, &tok, basety, &attr)) {
				jcc_logError(ctx, &tok->m_Loc, "Failed to parse function");
				return false;
			}
		} else {
			// Global variable
			if (!jcc_parseGlobalVariable(ctx, tu, &tok, basety, &attr)) {
				jcc_logError(ctx, &tok->m_Loc, "Failed to parse global variable");
				return false;
			}
		}
	}
	
	for (jx_cc_object_t* var = tu->m_GlobalsHead; var; var = var->m_Next) {
		if ((var->m_Flags & JCC_OBJECT_FLAGS_IS_ROOT_Msk) != 0) {
			jcc_tuFunctionMarkLive(tu, var);
		}
	}
	
	// Remove redundant tentative definitions.
	jcc_scanGlobals(tu);
	
	return true;
}

static jx_cc_type_t* jcc_typeAlloc(jx_cc_context_t* ctx, jx_cc_type_kind kind, int size, uint32_t align)
{
	jx_cc_type_t* ty = (jx_cc_type_t*)JX_ALLOC(ctx->m_LinearAllocator, sizeof(jx_cc_type_t));
	if (!ty) {
		return NULL;
	}

	jx_memset(ty, 0, sizeof(jx_cc_type_t));
	ty->m_Kind = kind;
	ty->m_Size = size;
	ty->m_Alignment = align;
	return ty;
}

static bool jcc_typeIsCompatible(const jx_cc_type_t* t1, const jx_cc_type_t* t2) 
{
	if (t1 == t2) {
		return true;
	}
	
	if (t1->m_OriginType) {
		return jcc_typeIsCompatible(t1->m_OriginType, t2);
	}
	
	if (t2->m_OriginType) {
		return jcc_typeIsCompatible(t1, t2->m_OriginType);
	}
	
	if (t1->m_Kind != t2->m_Kind) {
		return false;
	}
	
	switch (t1->m_Kind) {
	case JCC_TYPE_CHAR:
	case JCC_TYPE_SHORT:
	case JCC_TYPE_INT:
	case JCC_TYPE_LONG:
		return (t1->m_Flags & JCC_TYPE_FLAGS_IS_UNSIGNED_Msk) == (t2->m_Flags & JCC_TYPE_FLAGS_IS_UNSIGNED_Msk);
	case JCC_TYPE_FLOAT:
	case JCC_TYPE_DOUBLE:
		return true;
	case JCC_TYPE_PTR:
		return jcc_typeIsCompatible(t1->m_BaseType, t2->m_BaseType);
	case JCC_TYPE_FUNC: {
		if (!jcc_typeIsCompatible(t1->m_FuncRetType, t2->m_FuncRetType)) {
			return false;
		}
		
		if ((t1->m_Flags & JCC_TYPE_FLAGS_IS_VARIADIC_Msk) != (t2->m_Flags & JCC_TYPE_FLAGS_IS_VARIADIC_Msk)) {
			return false;
		}
		
		jx_cc_type_t* p1 = t1->m_FuncParams;
		jx_cc_type_t* p2 = t2->m_FuncParams;
		for (; p1 && p2; p1 = p1->m_Next, p2 = p2->m_Next) {
			if (!jcc_typeIsCompatible(p1, p2)) {
				return false;
			}
		}
		
		return p1 == NULL && p2 == NULL;
	} break;
	case JCC_TYPE_ARRAY:
		if (!jcc_typeIsCompatible(t1->m_BaseType, t2->m_BaseType)) {
			return false;
		}
		
		return true 
			&& t1->m_ArrayLen < 0
			&& t2->m_ArrayLen < 0
			&& t1->m_ArrayLen == t2->m_ArrayLen
			;
	default:
		break;
	}
	
	return false;
}

static bool jcc_typeIsSame(const jx_cc_type_t* t1, const jx_cc_type_t* t2)
{
	const bool t1IsPtr = t1->m_BaseType != NULL;
	const bool t2IsPtr = t2->m_BaseType != NULL;

	if (t1IsPtr != t2IsPtr) {
		return false;
	}

	if (t1IsPtr) {
		return jcc_typeIsSame(t1->m_BaseType, t2->m_BaseType);
	}

	return t1->m_Kind == t2->m_Kind;
}

static jx_cc_type_t* jcc_typeCopy(jx_cc_context_t* ctx, const jx_cc_type_t* ty)
{
	if (!ty) {
		return NULL;
	}

	jx_cc_type_t* ret = (jx_cc_type_t*)JX_ALLOC(ctx->m_LinearAllocator, sizeof(jx_cc_type_t));
	if (!ret) {
		return NULL;
	}

//	jx_memset(ret, 0, sizeof(jx_cc_type_t));
	jx_memcpy(ret, ty, sizeof(jx_cc_type_t));
	ret->m_OriginType = ty;

	return ret;
}

static jx_cc_type_t* jcc_typeAllocPointerTo(jx_cc_context_t* ctx, jx_cc_type_t* base)
{
	if (!base) {
		return NULL;
	}

	jx_cc_type_t* ty = jcc_typeAlloc(ctx, JCC_TYPE_PTR, 8, 8);
	if (!ty) {
		return NULL;
	}

	ty->m_BaseType = base;
	ty->m_Flags |= JCC_TYPE_FLAGS_IS_UNSIGNED_Msk;

	return ty;
}

static jx_cc_type_t* jcc_typeAllocFunc(jx_cc_context_t* ctx, jx_cc_type_t* return_ty)
{
	if (!return_ty) {
		return NULL;
	}

	// The C spec disallows sizeof(<function type>), but
	// GCC allows that and the expression is evaluated to 1.
	jx_cc_type_t* ty = jcc_typeAlloc(ctx, JCC_TYPE_FUNC, 1, 1);
	if (!ty) {
		return NULL;
	}

	ty->m_FuncRetType = return_ty;

	return ty;
}

static jx_cc_type_t* jcc_typeAllocArrayOf(jx_cc_context_t* ctx, jx_cc_type_t* base, int len)
{
	jx_cc_type_t* ty = jcc_typeAlloc(ctx, JCC_TYPE_ARRAY, base->m_Size * len, base->m_Alignment);
	if (!ty) {
		return NULL;
	}

	ty->m_BaseType = base;
	ty->m_ArrayLen = len;
	
	return ty;
}

static jx_cc_type_t* jcc_typeAllocEnum(jx_cc_context_t* ctx)
{
	return jcc_typeAlloc(ctx, JCC_TYPE_ENUM, 4, 4);
}

static jx_cc_type_t* jcc_typeAllocStruct(jx_cc_context_t* ctx)
{
	return jcc_typeAlloc(ctx, JCC_TYPE_STRUCT, 0, 1);
}

static jx_cc_type_t* jcc_typeGetCommon(jx_cc_context_t* ctx, jx_cc_type_t* ty1, jx_cc_type_t* ty2)
{
	if (ty1->m_BaseType) {
		return jcc_typeAllocPointerTo(ctx, ty1->m_BaseType);
	}
	
	if (ty1->m_Kind == JCC_TYPE_FUNC) {
		return jcc_typeAllocPointerTo(ctx, ty1);
	}
	
	if (ty2->m_Kind == JCC_TYPE_FUNC) {
		return jcc_typeAllocPointerTo(ctx, ty2);
	}
	
	if (ty1->m_Kind == JCC_TYPE_DOUBLE || ty2->m_Kind == JCC_TYPE_DOUBLE) {
		return kType_double;
	}
	
	if (ty1->m_Kind == JCC_TYPE_FLOAT || ty2->m_Kind == JCC_TYPE_FLOAT) {
		return kType_float;
	}
	
	if (ty1->m_Size < 4) {
		ty1 = kType_int;
	}
	
	if (ty2->m_Size < 4) {
		ty2 = kType_int;
	}
	
	if (ty1->m_Size != ty2->m_Size) {
		return (ty1->m_Size < ty2->m_Size) ? ty2 : ty1;
	}
	
	if ((ty2->m_Flags & JCC_TYPE_FLAGS_IS_UNSIGNED_Msk) != 0) {
		return ty2;
	}
	
	return ty1;
}

// For many binary operators, we implicitly promote operands so that
// both operands have the same type. Any integral type smaller than
// int is always promoted to int. If the type of one operand is larger
// than the other's (e.g. "long" vs. "int"), the smaller operand will
// be promoted to match with the other.
//
// This operation is called the "usual arithmetic conversion".
static bool jcc_astUsualArithmeticConversion(jx_cc_context_t* ctx, jx_cc_ast_expr_t** lhs, jx_cc_ast_expr_t** rhs)
{
	jx_cc_type_t* ty = jcc_typeGetCommon(ctx, (*lhs)->m_Type, (*rhs)->m_Type);
	if (!ty) {
		return false;
	}

	*lhs = jcc_astAllocExprCast(ctx, *lhs, ty);
	if (!(*lhs)) {
		return false;
	}

	*rhs = jcc_astAllocExprCast(ctx, *rhs, ty);
	if (!(*rhs)) {
		return false;
	}

	return true;
}

static bool jcc_astAddType(jx_cc_context_t* ctx, jx_cc_ast_node_t* node)
{
	switch (node->m_Kind) {
	case JCC_NODE_EXPR_NULL: {
		jx_cc_ast_expr_t* exprNode = (jx_cc_ast_expr_t*)node;
		if (!exprNode->m_Type) {
			exprNode->m_Type = kType_void;
		}
	} break;
	case JCC_NODE_EXPR_ADD:
	case JCC_NODE_EXPR_SUB:
	case JCC_NODE_EXPR_MUL:
	case JCC_NODE_EXPR_DIV:
	case JCC_NODE_EXPR_MOD:
	case JCC_NODE_EXPR_BITWISE_AND:
	case JCC_NODE_EXPR_BITWISE_OR:
	case JCC_NODE_EXPR_BITWISE_XOR: {
		jx_cc_ast_expr_t* exprNode = (jx_cc_ast_expr_t*)node;
		if (!exprNode->m_Type) {
			jx_cc_ast_expr_binary_t* binaryNode = (jx_cc_ast_expr_binary_t*)exprNode;

			if (!jcc_astAddType(ctx, &binaryNode->m_ExprLHS->super)) {
				return false;
			}
			if (!jcc_astAddType(ctx, &binaryNode->m_ExprRHS->super)) {
				return false;
			}

			if (!jcc_astUsualArithmeticConversion(ctx, &binaryNode->m_ExprLHS, &binaryNode->m_ExprRHS)) {
				return false;
			}

			exprNode->m_Type = binaryNode->m_ExprLHS->m_Type;
		}
	} break;
	case JCC_NODE_EXPR_NEG: {
		jx_cc_ast_expr_t* exprNode = (jx_cc_ast_expr_t*)node;
		if (!exprNode->m_Type) {
			jx_cc_ast_expr_unary_t* unaryNode = (jx_cc_ast_expr_unary_t*)exprNode;

			if (!jcc_astAddType(ctx, &unaryNode->m_Expr->super)) {
				return false;
			}

			jx_cc_type_t* ty = jcc_typeGetCommon(ctx, kType_int, unaryNode->m_Expr->m_Type);
			if (!ty) {
				return false;
			}

			unaryNode->m_Expr = jcc_astAllocExprCast(ctx, unaryNode->m_Expr, ty);
			if (!unaryNode->m_Expr) {
				return false;
			}

			exprNode->m_Type = ty;
		}
	} break;
	case JCC_NODE_EXPR_BITWISE_NOT: {
		jx_cc_ast_expr_t* exprNode = (jx_cc_ast_expr_t*)node;
		if (!exprNode->m_Type) {
			jx_cc_ast_expr_unary_t* unaryNode = (jx_cc_ast_expr_unary_t*)exprNode;

			if (!jcc_astAddType(ctx, &unaryNode->m_Expr->super)) {
				return false;
			}

			exprNode->m_Type = unaryNode->m_Expr->m_Type;
		}
	} break;
	case JCC_NODE_EXPR_LSHIFT:
	case JCC_NODE_EXPR_RSHIFT: {
		jx_cc_ast_expr_t* exprNode = (jx_cc_ast_expr_t*)node;
		if (!exprNode->m_Type) {
			jx_cc_ast_expr_binary_t* binaryNode = (jx_cc_ast_expr_binary_t*)exprNode;

			if (!jcc_astAddType(ctx, &binaryNode->m_ExprLHS->super)) {
				return false;
			}
			if (!jcc_astAddType(ctx, &binaryNode->m_ExprRHS->super)) {
				return false;
			}

			exprNode->m_Type = binaryNode->m_ExprLHS->m_Type;
		}
	} break;

	case JCC_NODE_EXPR_EQUAL:
	case JCC_NODE_EXPR_NOT_EQUAL:
	case JCC_NODE_EXPR_LESS_THAN:
	case JCC_NODE_EXPR_LESS_EQUAL: {
		jx_cc_ast_expr_t* exprNode = (jx_cc_ast_expr_t*)node;
		if (!exprNode->m_Type) {
			jx_cc_ast_expr_binary_t* binaryNode = (jx_cc_ast_expr_binary_t*)exprNode;

			if (!jcc_astAddType(ctx, &binaryNode->m_ExprLHS->super)) {
				return false;
			}
			if (!jcc_astAddType(ctx, &binaryNode->m_ExprRHS->super)) {
				return false;
			}

			if (!jcc_astUsualArithmeticConversion(ctx, &binaryNode->m_ExprLHS, &binaryNode->m_ExprRHS)) {
				return false;
			}

			exprNode->m_Type = kType_int;
		}
	} break;
	case JCC_NODE_EXPR_ASSIGN: {
		jx_cc_ast_expr_t* exprNode = (jx_cc_ast_expr_t*)node;
		if (!exprNode->m_Type) {
			jx_cc_ast_expr_binary_t* binaryNode = (jx_cc_ast_expr_binary_t*)exprNode;

			if (!jcc_astAddType(ctx, &binaryNode->m_ExprLHS->super)) {
				return false;
			}
			if (!jcc_astAddType(ctx, &binaryNode->m_ExprRHS->super)) {
				return false;
			}

			if (binaryNode->m_ExprLHS->m_Type->m_Kind == JCC_TYPE_ARRAY) {
				return false; // ERROR: not an lvalue
			}

			if (binaryNode->m_ExprLHS->m_Type->m_Kind != JCC_TYPE_STRUCT) {
				binaryNode->m_ExprRHS = jcc_astAllocExprCast(ctx, binaryNode->m_ExprRHS, binaryNode->m_ExprLHS->m_Type);
				if (!binaryNode->m_ExprRHS) {
					return false;
				}
			}

			exprNode->m_Type = binaryNode->m_ExprLHS->m_Type;
		}
	} break;
	case JCC_NODE_EXPR_CONDITIONAL: {
		jx_cc_ast_expr_t* exprNode = (jx_cc_ast_expr_t*)node;
		if (!exprNode->m_Type) {
			jx_cc_ast_expr_cond_t* condNode = (jx_cc_ast_expr_cond_t*)exprNode;

			if (!jcc_astAddType(ctx, &condNode->m_CondExpr->super)) {
				return false;
			}
			if (!jcc_astAddType(ctx, &condNode->m_ThenExpr->super)) {
				return false;
			}
			if (!jcc_astAddType(ctx, &condNode->m_ElseExpr->super)) {
				return false;
			}

			if (condNode->m_ThenExpr->m_Type->m_Kind == JCC_TYPE_VOID || condNode->m_ElseExpr->m_Type->m_Kind == JCC_TYPE_VOID) {
				exprNode->m_Type = kType_void;
			} else {
				if (!jcc_astUsualArithmeticConversion(ctx, &condNode->m_ThenExpr, &condNode->m_ElseExpr)) {
					return false;
				}

				exprNode->m_Type = condNode->m_ThenExpr->m_Type;
			}
		}
	} break;
	case JCC_NODE_EXPR_COMMA: {
		jx_cc_ast_expr_t* exprNode = (jx_cc_ast_expr_t*)node;
		if (!exprNode->m_Type) {
			jx_cc_ast_expr_binary_t* binaryNode = (jx_cc_ast_expr_binary_t*)exprNode;

			if (!jcc_astAddType(ctx, &binaryNode->m_ExprLHS->super)) {
				return false;
			}
			if (!jcc_astAddType(ctx, &binaryNode->m_ExprRHS->super)) {
				return false;
			}

			exprNode->m_Type = binaryNode->m_ExprRHS->m_Type;
		}
	} break;
	case JCC_NODE_EXPR_MEMBER: {
		jx_cc_ast_expr_t* exprNode = (jx_cc_ast_expr_t*)node;
		if (!exprNode->m_Type) {
			jx_cc_ast_expr_member_t* memberNode = (jx_cc_ast_expr_member_t*)exprNode;

			if (!jcc_astAddType(ctx, &memberNode->m_Expr->super)) {
				return false;
			}

			exprNode->m_Type = memberNode->m_Member->m_Type;
		}
	} break;
	case JCC_NODE_EXPR_ADDR: {
		jx_cc_ast_expr_t* exprNode = (jx_cc_ast_expr_t*)node;
		if (!exprNode->m_Type) {
			jx_cc_ast_expr_unary_t* unaryNode = (jx_cc_ast_expr_unary_t*)exprNode;

			if (!jcc_astAddType(ctx, &unaryNode->m_Expr->super)) {
				return false;
			}

			jx_cc_type_t* ty = unaryNode->m_Expr->m_Type;
			if (ty->m_Kind == JCC_TYPE_ARRAY) {
				exprNode->m_Type = jcc_typeAllocPointerTo(ctx, ty->m_BaseType);
			} else {
				exprNode->m_Type = jcc_typeAllocPointerTo(ctx, ty);
			}

			if (!exprNode->m_Type) {
				return false;
			}
		}
	} break;
	case JCC_NODE_EXPR_DEREF: {
		jx_cc_ast_expr_t* exprNode = (jx_cc_ast_expr_t*)node;
		if (!exprNode->m_Type) {
			jx_cc_ast_expr_unary_t* unaryNode = (jx_cc_ast_expr_unary_t*)exprNode;

			if (!jcc_astAddType(ctx, &unaryNode->m_Expr->super)) {
				return false;
			}

			if (!unaryNode->m_Expr->m_Type->m_BaseType) {
				return false; // ERROR: invalid pointer dereference
			}

			if (unaryNode->m_Expr->m_Type->m_BaseType->m_Kind == JCC_TYPE_VOID) {
				return false; // ERROR: dereferencing a void pointer
			}

			exprNode->m_Type = unaryNode->m_Expr->m_Type->m_BaseType;
		}
	} break;
	case JCC_NODE_EXPR_NOT: {
		jx_cc_ast_expr_t* exprNode = (jx_cc_ast_expr_t*)node;
		if (!exprNode->m_Type) {
			jx_cc_ast_expr_unary_t* unaryNode = (jx_cc_ast_expr_unary_t*)exprNode;

			if (!jcc_astAddType(ctx, &unaryNode->m_Expr->super)) {
				return false;
			}

			exprNode->m_Type = unaryNode->m_Expr->m_Type;
		}
	} break;
	case JCC_NODE_EXPR_LOGICAL_AND:
	case JCC_NODE_EXPR_LOGICAL_OR: {
		jx_cc_ast_expr_t* exprNode = (jx_cc_ast_expr_t*)node;
		if (!exprNode->m_Type) {
			jx_cc_ast_expr_binary_t* binaryNode = (jx_cc_ast_expr_binary_t*)exprNode;

			if (!jcc_astAddType(ctx, &binaryNode->m_ExprLHS->super)) {
				return false;
			}
			if (!jcc_astAddType(ctx, &binaryNode->m_ExprRHS->super)) {
				return false;
			}

			exprNode->m_Type = kType_int;
		}
	} break;
	case JCC_NODE_EXPR_FUNC_CALL: {
		jx_cc_ast_expr_t* exprNode = (jx_cc_ast_expr_t*)node;
		if (!exprNode->m_Type) {
			jx_cc_ast_expr_funccall_t* funcCallNode = (jx_cc_ast_expr_funccall_t*)exprNode;

			if (!jcc_astAddType(ctx, &funcCallNode->m_FuncExpr->super)) {
				return false;
			}

			for (uint32_t iArg = 0; iArg < funcCallNode->m_NumArgs; ++iArg) {
				if (!jcc_astAddType(ctx, (jx_cc_ast_node_t*)funcCallNode->m_Args[iArg])) {
					return false;
				}
			}

			exprNode->m_Type = funcCallNode->m_FuncType->m_FuncRetType;
		}
	} break;
	case JCC_NODE_STMT_RETURN: {
		jx_cc_ast_stmt_expr_t* exprNode = (jx_cc_ast_stmt_expr_t*)node;
		if (exprNode->m_Expr) {
			jcc_astAddType(ctx, &exprNode->m_Expr->super);
		}
	} break;
	case JCC_NODE_STMT_EXPR: {
		jx_cc_ast_stmt_expr_t* exprNode = (jx_cc_ast_stmt_expr_t*)node;
		jcc_astAddType(ctx, &exprNode->m_Expr->super);
	} break;
	case JCC_NODE_VARIABLE: {
		jx_cc_ast_expr_t* exprNode = (jx_cc_ast_expr_t*)node;
		if (!exprNode->m_Type) {
			jx_cc_ast_expr_variable_t* varNode = (jx_cc_ast_expr_variable_t*)exprNode;
			exprNode->m_Type = varNode->m_Var->m_Type;
		}
	} break;
	case JCC_NODE_NUMBER: {
		jx_cc_ast_expr_t* exprNode = (jx_cc_ast_expr_t*)node;
		if (!exprNode->m_Type) {
			exprNode->m_Type = kType_int;
		}
	} break;
	case JCC_NODE_STMT_IF: {
		jx_cc_ast_stmt_if_t* ifNode = (jx_cc_ast_stmt_if_t*)node;
		if (!jcc_astAddType(ctx, &ifNode->m_CondExpr->super)) {
			return false;
		}
		if (!jcc_astAddType(ctx, &ifNode->m_ThenStmt->super)) {
			return false;
		}
		if (ifNode->m_ElseStmt && !jcc_astAddType(ctx, &ifNode->m_ElseStmt->super)) {
			return false;
		}
	} break;
	case JCC_NODE_STMT_FOR: {
		jx_cc_ast_stmt_for_t* forNode = (jx_cc_ast_stmt_for_t*)node;
		if (forNode->m_InitStmt && !jcc_astAddType(ctx, &forNode->m_InitStmt->super)) {
			return false;
		}
		if (forNode->m_CondExpr && !jcc_astAddType(ctx, &forNode->m_CondExpr->super)) {
			return false;
		}
		if (forNode->m_IncExpr && !jcc_astAddType(ctx, &forNode->m_IncExpr->super)) {
			return false;
		}
		if (!jcc_astAddType(ctx, &forNode->m_BodyStmt->super)) {
			return false;
		}
	} break;
	case JCC_NODE_STMT_DO: {
		jx_cc_ast_stmt_do_t* doNode = (jx_cc_ast_stmt_do_t*)node;
		if (!jcc_astAddType(ctx, &doNode->m_CondExpr->super)) {
			return false;
		}
		if (!jcc_astAddType(ctx, &doNode->m_BodyStmt->super)) {
			return false;
		}
	} break;
	case JCC_NODE_STMT_SWITCH: {
		jx_cc_ast_stmt_switch_t* switchNode = (jx_cc_ast_stmt_switch_t*)node;
		if (!jcc_astAddType(ctx, &switchNode->m_CondExpr->super)) {
			return false;
		}
		if (!jcc_astAddType(ctx, &switchNode->m_BodyStmt->super)) {
			return false;
		}
		if (!jcc_astAddType(ctx, &switchNode->m_CaseListHead->super.super)) {
			return false;
		}
		if (switchNode->m_DefaultCase) {
			if (!jcc_astAddType(ctx, &switchNode->m_DefaultCase->super.super)) {
				return false;
			}
		}
	} break;
	case JCC_NODE_STMT_CASE: {
		jx_cc_ast_stmt_case_t* caseNode = (jx_cc_ast_stmt_case_t*)node;
		if (!jcc_astAddType(ctx, &caseNode->m_BodyStmt->super)) {
			return false;
		}
		if (caseNode->m_NextCase != NULL && !jcc_astAddType(ctx, &caseNode->m_NextCase->super.super)) {
			return false;
		}
	} break;
	case JCC_NODE_STMT_BLOCK: {
		jx_cc_ast_stmt_block_t* blockNode = (jx_cc_ast_stmt_block_t*)node;
		const uint32_t numChildren = blockNode->m_NumChildren;
		for (uint32_t iChild = 0; iChild < numChildren; ++iChild) {
			if (!jcc_astAddType(ctx, (jx_cc_ast_node_t*)blockNode->m_Children[iChild])) {
				return false;
			}
		}
	} break;
	case JCC_NODE_STMT_LABEL: {
		jx_cc_ast_stmt_label_t* lblNode = (jx_cc_ast_stmt_label_t*)node;
		if (!jcc_astAddType(ctx, (jx_cc_ast_node_t*)lblNode->m_Stmt)) {
			return false;
		}
	} break;
	case JCC_NODE_STMT_GOTO:
	case JCC_NODE_EXPR_CAST:
	case JCC_NODE_EXPR_MEMZERO:
	case JCC_NODE_STMT_ASM: {
		// No-op
	} break;
	case JCC_NODE_EXPR_COMPOUND_ASSIGN: {
		jx_cc_ast_expr_compound_assign_t* assignNode = (jx_cc_ast_expr_compound_assign_t*)node;
		if (!jcc_astAddType(ctx, (jx_cc_ast_node_t*)assignNode->m_ExprLHS)) {
			return false;
		}
		if (!jcc_astAddType(ctx, (jx_cc_ast_node_t*)assignNode->m_ExprRHS)) {
			return false;
		}
	} break;
	case JCC_NODE_EXPR_GET_ELEMENT_PTR: {
		jx_cc_ast_expr_get_element_ptr_t* gepNode = (jx_cc_ast_expr_get_element_ptr_t*)node;
		if (!jcc_astAddType(ctx, (jx_cc_ast_node_t*)gepNode->m_ExprPtr)) {
			return false;
		}
		if (!jcc_astAddType(ctx, (jx_cc_ast_node_t*)gepNode->m_ExprIndex)) {
			return false;
		}
	} break;
#if 0
	case JCC_NODE_STMT_CAS: {
		if (!jcc_astAddType(ctx, node->cas_addr)) {
			return false;
		}
		if (!jcc_astAddType(ctx, node->cas_old)) {
			return false;
		}
		if (!jcc_astAddType(ctx, node->cas_new)) {
			return false;
		}
		node->ty = kType_bool;

		if (node->cas_addr->ty->kind != JCC_TYPE_PTR) {
			return false; // ERROR: pointer expected
		}

		if (node->cas_old->ty->kind != JCC_TYPE_PTR) {
			return false; // ERROR: pointer expected
		}
	} break;
#endif
#if 0
	case JCC_NODE_STMT_EXCH: {
		if (node->lhs->ty->kind != JCC_TYPE_PTR) {
			return false; // ERROR: pointer expected
		}

		node->ty = node->lhs->ty->base;
	} break;
#endif
	default:
		JX_NOT_IMPLEMENTED();
		break;
	}

	return true;
}

// Consumes the current token if it matches `op`.
static bool jcc_tokExpectStr(jx_cc_token_t** tok, const char* op)
{
	const bool isOp = true
		&& jx_memcmp((*tok)->m_String, op, (*tok)->m_Length) == 0
		&& op[(*tok)->m_Length] == '\0'
		;

	if (!isOp) {
		return false;
	}

	*tok = (*tok)->m_Next;

	return true;
}

static bool jcc_tokIs(jx_cc_token_t* tok, jx_cc_token_kind kind)
{
	return tok->m_Kind == kind;
}

static bool jcc_tokExpect(jx_cc_token_t** tok, jx_cc_token_kind kind)
{
	if (!jcc_tokIs(*tok, kind)) {
		return false;
	}

	*tok = (*tok)->m_Next;

	return true;
}

// Create a new token.
static jx_cc_token_t* jcc_allocToken(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, jx_cc_token_kind kind, const char* start, const char* end)
{
	jx_cc_token_t* tok = JX_ALLOC(ctx->m_LinearAllocator, sizeof(jx_cc_token_t));
	if (!tok) {
		return NULL;
	}

	jx_memset(tok, 0, sizeof(jx_cc_token_t));
	tok->m_Kind = kind;
	tok->m_Length = (uint32_t)(end - start);
	tok->m_String = jx_strtable_insert(ctx->m_StringTable, start, tok->m_Length);
	tok->m_Loc.m_Filename = tu->m_CurFilename;
	tok->m_Loc.m_LineNum = tu->m_CurLineNumber;
	tok->m_Flags = 0
		| (tu->m_AtBeginOfLine ? JCC_TOKEN_FLAGS_AT_BEGIN_OF_LINE_Msk : 0)
		| (tu->m_HasSpace ? JCC_TOKEN_FLAGS_HAS_SPACE_Msk : 0)
		;

	tu->m_AtBeginOfLine = false;
	tu->m_HasSpace = false;

	return tok;
}

// Read an identifier and returns the length of it.
// If p does not point to a valid identifier, 0 is returned.
static uint32_t jcc_readIdentifier(const char* str)
{
	uint32_t cp = UINT32_MAX;
	uint32_t n = 0;
	if (!jx_utf8to_codepoint(&cp, &str[0], UINT32_MAX, &n)) {
		return 0; // ERROR: 
	}

	if (!jcc_isIdentifierCodepoint1(cp)) {
		return 0; // ERROR: 
	}

	uint32_t identifierLen = n;
	while (str[identifierLen] != '\0') {
		if (!jx_utf8to_codepoint(&cp, &str[identifierLen], UINT32_MAX, &n)) {
			return 0; // ERROR: 
		}

		if (!jcc_isIdentifierCodepoint2(cp)) {
			break;
		}

		identifierLen += n;
	}

	return identifierLen;
}

static uint32_t jcc_readEscapedChar(const char** new_pos, const char* p)
{
	uint32_t c = 0;

	if (p[0] >= '0' && p[0] <= '7') {
		// Read an octal number.
		c = *p++ - '0';
		if (p[0] >= '0' && p[0] <= '7') {
			c = (c << 3) | (*p++ - '0');

			if (p[0] >= '0' && p[0] <= '7') {
				c = (c << 3) | (*p++ - '0');
			}
		}
	} else if (p[0] == 'x') {
		// Read a hexadecimal number.
		p++;
		if (!jx_isxdigit(p[0])) {
			return UINT32_MAX; // ERROR: invalid hex escape sequence
		}

		for (; jx_isxdigit(p[0]); p++) {
			c = (c << 4) | jx_hexCharToInt(p[0]);
		}
	} else {
		// Escape sequences are defined using themselves here. E.g.
		// '\n' is implemented using '\n'. This tautological definition
		// works because the compiler that compiles our compiler knows
		// what '\n' actually is. In other words, we "inherit" the ASCII
		// code of '\n' from the compiler that compiles our compiler,
		// so we don't have to teach the actual code here.
		//
		// This fact has huge implications not only for the correctness
		// of the compiler but also for the security of the generated code.
		// For more info, read "Reflections on Trusting Trust" by Ken Thompson.
		// https://github.com/rui314/chibicc/wiki/thompson1984.pdf
		switch (p[0]) {
		case 'a': c = '\a'; break;
		case 'b': c = '\b'; break;
		case 't': c = '\t'; break;
		case 'n': c = '\n'; break;
		case 'v': c = '\v'; break;
		case 'f': c = '\f'; break;
		case 'r': c = '\r'; break;
		case 'e': c = 27;   break; // [GNU] \e for the ASCII escape character is a GNU C extension.
		default:
			c = p[0];
			break;
		}

		p++;
	}

	*new_pos = p;

	return c;
}

// Find a closing double-quote.
static const char* jcc_findStringLiteralEnd(const char* p)
{
	for (; *p != '"'; p++) {
		if (*p == '\n' || *p == '\0') {
			return NULL; // ERROR: unclosed string literal
		}

		if (*p == '\\') {
			p++;
		}
	}

	return p;
}

static jx_cc_token_t* jcc_readStringLiteral(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, const char* start, const char* quote)
{
	const char* end = jcc_findStringLiteralEnd(quote + 1);
	if (!end) {
		return NULL;
	}

	const size_t sz = end - quote;
	char* buf = (char*)JX_ALLOC(ctx->m_Allocator, sz);
	if (!buf) {
		return NULL;
	}
	jx_memset(buf, 0, sz);

	int len = 0;
	for (const char* p = quote + 1; p < end;) {
		if (*p == '\\') {
			uint32_t c = jcc_readEscapedChar(&p, p + 1);
			if (c == UINT32_MAX) {
				return NULL;
			}

			buf[len++] = (char)c;
		} else {
			buf[len++] = *p++;
		}
	}

	jx_cc_token_t* tok = jcc_allocToken(ctx, tu, JCC_TOKEN_STRING_LITERAL, start, end + 1);
	if (!tok) {
		return NULL;
	}

	tok->m_Type = jcc_typeAllocArrayOf(ctx, kType_char, len + 1);
	if (!tok->m_Type) {
		return NULL;
	}

	tok->m_Val_string = jx_strtable_insert(ctx->m_StringTable, buf, len);

	JX_FREE(ctx->m_Allocator, buf);

	return tok;
}

// Read a UTF-8-encoded string literal and transcode it in UTF-16.
//
// UTF-16 is yet another variable-width encoding for Unicode. Code
// points smaller than U+10000 are encoded in 2 bytes. Code points
// equal to or larger than that are encoded in 4 bytes. Each 2 bytes
// in the 4 byte sequence is called "surrogate", and a 4 byte sequence
// is called a "surrogate pair".
static jx_cc_token_t* jcc_readUTF16StringLiteral(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, const char* start, const char* quote)
{
	const char* end = jcc_findStringLiteralEnd(quote + 1);
	if (!end) {
		return NULL;
	}

	const size_t sz = sizeof(uint16_t) * (end - start);
	uint16_t* buf = (uint16_t*)JX_ALLOC(ctx->m_Allocator, sz);
	if (!buf) {
		return NULL;
	}
	jx_memset(buf, 0, sz);

	int len = 0;
	for (const char* p = quote + 1; p < end;) {
		if (*p == '\\') {
			uint32_t c = jcc_readEscapedChar(&p, p + 1);
			if (c == UINT32_MAX) {
				return NULL;
			}

			buf[len++] = (uint16_t)c;
		} else {
			uint32_t c = UINT32_MAX;
			uint32_t n = 0;
			if (!jx_utf8to_codepoint(&c, p, UINT32_MAX, &n)) {
				return NULL;
			}
			p += n;

			if (c < 0x10000) {
				// Encode a code point in 2 bytes.
				buf[len++] = (uint16_t)c;
			} else {
				// Encode a code point in 4 bytes.
				c -= 0x10000;
				buf[len++] = 0xd800 + ((c >> 10) & 0x3ff);
				buf[len++] = 0xdc00 + (c & 0x3ff);
			}
		}
	}

	jx_cc_token_t* tok = jcc_allocToken(ctx, tu, JCC_TOKEN_STRING_LITERAL, start, end + 1);
	if (!tok) {
		return NULL;
	}

	tok->m_Type = jcc_typeAllocArrayOf(ctx, kType_ushort, len + 1);
	if (!tok->m_Type) {
		return NULL;
	}

	tok->m_Val_string = jx_strtable_insert(ctx->m_StringTable, (const char*)buf, len * 2);

	JX_FREE(ctx->m_Allocator, buf);
	
	return tok;
}

// Read a UTF-8-encoded string literal and transcode it in UTF-32.
//
// UTF-32 is a fixed-width encoding for Unicode. Each code point is
// encoded in 4 bytes.
static jx_cc_token_t* jcc_readUTF32StringLiteral(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, const char* start, const char* quote, jx_cc_type_t* ty)
{
	const char* end = jcc_findStringLiteralEnd(quote + 1);
	if (!end) {
		return NULL;
	}

	const size_t sz = sizeof(uint32_t) * (end - quote);
	uint32_t* buf = (uint32_t*)JX_ALLOC(ctx->m_Allocator, sz);
	if (!buf) {
		return NULL;
	}
	jx_memset(buf, 0, sz);

	int len = 0;
	for (const char* p = quote + 1; p < end;) {
		if (*p == '\\') {
			uint32_t c = jcc_readEscapedChar(&p, p + 1);
			if (c == UINT32_MAX) {
				return NULL;
			}
			buf[len++] = c;
		} else {
			uint32_t n = 0;
			uint32_t c = UINT32_MAX;
			if (!jx_utf8to_codepoint(&c, p, UINT32_MAX, &n)) {
				return NULL;
			}
			p += n;
			buf[len++] = c;
		}
	}

	jx_cc_token_t* tok = jcc_allocToken(ctx, tu, JCC_TOKEN_STRING_LITERAL, start, end + 1);
	if (!tok) {
		return NULL;
	}

	tok->m_Type = jcc_typeAllocArrayOf(ctx, ty, len + 1);
	if (!tok->m_Type) {
		return NULL;
	}

	tok->m_Val_string = jx_strtable_insert(ctx->m_StringTable, (const char*)buf, len * 4);

	JX_FREE(ctx->m_Allocator, buf);

	return tok;
}

static jx_cc_token_t* jcc_readCharLiteral(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, const char* start, const char* quote, jx_cc_type_t* ty)
{
	const char* p = quote + 1;
	if (*p == '\0') {
		return NULL; // ERROR: unclosed char literal
	}

	uint32_t c = UINT32_MAX;
	if (*p == '\\') {
		c = jcc_readEscapedChar(&p, p + 1);
	} else {
		uint32_t n = 0;
		if (!jx_utf8to_codepoint(&c, p, UINT32_MAX, &n)) {
			return NULL;
		}
	
		p += n;
	}

	const char* end = jx_strchr(p, '\'');
	if (!end) {
		return NULL; // ERROR: unclosed char literal
	}

	jx_cc_token_t* tok = jcc_allocToken(ctx, tu, JCC_TOKEN_NUMBER, start, end + 1);
	if (!tok) {
		return NULL;
	}

	tok->m_Val_int = c;
	tok->m_Type = ty;

	return tok;
}

static jx_cc_token_t* jcc_readKnownTokenOrIdentifier(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, const char* str)
{
	jx_cc_token_t* token = NULL;

	const uint32_t identifierLen = jcc_readIdentifier(str);
	if (identifierLen) {
		jx_cc_token_kind kind = JCC_TOKEN_IDENTIFIER;

		const uint32_t numKeywords = sizeof(kKeywords) / sizeof(jcc_known_token_t);
		for (uint32_t iKeyword = 0; iKeyword < numKeywords; ++iKeyword) {
			const jcc_known_token_t* keyword = &kKeywords[iKeyword];
			if (identifierLen == keyword->m_Len && !jx_strncmp(str, keyword->m_Name, identifierLen)) {
				kind = keyword->m_Kind;
				break;
			}
		}

		token = jcc_allocToken(ctx, tu, kind, str, str + identifierLen);
	} else {
		// Punctuator?
		const uint32_t numPunctuators = sizeof(kPunctuators) / sizeof(jcc_known_token_t);
		for (uint32_t iPunct = 0; iPunct < numPunctuators; ++iPunct) {
			const jcc_known_token_t* punct = &kPunctuators[iPunct];
			if (!jx_strncmp(str, punct->m_Name, punct->m_Len)) {
				token = jcc_allocToken(ctx, tu, punct->m_Kind, str, str + punct->m_Len);
				break;
			}
		}
	}

	return token;
}

static bool jcc_convertPreprocessorInteger(jx_cc_token_t* tok)
{
	const char* p = tok->m_String;

	// Read a binary, octal, decimal or hexadecimal number.
	int base = 10;
	if (p[0] == '0' && p[1] == 'x' && jx_isxdigit(p[2])) {
		p += 2;
		base = 16;
	} else if (p[0] == '0' && p[1] == 'b' && (p[2] == '0' || p[2] == '1')) {
		p += 2;
		base = 2;
	} else if (p[0] == '0') {
		base = 8;
	}

	int64_t val = jx_strto_int(p, UINT32_MAX, &p, base, INT64_MAX);

	// Read U, L or LL suffixes.
	bool l = false;
	bool u = false;
	if (p[0] == 'u' || p[0] == 'U') {
		p++;
		u = true;

		if (p[0] == 'l' || p[0] == 'L') {
			if (p[1] == p[0]) {
				p += 2;
				l = true;
			} else {
				p++;
				l = true;
			}
		}
	} else if (p[0] == 'l' || p[0] == 'L') {
		// l, L, lu, lU, Lu, LU, LL, ll, llu, llU, LLu, LLU
		l = true;

		if (p[0] == p[1]) {
			// LL, ll, llu, llU, LLu, LLU
			p += 2;

			if (p[0] == 'u' || p[0] == 'U') {
				// llU, llU, LLu, LLU
				u = true;
				p++;
			} else {
				// ll, LL
			}
		} else {
			// l, L, lu, lU, Lu, LU
			p++;
			if (p[0] == 'u' || p[0] == 'U') {
				// lu, lU, Lu, LU
				u = true;
				p++;
			} else {
				// l, L
			}
		}
	}

	if (*p != '\0') {
		return false;
	}

	// Infer a type.
	jx_cc_type_t* ty = kType_int;
	if (base == 10) {
		if (l && u) {
			ty = kType_ulong;
		} else if (l) {
			ty = kType_long;
		} else if (u) {
			ty = (val >> 32)
				? kType_ulong
				: kType_uint
				;
		} else {
			ty = (val >> 31)
				? kType_long
				: kType_int
				;
		}
	} else {
		if (l && u) {
			ty = kType_ulong;
		} else if (l) {
			ty = (val >> 63)
				? kType_ulong
				: kType_long
				;
		} else if (u) {
			ty = (val >> 32)
				? kType_ulong
				: kType_uint
				;
		} else if (val >> 63) {
			ty = kType_ulong;
		} else if (val >> 32) {
			ty = kType_long;
		} else if (val >> 31) {
			ty = kType_uint;
		} else {
			ty = kType_int;
		}
	}

	tok->m_Kind = JCC_TOKEN_NUMBER;
	tok->m_Val_int = val;
	tok->m_Type = ty;

	return true;
}

// The definition of the numeric literal at the preprocessing stage
// is more relaxed than the definition of that at the later stages.
// In order to handle that, a numeric literal is tokenized as a
// "pp-number" token first and then converted to a regular number
// token after preprocessing.
//
// This function converts a pp-number token to a regular number token.
static bool jcc_convertPreprocessorNumber(jx_cc_token_t* tok)
{
	// Try to parse as an integer constant.
	if (jcc_convertPreprocessorInteger(tok)) {
		return true;
	}

	// If it's not an integer, it must be a floating point constant.
	// NOTE: val below used to be a long double.
	char* end;
	double val = jx_strto_double(tok->m_String, UINT32_MAX, &end, 0.0);

	jx_cc_type_t* ty;
	if (*end == 'f' || *end == 'F') {
		ty = kType_float;
		end++;
	} else if (*end == 'l' || *end == 'L') {
		ty = kType_double;
		end++;
	} else {
		ty = kType_double;
	}

	if (*end != '\0') {
		return false; // ERROR: invalid numeric constant
	}

	tok->m_Kind = JCC_TOKEN_NUMBER;
	tok->m_Val_float = val;
	tok->m_Type = ty;

	return true;
}

// Replaces \r or \r\n with \n.
static void jcc_canonicalizeNewlines(char* p)
{
	int i = 0, j = 0;

	while (p[i]) {
		if (p[i] == '\r' && p[i + 1] == '\n') {
			i += 2;
			p[j++] = '\n';
		} else if (p[i] == '\r') {
			i++;
			p[j++] = '\n';
		} else {
			p[j++] = p[i++];
		}
	}

	p[j] = '\0';
}

// Removes backslashes followed by a newline.
static void jcc_removeBackslashNewline(char* p)
{
	int i = 0, j = 0;

	// We want to keep the number of newline characters so that
	// the logical line number matches the physical one.
	// This counter maintain the number of newlines we have removed.
	int n = 0;

	while (p[i]) {
		if (p[i] == '\\' && p[i + 1] == '\n') {
			i += 2;
			n++;
		} else if (p[i] == '\n') {
			p[j++] = p[i++];
			for (; n > 0; n--) {
				p[j++] = '\n';
			}
		} else {
			p[j++] = p[i++];
		}
	}

	for (; n > 0; n--) {
		p[j++] = '\n';
	}
	p[j] = '\0';
}

static uint32_t jcc_readUniversalChar(const char* p, uint32_t len)
{
	uint32_t c = 0;
	for (uint32_t i = 0; i < len; i++) {
		if (!jx_isxdigit(p[i])) {
			return 0;
		}

		c = (c << 4) | jx_hexCharToInt(p[i]);
	}

	return c;
}

// Replace \u or \U escape sequences with corresponding UTF-8 bytes.
static void jcc_convertUniversalChars(char* p)
{
	char* q = p;

	while (*p) {
		if (p[0] == '\\' && p[1] == 'u') {
			const uint32_t c = jcc_readUniversalChar(p + 2, 4);
			if (c) {
				p += 6;
				q += jx_utf8from_utf32(q, 6, &c, 1);
			} else {
				*q++ = *p++;
			}
		} else if (p[0] == '\\' && p[1] == 'U') {
			const uint32_t c = jcc_readUniversalChar(p + 2, 8);
			if (c) {
				p += 10;
				q += jx_utf8from_utf32(q, 10, &c, 1);
			} else {
				*q++ = *p++;
			}
		} else if (p[0] == '\\') {
			*q++ = *p++;
			*q++ = *p++;
		} else {
			*q++ = *p++;
		}
	}

	*q = '\0';
}

static jx_cc_token_t* jcc_tokenizeString(jx_cc_context_t* ctx, jcc_translation_unit_t* tu, char* source, uint64_t sourceLen)
{
	char* p = source;

	// UTF-8 texts may start with a 3-byte "BOM" marker sequence.
	// If exists, just skip them because they are useless bytes.
	// (It is actually not recommended to add BOM markers to UTF-8
	// texts, but it's not uncommon particularly on Windows.)
	if (jx_utf8IsBOM(p)) {
		p += JX_UTF8_BOM_LEN;
	}

	jcc_canonicalizeNewlines(p);
	jcc_removeBackslashNewline(p);
	jcc_convertUniversalChars(p);

	jx_cc_token_t head = { 0 };
	jx_cc_token_t* cur = &head;

	tu->m_AtBeginOfLine = true;
	tu->m_HasSpace = false;
	tu->m_CurLineNumber = 1;

	while (*p) {
		const char* start = p;

		if (p[0] == '/' && p[1] == '/') {
			// Skip line comments.
			p += 2;
			while (*p != '\0' && *p != '\n') {
				p++;
			}
			// Don't skip \n. It will be handled in the next loop iteration

			tu->m_HasSpace = true;
		} else if (p[0] == '/' && p[1] == '*') {
			// Skip block comments.
			p += 2;
			while (*p != '\0' && !(p[0] == '*' && p[1] == '/')) {
				if (p[0] == '\n') {
					tu->m_CurLineNumber++;
				}
				p++;
			}
			p += 2;

			tu->m_HasSpace = true;
		} else if (p[0] == '\n') {
			// Skip newline.
			p++;
			tu->m_CurLineNumber++;
			tu->m_AtBeginOfLine = true;
			tu->m_HasSpace = false;
		} else if (jx_isspace(p[0])) {
			// Skip whitespace characters.
			p++;
			tu->m_HasSpace = true;
		} else if (jx_isdigit(p[0]) || (p[0] == '.' && jx_isdigit(p[1]))) {
			// Numeric literal
			const char* q = p++;
			for (;;) {
				if (p[0] && p[1] && (jx_tolower(p[0]) == 'e' || jx_tolower(p[0]) == 'p') && (p[1] == '+' || p[1] == '-')) {
					p += 2;
				} else if (jx_isdigit(p[0]) || jx_isalpha(p[0]) || p[0] == '.') {
					p++;
				} else {
					break;
				}
			}

			cur->m_Next = jcc_allocToken(ctx, tu, JCC_TOKEN_PREPROC_NUMBER, q, p);
			if (!cur->m_Next) {
				jcc_logError(ctx, JCC_SOURCE_LOCATION_CUR(), "Internal Error: Memory allocation failed.");
				return NULL;
			}
			cur = cur->m_Next;
		} else if (p[0] == '"') {
			// String literal
			cur->m_Next = jcc_readStringLiteral(ctx, tu, p, p);
			if (!cur->m_Next) {
				jcc_logError(ctx, JCC_SOURCE_LOCATION_CUR(), "Internal Error: Memory allocation failed.");
				return NULL;
			}
			cur = cur->m_Next;
			p += cur->m_Length;
		} else if (p[0] == 'u' && p[1] == '8' && p[2] == '\"') {
			// UTF-8 string literal
			cur->m_Next = jcc_readStringLiteral(ctx, tu, p, p + 2);
			if (!cur->m_Next) {
				jcc_logError(ctx, JCC_SOURCE_LOCATION_CUR(), "Internal Error: Memory allocation failed.");
				return NULL;
			}
			cur = cur->m_Next;
			p += cur->m_Length;
		} else if (p[0] == 'u' && p[1] == '\"') {
			// UTF-16 string literal
			cur->m_Next = jcc_readUTF16StringLiteral(ctx, tu, p, p + 1);
			if (!cur->m_Next) {
				jcc_logError(ctx, JCC_SOURCE_LOCATION_CUR(), "Internal Error: Memory allocation failed.");
				return NULL;
			}
			cur = cur->m_Next;
			p += cur->m_Length;
		} else if (p[0] == 'L' && p[1] == '\"') {
			// Wide string literal
			// TODO: What type should L strings be?
			cur->m_Next = jcc_readUTF32StringLiteral(ctx, tu, p, p + 1, kType_int);
			if (!cur->m_Next) {
				jcc_logError(ctx, JCC_SOURCE_LOCATION_CUR(), "Internal Error: Memory allocation failed.");
				return NULL;
			}
			cur = cur->m_Next;
			p += cur->m_Length;
		} else if (p[0] == 'U' && p[1] == '\"') {
			// UTF-32 string literal
			cur->m_Next = jcc_readUTF32StringLiteral(ctx, tu, p, p + 1, kType_uint);
			if (!cur->m_Next) {
				jcc_logError(ctx, JCC_SOURCE_LOCATION_CUR(), "Internal Error: Memory allocation failed.");
				return NULL;
			}
			cur = cur->m_Next;
			p += cur->m_Length;
		} else if (*p == '\'') {
			// Character literal
			cur->m_Next = jcc_readCharLiteral(ctx, tu, p, p, kType_int);
			if (!cur->m_Next) {
				jcc_logError(ctx, JCC_SOURCE_LOCATION_CUR(), "Internal Error: Memory allocation failed.");
				return NULL;
			}
			cur = cur->m_Next;
			cur->m_Val_int = (char)cur->m_Val_int;
			p += cur->m_Length;
		} else if (p[0] == 'u' && p[1] == '\'') {
			// UTF-16 character literal
			cur->m_Next = jcc_readCharLiteral(ctx, tu, p, p + 1, kType_ushort);
			if (!cur->m_Next) {
				jcc_logError(ctx, JCC_SOURCE_LOCATION_CUR(), "Internal Error: Memory allocation failed.");
				return NULL;
			}
			cur = cur->m_Next;
			cur->m_Val_int &= 0xffff;
			p += cur->m_Length;
		} else if (p[0] == 'L' && p[1] == '\'') {
			// Wide character literal
			cur->m_Next = jcc_readCharLiteral(ctx, tu, p, p + 1, kType_int);
			if (!cur->m_Next) {
				jcc_logError(ctx, JCC_SOURCE_LOCATION_CUR(), "Internal Error: Memory allocation failed.");
				return NULL;
			}
			cur = cur->m_Next;
			p += cur->m_Length;
		} else if (p[0] == 'U' && p[1] == '\'') {
			// UTF-32 character literal
			cur->m_Next = jcc_readCharLiteral(ctx, tu, p, p + 1, kType_uint);
			if (!cur->m_Next) {
				jcc_logError(ctx, JCC_SOURCE_LOCATION_CUR(), "Internal Error: Memory allocation failed.");
				return NULL;
			}
			cur = cur->m_Next;
			p += cur->m_Length;
		} else {
			// Keyword, punctuator or identifier
			cur->m_Next = jcc_readKnownTokenOrIdentifier(ctx, tu, p);
			if (!cur->m_Next) {
				jcc_logError(ctx, JCC_SOURCE_LOCATION_CUR(), "Internal Error: Memory allocation failed.");
				return NULL;
			}
			cur = cur->m_Next;
			p += cur->m_Length;
		}

		if (p == start) {
			jcc_logError(ctx, JCC_SOURCE_LOCATION_MAKE(tu->m_CurFilename, tu->m_CurLineNumber), "Unknown or invalid token '%c'", *p);
			return NULL;
		}
	}

	cur->m_Next = jcc_allocToken(ctx, tu, JCC_TOKEN_EOF, p, p);
	if (!cur->m_Next) {
		jcc_logError(ctx, JCC_SOURCE_LOCATION_CUR(), "Internal Error: Memory allocation failed.");
		return NULL;
	}
	cur = cur->m_Next;

	// Concatenate adjacent string literal tokens.
	{
		jx_cc_token_t* tok = head.m_Next;
		while (tok) {
			if (tok->m_Kind == JCC_TOKEN_STRING_LITERAL) {
				while (tok->m_Next->m_Kind == JCC_TOKEN_STRING_LITERAL) {
					// Concatenate strings
					const char* str1 = tok->m_Val_string;
					const char* str2 = tok->m_Next->m_Val_string;

					const uint32_t len1 = jx_strlen(str1);
					const uint32_t len2 = jx_strlen(str2);
					char* tmpBuf = (char*)JX_ALLOC(ctx->m_Allocator, len1 + len2);
					if (!tmpBuf) {
						jcc_logError(ctx, JCC_SOURCE_LOCATION_CUR(), "Internal Error: Memory allocation failed.");
						return NULL;
					}

					jx_memcpy(&tmpBuf[0], str1, len1);
					jx_memcpy(&tmpBuf[len1], str2, len2);
					tok->m_Val_string = jx_strtable_insert(ctx->m_StringTable, tmpBuf, len1 + len2);

					JX_FREE(ctx->m_Allocator, tmpBuf);

					tok->m_Type = jcc_typeAllocArrayOf(ctx, tok->m_Type->m_BaseType, len1 + len2);
					tok->m_Next = tok->m_Next->m_Next;
				}
			}

			tok = tok->m_Next;
		}
	}

	return head.m_Next;
}

static bool jcc_inRange(const uint32_t* range, uint32_t c)
{
	for (uint32_t i = 0; range[i] != UINT32_MAX; i += 2) {
		if (range[i] <= c && c <= range[i + 1]) {
			return true;
		}
	}

	return false;
}

// [https://www.sigbus.info/n1570#D] C11 allows not only ASCII but
// some multibyte characters in certan Unicode ranges to be used in an
// identifier.
//
// This function returns true if a given character is acceptable as
// the first character of an identifier.
static bool jcc_isIdentifierCodepoint1(uint32_t c)
{
	static const uint32_t range[] = {
		'_', '_',
		'a', 'z',
		'A', 'Z',
		'$', '$',
		0x00A8, 0x00A8,
		0x00AA, 0x00AA,
		0x00AD, 0x00AD,
		0x00AF, 0x00AF,
		0x00B2, 0x00B5,
		0x00B7, 0x00BA,
		0x00BC, 0x00BE,
		0x00C0, 0x00D6,
		0x00D8, 0x00F6,
		0x00F8, 0x00FF,
		0x0100, 0x02FF,
		0x0370, 0x167F,
		0x1681, 0x180D,
		0x180F, 0x1DBF,
		0x1E00, 0x1FFF,
		0x200B, 0x200D,
		0x202A, 0x202E,
		0x203F, 0x2040,
		0x2054, 0x2054,
		0x2060, 0x206F,
		0x2070, 0x20CF,
		0x2100, 0x218F,
		0x2460, 0x24FF,
		0x2776, 0x2793,
		0x2C00, 0x2DFF,
		0x2E80, 0x2FFF,
		0x3004, 0x3007,
		0x3021, 0x302F,
		0x3031, 0x303F,
		0x3040, 0xD7FF,
		0xF900, 0xFD3D,
		0xFD40, 0xFDCF,
		0xFDF0, 0xFE1F,
		0xFE30, 0xFE44,
		0xFE47, 0xFFFD,
		0x10000, 0x1FFFD,
		0x20000, 0x2FFFD,
		0x30000, 0x3FFFD,
		0x40000, 0x4FFFD,
		0x50000, 0x5FFFD,
		0x60000, 0x6FFFD,
		0x70000, 0x7FFFD,
		0x80000, 0x8FFFD,
		0x90000, 0x9FFFD,
		0xA0000, 0xAFFFD,
		0xB0000, 0xBFFFD,
		0xC0000, 0xCFFFD,
		0xD0000, 0xDFFFD,
		0xE0000, 0xEFFFD,
		UINT32_MAX,
	};

	return jcc_inRange(range, c);
}

// Returns true if a given character is acceptable as a non-first
// character of an identifier.
static bool jcc_isIdentifierCodepoint2(uint32_t c)
{
	static const uint32_t range[] = {
		'0', '9',
		'$', '$',
		0x0300, 0x036F,
		0x1DC0, 0x1DFF,
		0x20D0, 0x20FF,
		0xFE20, 0xFE2F,
		UINT32_MAX,
	};

	return false
		|| jcc_isIdentifierCodepoint1(c)
		|| jcc_inRange(range, c)
		;
}

static void jcc_logError(jx_cc_context_t* ctx, const jx_cc_source_loc_t* loc, const char* fmt, ...)
{
	char str[1024];

	va_list argList;
	va_start(argList, fmt);
	jx_vsnprintf(str, JX_COUNTOF(str), fmt, argList);
	va_end(argList);

	JX_LOG_ERROR(ctx->m_Logger, "jcc", "%s(%u): %s", loc->m_Filename, loc->m_LineNum, str);
}
