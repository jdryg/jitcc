// TODO
// - Machine IR
//  - x64 codegen
// - Basic IR passes
//  - Basic constant folding
//  - GetElementPtr index merging
// 
#include "jcc.h"
#include "jit.h"
#include "jit_gen.h"
#include "jir.h"
#include "jir_gen.h"
#include "jmir.h"
#include "jmir_gen.h"
#include <jlib/allocator.h>
#include <jlib/array.h>
#include <jlib/config.h>
#include <jlib/dbg.h>
#include <jlib/error.h>
#include <jlib/logger.h>
#include <jlib/hashmap.h>
#include <jlib/kernel.h>
#include <jlib/math.h>
#include <jlib/memory.h>
#include <jlib/os.h>
#include <jlib/string.h>
#include <stdio.h>

typedef enum jcc_basic_type
{
	JCC_BASIC_TYPE_VOID = 0,
	JCC_BASIC_TYPE_BOOL,
	JCC_BASIC_TYPE_INT8,
	JCC_BASIC_TYPE_UINT8,
	JCC_BASIC_TYPE_INT16,
	JCC_BASIC_TYPE_UINT16,
	JCC_BASIC_TYPE_INT32,
	JCC_BASIC_TYPE_UINT32,
	JCC_BASIC_TYPE_INT64,
	JCC_BASIC_TYPE_UINT64,
	JCC_BASIC_TYPE_FLOAT,
	JCC_BASIC_TYPE_DOUBLE,

	JCC_BASIC_TYPE_COUNT,

	JCC_BASIC_TYPE_UNKNOWN = 0x7FFFFFFF
} jcc_basic_type;

static const char* kBasicTypeName[] = {
	[JCC_BASIC_TYPE_VOID] = "void",
	[JCC_BASIC_TYPE_BOOL] = "bool",
	[JCC_BASIC_TYPE_INT8] = "i8",
	[JCC_BASIC_TYPE_UINT8] = "ui8",
	[JCC_BASIC_TYPE_INT16] = "i16",
	[JCC_BASIC_TYPE_UINT16] = "ui16",
	[JCC_BASIC_TYPE_INT32] = "i32",
	[JCC_BASIC_TYPE_UINT32] = "ui32",
	[JCC_BASIC_TYPE_INT64] = "i64",
	[JCC_BASIC_TYPE_UINT64] = "ui64",
	[JCC_BASIC_TYPE_FLOAT] = "float",
	[JCC_BASIC_TYPE_DOUBLE] = "double",
};

static const char* kTypeKindName[] = {
	[JCC_TYPE_VOID] = "void",
	[JCC_TYPE_BOOL] = "bool",
	[JCC_TYPE_CHAR] = "char",
	[JCC_TYPE_SHORT] = "short",
	[JCC_TYPE_INT] = "int",
	[JCC_TYPE_LONG] = "long",
	[JCC_TYPE_FLOAT] = "float",
	[JCC_TYPE_DOUBLE] = "double",
	[JCC_TYPE_ENUM] = "enum",
	[JCC_TYPE_PTR] = "ptr",
	[JCC_TYPE_FUNC] = "function",
	[JCC_TYPE_ARRAY] = "array",
	[JCC_TYPE_STRUCT] = "struct",
	[JCC_TYPE_UNION] = "union",
};

static const char* kNodeKindName[] = {
	[JCC_NODE_EXPR_NULL] = "null_expr",
	[JCC_NODE_EXPR_ADD] = "add",
	[JCC_NODE_EXPR_SUB] = "sub",
	[JCC_NODE_EXPR_MUL] = "mul",
	[JCC_NODE_EXPR_DIV] = "div",
	[JCC_NODE_EXPR_NEG] = "neg",
	[JCC_NODE_EXPR_MOD] = "mod",
	[JCC_NODE_EXPR_BITWISE_AND] = "bit_and",
	[JCC_NODE_EXPR_BITWISE_OR] = "bit_or",
	[JCC_NODE_EXPR_BITWISE_XOR] = "bit_xor",
	[JCC_NODE_EXPR_LSHIFT] = "shl",
	[JCC_NODE_EXPR_RSHIFT] = "shr",
	[JCC_NODE_EXPR_EQUAL] = "log_eq",
	[JCC_NODE_EXPR_NOT_EQUAL] = "log_ne",
	[JCC_NODE_EXPR_LESS_THAN] = "log_lt",
	[JCC_NODE_EXPR_LESS_EQUAL] = "log_le",
	[JCC_NODE_EXPR_ASSIGN] = "assign",
	[JCC_NODE_EXPR_CONDITIONAL] = "cond",
	[JCC_NODE_EXPR_COMMA] = "comma",
	[JCC_NODE_EXPR_MEMBER] = "member",
	[JCC_NODE_EXPR_ADDR] = "addr",
	[JCC_NODE_EXPR_DEREF] = "deref",
	[JCC_NODE_EXPR_NOT] = "log_not",
	[JCC_NODE_EXPR_BITWISE_NOT] = "bit_not",
	[JCC_NODE_EXPR_LOGICAL_AND] = "log_and",
	[JCC_NODE_EXPR_LOGICAL_OR] = "log_or",
	[JCC_NODE_STMT_RETURN] = "return",
	[JCC_NODE_STMT_IF] = "if",
	[JCC_NODE_STMT_FOR] = "for",
	[JCC_NODE_STMT_DO] = "do",
	[JCC_NODE_STMT_SWITCH] = "switch",
	[JCC_NODE_STMT_CASE] = "case",
	[JCC_NODE_STMT_BLOCK] = "block",
	[JCC_NODE_STMT_GOTO] = "goto",
	[JCC_NODE_STMT_LABEL] = "label",
	[JCC_NODE_EXPR_FUNC_CALL] = "func_call",
	[JCC_NODE_EXPR_COMPOUND_ASSIGN] = "compound_assignment",
	[JCC_NODE_EXPR_GET_ELEMENT_PTR] = "get_element_ptr",
	[JCC_NODE_STMT_EXPR] = "stmt_expr",
	[JCC_NODE_VARIABLE] = "var",
	[JCC_NODE_NUMBER] = "num",
	[JCC_NODE_EXPR_CAST] = "cast",
	[JCC_NODE_EXPR_MEMZERO] = "memzero",
	[JCC_NODE_STMT_ASM] = "asm",
};

