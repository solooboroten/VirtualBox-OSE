/* $Id$ */
/** @file
 * VBox Qt GUI - UIMediumManager class implementation.
 */

/*
 * Copyright (C) 2006-2014 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#ifdef VBOX_WITH_PRECOMPILED_HEADERS
# include "precomp.h"
#else  /* !VBOX_WITH_PRECOMPILED_HEADERS */

/* Qt includes: */
#include <QLabel>
#include <QProgressBar>
#include <QMenuBar>
#include <QHeaderView>
#include <QPushButton>

/* GUI includes: */
#include "VBoxGlobal.h"
#include "UIMediumManager.h"
#include "UIWizardCloneVD.h"
#include "UIMessageCenter.h"
#include "UIToolBar.h"
#include "QILabel.h"
#include "UIIconPool.h"
#include "UIMediumTypeChangeDialog.h"
#include "UIMedium.h"

/* COM includes: */
#include "COMEnums.h"
#include "CMachine.h"
#include "CMediumFormat.h"
#include "CStorageController.h"
#include "CMediumAttachment.h"

# ifdef Q_WS_MAC
#  include "UIWindowMenuManager.h"
# endif /* Q_WS_MAC */

#endif /* !VBOX_WITH_PRECOMPILED_HEADERS */


/** QTreeWidgetItem extension representing Medium Manager item. */
class UIMediumItem : public QTreeWidgetItem
{
public:

    /** UIMediumItem type for rtti needs. */
    enum { Type = QTreeWidgetItem::UserType + 1 };

    /** Constructor for top-level item. */
    UIMediumItem(const UIMedium &medium, QTreeWidget *pParent)
        : QTreeWidgetItem(pParent, Type)
        , m_medium(medium)
    { refresh(); }

    /** Constructor for child item. */
    UIMediumItem(const UIMedium &medium, UIMediumItem *pParent)
        : QTreeWidgetItem(pParent, Type)
        , m_medium(medium)
    { refresh(); }

    /** Copy UIMedium wrapped by <i>this</i> item. */
    virtual bool copy() = 0;
    /** Modify UIMedium wrapped by <i>this</i> item. */
    virtual bool modify() = 0;
    /** Remove UIMedium wrapped by <i>this</i> item. */
    virtual bool remove() = 0;
    /** Release UIMedium wrapped by <i>this</i> item. */
    virtual bool release()
    {
        /* Refresh: */
        refreshAll();

        /* Make sure medium was not released yet: */
        if (medium().curStateMachineIds().isEmpty())
            return true;

        /* Confirm release: */
        if (!msgCenter().confirmMediumRelease(medium(), treeWidget()))
            return false;

        /* Release: */
        foreach (const QString &strMachineID, medium().curStateMachineIds())
            if (!releaseFrom(strMachineID))
                return false;

        /* True by default: */
        return true;
    }

    /** Refresh item fully. */
    void refreshAll()
    {
        m_medium.refresh();
        refresh();
    }

    /** Returns UIMedium wrapped by <i>this</i> item. */
    const UIMedium& medium() const { return m_medium; }
    /** Defines UIMedium wrapped by <i>this</i> item. */
    void setMedium(const UIMedium &medium)
    {
        m_medium = medium;
        refresh();
    }

    /** Returns UIMediumType of the wrapped UIMedium. */
    UIMediumType mediumType() const { return m_medium.type(); }

    /** Returns KMediumState of the wrapped UIMedium. */
    KMediumState state() const { return m_medium.state(); }

    /** Returns QString <i>ID</i> of the wrapped UIMedium. */
    QString id() const { return m_medium.id(); }

    /** Returns QString <i>location</i> of the wrapped UIMedium. */
    QString location() const { return m_medium.location(); }

    /** Returns QString <i>hard-disk format</i> of the wrapped UIMedium. */
    QString hardDiskFormat() const { return m_medium.hardDiskFormat(); }
    /** Returns QString <i>hard-disk type</i> of the wrapped UIMedium. */
    QString hardDiskType() const { return m_medium.hardDiskType(); }

    /** Returns QString <i>storage details</i> of the wrapped UIMedium. */
    QString details() const { return m_medium.storageDetails(); }

    /** Returns QString <i>tool-tip</i> of the wrapped UIMedium. */
    QString toolTip() const { return m_medium.toolTip(); }

    /** Returns QString <i>usage</i> of the wrapped UIMedium. */
    QString usage() const { return m_medium.usage(); }
    /** Returns whether wrapped UIMedium is used or not. */
    bool isUsed() const { return m_medium.isUsed(); }
    /** Returns whether wrapped UIMedium is used in snapshots or not. */
    bool isUsedInSnapshots() const { return m_medium.isUsedInSnapshots(); }

    /** Operator< reimplementation used for sorting purposes. */
    bool operator<(const QTreeWidgetItem &other) const
    {
        int column = treeWidget()->sortColumn();
        ULONG64 thisValue = vboxGlobal().parseSize(      text(column));
        ULONG64 thatValue = vboxGlobal().parseSize(other.text(column));
        return thisValue && thatValue ? thisValue < thatValue : QTreeWidgetItem::operator<(other);
    }

protected:

    /** Release UIMedium wrapped by <i>this</i> item from virtual @a machine. */
    virtual bool releaseFrom(CMachine machine) = 0;

private:

    /** Refresh item information such as icon, text and tool-tip. */
    void refresh()
    {
        /* Fill-in columns: */
        setIcon(0, m_medium.icon());
        setText(0, m_medium.name());
        setText(1, m_medium.logicalSize());
        setText(2, m_medium.size());
        /* All columns get the same tooltip: */
        QString strToolTip = m_medium.toolTip();
        for (int i = 0; i < treeWidget()->columnCount(); ++i)
            setToolTip(i, strToolTip);
    }

    /** Release UIMedium wrapped by <i>this</i> item from virtual machine with @a strMachineID. */
    bool releaseFrom(const QString &strMachineID)
    {
        /* Open session: */
        CSession session = vboxGlobal().openSession(strMachineID);
        if (session.isNull())
            return false;

        /* Get machine: */
        CMachine machine = session.GetMachine();

        /* Prepare result: */
        bool fSuccess = false;

        /* Release medium from machine: */
        if (releaseFrom(machine))
        {
            /* Save machine settings: */
            machine.SaveSettings();
            if (!machine.isOk())
                msgCenter().cannotSaveMachineSettings(machine, treeWidget());
            else
                fSuccess = true;
        }

        /* Close session: */
        session.UnlockMachine();

        /* Return result: */
        return fSuccess;
    }

    /** UIMedium wrapped by <i>this</i> item. */
    UIMedium m_medium;
};


/** UIMediumItem extension representing hard-disk item. */
class UIMediumItemHD : public UIMediumItem
{
public:

    /** Constructor for top-level item. */
    UIMediumItemHD(const UIMedium &medium, QTreeWidget *pParent)
        : UIMediumItem(medium, pParent)
    {}

    /** Constructor for child item. */
    UIMediumItemHD(const UIMedium &medium, UIMediumItem *pParent)
        : UIMediumItem(medium, pParent)
    {}

protected:

    /** Copy UIMedium wrapped by <i>this</i> item. */
    bool copy()
    {
        /* Show Clone VD wizard: */
        UISafePointerWizard pWizard = new UIWizardCloneVD(treeWidget(), medium().medium());
        pWizard->prepare();
        pWizard->exec();

        /* Delete if still exists: */
        if (pWizard)
            delete pWizard;

        /* True by default: */
        return true;
    }

    /** Modify UIMedium wrapped by <i>this</i> item. */
    bool modify()
    {
        /* False by default: */
        bool fResult = false;

        /* Show Modify VD dialog: */
        UISafePointerDialog pDialog = new UIMediumTypeChangeDialog(treeWidget(), id());
        if (pDialog->exec() == QDialog::Accepted)
        {
            /* Update medium-item: */
            refreshAll();
            /* Change to passed: */
            fResult = true;
        }

        /* Delete if still exists: */
        if (pDialog)
            delete pDialog;

        /* Return result: */
        return fResult;
    }

    /** Remove UIMedium wrapped by <i>this</i> item. */
    bool remove()
    {
        /* Confirm medium removal: */
        if (!msgCenter().confirmMediumRemoval(medium(), treeWidget()))
            return false;

        /* Remember some of hard-disk attributes: */
        CMedium hardDisk = medium().medium();
        QString strMediumID = id();

        /* Propose to remove medium storage: */
        if (!maybeRemoveStorage())
            return false;

        /* Close hard-disk: */
        hardDisk.Close();
        if (!hardDisk.isOk())
        {
            msgCenter().cannotCloseMedium(medium(), hardDisk, treeWidget());
            return false;
        }

        /* Remove UIMedium finally: */
        vboxGlobal().deleteMedium(strMediumID);

        /* True by default: */
        return true;
    }

    /** Release UIMedium wrapped by <i>this</i> item from virtual @a machine. */
    bool releaseFrom(CMachine machine)
    {
        /* Enumerate attachments: */
        CMediumAttachmentVector attachments = machine.GetMediumAttachments();
        foreach (const CMediumAttachment &attachment, attachments)
        {
            /* Skip non-hard-disks: */
            if (attachment.GetType() != KDeviceType_HardDisk)
                continue;

            /* Skip unrelated hard-disks: */
            if (attachment.GetMedium().GetId() != id())
                continue;

            /* Remember controller: */
            CStorageController controller = machine.GetStorageControllerByName(attachment.GetController());

            /* Try to detach device: */
            machine.DetachDevice(attachment.GetController(), attachment.GetPort(), attachment.GetDevice());
            if (!machine.isOk())
            {
                /* Return failure: */
                msgCenter().cannotDetachDevice(machine, UIMediumType_HardDisk, location(),
                                               StorageSlot(controller.GetBus(), attachment.GetPort(), attachment.GetDevice()),
                                               treeWidget());
                return false;
            }

            /* Return success: */
            return true;
        }

        /* False by default: */
        return false;
    }

private:

