#include <stdio.h>
#include <setjmp.h>
#include <stdbool.h>
#include <assert.h>

#include <sys/types.h>
#include <ucontext.h>

//***************************************************************************
// local include files 
//***************************************************************************
#include "mcontext.h"

//****************************************************************************
// forward declarations 
//****************************************************************************

//****************************************************************************
// interface functions
//****************************************************************************

void* getContextPC(void* uCtxt) {
  mcontext_t *mc = GET_MCONTEXT(uCtxt);
  return MCONTEXT_PC(mc);
}

void* getContextSP(void* uCtxt) {
  mcontext_t *mc = GET_MCONTEXT(uCtxt);
  return MCONTEXT_SP(mc);
}

void* getContextFL(void* uCtxt) {
    mcontext_t *mc = GET_MCONTEXT(uCtxt);
    return MCONTEXT_FL(mc);
}

void* getContextRCX(void* uCtxt) {
    mcontext_t *mc = GET_MCONTEXT(uCtxt);
    return MCONTEXT_RCX(mc);
}

void* getContextR10(void* uCtxt) {
    mcontext_t *mc = GET_MCONTEXT(uCtxt);
    return MCONTEXT_R10(mc);
}

void* getContextRBX(void* uCtxt) {
    mcontext_t *mc = GET_MCONTEXT(uCtxt);
    return MCONTEXT_RBX(mc);
}

void* getContextRAX(void* uCtxt) {
    mcontext_t *mc = GET_MCONTEXT(uCtxt);
    return MCONTEXT_RAX(mc);
}

void* getContextRDX(void* uCtxt) {
    mcontext_t *mc = GET_MCONTEXT(uCtxt);
    return MCONTEXT_RDX(mc);
}

void* getContextR8(void* uCtxt) {
    mcontext_t *mc = GET_MCONTEXT(uCtxt);
    return MCONTEXT_R8(mc);
}

void* getContextR9(void* uCtxt) {
    mcontext_t *mc = GET_MCONTEXT(uCtxt);
    return MCONTEXT_R9(mc);
}

void* getContextR11(void* uCtxt) {
    mcontext_t *mc = GET_MCONTEXT(uCtxt);
    return MCONTEXT_R11(mc);
}

void* getContextR12(void* uCtxt) {
    mcontext_t *mc = GET_MCONTEXT(uCtxt);
    return MCONTEXT_R12(mc);
}

void* getContextR13(void* uCtxt) {
    mcontext_t *mc = GET_MCONTEXT(uCtxt);
    return MCONTEXT_R13(mc);
}

void* getContextR14(void* uCtxt) {
    mcontext_t *mc = GET_MCONTEXT(uCtxt);
    return MCONTEXT_R14(mc);
}

void* getContextR15(void* uCtxt) {
    mcontext_t *mc = GET_MCONTEXT(uCtxt);
    return MCONTEXT_R15(mc);
}

void* getContextRSI(void* uCtxt) {
    mcontext_t *mc = GET_MCONTEXT(uCtxt);
    return MCONTEXT_RSI(mc);
}

void* getContextRDI(void* uCtxt) {
    mcontext_t *mc = GET_MCONTEXT(uCtxt);
    return MCONTEXT_RBP(mc);
}