static const char* kIndexToStr[] = {
	"[0]", "[1]", "[2]", "[3]", "[4]", "[5]", "[6]", "[7]", "[8]", "[9]",
	"[10]", "[11]", "[12]", "[13]", "[14]", "[15]", "[16]", "[17]", "[18]", "[19]",
};

static void astDumpStructMember(jx_config_t* ast, const jx_cc_struct_member_t* member);
static void astDumpType(jx_config_t* ast, const char* name, const jx_cc_type_t* type);
static void astDumpExpression(jx_config_t* ast, const char* name, const jx_cc_ast_expr_t* node);
static void astDumpStatement(jx_config_t* ast, const char* name, const jx_cc_ast_stmt_t* node);
static void astDumpObject(jx_config_t* ast, const char* name, const jx_cc_object_t* obj);

static jx_cc_ast_expr_t* astCleanupExpression(jx_cc_ast_expr_t* node);
static jx_cc_ast_stmt_t* astCleanupStatement(jx_cc_ast_stmt_t* node);
static void astCleanupObject(jx_cc_object_t* obj);

static bool astIsTypeEqual(jx_cc_type_t* t1, jx_cc_type_t* t2)
{
	if ((t1->m_BaseType && !t2->m_BaseType) || (!t1->m_BaseType && t2->m_BaseType)) {
		return false;
	} else if (t1->m_BaseType && t2->m_BaseType) {
		if (!astIsTypeEqual(t1->m_BaseType, t2->m_BaseType)) {
			return false;
		}
	}

	return true
		&& t1->m_Kind == t2->m_Kind
		&& t1->m_Size == t2->m_Size
		&& t1->m_Alignment == t2->m_Alignment
		&& (t1->m_Flags & JCC_TYPE_FLAGS_IS_UNSIGNED_Msk) == (t2->m_Flags & JCC_TYPE_FLAGS_IS_UNSIGNED_Msk)
		&& (t1->m_Flags & JCC_TYPE_FLAGS_IS_ATOMIC_Msk) == (t2->m_Flags & JCC_TYPE_FLAGS_IS_ATOMIC_Msk)
		;
}

static jcc_basic_type astGetBasicType(const jx_cc_type_t* type)
{
	switch (type->m_Kind) {
	case JCC_TYPE_VOID:
		return JCC_BASIC_TYPE_VOID;
	case JCC_TYPE_BOOL:
		return JCC_BASIC_TYPE_BOOL;
	case JCC_TYPE_CHAR:
		return (type->m_Flags & JCC_TYPE_FLAGS_IS_UNSIGNED_Msk) != 0 ? JCC_BASIC_TYPE_UINT8 : JCC_BASIC_TYPE_INT8;
	case JCC_TYPE_SHORT:
		return (type->m_Flags & JCC_TYPE_FLAGS_IS_UNSIGNED_Msk) != 0 ? JCC_BASIC_TYPE_UINT16 : JCC_BASIC_TYPE_INT16;
	case JCC_TYPE_INT:
		return (type->m_Flags & JCC_TYPE_FLAGS_IS_UNSIGNED_Msk) != 0 ? JCC_BASIC_TYPE_UINT32 : JCC_BASIC_TYPE_INT32;
	case JCC_TYPE_LONG:
		return (type->m_Flags & JCC_TYPE_FLAGS_IS_UNSIGNED_Msk) != 0 ? JCC_BASIC_TYPE_UINT64 : JCC_BASIC_TYPE_INT64;
	case JCC_TYPE_FLOAT:
		return JCC_BASIC_TYPE_FLOAT;
	case JCC_TYPE_DOUBLE:
		return JCC_BASIC_TYPE_DOUBLE;
	default:
		break;
	}
	return JCC_BASIC_TYPE_UNKNOWN;
}

static void astDumpStructMember(jx_config_t* ast, const jx_cc_struct_member_t* member)
{
	jx_config_beginObject(ast, member->m_Name->m_String);
	{
		jx_config_setUint32(ast, "idx", member->m_ID);
		jx_config_setUint32(ast, "alignment", member->m_Alignment);
		jx_config_setUint32(ast, "offset", member->m_Offset);
		if (member->m_IsBitfield) {
			jx_config_setUint32(ast, "bit_offset", member->m_BitOffset);
			jx_config_setUint32(ast, "bit_width", member->m_BitWidth);
		}

		astDumpType(ast, "type", member->m_Type);
	}
	jx_config_endObject(ast);
}

