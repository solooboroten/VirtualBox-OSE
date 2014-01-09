/* $Id: UIGChooser.cpp 42547 2012-08-02 14:49:58Z vboxsync $ */
/** @file
 *
 * VBox frontends: Qt GUI ("VirtualBox"):
 * UIGChooser class implementation
 */

/*
 * Copyright (C) 2012 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

/* Qt includes: */
#include <QVBoxLayout>
#include <QStatusBar>

/* GUI includes: */
#include "UIGChooser.h"
#include "UIGChooserModel.h"
#include "UIGChooserView.h"
#include "UIVirtualBoxEventHandler.h"
#include "VBoxGlobal.h"

UIGChooser::UIGChooser(QWidget *pParent)
    : QWidget(pParent)
    , m_pMainLayout(0)
    , m_pChooserModel(0)
    , m_pChooserView(0)
    , m_pStatusBar(0)
{
    /* Create main-layout: */
    m_pMainLayout = new QVBoxLayout(this);
    m_pMainLayout->setContentsMargins(0, 0, 0, 0);
    m_pMainLayout->setSpacing(0);

    /* Create chooser-model: */
    m_pChooserModel = new UIGChooserModel(this);

    /* Create chooser-view: */
    m_pChooserView = new UIGChooserView(this);
    m_pChooserView->setFrameShape(QFrame::NoFrame);
    m_pChooserView->setFrameShadow(QFrame::Plain);
    m_pChooserView->setScene(m_pChooserModel->scene());
    m_pChooserView->show();
    setFocusProxy(m_pChooserView);

    /* Add tool-bar into layout: */
    m_pMainLayout->addWidget(m_pChooserView);

    /* Prepare connections: */
    prepareConnections();

    /* Load model: */
    m_pChooserModel->load();

    /* Load last selected item: */
    m_pChooserModel->setCurrentItemDefinition(vboxGlobal().virtualBox().GetExtraData(GUI_LastItemSelected));
}

UIGChooser::~UIGChooser()
{
    /* Save last selected item: */
    vboxGlobal().virtualBox().SetExtraData(GUI_LastItemSelected, m_pChooserModel->currentItemDefinition());

    /* Save model: */
    m_pChooserModel->save();
}

void UIGChooser::setCurrentItem(int iCurrentItemIndex)
{
    m_pChooserModel->setCurrentItem(iCurrentItemIndex);
}

UIVMItem* UIGChooser::currentItem() const
{
    return m_pChooserModel->currentItem();
}

QList<UIVMItem*> UIGChooser::currentItems() const
{
    return m_pChooserModel->currentItems();
}

bool UIGChooser::singleGroupSelected() const
{
    return m_pChooserModel->singleGroupSelected();
}

void UIGChooser::setStatusBar(QStatusBar *pStatusBar)
{
    /* Old status-bar set? */
    if (m_pStatusBar)
       m_pChooserModel->disconnect(m_pStatusBar);

    /* Connect new status-bar: */
    m_pStatusBar = pStatusBar;
    connect(m_pChooserModel, SIGNAL(sigClearStatusMessage()), m_pStatusBar, SLOT(clearMessage()));
    connect(m_pChooserModel, SIGNAL(sigShowStatusMessage(const QString&)), m_pStatusBar, SLOT(showMessage(const QString&)));
}

void UIGChooser::prepareConnections()
{
    /* Chooser-model connections: */
    connect(m_pChooserModel, SIGNAL(sigRootItemResized(const QSizeF&, int)),
            m_pChooserView, SLOT(sltHandleRootItemResized(const QSizeF&, int)));
    connect(m_pChooserModel, SIGNAL(sigSelectionChanged()), this, SIGNAL(sigSelectionChanged()));
    connect(m_pChooserModel, SIGNAL(sigSlidingStarted()), this, SIGNAL(sigSlidingStarted()));

    /* Chooser-view connections: */
    connect(m_pChooserView, SIGNAL(sigResized()), m_pChooserModel, SLOT(sltHandleViewResized()));

    /* Global connections: */
    connect(gVBoxEvents, SIGNAL(sigMachineStateChange(QString, KMachineState)), m_pChooserModel, SLOT(sltMachineStateChanged(QString, KMachineState)));
    connect(gVBoxEvents, SIGNAL(sigMachineDataChange(QString)), m_pChooserModel, SLOT(sltMachineDataChanged(QString)));
    connect(gVBoxEvents, SIGNAL(sigMachineRegistered(QString, bool)), m_pChooserModel, SLOT(sltMachineRegistered(QString, bool)));
    connect(gVBoxEvents, SIGNAL(sigSessionStateChange(QString, KSessionState)), m_pChooserModel, SLOT(sltSessionStateChanged(QString, KSessionState)));
    connect(gVBoxEvents, SIGNAL(sigSnapshotChange(QString, QString)), m_pChooserModel, SLOT(sltSnapshotChanged(QString, QString)));
}

