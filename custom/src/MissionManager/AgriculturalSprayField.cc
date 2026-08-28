#include "AgriculturalSprayField.h"

#include "FactMetaData.h"
#include "QGCLoggingCategory.h"
#include "QmlObjectListModel.h"
#include <QtCore/QUuid>

QGC_LOGGING_CATEGORY(AgriculturalSprayFieldLog, "qgc.custom.agriculturalsprayfield")

namespace {
constexpr const char* altitudeName = "Altitude";
constexpr const char* lineSpacingName = "LineSpacing";
constexpr const char* gridAngleName = "GridAngle";
constexpr const char* entryCornerName = "EntryCorner";
constexpr const char* boundaryMarginName = "BoundaryMargin";
constexpr const char* boundaryMarginScopeName = "BoundaryMarginScope";
constexpr const char* dropletClassName = "DropletClass";
constexpr const char* applicationRateName = "ApplicationRate";
}

AgriculturalSprayField::AgriculturalSprayField(const QString& name, QObject* parent)
    : QObject(parent),
      _id(QUuid::createUuid().toString(QUuid::WithoutBraces)),
      _name(name),
      _polygon(new QGCMapPolygon(this)),
    _transitPolyline(new QGCMapPolyline(this)),
      _nonSprayPolygons(new QmlObjectListModel(this)),
      _metaDataMap(FactMetaData::createMapFromJsonFile(QStringLiteral(":/json/AgriculturalSpray.SettingsGroup.json"), this)),
      _altitudeFact(0, QString::fromLatin1(altitudeName), FactMetaData::valueTypeDouble),
      _lineSpacingFact(0, QString::fromLatin1(lineSpacingName), FactMetaData::valueTypeDouble),
      _gridAngleFact(0, QString::fromLatin1(gridAngleName), FactMetaData::valueTypeDouble),
      _entryCornerFact(0, QString::fromLatin1(entryCornerName), FactMetaData::valueTypeUint32),
      _boundaryMarginFact(0, QString::fromLatin1(boundaryMarginName), FactMetaData::valueTypeDouble),
      _boundaryMarginScopeFact(0, QString::fromLatin1(boundaryMarginScopeName), FactMetaData::valueTypeUint32),
      _dropletClassFact(0, QString::fromLatin1(dropletClassName), FactMetaData::valueTypeUint32),
      _applicationRateFact(0, QString::fromLatin1(applicationRateName), FactMetaData::valueTypeDouble)
{
    for (Fact* fact : {&_altitudeFact, &_lineSpacingFact, &_gridAngleFact, &_entryCornerFact, &_boundaryMarginFact,
                       &_boundaryMarginScopeFact, &_dropletClassFact, &_applicationRateFact}) {
        _initializeFact(*fact, fact->name().toLatin1().constData());
    }
}

void AgriculturalSprayField::setId(const QString& id)
{
    if (!id.trimmed().isEmpty()) {
        _id = id.trimmed();
    }
}

void AgriculturalSprayField::setName(const QString& name)
{
    const QString trimmedName = name.trimmed();
    if (trimmedName.isEmpty() || _name == trimmedName) {
        return;
    }
    _name = trimmedName;
    emit nameChanged();
}

void AgriculturalSprayField::setDirectionVertexIndex(int index)
{
    if (_directionVertexIndex == index) {
        return;
    }
    _directionVertexIndex = index;
    emit directionVertexIndexChanged();
}

void AgriculturalSprayField::setMarginEdgeIndex(int index)
{
    if (_marginEdgeIndex == index) {
        return;
    }
    _marginEdgeIndex = index;
    emit marginEdgeIndexChanged();
}

QVariantList AgriculturalSprayField::marginEdgeIndices() const
{
    QVariantList indices;
    indices.reserve(_marginEdgeIndices.size());
    for (const int index : _marginEdgeIndices) {
        indices.append(index);
    }
    return indices;
}

void AgriculturalSprayField::setMarginEdgeState(int marginEdgeIndex, const QList<int>& marginEdgeIndices,
                                                const QMap<int, double>& marginEdgeMargins)
{
    const bool indexChanged = _marginEdgeIndex != marginEdgeIndex;
    const bool indicesChanged = _marginEdgeIndices != marginEdgeIndices;
    const bool marginsChanged = _marginEdgeMargins != marginEdgeMargins;
    _marginEdgeIndex = marginEdgeIndex;
    _marginEdgeIndices = marginEdgeIndices;
    _marginEdgeMargins = marginEdgeMargins;
    if (indexChanged) emit marginEdgeIndexChanged();
    if (indicesChanged) emit marginEdgeIndicesChanged();
    if (marginsChanged) emit fieldMarginRowsChanged();
}

void AgriculturalSprayField::setRoute(const QList<QGeoCoordinate>& coordinates, const QList<int>& segmentTypes,
                                      int sprayLegCount)
{
    _routeCoordinates = coordinates;
    _routeSegmentTypes = segmentTypes;
    _sprayLegCount = sprayLegCount;
    emit routeChanged();
}

void AgriculturalSprayField::setStatus(int status, const QString& statusText, const QString& errorText)
{
    if (_status == status && _statusText == statusText && _errorText == errorText) {
        return;
    }
    _status = status;
    _statusText = statusText;
    _errorText = errorText;
    emit statusChanged();
}

Fact* AgriculturalSprayField::fact(const QString& name)
{
    if (name == QLatin1String(altitudeName)) return altitude();
    if (name == QLatin1String(lineSpacingName)) return lineSpacing();
    if (name == QLatin1String(gridAngleName)) return gridAngle();
    if (name == QLatin1String(entryCornerName)) return entryCorner();
    if (name == QLatin1String(boundaryMarginName)) return boundaryMargin();
    if (name == QLatin1String(boundaryMarginScopeName)) return boundaryMarginScope();
    if (name == QLatin1String(dropletClassName)) return dropletClass();
    if (name == QLatin1String(applicationRateName)) return applicationRate();
    return nullptr;
}

bool AgriculturalSprayField::_initializeFact(Fact& fact, const char* name)
{
    FactMetaData* const metaData = _metaDataMap.value(QString::fromLatin1(name), nullptr);
    if (!metaData) {
        qCWarning(AgriculturalSprayFieldLog) << "Missing Fact metadata" << "name:" << name;
        return false;
    }
    fact.setMetaData(metaData, true);
    return true;
}
