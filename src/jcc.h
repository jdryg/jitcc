#ifndef JX_CC_H
#define JX_CC_H

#include <stdint.h>
#include <stdbool.h>
#include <jlib/macros.h> // JX_PAD

#if JX_PLATFORM_WINDOWS
#define JCC_CONFIG_MAX_RETVAL_SIZE 8 // 1 reg
#define JCC_CONFIG_LLP64           1 // long = int32_t, long long = int64_t
#else
#error "Platform not supported yet"
#endif

typedef struct jx_allocator_i jx_allocator_i;
typedef struct jx_logger_i jx_logger_i;
typedef enum jx_file_base_dir jx_file_base_dir;

typedef struct jx_cc_type_t jx_cc_type_t;
typedef struct jx_cc_ast_node_t jx_cc_ast_node_t;
typedef struct jx_cc_struct_member_t jx_cc_struct_member_t;
typedef struct jx_cc_relocation_t jx_cc_relocation_t;
typedef struct jx_cc_token_t jx_cc_token_t;
typedef struct jx_cc_object_t jx_cc_object_t;

typedef struct jx_cc_ast_stmt_t jx_cc_ast_stmt_t;
typedef struct jx_cc_ast_stmt_case_t jx_cc_ast_stmt_case_t;
typedef struct jx_cc_ast_stmt_goto_t jx_cc_ast_stmt_goto_t;
typedef struct jx_cc_ast_stmt_label_t jx_cc_ast_stmt_label_t;

typedef struct jx_cc_hideset_t jx_cc_hideset_t;

