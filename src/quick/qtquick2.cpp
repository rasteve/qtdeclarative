/****************************************************************************
**
** Copyright (C) 2012 Nokia Corporation and/or its subsidiary(-ies).
** Contact: http://www.qt-project.org/
**
** This file is part of the QtQml module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL$
** GNU Lesser General Public License Usage
** This file may be used under the terms of the GNU Lesser General Public
** License version 2.1 as published by the Free Software Foundation and
** appearing in the file LICENSE.LGPL included in the packaging of this
** file. Please review the following information to ensure the GNU Lesser
** General Public License version 2.1 requirements will be met:
** http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Nokia gives you certain additional
** rights. These rights are described in the Nokia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU General
** Public License version 3.0 as published by the Free Software Foundation
** and appearing in the file LICENSE.GPL included in the packaging of this
** file. Please review the following information to ensure the GNU General
** Public License version 3.0 requirements will be met:
** http://www.gnu.org/copyleft/gpl.html.
**
** Other Usage
** Alternatively, this file may be used in accordance with the terms and
** conditions contained in a signed written agreement between you and Nokia.
**
**
**
**
**
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include "qtquick2_p.h"
#include <private/qqmlengine_p.h>
#include <private/qquickutilmodule_p.h>
#include <private/qquickvaluetypes_p.h>
#include <private/qquickitemsmodule_p.h>

#include <private/qqmlenginedebugservice_p.h>
#include <private/qqmldebugstatesdelegate_p.h>
#include <private/qqmlbinding_p.h>
#include <private/qqmlcontext_p.h>
#include <private/qquickapplication_p.h>
#include <QtQuick/private/qquickpropertychanges_p.h>
#include <QtQuick/private/qquickstate_p.h>
#include <qqmlproperty.h>
#include <QtCore/QPointer>

QT_BEGIN_NAMESPACE

class QQmlQtQuick2DebugStatesDelegate : public QQmlDebugStatesDelegate
{
public:
    QQmlQtQuick2DebugStatesDelegate();
    virtual ~QQmlQtQuick2DebugStatesDelegate();
    virtual void buildStatesList(bool cleanList, const QList<QPointer<QObject> > &instances);
    virtual void updateBinding(QQmlContext *context,
                               const QQmlProperty &property,
                               const QVariant &expression, bool isLiteralValue,
                               const QString &fileName, int line, int column,
                               bool *isBaseState);
    virtual bool setBindingForInvalidProperty(QObject *object,
                                              const QString &propertyName,
                                              const QVariant &expression,
                                              bool isLiteralValue);
    virtual void resetBindingForInvalidProperty(QObject *object,
                                                const QString &propertyName);

private:
    void buildStatesList(QObject *obj);

    QList<QPointer<QQuickState> > m_allStates;
};

QQmlQtQuick2DebugStatesDelegate::QQmlQtQuick2DebugStatesDelegate()
{
}

QQmlQtQuick2DebugStatesDelegate::~QQmlQtQuick2DebugStatesDelegate()
{
}

void QQmlQtQuick2DebugStatesDelegate::buildStatesList(bool cleanList,
                                                      const QList<QPointer<QObject> > &instances)
{
    if (cleanList)
        m_allStates.clear();

    //only root context has all instances
    for (int ii = 0; ii < instances.count(); ++ii) {
        buildStatesList(instances.at(ii));
    }
}

void QQmlQtQuick2DebugStatesDelegate::buildStatesList(QObject *obj)
{
    if (QQuickState *state = qobject_cast<QQuickState *>(obj)) {
        m_allStates.append(state);
    }

    QObjectList children = obj->children();
    for (int ii = 0; ii < children.count(); ++ii) {
        buildStatesList(children.at(ii));
    }
}

void QQmlQtQuick2DebugStatesDelegate::updateBinding(QQmlContext *context,
                                                            const QQmlProperty &property,
                                                            const QVariant &expression, bool isLiteralValue,
                                                            const QString &fileName, int line, int column,
                                                            bool *inBaseState)
{
    typedef QPointer<QQuickState> QuickStatePointer;
    QObject *object = property.object();
    QString propertyName = property.name();
    foreach (const QuickStatePointer& statePointer, m_allStates) {
        if (QQuickState *state = statePointer.data()) {
            // here we assume that the revert list on itself defines the base state
            if (state->isStateActive() && state->containsPropertyInRevertList(object, propertyName)) {
                *inBaseState = false;

                QQmlBinding *newBinding = 0;
                if (!isLiteralValue) {
                    newBinding = new QQmlBinding(expression.toString(), false, object,
                                                 QQmlContextData::get(context), fileName,
                                                 line, column);
                    newBinding->setTarget(property);
                    newBinding->setNotifyOnValueChanged(true);
                }

                state->changeBindingInRevertList(object, propertyName, newBinding);

                if (isLiteralValue)
                    state->changeValueInRevertList(object, propertyName, expression);
            }
        }
    }
}

bool QQmlQtQuick2DebugStatesDelegate::setBindingForInvalidProperty(QObject *object,
                                                                           const QString &propertyName,
                                                                           const QVariant &expression,
                                                                           bool isLiteralValue)
{
    if (QQuickPropertyChanges *propertyChanges = qobject_cast<QQuickPropertyChanges *>(object)) {
        if (isLiteralValue)
            propertyChanges->changeValue(propertyName, expression);
        else
            propertyChanges->changeExpression(propertyName, expression.toString());
        return true;
    } else {
        return false;
    }
}

void QQmlQtQuick2DebugStatesDelegate::resetBindingForInvalidProperty(QObject *object, const QString &propertyName)
{
    if (QQuickPropertyChanges *propertyChanges = qobject_cast<QQuickPropertyChanges *>(object)) {
        propertyChanges->removeProperty(propertyName);
    }
}


void QQmlQtQuick2Module::defineModule()
{
    QQuickUtilModule::defineModule();
    QQmlEnginePrivate::defineModule();
    QQuickItemsModule::defineModule();

    qmlRegisterUncreatableType<QQuickApplication>("QtQuick",2,0,"Application", QQuickApplication::tr("Application is an abstract class"));

    QQuickValueTypes::registerValueTypes();

    if (QQmlEngineDebugService::isDebuggingEnabled()) {
        QQmlEngineDebugService::instance()->setStatesDelegate(
                    new QQmlQtQuick2DebugStatesDelegate);
    }
}

QT_END_NAMESPACE

