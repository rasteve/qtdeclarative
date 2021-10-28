/****************************************************************************
**
** Copyright (C) 2019 The Qt Company Ltd.
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

#include "qqmljsimportvisitor_p.h"
#include "qqmljsresourcefilemapper_p.h"

#include <QtCore/qfileinfo.h>
#include <QtCore/qdir.h>
#include <QtCore/qqueue.h>
#include <QtCore/qscopedvaluerollback.h>
#include <QtCore/qpoint.h>
#include <QtCore/qrect.h>
#include <QtCore/qsize.h>

#include <QtQml/private/qv4codegen_p.h>
#include <QtQml/private/qqmlstringconverters_p.h>

#include <algorithm>

QT_BEGIN_NAMESPACE

using namespace QQmlJS::AST;

/*!
  \internal
  Sets the name of \a scope to \a name based on \a type.
*/
inline void setScopeName(QQmlJSScope::Ptr &scope, QQmlJSScope::ScopeType type, const QString &name)
{
    Q_ASSERT(scope);
    if (type == QQmlJSScope::GroupedPropertyScope || type == QQmlJSScope::AttachedPropertyScope)
        scope->setInternalName(name);
    else
        scope->setBaseTypeName(name);
}

/*!
  \internal
  Returns the name of \a scope based on \a type.
*/
inline QString getScopeName(const QQmlJSScope::ConstPtr &scope, QQmlJSScope::ScopeType type)
{
    Q_ASSERT(scope);
    if (type == QQmlJSScope::GroupedPropertyScope || type == QQmlJSScope::AttachedPropertyScope)
        return scope->internalName();

    return scope->baseTypeName();
}

QQmlJSImportVisitor::QQmlJSImportVisitor(QQmlJSImporter *importer, QQmlJSLogger *logger,
                                         const QString &implicitImportDirectory,
                                         const QStringList &qmltypesFiles)
    : m_implicitImportDirectory(implicitImportDirectory),
      m_qmltypesFiles(qmltypesFiles),
      m_currentScope(QQmlJSScope::create(QQmlJSScope::JSFunctionScope)),
      m_importer(importer),
      m_logger(logger)
{
    Q_ASSERT(logger); // must be valid

    m_globalScope = m_currentScope;
    m_currentScope->setIsComposite(true);

    m_currentScope->setInternalName(u"global"_qs);

    QLatin1String jsGlobVars[] = { /* Not listed on the MDN page; browser and QML extensions: */
                                   // console/debug api
                                   QLatin1String("console"), QLatin1String("print"),
                                   // garbage collector
                                   QLatin1String("gc"),
                                   // i18n
                                   QLatin1String("qsTr"), QLatin1String("qsTrId"),
                                   QLatin1String("QT_TR_NOOP"), QLatin1String("QT_TRANSLATE_NOOP"),
                                   QLatin1String("QT_TRID_NOOP"),
                                   // XMLHttpRequest
                                   QLatin1String("XMLHttpRequest")
    };

    QQmlJSScope::JavaScriptIdentifier globalJavaScript = {
        QQmlJSScope::JavaScriptIdentifier::LexicalScoped, QQmlJS::SourceLocation()
    };
    for (const char **globalName = QV4::Compiler::Codegen::s_globalNames; *globalName != nullptr;
         ++globalName) {
        m_currentScope->insertJSIdentifier(QString::fromLatin1(*globalName), globalJavaScript);
    }
    for (const auto &jsGlobVar : jsGlobVars)
        m_currentScope->insertJSIdentifier(jsGlobVar, globalJavaScript);
}

QQmlJSImportVisitor::~QQmlJSImportVisitor() = default;

void QQmlJSImportVisitor::enterEnvironment(QQmlJSScope::ScopeType type, const QString &name,
                                           const QQmlJS::SourceLocation &location)
{
    m_currentScope = QQmlJSScope::create(type, m_currentScope);
    setScopeName(m_currentScope, type, name);
    m_currentScope->setIsComposite(true);
    m_currentScope->setSourceLocation(location);
}

bool QQmlJSImportVisitor::enterEnvironmentNonUnique(QQmlJSScope::ScopeType type,
                                                    const QString &name,
                                                    const QQmlJS::SourceLocation &location)
{
    Q_ASSERT(type == QQmlJSScope::GroupedPropertyScope
             || type == QQmlJSScope::AttachedPropertyScope);

    const auto pred = [&](const QQmlJSScope::ConstPtr &s) {
        // it's either attached or group property, so use internalName()
        // directly. see setScopeName() for details
        return s->internalName() == name;
    };
    const auto scopes = m_currentScope->childScopes();
    // TODO: linear search. might want to make childScopes() a set/hash-set and
    // use faster algorithm here
    auto it = std::find_if(scopes.begin(), scopes.end(), pred);
    if (it == scopes.end()) {
        // create and enter new scope
        enterEnvironment(type, name, location);
        return false;
    }
    // enter found scope
    m_currentScope = *it;
    return true;
}

void QQmlJSImportVisitor::leaveEnvironment()
{
    m_currentScope = m_currentScope->parentScope();
}

static bool mayBeUnresolvedGeneralizedGroupedProperty(const QQmlJSScope::ConstPtr &scope)
{
    return scope->scopeType() == QQmlJSScope::GroupedPropertyScope && !scope->baseType();
}

void QQmlJSImportVisitor::resolveAliasesAndIds()
{
    QQueue<QQmlJSScope::Ptr> objects;
    objects.enqueue(m_exportedRootScope);

    qsizetype lastRequeueLength = std::numeric_limits<qsizetype>::max();
    QQueue<QQmlJSScope::Ptr> requeue;

    while (!objects.isEmpty()) {
        const QQmlJSScope::Ptr object = objects.dequeue();
        const auto properties = object->ownProperties();

        bool doRequeue = false;
        for (auto property : properties) {
            if (!property.isAlias() || !property.type().isNull())
                continue;

            QStringList components = property.aliasExpression().split(u'.');
            QQmlJSScope::ConstPtr type;
            QQmlJSMetaProperty targetProperty;

            bool foundProperty = false;

            // The first component has to be an ID. Find the object it refers to.
            const auto it = m_scopesById.find(components.takeFirst());
            if (it != m_scopesById.end()) {
                foundProperty = true;
                type = *it;

                // Any further components are nested properties of that object.
                // Technically we can only resolve a limited depth in the engine, but the rules
                // on that are fuzzy and subject to change. Let's ignore it for now.
                // If the target is itself an alias and has not been resolved, re-queue the object
                // and try again later.
                while (type && !components.isEmpty()) {
                    const QString name = components.takeFirst();

                    if (!type->hasProperty(name)) {
                        foundProperty = false;
                        type = {};
                        break;
                    }

                    const auto target = type->property(name);
                    if (!target.type() && target.isAlias())
                        doRequeue = true;
                    type = target.type();
                    targetProperty = target;
                }
            }

            if (type.isNull()) {
                if (doRequeue)
                    continue;
                if (foundProperty) {
                    m_logger->logWarning(QStringLiteral("Cannot deduce type of alias \"%1\"")
                                                 .arg(property.propertyName()),
                                         Log_Alias, object->sourceLocation());
                } else {
                    m_logger->logWarning(QStringLiteral("Cannot resolve alias \"%1\"")
                                                 .arg(property.propertyName()),
                                         Log_Alias, object->sourceLocation());
                }
            } else {
                property.setType(type);
                // Copy additional property information from target
                property.setIsList(targetProperty.isList());
                property.setIsWritable(targetProperty.isWritable());
                property.setIsPointer(targetProperty.isPointer());

                if (const QString internalName = type->internalName(); !internalName.isEmpty())
                    property.setTypeName(internalName);
            }
            Q_ASSERT(property.index() >= 0); // this property is already in object

            object->addOwnProperty(property);
        }

        const auto childScopes = object->childScopes();
        for (const auto &childScope : childScopes) {
            if (mayBeUnresolvedGeneralizedGroupedProperty(childScope)) {
                const QString name = childScope->internalName();
                if (object->isNameDeferred(name)) {
                    auto it = m_scopesById.find(name);
                    if (it != m_scopesById.end()) {
                        QQmlJSScope::resolveGeneralizedGroup(
                                    childScope, *it, m_rootScopeImports, &m_usedTypes);
                    }
                }
            }
            objects.enqueue(childScope);
        }

        if (doRequeue)
            requeue.enqueue(object);

        if (objects.isEmpty() && requeue.length() < lastRequeueLength) {
            lastRequeueLength = requeue.length();
            objects.swap(requeue);
        }
    }

    while (!requeue.isEmpty()) {
        const QQmlJSScope::Ptr object = requeue.dequeue();
        const auto properties = object->ownProperties();
        for (const auto &property : properties) {
            if (!property.isAlias() || property.type())
                continue;
            m_logger->logWarning(QStringLiteral("Alias \"%1\" is part of an alias cycle")
                                         .arg(property.propertyName()),
                                 Log_Alias, object->sourceLocation());
        }
    }
}

