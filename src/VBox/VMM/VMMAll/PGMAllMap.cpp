/* $Id$ */
/** @file
 * PGM - Page Manager and Monitor - All context code.
 */

/*
 * Copyright (C) 2006-2007 Sun Microsystems, Inc.
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 *
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa
 * Clara, CA 95054 USA or visit http://www.sun.com if you need
 * additional information or have any questions.
 */

/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#define LOG_GROUP LOG_GROUP_PGM
#include <VBox/pgm.h>
#include "PGMInternal.h"
#include <VBox/vm.h>
#include <iprt/assert.h>
#include <iprt/asm.h>
#include <VBox/err.h>


/**
 * Maps a range of physical pages at a given virtual address
 * in the guest context.
 *
 * The GC virtual address range must be within an existing mapping.
 *
 * @returns VBox status code.
 * @param   pVM         The virtual machine.
 * @param   GCPtr       Where to map the page(s). Must be page aligned.
 * @param   HCPhys      Start of the range of physical pages. Must be page aligned.
 * @param   cbPages     Number of bytes to map. Must be page aligned.
 * @param   fFlags      Page flags (X86_PTE_*).
 */
VMMDECL(int) PGMMap(PVM pVM, RTGCUINTPTR GCPtr, RTHCPHYS HCPhys, uint32_t cbPages, unsigned fFlags)
{
    AssertMsg(pVM->pgm.s.offVM, ("Bad init order\n"));

    /*
     * Validate input.
     */
    AssertMsg(RT_ALIGN_T(GCPtr, PAGE_SIZE, RTGCUINTPTR) == GCPtr, ("Invalid alignment GCPtr=%#x\n", GCPtr));
    AssertMsg(cbPages > 0 && RT_ALIGN_32(cbPages, PAGE_SIZE) == cbPages, ("Invalid cbPages=%#x\n",  cbPages));
    AssertMsg(!(fFlags & X86_PDE_PG_MASK), ("Invalid flags %#x\n", fFlags));

    /* hypervisor defaults */
    if (!fFlags)
        fFlags = X86_PTE_P | X86_PTE_A | X86_PTE_D;

    /*
     * Find the mapping.
     */
    PPGMMAPPING pCur = pVM->pgm.s.CTX_SUFF(pMappings);
    while (pCur)
    {
        if (GCPtr - pCur->GCPtr < pCur->cb)
        {
            if (GCPtr + cbPages - 1 > pCur->GCPtrLast)
            {
                AssertMsgFailed(("Invalid range!!\n"));
                return VERR_INVALID_PARAMETER;
            }

            /*
             * Setup PTE.
             */
            X86PTEPAE Pte;
            Pte.u = fFlags | (HCPhys & X86_PTE_PAE_PG_MASK);

            /*
             * Update the page tables.
             */
            for (;;)
            {
                RTGCUINTPTR     off = GCPtr - pCur->GCPtr;
                const unsigned  iPT = off >> X86_PD_SHIFT;
                const unsigned  iPageNo = (off >> PAGE_SHIFT) & X86_PT_MASK;

                /* 32-bit */
                pCur->aPTs[iPT].CTX_SUFF(pPT)->a[iPageNo].u = (uint32_t)Pte.u;      /* ASSUMES HCPhys < 4GB and/or that we're never gonna do 32-bit on a PAE host! */

                /* pae */
                pCur->aPTs[iPT].CTX_SUFF(paPaePTs)[iPageNo / 512].a[iPageNo % 512].u = Pte.u;

                /* next */
                cbPages -= PAGE_SIZE;
                if (!cbPages)
                    break;
                GCPtr += PAGE_SIZE;
                Pte.u += PAGE_SIZE;
            }

            return VINF_SUCCESS;
        }

        /* next */
        pCur = pCur->CTX_SUFF(pNext);
    }

    AssertMsgFailed(("GCPtr=%#x was not found in any mapping ranges!\n",  GCPtr));
    return VERR_INVALID_PARAMETER;
}


/**
 * Sets (replaces) the page flags for a range of pages in a mapping.
 *
 * @returns VBox status.
 * @param   pVM         VM handle.
 * @param   GCPtr       Virtual address of the first page in the range.
 * @param   cb          Size (in bytes) of the range to apply the modification to.
 * @param   fFlags      Page flags X86_PTE_*, excluding the page mask of course.
 */
VMMDECL(int) PGMMapSetPage(PVM pVM, RTGCPTR GCPtr, uint64_t cb, uint64_t fFlags)
{
    return PGMMapModifyPage(pVM, GCPtr, cb, fFlags, 0);
}


