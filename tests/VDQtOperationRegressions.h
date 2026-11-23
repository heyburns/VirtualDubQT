#pragma once
#include <QString>
class VDQtTestFixtures;
// Dispatch a named controller workflow using caller-owned temporary media.
// CMake registers the same scenario names as individual CTest cases.
bool VDQtRunOperationRegression(const QString& scenario, VDQtTestFixtures& fixtures);