QQmlJSScope::Ptr QQmlJSImportVisitor::result() const
{
    return m_exportedRootScope;
}

QString QQmlJSImportVisitor::implicitImportDirectory(
        const QString &localFile, QQmlJSResourceFileMapper *mapper)
{
    if (mapper) {
        const auto resource = mapper->entry(
                    QQmlJSResourceFileMapper::localFileFilter(localFile));
        if (resource.isValid()) {
            return resource.resourcePath.contains(u'/')
                    ? (u':' + resource.resourcePath.left(
                           resource.resourcePath.lastIndexOf(u'/') + 1))
                    : QStringLiteral(":/");
        }
    }

    return QFileInfo(localFile).canonicalPath() + u'/';
}

bool QQmlJSImportVisitor::processImportWarnings(const QString &what, const QQmlJS::SourceLocation &srcLocation)
{
    const auto warnings = m_importer->takeWarnings();

    if (warnings.isEmpty())
        return true;

    m_logger->logWarning(QStringLiteral("Warnings occurred while importing %1:").arg(what),
                         Log_Import, srcLocation);
    m_logger->processMessages(warnings, QtWarningMsg, Log_Import);

    return false;
}

bool QQmlJSImportVisitor::importBaseModules()
{
    Q_ASSERT(m_rootScopeImports.isEmpty());
    m_rootScopeImports = m_importer->importBuiltins();

    const QQmlJS::SourceLocation invalidLoc;
    for (const QString &name : m_rootScopeImports.keys()) {
        addImportWithLocation(name, invalidLoc);
    }

    if (!m_qmltypesFiles.isEmpty())
        m_importer->importQmltypes(m_qmltypesFiles);

    // Pulling in the modules and neighboring qml files of the qmltypes we're trying to lint is not
    // something we need to do.
    if (!m_logger->fileName().endsWith(u".qmltypes"_qs)) {
        m_rootScopeImports.insert(m_importer->importDirectory(m_implicitImportDirectory));

        QQmlJSResourceFileMapper *mapper = m_importer->resourceFileMapper();

        // In instances where a qmldir entry exists somewhere in the resource files, import that
        // directory in order to allow for implicit imports of modules.
        if (mapper) {
            const QStringList filePaths = mapper->filePaths(QQmlJSResourceFileMapper::Filter {
                    QString(), QStringList(),
                    QQmlJSResourceFileMapper::Directory | QQmlJSResourceFileMapper::Recurse });
            auto qmldirEntry =
                    std::find_if(filePaths.constBegin(), filePaths.constEnd(),
                                 [](const QString &path) { return path.endsWith(u"/qmldir"); });

            if (qmldirEntry != filePaths.constEnd()) {
                m_rootScopeImports.insert(
                        m_importer->importDirectory(QFileInfo(*qmldirEntry).absolutePath()));
            }
        }
    }

    return processImportWarnings(QStringLiteral("base modules"));
}

bool QQmlJSImportVisitor::visit(QQmlJS::AST::UiProgram *)
{
    if (!importBaseModules()) {
        m_logger->logCritical(u"Failed to import base modules. Aborting."_qs, Log_Import);
        m_aborted = true;
        return false;
    }

    return true;
}

void QQmlJSImportVisitor::endVisit(UiProgram *)
{
    if (m_aborted)
        return;

    for (const auto &scope : m_objectBindingScopes) {
        if (checkInheritanceCycle(scope) == CycleFound)
            return;
    }

    for (const auto &scope : m_objectDefinitionScopes) {
        if (checkInheritanceCycle(scope) == CycleFound)
            return;
    }

    for (const auto &scope : m_pendingDefaultProperties.keys()) {
        if (checkInheritanceCycle(scope) == CycleFound)
            return;
    }

    resolveAliasesAndIds();

    for (const auto &scope : m_objectDefinitionScopes)
        checkGroupedAndAttachedScopes(scope);

    processDefaultProperties();
    processPropertyTypes();
    processPropertyBindings();
    checkSignals();
    processPropertyBindingObjects();
    checkRequiredProperties();

    auto unusedImports = m_importLocations;
    for (const QString &type : m_usedTypes) {
        for (const auto &importLocation : m_importTypeLocationMap.values(type))
            unusedImports.remove(importLocation);

        // If there are no more unused imports left we can abort early
        if (unusedImports.isEmpty())
            break;
    }

    for (const auto &import : unusedImports) {
        m_logger->logInfo(QString::fromLatin1("Unused import at %1:%2:%3")
                                  .arg(m_logger->fileName())
                                  .arg(import.startLine)
                                  .arg(import.startColumn),
                          Log_UnusedImport, import);
    }
}

static QQmlJSAnnotation::Value bindingToVariant(QQmlJS::AST::Statement *statement)
{
    ExpressionStatement *expr = cast<ExpressionStatement *>(statement);

    if (!statement || !expr->expression)
        return {};

    switch (expr->expression->kind) {
    case Node::Kind_StringLiteral:
        return cast<StringLiteral *>(expr->expression)->value.toString();
    case Node::Kind_NumericLiteral:
        return cast<NumericLiteral *>(expr->expression)->value;
    default:
        return {};
    }
}

QVector<QQmlJSAnnotation> QQmlJSImportVisitor::parseAnnotations(QQmlJS::AST::UiAnnotationList *list)
{

    QVector<QQmlJSAnnotation> annotationList;

    for (UiAnnotationList *item = list; item != nullptr; item = item->next) {
        UiAnnotation *annotation = item->annotation;

        QString name;
        for (auto id = annotation->qualifiedTypeNameId; id; id = id->next)
            name += id->name.toString() + QLatin1Char('.');

        name.chop(1);



        QQmlJSAnnotation qqmljsAnnotation;

        qqmljsAnnotation.name = name;

        for (UiObjectMemberList *memberItem = annotation->initializer->members; memberItem != nullptr; memberItem = memberItem->next) {
            switch (memberItem->member->kind) {
            case Node::Kind_UiScriptBinding: {
                auto *scriptBinding = QQmlJS::AST::cast<UiScriptBinding*>(memberItem->member);
                QString bindingName;
                for (auto id = scriptBinding->qualifiedId; id; id = id->next)
                    bindingName += id->name.toString() + QLatin1Char('.');

                bindingName.chop(1);

                qqmljsAnnotation.bindings[bindingName] = bindingToVariant(scriptBinding->statement);
                break;
            }
            default:
                // We ignore all the other information contained in the annotation
                break;
            }
        }

        annotationList.append(qqmljsAnnotation);
    }

    return annotationList;
}