/**
 * Modify page flags for a range of pages in a mapping.
 *
 * The existing flags are ANDed with the fMask and ORed with the fFlags.
 *
 * @returns VBox status code.
 * @param   pVM         VM handle.
 * @param   GCPtr       Virtual address of the first page in the range.
 * @param   cb          Size (in bytes) of the range to apply the modification to.
 * @param   fFlags      The OR  mask - page flags X86_PTE_*, excluding the page mask of course.
 * @param   fMask       The AND mask - page flags X86_PTE_*, excluding the page mask of course.
 */
VMMDECL(int)  PGMMapModifyPage(PVM pVM, RTGCPTR GCPtr, size_t cb, uint64_t fFlags, uint64_t fMask)
{
    /*
     * Validate input.
     */
    AssertMsg(!(fFlags & X86_PTE_PAE_PG_MASK), ("fFlags=%#x\n", fFlags));
    Assert(cb);

    /*
     * Align the input.
     */
    cb     += (RTGCUINTPTR)GCPtr & PAGE_OFFSET_MASK;
    cb      = RT_ALIGN_Z(cb, PAGE_SIZE);
    GCPtr   = (RTGCPTR)((RTGCUINTPTR)GCPtr & PAGE_BASE_GC_MASK);

    /*
     * Find the mapping.
     */
    PPGMMAPPING pCur = pVM->pgm.s.CTX_SUFF(pMappings);
    while (pCur)
    {
        RTGCUINTPTR off = (RTGCUINTPTR)GCPtr - (RTGCUINTPTR)pCur->GCPtr;
        if (off < pCur->cb)
        {
            AssertMsgReturn(off + cb <= pCur->cb,
                            ("Invalid page range %#x LB%#x. mapping '%s' %#x to %#x\n",
                             GCPtr, cb, pCur->pszDesc, pCur->GCPtr, pCur->GCPtrLast),
                            VERR_INVALID_PARAMETER);

            /*
             * Perform the requested operation.
             */
            while (cb > 0)
            {
                unsigned iPT  = off >> X86_PD_SHIFT;
                unsigned iPTE = (off >> PAGE_SHIFT) & X86_PT_MASK;
                while (cb > 0 && iPTE < RT_ELEMENTS(pCur->aPTs[iPT].CTX_SUFF(pPT)->a))
                {
                    /* 32-Bit */
                    pCur->aPTs[iPT].CTX_SUFF(pPT)->a[iPTE].u &= fMask | X86_PTE_PG_MASK;
                    pCur->aPTs[iPT].CTX_SUFF(pPT)->a[iPTE].u |= fFlags & ~X86_PTE_PG_MASK;

                    /* PAE */
                    pCur->aPTs[iPT].CTX_SUFF(paPaePTs)[iPTE / 512].a[iPTE % 512].u &= fMask | X86_PTE_PAE_PG_MASK;
                    pCur->aPTs[iPT].CTX_SUFF(paPaePTs)[iPTE / 512].a[iPTE % 512].u |= fFlags & ~X86_PTE_PAE_PG_MASK;

                    /* invalidate tls */
                    PGM_INVL_PG((RTGCUINTPTR)pCur->GCPtr + off);

                    /* next */
                    iPTE++;
                    cb -= PAGE_SIZE;
                    off += PAGE_SIZE;
                }
            }

            return VINF_SUCCESS;
        }
        /* next */
        pCur = pCur->CTX_SUFF(pNext);
    }

    AssertMsgFailed(("Page range %#x LB%#x not found\n", GCPtr, cb));
    return VERR_INVALID_PARAMETER;
}


#ifndef IN_RING0
/**
 * Sets all PDEs involved with the mapping in the shadow page table.
 *
 * @param   pVM         The VM handle.
 * @param   pMap        Pointer to the mapping in question.
 * @param   iNewPDE     The index of the 32-bit PDE corresponding to the base of the mapping.
 */
