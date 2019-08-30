#pragma once

#include <functional>
#include <memory>
#include <string>

#include "nod/Util.hpp"

namespace nod {

class DiscBase;

struct ExtractionContext final {
  bool force : 1;
  std::function<void(std::string_view, float)> progressCB;
};

std::unique_ptr<DiscBase> OpenDiscFromImage(SystemStringView path);
std::unique_ptr<DiscBase> OpenDiscFromImage(SystemStringView path, bool& isWii);

} // namespace nod
