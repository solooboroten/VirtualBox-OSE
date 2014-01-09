/** @file
 *
 * VBox frontends: Qt GUI ("VirtualBox"):
 * UIGChooserModel class declaration
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

#ifndef __UIGChooserModel_h__
#define __UIGChooserModel_h__

/* Qt includes: */
#include <QObject>
#include <QPointer>
#include <QTransform>
#include <QMap>
#include <QThread>
#include <QMutex>
#include <QWaitCondition>

/* GUI includes: */
#include "UIGChooserItem.h"

/* COM includes: */
#include "COMEnums.h"

/* Forward declaration: */
class QGraphicsScene;
class QGraphicsItem;
class QDrag;
class QMenu;
class QAction;
class QGraphicsSceneContextMenuEvent;
class CMachine;
class UIVMItem;
class UIGChooserHandlerMouse;
class UIGChooserHandlerKeyboard;
class QTimer;
class QPaintDevice;

/* Context-menu type: */
enum UIGraphicsSelectorContextMenuType
{
    UIGraphicsSelectorContextMenuType_Group,
    UIGraphicsSelectorContextMenuType_Machine
};

/* Graphics selector model: */
class UIGChooserModel : public QObject
{
    Q_OBJECT;

signals:

    /* Notifier: Root-item resize stuff: */
    void sigRootItemResized(const QSizeF &size, int iMinimumWidth);

    /* Notifier: Focus change: */
    void sigFocusChanged(UIGChooserItem *pFocusItem);

    /* Notifier: Selection change: */
    void sigSelectionChanged();

    /* Notifiers Status-bar stuff: */
    void sigClearStatusMessage();
    void sigShowStatusMessage(const QString &strStatusMessage);

    /* Notifier: Sliding start: */
    void sigSlidingStarted();

    /* Notifiers: Toggle stuff: */
    void sigToggleStarted();
    void sigToggleFinished();

    /* Notifiers: Group saving stuff: */
    void sigStartGroupSaving();
    void sigGroupSavingStateChanged();

public:

    /* Constructor/destructor: */
    UIGChooserModel(QObject *pParent);
    ~UIGChooserModel();

    /* API: Prepare/cleanup stuff: */
    void prepare();
    void cleanup();

    /* API: Scene getter: */
    QGraphicsScene* scene() const;

    /* API: Paint-device getter: */
    QPaintDevice* paintDevice() const;

    /* API: Root stuff: */
    UIGChooserItem* mainRoot() const;
    UIGChooserItem* root() const;
    void indentRoot(UIGChooserItem *pNewRootItem);
    void unindentRoot();
    bool isSlidingInProgress() const;

    /* API: Current item stuff: */
    void setCurrentItem(int iItemIndex);
    void setCurrentItem(UIGChooserItem *pItem);
    void unsetCurrentItem();
    UIVMItem* currentItem() const;
    QList<UIVMItem*> currentItems() const;
    void setCurrentItemDefinition(const QString &strDefinition);
    QString currentItemDefinition() const;
    bool isSingleGroupSelected() const;
    bool isAllItemsOfOneGroupSelected() const;

    /* API: Focus item stuff: */
    void setFocusItem(UIGChooserItem *pItem, bool fWithSelection = false);
    UIGChooserItem* focusItem() const;

    /* API: Positioning item stuff: */
    QGraphicsItem* itemAt(const QPointF &position, const QTransform &deviceTransform = QTransform()) const;

    /* API: Update stuff: */
    void updateGroupTree();

    /* API: Navigation stuff: */
    const QList<UIGChooserItem*>& navigationList() const;
    void removeFromNavigationList(UIGChooserItem *pItem);
    void clearNavigationList();
    void updateNavigation();

    /* API: Selection stuff: */
    const QList<UIGChooserItem*>& selectionList() const;
    void addToSelectionList(UIGChooserItem *pItem);
    void removeFromSelectionList(UIGChooserItem *pItem);
    void clearSelectionList();
    void notifySelectionChanged();

    /* API: Layout stuff: */
    void updateLayout();

    /* API: Editing stuff: */
    void startEditing();

    /* API: Drag and drop stuff: */
    void setCurrentDragObject(QDrag *pDragObject);

    /* API: Activate stuff: */
    void activate();

    /* API: Group name stuff: */
    QString uniqueGroupName(UIGChooserItem *pRoot);

    /* API: Group saving stuff: */
    void saveGroupSettings();
    bool isGroupSavingInProgress() const;

    /* API: Lookup stuff: */
    void lookFor(const QString &strLookupSymbol);
    bool isPerformingLookup() const;

private slots:

