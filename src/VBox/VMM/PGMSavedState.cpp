/* $Id$ */
/** @file
 * PGM - Page Manager and Monitor, The Saved State Part.
 */

/*
 * Copyright (C) 2006-2009 Sun Microsystems, Inc.
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
#include <VBox/dbgf.h>
#include <VBox/pgm.h>
#include <VBox/cpum.h>
#include <VBox/iom.h>
#include <VBox/sup.h>
#include <VBox/mm.h>
#include <VBox/em.h>
#include <VBox/stam.h>
#include <VBox/rem.h>
#include <VBox/selm.h>
#include <VBox/ssm.h>
#include <VBox/hwaccm.h>
#include "PGMInternal.h"
#include <VBox/vm.h>

#include <VBox/dbg.h>
#include <VBox/param.h>
#include <VBox/err.h>

#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/env.h>
#include <iprt/mem.h>
#include <iprt/file.h>
#include <iprt/string.h>
#include <iprt/thread.h>


/*******************************************************************************
*   Defined Constants And Macros                                               *
*******************************************************************************/
/** Saved state data unit version. */
#ifdef VBOX_WITH_LIVE_MIGRATION
# define PGM_SAVED_STATE_VERSION                10
#else
# define PGM_SAVED_STATE_VERSION                9
#endif
/** Saved state data unit version for 3.0. (pre live migration) */
#define PGM_SAVED_STATE_VERSION_3_0_0           9
/** Saved state data unit version for 2.2.2 and later. */
#define PGM_SAVED_STATE_VERSION_2_2_2           8
/** Saved state data unit version for 2.2.0. */
#define PGM_SAVED_STATE_VERSION_RR_DESC         7
/** Saved state data unit version. */
#define PGM_SAVED_STATE_VERSION_OLD_PHYS_CODE   6


/** @name Sparse state record types
 * @{  */
/** Zero page. No data. */
#define PGM_STATE_REC_ZERO              UINT8_C(0x00)
/** Raw page. */
#define PGM_STATE_REC_RAW               UINT8_C(0x01)
/** Virgin ROM page. Followed by protection (8-bit) and the raw bits. */
#define PGM_STATE_REC_ROM_VIRGIN        UINT8_C(0x02)
/** Raw shadowed ROM page. The protection (8-bit) preceeds the raw bits. */
#define PGM_STATE_REC_ROM_SHADOW        UINT8_C(0x03)
/** Zero shadowed ROM page. The protection (8-bit) is the only payload. */
#define PGM_STATE_REC_ROM_SHADOW_ZERO   UINT8_C(0x04)
/** ROM protection (8-bit). */
#define PGM_STATE_REC_ROM_PROT          UINT8_C(0x05)
/** The last record type. */
#define PGM_STATE_REC_LAST              PGM_STATE_REC_ROM_PROT
/** End marker. */
#define PGM_STATE_REC_END               UINT8_C(0xff)
/** Flag indicating that the data is preceeded by an RTGCPHYS containing the
 *  page address.  If not set, the page follows the immediately after the
 *  previous one. */
#define PGM_STATE_REC_FLAG_ADDR         UINT8_C(0x80)
/** @} */


/*******************************************************************************
*   Structures and Typedefs                                                    *
*******************************************************************************/
/** For loading old saved states. (pre-smp) */
typedef struct
{
    /** If set no conflict checks are required.  (boolean) */
    bool                            fMappingsFixed;
    /** Size of fixed mapping */
    uint32_t                        cbMappingFixed;
    /** Base address (GC) of fixed mapping */
    RTGCPTR                         GCPtrMappingFixed;
    /** A20 gate mask.
     * Our current approach to A20 emulation is to let REM do it and don't bother
     * anywhere else. The interesting Guests will be operating with it enabled anyway.
     * But whould need arrise, we'll subject physical addresses to this mask. */
    RTGCPHYS                        GCPhysA20Mask;
    /** A20 gate state - boolean! */
    bool                            fA20Enabled;
    /** The guest paging mode. */
    PGMMODE                         enmGuestMode;
} PGMOLD;


/*******************************************************************************
*   Global Variables                                                           *
*******************************************************************************/
/** PGM fields to save/load. */
static const SSMFIELD s_aPGMFields[] =
{
    SSMFIELD_ENTRY(         PGM, fMappingsFixed),
    SSMFIELD_ENTRY_GCPTR(   PGM, GCPtrMappingFixed),
    SSMFIELD_ENTRY(         PGM, cbMappingFixed),
    SSMFIELD_ENTRY_TERM()
};

static const SSMFIELD s_aPGMCpuFields[] =
{
    SSMFIELD_ENTRY(         PGMCPU, fA20Enabled),
    SSMFIELD_ENTRY_GCPHYS(  PGMCPU, GCPhysA20Mask),
    SSMFIELD_ENTRY(         PGMCPU, enmGuestMode),
    SSMFIELD_ENTRY_TERM()
};

static const SSMFIELD s_aPGMFields_Old[] =
{
    SSMFIELD_ENTRY(         PGMOLD, fMappingsFixed),
    SSMFIELD_ENTRY_GCPTR(   PGMOLD, GCPtrMappingFixed),
    SSMFIELD_ENTRY(         PGMOLD, cbMappingFixed),
    SSMFIELD_ENTRY(         PGMOLD, fA20Enabled),
    SSMFIELD_ENTRY_GCPHYS(  PGMOLD, GCPhysA20Mask),
    SSMFIELD_ENTRY(         PGMOLD, enmGuestMode),
    SSMFIELD_ENTRY_TERM()
};


/**
 * Find the ROM tracking structure for the given page.
 *
 * @returns Pointer to the ROM page structure. NULL if the caller didn't check
 *          that it's a ROM page.
 * @param   pVM         The VM handle.
 * @param   GCPhys      The address of the ROM page.
 */
static PPGMROMPAGE pgmR3GetRomPage(PVM pVM, RTGCPHYS GCPhys) /** @todo change this to take a hint. */
{
    for (PPGMROMRANGE pRomRange = pVM->pgm.s.CTX_SUFF(pRomRanges);
         pRomRange;
         pRomRange = pRomRange->CTX_SUFF(pNext))
    {
        RTGCPHYS off = GCPhys - pRomRange->GCPhys;
        if (GCPhys - pRomRange->GCPhys < pRomRange->cb)
            return &pRomRange->aPages[off >> PAGE_SHIFT];
    }
    return NULL;
}


/**
 * Save zero indicator + bits for the specified page.
 *
 * @returns VBox status code, errors are logged/asserted before returning.
 * @param   pVM         The VM handle.
 * @param   pSSH        The saved state handle.
 * @param   pPage       The page to save.
 * @param   GCPhys      The address of the page.
 * @param   pRam        The ram range (for error logging).
 */
static int pgmR3SavePage(PVM pVM, PSSMHANDLE pSSM, PPGMPAGE pPage, RTGCPHYS GCPhys, PPGMRAMRANGE pRam)
{
    int rc;
    if (PGM_PAGE_IS_ZERO(pPage))
        rc = SSMR3PutU8(pSSM, 0);
    else
    {
        void const *pvPage;
        rc = pgmPhysGCPhys2CCPtrInternalReadOnly(pVM, pPage, GCPhys, &pvPage);
        AssertLogRelMsgRCReturn(rc, ("pPage=%R[pgmpage] GCPhys=%#x %s\n", pPage, GCPhys, pRam->pszDesc), rc);

        SSMR3PutU8(pSSM, 1);
        rc = SSMR3PutMem(pSSM, pvPage, PAGE_SIZE);
    }
    return rc;
}


/**
 * Save a shadowed ROM page.
 *
 * Format: Type, protection, and two pages with zero indicators.
 *
 * @returns VBox status code, errors are logged/asserted before returning.
 * @param   pVM         The VM handle.
 * @param   pSSH        The saved state handle.
 * @param   pPage       The page to save.
 * @param   GCPhys      The address of the page.
 * @param   pRam        The ram range (for error logging).
 */
static int pgmR3SaveShadowedRomPage(PVM pVM, PSSMHANDLE pSSM, PPGMPAGE pPage, RTGCPHYS GCPhys, PPGMRAMRANGE pRam)
{
    /* Need to save both pages and the current state. */
    PPGMROMPAGE pRomPage = pgmR3GetRomPage(pVM, GCPhys);
    AssertLogRelMsgReturn(pRomPage, ("GCPhys=%RGp %s\n", GCPhys, pRam->pszDesc), VERR_INTERNAL_ERROR);

    SSMR3PutU8(pSSM, PGMPAGETYPE_ROM_SHADOW);
    SSMR3PutU8(pSSM, pRomPage->enmProt);

    int rc = pgmR3SavePage(pVM, pSSM, pPage, GCPhys, pRam);
    if (RT_SUCCESS(rc))
    {
        PPGMPAGE pPagePassive = PGMROMPROT_IS_ROM(pRomPage->enmProt) ? &pRomPage->Shadow : &pRomPage->Virgin;
        rc = pgmR3SavePage(pVM, pSSM, pPagePassive, GCPhys, pRam);
    }
    return rc;
}


/**
 * Prepare for a live save operation.
 *
 * This will attempt to allocate and initialize the tracking structures.  It
 * will also prepare for write monitoring of pages and initialize PGM::LiveSave.
 * pgmR3SaveDone will do the cleanups.
 *
 * @returns VBox status code.
 *
 * @param   pVM         The VM handle.
 * @param   pSSM        The SSM handle.
 */