// token
typedef enum jx_cc_token_kind 
{
	JCC_TOKEN_IDENTIFIER,     // Identifiers
	JCC_TOKEN_STRING_LITERAL, // String literals
	JCC_TOKEN_NUMBER,         // Numeric literals
	JCC_TOKEN_PREPROC_NUMBER, // Preprocessing numbers
	JCC_TOKEN_EOF,            // End-of-file markers

	// keywords
	JCC_TOKEN_RETURN,        // "return"
	JCC_TOKEN_IF,            // "if"
	JCC_TOKEN_ELSE,          // "else"
	JCC_TOKEN_FOR,           // "for"
	JCC_TOKEN_WHILE,         // "while"
	JCC_TOKEN_INT,           // "int"
	JCC_TOKEN_SIZEOF,        // "sizeof"
	JCC_TOKEN_CHAR,          // "char"
	JCC_TOKEN_STRUCT,        // "struct"
	JCC_TOKEN_UNION,         // "union"
	JCC_TOKEN_SHORT,         // "short"
	JCC_TOKEN_LONG,          // "long"
	JCC_TOKEN_VOID,          // "void"
	JCC_TOKEN_TYPEDEF,       // "typedef"
	JCC_TOKEN_BOOL,          // "_Bool"
	JCC_TOKEN_ENUM,          // "enum"
	JCC_TOKEN_STATIC,        // "static"
	JCC_TOKEN_GOTO,          // "goto"
	JCC_TOKEN_BREAK,         // "break"
	JCC_TOKEN_CONTINUE,      // "continue"
	JCC_TOKEN_SWITCH,        // "switch"
	JCC_TOKEN_CASE,          // "case"
	JCC_TOKEN_DEFAULT,       // "default"
	JCC_TOKEN_EXTERN,        // "extern"
	JCC_TOKEN_ALIGNOF,       // "_Alignof"
	JCC_TOKEN_ALIGNAS,       // "_Alignas"
	JCC_TOKEN_DO,            // "do"
	JCC_TOKEN_SIGNED,        // "signed"
	JCC_TOKEN_UNSIGNED,      // "unsigned"
	JCC_TOKEN_CONST,         // "const"
	JCC_TOKEN_VOLATILE,      // "volatile"
	JCC_TOKEN_AUTO,          // "auto"
	JCC_TOKEN_REGISTER,      // "register"
	JCC_TOKEN_RESTRICT,      // "restrict", "__restrict", "__restrict__"
	JCC_TOKEN_NORETURN,      // "_Noreturn"
	JCC_TOKEN_FLOAT,         // "float"
	JCC_TOKEN_DOUBLE,        // "double"
	JCC_TOKEN_TYPEOF,        // "typeof"
	JCC_TOKEN_ASM,           // "asm"
	JCC_TOKEN_THREAD_LOCAL,  // "_Thread_local", "__thread"
	JCC_TOKEN_ATOMIC,        // "_Atomic"
	JCC_TOKEN_ATTRIBUTE,     // "__attribute__"
	JCC_TOKEN_INLINE,        // "inline"
	JCC_TOKEN_GENERIC,       // "_Generic"
	JCC_TOKEN_PACKED,        // "packed"
	JCC_TOKEN_ALIGNED,       // "aligned"
	JCC_TOKEN_INCLUDE,       // "include"
	JCC_TOKEN_INCLUDE_NEXT,  // "include_next"
	JCC_TOKEN_DEFINE,        // "define"
	JCC_TOKEN_UNDEF,         // "undef"
	JCC_TOKEN_IFDEF,         // "ifdef"
	JCC_TOKEN_IFNDEF,        // "ifndef"
	JCC_TOKEN_ELIF,          // "elif"
	JCC_TOKEN_ENDIF,         // "endif"
	JCC_TOKEN_PRAGMA,        // "pragma"
	JCC_TOKEN_ONCE,          // "once"
	JCC_TOKEN_ERROR,         // "error"
	JCC_TOKEN_DEFINED,       // "defined"
	JCC_TOKEN_VA_OPT,        // "__VA_OPT__"

	// punctuators
	JCC_TOKEN_LSHIFT_ASSIGN,      // "<<=",
	JCC_TOKEN_RSHIFT_ASSIGN,      // ">>=",
	JCC_TOKEN_ELLIPSIS,           // "...",
	JCC_TOKEN_EQUAL,              // "==",
	JCC_TOKEN_NOT_EQUAL,          // "!=",
	JCC_TOKEN_LESS_EQUAL,         // "<=",
	JCC_TOKEN_GREATER_EQUAL,      // ">=",
	JCC_TOKEN_PTR,                // "->",
	JCC_TOKEN_ADD_ASSIGN,         // "+=",
	JCC_TOKEN_SUB_ASSIGN,         // "-=",
	JCC_TOKEN_MUL_ASSIGN,         // "*=",
	JCC_TOKEN_DIV_ASSIGN,         // "/=",
	JCC_TOKEN_INC,                // "++",
	JCC_TOKEN_DEC,                // "--",
	JCC_TOKEN_MOD_ASSIGN,         // "%=",
	JCC_TOKEN_AND_ASSIGN,         // "&=",
	JCC_TOKEN_OR_ASSIGN,          // "|=",
	JCC_TOKEN_XOR_ASSIGN,         // "^=",
	JCC_TOKEN_LOGICAL_AND,        // "&&",
	JCC_TOKEN_LOGICAL_OR,         // "||",
	JCC_TOKEN_LSHIFT,             // "<<",
	JCC_TOKEN_RSHIFT,             // ">>",
	JCC_TOKEN_HASHASH,            // "##",
	JCC_TOKEN_LOGICAL_NOT,        // "!",
	JCC_TOKEN_DOUBLE_QUOTE,       // "\"",
	JCC_TOKEN_HASH,               // "#",
	JCC_TOKEN_DOLLAR,             // "$",
	JCC_TOKEN_MOD,                // "%",
	JCC_TOKEN_AND,                // "&",
	JCC_TOKEN_SINGLE_QUOTE,       // "'",
	JCC_TOKEN_OPEN_PAREN,         // "(",
	JCC_TOKEN_CLOSE_PAREN,        // ")",
	JCC_TOKEN_MUL,                // "*",
	JCC_TOKEN_ADD,                // "+",
	JCC_TOKEN_COMMA,              // ",",
	JCC_TOKEN_SUB,                // "-",
	JCC_TOKEN_DOT,                // ".",
	JCC_TOKEN_DIV,                // "/",
	JCC_TOKEN_COLON,              // ":",
	JCC_TOKEN_SEMICOLON,          // ";",
	JCC_TOKEN_LESS,               // "<",
	JCC_TOKEN_ASSIGN,             // "=",
	JCC_TOKEN_GREATER,            // ">",
	JCC_TOKEN_QUESTIONMARK,       // "?",
	JCC_TOKEN_AT,                 // "@",
	JCC_TOKEN_OPEN_BRACKET,       // "[",
	JCC_TOKEN_BACKWORD_SLASH,     // "\\",
	JCC_TOKEN_CLOSE_BRACKET,      // "]",
	JCC_TOKEN_XOR,                // "^",
	JCC_TOKEN_UNDERSCORE,         // "_",
	JCC_TOKEN_GRAVE_ACCENT,       // "`",
	JCC_TOKEN_OPEN_CURLY_BRACKET, // "{",
	JCC_TOKEN_OR,                 // "|",
	JCC_TOKEN_CLOSE_CURLY_BRACKET, // "}",
	JCC_TOKEN_NOT,                 // "~",
} jx_cc_token_kind;

