#ifndef JX_ERROR_H
#error "Must be included from jx/error.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

static inline void jerrorInit(jx_error_t* err)
{
	err->m_Code = JX_ERROR_NONE;
	err->m_Msg = NULL;
}

static inline bool jerrorIsOk(const jx_error_t* err)
{
	return err->m_Code == JX_ERROR_NONE;
}

static void jerrorReset(jx_error_t* err)
{
	err->m_Code = JX_ERROR_NONE;
	err->m_Msg = NULL;
}

#ifdef __cplusplus
}
#endif
