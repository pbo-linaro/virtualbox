/** @file
 * VirtualBox ThreadTask class definition
 */

/*
 * Copyright (C) 2015 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#ifndef ____H_THREADTASK
#define ____H_THREADTASK

#include "VBox/com/string.h"

class ThreadTask
{
public:
    ThreadTask(const Utf8Str &t) : m_strTaskName(t){};
    virtual ~ThreadTask(){};
    HRESULT createThread();
    virtual void handler() = 0;
    static DECLCALLBACK(int) taskHandler(RTTHREAD thread, void *pvUser);
    static HRESULT i_setErrorStatic(HRESULT aResultCode, const Utf8Str &aText);

    inline Utf8Str getTaskName() const {return m_strTaskName;};

protected:
    ThreadTask():m_strTaskName("GenericTask"){};
    Utf8Str m_strTaskName;
};

#endif