typedef struct jx_cc_source_loc_t
{
	const char* m_Filename;
	uint32_t m_LineNum;
	JX_PAD(4);
} jx_cc_source_loc_t;

typedef struct jx_cc_label_t
{
	uint32_t m_ID;
} jx_cc_label_t;

#define JCC_TOKEN_FLAGS_AT_BEGIN_OF_LINE_Pos 0
#define JCC_TOKEN_FLAGS_AT_BEGIN_OF_LINE_Msk (1u << JCC_TOKEN_FLAGS_AT_BEGIN_OF_LINE_Pos)
#define JCC_TOKEN_FLAGS_HAS_SPACE_Pos        1
#define JCC_TOKEN_FLAGS_HAS_SPACE_Msk        (1u << JCC_TOKEN_FLAGS_HAS_SPACE_Pos)

typedef struct jx_cc_token_t 
{
	jx_cc_token_kind m_Kind;
	uint32_t m_Flags;         // JCC_TOKEN_FLAGS_xxx
	jx_cc_source_loc_t m_Loc;

	const char* m_String;     // Interned string
	uint32_t m_Length;        // Interned string length
	JX_PAD(4);

	jx_cc_type_t* m_Type;     // Used if JCC_TOKEN_NUMBER or JCC_TOKEN_STRING_LITERAL
	const char* m_Val_string; // (Interned) String literal contents including terminating '\0'
	double m_Val_float;       // If kind is JCC_TOKEN_NUMBER, its value
	int64_t m_Val_int;        // If kind is JCC_TOKEN_NUMBER, its value

	jx_cc_token_t* m_Next;    // Next token

	jx_cc_hideset_t* m_HideSet; // For macro expansion
	jx_cc_token_t* m_Origin;    // If this is expanded from a macro, the original token.
} jx_cc_token_t;

#define JCC_OBJECT_FLAGS_IS_LOCAL_Pos      0
#define JCC_OBJECT_FLAGS_IS_LOCAL_Msk      (1u << JCC_OBJECT_FLAGS_IS_LOCAL_Pos)
#define JCC_OBJECT_FLAGS_IS_FUNCTION_Pos   1
#define JCC_OBJECT_FLAGS_IS_FUNCTION_Msk   (1u << JCC_OBJECT_FLAGS_IS_FUNCTION_Pos)
#define JCC_OBJECT_FLAGS_IS_DEFINITION_Pos 2
#define JCC_OBJECT_FLAGS_IS_DEFINITION_Msk (1u << JCC_OBJECT_FLAGS_IS_DEFINITION_Pos)
#define JCC_OBJECT_FLAGS_IS_STATIC_Pos     3
#define JCC_OBJECT_FLAGS_IS_STATIC_Msk     (1u << JCC_OBJECT_FLAGS_IS_STATIC_Pos)
#define JCC_OBJECT_FLAGS_IS_TENTATIVE_Pos  4
#define JCC_OBJECT_FLAGS_IS_TENTATIVE_Msk  (1u << JCC_OBJECT_FLAGS_IS_TENTATIVE_Pos)
#define JCC_OBJECT_FLAGS_IS_TLS_Pos        5
#define JCC_OBJECT_FLAGS_IS_TLS_Msk        (1u << JCC_OBJECT_FLAGS_IS_TLS_Pos)
#define JCC_OBJECT_FLAGS_IS_INLINE_Pos     6
#define JCC_OBJECT_FLAGS_IS_INLINE_Msk     (1u << JCC_OBJECT_FLAGS_IS_INLINE_Pos)
#define JCC_OBJECT_FLAGS_IS_LIVE_Pos       7
#define JCC_OBJECT_FLAGS_IS_LIVE_Msk       (1u << JCC_OBJECT_FLAGS_IS_LIVE_Pos)
#define JCC_OBJECT_FLAGS_IS_ROOT_Pos       8
#define JCC_OBJECT_FLAGS_IS_ROOT_Msk       (1u << JCC_OBJECT_FLAGS_IS_ROOT_Pos)

