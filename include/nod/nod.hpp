#pragma once

#include <memory>
#include <functional>
#include "logvisor/logvisor.hpp"
#include "Util.hpp"

namespace nod {

class DiscBase;

struct ExtractionContext final {
  bool force : 1;
  std::function<void(std::string_view, float)> progressCB;
};

std::unique_ptr<DiscBase> OpenDiscFromImage(SystemStringView path);
std::unique_ptr<DiscBase> OpenDiscFromImage(SystemStringView path, bool& isWii);

} // namespace nod

#include "DiscGCN.hpp"
#include "DiscWii.hpp"
#include "IDiscIO.hpp"
