#include "AgriculturalSprayComplexItem.h"

#include <QtCore/QAbstractItemModel>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonValue>
#include <QtCore/QVariant>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "AgriculturalSprayPlanner.h"
#include "AppSettings.h"
#include "FactMetaData.h"
#include "FlightPathSegment.h"
#include "GeoFenceController.h"
#include "JsonParsing.h"
#include "MissionController.h"
#include "MissionItem.h"
#include "PlanMasterController.h"
#include "QGCFenceCircle.h"
#include "QGCFencePolygon.h"
#include "QGCGeo.h"
#include "QGCGeoBoundingCube.h"
#include "QGCLoggingCategory.h"
#include "QGCMapCircle.h"
#include "QGCMapPolygon.h"
#include "SettingsManager.h"

QGC_LOGGING_CATEGORY(AgriculturalSprayComplexItemLog, "qgc.custom.agriculturalspraycomplexitem")

namespace {

constexpr double CIRCLE_CHORD_ERROR_METERS = 0.05;

bool finiteCoordinate(const QGeoCoordinate& coordinate)
{
    return coordinate.isValid() && std::isfinite(coordinate.latitude()) && std::isfinite(coordinate.longitude());
}

QGeoCoordinate horizontalCoordinate(const QGeoCoordinate& coordinate)
{
    return QGeoCoordinate(coordinate.latitude(), coordinate.longitude(), 0.0);
}

}  // namespace

AgriculturalSprayComplexItem::AgriculturalSprayComplexItem(PlanMasterController* masterController, bool flyView)
    : ComplexMissionItem(masterController, flyView),
      _metaDataMap(
          FactMetaData::createMapFromJsonFile(QStringLiteral(":/json/AgriculturalSpray.SettingsGroup.json"), this)),
      _altitudeFact(0, QString::fromLatin1(altitudeName), FactMetaData::valueTypeDouble),
      _lineSpacingFact(0, QString::fromLatin1(lineSpacingName), FactMetaData::valueTypeDouble),
      _gridAngleFact(0, QString::fromLatin1(gridAngleName), FactMetaData::valueTypeDouble),
      _entryCornerFact(0, QString::fromLatin1(entryCornerName), FactMetaData::valueTypeUint32),
      _dropletClassFact(0, QString::fromLatin1(dropletClassName), FactMetaData::valueTypeUint32),
      _applicationRateFact(0, QString::fromLatin1(applicationRateName), FactMetaData::valueTypeDouble)
{
    _editorQml = QStringLiteral("qrc:/qml/Custom/Plan/AgriculturalSprayEditor.qml");

    _metadataValid =
        _initializeFact(_altitudeFact, altitudeName) && _initializeFact(_lineSpacingFact, lineSpacingName) &&
        _initializeFact(_gridAngleFact, gridAngleName) && _initializeFact(_entryCornerFact, entryCornerName) &&
        _initializeFact(_dropletClassFact, dropletClassName) &&
        _initializeFact(_applicationRateFact, applicationRateName);

    if (_metadataValid) {
        _altitudeFact.setRawValue(SettingsManager::instance()->appSettings()->defaultMissionItemAltitude()->rawValue());
    }

    for (Fact* fact : {&_altitudeFact, &_lineSpacingFact, &_gridAngleFact, &_entryCornerFact, &_dropletClassFact,
                       &_applicationRateFact}) {
        connect(fact, &Fact::valueChanged, this, &AgriculturalSprayComplexItem::_inputChanged);
    }
    connect(_missionController, &MissionController::plannedHomePositionChanged, this,
            &AgriculturalSprayComplexItem::_plannedHomePositionChanged);

    _connectFenceModels();
    _reconnectShapeSignals();
    _setStatus(NoInclusion, QString());
    setDirty(false);
    rebuild();
}

bool AgriculturalSprayComplexItem::_initializeFact(Fact& fact, const char* name)
{
    FactMetaData* const metaData = _metaDataMap.value(QString::fromLatin1(name), nullptr);
    if (!metaData) {
        qCWarning(AgriculturalSprayComplexItemLog) << "Missing Fact metadata"
                                                   << "name:" << name;
        return false;
    }

    fact.setMetaData(metaData, true);
    return true;
}