typedef struct jx_cc_object_t 
{
	jx_cc_object_t* m_Next;

	const char* m_Name;
	jx_cc_type_t* m_Type;
	const char* m_GlobalInitData;
	jx_cc_relocation_t* m_GlobalRelocations;
	jx_cc_object_t* m_FuncParams;
	jx_cc_ast_stmt_t* m_FuncBody;
	jx_cc_object_t* m_FuncLocals;
	jx_cc_object_t* m_FuncVarArgArea;
	const char** m_FuncRefsArr;
	uint32_t m_Alignment;
	uint32_t m_Flags; // JCC_OBJECT_FLAGS_xxx
} jx_cc_object_t;

// Global variable can be initialized either by a constant expression
// or a pointer to another global variable. This struct represents the
// latter.
typedef struct jx_cc_relocation_t 
{
	jx_cc_relocation_t* m_Next;
	char** m_Label;
	int64_t m_Addend;
	int32_t m_Offset;
} jx_cc_relocation_t;

typedef enum jx_cc_ast_node_kind
{
	JCC_NODE_EXPR_NULL = 0,    // Do nothing
	JCC_NODE_EXPR_ADD,         // +
	JCC_NODE_EXPR_SUB,         // -
	JCC_NODE_EXPR_MUL,         // *
	JCC_NODE_EXPR_DIV,         // /
	JCC_NODE_EXPR_NEG,         // unary -
	JCC_NODE_EXPR_MOD,         // %
	JCC_NODE_EXPR_BITWISE_AND, // &
	JCC_NODE_EXPR_BITWISE_OR,  // |
	JCC_NODE_EXPR_BITWISE_XOR, // ^
	JCC_NODE_EXPR_LSHIFT,      // <<
	JCC_NODE_EXPR_RSHIFT,      // >>
	JCC_NODE_EXPR_EQUAL,       // ==
	JCC_NODE_EXPR_NOT_EQUAL,   // !=
	JCC_NODE_EXPR_LESS_THAN,   // <
	JCC_NODE_EXPR_LESS_EQUAL,  // <=
	JCC_NODE_EXPR_ASSIGN,      // =
	JCC_NODE_EXPR_CONDITIONAL, // ?:
	JCC_NODE_EXPR_COMMA,       // ,
	JCC_NODE_EXPR_MEMBER,      // . (struct member access)
	JCC_NODE_EXPR_ADDR,        // unary &
	JCC_NODE_EXPR_DEREF,       // unary *
	JCC_NODE_EXPR_NOT,         // !
	JCC_NODE_EXPR_BITWISE_NOT, // ~
	JCC_NODE_EXPR_LOGICAL_AND, // &&
	JCC_NODE_EXPR_LOGICAL_OR,  // ||
	JCC_NODE_EXPR_CAST,        // Type cast
	JCC_NODE_EXPR_MEMZERO,     // Zero-clear a stack variable
	JCC_NODE_EXPR_FUNC_CALL,   // Function call
	JCC_NODE_EXPR_COMPOUND_ASSIGN, // Compound assignments +=, -=, *=, /=, %=, &=, |=, <<=, >>=, ^=
	JCC_NODE_EXPR_GET_ELEMENT_PTR,

	JCC_NODE_STMT_RETURN,      // "return"
	JCC_NODE_STMT_IF,          // "if"
	JCC_NODE_STMT_FOR,         // "for" or "while"
	JCC_NODE_STMT_DO,          // "do"
	JCC_NODE_STMT_SWITCH,      // "switch"
	JCC_NODE_STMT_CASE,        // "case"
	JCC_NODE_STMT_BLOCK,       // { ... }
	JCC_NODE_STMT_GOTO,        // "goto"
	JCC_NODE_STMT_LABEL,       // Labeled statement
	JCC_NODE_STMT_EXPR,        // Statement expression
	JCC_NODE_STMT_ASM,         // "asm"
//	JCC_NODE_STMT_CAS,         // Atomic compare-and-swap
//	JCC_NODE_STMT_EXCH,        // Atomic exchange

	JCC_NODE_VARIABLE,         // Variable
	JCC_NODE_NUMBER,           // Integer
} jx_cc_ast_node_kind;

