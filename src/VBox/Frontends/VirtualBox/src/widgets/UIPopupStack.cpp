/* $Id$ */
/** @file
 *
 * VBox frontends: Qt GUI ("VirtualBox"):
 * UIPopupStack class implementation
 */

/*
 * Copyright (C) 2013 Oracle Corporation
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
#include <QScrollArea>
#include <QEvent>
#include <QMainWindow>
#include <QMenuBar>
#include <QStatusBar>

/* GUI includes: */
#include "UIPopupStack.h"
#include "UIPopupStackViewport.h"

UIPopupStack::UIPopupStack(const QString &strID)
    : m_strID(strID)
    , m_pScrollArea(0)
    , m_pScrollViewport(0)
    , m_iParentMenuBarHeight(0)
    , m_iParentStatusBarHeight(0)
{
    /* Prepare: */
    prepare();
}

bool UIPopupStack::exists(const QString &strPopupPaneID) const
{
    /* Redirect question to viewport: */
    return m_pScrollViewport->exists(strPopupPaneID);
}

void UIPopupStack::createPopupPane(const QString &strPopupPaneID,
                                   const QString &strMessage, const QString &strDetails,
                                   const QMap<int, QString> &buttonDescriptions,
                                   bool fProposeAutoConfirmation)
{
    /* Redirect request to viewport: */
    m_pScrollViewport->createPopupPane(strPopupPaneID,
                                       strMessage, strDetails,
                                       buttonDescriptions,
                                       fProposeAutoConfirmation);

    /* Propagate width: */
    propagateWidth();
}

void UIPopupStack::updatePopupPane(const QString &strPopupPaneID,
                                   const QString &strMessage, const QString &strDetails)
{
    /* Redirect request to viewport: */
    m_pScrollViewport->updatePopupPane(strPopupPaneID,
                                       strMessage, strDetails);
}

void UIPopupStack::recallPopupPane(const QString &strPopupPaneID)
{
    /* Redirect request to viewport: */
    m_pScrollViewport->recallPopupPane(strPopupPaneID);
}

void UIPopupStack::setParent(QWidget *pParent)
{
    /* Call to base-class: */
    QWidget::setParent(pParent);
    /* Recalculate parent menu-bar height: */
    m_iParentMenuBarHeight = parentMenuBarHeight(pParent);
    /* Recalculate parent status-bar height: */
    m_iParentStatusBarHeight = parentStatusBarHeight(pParent);
}

void UIPopupStack::setParent(QWidget *pParent, Qt::WindowFlags flags)
{
    /* Call to base-class: */
    QWidget::setParent(pParent, flags);
    /* Recalculate parent menu-bar height: */
    m_iParentMenuBarHeight = parentMenuBarHeight(pParent);
    /* Recalculate parent status-bar height: */
    m_iParentStatusBarHeight = parentStatusBarHeight(pParent);
}

void UIPopupStack::sltAdjustGeometry()
{
    /* Make sure parent is currently set: */
    if (!parent())
        return;

    /* Read parent geometry: */
    QRect geo(parentWidget()->geometry());

    /* Determine origin: */
    bool fIsWindow = isWindow();
    int iX = fIsWindow ? geo.x() : 0;
    int iY = fIsWindow ? geo.y() : 0;
    /* Add menu-bar height: */
    iY += m_iParentMenuBarHeight;

    /* Determine size: */
    int iWidth = parentWidget()->width();
    int iHeight = parentWidget()->height();
    /* Subtract menu-bar and status-bar heights: */
    iHeight -= (m_iParentMenuBarHeight + m_iParentStatusBarHeight);
    /* Check if minimum height is even less than current: */
    if (m_pScrollViewport)
    {
        /* Get minimum viewport height: */
        int iMinimumHeight = m_pScrollViewport->minimumSizeHint().height();
        /* Subtract layout margins: */
        int iLeft, iTop, iRight, iBottom;
        m_pMainLayout->getContentsMargins(&iLeft, &iTop, &iRight, &iBottom);
        iMinimumHeight += (iTop + iBottom);
        /* Compare minimum and current height: */
        iHeight = qMin(iHeight, iMinimumHeight);
    }

    /* Adjust geometry: */
    setGeometry(iX, iY, iWidth, iHeight);
}

void UIPopupStack::sltPopupPaneRemoved(QString)
{
    /* Move focus to the parent: */
    if (parentWidget())
        parentWidget()->setFocus();
}

void UIPopupStack::sltPopupPanesRemoved()
{
    /* Ask popup-center to remove us: */
    emit sigRemove(m_strID);
}

void UIPopupStack::prepare()
{
    /* Configure background: */
    setAutoFillBackground(false);
#if defined(Q_WS_WIN) || defined (Q_WS_MAC)
    /* Using Qt API to enable translucent background for the Win/Mac host.
     * - Under x11 host Qt 4.8.3 has it broken wih KDE 4.9 for now: */
    setAttribute(Qt::WA_TranslucentBackground);
#endif /* Q_WS_WIN || Q_WS_MAC */

#ifdef Q_WS_MAC
    /* Do not hide popup-stack
     * and actually the seamless machine-window too
     * due to Qt bug on window deactivation... */
    setAttribute(Qt::WA_MacAlwaysShowToolWindow);
#endif /* Q_WS_MAC */

    /* Prepare content: */
    prepareContent();
}

