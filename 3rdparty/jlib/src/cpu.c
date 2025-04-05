#include <jlib/cpu.h>
#include <jlib/string.h>
#include <jlib/logger.h>
#include <intrin.h>

#define JCPUINFO_MAKE_ID(type, cpuid_eax_value, cpuid_ecx_value, cpuid_res_id, first_bit, num_bits) 0 \
	| (((cpuid_eax_value) & 0xFF) << 0) \
	| (((cpuid_ecx_value) & 0xFF) << 8) \
	| (((cpuid_res_id) & 0x03) << 16) \
	| (((first_bit) & 0x1F) << 18) \
	| (((num_bits) & 0x3F) << 23) \
	| (((type) & 0x01) << 30)

#define JCPUINFO_BASIC                          0
#define JCPUINFO_EXTENDED                       1

#define JCPUINFO_EAX                            0
#define JCPUINFO_EBX                            1
#define JCPUINFO_ECX                            2
#define JCPUINFO_EDX                            3

#define JCPUINFO_BASIC_VENDOR_ID_0              JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x00, 0x00, JCPUINFO_EBX, 0, 32)
#define JCPUINFO_BASIC_VENDOR_ID_1              JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x00, 0x00, JCPUINFO_EDX, 0, 32)
#define JCPUINFO_BASIC_VENDOR_ID_2              JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x00, 0x00, JCPUINFO_ECX, 0, 32)
#define JCPUINFO_BASIC_PROCESSOR_SIGNATURE      JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x01, 0x00, JCPUINFO_EAX, 0, 32)
#define JCPUINFO_BASIC_STEPPING_ID              JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x01, 0x00, JCPUINFO_EAX, 0, 4)
#define JCPUINFO_BASIC_MODEL_ID                 JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x01, 0x00, JCPUINFO_EAX, 4, 4)
#define JCPUINFO_BASIC_FAMILY_ID                JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x01, 0x00, JCPUINFO_EAX, 8, 4)
#define JCPUINFO_BASIC_PROCESSOR_TYPE           JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x01, 0x00, JCPUINFO_EAX, 12, 2)
#define JCPUINFO_BASIC_EXTENDED_MODEL_ID        JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x01, 0x00, JCPUINFO_EAX, 16, 4) // NOTE: Needs further processing. See comments below Table 3-9: Processor Type Field
#define JCPUINFO_BASIC_EXTENDED_FAMILY_ID       JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x01, 0x00, JCPUINFO_EAX, 20, 8) // NOTE: Needs further processing. See comments below Table 3-9: Processor Type Field
#define JCPUINFO_BASIC_BRAND_INDEX              JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x01, 0x00, JCPUINFO_EBX, 0, 8)
#define JCPUINFO_BASIC_CLFLUSH_LINE_SIZE        JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x01, 0x00, JCPUINFO_EBX, 8, 8)
#define JCPUINFO_BASIC_UNIQUE_INITIAL_APICS     JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x01, 0x00, JCPUINFO_EBX, 16, 8)
#define JCPUINFO_BASIC_INITIAL_APIC_ID          JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x01, 0x00, JCPUINFO_EBX, 24, 8)
#define JCPUINFO_BASIC_SSE3                     JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x01, 0x00, JCPUINFO_ECX, 0, 1)
#define JCPUINFO_BASIC_PCLMULQDQ                JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x01, 0x00, JCPUINFO_ECX, 1, 1)
#define JCPUINFO_BASIC_DTES64                   JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x01, 0x00, JCPUINFO_ECX, 2, 1)
#define JCPUINFO_BASIC_MONITOR                  JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x01, 0x00, JCPUINFO_ECX, 3, 1)
#define JCPUINFO_BASIC_DS_CPL                   JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x01, 0x00, JCPUINFO_ECX, 4, 1)
#define JCPUINFO_BASIC_VMX                      JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x01, 0x00, JCPUINFO_ECX, 5, 1)
#define JCPUINFO_BASIC_SMX                      JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x01, 0x00, JCPUINFO_ECX, 6, 1)
#define JCPUINFO_BASIC_EIST                     JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x01, 0x00, JCPUINFO_ECX, 7, 1)
#define JCPUINFO_BASIC_TM2                      JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x01, 0x00, JCPUINFO_ECX, 8, 1)
#define JCPUINFO_BASIC_SSSE3                    JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x01, 0x00, JCPUINFO_ECX, 9, 1)
#define JCPUINFO_BASIC_CNXT_ID                  JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x01, 0x00, JCPUINFO_ECX, 10, 1)
#define JCPUINFO_BASIC_SDBG                     JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x01, 0x00, JCPUINFO_ECX, 11, 1)
#define JCPUINFO_BASIC_FMA                      JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x01, 0x00, JCPUINFO_ECX, 12, 1)
#define JCPUINFO_BASIC_CMPXCHG16B               JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x01, 0x00, JCPUINFO_ECX, 13, 1)
#define JCPUINFO_BASIC_XTPR_UPDATE_CONTROL      JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x01, 0x00, JCPUINFO_ECX, 14, 1)
#define JCPUINFO_BASIC_PDCM                     JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x01, 0x00, JCPUINFO_ECX, 15, 1)
#define JCPUINFO_BASIC_PCID                     JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x01, 0x00, JCPUINFO_ECX, 17, 1)
#define JCPUINFO_BASIC_DCA                      JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x01, 0x00, JCPUINFO_ECX, 18, 1)
#define JCPUINFO_BASIC_SSE4_1                   JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x01, 0x00, JCPUINFO_ECX, 19, 1)
#define JCPUINFO_BASIC_SSE4_2                   JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x01, 0x00, JCPUINFO_ECX, 20, 1)
#define JCPUINFO_BASIC_X2APIC                   JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x01, 0x00, JCPUINFO_ECX, 21, 1)
#define JCPUINFO_BASIC_MOVBE                    JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x01, 0x00, JCPUINFO_ECX, 22, 1)
#define JCPUINFO_BASIC_POPCNT                   JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x01, 0x00, JCPUINFO_ECX, 23, 1)
#define JCPUINFO_BASIC_TSC_DEADLINE             JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x01, 0x00, JCPUINFO_ECX, 24, 1)
#define JCPUINFO_BASIC_AESNI                    JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x01, 0x00, JCPUINFO_ECX, 25, 1)
#define JCPUINFO_BASIC_XSAVE                    JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x01, 0x00, JCPUINFO_ECX, 26, 1)
#define JCPUINFO_BASIC_OSXSAVE                  JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x01, 0x00, JCPUINFO_ECX, 27, 1)
#define JCPUINFO_BASIC_AVX                      JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x01, 0x00, JCPUINFO_ECX, 28, 1)
#define JCPUINFO_BASIC_F16C                     JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x01, 0x00, JCPUINFO_ECX, 29, 1)
#define JCPUINFO_BASIC_RDRAND                   JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x01, 0x00, JCPUINFO_ECX, 30, 1)
#define JCPUINFO_BASIC_FPU                      JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x01, 0x00, JCPUINFO_EDX, 0, 1)
#define JCPUINFO_BASIC_VME                      JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x01, 0x00, JCPUINFO_EDX, 1, 1)
#define JCPUINFO_BASIC_DE                       JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x01, 0x00, JCPUINFO_EDX, 2, 1)
#define JCPUINFO_BASIC_PSE                      JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x01, 0x00, JCPUINFO_EDX, 3, 1)
#define JCPUINFO_BASIC_TSC                      JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x01, 0x00, JCPUINFO_EDX, 4, 1)
#define JCPUINFO_BASIC_MSR                      JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x01, 0x00, JCPUINFO_EDX, 5, 1)
#define JCPUINFO_BASIC_PAE                      JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x01, 0x00, JCPUINFO_EDX, 6, 1)
#define JCPUINFO_BASIC_MCE                      JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x01, 0x00, JCPUINFO_EDX, 7, 1)
#define JCPUINFO_BASIC_CX8                      JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x01, 0x00, JCPUINFO_EDX, 8, 1)
#define JCPUINFO_BASIC_APIC                     JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x01, 0x00, JCPUINFO_EDX, 9, 1)
#define JCPUINFO_BASIC_SEP                      JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x01, 0x00, JCPUINFO_EDX, 11, 1)
#define JCPUINFO_BASIC_MTRR                     JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x01, 0x00, JCPUINFO_EDX, 12, 1)
#define JCPUINFO_BASIC_PGE                      JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x01, 0x00, JCPUINFO_EDX, 13, 1)
#define JCPUINFO_BASIC_MCA                      JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x01, 0x00, JCPUINFO_EDX, 14, 1)
#define JCPUINFO_BASIC_CMOV                     JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x01, 0x00, JCPUINFO_EDX, 15, 1)
#define JCPUINFO_BASIC_PAT                      JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x01, 0x00, JCPUINFO_EDX, 16, 1)
#define JCPUINFO_BASIC_PSE_36                   JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x01, 0x00, JCPUINFO_EDX, 17, 1)
#define JCPUINFO_BASIC_PSN                      JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x01, 0x00, JCPUINFO_EDX, 18, 1)
#define JCPUINFO_BASIC_CLFSH                    JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x01, 0x00, JCPUINFO_EDX, 19, 1)
#define JCPUINFO_BASIC_DS                       JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x01, 0x00, JCPUINFO_EDX, 21, 1)
#define JCPUINFO_BASIC_ACPI                     JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x01, 0x00, JCPUINFO_EDX, 22, 1)
#define JCPUINFO_BASIC_MMX                      JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x01, 0x00, JCPUINFO_EDX, 23, 1)
#define JCPUINFO_BASIC_FXSR                     JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x01, 0x00, JCPUINFO_EDX, 24, 1)
#define JCPUINFO_BASIC_SSE                      JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x01, 0x00, JCPUINFO_EDX, 25, 1)
#define JCPUINFO_BASIC_SSE2                     JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x01, 0x00, JCPUINFO_EDX, 26, 1)
#define JCPUINFO_BASIC_SS                       JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x01, 0x00, JCPUINFO_EDX, 27, 1)
#define JCPUINFO_BASIC_HTT                      JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x01, 0x00, JCPUINFO_EDX, 28, 1)
#define JCPUINFO_BASIC_TM                       JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x01, 0x00, JCPUINFO_EDX, 29, 1)
#define JCPUINFO_BASIC_PBE                      JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x01, 0x00, JCPUINFO_EDX, 31, 1)
#define JCPUINFO_BASIC_TLB_CACHE_INFO_0         JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x02, 0x00, JCPUINFO_EAX, 0, 32)
#define JCPUINFO_BASIC_TLB_CACHE_INFO_1         JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x02, 0x00, JCPUINFO_EBX, 0, 32)
#define JCPUINFO_BASIC_TLB_CACHE_INFO_2         JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x02, 0x00, JCPUINFO_ECX, 0, 32)
#define JCPUINFO_BASIC_TLB_CACHE_INFO_3         JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x02, 0x00, JCPUINFO_EDX, 0, 32)

