#ifndef JX_X64_GEN_H
#define JX_X64_GEN_H

#include <stdint.h>
#include <stdbool.h>
#include "jit.h"

typedef struct jx_allocator_i jx_allocator_i;
typedef struct jx_x64_context_t jx_x64_context_t;
typedef struct jx_mir_context_t jx_mir_context_t;

typedef struct jx_x64gen_context_t jx_x64gen_context_t;

jx_x64gen_context_t* jx_x64gen_createContext(jx_x64_context_t* jitCtx, jx_mir_context_t* mirCtx, jx64GetExternalSymbolAddrCallback externalSymCb, void* userData, jx_allocator_i* allocator);
void jx_x64gen_destroyContext(jx_x64gen_context_t* ctx);

bool jx_x64gen_codeGen(jx_x64gen_context_t* ctx);

#endif // JX_X64_GEN_H
