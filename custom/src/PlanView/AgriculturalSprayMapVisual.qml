pragma ComponentBehavior: Bound

import QtQuick
import QtLocation
import QtPositioning

import QGroundControl
import QGroundControl.Controls
import QGroundControl.FlightMap

Item {
    id: _root

    required property var map
    required property var vehicle
    required property bool interactive

    readonly property var _missionItem:  object
    readonly property var _fields:       _missionItem ? _missionItem.fields : null
    readonly property var _selectedField: _missionItem ? _missionItem.selectedField : null
    readonly property var _routeSegments: _buildRouteSegments()
    readonly property var _entryCoordinate: _missionItem ? _missionItem.entryCoordinate : QtPositioning.coordinate()
    readonly property var _exitCoordinate:  _missionItem ? _missionItem.exitCoordinate : QtPositioning.coordinate()
    readonly property bool _showDirectionSelection: interactive && _selectedField
                                                    && !_missionItem.sourcePolygonTraceMode
                                                    && _selectedField.polygon.count > 2
    readonly property bool _showMarginEdge: !!(_missionItem && _missionItem.boundaryMargin
                                               && _missionItem.boundaryMarginScope
                                               && _selectedField && _selectedField.polygon.count > 2
                                               && _selectedField.boundaryMargin.rawValue > 0
                                               && _selectedField.boundaryMarginScope.rawValue === 0)
    readonly property bool _showMarginSelection: interactive && _showMarginEdge
                                                  && !_missionItem.sourcePolygonTraceMode
    readonly property var _marginEdgeIndices: _selectedField && _selectedField.marginEdgeIndices
                                               ? _selectedField.marginEdgeIndices
                                               : []
    readonly property color sprayLegColor: qgcPal.colorGreen
    readonly property color transitSegmentColor: qgcPal.colorOrange
    readonly property color nonSprayAreaColor: "white"
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
                sprayLeg: segmentTypes[index] === 0,
                nonSpray: segmentTypes[index] === 2
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

            MapPolyline {
                objectName: "agriculturalSprayDirectionEdge"
                path:       [_root._missionItem.directionEdgeStart, _root._missionItem.directionEdgeEnd]
                line.color: qgcPal.colorBlue
                line.width: ScreenTools.defaultFontPixelWidth * 0.7
                visible:    _root._showDirectionSelection
                            && _root._missionItem.directionEdgeStart.isValid
                            && _root._missionItem.directionEdgeEnd.isValid
                opacity:    _root.opacity
                z:          QGroundControl.zOrderMapItems + 1
            }

            MapItemView {
                model: _root._marginEdgeIndices

                delegate: MapPolyline {
                    required property int modelData

                    readonly property var _coordinates: _root._selectedField.polygon.path

                    objectName: "agriculturalSprayMarginEdge_" + modelData
                    path:       _coordinates.length > modelData
                                ? [_coordinates[modelData], _coordinates[(modelData + 1) % _coordinates.length]]
                                : []
                    line.color: qgcPal.colorYellow
                    line.width: ScreenTools.defaultFontPixelWidth
                    visible:    _root._showMarginEdge && path.length === 2 && path[0].isValid && path[1].isValid
                    opacity:    _root.opacity
                    z:          QGroundControl.zOrderMapItems + 2
                }
            }

            MapLineArrow {
                objectName:    "agriculturalSprayDirectionArrow"
                fromCoord:     _root._missionItem.directionEdgeStart
                toCoord:       _root._missionItem.directionEdgeEnd
                arrowPosition: 3
                visible:       _root._showDirectionSelection
                               && fromCoord.isValid
                               && toCoord.isValid
                opacity:       _root.opacity
                z:             QGroundControl.zOrderMapItems + 2
            }

            MapItemView {
                model: _root._selectedField ? _root._selectedField.polygon.path : []

                delegate: MapQuickItem {
                    id: directionVertexMarker

                    required property var modelData
                    required property int index
                    readonly property bool selected: index === _root._selectedField.directionVertexIndex

                    objectName:    "agriculturalSprayDirectionVertex_" + index
                    coordinate:    modelData
                    visible:       _root._showDirectionSelection
                    opacity:       _root.opacity
                    z:             QGroundControl.zOrderMapItems + 3
                    anchorPoint.x: sourceItem.width / 2
                    anchorPoint.y: sourceItem.height / 2

                    sourceItem: Rectangle {
                        width:        directionVertexMarker.selected ? ScreenTools.defaultFontPixelHeight * 1.25
                                                                    : ScreenTools.defaultFontPixelHeight
                        height:       width
                        radius:       width / 2
                        color:        directionVertexMarker.selected ? qgcPal.colorGreen : qgcPal.mapWidgetBorderLight
                        border.color: qgcPal.text
                        border.width: Math.max(1, ScreenTools.defaultFontPixelWidth * 0.15)

                        MouseArea {
                            anchors.fill: parent
                            onClicked: _root._missionItem.setDirectionVertexIndex(directionVertexMarker.index)
                        }
                    }
                }
            }

            MapItemView {
                model: _root._selectedField ? _root._selectedField.polygon.path : []

                delegate: MapQuickItem {
                    id: marginEdgeMarker

                    required property var modelData
                    required property int index
                    readonly property var _coordinates: _root._selectedField.polygon.path
                    readonly property var _nextCoordinate: _coordinates.length > 0
                                                               ? _coordinates[(index + 1) % _coordinates.length]
                                                               : QtPositioning.coordinate()
                    readonly property bool selected: _root._marginEdgeIndices.indexOf(index) >= 0

                    objectName: "agriculturalSprayMarginEdgeMarker_" + index
                    coordinate: modelData && modelData.isValid && _nextCoordinate.isValid
                                    ? modelData.atDistanceAndAzimuth(modelData.distanceTo(_nextCoordinate) / 2,
                                                                     modelData.azimuthTo(_nextCoordinate))
                                    : QtPositioning.coordinate()
                    visible:    _root._showMarginSelection && coordinate.isValid
                    opacity:    _root.opacity
                    z:          QGroundControl.zOrderMapItems + 4
                    anchorPoint.x: sourceItem.width / 2
                    anchorPoint.y: sourceItem.height / 2

                    sourceItem: Item {
                        width:  ScreenTools.defaultFontPixelHeight * 2.25
                        height: width

                        Rectangle {
                            anchors.centerIn: parent
                            width:            marginEdgeMarker.selected ? ScreenTools.defaultFontPixelHeight
                                                                        : ScreenTools.defaultFontPixelHeight * 0.8
                            height:           width
                            radius:           width / 2
                            color:            marginEdgeMarker.selected ? qgcPal.colorYellow
                                                                        : qgcPal.mapWidgetBorderLight
                            border.color:     qgcPal.text
                            border.width:     Math.max(1, ScreenTools.defaultFontPixelWidth * 0.15)
                        }

                        MouseArea {
                            anchors.fill: parent
                            onClicked: _root._missionItem.toggleMarginEdgeIndex(marginEdgeMarker.index)
                        }
                    }
                }
            }

            MapItemView {
                model: _root._routeSegments

                delegate: MapPolyline {
                    required property var modelData
                    required property int index

                    objectName: "agriculturalSprayRouteSegment_"
                                + (modelData.nonSpray ? "nonSpray_" : (modelData.sprayLeg ? "spray_" : "transit_"))
                                + index
                    path:       [modelData.fromCoordinate, modelData.toCoordinate]
                    line.color: modelData.nonSpray ? _root.nonSprayAreaColor
                                                  : (modelData.sprayLeg ? _root.sprayLegColor
                                                                       : _root.transitSegmentColor)
                    line.width: modelData.sprayLeg ? _root.sprayLegLineWidth : _root.transitSegmentLineWidth
                    opacity:    _root.opacity
                    z:          QGroundControl.zOrderWaypointLines
                }
            }

            Repeater {
                model: _root._selectedField ? _root._selectedField.nonSprayPolygons : null

                QGCMapPolygonVisuals {
                    required property var object

                    mapControl:      _root.map
                    mapPolygon:      object
                    interactive:     _root.interactive && object.interactive
                    borderWidth:     Math.max(1, ScreenTools.defaultFontPixelWidth * 0.25)
                    borderColor:     _root.nonSprayAreaColor
                    interiorColor:   _root.nonSprayAreaColor
                    interiorOpacity: 0.2 * _root.opacity
                }
            }

            MapItemView {
                model: _root._routeSegments

                delegate: MapLineArrow {
                    required property var modelData
                    required property int index

                    objectName:    "agriculturalSprayRouteArrow_"
                                   + (modelData.nonSpray ? "nonSpray_"
                                                        : (modelData.sprayLeg ? "spray_" : "transit_"))
                                   + index
                    fromCoord:     modelData.fromCoordinate
                    toCoord:       modelData.toCoordinate
                    arrowPosition: 3
                    opacity:       _root.opacity
                    z:             QGroundControl.zOrderWaypointLines + 1
                }
            }
        }
    }

    Repeater {
        model: _root._fields

        delegate: Item {
            required property var modelData
            required property int index

            QGCMapPolygonVisuals {
                mapControl: _root.map
                mapPolygon: modelData.polygon
                interactive: _root.interactive && index === _root._missionItem.selectedFieldIndex
                borderWidth: Math.max(1, ScreenTools.defaultFontPixelWidth * 0.35)
                borderColor: index === _root._missionItem.selectedFieldIndex ? qgcPal.colorGreen : qgcPal.colorBlue
                interiorColor: qgcPal.colorBlue
                interiorOpacity: index === _root._missionItem.selectedFieldIndex ? 0.12 : 0.05
            }

            MapQuickItem {
                coordinate: modelData.polygon.center
                visible: coordinate.isValid
                z: QGroundControl.zOrderMapItems + 5
                anchorPoint.x: sourceItem.width / 2
                anchorPoint.y: sourceItem.height / 2

                sourceItem: QGCMapLabel {
                    map: _root.map
                    text: modelData.name
                    color: index === _root._missionItem.selectedFieldIndex ? qgcPal.colorGreen : qgcPal.text
                }
            }

            QGCMapPolylineVisuals {
                mapControl: _root.map
                mapPolyline: modelData.transitPolyline
                interactive: _root.interactive && index === _root._missionItem.selectedFieldIndex
                           && index < _root._fields.count - 1
                lineWidth: _root.transitSegmentLineWidth
                lineColor: _root.transitSegmentColor
                visible: index < _root._fields.count - 1
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
