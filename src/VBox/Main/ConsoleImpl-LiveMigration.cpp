/* $Id$ */
/** @file
 * VBox Console COM Class implementation, The Live Migration Part.
 */

/*
 * Copyright (C) 2009 Sun Microsystems, Inc.
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
#include "ConsoleImpl.h"
#include "Global.h"
#include "Logging.h"
#include "ProgressImpl.h"

#include <iprt/err.h>
#include <iprt/rand.h>
#include <iprt/tcp.h>
#include <iprt/timer.h>

#include <VBox/vmapi.h>
#include <VBox/ssm.h>
#include <VBox/err.h>
#include <VBox/version.h>
#include <VBox/com/string.h>



/*******************************************************************************
*   Structures and Typedefs                                                    *
*******************************************************************************/
/**
 * Base class for the migration state.
 *
 * These classes are used as advanced structs, not as proper classes.
 */
class MigrationState
{
public:
    ComPtr<Console>     mptrConsole;
    PVM                 mpVM;
    Utf8Str             mstrPassword;

    /** @name stream stuff
     * @{  */
    RTSOCKET            mhSocket;
    uint64_t            moffStream;
    /** @} */

    MigrationState(Console *pConsole, PVM pVM)
        : mptrConsole(pConsole)
        , mpVM(pVM)
        , mhSocket(NIL_RTSOCKET)
        , moffStream(UINT64_MAX / 2)
    {
    }
};


/**
 * Migration state used by the source side.
 */
class MigrationStateSrc : public MigrationState
{
public:
    ComPtr<Progress>    mptrProgress;
    Utf8Str             mstrHostname;
    uint32_t            muPort;

    MigrationStateSrc(Console *pConsole, PVM pVM)
        : MigrationState(pConsole, pVM)
        , muPort(UINT32_MAX)
    {
    }
};


/**
 * Migration state used by the destiation side.
 */
class MigrationStateDst : public MigrationState
{
public:
    IMachine           *mpMachine;
    void               *mpvVMCallbackTask;
    int                 mRc;

    MigrationStateDst(Console *pConsole, PVM pVM, IMachine *pMachine)
        : MigrationState(pConsole, pVM)
        , mpMachine(pMachine)
        , mpvVMCallbackTask(NULL)
        , mRc(VINF_SUCCESS)
    {
    }
};



/*******************************************************************************
*   Global Variables                                                           *
*******************************************************************************/
static const char g_szWelcome[] = "VirtualBox-LiveMigration-1.0\n";


/**
 * Reads a string from the socket.
 *
 * @returns VBox status code.
 *
 * @param   pState      The live migration state structure.
 * @param   pszBuf      The output buffer.
 * @param   cchBuf      The size of the output buffer.
 *
 */
static int migrationTcpReadLine(MigrationState *pState, char *pszBuf, size_t cchBuf)
{
    char       *pszStart = pszBuf;
    RTSOCKET    Sock     = pState->mhSocket;

    AssertReturn(cchBuf > 1, VERR_INTERNAL_ERROR);

    /* dead simple (stupid) approach. */
    for (;;)
    {
        char ch;
        int rc = RTTcpRead(Sock, &ch, sizeof(ch), NULL);
        if (RT_FAILURE(rc))
        {
            LogRel(("Migration: RTTcpRead -> %Rrc while reading string ('%s')\n", rc, pszStart));
            return rc;
        }
        if (    ch == '\n'
            ||  ch == '\0')
            return VINF_SUCCESS;
        if (cchBuf <= 1)
        {
            LogRel(("Migration: String buffer overflow: '%s'\n", pszStart));
            return VERR_BUFFER_OVERFLOW;
        }
        *pszBuf++ = ch;
        *pszBuf   = '\0';
        cchBuf--;
    }
}


static int migrationTcpWriteACK(MigrationState *pState)
{
    int rc = RTTcpWrite(pState->mhSocket, "ACK\n", sizeof("ACK\n") - 1);
    if (RT_FAILURE(rc))
        LogRel(("Migration: RTTcpWrite(,ACK,) -> %Rrc\n", rc));
    return rc;
}


static int migrationTcpWriteNACK(MigrationState *pState)
{
    int rc = RTTcpWrite(pState->mhSocket, "NACK\n", sizeof("NACK\n") - 1);
    if (RT_FAILURE(rc))
        LogRel(("Migration: RTTcpWrite(,NACK,) -> %Rrc\n", rc));
    return rc;
}