static DECLCALLBACK(int) pgmR3LivePrep(PVM pVM, PSSMHANDLE pSSM)
{
    /*
     * Indicate that we will be using the write monitoring.
     */
    pgmLock(pVM);
    /** @todo find a way of mediating this when more users are added. */
    if (pVM->pgm.s.fPhysWriteMonitoringEngaged)
    {
        pgmUnlock(pVM);
        AssertLogRelFailedReturn(VERR_INTERNAL_ERROR_2);
    }
    pVM->pgm.s.fPhysWriteMonitoringEngaged = true;
    pgmUnlock(pVM);

    /*
     * Initialize the statistics.
     */
    pVM->pgm.s.LiveSave.cReadyPages = 0;
    pVM->pgm.s.LiveSave.cDirtyPages = 0;
    pVM->pgm.s.LiveSave.cMmioPages  = 0;

    /*
     * Try allocating tracking structures for the ram ranges.
     *
     * To avoid lock contention, we leave the lock every time we're allocating
     * a new array.  This means we'll have to ditch the allocation and start
     * all over again if the RAM range list changes in-between.
     *
     * Note! pgmR3SaveDone will always be called and it is therefore responsible
     *       for cleaning up.
     */
    PPGMRAMRANGE pCur;
    pgmLock(pVM);
    do
    {
        for (pCur = pVM->pgm.s.pRamRangesR3; pCur; pCur = pCur->pNextR3)
        {
            if (   !pCur->paLSPages
                && !PGM_RAM_RANGE_IS_AD_HOC(pCur))
            {
                uint32_t const  idRamRangesGen = pVM->pgm.s.idRamRangesGen;
                uint32_t const  cPages = pCur->cb >> PAGE_SHIFT;
                pgmUnlock(pVM);
                PPGMLIVESAVEPAGE paLSPages = (PPGMLIVESAVEPAGE)MMR3HeapAllocZ(pVM, MM_TAG_PGM, cPages * sizeof(PGMLIVESAVEPAGE));
                if (!paLSPages)
                    return VERR_NO_MEMORY;
                pgmLock(pVM);
                if (pVM->pgm.s.idRamRangesGen != idRamRangesGen)
                {
                    pgmUnlock(pVM);
                    MMR3HeapFree(paLSPages);
                    pgmLock(pVM);
                    break;              /* try again */
                }
                pCur->paLSPages = paLSPages;

                /*
                 * Initialize the array.
                 */
                uint32_t iPage = cPages;
                while (iPage-- > 0)
                {
                    /** @todo yield critsect! (after moving this away from EMT0) */
                    PCPGMPAGE pPage = &pCur->aPages[iPage];
                    paLSPages[iPage].uPassSaved             = UINT32_MAX;
                    paLSPages[iPage].cDirtied               = 0;
                    paLSPages[iPage].fDirty                 = 1; /* everything is dirty at this time */
                    paLSPages[iPage].fWriteMonitored        = 0;
                    paLSPages[iPage].fWriteMonitoredJustNow = 0;
                    paLSPages[iPage].u2Reserved             = 0;
                    switch (PGM_PAGE_GET_TYPE(pPage))
                    {
                        case PGMPAGETYPE_RAM:
                            if (PGM_PAGE_IS_ZERO(pPage))
                            {
                                paLSPages[iPage].fZero   = 1;
                                paLSPages[iPage].fShared = 0;
                            }
                            else if (PGM_PAGE_IS_SHARED(pPage))
                            {
                                paLSPages[iPage].fZero   = 0;
                                paLSPages[iPage].fShared = 1;
                            }
                            else
                            {
                                paLSPages[iPage].fZero   = 0;
                                paLSPages[iPage].fShared = 0;
                            }
                            paLSPages[iPage].fMmio   = 0;
                            pVM->pgm.s.LiveSave.cDirtyPages++;
                            break;

                        case PGMPAGETYPE_ROM_SHADOW:
                        case PGMPAGETYPE_ROM:
                        {
                            PPGMROMPAGE pRomPage = pgmR3GetRomPage(pVM, pCur->GCPhys + ((RTGCPHYS)iPage << PAGE_SHIFT));
                            pRomPage->LiveSave.u8Prot       = (uint8_t)pRomPage->enmProt;
                            pRomPage->LiveSave.fSavedVirgin = false;
                            pRomPage->LiveSave.fDone        = false;
                            pRomPage->LiveSave.fWrittenTo   = PGMROMPROT_IS_ROM(pRomPage->enmProt)
                                                            ? !PGM_PAGE_IS_ZERO(&pRomPage->Shadow)
                                                            : !PGM_PAGE_IS_ZERO(pPage);
                            paLSPages[iPage].fZero   = !pRomPage->LiveSave.fWrittenTo;
                            paLSPages[iPage].fShared = 0;
                            paLSPages[iPage].fDirty  = 1;
                            paLSPages[iPage].fMmio   = 0;
                            pVM->pgm.s.LiveSave.cDirtyPages += 2;
                            break;
                        }

                        default:
                            AssertMsgFailed(("%R[pgmpage]", pPage));
                        case PGMPAGETYPE_MMIO2:
                        case PGMPAGETYPE_MMIO2_ALIAS_MMIO:
                            paLSPages[iPage].fZero   = 0;
                            paLSPages[iPage].fShared = 0;
                            paLSPages[iPage].fDirty  = 1;
                            paLSPages[iPage].fMmio   = 1;
                            pVM->pgm.s.LiveSave.cMmioPages++;
                            break;

                        case PGMPAGETYPE_MMIO:
                            paLSPages[iPage].fZero   = 1;
                            paLSPages[iPage].fShared = 0;
                            paLSPages[iPage].fDirty  = 1;
                            paLSPages[iPage].fMmio   = 1;
                            pVM->pgm.s.LiveSave.cMmioPages++;
                            break;
                    }
                }
            }
        }
    } while (pCur);
    pgmUnlock(pVM);

    return VINF_SUCCESS;
}


/**
 * Takes care of the virgin ROM pages in the first pass.
 *
 * This is an attempt at simplifying the handling of ROM pages a little bit.
 * This ASSUMES that no new ROM ranges will be added and that they won't be
 * relinked in any way.
 *
 * @param   pVM     The VM handle.
 * @param   pSSM    The SSM handle.
 */
static int pgmR3LiveExecSaveVirginRomPages(PVM pVM, PSSMHANDLE pSSM)
{
    RTGCPHYS GCPhysLast = NIL_RTGCPHYS;
    pgmLock(pVM);
    for (PPGMROMRANGE pRom = pVM->pgm.s.pRomRangesR3; pRom; pRom = pRom->pNextR3)
    {
        uint32_t const cPages = pRom->cb >> PAGE_SHIFT;
        for (uint32_t iPage = 0; iPage < cPages; iPage++)
        {
            RTGCPHYS   GCPhys  = pRom->GCPhys + ((RTGCPHYS)iPage << PAGE_SHIFT);
            PGMROMPROT enmProt = pRom->aPages[iPage].enmProt;

            /* Get the virgin page descriptor. */
            PPGMPAGE pPage;
            if (PGMROMPROT_IS_ROM(enmProt))
                pPage = pgmPhysGetPage(&pVM->pgm.s, GCPhys);
            else
                pPage = &pRom->aPages[iPage].Virgin;

            /* Get the page bits. (Cannot use pgmPhysGCPhys2CCPtrInternalReadOnly here!) */
            int rc = VINF_SUCCESS;
            char abPage[PAGE_SIZE];
            if (!PGM_PAGE_IS_ZERO(pPage))
            {
                void *pvPage;
                PPGMPAGEMAP pMapIgnored;
                rc = pgmPhysPageMap(pVM, pPage, GCPhys, &pMapIgnored, &pvPage);
                if (RT_SUCCESS(rc))
                    memcpy(abPage, pvPage, PAGE_SIZE);
            }
            else
                ASMMemZeroPage(abPage);
            pgmUnlock(pVM);
            AssertLogRelMsgRCReturn(rc, ("rc=%Rrc GCPhys=%RGp\n", rc, GCPhys), rc);

            /* Save it. */
            if (GCPhys == GCPhysLast + PAGE_SIZE)
                SSMR3PutU8(pSSM, PGM_STATE_REC_ROM_VIRGIN);
            else
            {
                SSMR3PutU8(pSSM, PGM_STATE_REC_ROM_VIRGIN | PGM_STATE_REC_FLAG_ADDR);
                SSMR3PutGCPhys(pSSM, GCPhys);
            }
            SSMR3PutU8(pSSM, (uint8_t)enmProt);
            rc = SSMR3PutMem(pSSM, abPage, PAGE_SIZE);
            if (RT_FAILURE(rc))
                return rc;

            /* Update state. */
            pgmLock(pVM);
            GCPhysLast = GCPhys;
            pRom->aPages[iPage].LiveSave.fSavedVirgin = true;
            pRom->aPages[iPage].LiveSave.u8Prot = (uint8_t)enmProt;
            pVM->pgm.s.LiveSave.cDirtyPages--;
            pVM->pgm.s.LiveSave.cReadyPages++;

            /* If the page cannot be shadowed, mark it as done. */
            PPGMRAMRANGE pRam;
            pPage = pgmPhysGetPageAndRange(&pVM->pgm.s, GCPhys, &pRam);
            PPGMLIVESAVEPAGE pLSPage = &pRam->paLSPages[(GCPhys - pRam->GCPhys) >> PAGE_SHIFT];
            if (!(pRom->fFlags & PGMPHYS_ROM_FLAGS_SHADOWED))
            {
                pLSPage->fDirty = 0;
                pRom->aPages[iPage].LiveSave.fWrittenTo = false;
                pRom->aPages[iPage].LiveSave.fDone = true;
                pVM->pgm.s.LiveSave.cDirtyPages--;
                pVM->pgm.s.LiveSave.cReadyPages++;
            }
        }
    }
    pgmUnlock(pVM);
    return VINF_SUCCESS;
}


/**
 * pgmR3LiveExec part 1: Scan for page modifications and reprotect them.
 *
 * Note! Since we don't care about MMIO or MMIO2 pages and since we don't
 *       have any movable ROMs yet, we can safely yield the PGM when we
 *       detect contention.
 *
 *       This holds true for part 2 as well.
 *
 * @param   pVM     The VM handle.
 */