static void astDumpType(jx_config_t* ast, const char* name, const jx_cc_type_t* type)
{
	jcc_basic_type basicType = astGetBasicType(type);
	if (basicType < JCC_BASIC_TYPE_COUNT) {
		jx_config_setString(ast, name, kBasicTypeName[basicType], UINT32_MAX);
	} else {
		jx_config_beginObject(ast, name);

#if 0
		if (type->name) {
			jx_config_setString(ast, "name", type->name->loc, type->name->len);
		}
#endif

#if 0
		if (type->origin) {
			astDumpType(ast, "origin", type->origin);
		}
#endif

		if (type->m_BaseType) {
			astDumpType(ast, "base", type->m_BaseType);
		}

		jx_config_setString(ast, "kind", kTypeKindName[type->m_Kind], UINT32_MAX);

		if (type->m_Kind == JCC_TYPE_ARRAY) {
			jx_config_setInt32(ast, "size", type->m_Size);
			jx_config_setInt32(ast, "alignment", type->m_Alignment);
			jx_config_setInt32(ast, "length", type->m_ArrayLen);
		} else if (type->m_Kind == JCC_TYPE_STRUCT) {
			jx_config_setInt32(ast, "size", type->m_Size);
			jx_config_setInt32(ast, "alignment", type->m_Alignment);
			jx_config_setBoolean(ast, "is_atomic", (type->m_Flags & JCC_TYPE_FLAGS_IS_ATOMIC_Msk) != 0);
			jx_config_setBoolean(ast, "is_flexible", (type->m_Flags & JCC_TYPE_FLAGS_IS_FLEXIBLE_Msk) != 0);
			jx_config_setBoolean(ast, "is_packed", (type->m_Flags & JCC_TYPE_FLAGS_IS_PACKED_Msk) != 0);

			jx_config_beginObject(ast, "members");
			{
				jx_cc_struct_member_t* mem = type->m_StructMembers;
				while (mem) {
					astDumpStructMember(ast, mem);
					mem = mem->m_Next;
				}
			}
			jx_config_endObject(ast);
		} else if (type->m_Kind == JCC_TYPE_FUNC) {
			jx_config_setBoolean(ast, "is_variadic", (type->m_Flags & JCC_TYPE_FLAGS_IS_VARIADIC_Msk) != 0);

			astDumpType(ast, "return_type", type->m_FuncRetType);

			jx_config_beginArray(ast, "params");
			{
				jx_cc_type_t* param = type->m_FuncParams;
				uint32_t paramID = 0;
				while (param) {
					astDumpType(ast, kIndexToStr[paramID], param);

					param = param->m_Next;
					++paramID;
				}
			}
			jx_config_endArray(ast);
		} else {
			jx_config_setInt32(ast, "size", type->m_Size);
			jx_config_setInt32(ast, "alignment", type->m_Alignment);
			jx_config_setBoolean(ast, "is_unsigned", (type->m_Flags & JCC_TYPE_FLAGS_IS_UNSIGNED_Msk) != 0);
			jx_config_setBoolean(ast, "is_atomic", (type->m_Flags & JCC_TYPE_FLAGS_IS_ATOMIC_Msk) != 0);
		}

		jx_config_endObject(ast);
	}
}

static void astDumpExpression(jx_config_t* ast, const char* name, const jx_cc_ast_expr_t* node)
{
	jx_config_beginObject(ast, name);

	jx_config_setString(ast, "node_type", kNodeKindName[node->super.m_Kind], UINT32_MAX);
	if (node->m_Type) {
		astDumpType(ast, "type", node->m_Type);
	}

	switch (node->super.m_Kind) {
	case JCC_NODE_EXPR_NULL:
		break;
	case JCC_NODE_VARIABLE: {
#if 0
		astDumpObject(ast, "var", node->var);
#else
		jx_config_setUint32(ast, "var", (uint32_t)(uintptr_t)((jx_cc_ast_expr_variable_t*)node)->m_Var);
#endif
	} break;
	case JCC_NODE_EXPR_ADDR: {
		astDumpExpression(ast, "expr", ((jx_cc_ast_expr_unary_t*)node)->m_Expr);
	} break;
	case JCC_NODE_NUMBER: {
		jx_config_setInt32(ast, "val", (int32_t)((jx_cc_ast_expr_iconst_t*)node)->m_Value);
	} break;
	case JCC_NODE_EXPR_COMMA: {
		astDumpExpression(ast, "lhs", ((jx_cc_ast_expr_binary_t*)node)->m_ExprLHS);
		astDumpExpression(ast, "rhs", ((jx_cc_ast_expr_binary_t*)node)->m_ExprRHS);
	} break;
	case JCC_NODE_EXPR_ASSIGN: {
		astDumpExpression(ast, "lhs", ((jx_cc_ast_expr_binary_t*)node)->m_ExprLHS);
		astDumpExpression(ast, "rhs", ((jx_cc_ast_expr_binary_t*)node)->m_ExprRHS);
	} break;
	case JCC_NODE_EXPR_DEREF: 
	case JCC_NODE_EXPR_NEG: 
	case JCC_NODE_EXPR_NOT: 
	case JCC_NODE_EXPR_BITWISE_NOT: {
		astDumpExpression(ast, "expr", ((jx_cc_ast_expr_unary_t*)node)->m_Expr);
	} break;
	case JCC_NODE_EXPR_ADD: 
	case JCC_NODE_EXPR_SUB:
	case JCC_NODE_EXPR_MUL: 
	case JCC_NODE_EXPR_DIV: 
	case JCC_NODE_EXPR_EQUAL:
	case JCC_NODE_EXPR_NOT_EQUAL:
	case JCC_NODE_EXPR_LESS_THAN:
	case JCC_NODE_EXPR_LESS_EQUAL: 
	case JCC_NODE_EXPR_BITWISE_AND:
	case JCC_NODE_EXPR_BITWISE_OR:
	case JCC_NODE_EXPR_BITWISE_XOR:
	case JCC_NODE_EXPR_LOGICAL_AND: 
	case JCC_NODE_EXPR_LOGICAL_OR: 
	case JCC_NODE_EXPR_LSHIFT:
	case JCC_NODE_EXPR_RSHIFT: {
		astDumpExpression(ast, "lhs", ((jx_cc_ast_expr_binary_t*)node)->m_ExprLHS);
		astDumpExpression(ast, "rhs", ((jx_cc_ast_expr_binary_t*)node)->m_ExprRHS);
	} break;
	case JCC_NODE_EXPR_CAST: {
		astDumpExpression(ast, "expr", ((jx_cc_ast_expr_unary_t*)node)->m_Expr);
	} break;
	case JCC_NODE_EXPR_MEMZERO: {
#if 0
		astDumpObject(ast, "var", node->var);
#else
		jx_config_setUint32(ast, "var", (uint32_t)(uintptr_t)((jx_cc_ast_expr_variable_t*)node)->m_Var);
#endif
	} break;
	case JCC_NODE_EXPR_MEMBER: {
		jx_config_setInt32(ast, "offset", ((jx_cc_ast_expr_member_t*)node)->m_Member->m_Offset);
		astDumpExpression(ast, "member", ((jx_cc_ast_expr_member_t*)node)->m_Expr);
	} break;
	case JCC_NODE_EXPR_CONDITIONAL: {
		astDumpExpression(ast, "cond", ((jx_cc_ast_expr_cond_t*)node)->m_CondExpr);
		astDumpExpression(ast, "then", ((jx_cc_ast_expr_cond_t*)node)->m_ThenExpr);
		astDumpExpression(ast, "else", ((jx_cc_ast_expr_cond_t*)node)->m_ElseExpr);
	} break;
	case JCC_NODE_EXPR_FUNC_CALL: {
		// TODO
	} break;
	case JCC_NODE_EXPR_COMPOUND_ASSIGN: {
		jx_config_setString(ast, "op", kNodeKindName[((jx_cc_ast_expr_compound_assign_t*)node)->m_Op], UINT32_MAX);
		astDumpExpression(ast, "lhs", ((jx_cc_ast_expr_compound_assign_t*)node)->m_ExprLHS);
		astDumpExpression(ast, "rhs", ((jx_cc_ast_expr_compound_assign_t*)node)->m_ExprRHS);
	} break;
	case JCC_NODE_EXPR_GET_ELEMENT_PTR: {
		astDumpExpression(ast, "ptr", ((jx_cc_ast_expr_binary_t*)node)->m_ExprLHS);
		astDumpExpression(ast, "idx", ((jx_cc_ast_expr_binary_t*)node)->m_ExprRHS);
	} break;
	default:
		JX_NOT_IMPLEMENTED();
		break;
	}

	jx_config_endObject(ast);
}

