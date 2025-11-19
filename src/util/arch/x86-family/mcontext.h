#ifndef MCONTEXT_H
#define MCONTEXT_H

#include <ucontext.h>
#include <sys/ucontext.h>

#define GET_MCONTEXT(uCtxt) (&((ucontext_t *)uCtxt)->uc_mcontext)

//-------------------------------------------------------------------------
// define macros for extracting pc, bp, and sp from machine contexts. these
// macros bridge differences between machine context representations for
// various architectures
//-------------------------------------------------------------------------

#ifdef REG_RIP
#define REG_INST_PTR REG_RIP
#define REG_BASE_PTR REG_RBP
#define REG_STACK_PTR REG_RSP

#else
#ifdef REG_EIP
#define REG_INST_PTR REG_EIP
#define REG_BASE_PTR REG_EBP
#define REG_STACK_PTR REG_ESP

#else 
#ifdef REG_IP
#define REG_INST_PTR REG_IP
#define REG_BASE_PTR REG_BP
#define REG_STACK_PTR REG_SP
#endif
#endif
#endif


#define MCONTEXT_REG(mctxt, reg) ((mctxt)->gregs[reg])

#define LV_MCONTEXT_PC(mctxt)         MCONTEXT_REG(mctxt, REG_INST_PTR)
#define LV_MCONTEXT_BP(mctxt)         MCONTEXT_REG(mctxt, REG_BASE_PTR)
#define LV_MCONTEXT_SP(mctxt)         MCONTEXT_REG(mctxt, REG_STACK_PTR)

#define MCONTEXT_PC(mctxt) ((void *)  MCONTEXT_REG(mctxt, REG_INST_PTR))
#define MCONTEXT_BP(mctxt) ((void **) MCONTEXT_REG(mctxt, REG_BASE_PTR))
#define MCONTEXT_SP(mctxt) ((void **) MCONTEXT_REG(mctxt, REG_STACK_PTR))
#define MCONTEXT_FL(mctxt) ((void *) MCONTEXT_REG(mctxt, REG_EFL))
#define MCONTEXT_RCX(mctxt) ((void *) MCONTEXT_REG(mctxt, REG_RCX))
#define MCONTEXT_RBX(mctxt) ((void *) MCONTEXT_REG(mctxt, REG_RBX))
#define MCONTEXT_RAX(mctxt) ((void *) MCONTEXT_REG(mctxt, REG_RAX))
#define MCONTEXT_RDX(mctxt) ((void *) MCONTEXT_REG(mctxt, REG_RDX))
#define MCONTEXT_R8(mctxt) ((void *) MCONTEXT_REG(mctxt, REG_R8))
#define MCONTEXT_R9(mctxt) ((void *) MCONTEXT_REG(mctxt, REG_R9))
#define MCONTEXT_R10(mctxt) ((void *) MCONTEXT_REG(mctxt, REG_R10))
#define MCONTEXT_R11(mctxt) ((void *) MCONTEXT_REG(mctxt, REG_R11))
#define MCONTEXT_R12(mctxt) ((void *) MCONTEXT_REG(mctxt, REG_R12))
#define MCONTEXT_R13(mctxt) ((void *) MCONTEXT_REG(mctxt, REG_R13))
#define MCONTEXT_R14(mctxt) ((void *) MCONTEXT_REG(mctxt, REG_R14))
#define MCONTEXT_R15(mctxt) ((void *) MCONTEXT_REG(mctxt, REG_R15))
#define MCONTEXT_RSI(mctxt) ((void *) MCONTEXT_REG(mctxt, REG_RSI))
#define MCONTEXT_RDI(mctxt) ((void *) MCONTEXT_REG(mctxt, REG_RDI))
#define MCONTEXT_RBP(mctxt) ((void *) MCONTEXT_REG(mctxt, REG_RBP))

#endif // MCONTEXT_H