void pgmMapSetShadowPDEs(PVM pVM, PPGMMAPPING pMap, unsigned iNewPDE)
{
    Log4(("pgmMapSetShadowPDEs new pde %x (mappings enabled %d)\n", iNewPDE, pgmMapAreMappingsEnabled(&pVM->pgm.s)));

    if (!pgmMapAreMappingsEnabled(&pVM->pgm.s))
        return;

#ifdef VBOX_WITH_PGMPOOL_PAGING_ONLY
    if (!pVM->pgm.s.CTX_SUFF(pShwPageCR3))
        return;    /* too early */
#endif

    PGMMODE enmShadowMode = PGMGetShadowMode(pVM);
    Assert(enmShadowMode <= PGMMODE_PAE_NX);

    /*
     * Init the page tables and insert them into the page directories.
     */
    unsigned i = pMap->cPTs;
    iNewPDE += i;
    while (i-- > 0)
    {
        iNewPDE--;

        switch(enmShadowMode)
        {
            case PGMMODE_32_BIT:
            {
                PX86PD pShw32BitPd = pgmShwGet32BitPDPtr(&pVM->pgm.s);
                AssertFatal(pShw32BitPd);

                if (pShw32BitPd->a[iNewPDE].n.u1Present)
                {
                    Assert(!(pShw32BitPd->a[iNewPDE].u & PGM_PDFLAGS_MAPPING));
                    pgmPoolFree(pVM, pShw32BitPd->a[iNewPDE].u & X86_PDE_PG_MASK, pVM->pgm.s.CTX_SUFF(pShwPageCR3)->idx, iNewPDE);
                }

                X86PDE Pde;
                /* Default mapping page directory flags are read/write and supervisor; individual page attributes determine the final flags */
                Pde.u = PGM_PDFLAGS_MAPPING | X86_PDE_P | X86_PDE_A | X86_PDE_RW | X86_PDE_US | (uint32_t)pMap->aPTs[i].HCPhysPT;
                pShw32BitPd->a[iNewPDE]   = Pde;
                break;
            }

            case PGMMODE_PAE:
            case PGMMODE_PAE_NX:
            {
                PX86PDPT  pShwPdpt;
                PX86PDPAE pShwPaePd;
                const unsigned iPdPt = iNewPDE / 256;
                unsigned iPDE = iNewPDE * 2 % 512;

                pShwPdpt  = pgmShwGetPaePDPTPtr(&pVM->pgm.s);
                Assert(pShwPdpt);
                pShwPaePd = pgmShwGetPaePDPtr(&pVM->pgm.s, (iPdPt << X86_PDPT_SHIFT));
#ifdef VBOX_WITH_PGMPOOL_PAGING_ONLY
                if (!pShwPaePd)
                {
                    X86PDPE GstPdpe;

                    if (PGMGetGuestMode(pVM) < PGMMODE_PAE)
                    {
                        /* Fake PDPT entry; access control handled on the page table level, so allow everything. */
                        GstPdpe.u  = X86_PDPE_P;   /* rw/us are reserved for PAE pdpte's; accessed bit causes invalid VT-x guest state errors */
                    }
                    else
                    {
                        PX86PDPE pGstPdpe;
                        pGstPdpe = pgmGstGetPaePDPEPtr(&pVM->pgm.s, (iPdPt << X86_PDPT_SHIFT));
                        if (pGstPdpe)
                            GstPdpe = *pGstPdpe;
                        else
                            GstPdpe.u  = X86_PDPE_P;   /* rw/us are reserved for PAE pdpte's; accessed bit causes invalid VT-x guest state errors */
                    }
                    int rc = pgmShwSyncPaePDPtr(pVM, (iPdPt << X86_PDPT_SHIFT), &GstPdpe, &pShwPaePd);
                    AssertFatal(RT_SUCCESS(rc));
                    if (rc != VINF_SUCCESS)
                    {
                        rc = pgmShwSyncPaePDPtr(pVM, (iPdPt << X86_PDPT_SHIFT), &GstPdpe, &pShwPaePd);
                        AssertFatalMsg(rc == VINF_SUCCESS, ("rc = %Rrc\n", rc));
                    }
                }
#endif
                AssertFatal(pShwPaePd);

                PPGMPOOLPAGE pPoolPagePde = pgmPoolGetPageByHCPhys(pVM, pShwPdpt->a[iPdPt].u & X86_PDPE_PG_MASK);
                AssertFatal(pPoolPagePde);

                if (pShwPaePd->a[iPDE].n.u1Present)
                {
                    Assert(!(pShwPaePd->a[iPDE].u & PGM_PDFLAGS_MAPPING));
                    pgmPoolFree(pVM, pShwPaePd->a[iPDE].u & X86_PDE_PG_MASK, pPoolPagePde->idx, iNewPDE);
                }

                X86PDEPAE PdePae0;
                PdePae0.u = PGM_PDFLAGS_MAPPING | X86_PDE_P | X86_PDE_A | X86_PDE_RW | X86_PDE_US | pMap->aPTs[i].HCPhysPaePT0;
                pShwPaePd->a[iPDE] = PdePae0;

                /* 2nd 2 MB PDE of the 4 MB region */
                iPDE++;
                AssertFatal(iPDE < 512);

                if (pShwPaePd->a[iPDE].n.u1Present)
                {
                    Assert(!(pShwPaePd->a[iPDE].u & PGM_PDFLAGS_MAPPING));
                    pgmPoolFree(pVM, pShwPaePd->a[iPDE].u & X86_PDE_PG_MASK, pPoolPagePde->idx, iNewPDE);
                }

                X86PDEPAE PdePae1;
                PdePae1.u = PGM_PDFLAGS_MAPPING | X86_PDE_P | X86_PDE_A | X86_PDE_RW | X86_PDE_US | pMap->aPTs[i].HCPhysPaePT1;
                pShwPaePd->a[iPDE] = PdePae1;

                /* Set the PGM_PDFLAGS_MAPPING flag in the page directory pointer entry. (legacy PAE guest mode) */
                pShwPdpt->a[iPdPt].u |= PGM_PLXFLAGS_MAPPING;
                break;
            }

            default:
                AssertFailed();
                break;
        }
    }
}

