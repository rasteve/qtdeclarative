// Copyright (C) 2017 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only

import QtQuick
import QtQuick.Templates as T

T.ScrollBar {
    id: control
    objectName: "scrollbar-incomplete"

    contentItem: Item {
        objectName: "scrollbar-contentItem-incomplete"
    }

    background: Item {
        objectName: "scrollbar-background-incomplete"
    }
}