void AgriculturalSprayComplexItem::_connectFenceModels()
{
    if (!_masterController) {
        qCWarning(AgriculturalSprayComplexItemLog) << "No plan master controller is available";
        _shapeModelsValid = false;
        return;
    }

    _geoFenceController = _masterController->geoFenceController();
    if (!_geoFenceController) {
        qCWarning(AgriculturalSprayComplexItemLog) << "No GeoFence controller is available";
        _shapeModelsValid = false;
        return;
    }

    _polygonModel = _geoFenceController->polygons();
    _circleModel = _geoFenceController->circles();
    if (!_polygonModel || !_circleModel) {
        qCWarning(AgriculturalSprayComplexItemLog) << "GeoFence shape models are unavailable";
        _shapeModelsValid = false;
        return;
    }

    const auto connectModel = [this](QmlObjectListModel* model) {
        connect(model, &QAbstractItemModel::rowsInserted, this, &AgriculturalSprayComplexItem::_fenceModelChanged);
        connect(model, &QAbstractItemModel::rowsRemoved, this, &AgriculturalSprayComplexItem::_fenceModelChanged);
        connect(model, &QAbstractItemModel::rowsMoved, this, &AgriculturalSprayComplexItem::_fenceModelChanged);
        connect(model, &QAbstractItemModel::modelReset, this, &AgriculturalSprayComplexItem::_fenceModelChanged);
        connect(model, &QAbstractItemModel::layoutChanged, this, &AgriculturalSprayComplexItem::_fenceModelChanged);
        connect(model, &QAbstractItemModel::dataChanged, this, &AgriculturalSprayComplexItem::_fenceModelChanged);
        connect(model, &QmlObjectListModel::countChanged, this, &AgriculturalSprayComplexItem::_fenceModelChanged);
    };
    connectModel(_polygonModel);
    connectModel(_circleModel);
    connect(_geoFenceController, &GeoFenceController::loadComplete, this,
            &AgriculturalSprayComplexItem::_fenceInputChanged);
}

void AgriculturalSprayComplexItem::_reconnectShapeSignals()
{
    for (const QMetaObject::Connection& connection : std::as_const(_shapeConnections)) {
        disconnect(connection);
    }
    _shapeConnections.clear();
    _shapeConnectionsDirty = false;

    if (!_polygonModel || !_circleModel) {
        _shapeModelsValid = false;
        return;
    }

    _shapeModelsValid = true;
    for (int index = 0; index < _polygonModel->count(); ++index) {
        QGCFencePolygon* const polygon = qobject_cast<QGCFencePolygon*>((*_polygonModel)[index]);
        if (!polygon) {
            qCWarning(AgriculturalSprayComplexItemLog) << "GeoFence polygon model contains an invalid object"
                                                       << "index:" << index;
            _shapeModelsValid = false;
            continue;
        }
        _connectPolygonSignals(polygon);
    }

    for (int index = 0; index < _circleModel->count(); ++index) {
        QGCFenceCircle* const circle = qobject_cast<QGCFenceCircle*>((*_circleModel)[index]);
        if (!circle) {
            qCWarning(AgriculturalSprayComplexItemLog) << "GeoFence circle model contains an invalid object"
                                                       << "index:" << index;
            _shapeModelsValid = false;
            continue;
        }
        _connectCircleSignals(circle);
    }
}

void AgriculturalSprayComplexItem::_connectPolygonSignals(QGCFencePolygon* polygon)
{
    if (!polygon) {
        qCWarning(AgriculturalSprayComplexItemLog) << "Cannot observe a null GeoFence polygon";
        _shapeModelsValid = false;
        return;
    }

    _shapeConnections.append(
        connect(polygon, &QGCMapPolygon::pathChanged, this, &AgriculturalSprayComplexItem::_fenceInputChanged));
    _shapeConnections.append(
        connect(polygon, &QGCMapPolygon::dragPathChanged, this, &AgriculturalSprayComplexItem::_fenceInputChanged));
    _shapeConnections.append(
        connect(polygon, &QGCMapPolygon::countChanged, this, &AgriculturalSprayComplexItem::_fenceInputChanged));
    _shapeConnections.append(
        connect(polygon, &QGCMapPolygon::centerChanged, this, &AgriculturalSprayComplexItem::_fenceInputChanged));
    _shapeConnections.append(
        connect(polygon, &QGCMapPolygon::dragCenterChanged, this, &AgriculturalSprayComplexItem::_fenceInputChanged));
    _shapeConnections.append(
        connect(polygon, &QGCFencePolygon::inclusionChanged, this, &AgriculturalSprayComplexItem::_fenceInputChanged));
    _shapeConnections.append(
        connect(polygon, &QObject::destroyed, this, &AgriculturalSprayComplexItem::_fenceModelChanged));
}

void AgriculturalSprayComplexItem::_connectCircleSignals(QGCFenceCircle* circle)
{
    if (!circle || !circle->radius()) {
        qCWarning(AgriculturalSprayComplexItemLog) << "Cannot observe an invalid GeoFence circle";
        _shapeModelsValid = false;
        return;
    }

    _shapeConnections.append(
        connect(circle, &QGCMapCircle::centerChanged, this, &AgriculturalSprayComplexItem::_fenceInputChanged));
    _shapeConnections.append(
        connect(circle->radius(), &Fact::valueChanged, this, &AgriculturalSprayComplexItem::_fenceInputChanged));
    _shapeConnections.append(
        connect(circle, &QGCFenceCircle::inclusionChanged, this, &AgriculturalSprayComplexItem::_fenceInputChanged));
    _shapeConnections.append(
        connect(circle, &QObject::destroyed, this, &AgriculturalSprayComplexItem::_fenceModelChanged));
}

