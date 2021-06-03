/* $Id$ */
/** @file
 * Host audio driver - Pulse Audio.
 */

/*
 * Copyright (C) 2006-2021 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_DRV_HOST_AUDIO
#include <VBox/log.h>
#include <VBox/vmm/pdmaudioifs.h>
#include <VBox/vmm/pdmaudioinline.h>
#include <VBox/vmm/pdmaudiohostenuminline.h>

#include <stdio.h>

#include <iprt/alloc.h>
#include <iprt/mem.h>
#include <iprt/uuid.h>
#include <iprt/semaphore.h>

#include "DrvHostAudioPulseAudioStubsMangling.h"
#include "DrvHostAudioPulseAudioStubs.h"

#include <pulse/pulseaudio.h>
#ifndef PA_STREAM_NOFLAGS
# define PA_STREAM_NOFLAGS  (pa_context_flags_t)0x0000U /* since 0.9.19 */
#endif
#ifndef PA_CONTEXT_NOFLAGS
# define PA_CONTEXT_NOFLAGS (pa_context_flags_t)0x0000U /* since 0.9.19 */
#endif

#include "VBoxDD.h"


/*********************************************************************************************************************************
*   Defines                                                                                                                      *
*********************************************************************************************************************************/
/** Max number of errors reported by drvHstAudPaError per instance.
 * @todo Make this configurable thru driver config. */
#define VBOX_PULSEAUDIO_MAX_LOG_REL_ERRORS  99


/** @name DRVHSTAUDPAENUMCB_F_XXX
 * @{ */
/** No flags specified. */
#define DRVHSTAUDPAENUMCB_F_NONE            0
/** (Release) log found devices. */
#define DRVHSTAUDPAENUMCB_F_LOG             RT_BIT(0)
/** Only do default devices. */
#define DRVHSTAUDPAENUMCB_F_DEFAULT_ONLY    RT_BIT(1)
/** @} */


/*********************************************************************************************************************************
*   Structures                                                                                                                   *
*********************************************************************************************************************************/
/** Pointer to the instance data for a pulse audio host audio driver. */
typedef struct DRVHSTAUDPA *PDRVHSTAUDPA;


/**
 * Callback context for the server init context state changed callback.
 */
typedef struct DRVHSTAUDPASTATECHGCTX
{
    /** The event semaphore. */
    RTSEMEVENT                  hEvtInit;
    /** The returned context state. */
    pa_context_state_t volatile enmCtxState;
} DRVHSTAUDPASTATECHGCTX;
/** Pointer to a server init context state changed callback context. */
typedef DRVHSTAUDPASTATECHGCTX *PDRVHSTAUDPASTATECHGCTX;


/**
 * Enumeration callback context used by the pfnGetConfig code.
 */
typedef struct DRVHSTAUDPAENUMCBCTX
{
    /** Pointer to PulseAudio's threaded main loop. */
    pa_threaded_mainloop   *pMainLoop;
    /** Enumeration flags, DRVHSTAUDPAENUMCB_F_XXX. */
    uint32_t                fFlags;
    /** VBox status code for the operation.
     * The caller sets this to VERR_AUDIO_ENUMERATION_FAILED, the callback never
     * uses that status code. */
    int32_t                 rcEnum;
    /** Name of default sink being used. Must be free'd using RTStrFree(). */
    char                   *pszDefaultSink;
    /** Name of default source being used. Must be free'd using RTStrFree(). */
    char                   *pszDefaultSource;
    /** The device enumeration to fill, NULL if pfnGetConfig context.   */
    PPDMAUDIOHOSTENUM       pDeviceEnum;
} DRVHSTAUDPAENUMCBCTX;
/** Pointer to an enumeration callback context. */
typedef DRVHSTAUDPAENUMCBCTX *PDRVHSTAUDPAENUMCBCTX;


/**
 * Pulse audio device enumeration entry.
 */
typedef struct DRVHSTAUDPADEVENTRY
{
    /** The part we share with others. */
    PDMAUDIOHOSTDEV         Core;
    /** The pulse audio name.
     * @note Kind of must use fixed size field here as that allows
     *       PDMAudioHostDevDup() and PDMAudioHostEnumCopy() to work. */
    RT_FLEXIBLE_ARRAY_EXTENSION
    char                    szPulseName[RT_FLEXIBLE_ARRAY];
} DRVHSTAUDPADEVENTRY;
/** Pointer to a pulse audio device enumeration entry. */
typedef DRVHSTAUDPADEVENTRY *PDRVHSTAUDPADEVENTRY;


/**
 * Pulse audio stream data.
 */
typedef struct DRVHSTAUDPASTREAM
{
    /** Common part. */
    PDMAUDIOBACKENDSTREAM   Core;
    /** The stream's acquired configuration. */
    PDMAUDIOSTREAMCFG       Cfg;
    /** Pointer to driver instance. */
    PDRVHSTAUDPA      pDrv;
    /** Pointer to opaque PulseAudio stream. */
    pa_stream              *pStream;
    /** Input: Pointer to Pulse sample peek buffer. */
    const uint8_t          *pbPeekBuf;
    /** Input: Current size (in bytes) of peeked data in buffer. */
    size_t                  cbPeekBuf;
    /** Input: Our offset (in bytes) in peek data buffer. */
    size_t                  offPeekBuf;
    /** Output: Asynchronous drain operation.  This is used as an indicator of
     *  whether we're currently draining the stream (will be cleaned up before
     *  resume/re-enable). */
    pa_operation           *pDrainOp;
    /** Asynchronous cork/uncork operation.
     * (This solely for cancelling before destroying the stream, so the callback
     * won't do any after-freed accesses.) */
    pa_operation           *pCorkOp;
    /** Asynchronous trigger operation.
     * (This solely for cancelling before destroying the stream, so the callback
     * won't do any after-freed accesses.) */
    pa_operation           *pTriggerOp;
    /** Output: Current latency (in microsecs). */
    uint64_t                cUsLatency;
#ifdef LOG_ENABLED
    /** Creation timestamp (in microsecs) of stream playback / recording. */
    pa_usec_t               tsStartUs;
    /** Timestamp (in microsecs) when last read from / written to the stream. */
    pa_usec_t               tsLastReadWrittenUs;
#endif
#ifdef DEBUG
    /** Number of occurred audio data underflows. */
    uint32_t                cUnderflows;
#endif
    /** Pulse sample format and attribute specification. */
    pa_sample_spec          SampleSpec;
    /** Channel map. */
    pa_channel_map          ChannelMap;
    /** Pulse playback and buffer metrics. */
    pa_buffer_attr          BufAttr;
} DRVHSTAUDPASTREAM;
/** Pointer to pulse audio stream data. */
typedef DRVHSTAUDPASTREAM *PDRVHSTAUDPASTREAM;


/**
 * Pulse audio host audio driver instance data.
 * @implements PDMIAUDIOCONNECTOR
 */
typedef struct DRVHSTAUDPA
{
    /** Pointer to the driver instance structure. */
    PPDMDRVINS              pDrvIns;
    /** Pointer to PulseAudio's threaded main loop. */
    pa_threaded_mainloop   *pMainLoop;
    /**
     * Pointer to our PulseAudio context.
     * @note We use a pMainLoop in a separate thread (pContext).
     *       So either use callback functions or protect these functions
     *       by pa_threaded_mainloop_lock() / pa_threaded_mainloop_unlock().
     */
    pa_context             *pContext;
    /** Shutdown indicator. */
    volatile bool           fAbortLoop;
    /** Error count for not flooding the release log.
     *  Specify UINT32_MAX for unlimited logging. */
    uint32_t                cLogErrors;
    /** The stream (base) name; needed for distinguishing
     *  streams in the PulseAudio mixer controls if multiple
     *  VMs are running at the same time. */
    char                    szStreamName[64];
    /** Don't want to put this on the stack... */
    DRVHSTAUDPASTATECHGCTX   InitStateChgCtx;
    /** Pointer to host audio interface. */
    PDMIHOSTAUDIO           IHostAudio;
} DRVHSTAUDPA;



/*
 * Glue to make the code work systems with PulseAudio < 0.9.11.
 */
#if !defined(PA_CONTEXT_IS_GOOD) && PA_API_VERSION < 12 /* 12 = 0.9.11 where PA_STREAM_IS_GOOD was added */
DECLINLINE(bool) PA_CONTEXT_IS_GOOD(pa_context_state_t enmState)
{
    return enmState == PA_CONTEXT_CONNECTING
        || enmState == PA_CONTEXT_AUTHORIZING
        || enmState == PA_CONTEXT_SETTING_NAME
        || enmState == PA_CONTEXT_READY;
}
#endif

#if !defined(PA_STREAM_IS_GOOD) && PA_API_VERSION < 12 /* 12 = 0.9.11 where PA_STREAM_IS_GOOD was added */
DECLINLINE(bool) PA_STREAM_IS_GOOD(pa_stream_state_t enmState)
{
    return enmState == PA_STREAM_CREATING
        || enmState == PA_STREAM_READY;
}
#endif


/**
 * Converts a pulse audio error to a VBox status.
 *
 * @returns VBox status code.
 * @param   rcPa    The error code to convert.
 */
static int drvHstAudPaErrorToVBox(int rcPa)
{
    /** @todo Implement some PulseAudio -> VBox mapping here. */
    RT_NOREF(rcPa);
    return VERR_GENERAL_FAILURE;
}


/**
 * Logs a pulse audio (from context) and converts it to VBox status.
 *
 * @returns VBox status code.
 * @param   pThis       Our instance data.
 * @param   pszFormat   The format string for the release log (no newline) .
 * @param   ...         Format string arguments.
 */
static int drvHstAudPaError(PDRVHSTAUDPA pThis, const char *pszFormat, ...)
{
    AssertPtrReturn(pThis, VERR_INVALID_POINTER);
    AssertPtr(pszFormat);

    int const rcPa   = pa_context_errno(pThis->pContext);
    int const rcVBox = drvHstAudPaErrorToVBox(rcPa);

    if (   pThis->cLogErrors < VBOX_PULSEAUDIO_MAX_LOG_REL_ERRORS
        && LogRelIs2Enabled())
    {
        va_list va;
        va_start(va, pszFormat);
        LogRel(("PulseAudio: %N: %s (%d, %Rrc)\n", pszFormat, &va, pa_strerror(rcPa), rcPa, rcVBox));
        va_end(va);

        if (++pThis->cLogErrors == VBOX_PULSEAUDIO_MAX_LOG_REL_ERRORS)
            LogRel(("PulseAudio: muting errors (max %u)\n", VBOX_PULSEAUDIO_MAX_LOG_REL_ERRORS));
    }

    return rcVBox;
}


/**
 * Signal the main loop to abort. Just signalling isn't sufficient as the
 * mainloop might not have been entered yet.
 */
static void drvHstAudPaSignalWaiter(PDRVHSTAUDPA pThis)
{
    if (pThis)
    {
        pThis->fAbortLoop = true;
        pa_threaded_mainloop_signal(pThis->pMainLoop, 0);
    }
}


/**
 * Wrapper around pa_threaded_mainloop_wait().
 */
static void drvHstAudPaMainloopWait(PDRVHSTAUDPA pThis)
{
    /** @todo r=bird: explain this logic. */
    if (!pThis->fAbortLoop)
        pa_threaded_mainloop_wait(pThis->pMainLoop);
    pThis->fAbortLoop = false;
}


/**
 * Pulse audio callback for context status changes, init variant.
 */
static void drvHstAudPaCtxCallbackStateChanged(pa_context *pCtx, void *pvUser)
{
    AssertPtrReturnVoid(pCtx);

    PDRVHSTAUDPA pThis = (PDRVHSTAUDPA)pvUser;
    AssertPtrReturnVoid(pThis);

    switch (pa_context_get_state(pCtx))
    {
        case PA_CONTEXT_READY:
        case PA_CONTEXT_TERMINATED:
        case PA_CONTEXT_FAILED:
            drvHstAudPaSignalWaiter(pThis);
            break;

        default:
            break;
    }
}


/**
 * Synchronously wait until an operation completed.
 *
 * This will consume the pOperation reference.
 */
