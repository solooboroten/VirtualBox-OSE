/** @file
 *
 * VBox frontends: Qt GUI ("VirtualBox"):
 * UIGDetailsSet class declaration
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

#ifndef __UIGDetailsSet_h__
#define __UIGDetailsSet_h__

/* GUI includes: */
#include "UIGDetailsItem.h"
#include "UIDefs.h"

/* COM includes: */
#include "COMEnums.h"
#include "CMachine.h"

/* Forward declarations: */
class UIVMItem;

/* Details set
 * for graphics details model/view architecture: */
class UIGDetailsSet : public UIGDetailsItem
{
    Q_OBJECT;

signals:

    /* Notifiers: Prepare stuff: */
    void sigElementPrepared();
    void sigSetPrepared();
    void sigSetCreationDone();

public:

    /* Graphics-item type: */
    enum { Type = UIGDetailsItemType_Set };
    int type() const { return Type; }

    /* Constructor/destructor: */
    UIGDetailsSet(UIGDetailsItem *pParent, UIVMItem *pItem,
                  const QStringList &settings, bool fFullSet);
    ~UIGDetailsSet();

    /* API: Machine stuff: */
    const CMachine& machine() const;

private slots:

    /* Handlers: Prepare stuff: */
    void sltElementPrepared();
    void sltSetPrepared();

private:

    /* Data enumerator: */
    enum SetItemData
    {
        /* Layout hints: */
        SetData_Margin,
        SetData_Spacing
    };

    /* Data provider: */
    QVariant data(int iKey) const;

    /* Children stuff: */
    void addItem(UIGDetailsItem *pItem);
    void removeItem(UIGDetailsItem *pItem);
    QList<UIGDetailsItem*> items(UIGDetailsItemType type = UIGDetailsItemType_Element) const;
    bool hasItems(UIGDetailsItemType type = UIGDetailsItemType_Element) const;
    void clearItems(UIGDetailsItemType type = UIGDetailsItemType_Element);
    UIGDetailsElement* element(DetailsElementType elementType) const;

    /* Helpers: Layout stuff: */
    void updateSizeHint();
    void updateLayout();
    int minimumWidthHint() const;
    int minimumHeightHint() const;
    QSizeF sizeHint(Qt::SizeHint which, const QSizeF &constraint = QSizeF()) const;

    /* Helpers: Prepare stuff: */
    void prepareElements(bool fFullSet);
    void prepareElement();

    /* Main variables: */
    CMachine m_machine;
    QList<UIGDetailsItem*> m_elements;

    /* Prepare variables: */
    int m_iStep;
    int m_iLastStep;
    QStringList m_settings;
};

#endif /* __UIGDetailsSet_h__ */