static void astDumpStatement(jx_config_t* ast, const char* name, const jx_cc_ast_stmt_t* node)
{
	jx_config_beginObject(ast, name);

	jx_config_setString(ast, "node_type", kNodeKindName[node->super.m_Kind], UINT32_MAX);

	switch (node->super.m_Kind) {
	case JCC_NODE_STMT_BLOCK: {
		jx_cc_ast_stmt_block_t* blockNode = (jx_cc_ast_stmt_block_t*)node;
		jx_config_beginArray(ast, "stmt_list");
		{
			const uint32_t numChildren = blockNode->m_NumChildren;
			for (uint32_t iChild = 0;iChild < numChildren;++iChild) {
				astDumpStatement(ast, kIndexToStr[iChild], (jx_cc_ast_stmt_t*)blockNode->m_Children[iChild]);
			}
		}
		jx_config_endArray(ast);
	} break;
	case JCC_NODE_STMT_EXPR: {
		jx_cc_ast_stmt_expr_t* exprNode = (jx_cc_ast_stmt_expr_t*)node;
		astDumpExpression(ast, "expr", exprNode->m_Expr);
	} break;
	case JCC_NODE_STMT_IF: {
		jx_cc_ast_stmt_if_t* ifNode = (jx_cc_ast_stmt_if_t*)node;
		astDumpExpression(ast, "cond", ifNode->m_CondExpr);
		astDumpStatement(ast, "then", ifNode->m_ThenStmt);
		if (ifNode->m_ElseStmt) {
			astDumpStatement(ast, "else", ifNode->m_ElseStmt);
		}
	} break;
	case JCC_NODE_STMT_RETURN: {
		jx_cc_ast_stmt_expr_t* retNode = (jx_cc_ast_stmt_expr_t*)node;
		if (retNode->m_Expr) {
			astDumpExpression(ast, "expr", retNode->m_Expr);
		}
	} break;
	case JCC_NODE_STMT_FOR: {
		jx_cc_ast_stmt_for_t* forNode = (jx_cc_ast_stmt_for_t*)node;
		jx_config_setUint32(ast, "break_lbl", forNode->m_BreakLbl.m_ID);
		jx_config_setUint32(ast, "continue_lbl", forNode->m_ContinueLbl.m_ID);
		if (forNode->m_InitStmt) {
			astDumpStatement(ast, "init", forNode->m_InitStmt);
		}
		if (forNode->m_CondExpr) {
			astDumpExpression(ast, "cond", forNode->m_CondExpr);
		}
		if (forNode->m_IncExpr) {
			astDumpExpression(ast, "inc", forNode->m_IncExpr);
		}
		astDumpStatement(ast, "body", forNode->m_BodyStmt);
	} break;
	case JCC_NODE_STMT_SWITCH: {
		jx_cc_ast_stmt_switch_t* switchNode = (jx_cc_ast_stmt_switch_t*)node;
		jx_config_setUint32(ast, "break_lbl", switchNode->m_BreakLbl.m_ID);
		astDumpExpression(ast, "cond", switchNode->m_CondExpr);
		astDumpStatement(ast, "body", switchNode->m_BodyStmt);
	} break;
	case JCC_NODE_STMT_CASE: {
		jx_cc_ast_stmt_case_t* caseNode = (jx_cc_ast_stmt_case_t*)node;
		jx_config_setUint32(ast, "label", caseNode->m_Lbl.m_ID);
		jx_config_beginArray(ast, "range");
		{
			jx_config_setInt32(ast, kIndexToStr[0], (int32_t)caseNode->m_Range[0]);
			jx_config_setInt32(ast, kIndexToStr[1], (int32_t)caseNode->m_Range[1]);
		}
		jx_config_endArray(ast);
		
		astDumpStatement(ast, "body", caseNode->m_BodyStmt);
	} break;
	case JCC_NODE_STMT_GOTO: {
		jx_cc_ast_stmt_goto_t* gotoNode = (jx_cc_ast_stmt_goto_t*)node;
		jx_config_setUint32(ast, "label", gotoNode->m_UniqueLabel.m_ID);
	} break;
	case JCC_NODE_STMT_DO: {
		jx_cc_ast_stmt_do_t* doNode = (jx_cc_ast_stmt_do_t*)node;
		jx_config_setUint32(ast, "break_lbl", doNode->m_BreakLbl.m_ID);
		jx_config_setUint32(ast, "continue_lbl", doNode->m_ContinueLbl.m_ID);
		astDumpExpression(ast, "cond", doNode->m_CondExpr);
		astDumpStatement(ast, "body", doNode->m_BodyStmt);
	} break;
	case JCC_NODE_STMT_LABEL: {
		// TODO: 
	} break;
	default:
		JX_NOT_IMPLEMENTED();
		break;
	}

	jx_config_endObject(ast);
}