static int drvHstAudPaWaitForEx(PDRVHSTAUDPA pThis, pa_operation *pOperation, RTMSINTERVAL cMsTimeout)
{
    AssertPtrReturn(pOperation, VERR_INVALID_POINTER);

    uint64_t const       msStart = RTTimeMilliTS();
    pa_operation_state_t enmOpState;
    while ((enmOpState = pa_operation_get_state(pOperation)) == PA_OPERATION_RUNNING)
    {
        if (!pThis->fAbortLoop) /** @todo r=bird: I do _not_ get the logic behind this fAbortLoop mechanism, it looks more
                                 * than a little mixed up and too much generalized see drvHstAudPaSignalWaiter. */
        {
            AssertPtr(pThis->pMainLoop);
            pa_threaded_mainloop_wait(pThis->pMainLoop);
            if (   !pThis->pContext
                || pa_context_get_state(pThis->pContext) != PA_CONTEXT_READY)
            {
                pa_operation_cancel(pOperation);
                pa_operation_unref(pOperation);
                LogRel(("PulseAudio: pa_context_get_state context not ready\n"));
                return VERR_INVALID_STATE;
            }
        }
        pThis->fAbortLoop = false;

        /*
         * Note! This timeout business is a bit bogus as pa_threaded_mainloop_wait is indefinite.
         */
        if (RTTimeMilliTS() - msStart >= cMsTimeout)
        {
            enmOpState = pa_operation_get_state(pOperation);
            if (enmOpState != PA_OPERATION_RUNNING)
                break;
            pa_operation_cancel(pOperation);
            pa_operation_unref(pOperation);
            return VERR_TIMEOUT;
        }
    }

    pa_operation_unref(pOperation);
    if (enmOpState == PA_OPERATION_DONE)
        return VINF_SUCCESS;
    return VERR_CANCELLED;
}


static int drvHstAudPaWaitFor(PDRVHSTAUDPA pThis, pa_operation *pOP)
{
    return drvHstAudPaWaitForEx(pThis, pOP, 10 * RT_MS_1SEC);
}



/*********************************************************************************************************************************
*   PDMIHOSTAUDIO                                                                                                                *
*********************************************************************************************************************************/

/**
 * Worker for drvHstAudPaEnumSourceCallback() and
 * drvHstAudPaEnumSinkCallback() that adds an entry to the enumeration
 * result.
 */
static void drvHstAudPaEnumAddDevice(PDRVHSTAUDPAENUMCBCTX pCbCtx, PDMAUDIODIR enmDir, const char *pszName,
                                        const char *pszDesc, uint8_t cChannelsInput, uint8_t cChannelsOutput,
                                        const char *pszDefaultName)
{
    size_t const         cchName = strlen(pszName);
    PDRVHSTAUDPADEVENTRY pDev    = (PDRVHSTAUDPADEVENTRY)PDMAudioHostDevAlloc(RT_UOFFSETOF(DRVHSTAUDPADEVENTRY, szPulseName)
                                                                            + RT_ALIGN_Z(cchName + 1, 16));
    if (pDev != NULL)
    {
        memcpy(pDev->szPulseName, pszName, cchName);
        pDev->szPulseName[cchName] = '\0';

        pDev->Core.enmUsage           = enmDir;
        pDev->Core.enmType            = RTStrIStr(pszDesc, "built-in") != NULL
                                      ? PDMAUDIODEVICETYPE_BUILTIN : PDMAUDIODEVICETYPE_UNKNOWN;
        if (RTStrCmp(pszName, pszDefaultName) != 0)
            pDev->Core.fFlags         = PDMAUDIOHOSTDEV_F_NONE;
        else
            pDev->Core.fFlags         = enmDir == PDMAUDIODIR_IN ? PDMAUDIOHOSTDEV_F_DEFAULT_IN : PDMAUDIOHOSTDEV_F_DEFAULT_OUT;
        pDev->Core.cMaxInputChannels  = cChannelsInput;
        pDev->Core.cMaxOutputChannels = cChannelsOutput;
        RTStrCopy(pDev->Core.szName, sizeof(pDev->Core.szName),
                  pszDesc && *pszDesc ? pszDesc : pszName);

        PDMAudioHostEnumAppend(pCbCtx->pDeviceEnum, &pDev->Core);
    }
    else
        pCbCtx->rcEnum = VERR_NO_MEMORY;
}


/**
 * Enumeration callback - source info.
 *
 * @param   pCtx        The context (DRVHSTAUDPA::pContext).
 * @param   pInfo       The info.  NULL when @a eol is not zero.
 * @param   eol         Error-or-last indicator or something like that:
 *                          -  0: Normal call with info.
 *                          -  1: End of list, no info.
 *                          - -1: Error callback, no info.
 * @param   pvUserData  Pointer to our DRVHSTAUDPAENUMCBCTX structure.
 */
static void drvHstAudPaEnumSourceCallback(pa_context *pCtx, const pa_source_info *pInfo, int eol, void *pvUserData)
{
    LogFlowFunc(("pCtx=%p pInfo=%p eol=%d pvUserData=%p\n", pCtx, pInfo, eol, pvUserData));
    PDRVHSTAUDPAENUMCBCTX pCbCtx = (PDRVHSTAUDPAENUMCBCTX)pvUserData;
    AssertPtrReturnVoid(pCbCtx);
    Assert((pInfo == NULL) == (eol != 0));
    RT_NOREF(pCtx);

    if (eol == 0 && pInfo != NULL)
    {
        LogRel2(("Pulse Audio: Source #%u: %u Hz %uch format=%u name='%s' desc='%s' driver='%s' flags=%#x\n",
                 pInfo->index, pInfo->sample_spec.rate, pInfo->sample_spec.channels, pInfo->sample_spec.format,
                 pInfo->name, pInfo->description, pInfo->driver, pInfo->flags));
        drvHstAudPaEnumAddDevice(pCbCtx, PDMAUDIODIR_IN, pInfo->name, pInfo->description,
                                    pInfo->sample_spec.channels, 0 /*cChannelsOutput*/, pCbCtx->pszDefaultSource);
    }
    else if (eol == 1 && !pInfo && pCbCtx->rcEnum == VERR_AUDIO_ENUMERATION_FAILED)
        pCbCtx->rcEnum = VINF_SUCCESS;

    /* Wake up the calling thread when done: */
    if (eol != 0)
        pa_threaded_mainloop_signal(pCbCtx->pMainLoop, 0);
}


/**
 * Enumeration callback - sink info.
 *
 * @param   pCtx        The context (DRVHSTAUDPA::pContext).
 * @param   pInfo       The info.  NULL when @a eol is not zero.
 * @param   eol         Error-or-last indicator or something like that:
 *                          -  0: Normal call with info.
 *                          -  1: End of list, no info.
 *                          - -1: Error callback, no info.
 * @param   pvUserData  Pointer to our DRVHSTAUDPAENUMCBCTX structure.
 */
static void drvHstAudPaEnumSinkCallback(pa_context *pCtx, const pa_sink_info *pInfo, int eol, void *pvUserData)
{
    LogFlowFunc(("pCtx=%p pInfo=%p eol=%d pvUserData=%p\n", pCtx, pInfo, eol, pvUserData));
    PDRVHSTAUDPAENUMCBCTX pCbCtx = (PDRVHSTAUDPAENUMCBCTX)pvUserData;
    AssertPtrReturnVoid(pCbCtx);
    Assert((pInfo == NULL) == (eol != 0));
    RT_NOREF(pCtx);

    if (eol == 0 && pInfo != NULL)
    {
        LogRel2(("Pulse Audio: Sink #%u: %u Hz %uch format=%u name='%s' desc='%s' driver='%s' flags=%#x\n",
                 pInfo->index, pInfo->sample_spec.rate, pInfo->sample_spec.channels, pInfo->sample_spec.format,
                 pInfo->name, pInfo->description, pInfo->driver, pInfo->flags));
        drvHstAudPaEnumAddDevice(pCbCtx, PDMAUDIODIR_OUT, pInfo->name, pInfo->description,
                                    0 /*cChannelsInput*/, pInfo->sample_spec.channels, pCbCtx->pszDefaultSink);
    }
    else if (eol == 1 && !pInfo && pCbCtx->rcEnum == VERR_AUDIO_ENUMERATION_FAILED)
        pCbCtx->rcEnum = VINF_SUCCESS;

    /* Wake up the calling thread when done: */
    if (eol != 0)
        pa_threaded_mainloop_signal(pCbCtx->pMainLoop, 0);
}


/**
 * Enumeration callback - service info.
 *
 * Copy down the default names.
 */
static void drvHstAudPaEnumServerCallback(pa_context *pCtx, const pa_server_info *pInfo, void *pvUserData)
{
    LogFlowFunc(("pCtx=%p pInfo=%p pvUserData=%p\n", pCtx, pInfo, pvUserData));
    PDRVHSTAUDPAENUMCBCTX pCbCtx = (PDRVHSTAUDPAENUMCBCTX)pvUserData;
    AssertPtrReturnVoid(pCbCtx);
    RT_NOREF(pCtx);

    if (pInfo)
    {
        LogRel2(("PulseAudio: Server info: user=%s host=%s ver=%s name=%s defsink=%s defsrc=%s spec: %d %uHz %uch\n",
                 pInfo->user_name, pInfo->host_name, pInfo->server_version, pInfo->server_name,
                 pInfo->default_sink_name, pInfo->default_source_name,
                 pInfo->sample_spec.format, pInfo->sample_spec.rate, pInfo->sample_spec.channels));

        Assert(!pCbCtx->pszDefaultSink);
        Assert(!pCbCtx->pszDefaultSource);
        Assert(pCbCtx->rcEnum == VERR_AUDIO_ENUMERATION_FAILED);
        pCbCtx->rcEnum = VINF_SUCCESS;

        if (pInfo->default_sink_name)
        {
            Assert(RTStrIsValidEncoding(pInfo->default_sink_name));
            pCbCtx->pszDefaultSink = RTStrDup(pInfo->default_sink_name);
            AssertStmt(pCbCtx->pszDefaultSink, pCbCtx->rcEnum = VERR_NO_STR_MEMORY);
        }

        if (pInfo->default_source_name)
        {
            Assert(RTStrIsValidEncoding(pInfo->default_source_name));
            pCbCtx->pszDefaultSource = RTStrDup(pInfo->default_source_name);
            AssertStmt(pCbCtx->pszDefaultSource, pCbCtx->rcEnum = VERR_NO_STR_MEMORY);
        }
    }
    else
        pCbCtx->rcEnum = VERR_INVALID_POINTER;

    pa_threaded_mainloop_signal(pCbCtx->pMainLoop, 0);
}


/**
 * @note Called with the PA main loop locked.
 */
static int drvHstAudPaEnumerate(PDRVHSTAUDPA pThis, uint32_t fEnum, PPDMAUDIOHOSTENUM pDeviceEnum)
{
    DRVHSTAUDPAENUMCBCTX CbCtx        = { pThis->pMainLoop, fEnum, VERR_AUDIO_ENUMERATION_FAILED, NULL, NULL, pDeviceEnum };
    bool const           fLog         = (fEnum & DRVHSTAUDPAENUMCB_F_LOG);
    bool const           fOnlyDefault = (fEnum & DRVHSTAUDPAENUMCB_F_DEFAULT_ONLY);
    int                  rc;

    /*
     * Check if server information is available and bail out early if it isn't.
     * This should give us a default (playback) sink and (recording) source.
     */
    LogRel(("PulseAudio: Retrieving server information ...\n"));
    CbCtx.rcEnum = VERR_AUDIO_ENUMERATION_FAILED;
    pa_operation *paOpServerInfo = pa_context_get_server_info(pThis->pContext, drvHstAudPaEnumServerCallback, &CbCtx);
    if (paOpServerInfo)
        rc = drvHstAudPaWaitFor(pThis, paOpServerInfo);
    else
    {
        LogRel(("PulseAudio: Server information not available, skipping enumeration.\n"));
        return VINF_SUCCESS;
    }
    if (RT_SUCCESS(rc))
        rc = CbCtx.rcEnum;
    if (RT_FAILURE(rc))
    {
        if (fLog)
            LogRel(("PulseAudio: Error enumerating PulseAudio server properties: %Rrc\n", rc));
        return rc;
    }

    /*
     * Get info about the playback sink.
     */
    if (fLog && CbCtx.pszDefaultSink)
        LogRel2(("PulseAudio: Default output sink is '%s'\n", CbCtx.pszDefaultSink));
    else if (fLog)
        LogRel2(("PulseAudio: No default output sink found\n"));

    if (CbCtx.pszDefaultSink || !fOnlyDefault)
    {
        CbCtx.rcEnum = VERR_AUDIO_ENUMERATION_FAILED;
        if (!fOnlyDefault)
            rc = drvHstAudPaWaitFor(pThis,
                                       pa_context_get_sink_info_list(pThis->pContext, drvHstAudPaEnumSinkCallback, &CbCtx));
        else
            rc = drvHstAudPaWaitFor(pThis, pa_context_get_sink_info_by_name(pThis->pContext, CbCtx.pszDefaultSink,
                                                                               drvHstAudPaEnumSinkCallback, &CbCtx));
        if (RT_SUCCESS(rc))
            rc = CbCtx.rcEnum;
        if (fLog && RT_FAILURE(rc))
            LogRel(("PulseAudio: Error enumerating properties for default output sink '%s': %Rrc\n",
                    CbCtx.pszDefaultSink, rc));
    }

    /*
     * Get info about the recording source.
     */
    if (fLog && CbCtx.pszDefaultSource)
        LogRel2(("PulseAudio: Default input source is '%s'\n", CbCtx.pszDefaultSource));
    else if (fLog)
        LogRel2(("PulseAudio: No default input source found\n"));
    if (CbCtx.pszDefaultSource || !fOnlyDefault)
    {
        CbCtx.rcEnum = VERR_AUDIO_ENUMERATION_FAILED;
        int rc2;
        if (!fOnlyDefault)
            rc2 = drvHstAudPaWaitFor(pThis, pa_context_get_source_info_list(pThis->pContext,
                                                                               drvHstAudPaEnumSourceCallback, &CbCtx));
        else
            rc2 = drvHstAudPaWaitFor(pThis, pa_context_get_source_info_by_name(pThis->pContext, CbCtx.pszDefaultSource,
                                                                                  drvHstAudPaEnumSourceCallback, &CbCtx));
        if (RT_SUCCESS(rc2))
            rc2 = CbCtx.rcEnum;
        if (fLog && RT_FAILURE(rc2))
            LogRel(("PulseAudio: Error enumerating properties for default input source '%s': %Rrc\n",
                    CbCtx.pszDefaultSource, rc));
        if (RT_SUCCESS(rc))
            rc = rc2;
    }

    /* clean up */
    RTStrFree(CbCtx.pszDefaultSink);
    RTStrFree(CbCtx.pszDefaultSource);

    LogFlowFuncLeaveRC(rc);
    return rc;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnGetConfig}
 */