void AgriculturalSprayComplexItem::_inputChanged()
{
    if (_loading) {
        return;
    }

    setDirty(true);
    _invalidateRoute();
    _scheduleRebuild();
}

void AgriculturalSprayComplexItem::_fenceInputChanged()
{
    if (_loading) {
        return;
    }

    _invalidateRoute();
    _scheduleRebuild();
}

void AgriculturalSprayComplexItem::_fenceModelChanged()
{
    _shapeConnectionsDirty = true;
    _fenceInputChanged();
}

void AgriculturalSprayComplexItem::rebuild()
{
    _invalidateRoute();
    _scheduleRebuild();
}

void AgriculturalSprayComplexItem::refreshAfterLoad()
{
    _loading = false;
    _shapeConnectionsDirty = true;
    _invalidateRoute();
    _scheduleRebuild();
    setDirty(false);
}

void AgriculturalSprayComplexItem::_scheduleRebuild()
{
    if (_rebuildPending) {
        return;
    }

    _rebuildPending = true;
    QMetaObject::invokeMethod(this, &AgriculturalSprayComplexItem::_rebuildQueued, Qt::QueuedConnection);
}

void AgriculturalSprayComplexItem::_invalidateRoute()
{
    const bool wasReady = readyForSaveState() == ReadyForSave;
    const int previousLastSequenceNumber = lastSequenceNumber();

    _clearGeneratedRoute();
    _setStatus(Generating, QString());

    if (previousLastSequenceNumber != lastSequenceNumber()) {
        emit lastSequenceNumberChanged(lastSequenceNumber());
    }
    if (wasReady != (readyForSaveState() == ReadyForSave)) {
        emit readyForSaveStateChanged();
    }
}

void AgriculturalSprayComplexItem::_clearGeneratedRoute()
{
    const bool hadRoute = !_routeCoordinates.isEmpty();
    const bool hadSegmentTypes = !_routeSegmentTypes.isEmpty();
    const bool wasIncomplete = _isIncomplete;
    const bool specifiedCoordinate = specifiesCoordinate();
    const bool sameEntryExit = exitCoordinateSameAsEntry();
    const bool hadDistance = _complexDistance != 0.0;
    const bool hadSprayLegs = _sprayLegCount != 0;

    _routeCoordinates.clear();
    _routeSegmentTypes.clear();
    _complexDistance = 0.0;
    _sprayLegCount = 0;
    _isIncomplete = true;

    _setBoundingCube(QGCGeoBoundingCube());
    _rebuildFlightPathSegments();

    if (hadRoute) {
        emit routeCoordinatesChanged();
        emit coordinateChanged(coordinate());
        emit entryCoordinateChanged(entryCoordinate());
        emit exitCoordinateChanged(exitCoordinate());
        emit greatestDistanceToChanged();
        emit minAMSLAltitudeChanged();
        emit maxAMSLAltitudeChanged();
        _amslEntryAltChanged();
        _amslExitAltChanged();
    }
    if (hadSegmentTypes) {
        emit routeSegmentTypesChanged();
    }
    if (hadDistance) {
        emit complexDistanceChanged();
    }
    if (hadSprayLegs) {
        emit sprayLegCountChanged();
    }
    if (specifiedCoordinate != specifiesCoordinate()) {
        emit specifiesCoordinateChanged();
    }
    if (sameEntryExit != exitCoordinateSameAsEntry()) {
        emit exitCoordinateSameAsEntryChanged(exitCoordinateSameAsEntry());
    }
    if (wasIncomplete != _isIncomplete) {
        emit isIncompleteChanged();
    }
}

bool AgriculturalSprayComplexItem::_validateCurrentFacts(QString& errorText)
{
    const std::array<Fact*, 6> facts = {
        &_altitudeFact,    &_lineSpacingFact,  &_gridAngleFact,
        &_entryCornerFact, &_dropletClassFact, &_applicationRateFact,
    };

    for (Fact* fact : facts) {
        if (!fact || !fact->metaData()) {
            errorText = tr("Fact metadata is unavailable.");
            return false;
        }

        QVariant typedValue;
        QString validationError;
        if (!fact->metaData()->convertAndValidateRaw(fact->rawValue(), false, typedValue, validationError) ||
            !std::isfinite(typedValue.toDouble())) {
            errorText = tr("%1 is invalid: %2").arg(fact->name(), validationError);
            return false;
        }
    }

    const uint entryCornerValue = _entryCornerFact.rawValue().toUInt();
    const uint dropletClassValue = _dropletClassFact.rawValue().toUInt();
    if (entryCornerValue > static_cast<uint>(BottomRight)) {
        errorText = tr("Entry corner is outside the supported range.");
        return false;
    }
    if (dropletClassValue > static_cast<uint>(UltraCoarse)) {
        errorText = tr("Droplet class is outside the supported range.");
        return false;
    }

    return true;
}

