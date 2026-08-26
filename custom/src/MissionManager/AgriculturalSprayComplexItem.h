#pragma once

#include <QtCore/QFutureWatcher>
#include <QtCore/QList>
#include <QtCore/QLoggingCategory>
#include <QtCore/QMap>
#include <QtCore/QMetaObject>
#include <QtCore/QPointer>
#include <QtCore/QString>
#include <QtCore/QVariantList>
#include <QtPositioning/QGeoCoordinate>
#include <optional>

#include "AgriculturalSprayPlanner.h"
#include "ComplexMissionItem.h"
#include "Fact.h"

class GeoFenceController;
class FactMetaData;
class MissionItem;
class PlanMasterController;
class QGCMapPolygon;
class QGCFenceCircle;
class QGCFencePolygon;
class QmlObjectListModel;

Q_DECLARE_LOGGING_CATEGORY(AgriculturalSprayComplexItemLog)

class AgriculturalSprayComplexItem final : public ComplexMissionItem
{
    Q_OBJECT

    Q_PROPERTY(Fact* altitude READ altitude CONSTANT)
    Q_PROPERTY(Fact* relativeAltitude READ altitude CONSTANT)
    Q_PROPERTY(Fact* lineSpacing READ lineSpacing CONSTANT)
    Q_PROPERTY(Fact* gridAngle READ gridAngle CONSTANT)
    Q_PROPERTY(Fact* entryCorner READ entryCorner CONSTANT)
    Q_PROPERTY(Fact* boundaryMargin READ boundaryMargin CONSTANT)
    Q_PROPERTY(Fact* boundaryMarginScope READ boundaryMarginScope CONSTANT)
    Q_PROPERTY(int directionVertexIndex READ directionVertexIndex WRITE setDirectionVertexIndex NOTIFY
                   directionVertexIndexChanged)
    Q_PROPERTY(int marginEdgeIndex READ marginEdgeIndex WRITE setMarginEdgeIndex NOTIFY marginEdgeIndexChanged)
    Q_PROPERTY(QVariantList marginEdgeIndices READ marginEdgeIndices NOTIFY marginEdgeIndicesChanged)
    Q_PROPERTY(QVariantList fieldMarginRows READ fieldMarginRows NOTIFY fieldMarginRowsChanged)
    Q_PROPERTY(QList<QGeoCoordinate> sourcePolygonCoordinates READ sourcePolygonCoordinates NOTIFY
                   sourcePolygonCoordinatesChanged)
    Q_PROPERTY(QGeoCoordinate directionEdgeStart READ directionEdgeStart NOTIFY directionEdgeChanged)
    Q_PROPERTY(QGeoCoordinate directionEdgeEnd READ directionEdgeEnd NOTIFY directionEdgeChanged)
    Q_PROPERTY(QGeoCoordinate marginEdgeStart READ marginEdgeStart NOTIFY marginEdgeChanged)
    Q_PROPERTY(QGeoCoordinate marginEdgeEnd READ marginEdgeEnd NOTIFY marginEdgeChanged)
    Q_PROPERTY(bool sourcePolygonTraceMode READ sourcePolygonTraceMode NOTIFY sourcePolygonTraceModeChanged)
    Q_PROPERTY(Fact* dropletClass READ dropletClass CONSTANT)
    Q_PROPERTY(Fact* applicationRate READ applicationRate CONSTANT)
    Q_PROPERTY(QVariantList exclusionMarginRows READ exclusionMarginRows NOTIFY exclusionMarginRowsChanged)
    Q_PROPERTY(QmlObjectListModel* nonSprayPolygons READ nonSprayPolygons CONSTANT)
    Q_PROPERTY(Status status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
    Q_PROPERTY(QString errorText READ errorText NOTIFY errorTextChanged)
    Q_PROPERTY(QList<QGeoCoordinate> routeCoordinates READ routeCoordinates NOTIFY routeCoordinatesChanged)
    Q_PROPERTY(QList<int> routeSegmentTypes READ routeSegmentTypes NOTIFY routeSegmentTypesChanged)
    Q_PROPERTY(int sprayLegCount READ sprayLegCount NOTIFY sprayLegCountChanged)

public:
    explicit AgriculturalSprayComplexItem(PlanMasterController* masterController, bool flyView);

    enum EntryCorner
    {
        TopLeft = 0,
        TopRight,
        BottomLeft,
        BottomRight,
    };
    Q_ENUM(EntryCorner)

    enum BoundaryMarginScope
    {
        SelectedEdge = 0,
        AllEdges,
    };
    Q_ENUM(BoundaryMarginScope)

    enum DropletClass
    {
        Fine = 1,
        Medium = 2,
        Coarse = 3,
    };
    Q_ENUM(DropletClass)
    static constexpr DropletClass UltraCoarse = Coarse;  // Source compatibility for callers of the retired class.

    enum Status
    {
        NoInclusion,
        InvalidArea,
        EmptyEffectiveArea,
        NoSafeRoute,
        Generating,
        Ready,
        GenerationError,
    };
    Q_ENUM(Status)

    enum RouteSegmentType
    {
        SprayLeg,
        Transit,
        NonSpray,
    };
    Q_ENUM(RouteSegmentType)

    Fact* altitude() { return &_altitudeFact; }

    Fact* lineSpacing() { return &_lineSpacingFact; }

    Fact* gridAngle() { return &_gridAngleFact; }

    Fact* entryCorner() { return &_entryCornerFact; }

    Fact* boundaryMargin() { return &_boundaryMarginFact; }

    Fact* boundaryMarginScope() { return &_boundaryMarginScopeFact; }

    int directionVertexIndex() const { return _directionVertexIndex; }

    int marginEdgeIndex() const { return _marginEdgeIndex; }

    QVariantList marginEdgeIndices() const;

    QVariantList fieldMarginRows() const;

    QList<QGeoCoordinate> sourcePolygonCoordinates() const;

    QGeoCoordinate directionEdgeStart() const;

    QGeoCoordinate directionEdgeEnd() const;

    QGeoCoordinate marginEdgeStart() const;

    QGeoCoordinate marginEdgeEnd() const;

    bool sourcePolygonTraceMode() const;

    Fact* dropletClass() { return &_dropletClassFact; }

    Fact* applicationRate() { return &_applicationRateFact; }

    Status status() const { return _status; }

    QString statusText() const { return _statusText; }

    QString errorText() const { return _errorText; }

    QList<QGeoCoordinate> routeCoordinates() const { return _routeCoordinates; }

    QList<int> routeSegmentTypes() const { return _routeSegmentTypes; }

    int sprayLegCount() const { return _sprayLegCount; }

    QVariantList exclusionMarginRows() const;

    QmlObjectListModel* nonSprayPolygons() const { return _nonSprayPolygons; }

    Q_INVOKABLE void rebuild();
    Q_INVOKABLE QObject* addNonSprayPolygon();
    Q_INVOKABLE void removeNonSprayPolygon(QObject* polygon);
    Q_INVOKABLE void setDirectionVertexIndex(int index);
    Q_INVOKABLE void setMarginEdgeIndex(int index);
    Q_INVOKABLE void toggleMarginEdgeIndex(int index);
    Q_INVOKABLE void setFieldMargin(int index, double margin);
    Q_INVOKABLE void setExclusionMargin(QObject* shape, double margin);

    /// Re-snapshots the fully loaded GeoFence without making the mission item dirty.
    void refreshAfterLoad();
    void beginInteractiveCreation() final;

    // ComplexMissionItem overrides
    QString patternName() const final { return tr(canonicalName); }

    double complexDistance() const final { return _complexDistance; }

    double minAMSLAltitude() const final;
    double maxAMSLAltitude() const final;
    int lastSequenceNumber() const final;
    bool load(const QJsonObject& complexObject, int sequenceNumber, QString& errorString) final;
    double greatestDistanceTo(const QGeoCoordinate& other) const final;

    QString mapVisualQML() const final { return QStringLiteral("qrc:/qml/Custom/Plan/AgriculturalSprayMapVisual.qml"); }

    // VisualMissionItem overrides
    bool dirty() const final { return _dirty; }

    bool isSimpleItem() const final { return false; }

    bool isStandaloneCoordinate() const final { return false; }

    bool specifiesCoordinate() const final { return _status == Ready && !_routeCoordinates.isEmpty(); }

    bool specifiesAltitudeOnly() const final { return false; }

    QString commandDescription() const final { return tr(canonicalName); }

    QString commandName() const final { return tr(canonicalName); }

    QString abbreviation() const final { return tr("AS"); }

    QGeoCoordinate coordinate() const final { return entryCoordinate(); }

    QGeoCoordinate entryCoordinate() const final;
    QGeoCoordinate exitCoordinate() const final;
    bool exitCoordinateSameAsEntry() const final;

    double editableAlt() const final { return _altitudeFact.rawValue().toDouble(); }

    double amslEntryAlt() const final;

    double amslExitAlt() const final { return amslEntryAlt(); }

    int sequenceNumber() const final { return _sequenceNumber; }

    double specifiedFlightSpeed() final;
    double specifiedGimbalYaw() final;
    double specifiedGimbalPitch() final;
    void appendMissionItems(QList<MissionItem*>& items, QObject* missionItemParent) final;
    void setMissionFlightStatus(MissionFlightStatus_t& missionFlightStatus) final;
    void applyNewAltitude(double newAltitude) final;

    double additionalTimeDelay() const final { return 0.0; }

    ReadyForSaveState readyForSaveState() const final;
    void setDirty(bool dirty) final;
    void setCoordinate(const QGeoCoordinate& coordinate) final;
    void setSequenceNumber(int sequenceNumber) final;
    void save(QJsonArray& missionItems) final;

    static constexpr const char* canonicalName = QT_TR_NOOP("Agricultural Spray");
    static constexpr const char* jsonComplexItemTypeValue = "AgriculturalSpray";
    static constexpr const char* settingsGroup = "AgriculturalSpray";
    static constexpr const char* altitudeName = "Altitude";
    static constexpr const char* lineSpacingName = "LineSpacing";
    static constexpr const char* gridAngleName = "GridAngle";
    static constexpr const char* entryCornerName = "EntryCorner";
    static constexpr const char* boundaryMarginName = "BoundaryMargin";
    static constexpr const char* boundaryMarginScopeName = "BoundaryMarginScope";
    static constexpr const char* dropletClassName = "DropletClass";
    static constexpr const char* applicationRateName = "ApplicationRate";

signals:
    void statusChanged(Status status);
    void statusTextChanged();
    void errorTextChanged();
    void routeCoordinatesChanged();
    void routeSegmentTypesChanged();
    void sprayLegCountChanged();
    void directionVertexIndexChanged();
    void marginEdgeIndexChanged();
    void marginEdgeIndicesChanged();
    void fieldMarginRowsChanged();
    void sourcePolygonCoordinatesChanged();
    void directionEdgeChanged();
    void marginEdgeChanged();
    void sourcePolygonTraceModeChanged();
    void exclusionMarginRowsChanged();

protected:
    bool coordinateTerrainAltitudeQueryEnabled() const final { return false; }

private slots:
    void _inputChanged();
    void _fenceInputChanged();
    void _fenceModelChanged();
    void _rebuildQueued();
    void _plannedHomePositionChanged();
    void _plannerFinished();

private:
    bool _initializeFact(Fact& fact, const char* name);
    bool _validateCurrentFacts(QString& errorText);
    bool _validateLoadValue(Fact& fact, const QJsonValue& jsonValue, QVariant& typedValue, QString& errorText);
    bool _snapshotPlannerInput(AgriculturalSpray::PlannerInput& input, QGeoCoordinate& origin, Status& failureStatus,
                               QString& errorText);
    void _connectFenceModels();
    void _reconnectShapeSignals();
    void _connectPolygonSignals(QGCFencePolygon* polygon);
    void _connectCircleSignals(QGCFenceCircle* circle);
    void _connectNonSprayPolygon(QGCMapPolygon* polygon);
    void _nonSprayPolygonChanged();
    void _sourcePolygonTraceModeChanged(QGCFencePolygon* polygon, bool traceMode);
    void _invalidateRoute();
    void _scheduleRebuild();
    void _clearGeneratedRoute();
    void _publishPlannerResult(const AgriculturalSpray::PlannerResult& result, const QGeoCoordinate& origin);
    void _publishSuccessfulRoute(const AgriculturalSpray::PlannerResult& result, const QGeoCoordinate& origin);
    void _setStatus(Status status, const QString& errorText);
    QString _textForStatus(Status status, const QString& errorText) const;
    void _rebuildFlightPathSegments();
    void _updateBoundingCube();
    void _emitSuccessfulRouteNotifications(int previousLastSequenceNumber);
    bool _resolveSourcePolygon(QString& errorText);
    int _sourcePolygonIndex() const;
    bool _normalizeLoadedDropletClass(QJsonValue jsonValue, QVariant& typedValue, QString& errorText);
    bool _resolveLoadedExclusionMargins(QString& errorText);
    void _normalizeDirectionVertexIndex();
    void _normalizeMarginEdgeIndex();
    void _migrateLegacyDirectionSelection();
    void _startPendingPlanner();
    int _missionItemCount() const;
    double _exclusionMargin(const QObject* shape) const;
    void _pruneExclusionMargins();

    struct PendingPlan
    {
        AgriculturalSpray::PlannerInput input;
        QGeoCoordinate origin;
        quint64 revision = 0;
    };

    QMap<QString, FactMetaData*> _metaDataMap;
    Fact _altitudeFact;
    Fact _lineSpacingFact;
    Fact _gridAngleFact;
    Fact _entryCornerFact;
    Fact _boundaryMarginFact;
    Fact _boundaryMarginScopeFact;
    Fact _dropletClassFact;
    Fact _applicationRateFact;

    int _directionVertexIndex = 0;
    int _marginEdgeIndex = 0;
    QList<int> _marginEdgeIndices{0};
    QMap<int, double> _marginEdgeMargins{{0, 1.0}};
    bool _legacyDirectionPending = false;
    bool _loadedDirectionRequiresValidation = false;
    bool _loadedMarginEdgeRequiresValidation = false;
    double _legacyGridAngleDegrees = 0.0;
    EntryCorner _legacyEntryCorner = TopLeft;

    GeoFenceController* _geoFenceController = nullptr;
    QmlObjectListModel* _polygonModel = nullptr;
    QmlObjectListModel* _circleModel = nullptr;
    QPointer<QGCFencePolygon> _sourcePolygon;
    int _loadedSourcePolygonIndex = -1;
    bool _sourcePolygonReferencePresent = false;
    bool _sourcePolygonResolved = false;
    QList<QMetaObject::Connection> _shapeConnections;
    QMap<QObject*, double> _exclusionMargins;
    QMap<int, double> _loadedPolygonExclusionMargins;
    QMap<int, double> _loadedCircleExclusionMargins;
    bool _loadedExclusionMarginsPending = false;
    double _loadedExclusionMarginDefault = 1.0;
    double _defaultExclusionMargin = 1.0;
    QmlObjectListModel* _nonSprayPolygons = nullptr;

    QList<QGeoCoordinate> _routeCoordinates;
    QList<int> _routeSegmentTypes;
    Status _status = NoInclusion;
    QString _statusText;
    QString _errorText;
    int _sequenceNumber = 0;
    int _sprayLegCount = 0;
    double _complexDistance = 0.0;
    bool _metadataValid = true;
    bool _shapeModelsValid = true;
    bool _shapeConnectionsDirty = true;
    bool _rebuildPending = false;
    bool _loading = false;
    bool _loadedFromJson = false;
    bool _postLoadRefreshReceived = false;
    bool _sourceResolutionRetryPending = false;

    QFutureWatcher<AgriculturalSpray::PlannerResult> _plannerWatcher;
    std::optional<PendingPlan> _pendingPlan;
    std::optional<PendingPlan> _runningPlan;
    quint64 _plannerRevision = 0;

    static constexpr int _jsonVersion = 7;
    static constexpr int _nonSprayPolygonJsonVersion = 7;
    static constexpr int _perEdgeMarginJsonVersion = 6;
    static constexpr int _multiEdgeJsonVersion = 5;
    static constexpr int _exclusionMarginJsonVersion = 4;
    static constexpr int _boundaryMarginJsonVersion = 3;
    static constexpr int _directionJsonVersion = 2;
    static constexpr int _legacyJsonVersion = 1;
    static constexpr const char* _sourcePolygonIndexKey = "sourcePolygonIndex";
    static constexpr const char* _directionVertexIndexKey = "directionVertexIndex";
    static constexpr const char* _marginEdgeIndexKey = "marginEdgeIndex";
    static constexpr const char* _marginEdgeIndicesKey = "marginEdgeIndices";
    static constexpr const char* _marginEdgeMarginsKey = "marginEdgeMargins";
    static constexpr const char* _exclusionMarginsKey = "exclusionMargins";
    static constexpr const char* _nonSprayPolygonsKey = "nonSprayPolygons";
};