    /* Handlers: Global events: */
    void sltMachineStateChanged(QString strId, KMachineState state);
    void sltMachineDataChanged(QString strId);
    void sltMachineRegistered(QString strId, bool fRegistered);
    void sltSessionStateChanged(QString strId, KSessionState state);
    void sltSnapshotChanged(QString strId, QString strSnapshotId);

    /* Handler: View-resize: */
    void sltHandleViewResized();

    /* Handler: Drag object destruction: */
    void sltCurrentDragObjectDestroyed();
    void sltStartScrolling();

    /* Handlers: Remove currently selected items: */
    void sltRemoveCurrentlySelectedGroup();
    void sltRemoveCurrentlySelectedMachine();

    /* Handler: Group add stuff: */
    void sltAddGroupBasedOnChosenItems();

    /* Handler: Group name editing stuff: */
    void sltStartEditingSelectedGroup();

    /* Handler: Create new machine stuff: */
    void sltCreateNewMachine();

    /* Handler: Context menu hovering: */
    void sltActionHovered(QAction *pAction);

    /* Handler: Focus item destruction: */
    void sltFocusItemDestroyed();

    /* Handlers: Root-sliding stuff: */
    void sltLeftRootSlidingProgress();
    void sltRightRootSlidingProgress();
    void sltSlidingComplete();

    /* Handler: Refresh stuff: */
    void sltPerformRefreshAction();

    /* Handlers: Sorting stuff: */
    void sltSortParentGroup();
    void sltSortGroup();

    /* Handlers: Group saving stuff: */
    void sltGroupSavingStart();
    void sltGroupDefinitionsSaveComplete();
    void sltGroupOrdersSaveComplete();
    void sltReloadMachine(const QString &strId);

    /* Handler: Lookup stuff: */
    void sltEraseLookupTimer();

private:

    /* Data enumerator: */
    enum SelectorModelData
    {
        /* Layout hints: */
        SelectorModelData_Margin
    };

    /* Data provider: */
    QVariant data(int iKey) const;

    /* Helpers: Prepare stuff: */
    void prepareScene();
    void prepareRoot();
    void prepareLookup();
    void prepareContextMenu();
    void prepareHandlers();
    void prepareGroupTree();

    /* Helpers: Cleanup stuff: */
    void cleanupGroupTree();
    void cleanupHandlers();
    void cleanupContextMenu();
    void cleanupLookup();
    void cleanupRoot();
    void cleanupScene();

    /* Event handler: */
    bool eventFilter(QObject *pWatched, QEvent *pEvent);

    /* Helpers: Focus item stuff: */
    void clearRealFocus();

    /* Helpers: Current item stuff: */
    UIVMItem* searchCurrentItem(const QList<UIGChooserItem*> &list) const;
    void enumerateCurrentItems(const QList<UIGChooserItem*> &il, QList<UIVMItem*> &ol) const;
    bool contains(const QList<UIVMItem*> &list, UIVMItem *pItem) const;

    /* Helpers: Loading: */
    void loadGroupTree();
    void addMachineIntoTheTree(const CMachine &machine, bool fMakeItVisible = false);
    UIGChooserItem* getGroupItem(const QString &strName, UIGChooserItem *pParentItem, bool fAllGroupsOpened);
    bool shouldBeGroupOpened(UIGChooserItem *pParentItem, const QString &strName);
    int getDesiredPosition(UIGChooserItem *pParentItem, UIGChooserItemType type, const QString &strName);
    int positionFromDefinitions(UIGChooserItem *pParentItem, UIGChooserItemType type, const QString &strName);
    void createMachineItem(const CMachine &machine, UIGChooserItem *pParentItem);

    /* Helpers: Group saving stuff: */
    void saveGroupDefinitions();
    void saveGroupOrders();
    void gatherGroupDefinitions(QMap<QString, QStringList> &groups, UIGChooserItem *pParentGroup);
    void gatherGroupOrders(QMap<QString, QStringList> &groups, UIGChooserItem *pParentItem);
    QString fullName(UIGChooserItem *pItem);

    /* Helpers: Update stuff: */
    void updateGroupTree(UIGChooserItem *pGroupItem);

    /* Helpers: Navigation stuff: */
    QList<UIGChooserItem*> createNavigationList(UIGChooserItem *pItem);

    /* Helpers: Search stuff: */
    UIGChooserItem* findGroupItem(const QString &strName, UIGChooserItem *pParent);
    UIGChooserItem* findMachineItem(const QString &strName, UIGChooserItem *pParent);

    /* Helpers: Global event stuff: */
    void updateMachineItems(const QString &strId, UIGChooserItem *pParent);
    void removeMachineItems(const QString &strId, UIGChooserItem *pParent);

    /* Helpers: Context menu stuff: */
    bool processContextMenuEvent(QGraphicsSceneContextMenuEvent *pEvent);
    void popupContextMenu(UIGraphicsSelectorContextMenuType type, QPoint point);