typedef struct jx_cc_ast_node_t
{
	jx_cc_ast_node_kind m_Kind;
	JX_PAD(4);
	jx_cc_token_t* m_Token;
} jx_cc_ast_node_t;

typedef struct jx_cc_ast_expr_t
{
	JX_INHERITS(jx_cc_ast_node_t);
	jx_cc_type_t* m_Type;
} jx_cc_ast_expr_t;

typedef struct jx_cc_ast_stmt_t
{
	JX_INHERITS(jx_cc_ast_node_t);
} jx_cc_ast_stmt_t;

typedef struct jx_cc_ast_expr_binary_t
{
	JX_INHERITS(jx_cc_ast_expr_t);
	jx_cc_ast_expr_t* m_ExprLHS;
	jx_cc_ast_expr_t* m_ExprRHS;
} jx_cc_ast_expr_binary_t;

typedef struct jx_cc_ast_expr_compound_assign_t
{
	JX_INHERITS(jx_cc_ast_expr_t);
	jx_cc_ast_expr_t* m_ExprLHS; 
	jx_cc_ast_expr_t* m_ExprRHS;
	jx_cc_ast_node_kind m_Op;
} jx_cc_ast_expr_compound_assign_t;

typedef struct jx_cc_ast_expr_get_element_ptr_t
{
	JX_INHERITS(jx_cc_ast_expr_t);
	jx_cc_ast_expr_t* m_ExprPtr;
	jx_cc_ast_expr_t* m_ExprIndex;
} jx_cc_ast_expr_get_element_ptr_t;

typedef struct jx_cc_ast_expr_unary_t
{
	JX_INHERITS(jx_cc_ast_expr_t);
	jx_cc_ast_expr_t* m_Expr;
} jx_cc_ast_expr_unary_t;

typedef struct jx_cc_ast_expr_iconst_t
{
	JX_INHERITS(jx_cc_ast_expr_t);
	int64_t m_Value;
} jx_cc_ast_expr_iconst_t;

typedef struct jx_cc_ast_expr_fconst_t
{
	JX_INHERITS(jx_cc_ast_expr_t);
	double m_Value;
} jx_cc_ast_expr_fconst_t;

typedef struct jx_cc_ast_expr_variable_t
{
	JX_INHERITS(jx_cc_ast_expr_t);
	jx_cc_object_t* m_Var;
} jx_cc_ast_expr_variable_t;

typedef struct jx_cc_ast_expr_funccall_t
{
	JX_INHERITS(jx_cc_ast_expr_t);
	jx_cc_ast_expr_t* m_FuncExpr;
	jx_cc_type_t* m_FuncType;
	jx_cc_ast_expr_t** m_Args;
	uint32_t m_NumArgs;
	JX_PAD(4);
} jx_cc_ast_expr_funccall_t;

typedef struct jx_cc_ast_expr_member_t
{
	JX_INHERITS(jx_cc_ast_expr_t);
	jx_cc_ast_expr_t* m_Expr;
	jx_cc_struct_member_t* m_Member;
} jx_cc_ast_expr_member_t;

typedef struct jx_cc_ast_expr_cond_t
{
	JX_INHERITS(jx_cc_ast_expr_t);
	jx_cc_ast_expr_t* m_CondExpr;
	jx_cc_ast_expr_t* m_ThenExpr;
	jx_cc_ast_expr_t* m_ElseExpr;
} jx_cc_ast_expr_cond_t;

typedef struct jx_cc_ast_stmt_block_t
{
	JX_INHERITS(jx_cc_ast_stmt_t);
	jx_cc_ast_stmt_t** m_Children;
	uint32_t m_NumChildren;
	JX_PAD(4);
} jx_cc_ast_stmt_block_t;

typedef struct jx_cc_ast_stmt_asm_t
{
	JX_INHERITS(jx_cc_ast_stmt_t);
	const char* m_AsmCodeStr;
	uint32_t m_Specifiers; // NOTE(JD): volatile/inline are ignored; member always 0
	JX_PAD(4);
} jx_cc_ast_stmt_asm_t;