/**
 * Reads an ACK or NACK.
 *
 * @returns S_OK on ACK, E_FAIL+setError() on failure or NACK.
 * @param   pState              The live migration source state.
 *
 * @remarks the setError laziness forces this to be a Console member.
 */
HRESULT
Console::migrationSrcReadACK(MigrationStateSrc *pState)
{
    char szMsg[128];
    int vrc = migrationTcpReadLine(pState, szMsg, sizeof(szMsg));
    if (RT_FAILURE(vrc))
        return setError(E_FAIL, tr("Failed reading ACK: %Rrc"), vrc);
    if (strcmp(szMsg, "ACK\n"))
    {
        if (strcmp(szMsg, "NACK\n"))
            return setError(E_FAIL, "NACK");
        return setError(E_FAIL, tr("Expected ACK or NACK, got '%s'"), szMsg);
    }
    return S_OK;
}

/**
 * Submitts a command to the destination and waits for the ACK.
 *
 * @returns S_OK on ACKed command, E_FAIL+setError() on failure.
 *
 * @param   pState              The live migration source state.
 * @param   pszCommand          The command.
 *
 * @remarks the setError laziness forces this to be a Console member.
 */
HRESULT
Console::migrationSrcSubmitCommand(MigrationStateSrc *pState, const char *pszCommand)
{
    size_t cchCommand = strlen(pszCommand);
    int vrc = RTTcpWrite(pState->mhSocket, pszCommand, cchCommand);
    if (RT_SUCCESS(vrc))
        vrc = RTTcpWrite(pState->mhSocket, "\n", sizeof("\n") - 1);
    if (RT_FAILURE(vrc))
        return setError(E_FAIL, tr("Failed writing command '%s': %Rrc"), pszCommand, vrc);
    return migrationSrcReadACK(pState);
}


/**
 * @copydoc SSMSTRMOPS::pfnWrite
 */
static DECLCALLBACK(int) migrationTcpOpWrite(void *pvUser, uint64_t offStream, const void *pvBuf, size_t cbToWrite)
{
    MigrationState *pState = (MigrationState *)pvUser;
    int rc = RTTcpWrite(pState->mhSocket, pvBuf, cbToWrite);
    if (RT_SUCCESS(rc))
    {
        pState->moffStream += cbToWrite;
        return VINF_SUCCESS;
    }
    return rc;
}


/**
 * @copydoc SSMSTRMOPS::pfnRead
 */
static DECLCALLBACK(int) migrationTcpOpRead(void *pvUser, uint64_t offStream, void *pvBuf, size_t cbToRead, size_t *pcbRead)
{
    MigrationState *pState = (MigrationState *)pvUser;
    int rc = RTTcpRead(pState->mhSocket, pvBuf, cbToRead, pcbRead);
    if (RT_SUCCESS(rc))
    {
        pState->moffStream += pcbRead ? *pcbRead : cbToRead;
        return VINF_SUCCESS;
    }
    return rc;
}


/**
 * @copydoc SSMSTRMOPS::pfnSeek
 */
static DECLCALLBACK(int) migrationTcpOpSeek(void *pvUser, int64_t offSeek, unsigned uMethod, uint64_t *poffActual)
{
    return VERR_NOT_SUPPORTED;
}


/**
 * @copydoc SSMSTRMOPS::pfnTell
 */
static DECLCALLBACK(uint64_t) migrationTcpOpTell(void *pvUser)
{
    MigrationState *pState = (MigrationState *)pvUser;
    return pState->moffStream;
}


/**
 * @copydoc SSMSTRMOPS::pfnSize
 */
static DECLCALLBACK(int) migrationTcpOpSize(void *pvUser, uint64_t *pcb)
{
    return VERR_NOT_SUPPORTED;
}


/**
 * @copydoc SSMSTRMOPS::pfnClose
 */
static DECLCALLBACK(int) migrationTcpOpClose(void *pvUser)
{
    return VINF_SUCCESS;
}


/**
 * Method table for a TCP based stream.
 */