void QQmlJSImportVisitor::processDefaultProperties()
{
    for (auto it = m_pendingDefaultProperties.constBegin();
         it != m_pendingDefaultProperties.constEnd(); ++it) {
        QQmlJSScope::ConstPtr parentScope = it.key();

        // We can't expect custom parser default properties to be sensible, discard them for now.
        if (parentScope->isInCustomParserParent())
            continue;

        /* consider:
         *
         *      QtObject { // <- parentScope
         *          default property var p // (1)
         *          QtObject {} // (2)
         *      }
         *
         * `p` (1) is a property of a subtype of QtObject, it couldn't be used
         * in a property binding (2)
         */
        // thus, use a base type of parent scope to detect a default property
        parentScope = parentScope->baseType();

        const QString defaultPropertyName =
                parentScope ? parentScope->defaultPropertyName() : QString();

        if (defaultPropertyName.isEmpty()) {
            // If the parent scope is based on Component it can have any child element
            // TODO: We should also store these somewhere
            bool isComponent = false;
            for (QQmlJSScope::ConstPtr s = parentScope; s; s = s->baseType()) {
                if (s->internalName() == QStringLiteral("QQmlComponent")) {
                    isComponent = true;
                    break;
                }
            }

            if (!isComponent) {
                m_logger->logWarning(
                        QStringLiteral("Cannot assign to non-existent default property"),
                        Log_Property, it.value().constFirst()->sourceLocation());
            }

            continue;
        }

        const QQmlJSMetaProperty defaultProp = parentScope->property(defaultPropertyName);

        if (it.value().length() > 1 && !defaultProp.isList()) {
            m_logger->logWarning(
                    QStringLiteral("Cannot assign multiple objects to a default non-list property"),
                    Log_Property, it.value().constFirst()->sourceLocation());
        }

        QQmlJSMetaPropertyBinding binding(defaultProp);
        auto propType = defaultProp.type();
        if (propType.isNull() || !propType->isFullyResolved())
            return;

        const auto scopes = *it;
        for (const auto &scope : scopes) {
            binding.setValue(QQmlJSScope::ConstPtr(scope));
            binding.setValueTypeName(getScopeName(scope, QQmlJSScope::QMLScope));
            it.key()->addOwnPropertyBinding(binding);

            if (!scope->isFullyResolved()) // should be an error somewhere else
                continue;

            // Assigning any element to a QQmlComponent property implicitly wraps it into a Component
            // Check whether the property can be assigned the scope
            if (propType->canAssign(scope)) {
                if (propType->causesImplicitComponentWrapping()) {
                    // mark the scope as implicitly wrapped, unless it is a Component
                    scope->setIsWrappedInImplicitComponent(!scope->causesImplicitComponentWrapping());
                }
                continue;
            }

            m_logger->logWarning(
                    QStringLiteral("Cannot assign to default property of incompatible type"),
                    Log_Property, scope->sourceLocation());
        }
    }
}

void QQmlJSImportVisitor::processPropertyTypes()
{
    for (const PendingPropertyType &type : m_pendingPropertyTypes) {
        Q_ASSERT(type.scope->hasOwnProperty(type.name));

        auto property = type.scope->ownProperty(type.name);

        if (const auto propertyType = m_rootScopeImports.value(property.typeName())) {
            property.setType(propertyType);
            type.scope->addOwnProperty(property);
        } else {
            m_logger->logWarning(
                    property.typeName()
                            + QStringLiteral(" was not found. Did you add all import paths?"),
                    Log_Import, type.location);
        }
    }
}

void QQmlJSImportVisitor::processPropertyBindingObjects()
{
    for (const PendingPropertyObjectBinding &objectBinding : m_pendingPropertyObjectBindings) {
        const QString propertyName = objectBinding.name;
        QQmlJSScope::ConstPtr childScope = objectBinding.childScope;

        QQmlJSMetaProperty property = objectBinding.scope->property(propertyName);

        if (property.isValid() && !property.type().isNull()
            && (objectBinding.onToken || property.type()->canAssign(objectBinding.childScope))) {


            if (property.type()->causesImplicitComponentWrapping())
                objectBinding.childScope->setIsWrappedInImplicitComponent(!objectBinding.childScope->causesImplicitComponentWrapping());

            QQmlJSMetaPropertyBinding binding =
                    objectBinding.scope->hasOwnPropertyBinding(propertyName)
                    ? objectBinding.scope->ownPropertyBinding(propertyName)
                    : QQmlJSMetaPropertyBinding(property);

            const QString typeName = getScopeName(childScope, QQmlJSScope::QMLScope);

            if (objectBinding.onToken) {
                if (childScope->hasInterface(QStringLiteral("QQmlPropertyValueInterceptor"))) {
                    if (binding.hasInterceptor()) {
                        m_logger->logWarning(
                                QStringLiteral("Duplicate interceptor on property \"%1\"")
                                        .arg(propertyName),
                                Log_Property, objectBinding.location);
                    } else {
                        binding.setInterceptor(childScope);
                        binding.setInterceptorTypeName(typeName);

                        objectBinding.scope->addOwnPropertyBinding(binding);
                    }
                } else if (childScope->hasInterface(QStringLiteral("QQmlPropertyValueSource"))) {
                    if (binding.hasValueSource()) {
                        m_logger->logWarning(
                                QStringLiteral("Duplicate value source on property \"%1\"")
                                        .arg(propertyName),
                                Log_Property, objectBinding.location);
                    } else if (binding.hasValue()) {
                        m_logger->logWarning(
                                QStringLiteral("Cannot combine value source and binding on "
                                               "property \"%1\"")
                                        .arg(propertyName),
                                Log_Property, objectBinding.location);
                    } else {
                        binding.setValueSource(childScope);
                        binding.setValueSourceTypeName(typeName);
                        objectBinding.scope->addOwnPropertyBinding(binding);
                    }
                } else {
                    m_logger->logWarning(
                            QStringLiteral("On-binding for property \"%1\" has wrong type \"%2\"")
                                    .arg(propertyName)
                                    .arg(typeName),
                            Log_Property, objectBinding.location);
                }
            } else {
                // TODO: Warn here if binding.hasValue() is true
                if (binding.hasValueSource()) {
                    m_logger->logWarning(
                            QStringLiteral(
                                    "Cannot combine value source and binding on property \"%1\"")
                                    .arg(propertyName),
                            Log_Property, objectBinding.location);
                } else {
                    binding.setValue(childScope);
                    binding.setValueTypeName(typeName);
                    objectBinding.scope->addOwnPropertyBinding(binding);
                }
            }
        } else if (!objectBinding.scope->isFullyResolved()) {
            // If the current scope is not fully resolved we cannot tell whether the property exists
            // or not (we already warn elsewhere)
        } else if (!property.isValid()) {
            m_logger->logWarning(QStringLiteral("Property \"%1\" is invalid or does not exist")
                                         .arg(propertyName),
                                 Log_Property, objectBinding.location);
        } else if (property.type().isNull() || !property.type()->isFullyResolved()) {
            // Property type is not fully resolved we cannot tell any more than this
            m_logger->logWarning(
                    QStringLiteral("Property \"%1\" has incomplete type \"%2\". You may be "
                                   "missing an import.")
                            .arg(propertyName)
                            .arg(property.typeName()),
                    Log_Property, objectBinding.location);
        } else if (!childScope->isFullyResolved()) {
            // If the childScope type is not fully resolved we cannot tell whether the type is
            // incompatible (we already warn elsewhere)
        } else {
            // the type is incompatible
            m_logger->logWarning(QStringLiteral("Property \"%1\" of type \"%2\" is assigned an "
                                                "incompatible type \"%3\"")
                                         .arg(propertyName)
                                         .arg(property.typeName())
                                         .arg(getScopeName(childScope, QQmlJSScope::QMLScope)),
                                 Log_Property, objectBinding.location);
        }
    }
}

