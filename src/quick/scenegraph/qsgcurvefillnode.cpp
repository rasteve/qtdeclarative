// Copyright (C) 2023 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#include "qsgcurvefillnode_p.h"
#include "qsgcurvefillnode_p_p.h"

QT_BEGIN_NAMESPACE

QSGCurveFillNode::QSGCurveFillNode()
{
    setFlag(OwnsGeometry, true);
    setFlag(UsePreprocess, true);
    setGeometry(new QSGGeometry(attributes(), 0, 0));

    updateMaterial();
}

void QSGCurveFillNode::updateMaterial()
{
    m_material.reset(new QSGCurveFillMaterial(this));
    setMaterial(m_material.data());
}

void QSGCurveFillNode::cookGeometry()
{
    QSGGeometry *g = geometry();
    if (g->indexType() != QSGGeometry::UnsignedIntType) {
        g = new QSGGeometry(attributes(),
                            m_uncookedVertexes.size(),
                            m_uncookedIndexes.size(),
                            QSGGeometry::UnsignedIntType);
        setGeometry(g);
    } else {
        g->allocate(m_uncookedVertexes.size(), m_uncookedIndexes.size());
    }

    g->setDrawingMode(QSGGeometry::DrawTriangles);
    memcpy(g->vertexData(),
           m_uncookedVertexes.constData(),
           g->vertexCount() * g->sizeOfVertex());
    memcpy(g->indexData(),
           m_uncookedIndexes.constData(),
           g->indexCount() * g->sizeOfIndex());

    m_uncookedIndexes.clear();
    m_uncookedIndexes.squeeze();
    m_uncookedVertexes.clear();
    m_uncookedVertexes.squeeze();
}

const QSGGeometry::AttributeSet &QSGCurveFillNode::attributes()
{
    static QSGGeometry::Attribute data[] = {
        QSGGeometry::Attribute::createWithAttributeType(0, 2, QSGGeometry::FloatType, QSGGeometry::PositionAttribute),
        QSGGeometry::Attribute::createWithAttributeType(1, 3, QSGGeometry::FloatType, QSGGeometry::TexCoordAttribute),
        QSGGeometry::Attribute::createWithAttributeType(2, 4, QSGGeometry::FloatType, QSGGeometry::UnknownAttribute),
        QSGGeometry::Attribute::createWithAttributeType(3, 2, QSGGeometry::FloatType, QSGGeometry::UnknownAttribute),
    };
    static QSGGeometry::AttributeSet attrs = { 4, sizeof(CurveNodeVertex), data };
    return attrs;
}

QT_END_NAMESPACE

#include "moc_qsgcurvefillnode_p.cpp"