static void astDumpObject(jx_config_t* ast, const char* name, const jx_cc_object_t* obj)
{
	jx_config_beginObject(ast, name);

	jx_config_setString(ast, "obj_type", (obj->m_Flags & JCC_OBJECT_FLAGS_IS_FUNCTION_Msk) != 0 ? "function" : "variable", UINT32_MAX);
	jx_config_setUint32(ast, "uid", (uint32_t)(uintptr_t)obj);
	if (obj->m_Name) {
		jx_config_setString(ast, "name", obj->m_Name, UINT32_MAX);
	}
	astDumpType(ast, "type", obj->m_Type);
	jx_config_setBoolean(ast, "is_local", (obj->m_Flags & JCC_OBJECT_FLAGS_IS_LOCAL_Msk) != 0);
	jx_config_setBoolean(ast, "is_definition", (obj->m_Flags & JCC_OBJECT_FLAGS_IS_DEFINITION_Msk) != 0);
	jx_config_setBoolean(ast, "is_static", (obj->m_Flags & JCC_OBJECT_FLAGS_IS_STATIC_Msk) != 0);

	if ((obj->m_Flags & JCC_OBJECT_FLAGS_IS_FUNCTION_Msk) != 0) {
		jx_config_setBoolean(ast, "is_inline", (obj->m_Flags & JCC_OBJECT_FLAGS_IS_INLINE_Msk) != 0);
		jx_config_setBoolean(ast, "is_live", (obj->m_Flags & JCC_OBJECT_FLAGS_IS_LIVE_Msk) != 0);
		jx_config_setBoolean(ast, "is_root", (obj->m_Flags & JCC_OBJECT_FLAGS_IS_ROOT_Msk) != 0);

		jx_config_beginObject(ast, "params");
		{
			jx_cc_object_t* param = obj->m_FuncParams;
			while (param) {
				astDumpObject(ast, param->m_Name, param);
				param = param->m_Next;
			}
		}
		jx_config_endObject(ast);

		jx_config_beginArray(ast, "locals");
		{
			uint32_t localID = 0;
			jx_cc_object_t* local = obj->m_FuncLocals;
			while (local) {
				astDumpObject(ast, kIndexToStr[localID], local);
				++localID;
				local = local->m_Next;
			}
		}
		jx_config_endArray(ast);

		if (obj->m_FuncBody) {
			astDumpStatement(ast, "body", obj->m_FuncBody);
		}
	} else {
		jx_config_setBoolean(ast, "is_tentative", (obj->m_Flags & JCC_OBJECT_FLAGS_IS_TENTATIVE_Msk) != 0);
		jx_config_setBoolean(ast, "is_tls", (obj->m_Flags & JCC_OBJECT_FLAGS_IS_TLS_Msk) != 0);
		jx_config_setInt32(ast, "alignment", obj->m_Alignment);
	}

	jx_config_endObject(ast);
}

static jx_cc_ast_expr_t* astCleanupExpression(jx_cc_ast_expr_t* node)
{
	switch (node->super.m_Kind) {
	case JCC_NODE_EXPR_NULL:
		node = NULL;
	case JCC_NODE_VARIABLE: {
	} break;
	case JCC_NODE_NUMBER: {
	} break;
	case JCC_NODE_EXPR_ADDR: {
		jx_cc_ast_expr_unary_t* unaryNode = (jx_cc_ast_expr_unary_t*)node;
		unaryNode->m_Expr = astCleanupExpression(unaryNode->m_Expr);
	} break;
	case JCC_NODE_EXPR_COMMA: {
		jx_cc_ast_expr_binary_t* commaNode = (jx_cc_ast_expr_binary_t*)node;
		commaNode->m_ExprLHS = astCleanupExpression(commaNode->m_ExprLHS);
		commaNode->m_ExprRHS = astCleanupExpression(commaNode->m_ExprRHS);
		if (!commaNode->m_ExprLHS && !commaNode->m_ExprRHS) {
			node = NULL;
		} else if (!commaNode->m_ExprLHS) {
			node = commaNode->m_ExprRHS;
		} else if (!commaNode->m_ExprRHS) {
			node = commaNode->m_ExprLHS;
		} else {
			// TODO: Comma expression where lhs = memzero and rhs = assign and the assignment
			// fills the whole variable => remove memzero and replace comma expression with 
			// rhs expression
		}
	} break;
	case JCC_NODE_EXPR_ASSIGN: {
		jx_cc_ast_expr_binary_t* binaryNode = (jx_cc_ast_expr_binary_t*)node;
		binaryNode->m_ExprLHS = astCleanupExpression(binaryNode->m_ExprLHS);
		binaryNode->m_ExprRHS = astCleanupExpression(binaryNode->m_ExprRHS);
	} break;
	case JCC_NODE_EXPR_DEREF: {
		jx_cc_ast_expr_unary_t* unaryNode = (jx_cc_ast_expr_unary_t*)node;
		unaryNode->m_Expr = astCleanupExpression(unaryNode->m_Expr);
	} break;
	case JCC_NODE_EXPR_ADD: 
	case JCC_NODE_EXPR_SUB:
	case JCC_NODE_EXPR_MUL: 
	case JCC_NODE_EXPR_DIV:
	case JCC_NODE_EXPR_EQUAL: 
	case JCC_NODE_EXPR_NOT_EQUAL:
	case JCC_NODE_EXPR_LESS_THAN: 
	case JCC_NODE_EXPR_LESS_EQUAL: {
		jx_cc_ast_expr_binary_t* binaryNode = (jx_cc_ast_expr_binary_t*)node;
		binaryNode->m_ExprLHS = astCleanupExpression(binaryNode->m_ExprLHS);
		binaryNode->m_ExprRHS = astCleanupExpression(binaryNode->m_ExprRHS);
	} break;
	case JCC_NODE_EXPR_CAST: {
		jx_cc_ast_expr_unary_t* castNode = (jx_cc_ast_expr_unary_t*)node;
		castNode->m_Expr = astCleanupExpression(castNode->m_Expr);

		jx_cc_type_t* to = node->m_Type;
		jx_cc_type_t* from = castNode->m_Expr->m_Type;
		if (astIsTypeEqual(to, from)) {
			node = castNode->m_Expr;
		} else {
			if (castNode->m_Expr->super.m_Kind == JCC_NODE_NUMBER) {
				// Check if constant can be converted to the casted type without an actual cast.
				jcc_basic_type basicTo = astGetBasicType(to);
				jcc_basic_type basicFrom = astGetBasicType(from);

				if (basicTo == JCC_BASIC_TYPE_INT64 && basicFrom == JCC_BASIC_TYPE_INT32) {
					// TODO: sign or zero extension
					castNode->m_Expr->m_Type = to;
					node = castNode->m_Expr;
				}
			}
		}
	} break;
	case JCC_NODE_EXPR_NEG: {
		jx_cc_ast_expr_unary_t* unaryExpr = (jx_cc_ast_expr_unary_t*)node;
		unaryExpr->m_Expr = astCleanupExpression(unaryExpr->m_Expr);
	} break;
	case JCC_NODE_EXPR_MEMZERO: {
	} break;
	case JCC_NODE_EXPR_MEMBER: {
	} break;
	case JCC_NODE_EXPR_FUNC_CALL: {
		jx_cc_ast_expr_funccall_t* funcCallNode = (jx_cc_ast_expr_funccall_t*)node;
		funcCallNode->m_FuncExpr = astCleanupExpression(funcCallNode->m_FuncExpr);

		const uint32_t numArgs = funcCallNode->m_NumArgs;
		for (uint32_t iArg = 0; iArg < numArgs; ++iArg) {
			funcCallNode->m_Args[iArg] = astCleanupExpression(funcCallNode->m_Args[iArg]);
		}
	} break;
	default: 
		JX_NOT_IMPLEMENTED();
		break;
	}

	return node;
}

