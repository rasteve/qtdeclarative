// Copyright (C) 2017 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only

import QtQuick
import QtQuick.Templates as T

T.BusyIndicator {
    id: control
    objectName: "busyindicator-simple"

    implicitWidth: Math.max(contentItem.implicitWidth, background.implicitWidth)
    implicitHeight: Math.max(contentItem.implicitHeight, background.implicitHeight)

    contentItem: Item {
        objectName: "busyindicator-contentItem-simple"
    }

    background: Rectangle {
        objectName: "busyindicator-background-simple"
        color: control.running ? "red" : "green"
    }
}
