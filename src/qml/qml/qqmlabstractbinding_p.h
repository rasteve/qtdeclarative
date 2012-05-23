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

#ifndef QQMLABSTRACTBINDING_P_H
#define QQMLABSTRACTBINDING_P_H

//
//  W A R N I N G
//  -------------
//
// This file is not part of the Qt API.  It exists purely as an
// implementation detail.  This header file may change from version to
// version without notice, or even be removed.
//
// We mean it.
//

#include <QtCore/qsharedpointer.h>
#include <private/qtqmlglobal_p.h>
#include <private/qqmlproperty_p.h>
#include <private/qpointervaluepair_p.h>

QT_BEGIN_NAMESPACE

class Q_QML_PRIVATE_EXPORT QQmlAbstractBinding
{
public:
    struct VTable {
        void (*destroy)(QQmlAbstractBinding *);
        QString (*expression)(const QQmlAbstractBinding *);
        int (*propertyIndex)(const QQmlAbstractBinding *);
        QObject *(*object)(const QQmlAbstractBinding *);
        void (*setEnabled)(QQmlAbstractBinding *, bool, QQmlPropertyPrivate::WriteFlags);
        void (*update)(QQmlAbstractBinding *, QQmlPropertyPrivate::WriteFlags);
        void (*retargetBinding)(QQmlAbstractBinding *, QObject *, int);
    };

    typedef QWeakPointer<QQmlAbstractBinding> Pointer;

    enum BindingType { Binding = 0, V4 = 1, V8 = 2, ValueTypeProxy = 3 };
    inline BindingType bindingType() const;

    // Destroy the binding.  Use this instead of calling delete.
    // Bindings are free to implement their own memory management, so the delete operator is
    // not necessarily safe.  The default implementation clears the binding, removes it from
    // the object and calls delete.
    void destroy() { vtable()->destroy(this); }
    QString expression() const { return vtable()->expression(this); }

    // Should return the encoded property index for the binding.  Should return this value
    // even if the binding is not enabled or added to an object.
    // Encoding is:  coreIndex | (valueTypeIndex << 24)
    int propertyIndex() const { return vtable()->propertyIndex(this); }
    // Should return the object for the binding.  Should return this object even if the
    // binding is not enabled or added to the object.
    QObject *object() const { return vtable()->object(this); }

    void setEnabled(bool e) { setEnabled(e, QQmlPropertyPrivate::DontRemoveBinding); }
    void setEnabled(bool e, QQmlPropertyPrivate::WriteFlags f) { vtable()->setEnabled(this, e, f); }

    void update() { update(QQmlPropertyPrivate::DontRemoveBinding); }
    void update(QQmlPropertyPrivate::WriteFlags f) { vtable()->update(this, f); }

    void addToObject();
    void removeFromObject();

    static inline Pointer getPointer(QQmlAbstractBinding *p);
    static void printBindingLoopError(QQmlProperty &prop);

    // Default implementation for some VTable functions
    template<typename T>
    static void default_destroy(QQmlAbstractBinding *);
    static QString default_expression(const QQmlAbstractBinding *);
    static void default_retargetBinding(QQmlAbstractBinding *, QObject *, int);

protected:
    QQmlAbstractBinding(BindingType);
    ~QQmlAbstractBinding();
    void clear();

    // Called by QQmlPropertyPrivate to "move" a binding to a different property.
    // This is only used for alias properties. The default implementation qFatal()'s
    // to ensure that the method is never called for binding types that don't support it.
    void retargetBinding(QObject *o, int i) { vtable()->retargetBinding(this, o, i); }

private:
    Pointer weakPointer();

    friend class QQmlData;
    friend class QQmlComponentPrivate;
    friend class QQmlValueTypeProxyBinding;
    friend class QQmlPropertyPrivate;
    friend class QQmlVME;
    friend class QtSharedPointer::ExternalRefCount<QQmlAbstractBinding>;

    typedef QSharedPointer<QQmlAbstractBinding> SharedPointer;
    // To save memory, we also store the rarely used weakPointer() instance in here
    // We also use the flag bits:
    //    m_mePtr.flag1: added to object
    QPointerValuePair<QQmlAbstractBinding*, SharedPointer> m_mePtr;

    inline void setAddedToObject(bool v);
    inline bool isAddedToObject() const;

    inline QQmlAbstractBinding *nextBinding() const;
    inline void setNextBinding(QQmlAbstractBinding *);

    uintptr_t m_nextBindingPtr;

    static VTable *vTables[];
    inline const VTable *vtable() const { return vTables[bindingType()]; }
};

QQmlAbstractBinding::Pointer
QQmlAbstractBinding::getPointer(QQmlAbstractBinding *p)
{
    return p ? p->weakPointer() : Pointer();
}

void QQmlAbstractBinding::setAddedToObject(bool v)
{
    m_mePtr.setFlagValue(v);
}

bool QQmlAbstractBinding::isAddedToObject() const
{
    return m_mePtr.flag();
}

QQmlAbstractBinding *QQmlAbstractBinding::nextBinding() const
{
    return (QQmlAbstractBinding *)(m_nextBindingPtr & ~0x3);
}

void QQmlAbstractBinding::setNextBinding(QQmlAbstractBinding *b)
{
    m_nextBindingPtr = uintptr_t(b) | (m_nextBindingPtr & 0x3);
}

QQmlAbstractBinding::BindingType QQmlAbstractBinding::bindingType() const
{
    return (BindingType)(m_nextBindingPtr & 0x3);
}

template<typename T>
void QQmlAbstractBinding::default_destroy(QQmlAbstractBinding *This)
{
    This->removeFromObject();
    This->clear();
    delete static_cast<T *>(This);
}

QT_END_NAMESPACE

#endif // QQMLABSTRACTBINDING_P_H
