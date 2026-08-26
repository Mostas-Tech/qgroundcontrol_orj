#pragma once

#include <QtCore/QMap>
#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QVariantList>
#include <QtPositioning/QGeoCoordinate>

#include "Fact.h"
#include "QGCMapPolygon.h"
#include "QGCMapPolyline.h"

class FactMetaData;
class QmlObjectListModel;

class AgriculturalSprayField final : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString id READ id CONSTANT)
    Q_PROPERTY(QString name READ name WRITE setName NOTIFY nameChanged)
    Q_PROPERTY(QGCMapPolygon* polygon READ polygon CONSTANT)
    Q_PROPERTY(QGCMapPolyline* transitPolyline READ transitPolyline CONSTANT)
    Q_PROPERTY(QmlObjectListModel* nonSprayPolygons READ nonSprayPolygons CONSTANT)
    Q_PROPERTY(Fact* altitude READ altitude CONSTANT)
    Q_PROPERTY(Fact* lineSpacing READ lineSpacing CONSTANT)
    Q_PROPERTY(Fact* gridAngle READ gridAngle CONSTANT)
    Q_PROPERTY(Fact* entryCorner READ entryCorner CONSTANT)
    Q_PROPERTY(Fact* boundaryMargin READ boundaryMargin CONSTANT)
    Q_PROPERTY(Fact* boundaryMarginScope READ boundaryMarginScope CONSTANT)
    Q_PROPERTY(Fact* dropletClass READ dropletClass CONSTANT)
    Q_PROPERTY(Fact* applicationRate READ applicationRate CONSTANT)
    Q_PROPERTY(int directionVertexIndex READ directionVertexIndex WRITE setDirectionVertexIndex NOTIFY
                   directionVertexIndexChanged)
    Q_PROPERTY(int marginEdgeIndex READ marginEdgeIndex WRITE setMarginEdgeIndex NOTIFY marginEdgeIndexChanged)
    Q_PROPERTY(QVariantList marginEdgeIndices READ marginEdgeIndices NOTIFY marginEdgeIndicesChanged)
    Q_PROPERTY(QList<QGeoCoordinate> routeCoordinates READ routeCoordinates NOTIFY routeChanged)
    Q_PROPERTY(QList<int> routeSegmentTypes READ routeSegmentTypes NOTIFY routeChanged)
    Q_PROPERTY(int sprayLegCount READ sprayLegCount NOTIFY routeChanged)
    Q_PROPERTY(int status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusChanged)
    Q_PROPERTY(QString errorText READ errorText NOTIFY statusChanged)

public:
    AgriculturalSprayField(const QString& name, QObject* parent = nullptr);

    QString id() const { return _id; }
    void setId(const QString& id);
    QString name() const { return _name; }
    void setName(const QString& name);

    QGCMapPolygon* polygon() const { return _polygon; }
    QGCMapPolyline* transitPolyline() const { return _transitPolyline; }
    QmlObjectListModel* nonSprayPolygons() const { return _nonSprayPolygons; }

    Fact* altitude() { return &_altitudeFact; }
    const Fact* altitude() const { return &_altitudeFact; }
    Fact* lineSpacing() { return &_lineSpacingFact; }
    const Fact* lineSpacing() const { return &_lineSpacingFact; }
    Fact* gridAngle() { return &_gridAngleFact; }
    const Fact* gridAngle() const { return &_gridAngleFact; }
    Fact* entryCorner() { return &_entryCornerFact; }
    const Fact* entryCorner() const { return &_entryCornerFact; }
    Fact* boundaryMargin() { return &_boundaryMarginFact; }
    const Fact* boundaryMargin() const { return &_boundaryMarginFact; }
    Fact* boundaryMarginScope() { return &_boundaryMarginScopeFact; }
    const Fact* boundaryMarginScope() const { return &_boundaryMarginScopeFact; }
    Fact* dropletClass() { return &_dropletClassFact; }
    const Fact* dropletClass() const { return &_dropletClassFact; }
    Fact* applicationRate() { return &_applicationRateFact; }
    const Fact* applicationRate() const { return &_applicationRateFact; }

    int directionVertexIndex() const { return _directionVertexIndex; }
    void setDirectionVertexIndex(int index);
    int marginEdgeIndex() const { return _marginEdgeIndex; }
    void setMarginEdgeIndex(int index);
    QVariantList marginEdgeIndices() const;
    const QList<int>& marginEdgeIndexList() const { return _marginEdgeIndices; }
    double marginEdgeMargin(int index, double defaultMargin) const { return _marginEdgeMargins.value(index, defaultMargin); }
    void setMarginEdgeState(int marginEdgeIndex, const QList<int>& marginEdgeIndices,
                            const QMap<int, double>& marginEdgeMargins);

    QList<QGeoCoordinate> routeCoordinates() const { return _routeCoordinates; }
    QList<int> routeSegmentTypes() const { return _routeSegmentTypes; }
    int sprayLegCount() const { return _sprayLegCount; }
    int status() const { return _status; }
    QString statusText() const { return _statusText; }
    QString errorText() const { return _errorText; }

    void setRoute(const QList<QGeoCoordinate>& coordinates, const QList<int>& segmentTypes, int sprayLegCount);
    void setStatus(int status, const QString& statusText, const QString& errorText);
    Fact* fact(const QString& name);

signals:
    void nameChanged();
    void routeChanged();
    void statusChanged();
    void directionVertexIndexChanged();
    void marginEdgeIndexChanged();
    void marginEdgeIndicesChanged();
    void fieldMarginRowsChanged();

private:
    bool _initializeFact(Fact& fact, const char* name);

    QString _id;
    QString _name;
    QGCMapPolygon* const _polygon;
    QGCMapPolyline* const _transitPolyline;
    QmlObjectListModel* const _nonSprayPolygons;
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
    QList<QGeoCoordinate> _routeCoordinates;
    QList<int> _routeSegmentTypes;
    int _sprayLegCount = 0;
    int _status = 0;
    QString _statusText;
    QString _errorText;
};