bool AgriculturalSprayComplexItem::_snapshotPlannerInput(AgriculturalSpray::PlannerInput& input, QGeoCoordinate& origin,
                                                         Status& failureStatus, QString& errorText)
{
    if (!_shapeModelsValid || !_polygonModel || !_circleModel) {
        failureStatus = GenerationError;
        errorText = tr("GeoFence shape models are unavailable or contain invalid objects.");
        return false;
    }

    if (!_validateCurrentFacts(errorText)) {
        failureStatus = InvalidArea;
        return false;
    }

    const std::size_t shapeCount =
        static_cast<std::size_t>(_polygonModel->count()) + static_cast<std::size_t>(_circleModel->count());
    if (shapeCount > input.limits.maxShapes) {
        failureStatus = GenerationError;
        errorText = tr("GeoFence shape count exceeds the planner limit of %1.")
                        .arg(static_cast<qulonglong>(input.limits.maxShapes));
        return false;
    }

    bool hasInclusion = false;
    for (int index = 0; index < _polygonModel->count(); ++index) {
        const QGCFencePolygon* const polygon = qobject_cast<const QGCFencePolygon*>((*_polygonModel)[index]);
        if (!polygon) {
            failureStatus = GenerationError;
            errorText = tr("GeoFence polygon model contains an invalid object.");
            return false;
        }
        if (!polygon->inclusion()) {
            continue;
        }
        hasInclusion = true;
        for (const QGeoCoordinate& coordinate : polygon->coordinateList()) {
            if (finiteCoordinate(coordinate)) {
                origin = horizontalCoordinate(coordinate);
                break;
            }
        }
        if (origin.isValid()) {
            break;
        }
    }

    if (!origin.isValid()) {
        for (int index = 0; index < _circleModel->count(); ++index) {
            QGCFenceCircle* const circle = qobject_cast<QGCFenceCircle*>((*_circleModel)[index]);
            if (!circle) {
                failureStatus = GenerationError;
                errorText = tr("GeoFence circle model contains an invalid object.");
                return false;
            }
            if (!circle->inclusion()) {
                continue;
            }
            hasInclusion = true;
            if (finiteCoordinate(circle->center())) {
                origin = horizontalCoordinate(circle->center());
                break;
            }
        }
    }

    if (!hasInclusion) {
        failureStatus = NoInclusion;
        errorText = tr("At least one inclusion polygon or circle is required.");
        return false;
    }
    if (!origin.isValid()) {
        failureStatus = InvalidArea;
        errorText = tr("No inclusion shape contains a valid geographic coordinate.");
        return false;
    }

    const auto localPoint = [&origin](const QGeoCoordinate& coordinate) {
        double north = std::numeric_limits<double>::quiet_NaN();
        double east = std::numeric_limits<double>::quiet_NaN();
        double down = std::numeric_limits<double>::quiet_NaN();
        if (finiteCoordinate(coordinate)) {
            QGCGeo::convertGeoToNed(horizontalCoordinate(coordinate), origin, north, east, down);
        }
        return AgriculturalSpray::Point{north, east};
    };

    input.inclusions.reserve(shapeCount);
    input.exclusions.reserve(shapeCount);
    for (int index = 0; index < _polygonModel->count(); ++index) {
        const QGCFencePolygon* const fencePolygon = qobject_cast<const QGCFencePolygon*>((*_polygonModel)[index]);
        if (!fencePolygon) {
            failureStatus = GenerationError;
            errorText = tr("GeoFence polygon model contains an invalid object.");
            return false;
        }

        const QList<QGeoCoordinate> coordinates = fencePolygon->coordinateList();
        if (static_cast<std::size_t>(coordinates.count()) > input.limits.maxShapeVertices) {
            failureStatus = GenerationError;
            errorText = tr("A GeoFence polygon exceeds the planner vertex limit of %1.")
                            .arg(static_cast<qulonglong>(input.limits.maxShapeVertices));
            return false;
        }

        AgriculturalSpray::Polygon polygon;
        polygon.vertices.reserve(static_cast<std::size_t>(coordinates.count()));
        for (const QGeoCoordinate& coordinate : coordinates) {
            polygon.vertices.push_back(localPoint(coordinate));
        }

        (fencePolygon->inclusion() ? input.inclusions : input.exclusions).emplace_back(std::move(polygon));
    }

    for (int index = 0; index < _circleModel->count(); ++index) {
        QGCFenceCircle* const fenceCircle = qobject_cast<QGCFenceCircle*>((*_circleModel)[index]);
        if (!fenceCircle || !fenceCircle->radius()) {
            failureStatus = GenerationError;
            errorText = tr("GeoFence circle model contains an invalid object.");
            return false;
        }

        AgriculturalSpray::Circle circle{
            .center = localPoint(fenceCircle->center()),
            .radius = fenceCircle->radius()->rawValue().toDouble(),
        };
        (fenceCircle->inclusion() ? input.inclusions : input.exclusions).emplace_back(circle);
    }

    input.spacing = _lineSpacingFact.rawValue().toDouble();
    input.gridAngleDegrees = _gridAngleFact.rawValue().toDouble();
    input.circleChordError = CIRCLE_CHORD_ERROR_METERS;
    input.entryCorner = static_cast<AgriculturalSpray::EntryCorner>(_entryCornerFact.rawValue().toUInt());
    return true;
}