static void pgmR3LiveExecPart1(PVM pVM)
{
    RTGCPHYS GCPhysCur = 0;
    PPGMRAMRANGE pCur;
    pgmLock(pVM);
    do
    {
        uint32_t const  idRamRangesGen = pVM->pgm.s.idRamRangesGen;
        uint32_t        cSinceYield    = 0;
        for (pCur = pVM->pgm.s.pRamRangesR3; pCur; pCur = pCur->pNextR3)
        {
            if (pCur->GCPhysLast > GCPhysCur)
            {
                PPGMLIVESAVEPAGE paLSPages = pCur->paLSPages;
                uint32_t         cPages    = pCur->cb >> PAGE_SHIFT;
                uint32_t         iPage     = GCPhysCur <= pCur->GCPhys ? 0 : (GCPhysCur - pCur->GCPhys) >> PAGE_SHIFT;
                GCPhysCur = 0;
                for (; iPage < cPages; iPage++, cSinceYield++)
                {
                    /* Do yield first. */
                    if (    (cSinceYield & 0x7ff) == 0x7ff
                        &&  PDMR3CritSectYield(&pVM->pgm.s.CritSect)
                        &&  pVM->pgm.s.idRamRangesGen != idRamRangesGen)
                    {
                        GCPhysCur = pCur->GCPhys + ((RTGCPHYS)iPage << PAGE_SHIFT);
                        break; /* restart */
                    }

                    /* Process the page. */
                    if (paLSPages[iPage].fMmio)
                    {
                        switch (PGM_PAGE_GET_TYPE(&pCur->aPages[iPage]))
                        {
                            case PGMPAGETYPE_RAM:
                            {
                                switch (PGM_PAGE_GET_STATE(&pCur->aPages[iPage]))
                                {
                                    case PGM_PAGE_STATE_ALLOCATED:
                                        /** @todo Optimize this: Don't always re-enable write
                                         * monitoring if the page is known to be very busy. */
                                        if (PGM_PAGE_IS_WRITTEN_TO(&pCur->aPages[iPage]))
                                        {
                                            Assert(paLSPages[iPage].fWriteMonitored);
                                            PGM_PAGE_CLEAR_WRITTEN_TO(&pCur->aPages[iPage]);
                                            Assert(pVM->pgm.s.cWrittenToPages > 0);
                                            pVM->pgm.s.cWrittenToPages--;
                                        }
                                        else
                                        {
                                            Assert(!paLSPages[iPage].fWriteMonitored);
                                            pVM->pgm.s.LiveSave.cMonitoredPages++;
                                        }

                                        if (!paLSPages[iPage].fDirty)
                                        {
                                            pVM->pgm.s.LiveSave.cDirtyPages++;
                                            pVM->pgm.s.LiveSave.cReadyPages--;
                                            if (++paLSPages[iPage].cDirtied > PGMLIVSAVEPAGE_MAX_DIRTIED)
                                                paLSPages[iPage].cDirtied = PGMLIVSAVEPAGE_MAX_DIRTIED;
                                        }

                                        PGM_PAGE_SET_STATE(&pCur->aPages[iPage], PGM_PAGE_STATE_WRITE_MONITORED);
                                        pVM->pgm.s.cMonitoredPages++;
                                        paLSPages[iPage].fWriteMonitored        = 1;
                                        paLSPages[iPage].fWriteMonitoredJustNow = 1;
                                        paLSPages[iPage].fDirty                 = 1;
                                        paLSPages[iPage].fZero                  = 0;
                                        paLSPages[iPage].fShared                = 0;
                                        break;

                                    case PGM_PAGE_STATE_WRITE_MONITORED:
                                        Assert(paLSPages[iPage].fWriteMonitored);
                                        paLSPages[iPage].fWriteMonitoredJustNow = 0;
                                        break;

                                    case PGM_PAGE_STATE_ZERO:
                                        if (!paLSPages[iPage].fZero)
                                        {
                                            paLSPages[iPage].fZero = 1;
                                            paLSPages[iPage].fShared = 0;
                                            if (!paLSPages[iPage].fDirty)
                                            {
                                                paLSPages[iPage].fDirty = 1;
                                                pVM->pgm.s.LiveSave.cReadyPages--;
                                                pVM->pgm.s.LiveSave.cDirtyPages++;
                                            }
                                        }
                                        break;

                                    case PGM_PAGE_STATE_SHARED:
                                        if (!paLSPages[iPage].fShared)
                                        {
                                            paLSPages[iPage].fZero = 0;
                                            paLSPages[iPage].fShared = 1;
                                            if (!paLSPages[iPage].fDirty)
                                            {
                                                paLSPages[iPage].fDirty = 1;
                                                pVM->pgm.s.LiveSave.cReadyPages--;
                                                pVM->pgm.s.LiveSave.cDirtyPages++;
                                            }
                                        }
                                        break;
                                }
                                break;
                            }

                            /* All writes to the shadow page are intercepted. */
                            case PGMPAGETYPE_ROM_SHADOW: /* (The shadow page is active.) */
                            case PGMPAGETYPE_ROM:        /* (The virgin page is active.) */
                            {
                                PPGMROMPAGE pRomPage = pgmR3GetRomPage(pVM, pCur->GCPhys + ((RTGCPHYS)iPage << PAGE_SHIFT));
                                if (!pRomPage->LiveSave.fDone)
                                {
                                    if (pRomPage->LiveSave.fWrittenTo)
                                    {
                                        pRomPage->LiveSave.fWrittenTo = false;
                                        if (!paLSPages[iPage].fDirty)
                                        {
                                            paLSPages[iPage].fDirty = 1;
                                            pVM->pgm.s.LiveSave.cReadyPages--;
                                            pVM->pgm.s.LiveSave.cDirtyPages++;
                                        }
                                        paLSPages[iPage].fWriteMonitoredJustNow = 1;
                                    }
                                    else
                                        paLSPages[iPage].fWriteMonitoredJustNow = 0;
                                    paLSPages[iPage].fWriteMonitored = 1;
                                }
                                else
                                {
                                    Assert(PGM_PAGE_GET_TYPE(&pCur->aPages[iPage]) == PGMPAGETYPE_ROM);
                                    Assert(!paLSPages[iPage].fDirty);
                                }
                                break;
                            }

                            default:
                                AssertMsgFailed(("%R[pgmpage]", &pCur->aPages[iPage]));
                            case PGMPAGETYPE_MMIO2:
                            case PGMPAGETYPE_MMIO2_ALIAS_MMIO:
                            case PGMPAGETYPE_MMIO:
                                if (PGM_PAGE_GET_TYPE(&pCur->aPages[iPage]) == PGMPAGETYPE_MMIO)
                                {
                                    paLSPages[iPage].fZero  = 0;
                                    paLSPages[iPage].fDirty = 1;
                                    paLSPages[iPage].fMmio  = 1;
                                }
                                else
                                {
                                    paLSPages[iPage].fZero  = 1;
                                    paLSPages[iPage].fDirty = 1;
                                    paLSPages[iPage].fMmio  = 1;
                                }
                                if (paLSPages[iPage].fWriteMonitored)
                                {
                                    if (RT_UNLIKELY(PGM_PAGE_GET_STATE(&pCur->aPages[iPage]) == PGM_PAGE_STATE_WRITE_MONITORED))
                                    {
                                        AssertMsgFailed(("%R[pgmpage]", &pCur->aPages[iPage])); /* shouldn't happen. */
                                        PGM_PAGE_SET_STATE(&pCur->aPages[iPage], PGM_PAGE_STATE_ALLOCATED);
                                        Assert(pVM->pgm.s.cMonitoredPages > 0);
                                        pVM->pgm.s.cMonitoredPages--;
                                    }
                                    if (PGM_PAGE_IS_WRITTEN_TO(&pCur->aPages[iPage]))
                                    {
                                        PGM_PAGE_CLEAR_WRITTEN_TO(&pCur->aPages[iPage]);
                                        Assert(pVM->pgm.s.cWrittenToPages > 0);
                                        pVM->pgm.s.cWrittenToPages--;
                                    }
                                    pVM->pgm.s.LiveSave.cMonitoredPages--;
                                }
                                pVM->pgm.s.LiveSave.cMmioPages++;
                                break;
                        }
                    }
                } /* for each page in range */

                if (GCPhysCur != 0)
                    break; /* Yield + ramrange change */
                GCPhysCur = pCur->GCPhysLast;
            }
        } /* for each range */
    } while (pCur);
    pgmUnlock(pVM);
}


/**
 * pgmR3LiveExec part 2: Save quiescent pages.
 *
 * @returns VBox status code.
 * @param   pVM     The VM handle.
 * @param   pSSM    The SSM handle.
 * @param   uPass   The pass.
 */
