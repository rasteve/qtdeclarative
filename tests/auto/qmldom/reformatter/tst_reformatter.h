// Copyright (C) 2021 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only

#ifndef TST_QMLDOMCODEFORMATTER_H
#define TST_QMLDOMCODEFORMATTER_H
#include <QtQmlDom/private/qqmldomlinewriter_p.h>
#include <QtQmlDom/private/qqmldomindentinglinewriter_p.h>
#include <QtQmlDom/private/qqmldomoutwriter_p.h>
#include <QtQmlDom/private/qqmldomitem_p.h>
#include <QtQmlDom/private/qqmldomtop_p.h>
#include <QtQmlDom/private/qqmldomreformatter_p.h>

#include <QtTest/QtTest>
#include <QCborValue>
#include <QDebug>
#include <QLibraryInfo>

#include <memory>

QT_BEGIN_NAMESPACE
namespace QQmlJS {
namespace Dom {

class TestReformatter : public QObject
{
    Q_OBJECT
public:
private:
    QString formatJSCode(const QString &jsCode)
    {
        return formatPlainJS(jsCode, ScriptExpression::ExpressionType::JSCode);
    }

    QString formatJSModuleCode(const QString &jsCode)
    {
        return formatPlainJS(jsCode, ScriptExpression::ExpressionType::MJSCode);
    }

    // "Unix" LineWriter (with '\n' line endings) is used by default,
    // under the assumption that line endings are properly tested in lineWriter() test.
    QString formatPlainJS(const QString &jsCode, ScriptExpression::ExpressionType exprType)
    {
        QString resultStr;
        QTextStream res(&resultStr);
        LineWriterOptions opts;
        opts.lineEndings = LineWriterOptions::LineEndings::Unix;
        LineWriter lw([&res](QStringView s) { res << s; }, QLatin1String("*testStream*"), opts);
        OutWriter ow(lw);

        const ScriptExpression scriptItem(jsCode, exprType);
        scriptItem.writeOut(DomItem(), ow);

        lw.flush(); // flush instead of eof to protect traling spaces
        res.flush();
        return resultStr;
    }

private slots:
    void reindent_data()
    {
        QTest::addColumn<QString>("inFile");
        QTest::addColumn<QString>("outFile");

        QTest::newRow("file1") << QStringLiteral(u"file1.qml") << QStringLiteral(u"file1.qml");
        QTest::newRow("file1 unindented")
                << QStringLiteral(u"file1Unindented.qml") << QStringLiteral(u"file1.qml");
    }

    void reindent()
    {
        QFETCH(QString, inFile);
        QFETCH(QString, outFile);

        QFile fIn(QLatin1String(QT_QMLTEST_DATADIR) + QLatin1String("/reformatter/") + inFile);
        if (!fIn.open(QIODevice::ReadOnly | QIODevice::Text)) {
            qWarning() << "could not open file" << inFile;
            return;
        }
        QFile fOut(QLatin1String(QT_QMLTEST_DATADIR) + QLatin1String("/reformatter/") + outFile);
        if (!fOut.open(QIODevice::ReadOnly | QIODevice::Text)) {
            qWarning() << "could not open file" << outFile;
            return;
        }
        QTextStream in(&fIn);
        QTextStream out(&fOut);
        QString resultStr;
        QTextStream res(&resultStr);
        QString line = in.readLine();
        IndentingLineWriter lw([&res](QStringView s) { res << s; }, QLatin1String("*testStream*"));
        QList<SourceLocation *> sourceLocations;
        while (!line.isNull()) {
            SourceLocation *loc = new SourceLocation;
            sourceLocations.append(loc);
            lw.write(line, loc);
            lw.write(u"\n");
            line = in.readLine();
        }
        lw.eof();
        res.flush();
        QString fullRes = resultStr;
        res.seek(0);
        line = out.readLine();
        QString resLine = res.readLine();
        int iLoc = 0;
        int nextLoc = 0;
        while (!line.isNull() && !resLine.isNull()) {
            QCOMPARE(resLine, line);
            if (iLoc == nextLoc && iLoc < sourceLocations.size()) {
                QString l2 =
                        fullRes.mid(sourceLocations[iLoc]->offset, sourceLocations[iLoc]->length);
                if (!l2.contains(QLatin1Char('\n'))) {
                    QCOMPARE(l2, line);
                } else {
                    qDebug() << "skip checks of multiline location (line was split)" << l2;
                    iLoc -= l2.count(QLatin1Char('\n'));
                }
                ++nextLoc;
            } else {
                qDebug() << "skip multiline recover";
            }
            ++iLoc;
            line = out.readLine();
            resLine = res.readLine();
        }
        QCOMPARE(resLine.isNull(), line.isNull());
        for (auto sLoc : sourceLocations)
            delete sLoc;
    }