static DECLCALLBACK(int) drvHstAudPaHA_GetConfig(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDCFG pBackendCfg)
{
    PDRVHSTAUDPA pThis = RT_FROM_MEMBER(pInterface, DRVHSTAUDPA, IHostAudio);
    AssertPtrReturn(pBackendCfg, VERR_INVALID_POINTER);

    /*
     * The configuration.
     */
    RTStrCopy(pBackendCfg->szName, sizeof(pBackendCfg->szName), "PulseAudio");
    pBackendCfg->cbStream       = sizeof(DRVHSTAUDPASTREAM);
    pBackendCfg->fFlags         = 0;
    pBackendCfg->cMaxStreamsOut = UINT32_MAX;
    pBackendCfg->cMaxStreamsIn  = UINT32_MAX;

#if 0
    /*
     * In case we want to gather info about default devices, we can do this:
     */
    PDMAUDIOHOSTENUM DeviceEnum;
    PDMAudioHostEnumInit(&DeviceEnum);
    pa_threaded_mainloop_lock(pThis->pMainLoop);
    int rc = drvHstAudPaEnumerate(pThis, DRVHSTAUDPAENUMCB_F_DEFAULT_ONLY | DRVHSTAUDPAENUMCB_F_LOG, &DeviceEnum);
    pa_threaded_mainloop_unlock(pThis->pMainLoop);
    AssertRCReturn(rc, rc);
    /** @todo do stuff with DeviceEnum. */
    PDMAudioHostEnumDelete(&DeviceEnum);
#else
    RT_NOREF(pThis);
#endif
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnGetDevices}
 */
static DECLCALLBACK(int) drvHstAudPaHA_GetDevices(PPDMIHOSTAUDIO pInterface, PPDMAUDIOHOSTENUM pDeviceEnum)
{
    PDRVHSTAUDPA pThis = RT_FROM_MEMBER(pInterface, DRVHSTAUDPA, IHostAudio);
    AssertPtrReturn(pDeviceEnum, VERR_INVALID_POINTER);
    PDMAudioHostEnumInit(pDeviceEnum);

    /* Refine it or something (currently only some LogRel2 stuff): */
    pa_threaded_mainloop_lock(pThis->pMainLoop);
    int rc = drvHstAudPaEnumerate(pThis, DRVHSTAUDPAENUMCB_F_NONE, pDeviceEnum);
    pa_threaded_mainloop_unlock(pThis->pMainLoop);
    return rc;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnGetStatus}
 */
static DECLCALLBACK(PDMAUDIOBACKENDSTS) drvHstAudPaHA_GetStatus(PPDMIHOSTAUDIO pInterface, PDMAUDIODIR enmDir)
{
    RT_NOREF(pInterface, enmDir);
    return PDMAUDIOBACKENDSTS_RUNNING;
}


/**
 * Stream status changed.
 */
static void drvHstAudPaStreamStateChangedCallback(pa_stream *pStream, void *pvUser)
{
    AssertPtrReturnVoid(pStream);

    PDRVHSTAUDPA pThis = (PDRVHSTAUDPA)pvUser;
    AssertPtrReturnVoid(pThis);

    switch (pa_stream_get_state(pStream))
    {
        case PA_STREAM_READY:
        case PA_STREAM_FAILED:
        case PA_STREAM_TERMINATED:
            drvHstAudPaSignalWaiter(pThis);
            break;

        default:
            break;
    }
}

#ifdef DEBUG

/**
 * Debug PA callback: Need data to output.
 */
static void drvHstAudPaStreamReqWriteDebugCallback(pa_stream *pStream, size_t cbLen, void *pvContext)
{
    RT_NOREF(cbLen, pvContext);
    pa_usec_t cUsLatency = 0;
    int       fNegative  = 0;
    int       rcPa = pa_stream_get_latency(pStream, &cUsLatency, &fNegative);
    Log2Func(("Requesting %zu bytes; Latency: %'RU64 us%s\n",
              cbLen, cUsLatency, rcPa == 0 ? " - pa_stream_get_latency failed!" : ""));
}


/**
 * Debug PA callback: Underflow.  This may happen when draing/corking.
 */
static void drvHstAudPaStreamUnderflowDebugCallback(pa_stream *pStream, void *pvContext)
{
    PDRVHSTAUDPASTREAM pStrm = (PDRVHSTAUDPASTREAM)pvContext;
    AssertPtrReturnVoid(pStrm);

    pStrm->cUnderflows++;

    LogRel2(("PulseAudio: Warning: Hit underflow #%RU32\n", pStrm->cUnderflows));

    if (   pStrm->cUnderflows >= 6                /** @todo Make this check configurable. */
        && pStrm->cUsLatency  < 2U*RT_US_1SEC)
    {
        pStrm->cUsLatency = pStrm->cUsLatency * 3 / 2;
        LogRel2(("PulseAudio: Increasing output latency to %'RU64 us\n", pStrm->cUsLatency));

        pStrm->BufAttr.maxlength = pa_usec_to_bytes(pStrm->cUsLatency, &pStrm->SampleSpec);
        pStrm->BufAttr.tlength   = pa_usec_to_bytes(pStrm->cUsLatency, &pStrm->SampleSpec);
        pa_operation *pOperation = pa_stream_set_buffer_attr(pStream, &pStrm->BufAttr, NULL, NULL);
        if (pOperation)
            pa_operation_unref(pOperation);
        else
            LogRel2(("pa_stream_set_buffer_attr failed!\n"));

        pStrm->cUnderflows = 0;
    }

    pa_usec_t cUsLatency = 0;
    int       fNegative  = 0;
    pa_stream_get_latency(pStream, &cUsLatency, &fNegative);
    LogRel2(("PulseAudio: Latency now is %'RU64 us\n", cUsLatency));

# ifdef LOG_ENABLED
    if (LogIs2Enabled())
    {
        const pa_timing_info *pTInfo = pa_stream_get_timing_info(pStream);
        AssertReturnVoid(pTInfo);
        const pa_sample_spec *pSpec  = pa_stream_get_sample_spec(pStream);
        AssertReturnVoid(pSpec);
        Log2Func(("writepos=%'RU64 us, readpost=%'RU64 us, age=%'RU64 us, latency=%'RU64 us (%RU32Hz %RU8ch)\n",
                  pa_bytes_to_usec(pTInfo->write_index, pSpec), pa_bytes_to_usec(pTInfo->read_index, pSpec),
                  pa_rtclock_now() - pStrm->tsStartUs, cUsLatency, pSpec->rate, pSpec->channels));
    }
# endif
}


/**
 * Debug PA callback: Overflow.  This may happen when draing/corking.
 */
static void drvHstAudPaStreamOverflowDebugCallback(pa_stream *pStream, void *pvContext)
{
    RT_NOREF(pStream, pvContext);
    Log2Func(("Warning: Hit overflow\n"));
}

#endif /* DEBUG */

/**
 * Converts from PDM PCM properties to pulse audio format.
 *
 * Worker for the stream creation code.
 *
 * @returns PA format.
 * @retval  PA_SAMPLE_INVALID if format not supported.
 * @param   pProps      The PDM audio source properties.
 */
static pa_sample_format_t drvHstAudPaPropsToPulse(PCPDMAUDIOPCMPROPS pProps)
{
    switch (PDMAudioPropsSampleSize(pProps))
    {
        case 1:
            if (!PDMAudioPropsIsSigned(pProps))
                return PA_SAMPLE_U8;
            break;

        case 2:
            if (PDMAudioPropsIsSigned(pProps))
                return PDMAudioPropsIsLittleEndian(pProps) ? PA_SAMPLE_S16LE : PA_SAMPLE_S16BE;
            break;

#ifdef PA_SAMPLE_S32LE
        case 4:
            if (PDMAudioPropsIsSigned(pProps))
                return PDMAudioPropsIsLittleEndian(pProps) ? PA_SAMPLE_S32LE : PA_SAMPLE_S32BE;
            break;
#endif
    }

    AssertMsgFailed(("%RU8%s not supported\n", PDMAudioPropsSampleSize(pProps), PDMAudioPropsIsSigned(pProps) ? "S" : "U"));
    return PA_SAMPLE_INVALID;
}


/**
 * Converts from pulse audio sample specification to PDM PCM audio properties.
 *
 * Worker for the stream creation code.
 *
 * @returns VBox status code.
 * @param   pProps      The PDM audio source properties.
 * @param   enmPulseFmt The PA format.
 * @param   cChannels   The number of channels.
 * @param   uHz         The frequency.
 */
static int drvHstAudPaToAudioProps(PPDMAUDIOPCMPROPS pProps, pa_sample_format_t enmPulseFmt, uint8_t cChannels, uint32_t uHz)
{
    AssertReturn(cChannels > 0, VERR_INVALID_PARAMETER);
    AssertReturn(cChannels < 16, VERR_INVALID_PARAMETER);

    switch (enmPulseFmt)
    {
        case PA_SAMPLE_U8:
            PDMAudioPropsInit(pProps, 1 /*8-bit*/, false /*signed*/, cChannels, uHz);
            break;

        case PA_SAMPLE_S16LE:
            PDMAudioPropsInitEx(pProps, 2 /*16-bit*/, true /*signed*/, cChannels, uHz, true /*fLittleEndian*/, false /*fRaw*/);
            break;

        case PA_SAMPLE_S16BE:
            PDMAudioPropsInitEx(pProps, 2 /*16-bit*/, true /*signed*/, cChannels, uHz, false /*fLittleEndian*/, false /*fRaw*/);
            break;

#ifdef PA_SAMPLE_S32LE
        case PA_SAMPLE_S32LE:
            PDMAudioPropsInitEx(pProps, 4 /*32-bit*/, true /*signed*/, cChannels, uHz, true /*fLittleEndian*/, false /*fRaw*/);
            break;
#endif

#ifdef PA_SAMPLE_S32BE
        case PA_SAMPLE_S32BE:
            PDMAudioPropsInitEx(pProps, 4 /*32-bit*/, true /*signed*/, cChannels, uHz, false /*fLittleEndian*/, false /*fRaw*/);
            break;
#endif

        default:
            AssertLogRelMsgFailed(("PulseAudio: Format (%d) not supported\n", enmPulseFmt));
            return VERR_NOT_SUPPORTED;
    }

    return VINF_SUCCESS;
}


/**
 * Worker that does the actual creation of an PA stream.
 *
 * @returns VBox status code.
 * @param   pThis       Our driver instance data.
 * @param   pStreamPA   Our stream data.
 * @param   pszName     How we name the stream.
 * @param   pCfgAcq     The requested stream properties, the Props member is
 *                      updated upon successful return.
 *
 * @note    Caller owns the mainloop lock.
 */