void UIPopupStack::prepareContent()
{
    /* Create main-layout: */
    m_pMainLayout = new QVBoxLayout(this);
    {
        /* Configure main-layout: */
        m_pMainLayout->setContentsMargins(0, 0, 0, 0);
        /* Create scroll-area: */
        m_pScrollArea = new QScrollArea;
        {
            /* Configure scroll-area: */
            m_pScrollArea->setCursor(Qt::ArrowCursor);
            m_pScrollArea->setWidgetResizable(true);
            m_pScrollArea->setFrameStyle(QFrame::NoFrame | QFrame::Plain);
            m_pScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
            //m_pScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
            QPalette pal = m_pScrollArea->palette();
            pal.setColor(QPalette::Window, QColor(Qt::transparent));
            m_pScrollArea->setPalette(pal);
            /* Create scroll-viewport: */
            m_pScrollViewport = new UIPopupStackViewport;
            {
                /* Configure scroll-viewport: */
                m_pScrollViewport->setCursor(Qt::ArrowCursor);
                /* Connect scroll-viewport: */
                connect(this, SIGNAL(sigProposeStackViewportWidth(int)),
                        m_pScrollViewport, SLOT(sltHandleProposalForWidth(int)));
                connect(m_pScrollViewport, SIGNAL(sigSizeHintChanged()),
                        this, SLOT(sltAdjustGeometry()));
                connect(m_pScrollViewport, SIGNAL(sigPopupPaneDone(QString, int)),
                        this, SIGNAL(sigPopupPaneDone(QString, int)));
                connect(m_pScrollViewport, SIGNAL(sigPopupPaneRemoved(QString)),
                        this, SLOT(sltPopupPaneRemoved(QString)));
                connect(m_pScrollViewport, SIGNAL(sigPopupPanesRemoved()),
                        this, SLOT(sltPopupPanesRemoved()));
            }
            /* Assign scroll-viewport to scroll-area: */
            m_pScrollArea->setWidget(m_pScrollViewport);
        }
        /* Add scroll-area to layout: */
        m_pMainLayout->addWidget(m_pScrollArea);
    }
}

bool UIPopupStack::eventFilter(QObject *pWatched, QEvent *pEvent)
{
    /* Call to base-class if that is not parent event: */
    if (!parent() || pWatched != parent())
        return QWidget::eventFilter(pWatched, pEvent);

    /* Handle parent geometry events: */
    switch (pEvent->type())
    {
        case QEvent::Resize:
        {
            /* Propagate width: */
            propagateWidth();
            /* Adjust geometry: */
            sltAdjustGeometry();
            break;
        }
        case QEvent::Move:
        {
            /* Adjust geometry: */
            sltAdjustGeometry();
            break;
        }
    }

    /* Call to base-class: */
    return QWidget::eventFilter(pWatched, pEvent);
}

void UIPopupStack::showEvent(QShowEvent*)
{
    /* Propagate width: */
    propagateWidth();
    /* Adjust geometry: */
    sltAdjustGeometry();
}

void UIPopupStack::propagateWidth()
{
    /* Make sure parent is currently set: */
    if (!parent())
        return;

    /* Get parent width: */
    int iWidth = parentWidget()->width();
    /* Subtract left/right layout margins: */
    if (m_pMainLayout)
    {
        int iLeft, iTop, iRight, iBottom;
        m_pMainLayout->getContentsMargins(&iLeft, &iTop, &iRight, &iBottom);
        iWidth -= (iLeft + iRight);
    }
    /* Subtract scroll-area frame-width: */
    if (m_pScrollArea)
    {
        iWidth -= 2 * m_pScrollArea->frameWidth();
    }

    /* Propose resulting width to viewport: */
    emit sigProposeStackViewportWidth(iWidth);
}

/* static */
int UIPopupStack::parentMenuBarHeight(QWidget *pParent)
{
    /* Menu-bar can exist only on QMainWindow sub-class: */
    if (QMainWindow *pMainWindow = qobject_cast<QMainWindow*>(pParent))
    {
        /* Search for existing menu-bar child: */
        if (QMenuBar *pMenuBar = pMainWindow->findChild<QMenuBar*>())
            return pMenuBar->height();
    }
    /* Zero by default: */
    return 0;
}

/* static */
int UIPopupStack::parentStatusBarHeight(QWidget *pParent)
{
    /* Status-bar can exist only on QMainWindow sub-class: */
    if (QMainWindow *pMainWindow = qobject_cast<QMainWindow*>(pParent))
    {
        /* Search for existing status-bar child: */
        if (QStatusBar *pStatusBar = pMainWindow->findChild<QStatusBar*>())
            return pStatusBar->height();
    }
    /* Zero by default: */
    return 0;
}