void QQmlJSImportVisitor::checkRequiredProperties()
{
    for (const auto &required : m_requiredProperties) {
        if (!required.scope->hasProperty(required.name)) {
            m_logger->logWarning(
                    QStringLiteral("Property \"%1\" was marked as required but does not exist.")
                            .arg(required.name),
                    Log_Required, required.location);
        }
    }

    for (const auto &defScope : m_objectDefinitionScopes) {
        if (defScope->parentScope() == m_globalScope || defScope->isInlineComponent() || defScope->isComponentRootElement())
            continue;

        QVector<QQmlJSScope::ConstPtr> scopesToSearch;
        for (QQmlJSScope::ConstPtr scope = defScope; scope; scope = scope->baseType()) {
            scopesToSearch << scope;
            const auto ownProperties = scope->ownProperties();
            for (auto propertyIt = ownProperties.constBegin();
                 propertyIt != ownProperties.constEnd(); ++propertyIt) {
                const QString propName = propertyIt.key();

                QQmlJSScope::ConstPtr prevRequiredScope;
                for (QQmlJSScope::ConstPtr requiredScope : scopesToSearch) {
                    if (requiredScope->isPropertyLocallyRequired(propName)) {
                        bool found =
                                std::find_if(scopesToSearch.constBegin(), scopesToSearch.constEnd(),
                                             [&](QQmlJSScope::ConstPtr scope) {
                                                 return scope->hasPropertyBinding(propName);
                                             })
                                != scopesToSearch.constEnd();

                        if (!found) {
                            const QString propertyScopeName = scopesToSearch.length() > 1
                                    ? getScopeName(scopesToSearch.at(scopesToSearch.length() - 2),
                                                   QQmlJSScope::QMLScope)
                                    : u"here"_qs;
                            const QString requiredScopeName = prevRequiredScope
                                    ? getScopeName(prevRequiredScope, QQmlJSScope::QMLScope)
                                    : u"here"_qs;

                            QString message =
                                    QStringLiteral(
                                            "Component is missing required property %1 from %2")
                                            .arg(propName)
                                            .arg(propertyScopeName);
                            if (requiredScope != scope)
                                message += QStringLiteral(" (marked as required by %3)")
                                                   .arg(requiredScopeName);

                            m_logger->logWarning(message, Log_Required, defScope->sourceLocation());
                        }
                    }
                    prevRequiredScope = requiredScope;
                }
            }
        }
    }
}

void QQmlJSImportVisitor::processPropertyBindings()
{
    for (auto it = m_propertyBindings.constBegin(); it != m_propertyBindings.constEnd(); ++it) {
        QQmlJSScope::Ptr scope = it.key();
        for (auto &[visibilityScope, location, name] : it.value()) {
            if (!scope->hasProperty(name)) {
                // These warnings do not apply for custom parsers and their children and need to be
                // handled on a case by case basis

                if (scope->isInCustomParserParent())
                    continue;

                // TODO: Can this be in a better suited category?
                m_logger->logWarning(
                        QStringLiteral("Binding assigned to \"%1\", but no property \"%1\" "
                                       "exists in the current element.")
                                .arg(name),
                        Log_Type, location);
                continue;
            }

            const auto property = scope->property(name);
            if (!property.type()) {
                m_logger->logWarning(
                        QStringLiteral("No type found for property \"%1\". This may be due "
                                       "to a missing import statement or incomplete "
                                       "qmltypes files.")
                                .arg(name),
                        Log_Type, location);
            }

            const auto &annotations = property.annotations();

            const auto deprecationAnn =
                    std::find_if(annotations.cbegin(), annotations.cend(),
                                 [](const QQmlJSAnnotation &ann) { return ann.isDeprecation(); });

            if (deprecationAnn != annotations.cend()) {
                const auto deprecation = deprecationAnn->deprecation();

                QString message = QStringLiteral("Binding on deprecated property \"%1\"")
                                          .arg(property.propertyName());

                if (!deprecation.reason.isEmpty())
                    message.append(QStringLiteral(" (Reason: %1)").arg(deprecation.reason));

                m_logger->logWarning(message, Log_Deprecation, location);
            }

            QQmlJSMetaPropertyBinding binding(property);

            // TODO: Actually store the value

            visibilityScope->addOwnPropertyBinding(binding);
        }
    }
}

static QString signalName(QStringView handlerName)
{
    if (handlerName.startsWith(u"on") && handlerName.size() > 2) {
        QString signal = handlerName.mid(2).toString();
        for (int i = 0; i < signal.length(); ++i) {
            QChar &ch = signal[i];
            if (ch.isLower())
                return QString();
            if (ch.isUpper()) {
                ch = ch.toLower();
                return signal;
            }
        }
    }
    return QString();
}

void QQmlJSImportVisitor::checkSignals()
{
    for (auto it = m_signals.constBegin(); it != m_signals.constEnd(); ++it) {
        for (const auto &scopeAndPair : it.value()) {
            const auto location = scopeAndPair.dataLocation;
            const auto &pair = scopeAndPair.data;
            const QString signal = signalName(pair.first);

            if (!it.key()->hasMethod(signal)) {
                m_logger->logWarning(QStringLiteral("no matching signal found for handler \"%1\"")
                                             .arg(pair.first),
                                     Log_UnqualifiedAccess, location);
                continue;
            }

            QQmlJSMetaMethod scopeSignal;
            for (QQmlJSScope::ConstPtr scope = it.key(); scope; scope = scope->baseType()) {
                const auto methods = scope->ownMethods();
                const auto methodsRange = methods.equal_range(signal);
                for (auto method = methodsRange.first; method != methodsRange.second; ++method) {
                    if (method->methodType() != QQmlJSMetaMethod::Signal)
                        continue;
                    scopeSignal = *method;
                    break;
                }
            }

            const QStringList signalParameters = scopeSignal.parameterNames();

            if (pair.second.length() > signalParameters.length()) {
                m_logger->logWarning(QStringLiteral("Signal handler for \"%2\" has more formal"
                                                    " parameters than the signal it handles.")
                                             .arg(pair.first),
                                     Log_Signal, location);
                continue;
            }

            for (qsizetype i = 0; i < pair.second.length(); i++) {
                const QStringView handlerParameter = pair.second.at(i);
                const qsizetype j = signalParameters.indexOf(handlerParameter);
                if (j == i || j < 0)
                    continue;

                m_logger->logWarning(QStringLiteral("Parameter %1 to signal handler for \"%2\""
                                                    " is called \"%3\". The signal has a parameter"
                                                    " of the same name in position %4.")
                                             .arg(i + 1)
                                             .arg(pair.first, handlerParameter)
                                             .arg(j + 1),
                                     Log_Signal, location);
            }
        }
    }
}

void QQmlJSImportVisitor::addDefaultProperties()
{
    if (m_currentScope == m_exportedRootScope || m_currentScope->parentScope()->isArrayScope()
        || m_currentScope->isInlineComponent()) // inapplicable
        return;

    m_pendingDefaultProperties[m_currentScope->parentScope()] << m_currentScope;
}

QQmlJSImportVisitor::HasCycle
QQmlJSImportVisitor::checkInheritanceCycle(QQmlJSScope::ConstPtr scope)
{
    QQmlJSScope::ConstPtr originalScope = scope;
    QList<QQmlJSScope::ConstPtr> scopes;
    while (!scope.isNull()) {
        for (const QQmlJSAnnotation &annotation : scope->annotations()) {
            if (annotation.isDeprecation()) {
                QQQmlJSDeprecation deprecation = annotation.deprecation();

                QString message =
                        QStringLiteral("Type \"%1\" is deprecated").arg(scope->internalName());

                if (!deprecation.reason.isEmpty())
                    message.append(QStringLiteral(" (Reason: %1)").arg(deprecation.reason));

                m_logger->logWarning(message, Log_Deprecation, originalScope->sourceLocation());
            }
        }

        if (scopes.contains(scope)) {
            QString inheritenceCycle;
            for (const auto &seen : qAsConst(scopes)) {
                if (!inheritenceCycle.isEmpty())
                    inheritenceCycle.append(QLatin1String(" -> "));
                inheritenceCycle.append(seen->baseTypeName());
            }

            m_logger->logWarning(QStringLiteral("%1 is part of an inheritance cycle: %2")
                                         .arg(scope->internalName())
                                         .arg(inheritenceCycle),
                                 Log_InheritanceCycle);
            return CycleFound;
        }

        scopes.append(scope);

        if (scope->baseTypeName().isEmpty()) {
            break;
        } else if (auto newScope = scope->baseType()) {
            scope = newScope;
        } else {
            m_logger->logWarning(
                    scope->baseTypeName()
                            + QStringLiteral(" was not found. Did you add all import paths?"),
                    Log_Import);
            break;
        }
    }

    return CycleNotFound;
}