static int drvHstAudPaStreamCreateLocked(PDRVHSTAUDPA pThis, PDRVHSTAUDPASTREAM pStreamPA,
                                         const char *pszName, PPDMAUDIOSTREAMCFG pCfgAcq)
{
    /*
     * Create the stream.
     */
    pa_stream *pStream = pa_stream_new(pThis->pContext, pszName, &pStreamPA->SampleSpec, &pStreamPA->ChannelMap);
    if (!pStream)
    {
        LogRel(("PulseAudio: Failed to create stream '%s': %s (%d)\n",
                pszName, pa_strerror(pa_context_errno(pThis->pContext)), pa_context_errno(pThis->pContext)));
        return VERR_AUDIO_STREAM_COULD_NOT_CREATE;
    }

    /*
     * Set the state callback, and in debug builds a few more...
     */
#ifdef DEBUG
    pa_stream_set_write_callback(       pStream, drvHstAudPaStreamReqWriteDebugCallback,  pStreamPA);
    pa_stream_set_underflow_callback(   pStream, drvHstAudPaStreamUnderflowDebugCallback, pStreamPA);
    if (pCfgAcq->enmDir == PDMAUDIODIR_OUT)
        pa_stream_set_overflow_callback(pStream, drvHstAudPaStreamOverflowDebugCallback,  pStreamPA);
#endif
    pa_stream_set_state_callback(       pStream, drvHstAudPaStreamStateChangedCallback,   pThis);

    /*
     * Connect the stream.
     */
    int             rc;
    unsigned const  fFlags = PA_STREAM_START_CORKED /* Require explicit starting (uncorking). */
                             /* For using pa_stream_get_latency() and pa_stream_get_time(). */
                           | PA_STREAM_INTERPOLATE_TIMING | PA_STREAM_AUTO_TIMING_UPDATE
#if PA_API_VERSION >= 12
                           | PA_STREAM_ADJUST_LATENCY
#endif
                           ;
    if (pCfgAcq->enmDir == PDMAUDIODIR_IN)
    {
        LogFunc(("Input stream attributes: maxlength=%d fragsize=%d\n",
                 pStreamPA->BufAttr.maxlength, pStreamPA->BufAttr.fragsize));
        rc = pa_stream_connect_record(pStream, NULL /*dev*/, &pStreamPA->BufAttr, (pa_stream_flags_t)fFlags);
    }
    else
    {
        LogFunc(("Output buffer attributes: maxlength=%d tlength=%d prebuf=%d minreq=%d\n",
                 pStreamPA->BufAttr.maxlength, pStreamPA->BufAttr.tlength, pStreamPA->BufAttr.prebuf, pStreamPA->BufAttr.minreq));
        rc = pa_stream_connect_playback(pStream, NULL /*dev*/, &pStreamPA->BufAttr, (pa_stream_flags_t)fFlags,
                                        NULL /*volume*/, NULL /*sync_stream*/);
    }
    if (rc >= 0)
    {
        /*
         * Wait for the stream to become ready.
         */
        uint64_t const nsStart = RTTimeNanoTS();
        pa_stream_state_t enmStreamState;
        while (   (enmStreamState = pa_stream_get_state(pStream)) != PA_STREAM_READY
               && PA_STREAM_IS_GOOD(enmStreamState)
               && RTTimeNanoTS() - nsStart < RT_NS_10SEC /* not really timed */ )
            drvHstAudPaMainloopWait(pThis);
        if (enmStreamState == PA_STREAM_READY)
        {
            LogFunc(("Connecting stream took %'RU64 ns\n", RTTimeNanoTS() - nsStart));
#ifdef LOG_ENABLED
            pStreamPA->tsStartUs = pa_rtclock_now();
#endif
            /*
             * Update the buffer attributes.
             */
            const pa_buffer_attr *pBufAttribs = pa_stream_get_buffer_attr(pStream);
            AssertPtr(pBufAttribs);
            if (pBufAttribs)
            {
                pStreamPA->BufAttr = *pBufAttribs;
                LogFunc(("Obtained %s buffer attributes: maxlength=%RU32 tlength=%RU32 prebuf=%RU32 minreq=%RU32 fragsize=%RU32\n",
                         pCfgAcq->enmDir == PDMAUDIODIR_IN ? "input" : "output", pBufAttribs->maxlength, pBufAttribs->tlength,
                         pBufAttribs->prebuf, pBufAttribs->minreq, pBufAttribs->fragsize));

                /*
                 * Convert the sample spec back to PDM speak.
                 * Note! This isn't strictly speaking needed as SampleSpec has *not* been
                 *       modified since the caller converted it from pCfgReq.
                 */
                rc = drvHstAudPaToAudioProps(&pCfgAcq->Props, pStreamPA->SampleSpec.format,
                                                pStreamPA->SampleSpec.channels, pStreamPA->SampleSpec.rate);
                if (RT_SUCCESS(rc))
                {
                    pStreamPA->pStream = pStream;
                    LogFlowFunc(("returns VINF_SUCCESS\n"));
                    return VINF_SUCCESS;
                }
            }
            else
            {
                LogRelMax(99, ("PulseAudio: Failed to get buffer attribs for stream '%s': %s (%d)\n",
                               pszName, pa_strerror(pa_context_errno(pThis->pContext)), pa_context_errno(pThis->pContext)));
                rc = VERR_AUDIO_STREAM_COULD_NOT_CREATE;
            }
        }
        else
        {
            LogRelMax(99, ("PulseAudio: Failed to initialize stream '%s': state=%d, waited %'RU64 ns\n",
                           pszName, enmStreamState, RTTimeNanoTS() - nsStart));
            rc = VERR_AUDIO_STREAM_COULD_NOT_CREATE;
        }
        pa_stream_disconnect(pStream);
    }
    else
    {
        LogRelMax(99, ("PulseAudio: Could not connect %s stream '%s': %s (%d/%d)\n",
                       pCfgAcq->enmDir == PDMAUDIODIR_IN ? "input" : "output",
                       pszName, pa_strerror(pa_context_errno(pThis->pContext)), pa_context_errno(pThis->pContext), rc));
        rc = VERR_AUDIO_STREAM_COULD_NOT_CREATE;
    }

    pa_stream_unref(pStream);
    Assert(RT_FAILURE_NP(rc));
    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}


/**
 * Translates a PDM channel ID to a PA channel position.
 *
 * @returns PA channel position, INVALID if no mapping found.
 */
static pa_channel_position_t drvHstAudPaConvertChannelId(uint8_t idChannel)
{
    switch (idChannel)
    {
        case PDMAUDIOCHANNELID_FRONT_LEFT:              return PA_CHANNEL_POSITION_FRONT_LEFT;
        case PDMAUDIOCHANNELID_FRONT_RIGHT:             return PA_CHANNEL_POSITION_FRONT_RIGHT;
        case PDMAUDIOCHANNELID_FRONT_CENTER:            return PA_CHANNEL_POSITION_FRONT_CENTER;
        case PDMAUDIOCHANNELID_LFE:                     return PA_CHANNEL_POSITION_LFE;
        case PDMAUDIOCHANNELID_REAR_LEFT:               return PA_CHANNEL_POSITION_REAR_LEFT;
        case PDMAUDIOCHANNELID_REAR_RIGHT:              return PA_CHANNEL_POSITION_REAR_RIGHT;
        case PDMAUDIOCHANNELID_FRONT_LEFT_OF_CENTER:    return PA_CHANNEL_POSITION_FRONT_LEFT_OF_CENTER;
        case PDMAUDIOCHANNELID_FRONT_RIGHT_OF_CENTER:   return PA_CHANNEL_POSITION_FRONT_RIGHT_OF_CENTER;
        case PDMAUDIOCHANNELID_REAR_CENTER:             return PA_CHANNEL_POSITION_REAR_CENTER;
        case PDMAUDIOCHANNELID_SIDE_LEFT:               return PA_CHANNEL_POSITION_SIDE_LEFT;
        case PDMAUDIOCHANNELID_SIDE_RIGHT:              return PA_CHANNEL_POSITION_SIDE_RIGHT;
        case PDMAUDIOCHANNELID_TOP_CENTER:              return PA_CHANNEL_POSITION_TOP_CENTER;
        case PDMAUDIOCHANNELID_FRONT_LEFT_HEIGHT:       return PA_CHANNEL_POSITION_TOP_FRONT_LEFT;
        case PDMAUDIOCHANNELID_FRONT_CENTER_HEIGHT:     return PA_CHANNEL_POSITION_TOP_FRONT_CENTER;
        case PDMAUDIOCHANNELID_FRONT_RIGHT_HEIGHT:      return PA_CHANNEL_POSITION_TOP_FRONT_RIGHT;
        case PDMAUDIOCHANNELID_REAR_LEFT_HEIGHT:        return PA_CHANNEL_POSITION_TOP_REAR_LEFT;
        case PDMAUDIOCHANNELID_REAR_CENTER_HEIGHT:      return PA_CHANNEL_POSITION_TOP_REAR_CENTER;
        case PDMAUDIOCHANNELID_REAR_RIGHT_HEIGHT:       return PA_CHANNEL_POSITION_TOP_REAR_RIGHT;
        default:                                        return PA_CHANNEL_POSITION_INVALID;
    }
}


/**
 * Translates a PA channel position to a PDM channel ID.
 *
 * @returns PDM channel ID, UNKNOWN if no mapping found.
 */
static PDMAUDIOCHANNELID drvHstAudPaConvertChannelPos(pa_channel_position_t enmChannelPos)
{
    switch (enmChannelPos)
    {
        case PA_CHANNEL_POSITION_INVALID:               return PDMAUDIOCHANNELID_INVALID;
        case PA_CHANNEL_POSITION_MONO:                  return PDMAUDIOCHANNELID_MONO;
        case PA_CHANNEL_POSITION_FRONT_LEFT:            return PDMAUDIOCHANNELID_FRONT_LEFT;
        case PA_CHANNEL_POSITION_FRONT_RIGHT:           return PDMAUDIOCHANNELID_FRONT_RIGHT;
        case PA_CHANNEL_POSITION_FRONT_CENTER:          return PDMAUDIOCHANNELID_FRONT_CENTER;
        case PA_CHANNEL_POSITION_LFE:                   return PDMAUDIOCHANNELID_LFE;
        case PA_CHANNEL_POSITION_REAR_LEFT:             return PDMAUDIOCHANNELID_REAR_LEFT;
        case PA_CHANNEL_POSITION_REAR_RIGHT:            return PDMAUDIOCHANNELID_REAR_RIGHT;
        case PA_CHANNEL_POSITION_FRONT_LEFT_OF_CENTER:  return PDMAUDIOCHANNELID_FRONT_LEFT_OF_CENTER;
        case PA_CHANNEL_POSITION_FRONT_RIGHT_OF_CENTER: return PDMAUDIOCHANNELID_FRONT_RIGHT_OF_CENTER;
        case PA_CHANNEL_POSITION_REAR_CENTER:           return PDMAUDIOCHANNELID_REAR_CENTER;
        case PA_CHANNEL_POSITION_SIDE_LEFT:             return PDMAUDIOCHANNELID_SIDE_LEFT;
        case PA_CHANNEL_POSITION_SIDE_RIGHT:            return PDMAUDIOCHANNELID_SIDE_RIGHT;
        case PA_CHANNEL_POSITION_TOP_CENTER:            return PDMAUDIOCHANNELID_TOP_CENTER;
        case PA_CHANNEL_POSITION_TOP_FRONT_LEFT:        return PDMAUDIOCHANNELID_FRONT_LEFT_HEIGHT;
        case PA_CHANNEL_POSITION_TOP_FRONT_CENTER:      return PDMAUDIOCHANNELID_FRONT_CENTER_HEIGHT;
        case PA_CHANNEL_POSITION_TOP_FRONT_RIGHT:       return PDMAUDIOCHANNELID_FRONT_RIGHT_HEIGHT;
        case PA_CHANNEL_POSITION_TOP_REAR_LEFT:         return PDMAUDIOCHANNELID_REAR_LEFT_HEIGHT;
        case PA_CHANNEL_POSITION_TOP_REAR_CENTER:       return PDMAUDIOCHANNELID_REAR_CENTER_HEIGHT;
        case PA_CHANNEL_POSITION_TOP_REAR_RIGHT:        return PDMAUDIOCHANNELID_REAR_RIGHT_HEIGHT;
        default:                                        return PDMAUDIOCHANNELID_UNKNOWN;
    }
}



/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamCreate}
 */
