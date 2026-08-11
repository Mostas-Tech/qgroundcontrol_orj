#include "CustomPlugin.h"

#include <QtCore/QApplicationStatic>

#include "QGCLoggingCategory.h"

QGC_LOGGING_CATEGORY(CustomLog, "Custom.CustomPlugin")

Q_APPLICATION_STATIC(CustomPlugin, _customPluginInstance);

CustomPlugin::CustomPlugin(QObject* parent) : QGCCorePlugin(parent)
{
    qCDebug(CustomLog) << this;
}

QGCCorePlugin* CustomPlugin::instance()
{
    return _customPluginInstance();
}
