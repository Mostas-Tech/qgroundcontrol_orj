pragma ComponentBehavior: Bound

import QtQuick
import QtLocation
import QtPositioning

import QGroundControl
import QGroundControl.Controls

Item {
    id: _root

    required property var map
    required property var vehicle
    required property bool interactive

    readonly property var _missionItem:  object
    readonly property var _routeSegments: _buildRouteSegments()
    readonly property var _entryCoordinate: _missionItem ? _missionItem.entryCoordinate : QtPositioning.coordinate()
    readonly property var _exitCoordinate:  _missionItem ? _missionItem.exitCoordinate : QtPositioning.coordinate()
    readonly property color sprayLegColor: qgcPal.colorGreen
    readonly property color transitSegmentColor: qgcPal.colorOrange
    readonly property real sprayLegWidthMultiplier: 0.5
    readonly property real transitSegmentWidthMultiplier: 0.4
    readonly property real sprayLegLineWidth: ScreenTools.defaultFontPixelWidth * sprayLegWidthMultiplier
    readonly property real transitSegmentLineWidth: ScreenTools.defaultFontPixelWidth * transitSegmentWidthMultiplier
    property var _routeGroup

    objectName: "agriculturalSprayMapVisual"

    function _buildRouteSegments() {
        if (!_missionItem || !_missionItem.specifiesCoordinate) {
            return []
        }

        const coordinates = _missionItem.routeCoordinates
        const segmentTypes = _missionItem.routeSegmentTypes
        if (!coordinates || !segmentTypes || coordinates.length < 2 || segmentTypes.length !== coordinates.length - 1) {
            return []
        }

        const segments = []
        for (let index = 0; index < segmentTypes.length; index++) {
            const fromCoordinate = coordinates[index]
            const toCoordinate = coordinates[index + 1]
            if (!fromCoordinate || !toCoordinate || !fromCoordinate.isValid || !toCoordinate.isValid) {
                return []
            }

            segments.push({
                fromCoordinate: fromCoordinate,
                toCoordinate: toCoordinate,
                sprayLeg: segmentTypes[index] === 0
            })
        }
        return segments
    }

    QGCPalette {
        id: qgcPal
        colorGroupEnabled: _root.enabled
    }

    QGCDynamicObjectManager {
        id: objectManager
    }

    Component {
        id: routeViewComponent

        MapItemGroup {
            objectName: "agriculturalSprayRoute"

            MapItemView {
                model: _root._routeSegments

                delegate: MapPolyline {
                    required property var modelData
                    required property int index

                    objectName: "agriculturalSprayRouteSegment_" + (modelData.sprayLeg ? "spray_" : "transit_") + index
                    path:       [modelData.fromCoordinate, modelData.toCoordinate]
                    line.color: modelData.sprayLeg ? _root.sprayLegColor : _root.transitSegmentColor
                    line.width: modelData.sprayLeg ? _root.sprayLegLineWidth : _root.transitSegmentLineWidth
                    opacity:    _root.opacity
                    z:          QGroundControl.zOrderWaypointLines
                }
            }
        }
    }

    Component {
        id: entryLabelComponent

        MapQuickItem {
            objectName:     "agriculturalSprayEntryLabel"
            coordinate:     _root._entryCoordinate
            visible:        _root._routeSegments.length > 0 && coordinate.isValid
            opacity:        _root.opacity
            z:              QGroundControl.zOrderMapItems
            anchorPoint.x:  sourceItem.width / 2
            anchorPoint.y:  sourceItem.height + (ScreenTools.defaultFontPixelHeight * 0.25)

            sourceItem: QGCMapLabel {
                map:  _root.map
                text: qsTr("Entry")
            }
        }
    }

    Component {
        id: exitLabelComponent

        MapQuickItem {
            objectName:     "agriculturalSprayExitLabel"
            coordinate:     _root._exitCoordinate
            visible:        _root._routeSegments.length > 0 && coordinate.isValid
            opacity:        _root.opacity
            z:              QGroundControl.zOrderMapItems
            anchorPoint.x:  sourceItem.width / 2
            anchorPoint.y:  -(ScreenTools.defaultFontPixelHeight * 0.25)

            sourceItem: QGCMapLabel {
                map:  _root.map
                text: qsTr("Exit")
            }
        }
    }

    Component.onCompleted: {
        if (!map || !map.addMapItemGroup) {
            return
        }
        _routeGroup = routeViewComponent.createObject(map)
        map.addMapItemGroup(_routeGroup)
        objectManager.createObjects([entryLabelComponent, exitLabelComponent], map, true)
    }

    Component.onDestruction: {
        if (_routeGroup) {
            map.removeMapItemGroup(_routeGroup)
            _routeGroup.destroy()
            _routeGroup = null
        }
        objectManager.destroyObjects()
    }
}