static jx_cc_ast_stmt_t* astCleanupStatement(jx_cc_ast_stmt_t* node)
{
	switch (node->super.m_Kind) {
	case JCC_NODE_STMT_BLOCK: {
		jx_cc_ast_stmt_block_t* blockNode = (jx_cc_ast_stmt_block_t*)node;
		
		const uint32_t numChildren = blockNode->m_NumChildren;
		for (uint32_t iChild = numChildren; iChild > 0; --iChild) {
			const uint32_t childID = iChild - 1;
			jx_cc_ast_stmt_t* stmt = (jx_cc_ast_stmt_t*)blockNode->m_Children[childID];

			jx_cc_ast_stmt_t* newStmt = astCleanupStatement(stmt);
			if (!newStmt) {
				jx_memmove(&blockNode->m_Children[childID], &blockNode->m_Children[childID + 1], sizeof(jx_cc_ast_node_t*) * (blockNode->m_NumChildren - childID - 1));
				blockNode->m_NumChildren--;
			} else {
				blockNode->m_Children[childID] = newStmt;
			}
		}

		// If a block does not contain any statements remove it from the tree.
		if (blockNode->m_NumChildren == 0) {
			node = NULL;
		}

		// TODO: If body has a single statement, replace the block with 
		// the single statement
	} break;
	case JCC_NODE_STMT_EXPR: {
		jx_cc_ast_stmt_expr_t* stmt = (jx_cc_ast_stmt_expr_t*)node;

		if (stmt->m_Expr) {
			stmt->m_Expr = astCleanupExpression(stmt->m_Expr);
		}

		if (!stmt->m_Expr) {
			node = NULL;
		}
	} break;
	case JCC_NODE_STMT_IF: {
		jx_cc_ast_stmt_if_t* ifNode = (jx_cc_ast_stmt_if_t*)node;

		ifNode->m_CondExpr = astCleanupExpression(ifNode->m_CondExpr);
		ifNode->m_ThenStmt = astCleanupStatement(ifNode->m_ThenStmt);
		if (ifNode->m_ElseStmt) {
			ifNode->m_ElseStmt = astCleanupStatement(ifNode->m_ElseStmt);
		}
	} break;
	case JCC_NODE_STMT_RETURN: {
		jx_cc_ast_stmt_expr_t* retNode = (jx_cc_ast_stmt_expr_t*)node;

		if (retNode->m_Expr) {
			retNode->m_Expr = astCleanupExpression(retNode->m_Expr);
		}
	} break;
	case JCC_NODE_STMT_FOR: {
		jx_cc_ast_stmt_for_t* forNode = (jx_cc_ast_stmt_for_t*)node;

		if (forNode->m_InitStmt) {
			forNode->m_InitStmt = astCleanupStatement(forNode->m_InitStmt);
		}
		if (forNode->m_CondExpr) {
			forNode->m_CondExpr = astCleanupExpression(forNode->m_CondExpr);
		}
		forNode->m_BodyStmt = astCleanupStatement(forNode->m_BodyStmt);
		if (forNode->m_IncExpr) {
			forNode->m_IncExpr = astCleanupExpression(forNode->m_IncExpr);
		}
	} break;
	case JCC_NODE_STMT_SWITCH: {
		jx_cc_ast_stmt_switch_t* switchNode = (jx_cc_ast_stmt_switch_t*)node;
		switchNode->m_CondExpr = astCleanupExpression(switchNode->m_CondExpr);
		switchNode->m_BodyStmt = astCleanupStatement(switchNode->m_BodyStmt);
	} break;
	case JCC_NODE_STMT_CASE: {
		jx_cc_ast_stmt_case_t* caseNode = (jx_cc_ast_stmt_case_t*)node;
		caseNode->m_BodyStmt = astCleanupStatement(caseNode->m_BodyStmt);
	} break;
	case JCC_NODE_STMT_GOTO: {
		// No-op
	} break;
	case JCC_NODE_STMT_DO: {
		jx_cc_ast_stmt_do_t* doNode = (jx_cc_ast_stmt_do_t*)node;

		doNode->m_CondExpr = astCleanupExpression(doNode->m_CondExpr);
		doNode->m_BodyStmt = astCleanupStatement(doNode->m_BodyStmt);
	} break;
	default:
		JX_NOT_IMPLEMENTED();
		break;
	}

	return node;
}

