/* $Id$ */
/** @file
 * IEM - Instruction Decoding and Emulation.
 */

/*
 * Copyright (C) 2011-2023 Oracle and/or its affiliates.
 *
 * This file is part of VirtualBox base platform packages, as
 * available from https://www.virtualbox.org.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation, in version 3 of the
 * License.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <https://www.gnu.org/licenses>.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#ifndef LOG_GROUP /* defined when included by tstIEMCheckMc.cpp */
# define LOG_GROUP LOG_GROUP_IEM
#endif
#define VMCPU_INCL_CPUM_GST_CTX
#include <VBox/vmm/iem.h>
#include <VBox/vmm/cpum.h>
#include <VBox/vmm/apic.h>
#include <VBox/vmm/pdm.h>
#include <VBox/vmm/pgm.h>
#include <VBox/vmm/iom.h>
#include <VBox/vmm/em.h>
#include <VBox/vmm/hm.h>
#include <VBox/vmm/nem.h>
#include <VBox/vmm/gim.h>
#ifdef VBOX_WITH_NESTED_HWVIRT_SVM
# include <VBox/vmm/em.h>
# include <VBox/vmm/hm_svm.h>
#endif
#ifdef VBOX_WITH_NESTED_HWVIRT_VMX
# include <VBox/vmm/hmvmxinline.h>
#endif
#include <VBox/vmm/tm.h>
#include <VBox/vmm/dbgf.h>
#include <VBox/vmm/dbgftrace.h>
#ifndef TST_IEM_CHECK_MC
# include "IEMInternal.h"
#endif
#include <VBox/vmm/vmcc.h>
#include <VBox/log.h>
#include <VBox/err.h>
#include <VBox/param.h>
#include <VBox/dis.h>
#include <VBox/disopcode-x86-amd64.h>
#include <iprt/asm-math.h>
#include <iprt/assert.h>
#include <iprt/string.h>
#include <iprt/x86.h>

#ifndef TST_IEM_CHECK_MC
# include "IEMInline.h"
# include "IEMOpHlp.h"
# include "IEMMc.h"
#endif

#include "IEMThreadedFunctions.h"


/*
 * Narrow down configs here to avoid wasting time on unused configs here.
 */

#ifndef IEM_WITH_CODE_TLB
# error The code TLB must be enabled for the recompiler.
#endif

#ifndef IEM_WITH_DATA_TLB
# error The data TLB must be enabled for the recompiler.
#endif

#ifndef IEM_WITH_SETJMP
# error The setjmp approach must be enabled for the recompiler.
#endif


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * A call for the threaded call table.
 */
typedef struct IEMTHRDEDCALLENTRY
{
    /** The function to call (IEMTHREADEDFUNCS). */
    uint16_t    enmFunction;
    uint16_t    uUnused0;

    /** The opcode length. */
    uint8_t     cbOpcode;
    /** The opcode chunk number.
     * @note sketches for discontiguous opcode support  */
    uint8_t     idxOpcodeChunk;
    /** The offset into the opcode chunk of this function.
     * @note sketches for discontiguous opcode support  */
    uint16_t    offOpcodeChunk;

    /** Generic parameters. */
    uint64_t    auParams[3];
} IEMTHRDEDCALLENTRY;
AssertCompileSize(IEMTHRDEDCALLENTRY, sizeof(uint64_t) * 4);
/** Pointer to a threaded call entry. */
typedef IEMTHRDEDCALLENTRY       *PIEMTHRDEDCALLENTRY;
/** Pointer to a const threaded call entry. */
typedef IEMTHRDEDCALLENTRY const *PCIEMTHRDEDCALLENTRY;

/** @name IEM_F_XXX - Execution mode flags (IEMCPU::fExec, IEMTB::fFlags).
 *
 * These flags are set when entering IEM and adjusted as code is executed, such
 * that they will always contain the current values as instructions are
 * finished.
 *
 * In recompiled execution mode, (most of) these flags are included in the
 * translation block selection key and stored in IEMTB::fFlags alongside the
 * IEMTB_F_XXX flags.  The latter flags uses bits 31 thru 24, which are all zero
 * in IEMCPU::fExec.
 *
 * @{ */