void AgriculturalSprayComplexItem::_rebuildQueued()
{
    _rebuildPending = false;
    if (_loading) {
        return;
    }

    if (_shapeConnectionsDirty) {
        _reconnectShapeSignals();
    }

    if (!_metadataValid) {
        const QString error = tr("Agricultural Spray Fact metadata could not be loaded.");
        qCWarning(AgriculturalSprayComplexItemLog) << error;
        _setStatus(GenerationError, error);
        return;
    }

    AgriculturalSpray::PlannerInput input;
    QGeoCoordinate origin;
    Status failureStatus = GenerationError;
    QString error;
    if (!_snapshotPlannerInput(input, origin, failureStatus, error)) {
        if (failureStatus == NoInclusion) {
            qCDebug(AgriculturalSprayComplexItemLog) << error;
        } else {
            qCWarning(AgriculturalSprayComplexItemLog) << "Route input snapshot failed"
                                                       << "status:" << failureStatus << "error:" << error;
        }
        _setStatus(failureStatus, error);
        return;
    }

    _setStatus(Generating, QString());
    const AgriculturalSpray::PlannerResult result = AgriculturalSpray::plan(input);
    _publishPlannerResult(result, origin);
}

void AgriculturalSprayComplexItem::_publishPlannerResult(const AgriculturalSpray::PlannerResult& result,
                                                         const QGeoCoordinate& origin)
{
    if (result.succeeded()) {
        _publishSuccessfulRoute(result, origin);
        return;
    }

    const QString plannerError = QString::fromStdString(result.error);
    Status itemStatus = GenerationError;
    switch (result.status) {
        case AgriculturalSpray::PlannerStatus::Success:
            itemStatus = GenerationError;
            break;
        case AgriculturalSpray::PlannerStatus::InvalidInput:
            itemStatus = InvalidArea;
            break;
        case AgriculturalSpray::PlannerStatus::ComplexityLimit:
            itemStatus = GenerationError;
            break;
        case AgriculturalSpray::PlannerStatus::EmptyRegion:
            itemStatus = EmptyEffectiveArea;
            break;
        case AgriculturalSpray::PlannerStatus::DisconnectedRegion:
        case AgriculturalSpray::PlannerStatus::NoRoute:
            itemStatus = NoSafeRoute;
            break;
    }

    qCWarning(AgriculturalSprayComplexItemLog) << "Planner did not produce a route"
                                               << "plannerStatus:" << static_cast<int>(result.status)
                                               << "itemStatus:" << itemStatus << "error:" << plannerError;
    _setStatus(itemStatus, plannerError);
}

void AgriculturalSprayComplexItem::_publishSuccessfulRoute(const AgriculturalSpray::PlannerResult& result,
                                                           const QGeoCoordinate& origin)
{
    if (result.route.size() < 2 || result.legs.empty() || !std::isfinite(result.distance) || result.distance < 0.0) {
        const QString error = tr("The planner reported success with an invalid route.");
        qCWarning(AgriculturalSprayComplexItemLog) << error;
        _setStatus(GenerationError, error);
        return;
    }

    QList<QGeoCoordinate> routeCoordinates;
    QList<int> routeSegmentTypes;
    routeCoordinates.reserve(static_cast<qsizetype>(result.route.size()));
    for (const AgriculturalSpray::RoutePoint& routePoint : result.route) {
        QGeoCoordinate coordinate;
        QGCGeo::convertNedToGeo(routePoint.position.north, routePoint.position.east, 0.0, origin, coordinate);
        if (!finiteCoordinate(coordinate)) {
            const QString error = tr("The planner produced a coordinate that cannot be converted to WGS84.");
            qCWarning(AgriculturalSprayComplexItemLog) << error;
            _setStatus(GenerationError, error);
            return;
        }
        coordinate.setAltitude(_altitudeFact.rawValue().toDouble());
        routeCoordinates.append(coordinate);
    }

    routeSegmentTypes.reserve(std::max<qsizetype>(0, routeCoordinates.count() - 1));
    for (std::size_t index = 1; index < result.route.size(); ++index) {
        const bool spraySegment = result.route[index - 1].type == AgriculturalSpray::RoutePointType::SprayStart &&
                                  result.route[index].type == AgriculturalSpray::RoutePointType::SprayEnd;
        routeSegmentTypes.append(spraySegment ? SprayLeg : Transit);
    }

    const int previousLastSequenceNumber = lastSequenceNumber();
    _routeCoordinates = std::move(routeCoordinates);
    _routeSegmentTypes = std::move(routeSegmentTypes);
    _complexDistance = result.distance;
    _sprayLegCount = static_cast<int>(result.legs.size());
    _isIncomplete = false;
    _setStatus(Ready, QString());
    _updateBoundingCube();
    _rebuildFlightPathSegments();
    _emitSuccessfulRouteNotifications(previousLastSequenceNumber);
}