    void lineByLineReformatter_data()
    {
        QTest::addColumn<QString>("inFile");
        QTest::addColumn<QString>("outFile");
        QTest::addColumn<LineWriterOptions>("options");
        LineWriterOptions defaultOptions;
        LineWriterOptions noReorderOptions;
        noReorderOptions.attributesSequence = LineWriterOptions::AttributesSequence::Preserve;

        QTest::newRow("file1") << QStringLiteral(u"file1.qml")
                               << QStringLiteral(u"file1Reformatted.qml") << defaultOptions;

        QTest::newRow("file2") << QStringLiteral(u"file2.qml")
                               << QStringLiteral(u"file2Reformatted.qml") << defaultOptions;

        QTest::newRow("commentedFile")
                << QStringLiteral(u"commentedFile.qml")
                << QStringLiteral(u"commentedFileReformatted.qml") << defaultOptions;

        QTest::newRow("required") << QStringLiteral(u"required.qml")
                                  << QStringLiteral(u"requiredReformatted.qml") << defaultOptions;

        QTest::newRow("inline") << QStringLiteral(u"inline.qml")
                                << QStringLiteral(u"inlineReformatted.qml") << defaultOptions;

        QTest::newRow("spread") << QStringLiteral(u"spread.qml")
                                << QStringLiteral(u"spreadReformatted.qml") << defaultOptions;

        QTest::newRow("template") << QStringLiteral(u"template.qml")
                                  << QStringLiteral(u"templateReformatted.qml") << defaultOptions;

        QTest::newRow("typeAnnotations")
                << QStringLiteral(u"typeAnnotations.qml")
                << QStringLiteral(u"typeAnnotationsReformatted.qml") << defaultOptions;

        QTest::newRow("file1NoReorder")
                << QStringLiteral(u"file1.qml") << QStringLiteral(u"file1Reformatted2.qml")
                << noReorderOptions;
    }

    void lineByLineReformatter()
    {
        QFETCH(QString, inFile);
        QFETCH(QString, outFile);
        QFETCH(LineWriterOptions, options);

        QString baseDir = QLatin1String(QT_QMLTEST_DATADIR) + QLatin1String("/reformatter");
        QStringList qmltypeDirs =
                QStringList({ baseDir, QLibraryInfo::path(QLibraryInfo::Qml2ImportsPath) });
        auto envPtr = DomEnvironment::create(
                qmltypeDirs,
                QQmlJS::Dom::DomEnvironment::Option::SingleThreaded
                        | QQmlJS::Dom::DomEnvironment::Option::NoDependencies);
        QString testFilePath = baseDir + QLatin1Char('/') + inFile;
        DomItem tFile;
        envPtr->loadBuiltins();
        envPtr->loadFile(FileToLoad::fromFileSystem(envPtr, testFilePath),
                         [&tFile](Path, const DomItem &, const DomItem &newIt) { tFile = newIt; });
        envPtr->loadPendingDependencies();

        MutableDomItem myFile = tFile.field(Fields::currentItem);

        QString resultStr;
        QTextStream res(&resultStr);
        IndentingLineWriter lw([&res](QStringView s) { res << s; }, QLatin1String("*testStream*"),
                               options);
        OutWriter ow(lw);
        DomItem qmlFile = tFile.field(Fields::currentItem);
        qmlFile.writeOut(ow);
        lw.eof();
        res.flush();
        QString fullRes = resultStr;
        res.seek(0);
        QFile fOut(baseDir + QLatin1Char('/') + outFile);
        if (!fOut.open(QIODevice::ReadOnly | QIODevice::Text)) {
            qWarning() << "could not open file" << outFile;
            return;
        }
        QTextStream out(&fOut);
        QString line = out.readLine();
        QString resLine = res.readLine();
        auto writeReformatted = [fullRes]() {
            qDebug().noquote().nospace() << "Reformatted output:\n"
                                         << "-----------------\n"
                                         << fullRes << "-----------------\n";
        };
        while (!line.isNull() && !resLine.isNull()) {
            if (resLine != line)
                writeReformatted();
            QCOMPARE(resLine, line);
            line = out.readLine();
            resLine = res.readLine();
        }
        if (resLine.isNull() != line.isNull()) {
            writeReformatted();
            qDebug() << "reformatted at end" << resLine.isNull() << resLine
                     << "reference at end:" << line.isNull() << line;
        }
        QCOMPARE(resLine.isNull(), line.isNull());
    }

