#ifndef JX_IR_PASS_H
#define JX_IR_PASS_H

#include <stdint.h>
#include <stdbool.h>

typedef struct jx_allocator_i jx_allocator_i;
typedef struct jx_ir_function_pass_t jx_ir_function_pass_t;
typedef struct jx_ir_module_pass_t jx_ir_module_pass_t;

typedef bool (*jirFuncPassCtorFunc)(jx_ir_function_pass_t* pass, jx_allocator_i* allocator);
typedef bool (*jirModulePassCtorFunc)(jx_ir_module_pass_t* pass, jx_allocator_i* allocator);

bool jx_ir_funcPassCreate_singleRetBlock(jx_ir_function_pass_t* pass, jx_allocator_i* allocator);
bool jx_ir_funcPassCreate_simplifyCFG(jx_ir_function_pass_t* pass, jx_allocator_i* allocator);
bool jx_ir_funcPassCreate_simpleSSA(jx_ir_function_pass_t* pass, jx_allocator_i* allocator);
bool jx_ir_funcPassCreate_constantFolding(jx_ir_function_pass_t* pass, jx_allocator_i* allocator);
bool jx_ir_funcPassCreate_peephole(jx_ir_function_pass_t* pass, jx_allocator_i* allocator);
bool jx_ir_funcPassCreate_canonicalizeOperands(jx_ir_function_pass_t* pass, jx_allocator_i* allocator);
bool jx_ir_funcPassCreate_reorderBasicBlocks(jx_ir_function_pass_t* pass, jx_allocator_i* allocator);
bool jx_ir_funcPassCreate_removeRedundantPhis(jx_ir_function_pass_t* pass, jx_allocator_i* allocator);
bool jx_ir_funcPassCreate_deadCodeElimination(jx_ir_function_pass_t* pass, jx_allocator_i* allocator);
bool jx_ir_funcPassCreate_localValueNumbering(jx_ir_function_pass_t* pass, jx_allocator_i* allocator);

bool jx_ir_modulePassCreate_inlineFuncs(jx_ir_module_pass_t* pass, jx_allocator_i* allocator);

#endif // JX_IR_PASS_H