static DECLCALLBACK(int) drvHstAudPaHA_StreamCreate(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream,
                                                    PPDMAUDIOSTREAMCFG pCfgReq, PPDMAUDIOSTREAMCFG pCfgAcq)
{
    PDRVHSTAUDPA        pThis     = RT_FROM_MEMBER(pInterface, DRVHSTAUDPA, IHostAudio);
    PDRVHSTAUDPASTREAM  pStreamPA = (PDRVHSTAUDPASTREAM)pStream;
    AssertPtrReturn(pStreamPA, VERR_INVALID_POINTER);
    AssertPtrReturn(pCfgReq, VERR_INVALID_POINTER);
    AssertPtrReturn(pCfgAcq, VERR_INVALID_POINTER);
    AssertReturn(pCfgReq->enmDir == PDMAUDIODIR_IN || pCfgReq->enmDir == PDMAUDIODIR_OUT, VERR_INVALID_PARAMETER);
    Assert(PDMAudioStrmCfgEquals(pCfgReq, pCfgAcq));
    int rc;

    /*
     * Prepare name, sample spec and the stream instance data.
     */
    char szName[256];
    RTStrPrintf(szName, sizeof(szName), "VirtualBox %s [%s]", PDMAudioPathGetName(pCfgReq->enmPath), pThis->szStreamName);

    pStreamPA->pDrv                = pThis;
    pStreamPA->pDrainOp            = NULL;
    pStreamPA->pbPeekBuf           = NULL;
    pStreamPA->SampleSpec.rate     = PDMAudioPropsHz(&pCfgReq->Props);
    pStreamPA->SampleSpec.channels = PDMAudioPropsChannels(&pCfgReq->Props);
    pStreamPA->SampleSpec.format   = drvHstAudPaPropsToPulse(&pCfgReq->Props);

    /*
     * Initialize the channelmap.  This may change the channel count.
     */
    AssertCompile(RT_ELEMENTS(pStreamPA->ChannelMap.map) >= PDMAUDIO_MAX_CHANNELS);
    uint8_t const cSrcChannels = pStreamPA->ChannelMap.channels = PDMAudioPropsChannels(&pCfgReq->Props);
    uintptr_t iDst = 0;
    if (cSrcChannels == 1 && pCfgReq->Props.aidChannels[0] == PDMAUDIOCHANNELID_MONO)
        pStreamPA->ChannelMap.map[iDst++] = PA_CHANNEL_POSITION_MONO;
    else
    {
        uintptr_t iSrc;
        for (iSrc = iDst = 0; iSrc < cSrcChannels; iSrc++)
        {
            pStreamPA->ChannelMap.map[iDst] = drvHstAudPaConvertChannelId(pCfgReq->Props.aidChannels[iSrc]);
            if (pStreamPA->ChannelMap.map[iDst] != PA_CHANNEL_POSITION_INVALID)
                iDst++;
            else
            {
                LogRel2(("PulseAudio: Dropping channel #%u (%d/%s)\n", iSrc, pCfgReq->Props.aidChannels[iSrc],
                         PDMAudioChannelIdGetName((PDMAUDIOCHANNELID)pCfgReq->Props.aidChannels[iSrc])));
                pStreamPA->ChannelMap.channels--;
                pStreamPA->SampleSpec.channels--;
                PDMAudioPropsSetChannels(&pCfgAcq->Props, pStreamPA->SampleSpec.channels);
            }
        }
        Assert(iDst == pStreamPA->ChannelMap.channels);
    }
    while (iDst < RT_ELEMENTS(pStreamPA->ChannelMap.map))
        pStreamPA->ChannelMap.map[iDst++] = PA_CHANNEL_POSITION_INVALID;

    LogFunc(("Opening '%s', rate=%dHz, channels=%d (%d), format=%s\n", szName, pStreamPA->SampleSpec.rate,
             pStreamPA->SampleSpec.channels, cSrcChannels, pa_sample_format_to_string(pStreamPA->SampleSpec.format)));

    if (pa_sample_spec_valid(&pStreamPA->SampleSpec))
    {
        /*
         * Set up buffer attributes according to the stream type.
         *
         * For output streams we configure pre-buffering as requested, since
         * there is little point in using a different size than DrvAudio. This
         * assumes that a 'drain' request will override the prebuf size.
         */
        pStreamPA->BufAttr.maxlength = UINT32_MAX; /* Let the PulseAudio server choose the biggest size it can handle. */
        if (pCfgReq->enmDir == PDMAUDIODIR_IN)
        {
            pStreamPA->BufAttr.fragsize  = PDMAudioPropsFramesToBytes(&pCfgAcq->Props, pCfgReq->Backend.cFramesPeriod);
            LogFunc(("Requesting: BufAttr: fragsize=%RU32\n", pStreamPA->BufAttr.fragsize));
            /* (rlength, minreq and prebuf are playback only) */
        }
        else
        {
            pStreamPA->cUsLatency        = PDMAudioPropsFramesToMicro(&pCfgAcq->Props, pCfgReq->Backend.cFramesBufferSize);
            pStreamPA->BufAttr.tlength   = pa_usec_to_bytes(pStreamPA->cUsLatency, &pStreamPA->SampleSpec);
            pStreamPA->BufAttr.minreq    = PDMAudioPropsFramesToBytes(&pCfgAcq->Props, pCfgReq->Backend.cFramesPeriod);
            pStreamPA->BufAttr.prebuf    = pa_usec_to_bytes(PDMAudioPropsFramesToMicro(&pCfgAcq->Props,
                                                                                       pCfgReq->Backend.cFramesPreBuffering),
                                                            &pStreamPA->SampleSpec);
            /* (fragsize is capture only) */
            LogRel2(("PulseAudio: Initial output latency is %RU64 us (%RU32 bytes)\n",
                     pStreamPA->cUsLatency, pStreamPA->BufAttr.tlength));
            LogFunc(("Requesting: BufAttr: tlength=%RU32 maxLength=%RU32 minReq=%RU32 maxlength=-1\n",
                     pStreamPA->BufAttr.tlength, pStreamPA->BufAttr.maxlength, pStreamPA->BufAttr.minreq));
        }

        /*
         * Do the actual PA stream creation.
         */
        pa_threaded_mainloop_lock(pThis->pMainLoop);
        rc = drvHstAudPaStreamCreateLocked(pThis, pStreamPA, szName, pCfgAcq);
        pa_threaded_mainloop_unlock(pThis->pMainLoop);
        if (RT_SUCCESS(rc))
        {
            /*
             * Set the acquired stream config according to the actual buffer
             * attributes we got and the stream type.
             */
            if (pCfgReq->enmDir == PDMAUDIODIR_IN)
            {
                pCfgAcq->Backend.cFramesPeriod       = PDMAudioPropsBytesToFrames(&pCfgAcq->Props, pStreamPA->BufAttr.fragsize);
                pCfgAcq->Backend.cFramesBufferSize   = pStreamPA->BufAttr.maxlength != UINT32_MAX /* paranoia */
                                                     ? PDMAudioPropsBytesToFrames(&pCfgAcq->Props, pStreamPA->BufAttr.maxlength)
                                                     : pCfgAcq->Backend.cFramesPeriod * 2 /* whatever */;
                pCfgAcq->Backend.cFramesPreBuffering = pCfgAcq->Backend.cFramesPeriod;
            }
            else
            {
                pCfgAcq->Backend.cFramesPeriod        = PDMAudioPropsBytesToFrames(&pCfgAcq->Props, pStreamPA->BufAttr.minreq);
                pCfgAcq->Backend.cFramesBufferSize    = PDMAudioPropsBytesToFrames(&pCfgAcq->Props, pStreamPA->BufAttr.tlength);
                pCfgAcq->Backend.cFramesPreBuffering  = pCfgReq->Backend.cFramesPreBuffering
                                                      * pCfgAcq->Backend.cFramesBufferSize
                                                      / RT_MAX(pCfgReq->Backend.cFramesBufferSize, 1);
            }

            /*
             * Translate back the channel mapping.
             */
            for (iDst = 0; iDst < pStreamPA->ChannelMap.channels; iDst++)
                 pCfgReq->Props.aidChannels[iDst] = drvHstAudPaConvertChannelPos(pStreamPA->ChannelMap.map[iDst]);
            while (iDst < RT_ELEMENTS(pCfgReq->Props.aidChannels))
                pCfgReq->Props.aidChannels[iDst++] = PDMAUDIOCHANNELID_INVALID;

            PDMAudioStrmCfgCopy(&pStreamPA->Cfg, pCfgAcq);
        }
    }
    else
    {
        LogRel(("PulseAudio: Unsupported sample specification for stream '%s'\n", szName));
        rc = VERR_AUDIO_STREAM_COULD_NOT_CREATE;
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Cancel and release any pending stream requests (drain and cork/uncork).
 *
 * @note Caller has locked the mainloop.
 */
static void drvHstAudPaStreamCancelAndReleaseOperations(PDRVHSTAUDPASTREAM pStreamPA)
{
    if (pStreamPA->pDrainOp)
    {
        LogFlowFunc(("drain operation (%p) status: %d\n", pStreamPA->pDrainOp, pa_operation_get_state(pStreamPA->pDrainOp)));
        pa_operation_cancel(pStreamPA->pDrainOp);
        pa_operation_unref(pStreamPA->pDrainOp);
        pStreamPA->pDrainOp = NULL;
    }

    if (pStreamPA->pCorkOp)
    {
        LogFlowFunc(("cork operation (%p) status: %d\n", pStreamPA->pCorkOp, pa_operation_get_state(pStreamPA->pCorkOp)));
        pa_operation_cancel(pStreamPA->pCorkOp);
        pa_operation_unref(pStreamPA->pCorkOp);
        pStreamPA->pCorkOp = NULL;
    }

    if (pStreamPA->pTriggerOp)
    {
        LogFlowFunc(("trigger operation (%p) status: %d\n", pStreamPA->pTriggerOp, pa_operation_get_state(pStreamPA->pTriggerOp)));
        pa_operation_cancel(pStreamPA->pTriggerOp);
        pa_operation_unref(pStreamPA->pTriggerOp);
        pStreamPA->pTriggerOp = NULL;
    }
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamDestroy}
 */
static DECLCALLBACK(int) drvHstAudPaHA_StreamDestroy(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream, bool fImmediate)
{
    PDRVHSTAUDPA        pThis     = RT_FROM_MEMBER(pInterface, DRVHSTAUDPA, IHostAudio);
    PDRVHSTAUDPASTREAM  pStreamPA = (PDRVHSTAUDPASTREAM)pStream;
    AssertPtrReturn(pStreamPA, VERR_INVALID_POINTER);
    RT_NOREF(fImmediate);

    if (pStreamPA->pStream)
    {
        pa_threaded_mainloop_lock(pThis->pMainLoop);

        drvHstAudPaStreamCancelAndReleaseOperations(pStreamPA);
        pa_stream_disconnect(pStreamPA->pStream);

        pa_stream_unref(pStreamPA->pStream);
        pStreamPA->pStream = NULL;

        pa_threaded_mainloop_unlock(pThis->pMainLoop);
    }

    return VINF_SUCCESS;
}


/**
 * Common worker for the cork/uncork completion callbacks.
 * @note This is fully async, so nobody is waiting for this.
 */
static void drvHstAudPaStreamCorkUncorkCommon(PDRVHSTAUDPASTREAM pStreamPA, int fSuccess, const char *pszOperation)
{
    AssertPtrReturnVoid(pStreamPA);
    LogFlowFunc(("%s '%s': fSuccess=%RTbool\n", pszOperation, pStreamPA->Cfg.szName, fSuccess));

    if (!fSuccess)
        drvHstAudPaError(pStreamPA->pDrv, "%s stream '%s' failed", pszOperation, pStreamPA->Cfg.szName);

    if (pStreamPA->pCorkOp)
    {
        pa_operation_unref(pStreamPA->pCorkOp);
        pStreamPA->pCorkOp = NULL;
    }
}


/**
 * Completion callback used with pa_stream_cork(,false,).
 */
static void drvHstAudPaStreamUncorkCompletionCallback(pa_stream *pStream, int fSuccess, void *pvUser)
{
    RT_NOREF(pStream);
    drvHstAudPaStreamCorkUncorkCommon((PDRVHSTAUDPASTREAM)pvUser, fSuccess, "Uncorking");
}


/**
 * Completion callback used with pa_stream_cork(,true,).
 */
static void drvHstAudPaStreamCorkCompletionCallback(pa_stream *pStream, int fSuccess, void *pvUser)
{
    RT_NOREF(pStream);
    drvHstAudPaStreamCorkUncorkCommon((PDRVHSTAUDPASTREAM)pvUser, fSuccess, "Corking");
}


/**
 * @ interface_method_impl{PDMIHOSTAUDIO,pfnStreamEnable}
 */
static DECLCALLBACK(int) drvHstAudPaHA_StreamEnable(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream)
{
    PDRVHSTAUDPA        pThis     = RT_FROM_MEMBER(pInterface, DRVHSTAUDPA, IHostAudio);
    PDRVHSTAUDPASTREAM  pStreamPA = (PDRVHSTAUDPASTREAM)pStream;
    LogFlowFunc(("\n"));

    /*
     * Uncork (start or resume playback/capture) the stream.
     */
    pa_threaded_mainloop_lock(pThis->pMainLoop);

    drvHstAudPaStreamCancelAndReleaseOperations(pStreamPA);
    pStreamPA->pCorkOp = pa_stream_cork(pStreamPA->pStream, 0 /*uncork it*/,
                                        drvHstAudPaStreamUncorkCompletionCallback, pStreamPA);
    LogFlowFunc(("Uncorking '%s': %p (async)\n", pStreamPA->Cfg.szName, pStreamPA->pCorkOp));
    int const rc = pStreamPA->pCorkOp ? VINF_SUCCESS
                 : drvHstAudPaError(pThis, "pa_stream_cork('%s', 0 /*uncork it*/,,) failed", pStreamPA->Cfg.szName);


    pa_threaded_mainloop_unlock(pThis->pMainLoop);

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}


/**
 * @ interface_method_impl{PDMIHOSTAUDIO,pfnStreamDisable}
 */
static DECLCALLBACK(int) drvHstAudPaHA_StreamDisable(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream)
{
    PDRVHSTAUDPA        pThis     = RT_FROM_MEMBER(pInterface, DRVHSTAUDPA, IHostAudio);
    PDRVHSTAUDPASTREAM  pStreamPA = (PDRVHSTAUDPASTREAM)pStream;
    LogFlowFunc(("\n"));

    pa_threaded_mainloop_lock(pThis->pMainLoop);

    /*
     * For output streams, we will ignore the request if there is a pending drain
     * as it will cork the stream in the end.
     */
    if (pStreamPA->Cfg.enmDir == PDMAUDIODIR_OUT)
    {
        if (pStreamPA->pDrainOp)
        {
            pa_operation_state_t const enmOpState = pa_operation_get_state(pStreamPA->pDrainOp);
            if (enmOpState == PA_OPERATION_RUNNING)
            {
/** @todo consider corking it immediately instead, as that's what the caller
 *        wants now... */
                LogFlowFunc(("Drain (%p) already running on '%s', skipping.\n", pStreamPA->pDrainOp, pStreamPA->Cfg.szName));
                pa_threaded_mainloop_unlock(pThis->pMainLoop);
                return VINF_SUCCESS;
            }
            LogFlowFunc(("Drain (%p) not running: %d\n", pStreamPA->pDrainOp, enmOpState));
        }
    }
    /*
     * For input stream we always cork it, but we clean up the peek buffer first.
     */
    /** @todo r=bird: It is (probably) not technically be correct to drop the peek buffer
     *        here when we're only pausing the stream (VM paused) as it means we'll
     *        risk underruns when later resuming. */
    else if (pStreamPA->pbPeekBuf) /** @todo Do we need to drop the peek buffer?*/
    {
        pStreamPA->pbPeekBuf  = NULL;
        pStreamPA->cbPeekBuf  = 0;
        pa_stream_drop(pStreamPA->pStream);
    }

    /*
     * Cork (pause playback/capture) the stream.
     */
    drvHstAudPaStreamCancelAndReleaseOperations(pStreamPA);
    pStreamPA->pCorkOp = pa_stream_cork(pStreamPA->pStream, 1 /* cork it */,
                                        drvHstAudPaStreamCorkCompletionCallback, pStreamPA);
    LogFlowFunc(("Corking '%s': %p (async)\n", pStreamPA->Cfg.szName, pStreamPA->pCorkOp));
    int const rc = pStreamPA->pCorkOp ? VINF_SUCCESS
                 : drvHstAudPaError(pThis, "pa_stream_cork('%s', 1 /*cork*/,,) failed", pStreamPA->Cfg.szName);

    pa_threaded_mainloop_unlock(pThis->pMainLoop);
    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}


/**
 * @ interface_method_impl{PDMIHOSTAUDIO,pfnStreamPause}
 */
static DECLCALLBACK(int) drvHstAudPaHA_StreamPause(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream)
{
    /* Same as disable. */
    return drvHstAudPaHA_StreamDisable(pInterface, pStream);
}


/**
 * @ interface_method_impl{PDMIHOSTAUDIO,pfnStreamResume}
 */
static DECLCALLBACK(int) drvHstAudPaHA_StreamResume(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream)
{
    /* Same as enable. */
    return drvHstAudPaHA_StreamEnable(pInterface, pStream);
}


/**
 * Pulse audio pa_stream_drain() completion callback.
 * @note This is fully async, so nobody is waiting for this.
 */
static void drvHstAudPaStreamDrainCompletionCallback(pa_stream *pStream, int fSuccess, void *pvUser)
{
    PDRVHSTAUDPASTREAM pStreamPA = (PDRVHSTAUDPASTREAM)pvUser;
    AssertPtrReturnVoid(pStreamPA);
    Assert(pStreamPA->pStream == pStream);
    LogFlowFunc(("'%s': fSuccess=%RTbool\n", pStreamPA->Cfg.szName, fSuccess));

    if (!fSuccess)
        drvHstAudPaError(pStreamPA->pDrv, "Draining stream '%s' failed", pStreamPA->Cfg.szName);

    /* Now cork the stream (doing it unconditionally atm). */
    if (pStreamPA->pCorkOp)
    {
        LogFlowFunc(("Cancelling & releasing cork/uncork operation %p (state: %d)\n",
                     pStreamPA->pCorkOp, pa_operation_get_state(pStreamPA->pCorkOp)));
        pa_operation_cancel(pStreamPA->pCorkOp);
        pa_operation_unref(pStreamPA->pCorkOp);
    }

    pStreamPA->pCorkOp = pa_stream_cork(pStream, 1 /* cork it*/, drvHstAudPaStreamCorkCompletionCallback, pStreamPA);
    if (pStreamPA->pCorkOp)
        LogFlowFunc(("Started cork operation %p of %s (following drain)\n", pStreamPA->pCorkOp, pStreamPA->Cfg.szName));
    else
        drvHstAudPaError(pStreamPA->pDrv, "pa_stream_cork failed on '%s' (following drain)", pStreamPA->Cfg.szName);
}


/**
 * Callback used with pa_stream_tigger(), starts draining.
 */
static void drvHstAudPaStreamTriggerCompletionCallback(pa_stream *pStream, int fSuccess, void *pvUser)
{
    PDRVHSTAUDPASTREAM pStreamPA = (PDRVHSTAUDPASTREAM)pvUser;
    AssertPtrReturnVoid(pStreamPA);
    RT_NOREF(pStream);
    LogFlowFunc(("'%s': fSuccess=%RTbool\n", pStreamPA->Cfg.szName, fSuccess));

    if (!fSuccess)
        drvHstAudPaError(pStreamPA->pDrv, "Forcing playback before drainig '%s' failed", pStreamPA->Cfg.szName);

    if (pStreamPA->pTriggerOp)
    {
        pa_operation_unref(pStreamPA->pTriggerOp);
        pStreamPA->pTriggerOp = NULL;
    }
}


/**
 * @ interface_method_impl{PDMIHOSTAUDIO,pfnStreamDrain}
 */
static DECLCALLBACK(int) drvHstAudPaHA_StreamDrain(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream)
{
    PDRVHSTAUDPA        pThis     = RT_FROM_MEMBER(pInterface, DRVHSTAUDPA, IHostAudio);
    PDRVHSTAUDPASTREAM  pStreamPA = (PDRVHSTAUDPASTREAM)pStream;
    AssertReturn(pStreamPA->Cfg.enmDir == PDMAUDIODIR_OUT, VERR_INVALID_PARAMETER);
    LogFlowFunc(("\n"));

    pa_threaded_mainloop_lock(pThis->pMainLoop);

    /*
     * If there is a drain running already, don't try issue another as pulse
     * doesn't support more than one concurrent drain per stream.
     */
    if (pStreamPA->pDrainOp)
    {
        if (pa_operation_get_state(pStreamPA->pDrainOp) == PA_OPERATION_RUNNING)
        {
            pa_threaded_mainloop_unlock(pThis->pMainLoop);
            LogFlowFunc(("returns VINF_SUCCESS (drain already running)\n"));
            return VINF_SUCCESS;
        }
        LogFlowFunc(("Releasing drain operation %p (state: %d)\n", pStreamPA->pDrainOp, pa_operation_get_state(pStreamPA->pDrainOp)));
        pa_operation_unref(pStreamPA->pDrainOp);
        pStreamPA->pDrainOp = NULL;
    }

    /*
     * Make sure pre-buffered data is played before we drain it.
     *
     * ASSUMES that the async stream requests are executed in the order they're
     * issued here, so that we avoid waiting for the trigger request to complete.
     */
    int rc = VINF_SUCCESS;
    if (true /** @todo skip this if we're already playing or haven't written any data to the stream since xxxx. */)
    {
        if (pStreamPA->pTriggerOp)
        {
            LogFlowFunc(("Cancelling & releasing trigger operation %p (state: %d)\n",
                         pStreamPA->pTriggerOp, pa_operation_get_state(pStreamPA->pTriggerOp)));
            pa_operation_cancel(pStreamPA->pTriggerOp);
            pa_operation_unref(pStreamPA->pTriggerOp);
        }
        pStreamPA->pTriggerOp = pa_stream_trigger(pStreamPA->pStream, drvHstAudPaStreamTriggerCompletionCallback, pStreamPA);
        if (pStreamPA->pTriggerOp)
            LogFlowFunc(("Started tigger operation %p on %s\n", pStreamPA->pTriggerOp, pStreamPA->Cfg.szName));
        else
            rc = drvHstAudPaError(pStreamPA->pDrv, "pa_stream_trigger failed on '%s'", pStreamPA->Cfg.szName);
    }

    /*
     * Initiate the draining (async), will cork the stream when it completes.
     */
    pStreamPA->pDrainOp = pa_stream_drain(pStreamPA->pStream, drvHstAudPaStreamDrainCompletionCallback, pStreamPA);
    if (pStreamPA->pDrainOp)
        LogFlowFunc(("Started drain operation %p of %s\n", pStreamPA->pDrainOp, pStreamPA->Cfg.szName));
    else
        rc = drvHstAudPaError(pStreamPA->pDrv, "pa_stream_drain failed on '%s'", pStreamPA->Cfg.szName);

    pa_threaded_mainloop_unlock(pThis->pMainLoop);
    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamControl}
 */
static DECLCALLBACK(int) drvHstAudPaHA_StreamControl(PPDMIHOSTAUDIO pInterface,
                                                     PPDMAUDIOBACKENDSTREAM pStream, PDMAUDIOSTREAMCMD enmStreamCmd)
{
    /** @todo r=bird: I'd like to get rid of this pfnStreamControl method,
     *        replacing it with individual StreamXxxx methods.  That would save us
     *        potentally huge switches and more easily see which drivers implement
     *        which operations (grep for pfnStreamXxxx). */
    switch (enmStreamCmd)
    {
        case PDMAUDIOSTREAMCMD_ENABLE:
            return drvHstAudPaHA_StreamEnable(pInterface, pStream);
        case PDMAUDIOSTREAMCMD_DISABLE:
            return drvHstAudPaHA_StreamDisable(pInterface, pStream);
        case PDMAUDIOSTREAMCMD_PAUSE:
            return drvHstAudPaHA_StreamPause(pInterface, pStream);
        case PDMAUDIOSTREAMCMD_RESUME:
            return drvHstAudPaHA_StreamResume(pInterface, pStream);
        case PDMAUDIOSTREAMCMD_DRAIN:
            return drvHstAudPaHA_StreamDrain(pInterface, pStream);

        case PDMAUDIOSTREAMCMD_END:
        case PDMAUDIOSTREAMCMD_32BIT_HACK:
        case PDMAUDIOSTREAMCMD_INVALID:
            /* no default*/
            break;
    }
    return VERR_NOT_SUPPORTED;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamGetReadable}
 */
static DECLCALLBACK(uint32_t) drvHstAudPaHA_StreamGetReadable(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream)
{
    PDRVHSTAUDPA        pThis      = RT_FROM_MEMBER(pInterface, DRVHSTAUDPA, IHostAudio);
    PDRVHSTAUDPASTREAM  pStreamPA  = (PDRVHSTAUDPASTREAM)pStream;
    uint32_t            cbReadable = 0;
    if (pStreamPA->Cfg.enmDir == PDMAUDIODIR_IN)
    {
        pa_threaded_mainloop_lock(pThis->pMainLoop);

        pa_stream_state_t const enmState = pa_stream_get_state(pStreamPA->pStream);
        if (PA_STREAM_IS_GOOD(enmState))
        {
            size_t cbReadablePa = pa_stream_readable_size(pStreamPA->pStream);
            if (cbReadablePa != (size_t)-1)
                cbReadable = (uint32_t)cbReadablePa;
            else
                drvHstAudPaError(pThis, "pa_stream_readable_size failed on '%s'", pStreamPA->Cfg.szName);
        }
        else
            LogFunc(("non-good stream state: %d\n", enmState));

        pa_threaded_mainloop_unlock(pThis->pMainLoop);
    }
    Log3Func(("returns %#x (%u)\n", cbReadable, cbReadable));
    return cbReadable;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamGetWritable}
 */
static DECLCALLBACK(uint32_t) drvHstAudPaHA_StreamGetWritable(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream)
{
    PDRVHSTAUDPA        pThis      = RT_FROM_MEMBER(pInterface, DRVHSTAUDPA, IHostAudio);
    PDRVHSTAUDPASTREAM  pStreamPA  = (PDRVHSTAUDPASTREAM)pStream;
    uint32_t            cbWritable = 0;
    if (pStreamPA->Cfg.enmDir == PDMAUDIODIR_OUT)
    {
        pa_threaded_mainloop_lock(pThis->pMainLoop);

        pa_stream_state_t const enmState = pa_stream_get_state(pStreamPA->pStream);
        if (PA_STREAM_IS_GOOD(enmState))
        {
            size_t cbWritablePa = pa_stream_writable_size(pStreamPA->pStream);
            if (cbWritablePa != (size_t)-1)
                cbWritable = cbWritablePa <= UINT32_MAX ? (uint32_t)cbWritablePa : UINT32_MAX;
            else
                drvHstAudPaError(pThis, "pa_stream_writable_size failed on '%s'", pStreamPA->Cfg.szName);
        }
        else
            LogFunc(("non-good stream state: %d\n", enmState));

        pa_threaded_mainloop_unlock(pThis->pMainLoop);
    }
    Log3Func(("returns %#x (%u) [max=%#RX32 min=%#RX32]\n",
              cbWritable, cbWritable, pStreamPA->BufAttr.maxlength, pStreamPA->BufAttr.minreq));
    return cbWritable;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamGetState}
 */
static DECLCALLBACK(PDMHOSTAUDIOSTREAMSTATE) drvHstAudPaHA_StreamGetState(PPDMIHOSTAUDIO pInterface,
                                                                          PPDMAUDIOBACKENDSTREAM pStream)
{
    PDRVHSTAUDPA        pThis     = RT_FROM_MEMBER(pInterface, DRVHSTAUDPA, IHostAudio);
    AssertPtrReturn(pStream, PDMHOSTAUDIOSTREAMSTATE_INVALID);
    PDRVHSTAUDPASTREAM  pStreamPA = (PDRVHSTAUDPASTREAM)pStream;
    AssertPtrReturn(pStreamPA, PDMHOSTAUDIOSTREAMSTATE_INVALID);

    /* Check PulseAudio's general status. */
    PDMHOSTAUDIOSTREAMSTATE enmBackendStreamState = PDMHOSTAUDIOSTREAMSTATE_NOT_WORKING;
    if (pThis->pContext)
    {
        pa_context_state_t const enmPaCtxState = pa_context_get_state(pThis->pContext);
        if (PA_CONTEXT_IS_GOOD(enmPaCtxState))
        {
            pa_stream_state_t const enmPaStreamState = pa_stream_get_state(pStreamPA->pStream);
            if (PA_STREAM_IS_GOOD(enmPaStreamState))
            {
                if (enmPaStreamState != PA_STREAM_CREATING)
                {
                    if (   pStreamPA->Cfg.enmDir != PDMAUDIODIR_OUT
                        || pStreamPA->pDrainOp == NULL
                        || pa_operation_get_state(pStreamPA->pDrainOp) != PA_OPERATION_RUNNING)
                        enmBackendStreamState = PDMHOSTAUDIOSTREAMSTATE_OKAY;
                    else
                        enmBackendStreamState = PDMHOSTAUDIOSTREAMSTATE_DRAINING;
                }
                else
                    enmBackendStreamState = PDMHOSTAUDIOSTREAMSTATE_INITIALIZING;
            }
            else
                LogFunc(("non-good PA stream state: %d\n", enmPaStreamState));
        }
        else
            LogFunc(("non-good PA context state: %d\n", enmPaCtxState));
    }
    else
        LogFunc(("No context!\n"));
    LogFlowFunc(("returns %s for stream '%s'\n", PDMHostAudioStreamStateGetName(enmBackendStreamState), pStreamPA->Cfg.szName));
    return enmBackendStreamState;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamPlay}
 */
static DECLCALLBACK(int) drvHstAudPaHA_StreamPlay(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream,
                                                  const void *pvBuf, uint32_t cbBuf, uint32_t *pcbWritten)
{
    PDRVHSTAUDPA        pThis     = RT_FROM_MEMBER(pInterface, DRVHSTAUDPA, IHostAudio);
    PDRVHSTAUDPASTREAM  pStreamPA = (PDRVHSTAUDPASTREAM)pStream;
    AssertPtrReturn(pStreamPA, VERR_INVALID_POINTER);
    AssertPtrReturn(pcbWritten, VERR_INVALID_POINTER);
    if (cbBuf)
        AssertPtrReturn(pvBuf, VERR_INVALID_POINTER);
    else
    {
        /* Fend off draining calls. */
        *pcbWritten = 0;
        return VINF_SUCCESS;
    }

    pa_threaded_mainloop_lock(pThis->pMainLoop);

#ifdef LOG_ENABLED
    const pa_usec_t tsNowUs = pa_rtclock_now();
    Log3Func(("play delta: %'RU64 us; cbBuf=%#x\n", tsNowUs - pStreamPA->tsLastReadWrittenUs, cbBuf));
    pStreamPA->tsLastReadWrittenUs = tsNowUs;
#endif

    /*
     * Using a loop here so we can take maxlength into account when writing.
     */
    int      rc             = VINF_SUCCESS;
    uint32_t cbTotalWritten = 0;
    uint32_t iLoop;
    for (iLoop = 0; ; iLoop++)
    {
        size_t const cbWriteable = pa_stream_writable_size(pStreamPA->pStream);
        if (   cbWriteable != (size_t)-1
            && cbWriteable >= PDMAudioPropsFrameSize(&pStreamPA->Cfg.Props))
        {
            uint32_t cbToWrite = (uint32_t)RT_MIN(RT_MIN(cbWriteable, pStreamPA->BufAttr.maxlength), cbBuf);
            cbToWrite = PDMAudioPropsFloorBytesToFrame(&pStreamPA->Cfg.Props, cbToWrite);
            if (pa_stream_write(pStreamPA->pStream, pvBuf, cbToWrite, NULL /*pfnFree*/, 0 /*offset*/, PA_SEEK_RELATIVE) >= 0)
            {
                cbTotalWritten += cbToWrite;
                cbBuf          -= cbToWrite;
                if (!cbBuf)
                    break;
                pvBuf = (uint8_t const *)pvBuf + cbToWrite;
                Log3Func(("%#x left to write\n", cbBuf));
            }
            else
            {
                rc = drvHstAudPaError(pStreamPA->pDrv, "Failed to write to output stream");
                break;
            }
        }
        else
        {
            if (cbWriteable == (size_t)-1)
                rc = drvHstAudPaError(pStreamPA->pDrv, "pa_stream_writable_size failed on '%s'", pStreamPA->Cfg.szName);
            break;
        }
    }

    pa_threaded_mainloop_unlock(pThis->pMainLoop);

    *pcbWritten = cbTotalWritten;
    if (RT_SUCCESS(rc) || cbTotalWritten == 0)
    { /* likely */ }
    else
    {
        LogFunc(("Supressing %Rrc because we wrote %#x bytes\n", rc, cbTotalWritten));
        rc = VINF_SUCCESS;
    }
    Log3Func(("returns %Rrc *pcbWritten=%#x iLoop=%u\n", rc, cbTotalWritten, iLoop));
    return rc;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamCapture}
 */
static DECLCALLBACK(int) drvHstAudPaHA_StreamCapture(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream,
                                                     void *pvBuf, uint32_t cbBuf, uint32_t *pcbRead)
{
    PDRVHSTAUDPA        pThis     = RT_FROM_MEMBER(pInterface, DRVHSTAUDPA, IHostAudio);
    PDRVHSTAUDPASTREAM  pStreamPA = (PDRVHSTAUDPASTREAM)pStream;
    AssertPtrReturn(pStreamPA, VERR_INVALID_POINTER);
    AssertPtrReturn(pvBuf, VERR_INVALID_POINTER);
    AssertReturn(cbBuf, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pcbRead, VERR_INVALID_POINTER);

#ifdef LOG_ENABLED
    const pa_usec_t tsNowUs = pa_rtclock_now();
    Log3Func(("capture delta: %'RU64 us; cbBuf=%#x\n", tsNowUs - pStreamPA->tsLastReadWrittenUs, cbBuf));
    pStreamPA->tsLastReadWrittenUs = tsNowUs;
#endif

    /*
     * If we have left over peek buffer space from the last call,
     * copy out the data from there.
     */
    uint32_t cbTotalRead = 0;
    if (   pStreamPA->pbPeekBuf
        && pStreamPA->offPeekBuf < pStreamPA->cbPeekBuf)
    {
        uint32_t cbToCopy = pStreamPA->cbPeekBuf - pStreamPA->offPeekBuf;
        if (cbToCopy >= cbBuf)
        {
            memcpy(pvBuf, &pStreamPA->pbPeekBuf[pStreamPA->offPeekBuf], cbBuf);
            pStreamPA->offPeekBuf += cbBuf;
            *pcbRead               = cbBuf;
            if (cbToCopy == cbBuf)
            {
                pa_threaded_mainloop_lock(pThis->pMainLoop);
                pStreamPA->pbPeekBuf  = NULL;
                pStreamPA->cbPeekBuf  = 0;
                pa_stream_drop(pStreamPA->pStream);
                pa_threaded_mainloop_unlock(pThis->pMainLoop);
            }
            Log3Func(("returns *pcbRead=%#x from prev peek buf (%#x/%#x)\n", cbBuf, pStreamPA->offPeekBuf, pStreamPA->cbPeekBuf));
            return VINF_SUCCESS;
        }

        memcpy(pvBuf, &pStreamPA->pbPeekBuf[pStreamPA->offPeekBuf], cbToCopy);
        cbBuf       -= cbToCopy;
        pvBuf        = (uint8_t *)pvBuf + cbToCopy;
        cbTotalRead += cbToCopy;
        pStreamPA->offPeekBuf = pStreamPA->cbPeekBuf;
    }

    /*
     * Copy out what we can.
     */
    int rc = VINF_SUCCESS;
    pa_threaded_mainloop_lock(pThis->pMainLoop);
    while (cbBuf > 0)
    {
        /*
         * Drop the old peek buffer first, if we have one.
         */
        if (pStreamPA->pbPeekBuf)
        {
            Assert(pStreamPA->offPeekBuf >= pStreamPA->cbPeekBuf);
            pStreamPA->pbPeekBuf  = NULL;
            pStreamPA->cbPeekBuf  = 0;
            pa_stream_drop(pStreamPA->pStream);
        }

        /*
         * Check if there is anything to read, the get the peek buffer for it.
         */
        size_t cbAvail = pa_stream_readable_size(pStreamPA->pStream);
        if (cbAvail > 0 && cbAvail != (size_t)-1)
        {
            pStreamPA->pbPeekBuf  = NULL;
            pStreamPA->cbPeekBuf  = 0;
            int rcPa = pa_stream_peek(pStreamPA->pStream, (const void **)&pStreamPA->pbPeekBuf, &pStreamPA->cbPeekBuf);
            if (rcPa == 0)
            {
                if (pStreamPA->cbPeekBuf)
                {
                    if (pStreamPA->pbPeekBuf)
                    {
                        /*
                         * We got data back. Copy it into the return buffer, return if it's full.
                         */
                        if (cbBuf < pStreamPA->cbPeekBuf)
                        {
                            memcpy(pvBuf, pStreamPA->pbPeekBuf, cbBuf);
                            cbTotalRead          += cbBuf;
                            pStreamPA->offPeekBuf = cbBuf;
                            cbBuf = 0;
                            break;
                        }
                        memcpy(pvBuf, pStreamPA->pbPeekBuf, pStreamPA->cbPeekBuf);
                        cbBuf       -= pStreamPA->cbPeekBuf;
                        pvBuf        = (uint8_t *)pvBuf + pStreamPA->cbPeekBuf;
                        cbTotalRead += pStreamPA->cbPeekBuf;

                        pStreamPA->pbPeekBuf = NULL;
                    }
                    else
                    {
                        /*
                         * We got a hole (drop needed). We will skip it as we leave it to
                         * the device's DMA engine to fill in buffer gaps with silence.
                         */
                        LogFunc(("pa_stream_peek returned a %#zx (%zu) byte hole - skipping.\n",
                                 pStreamPA->cbPeekBuf, pStreamPA->cbPeekBuf));
                    }
                    pStreamPA->cbPeekBuf = 0;
                    pa_stream_drop(pStreamPA->pStream);
                }
                else
                {
                    Assert(!pStreamPA->pbPeekBuf);
                    LogFunc(("pa_stream_peek returned empty buffer\n"));
                    break;
                }
            }
            else
            {
                rc = drvHstAudPaError(pStreamPA->pDrv, "pa_stream_peek failed on '%s' (%d)", pStreamPA->Cfg.szName, rcPa);
                pStreamPA->pbPeekBuf  = NULL;
                pStreamPA->cbPeekBuf  = 0;
                break;
            }
        }
        else
        {
            if (cbAvail != (size_t)-1)
                rc = drvHstAudPaError(pStreamPA->pDrv, "pa_stream_readable_size failed on '%s'", pStreamPA->Cfg.szName);
            break;
        }
    }
    pa_threaded_mainloop_unlock(pThis->pMainLoop);

    *pcbRead = cbTotalRead;
    if (RT_SUCCESS(rc) || cbTotalRead == 0)
    { /* likely */ }
    else
    {
        LogFunc(("Supressing %Rrc because we're returning %#x bytes\n", rc, cbTotalRead));
        rc = VINF_SUCCESS;
    }
    Log3Func(("returns %Rrc *pcbRead=%#x (%#x left, peek %#x/%#x)\n",
              rc, cbTotalRead, cbBuf, pStreamPA->offPeekBuf, pStreamPA->cbPeekBuf));
    return rc;
}


/*********************************************************************************************************************************
*   PDMIBASE                                                                                                                     *
*********************************************************************************************************************************/

/**
 * @interface_method_impl{PDMIBASE,pfnQueryInterface}
 */
static DECLCALLBACK(void *) drvHstAudPaQueryInterface(PPDMIBASE pInterface, const char *pszIID)
{
    AssertPtrReturn(pInterface, NULL);
    AssertPtrReturn(pszIID, NULL);

    PPDMDRVINS   pDrvIns = PDMIBASE_2_PDMDRV(pInterface);
    PDRVHSTAUDPA pThis   = PDMINS_2_DATA(pDrvIns, PDRVHSTAUDPA);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIBASE, &pDrvIns->IBase);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIHOSTAUDIO, &pThis->IHostAudio);

    return NULL;
}


