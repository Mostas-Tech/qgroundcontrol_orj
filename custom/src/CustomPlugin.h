#pragma once

#include "QGCCorePlugin.h"

Q_DECLARE_LOGGING_CATEGORY(CustomLog)

/// Company custom build plugin.
class CustomPlugin : public QGCCorePlugin
{
    Q_OBJECT

public:
    explicit CustomPlugin(QObject* parent = nullptr);

    static QGCCorePlugin* instance();

    QVariantList complexMissionItemNames(Vehicle* vehicle) override;
    bool canCreateComplexMissionItem(const QString& complexItemType, const PlanMasterController* masterController,
                                     const QmlObjectListModel* targetVisualItems, QString& errorMessage) const override;
    ComplexMissionItem* createComplexMissionItem(const QString& complexItemType, PlanMasterController* masterController,
                                                 bool flyView, const QString& kmlOrShpFile = QString()) override;
    void postLoadFromJson(PlanMasterController* controller, QJsonObject& json) override;
};
