/* $Id$ */
/** @file
 * VBox Qt GUI - Qt extensions: UISettingsPageValidator class implementation.
 */

/*
 * Copyright (C) 2006-2023 Oracle and/or its affiliates.
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

/* GUI includes: */
#include "UISettingsPage.h"
#include "UISettingsPageValidator.h"


UISettingsPageValidator::UISettingsPageValidator(QObject *pParent, UISettingsPage *pPage)
    : QObject(pParent)
    , m_pPage(pPage)
    , m_fIsValid(true)
{
}

QPixmap UISettingsPageValidator::warningPixmap() const
{
    return m_pPage->warningPixmap();
}

QString UISettingsPageValidator::internalName() const
{
    return m_pPage->internalName();
}

void UISettingsPageValidator::setLastMessage(const QString &strLastMessage)
{
    /* Remember new message: */
    m_strLastMessage = strLastMessage;

    /* Should we show corresponding warning icon? */
    if (m_strLastMessage.isEmpty())
        emit sigHideWarningIcon();
    else
        emit sigShowWarningIcon();
}

void UISettingsPageValidator::revalidate()
{
    /* Notify listener(s) about validity change: */
    emit sigValidityChanged(this);
}