static int pgmR3LiveExecPart2(PVM pVM, PSSMHANDLE pSSM, uint32_t uPass)
{
    RTGCPHYS GCPhysLast = NIL_RTGCPHYS;
    RTGCPHYS GCPhysCur = 0;
    PPGMRAMRANGE pCur;
    pgmLock(pVM);
    do
    {
        uint32_t const  idRamRangesGen = pVM->pgm.s.idRamRangesGen;
        uint32_t        cSinceYield    = 0;
        for (pCur = pVM->pgm.s.pRamRangesR3; pCur; pCur = pCur->pNextR3)
        {
            if (pCur->GCPhysLast > GCPhysCur)
            {
                PPGMLIVESAVEPAGE paLSPages = pCur->paLSPages;
                uint32_t         cPages    = pCur->cb >> PAGE_SHIFT;
                uint32_t         iPage     = GCPhysCur <= pCur->GCPhys ? 0 : (GCPhysCur - pCur->GCPhys) >> PAGE_SHIFT;
                GCPhysCur = 0;
                for (; iPage < cPages; iPage++, cSinceYield++)
                {
                    /* Do yield first. */
                    if (    (cSinceYield & 0x7ff) == 0x7ff
                        &&  PDMR3CritSectYield(&pVM->pgm.s.CritSect)
                        &&  pVM->pgm.s.idRamRangesGen != idRamRangesGen)
                    {
                        GCPhysCur = pCur->GCPhys + ((RTGCPHYS)iPage << PAGE_SHIFT);
                        break; /* restart */
                    }

                    /*
                     * Save dirty pages that hasn't changed since part 1.
                     * (Use if instead of switch here so we can easily break out of the loop.)
                     */
                    if (   paLSPages[iPage].fDirty
                        && !paLSPages[iPage].fMmio
                        && !paLSPages[iPage].fWriteMonitoredJustNow
                        && (   (   PGM_PAGE_GET_TYPE(&pCur->aPages[iPage]) == PGMPAGETYPE_RAM
                                && PGM_PAGE_GET_STATE(&pCur->aPages[iPage])
                                   == (  paLSPages[iPage].fZero
                                       ? PGM_PAGE_STATE_ZERO
                                       : paLSPages[iPage].fShared
                                       ? PGM_PAGE_STATE_SHARED
                                       : PGM_PAGE_STATE_WRITE_MONITORED)
                               )
                            || PGM_PAGE_GET_TYPE(&pCur->aPages[iPage]) == PGMPAGETYPE_ROM
                            || PGM_PAGE_GET_TYPE(&pCur->aPages[iPage]) == PGMPAGETYPE_ROM_SHADOW
                           )
                       )
                    {
                        int         rc;
                        char        abPage[PAGE_SIZE];
                        RTGCPHYS    GCPhys = pCur->GCPhys + ((RTGCPHYS)iPage << PAGE_SHIFT);

                        if (   PGM_PAGE_GET_TYPE(&pCur->aPages[iPage]) == PGMPAGETYPE_RAM
                            && PGM_PAGE_GET_STATE(&pCur->aPages[iPage]) != PGM_PAGE_STATE_ZERO)
                        {
                            /*
                             * Copy the page and then save it outside the lock (since any
                             * SSM call may block).
                             */
                            void const *pvPage;
                            rc = pgmPhysGCPhys2CCPtrInternalReadOnly(pVM, &pCur->aPages[iPage], GCPhys, &pvPage);
                            if (RT_SUCCESS(rc))
                                memcpy(abPage, pvPage, PAGE_SIZE);
                            pgmUnlock(pVM);
                            AssertLogRelMsgRCReturn(rc, ("rc=%Rrc GCPhys=%RGp\n", rc, GCPhys), rc);

                            if (GCPhys == GCPhysLast + PAGE_SIZE)
                                SSMR3PutU8(pSSM, PGM_STATE_REC_RAW);
                            else
                            {
                                SSMR3PutU8(pSSM, PGM_STATE_REC_RAW | PGM_STATE_REC_FLAG_ADDR);
                                SSMR3PutGCPhys(pSSM, GCPhys);
                            }
                            rc = SSMR3PutMem(pSSM, abPage, PAGE_SIZE);
                        }
                        else if (   PGM_PAGE_GET_TYPE(&pCur->aPages[iPage]) == PGMPAGETYPE_RAM
                                 && PGM_PAGE_GET_STATE(&pCur->aPages[iPage]) == PGM_PAGE_STATE_ZERO)
                        {
                            /*
                             * Dirty zero page.
                             */
                            pgmUnlock(pVM);

                            if (GCPhys == GCPhysLast + PAGE_SIZE)
                                rc = SSMR3PutU8(pSSM, PGM_STATE_REC_RAW);
                            else
                            {
                                SSMR3PutU8(pSSM, PGM_STATE_REC_RAW | PGM_STATE_REC_FLAG_ADDR);
                                rc = SSMR3PutGCPhys(pSSM, GCPhys);
                            }
                        }
                        else
                        {
                            /*
                             * Dirty shadow ROM page.
                             */
                            Assert(   PGM_PAGE_GET_TYPE(&pCur->aPages[iPage]) == PGMPAGETYPE_RAM
                                   || PGM_PAGE_GET_TYPE(&pCur->aPages[iPage]) == PGMPAGETYPE_ROM_SHADOW);
                            PPGMROMPAGE pRomPage = pgmR3GetRomPage(pVM, pCur->GCPhys + ((RTGCPHYS)iPage << PAGE_SHIFT));
                            if (pRomPage->LiveSave.fWrittenTo)
                                continue; /* modified already, skip it. */

                            PGMROMPROT  enmProt = pRomPage->enmProt;
                            PPGMPAGE    pPage = PGMROMPROT_IS_ROM(enmProt) ? &pRomPage->Shadow : &pCur->aPages[iPage];
                            bool        fZero = PGM_PAGE_IS_ZERO(pPage);
                            if (!fZero)
                            {
                                void *pvPage;
                                PPGMPAGEMAP pMapIgnored;
                                rc = pgmPhysPageMap(pVM, pPage, GCPhys, &pMapIgnored, &pvPage);
                                if (RT_SUCCESS(rc))
                                    memcpy(abPage, pvPage, PAGE_SIZE);
                            }
                            pRomPage->LiveSave.u8Prot = (uint8_t)enmProt;
                            pgmUnlock(pVM);
                            AssertLogRelMsgRCReturn(rc, ("rc=%Rrc GCPhys=%RGp\n", rc, GCPhys), rc);

                            if (GCPhys == GCPhysLast + PAGE_SIZE)
                                SSMR3PutU8(pSSM, (fZero ? PGM_STATE_REC_ROM_SHADOW_ZERO : PGM_STATE_REC_ROM_SHADOW));
                            else
                            {
                                SSMR3PutU8(pSSM, (fZero ? PGM_STATE_REC_ROM_SHADOW_ZERO : PGM_STATE_REC_ROM_SHADOW) | PGM_STATE_REC_FLAG_ADDR);
                                SSMR3PutGCPhys(pSSM, GCPhys);
                            }
                            rc = SSMR3PutU8(pSSM, (uint8_t)enmProt);
                            if (!fZero)
                                rc = SSMR3PutMem(pSSM, abPage, PAGE_SIZE);
                        }

                        /* common tail */
                        if (RT_FAILURE(rc))
                            return rc;

                        pgmLock(pVM);
                        GCPhysLast = GCPhys;
                        paLSPages[iPage].fDirty = 0;
                        if (idRamRangesGen != pVM->pgm.s.idRamRangesGen)
                        {
                            GCPhysCur = GCPhys | PAGE_OFFSET_MASK;
                            break; /* restart */
                        }
                    }
                } /* for each page in range */

                if (GCPhysCur != 0)
                    break; /* Yield + ramrange change */
                GCPhysCur = pCur->GCPhysLast;
            }
        } /* for each range */
    } while (pCur);
    pgmUnlock(pVM);

    return VINF_SUCCESS;
}


/**
 * Execute a live save pass.
 *
 * @returns VBox status code.
 *
 * @param   pVM         The VM handle.
 * @param   pSSM        The SSM handle.
 */
static DECLCALLBACK(int)  pgmR3LiveExec(PVM pVM, PSSMHANDLE pSSM, uint32_t uPass)
{
    if (uPass == 0)
    {
        int rc = pgmR3LiveExecSaveVirginRomPages(pVM, pSSM);
        if (RT_FAILURE(rc))
            return rc;
    }
    pgmR3LiveExecPart1(pVM);
    pgmR3PoolClearAll(pVM); /** @todo this could perhaps be optimized a bit. */
    return pgmR3LiveExecPart2(pVM, pSSM, uPass);
}


/**
 * Votes on whether the live save phase is done or not.
 *
 * @returns VBox status code.
 *
 * @param   pVM         The VM handle.
 * @param   pSSM        The SSM handle.
 */
static DECLCALLBACK(int)  pgmR3LiveVote(PVM pVM, PSSMHANDLE pSSM)
{
    return VINF_SUCCESS;
}


/**
 * Execute state save operation.
 *
 * @returns VBox status code.
 * @param   pVM             VM Handle.
 * @param   pSSM            SSM operation handle.
 */
static DECLCALLBACK(int) pgmR3SaveExec(PVM pVM, PSSMHANDLE pSSM)
{
    int         rc;
    unsigned    i;
    PPGM        pPGM = &pVM->pgm.s;

    /*
     * Lock PGM and set the no-more-writes indicator.
     */
    pgmLock(pVM);
    pVM->pgm.s.fNoMorePhysWrites = true;

    /*
     * Save basic data (required / unaffected by relocation).
     */
    SSMR3PutStruct(pSSM, pPGM, &s_aPGMFields[0]);

    for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
    {
        PVMCPU pVCpu = &pVM->aCpus[idCpu];
        SSMR3PutStruct(pSSM, &pVCpu->pgm.s, &s_aPGMCpuFields[0]);
    }

    /*
     * The guest mappings.
     */
    i = 0;
    for (PPGMMAPPING pMapping = pPGM->pMappingsR3; pMapping; pMapping = pMapping->pNextR3, i++)
    {
        SSMR3PutU32(      pSSM, i);
        SSMR3PutStrZ(     pSSM, pMapping->pszDesc); /* This is the best unique id we have... */
        SSMR3PutGCPtr(    pSSM, pMapping->GCPtr);
        SSMR3PutGCUIntPtr(pSSM, pMapping->cPTs);
    }
    rc = SSMR3PutU32(pSSM, ~0); /* terminator. */

    /*
     * Ram ranges and the memory they describe.
     */
    i = 0;
    for (PPGMRAMRANGE pRam = pPGM->pRamRangesR3; pRam; pRam = pRam->pNextR3, i++)
    {
        /*
         * Save the ram range details.
         */
        SSMR3PutU32(pSSM,       i);
        SSMR3PutGCPhys(pSSM,    pRam->GCPhys);
        SSMR3PutGCPhys(pSSM,    pRam->GCPhysLast);
        SSMR3PutGCPhys(pSSM,    pRam->cb);
        SSMR3PutU8(pSSM,        !!pRam->pvR3);      /* Boolean indicating memory or not. */
        SSMR3PutStrZ(pSSM,      pRam->pszDesc);     /* This is the best unique id we have... */

        /*
         * Iterate the pages, only two special case.
         */
        uint32_t const cPages = pRam->cb >> PAGE_SHIFT;
        for (uint32_t iPage = 0; iPage < cPages; iPage++)
        {
            RTGCPHYS GCPhysPage = pRam->GCPhys + ((RTGCPHYS)iPage << PAGE_SHIFT);
            PPGMPAGE pPage      = &pRam->aPages[iPage];
            uint8_t  uType      = PGM_PAGE_GET_TYPE(pPage);

            if (uType == PGMPAGETYPE_ROM_SHADOW)
                rc = pgmR3SaveShadowedRomPage(pVM, pSSM, pPage, GCPhysPage, pRam);
            else if (uType == PGMPAGETYPE_MMIO2_ALIAS_MMIO)
            {
                /* MMIO2 alias -> MMIO; the device will just have to deal with this. */
                SSMR3PutU8(pSSM, PGMPAGETYPE_MMIO);
                rc = SSMR3PutU8(pSSM, 0 /* ZERO */);
            }
            else
            {
                SSMR3PutU8(pSSM, uType);
                rc = pgmR3SavePage(pVM, pSSM, pPage, GCPhysPage, pRam);
            }
            if (RT_FAILURE(rc))
                break;
        }
        if (RT_FAILURE(rc))
            break;
    }

    pgmUnlock(pVM);
    return SSMR3PutU32(pSSM, ~0); /* terminator. */
}