/** Mode: The block target mode mask. */
#define IEM_F_MODE_MASK                     UINT32_C(0x0000000f)
/** Mode: The IEMMODE part of the IEMTB_F_MODE_MASK value. */
#define IEM_F_MODE_CPUMODE_MASK             UINT32_C(0x00000003)
/** X86 Mode: Bit used to indicating pre-386 CPU in 16-bit mode (for eliminating
 * conditional in EIP/IP updating), and flat wide open CS, SS DS, and ES in
 * 32-bit mode (for simplifying most memory accesses). */
#define IEM_F_MODE_X86_FLAT_OR_PRE_386_MASK UINT32_C(0x00000004)
/** X86 Mode: Bit indicating protected mode. */
#define IEM_F_MODE_X86_PROT_MASK            UINT32_C(0x00000008)
/** @todo mix in paging as well? it's not all that important compared to
 *        protected mode. */

/** X86 Mode: 16-bit on 386 or later. */
#define IEM_F_MODE_X86_16BIT                UINT32_C(0x00000000)
/** X86 Mode: 80286, 80186 and 8086/88 targetting blocks (EIP update opt). */
#define IEM_F_MODE_X86_16BIT_PRE_386        UINT32_C(0x00000004)
/** X86 Mode: 16-bit protected mode on 386 or later. */
#define IEM_F_MODE_X86_16BIT_PROT           UINT32_C(0x00000008)
/** X86 Mode: 16-bit protected mode on 386 or later. */
#define IEM_F_MODE_X86_16BIT_PROT_PRE_386   UINT32_C(0x0000000c)

/** X86 Mode: 32-bit on 386 or later. */
#define IEM_F_MODE_X86_32BIT                UINT32_C(0x00000001)
/** X86 Mode: 32-bit mode with wide open flat CS, SS, DS and ES. */
#define IEM_F_MODE_X86_32BIT_FLAT           UINT32_C(0x00000005)
/** X86 Mode: 32-bit protected mode. */
#define IEM_F_MODE_X86_32BIT_PROT           UINT32_C(0x00000009)
/** X86 Mode: 32-bit protected mode with wide open flat CS, SS, DS and ES. */
#define IEM_F_MODE_X86_32BIT_PROT_FLAT      UINT32_C(0x0000000d)

/** X86 Mode: 64-bit (includes protected, but not the flat bit). */
#define IEM_F_MODE_X86_64BIT                UINT32_C(0x0000000a)


/** Bypass access handlers when set. */
#define IEM_F_BYPASS_HANDLERS               UINT32_C(0x00000010)
/** Have pending hardware instruction breakpoints.   */
#define IEM_F_PENDING_BRK_INSTR             UINT32_C(0x00000020)
/** Have pending hardware data breakpoints.   */
#define IEM_F_PENDING_BRK_DATA              UINT32_C(0x00000040)

/** X86: Have pending hardware I/O breakpoints. */
#define IEM_F_PENDING_BRK_X86_IO            UINT32_C(0x00000400)
/** X86: Disregard the lock prefix (implied or not) when set. */
#define IEM_F_X86_DISREGARD_LOCK            UINT32_C(0x00000800)

/** The CPL. */
#define IEM_F_X86_CPL_MASK                  UINT32_C(0x00000300)

/** X86 execution context.
 * The IEM_F_X86_CTX_XXX values are individual flags that can be combined (with
 * the exception of IEM_F_X86_CTX_NORMAL).  This allows running VMs from SMM
 * mode. */
#define IEM_F_X86_CTX_MASK                  UINT32_C(0x0000f000)
/** X86 context: Plain regular execution context. */
#define IEM_F_X86_CTX_NORMAL                UINT32_C(0x00000000)
/** X86 context: VT-x enabled. */
#define IEM_F_X86_CTX_VTX                   UINT32_C(0x00001000)
/** X86 context: AMD-V enabled. */
#define IEM_F_X86_CTX_SVM                   UINT32_C(0x00002000)
/** X86 context: In AMD-V or VT-x guest mode. */
#define IEM_F_X86_CTX_IN_GUEST              UINT32_C(0x00004000)
/** X86 context: System management mode (SMM). */
#define IEM_F_X86_CTX_SMM                   UINT32_C(0x00008000)

/** @} */