void QQmlJSImportVisitor::checkGroupedAndAttachedScopes(QQmlJSScope::ConstPtr scope)
{
    // These warnings do not apply for custom parsers and their children and need to be handled on a
    // case by case basis
    if (scope->isInCustomParserParent())
        return;

    auto children = scope->childScopes();
    while (!children.isEmpty()) {
        auto childScope = children.takeFirst();
        const auto type = childScope->scopeType();
        switch (type) {
        case QQmlJSScope::GroupedPropertyScope:
        case QQmlJSScope::AttachedPropertyScope:
            if (!childScope->baseType()) {
                m_logger->logWarning(QStringLiteral("unknown %1 property scope %2.")
                                             .arg(type == QQmlJSScope::GroupedPropertyScope
                                                          ? QStringLiteral("grouped")
                                                          : QStringLiteral("attached"),
                                                  childScope->internalName()),
                                     Log_UnqualifiedAccess, childScope->sourceLocation());
            }
            children.append(childScope->childScopes());
        default:
            break;
        }
    }
}

void QQmlJSImportVisitor::flushPendingSignalParameters()
{
    const QQmlJSMetaSignalHandler handler = m_signalHandlers[m_pendingSignalHandler];
    for (const QString &parameter : handler.signalParameters) {
        m_currentScope->insertJSIdentifier(
                parameter, { QQmlJSScope::JavaScriptIdentifier::Injected, m_pendingSignalHandler });
    }
    m_pendingSignalHandler = QQmlJS::SourceLocation();
}

bool QQmlJSImportVisitor::visit(QQmlJS::AST::ExpressionStatement *ast)
{
    if (m_pendingSignalHandler.isValid()) {
        enterEnvironment(QQmlJSScope::JSFunctionScope, u"signalhandler"_qs,
                         ast->firstSourceLocation());
        flushPendingSignalParameters();
    }
    return true;
}

void QQmlJSImportVisitor::endVisit(QQmlJS::AST::ExpressionStatement *)
{
    if (m_currentScope->scopeType() == QQmlJSScope::JSFunctionScope
        && m_currentScope->baseTypeName() == u"signalhandler"_qs) {
        leaveEnvironment();
    }
}

bool QQmlJSImportVisitor::visit(QQmlJS::AST::StringLiteral *sl)
{
    const QString s = m_logger->code().mid(sl->literalToken.begin(), sl->literalToken.length);

    if (s.contains(QLatin1Char('\r')) || s.contains(QLatin1Char('\n')) || s.contains(QChar(0x2028u))
        || s.contains(QChar(0x2029u))) {
        m_logger->logWarning(QStringLiteral("String contains unescaped line terminator which is "
                                            "deprecated. Use a template "
                                            "literal instead."),
                             Log_MultilineString, sl->literalToken);
    }

    return true;
}

bool QQmlJSImportVisitor::visit(UiObjectDefinition *definition)
{
    QString superType;
    for (auto segment = definition->qualifiedTypeNameId; segment; segment = segment->next) {
        if (!superType.isEmpty())
            superType.append(u'.');
        superType.append(segment->name.toString());
    }

    Q_ASSERT(!superType.isEmpty());
    if (superType.front().isUpper()) {
        enterEnvironment(QQmlJSScope::QMLScope, superType, definition->firstSourceLocation());
        if (!m_exportedRootScope)
            m_exportedRootScope = m_currentScope;

        if (m_nextIsInlineComponent) {
            m_currentScope->setIsInlineComponent(true);
            m_rootScopeImports.insert(m_inlineComponentName.toString(), m_currentScope);
            m_nextIsInlineComponent = false;
        }
    } else {
        enterEnvironmentNonUnique(QQmlJSScope::GroupedPropertyScope, superType,
                                  definition->firstSourceLocation());
        Q_ASSERT(m_exportedRootScope);
    }

    m_currentScope->setAnnotations(parseAnnotations(definition->annotations));

    QQmlJSScope::resolveTypes(m_currentScope, m_rootScopeImports, &m_usedTypes);
    addDefaultProperties();
    if (m_currentScope->scopeType() == QQmlJSScope::QMLScope)
        m_qmlTypes.append(m_currentScope);
    return true;
}

void QQmlJSImportVisitor::endVisit(UiObjectDefinition *)
{
    QQmlJSScope::resolveTypes(m_currentScope, m_rootScopeImports, &m_usedTypes);
    leaveEnvironment();
}

bool QQmlJSImportVisitor::visit(UiInlineComponent *component)
{
    if (!m_inlineComponentName.isNull()) {
        m_logger->logWarning(u"Nested inline components are not supported"_qs, Log_Syntax,
                             component->firstSourceLocation());
        return true;
    }

    m_nextIsInlineComponent = true;
    m_inlineComponentName = component->name;
    return true;
}

void QQmlJSImportVisitor::endVisit(UiInlineComponent *)
{
    m_inlineComponentName = QStringView();
    Q_ASSERT(!m_nextIsInlineComponent);
}

bool QQmlJSImportVisitor::visit(UiPublicMember *publicMember)
{
    switch (publicMember->type) {
    case UiPublicMember::Signal: {
        UiParameterList *param = publicMember->parameters;
        QQmlJSMetaMethod method;
        method.setMethodType(QQmlJSMetaMethod::Signal);
        method.setMethodName(publicMember->name.toString());
        while (param) {
            method.addParameter(param->name.toString(), param->type->name.toString());
            param = param->next;
        }
        m_currentScope->addOwnMethod(method);
        break;
    }
    case UiPublicMember::Property: {
        QString typeName = publicMember->memberType
                ? publicMember->memberType->name.toString()
                : QString();
        QString aliasExpr;
        const bool isAlias = (typeName == u"alias"_qs);
        if (isAlias) {
            typeName.clear(); // type name is useless for alias here, so keep it empty
            const auto expression = cast<ExpressionStatement *>(publicMember->statement);
            auto node = expression->expression;
            auto fex = cast<FieldMemberExpression *>(node);
            while (fex) {
                node = fex->base;
                aliasExpr.prepend(u'.' + fex->name.toString());
                fex = cast<FieldMemberExpression *>(node);
            }

            if (const auto idExpression = cast<IdentifierExpression *>(node)) {
                aliasExpr.prepend(idExpression->name.toString());
            } else {
                m_logger->logWarning(QStringLiteral("Invalid alias expression. Only IDs and field "
                                                    "member expressions can be aliased."),
                                     Log_Alias, expression->firstSourceLocation());
            }
        } else {
            const auto name = publicMember->memberType->name.toString();
            if (m_rootScopeImports.contains(name) && !m_rootScopeImports[name].isNull()) {
                if (m_importTypeLocationMap.contains(name))
                    m_usedTypes.insert(name);
            }
        }
        QQmlJSMetaProperty prop;
        prop.setPropertyName(publicMember->name.toString());
        prop.setIsList(publicMember->typeModifier == QLatin1String("list"));
        prop.setIsWritable(!publicMember->isReadonlyMember);
        prop.setAliasExpression(aliasExpr);
        const auto type = isAlias ? QQmlJSScope::ConstPtr() : m_rootScopeImports.value(typeName);
        if (type) {
            prop.setType(type);
            const QString internalName = type->internalName();
            prop.setTypeName(internalName.isEmpty() ? typeName : internalName);
        } else if (!isAlias) {
            m_pendingPropertyTypes << PendingPropertyType { m_currentScope, prop.propertyName(),
                                                            publicMember->firstSourceLocation() };
            prop.setTypeName(typeName);
        }
        prop.setAnnotations(parseAnnotations(publicMember->annotations));
        if (publicMember->isDefaultMember)
            m_currentScope->setOwnDefaultPropertyName(prop.propertyName());
        prop.setIndex(m_currentScope->ownProperties().size());
        m_currentScope->insertPropertyIdentifier(prop);
        if (publicMember->isRequired)
            m_currentScope->setPropertyLocallyRequired(prop.propertyName(), true);

        parseLiteralBinding(publicMember->name.toString(), publicMember->statement);

        break;
    }
    }

    return true;
}

bool QQmlJSImportVisitor::visit(UiRequired *required)
{
    const QString name = required->name.toString();

    m_requiredProperties << RequiredProperty { m_currentScope, name,
                                               required->firstSourceLocation() };

    m_currentScope->setPropertyLocallyRequired(name, true);
    return true;
}

