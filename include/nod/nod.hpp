#pragma once

#include <functional>
#include <memory>
#include <string>

#include "nod/Util.hpp"

namespace nod {

class DiscBase;

struct ExtractionContext final {
  bool force : 1;
  std::function<void(nod::SystemStringView, float)> progressCB;
};

std::unique_ptr<DiscBase> OpenDiscFromImage(SystemStringView path);
std::unique_ptr<DiscBase> OpenDiscFromImage(SystemStringView path, bool& isWii, Codepage_t codepage = CP_US_ASCII);

} // namespace nod