typedef struct jx_cc_ast_stmt_expr_t
{
	JX_INHERITS(jx_cc_ast_stmt_t);
	jx_cc_ast_expr_t* m_Expr;
} jx_cc_ast_stmt_expr_t;

typedef struct jx_cc_ast_stmt_if_t
{
	JX_INHERITS(jx_cc_ast_stmt_t);
	jx_cc_ast_expr_t* m_CondExpr;
	jx_cc_ast_stmt_t* m_ThenStmt;
	jx_cc_ast_stmt_t* m_ElseStmt;
	jx_cc_label_t m_ThenLbl;
	jx_cc_label_t m_ElseLbl;
	jx_cc_label_t m_EndLbl;
	JX_PAD(4);
} jx_cc_ast_stmt_if_t;

typedef struct jx_cc_ast_stmt_switch_t
{
	JX_INHERITS(jx_cc_ast_stmt_t);
	jx_cc_ast_expr_t* m_CondExpr;
	jx_cc_ast_stmt_t* m_BodyStmt;
	jx_cc_ast_stmt_case_t* m_CaseListHead;
	jx_cc_ast_stmt_case_t* m_DefaultCase;
	jx_cc_label_t m_BreakLbl;
	JX_PAD(4);
} jx_cc_ast_stmt_switch_t;

typedef struct jx_cc_ast_stmt_case_t
{
	JX_INHERITS(jx_cc_ast_stmt_t);
	jx_cc_ast_stmt_t* m_BodyStmt;
	jx_cc_ast_stmt_case_t* m_NextCase;
	int64_t m_Range[2];
	jx_cc_label_t m_Lbl;
	JX_PAD(4);
} jx_cc_ast_stmt_case_t;

typedef struct jx_cc_ast_stmt_for_t
{
	JX_INHERITS(jx_cc_ast_stmt_t);
	jx_cc_ast_stmt_t* m_InitStmt; // declaration or expression statement
	jx_cc_ast_expr_t* m_CondExpr;
	jx_cc_ast_expr_t* m_IncExpr;
	jx_cc_ast_stmt_t* m_BodyStmt;
	jx_cc_label_t m_BreakLbl;
	jx_cc_label_t m_ContinueLbl;
	jx_cc_label_t m_BodyLbl;
} jx_cc_ast_stmt_for_t;

typedef struct jx_cc_ast_stmt_do_t
{
	JX_INHERITS(jx_cc_ast_stmt_t);
	jx_cc_ast_stmt_t* m_BodyStmt;
	jx_cc_ast_expr_t* m_CondExpr;
	jx_cc_label_t m_BreakLbl;
	jx_cc_label_t m_ContinueLbl;
	jx_cc_label_t m_BodyLbl;
} jx_cc_ast_stmt_do_t;

typedef struct jx_cc_ast_stmt_goto_t
{
	JX_INHERITS(jx_cc_ast_stmt_t);
	jx_cc_ast_stmt_goto_t* m_NextGoto;
	const char* m_Label; // Interned
	jx_cc_label_t m_UniqueLabel;
	JX_PAD(4);
} jx_cc_ast_stmt_goto_t;

typedef struct jx_cc_ast_stmt_label_t
{
	JX_INHERITS(jx_cc_ast_stmt_t);
	jx_cc_ast_stmt_t* m_Stmt;
	jx_cc_ast_stmt_label_t* m_NextLabel;
	const char* m_Label; // Interned
	jx_cc_label_t m_UniqueLabel;
	JX_PAD(4);
} jx_cc_ast_stmt_label_t;

typedef enum jx_cc_type_kind
{
	JCC_TYPE_VOID,
	JCC_TYPE_BOOL,
	JCC_TYPE_CHAR,
	JCC_TYPE_SHORT,
	JCC_TYPE_INT,
	JCC_TYPE_LONG,
	JCC_TYPE_FLOAT,
	JCC_TYPE_DOUBLE,
	JCC_TYPE_ENUM,
	JCC_TYPE_PTR,
	JCC_TYPE_FUNC,
	JCC_TYPE_ARRAY,
	JCC_TYPE_STRUCT,
	JCC_TYPE_UNION,
} jx_cc_type_kind;