/*********************************************************************************************************************************
*   PDMDRVREG                                                                                                                    *
*********************************************************************************************************************************/

/**
 * Destructs a PulseAudio Audio driver instance.
 *
 * @copydoc FNPDMDRVDESTRUCT
 */
static DECLCALLBACK(void) drvHstAudPaDestruct(PPDMDRVINS pDrvIns)
{
    PDMDRV_CHECK_VERSIONS_RETURN_VOID(pDrvIns);
    PDRVHSTAUDPA pThis = PDMINS_2_DATA(pDrvIns, PDRVHSTAUDPA);
    LogFlowFuncEnter();

    if (pThis->pMainLoop)
        pa_threaded_mainloop_stop(pThis->pMainLoop);

    if (pThis->pContext)
    {
        pa_context_disconnect(pThis->pContext);
        pa_context_unref(pThis->pContext);
        pThis->pContext = NULL;
    }

    if (pThis->pMainLoop)
    {
        pa_threaded_mainloop_free(pThis->pMainLoop);
        pThis->pMainLoop = NULL;
    }

    LogFlowFuncLeave();
}


/**
 * Pulse audio callback for context status changes, init variant.
 *
 * Signalls our event semaphore so we can do a timed wait from
 * drvHstAudPaConstruct().
 */
static void drvHstAudPaCtxCallbackStateChangedInit(pa_context *pCtx, void *pvUser)
{
    AssertPtrReturnVoid(pCtx);
    PDRVHSTAUDPASTATECHGCTX pStateChgCtx = (PDRVHSTAUDPASTATECHGCTX)pvUser;
    pa_context_state_t     enmCtxState  = pa_context_get_state(pCtx);
    switch (enmCtxState)
    {
        case PA_CONTEXT_READY:
        case PA_CONTEXT_TERMINATED:
        case PA_CONTEXT_FAILED:
            AssertPtrReturnVoid(pStateChgCtx);
            pStateChgCtx->enmCtxState = enmCtxState;
            RTSemEventSignal(pStateChgCtx->hEvtInit);
            break;

        default:
            break;
    }
}