static SSMSTRMOPS const g_migrationTcpOps =
{
    SSMSTRMOPS_VERSION,
    migrationTcpOpWrite,
    migrationTcpOpRead,
    migrationTcpOpSeek,
    migrationTcpOpTell,
    migrationTcpOpSize,
    migrationTcpOpClose,
    SSMSTRMOPS_VERSION
};


/**
 * @copydoc FNRTTIMERLR
 */
static DECLCALLBACK(void) migrationTimeout(RTTIMERLR hTimerLR, void *pvUser, uint64_t iTick)
{
    /* This is harmless for any open connections. */
    RTTcpServerShutdown((PRTTCPSERVER)pvUser);
}


/**
 * Do the live migration.
 *
 * @returns VBox status code.
 * @param   pState              The migration state.
 */
HRESULT
Console::migrationSrc(MigrationStateSrc *pState)
{
    AutoCaller autoCaller(this);
    CheckComRCReturnRC(autoCaller.rc());

    /*
     * Wait for Console::Migrate to change the state.
     */
    { AutoWriteLock autoLock(); }

    /*
     * Try connect to the destination machine.
     * (Note. The caller cleans up mhSocket, so we can return without worries.)
     */
    int vrc = RTTcpClientConnect(pState->mstrHostname.c_str(), pState->muPort, &pState->mhSocket);
    if (RT_SUCCESS(vrc))
        return setError(E_FAIL, tr("Failed to connect to port %u on '%s': %Rrc"),
                        pState->muPort, pState->mstrHostname.c_str(), vrc);

    /* Read and check the welcome message. */
    char szLine[RT_MAX(128, sizeof(g_szWelcome))];
    vrc = RTTcpRead(pState->mhSocket, szLine, sizeof(g_szWelcome) - 1, NULL);
    if (RT_FAILURE(vrc))
        return setError(E_FAIL, tr("Failed to read welcome message: %Rrc"), vrc);
    if (!strcmp(szLine, g_szWelcome))
        return setError(E_FAIL, tr("Unexpected welcome '%s'"), szLine);

    /* password */
    pState->mstrPassword.append('\n');
    vrc = RTTcpWrite(pState->mhSocket, pState->mstrPassword.c_str(), pState->mstrPassword.length());
    if (RT_FAILURE(vrc))
        return setError(E_FAIL, tr("Failed to send password: %Rrc"), vrc);

    /* ACK */
    HRESULT hrc = migrationSrcReadACK(pState);
    if (FAILED(hrc))
        return hrc;

    /*
     * Do compatability checks of the VM config and the host hardware.
     */
    /** @todo later */

    /*
     * Start loading the state.
     */
    hrc = migrationSrcSubmitCommand(pState, "load");
    if (FAILED(hrc))
        return hrc;

    void *pvUser = static_cast<void *>(static_cast<MigrationState *>(pState));
    vrc = VMR3Migrate(pState->mpVM, &g_migrationTcpOps, pvUser, NULL/** @todo progress*/, pvUser);
    if (vrc)
        return setError(E_FAIL, tr("VMR3Migrate -> %Rrc"), vrc);

    hrc = migrationSrcReadACK(pState);
    if (FAILED(hrc))
        return hrc;

    /*
     * State fun? Automatic power off?
     */

    return S_OK;
}


/**
 * Static thread method wrapper.
 *
 * @returns VINF_SUCCESS (ignored).
 * @param   hThread             The thread.
 * @param   pvUser              Pointer to a MigrationStateSrc instance.
 */
/*static*/ DECLCALLBACK(int)
Console::migrationSrcThreadWrapper(RTTHREAD hThread, void *pvUser)
{
    MigrationStateSrc *pState = (MigrationStateSrc *)pvUser;

    HRESULT hrc = pState->mptrConsole->migrationSrc(pState);
    pState->mptrProgress->notifyComplete(hrc);

    if (pState->mhSocket != NIL_RTSOCKET)
    {
        RTTcpClientClose(pState->mhSocket);
        pState->mhSocket = NIL_RTSOCKET;
    }
    delete pState;

    return VINF_SUCCESS;
}


/**
 * Start live migration to the specified target.
 *
 * @returns COM status code.
 *
 * @param   aHostname   The name of the target host.
 * @param   aPort       The TCP port number.
 * @param   aPassword   The password.
 * @param   aProgress   Where to return the progress object.
 */