/**
 * Cleans up after an save state operation.
 *
 * @returns VBox status code.
 * @param   pVM             VM Handle.
 * @param   pSSM            SSM operation handle.
 */
static DECLCALLBACK(int) pgmR3SaveDone(PVM pVM, PSSMHANDLE pSSM)
{
    /*
     * Free the tracking arrays and disable write monitoring.
     *
     * Play nice with the PGM lock in case we're called while the VM is still
     * running.  This means we have to delay the freeing since we wish to use
     * paLSPages as an indicator of which RAM ranges which we need to scan for
     * write monitored pages.
     */
    void *pvToFree = NULL;
    PPGMRAMRANGE pCur;
    uint32_t cMonitoredPages = 0;
    pgmLock(pVM);
    do
    {
        for (pCur = pVM->pgm.s.pRamRangesR3; pCur; pCur = pCur->pNextR3)
        {
            if (pCur->paLSPages)
            {
                if (pvToFree)
                {
                    uint32_t idRamRangesGen = pVM->pgm.s.idRamRangesGen;
                    pgmUnlock(pVM);
                    MMR3HeapFree(pvToFree);
                    pvToFree = NULL;
                    pgmLock(pVM);
                    if (idRamRangesGen != pVM->pgm.s.idRamRangesGen)
                        break;          /* start over again. */
                }

                pvToFree = pCur->paLSPages;
                pCur->paLSPages = NULL;

                uint32_t iPage = pCur->cb >> PAGE_SHIFT;
                while (iPage--)
                {
                    PPGMPAGE pPage = &pCur->aPages[iPage];
                    PGM_PAGE_CLEAR_WRITTEN_TO(pPage);
                    if (PGM_PAGE_GET_STATE(pPage) == PGM_PAGE_STATE_WRITE_MONITORED)
                    {
                        PGM_PAGE_SET_STATE(pPage, PGM_PAGE_STATE_ALLOCATED);
                        cMonitoredPages++;
                    }
                }
            }
        }
    } while (pCur);

    /** @todo this is blindly assuming that we're the only user of write
     *        monitoring. Fix this when more users are added. */
    pVM->pgm.s.fPhysWriteMonitoringEngaged = false;
    pgmUnlock(pVM);

    MMR3HeapFree(pvToFree);
    pvToFree = NULL;

    return VINF_SUCCESS;
}


/**
 * Worker for pgmR3Load and pgmR3LoadLocked.
 *
 * @returns VBox status code.
 *
 * @param   pVM                 The VM handle.
 * @param   pSSM                The SSM handle.
 * @param   uVersion            The saved state version.
 */
static int pgmR3LoadLockedMemory(PVM pVM, PSSMHANDLE pSSM, uint32_t uPass)
{
    /*
     * Process page records until we hit the terminator.
     */
    PPGMRAMRANGE    pRamHint   = NULL;
    RTGCPHYS        GCPhysLast = NIL_RTGCPHYS;
    for (;;)
    {
        /* Get the record type and flags. */
        uint8_t u8;
        int rc = SSMR3GetU8(pSSM, &u8);
        if (RT_FAILURE(rc))
            return rc;
        if (u8 == PGM_STATE_REC_END)
            return VINF_SUCCESS;
        AssertLogRelMsgReturn((u8 & ~PGM_STATE_REC_FLAG_ADDR) <= PGM_STATE_REC_LAST, ("%#x\n", u8), VERR_SSM_DATA_UNIT_FORMAT_CHANGED);

        /* Get the address. */
        RTGCPHYS GCPhys;
        if (!(u8 & PGM_STATE_REC_FLAG_ADDR))
        {
            AssertLogRelReturn(GCPhysLast != NIL_RTGCPHYS, VERR_SSM_DATA_UNIT_FORMAT_CHANGED);
            GCPhys = GCPhysLast + PAGE_SIZE;
        }
        else
        {
            rc = SSMR3GetGCPhys(pSSM, &GCPhys);
            if (RT_FAILURE(rc))
                return rc;
            AssertLogRelMsgReturn(GCPhys & PAGE_OFFSET_MASK, ("%RGp\n", GCPhys), VERR_SSM_DATA_UNIT_FORMAT_CHANGED);
        }

        /* Get the ram range and page. */
        PPGMPAGE pPage;
        rc = pgmPhysGetPageWithHintEx(&pVM->pgm.s, GCPhys, &pPage, &pRamHint);
        AssertLogRelMsgRCReturn(rc, ("rc=%Rrc %RGp\n", rc, GCPhys), rc);

        /*
         * Take action according to the record type.
         */
        switch (u8 & ~PGM_STATE_REC_FLAG_ADDR)
        {
            case PGM_STATE_REC_ZERO:
            {
                if (PGM_PAGE_IS_ZERO(pPage))
                    break;
                /** @todo implement zero page replacing. */
                AssertLogRelMsgReturn(PGM_PAGE_GET_STATE(pPage) == PGM_PAGE_STATE_ALLOCATED, ("GCPhys=%RGp %R[pgmpage]\n", GCPhys, pPage), VERR_INTERNAL_ERROR_5);
                void *pvDstPage;
                rc = pgmPhysGCPhys2CCPtrInternal(pVM, pPage, GCPhys, &pvDstPage);
                AssertLogRelMsgRCReturn(rc, ("GCPhys=%RGp %R[pgmpage] rc=%Rrc\n", GCPhys, pPage, rc), rc);
                ASMMemZeroPage(pvDstPage);
                break;
            }

            case PGM_STATE_REC_RAW:
            {
                void *pvDstPage;
                rc = pgmPhysGCPhys2CCPtrInternal(pVM, pPage, GCPhys, &pvDstPage);
                AssertLogRelMsgRCReturn(rc, ("GCPhys=%RGp %R[pgmpage] rc=%Rrc\n", GCPhys, pPage, rc), rc);
                rc = SSMR3GetMem(pSSM, pvDstPage, PAGE_SIZE);
                if (RT_FAILURE(rc))
                    return rc;
                break;
            }

            case PGM_STATE_REC_ROM_VIRGIN:
            case PGM_STATE_REC_ROM_SHADOW:
            case PGM_STATE_REC_ROM_SHADOW_ZERO:
            case PGM_STATE_REC_ROM_PROT:
            {
                PPGMROMPAGE pRomPage = pgmR3GetRomPage(pVM, GCPhys);
                AssertLogRelMsgReturn(pRomPage, ("GCPhys=%RGp\n", GCPhys), VERR_INTERNAL_ERROR);

                uint8_t u8Prot;
                rc = SSMR3GetU8(pSSM, &u8Prot);
                if (RT_FAILURE(rc))
                    return rc;
                PGMROMPROT enmProt = (PGMROMPROT)u8Prot;
                AssertLogRelMsgReturn(enmProt > PGMROMPROT_INVALID && enmProt < PGMROMPROT_END, ("GCPhys=%RGp enmProt=%d\n", GCPhys, enmProt), VERR_INTERNAL_ERROR);

                /* Make the protection change. */
                if (enmProt != pRomPage->enmProt)
                {
                    rc = PGMR3PhysRomProtect(pVM, GCPhys, PAGE_SIZE, enmProt);
                    AssertLogRelMsgRCReturn(rc, ("GCPhys=%RGp rc=%Rrc\n", GCPhys, rc), rc);
                    AssertLogRelReturn(pRomPage->enmProt == enmProt, VERR_INTERNAL_ERROR);
                }

                /* Get the right page descriptor. */
                PPGMPAGE pRealPage;
                switch (u8 & ~PGM_STATE_REC_FLAG_ADDR)
                {
                    case PGM_STATE_REC_ROM_VIRGIN:
                        if (!PGMROMPROT_IS_ROM(enmProt))
                            pRealPage = &pRomPage->Virgin;
                        else
                            pRealPage = pPage;
                        break;

                    case PGM_STATE_REC_ROM_SHADOW:
                    case PGM_STATE_REC_ROM_SHADOW_ZERO:
                        if (PGMROMPROT_IS_ROM(enmProt))
                            pRealPage = &pRomPage->Shadow;
                        else
                            pRealPage = pPage;
                        break;

                    case PGM_STATE_REC_ROM_PROT:
                        pRealPage = NULL;
                        break;

                    default:
                        AssertMsgFailedReturn(("%#x\n", u8), VERR_INTERNAL_ERROR);
                }

                /* Map it if necessary. */
                void *pvDstPage = NULL;
                switch (u8 & ~PGM_STATE_REC_FLAG_ADDR)
                {
                    case PGM_STATE_REC_ROM_SHADOW_ZERO:
                        if (PGM_PAGE_IS_ZERO(pRealPage))
                            break;
                        /** @todo implement zero page replacing. */
                        /* fall thru */
                    case PGM_STATE_REC_ROM_VIRGIN:
                    case PGM_STATE_REC_ROM_SHADOW:
                    {
                        if (RT_UNLIKELY(PGM_PAGE_GET_STATE(pRealPage) != PGM_PAGE_STATE_ALLOCATED))
                        {
                            rc = pgmPhysPageMakeWritable(pVM, pRealPage, GCPhys);
                            AssertLogRelMsgRCReturn(rc, ("GCPhys=%RGp rc=%Rrc\n", GCPhys, rc), rc);
                        }
                        PPGMPAGEMAP pMapIgnored;
                        rc = pgmPhysPageMap(pVM, pRealPage, GCPhys, &pMapIgnored, &pvDstPage);
                        AssertLogRelMsgRCReturn(rc, ("GCPhys=%RGp rc=%Rrc\n", GCPhys, rc), rc);
                        break;
                    }

                    case PGM_STATE_REC_ROM_PROT:
                        break;
                }

                /* Load the bits. */
                switch (u8 & ~PGM_STATE_REC_FLAG_ADDR)
                {
                    case PGM_STATE_REC_ROM_SHADOW_ZERO:
                        if (pvDstPage)
                            ASMMemZeroPage(pvDstPage);
                        break;

                    case PGM_STATE_REC_ROM_VIRGIN:
                    case PGM_STATE_REC_ROM_SHADOW:
                    {
                        rc = SSMR3GetMem(pSSM, pvDstPage, PAGE_SIZE);
                        if (RT_FAILURE(rc))
                            return rc;
                        break;
                    }

                    case PGM_STATE_REC_ROM_PROT:
                        break;
                }
                break;
            }

            default:
                AssertMsgFailedReturn(("%#x\n", u8), VERR_INTERNAL_ERROR);
        }
    }
}