    /** Proposes user to remove CMedium storage wrapped by <i>this</i> item. */
    bool maybeRemoveStorage()
    {
        /* Remember some of hard-disk attributes: */
        CMedium hardDisk = medium().medium();
        QString strLocation = location();

        /* We don't want to try to delete inaccessible storage as it will most likely fail.
         * Note that UIMessageCenter::confirmMediumRemoval() is aware of that and
         * will give a corresponding hint. Therefore, once the code is changed below,
         * the hint should be re-checked for validity. */
        bool fDeleteStorage = false;
        qulonglong uCapability = 0;
        QVector<KMediumFormatCapabilities> capabilities = hardDisk.GetMediumFormat().GetCapabilities();
        foreach (KMediumFormatCapabilities capability, capabilities)
            uCapability |= capability;
        if (state() != KMediumState_Inaccessible && uCapability & MediumFormatCapabilities_File)
        {
            int rc = msgCenter().confirmDeleteHardDiskStorage(strLocation, treeWidget());
            if (rc == AlertButton_Cancel)
                return false;
            fDeleteStorage = rc == AlertButton_Choice1;
        }

        /* If user wish to delete storage: */
        if (fDeleteStorage)
        {
            /* Prepare delete storage progress: */
            CProgress progress = hardDisk.DeleteStorage();
            if (!hardDisk.isOk())
            {
                msgCenter().cannotDeleteHardDiskStorage(hardDisk, strLocation, treeWidget());
                return false;
            }
            /* Show delete storage progress: */
            msgCenter().showModalProgressDialog(progress, UIMediumManager::tr("Removing medium..."),
                                                ":/progress_media_delete_90px.png", treeWidget());
            if (!progress.isOk() || progress.GetResultCode() != 0)
            {
                msgCenter().cannotDeleteHardDiskStorage(progress, strLocation, treeWidget());
                return false;
            }
        }

        /* True by default: */
        return true;
    }
};

/** UIMediumItem extension representing optical-disk item. */
class UIMediumItemCD : public UIMediumItem
{
public:

    /** Constructor for top-level item. */
    UIMediumItemCD(const UIMedium &medium, QTreeWidget *pParent)
        : UIMediumItem(medium, pParent)
    {}

protected:

    /** Copy UIMedium wrapped by <i>this</i> item. */
    bool copy()
    {
        AssertMsgFailedReturn(("That functionality in not supported!\n"), false);
    }

    /** Modify UIMedium wrapped by <i>this</i> item. */
    bool modify()
    {
        AssertMsgFailedReturn(("That functionality in not supported!\n"), false);
    }

    /** Remove UIMedium wrapped by <i>this</i> item. */
    bool remove()
    {
        /* Confirm medium removal: */
        if (!msgCenter().confirmMediumRemoval(medium(), treeWidget()))
            return false;

        /* Remember some of optical-disk attributes: */
        CMedium image = medium().medium();
        QString strMediumID = id();

        /* Close optical-disk: */
        image.Close();
        if (!image.isOk())
        {
            msgCenter().cannotCloseMedium(medium(), image, treeWidget());
            return false;
        }

        /* Remove UIMedium finally: */
        vboxGlobal().deleteMedium(strMediumID);

        /* True by default: */
        return true;
    }

    /** Release UIMedium wrapped by <i>this</i> item from virtual @a machine. */
    bool releaseFrom(CMachine machine)
    {
        /* Enumerate attachments: */
        CMediumAttachmentVector attachments = machine.GetMediumAttachments();
        foreach (const CMediumAttachment &attachment, attachments)
        {
            /* Skip non-optical-disks: */
            if (attachment.GetType() != KDeviceType_DVD)
                continue;

            /* Skip unrelated optical-disks: */
            if (attachment.GetMedium().GetId() != id())
                continue;

            /* Try to unmount device: */
            machine.MountMedium(attachment.GetController(), attachment.GetPort(), attachment.GetDevice(), CMedium(), false /* force */);
            if (!machine.isOk())
            {
                /* Return failure: */
                msgCenter().cannotRemountMedium(machine, medium(), false /* mount? */, false /* retry? */, treeWidget());
                return false;
            }

            /* Return success: */
            return true;
        }

        /* Return failure: */
        return false;
    }
};

/** UIMediumItem extension representing floppy-disk item. */
class UIMediumItemFD : public UIMediumItem
{
public:

    /** Constructor for top-level item. */
    UIMediumItemFD(const UIMedium &medium, QTreeWidget *pParent)
        : UIMediumItem(medium, pParent)
    {}

protected:

    /** Copy UIMedium wrapped by <i>this</i> item. */
    bool copy()
    {
        AssertMsgFailedReturn(("That functionality in not supported!\n"), false);
    }

    /** Modify UIMedium wrapped by <i>this</i> item. */
    bool modify()
    {
        AssertMsgFailedReturn(("That functionality in not supported!\n"), false);
    }

    /** Remove UIMedium wrapped by <i>this</i> item. */
    bool remove()
    {
        /* Confirm medium removal: */
        if (!msgCenter().confirmMediumRemoval(medium(), treeWidget()))
            return false;

        /* Remember some of floppy-disk attributes: */
        CMedium image = medium().medium();
        QString strMediumID = id();

        /* Close floppy-disk: */
        image.Close();
        if (!image.isOk())
        {
            msgCenter().cannotCloseMedium(medium(), image, treeWidget());
            return false;
        }

        /* Remove UIMedium finally: */
        vboxGlobal().deleteMedium(strMediumID);

        /* True by default: */
        return true;
    }

    /** Release UIMedium wrapped by <i>this</i> item from virtual @a machine. */
    bool releaseFrom(CMachine machine)
    {
        /* Enumerate attachments: */
        CMediumAttachmentVector attachments = machine.GetMediumAttachments();
        foreach (const CMediumAttachment &attachment, attachments)
        {
            /* Skip non-floppy-disks: */
            if (attachment.GetType() != KDeviceType_Floppy)
                continue;

            /* Skip unrelated floppy-disks: */
            if (attachment.GetMedium().GetId() != id())
                continue;

            /* Try to unmount device: */
            machine.MountMedium(attachment.GetController(), attachment.GetPort(), attachment.GetDevice(), CMedium(), false /* force */);
            if (!machine.isOk())
            {
                /* Return failure: */
                msgCenter().cannotRemountMedium(machine, medium(), false /* mount? */, false /* retry? */, treeWidget());
                return false;
            }

            /* Return success: */
            return true;
        }

        /* Return failure: */
        return false;
    }
};


/** Functor allowing to check if passed UIMediumItem is suitable by @a strID. */
class CheckIfSuitableByID : public CheckIfSuitableBy
{
public:
    /** Constructor accepting @a strID to compare with. */
    CheckIfSuitableByID(const QString &strID) : m_strID(strID) {}

private:
    /** Determines whether passed UIMediumItem is suitable by @a strID. */
    bool isItSuitable(UIMediumItem *pItem) const { return pItem->id() == m_strID; }
    /** Holds the @a strID to compare to. */
    QString m_strID;
};

/** Functor allowing to check if passed UIMediumItem is suitable by @a state. */
class CheckIfSuitableByState : public CheckIfSuitableBy
{
public:
    /** Constructor accepting @a state to compare with. */
    CheckIfSuitableByState(KMediumState state) : m_state(state) {}

private:
    /** Determines whether passed UIMediumItem is suitable by @a state. */
    bool isItSuitable(UIMediumItem *pItem) const { return pItem->state() == m_state; }
    /** Holds the @a state to compare to. */
    KMediumState m_state;
};


/** Medium manager progress-bar.
  * Reflects medium-enumeration progress, stays hidden otherwise. */
class UIEnumerationProgressBar : public QWidget
{
    Q_OBJECT;

public:

    /** Constructor on the basis of passed @a pParent. */
    UIEnumerationProgressBar(QWidget *pParent)
        : QWidget(pParent)
    {
        /* Prepare: */
        prepare();
    }

    /** Defines progress-bar label-text. */
    void setText(const QString &strText) { m_pLabel->setText(strText); }

    /** Returns progress-bar current-value. */
    int value() const { return m_pProgressBar->value(); }
    /** Defines progress-bar current-value. */
    void setValue(int iValue) { m_pProgressBar->setValue(iValue); }
    /** Defines progress-bar maximum-value. */
    void setMaximum(int iValue) { m_pProgressBar->setMaximum(iValue); }

private:

    /** Prepares progress-bar content. */
    void prepare()
    {
        /* Create layout: */
        QHBoxLayout *pLayout = new QHBoxLayout(this);
        {
            /* Configure layout: */
            pLayout->setContentsMargins(0, 0, 0, 0);
            /* Create label: */
            m_pLabel = new QLabel;
            /* Create progress-bar: */
            m_pProgressBar = new QProgressBar;
            {
                /* Configure progress-bar: */
                m_pProgressBar->setTextVisible(false);
            }
            /* Add widgets into layout: */
            pLayout->addWidget(m_pLabel);
            pLayout->addWidget(m_pProgressBar);
        }
    }

