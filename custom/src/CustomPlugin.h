#pragma once

#include "QGCCorePlugin.h"

Q_DECLARE_LOGGING_CATEGORY(CustomLog)

/// Company custom build plugin. Currently a pass-through with no overrides,
/// so the application looks and behaves identical to stock QGroundControl.
class CustomPlugin : public QGCCorePlugin
{
    Q_OBJECT

public:
    explicit CustomPlugin(QObject* parent = nullptr);

    static QGCCorePlugin* instance();
};