static void astCleanupObject(jx_cc_object_t* obj)
{
	if ((obj->m_Flags & JCC_OBJECT_FLAGS_IS_FUNCTION_Msk) != 0) {
		if ((obj->m_Flags & JCC_OBJECT_FLAGS_IS_DEFINITION_Msk) != 0 && (obj->m_Flags & JCC_OBJECT_FLAGS_IS_LIVE_Msk) != 0) {
			obj->m_FuncBody = astCleanupStatement(obj->m_FuncBody);
		}
	} else {

	}
}

#include <Windows.h>

int main(int argc, char** argv)
{
	jx_kernel_initAPI();

	// Redirect system logger to file and console
	{
		// Change application directories.
		if (os_api->fsSetBaseDir(JX_FILE_BASE_DIR_USERDATA, JX_FILE_BASE_DIR_USERDATA, "jitcc") != JX_ERROR_NONE || 
			os_api->fsSetBaseDir(JX_FILE_BASE_DIR_USERAPPDATA, JX_FILE_BASE_DIR_USERAPPDATA, "jitcc") != JX_ERROR_NONE || 
			os_api->fsSetBaseDir(JX_FILE_BASE_DIR_TEMP, JX_FILE_BASE_DIR_TEMP, "jitcc") != JX_ERROR_NONE) {
			return -1;
		}

		jx_logger_i* sysLogger = logger_api->createCompositeLogger(allocator_api->m_SystemAllocator, 0);
		if (sysLogger) {
			jx_logger_i* fileLogger = logger_api->createFileLogger(allocator_api->m_SystemAllocator, JX_FILE_BASE_DIR_USERDATA, "jitcc.log", JX_LOGGER_FLAGS_MULTITHREADED | JX_LOGGER_FLAGS_APPEND_TIMESTAMP | JX_LOGGER_FLAGS_FLUSH_ON_EVERY_LOG);
			if (fileLogger) {
				logger_api->compositeLoggerAddChild(sysLogger, fileLogger);
			}

			jx_logger_i* consoleLogger = logger_api->createConsoleLogger(allocator_api->m_SystemAllocator, JX_LOGGER_FLAGS_MULTITHREADED);
			if (consoleLogger) {
				logger_api->compositeLoggerAddChild(sysLogger, consoleLogger);
			}

			// Replace system logger
			{
				logger_api->inMemoryLoggerDumpToLogger(logger_api->m_SystemLogger, fileLogger);
				logger_api->destroyLogger(logger_api->m_SystemLogger);
				logger_api->m_SystemLogger = sysLogger;
			}
		}
	}

	jx_allocator_i* allocator = allocator_api->createAllocator("jcc");

#if 1
	for (uint32_t iTest = 1; iTest <= 100; ++iTest) {
		char sourceFile[256];
		jx_snprintf(sourceFile, JX_COUNTOF(sourceFile), "test/c-testsuite/%05d.c", iTest);

		JX_SYS_LOG_INFO(NULL, "%s: ", sourceFile);

		const bool skipTest = false
			|| iTest == 25 // Uses external functions (strlen)
			|| iTest == 40 // Uses external functions (calloc)
			|| iTest == 45 // Global pointer to global variable (relocations)
			|| iTest == 49 // Global pointer to global variable (relocations)
			|| iTest == 56 // Uses external functions (printf)
			|| iTest == 61 // Missing; Requires preprocessor
			|| iTest == 62 // Missing; Requires preprocessor
			|| iTest == 63 // Missing; Requires preprocessor
			|| iTest == 64 // Missing; Requires preprocessor
			|| iTest == 65 // Missing; Requires preprocessor
			|| iTest == 66 // Missing; Requires preprocessor
			|| iTest == 67 // Missing; Requires preprocessor
			|| iTest == 68 // Missing; Requires preprocessor
			|| iTest == 69 // Missing; Requires preprocessor
			|| iTest == 70 // Missing; Requires preprocessor
			|| iTest == 71 // Missing; Requires preprocessor
			|| iTest == 74 // Missing; Requires preprocessor
			|| iTest == 75 // Missing; Requires preprocessor
			|| iTest == 79 // Missing; Requires preprocessor
			|| iTest == 83 // Missing; Requires preprocessor
			|| iTest == 84 // Missing; Requires preprocessor
			|| iTest == 85 // Missing; Requires preprocessor
			|| iTest == 89 // Global pointer to global variable (relocations)
			|| iTest == 97 // Missing; Requires preprocessor
			;
		if (skipTest) {
			JX_SYS_LOG_WARNING(NULL, "SKIPPED\n");
			continue;
		}

		jx_cc_context_t* ctx = jx_cc_createContext(allocator, logger_api->m_SystemLogger);
		jx_cc_translation_unit_t* tu = jx_cc_compileFile(ctx, JX_FILE_BASE_DIR_INSTALL, sourceFile);
		if (tu) {
			jx_ir_context_t* irCtx = jx_ir_createContext(allocator);
			jx_irgen_context_t* genCtx = jx_irgen_createContext(irCtx, allocator);

			jx_irgen_moduleGen(genCtx, sourceFile, tu);

			jx_irgen_destroyContext(genCtx);

			jx_mir_context_t* mirCtx = jx_mir_createContext(allocator);
			jx_mirgen_context_t* mirGenCtx = jx_mirgen_createContext(irCtx, mirCtx, allocator);

			jx_ir_module_t* irMod = jx_ir_getModule(irCtx, 0);
			if (irMod) {
				jx_mirgen_moduleGen(mirGenCtx, irMod);
			}

			jx_mirgen_destroyContext(mirGenCtx);

			jx_x64_context_t* jitCtx = jx_x64_createContext(allocator);

			if (jx_x64_emitCode(jitCtx, mirCtx, allocator)) {
				uint32_t bufferSize = 0;
				const uint8_t* buffer = jx64_getBuffer(jitCtx, &bufferSize);

				void* execBuf = VirtualAlloc(NULL, bufferSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
				if (execBuf) {
					jx_memcpy(execBuf, buffer, bufferSize);

					DWORD oldProtect = 0;
					VirtualProtect(execBuf, bufferSize, PAGE_EXECUTE_READWRITE, &oldProtect);

					typedef int32_t(*pfnMain)(void);
					jx_x64_label_t* lblMain = jx64_funcGetLabelByName(jitCtx, "main");
					if (lblMain) {
						pfnMain mainFunc = (pfnMain)((uint8_t*)execBuf + jx64_labelGetOffset(jitCtx, lblMain));
						int32_t ret = mainFunc();
						if (ret == 0) {
							JX_SYS_LOG_DEBUG(NULL, "PASS\n", sourceFile);
						} else {
							JX_SYS_LOG_ERROR(NULL, "FAIL\n", sourceFile);
						}
					} else {
						JX_SYS_LOG_ERROR(NULL, "main() not found!\n", sourceFile);
					}

					VirtualFree(execBuf, 0, MEM_RELEASE);
				}
			}

			jx_x64_destroyContext(jitCtx);
			jx_mir_destroyContext(mirCtx);
			jx_ir_destroyContext(irCtx);
		} else {
			JX_SYS_LOG_ERROR(NULL, "Compilation failed.\n", sourceFile);
		}
		jx_cc_destroyContext(ctx);
	}
#else
	jx_cc_context_t* ctx = jx_cc_createContext(allocator, logger_api->m_SystemLogger);

	const char* sourceFile = "test/c-testsuite/00034.c";
//	const char* sourceFile = "test/pointer_arithmetic.c";

	JX_SYS_LOG_INFO(NULL, "%s\n", sourceFile);
	jx_cc_translation_unit_t* tu = jx_cc_compileFile(ctx, JX_FILE_BASE_DIR_INSTALL, sourceFile);
	if (!tu) {
		JX_SYS_LOG_INFO(NULL, "Failed to compile \"%s\"\n", sourceFile);
		goto end;
	}

#if 0
	// Cleanup ast
	JX_SYS_LOG_INFO(NULL, "Cleaning up AST...\n");
	{
		jx_cc_object_t* ptr = globals;
		while (ptr) {
			astCleanupObject(ptr);
			ptr = ptr->m_Next;
		}
	}
#endif

#if 0
	JX_SYS_LOG_INFO(NULL, "Saving AST to JSON...\n");
	{
		jx_config_t* ast = jx_config_createConfig(allocator);

		jx_config_beginObject(ast, "globals");

		jx_cc_object_t* ptr = tu->m_Globals;
		while (ptr) {
			astDumpObject(ast, ptr->m_Name, ptr);
			ptr = ptr->m_Next;
		}

		jx_config_endObject(ast);

		jx_config_saveJSON(ast, JX_FILE_BASE_DIR_INSTALL, "hello_world.json");

		jx_config_destroyConfig(ast);
	}
#endif

	JX_SYS_LOG_INFO(NULL, "Building IR...\n");
	{
		jx_ir_context_t* irCtx = jx_ir_createContext(allocator);
		jx_irgen_context_t* genCtx = jx_irgen_createContext(irCtx, allocator);

		jx_irgen_moduleGen(genCtx, sourceFile, tu);

		jx_irgen_destroyContext(genCtx);

		jx_string_buffer_t* sb = jx_strbuf_create(allocator);
		jx_ir_print(irCtx, sb);
		jx_strbuf_nullTerminate(sb);
		JX_SYS_LOG_INFO(NULL, "%s", jx_strbuf_getString(sb, NULL));
		jx_strbuf_destroy(sb);

		{
			jx_mir_context_t* mirCtx = jx_mir_createContext(allocator);
			jx_mirgen_context_t* mirGenCtx = jx_mirgen_createContext(irCtx, mirCtx, allocator);

			jx_ir_module_t* irMod = jx_ir_getModule(irCtx, 0);
			if (irMod) {
				jx_mirgen_moduleGen(mirGenCtx, irMod);
			}

			jx_mirgen_destroyContext(mirGenCtx);

			sb = jx_strbuf_create(allocator);
			jx_mir_print(mirCtx, sb);
			jx_strbuf_nullTerminate(sb);
			JX_SYS_LOG_INFO(NULL, "%s", jx_strbuf_getString(sb, NULL));
			jx_strbuf_destroy(sb);

			jx_x64_context_t* jitCtx = jx_x64_createContext(allocator);

			if (jx_x64_emitCode(jitCtx, mirCtx, allocator)) {
				sb = jx_strbuf_create(allocator);

				uint32_t bufferSize = 0;
				const uint8_t* buffer = jx64_getBuffer(jitCtx, &bufferSize);
				for (uint32_t i = 0; i < bufferSize; ++i) {
					jx_strbuf_printf(sb, "%02X", buffer[i]);
				}

				jx_strbuf_nullTerminate(sb);
				JX_SYS_LOG_INFO(NULL, "\n%s\n\n", jx_strbuf_getString(sb, NULL));
				jx_strbuf_destroy(sb);

				void* execBuf = VirtualAlloc(NULL, bufferSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
				if (execBuf) {
					jx_memcpy(execBuf, buffer, bufferSize);

					DWORD oldProtect = 0;
					VirtualProtect(execBuf, bufferSize, PAGE_EXECUTE_READWRITE, &oldProtect);

					typedef int32_t (*pfnMain)(void);
					jx_x64_label_t* lblMain = jx64_funcGetLabelByName(jitCtx, "main");
					if (lblMain) {
						pfnMain mainFunc = (pfnMain)((uint8_t*)execBuf + jx64_labelGetOffset(jitCtx, lblMain));
						int32_t ret = mainFunc();
						JX_SYS_LOG_DEBUG(NULL, "main() returned %d\n", ret);
					} else {
						JX_SYS_LOG_ERROR(NULL, "main() not found!\n");
					}

					VirtualFree(execBuf, 0, MEM_RELEASE);
				}
			}

			jx_mir_destroyContext(mirCtx);
		}

		jx_ir_destroyContext(irCtx);
	}

end:
	jx_cc_destroyContext(ctx);
#endif

	allocator_api->destroyAllocator(allocator);

	jx_kernel_shutdownAPI();
 
	return 0;
}