#define JCPUINFO_BASIC_MAX_SUBLEAVES_07         JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x07, 0x00, JCPUINFO_EAX, 0, 32)
#define JCPUINFO_BASIC_FSGSBASE                 JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x07, 0x00, JCPUINFO_EBX, 0, 1)
#define JCPUINFO_BASIC_IA32_TSC_ADJUST          JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x07, 0x00, JCPUINFO_EBX, 1, 1)
#define JCPUINFO_BASIC_SGX                      JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x07, 0x00, JCPUINFO_EBX, 2, 1)
#define JCPUINFO_BASIC_BMI1                     JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x07, 0x00, JCPUINFO_EBX, 3, 1)
#define JCPUINFO_BASIC_HLE                      JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x07, 0x00, JCPUINFO_EBX, 4, 1)
#define JCPUINFO_BASIC_AVX2                     JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x07, 0x00, JCPUINFO_EBX, 5, 1)
#define JCPUINFO_BASIC_FDP_EXCPTN_ONLY          JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x07, 0x00, JCPUINFO_EBX, 6, 1)
#define JCPUINFO_BASIC_SMEP                     JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x07, 0x00, JCPUINFO_EBX, 7, 1)
#define JCPUINFO_BASIC_BMI2                     JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x07, 0x00, JCPUINFO_EBX, 8, 1)
#define JCPUINFO_BASIC_ENHANCED_REPMOVSB        JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x07, 0x00, JCPUINFO_EBX, 9, 1)
#define JCPUINFO_BASIC_INVPCID                  JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x07, 0x00, JCPUINFO_EBX, 10, 1)
#define JCPUINFO_BASIC_RTM                      JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x07, 0x00, JCPUINFO_EBX, 11, 1)
#define JCPUINFO_BASIC_RDT_M                    JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x07, 0x00, JCPUINFO_EBX, 12, 1)
#define JCPUINFO_BASIC_DEPRECATED_FPU_CS_DS     JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x07, 0x00, JCPUINFO_EBX, 13, 1)
#define JCPUINFO_BASIC_MPX                      JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x07, 0x00, JCPUINFO_EBX, 14, 1)
#define JCPUINFO_BASIC_RDT_A                    JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x07, 0x00, JCPUINFO_EBX, 15, 1)
#define JCPUINFO_BASIC_AVX512F                  JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x07, 0x00, JCPUINFO_EBX, 16, 1)
#define JCPUINFO_BASIC_AVX512DQ                 JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x07, 0x00, JCPUINFO_EBX, 17, 1)
#define JCPUINFO_BASIC_RDSEED                   JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x07, 0x00, JCPUINFO_EBX, 18, 1)
#define JCPUINFO_BASIC_ADX                      JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x07, 0x00, JCPUINFO_EBX, 19, 1)
#define JCPUINFO_BASIC_SMAP                     JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x07, 0x00, JCPUINFO_EBX, 20, 1)
#define JCPUINFO_BASIC_AVX512_IFMA              JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x07, 0x00, JCPUINFO_EBX, 21, 1)
#define JCPUINFO_BASIC_CLFLUSHOPT               JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x07, 0x00, JCPUINFO_EBX, 23, 1)
#define JCPUINFO_BASIC_CLWB                     JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x07, 0x00, JCPUINFO_EBX, 24, 1)
#define JCPUINFO_BASIC_INTEL_PROCESSOR_TRACE    JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x07, 0x00, JCPUINFO_EBX, 25, 1)
#define JCPUINFO_BASIC_AVX512PF                 JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x07, 0x00, JCPUINFO_EBX, 26, 1)
#define JCPUINFO_BASIC_AVX512ER                 JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x07, 0x00, JCPUINFO_EBX, 27, 1)
#define JCPUINFO_BASIC_AVX512CD                 JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x07, 0x00, JCPUINFO_EBX, 28, 1)
#define JCPUINFO_BASIC_SHA                      JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x07, 0x00, JCPUINFO_EBX, 29, 1)
#define JCPUINFO_BASIC_AVX512BW                 JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x07, 0x00, JCPUINFO_EBX, 30, 1)
#define JCPUINFO_BASIC_AVX512VL                 JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x07, 0x00, JCPUINFO_EBX, 31, 1)
#define JCPUINFO_BASIC_PREFETCHWT1              JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x07, 0x00, JCPUINFO_ECX, 0, 1)
#define JCPUINFO_BASIC_AVX512_VBMI              JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x07, 0x00, JCPUINFO_ECX, 1, 1)
#define JCPUINFO_BASIC_UMIP                     JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x07, 0x00, JCPUINFO_ECX, 2, 1)
#define JCPUINFO_BASIC_PKU                      JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x07, 0x00, JCPUINFO_ECX, 3, 1)
#define JCPUINFO_BASIC_OSPKE                    JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x07, 0x00, JCPUINFO_ECX, 4, 1)
#define JCPUINFO_BASIC_WAITPKG                  JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x07, 0x00, JCPUINFO_ECX, 5, 1)
#define JCPUINFO_BASIC_AVX512_VBMI2             JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x07, 0x00, JCPUINFO_ECX, 6, 1)
#define JCPUINFO_BASIC_CET_SS                   JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x07, 0x00, JCPUINFO_ECX, 7, 1)
#define JCPUINFO_BASIC_GFNI                     JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x07, 0x00, JCPUINFO_ECX, 8, 1)
#define JCPUINFO_BASIC_VAES                     JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x07, 0x00, JCPUINFO_ECX, 9, 1)
#define JCPUINFO_BASIC_VPCLMULQDQ               JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x07, 0x00, JCPUINFO_ECX, 10, 1)
#define JCPUINFO_BASIC_AVX512_VNNI              JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x07, 0x00, JCPUINFO_ECX, 11, 1)
#define JCPUINFO_BASIC_AVX512_BITALG            JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x07, 0x00, JCPUINFO_ECX, 12, 1)
#define JCPUINFO_BASIC_TME_EN                   JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x07, 0x00, JCPUINFO_ECX, 13, 1)
#define JCPUINFO_BASIC_AVX512_VPOPCNTDQ         JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x07, 0x00, JCPUINFO_ECX, 14, 1)
#define JCPUINFO_BASIC_LA57                     JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x07, 0x00, JCPUINFO_ECX, 16, 1)
#define JCPUINFO_BASIC_MAWAU                    JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x07, 0x00, JCPUINFO_ECX, 17, 5)
#define JCPUINFO_BASIC_RDPID                    JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x07, 0x00, JCPUINFO_ECX, 22, 1)
#define JCPUINFO_BASIC_KL                       JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x07, 0x00, JCPUINFO_ECX, 23, 1)
#define JCPUINFO_BASIC_CLDEMOTE                 JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x07, 0x00, JCPUINFO_ECX, 25, 1)
#define JCPUINFO_BASIC_MOVDIRI                  JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x07, 0x00, JCPUINFO_ECX, 27, 1)
#define JCPUINFO_BASIC_MOVDIR64B                JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x07, 0x00, JCPUINFO_ECX, 28, 1)
#define JCPUINFO_BASIC_SGX_LC                   JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x07, 0x00, JCPUINFO_ECX, 30, 1)
#define JCPUINFO_BASIC_PKS                      JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x07, 0x00, JCPUINFO_ECX, 31, 1)

