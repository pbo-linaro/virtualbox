/* $Id$ */
/** @file
 * Shared Clipboard guest/host Qt code test cases.
 */

/*
 * Copyright (C) 2011-2020 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#include <QApplication>
#include <QMainWindow>
#include <QLabel>
#include <QHBoxLayout>
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QMainWindow window;
    QWidget *pWidget = new QWidget;
    window.setCentralWidget(pWidget);
    QHBoxLayout *pLayout = new QHBoxLayout(pWidget);

    QLabel *pLabel = new QLabel;
    pLabel->setText("Label is here");
    pLayout->addWidget(pLabel);


    window.show();

    app.exec();
}
