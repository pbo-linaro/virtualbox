/* $Id$ */
/** @file
 * VBox Qt GUI - UIWizardImportApp class declaration.
 */

/*
 * Copyright (C) 2009-2021 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#ifndef FEQT_INCLUDED_SRC_wizards_importappliance_UIWizardImportApp_h
#define FEQT_INCLUDED_SRC_wizards_importappliance_UIWizardImportApp_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* GUI includes: */
#include "UIWizard.h"

/* COM includes: */
#include "COMEnums.h"
#include "CAppliance.h"

/** MAC address policies. */
enum MACAddressImportPolicy
{
    MACAddressImportPolicy_KeepAllMACs,
    MACAddressImportPolicy_KeepNATMACs,
    MACAddressImportPolicy_StripAllMACs,
    MACAddressImportPolicy_MAX
};
Q_DECLARE_METATYPE(MACAddressImportPolicy);

/** Import Appliance wizard. */
class UIWizardImportApp : public UIWizard
{
    Q_OBJECT;

public:

    /** Basic page IDs. */
    enum
    {
        Page1,
        Page2
    };

    /** Expert page IDs. */
    enum
    {
        PageExpert
    };

    /** Constructs export appliance wizard passing @a pParent to the base-class.
      * @param  strFileName  Brings appliance file name. */
    UIWizardImportApp(QWidget *pParent, bool fImportFromOCIByDefault, const QString &strFileName);

    /** Prepares all. */
    virtual void prepare() /* override */;

    /** Returns appliance. */
    CAppliance appliance() const;
    /** Defines @a strFileName. */
    bool setFile(const QString &strFileName);
    /** Returns whether appliance is valid. */
    bool isValid() const;

    /** Imports appliance. */
    bool importAppliance();

protected:

    /** Handles translation event. */
    virtual void retranslateUi() /* override */;

private:

    /** Returns a list of license agreement pairs. */
    QList<QPair<QString, QString> > licenseAgreements() const;

    /** Holds whether default source should be Import from OCI. */
    bool     m_fImportFromOCIByDefault;
    /** Handles the appliance file name. */
    QString  m_strFileName;

    /** Holds the appliance wrapper instance. */
    CAppliance  m_comAppliance;
};

/** Safe pointer to appliance wizard. */
typedef QPointer<UIWizardImportApp> UISafePointerWizardImportApp;

#endif /* !FEQT_INCLUDED_SRC_wizards_importappliance_UIWizardImportApp_h */
