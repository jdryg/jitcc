#ifndef JX_MIR_PASS_H
#define JX_MIR_PASS_H

#include <stdint.h>
#include <stdbool.h>

typedef struct jx_allocator_i jx_allocator_i;
typedef struct jx_mir_function_pass_t jx_mir_function_pass_t;

typedef bool (*jmirFuncPassCtorFunc)(jx_mir_function_pass_t* pass, jx_allocator_i* allocator);

bool jx_mir_funcPassCreate_removeFallthroughJmp(jx_mir_function_pass_t* pass, jx_allocator_i* allocator);
bool jx_mir_funcPassCreate_removeRedundantMoves(jx_mir_function_pass_t* pass, jx_allocator_i* allocator);
bool jx_mir_funcPassCreate_simplifyCondJmp(jx_mir_function_pass_t* pass, jx_allocator_i* allocator);
bool jx_mir_funcPassCreate_fixMemMemOps(jx_mir_function_pass_t* pass, jx_allocator_i* allocator);
bool jx_mir_funcPassCreate_regAlloc(jx_mir_function_pass_t* pass, jx_allocator_i* allocator);
bool jx_mir_funcPassCreate_peephole(jx_mir_function_pass_t* pass, jx_allocator_i* allocator);
bool jx_mir_funcPassCreate_combineLEAs(jx_mir_function_pass_t* pass, jx_allocator_i* allocator);
bool jx_mir_funcPassCreate_deadCodeElimination(jx_mir_function_pass_t* pass, jx_allocator_i* allocator);
bool jx_mir_funcPassCreate_redundantConstElimination(jx_mir_function_pass_t* pass, jx_allocator_i* allocator);

#endif // JX_MIR_PASS_H