#define JCPUINFO_BASIC_PROCESSOR_BASE_FREQ      JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x16, 0x00, JCPUINFO_EAX, 0, 16)
#define JCPUINFO_BASIC_PROCESSOR_MAX_FREQ       JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x16, 0x00, JCPUINFO_EBX, 0, 16)
#define JCPUINFO_BASIC_BUS_FREQ                 JCPUINFO_MAKE_ID(JCPUINFO_BASIC, 0x16, 0x00, JCPUINFO_ECX, 0, 16)

#define JCPUINFO_EXT_MAX_EXTENDED_FUNC_ID       JCPUINFO_MAKE_ID(JCPUINFO_EXTENDED, 0x00, 0x00, JCPUINFO_EAX, 0, 32)
#define JCPUINFO_EXT_PROCESSOR_BRAND_STRING_0   JCPUINFO_MAKE_ID(JCPUINFO_EXTENDED, 0x02, 0x00, JCPUINFO_EAX, 0, 32)
#define JCPUINFO_EXT_PROCESSOR_BRAND_STRING_1   JCPUINFO_MAKE_ID(JCPUINFO_EXTENDED, 0x02, 0x00, JCPUINFO_EBX, 0, 32)
#define JCPUINFO_EXT_PROCESSOR_BRAND_STRING_2   JCPUINFO_MAKE_ID(JCPUINFO_EXTENDED, 0x02, 0x00, JCPUINFO_ECX, 0, 32)
#define JCPUINFO_EXT_PROCESSOR_BRAND_STRING_3   JCPUINFO_MAKE_ID(JCPUINFO_EXTENDED, 0x02, 0x00, JCPUINFO_EDX, 0, 32)
#define JCPUINFO_EXT_PROCESSOR_BRAND_STRING_4   JCPUINFO_MAKE_ID(JCPUINFO_EXTENDED, 0x03, 0x00, JCPUINFO_EAX, 0, 32)
#define JCPUINFO_EXT_PROCESSOR_BRAND_STRING_5   JCPUINFO_MAKE_ID(JCPUINFO_EXTENDED, 0x03, 0x00, JCPUINFO_EBX, 0, 32)
#define JCPUINFO_EXT_PROCESSOR_BRAND_STRING_6   JCPUINFO_MAKE_ID(JCPUINFO_EXTENDED, 0x03, 0x00, JCPUINFO_ECX, 0, 32)
#define JCPUINFO_EXT_PROCESSOR_BRAND_STRING_7   JCPUINFO_MAKE_ID(JCPUINFO_EXTENDED, 0x03, 0x00, JCPUINFO_EDX, 0, 32)
#define JCPUINFO_EXT_PROCESSOR_BRAND_STRING_8   JCPUINFO_MAKE_ID(JCPUINFO_EXTENDED, 0x04, 0x00, JCPUINFO_EAX, 0, 32)
#define JCPUINFO_EXT_PROCESSOR_BRAND_STRING_9   JCPUINFO_MAKE_ID(JCPUINFO_EXTENDED, 0x04, 0x00, JCPUINFO_EBX, 0, 32)
#define JCPUINFO_EXT_PROCESSOR_BRAND_STRING_10  JCPUINFO_MAKE_ID(JCPUINFO_EXTENDED, 0x04, 0x00, JCPUINFO_ECX, 0, 32)
#define JCPUINFO_EXT_PROCESSOR_BRAND_STRING_11  JCPUINFO_MAKE_ID(JCPUINFO_EXTENDED, 0x04, 0x00, JCPUINFO_EDX, 0, 32)