/** @name IEMTB_F_XXX - Translation block flags (IEMTB::fFlags).
 *
 * Extends the IEM_F_XXX flags (subject to IEMTB_F_IEM_F_MASK) to make up the
 * translation block flags.  The combined flag mask (subject to
 * IEMTB_F_KEY_MASK) is used as part of the lookup key for translation blocks.
 *
 * @{ */
/** Mask of IEM_F_XXX flags included in IEMTB_F_XXX. */
#define IEMTB_F_IEM_F_MASK              UINT32_C(0x00ffffff)

/** Type: The block type mask. */
#define IEMTB_F_TYPE_MASK               UINT32_C(0x03000000)
/** Type: Purly threaded recompiler (via tables). */
#define IEMTB_F_TYPE_THREADED           UINT32_C(0x01000000)
/** Type: Native recompilation.  */
#define IEMTB_F_TYPE_NATIVE             UINT32_C(0x02000000)

/** Mask of the IEMTB_F_XXX flags that are part of the TB lookup key.
 * @note We don't   */
#define IEMTB_F_KEY_MASK                ((UINT32_C(0xffffffff) & ~IEM_F_X86_CTX_MASK) | IEM_F_X86_CTX_SMM)
/** @} */

AssertCompile( (IEM_F_MODE_X86_16BIT              & IEM_F_MODE_CPUMODE_MASK) == IEMMODE_16BIT);
AssertCompile(!(IEM_F_MODE_X86_16BIT              & IEM_F_MODE_X86_FLAT_OR_PRE_386_MASK));
AssertCompile(!(IEM_F_MODE_X86_16BIT              & IEM_F_MODE_X86_PROT_MASK));
AssertCompile( (IEM_F_MODE_X86_16BIT_PRE_386      & IEM_F_MODE_CPUMODE_MASK) == IEMMODE_16BIT);
AssertCompile(  IEM_F_MODE_X86_16BIT_PRE_386      & IEM_F_MODE_X86_FLAT_OR_PRE_386_MASK);
AssertCompile(!(IEM_F_MODE_X86_16BIT_PRE_386      & IEM_F_MODE_X86_PROT_MASK));
AssertCompile( (IEM_F_MODE_X86_16BIT_PROT         & IEM_F_MODE_CPUMODE_MASK) == IEMMODE_16BIT);
AssertCompile(!(IEM_F_MODE_X86_16BIT_PROT         & IEM_F_MODE_X86_FLAT_OR_PRE_386_MASK));
AssertCompile(  IEM_F_MODE_X86_16BIT_PROT         & IEM_F_MODE_X86_PROT_MASK);
AssertCompile( (IEM_F_MODE_X86_16BIT_PROT_PRE_386 & IEM_F_MODE_CPUMODE_MASK) == IEMMODE_16BIT);
AssertCompile(  IEM_F_MODE_X86_16BIT_PROT_PRE_386 & IEM_F_MODE_X86_FLAT_OR_PRE_386_MASK);
AssertCompile(  IEM_F_MODE_X86_16BIT_PROT_PRE_386 & IEM_F_MODE_X86_PROT_MASK);

AssertCompile( (IEM_F_MODE_X86_32BIT              & IEM_F_MODE_CPUMODE_MASK) == IEMMODE_32BIT);
AssertCompile(!(IEM_F_MODE_X86_32BIT              & IEM_F_MODE_X86_FLAT_OR_PRE_386_MASK));
AssertCompile(!(IEM_F_MODE_X86_32BIT              & IEM_F_MODE_X86_PROT_MASK));
AssertCompile( (IEM_F_MODE_X86_32BIT_FLAT         & IEM_F_MODE_CPUMODE_MASK) == IEMMODE_32BIT);
AssertCompile(  IEM_F_MODE_X86_32BIT_FLAT         & IEM_F_MODE_X86_FLAT_OR_PRE_386_MASK);
AssertCompile(!(IEM_F_MODE_X86_32BIT_FLAT         & IEM_F_MODE_X86_PROT_MASK));
AssertCompile( (IEM_F_MODE_X86_32BIT_PROT         & IEM_F_MODE_CPUMODE_MASK) == IEMMODE_32BIT);
AssertCompile(!(IEM_F_MODE_X86_32BIT_PROT         & IEM_F_MODE_X86_FLAT_OR_PRE_386_MASK));
AssertCompile(  IEM_F_MODE_X86_32BIT_PROT         & IEM_F_MODE_X86_PROT_MASK);
AssertCompile( (IEM_F_MODE_X86_32BIT_PROT_FLAT    & IEM_F_MODE_CPUMODE_MASK) == IEMMODE_32BIT);
AssertCompile(  IEM_F_MODE_X86_32BIT_PROT_FLAT    & IEM_F_MODE_X86_FLAT_OR_PRE_386_MASK);
AssertCompile(  IEM_F_MODE_X86_32BIT_PROT_FLAT    & IEM_F_MODE_X86_PROT_MASK);

