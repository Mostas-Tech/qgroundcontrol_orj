#pragma once

#include <QtQml/QQmlAbstractUrlInterceptor>

#include "QGCCorePlugin.h"

Q_DECLARE_LOGGING_CATEGORY(CustomLog)

class QQmlApplicationEngine;

/// Company custom build plugin.
class CustomPlugin : public QGCCorePlugin
{
    Q_OBJECT

public:
    explicit CustomPlugin(QObject* parent = nullptr);

    static QGCCorePlugin* instance();

    QGCOptions* options() override;
    QQmlApplicationEngine* createQmlApplicationEngine(QObject* parent) final;
    void destroyQmlApplicationEngine(QQmlApplicationEngine* qmlEngine) final;
    QVariantList complexMissionItemNames(Vehicle* vehicle) override;
    bool canCreateComplexMissionItem(const QString& complexItemType, const PlanMasterController* masterController,
                                     const QmlObjectListModel* targetVisualItems, QString& errorMessage) const override;
    ComplexMissionItem* createComplexMissionItem(const QString& complexItemType, PlanMasterController* masterController,
                                                 bool flyView, const QString& kmlOrShpFile = QString()) override;
    void postLoadFromJson(PlanMasterController* controller, QJsonObject& json) override;

private:
    QGCOptions* _customOptions = nullptr;
    QQmlApplicationEngine* _qmlEngine = nullptr;
    class CustomOverrideInterceptor* _urlInterceptor = nullptr;
};

class CustomOverrideInterceptor : public QQmlAbstractUrlInterceptor
{
public:
    QUrl intercept(const QUrl& url, QQmlAbstractUrlInterceptor::DataType type) final;
};
