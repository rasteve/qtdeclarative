/****************************************************************************
**
** Copyright (C) 2021 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of the tools applications of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:GPL-EXCEPT$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3 as published by the Free Software
** Foundation with exceptions as appearing in the file LICENSE.GPL3-EXCEPT
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-3.0.html.
**
** $QT_END_LICENSE$
**
****************************************************************************/

#ifndef QQMLJSSTORAGEGENERALIZER_P_H
#define QQMLJSSTORAGEGENERALIZER_P_H

//
//  W A R N I N G
//  -------------
//
// This file is not part of the Qt API.  It exists purely as an
// implementation detail.  This header file may change from version to
// version without notice, or even be removed.
//
// We mean it.

#include <private/qqmljscompilepass_p.h>

QT_BEGIN_NAMESPACE

class Q_QMLCOMPILER_PRIVATE_EXPORT QQmlJSStorageGeneralizer : public QQmlJSCompilePass
{
public:
    QQmlJSStorageGeneralizer(const QV4::Compiler::JSUnitGenerator *jsUnitGenerator,
                             const QQmlJSTypeResolver *typeResolver, QQmlJSLogger *logger)
        : QQmlJSCompilePass(jsUnitGenerator, typeResolver, logger)
    {}

    InstructionAnnotations run(InstructionAnnotations annotations, Function *function,
                               QQmlJS::DiagnosticMessage *error);

protected:
    // We don't have to use the byte code here. We only transform the instruction annotations.
    Verdict startInstruction(QV4::Moth::Instr::Type) override { return SkipInstruction; }
    void endInstruction(QV4::Moth::Instr::Type) override {}
};

QT_END_NAMESPACE

#endif // QQMLJSSTORAGEGENERALIZER_P_H