    /** Progress-bar label. */
    QLabel *m_pLabel;
    /** Progress-bar itself. */
    QProgressBar *m_pProgressBar;
};


/* static */
UIMediumManager* UIMediumManager::m_spInstance = 0;
UIMediumManager* UIMediumManager::instance() { return m_spInstance; }

UIMediumManager::UIMediumManager(QWidget *pCenterWidget, bool fRefresh /* = true */)
    : QIWithRetranslateUI2<QIMainDialog>(0, Qt::Dialog)
    , m_pPseudoParentWidget(pCenterWidget)
    , m_fRefresh(fRefresh)
    , m_fPreventChangeCurrentItem(false)
    , m_fInaccessibleHD(false)
    , m_fInaccessibleCD(false)
    , m_fInaccessibleFD(false)
    , m_iconHD(UIIconPool::iconSet(":/hd_16px.png", ":/hd_disabled_16px.png"))
    , m_iconCD(UIIconPool::iconSet(":/cd_16px.png", ":/cd_disabled_16px.png"))
    , m_iconFD(UIIconPool::iconSet(":/fd_16px.png", ":/fd_disabled_16px.png"))
{
    /* Prepare: */
    prepare();
}

UIMediumManager::~UIMediumManager()
{
    /* Cleanup: */
    cleanup();

    /* Cleanup instance: */
    m_spInstance = 0;
}

/* static */
void UIMediumManager::showModeless(QWidget *pCenterWidget /* = 0 */, bool fRefresh /* = true */)
{
    /* Create instance if not yet created: */
    if (!m_spInstance)
        m_spInstance = new UIMediumManager(pCenterWidget, fRefresh);

    /* Show instance: */
    m_spInstance->show();
    m_spInstance->setWindowState(m_spInstance->windowState() & ~Qt::WindowMinimized);
    m_spInstance->activateWindow();
}

void UIMediumManager::sltHandleMediumCreated(const QString &strMediumID)
{
    /* Search for corresponding medium: */
    UIMedium medium = vboxGlobal().medium(strMediumID);

    /* Ignore non-interesting mediums: */
    if (medium.isNull() || medium.isHostDrive())
        return;

    /* Ignore mediums (and their children) which are
     * marked as hidden or attached to hidden machines only: */
    if (isMediumAttachedToHiddenMachinesOnly(medium))
        return;

    /* Create medium-item for corresponding medium: */
    UIMediumItem *pMediumItem = createMediumItem(medium);
    AssertPtrReturnVoid(pMediumItem);

    /* If medium-item change allowed and
     * 1. medium-enumeration is not currently in progress or
     * 2. if there is no currently medium-item selected
     * we have to choose newly added medium-item as current one: */
    if (   !m_fPreventChangeCurrentItem
        && (   !vboxGlobal().isMediumEnumerationInProgress()
            || !mediumItem(medium.type())))
        setCurrentItem(treeWidget(medium.type()), pMediumItem);
}

void UIMediumManager::sltHandleMediumDeleted(const QString &strMediumID)
{
    /* Make sure corresponding medium-item deleted: */
    deleteMediumItem(strMediumID);
}

void UIMediumManager::sltHandleMediumEnumerationStart()
{
    /* Disable 'refresh' action: */
    m_pActionRefresh->setEnabled(false);

    /* Reset and show progress-bar: */
    m_pProgressBar->setMaximum(vboxGlobal().mediumIDs().size());
    m_pProgressBar->setValue(0);
    m_pProgressBar->show();

    /* Reset inaccessibility flags: */
    m_fInaccessibleHD =
        m_fInaccessibleCD =
            m_fInaccessibleFD = false;

    /* Reset tab-widget icons: */
    mTabWidget->setTabIcon(TabIndex_HD, m_iconHD);
    mTabWidget->setTabIcon(TabIndex_CD, m_iconCD);
    mTabWidget->setTabIcon(TabIndex_FD, m_iconFD);

    /* Repopulate tree-widgets content: */
    repopulateTreeWidgets();

    /* Re-fetch all current medium-items: */
    refetchCurrentMediumItems();
}

void UIMediumManager::sltHandleMediumEnumerated(const QString &strMediumID)
{
    /* Search for corresponding medium: */
    UIMedium medium = vboxGlobal().medium(strMediumID);

    /* Ignore non-interesting mediums: */
    if (medium.isNull() || medium.isHostDrive())
        return;

    /* Ignore mediums (and their children) which are
     * marked as hidden or attached to hidden machines only: */
    if (isMediumAttachedToHiddenMachinesOnly(medium))
        return;

    /* Update medium-item for corresponding medium: */
    updateMediumItem(medium);

    /* Advance progress-bar: */
    m_pProgressBar->setValue(m_pProgressBar->value() + 1);
}

void UIMediumManager::sltHandleMediumEnumerationFinish()
{
    /* Hide progress-bar: */
    m_pProgressBar->hide();

    /* Enable 'refresh' action: */
    m_pActionRefresh->setEnabled(true);

    /* Re-fetch all current medium-items: */
    refetchCurrentMediumItems();
}

void UIMediumManager::sltCopyMedium()
{
    /* Get current medium-item: */
    UIMediumItem *pMediumItem = currentMediumItem();
    AssertMsgReturnVoid(pMediumItem, ("Current item must not be null"));
    AssertReturnVoid(!pMediumItem->id().isNull());

    /* Copy current medium-item: */
    pMediumItem->copy();
}

void UIMediumManager::sltModifyMedium()
{
    /* Get current medium-item: */
    UIMediumItem *pMediumItem = currentMediumItem();
    AssertMsgReturnVoid(pMediumItem, ("Current item must not be null"));
    AssertReturnVoid(!pMediumItem->id().isNull());

    /* Modify current medium-item: */
    bool fResult = pMediumItem->modify();

    /* Update HD information-panes: */
    if (fResult)
        updateInformationPanesHD();
}

void UIMediumManager::sltRemoveMedium()
{
    /* Get current medium-item: */
    UIMediumItem *pMediumItem = currentMediumItem();
    AssertMsgReturnVoid(pMediumItem, ("Current item must not be null"));
    AssertReturnVoid(!pMediumItem->id().isNull());

    /* Remove current medium-item: */
    pMediumItem->remove();
}

void UIMediumManager::sltReleaseMedium()
{
    /* Get current medium-item: */
    UIMediumItem *pMediumItem = currentMediumItem();
    AssertMsgReturnVoid(pMediumItem, ("Current item must not be null"));
    AssertReturnVoid(!pMediumItem->id().isNull());

    /* Remove current medium-item: */
    bool fResult = pMediumItem->release();

    /* Refetch currently chosen medium-item: */
    if (fResult)
        refetchCurrentChosenMediumItem();
}

void UIMediumManager::sltRefreshAll()
{
    /* Start medium-enumeration: */
    vboxGlobal().startMediumEnumeration();
}

void UIMediumManager::sltHandleCurrentTabChanged()
{
    /* Get current tree-widget: */
    QTreeWidget *pTreeWidget = currentTreeWidget();

    /* If another tree-widget was focused before,
     * move focus to current tree-widget: */
    if (qobject_cast<QTreeWidget*>(focusWidget()))
        pTreeWidget->setFocus();

    /* Re-fetch currently chosen medium-item: */
    refetchCurrentChosenMediumItem();
}

void UIMediumManager::sltHandleCurrentItemChanged()
{
    /* Get sender() tree-widget: */
    QTreeWidget *pTreeWidget = qobject_cast<QTreeWidget*>(sender());
    AssertMsgReturnVoid(pTreeWidget, ("This slot should be called by tree-widget only!\n"));

    /* Re-fetch current medium-item of required type: */
    refetchCurrentMediumItem(mediumType(pTreeWidget));
}

void UIMediumManager::sltHandleDoubleClick()
{
    /* Skip for non-hard-drives: */
    if (currentMediumType() != UIMediumType_HardDisk)
        return;

    /* Call for modify-action: */
    sltModifyMedium();
}

void UIMediumManager::sltHandleContextMenuCall(const QPoint &position)
{
    /* Get current tree-widget / item: */
    QTreeWidget *pTreeWidget = currentTreeWidget();
    QTreeWidgetItem *pItem = pTreeWidget->itemAt(position);

    /* Skip further actions if item was not found: */
    if (!pItem)
        return;

    /* Make sure that item is current one: */
    setCurrentItem(pTreeWidget, pItem);
    /* Show item context menu: */
    m_pContextMenu->exec(pTreeWidget->viewport()->mapToGlobal(position));
}

void UIMediumManager::sltPerformTablesAdjustment()
{
    /* Get all the tree-widgets: */
    QList<QTreeWidget*> trees;
    trees << treeWidget(UIMediumType_HardDisk);
    trees << treeWidget(UIMediumType_DVD);
    trees << treeWidget(UIMediumType_Floppy);

    /* Calculate deduction for every header: */
    QList<int> deductions;
    foreach (QTreeWidget *pTreeWidget, trees)
    {
        int iDeduction = 0;
        for (int iHeaderIndex = 1; iHeaderIndex < pTreeWidget->header()->count(); ++iHeaderIndex)
            iDeduction += pTreeWidget->header()->sectionSize(iHeaderIndex);
        deductions << iDeduction;
    }

    /* Adjust the table's first column: */
    for (int iTreeIndex = 0; iTreeIndex < trees.size(); ++iTreeIndex)
    {
        QTreeWidget *pTreeWidget = trees[iTreeIndex];
        int iSize0 = pTreeWidget->viewport()->width() - deductions[iTreeIndex];
        if (pTreeWidget->header()->sectionSize(0) != iSize0)
            pTreeWidget->header()->resizeSection(0, iSize0);
    }
}