/**
 * Clears all PDEs involved with the mapping in the shadow page table.
 *
 * @param   pVM         The VM handle.
 * @param   pShwPageCR3 CR3 root page
 * @param   pMap        Pointer to the mapping in question.
 * @param   iOldPDE     The index of the 32-bit PDE corresponding to the base of the mapping.
 */
void pgmMapClearShadowPDEs(PVM pVM, PPGMPOOLPAGE pShwPageCR3, PPGMMAPPING pMap, unsigned iOldPDE)
{
    Log(("pgmMapClearShadowPDEs old pde %x (mappings enabled %d)\n", iOldPDE, pgmMapAreMappingsEnabled(&pVM->pgm.s)));

    if (!pgmMapAreMappingsEnabled(&pVM->pgm.s))
        return;

#ifdef VBOX_WITH_PGMPOOL_PAGING_ONLY
    Assert(pShwPageCR3);
#endif

    unsigned i = pMap->cPTs;
    PGMMODE  enmShadowMode = PGMGetShadowMode(pVM);

    iOldPDE += i;
    while (i-- > 0)
    {
        iOldPDE--;

        switch(enmShadowMode)
        {
            case PGMMODE_32_BIT:
            {
#ifdef VBOX_WITH_PGMPOOL_PAGING_ONLY
                PX86PD pShw32BitPd = (PX86PD)PGMPOOL_PAGE_2_PTR_BY_PGM(&pVM->pgm.s, pShwPageCR3);
#else
                PX86PD pShw32BitPd = pgmShwGet32BitPDPtr(&pVM->pgm.s);
#endif
                AssertFatal(pShw32BitPd);

                pShw32BitPd->a[iOldPDE].u   = 0;
                break;
            }

            case PGMMODE_PAE:
            case PGMMODE_PAE_NX:
            {
                PX86PDPT  pPdpt = NULL;
                PX86PDPAE pShwPaePd = NULL;

                const unsigned iPD = iOldPDE / 256;         /* iOldPDE * 2 / 512; iOldPDE is in 4 MB pages */
                unsigned iPDE = iOldPDE * 2 % 512;
#ifdef VBOX_WITH_PGMPOOL_PAGING_ONLY
                pPdpt     = (PX86PDPT)PGMPOOL_PAGE_2_PTR_BY_PGM(&pVM->pgm.s, pShwPageCR3);
                pShwPaePd = pgmShwGetPaePDPtr(&pVM->pgm.s, pPdpt, (iPD << X86_PDPT_SHIFT));
#else
                pPdpt     = pgmShwGetPaePDPTPtr(&pVM->pgm.s); 
                pShwPaePd = pgmShwGetPaePDPtr(&pVM->pgm.s, (iPD << X86_PDPT_SHIFT)); 
#endif
                AssertFatal(pShwPaePd);

                pShwPaePd->a[iPDE].u = 0;

                iPDE++;
                AssertFatal(iPDE < 512);

                pShwPaePd->a[iPDE].u = 0;
                /* Clear the PGM_PDFLAGS_MAPPING flag for the page directory pointer entry. (legacy PAE guest mode) */
                pPdpt->a[iPD].u &= ~PGM_PLXFLAGS_MAPPING;
                break;
            }

            default:
                AssertFailed();
                break;
        }
    }
}
#endif /* !IN_RING0 */

#ifdef VBOX_STRICT
/**
 * Clears all PDEs involved with the mapping in the shadow page table.
 *
 * @param   pVM         The VM handle.
 * @param   pShwPageCR3 CR3 root page
 * @param   pMap        Pointer to the mapping in question.
 * @param   iPDE        The index of the 32-bit PDE corresponding to the base of the mapping.
 */