    void manualReformatter_data()
    {
        LineWriterOptions noReorderOptions;
        QTest::addColumn<QString>("inFile");
        QTest::addColumn<QString>("outFile");
        QTest::addColumn<LineWriterOptions>("options");
        LineWriterOptions defaultOptions;

        noReorderOptions.attributesSequence = LineWriterOptions::AttributesSequence::Preserve;

        QTest::newRow("file1") << QStringLiteral(u"file1.qml")
                               << QStringLiteral(u"file1Reformatted.qml") << defaultOptions;

        QTest::newRow("file2") << QStringLiteral(u"file2.qml")
                               << QStringLiteral(u"file2Reformatted.qml") << defaultOptions;

        QTest::newRow("commentedFile")
                << QStringLiteral(u"commentedFile.qml")
                << QStringLiteral(u"commentedFileReformatted2.qml") << defaultOptions;

        QTest::newRow("required") << QStringLiteral(u"required.qml")
                                  << QStringLiteral(u"requiredReformatted2.qml") << defaultOptions;

        QTest::newRow("inline") << QStringLiteral(u"inline.qml")
                                << QStringLiteral(u"inlineReformatted.qml") << defaultOptions;

        QTest::newRow("spread") << QStringLiteral(u"spread.qml")
                                << QStringLiteral(u"spreadReformatted.qml") << defaultOptions;

        QTest::newRow("template") << QStringLiteral(u"template.qml")
                                  << QStringLiteral(u"templateReformatted.qml") << defaultOptions;

        QTest::newRow("arrowFunctions")
                << QStringLiteral(u"arrowFunctions.qml")
                << QStringLiteral(u"arrowFunctionsReformatted.qml") << defaultOptions;

        QTest::newRow("file1NoReorder")
                << QStringLiteral(u"file1.qml") << QStringLiteral(u"file1Reformatted2.qml")
                << noReorderOptions;
        QTest::newRow("noMerge")
                << QStringLiteral(u"noMerge.qml") << QStringLiteral(u"noMergeReformatted.qml")
                << defaultOptions;
    }

    void manualReformatter()
    {
        QFETCH(QString, inFile);
        QFETCH(QString, outFile);
        QFETCH(LineWriterOptions, options);

        QString baseDir = QLatin1String(QT_QMLTEST_DATADIR) + QLatin1String("/reformatter");
        QStringList qmltypeDirs =
                QStringList({ baseDir, QLibraryInfo::path(QLibraryInfo::Qml2ImportsPath) });
        auto envPtr = DomEnvironment::create(
                qmltypeDirs,
                QQmlJS::Dom::DomEnvironment::Option::SingleThreaded
                        | QQmlJS::Dom::DomEnvironment::Option::NoDependencies);
        QString testFilePath = baseDir + QLatin1Char('/') + inFile;
        DomItem tFile;
        envPtr->loadBuiltins();
        envPtr->loadFile(FileToLoad::fromFileSystem(envPtr, testFilePath),
                         [&tFile](Path, const DomItem &, const DomItem &newIt) { tFile = newIt; });
        envPtr->loadPendingDependencies();

        QString resultStr;
        QTextStream res(&resultStr);
        LineWriter lw([&res](QStringView s) { res << s; }, QLatin1String("*testStream*"), options);
        OutWriter ow(lw);
        ow.indentNextlines = true;
        DomItem qmlFile = tFile.field(Fields::currentItem);
        qmlFile.writeOut(ow);
        lw.eof();
        res.flush();
        QString fullRes = resultStr;
        res.seek(0);
        QFile fOut(baseDir + QLatin1Char('/') + outFile);
        if (!fOut.open(QIODevice::ReadOnly | QIODevice::Text)) {
            qWarning() << "could not open file" << outFile;
            return;
        }
        QTextStream out(&fOut);
        QString line = out.readLine();
        QString resLine = res.readLine();
        auto writeReformatted = [fullRes]() {
            qDebug().noquote().nospace() << "Reformatted output:\n"
                                         << "-----------------\n"
                                         << fullRes << "-----------------\n";
        };
        while (!line.isNull() && !resLine.isNull()) {
            if (resLine != line)
                writeReformatted();
            QCOMPARE(resLine, line);
            line = out.readLine();
            resLine = res.readLine();
        }
        if (resLine.isNull() != line.isNull()) {
            writeReformatted();
            qDebug() << "reformatted at end" << resLine.isNull() << resLine
                     << "reference at end:" << line.isNull() << line;
        }
        QCOMPARE(resLine.isNull(), line.isNull());
    }