void AgriculturalSprayComplexItem::_emitSuccessfulRouteNotifications(int previousLastSequenceNumber)
{
    emit routeCoordinatesChanged();
    emit routeSegmentTypesChanged();
    emit sprayLegCountChanged();
    emit complexDistanceChanged();
    emit coordinateChanged(coordinate());
    emit entryCoordinateChanged(entryCoordinate());
    emit exitCoordinateChanged(exitCoordinate());
    emit specifiesCoordinateChanged();
    emit exitCoordinateSameAsEntryChanged(exitCoordinateSameAsEntry());
    emit greatestDistanceToChanged();
    emit isIncompleteChanged();
    emit readyForSaveStateChanged();
    emit minAMSLAltitudeChanged();
    emit maxAMSLAltitudeChanged();
    _amslEntryAltChanged();
    _amslExitAltChanged();
    if (previousLastSequenceNumber != lastSequenceNumber()) {
        emit lastSequenceNumberChanged(lastSequenceNumber());
    }
}

void AgriculturalSprayComplexItem::_setStatus(Status status, const QString& errorText)
{
    const QString newStatusText = _textForStatus(status, errorText);

    if (_status != status) {
        _status = status;
        emit statusChanged(_status);
    }
    if (_errorText != errorText) {
        _errorText = errorText;
        emit errorTextChanged();
    }
    if (_statusText != newStatusText) {
        _statusText = newStatusText;
        emit statusTextChanged();
    }
}

QString AgriculturalSprayComplexItem::_textForStatus(Status status, const QString& errorText) const
{
    QString text;
    switch (status) {
        case NoInclusion:
            text = tr("Add at least one inclusion polygon or circle in the GeoFence.");
            break;
        case InvalidArea:
            text = tr("The spray area or spray settings are invalid.");
            break;
        case EmptyEffectiveArea:
            text = tr("The effective spray area is empty.");
            break;
        case NoSafeRoute:
            text = tr("No safe route connects all spray legs.");
            break;
        case Generating:
            text = tr("Generating spray route.");
            break;
        case Ready:
            text = tr("Spray route ready.");
            break;
        case GenerationError:
            text = tr("Spray route generation failed.");
            break;
    }

    if (!errorText.isEmpty()) {
        text += QStringLiteral(" %1").arg(errorText);
    }
    return text;
}

void AgriculturalSprayComplexItem::_rebuildFlightPathSegments()
{
    _flightPathSegments.beginResetModel();
    _flightPathSegments.clearAndDeleteContents();

    if (_routeCoordinates.count() > 1) {
        const double amslAltitude = amslEntryAlt();
        for (qsizetype index = 1; index < _routeCoordinates.count(); ++index) {
            _flightPathSegments.append(new FlightPathSegment(FlightPathSegment::SegmentTypeGeneric,
                                                             _routeCoordinates[index - 1], amslAltitude,
                                                             _routeCoordinates[index], amslAltitude, false, this));
        }
    }

    _flightPathSegments.endResetModel();
    if (_missionController) {
        _missionController->recalcTerrainProfile();
    }
}

void AgriculturalSprayComplexItem::_plannedHomePositionChanged()
{
    if (_status != Ready) {
        return;
    }

    _updateBoundingCube();
    _rebuildFlightPathSegments();
    emit minAMSLAltitudeChanged();
    emit maxAMSLAltitudeChanged();
    _amslEntryAltChanged();
    _amslExitAltChanged();
}

void AgriculturalSprayComplexItem::_updateBoundingCube()
{
    if (_routeCoordinates.isEmpty()) {
        _setBoundingCube(QGCGeoBoundingCube());
        return;
    }

    double north = -90.0;
    double south = 90.0;
    double east = -180.0;
    double west = 180.0;
    for (const QGeoCoordinate& coordinate : std::as_const(_routeCoordinates)) {
        north = std::max(north, coordinate.latitude());
        south = std::min(south, coordinate.latitude());
        east = std::max(east, coordinate.longitude());
        west = std::min(west, coordinate.longitude());
    }

    const double altitude = amslEntryAlt();
    _setBoundingCube(QGCGeoBoundingCube(QGeoCoordinate(north, west, altitude), QGeoCoordinate(south, east, altitude)));
}

