#ifndef JX_MACHINE_IR_GEN_H
#define JX_MACHINE_IR_GEN_H

#include <stdint.h>
#include <stdbool.h>

typedef struct jx_allocator_i jx_allocator_i;
typedef struct jx_ir_context_t jx_ir_context_t;
typedef struct jx_mir_context_t jx_mir_context_t;
typedef struct jx_ir_module_t jx_ir_module_t;

typedef struct jx_mirgen_context_t jx_mirgen_context_t;

jx_mirgen_context_t* jx_mirgen_createContext(jx_ir_context_t* irCtx, jx_mir_context_t* mirCtx, jx_allocator_i* allocator);
void jx_mirgen_destroyContext(jx_mirgen_context_t* ctx);

bool jx_mirgen_moduleGen(jx_mirgen_context_t* ctx, jx_ir_module_t* mod);

#endif // JX_MACHINE_IR_GEN_H