    void indentInfo()
    {
        IndentInfo i1(u"\n\n  ", 4);
        QCOMPARE(i1.trailingString, u"  ");
        QCOMPARE(i1.nNewlines, 2);
        QCOMPARE(i1.column, 2);
        IndentInfo i2(u"\r\n\r\n  ", 4);
        QCOMPARE(i2.trailingString, u"  ");
        QCOMPARE(i2.nNewlines, 2);
        QCOMPARE(i2.column, 2);
        IndentInfo i3(u"\n ", 4);
        QCOMPARE(i3.trailingString, u" ");
        QCOMPARE(i3.nNewlines, 1);
        QCOMPARE(i3.column, 1);
        IndentInfo i4(u"\r\n ", 4);
        QCOMPARE(i4.trailingString, u" ");
        QCOMPARE(i4.nNewlines, 1);
        QCOMPARE(i4.column, 1);
        IndentInfo i5(u"\n", 4);
        QCOMPARE(i5.trailingString, u"");
        QCOMPARE(i5.nNewlines, 1);
        QCOMPARE(i5.column, 0);
        IndentInfo i6(u"\r\n", 4);
        QCOMPARE(i6.trailingString, u"");
        QCOMPARE(i6.nNewlines, 1);
        QCOMPARE(i6.column, 0);
        IndentInfo i7(u"  ", 4);
        QCOMPARE(i7.trailingString, u"  ");
        QCOMPARE(i7.nNewlines, 0);
        QCOMPARE(i7.column, 2);
        IndentInfo i8(u"", 4);
        QCOMPARE(i8.trailingString, u"");
        QCOMPARE(i8.nNewlines, 0);
        QCOMPARE(i8.column, 0);
    }

    void lineWriter()
    {
        {
            QString res;
            LineWriterOptions opts;
            opts.lineEndings = LineWriterOptions::LineEndings::Unix;
            LineWriter lw([&res](QStringView v) { res.append(v); }, QLatin1String("*testStream*"),
                          opts);
            lw.write(u"a\nb");
            lw.write(u"c\r\nd");
            lw.write(u"e\rf");
            lw.write(u"g\r\n");
            lw.write(u"h\r");
            lw.write(u"\n");
            QCOMPARE(res, u"a\nbc\nde\nfg\nh\n\n");
        }
        {
            QString res;
            LineWriterOptions opts;
            opts.lineEndings = LineWriterOptions::LineEndings::Windows;
            LineWriter lw([&res](QStringView v) { res.append(v); }, QLatin1String("*testStream*"),
                          opts);
            lw.write(u"a\nb");
            lw.write(u"c\r\nd");
            lw.write(u"e\rf");
            lw.write(u"g\r\n");
            lw.write(u"h\r");
            lw.write(u"\n");
            QCOMPARE(res, u"a\r\nbc\r\nde\r\nfg\r\nh\r\n\r\n");
        }
        {
            QString res;
            LineWriterOptions opts;
            opts.lineEndings = LineWriterOptions::LineEndings::OldMacOs;
            LineWriter lw([&res](QStringView v) { res.append(v); }, QLatin1String("*testStream*"),
                          opts);
            lw.write(u"a\nb");
            lw.write(u"c\r\nd");
            lw.write(u"e\rf");
            lw.write(u"g\r\n");
            lw.write(u"h\r");
            lw.write(u"\n");
            QCOMPARE(res, u"a\rbc\rde\rfg\rh\r\r");
        }
    }