AssertCompile( (IEM_F_MODE_X86_64BIT              & IEM_F_MODE_CPUMODE_MASK) == IEMMODE_64BIT);
AssertCompile(  IEM_F_MODE_X86_64BIT              & IEM_F_MODE_X86_PROT_MASK);
AssertCompile(!(IEM_F_MODE_X86_64BIT              & IEM_F_MODE_X86_FLAT_OR_PRE_386_MASK));


/**
 * Translation block.
 */
typedef struct IEMTB
{
    /** Next block with the same hash table entry. */
    PIEMTB volatile     pNext;
    /** List on the local VCPU for blocks. */
    RTLISTNODE          LocalList;

    /** @name What uniquely identifies the block.
     * @{ */
    RTGCPHYS            GCPhysPc;
    uint64_t            uPc;
    uint32_t            fFlags;
    union
    {
        struct
        {
            /** The CS base. */
            uint32_t uCsBase;
            /** The CS limit (UINT32_MAX for 64-bit code). */
            uint32_t uCsLimit;
            /** The CS selector value. */
            uint16_t CS;
            /**< Relevant X86DESCATTR_XXX bits. */
            uint16_t fAttr;
        } x86;
    };
    /** @} */

    /** Number of bytes of opcodes covered by this block.
     * @todo Support discontiguous chunks of opcodes in same block, though maybe
     *       restrict to the initial page or smth. */
    uint32_t    cbPC;

    union
    {
        struct
        {
            /** Number of calls in paCalls. */
            uint32_t            cCalls;
            /** Number of calls allocated. */
            uint32_t            cAllocated;
            /** The call sequence table. */
            PIEMTHRDEDCALLENTRY paCalls;
        } Thrd;
    };


} IEMTB;


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#define g_apfnOneByteMap    g_apfnIemThreadedRecompilerOneByteMap


#undef IEM_MC_CALC_RM_EFF_ADDR
#ifndef IEM_WITH_SETJMP
# define IEM_MC_CALC_RM_EFF_ADDR(a_GCPtrEff, bRm, cbImm) \
    uint64_t uEffAddrInfo; \
    IEM_MC_RETURN_ON_FAILURE(iemOpHlpCalcRmEffAddrJmpEx(pVCpu, (bRm), (cbImm), &(a_GCPtrEff), &uEffAddrInfo))
#else
# define IEM_MC_CALC_RM_EFF_ADDR(a_GCPtrEff, bRm, cbImm) \
    uint64_t uEffAddrInfo; \
    ((a_GCPtrEff) = iemOpHlpCalcRmEffAddrJmpEx(pVCpu, (bRm), (cbImm), &uEffAddrInfo))
#endif

#define IEM_MC2_EMIT_CALL_1(a_enmFunction, a_uArg0) do { \
        IEMTHREADEDFUNCS const enmFunctionCheck = a_enmFunction; RT_NOREF(enmFunctionCheck); \
        uint64_t         const uArg0Check       = (a_uArg0);     RT_NOREF(uArg0Check); \
        \
        PIEMTB              const pTb   = pVCpu->iem.s.pCurTbR3; \
        PIEMTHRDEDCALLENTRY const pCall = &pTb->Thrd.paCalls[pTb->Thrd.cCalls++]; \
        pCall->enmFunction = a_enmFunction; \
        pCall->cbOpcode    = IEM_GET_INSTR_LEN(pVCpu); \
        pCall->auParams[0] = a_uArg0; \
        pCall->auParams[1] = 0; \
        pCall->auParams[2] = 0; \
    } while (0)