void UIMediumManager::prepare()
{
    /* Prepare this: */
    prepareThis();
    /* Prepare actions: */
    prepareActions();
    /* Prepare menu-bar: */
    prepareMenuBar();
    /* Prepare tool-bar: */
    prepareToolBar();
    /* Prepare context-menu: */
    prepareContextMenu();
    /* Prepare tab-widget: */
    prepareTabWidget();
    /* Prepare tree-widgets: */
    prepareTreeWidgets();
    /* Prepare information-panes: */
    prepareInformationPanes();
    /* Prepare button-box: */
    prepareButtonBox();
    /* Prepare progress-bar: */
    prepareProgressBar();

    /* Translate dialog: */
    retranslateUi();

#ifdef Q_WS_MAC
    /* Prepare Mac window-menu.
     * Should go *after* translation! */
    prepareMacWindowMenu();
#endif /* Q_WS_MAC */

    /* Center according pseudo-parent widget: */
    centerAccording(m_pPseudoParentWidget);

    /* Initialize information-panes: */
    updateInformationPanes(UIMediumType_All);

    /* Start medium-enumeration (if necessary): */
    if (m_fRefresh && !vboxGlobal().isMediumEnumerationInProgress())
        vboxGlobal().startMediumEnumeration();
    /* Emulate medium-enumeration otherwise: */
    else
    {
        /* Start medium-enumeration: */
        sltHandleMediumEnumerationStart();

        /* Finish medium-enumeration (if necessary): */
        if (!vboxGlobal().isMediumEnumerationInProgress())
            sltHandleMediumEnumerationFinish();
    }
}

void UIMediumManager::prepareThis()
{
    /* Dialog should delete itself on 'close': */
    setAttribute(Qt::WA_DeleteOnClose);

    /* And no need to count it as important for application.
     * This way it will NOT be taken into account
     * when other top-level windows will be closed: */
    setAttribute(Qt::WA_QuitOnClose, false);

    /* Apply window icons: */
    setWindowIcon(UIIconPool::iconSetFull(QSize(32, 32), QSize(16, 16),
                                          ":/diskimage_32px.png", ":/diskimage_16px.png"));

    /* Apply UI decorations: */
    Ui::UIMediumManager::setupUi(this);

    /* Configure medium-processing connections: */
    connect(&vboxGlobal(), SIGNAL(sigMediumCreated(const QString&)),
            this, SLOT(sltHandleMediumCreated(const QString&)));
    connect(&vboxGlobal(), SIGNAL(sigMediumDeleted(const QString&)),
            this, SLOT(sltHandleMediumDeleted(const QString&)));

    /* Configure medium-enumeration connections: */
    connect(&vboxGlobal(), SIGNAL(sigMediumEnumerationStarted()),
            this, SLOT(sltHandleMediumEnumerationStart()));
    connect(&vboxGlobal(), SIGNAL(sigMediumEnumerated(const QString&)),
            this, SLOT(sltHandleMediumEnumerated(const QString&)));
    connect(&vboxGlobal(), SIGNAL(sigMediumEnumerationFinished()),
            this, SLOT(sltHandleMediumEnumerationFinish()));
}

void UIMediumManager::prepareActions()
{
    /* Create copy-action: */
    m_pActionCopy = new QAction(this);
    {
        /* Configure copy-action: */
        m_pActionCopy->setShortcut(QKeySequence("Ctrl+O"));
        m_pActionCopy->setIcon(UIIconPool::iconSetFull(QSize(22, 22), QSize(16, 16),
                                                       ":/hd_add_22px.png", ":/hd_add_16px.png",
                                                       ":/hd_add_disabled_22px.png", ":/hd_add_disabled_16px.png"));
        connect(m_pActionCopy, SIGNAL(triggered()), this, SLOT(sltCopyMedium()));
    }

    /* Create modify-action: */
    m_pActionModify = new QAction(this);
    {
        /* Configure modify-action: */
        m_pActionModify->setShortcut(QKeySequence("Ctrl+Space"));
        m_pActionModify->setIcon(UIIconPool::iconSetFull(QSize(22, 22), QSize(16, 16),
                                                         ":/hd_new_22px.png", ":/hd_new_16px.png",
                                                         ":/hd_new_disabled_22px.png", ":/hd_new_disabled_16px.png"));
        connect(m_pActionModify, SIGNAL(triggered()), this, SLOT(sltModifyMedium()));
    }

    /* Create remove-action: */
    m_pActionRemove  = new QAction(this);
    {
        /* Configure remove-action: */
        m_pActionRemove->setShortcut(QKeySequence(QKeySequence::Delete));
        m_pActionRemove->setIcon(UIIconPool::iconSetFull(QSize(22, 22), QSize(16, 16),
                                                         ":/hd_remove_22px.png", ":/hd_remove_16px.png",
                                                         ":/hd_remove_disabled_22px.png", ":/hd_remove_disabled_16px.png"));
        connect(m_pActionRemove, SIGNAL(triggered()), this, SLOT(sltRemoveMedium()));
    }

    /* Create release-action: */
    m_pActionRelease = new QAction(this);
    {
        /* Configure release-action: */
        m_pActionRelease->setShortcut(QKeySequence("Ctrl+L"));
        m_pActionRelease->setIcon(UIIconPool::iconSetFull(QSize(22, 22), QSize(16, 16),
                                                          ":/hd_release_22px.png", ":/hd_release_16px.png",
                                                          ":/hd_release_disabled_22px.png", ":/hd_release_disabled_16px.png"));
        connect(m_pActionRelease, SIGNAL(triggered()), this, SLOT(sltReleaseMedium()));
    }

    /* Create refresh-action: */
    m_pActionRefresh = new QAction(this);
    {
        /* Configure refresh-action: */
        m_pActionRefresh->setShortcut(QKeySequence(QKeySequence::Refresh));
        m_pActionRefresh->setIcon(UIIconPool::iconSetFull(QSize(22, 22), QSize(16, 16),
                                                          ":/refresh_22px.png", ":/refresh_16px.png",
                                                          ":/refresh_disabled_22px.png", ":/refresh_disabled_16px.png"));
        connect(m_pActionRefresh, SIGNAL(triggered()), this, SLOT(sltRefreshAll()));
    }
}

void UIMediumManager::prepareMenuBar()
{
    /* Create actions-menu for menu-bar: */
    m_pMenu = menuBar()->addMenu(QString());
    {
        /* Configure menu-bar menu: */
        m_pMenu->addAction(m_pActionCopy);
        m_pMenu->addAction(m_pActionModify);
        m_pMenu->addAction(m_pActionRemove);
        m_pMenu->addAction(m_pActionRelease);
        m_pMenu->addAction(m_pActionRefresh);
    }
}

void UIMediumManager::prepareToolBar()
{
    /* Create tool-bar: */
    m_pToolBar = new UIToolBar(this);
    {
        /* Configure tool-bar: */
        m_pToolBar->setIconSize(QSize(22, 22));
        m_pToolBar->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        m_pToolBar->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
        /* Add tool-bar actions: */
        m_pToolBar->addAction(m_pActionCopy);
        m_pToolBar->addAction(m_pActionModify);
        m_pToolBar->addAction(m_pActionRemove);
        m_pToolBar->addAction(m_pActionRelease);
        m_pToolBar->addAction(m_pActionRefresh);
        /* Integrate tool-bar into dialog: */
        QVBoxLayout *pMainLayout = qobject_cast<QVBoxLayout*>(centralWidget()->layout());
        Assert(pMainLayout);
#if MAC_LEOPARD_STYLE
        /* Enable unified tool-bars on Mac OS X. Available on Qt >= 4.3: */
        addToolBar(m_pToolBar);
        m_pToolBar->setMacToolbar();
        /* No spacing/margin on the Mac: */
        pMainLayout->setContentsMargins(0, 0, 0, 0);
        pMainLayout->insertSpacing(0, 10);
#else /* MAC_LEOPARD_STYLE */
        /* Add the tool-bar: */
        pMainLayout->insertWidget(0, m_pToolBar);
        /* Set spacing/margin like in the selector window: */
        pMainLayout->setSpacing(5);
        pMainLayout->setContentsMargins(5, 5, 5, 5);
#endif /* MAC_LEOPARD_STYLE */
    }
}

void UIMediumManager::prepareContextMenu()
{
    /* Create context-menu: */
    m_pContextMenu = new QMenu(this);
    {
        /* Configure contex-menu: */
        m_pContextMenu->addAction(m_pActionCopy);
        m_pContextMenu->addAction(m_pActionModify);
        m_pContextMenu->addAction(m_pActionRemove);
        m_pContextMenu->addAction(m_pActionRelease);
    }
}

void UIMediumManager::prepareTabWidget()
{
    /* Tab-widget created in .ui file. */
    {
        /* Configure tab-widget: */
        mTabWidget->setFocusPolicy(Qt::TabFocus);
        mTabWidget->setTabIcon(TabIndex_HD, m_iconHD);
        mTabWidget->setTabIcon(TabIndex_CD, m_iconCD);
        mTabWidget->setTabIcon(TabIndex_FD, m_iconFD);
        connect(mTabWidget, SIGNAL(currentChanged(int)), this, SLOT(sltHandleCurrentTabChanged()));
    }
}