/**
 * Load an ignored page.
 *
 * @returns VBox status code.
 * @param   pSSM            The saved state handle.
 */
static int pgmR3LoadPageToDevNull(PSSMHANDLE pSSM)
{
    uint8_t abPage[PAGE_SIZE];
    return SSMR3GetMem(pSSM, &abPage[0], sizeof(abPage));
}


/**
 * Loads a page without any bits in the saved state, i.e. making sure it's
 * really zero.
 *
 * @returns VBox status code.
 * @param   pVM             The VM handle.
 * @param   uType           The page type or PGMPAGETYPE_INVALID (old saved
 *                          state).
 * @param   pPage           The guest page tracking structure.
 * @param   GCPhys          The page address.
 * @param   pRam            The ram range (logging).
 */
static int pgmR3LoadPageZero(PVM pVM, uint8_t uType, PPGMPAGE pPage, RTGCPHYS GCPhys, PPGMRAMRANGE pRam)
{
    if (    PGM_PAGE_GET_TYPE(pPage) != uType
        &&  uType != PGMPAGETYPE_INVALID)
        return VERR_SSM_UNEXPECTED_DATA;

    /* I think this should be sufficient. */
    if (!PGM_PAGE_IS_ZERO(pPage))
        return VERR_SSM_UNEXPECTED_DATA;

    NOREF(pVM);
    NOREF(GCPhys);
    NOREF(pRam);
    return VINF_SUCCESS;
}


/**
 * Loads a page from the saved state.
 *
 * @returns VBox status code.
 * @param   pVM             The VM handle.
 * @param   pSSM            The SSM handle.
 * @param   uType           The page type or PGMPAGETYEP_INVALID (old saved
 *                          state).
 * @param   pPage           The guest page tracking structure.
 * @param   GCPhys          The page address.
 * @param   pRam            The ram range (logging).
 */
static int pgmR3LoadPageBits(PVM pVM, PSSMHANDLE pSSM, uint8_t uType, PPGMPAGE pPage, RTGCPHYS GCPhys, PPGMRAMRANGE pRam)
{
    int rc;

    /*
     * Match up the type, dealing with MMIO2 aliases (dropped).
     */
    AssertLogRelMsgReturn(   PGM_PAGE_GET_TYPE(pPage) == uType
                          || uType == PGMPAGETYPE_INVALID,
                          ("pPage=%R[pgmpage] GCPhys=%#x %s\n", pPage, GCPhys, pRam->pszDesc),
                          VERR_SSM_UNEXPECTED_DATA);

    /*
     * Load the page.
     */
    void *pvPage;
    rc = pgmPhysGCPhys2CCPtrInternal(pVM, pPage, GCPhys, &pvPage);
    if (RT_SUCCESS(rc))
        rc = SSMR3GetMem(pSSM, pvPage, PAGE_SIZE);

    return rc;
}


/**
 * Loads a page (counter part to pgmR3SavePage).
 *
 * @returns VBox status code, fully bitched errors.
 * @param   pVM             The VM handle.
 * @param   pSSM            The SSM handle.
 * @param   uType           The page type.
 * @param   pPage           The page.
 * @param   GCPhys          The page address.
 * @param   pRam            The RAM range (for error messages).
 */
static int pgmR3LoadPage(PVM pVM, PSSMHANDLE pSSM, uint8_t uType, PPGMPAGE pPage, RTGCPHYS GCPhys, PPGMRAMRANGE pRam)
{
    uint8_t         uState;
    int rc = SSMR3GetU8(pSSM, &uState);
    AssertLogRelMsgRCReturn(rc, ("pPage=%R[pgmpage] GCPhys=%#x %s rc=%Rrc\n", pPage, GCPhys, pRam->pszDesc, rc), rc);
    if (uState == 0 /* zero */)
        rc = pgmR3LoadPageZero(pVM, uType, pPage, GCPhys, pRam);
    else if (uState == 1)
        rc = pgmR3LoadPageBits(pVM, pSSM, uType, pPage, GCPhys, pRam);
    else
        rc = VERR_INTERNAL_ERROR;
    AssertLogRelMsgRCReturn(rc, ("pPage=%R[pgmpage] uState=%d uType=%d GCPhys=%RGp %s rc=%Rrc\n",
                                 pPage, uState, uType, GCPhys, pRam->pszDesc, rc),
                            rc);
    return VINF_SUCCESS;
}


/**
 * Loads a shadowed ROM page.
 *
 * @returns VBox status code, errors are fully bitched.
 * @param   pVM             The VM handle.
 * @param   pSSM            The saved state handle.
 * @param   pPage           The page.
 * @param   GCPhys          The page address.
 * @param   pRam            The RAM range (for error messages).
 */
static int pgmR3LoadShadowedRomPage(PVM pVM, PSSMHANDLE pSSM, PPGMPAGE pPage, RTGCPHYS GCPhys, PPGMRAMRANGE pRam)
{
    /*
     * Load and set the protection first, then load the two pages, the first
     * one is the active the other is the passive.
     */
    PPGMROMPAGE pRomPage = pgmR3GetRomPage(pVM, GCPhys);
    AssertLogRelMsgReturn(pRomPage, ("GCPhys=%RGp %s\n", GCPhys, pRam->pszDesc), VERR_INTERNAL_ERROR);

    uint8_t     uProt;
    int rc = SSMR3GetU8(pSSM, &uProt);
    AssertLogRelMsgRCReturn(rc, ("pPage=%R[pgmpage] GCPhys=%#x %s\n", pPage, GCPhys, pRam->pszDesc), rc);
    PGMROMPROT  enmProt = (PGMROMPROT)uProt;
    AssertLogRelMsgReturn(    enmProt >= PGMROMPROT_INVALID
                          &&  enmProt <  PGMROMPROT_END,
                          ("enmProt=%d pPage=%R[pgmpage] GCPhys=%#x %s\n", enmProt, pPage, GCPhys, pRam->pszDesc),
                          VERR_SSM_UNEXPECTED_DATA);

    if (pRomPage->enmProt != enmProt)
    {
        rc = PGMR3PhysRomProtect(pVM, GCPhys, PAGE_SIZE, enmProt);
        AssertLogRelRCReturn(rc, rc);
        AssertLogRelReturn(pRomPage->enmProt == enmProt, VERR_INTERNAL_ERROR);
    }

    PPGMPAGE pPageActive  = PGMROMPROT_IS_ROM(enmProt) ? &pRomPage->Virgin      : &pRomPage->Shadow;
    PPGMPAGE pPagePassive = PGMROMPROT_IS_ROM(enmProt) ? &pRomPage->Shadow      : &pRomPage->Virgin;
    uint8_t  u8ActiveType = PGMROMPROT_IS_ROM(enmProt) ? PGMPAGETYPE_ROM        : PGMPAGETYPE_ROM_SHADOW;
    uint8_t  u8PassiveType= PGMROMPROT_IS_ROM(enmProt) ? PGMPAGETYPE_ROM_SHADOW : PGMPAGETYPE_ROM;

    rc = pgmR3LoadPage(pVM, pSSM, u8ActiveType, pPage, GCPhys, pRam);
    if (RT_SUCCESS(rc))
    {
        *pPageActive = *pPage;
        rc = pgmR3LoadPage(pVM, pSSM, u8PassiveType, pPagePassive, GCPhys, pRam);
    }
    return rc;
}

/**
 * Ram range flags and bits for older versions of the saved state.
 *
 * @returns VBox status code.
 *
 * @param   pVM         The VM handle
 * @param   pSSM        The SSM handle.
 * @param   uVersion    The saved state version.
 */