#define JCPUINFO_PROCESSOR_BRAND_STRING_MAX_LEN 64
#define JCPUINFO_VENDOR_ID_MAX_LEN              32

typedef struct jx_cpu_info
{
	uint64_t m_Features;
	jx_cpu_version_t m_Version;
	uint16_t m_BusFreq;
	char m_VendorID[JCPUINFO_VENDOR_ID_MAX_LEN];
	char m_ProcessorBrandString[JCPUINFO_PROCESSOR_BRAND_STRING_MAX_LEN];
} jx_cpu_info;

static jx_cpu_info s_CPUInfo = { 0 };

static const char* _jx_cpu_getVendorID(void);
static const char* _jx_cpu_getProcessorBrandString(void);
static uint64_t _jx_cpu_getFeatures(void);
static const jx_cpu_version_t* _jx_cpu_getVersion(void);
static uint32_t _jx_cpu_readInfo(uint32_t id);

jx_cpu_api* cpu_api = &(jx_cpu_api) {
	.getVendorID = _jx_cpu_getVendorID,
	.getProcessorBrandString = _jx_cpu_getProcessorBrandString,
	.getFeatures = _jx_cpu_getFeatures,
	.getVersionInfo = _jx_cpu_getVersion
};

bool jx_cpu_initAPI(void)
{
	jx_cpu_info* info = &s_CPUInfo;

	// Check for CPUID support.
	// NOTE: This shouldn't be needed. I'm just bored and decided to go "by the book"
	// The Book: https://www.scss.tcd.ie/~jones/CS4021/processor-identification-cpuid-instruction-note.pdf
	{
		// Read EFLAGS register and toggle ID bit
		const uint64_t eflagsInitial = __readeflags();
		const uint64_t eflagsNew = eflagsInitial ^ (1ull << 21);

		// Write EFLAGS register
		__writeeflags(eflagsNew);

		// Check if write succeeded
		if (__readeflags() != eflagsNew) {
			// CPUID instruction not supported.
			return false;
		}

		// Reset EFLAGS register to initial value
		__writeeflags(eflagsInitial);
	}

	// Vendor ID
	{
		char* str = &info->m_VendorID[0];
		*(uint32_t*)&str[0] = _jx_cpu_readInfo(JCPUINFO_BASIC_VENDOR_ID_0);
		*(uint32_t*)&str[4] = _jx_cpu_readInfo(JCPUINFO_BASIC_VENDOR_ID_1);
		*(uint32_t*)&str[8] = _jx_cpu_readInfo(JCPUINFO_BASIC_VENDOR_ID_2);
		str[12] = '\0';
	}

	// Processor Brand String
	{
		char* str = &info->m_ProcessorBrandString[0];

		const uint32_t maxExtendedFuncID = _jx_cpu_readInfo(JCPUINFO_EXT_MAX_EXTENDED_FUNC_ID);
		if (maxExtendedFuncID >= 0x80000004) {
			// If the CPUID.80000000h.EAX is greater then or equal to 0x80000004 the 
			// Brand String feature is supported and software should use CPUID functions
			// 0x80000002 through 0x80000004 to identify the processor.
			*(uint32_t*)&str[0] = _jx_cpu_readInfo(JCPUINFO_EXT_PROCESSOR_BRAND_STRING_0);
			*(uint32_t*)&str[4] = _jx_cpu_readInfo(JCPUINFO_EXT_PROCESSOR_BRAND_STRING_1);
			*(uint32_t*)&str[8] = _jx_cpu_readInfo(JCPUINFO_EXT_PROCESSOR_BRAND_STRING_2);
			*(uint32_t*)&str[12] = _jx_cpu_readInfo(JCPUINFO_EXT_PROCESSOR_BRAND_STRING_3);
			*(uint32_t*)&str[16] = _jx_cpu_readInfo(JCPUINFO_EXT_PROCESSOR_BRAND_STRING_4);
			*(uint32_t*)&str[20] = _jx_cpu_readInfo(JCPUINFO_EXT_PROCESSOR_BRAND_STRING_5);
			*(uint32_t*)&str[24] = _jx_cpu_readInfo(JCPUINFO_EXT_PROCESSOR_BRAND_STRING_6);
			*(uint32_t*)&str[28] = _jx_cpu_readInfo(JCPUINFO_EXT_PROCESSOR_BRAND_STRING_7);
			*(uint32_t*)&str[32] = _jx_cpu_readInfo(JCPUINFO_EXT_PROCESSOR_BRAND_STRING_8);
			*(uint32_t*)&str[36] = _jx_cpu_readInfo(JCPUINFO_EXT_PROCESSOR_BRAND_STRING_9);
			*(uint32_t*)&str[40] = _jx_cpu_readInfo(JCPUINFO_EXT_PROCESSOR_BRAND_STRING_10);
			*(uint32_t*)&str[44] = _jx_cpu_readInfo(JCPUINFO_EXT_PROCESSOR_BRAND_STRING_11);
			str[48] = '\0';
		} else {
			// If the Brand String feature is not supported execute CPUID.1.EBX to get
			// the Brand ID. If the Brand ID is not zero the brand id feature is supported.
			const uint32_t brandID = _jx_cpu_readInfo(JCPUINFO_BASIC_BRAND_INDEX);
			if (brandID != 0) {
				// NOTE: Since Brand ID is going to be different between vendors, don't try
				// to decode it. Just write the Brand ID to the processor brand string.
				jx_snprintf(str, JCPUINFO_PROCESSOR_BRAND_STRING_MAX_LEN, "BrandID: %02X", brandID);
			} else {
				// If the Brand ID feature is not supported software should use the processor
				// signature (NOTE: version info) in conjuction with cache descriptors to 
				// identify the processor.
				const uint32_t processorSignature = _jx_cpu_readInfo(JCPUINFO_BASIC_PROCESSOR_SIGNATURE);
				jx_snprintf(str, JCPUINFO_PROCESSOR_BRAND_STRING_MAX_LEN, "Signature: %08X", processorSignature);
			}
		}
	}

	// Version
	{
		jx_cpu_version_t* ver = &info->m_Version;
		ver->m_SteppingID = (uint8_t)_jx_cpu_readInfo(JCPUINFO_BASIC_STEPPING_ID);

		const uint8_t familyID = (uint8_t)_jx_cpu_readInfo(JCPUINFO_BASIC_FAMILY_ID);
		if (familyID == 0x0F) {
			ver->m_FamilyID = familyID + (uint8_t)_jx_cpu_readInfo(JCPUINFO_BASIC_EXTENDED_FAMILY_ID);
		} else {
			ver->m_FamilyID = familyID;
		}

		ver->m_ModelID = (uint8_t)_jx_cpu_readInfo(JCPUINFO_BASIC_MODEL_ID);
		if (familyID == 0x06 || familyID == 0x0F) {
			ver->m_ModelID += (uint8_t)_jx_cpu_readInfo(JCPUINFO_BASIC_EXTENDED_MODEL_ID) << 4;
		}
	}

	// Features
	{
		uint64_t features = 0ull;
		features |= (_jx_cpu_readInfo(JCPUINFO_BASIC_MMX) != 0) ? JX_CPU_FEATURE_MMX : 0ull;
		features |= (_jx_cpu_readInfo(JCPUINFO_BASIC_SSE) != 0) ? JX_CPU_FEATURE_SSE : 0ull;
		features |= (_jx_cpu_readInfo(JCPUINFO_BASIC_SSE2) != 0) ? JX_CPU_FEATURE_SSE2 : 0ull;
		features |= (_jx_cpu_readInfo(JCPUINFO_BASIC_SSE3) != 0) ? JX_CPU_FEATURE_SSE3 : 0ull;
		features |= (_jx_cpu_readInfo(JCPUINFO_BASIC_SSSE3) != 0) ? JX_CPU_FEATURE_SSSE3 : 0ull;
		features |= (_jx_cpu_readInfo(JCPUINFO_BASIC_SSE4_1) != 0) ? JX_CPU_FEATURE_SSE4_1 : 0ull;
		features |= (_jx_cpu_readInfo(JCPUINFO_BASIC_SSE4_2) != 0) ? JX_CPU_FEATURE_SSE4_2 : 0ull;
		features |= (_jx_cpu_readInfo(JCPUINFO_BASIC_AVX) != 0) ? JX_CPU_FEATURE_AVX : 0ull;
		features |= (_jx_cpu_readInfo(JCPUINFO_BASIC_AVX2) != 0) ? JX_CPU_FEATURE_AVX2 : 0ull;
		features |= (_jx_cpu_readInfo(JCPUINFO_BASIC_AVX512F) != 0) ? JX_CPU_FEATURE_AVX512F : 0ull;
		features |= (_jx_cpu_readInfo(JCPUINFO_BASIC_BMI1) != 0) ? JX_CPU_FEATURE_BMI1 : 0ull;
		features |= (_jx_cpu_readInfo(JCPUINFO_BASIC_BMI2) != 0) ? JX_CPU_FEATURE_BMI2 : 0ull;
		features |= (_jx_cpu_readInfo(JCPUINFO_BASIC_POPCNT) != 0) ? JX_CPU_FEATURE_POPCNT : 0ull;
		features |= (_jx_cpu_readInfo(JCPUINFO_BASIC_F16C) != 0) ? JX_CPU_FEATURE_F16C : 0ull;
		features |= (_jx_cpu_readInfo(JCPUINFO_BASIC_FMA) != 0) ? JX_CPU_FEATURE_FMA : 0ull;
		features |= (_jx_cpu_readInfo(JCPUINFO_BASIC_ENHANCED_REPMOVSB) != 0) ? JX_CPU_FEATURE_ERMSB : 0ull;
		info->m_Features = features;
	}

	return true;
}

