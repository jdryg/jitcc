#ifndef JX_APPLICATION_H
#define JX_APPLICATION_H

#include <stdint.h>
#include <stdbool.h>

typedef enum jx_app_tick_result
{
	JAPP_TICK_EXIT     = 0,
	JAPP_TICK_CONTINUE = 1,
	JAPP_TICK_RESTART  = 2,
} jx_app_tick_result;

#define JAPP_FLAGS_RESTART (1u << 0)

typedef struct jx_application_o jx_application_o;

typedef struct jx_application_i
{
	jx_application_o* m_Inst;

	int32_t (*init)(jx_application_o* inst, int32_t argc, char** argv, uint32_t flags);
	void (*shutdown)(jx_application_o* inst, uint32_t flags);
	jx_app_tick_result (*tick)(jx_application_o* inst);
} jx_application_i;

#endif // JX_APPLICATION_H
