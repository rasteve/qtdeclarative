// Copyright (C) 2018 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only

import QtQuick 2.12
import QtQuick.Window 2.3
import QtQml.Models

Item {
    width: 640
    height: 450

    ListView {
        width: 600
        height: 400
        model: 2
        delegate: DelegateChooser {
            DelegateChoice {
                index: 0
                delegate: Rectangle {
                    width: 100
                    height: 100
                    color:"green"
                }
            }
        }
    }
}