#define IEM_MC2_EMIT_CALL_2(a_enmFunction, a_uArg0, a_uArg1) do { \
        IEMTHREADEDFUNCS const enmFunctionCheck = a_enmFunction; RT_NOREF(enmFunctionCheck); \
        uint64_t         const uArg0Check       = (a_uArg0);     RT_NOREF(uArg0Check); \
        uint64_t         const uArg1Check       = (a_uArg1);     RT_NOREF(uArg1Check); \
        \
        PIEMTB              const pTb   = pVCpu->iem.s.pCurTbR3; \
        PIEMTHRDEDCALLENTRY const pCall = &pTb->Thrd.paCalls[pTb->Thrd.cCalls++]; \
        pCall->enmFunction = a_enmFunction; \
        pCall->cbOpcode    = IEM_GET_INSTR_LEN(pVCpu); \
        pCall->auParams[0] = a_uArg0; \
        pCall->auParams[1] = a_uArg1; \
        pCall->auParams[2] = 0; \
    } while (0)
#define IEM_MC2_EMIT_CALL_3(a_enmFunction, a_uArg0, a_uArg1, a_uArg2) do { \
        IEMTHREADEDFUNCS const enmFunctionCheck = a_enmFunction; RT_NOREF(enmFunctionCheck); \
        uint64_t         const uArg0Check       = (a_uArg0);     RT_NOREF(uArg0Check); \
        uint64_t         const uArg1Check       = (a_uArg1);     RT_NOREF(uArg1Check); \
        uint64_t         const uArg2Check       = (a_uArg2);     RT_NOREF(uArg2Check); \
        \
        PIEMTB              const pTb   = pVCpu->iem.s.pCurTbR3; \
        PIEMTHRDEDCALLENTRY const pCall = &pTb->Thrd.paCalls[pTb->Thrd.cCalls++]; \
        pCall->enmFunction = a_enmFunction; \
        pCall->cbOpcode    = IEM_GET_INSTR_LEN(pVCpu); \
        pCall->auParams[0] = a_uArg0; \
        pCall->auParams[1] = a_uArg1; \
        pCall->auParams[2] = a_uArg2; \
    } while (0)


/*
 * IEM_MC_DEFER_TO_CIMPL_0 is easily wrapped up.
 *
 * Doing so will also take care of IEMOP_RAISE_DIVIDE_ERROR, IEMOP_RAISE_INVALID_LOCK_PREFIX,
 * IEMOP_RAISE_INVALID_OPCODE and their users.
 */
#undef IEM_MC_DEFER_TO_CIMPL_0
#define IEM_MC_DEFER_TO_CIMPL_0(a_pfnCImpl) iemThreadedRecompilerMcDeferToCImpl0(pVCpu, a_pfnCImpl)

typedef IEM_CIMPL_DECL_TYPE_0(FNIEMCIMPL0);
typedef FNIEMCIMPL0 *PFNIEMCIMPL0;

DECLINLINE(VBOXSTRICTRC) iemThreadedRecompilerMcDeferToCImpl0(PVMCPUCC pVCpu, PFNIEMCIMPL0 pfnCImpl)
{
    return pfnCImpl(pVCpu, IEM_GET_INSTR_LEN(pVCpu));
}

/** @todo deal with IEM_MC_DEFER_TO_CIMPL_1, IEM_MC_DEFER_TO_CIMPL_2 and
 *        IEM_MC_DEFER_TO_CIMPL_3 as well. */

/*
 * Include the "annotated" IEMAllInstructions*.cpp.h files.
 */
#include "IEMThreadedInstructions.cpp.h"



/*
 * Real code.
 */

static VBOXSTRICTRC iemThreadedCompile(PVMCCV pVM, PVMCPUCC pVCpu)
{
    RT_NOREF(pVM, pVCpu, pTb);
    return VERR_NOT_IMPLEMENTED;
}


static VBOXSTRICTRC iemThreadedCompileLongJumped(PVMCCV pVM, PVMCPUCC pVCpu, VBOXSTRICTRC rcStrict)
{
    RT_NOREF(pVM, pVCpu);
    return rcStrict;
}


static PIEMTB iemThreadedTbLookup(PVMCCV pVM, PVMCPUCC pVCpu, RTGCPHYS GCPhysPC, uint64_t uPc)
{
    RT_NOREF(pVM, pVCpu, GCPhysPC, uPc);
    return NULL;
}