#define JCC_TYPE_FLAGS_IS_UNSIGNED_Pos 0
#define JCC_TYPE_FLAGS_IS_UNSIGNED_Msk (1u << JCC_TYPE_FLAGS_IS_UNSIGNED_Pos)
#define JCC_TYPE_FLAGS_IS_ATOMIC_Pos   1
#define JCC_TYPE_FLAGS_IS_ATOMIC_Msk   (1u << JCC_TYPE_FLAGS_IS_ATOMIC_Pos)
#define JCC_TYPE_FLAGS_IS_FLEXIBLE_Pos 2
#define JCC_TYPE_FLAGS_IS_FLEXIBLE_Msk (1u << JCC_TYPE_FLAGS_IS_FLEXIBLE_Pos)
#define JCC_TYPE_FLAGS_IS_PACKED_Pos   3
#define JCC_TYPE_FLAGS_IS_PACKED_Msk   (1u << JCC_TYPE_FLAGS_IS_PACKED_Pos)
#define JCC_TYPE_FLAGS_IS_VARIADIC_Pos 4
#define JCC_TYPE_FLAGS_IS_VARIADIC_Msk (1u << JCC_TYPE_FLAGS_IS_VARIADIC_Pos)

typedef struct jx_cc_type_t 
{
	jx_cc_type_t* m_Next; // NOTE(JD): Only used for function parameters.

	const jx_cc_type_t* m_OriginType;  // NOTE(JD): This is only used by jcc_typeIsCompatible().

	// Pointer-to or array-of type. We intentionally use the same member
	// to represent pointer/array duality in C.
	//
	// In many contexts in which a pointer is expected, we examine this
	// member instead of "kind" member to determine whether a type is a
	// pointer or not. That means in many contexts "array of T" is
	// naturally handled as if it were "pointer to T", as required by
	// the C spec.
	jx_cc_type_t* m_BaseType;

	jx_cc_type_t* m_FuncRetType;
	jx_cc_type_t* m_FuncParams;

	jx_cc_token_t* m_DeclName;

	jx_cc_struct_member_t* m_StructMembers;

	jx_cc_type_kind m_Kind;
	int32_t m_Size; // NOTE(JD): Might be negative in case of incomplete types
	uint32_t m_Alignment;
	int32_t m_ArrayLen;
	uint32_t m_Flags; // JCC_TYPE_FLAGS_xxx

	JX_PAD(4);
} jx_cc_type_t;

typedef struct jx_cc_struct_member_t 
{
	jx_cc_struct_member_t* m_Next;
	jx_cc_type_t* m_Type;
	jx_cc_token_t* m_Name;
	uint32_t m_ID;
	uint32_t m_Alignment;
	uint32_t m_Offset;
	uint32_t m_BitOffset;
	uint32_t m_BitWidth;
	bool m_IsBitfield;
	JX_PAD(3);
} jx_cc_struct_member_t;

typedef struct jx_cc_translation_unit_t
{
	jx_cc_object_t* m_Globals;
	uint32_t m_NumErrors;
	uint32_t m_NumWarnings;
} jx_cc_translation_unit_t;

typedef struct jx_cc_context_t jx_cc_context_t;

jx_cc_context_t* jx_cc_createContext(jx_allocator_i* allocator, jx_logger_i* logger);
void jx_cc_destroyContext(jx_cc_context_t* ctx);
jx_cc_translation_unit_t* jx_cc_compileFile(jx_cc_context_t* ctx, jx_file_base_dir baseDir, const char* filename);

static inline bool jx_cc_typeIsFloat(const jx_cc_type_t* ty)
{
	const jx_cc_type_kind k = ty->m_Kind;
	return false
		|| k == JCC_TYPE_FLOAT
		|| k == JCC_TYPE_DOUBLE
		;
}

static inline bool jx_cc_typeIsInteger(const jx_cc_type_t* ty)
{
	const jx_cc_type_kind k = ty->m_Kind;
	return false
		|| k == JCC_TYPE_BOOL
		|| k == JCC_TYPE_CHAR
		|| k == JCC_TYPE_SHORT
		|| k == JCC_TYPE_INT
		|| k == JCC_TYPE_LONG
		|| k == JCC_TYPE_ENUM
		;
}

static inline bool jx_cc_typeIsNumeric(const jx_cc_type_t* ty)
{
	return false
		|| jx_cc_typeIsInteger(ty)
		|| jx_cc_typeIsFloat(ty)
		;
}

#endif // JX_CC_H
