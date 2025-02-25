// Copyright (C) 2020 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#include "qquickwheelhandler_p.h"
#include "qquickwheelhandler_p_p.h"
#include <QtQuick/private/qquickitem_p.h>
#include <QLoggingCategory>
#include <QtMath>

QT_BEGIN_NAMESPACE

Q_STATIC_LOGGING_CATEGORY(lcWheelHandler, "qt.quick.handler.wheel")

/*!
    \qmltype WheelHandler
    \nativetype QQuickWheelHandler
    \inherits SinglePointHandler
    \inqmlmodule QtQuick
    \ingroup qtquick-input-handlers
    \brief Handler for the mouse wheel.

    WheelHandler is a handler that is used to interactively manipulate some
    numeric property of an Item as the user rotates the mouse wheel. Like other
    Input Handlers, by default it manipulates its \l {PointerHandler::target}
    {target}.  Declare \l property to control which target property will be
    manipulated:

    \snippet pointerHandlers/wheelHandler.qml 0

    \l BoundaryRule is quite useful in combination with WheelHandler (as well
    as with other Input Handlers) to declare the allowed range of values that
    the target property can have. For example it is possible to implement
    scrolling using a combination of WheelHandler and \l DragHandler to
    manipulate the scrollable Item's \l{QQuickItem::y}{y} property when the
    user rotates the wheel or drags the item on a touchscreen, and
    \l BoundaryRule to limit the range of motion from the top to the bottom:

    \snippet pointerHandlers/handlerFlick.qml 0

    Alternatively, if \l property is not set or \l target is null,
    WheelHandler will not automatically manipulate anything; but the
    \l rotation property can be used in a binding to manipulate another
    property, or you can implement \c onWheel and handle the wheel event
    directly.

    WheelHandler handles only a rotating mouse wheel by default; this
    can be changed by setting acceptedDevices.

    \sa MouseArea, Flickable, {Qt Quick Examples - Pointer Handlers}
*/

QQuickWheelHandler::QQuickWheelHandler(QQuickItem *parent)
    : QQuickSinglePointHandler(*(new QQuickWheelHandlerPrivate), parent)
{
    setAcceptedDevices(QInputDevice::DeviceType::Mouse);
}

/*!
    \qmlproperty enumeration QtQuick::WheelHandler::orientation

    Which wheel to react to.  The default is \c Qt.Vertical.

    Not every mouse has a \c Horizontal wheel; sometimes it is emulated by
    tilting the wheel sideways. A touchpad can usually generate both vertical
    and horizontal wheel events.
*/
Qt::Orientation QQuickWheelHandler::orientation() const
{
    Q_D(const QQuickWheelHandler);
    return d->orientation;
}

void QQuickWheelHandler::setOrientation(Qt::Orientation orientation)
{
    Q_D(QQuickWheelHandler);
    if (d->orientation == orientation)
        return;

    d->orientation = orientation;
    emit orientationChanged();
}

/*!
    \qmlproperty bool QtQuick::WheelHandler::invertible

    Whether or not to reverse the direction of property change if
    \l QWheelEvent::inverted is \c true. The default is \c true.

    If the operating system has a "natural scrolling" setting that causes
    scrolling to be in the same direction as the finger movement, then if this
    property is set to \c true, and WheelHandler is directly setting a property
    on \l target, the direction of movement will correspond to the system setting.
    If this property is set to \c false, it will invert the \l rotation so that
    the direction of motion is always the same as the direction of finger movement.
*/
bool QQuickWheelHandler::isInvertible() const
{
    Q_D(const QQuickWheelHandler);
    return d->invertible;
}

void QQuickWheelHandler::setInvertible(bool invertible)
{
    Q_D(QQuickWheelHandler);
    if (d->invertible == invertible)
        return;

    d->invertible = invertible;
    emit invertibleChanged();
}

