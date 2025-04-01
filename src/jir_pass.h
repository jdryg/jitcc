#ifndef JX_IR_PASS_H
#define JX_IR_PASS_H

#include <stdint.h>
#include <stdbool.h>

typedef struct jx_allocator_i jx_allocator_i;
typedef struct jx_ir_function_pass_t jx_ir_function_pass_t;

bool jx_ir_funcPassCreate_singleRetBlock(jx_ir_function_pass_t* pass, jx_allocator_i* allocator);
bool jx_ir_funcPassCreate_simplifyCFG(jx_ir_function_pass_t* pass, jx_allocator_i* allocator);
bool jx_ir_funcPassCreate_simpleSSA(jx_ir_function_pass_t* pass, jx_allocator_i* allocator);

#endif // JX_IR_PASS_H