STDMETHODIMP
Console::Migrate(IN_BSTR aHostname, ULONG aPort, IN_BSTR aPassword, IProgress **aProgress)
{
    /*
     * Validate parameters, check+hold object status, write lock the object
     * and validate the state.
     */
    CheckComArgOutPointerValid(aProgress);
    CheckComArgStrNotEmptyOrNull(aHostname);
    CheckComArgNotNull(aHostname);
    CheckComArgExprMsg(aPort, aPort > 0 && aPort <= 65535, ("is %u", aPort));

    AutoCaller autoCaller(this);
    CheckComRCReturnRC(autoCaller.rc());

    AutoWriteLock autoLock(this);
    LogFlowThisFunc(("mMachineState=%d\n", mMachineState));

    switch (mMachineState)
    {
        case MachineState_Running:
        case MachineState_Paused:
            break;

        default:
            return setError(VBOX_E_INVALID_VM_STATE,
                tr("Invalid machine state: %s (must be Running, Paused or Stuck)"),
                Global::stringifyMachineState(mMachineState));
    }


    /*
     * Create a progress object, spawn a worker thread and change the state.
     * Note! The thread won't start working until we release the lock.
     */
    LogFlowThisFunc(("Initiating LIVE MIGRATION request...\n"));

    ComObjPtr<Progress> ptrMigrateProgress;
    HRESULT hrc = ptrMigrateProgress.createObject();
    CheckComRCReturnRC(hrc);
    hrc = ptrMigrateProgress->init(static_cast<IConsole *>(this),
                                   Bstr(tr("Live Migration")),
                                   TRUE /*aCancelable*/);
    CheckComRCReturnRC(hrc);

    MigrationStateSrc *pState = new MigrationStateSrc(this, mpVM);
    pState->mstrPassword = aPassword;
    pState->mstrHostname = aHostname;
    pState->muPort       = aPort;
    pState->mptrProgress = ptrMigrateProgress;

    int vrc = RTThreadCreate(NULL, Console::migrationSrcThreadWrapper, pState, 0 /*cbStack*/,
                             RTTHREADTYPE_EMULATION, 0 /*fFlags*/, "Migrate");
    if (RT_SUCCESS(vrc))
        delete pState;

    return E_FAIL;
}


/**
 * Creates a TCP server that listens for the source machine and passes control
 * over to Console::migrationDstServeConnection().
 *
 * @returns VBox status code.
 * @param   pVM                 The VM handle
 * @param   pMachine            The IMachine for the virtual machine.
 * @param   pvVMCallbackTask    The callback task pointer for
 *                              stateProgressCallback().
 */
int
Console::migrationDst(PVM pVM, IMachine *pMachine, void *pvVMCallbackTask)
{
    /*
     * Get the config.
     */
    ULONG uPort;
    HRESULT hrc = pMachine->COMGETTER(LiveMigrationPort)(&uPort);
    if (FAILED(hrc))
        return VERR_GENERAL_FAILURE;

    Bstr bstrPassword;
    hrc = pMachine->COMGETTER(LiveMigrationPassword)(bstrPassword.asOutParam());
    if (FAILED(hrc))
        return VERR_GENERAL_FAILURE;
    Utf8Str strPassword(bstrPassword);

    Utf8Str strBind("");
    /** @todo Add a bind address property. */
    const char *pszBindAddress = strBind.isEmpty() ? NULL : strBind.c_str();

    /*
     * Create the TCP server.
     */
    int vrc;
    PRTTCPSERVER hServer;
    if (uPort)
        vrc = RTTcpServerCreateEx(pszBindAddress, uPort, &hServer);
    else
    {
        for (int cTries = 10240; cTries > 0; cTries--)
        {
            uPort = RTRandU32Ex(cTries >= 8192 ? 49152 : 1024, 65534);
            vrc = RTTcpServerCreateEx(pszBindAddress, uPort, &hServer);
            if (vrc != VERR_NET_ADDRESS_IN_USE)
                break;
        }
        if (RT_SUCCESS(vrc))
        {
            HRESULT hrc = pMachine->COMSETTER(LiveMigrationPort)(uPort);
            if (FAILED(hrc))
            {
                RTTcpServerDestroy(hServer);
                return VERR_GENERAL_FAILURE;
            }
        }
    }
    if (RT_FAILURE(vrc))
        return vrc;

    /*
     * Create a one-shot timer for timing out after 5 mins.
     */
    RTTIMERLR hTimerLR;
    vrc = RTTimerLRCreateEx(&hTimerLR, 0 /*ns*/, RTTIMER_FLAGS_CPU_ANY, migrationTimeout, hServer);
    if (RT_SUCCESS(vrc))
    {
        vrc = RTTimerLRStart(hTimerLR, 5*60*UINT64_C(1000000000) /*ns*/);
        if (RT_SUCCESS(vrc))
        {
            /*
             * Do the job, when it returns we're done.
             */
            MigrationStateDst State(this, pVM, pMachine);
            State.mstrPassword = strPassword;

            vrc = RTTcpServerListen(hServer, Console::migrationDstServeConnection, &State);
            if (vrc == VERR_TCP_SERVER_STOP)
                vrc = State.mRc;
            if (RT_FAILURE(vrc))
                LogRel(("Migration: RTTcpServerListen -> %Rrc\n", vrc));
        }

        RTTimerLRDestroy(hTimerLR);
    }
    RTTcpServerDestroy(hServer);

    return vrc;
}