void UIMediumManager::prepareTreeWidgets()
{
    /* Prepare tree-widget HD: */
    prepareTreeWidgetHD();
    /* Prepare tree-widget CD: */
    prepareTreeWidgetCD();
    /* Prepare tree-widget FD: */
    prepareTreeWidgetFD();

    /* Focus current tree-widget: */
    currentTreeWidget()->setFocus();
}

void UIMediumManager::prepareTreeWidgetHD()
{
    /* HD tree-widget created in .ui file. */
    {
        /* Configure HD tree-widget: */
        QTreeWidget *pTreeWidget = treeWidget(UIMediumType_HardDisk);
        pTreeWidget->setColumnCount(3);
        pTreeWidget->sortItems(0, Qt::AscendingOrder);
        pTreeWidget->header()->setResizeMode(0, QHeaderView::Fixed);
        pTreeWidget->header()->setResizeMode(1, QHeaderView::ResizeToContents);
        pTreeWidget->header()->setResizeMode(2, QHeaderView::ResizeToContents);
        pTreeWidget->header()->setStretchLastSection(false);
        pTreeWidget->setSortingEnabled(true);
        connect(pTreeWidget, SIGNAL(currentItemChanged(QTreeWidgetItem*, QTreeWidgetItem*)),
                this, SLOT(sltHandleCurrentItemChanged()));
        connect(pTreeWidget, SIGNAL(itemDoubleClicked(QTreeWidgetItem*, int)),
                this, SLOT(sltHandleDoubleClick()));
        connect(pTreeWidget, SIGNAL(customContextMenuRequested(const QPoint&)),
                this, SLOT(sltHandleContextMenuCall(const QPoint&)));
        connect(pTreeWidget, SIGNAL(resized(const QSize&, const QSize&)),
                this, SLOT(sltPerformTablesAdjustment()), Qt::QueuedConnection);
        connect(pTreeWidget->header(), SIGNAL(sectionResized(int, int, int)),
                this, SLOT(sltPerformTablesAdjustment()), Qt::QueuedConnection);
    }
}

void UIMediumManager::prepareTreeWidgetCD()
{
    /* CD tree-widget created in .ui file. */
    {
        /* Configure CD tree-widget: */
        QTreeWidget *pTreeWidget = treeWidget(UIMediumType_DVD);
        pTreeWidget->setColumnCount(2);
        pTreeWidget->sortItems(0, Qt::AscendingOrder);
        pTreeWidget->header()->setResizeMode(0, QHeaderView::Fixed);
        pTreeWidget->header()->setResizeMode(1, QHeaderView::ResizeToContents);
        pTreeWidget->header()->setStretchLastSection(false);
        pTreeWidget->setSortingEnabled(true);
        connect(pTreeWidget, SIGNAL(currentItemChanged(QTreeWidgetItem*, QTreeWidgetItem*)),
                this, SLOT(sltHandleCurrentItemChanged()));
        connect(pTreeWidget, SIGNAL(itemDoubleClicked(QTreeWidgetItem*, int)),
                this, SLOT(sltHandleDoubleClick()));
        connect(pTreeWidget, SIGNAL(customContextMenuRequested(const QPoint&)),
                this, SLOT(sltHandleContextMenuCall(const QPoint&)));
        connect(pTreeWidget, SIGNAL(resized(const QSize&, const QSize&)),
                this, SLOT(sltPerformTablesAdjustment()), Qt::QueuedConnection);
        connect(pTreeWidget->header(), SIGNAL(sectionResized(int, int, int)),
                this, SLOT(sltPerformTablesAdjustment()), Qt::QueuedConnection);
    }
}

void UIMediumManager::prepareTreeWidgetFD()
{
    /* FD tree-widget created in .ui file. */
    {
        /* Configure FD tree-widget: */
        QTreeWidget *pTreeWidget = treeWidget(UIMediumType_Floppy);
        pTreeWidget->setColumnCount(2);
        pTreeWidget->sortItems(0, Qt::AscendingOrder);
        pTreeWidget->header()->setResizeMode(0, QHeaderView::Fixed);
        pTreeWidget->header()->setResizeMode(1, QHeaderView::ResizeToContents);
        pTreeWidget->header()->setStretchLastSection(false);
        pTreeWidget->setSortingEnabled(true);
        connect(pTreeWidget, SIGNAL(currentItemChanged(QTreeWidgetItem*, QTreeWidgetItem*)),
                this, SLOT(sltHandleCurrentItemChanged()));
        connect(pTreeWidget, SIGNAL(itemDoubleClicked(QTreeWidgetItem*, int)),
                this, SLOT(sltHandleDoubleClick()));
        connect(pTreeWidget, SIGNAL(customContextMenuRequested(const QPoint&)),
                this, SLOT(sltHandleContextMenuCall(const QPoint&)));
        connect(pTreeWidget, SIGNAL(resized(const QSize&, const QSize&)),
                this, SLOT(sltPerformTablesAdjustment()), Qt::QueuedConnection);
        connect(pTreeWidget->header(), SIGNAL(sectionResized(int, int, int)),
                this, SLOT(sltPerformTablesAdjustment()), Qt::QueuedConnection);
    }
}

void UIMediumManager::prepareInformationPanes()
{
    /* Information-panes created in .ui file. */
    {
        /* Configure information-panes: */
        QList<QILabel*> panes = findChildren<QILabel*>();
        foreach (QILabel *pPane, panes)
            pPane->setFullSizeSelection(true);
    }
}

void UIMediumManager::prepareButtonBox()
{
    /* Button-box created in .ui file. */
    {
        /* Configure button-box: */
        mButtonBox->button(QDialogButtonBox::Ok)->setDefault(true);
        connect(mButtonBox, SIGNAL(accepted()), this, SLOT(accept()));
        connect(mButtonBox, SIGNAL(helpRequested()), &msgCenter(), SLOT(sltShowHelpHelpDialog()));
    }
}

void UIMediumManager::prepareProgressBar()
{
    /* Create progress-bar: */
    m_pProgressBar = new UIEnumerationProgressBar(this);
    {
        /* Configure progress-bar: */
        m_pProgressBar->hide();
        mButtonBox->addExtraWidget(m_pProgressBar);
    }
}

#ifdef Q_WS_MAC
void UIMediumManager::prepareMacWindowMenu()
{
    /* Create window-menu for menu-bar: */
    menuBar()->addMenu(UIWindowMenuManager::instance()->createMenu(this));
    UIWindowMenuManager::instance()->addWindow(this);
}
#endif /* Q_WS_MAC */

void UIMediumManager::repopulateTreeWidgets()
{
    /* Remember current medium-items: */
    if (UIMediumItem *pMediumItem = mediumItem(UIMediumType_HardDisk))
        m_strCurrentIdHD = pMediumItem->id();
    if (UIMediumItem *pMediumItem = mediumItem(UIMediumType_DVD))
        m_strCurrentIdCD = pMediumItem->id();
    if (UIMediumItem *pMediumItem = mediumItem(UIMediumType_Floppy))
        m_strCurrentIdFD = pMediumItem->id();

    /* Clear tree-widgets: */
    QTreeWidget *pTreeWidgetHD = treeWidget(UIMediumType_HardDisk);
    QTreeWidget *pTreeWidgetCD = treeWidget(UIMediumType_DVD);
    QTreeWidget *pTreeWidgetFD = treeWidget(UIMediumType_Floppy);
    setCurrentItem(pTreeWidgetHD, 0);
    setCurrentItem(pTreeWidgetCD, 0);
    setCurrentItem(pTreeWidgetFD, 0);
    pTreeWidgetHD->clear();
    pTreeWidgetCD->clear();
    pTreeWidgetFD->clear();

    /* Create medium-items (do not change current one): */
    m_fPreventChangeCurrentItem = true;
    foreach (const QString &strMediumID, vboxGlobal().mediumIDs())
        sltHandleMediumCreated(strMediumID);
    m_fPreventChangeCurrentItem = false;

    /* Select first item as current one if nothing selected: */
    if (!mediumItem(UIMediumType_HardDisk))
        if (QTreeWidgetItem *pItem = pTreeWidgetHD->topLevelItem(0))
            setCurrentItem(pTreeWidgetHD, pItem);
    if (!mediumItem(UIMediumType_DVD))
        if (QTreeWidgetItem *pItem = pTreeWidgetCD->topLevelItem(0))
            setCurrentItem(pTreeWidgetCD, pItem);
    if (!mediumItem(UIMediumType_Floppy))
        if (QTreeWidgetItem *pItem = pTreeWidgetFD->topLevelItem(0))
            setCurrentItem(pTreeWidgetFD, pItem);
}

void UIMediumManager::refetchCurrentMediumItem(UIMediumType type)
{
    /* Get corresponding medium-item: */
    UIMediumItem *pMediumItem = mediumItem(type);

    /* If medium-item set: */
    if (pMediumItem)
    {
        /* Set the file for the proxy icon: */
        setFileForProxyIcon(pMediumItem->location());
        /* Make sure current medium-item visible: */
        treeWidget(type)->scrollToItem(pMediumItem, QAbstractItemView::EnsureVisible);
    }

    /* Update actions: */
    updateActions();

    /* Update corresponding information-panes: */
    updateInformationPanes(type);
}

