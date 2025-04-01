#ifndef JX_IR_GEN_H
#define JX_IR_GEN_H

#include <stdint.h>
#include <stdbool.h>

typedef struct jx_allocator_i jx_allocator_i;
typedef struct jx_cc_translation_unit_t jx_cc_translation_unit_t;
typedef struct jx_ir_context_t jx_ir_context_t;

typedef struct jx_irgen_context_t jx_irgen_context_t;

jx_irgen_context_t* jx_irgen_createContext(jx_ir_context_t* irCtx, jx_allocator_i* allocator);
void jx_irgen_destroyContext(jx_irgen_context_t* ctx);

bool jx_irgen_moduleGen(jx_irgen_context_t* ctx, const char* moduleName, jx_cc_translation_unit_t* tu);

#endif // JX_IR_GEN_H