    void hoistableDeclaration_data()
    {
        QTest::addColumn<QString>("declarationToBeFormatted");
        QTest::addColumn<QString>("expectedFormattedDeclaration");

        QTest::newRow("Function") << QStringLiteral(u"function a(a,b){}")
                                  << QStringLiteral(u"function a(a, b) {}");
        QTest::newRow("AnonymousFunction") << QStringLiteral(u"let f=function (a,b){}")
                                           << QStringLiteral(u"let f = function (a, b) {}");
        QTest::newRow("Generator_lhs_star")
                << QStringLiteral(u"function* g(a,b){}") << QStringLiteral(u"function* g(a, b) {}");
        QTest::newRow("Generator_rhs_star")
                << QStringLiteral(u"function *g(a,b){}") << QStringLiteral(u"function* g(a, b) {}");
        QTest::newRow("AnonymousGenerator") << QStringLiteral(u"let g=function * (a,b){}")
                                            << QStringLiteral(u"let g = function* (a, b) {}");
    }

    // https://262.ecma-international.org/7.0/#prod-HoistableDeclaration
    void hoistableDeclaration()
    {
        QFETCH(QString, declarationToBeFormatted);
        QFETCH(QString, expectedFormattedDeclaration);

        QString formattedDeclaration = formatJSCode(declarationToBeFormatted);

        QCOMPARE(formattedDeclaration, expectedFormattedDeclaration);
    }

    void exportDeclarations_data()
    {
        QTest::addColumn<QString>("exportToBeFormatted");
        QTest::addColumn<QString>("expectedFormattedExport");
        // not exhaustive list of ExportDeclarations as per
        // https://262.ecma-international.org/7.0/#prod-ExportDeclaration

        // LexicalDeclaration
        QTest::newRow("LexicalDeclaration_let_Binding")
                << QStringLiteral(u"export let name") << QStringLiteral(u"export let name;");
        QTest::newRow("LexicalDeclaration_const_BindingList")
                << QStringLiteral(u"export const "
                                  u"n1=1,n2=2,n3=3,n4=4,n5=5")
                << QStringLiteral(u"export const "
                                  u"n1 = 1, n2 = 2, n3 = 3, n4 = 4, n5 = 5;");
        QTest::newRow("LexicalDeclaration_const_ArrayBinding")
                << QStringLiteral(u"export const "
                                  u"[a,b]=a_and_b")
                << QStringLiteral(u"export const "
                                  u"[a, b] = a_and_b;");
        QTest::newRow("LexicalDeclaration_let_ObjectBinding")
                << QStringLiteral(u"export let "
                                  u"{a,b:c}=a_and_b")
                << QStringLiteral(u"export let "
                                  u"{\na,\nb: c\n} = a_and_b;");

        // ClassDeclaration
        QTest::newRow("ClassDeclaration") << QStringLiteral(u"export "
                                                            u"class A extends B{}")
                                          << QStringLiteral(u"export "
                                                            u"class A extends B {}");

        // HoistableDeclaration
        QTest::newRow("HoistableDeclaration_FunctionDeclaration")
                << QStringLiteral(u"export "
                                  u"function a(a,b){}")
                << QStringLiteral(u"export "
                                  u"function a(a, b) {}");
        QTest::newRow("HoistableDeclaration_GeneratorDeclaration")
                << QStringLiteral(u"export "
                                  u"function * g(a,b){}")
                << QStringLiteral(u"export "
                                  u"function* g(a, b) {}");
    }

    // https://262.ecma-international.org/7.0/#prod-ExportDeclaration
    void exportDeclarations()
    {
        QFETCH(QString, exportToBeFormatted);
        QFETCH(QString, expectedFormattedExport);

        QString formattedExport = formatJSModuleCode(exportToBeFormatted);

        QCOMPARE(formattedExport, expectedFormattedExport);
    }

private:
};

} // namespace Dom
} // namespace QQmlJS
QT_END_NAMESPACE

#endif // TST_QMLDOMSCANNER_H
