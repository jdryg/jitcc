#ifndef JX_X64_GEN_H
#define JX_X64_GEN_H

#include <stdint.h>
#include <stdbool.h>

typedef struct jx_allocator_i jx_allocator_i;
typedef struct jx_x64_context_t jx_x64_context_t;
typedef struct jx_mir_context_t jx_mir_context_t;

bool jx_x64_emitCode(jx_x64_context_t* jitCtx, jx_mir_context_t* mirCtx, jx_allocator_i* allocator);

#endif // JX_X64_GEN_H