void pgmMapCheckShadowPDEs(PVM pVM, PPGMPOOLPAGE pShwPageCR3, PPGMMAPPING pMap, unsigned iPDE)
{
    if (!pgmMapAreMappingsEnabled(&pVM->pgm.s))
        return;

#ifdef VBOX_WITH_PGMPOOL_PAGING_ONLY
    Assert(pShwPageCR3);
#endif

    unsigned i = pMap->cPTs;
    PGMMODE  enmShadowMode = PGMGetShadowMode(pVM);

    iPDE += i;
    while (i-- > 0)
    {
        iPDE--;

        switch(enmShadowMode)
        {
            case PGMMODE_32_BIT:
            {
#ifdef VBOX_WITH_PGMPOOL_PAGING_ONLY
                PX86PD pShw32BitPd = (PX86PD)PGMPOOL_PAGE_2_PTR_BY_PGM(&pVM->pgm.s, pShwPageCR3);
#else
                PX86PD pShw32BitPd = pgmShwGet32BitPDPtr(&pVM->pgm.s);
#endif
                AssertFatal(pShw32BitPd);

                AssertMsg(pShw32BitPd->a[iPDE].u == (PGM_PDFLAGS_MAPPING | X86_PDE_P | X86_PDE_A | X86_PDE_RW | X86_PDE_US | (uint32_t)pMap->aPTs[i].HCPhysPT),
                          ("Expected %x vs %x\n", pShw32BitPd->a[iPDE].u, (PGM_PDFLAGS_MAPPING | X86_PDE_P | X86_PDE_A | X86_PDE_RW | X86_PDE_US | (uint32_t)pMap->aPTs[i].HCPhysPT)));
                break;
            }

            case PGMMODE_PAE:
            case PGMMODE_PAE_NX:
            {
                PX86PDPT  pPdpt = NULL;
                PX86PDPAE pShwPaePd = NULL;

                const unsigned iPD = iPDE / 256;         /* iPDE * 2 / 512; iPDE is in 4 MB pages */
                unsigned iPaePDE = iPDE * 2 % 512;
#ifdef VBOX_WITH_PGMPOOL_PAGING_ONLY
                pPdpt     = (PX86PDPT)PGMPOOL_PAGE_2_PTR_BY_PGM(&pVM->pgm.s, pShwPageCR3);
                pShwPaePd = pgmShwGetPaePDPtr(&pVM->pgm.s, pPdpt, (iPD << X86_PDPT_SHIFT));
#else
                pPdpt     = pgmShwGetPaePDPTPtr(&pVM->pgm.s); 
                pShwPaePd = pgmShwGetPaePDPtr(&pVM->pgm.s, (iPD << X86_PDPT_SHIFT)); 
#endif
                AssertFatal(pShwPaePd);

                AssertMsg(pShwPaePd->a[iPaePDE].u == (PGM_PDFLAGS_MAPPING | X86_PDE_P | X86_PDE_A | X86_PDE_RW | X86_PDE_US | pMap->aPTs[i].HCPhysPaePT0),
                         ("Expected %RX64 vs %RX64\n", pShwPaePd->a[iPDE].u, (PGM_PDFLAGS_MAPPING | X86_PDE_P | X86_PDE_A | X86_PDE_RW | X86_PDE_US | (uint32_t)pMap->aPTs[i].HCPhysPT)));

                iPaePDE++;
                AssertFatal(iPaePDE < 512);

                AssertMsg(pShwPaePd->a[iPaePDE].u == (PGM_PDFLAGS_MAPPING | X86_PDE_P | X86_PDE_A | X86_PDE_RW | X86_PDE_US | pMap->aPTs[i].HCPhysPaePT1),
                         ("Expected %RX64 vs %RX64\n", pShwPaePd->a[iPDE].u, (PGM_PDFLAGS_MAPPING | X86_PDE_P | X86_PDE_A | X86_PDE_RW | X86_PDE_US | (uint32_t)pMap->aPTs[i].HCPhysPT)));
                break;
            }

            default:
                AssertFailed();
                break;
        }
    }
}

/**
 * Check the hypervisor mappings in the active CR3.
 *
 * @param   pVM         The virtual machine.
 */
VMMDECL(void) PGMMapCheck(PVM pVM)
{
#ifdef VBOX_WITH_PGMPOOL_PAGING_ONLY
    /*
     * Can skip this if mappings are disabled.
     */
    if (!pgmMapAreMappingsEnabled(&pVM->pgm.s))
        return;

# ifdef IN_RING0
    AssertFailed();
# else
#  ifdef VBOX_WITH_PGMPOOL_PAGING_ONLY
    Assert(pVM->pgm.s.CTX_SUFF(pShwPageCR3));
#  endif

    /*
     * Iterate mappings.
     */
    for (PPGMMAPPING pCur = pVM->pgm.s.CTX_SUFF(pMappings); pCur; pCur = pCur->CTX_SUFF(pNext))
    {
        unsigned iPDE = pCur->GCPtr >> X86_PD_SHIFT;

        pgmMapCheckShadowPDEs(pVM, pVM->pgm.s.CTX_SUFF(pShwPageCR3), pCur, iPDE);
    }
# endif /* IN_RING0 */
#endif /* VBOX_WITH_PGMPOOL_PAGING_ONLY */
}
#endif