    /* Handlers: Scroll event: */
    bool processDragMoveEvent(QGraphicsSceneDragDropEvent *pEvent);

    /* Helper: Root item stuff: */
    void slideRoot(bool fForward);

    /* Helper: Search stuff: */
    QList<UIGChooserItem*> gatherMachineItems(const QList<UIGChooserItem*> &selectedItems) const;

    /* Helpers: Remove stuff: */
    void removeMachineItems(const QStringList &names, QList<UIGChooserItem*> &selectedItems);
    void unregisterMachines(const QStringList &ids);

    /* Helper: Refresh stuff: */
    void enumerateInaccessibleItems(const QList<UIGChooserItem*> &il, QList<UIGChooserItem*> &ol) const;
    bool contains(const QList<UIGChooserItem*> &il, UIGChooserItem *pLookupItem) const;

    /* Helper: Sorting stuff: */
    void sortItems(UIGChooserItem *pParent, bool fRecursively = false);

    /* Helpers: Group saving stuff: */
    void makeSureGroupDefinitionsSaveIsFinished();
    void makeSureGroupOrdersSaveIsFinished();

    /* Helper: Lookup stuff: */
    UIGChooserItem* lookForItem(UIGChooserItem *pParent, const QString &strStartingFrom);

    /* Variables: */
    QGraphicsScene *m_pScene;

    QList<UIGChooserItem*> m_rootStack;
    bool m_fSliding;
    UIGChooserItem *m_pLeftRoot;
    UIGChooserItem *m_pRightRoot;
    QPointer<UIGChooserItem> m_pAfterSlidingFocus;

    QMap<QString, QStringList> m_groups;
    QList<UIGChooserItem*> m_navigationList;
    QList<UIGChooserItem*> m_selectionList;
    UIGChooserHandlerMouse *m_pMouseHandler;
    UIGChooserHandlerKeyboard *m_pKeyboardHandler;
    QPointer<QDrag> m_pCurrentDragObject;
    int m_iScrollingTokenSize;
    bool m_fIsScrollingInProgress;
    QPointer<UIGChooserItem> m_pFocusItem;
    QMenu *m_pContextMenuGroup;
    QMenu *m_pContextMenuMachine;

    /* Variables: Lookup stuff: */
    QTimer *m_pLookupTimer;
    QString m_strLookupString;
};

/* Represents group definitions save error types: */
enum UIGroupsSavingError
{
    UIGroupsSavingError_MachineLockFailed,
    UIGroupsSavingError_MachineGroupSetFailed,
    UIGroupsSavingError_MachineSettingsSaveFailed
};
Q_DECLARE_METATYPE(UIGroupsSavingError);

/* Allows to save group definitions asynchronously: */
class UIGroupDefinitionSaveThread : public QThread
{
    Q_OBJECT;

signals:

    /* Notifier: Error stuff: */
    void sigError(UIGroupsSavingError errorType, const CMachine &machine);

    /* Notifier: */
    void sigReload(QString strId);

    /* Notifier: Complete stuff: */
    void sigComplete();

public:

    /* Singleton stuff: */
    static UIGroupDefinitionSaveThread* instance();
    static void prepare();
    static void cleanup();

    /* API: Configuring stuff: */
    void configure(QObject *pParent,
                   const QMap<QString, QStringList> &oldLists,
                   const QMap<QString, QStringList> &newLists);

private slots:

    /* Handler: Error stuff: */
    void sltHandleError(UIGroupsSavingError errorType, const CMachine &machine);

private:

    /* Constructor/destructor: */
    UIGroupDefinitionSaveThread();
    ~UIGroupDefinitionSaveThread();

    /* Worker thread stuff: */
    void run();

    /* Variables: */
    static UIGroupDefinitionSaveThread *m_spInstance;
    QMap<QString, QStringList> m_oldLists;
    QMap<QString, QStringList> m_newLists;
    QMutex m_mutex;
    QWaitCondition m_condition;
};

/* Allows to save group order asynchronously: */
class UIGroupOrderSaveThread : public QThread
{
    Q_OBJECT;

signals:

    /* Notifier: Complete stuff: */
    void sigComplete();

public:

    /* Singleton stuff: */
    static UIGroupOrderSaveThread* instance();
    static void prepare();
    static void cleanup();

    /* API: Configuring stuff: */
    void configure(QObject *pParent, const QMap<QString, QStringList> &groups);

private:

    /* Constructor/destructor: */
    UIGroupOrderSaveThread();
    ~UIGroupOrderSaveThread();

    /* Worker thread stuff: */
    void run();

    /* Variables: */
    static UIGroupOrderSaveThread *m_spInstance;
    QMap<QString, QStringList> m_groups;
};

#endif /* __UIGChooserModel_h__ */

