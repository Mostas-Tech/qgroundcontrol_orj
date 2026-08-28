#include "AgriculturalSprayPlanViewUITest.h"

#include <QtCore/QScopedPointer>
#include <QtCore/QRegularExpression>
#include <QtGui/QColor>
#include <QtPositioning/QGeoCoordinate>
#include <QtQml/QQmlApplicationEngine>
#include <QtQml/QQmlComponent>
#include <QtQml/QQmlContext>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>
#include <QtTest/QTest>

#include "GeoFenceController.h"
#include "MultiVehicleManager.h"
#include "PlanMasterController.h"
#include "QGCFenceCircle.h"
#include "QGCCorePlugin.h"
#include "QGCPalette.h"
#include "QmlObjectListModel.h"

UT_REGISTER_TEST(AgriculturalSprayPlanViewUITest, TestLabel::Integration, TestLabel::MissionManager)

namespace {
constexpr auto kPlanEditLayersHarnessUrl = "qrc:/unittest/PlanEditLayersHarness.qml";
constexpr auto kGeoFenceEditorHarnessUrl = "qrc:/unittest/GeoFenceEditorHarness.qml";
constexpr auto kTestMapUrl = "qrc:/unittest/TestMap.qml";

const char* kPlanEditLayersHarness = R"(
import QtQml

import QGroundControl.PlanView

QtObject {
    property var initialCameraSettings: ({ sequenceNumber: 0 })
    property var regularMissionItem: ({ sequenceNumber: 1 })

    readonly property var layerSwitcherChoices:
        PlanEditLayers.layerInfos.filter(layer => layer.layer !== PlanEditLayers.layerMission)
    readonly property var layerInfos: PlanEditLayers.layerInfos
    readonly property bool defaultsVisible: PlanEditLayers.planTreeNodeVisible("defaultsGroup", null)
    readonly property bool initialCameraSettingsVisible:
        PlanEditLayers.planTreeNodeVisible("missionItem", initialCameraSettings)
    readonly property bool planInfoVisible: PlanEditLayers.planTreeNodeVisible("planFileGroup", null)
    readonly property bool rallyGroupVisible: PlanEditLayers.planTreeNodeVisible("rallyGroup", null)
    readonly property bool rallyHeaderVisible: PlanEditLayers.planTreeNodeVisible("rallyHeader", null)
    readonly property bool rallyItemVisible: PlanEditLayers.planTreeNodeVisible("rallyItem", null)
    readonly property var rallyLayer: PlanEditLayers.infoForNodeType("rallyGroup")
    readonly property bool regularMissionItemVisible:
        PlanEditLayers.planTreeNodeVisible("missionItem", regularMissionItem)
    readonly property bool transformEditorVisible: PlanEditLayers.planTreeNodeVisible("transformEditor", null)
    readonly property bool transformVisible: PlanEditLayers.planTreeNodeVisible("transformGroup", null)
}
)";

const char* kGeoFenceEditorHarness = R"(
import QtQuick
import QtQml

import QGroundControl
import QGroundControl.Controls

Item {
    id: root

    required property var flightMap
    required property var myGeoFenceController

    readonly property var editor: editorLoader.item

    QGCPalette {
        id: qgcPal
    }

    Loader {
        id: editorLoader
    }

    Component.onCompleted: {
        editorLoader.setSource("qrc:/qml/QGroundControl/PlanView/GeoFenceEditor.qml", {
            width: Qt.binding(() => root.width),
            flightMap: root.flightMap,
            myGeoFenceController: root.myGeoFenceController
        })
    }
}
)";

const char* kTestMap = R"(
import QtLocation

Map {
    plugin: Plugin {
        name: "QGroundControl"
    }
}
)";

bool _containsLayerNodeType(const QVariantList& layers, const QString& nodeType)
{
    for (const QVariant& layer : layers) {
        if (layer.toMap().value(QStringLiteral("nodeType")).toString() == nodeType) {
            return true;
        }
    }
    return false;
}
} // namespace

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

}