/**
 * Apply the hypervisor mappings to the active CR3.
 *
 * @returns VBox status.
 * @param   pVM         The virtual machine.
 */
VMMDECL(int) PGMMapActivateAll(PVM pVM)
{
#ifdef VBOX_WITH_PGMPOOL_PAGING_ONLY
    /*
     * Can skip this if mappings are disabled.
     */
    if (!pgmMapAreMappingsEnabled(&pVM->pgm.s))
#else
    /*
     * Can skip this if mappings are safely fixed.
     */
    if (pVM->pgm.s.fMappingsFixed)
#endif
        return VINF_SUCCESS;

    /* @note A log flush (in RC) can cause problems when called from MapCR3 (inconsistent state will trigger assertions). */
    Log4(("PGMMapActivateAll fixed mappings=%d\n", pVM->pgm.s.fMappingsFixed));

#ifdef IN_RING0
    AssertFailed();
    return VERR_INTERNAL_ERROR;
#else
# ifdef VBOX_WITH_PGMPOOL_PAGING_ONLY
    Assert(pVM->pgm.s.CTX_SUFF(pShwPageCR3));
# endif

    /*
     * Iterate mappings.
     */
    for (PPGMMAPPING pCur = pVM->pgm.s.CTX_SUFF(pMappings); pCur; pCur = pCur->CTX_SUFF(pNext))
    {
        unsigned iPDE = pCur->GCPtr >> X86_PD_SHIFT;

        pgmMapSetShadowPDEs(pVM, pCur, iPDE);
    }
    return VINF_SUCCESS;
#endif /* IN_RING0 */
}

/**
 * Remove the hypervisor mappings from the active CR3
 *
 * @returns VBox status.
 * @param   pVM         The virtual machine.
 */
VMMDECL(int) PGMMapDeactivateAll(PVM pVM)
{
#ifdef VBOX_WITH_PGMPOOL_PAGING_ONLY
    /*
     * Can skip this if mappings are disabled.
     */
    if (!pgmMapAreMappingsEnabled(&pVM->pgm.s))
#else
    /*
     * Can skip this if mappings are safely fixed.
     */
    if (pVM->pgm.s.fMappingsFixed)
#endif
        return VINF_SUCCESS;

    Log(("PGMMapDeactivateAll fixed mappings=%d\n", pVM->pgm.s.fMappingsFixed));

#ifdef IN_RING0
    AssertFailed();
    return VERR_INTERNAL_ERROR;
#else
# ifdef VBOX_WITH_PGMPOOL_PAGING_ONLY
    Assert(pVM->pgm.s.CTX_SUFF(pShwPageCR3));
# endif

    /*
     * Iterate mappings.
     */
    for (PPGMMAPPING pCur = pVM->pgm.s.CTX_SUFF(pMappings); pCur; pCur = pCur->CTX_SUFF(pNext))
    {
        unsigned iPDE = pCur->GCPtr >> X86_PD_SHIFT;

        pgmMapClearShadowPDEs(pVM, pVM->pgm.s.CTX_SUFF(pShwPageCR3), pCur, iPDE);
    }
    return VINF_SUCCESS;
#endif /* IN_RING0 */
}


/**
 * Remove the hypervisor mappings from the specified CR3
 *
 * @returns VBox status.
 * @param   pVM         The virtual machine.
 * @param   pShwPageCR3 CR3 root page
 */
int pgmMapDeactivateCR3(PVM pVM, PPGMPOOLPAGE pShwPageCR3)
{
#ifdef VBOX_WITH_PGMPOOL_PAGING_ONLY
    /*
     * Can skip this if mappings are disabled.
     */
    if (!pgmMapAreMappingsEnabled(&pVM->pgm.s))
#else
    /*
     * Can skip this if mappings are safely fixed.
     */
    if (pVM->pgm.s.fMappingsFixed)
#endif
        return VINF_SUCCESS;

#ifdef IN_RING0
    AssertFailed();
    return VERR_INTERNAL_ERROR;
#else
# ifdef VBOX_WITH_PGMPOOL_PAGING_ONLY
    Assert(pVM->pgm.s.CTX_SUFF(pShwPageCR3));
# endif

    /*
     * Iterate mappings.
     */
    for (PPGMMAPPING pCur = pVM->pgm.s.CTX_SUFF(pMappings); pCur; pCur = pCur->CTX_SUFF(pNext))
    {
        unsigned iPDE = pCur->GCPtr >> X86_PD_SHIFT;

        pgmMapClearShadowPDEs(pVM, pShwPageCR3, pCur, iPDE);
    }
    return VINF_SUCCESS;
#endif /* IN_RING0 */
}

