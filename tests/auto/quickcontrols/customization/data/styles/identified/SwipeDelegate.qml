// Copyright (C) 2017 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only

import QtQuick
import QtQuick.Templates as T

T.SwipeDelegate {
    id: control
    objectName: "swipedelegate-identified"

    contentItem: Item {
        id: contentItem
        objectName: "swipedelegate-contentItem-identified"
    }

    background: Item {
        id: background
        objectName: "swipedelegate-background-identified"
    }
}