QGeoCoordinate AgriculturalSprayComplexItem::entryCoordinate() const
{
    return _routeCoordinates.isEmpty() ? QGeoCoordinate() : _routeCoordinates.first();
}

QGeoCoordinate AgriculturalSprayComplexItem::exitCoordinate() const
{
    return _routeCoordinates.isEmpty() ? QGeoCoordinate() : _routeCoordinates.last();
}

bool AgriculturalSprayComplexItem::exitCoordinateSameAsEntry() const
{
    return !_routeCoordinates.isEmpty() && entryCoordinate() == exitCoordinate();
}

double AgriculturalSprayComplexItem::amslEntryAlt() const
{
    if (!specifiesCoordinate() || !_missionController) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    return _altitudeFact.rawValue().toDouble() + _missionController->plannedHomePosition().altitude();
}

double AgriculturalSprayComplexItem::minAMSLAltitude() const
{
    return amslEntryAlt();
}

double AgriculturalSprayComplexItem::maxAMSLAltitude() const
{
    return amslEntryAlt();
}

int AgriculturalSprayComplexItem::lastSequenceNumber() const
{
    return _routeCoordinates.isEmpty() ? _sequenceNumber : _sequenceNumber + _routeCoordinates.count() - 1;
}

double AgriculturalSprayComplexItem::greatestDistanceTo(const QGeoCoordinate& other) const
{
    if (!other.isValid()) {
        return 0.0;
    }

    double greatestDistance = 0.0;
    for (const QGeoCoordinate& coordinate : _routeCoordinates) {
        greatestDistance = std::max(greatestDistance, coordinate.distanceTo(other));
    }
    return greatestDistance;
}

double AgriculturalSprayComplexItem::specifiedFlightSpeed()
{
    return std::numeric_limits<double>::quiet_NaN();
}

double AgriculturalSprayComplexItem::specifiedGimbalYaw()
{
    return std::numeric_limits<double>::quiet_NaN();
}

double AgriculturalSprayComplexItem::specifiedGimbalPitch()
{
    return std::numeric_limits<double>::quiet_NaN();
}

void AgriculturalSprayComplexItem::appendMissionItems(QList<MissionItem*>& items, QObject* missionItemParent)
{
    if (readyForSaveState() != ReadyForSave) {
        qCWarning(AgriculturalSprayComplexItemLog) << "Mission expansion skipped because the route is not ready"
                                                   << "status:" << _status << "error:" << _errorText;
        return;
    }

    int sequenceNumber = _sequenceNumber;
    const double altitude = _altitudeFact.rawValue().toDouble();
    for (const QGeoCoordinate& coordinate : std::as_const(_routeCoordinates)) {
        items.append(new MissionItem(sequenceNumber++, MAV_CMD_NAV_WAYPOINT, MAV_FRAME_GLOBAL_RELATIVE_ALT, 0.0, 0.0,
                                     0.0, std::numeric_limits<double>::quiet_NaN(), coordinate.latitude(),
                                     coordinate.longitude(), altitude, true, false, missionItemParent));
    }
}

void AgriculturalSprayComplexItem::setMissionFlightStatus(MissionFlightStatus_t& missionFlightStatus)
{
    ComplexMissionItem::setMissionFlightStatus(missionFlightStatus);
}

void AgriculturalSprayComplexItem::applyNewAltitude(double newAltitude)
{
    _altitudeFact.setRawValue(newAltitude);
}

VisualMissionItem::ReadyForSaveState AgriculturalSprayComplexItem::readyForSaveState() const
{
    return specifiesCoordinate() && !_isIncomplete ? ReadyForSave : NotReadyForSaveData;
}

void AgriculturalSprayComplexItem::setDirty(bool dirty)
{
    if (_dirty != dirty) {
        _dirty = dirty;
        emit dirtyChanged(_dirty);
    }
}

void AgriculturalSprayComplexItem::setCoordinate(const QGeoCoordinate& coordinate)
{
    Q_UNUSED(coordinate);
}

void AgriculturalSprayComplexItem::setSequenceNumber(int sequenceNumber)
{
    if (_sequenceNumber != sequenceNumber) {
        _sequenceNumber = sequenceNumber;
        emit sequenceNumberChanged(_sequenceNumber);
        emit lastSequenceNumberChanged(lastSequenceNumber());
    }
}