#ifndef IN_RING0
/**
 * Checks guest PD for conflicts with VMM GC mappings.
 *
 * @returns true if conflict detected.
 * @returns false if not.
 * @param   pVM                 The virtual machine.
 */
VMMDECL(bool) PGMMapHasConflicts(PVM pVM)
{
    /*
     * Can skip this if mappings are safely fixed.
     */
    if (pVM->pgm.s.fMappingsFixed)
        return false;

    PGMMODE const enmGuestMode = PGMGetGuestMode(pVM);
    Assert(enmGuestMode <= PGMMODE_PAE_NX);

    /*
     * Iterate mappings.
     */
    if (enmGuestMode == PGMMODE_32_BIT)
    {
        /*
         * Resolve the page directory.
         */
        PX86PD pPD = pgmGstGet32bitPDPtr(&pVM->pgm.s);
        Assert(pPD);

        for (PPGMMAPPING pCur = pVM->pgm.s.CTX_SUFF(pMappings); pCur; pCur = pCur->CTX_SUFF(pNext))
        {
            unsigned iPDE = pCur->GCPtr >> X86_PD_SHIFT;
            unsigned iPT = pCur->cPTs;
            while (iPT-- > 0)
                if (    pPD->a[iPDE + iPT].n.u1Present /** @todo PGMGstGetPDE. */
                    &&  (pVM->fRawR0Enabled || pPD->a[iPDE + iPT].n.u1User))
                {
                    STAM_COUNTER_INC(&pVM->pgm.s.StatR3DetectedConflicts);

#ifdef IN_RING3
                    Log(("PGMHasMappingConflicts: Conflict was detected at %08RX32 for mapping %s (32 bits)\n"
                         "                        iPDE=%#x iPT=%#x PDE=%RGp.\n",
                        (iPT + iPDE) << X86_PD_SHIFT, pCur->pszDesc,
                        iPDE, iPT, pPD->a[iPDE + iPT].au32[0]));
#else
                    Log(("PGMHasMappingConflicts: Conflict was detected at %08RX32 for mapping (32 bits)\n"
                         "                        iPDE=%#x iPT=%#x PDE=%RGp.\n",
                        (iPT + iPDE) << X86_PD_SHIFT,
                        iPDE, iPT, pPD->a[iPDE + iPT].au32[0]));
#endif
                    return true;
                }
        }
    }
    else if (   enmGuestMode == PGMMODE_PAE
             || enmGuestMode == PGMMODE_PAE_NX)
    {
        for (PPGMMAPPING pCur = pVM->pgm.s.CTX_SUFF(pMappings); pCur; pCur = pCur->CTX_SUFF(pNext))
        {
            RTGCPTR   GCPtr = pCur->GCPtr;

            unsigned  iPT = pCur->cb >> X86_PD_PAE_SHIFT;
            while (iPT-- > 0)
            {
                X86PDEPAE Pde = pgmGstGetPaePDE(&pVM->pgm.s, GCPtr);

                if (   Pde.n.u1Present
                    && (pVM->fRawR0Enabled || Pde.n.u1User))
                {
                    STAM_COUNTER_INC(&pVM->pgm.s.StatR3DetectedConflicts);
#ifdef IN_RING3
                    Log(("PGMHasMappingConflicts: Conflict was detected at %RGv for mapping %s (PAE)\n"
                         "                        PDE=%016RX64.\n",
                        GCPtr, pCur->pszDesc, Pde.u));
#else
                    Log(("PGMHasMappingConflicts: Conflict was detected at %RGv for mapping (PAE)\n"
                         "                        PDE=%016RX64.\n",
                        GCPtr, Pde.u));
#endif
                    return true;
                }
                GCPtr += (1 << X86_PD_PAE_SHIFT);
            }
        }
    }
    else
        AssertFailed();

    return false;
}

# ifdef VBOX_WITH_PGMPOOL_PAGING_ONLY
/**
 * Checks and resolves (ring 3 only) guest conflicts with VMM GC mappings.
 *
 * @returns VBox status.
 * @param   pVM                 The virtual machine.
 */