/*!
    \qmlproperty real QtQuick::WheelHandler::activeTimeout

    The amount of time in seconds after which the \l active property will
    revert to \c false if no more wheel events are received. The default is
    \c 0.1 (100 ms).

    When WheelHandler handles events that contain
    \l {Qt::ScrollPhase}{scroll phase} information, such as events from some
    touchpads, the \l active property will become \c false as soon as an event
    with phase \l Qt::ScrollEnd is received; in that case the timeout is not
    necessary. But a conventional mouse with a wheel does not provide a scroll
    phase: the mouse cannot detect when the user has decided to stop
    scrolling, so the \l active property transitions to \c false after this
    much time has elapsed.

    \sa QWheelEvent::phase()
*/
qreal QQuickWheelHandler::activeTimeout() const
{
    Q_D(const QQuickWheelHandler);
    return d->activeTimeout;
}

void QQuickWheelHandler::setActiveTimeout(qreal timeout)
{
    Q_D(QQuickWheelHandler);
    if (qFuzzyCompare(d->activeTimeout, timeout))
        return;

    if (timeout < 0) {
        qWarning("activeTimeout must be positive");
        return;
    }

    d->activeTimeout = timeout;
    emit activeTimeoutChanged();
}

/*!
    \qmlproperty real QtQuick::WheelHandler::rotation

    The angle through which the mouse wheel has been rotated since the last
    time this property was set, in wheel degrees.

    A positive value indicates that the wheel was rotated up/right;
    a negative value indicates that the wheel was rotated down/left.

    A basic mouse click-wheel works in steps of 15 degrees.

    The default is \c 0 at startup. It can be programmatically set to any value
    at any time. The value will be adjusted from there as the user rotates the
    mouse wheel.

    \sa orientation
*/
qreal QQuickWheelHandler::rotation() const
{
    Q_D(const QQuickWheelHandler);
    return d->rotation * d->rotationScale;
}

void QQuickWheelHandler::setRotation(qreal rotation)
{
    Q_D(QQuickWheelHandler);
    if (qFuzzyCompare(d->rotation, rotation / d->rotationScale))
        return;

    d->rotation = rotation / d->rotationScale;
    emit rotationChanged();
}

/*!
    \qmlproperty real QtQuick::WheelHandler::rotationScale

    The scaling to be applied to the \l rotation property, and to the
    \l property on the \l target item, if any. The default is 1, such that
    \l rotation will be in units of degrees of rotation. It can be set to a
    negative number to invert the effect of the direction of mouse wheel
    rotation.
*/
qreal QQuickWheelHandler::rotationScale() const
{
    Q_D(const QQuickWheelHandler);
    return d->rotationScale;
}

void QQuickWheelHandler::setRotationScale(qreal rotationScale)
{
    Q_D(QQuickWheelHandler);
    if (qFuzzyCompare(d->rotationScale, rotationScale))
        return;
    if (qFuzzyIsNull(rotationScale)) {
        qWarning("rotationScale cannot be set to zero");
        return;
    }

    d->rotationScale = rotationScale;
    emit rotationScaleChanged();
}

/*!
    \qmlproperty string QtQuick::WheelHandler::property

    The property to be modified on the \l target when the mouse wheel is rotated.

    The default is no property (empty string). When no target property is being
    automatically modified, you can use bindings to react to mouse wheel
    rotation in arbitrary ways.

    You can use the mouse wheel to adjust any numeric property.  For example if
    \c property is set to \c x, the \l target will move horizontally as the
    wheel is rotated.  The following properties have special behavior:

    \value scale
        \l{QQuickItem::scale}{scale} will be modified in a non-linear fashion
        as described under \l targetScaleMultiplier.  If
        \l targetTransformAroundCursor is \c true, the \l{QQuickItem::x}{x} and
        \l{QQuickItem::y}{y} properties will be simultaneously adjusted so that
        the user will effectively zoom into or out of the point under the mouse
        cursor.
    \value rotation
        \l{QQuickItem::rotation}{rotation} will be set to \l rotation.  If
        \l targetTransformAroundCursor is \c true, the l{QQuickItem::x}{x} and
        \l{QQuickItem::y}{y} properties will be simultaneously adjusted so
        that the user will effectively rotate the item around the point under
        the mouse cursor.

    The adjustment of the given target property is always scaled by \l rotationScale.
*/
QString QQuickWheelHandler::property() const
{
    Q_D(const QQuickWheelHandler);
    return d->propertyName;
}

