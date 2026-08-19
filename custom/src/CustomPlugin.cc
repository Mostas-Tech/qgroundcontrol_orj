#include "CustomPlugin.h"

#include <QtCore/QApplicationStatic>
#include <QtCore/QFile>
#include <QtQml/QQmlApplicationEngine>

#include "CustomOptions.h"
#include "MissionController.h"
#include "MissionManager/AgriculturalSprayComplexItem.h"
#include "PlanMasterController.h"
#include "QGCLoggingCategory.h"
#include "QmlObjectListModel.h"

QGC_LOGGING_CATEGORY(CustomLog, "qgc.custom.customplugin")

Q_APPLICATION_STATIC(CustomPlugin, _customPluginInstance);

CustomPlugin::CustomPlugin(QObject* parent) : QGCCorePlugin(parent)
    , _customOptions(new CustomOptions(this))
{
    qCDebug(CustomLog) << this;
}

QGCCorePlugin* CustomPlugin::instance()
{
    return _customPluginInstance();
}

QGCOptions* CustomPlugin::options()
{
    return _customOptions;
}

QQmlApplicationEngine* CustomPlugin::createQmlApplicationEngine(QObject* parent)
{
    _qmlEngine = QGCCorePlugin::createQmlApplicationEngine(parent);
    _urlInterceptor = new CustomOverrideInterceptor();
    _qmlEngine->addUrlInterceptor(_urlInterceptor);

    return _qmlEngine;
}

void CustomPlugin::destroyQmlApplicationEngine(QQmlApplicationEngine* qmlEngine)
{
    if (qmlEngine && (qmlEngine == _qmlEngine)) {
        qmlEngine->removeUrlInterceptor(_urlInterceptor);
        delete _urlInterceptor;
        _urlInterceptor = nullptr;
        _qmlEngine = nullptr;
    }

    QGCCorePlugin::destroyQmlApplicationEngine(qmlEngine);
}

QUrl CustomOverrideInterceptor::intercept(const QUrl& url, QQmlAbstractUrlInterceptor::DataType type)
{
    switch (type) {
    case QQmlAbstractUrlInterceptor::QmlFile:
    case QQmlAbstractUrlInterceptor::UrlString:
        if (url.scheme() == QStringLiteral("qrc")) {
            const QString overrideResource = QStringLiteral(":/Custom%1").arg(url.path());
            if (QFile::exists(overrideResource)) {
                QUrl result;
                result.setScheme(QStringLiteral("qrc"));
                result.setPath('/' + overrideResource.mid(2));
                return result;
            }
        }
        break;
    default:
        break;
    }

    return url;
}

QVariantList CustomPlugin::complexMissionItemNames(Vehicle* vehicle)
{
    if (!vehicle) {
        qCWarning(CustomLog) << "Cannot build complex mission item names without a vehicle";
        return {};
    }

    QVariantList items = QGCCorePlugin::complexMissionItemNames(vehicle);
    QVariantMap sprayEntry;
    sprayEntry[QStringLiteral("canonicalName")] = QString::fromLatin1(AgriculturalSprayComplexItem::canonicalName);
    sprayEntry[QStringLiteral("translatedName")] =
        AgriculturalSprayComplexItem::tr(AgriculturalSprayComplexItem::canonicalName);
    items.append(sprayEntry);
    return items;
}

bool CustomPlugin::canCreateComplexMissionItem(const QString& complexItemType,
                                               const PlanMasterController* masterController,
                                               const QmlObjectListModel* targetVisualItems, QString& errorMessage) const
{
    errorMessage.clear();

    if (!masterController) {
        errorMessage = tr("The complex mission item cannot be added because the target plan is unavailable.");
        qCWarning(CustomLog) << errorMessage << "type:" << complexItemType;
        return false;
    }
    if (!targetVisualItems) {
        errorMessage = tr("The complex mission item cannot be added because the target mission is unavailable.");
        qCWarning(CustomLog) << errorMessage << "type:" << complexItemType;
        return false;
    }

    const bool isAgriculturalSpray =
        complexItemType == QLatin1String(AgriculturalSprayComplexItem::canonicalName) ||
        complexItemType == QLatin1String(AgriculturalSprayComplexItem::jsonComplexItemTypeValue);
    if (!isAgriculturalSpray) {
        return QGCCorePlugin::canCreateComplexMissionItem(complexItemType, masterController, targetVisualItems,
                                                          errorMessage);
    }

    if (!QGCCorePlugin::canCreateComplexMissionItem(complexItemType, masterController, targetVisualItems,
                                                    errorMessage)) {
        return false;
    }

    for (int index = 0; index < targetVisualItems->count(); ++index) {
        AgriculturalSprayComplexItem* const sprayItem = targetVisualItems->value<AgriculturalSprayComplexItem*>(index);
        if (sprayItem) {
            errorMessage = tr("Only one Agricultural Spray item can be added to a plan.");
            return false;
        }
    }

    return true;
}

ComplexMissionItem* CustomPlugin::createComplexMissionItem(const QString& complexItemType,
                                                           PlanMasterController* masterController, bool flyView,
                                                           const QString& kmlOrShpFile)
{
    if (!masterController) {
        qCWarning(CustomLog) << "Cannot create a complex mission item without a plan master controller"
                             << "type:" << complexItemType;
        return nullptr;
    }

    if (complexItemType == QLatin1String(AgriculturalSprayComplexItem::canonicalName) ||
        complexItemType == QLatin1String(AgriculturalSprayComplexItem::jsonComplexItemTypeValue)) {
        return new AgriculturalSprayComplexItem(masterController, flyView);
    }

    return QGCCorePlugin::createComplexMissionItem(complexItemType, masterController, flyView, kmlOrShpFile);
}

void CustomPlugin::postLoadFromJson(PlanMasterController* controller, QJsonObject& json)
{
    Q_UNUSED(json);

    if (!controller) {
        qCWarning(CustomLog) << "Cannot refresh loaded Agricultural Spray items without a plan master controller";
        return;
    }

    MissionController* const missionController = controller->missionController();
    if (!missionController) {
        qCWarning(CustomLog) << "Cannot refresh loaded Agricultural Spray items without a mission controller";
        return;
    }

    QmlObjectListModel* const visualItems = missionController->visualItems();
    if (!visualItems) {
        qCWarning(CustomLog) << "Cannot refresh loaded Agricultural Spray items without a mission model";
        return;
    }

    for (int index = 0; index < visualItems->count(); ++index) {
        AgriculturalSprayComplexItem* const sprayItem = visualItems->value<AgriculturalSprayComplexItem*>(index);
        if (sprayItem && sprayItem->masterController() == controller) {
            sprayItem->refreshAfterLoad();
        }
    }
}