/**
 * Constructs a PulseAudio Audio driver instance.
 *
 * @copydoc FNPDMDRVCONSTRUCT
 */
static DECLCALLBACK(int) drvHstAudPaConstruct(PPDMDRVINS pDrvIns, PCFGMNODE pCfg, uint32_t fFlags)
{
    RT_NOREF(pCfg, fFlags);
    PDMDRV_CHECK_VERSIONS_RETURN(pDrvIns);
    PDRVHSTAUDPA pThis = PDMINS_2_DATA(pDrvIns, PDRVHSTAUDPA);
    LogRel(("Audio: Initializing PulseAudio driver\n"));

    /*
     * Initialize instance data.
     */
    pThis->pDrvIns                   = pDrvIns;
    /* IBase */
    pDrvIns->IBase.pfnQueryInterface = drvHstAudPaQueryInterface;
    /* IHostAudio */
    pThis->IHostAudio.pfnGetConfig                  = drvHstAudPaHA_GetConfig;
    pThis->IHostAudio.pfnGetDevices                 = drvHstAudPaHA_GetDevices;
    pThis->IHostAudio.pfnSetDevice                  = NULL;
    pThis->IHostAudio.pfnGetStatus                  = drvHstAudPaHA_GetStatus;
    pThis->IHostAudio.pfnDoOnWorkerThread           = NULL;
    pThis->IHostAudio.pfnStreamConfigHint           = NULL;
    pThis->IHostAudio.pfnStreamCreate               = drvHstAudPaHA_StreamCreate;
    pThis->IHostAudio.pfnStreamInitAsync            = NULL;
    pThis->IHostAudio.pfnStreamDestroy              = drvHstAudPaHA_StreamDestroy;
    pThis->IHostAudio.pfnStreamNotifyDeviceChanged  = NULL;
    pThis->IHostAudio.pfnStreamControl              = drvHstAudPaHA_StreamControl;
    pThis->IHostAudio.pfnStreamGetReadable          = drvHstAudPaHA_StreamGetReadable;
    pThis->IHostAudio.pfnStreamGetWritable          = drvHstAudPaHA_StreamGetWritable;
    pThis->IHostAudio.pfnStreamGetPending           = NULL;
    pThis->IHostAudio.pfnStreamGetState             = drvHstAudPaHA_StreamGetState;
    pThis->IHostAudio.pfnStreamPlay                 = drvHstAudPaHA_StreamPlay;
    pThis->IHostAudio.pfnStreamCapture              = drvHstAudPaHA_StreamCapture;

    /*
     * Read configuration.
     */
    int rc2 = CFGMR3QueryString(pCfg, "VmName", pThis->szStreamName, sizeof(pThis->szStreamName));
    AssertMsgRCReturn(rc2, ("Confguration error: No/bad \"VmName\" value, rc=%Rrc\n", rc2), rc2);

    /*
     * Load the pulse audio library.
     */
    int rc = audioLoadPulseLib();
    if (RT_SUCCESS(rc))
        LogRel(("PulseAudio: Using version %s\n", pa_get_library_version()));
    else
    {
        LogRel(("PulseAudio: Failed to load the PulseAudio shared library! Error %Rrc\n", rc));
        return rc;
    }

    /*
     * Set up the basic pulse audio bits (remember the destructore is always called).
     */
    //pThis->fAbortLoop = false;
    pThis->pMainLoop = pa_threaded_mainloop_new();
    if (!pThis->pMainLoop)
    {
        LogRel(("PulseAudio: Failed to allocate main loop: %s\n", pa_strerror(pa_context_errno(pThis->pContext))));
        return VERR_NO_MEMORY;
    }

    pThis->pContext = pa_context_new(pa_threaded_mainloop_get_api(pThis->pMainLoop), "VirtualBox");
    if (!pThis->pContext)
    {
        LogRel(("PulseAudio: Failed to allocate context: %s\n", pa_strerror(pa_context_errno(pThis->pContext))));
        return VERR_NO_MEMORY;
    }

    if (pa_threaded_mainloop_start(pThis->pMainLoop) < 0)
    {
        LogRel(("PulseAudio: Failed to start threaded mainloop: %s\n", pa_strerror(pa_context_errno(pThis->pContext))));
        return VERR_AUDIO_BACKEND_INIT_FAILED;
    }

    /*
     * Connect to the pulse audio server.
     *
     * We install an init state callback so we can do a timed wait in case
     * connecting to the pulseaudio server should take too long.
     */
    pThis->InitStateChgCtx.hEvtInit    = NIL_RTSEMEVENT;
    pThis->InitStateChgCtx.enmCtxState = PA_CONTEXT_UNCONNECTED;
    rc = RTSemEventCreate(&pThis->InitStateChgCtx.hEvtInit);
    AssertLogRelRCReturn(rc, rc);

    pa_threaded_mainloop_lock(pThis->pMainLoop);
    pa_context_set_state_callback(pThis->pContext, drvHstAudPaCtxCallbackStateChangedInit, &pThis->InitStateChgCtx);
    if (!pa_context_connect(pThis->pContext, NULL /* pszServer */, PA_CONTEXT_NOFLAGS, NULL))
    {
        pa_threaded_mainloop_unlock(pThis->pMainLoop);

        rc = RTSemEventWait(pThis->InitStateChgCtx.hEvtInit, RT_MS_10SEC); /* 10 seconds should be plenty. */
        if (RT_SUCCESS(rc))
        {
            if (pThis->InitStateChgCtx.enmCtxState == PA_CONTEXT_READY)
            {
                /* Install the main state changed callback to know if something happens to our acquired context. */
                pa_threaded_mainloop_lock(pThis->pMainLoop);
                pa_context_set_state_callback(pThis->pContext, drvHstAudPaCtxCallbackStateChanged, pThis /* pvUserData */);
                pa_threaded_mainloop_unlock(pThis->pMainLoop);
            }
            else
            {
                LogRel(("PulseAudio: Failed to initialize context (state %d, rc=%Rrc)\n", pThis->InitStateChgCtx.enmCtxState, rc));
                rc = VERR_AUDIO_BACKEND_INIT_FAILED;
            }
        }
        else
        {
            LogRel(("PulseAudio: Waiting for context to become ready failed: %Rrc\n", rc));
            rc = VERR_AUDIO_BACKEND_INIT_FAILED;
        }
    }
    else
    {
        pa_threaded_mainloop_unlock(pThis->pMainLoop);
        LogRel(("PulseAudio: Failed to connect to server: %s\n", pa_strerror(pa_context_errno(pThis->pContext))));
        rc = VERR_AUDIO_BACKEND_INIT_FAILED; /* bird: This used to be VINF_SUCCESS. */
    }

    RTSemEventDestroy(pThis->InitStateChgCtx.hEvtInit);
    pThis->InitStateChgCtx.hEvtInit = NIL_RTSEMEVENT;

    return rc;
}