void QQuickWheelHandler::setProperty(const QString &propertyName)
{
    Q_D(QQuickWheelHandler);
    if (d->propertyName == propertyName)
        return;

    d->propertyName = propertyName;
    d->metaPropertyDirty = true;
    emit propertyChanged();
}

/*!
    \qmlproperty real QtQuick::WheelHandler::targetScaleMultiplier

    The amount by which the \l target \l{QQuickItem::scale}{scale} is to be
    multiplied whenever the \l rotation changes by 15 degrees.  This
    is relevant only when \l property is \c "scale".

    The \c scale will be multiplied by
    \c targetScaleMultiplier \sup {angleDelta * rotationScale / 15}.
    The default is \c 2 \sup {1/3}, which means that if \l rotationScale is left
    at its default value, and the mouse wheel is rotated by one "click"
    (15 degrees), the \l target will be scaled by approximately 1.25; after
    three "clicks" its size will be doubled or halved, depending on the
    direction that the wheel is rotated. If you want to make it double or halve
    with every 2 clicks of the wheel, set this to \c 2 \sup {1/2} (1.4142).
    If you want to make it scale the opposite way as the wheel is rotated,
    set \c rotationScale to a negative value.
*/
qreal QQuickWheelHandler::targetScaleMultiplier() const
{
    Q_D(const QQuickWheelHandler);
    return d->targetScaleMultiplier;
}

void QQuickWheelHandler::setTargetScaleMultiplier(qreal targetScaleMultiplier)
{
    Q_D(QQuickWheelHandler);
    if (qFuzzyCompare(d->targetScaleMultiplier, targetScaleMultiplier))
        return;

    d->targetScaleMultiplier = targetScaleMultiplier;
    emit targetScaleMultiplierChanged();
}

/*!
    \qmlproperty bool QtQuick::WheelHandler::targetTransformAroundCursor

    Whether the \l target should automatically be repositioned in such a way
    that it is transformed around the mouse cursor position while the
    \l property is adjusted.  The default is \c true.

    If \l property is set to \c "rotation" and \l targetTransformAroundCursor
    is \c true, then as the wheel is rotated, the \l target item will rotate in
    place around the mouse cursor position. If \c targetTransformAroundCursor
    is \c false, it will rotate around its
    \l{QQuickItem::transformOrigin}{transformOrigin} instead.
*/
bool QQuickWheelHandler::isTargetTransformAroundCursor() const
{
    Q_D(const QQuickWheelHandler);
    return d->targetTransformAroundCursor;
}

void QQuickWheelHandler::setTargetTransformAroundCursor(bool ttac)
{
    Q_D(QQuickWheelHandler);
    if (d->targetTransformAroundCursor == ttac)
        return;

    d->targetTransformAroundCursor = ttac;
    emit targetTransformAroundCursorChanged();
}

/*!
    \qmlproperty bool QtQuick::WheelHandler::blocking
    \since 6.3

    Whether this handler prevents other items or handlers behind it from
    handling the same wheel event. This property is \c true by default.
*/
bool QQuickWheelHandler::isBlocking() const
{
    Q_D(const QQuickWheelHandler);
    return d->blocking;
}

void QQuickWheelHandler::setBlocking(bool blocking)
{
    Q_D(QQuickWheelHandler);
    if (d->blocking == blocking)
        return;

    d->blocking = blocking;
    emit blockingChanged();
}

