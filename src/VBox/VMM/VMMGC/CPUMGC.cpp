/** @file
 *
 * CPUM - Guest Context Code.
 */

/*
 * Copyright (C) 2006 InnoTek Systemberatung GmbH
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software Foundation,
 * in version 2 as it comes in the "COPYING" file of the VirtualBox OSE
 * distribution. VirtualBox OSE is distributed in the hope that it will
 * be useful, but WITHOUT ANY WARRANTY of any kind.
 *
 * If you received this file as part of a commercial VirtualBox
 * distribution, then only the terms of your commercial VirtualBox
 * license agreement apply instead of the previous paragraph.
 */


/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#define LOG_GROUP LOG_GROUP_CPUM
#include <VBox/cpum.h>
#include <VBox/vmm.h>
#include <VBox/trpm.h>
#include "CPUMInternal.h"
#include <VBox/vm.h>
#include <VBox/err.h>
#include <iprt/assert.h>
#include <VBox/log.h>


/*******************************************************************************
*   Internal Functions                                                         *
*******************************************************************************/
__BEGIN_DECLS /* addressed from asm (not called so no DECLASM). */
DECLCALLBACK(int) cpumGCHandleNPAndGP(PVM pVM, PCPUMCTXCORE pRegFrame, uintptr_t uUser);
__END_DECLS


/**
 * Deal with traps occuring during segment loading and IRET
 * when resuming guest context.
 *
 * @returns VBox status code.
 * @param   pVM         The VM handle.
 * @param   pRegFrame   The register frame.
 * @param   uUser       User argument. In this case a combination of the
 *                      CPUM_HANDLER_* \#defines.
 */
DECLCALLBACK(int) cpumGCHandleNPAndGP(PVM pVM, PCPUMCTXCORE pRegFrame, uintptr_t uUser)
{
    LogFlow(("cpumGCHandleNPAndGP: eip=%RX32 uUser=%#x\n", pRegFrame->eip, uUser));

    /*
     * Update the guest cpu state.
     */
    if (uUser & CPUM_HANDLER_CTXCORE_IN_EBP)
    {
        PCPUMCTXCORE  pGstCtxCore = CPUMCTX2CORE(&pVM->cpum.s.Guest);
        PCCPUMCTXCORE pGstCtxCoreSrc = (PCPUMCTXCORE)pRegFrame->ebp;
        *pGstCtxCore = *pGstCtxCoreSrc;
    }

    /*
     * Take action based on what's happended.
     */
    switch (uUser & CPUM_HANDLER_TYPEMASK)
    {
        case CPUM_HANDLER_GS:
        //    if (!pVM->cpum.s.Guest.ldtr)
        //    {
        //        pRegFrame->gs = 0;
        //        pRegFrame->eip += 6; /* mov gs, [edx + CPUM.Guest.gs] */
        //        return VINF_SUCCESS;
        //    }
        case CPUM_HANDLER_DS:
        case CPUM_HANDLER_ES:
        case CPUM_HANDLER_FS:
            TRPMGCHyperReturnToHost(pVM, VINF_EM_RAW_STALE_SELECTOR);
            break;

        case CPUM_HANDLER_IRET:
            TRPMGCHyperReturnToHost(pVM, VINF_EM_RAW_IRET_TRAP);
            break;
    }
    return VERR_TRPM_DONT_PANIC;
}