void AgriculturalSprayPlanViewUITest::_routeVisualStyleProperties()
{
    ignoreLogMessage("default", QtWarningMsg, QRegularExpression(QStringLiteral("^QFontDatabase: Cannot find font directory")));
    QGCCorePlugin::instance()->init();
    _engine = QGCCorePlugin::instance()->createQmlApplicationEngine(this);
    QVERIFY(_engine);

    QObject map;
    QObject vehicle;
    QObject missionItem;
    missionItem.setProperty("specifiesCoordinate", false);
    QQmlContext context(_engine->rootContext());
    context.setContextProperty(QStringLiteral("object"), &missionItem);

    QQmlComponent component(_engine, QUrl(QStringLiteral("qrc:/qml/Custom/Plan/AgriculturalSprayMapVisual.qml")),
                            QQmlComponent::PreferSynchronous);
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    const QVariantMap initialProperties = {
        {QStringLiteral("map"), QVariant::fromValue(&map)},
        {QStringLiteral("vehicle"), QVariant::fromValue(&vehicle)},
        {QStringLiteral("interactive"), false},
    };
    QScopedPointer<QObject> visual(component.createWithInitialProperties(initialProperties, &context));
    QVERIFY(visual);
    QVERIFY2(!component.isError(), qPrintable(component.errorString()));

    QGCPalette palette;
    QCOMPARE(visual->property("sprayLegColor").value<QColor>(), palette.colorGreen());
    QCOMPARE(visual->property("transitSegmentColor").value<QColor>(), palette.colorOrange());
    QCOMPARE(visual->property("nonSprayAreaColor").value<QColor>(), QColor(QStringLiteral("white")));
    QCOMPARE(visual->property("sprayLegWidthMultiplier").toDouble(), 0.5);
    QCOMPARE(visual->property("transitSegmentWidthMultiplier").toDouble(), 0.4);
}

void AgriculturalSprayPlanViewUITest::_customPlanTreeAndLayerData()
{
    QGCCorePlugin::instance()->init();
    _engine = QGCCorePlugin::instance()->createQmlApplicationEngine(this);
    QVERIFY(_engine);

    QQmlComponent layerHarnessComponent(_engine);
    layerHarnessComponent.setData(kPlanEditLayersHarness, QUrl(QString::fromLatin1(kPlanEditLayersHarnessUrl)));
    QVERIFY2(layerHarnessComponent.isReady(), qPrintable(layerHarnessComponent.errorString()));
    QScopedPointer<QObject> layerHarness(layerHarnessComponent.create());
    QVERIFY(layerHarness);

    QVERIFY(!layerHarness->property("planInfoVisible").toBool());
    QVERIFY(!layerHarness->property("defaultsVisible").toBool());
    QVERIFY(!layerHarness->property("initialCameraSettingsVisible").toBool());
    QVERIFY(layerHarness->property("regularMissionItemVisible").toBool());
    QVERIFY(!layerHarness->property("rallyGroupVisible").toBool());
    QVERIFY(!layerHarness->property("rallyHeaderVisible").toBool());
    QVERIFY(!layerHarness->property("rallyItemVisible").toBool());
    QVERIFY(!layerHarness->property("transformVisible").toBool());
    QVERIFY(!layerHarness->property("transformEditorVisible").toBool());

    const QVariantList layerInfos = layerHarness->property("layerInfos").toList();
    const QVariantList layerSwitcherChoices = layerHarness->property("layerSwitcherChoices").toList();
    QCOMPARE(layerInfos.count(), 2);
    QCOMPARE(layerSwitcherChoices.count(), 1);
    QVERIFY(!_containsLayerNodeType(layerInfos, QStringLiteral("rallyGroup")));
    QVERIFY(!_containsLayerNodeType(layerSwitcherChoices, QStringLiteral("rallyGroup")));
    QVERIFY(layerHarness->property("rallyLayer").isNull());
}

