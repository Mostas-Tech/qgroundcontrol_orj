pragma Singleton

import QtQuick

import QGroundControl

/// Single source of truth for the Plan View map editing layers. Used by the
/// layer switcher (PlanView), the plan tree view group headers, and the map
/// visual bindings.
QtObject {
    readonly property int layerMission: 1
    readonly property int layerFence:   2
    readonly property int layerRally:   3

    readonly property var layerInfos: [
        { layer: layerMission, nodeType: "missionGroup", icon: "/res/waypoint.svg",   name: qsTr("Mission") },
        { layer: layerFence,   nodeType: "fenceGroup",   icon: "/res/GeoFence.svg",   name: qsTr("GeoFence") }
    ].concat(QGroundControl.corePlugin.options.showRallyLayer && QGroundControl.corePlugin.options.showRallyPoints ? [
        { layer: layerRally,   nodeType: "rallyGroup",   icon: "/res/RallyPoint.svg", name: qsTr("Rally Points") }
    ] : [])

    function infoForNodeType(nodeType) {
        return layerInfos.find(l => l.nodeType === nodeType) ?? null
    }

    function planTreeNodeVisible(nodeType, nodeObject) {
        return (nodeType !== "planFileGroup" && nodeType !== "planFileInfo"
                || QGroundControl.corePlugin.options.showPlanInfo)
            && (nodeType !== "defaultsGroup" && nodeType !== "defaultsInfo"
                || QGroundControl.corePlugin.options.showPlanDefaults)
            && (nodeType !== "missionItem" || QGroundControl.corePlugin.options.showInitialCameraSettings
                || !nodeObject || nodeObject.sequenceNumber !== 0)
            && (nodeType !== "rallyGroup" && nodeType !== "rallyHeader" && nodeType !== "rallyItem"
                || QGroundControl.corePlugin.options.showRallyPoints)
            && (nodeType !== "transformGroup" && nodeType !== "transformEditor"
                || QGroundControl.corePlugin.options.showTransform)
    }
}