void UIMediumManager::refetchCurrentChosenMediumItem()
{
    refetchCurrentMediumItem(currentMediumType());
}

void UIMediumManager::refetchCurrentMediumItems()
{
    refetchCurrentMediumItem(UIMediumType_HardDisk);
    refetchCurrentMediumItem(UIMediumType_DVD);
    refetchCurrentMediumItem(UIMediumType_Floppy);
}

void UIMediumManager::updateActions()
{
    /* Get current medium-item: */
    UIMediumItem *pMediumItem = currentMediumItem();

    /* Calculate actions accessibility: */
    bool fNotInEnumeration = !vboxGlobal().isMediumEnumerationInProgress();
    bool fActionEnabledCopy = currentMediumType() == UIMediumType_HardDisk &&
                              fNotInEnumeration && pMediumItem && checkMediumFor(pMediumItem, Action_Copy);
    bool fActionEnabledModify = currentMediumType() == UIMediumType_HardDisk &&
                                fNotInEnumeration && pMediumItem && checkMediumFor(pMediumItem, Action_Modify);
    bool fActionEnabledRemove = fNotInEnumeration && pMediumItem && checkMediumFor(pMediumItem, Action_Remove);
    bool fActionEnabledRelease = fNotInEnumeration && pMediumItem && checkMediumFor(pMediumItem, Action_Release);

    /* Apply actions accessibility: */
    m_pActionCopy->setEnabled(fActionEnabledCopy);
    m_pActionModify->setEnabled(fActionEnabledModify);
    m_pActionRemove->setEnabled(fActionEnabledRemove);
    m_pActionRelease->setEnabled(fActionEnabledRelease);
}

void UIMediumManager::updateTabIcons(UIMediumItem *pMediumItem, Action action)
{
    /* Make sure medium-item is valid: */
    AssertReturnVoid(pMediumItem);

    /* Prepare data for tab: */
    int iTab = -1;
    const QIcon *pIcon = 0;
    bool *pfInaccessible = 0;
    switch (pMediumItem->mediumType())
    {
        case UIMediumType_HardDisk:
            iTab = TabIndex_HD;
            pIcon = &m_iconHD;
            pfInaccessible = &m_fInaccessibleHD;
            break;
        case UIMediumType_DVD:
            iTab = TabIndex_CD;
            pIcon = &m_iconCD;
            pfInaccessible = &m_fInaccessibleCD;
            break;
        case UIMediumType_Floppy:
            iTab = TabIndex_FD;
            pIcon = &m_iconFD;
            pfInaccessible = &m_fInaccessibleFD;
            break;
        default:
            AssertFailed();
    }
    AssertReturnVoid(iTab != -1 && pIcon && pfInaccessible);

    switch (action)
    {
        case Action_Add:
        {
            /* Does it change the overall state? */
            if (*pfInaccessible || pMediumItem->state() != KMediumState_Inaccessible)
                break; /* no */

            *pfInaccessible = true;

            mTabWidget->setTabIcon(iTab, vboxGlobal().warningIcon());

            break;
        }
        case Action_Edit:
        case Action_Remove:
        {
            bool fCheckRest = false;

            if (action == Action_Edit)
            {
                /* Does it change the overall state? */
                if ((*pfInaccessible && pMediumItem->state() == KMediumState_Inaccessible) ||
                    (!*pfInaccessible && pMediumItem->state() != KMediumState_Inaccessible))
                    break; /* no */

                /* Is the given item in charge? */
                if (!*pfInaccessible && pMediumItem->state() == KMediumState_Inaccessible)
                    *pfInaccessible = true; /* yes */
                else
                    fCheckRest = true; /* no */
            }
            else
                fCheckRest = true;

            if (fCheckRest)
            {
                /* Find the first KMediumState_Inaccessible item to be in charge: */
                CheckIfSuitableByState lookForState(KMediumState_Inaccessible);
                CheckIfSuitableByID ignoreID(pMediumItem->id());
                UIMediumItem *pInaccessibleMediumItem = searchItem(pMediumItem->treeWidget(), lookForState, &ignoreID);
                *pfInaccessible = !!pInaccessibleMediumItem;
            }

            if (*pfInaccessible)
                mTabWidget->setTabIcon(iTab, vboxGlobal().warningIcon());
            else
                mTabWidget->setTabIcon(iTab, *pIcon);

            break;
        }
    }
}

void UIMediumManager::updateInformationPanes(UIMediumType type /* = UIMediumType_Invalid */)
{
    /* Make sure type is valid: */
    if (type == UIMediumType_Invalid)
        type = currentMediumType();

    /* Depending on required type: */
    switch (type)
    {
        case UIMediumType_HardDisk: updateInformationPanesHD(); break;
        case UIMediumType_DVD:      updateInformationPanesCD(); break;
        case UIMediumType_Floppy:   updateInformationPanesFD(); break;
        case UIMediumType_All:
            updateInformationPanesHD();
            updateInformationPanesCD();
            updateInformationPanesFD();
            break;
        default: break;
    }
}

void UIMediumManager::updateInformationPanesHD()
{
    /* Get current hard-drive medium-item: */
    UIMediumItem *pCurrentItem = mediumItem(UIMediumType_HardDisk);

    /* If current item is not set: */
    if (!pCurrentItem)
    {
        /* Just clear information panes: */
        m_pTypePane->clear();
        m_pLocationPane->clear();
        m_pFormatPane->clear();
        m_pDetailsPane->clear();
        m_pUsagePane->clear();
    }
    /* If current item is set: */
    else
    {
        /* Acquire required details: */
        QString strDetails = pCurrentItem->details();
        QString strUsage = pCurrentItem->usage().isNull() ?
                           formatPaneText(QApplication::translate("VBoxMediaManagerDlg", "<i>Not&nbsp;Attached</i>"), false) :
                           formatPaneText(pCurrentItem->usage());
        m_pTypePane->setText(pCurrentItem->hardDiskType());
        m_pLocationPane->setText(formatPaneText(pCurrentItem->location(), true, "end"));
        m_pFormatPane->setText(pCurrentItem->hardDiskFormat());
        m_pDetailsPane->setText(strDetails);
        m_pUsagePane->setText(strUsage);
    }

    /* Enable/disable information-panes container: */
    mHDContainer->setEnabled(pCurrentItem);
}

void UIMediumManager::updateInformationPanesCD()
{
    /* Get current optical medium-item: */
    UIMediumItem *pCurrentItem = mediumItem(UIMediumType_DVD);

    /* If current item is not set: */
    if (!pCurrentItem)
    {
        /* Just clear information panes: */
        mIpCD1->clear();
        mIpCD2->clear();
    }
    /* If current item is set: */
    else
    {
        /* Update required details: */
        QString strUsage = pCurrentItem->usage().isNull() ?
                           formatPaneText(QApplication::translate("VBoxMediaManagerDlg", "<i>Not&nbsp;Attached</i>"), false) :
                           formatPaneText(pCurrentItem->usage());
        mIpCD1->setText(formatPaneText(pCurrentItem->location(), true, "end"));
        mIpCD2->setText(strUsage);
    }

    /* Enable/disable information-panes container: */
    mCDContainer->setEnabled(pCurrentItem);
}

void UIMediumManager::updateInformationPanesFD()
{
    /* Get current floppy medium-item: */
    UIMediumItem *pCurrentItem = mediumItem(UIMediumType_Floppy);

    /* If current item is not set: */
    if (!pCurrentItem)
    {
        /* Just clear information panes: */
        mIpFD1->clear();
        mIpFD2->clear();
    }
    /* If current item is set: */
    else
    {
        /* Update required details: */
        QString strUsage = pCurrentItem->usage().isNull() ?
                           formatPaneText(QApplication::translate("VBoxMediaManagerDlg", "<i>Not&nbsp;Attached</i>"), false) :
                           formatPaneText(pCurrentItem->usage());
        mIpFD1->setText(formatPaneText(pCurrentItem->location(), true, "end"));
        mIpFD2->setText(strUsage);
    }

    /* Enable/disable information-panes container: */
    mFDContainer->setEnabled(pCurrentItem);
}

#ifdef Q_WS_MAC
void UIMediumManager::cleanupMacWindowMenu()
{
    /* Destroy window-menu of menu-bar: */
    UIWindowMenuManager::instance()->removeWindow(this);
    UIWindowMenuManager::instance()->destroyMenu(this);
}
#endif /* Q_WS_MAC */

void UIMediumManager::cleanup()
{
#ifdef Q_WS_MAC
    /* Cleanup Mac window-menu: */
    cleanupMacWindowMenu();
#endif /* Q_WS_MAC */
}