bool QQuickWheelHandler::wantsPointerEvent(QPointerEvent *event)
{
    if (!event)
        return false;
    if (event->type() != QEvent::Wheel)
        return false;
    QWheelEvent *we = static_cast<QWheelEvent *>(event);
    if (!acceptedDevices().testFlag(QPointingDevice::DeviceType::TouchPad)
            && we->source() != Qt::MouseEventNotSynthesized)
        return false;
    if (!active()) {
        switch (orientation()) {
        case Qt::Horizontal:
            if (!(we->angleDelta().x()) && !(we->pixelDelta().x()))
                return false;
            break;
        case Qt::Vertical:
            if (!(we->angleDelta().y()) && !(we->pixelDelta().y()))
                return false;
            break;
        }
    }
    auto &point = event->point(0);
    if (QQuickPointerDeviceHandler::wantsPointerEvent(event) && wantsEventPoint(event, point) && parentContains(point)) {
        setPointId(point.id());
        return true;
    }
    return false;
}

void QQuickWheelHandler::handleEventPoint(QPointerEvent *ev, QEventPoint &point)
{
    Q_D(QQuickWheelHandler);
    QQuickSinglePointHandler::handleEventPoint(ev, point);

    if (ev->type() != QEvent::Wheel)
        return;
    const QWheelEvent *event = static_cast<const QWheelEvent *>(ev);
    setActive(true); // ScrollEnd will not happen unless it was already active (see setActive(false) below)
    if (d->blocking)
        point.setAccepted();
    qreal inversion = !d->invertible && event->isInverted() ? -1 : 1;
    qreal angleDelta = inversion * qreal(orientation() == Qt::Horizontal ? event->angleDelta().x() :
                                                                           event->angleDelta().y()) / 8;
    d->rotation += angleDelta;
    emit rotationChanged();

    d->wheelEvent.reset(event);
    emit wheel(&d->wheelEvent);
    if (!d->propertyName.isEmpty() && target()) {
        QQuickItem *t = target();
        // writing target()'s property is done via QMetaProperty::write() so that any registered interceptors can react.
        if (d->propertyName == QLatin1String("scale")) {
            qreal multiplier = qPow(d->targetScaleMultiplier, angleDelta * d->rotationScale / 15); // wheel "clicks"
            const QPointF centroidParentPos = t->parentItem()->mapFromScene(point.scenePosition());
            const QPointF positionWas = t->position();
            const qreal scaleWas = t->scale();
            const qreal activePropertyValue = scaleWas * multiplier;
            qCDebug(lcWheelHandler) << objectName() << "angle delta" << event->angleDelta() << "pixel delta" << event->pixelDelta()
                                    << "@" << point.position() << "in parent" << centroidParentPos
                                    << "in scene" << point.scenePosition()
                                    << "multiplier" << multiplier << "scale" << scaleWas
                                    << "->" << activePropertyValue;
            d->targetMetaProperty().write(t, activePropertyValue);
            if (d->targetTransformAroundCursor) {
                // If an interceptor intervened, scale may now be different than we asked for.  Adjust accordingly.
                multiplier = t->scale() / scaleWas;
                const QPointF adjPos = QQuickItemPrivate::get(t)->adjustedPosForTransform(
                            centroidParentPos, positionWas, QVector2D(), scaleWas, multiplier, t->rotation(), 0);
                qCDebug(lcWheelHandler) << "adjusting item pos" << adjPos << "in scene" << t->parentItem()->mapToScene(adjPos);
                t->setPosition(adjPos);
            }
        } else if (d->propertyName == QLatin1String("rotation")) {
            const QPointF positionWas = t->position();
            const qreal rotationWas = t->rotation();
            const qreal activePropertyValue = rotationWas + angleDelta * d->rotationScale;
            const QPointF centroidParentPos = t->parentItem()->mapFromScene(point.scenePosition());
            qCDebug(lcWheelHandler) << objectName() << "angle delta" << event->angleDelta() << "pixel delta" << event->pixelDelta()
                                    << "@" << point.position() << "in parent" << centroidParentPos
                                    << "in scene" << point.scenePosition() << "rotation" << t->rotation()
                                    << "->" << activePropertyValue;
            d->targetMetaProperty().write(t, activePropertyValue);
            if (d->targetTransformAroundCursor) {
                // If an interceptor intervened, rotation may now be different than we asked for.  Adjust accordingly.
                const QPointF adjPos = QQuickItemPrivate::get(t)->adjustedPosForTransform(
                            centroidParentPos, positionWas, QVector2D(),
                            t->scale(), 1, rotationWas, t->rotation() - rotationWas);
                qCDebug(lcWheelHandler) << "adjusting item pos" << adjPos << "in scene" << t->parentItem()->mapToScene(adjPos);
                t->setPosition(adjPos);
            }
        } else {
            qCDebug(lcWheelHandler) << objectName() << "angle delta" << event->angleDelta() << "scaled" << angleDelta
                                    << "total" << d->rotation << "pixel delta" << event->pixelDelta()
                                    << "@" << point.position() << "in scene" << point.scenePosition() << "rotation" << t->rotation();
            qreal delta = 0;
            if (event->hasPixelDelta()) {
                delta = inversion * d->rotationScale * qreal(orientation() == Qt::Horizontal ? event->pixelDelta().x() : event->pixelDelta().y());
                qCDebug(lcWheelHandler) << "changing target" << d->propertyName << "by pixel delta" << delta << "from" << event;
            } else {
                delta = angleDelta * d->rotationScale;
                qCDebug(lcWheelHandler) << "changing target" << d->propertyName << "by scaled angle delta" << delta << "from" << event;
            }
            bool ok = false;
            qreal value = d->targetMetaProperty().read(t).toReal(&ok);
            if (ok)
                d->targetMetaProperty().write(t, value + qreal(delta));
            else
                qWarning() << "failed to read property" << d->propertyName << "of" << t;
        }
    }
    switch (event->phase()) {
    case Qt::ScrollEnd:
        qCDebug(lcWheelHandler) << objectName() << "deactivating due to ScrollEnd phase";
        setActive(false);
        break;
    case Qt::NoScrollPhase:
        d->deactivationTimer.start(qRound(d->activeTimeout * 1000), this);
        break;
    case Qt::ScrollBegin:
    case Qt::ScrollUpdate:
    case Qt::ScrollMomentum:
        break;
    }
}