void QQmlJSImportVisitor::visitFunctionExpressionHelper(QQmlJS::AST::FunctionExpression *fexpr)
{
    using namespace QQmlJS::AST;
    auto name = fexpr->name.toString();
    if (!name.isEmpty()) {
        QQmlJSMetaMethod method(name);
        method.setMethodType(QQmlJSMetaMethod::Method);

        if (!m_pendingMethodAnnotations.isEmpty()) {
            method.setAnnotations(m_pendingMethodAnnotations);
            m_pendingMethodAnnotations.clear();
        }

        if (const auto *formals = fexpr->formals) {
            const auto parameters = formals->formals();
            for (const auto &parameter : parameters) {
                const QString type = parameter.typeName();
                method.addParameter(parameter.id,
                                    type.isEmpty() ? QStringLiteral("var") : type);
            }
        }
        method.setReturnTypeName(fexpr->typeAnnotation
                                 ? fexpr->typeAnnotation->type->toString()
                                 : QStringLiteral("var"));
        m_currentScope->addOwnMethod(method);

        if (m_currentScope->scopeType() != QQmlJSScope::QMLScope) {
            m_currentScope->insertJSIdentifier(
                        name, {
                            QQmlJSScope::JavaScriptIdentifier::LexicalScoped,
                            fexpr->firstSourceLocation()
                        });
        }
        enterEnvironment(QQmlJSScope::JSFunctionScope, name, fexpr->firstSourceLocation());
    } else {
        enterEnvironment(QQmlJSScope::JSFunctionScope, QStringLiteral("<anon>"),
                         fexpr->firstSourceLocation());
    }
}

bool QQmlJSImportVisitor::visit(QQmlJS::AST::FunctionExpression *fexpr)
{
    visitFunctionExpressionHelper(fexpr);
    return true;
}

void QQmlJSImportVisitor::endVisit(QQmlJS::AST::FunctionExpression *)
{
    leaveEnvironment();
}

bool QQmlJSImportVisitor::visit(QQmlJS::AST::UiSourceElement *srcElement)
{
    m_pendingMethodAnnotations = parseAnnotations(srcElement->annotations);
    return true;
}

bool QQmlJSImportVisitor::visit(QQmlJS::AST::FunctionDeclaration *fdecl)
{
    m_logger->logWarning(u"Declared function \"%1\""_qs.arg(fdecl->name), Log_ControlsSanity,
                         fdecl->firstSourceLocation());
    visitFunctionExpressionHelper(fdecl);
    return true;
}

void QQmlJSImportVisitor::endVisit(QQmlJS::AST::FunctionDeclaration *)
{
    leaveEnvironment();
}

bool QQmlJSImportVisitor::visit(QQmlJS::AST::ClassExpression *ast)
{
    QQmlJSMetaProperty prop;
    prop.setPropertyName(ast->name.toString());
    m_currentScope->addOwnProperty(prop);
    enterEnvironment(QQmlJSScope::JSFunctionScope, ast->name.toString(),
                     ast->firstSourceLocation());
    return true;
}

void QQmlJSImportVisitor::endVisit(QQmlJS::AST::ClassExpression *)
{
    leaveEnvironment();
}

void QQmlJSImportVisitor::parseLiteralBinding(const QString name,
                                              const QQmlJS::AST::Statement *statement)
{
    const auto *exprStatement = cast<const ExpressionStatement *>(statement);

    if (exprStatement == nullptr)
        return;

    QQmlJSMetaPropertyBinding binding;

    // TODO: The literal values are not used yet but may be used later to further validate bindings
    binding.setLiteralValue(
            u""_qs); // If no literal value can be provided, use empty string as a place holder

    QString literalType;

    switch (exprStatement->expression->kind) {
    case Node::Kind_TrueLiteral:
    case Node::Kind_FalseLiteral:
        literalType = u"bool"_qs;
        break;
    case Node::Kind_NullExpression:
        literalType = u"var"_qs;
        break;
    case Node::Kind_NumericLiteral:
        literalType = u"double"_qs;
        binding.setLiteralValue(cast<NumericLiteral *>(exprStatement->expression)->value);
        break;
    case Node::Kind_StringLiteral:
        literalType = u"string"_qs;
        binding.setLiteralValue(cast<StringLiteral *>(exprStatement->expression)->value.toString());
        break;
    case Node::Kind_RegExpLiteral:
        literalType = u"$anonymous$.QRegularExpression"_qs;
        binding.setLiteralValue(
                cast<RegExpLiteral *>(exprStatement->expression)->pattern.toString());
        break;
    default:
        return;
    }

    if (!m_rootScopeImports.contains(literalType))
        return;

    binding.setValue(m_rootScopeImports[literalType]);
    binding.setValueTypeName(literalType);
    binding.setPropertyName(name);
    binding.setSourceLocation(exprStatement->expression->firstSourceLocation());

    m_currentScope->addOwnPropertyBinding(binding);

    m_literalScopesToCheck << m_currentScope;
}

bool QQmlJSImportVisitor::visit(UiScriptBinding *scriptBinding)
{
    m_savedBindingOuterScope = m_currentScope;
    const auto id = scriptBinding->qualifiedId;
    const auto *statement = cast<ExpressionStatement *>(scriptBinding->statement);
    if (!id->next && id->name == QLatin1String("id")) {
        const QString name = [&]() {
            if (const auto *idExpression = cast<IdentifierExpression *>(statement->expression))
                return idExpression->name.toString();
            else if (const auto *idString = cast<StringLiteral *>(statement->expression)) {
                m_logger->logInfo(u"ids do not need quotation marks"_qs, Log_Syntax, idString->firstSourceLocation());
                return idString->value.toString();
            }
            m_logger->logWarning(u"Failed to parse id"_qs, Log_Syntax, statement->expression->firstSourceLocation());
            return QString();

        }();
        m_scopesById.insert(name, m_currentScope);
        return true;
    }


    auto group = id;
    for (; group->next; group = group->next) {
        const QString name = group->name.toString();
        if (name.isEmpty())
            break;

        enterEnvironmentNonUnique(name.front().isUpper() ? QQmlJSScope::AttachedPropertyScope
                                                         : QQmlJSScope::GroupedPropertyScope,
                                  name, group->firstSourceLocation());
    }

    auto name = group->name;

    if (id && id->name.toString() == u"anchors")
        m_logger->logWarning(u"Using anchors here"_qs, Log_ControlsSanity,
                             scriptBinding->firstSourceLocation());

    const QString signal = signalName(name);

    if (signal.isEmpty()) {
        m_propertyBindings[m_currentScope].append(
                { m_savedBindingOuterScope, group->firstSourceLocation(), name.toString() });
        parseLiteralBinding(name.toString(), scriptBinding->statement);
    } else {
        const auto statement = scriptBinding->statement;
        QStringList signalParameters;

        if (ExpressionStatement *expr = cast<ExpressionStatement *>(statement)) {
            if (FunctionExpression *func = expr->expression->asFunctionDefinition()) {
                for (FormalParameterList *formal = func->formals; formal; formal = formal->next)
                    signalParameters << formal->element->bindingIdentifier.toString();
            }
        }

        m_logger->logWarning(u"Declared signal handler \"%1\""_qs.arg(name), Log_ControlsSanity,
                             scriptBinding->firstSourceLocation());

        m_signals[m_currentScope].append({ m_savedBindingOuterScope, group->firstSourceLocation(),
                                           qMakePair(name.toString(), signalParameters) });

        QQmlJSMetaMethod scopeSignal;
        for (QQmlJSScope::ConstPtr qmlScope = m_savedBindingOuterScope;
             qmlScope; qmlScope = qmlScope->baseType()) {
            const auto methods = qmlScope->ownMethods();
            const auto methodsRange = methods.equal_range(signal);
            for (auto method = methodsRange.first; method != methodsRange.second; ++method) {
                if (method->methodType() != QQmlJSMetaMethod::Signal)
                    continue;
                scopeSignal = *method;
                break;
            }
        }

        const auto firstSourceLocation = statement->firstSourceLocation();
        bool hasMultilineStatementBody =
                statement->lastSourceLocation().startLine > firstSourceLocation.startLine;
        m_pendingSignalHandler = firstSourceLocation;
        m_signalHandlers.insert(firstSourceLocation,
                                { scopeSignal.parameterNames(), hasMultilineStatementBody });
    }

    // Leave any group/attached scopes so that the binding scope doesn't see its properties.
    while (m_currentScope->scopeType() == QQmlJSScope::GroupedPropertyScope
           || m_currentScope->scopeType() == QQmlJSScope::AttachedPropertyScope) {
        leaveEnvironment();
    }

    if (!statement || !statement->expression->asFunctionDefinition()) {
        enterEnvironment(QQmlJSScope::JSFunctionScope, QStringLiteral("binding"),
                         scriptBinding->statement->firstSourceLocation());
    }

    return true;
}