void UIMediumManager::retranslateUi()
{
    /* Translate uic generated strings: */
    Ui::UIMediumManager::retranslateUi(this);

    /* Menu: */
    m_pMenu->setTitle(QApplication::translate("VBoxMediaManagerDlg", "&Actions"));
    /* Action names: */
    m_pActionCopy->setText(QApplication::translate("VBoxMediaManagerDlg", "&Copy..."));
    m_pActionModify->setText(QApplication::translate("VBoxMediaManagerDlg", "&Modify..."));
    m_pActionRemove->setText(QApplication::translate("VBoxMediaManagerDlg", "R&emove"));
    m_pActionRelease->setText(QApplication::translate("VBoxMediaManagerDlg", "Re&lease"));
    m_pActionRefresh->setText(QApplication::translate("VBoxMediaManagerDlg", "Re&fresh"));
    /* Action tool-tips: */
    m_pActionCopy->setToolTip(m_pActionCopy->text().remove('&') + QString(" (%1)").arg(m_pActionCopy->shortcut().toString()));
    m_pActionModify->setToolTip(m_pActionModify->text().remove('&') + QString(" (%1)").arg(m_pActionModify->shortcut().toString()));
    m_pActionRemove->setToolTip(m_pActionRemove->text().remove('&') + QString(" (%1)").arg(m_pActionRemove->shortcut().toString()));
    m_pActionRelease->setToolTip(m_pActionRelease->text().remove('&') + QString(" (%1)").arg(m_pActionRelease->shortcut().toString()));
    m_pActionRefresh->setToolTip(m_pActionRefresh->text().remove('&') + QString(" (%1)").arg(m_pActionRefresh->shortcut().toString()));
    /* Action status-tips: */
    m_pActionCopy->setStatusTip(QApplication::translate("VBoxMediaManagerDlg", "Copy an existing disk image file"));
    m_pActionModify->setStatusTip(QApplication::translate("VBoxMediaManagerDlg", "Modify the attributes of the selected disk image file"));
    m_pActionRemove->setStatusTip(QApplication::translate("VBoxMediaManagerDlg", "Remove the selected disk image file"));
    m_pActionRelease->setStatusTip(QApplication::translate("VBoxMediaManagerDlg", "Release the selected disk image file by detaching it from the machines"));
    m_pActionRefresh->setStatusTip(QApplication::translate("VBoxMediaManagerDlg", "Refresh the list of disk image files"));

    /* Tool-bar: */
#ifdef Q_WS_MAC
# ifdef QT_MAC_USE_COCOA
    /* There is a bug in Qt Cocoa which result in showing a "more arrow" when
       the necessary size of the toolbar is increased. Also for some languages
       the with doesn't match if the text increase. So manually adjust the size
       after changing the text. */
    m_pToolBar->updateLayout();
# endif /* QT_MAC_USE_COCOA */
#endif /* Q_WS_MAC */

    // TODO: Just rename translation context in .nls files!
    /* Translations moved from VBoxMediaManagerDlg.ui file to keep old translation context: */
    setWindowTitle(QApplication::translate("VBoxMediaManagerDlg", "Virtual Media Manager"));
    mTabWidget->setTabText(0, tr("&Hard drives"));
    QTreeWidget *pTreeWidgetHD = treeWidget(UIMediumType_HardDisk);
    pTreeWidgetHD->headerItem()->setText(0, QApplication::translate("VBoxMediaManagerDlg", "Name"));
    pTreeWidgetHD->headerItem()->setText(1, QApplication::translate("VBoxMediaManagerDlg", "Virtual Size"));
    pTreeWidgetHD->headerItem()->setText(2, QApplication::translate("VBoxMediaManagerDlg", "Actual Size"));
    m_pTypeLabel->setText(QApplication::translate("VBoxMediaManagerDlg", "Type:"));
    m_pLocationLabel->setText(QApplication::translate("VBoxMediaManagerDlg", "Location:"));
    m_pFormatLabel->setText(QApplication::translate("VBoxMediaManagerDlg", "Format:"));
    m_pDetailsLabel->setText(QApplication::translate("VBoxMediaManagerDlg", "Storage details:"));
    m_pUsageLabel->setText(QApplication::translate("VBoxMediaManagerDlg", "Attached to:"));
    mTabWidget->setTabText(1, tr("&Optical disks"));
    QTreeWidget *pTreeWidgetCD = treeWidget(UIMediumType_DVD);
    pTreeWidgetCD->headerItem()->setText(0, QApplication::translate("VBoxMediaManagerDlg", "Name"));
    pTreeWidgetCD->headerItem()->setText(1, QApplication::translate("VBoxMediaManagerDlg", "Size"));
    mLbCD1->setText(QApplication::translate("VBoxMediaManagerDlg", "Location:"));
    mLbCD2->setText(QApplication::translate("VBoxMediaManagerDlg", "Attached to:"));
    mTabWidget->setTabText(2, tr("&Floppy disks"));
    QTreeWidget *pTreeWidgetFD = treeWidget(UIMediumType_Floppy);
    pTreeWidgetFD->headerItem()->setText(0, QApplication::translate("VBoxMediaManagerDlg", "Name"));
    pTreeWidgetFD->headerItem()->setText(1, QApplication::translate("VBoxMediaManagerDlg", "Size"));
    mLbFD1->setText(QApplication::translate("VBoxMediaManagerDlg", "Location:"));
    mLbFD2->setText(QApplication::translate("VBoxMediaManagerDlg", "Attached to:"));

    /* Progress-bar: */
    m_pProgressBar->setText(QApplication::translate("VBoxMediaManagerDlg", "Checking accessibility"));
#ifdef Q_WS_MAC
    /* Make sure that the widgets aren't jumping around
     * while the progress-bar get visible. */
    m_pProgressBar->adjustSize();
    int h = m_pProgressBar->height();
    mButtonBox->setMinimumHeight(h + 12);
#endif /* Q_WS_MAC */

    /* Button-box: */
    mButtonBox->button(QDialogButtonBox::Ok)->setText(tr("C&lose"));

    /* Full refresh if there is at least one item present: */
    if (pTreeWidgetHD->topLevelItemCount() || pTreeWidgetCD->topLevelItemCount() || pTreeWidgetFD->topLevelItemCount())
        sltRefreshAll();
}

UIMediumItem* UIMediumManager::createMediumItem(const UIMedium &medium)
{
    /* Get medium type: */
    UIMediumType type = medium.type();

    /* Create medium-item: */
    UIMediumItem *pMediumItem = 0;
    switch (type)
    {
        /* Of hard-drive type: */
        case UIMediumType_HardDisk:
        {
            pMediumItem = createHardDiskItem(medium);
            AssertPtrReturn(pMediumItem, 0);
            if (pMediumItem->id() == m_strCurrentIdHD)
            {
                setCurrentItem(treeWidget(UIMediumType_HardDisk), pMediumItem);
                m_strCurrentIdHD = QString();
            }
            break;
        }
        /* Of optical-image type: */
        case UIMediumType_DVD:
        {
            pMediumItem = new UIMediumItemCD(medium, treeWidget(UIMediumType_DVD));
            LogRel2(("UIMediumManager: Optical medium-item with ID={%s} created.\n", medium.id().toAscii().constData()));
            AssertPtrReturn(pMediumItem, 0);
            if (pMediumItem->id() == m_strCurrentIdCD)
            {
                setCurrentItem(treeWidget(UIMediumType_DVD), pMediumItem);
                m_strCurrentIdCD = QString();
            }
            break;
        }
        /* Of floppy-image type: */
        case UIMediumType_Floppy:
        {
            pMediumItem = new UIMediumItemFD(medium, treeWidget(UIMediumType_Floppy));
            LogRel2(("UIMediumManager: Floppy medium-item with ID={%s} created.\n", medium.id().toAscii().constData()));
            AssertPtrReturn(pMediumItem, 0);
            if (pMediumItem->id() == m_strCurrentIdFD)
            {
                setCurrentItem(treeWidget(UIMediumType_Floppy), pMediumItem);
                m_strCurrentIdFD = QString();
            }
            break;
        }
        default: AssertMsgFailed(("Medium-type unknown: %d\n", type)); break;
    }
    AssertPtrReturn(pMediumItem, 0);

    /* Update tab-icons: */
    updateTabIcons(pMediumItem, Action_Add);

    /* Re-fetch medium-item if it is current one created: */
    if (pMediumItem == mediumItem(type))
        refetchCurrentMediumItem(type);

    /* Return created medium-item: */
    return pMediumItem;
}

UIMediumItem* UIMediumManager::createHardDiskItem(const UIMedium &medium)
{
    /* Make sure passed medium is valid: */
    AssertReturn(!medium.medium().isNull(), 0);

    /* Get corresponding tree-widget: */
    QTreeWidget *pTreeWidget = treeWidget(UIMediumType_HardDisk);

    /* Search for existing medium-item: */
    UIMediumItem *pMediumItem = searchItem(pTreeWidget, CheckIfSuitableByID(medium.id()));

    /* If medium-item do not exists: */
    if (!pMediumItem)
    {
        /* If medium have a parent: */
        if (medium.parentID() != UIMedium::nullID())
        {
            /* Try to find parent medium-item: */
            UIMediumItem *pParentMediumItem = searchItem(pTreeWidget, CheckIfSuitableByID(medium.parentID()));
            /* If parent medium-item was not found: */
            if (!pParentMediumItem)
            {
                /* Make sure corresponding parent medium is already cached! */
                UIMedium parentMedium = vboxGlobal().medium(medium.parentID());
                if (parentMedium.isNull())
                    AssertMsgFailed(("Parent medium with ID={%s} was not found!\n", medium.parentID().toAscii().constData()));
                /* Try to create parent medium-item: */
                else
                    pParentMediumItem = createHardDiskItem(parentMedium);
            }
            /* If parent medium-item was found: */
            if (pParentMediumItem)
            {
                pMediumItem = new UIMediumItemHD(medium, pParentMediumItem);
                LogRel2(("UIMediumManager: Child hard-drive medium-item with ID={%s} created.\n", medium.id().toAscii().constData()));
            }
        }
        /* Else just create item as top-level one: */
        if (!pMediumItem)
        {
            pMediumItem = new UIMediumItemHD(medium, pTreeWidget);
            LogRel2(("UIMediumManager: Root hard-drive medium-item with ID={%s} created.\n", medium.id().toAscii().constData()));
        }
    }

    /* Return created medium-item: */
    return pMediumItem;
}