static int pgmR3LoadLockedMemoryOld(PVM pVM, PSSMHANDLE pSSM, uint32_t uVersion)
{
    PPGM pPGM = &pVM->pgm.s;

    /*
     * Ram range flags and bits.
     */
    uint32_t i = 0;
    for (PPGMRAMRANGE pRam = pPGM->pRamRangesR3; ; pRam = pRam->pNextR3, i++)
    {
        /* Check the seqence number / separator. */
        uint32_t u32Sep;
        int rc = SSMR3GetU32(pSSM, &u32Sep);
        if (RT_FAILURE(rc))
            return rc;
        if (u32Sep == ~0U)
            break;
        if (u32Sep != i)
        {
            AssertMsgFailed(("u32Sep=%#x (last)\n", u32Sep));
            return VERR_SSM_DATA_UNIT_FORMAT_CHANGED;
        }
        AssertLogRelReturn(pRam, VERR_SSM_DATA_UNIT_FORMAT_CHANGED);

        /* Get the range details. */
        RTGCPHYS GCPhys;
        SSMR3GetGCPhys(pSSM, &GCPhys);
        RTGCPHYS GCPhysLast;
        SSMR3GetGCPhys(pSSM, &GCPhysLast);
        RTGCPHYS cb;
        SSMR3GetGCPhys(pSSM, &cb);
        uint8_t     fHaveBits;
        rc = SSMR3GetU8(pSSM, &fHaveBits);
        if (RT_FAILURE(rc))
            return rc;
        if (fHaveBits & ~1)
        {
            AssertMsgFailed(("u32Sep=%#x (last)\n", u32Sep));
            return VERR_SSM_DATA_UNIT_FORMAT_CHANGED;
        }
        size_t  cchDesc = 0;
        char    szDesc[256];
        szDesc[0] = '\0';
        if (uVersion >= PGM_SAVED_STATE_VERSION_RR_DESC)
        {
            rc = SSMR3GetStrZ(pSSM, szDesc, sizeof(szDesc));
            if (RT_FAILURE(rc))
                return rc;
            /* Since we've modified the description strings in r45878, only compare
               them if the saved state is more recent. */
            if (uVersion != PGM_SAVED_STATE_VERSION_RR_DESC)
                cchDesc = strlen(szDesc);
        }

        /*
         * Match it up with the current range.
         *
         * Note there is a hack for dealing with the high BIOS mapping
         * in the old saved state format, this means we might not have
         * a 1:1 match on success.
         */
        if (    (   GCPhys     != pRam->GCPhys
                 || GCPhysLast != pRam->GCPhysLast
                 || cb         != pRam->cb
                 ||  (   cchDesc
                      && strcmp(szDesc, pRam->pszDesc)) )
                /* Hack for PDMDevHlpPhysReserve(pDevIns, 0xfff80000, 0x80000, "High ROM Region"); */
            &&  (   uVersion != PGM_SAVED_STATE_VERSION_OLD_PHYS_CODE
                 || GCPhys     != UINT32_C(0xfff80000)
                 || GCPhysLast != UINT32_C(0xffffffff)
                 || pRam->GCPhysLast != GCPhysLast
                 || pRam->GCPhys     <  GCPhys
                 || !fHaveBits)
           )
        {
            LogRel(("Ram range: %RGp-%RGp %RGp bytes %s %s\n"
                    "State    : %RGp-%RGp %RGp bytes %s %s\n",
                    pRam->GCPhys, pRam->GCPhysLast, pRam->cb, pRam->pvR3 ? "bits" : "nobits", pRam->pszDesc,
                    GCPhys, GCPhysLast, cb, fHaveBits ? "bits" : "nobits", szDesc));
            /*
             * If we're loading a state for debugging purpose, don't make a fuss if
             * the MMIO and ROM stuff isn't 100% right, just skip the mismatches.
             */
            if (    SSMR3HandleGetAfter(pSSM) != SSMAFTER_DEBUG_IT
                ||  GCPhys < 8 * _1M)
                AssertFailedReturn(VERR_SSM_LOAD_CONFIG_MISMATCH);

            AssertMsgFailed(("debug skipping not implemented, sorry\n"));
            continue;
        }

        uint32_t cPages = (GCPhysLast - GCPhys + 1) >> PAGE_SHIFT;
        if (uVersion >= PGM_SAVED_STATE_VERSION_RR_DESC)
        {
            /*
             * Load the pages one by one.
             */
            for (uint32_t iPage = 0; iPage < cPages; iPage++)
            {
                RTGCPHYS const  GCPhysPage = ((RTGCPHYS)iPage << PAGE_SHIFT) + pRam->GCPhys;
                PPGMPAGE        pPage      = &pRam->aPages[iPage];
                uint8_t         uType;
                rc = SSMR3GetU8(pSSM, &uType);
                AssertLogRelMsgRCReturn(rc, ("pPage=%R[pgmpage] iPage=%#x GCPhysPage=%#x %s\n", pPage, iPage, GCPhysPage, pRam->pszDesc), rc);
                if (uType == PGMPAGETYPE_ROM_SHADOW)
                    rc = pgmR3LoadShadowedRomPage(pVM, pSSM, pPage, GCPhysPage, pRam);
                else
                    rc = pgmR3LoadPage(pVM, pSSM, uType, pPage, GCPhysPage, pRam);
                AssertLogRelMsgRCReturn(rc, ("rc=%Rrc iPage=%#x GCPhysPage=%#x %s\n", rc, iPage, GCPhysPage, pRam->pszDesc), rc);
            }
        }
        else
        {
            /*
             * Old format.
             */
            AssertLogRelReturn(!pVM->pgm.s.fRamPreAlloc, VERR_NOT_SUPPORTED); /* can't be detected. */

            /* Of the page flags, pick up MMIO2 and ROM/RESERVED for the !fHaveBits case.
               The rest is generally irrelevant and wrong since the stuff have to match registrations. */
            uint32_t fFlags = 0;
            for (uint32_t iPage = 0; iPage < cPages; iPage++)
            {
                uint16_t u16Flags;
                rc = SSMR3GetU16(pSSM, &u16Flags);
                AssertLogRelMsgRCReturn(rc, ("rc=%Rrc iPage=%#x GCPhys=%#x %s\n", rc, iPage, pRam->GCPhys, pRam->pszDesc), rc);
                fFlags |= u16Flags;
            }

            /* Load the bits */
            if (    !fHaveBits
                &&  GCPhysLast < UINT32_C(0xe0000000))
            {
                /*
                 * Dynamic chunks.
                 */
                const uint32_t cPagesInChunk = (1*1024*1024) >> PAGE_SHIFT;
                AssertLogRelMsgReturn(cPages % cPagesInChunk == 0,
                                      ("cPages=%#x cPagesInChunk=%#x\n", cPages, cPagesInChunk, pRam->GCPhys, pRam->pszDesc),
                                      VERR_SSM_DATA_UNIT_FORMAT_CHANGED);

                for (uint32_t iPage = 0; iPage < cPages; /* incremented by inner loop */ )
                {
                    uint8_t fPresent;
                    rc = SSMR3GetU8(pSSM, &fPresent);
                    AssertLogRelMsgRCReturn(rc, ("rc=%Rrc iPage=%#x GCPhys=%#x %s\n", rc, iPage, pRam->GCPhys, pRam->pszDesc), rc);
                    AssertLogRelMsgReturn(fPresent == (uint8_t)true || fPresent == (uint8_t)false,
                                          ("fPresent=%#x iPage=%#x GCPhys=%#x %s\n", fPresent, iPage, pRam->GCPhys, pRam->pszDesc),
                                          VERR_SSM_DATA_UNIT_FORMAT_CHANGED);

                    for (uint32_t iChunkPage = 0; iChunkPage < cPagesInChunk; iChunkPage++, iPage++)
                    {
                        RTGCPHYS const  GCPhysPage = ((RTGCPHYS)iPage << PAGE_SHIFT) + pRam->GCPhys;
                        PPGMPAGE        pPage      = &pRam->aPages[iPage];
                        if (fPresent)
                        {
                            if (PGM_PAGE_GET_TYPE(pPage) == PGMPAGETYPE_MMIO)
                                rc = pgmR3LoadPageToDevNull(pSSM);
                            else
                                rc = pgmR3LoadPageBits(pVM, pSSM, PGMPAGETYPE_INVALID, pPage, GCPhysPage, pRam);
                        }
                        else
                            rc = pgmR3LoadPageZero(pVM, PGMPAGETYPE_INVALID, pPage, GCPhysPage, pRam);
                        AssertLogRelMsgRCReturn(rc, ("rc=%Rrc iPage=%#x GCPhysPage=%#x %s\n", rc, iPage, GCPhysPage, pRam->pszDesc), rc);
                    }
                }
            }
            else if (pRam->pvR3)
            {
                /*
                 * MMIO2.
                 */
                AssertLogRelMsgReturn((fFlags & 0x0f) == RT_BIT(3) /*MM_RAM_FLAGS_MMIO2*/,
                                      ("fFlags=%#x GCPhys=%#x %s\n", fFlags, pRam->GCPhys, pRam->pszDesc),
                                      VERR_SSM_DATA_UNIT_FORMAT_CHANGED);
                AssertLogRelMsgReturn(pRam->pvR3,
                                      ("GCPhys=%#x %s\n", pRam->GCPhys, pRam->pszDesc),
                                      VERR_SSM_DATA_UNIT_FORMAT_CHANGED);

                rc = SSMR3GetMem(pSSM, pRam->pvR3, pRam->cb);
                AssertLogRelMsgRCReturn(rc, ("GCPhys=%#x %s\n", pRam->GCPhys, pRam->pszDesc), rc);
            }
            else if (GCPhysLast < UINT32_C(0xfff80000))
            {
                /*
                 * PCI MMIO, no pages saved.
                 */
            }
            else
            {
                /*
                 * Load the 0xfff80000..0xffffffff BIOS range.
                 * It starts with X reserved pages that we have to skip over since
                 * the RAMRANGE create by the new code won't include those.
                 */
                AssertLogRelMsgReturn(   !(fFlags & RT_BIT(3) /*MM_RAM_FLAGS_MMIO2*/)
                                      && (fFlags  & RT_BIT(0) /*MM_RAM_FLAGS_RESERVED*/),
                                      ("fFlags=%#x GCPhys=%#x %s\n", fFlags, pRam->GCPhys, pRam->pszDesc),
                                      VERR_SSM_DATA_UNIT_FORMAT_CHANGED);
                AssertLogRelMsgReturn(GCPhys == UINT32_C(0xfff80000),
                                      ("GCPhys=%RGp pRamRange{GCPhys=%#x %s}\n", GCPhys, pRam->GCPhys, pRam->pszDesc),
                                      VERR_SSM_DATA_UNIT_FORMAT_CHANGED);

                /* Skip wasted reserved pages before the ROM. */
                while (GCPhys < pRam->GCPhys)
                {
                    rc = pgmR3LoadPageToDevNull(pSSM);
                    GCPhys += PAGE_SIZE;
                }

                /* Load the bios pages. */
                cPages = pRam->cb >> PAGE_SHIFT;
                for (uint32_t iPage = 0; iPage < cPages; iPage++)
                {
                    RTGCPHYS const  GCPhysPage = ((RTGCPHYS)iPage << PAGE_SHIFT) + pRam->GCPhys;
                    PPGMPAGE        pPage      = &pRam->aPages[iPage];

                    AssertLogRelMsgReturn(PGM_PAGE_GET_TYPE(pPage) == PGMPAGETYPE_ROM,
                                          ("GCPhys=%RGp pPage=%R[pgmpage]\n", GCPhys, GCPhys),
                                          VERR_SSM_DATA_UNIT_FORMAT_CHANGED);
                    rc = pgmR3LoadPageBits(pVM, pSSM, PGMPAGETYPE_ROM, pPage, GCPhysPage, pRam);
                    AssertLogRelMsgRCReturn(rc, ("rc=%Rrc iPage=%#x GCPhys=%#x %s\n", rc, iPage, pRam->GCPhys, pRam->pszDesc), rc);
                }
            }
        }
    }

    return VINF_SUCCESS;
}


/**
 * Worker for pgmR3Load.
 *
 * @returns VBox status code.
 *
 * @param   pVM                 The VM handle.
 * @param   pSSM                The SSM handle.
 * @param   uVersion            The saved state version.
 */
