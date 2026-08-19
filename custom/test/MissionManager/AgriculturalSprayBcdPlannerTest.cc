#include "AgriculturalSprayBcdPlannerTest.h"

#include <QtCore/QElapsedTimer>
#include <QtTest/QTest>
#include <cmath>
#include <variant>

#include "AgriculturalSprayPlanner.h"

namespace {

using namespace AgriculturalSpray;

Polygon rectangle(double minimumNorth, double maximumNorth, double minimumEast, double maximumEast)
{
    return {{{minimumNorth, minimumEast},
             {maximumNorth, minimumEast},
             {maximumNorth, maximumEast},
             {minimumNorth, maximumEast}}};
}

PlannerInput scenarioInput(int scenario, int directionVertexIndex)
{
    PlannerInput input;
    input.inclusions = {rectangle(0.0, 200.0, 0.0, 160.0)};
    input.spacing = 8.0;
    switch (scenario) {
        case 1:
            input.exclusions = {Circle{{100.0, 80.0}, 40.0}};
            break;
        case 2:
            input.exclusions = {rectangle(70.0, 130.0, 50.0, 110.0)};
            break;
        case 3:
            input.inclusions = {
                Polygon{{{0.0, 0.0}, {200.0, 0.0}, {200.0, 60.0}, {90.0, 60.0}, {90.0, 160.0}, {0.0, 160.0}}}};
            break;
        case 4:
            input.exclusions = {Circle{{65.0, 55.0}, 25.0}, rectangle(115.0, 160.0, 90.0, 135.0)};
            break;
        case 5:
            input.exclusions = {rectangle(70.0, 130.0, 0.0, 68.0), rectangle(70.0, 130.0, 92.0, 160.0)};
            break;
        case 6:
            input.exclusions = {rectangle(5.0, 45.0, 5.0, 42.0)};
            break;
        case 7:
            input.inclusions = {rectangle(0.0, 160.0, 0.0, 120.0)};
            input.exclusions = {Circle{{40.0, 35.0}, 15.0}, Circle{{125.0, 85.0}, 18.0},
                                rectangle(70.0, 95.0, 50.0, 72.0)};
            input.limits.exactCellLimit = 1;
            break;
        default:
            break;
    }

    const Polygon& source = std::get<Polygon>(input.inclusions.front());
    const std::size_t vertexIndex = static_cast<std::size_t>(directionVertexIndex);
    input.entryPoint = source.vertices[vertexIndex];
    input.sweepDirection = source.vertices[(vertexIndex + 1) % source.vertices.size()] - *input.entryPoint;
    return input;
}

bool sameResult(const PlannerResult& first, const PlannerResult& second)
{
    if (first.status != second.status || first.method != second.method ||
        first.usedLegacyFallback != second.usedLegacyFallback || first.route.size() != second.route.size() ||
        first.legs.size() != second.legs.size()) {
        return false;
    }
    for (std::size_t index = 0; index < first.route.size(); ++index) {
        if (first.route[index].type != second.route[index].type ||
            distance(first.route[index].position, second.route[index].position) > 1e-8) {
            return false;
        }
    }
    return true;
}

}  // namespace

void AgriculturalSprayBcdPlannerTest::_eightMeterScenarios_data()
{
    QTest::addColumn<int>("scenario");
    QTest::addColumn<int>("directionVertexIndex");

    QTest::newRow("unobstructed-rectangle") << 0 << 0;
    QTest::newRow("large-circle") << 1 << 0;
    QTest::newRow("large-polygon-corner-0") << 2 << 0;
    QTest::newRow("large-polygon-corner-2") << 2 << 2;
    QTest::newRow("l-field") << 3 << 0;
    QTest::newRow("two-obstacles") << 4 << 0;
    QTest::newRow("narrow-corridor") << 5 << 0;
    QTest::newRow("edge-obstacle") << 6 << 0;
    QTest::newRow("seeded-stress") << 7 << 0;
}

void AgriculturalSprayBcdPlannerTest::_eightMeterScenarios()
{
    QFETCH(int, scenario);
    QFETCH(int, directionVertexIndex);

    const PlannerInput input = scenarioInput(scenario, directionVertexIndex);
    QElapsedTimer timer;
    timer.start();
    const PlannerResult result = plan(input);
    const qint64 elapsedMilliseconds = timer.elapsed();

    QCOMPARE(static_cast<int>(result.status), static_cast<int>(PlannerStatus::Success));
    QVERIFY(!result.route.empty());
    QVERIFY(!result.legs.empty());
    QVERIFY(distance(result.route.front().position, *input.entryPoint) <= 1e-8);
    QVERIFY(result.activeCellCount >= 1);
    QVERIFY(result.sprayDistance > 0.0);
    QVERIFY(result.estimatedTime > 0.0);
    QVERIFY(result.transitDistance >= 0.0);
    QVERIFY2(elapsedMilliseconds < 30000, qPrintable(QStringLiteral("Planner took %1 ms").arg(elapsedMilliseconds)));

    const double sweepLength = length(*input.sweepDirection);
    const Point normalizedSweep{input.sweepDirection->north / sweepLength, input.sweepDirection->east / sweepLength};
    for (const SprayLeg& leg : result.legs) {
        const Point legDirection = leg.end - leg.start;
        QVERIFY(length(legDirection) > 1e-8);
        const double crossProduct =
            legDirection.north * normalizedSweep.east - legDirection.east * normalizedSweep.north;
        QVERIFY(std::abs(crossProduct) <= 1e-6 * length(legDirection));
    }
    const Point firstLegDirection = result.legs.front().end - result.legs.front().start;
    const double firstDirectionDot =
        firstLegDirection.north * normalizedSweep.north + firstLegDirection.east * normalizedSweep.east;
    QVERIFY(firstDirectionDot > 0.0);
    for (std::size_t index = 1; index < result.route.size(); ++index) {
        QVERIFY(distance(result.route[index - 1].position, result.route[index].position) > 1e-8);
    }

    const PlannerResult repeated = plan(input);
    QVERIFY(sameResult(result, repeated));

    if (scenario == 0) {
        QCOMPARE(static_cast<int>(result.method), static_cast<int>(PlannerMethod::ExactCellOptimization));
        QVERIFY(!result.usedLegacyFallback);
    } else if (scenario == 7) {
        QCOMPARE(static_cast<int>(result.method), static_cast<int>(PlannerMethod::HeuristicCellOptimization));
        QVERIFY(!result.usedLegacyFallback);
    }
}

UT_REGISTER_TEST(AgriculturalSprayBcdPlannerTest, TestLabel::Unit, TestLabel::MissionManager)