/**
 * Pulse audio driver registration record.
 */
const PDMDRVREG g_DrvHostPulseAudio =
{
    /* u32Version */
    PDM_DRVREG_VERSION,
    /* szName */
    "PulseAudio",
    /* szRCMod */
    "",
    /* szR0Mod */
    "",
    /* pszDescription */
    "Pulse Audio host driver",
    /* fFlags */
     PDM_DRVREG_FLAGS_HOST_BITS_DEFAULT,
    /* fClass. */
    PDM_DRVREG_CLASS_AUDIO,
    /* cMaxInstances */
    ~0U,
    /* cbInstance */
    sizeof(DRVHSTAUDPA),
    /* pfnConstruct */
    drvHstAudPaConstruct,
    /* pfnDestruct */
    drvHstAudPaDestruct,
    /* pfnRelocate */
    NULL,
    /* pfnIOCtl */
    NULL,
    /* pfnPowerOn */
    NULL,
    /* pfnReset */
    NULL,
    /* pfnSuspend */
    NULL,
    /* pfnResume */
    NULL,
    /* pfnAttach */
    NULL,
    /* pfnDetach */
    NULL,
    /* pfnPowerOff */
    NULL,
    /* pfnSoftReset */
    NULL,
    /* u32EndVersion */
    PDM_DRVREG_VERSION
};

