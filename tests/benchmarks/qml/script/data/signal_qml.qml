// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only

import Qt.test 1.0

TestObject {
    onMySignal: { var a = 1; return a; }
}

