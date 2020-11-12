/* $Id$ */
/** @file
 * X11 Guest client - seamless mode, missing proper description while using the
 * potentially confusing word 'host'.
 */

/*
 * Copyright (C) 2006-2020 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#ifndef GA_INCLUDED_SRC_x11_VBoxClient_seamless_h
#define GA_INCLUDED_SRC_x11_VBoxClient_seamless_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/thread.h>

#include <VBox/log.h>
#include <VBox/VBoxGuestLib.h>      /* for the R3 guest library functions  */

#include "seamless-x11.h"

/**
 * Interface to the host
 */
class SeamlessMain
{
private:
    // We don't want a copy constructor or assignment operator
    SeamlessMain(const SeamlessMain&);
    SeamlessMain& operator=(const SeamlessMain&);

    /** X11 event monitor object */
    SeamlessX11 mX11Monitor;

    /** Thread to start and stop when we enter and leave seamless mode which
     *  monitors X11 windows in the guest. */
    RTTHREAD mX11MonitorThread;
    /** Should the X11 monitor thread be stopping? */
    volatile bool mX11MonitorThreadStopping;

    /** The current seamless mode we are in. */
    VMMDevSeamlessMode mMode;
    /** Is the service currently paused? */
    volatile bool mfPaused;

    /**
     * Waits for a seamless state change events from the host and dispatch it.  This is
     * meant to be called by the host event monitor thread exclusively.
     *
     * @returns        IRPT return code.
     */
    int nextStateChangeEvent(void);

    /** Thread function to monitor X11 window configuration changes. */
    static DECLCALLBACK(int) x11MonitorThread(RTTHREAD self, void *pvUser);

    /** Helper to start the X11 monitor thread. */
    int startX11MonitorThread(void);

    /** Helper to stop the X11 monitor thread again. */
    int stopX11MonitorThread(void);

    /** Is the service currently actively monitoring X11 windows? */
    bool isX11MonitorThreadRunning()
    {
        return mX11MonitorThread != NIL_RTTHREAD;
    }

public:
    SeamlessMain(void);
    virtual ~SeamlessMain();
#ifdef RT_NEED_NEW_AND_DELETE
    RTMEM_IMPLEMENT_NEW_AND_DELETE();
#endif

    /** @copydoc VBCLSERVICE::pfnInit */
    int init(void);

    /** @copydoc VBCLSERVICE::pfnWorker */
    int worker(bool volatile *pfShutdown);

    /** @copydoc VBCLSERVICE::pfnStop */
    void stop(void);

    /** @copydoc VBCLSERVICE::pfnTerm */
    int term(void);
};

#endif /* !GA_INCLUDED_SRC_x11_VBoxClient_seamless_h */