void jx_cpu_logInfo(jx_logger_i* logger)
{
	jx_cpu_info* info = &s_CPUInfo;
	JX_LOG_DEBUG(logger, "cpu", "Vendor ID : % s\n", info->m_VendorID);
	JX_LOG_DEBUG(logger, "cpu", "Processor: %s\n", info->m_ProcessorBrandString);
	JX_LOG_DEBUG(logger, "cpu", "Family: %02Xh, Model: %02Xh, Stepping: %u\n", info->m_Version.m_FamilyID, info->m_Version.m_ModelID, info->m_Version.m_SteppingID);
	JX_LOG_DEBUG(logger, "cpu", "Features : %I64Xh\n", info->m_Features);

	char featureStr[256] = { 0 };
	bool appendSeparator = false;
	for (uint32_t i = 0; i < JX_CPU_NUM_FEATURES; ++i) {
		if ((info->m_Features & (1ull << i)) != 0) {
			if (appendSeparator) {
				jx_strncat(featureStr, JX_COUNTOF(featureStr), ", ", 2);
			}
			jx_strncat(featureStr, JX_COUNTOF(featureStr), jx_cpu_kFeatureName[i], UINT32_MAX);
			appendSeparator = true;
		}
	}
	JX_LOG_DEBUG(logger, "cpu", "- %s\n", featureStr);
}

static const char* _jx_cpu_getVendorID(void)
{
	return &s_CPUInfo.m_VendorID[0];
}

static const char* _jx_cpu_getProcessorBrandString(void)
{
	return &s_CPUInfo.m_ProcessorBrandString[0];
}

static uint64_t _jx_cpu_getFeatures(void)
{
	return s_CPUInfo.m_Features;
}

static const jx_cpu_version_t* _jx_cpu_getVersion(void)
{
	return &s_CPUInfo.m_Version;
}

static uint32_t _jx_cpu_readInfo(uint32_t id)
{
	const uint32_t cpuidEAX = (id >> 0) & 0xFF;
	const uint32_t cpuidECX = (id >> 8) & 0xFF;
	const uint32_t cpuidResID = (id >> 16) & 0x03;
	const uint32_t firstBit = (id >> 18) & 0x1F;
	const uint32_t numBits = (id >> 23) & 0x3F;
	const uint32_t type = (id >> 30) & 0x01;

	int32_t info[4];
	__cpuidex(&info[0], (type == JCPUINFO_BASIC ? 0 : 0x80000000) + cpuidEAX, cpuidECX);

	return (((uint32_t)info[cpuidResID]) >> firstBit) & (uint32_t)((1ull << numBits) - 1);
}