void QQuickWheelHandler::onTargetChanged(QQuickItem *oldTarget)
{
    Q_UNUSED(oldTarget);
    Q_D(QQuickWheelHandler);
    d->metaPropertyDirty = true;
}

void QQuickWheelHandler::onActiveChanged()
{
    Q_D(QQuickWheelHandler);
    if (!active())
        d->deactivationTimer.stop();
}

void QQuickWheelHandler::timerEvent(QTimerEvent *event)
{
    Q_D(const QQuickWheelHandler);
    if (event->timerId() == d->deactivationTimer.timerId()) {
        qCDebug(lcWheelHandler) << objectName() << "deactivating due to timeout";
        setActive(false);
    }
}

/*!
    \qmlsignal QtQuick::WheelHandler::wheel(WheelEvent event)

    This signal is emitted every time this handler receives an \a event
    of type \l QWheelEvent: that is, every time the wheel is moved or the
    scrolling gesture is updated.
*/

QQuickWheelHandlerPrivate::QQuickWheelHandlerPrivate()
    : QQuickSinglePointHandlerPrivate()
{
}

QMetaProperty &QQuickWheelHandlerPrivate::targetMetaProperty() const
{
    Q_Q(const QQuickWheelHandler);
    if (metaPropertyDirty && q->target()) {
        if (!propertyName.isEmpty()) {
            const QMetaObject *targetMeta = q->target()->metaObject();
            metaProperty = targetMeta->property(
                        targetMeta->indexOfProperty(propertyName.toLocal8Bit().constData()));
        }
        metaPropertyDirty = false;
    }
    return metaProperty;
}

/*! \internal
    \qmlproperty flags QtQuick::WheelHandler::acceptedButtons

    This overrides QtQuick::PointerDeviceHandler::acceptedButtons
    and hides it from the documentation as the property is not relevant for
    WheelHandler.
*/