/**
 * @copydoc FNRTTCPSERVE
 */
/*static*/ DECLCALLBACK(int)
Console::migrationDstServeConnection(RTSOCKET Sock, void *pvUser)
{
    MigrationStateDst *pState   = (MigrationStateDst *)pvUser;

    /*
     * Say hello.
     */
    int vrc = RTTcpWrite(Sock, g_szWelcome, sizeof(g_szWelcome) - 1);
    if (RT_FAILURE(vrc))
    {
        LogRel(("Migration: Failed to write welcome message: %Rrc\n", vrc));
        return VINF_SUCCESS;
    }

    /*
     * Password (includes '\n', see migrationDst).  If it's right, tell
     * the TCP server to stop listening (frees up host resources and makes sure
     * this is the last connection attempt).
     */
    pState->mstrPassword.append('\n');
    const char *pszPassword = pState->mstrPassword.c_str();
    unsigned    off = 0;
    while (pszPassword[off])
    {
        char ch;
        vrc = RTTcpRead(Sock, &ch, sizeof(ch), NULL);
        if (    RT_FAILURE(vrc)
            ||  pszPassword[off] != ch)
        {
            if (RT_FAILURE(vrc))
                LogRel(("Migration: Password read failure (off=%u): %Rrc\n", off, vrc));
            else
                LogRel(("Migration: Invalid password (off=%u)\n", off));
            migrationTcpWriteNACK(pState);
            return VINF_SUCCESS;
        }
        off++;
    }
    vrc = migrationTcpWriteACK(pState);
    if (RT_FAILURE(vrc))
        return vrc;
    RTTcpServerShutdown((PRTTCPSERVER)pvUser);

    /*
     * Command processing loop.
     */
    pState->mhSocket = Sock;
    for (;;)
    {
        char szCmd[128];
        vrc = migrationTcpReadLine(pState, szCmd, sizeof(szCmd));
        if (RT_FAILURE(vrc))
            break;

        if (!strcmp(szCmd, "load"))
        {
            vrc = migrationTcpWriteACK(pState);
            if (RT_FAILURE(vrc))
                break;

            pState->moffStream = 0;
            void *pvUser = static_cast<void *>(static_cast<MigrationState *>(pState));
            vrc = VMR3LoadFromStream(pState->mpVM, &g_migrationTcpOps, pvUser,
                                     Console::stateProgressCallback, pState->mpvVMCallbackTask);
            if (RT_FAILURE(vrc))
            {
                LogRel(("Migration: VMR3LoadFromStream -> %Rrc\n", vrc));
                break;
            }

            vrc = migrationTcpWriteACK(pState);
            if (RT_FAILURE(vrc))
                break;
        }
        /** @todo implement config verification and hardware compatability checks. Or
         *        maybe leave part of these to the saved state machinery? */
        else if (!strcmp(szCmd, "done"))
        {
            migrationTcpWriteACK(pState);
            break;
        }
        else
        {
            LogRel(("Migration: Unknown command '%s'\n", szCmd));
            break;
        }
    }
    pState->mhSocket = NIL_RTSOCKET;

    return VERR_TCP_SERVER_STOP;
}

