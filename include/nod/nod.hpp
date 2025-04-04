#pragma once

#include <functional>
#include <memory>
#include <string_view>

namespace nod {

class DiscBase;

struct ExtractionContext final {
  bool force : 1;
  std::function<void(std::string_view, float)> progressCB;
};

std::unique_ptr<DiscBase> OpenDiscFromImage(std::string_view path);
std::unique_ptr<DiscBase> OpenDiscFromImage(std::string_view path, bool& isWii);

} // namespace nod