static VBOXSTRICTRC iemThreadedTbExec(PVMCCV pVM, PVMCPUCC pVCpu, PIEMTB pTb)
{
    RT_NOREF(pVM, pVCpu, pTb);
    return VERR_NOT_IMPLEMENTED;
}


/**
 * This is called when the PC doesn't match the current pbInstrBuf.
 */
static uint64_t iemGetPcWithPhysAndCodeMissed(PVMCPUCC pVCpu, uint64_t const uPc, PRTGCPHYS pPhys)
{
    /** @todo see iemOpcodeFetchBytesJmp */
    pVCpu->iem.s.pbInstrBuf = NULL;

    pVCpu->iem.s.offInstrNextByte = 0;
    pVCpu->iem.s.offCurInstrStart = 0;
    pVCpu->iem.s.cbInstrBuf       = 0;
    pVCpu->iem.s.cbInstrBufTotal  = 0;

}


/** @todo need private inline decl for throw/nothrow matching IEM_WITH_SETJMP? */
DECL_INLINE_THROW(uint64_t) iemGetPcWithPhysAndCode(PVMCPUCC pVCpu, PRTGCPHYS pPhys)
{
    Assert(pVCpu->cpum.GstCtx.cs.u64Base == 0 || pVCpu->iem.s.enmCpuMode != IEMMODE_64BIT);
    uint64_t const uPc = pVCpu->cpum.GstCtx.rip + pVCpu->cpum.GstCtx.cs.u64Base;
    if (pVCpu->iem.s.pbInstrBuf)
    {
        uint64_t off = uPc - pVCpu->iem.s.uInstrBufPc;
        if (off < pVCpu->iem.s.cbInstrBufTotal)
        {
            pVCpu->iem.s.offInstrNextByte = (uint32_t)off;
            pVCpu->iem.s.offCurInstrStart = (uint16_t)off;
            if ((uint16_t)off + 15 <= pVCpu->iem.s.cbInstrBufTotal)
                pVCpu->iem.s.cbInstrBuf = (uint16_t)off + 15;
            else
                pVCpu->iem.s.cbInstrBuf = pVCpu->iem.s.cbInstrBufTotal;

            *pPhys = pVCpu->iem.s.GCPhysInstrBuf + off;
            return uPc;
        }
    }
    return iemGetPcWithPhysAndCodeMissed(pVCpu, uPc, pPhys);
}


VMMDECL(VBOXSTRICTRC) IEMExecRecompilerThreaded(PVMCC pVM, PVMCPUCC pVCpu)
{
    /*
     * Init the execution environment.
     */
    iemInitExec(pVCpu, false);
    ...

    /*
     * Run-loop.
     *
     * If we're using setjmp/longjmp we combine all the catching here to avoid
     * having to call setjmp for each block we're executing.
     */
    for (;;)
    {
        PIEMTB       pTb = NULL;
        VBOXSTRICTRC rcStrict;
#ifdef IEM_WITH_SETJMP
        IEM_TRY_SETJMP(pVCpu, rcStrict)
#endif
        {
            for (;;)
            {
                /* Translate PC to physical address, we'll need this for both lookup and compilation. */
                RTGCPHYS       GCPhysPC;
                uint64_t const uPC = iemGetPcWithPhysAndCode(pVCpu, &GCPhysPC);

                pTb = iemThreadedTbLookup(pVM, pVCpu, GCPhysPC, uPc);
                if (pTb)
                    rcStrict = iemThreadedTbExec(pVM, pVCpu, pTb);
                else
                    rcStrict = iemThreadedCompile(pVM, pVCpu, GCPhysPC, uPc);
                if (rcStrict == VINF_SUCCESS)
                { /* likely */ }
                else
                    return rcStrict;
            }
        }
#ifdef IEM_WITH_SETJMP
        IEM_CATCH_LONGJMP_BEGIN(pVCpu, rcStrict);
        {
            pVCpu->iem.s.cLongJumps++;
            if (pTb)
                return rcStrict;
            return iemThreadedCompileLongJumped(pVM, pVCpu, rcStrict);
        }
        IEM_CATCH_LONGJMP_END(pVCpu);
#endif
    }
}