void QQmlJSImportVisitor::endVisit(UiScriptBinding *)
{
    if (m_savedBindingOuterScope) {
        m_currentScope = m_savedBindingOuterScope;
        m_savedBindingOuterScope = {};
    }
}

bool QQmlJSImportVisitor::visit(UiArrayBinding *arrayBinding)
{
    QString name;
    for (auto id = arrayBinding->qualifiedId; id; id = id->next)
        name += id->name.toString() + QLatin1Char('.');

    name.chop(1);

    enterEnvironment(QQmlJSScope::QMLScope, name, arrayBinding->firstSourceLocation());
    m_currentScope->setIsArrayScope(true);

    // TODO: support group/attached properties

    return true;
}

void QQmlJSImportVisitor::endVisit(UiArrayBinding *)
{
    leaveEnvironment();

    // TODO: Actually generate a binding from the scope
}

bool QQmlJSImportVisitor::visit(QQmlJS::AST::UiEnumDeclaration *uied)
{
    QQmlJSMetaEnum qmlEnum(uied->name.toString());
    for (const auto *member = uied->members; member; member = member->next) {
        qmlEnum.addKey(member->member.toString());
        qmlEnum.addValue(int(member->value));
    }
    m_currentScope->addOwnEnumeration(qmlEnum);
    return true;
}

void QQmlJSImportVisitor::addImportWithLocation(const QString &name,
                                                const QQmlJS::SourceLocation &loc)
{
    if (m_importTypeLocationMap.contains(name)
        && m_importTypeLocationMap.values(name).contains(loc))
        return;

    m_importTypeLocationMap.insert(name, loc);
    m_importLocations.insert(loc);
}

bool QQmlJSImportVisitor::visit(QQmlJS::AST::UiImport *import)
{
    auto addImportLocation = [this, import](const QString &name) {
        addImportWithLocation(name, import->firstSourceLocation());
    };
    // construct path
    QString prefix = QLatin1String("");
    if (import->asToken.isValid()) {
        prefix += import->importId;
    }
    auto filename = import->fileName.toString();
    if (!filename.isEmpty()) {
        const QFileInfo file(filename);
        const QString absolute = file.isRelative()
                ? QDir(m_implicitImportDirectory).filePath(filename)
                : filename;

        if (absolute.startsWith(u':')) {
            if (m_importer->resourceFileMapper()) {
                if (m_importer->resourceFileMapper()->isFile(absolute.mid(1))) {
                    const auto entry = m_importer->resourceFileMapper()->entry(
                                QQmlJSResourceFileMapper::resourceFileFilter(absolute.mid(1)));
                    const auto scope = m_importer->importFile(entry.filePath);
                    const QString actualPrefix = prefix.isEmpty()
                            ? QFileInfo(entry.resourcePath).baseName()
                            : prefix;
                    m_rootScopeImports.insert(actualPrefix, scope);

                    addImportLocation(actualPrefix);
                } else {
                    const auto scopes = m_importer->importDirectory(absolute, prefix);
                    m_rootScopeImports.insert(scopes);
                    for (const QString &key : scopes.keys())
                        addImportLocation(key);
                }
            }

            processImportWarnings(QStringLiteral("URL \"%1\"").arg(absolute), import->firstSourceLocation());
            return true;
        }

        QFileInfo path(absolute);
        if (path.isDir()) {
            const auto scopes = m_importer->importDirectory(path.canonicalFilePath(), prefix);
            m_rootScopeImports.insert(scopes);
            for (const QString &key : scopes.keys())
                addImportLocation(key);
        } else if (path.isFile()) {
            const auto scope = m_importer->importFile(path.canonicalFilePath());
            const QString actualPrefix = prefix.isEmpty() ? scope->internalName() : prefix;
            m_rootScopeImports.insert(actualPrefix, scope);
            addImportLocation(actualPrefix);
        }

        processImportWarnings(QStringLiteral("path \"%1\"").arg(path.canonicalFilePath()), import->firstSourceLocation());
        return true;
    }

    QString path {};
    auto uri = import->importUri;
    while (uri) {
        path.append(uri->name);
        path.append(u'/');
        uri = uri->next;
    }
    path.chop(1);

    const auto imported = m_importer->importModule(
                path, prefix, import->version ? import->version->version : QTypeRevision());

    m_rootScopeImports.insert(imported);
    for (const QString &key : imported.keys())
        addImportLocation(key);

    processImportWarnings(QStringLiteral("module \"%1\"").arg(path), import->firstSourceLocation());
    return true;
}

void QQmlJSImportVisitor::throwRecursionDepthError()
{
    m_logger->logCritical(QStringLiteral("Maximum statement or expression depth exceeded"),
                          Log_RecursionDepthError);
}

bool QQmlJSImportVisitor::visit(QQmlJS::AST::ClassDeclaration *ast)
{
    enterEnvironment(QQmlJSScope::JSFunctionScope, ast->name.toString(),
                     ast->firstSourceLocation());
    return true;
}

void QQmlJSImportVisitor::endVisit(QQmlJS::AST::ClassDeclaration *)
{
    leaveEnvironment();
}

bool QQmlJSImportVisitor::visit(QQmlJS::AST::ForStatement *ast)
{
    enterEnvironment(QQmlJSScope::JSLexicalScope, QStringLiteral("forloop"),
                     ast->firstSourceLocation());
    return true;
}

void QQmlJSImportVisitor::endVisit(QQmlJS::AST::ForStatement *)
{
    leaveEnvironment();
}

bool QQmlJSImportVisitor::visit(QQmlJS::AST::ForEachStatement *ast)
{
    enterEnvironment(QQmlJSScope::JSLexicalScope, QStringLiteral("foreachloop"),
                     ast->firstSourceLocation());
    return true;
}

void QQmlJSImportVisitor::endVisit(QQmlJS::AST::ForEachStatement *)
{
    leaveEnvironment();
}

bool QQmlJSImportVisitor::visit(QQmlJS::AST::Block *ast)
{
    enterEnvironment(QQmlJSScope::JSLexicalScope, QStringLiteral("block"),
                     ast->firstSourceLocation());

    if (m_pendingSignalHandler.isValid())
        flushPendingSignalParameters();

    return true;
}

void QQmlJSImportVisitor::endVisit(QQmlJS::AST::Block *)
{
    leaveEnvironment();
}

bool QQmlJSImportVisitor::visit(QQmlJS::AST::CaseBlock *ast)
{
    enterEnvironment(QQmlJSScope::JSLexicalScope, QStringLiteral("case"),
                     ast->firstSourceLocation());
    return true;
}

void QQmlJSImportVisitor::endVisit(QQmlJS::AST::CaseBlock *)
{
    leaveEnvironment();
}

bool QQmlJSImportVisitor::visit(QQmlJS::AST::Catch *catchStatement)
{
    enterEnvironment(QQmlJSScope::JSLexicalScope, QStringLiteral("catch"),
                     catchStatement->firstSourceLocation());
    m_currentScope->insertJSIdentifier(
                catchStatement->patternElement->bindingIdentifier.toString(), {
                    QQmlJSScope::JavaScriptIdentifier::LexicalScoped,
                    catchStatement->patternElement->firstSourceLocation()
                });
    return true;
}

void QQmlJSImportVisitor::endVisit(QQmlJS::AST::Catch *)
{
    leaveEnvironment();
}