void AgriculturalSprayComplexItem::save(QJsonArray& missionItems)
{
    QJsonObject saveObject;
    saveObject[JsonParsing::jsonVersionKey] = _jsonVersion;
    saveObject[VisualMissionItem::jsonTypeKey] = VisualMissionItem::jsonTypeComplexItemValue;
    saveObject[ComplexMissionItem::jsonComplexItemTypeKey] = jsonComplexItemTypeValue;
    saveObject[altitudeName] = _altitudeFact.rawValue().toDouble();
    saveObject[lineSpacingName] = _lineSpacingFact.rawValue().toDouble();
    saveObject[gridAngleName] = _gridAngleFact.rawValue().toDouble();
    saveObject[entryCornerName] = static_cast<int>(_entryCornerFact.rawValue().toUInt());
    saveObject[dropletClassName] = static_cast<int>(_dropletClassFact.rawValue().toUInt());
    saveObject[applicationRateName] = _applicationRateFact.rawValue().toDouble();
    missionItems.append(saveObject);
}

bool AgriculturalSprayComplexItem::_validateLoadValue(Fact& fact, const QJsonValue& jsonValue, QVariant& typedValue,
                                                      QString& errorText)
{
    if (!fact.metaData()) {
        errorText = tr("%1 metadata is unavailable.").arg(fact.name());
        return false;
    }

    QString validationError;
    if (!fact.metaData()->convertAndValidateRaw(jsonValue.toVariant(), false, typedValue, validationError) ||
        !std::isfinite(typedValue.toDouble())) {
        errorText = tr("%1 is invalid: %2").arg(fact.name(), validationError);
        return false;
    }

    if ((&fact == &_entryCornerFact || &fact == &_dropletClassFact) &&
        jsonValue.toDouble() != std::floor(jsonValue.toDouble())) {
        errorText = tr("%1 must be an integer enum value.").arg(fact.name());
        return false;
    }
    return true;
}

bool AgriculturalSprayComplexItem::load(const QJsonObject& complexObject, int sequenceNumber, QString& errorString)
{
    _invalidateRoute();

    const QList<JsonParsing::KeyValidateInfo> keyInfoList = {
        {JsonParsing::jsonVersionKey, QJsonValue::Double, true},
        {VisualMissionItem::jsonTypeKey, QJsonValue::String, true},
        {ComplexMissionItem::jsonComplexItemTypeKey, QJsonValue::String, true},
        {altitudeName, QJsonValue::Double, true},
        {lineSpacingName, QJsonValue::Double, true},
        {gridAngleName, QJsonValue::Double, true},
        {entryCornerName, QJsonValue::Double, true},
        {dropletClassName, QJsonValue::Double, true},
        {applicationRateName, QJsonValue::Double, true},
    };

    if (!JsonParsing::validateKeysStrict(complexObject, keyInfoList, errorString)) {
        qCWarning(AgriculturalSprayComplexItemLog) << "JSON validation failed"
                                                   << "error:" << errorString;
        _setStatus(GenerationError, errorString);
        return false;
    }
    if (complexObject[JsonParsing::jsonVersionKey].toDouble() != static_cast<double>(_jsonVersion)) {
        errorString = tr("Agricultural Spray version %1 is not supported.")
                          .arg(complexObject[JsonParsing::jsonVersionKey].toDouble());
        qCWarning(AgriculturalSprayComplexItemLog) << errorString;
        _setStatus(GenerationError, errorString);
        return false;
    }
    if (complexObject[VisualMissionItem::jsonTypeKey].toString() !=
            QLatin1String(VisualMissionItem::jsonTypeComplexItemValue) ||
        complexObject[ComplexMissionItem::jsonComplexItemTypeKey].toString() !=
            QLatin1String(jsonComplexItemTypeValue)) {
        errorString = tr("The JSON object is not an Agricultural Spray complex mission item.");
        qCWarning(AgriculturalSprayComplexItemLog) << errorString;
        _setStatus(GenerationError, errorString);
        return false;
    }
    if (!_metadataValid) {
        errorString = tr("Agricultural Spray Fact metadata could not be loaded.");
        qCWarning(AgriculturalSprayComplexItemLog) << errorString;
        _setStatus(GenerationError, errorString);
        return false;
    }

    const std::array<Fact*, 6> facts = {
        &_altitudeFact,    &_lineSpacingFact,  &_gridAngleFact,
        &_entryCornerFact, &_dropletClassFact, &_applicationRateFact,
    };
    const std::array<const char*, 6> keys = {
        altitudeName, lineSpacingName, gridAngleName, entryCornerName, dropletClassName, applicationRateName,
    };
    std::array<QVariant, 6> values;

    for (std::size_t index = 0; index < facts.size(); ++index) {
        if (!_validateLoadValue(*facts[index], complexObject[keys[index]], values[index], errorString)) {
            qCWarning(AgriculturalSprayComplexItemLog) << "JSON Fact validation failed"
                                                       << "name:" << facts[index]->name() << "error:" << errorString;
            _setStatus(GenerationError, errorString);
            return false;
        }
    }

    _loading = true;
    for (std::size_t index = 0; index < facts.size(); ++index) {
        facts[index]->setRawValue(values[index]);
    }
    _loading = false;

    setSequenceNumber(sequenceNumber);
    refreshAfterLoad();
    return true;
}
