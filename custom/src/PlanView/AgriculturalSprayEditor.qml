import QtQuick
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls
import QGroundControl.FactControls

Rectangle {
    id: _root

    required property var missionItem
    required property real availableWidth

    readonly property var  _missionController: missionItem.masterController.missionController
    readonly property real _margin:            ScreenTools.defaultFontPixelWidth / 2
    readonly property real _fieldWidth:        ScreenTools.defaultFontPixelWidth * 10.5
    readonly property real _radius:            ScreenTools.defaultFontPixelWidth / 2
    readonly property bool _noInclusion:       missionItem.status === 0
    readonly property bool _routeReady:        missionItem.specifiesCoordinate
    readonly property var _selectedField:      missionItem.selectedField

    function setNonSprayPolygonInteractive(selectedPolygon, interactive) {
        for (let index = 0; index < missionItem.nonSprayPolygons.count; index++) {
            const polygon = missionItem.nonSprayPolygons.get(index)
            polygon.interactive = interactive && polygon === selectedPolygon
        }
    }

    objectName: "agriculturalSprayEditor"
    width:      availableWidth
    height:     editorColumn.implicitHeight + (_margin * 2)
    color:      qgcPal.windowShadeDark
    radius:     _radius

    QGCPalette {
        id: qgcPal
        colorGroupEnabled: _root.enabled
    }

    ColumnLayout {
        id: editorColumn

        anchors.margins: _root._margin
        anchors.left:    parent.left
        anchors.right:   parent.right
        anchors.top:     parent.top
        spacing:         _root._margin

        QGCLabel {
            objectName:             "agriculturalSprayStatusLabel"
            Layout.fillWidth:       true
            text:                   _root.missionItem.statusText
            wrapMode:               Text.WordWrap
            horizontalAlignment:    Text.AlignHCenter
            color:                  _root._routeReady ? qgcPal.text : qgcPal.warningText
        }

        RowLayout {
            Layout.fillWidth: true
            QGCLabel { text: qsTr("Fields") }
            QGCComboBox {
                objectName: "agriculturalSprayFieldSelector"
                Layout.fillWidth: true
                model: _root.missionItem.fieldRows
                textRole: "name"
                currentIndex: _root.missionItem.selectedFieldRow
                onActivated: _root.missionItem.setSelectedFieldIndex(_root.missionItem.fieldRows[index].index)
            }
            QGCTextField {
                id: fieldNameField
                Layout.preferredWidth: _root._fieldWidth
                text: _root.missionItem.selectedFieldRow >= 0
                      ? _root.missionItem.fieldRows[_root.missionItem.selectedFieldRow].name
                      : ""
                onEditingFinished: _root.missionItem.renameField(_root.missionItem.selectedFieldIndex, text)
            }
            QGCButton {
                text: qsTr("Up")
                enabled: _root.missionItem.selectedFieldRow > 0
                onClicked: {
                    const row = _root.missionItem.selectedFieldRow
                    _root.missionItem.moveField(_root.missionItem.fieldRows[row].index,
                                                _root.missionItem.fieldRows[row - 1].index)
                }
            }
            QGCButton {
                text: qsTr("Down")
                enabled: _root.missionItem.selectedFieldRow >= 0
                         && _root.missionItem.selectedFieldRow < _root.missionItem.fieldRows.length - 1
                onClicked: {
                    const row = _root.missionItem.selectedFieldRow
                    _root.missionItem.moveField(_root.missionItem.fieldRows[row].index,
                                                _root.missionItem.fieldRows[row + 1].index)
                }
            }
            QGCButton {
                text: qsTr("Add")
                onClicked: _root.missionItem.addField()
            }
            QGCButton {
                text: qsTr("Delete")
                enabled: _root.missionItem.fieldRows.length > 1
                onClicked: _root.missionItem.removeField(_root.missionItem.selectedFieldIndex)
            }
        }

        RowLayout {
            Layout.fillWidth: true
            visible: _root.missionItem.selectedFieldRow >= 0
                     && _root.missionItem.selectedFieldRow < _root.missionItem.fieldRows.length - 1

            QGCButton {
                Layout.fillWidth: true
                text: _root._selectedField && _root._selectedField.transitPolyline.traceMode
                      ? qsTr("Finish transit") : qsTr("Add transit waypoint")
                onClicked: {
                    if (_root._selectedField) {
                        _root._selectedField.transitPolyline.traceMode = !_root._selectedField.transitPolyline.traceMode
                    }
                }
            }
            QGCButton {
                text: qsTr("Clear")
                enabled: _root._selectedField && !_root._selectedField.transitPolyline.empty
                onClicked: _root._selectedField.transitPolyline.clear()
            }
        }

        QGCLabel {
            objectName:         "agriculturalSprayNoInclusionGuidance"
            Layout.fillWidth:   true
            text:               qsTr("Draw and finish the new field polygon in the GeoFence layer.")
            wrapMode:           Text.WordWrap
            visible:            _root._noInclusion
        }

        QGCButton {
            objectName:         "agriculturalSprayEditGeoFenceButton"
            Layout.fillWidth:   true
            text:               qsTr("Edit GeoFence")
            visible:            _root._noInclusion
            onClicked:          _root._missionController.requestPlanEditLayer("fenceGroup")
        }

        QGCLabel {
            objectName:         "agriculturalSprayErrorGuidance"
            Layout.fillWidth:   true
            text:               _root.missionItem.errorText
            wrapMode:           Text.WordWrap
            color:              qgcPal.warningText
            visible:            _root.missionItem.errorText.length > 0
        }

        GridLayout {
            Layout.fillWidth:   true
            columns:            2
            columnSpacing:      _root._margin
            rowSpacing:         _root._margin

            QGCLabel { text: qsTr("Altitude") }
            FactTextField {
                objectName:             "agriculturalSprayAltitudeField"
                Layout.fillWidth:       true
                Layout.preferredWidth:  _root._fieldWidth
                fact:                   _root._selectedField ? _root._selectedField.altitude : null
            }

            QGCLabel { text: qsTr("Line spacing") }
            FactTextField {
                objectName:             "agriculturalSprayLineSpacingField"
                Layout.fillWidth:       true
                Layout.preferredWidth:  _root._fieldWidth
                fact:                   _root._selectedField ? _root._selectedField.lineSpacing : null
            }

            QGCLabel { text: qsTr("Boundary margin") }
            FactTextField {
                objectName:             "agriculturalSprayBoundaryMarginField"
                Layout.fillWidth:       true
                Layout.preferredWidth:  _root._fieldWidth
                fact:                   _root._selectedField ? _root._selectedField.boundaryMargin : null
            }

            QGCLabel { text: qsTr("Margin scope") }
            FactComboBox {
                objectName:             "agriculturalSprayBoundaryMarginScopeCombo"
                Layout.fillWidth:       true
                Layout.preferredWidth:  _root._fieldWidth
                fact:                   _root._selectedField ? _root._selectedField.boundaryMarginScope : null
                indexModel:             false
            }

            QGCLabel {
                objectName:         "agriculturalSprayMarginEdgeGuidance"
                Layout.fillWidth:   true
                Layout.columnSpan:  2
                text:               qsTr("Tap an edge marker on the map to choose the boundary margin edge.")
                wrapMode:           Text.WordWrap
                visible:            _root.missionItem.boundaryMargin.rawValue > 0
                                    && _root.missionItem.boundaryMarginScope.rawValue === 0
            }

            Repeater {
                model: _root.missionItem.boundaryMarginScope.rawValue === 0
                       ? _root.missionItem.fieldMarginRows
                       : []

                RowLayout {
                    required property var modelData

                    Layout.fillWidth:   true
                    Layout.columnSpan:  2
                    spacing:            _root._margin

                    QGCLabel {
                        Layout.fillWidth: true
                        text:             modelData.label
                    }

                    QGCTextField {
                        Layout.preferredWidth: _root._fieldWidth
                        text:                  Number(modelData.margin).toLocaleString(Qt.locale(), 'f', 2)
                        inputMethodHints:      Qt.ImhFormattedNumbersOnly
                        validator:             DoubleValidator { bottom: 0.0; decimals: 2 }
                        onEditingFinished: {
                            const value = Number.fromLocaleString(Qt.locale(), text)
                            if (!isNaN(value) && value >= 0) {
                                _root.missionItem.setFieldMargin(modelData.edgeIndex, value)
                            }
                        }
                    }

                    QGCLabel { text: qsTr("m") }
                }
            }

            QGCLabel {
                objectName:         "agriculturalSprayDirectionGuidance"
                Layout.fillWidth:   true
                Layout.columnSpan:  2
                text:               qsTr("Tap a starting corner on the map. The edge after that corner sets the flight direction.")
                wrapMode:           Text.WordWrap
            }

            QGCLabel { text: qsTr("Droplet class") }
            FactComboBox {
                objectName:             "agriculturalSprayDropletClassCombo"
                Layout.fillWidth:       true
                Layout.preferredWidth:  _root._fieldWidth
                fact:                   _root._selectedField ? _root._selectedField.dropletClass : null
                indexModel:             false
            }

            QGCLabel { text: qsTr("Application rate") }
            FactTextField {
                objectName:             "agriculturalSprayApplicationRateField"
                Layout.fillWidth:       true
                Layout.preferredWidth:  _root._fieldWidth
                fact:                   _root._selectedField ? _root._selectedField.applicationRate : null
            }
        }

        ColumnLayout {
            readonly property var _rows: _root.missionItem.exclusionMarginRows || []

            Layout.fillWidth: true
            spacing:          _root._margin
            visible:          _rows.length > 0

            QGCLabel {
                Layout.fillWidth: true
                text:             qsTr("Exclusion margins")
                font.bold:        true
            }

            QGCLabel {
                Layout.fillWidth: true
                text:             qsTr("Additional distance kept from each exclusion GeoFence while generating the spray route.")
                wrapMode:         Text.WordWrap
            }

            Repeater {
                model: parent._rows

                RowLayout {
                    required property var modelData

                    Layout.fillWidth: true
                    spacing:          _root._margin

                    QGCLabel {
                        Layout.fillWidth: true
                        text:             modelData.label
                    }

                    QGCTextField {
                        Layout.preferredWidth: _root._fieldWidth
                        text:                  Number(modelData.margin).toLocaleString(Qt.locale(), 'f', 2)
                        inputMethodHints:      Qt.ImhFormattedNumbersOnly
                        validator:             DoubleValidator { bottom: 0.0; decimals: 2 }
                        onEditingFinished: {
                            const value = Number.fromLocaleString(Qt.locale(), text)
                            if (!isNaN(value) && value >= 0) {
                                _root.missionItem.setExclusionMargin(modelData.shape, value)
                            }
                        }
                    }

                    QGCLabel { text: qsTr("m") }
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing:          _root._margin

            QGCLabel {
                Layout.fillWidth: true
                text:             qsTr("Traversable non-spray areas")
                font.bold:        true
            }

            QGCLabel {
                Layout.fillWidth: true
                text:             qsTr("The route can cross these polygons, but spraying is paused inside them.")
                wrapMode:         Text.WordWrap
            }

            QGCButton {
                objectName:       "agriculturalSprayAddNonSprayPolygonButton"
                Layout.fillWidth: true
                text:             qsTr("Add non-spray area")
                onClicked: {
                    const polygon = _root.missionItem.addNonSprayPolygon()
                    if (polygon) {
                        _root.setNonSprayPolygonInteractive(polygon, true)
                    }
                }
            }

            Repeater {
                model: _root.missionItem.nonSprayPolygons

                RowLayout {
                    required property var object
                    required property int index

                    Layout.fillWidth: true
                    spacing:          _root._margin

                    QGCLabel {
                        Layout.fillWidth: true
                        text:             qsTr("Non-spray area %1").arg(parent.index + 1)
                    }

                    QGCButton {
                        text: qsTr("Edit")
                        onClicked: _root.setNonSprayPolygonInteractive(parent.object, !parent.object.interactive)
                    }

                    QGCButton {
                        text:      qsTr("Delete")
                        onClicked: _root.missionItem.removeNonSprayPolygon(parent.object)
                    }
                }
            }
        }
    }

}