/*!
    \qmlproperty flags QtQuick::WheelHandler::acceptedDevices

    The types of pointing devices that can activate this handler.

    By default, this property is set to
    \l{QInputDevice::DeviceType}{PointerDevice.Mouse}, so as to react only to
    events from an actual mouse wheel.

    WheelHandler can be made to respond to both mouse wheel and touchpad
    scrolling by setting acceptedDevices to
    \c{PointerDevice.Mouse | PointerDevice.TouchPad}.

    \note Some non-mouse hardware (such as a touch-sensitive Wacom tablet, or a
    Linux laptop touchpad) generates real wheel events from gestures.
    WheelHandler will respond to those events as wheel events even if
    \c acceptedDevices remains set to its default value.
*/

/*!
    \qmlproperty flags QtQuick::WheelHandler::acceptedModifiers

    If this property is set, it will require the given keyboard modifiers to
    be pressed in order to react to wheel events, and otherwise ignore them.

    If this property is set to \c Qt.KeyboardModifierMask (the default value),
    the WheelHandler ignores the modifier keys.

    For example, an \l [QML] Item could have two handlers, one of which is
    enabled only if the required keyboard modifier is pressed, while the other
    ignores events if any modifier is pressed:

    \snippet pointerHandlers/wheelHandlerAcceptedModifiers.qml entire

    The available modifiers are as follows:

    \value NoModifier       No modifier key is allowed.
    \value ShiftModifier    A Shift key on the keyboard must be pressed.
    \value ControlModifier  A Ctrl key on the keyboard must be pressed.
    \value AltModifier      An Alt key on the keyboard must be pressed.
    \value MetaModifier     A Meta key on the keyboard must be pressed.
    \value KeypadModifier   A keypad button must be pressed.
    \value GroupSwitchModifier X11 only (unless activated on Windows by a command line argument).
                            A Mode_switch key on the keyboard must be pressed.
    \value KeyboardModifierMask The handler does not care which modifiers are pressed.

    \sa Qt::KeyboardModifier
*/

/*! \internal
    \qmlproperty flags QtQuick::WheelHandler::acceptedPointerTypes

    This overrides QtQuick::PointerDeviceHandler::acceptedPointerTypes
    and hides it from the documentation as the property is not relevant for
    WheelHandler.
*/

/*!
    \readonly
    \qmlproperty bool QtQuick::WheelHandler::active

    This holds \c true whenever the WheelHandler has recently seen a
    QWheelEvent, is keeping its properties up-to-date, and actively manipulating
    its \l target (if any).

    \sa activeTimeout
*/

/*! \internal
    \qmlproperty flags QtQuick::WheelHandler::cursorShape

    This overrides QtQuick::PointerHandler::cursorShape
    and hides it from the documentation as the property is not relevant for
    WheelHandler.
*/

/*! \internal
    \qmlproperty flags QtQuick::WheelHandler::dragThreshold

    This overrides QtQuick::PointerHandler::dragThreshold
    and hides it from the documentation as the property is not relevant for
    WheelHandler.
*/

/*! \internal
    \qmlproperty flags QtQuick::WheelHandler::grabPermissions

    This overrides QtQuick::PointerHandler::grabPermissions
    and hides it from the documentation as the property is not relevant for
    WheelHandler.
*/

/*!
    \qmlproperty real QtQuick::WheelHandler::margin

    The margin beyond the bounds of the \l {PointerHandler::parent}{parent}
    item within which the WheelHandler can react. For example if \c margin
    is set to \c 10, you could place the cursor up to 10 pixels outside the
    visible edge of the item, and it will still react to the wheel:

    \snippet pointerHandlers/wheelHandlerMargin.qml entire

    The default value is \c 0.
*/

/*! \internal
    \qmlsignal QtQuick::WheelHandler::grabChanged(PointerDevice::GrabTransition transition, eventPoint point)

    This overrides QtQuick::PointerHandler::grabChanged
    and hides it from the documentation as the signal is not relevant for
    WheelHandler.
*/

/*! \internal
    \qmlsignal QtQuick::WheelHandler::canceled(eventPoint point)

    This overrides QtQuick::PointerHandler::canceled
    and hides it from the documentation as the signal is not relevant for
    WheelHandler.
*/

QT_END_NAMESPACE

#include "moc_qquickwheelhandler_p.cpp"