void UIMediumManager::updateMediumItem(const UIMedium &medium)
{
    /* Get medium type: */
    UIMediumType type = medium.type();

    /* Search for existing medium-item, create if was not found: */
    UIMediumItem *pMediumItem = searchItem(treeWidget(type), CheckIfSuitableByID(medium.id()));
    if (!pMediumItem) pMediumItem = createMediumItem(medium);
    AssertPtrReturnVoid(pMediumItem);

    /* Update medium-item: */
    pMediumItem->setMedium(medium);
    LogRel2(("UIMediumManager: Medium-item with ID={%s} updated.\n", medium.id().toAscii().constData()));

    /* Update tab-icons: */
    updateTabIcons(pMediumItem, Action_Edit);

    /* Re-fetch medium-item if it is current one updated: */
    if (pMediumItem == mediumItem(type))
        refetchCurrentMediumItem(type);
}

void UIMediumManager::deleteMediumItem(const QString &strMediumID)
{
    /* Search for corresponding tree-widget: */
    QList<UIMediumType> types;
    types << UIMediumType_HardDisk << UIMediumType_DVD << UIMediumType_Floppy;
    QTreeWidget *pTreeWidget = 0;
    UIMediumItem *pMediumItem = 0;
    foreach (UIMediumType type, types)
    {
        /* Get iterated tree-widget: */
        pTreeWidget = treeWidget(type);
        /* Search for existing medium-item: */
        pMediumItem = searchItem(pTreeWidget, CheckIfSuitableByID(strMediumID));
        if (pMediumItem)
            break;
    }

    /* Ignore medium-item (if it was not found): */
    if (!pMediumItem)
        return;

    /* Update tab-icons: */
    updateTabIcons(pMediumItem, Action_Remove);

    /* Delete medium-item: */
    delete pMediumItem;
    LogRel2(("UIMediumManager: Medium-item with ID={%s} deleted.\n", strMediumID.toAscii().constData()));

    /* If there is no current medium-item now selected
     * we have to choose first-available medium-item as current one: */
    if (!pTreeWidget->currentItem())
        setCurrentItem(pTreeWidget, pTreeWidget->topLevelItem(0));
}

UIMediumType UIMediumManager::mediumType(QTreeWidget *pTreeWidget) const
{
    /* Hard-drive tree-widget: */
    if (pTreeWidget == treeWidget(UIMediumType_HardDisk)) return UIMediumType_HardDisk;
    /* Optical-image tree-widget: */
    if (pTreeWidget == treeWidget(UIMediumType_DVD)) return UIMediumType_DVD;
    /* Floppy-image tree-widget: */
    if (pTreeWidget == treeWidget(UIMediumType_Floppy)) return UIMediumType_Floppy;
    /* Invalid by default: */
    AssertFailedReturn(UIMediumType_Invalid);
}

UIMediumType UIMediumManager::currentMediumType() const
{
    /* Return current medium type: */
    switch (mTabWidget->currentIndex())
    {
        case TabIndex_HD: return UIMediumType_HardDisk;
        case TabIndex_CD: return UIMediumType_DVD;
        case TabIndex_FD: return UIMediumType_Floppy;
        default: AssertMsgFailed(("Unknown page type: %d\n", mTabWidget->currentIndex())); break;
    }
    /* Invalid by default: */
    return UIMediumType_Invalid;
}

QTreeWidget* UIMediumManager::treeWidget(UIMediumType type) const
{
    /* Return corresponding tree-widget for known medium types: */
    switch (type)
    {
        case UIMediumType_HardDisk: return mTwHD;
        case UIMediumType_DVD:      return mTwCD;
        case UIMediumType_Floppy:   return mTwFD;
        default: AssertMsgFailed(("Unknown medium type: %d\n", type)); break;
    }
    /* Null by default: */
    return 0;
}

QTreeWidget* UIMediumManager::currentTreeWidget() const
{
    /* Return current tree-widget: */
    return treeWidget(currentMediumType());
}

UIMediumItem* UIMediumManager::mediumItem(UIMediumType type) const
{
    /* Get corresponding tree-widget: */
    QTreeWidget *pTreeWidget = treeWidget(type);
    /* Return corresponding medium-item: */
    return pTreeWidget ? toMediumItem(pTreeWidget->currentItem()) : 0;
}

UIMediumItem* UIMediumManager::currentMediumItem() const
{
    /* Return current medium-item: */
    return mediumItem(currentMediumType());
}

void UIMediumManager::setCurrentItem(QTreeWidget *pTreeWidget, QTreeWidgetItem *pItem)
{
    /* Make sure passed tree-widget is valid: */
    AssertPtrReturnVoid(pTreeWidget);

    /* Make passed item current for passed tree-widget: */
    pTreeWidget->setCurrentItem(pItem);

    /* If non NULL item was passed: */
    if (pItem)
    {
        /* Make sure it's also selected, and visible: */
        pItem->setSelected(true);
        pTreeWidget->scrollToItem(pItem, QAbstractItemView::EnsureVisible);
    }

    /* Re-fetch currently chosen medium-item: */
    refetchCurrentChosenMediumItem();
}

/* static */
UIMediumItem* UIMediumManager::searchItem(QTreeWidget *pTreeWidget, const CheckIfSuitableBy &condition, CheckIfSuitableBy *pException)
{
    /* Make sure argument is valid: */
    if (!pTreeWidget)
        return 0;

    /* Return wrapper: */
    return searchItem(pTreeWidget->invisibleRootItem(), condition, pException);
}

/* static */
UIMediumItem* UIMediumManager::searchItem(QTreeWidgetItem *pParentItem, const CheckIfSuitableBy &condition, CheckIfSuitableBy *pException)
{
    /* Make sure argument is valid: */
    if (!pParentItem)
        return 0;

    /* Verify passed item if it is of 'medium' type too: */
    if (UIMediumItem *pMediumParentItem = toMediumItem(pParentItem))
        if (   condition.isItSuitable(pMediumParentItem)
            && (!pException || !pException->isItSuitable(pMediumParentItem)))
            return pMediumParentItem;

    /* Iterate other all the children: */
    for (int iChildIndex = 0; iChildIndex < pParentItem->childCount(); ++iChildIndex)
        if (UIMediumItem *pMediumChildItem = toMediumItem(pParentItem->child(iChildIndex)))
            if (UIMediumItem *pRequiredMediumChildItem = searchItem(pMediumChildItem, condition, pException))
                return pRequiredMediumChildItem;

    /* Null by default: */
    return 0;
}

/* static */
bool UIMediumManager::checkMediumFor(UIMediumItem *pItem, Action action)
{
    /* Make sure passed ID is valid: */
    AssertReturn(pItem, false);

    switch (action)
    {
        case Action_Edit:
        {
            /* Edit means changing the description and alike; any media that is
             * not being read to or written from can be altered in these terms. */
            switch (pItem->state())
            {
                case KMediumState_NotCreated:
                case KMediumState_Inaccessible:
                case KMediumState_LockedRead:
                case KMediumState_LockedWrite:
                    return false;
                default:
                    break;
            }
            return true;
        }
        case Action_Copy:
        {
            /* False for children: */
            return pItem->medium().parentID() == UIMedium::nullID();
        }
        case Action_Modify:
        {
            /* False for children: */
            return pItem->medium().parentID() == UIMedium::nullID();
        }
        case Action_Remove:
        {
            /* Removable if not attached to anything: */
            return !pItem->isUsed();
        }
        case Action_Release:
        {
            /* Releasable if attached but not in snapshots: */
            return pItem->isUsed() && !pItem->isUsedInSnapshots();
        }
    }

    AssertFailedReturn(false);
}

/* static */
UIMediumItem* UIMediumManager::toMediumItem(QTreeWidgetItem *pItem)
{
    /* Cast passed QTreeWidgetItem to UIMediumItem if possible: */
    return pItem && pItem->type() == UIMediumItem::Type ? static_cast<UIMediumItem*>(pItem) : 0;
}

/* static */
QString UIMediumManager::formatPaneText(const QString &strText, bool fCompact /* = true */,
                                        const QString &strElipsis /* = "middle" */)
{
    QString compactString = QString("<compact elipsis=\"%1\">").arg(strElipsis);
    QString strInfo = QString("<nobr>%1%2%3</nobr>")
                              .arg(fCompact ? compactString : "")
                              .arg(strText.isEmpty() ?
                                   QApplication::translate("VBoxMediaManagerDlg", "--", "no info") :
                                   strText)
                              .arg(fCompact ? "</compact>" : "");
    return strInfo;
}

/* static */
bool UIMediumManager::isMediumAttachedToHiddenMachinesOnly(const UIMedium &medium)
{
    /* Iterate till the root: */
    UIMedium mediumIterator = medium;
    do
    {
        /* Ignore medium if its hidden
         * or attached to hidden machines only: */
        if (mediumIterator.isHidden())
            return true;
        /* Move iterator to parent: */
        mediumIterator = mediumIterator.parent();
    }
    while (!mediumIterator.isNull());
    /* False by default: */
    return false;
}

#include "UIMediumManager.moc"