VMMDECL(int) PGMMapResolveConflicts(PVM pVM)
{
    /*
     * Can skip this if mappings are safely fixed.
     */
    if (pVM->pgm.s.fMappingsFixed)
        return VINF_SUCCESS;

    PGMMODE const enmGuestMode = PGMGetGuestMode(pVM);
    Assert(enmGuestMode <= PGMMODE_PAE_NX);

    /*
     * Iterate mappings.
     */
    if (enmGuestMode == PGMMODE_32_BIT)
    {
        /*
         * Resolve the page directory.
         */
        PX86PD pPD = pgmGstGet32bitPDPtr(&pVM->pgm.s);
        Assert(pPD);

        for (PPGMMAPPING pCur = pVM->pgm.s.CTX_SUFF(pMappings); pCur; pCur = pCur->CTX_SUFF(pNext))
        {
            unsigned iPDE = pCur->GCPtr >> X86_PD_SHIFT;
            unsigned iPT = pCur->cPTs;
            while (iPT-- > 0)
            {
                if (    pPD->a[iPDE + iPT].n.u1Present /** @todo PGMGstGetPDE. */
                    &&  (pVM->fRawR0Enabled || pPD->a[iPDE + iPT].n.u1User))
                {
                    STAM_COUNTER_INC(&pVM->pgm.s.StatR3DetectedConflicts);

#ifdef IN_RING3
                    Log(("PGMHasMappingConflicts: Conflict was detected at %08RX32 for mapping %s (32 bits)\n"
                         "                        iPDE=%#x iPT=%#x PDE=%RGp.\n",
                        (iPT + iPDE) << X86_PD_SHIFT, pCur->pszDesc,
                        iPDE, iPT, pPD->a[iPDE + iPT].au32[0]));
                    int rc = pgmR3SyncPTResolveConflict(pVM, pCur, pPD, iPDE << X86_PD_SHIFT);
                    AssertRCReturn(rc, rc);

                    /*
                     * Update pCur.
                     */
                    pCur = pVM->pgm.s.CTX_SUFF(pMappings);
                    while (pCur && pCur->GCPtr < (iPDE << X86_PD_SHIFT))
                        pCur = pCur->CTX_SUFF(pNext);
                    break;
#else
                    Log(("PGMHasMappingConflicts: Conflict was detected at %08RX32 for mapping (32 bits)\n"
                         "                        iPDE=%#x iPT=%#x PDE=%RGp.\n",
                        (iPT + iPDE) << X86_PD_SHIFT,
                        iPDE, iPT, pPD->a[iPDE + iPT].au32[0]));
                    return VINF_PGM_SYNC_CR3;
#endif
                }
            }
            if (!pCur) 
                break;
        }
    }
    else if (   enmGuestMode == PGMMODE_PAE
             || enmGuestMode == PGMMODE_PAE_NX)
    {
        for (PPGMMAPPING pCur = pVM->pgm.s.CTX_SUFF(pMappings); pCur; pCur = pCur->CTX_SUFF(pNext))
        {
            RTGCPTR   GCPtr = pCur->GCPtr;

            unsigned  iPT = pCur->cb >> X86_PD_PAE_SHIFT;
            while (iPT-- > 0)
            {
                X86PDEPAE Pde = pgmGstGetPaePDE(&pVM->pgm.s, GCPtr);

                if (   Pde.n.u1Present
                    && (pVM->fRawR0Enabled || Pde.n.u1User))
                {
                    STAM_COUNTER_INC(&pVM->pgm.s.StatR3DetectedConflicts);
#ifdef IN_RING3
                    Log(("PGMHasMappingConflicts: Conflict was detected at %RGv for mapping %s (PAE)\n"
                         "                        PDE=%016RX64.\n",
                        GCPtr, pCur->pszDesc, Pde.u));
                    int rc = pgmR3SyncPTResolveConflictPAE(pVM, pCur, GCPtr);
                    AssertRCReturn(rc, rc);

                    /*
                     * Update pCur.
                     */
                    pCur = pVM->pgm.s.CTX_SUFF(pMappings);
                    while (pCur && pCur->GCPtr < GCPtr)
                        pCur = pCur->CTX_SUFF(pNext);
                    break;
#else
                    Log(("PGMHasMappingConflicts: Conflict was detected at %RGv for mapping (PAE)\n"
                         "                        PDE=%016RX64.\n",
                        GCPtr, Pde.u));
                    return VINF_PGM_SYNC_CR3;
#endif
                }
                GCPtr += (1 << X86_PD_PAE_SHIFT);
            }
            if (!pCur) 
                break;
        }
    }
    else
        AssertFailed();

    return VINF_SUCCESS;
}
# endif /* VBOX_WITH_PGMPOOL_PAGING_ONLY */

#endif /* IN_RING0 */

