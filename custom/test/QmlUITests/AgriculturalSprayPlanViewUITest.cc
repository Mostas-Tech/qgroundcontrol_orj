#include "AgriculturalSprayPlanViewUITest.h"

#include <QtQml/QQmlApplicationEngine>
#include <QtQml/QQmlComponent>
#include <QtTest/QTest>

#include "QGCCorePlugin.h"

UT_REGISTER_TEST(AgriculturalSprayPlanViewUITest, TestLabel::Integration, TestLabel::MissionManager)

void AgriculturalSprayPlanViewUITest::_qmlComponentsLoad()
{
    QGCCorePlugin::instance()->init();
    _engine = QGCCorePlugin::instance()->createQmlApplicationEngine(this);
    QVERIFY(_engine);

    const QList<QUrl> componentUrls = {
        QUrl(QStringLiteral("qrc:/qml/Custom/Plan/AgriculturalSprayEditor.qml")),
        QUrl(QStringLiteral("qrc:/qml/Custom/Plan/AgriculturalSprayMapVisual.qml")),
    };
    for (const QUrl& url : componentUrls) {
        QQmlComponent component(_engine, url, QQmlComponent::PreferSynchronous);
        QVERIFY2(component.isReady(),
                 qPrintable(QStringLiteral("%1: %2").arg(url.toString(), component.errorString())));
    }

    // Pattern-menu selection, editor interaction, fence edits, and route rendering
    // remain manual-only until those flows expose deterministic QML test hooks.
}
