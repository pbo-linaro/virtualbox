/** @file
 *
 * VBox frontends: VBoxSDL (simple frontend based on SDL):
 * Main header
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

#ifndef __H_VBOXSDL
#define __H_VBOXSDL

/* convince SDL to not overload main() */
#define _SDL_main_h
/* include this first so Windows.h get's in before our stuff. */
#include <SDL.h>

/** custom SDL event for display update handling */
#define SDL_USER_EVENT_UPDATERECT         (SDL_USEREVENT + 4)
/** custom SDL event for resize handling */
#define SDL_USER_EVENT_RESIZE             (SDL_USEREVENT + 5)
/** custom SDL for XPCOM event queue processing */
#define SDL_USER_EVENT_XPCOM_EVENTQUEUE   (SDL_USEREVENT + 6)
/** custom SDL event for updating the titlebar */
#define SDL_USER_EVENT_UPDATE_TITLEBAR    (SDL_USEREVENT + 7)
/** custom SDL user event for terminating the session */
#define SDL_USER_EVENT_TERMINATE          (SDL_USEREVENT + 8)
/** custom SDL user event for secure label update notification */
#define SDL_USER_EVENT_SECURELABEL_UPDATE (SDL_USEREVENT + 9)
/** custom SDL user event for pointer shape change request */
#define SDL_USER_EVENT_POINTER_CHANGE     (SDL_USEREVENT + 10)
/** custom SDL user event for a regular timer */
#define SDL_USER_EVENT_TIMER              (SDL_USEREVENT + 11)
/** custom SDL user event for resetting mouse cursor */
#define SDL_USER_EVENT_GUEST_CAP_CHANGED  (SDL_USEREVENT + 12)


/** The user.code field of the SDL_USER_EVENT_TERMINATE event.
 * @{
 */
/** Normal termination. */
#define VBOXSDL_TERM_NORMAL             0
/** Abnormal termination. */
#define VBOXSDL_TERM_ABEND              1
/** @} */

void ResetVM(void);
void SaveState(void);

#ifdef VBOX_WIN32_UI
int initUI(bool fResizable);
int uninitUI(void);
int resizeUI(uint16_t width, uint16_t height);
int setUITitle(char *title);
#endif /* VBOX_WIN32_UI */

#endif // __H_VBOXSDL
