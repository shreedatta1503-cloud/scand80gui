// ----------------------------------------------------------------------------
// Detector — evaluates a component's detect rule to decide whether it is
// already satisfied on the machine (present + up to date) or must be repaired.
// ----------------------------------------------------------------------------
#pragma once

#include "Config.h"
#include <string>

namespace qgc {

// Returns true if the component is considered present/satisfied. `installDir`
// is the launcher's install directory (file-relative rules resolve against it).
bool isComponentSatisfied(const Component &component, const std::wstring &installDir);

} // namespace qgc