static int pgmR3LoadFinalLocked(PVM pVM, PSSMHANDLE pSSM, uint32_t uVersion)
{
    PPGM        pPGM = &pVM->pgm.s;
    int         rc;
    uint32_t    u32Sep;

    /*
     * Load basic data (required / unaffected by relocation).
     */
    if (uVersion >= PGM_SAVED_STATE_VERSION_3_0_0)
    {
        rc = SSMR3GetStruct(pSSM, pPGM, &s_aPGMFields[0]);
        AssertLogRelRCReturn(rc, rc);

        for (VMCPUID i = 0; i < pVM->cCpus; i++)
        {
            rc = SSMR3GetStruct(pSSM, &pVM->aCpus[i].pgm.s, &s_aPGMCpuFields[0]);
            AssertLogRelRCReturn(rc, rc);
        }
    }
    else if (uVersion >= PGM_SAVED_STATE_VERSION_RR_DESC)
    {
        AssertRelease(pVM->cCpus == 1);

        PGMOLD pgmOld;
        rc = SSMR3GetStruct(pSSM, &pgmOld, &s_aPGMFields_Old[0]);
        AssertLogRelRCReturn(rc, rc);

        pPGM->fMappingsFixed    = pgmOld.fMappingsFixed;
        pPGM->GCPtrMappingFixed = pgmOld.GCPtrMappingFixed;
        pPGM->cbMappingFixed    = pgmOld.cbMappingFixed;

        pVM->aCpus[0].pgm.s.fA20Enabled   = pgmOld.fA20Enabled;
        pVM->aCpus[0].pgm.s.GCPhysA20Mask = pgmOld.GCPhysA20Mask;
        pVM->aCpus[0].pgm.s.enmGuestMode  = pgmOld.enmGuestMode;
    }
    else
    {
        AssertRelease(pVM->cCpus == 1);

        SSMR3GetBool(pSSM,      &pPGM->fMappingsFixed);
        SSMR3GetGCPtr(pSSM,     &pPGM->GCPtrMappingFixed);
        SSMR3GetU32(pSSM,       &pPGM->cbMappingFixed);

        uint32_t cbRamSizeIgnored;
        rc = SSMR3GetU32(pSSM, &cbRamSizeIgnored);
        if (RT_FAILURE(rc))
            return rc;
        SSMR3GetGCPhys(pSSM,    &pVM->aCpus[0].pgm.s.GCPhysA20Mask);

        uint32_t u32 = 0;
        SSMR3GetUInt(pSSM,      &u32);
        pVM->aCpus[0].pgm.s.fA20Enabled = !!u32;
        SSMR3GetUInt(pSSM,      &pVM->aCpus[0].pgm.s.fSyncFlags);
        RTUINT uGuestMode;
        SSMR3GetUInt(pSSM,      &uGuestMode);
        pVM->aCpus[0].pgm.s.enmGuestMode = (PGMMODE)uGuestMode;

        /* check separator. */
        SSMR3GetU32(pSSM, &u32Sep);
        if (RT_FAILURE(rc))
            return rc;
        if (u32Sep != (uint32_t)~0)
        {
            AssertMsgFailed(("u32Sep=%#x (first)\n", u32Sep));
            return VERR_SSM_DATA_UNIT_FORMAT_CHANGED;
        }
    }

    /*
     * The guest mappings.
     */
    uint32_t i = 0;
    for (;; i++)
    {
        /* Check the seqence number / separator. */
        rc = SSMR3GetU32(pSSM, &u32Sep);
        if (RT_FAILURE(rc))
            return rc;
        if (u32Sep == ~0U)
            break;
        if (u32Sep != i)
        {
            AssertMsgFailed(("u32Sep=%#x (last)\n", u32Sep));
            return VERR_SSM_DATA_UNIT_FORMAT_CHANGED;
        }

        /* get the mapping details. */
        char szDesc[256];
        szDesc[0] = '\0';
        rc = SSMR3GetStrZ(pSSM, szDesc, sizeof(szDesc));
        if (RT_FAILURE(rc))
            return rc;
        RTGCPTR GCPtr;
        SSMR3GetGCPtr(pSSM, &GCPtr);
        RTGCPTR cPTs;
        rc = SSMR3GetGCUIntPtr(pSSM, &cPTs);
        if (RT_FAILURE(rc))
            return rc;

        /* find matching range. */
        PPGMMAPPING pMapping;
        for (pMapping = pPGM->pMappingsR3; pMapping; pMapping = pMapping->pNextR3)
            if (    pMapping->cPTs == cPTs
                &&  !strcmp(pMapping->pszDesc, szDesc))
                break;
        AssertLogRelMsgReturn(pMapping, ("Couldn't find mapping: cPTs=%#x szDesc=%s (GCPtr=%RGv)\n",
                                         cPTs, szDesc, GCPtr),
                              VERR_SSM_LOAD_CONFIG_MISMATCH);

        /* relocate it. */
        if (pMapping->GCPtr != GCPtr)
        {
            AssertMsg((GCPtr >> X86_PD_SHIFT << X86_PD_SHIFT) == GCPtr, ("GCPtr=%RGv\n", GCPtr));
            pgmR3MapRelocate(pVM, pMapping, pMapping->GCPtr, GCPtr);
        }
        else
            Log(("pgmR3Load: '%s' needed no relocation (%RGv)\n", szDesc, GCPtr));
    }

    /*
     * Load the RAM contents.
     */
    if (uVersion > PGM_SAVED_STATE_VERSION_3_0_0)
        return pgmR3LoadLockedMemory(pVM, pSSM, SSM_PASS_FINAL);
    return pgmR3LoadLockedMemoryOld(pVM, pSSM, uVersion);
}


/**
 * Prepare state load operation.
 *
 * @returns VBox status code.
 * @param   pVM             VM Handle.
 * @param   pSSM            SSM operation handle.
 */
static DECLCALLBACK(int) pgmR3LoadPrep(PVM pVM, PSSMHANDLE pSSM)
{
    /*
     * Call the reset function to make sure all the memory is cleared.
     */
    PGMR3Reset(pVM);
    NOREF(pSSM);
    return VINF_SUCCESS;
}


/**
 * Execute state load operation.
 *
 * @returns VBox status code.
 * @param   pVM             VM Handle.
 * @param   pSSM            SSM operation handle.
 * @param   uVersion        Data layout version.
 * @param   uPass           The data pass.
 */
static DECLCALLBACK(int) pgmR3Load(PVM pVM, PSSMHANDLE pSSM, uint32_t uVersion, uint32_t uPass)
{
    int     rc;
    PPGM    pPGM = &pVM->pgm.s;
    Assert(uPass == SSM_PASS_FINAL); NOREF(uPass);

    /*
     * Validate version.
     */
    if (   (   uPass != SSM_PASS_FINAL
            && uVersion != PGM_SAVED_STATE_VERSION)
        || (   uVersion != PGM_SAVED_STATE_VERSION
            && uVersion != PGM_SAVED_STATE_VERSION_3_0_0
            && uVersion != PGM_SAVED_STATE_VERSION_2_2_2
            && uVersion != PGM_SAVED_STATE_VERSION_RR_DESC
            && uVersion != PGM_SAVED_STATE_VERSION_OLD_PHYS_CODE)
       )
    {
        AssertMsgFailed(("pgmR3Load: Invalid version uVersion=%d (current %d)!\n", uVersion, PGM_SAVED_STATE_VERSION));
        return VERR_SSM_UNSUPPORTED_DATA_UNIT_VERSION;
    }

    if (uPass != SSM_PASS_FINAL)
    {
        /*
         * The non-final passes contains only memory.
         */
        pgmLock(pVM);
        rc = pgmR3LoadLockedMemory(pVM, pSSM, uPass);
        pgmUnlock(pVM);
    }
    else
    {
        /*
         * Do the loading while owning the lock because a bunch of the functions
         * we're using requires this.
         */
        pgmLock(pVM);
        rc = pgmR3LoadFinalLocked(pVM, pSSM, uVersion);
        pgmUnlock(pVM);
        if (RT_SUCCESS(rc))
        {
            /*
             * We require a full resync now.
             */
            for (VMCPUID i = 0; i < pVM->cCpus; i++)
            {
                PVMCPU pVCpu = &pVM->aCpus[i];
                VMCPU_FF_SET(pVCpu, VMCPU_FF_PGM_SYNC_CR3_NON_GLOBAL);
                VMCPU_FF_SET(pVCpu, VMCPU_FF_PGM_SYNC_CR3);

                pVCpu->pgm.s.fSyncFlags |= PGM_SYNC_UPDATE_PAGE_BIT_VIRTUAL;
            }

            pgmR3HandlerPhysicalUpdateAll(pVM);

            for (VMCPUID i = 0; i < pVM->cCpus; i++)
            {
                PVMCPU pVCpu = &pVM->aCpus[i];

                /*
                 * Change the paging mode.
                 */
                rc = PGMR3ChangeMode(pVM, pVCpu, pVCpu->pgm.s.enmGuestMode);

                /* Restore pVM->pgm.s.GCPhysCR3. */
                Assert(pVCpu->pgm.s.GCPhysCR3 == NIL_RTGCPHYS);
                RTGCPHYS GCPhysCR3 = CPUMGetGuestCR3(pVCpu);
                if (    pVCpu->pgm.s.enmGuestMode == PGMMODE_PAE
                    ||  pVCpu->pgm.s.enmGuestMode == PGMMODE_PAE_NX
                    ||  pVCpu->pgm.s.enmGuestMode == PGMMODE_AMD64
                    ||  pVCpu->pgm.s.enmGuestMode == PGMMODE_AMD64_NX)
                    GCPhysCR3 = (GCPhysCR3 & X86_CR3_PAE_PAGE_MASK);
                else
                    GCPhysCR3 = (GCPhysCR3 & X86_CR3_PAGE_MASK);
                pVCpu->pgm.s.GCPhysCR3 = GCPhysCR3;
            }
        }
    }

    return rc;
}


/**
 * Registers the saved state callbacks with SSM.
 *
 * @returns VBox status code.
 * @param   pVM     Pointer to VM structure.
 * @param   cbRam   The RAM size.
 */
int pgmR3InitSavedState(PVM pVM, uint64_t cbRam)
{
    return SSMR3RegisterInternal(pVM, "pgm", 1, PGM_SAVED_STATE_VERSION, (size_t)cbRam + sizeof(PGM),
                                 pgmR3LivePrep, pgmR3LiveExec, pgmR3LiveVote,
                                 NULL, pgmR3SaveExec, pgmR3SaveDone,
                                 pgmR3LoadPrep, pgmR3Load, NULL);
}