void AgriculturalSprayPlanViewUITest::_customGeoFenceVisualsAndCircleAction()
{
    QGCCorePlugin::instance()->init();
    MultiVehicleManager::instance()->init();
    _engine = QGCCorePlugin::instance()->createQmlApplicationEngine(this);
    QVERIFY(_engine);

    PlanMasterController planMasterController;
    planMasterController.setFlyView(false);
    planMasterController.start();
    GeoFenceController* const fenceController = planMasterController.geoFenceController();
    QVERIFY(fenceController);
    QCOMPARE(fenceController->circles()->count(), 0);

    QQuickWindow harnessWindow;
    harnessWindow.setGeometry(0, 0, 640, 480);

    QQmlComponent mapComponent(_engine);
    mapComponent.setData(kTestMap, QUrl(QString::fromLatin1(kTestMapUrl)));
    QVERIFY2(mapComponent.isReady(), qPrintable(mapComponent.errorString()));
    const QVariantMap mapProperties = {
        {QStringLiteral("width"), 640.0},
        {QStringLiteral("height"), 480.0},
    };
    QScopedPointer<QObject> map(mapComponent.createWithInitialProperties(mapProperties));
    QVERIFY2(map, qPrintable(mapComponent.errorString()));
    QQuickItem* const mapItem = qobject_cast<QQuickItem*>(map.data());
    QVERIFY(mapItem);
    mapItem->setParentItem(harnessWindow.contentItem());
    const QGeoCoordinate mapCenter(47.397742, 8.545594);
    QVERIFY(map->setProperty("center", QVariant::fromValue(mapCenter)));

    QQmlComponent visualsComponent(_engine, QUrl(QStringLiteral("qrc:/qml/QGroundControl/PlanView/GeoFenceMapVisuals.qml")),
                                   QQmlComponent::PreferSynchronous);
    QVERIFY2(visualsComponent.isReady(), qPrintable(visualsComponent.errorString()));
    const QVariantMap visualsProperties = {
        {QStringLiteral("map"), QVariant::fromValue(map.data())},
        {QStringLiteral("myGeoFenceController"), QVariant::fromValue(static_cast<QObject*>(fenceController))},
        {QStringLiteral("homePosition"), QVariant::fromValue(QGeoCoordinate())},
    };
    QScopedPointer<QObject> visuals(visualsComponent.createWithInitialProperties(visualsProperties));
    QVERIFY2(visuals, qPrintable(visualsComponent.errorString()));
    QQuickItem* const visualsItem = qobject_cast<QQuickItem*>(visuals.data());
    QVERIFY(visualsItem);
    visualsItem->setParentItem(mapItem);
    QCOMPARE(visuals->property("_borderColor").value<QColor>(), QColorConstants::Blue);

    QQmlComponent editorHarnessComponent(_engine);
    editorHarnessComponent.setData(kGeoFenceEditorHarness, QUrl(QString::fromLatin1(kGeoFenceEditorHarnessUrl)));
    QVERIFY2(editorHarnessComponent.isReady(), qPrintable(editorHarnessComponent.errorString()));
    const QVariantMap editorHarnessProperties = {
        {QStringLiteral("width"), 640.0},
        {QStringLiteral("myGeoFenceController"), QVariant::fromValue(static_cast<QObject*>(fenceController))},
        {QStringLiteral("flightMap"), QVariant::fromValue(map.data())},
    };
    QScopedPointer<QObject> editorHarness(editorHarnessComponent.createWithInitialProperties(editorHarnessProperties));
    QVERIFY2(editorHarness, qPrintable(editorHarnessComponent.errorString()));
    QQuickItem* const editorHarnessItem = qobject_cast<QQuickItem*>(editorHarness.data());
    QVERIFY(editorHarnessItem);
    editorHarnessItem->setParentItem(harnessWindow.contentItem());
    QTRY_VERIFY_WITH_TIMEOUT(editorHarness->property("editor").value<QObject*>(), TestTimeout::shortMs());
    QObject* const editor = editorHarness->property("editor").value<QObject*>();
    QVERIFY(editor);

    QObject* const addCircleButton = editor->findChild<QObject*>(QStringLiteral("planTree_geoFenceAddCircleButton"));
    QVERIFY(addCircleButton);
    QVERIFY(QMetaObject::invokeMethod(addCircleButton, "click"));
    QCOMPARE(fenceController->circles()->count(), 1);

    QGCFenceCircle* const circle = fenceController->circles()->value<QGCFenceCircle*>(0);
    QVERIFY(circle);
    QCOMPARE(circle->center(), mapCenter);
    QCOMPARE(circle->radius()->rawValue().toDouble(), 3.0);
    QVERIFY(!circle->inclusion());
}