bool QQmlJSImportVisitor::visit(QQmlJS::AST::WithStatement *ast)
{
    enterEnvironment(QQmlJSScope::JSLexicalScope, QStringLiteral("with"),
                     ast->firstSourceLocation());

    m_logger->logWarning(
            QStringLiteral("with statements are strongly discouraged in QML "
                           "and might cause false positives when analysing unqualified "
                           "identifiers"),
            Log_WithStatement, ast->firstSourceLocation());

    return true;
}

void QQmlJSImportVisitor::endVisit(QQmlJS::AST::WithStatement *)
{
    leaveEnvironment();
}

bool QQmlJSImportVisitor::visit(QQmlJS::AST::VariableDeclarationList *vdl)
{
    while (vdl) {
        m_currentScope->insertJSIdentifier(
                    vdl->declaration->bindingIdentifier.toString(),
                    {
                        (vdl->declaration->scope == QQmlJS::AST::VariableScope::Var)
                            ? QQmlJSScope::JavaScriptIdentifier::FunctionScoped
                            : QQmlJSScope::JavaScriptIdentifier::LexicalScoped,
                        vdl->declaration->firstSourceLocation()
                    });
        vdl = vdl->next;
    }
    return true;
}

bool QQmlJSImportVisitor::visit(QQmlJS::AST::FormalParameterList *fpl)
{
    for (auto const &boundName : fpl->boundNames()) {
        m_currentScope->insertJSIdentifier(
                    boundName.id, {
                        QQmlJSScope::JavaScriptIdentifier::Parameter,
                        fpl->firstSourceLocation()
                    });
    }
    return true;
}

bool QQmlJSImportVisitor::visit(QQmlJS::AST::UiObjectBinding *uiob)
{
    // ... __styleData: QtObject {...}

    Q_ASSERT(uiob->qualifiedTypeNameId);
    QString name;
    for (auto id = uiob->qualifiedTypeNameId; id; id = id->next)
        name += id->name.toString() + QLatin1Char('.');

    name.chop(1);

    bool needsResolution = false;
    int scopesEnteredCounter = 0;
    for (auto group = uiob->qualifiedId; group->next; group = group->next) {
        const QString idName = group->name.toString();

        if (idName.isEmpty())
            break;

        const auto scopeKind = idName.front().isUpper() ? QQmlJSScope::AttachedPropertyScope
                                                        : QQmlJSScope::GroupedPropertyScope;
        bool exists = enterEnvironmentNonUnique(scopeKind, idName, group->firstSourceLocation());
        ++scopesEnteredCounter;
        needsResolution = needsResolution || !exists;
    }

    for (int i=0; i < scopesEnteredCounter; ++i) { // leave the scopes we entered again
        leaveEnvironment();
    }

    // recursively resolve types for current scope if new scopes are found
    if (needsResolution)
        QQmlJSScope::resolveTypes(m_currentScope, m_rootScopeImports, &m_usedTypes);

    enterEnvironment(QQmlJSScope::QMLScope, name,
                     uiob->qualifiedTypeNameId->identifierToken);
    QQmlJSScope::resolveTypes(m_currentScope, m_rootScopeImports, &m_usedTypes);

    m_qmlTypes.append(m_currentScope); // new QMLScope is created here, so add it
    m_objectBindingScopes << m_currentScope;
    return true;
}

void QQmlJSImportVisitor::endVisit(QQmlJS::AST::UiObjectBinding *uiob)
{
    QQmlJSScope::resolveTypes(m_currentScope, m_rootScopeImports, &m_usedTypes);
    // must be mutable, as we might mark it as implicitly wrapped in a component
    const QQmlJSScope::Ptr childScope = m_currentScope;
    leaveEnvironment();

    auto group = uiob->qualifiedId;
    int scopesEnteredCounter = 0;
    for (; group->next; group = group->next) {
        const QString idName = group->name.toString();

        if (idName.isEmpty())
            break;

        const auto scopeKind = idName.front().isUpper() ? QQmlJSScope::AttachedPropertyScope
                                                        : QQmlJSScope::GroupedPropertyScope;
        // definitely exists
        [[maybe_unused]] bool exists = enterEnvironmentNonUnique(scopeKind, idName, group->firstSourceLocation());
        Q_ASSERT(exists);
        scopesEnteredCounter++;
    }

    // on ending the visit to UiObjectBinding, set the property type to the
    // just-visited one if the property exists and this type is valid

    const QString propertyName = group->name.toString();

    if (m_scopesById.values().contains(childScope)
        && m_currentScope->isNameDeferred(propertyName)) {
        m_logger->logWarning(
                u"Assigning an id to an object bound to deferred property \"%1\" will make the property immediate"_qs
                        .arg(propertyName),
                Log_Property, uiob->firstSourceLocation());
    }

    if (m_currentScope->isInCustomParserParent()) {
        // These warnings do not apply for custom parsers and their children and need to be handled
        // on a case by case basis
    } else {
        m_pendingPropertyObjectBindings
                << PendingPropertyObjectBinding { m_currentScope, childScope, propertyName,
                                                  uiob->firstSourceLocation(), uiob->hasOnToken };
    }

    for (int i = 0; i < scopesEnteredCounter; ++i)
        leaveEnvironment();
}

bool QQmlJSImportVisitor::visit(ExportDeclaration *)
{
    Q_ASSERT(!m_exportedRootScope.isNull());
    Q_ASSERT(m_exportedRootScope != m_globalScope);
    Q_ASSERT(m_currentScope == m_globalScope);
    m_currentScope = m_exportedRootScope;
    return true;
}

void QQmlJSImportVisitor::endVisit(ExportDeclaration *)
{
    Q_ASSERT(!m_exportedRootScope.isNull());
    m_currentScope = m_exportedRootScope->parentScope();
    Q_ASSERT(m_currentScope == m_globalScope);
}

bool QQmlJSImportVisitor::visit(ESModule *module)
{
    enterEnvironment(QQmlJSScope::JSLexicalScope, QStringLiteral("module"),
                     module->firstSourceLocation());
    Q_ASSERT(m_exportedRootScope.isNull());
    m_exportedRootScope = m_currentScope;
    m_exportedRootScope->setIsScript(true);
    importBaseModules();
    leaveEnvironment();
    return true;
}

void QQmlJSImportVisitor::endVisit(ESModule *)
{
    QQmlJSScope::resolveTypes(m_exportedRootScope, m_rootScopeImports, &m_usedTypes);
}

bool QQmlJSImportVisitor::visit(Program *)
{
    Q_ASSERT(m_globalScope == m_currentScope);
    Q_ASSERT(m_exportedRootScope.isNull());
    m_exportedRootScope = m_currentScope;
    m_exportedRootScope->setIsScript(true);
    importBaseModules();
    return true;
}

void QQmlJSImportVisitor::endVisit(Program *)
{
    QQmlJSScope::resolveTypes(m_exportedRootScope, m_rootScopeImports, &m_usedTypes);
}

void QQmlJSImportVisitor::endVisit(QQmlJS::AST::FieldMemberExpression *fieldMember)
{
    const QString name = fieldMember->name.toString();
    if (m_importTypeLocationMap.contains(name)) {
        if (auto it = m_rootScopeImports.find(name); it != m_rootScopeImports.end() && !*(it))
            m_usedTypes.insert(name);
    }
}

bool QQmlJSImportVisitor::visit(QQmlJS::AST::IdentifierExpression *idexp)
{
    const QString name = idexp->name.toString();
    if (name.front().isUpper() && m_importTypeLocationMap.contains(name)) {
        m_usedTypes.insert(name);
    }

    return true;
}

bool QQmlJSImportVisitor::visit(QQmlJS::AST::PatternElement *element)
{
    // Handles variable declarations such as var x = [1,2,3].
    if (element->isVariableDeclaration()) {
        QQmlJS::AST::BoundNames names;
        element->boundNames(&names);
        for (const auto &name : names) {
            m_currentScope->insertJSIdentifier(
                    name.id,
                    { (element->scope == QQmlJS::AST::VariableScope::Var)
                              ? QQmlJSScope::JavaScriptIdentifier::FunctionScoped
                              : QQmlJSScope::JavaScriptIdentifier::LexicalScoped,
                      element->firstSourceLocation() });
        }
    }

    return true;
}

QT_END_NAMESPACE
