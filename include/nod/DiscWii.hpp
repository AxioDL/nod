#pragma once

#include "nod/DiscBase.hpp"

namespace nod {
class DiscBuilderWii;

class DiscWii : public DiscBase {
public:
  DiscWii(std::unique_ptr<IDiscIO>&& dio, bool& err);
  DiscBuilderWii makeMergeBuilder(std::string_view outPath, bool dualLayer, FProgress progressCB);
  bool extractDiscHeaderFiles(std::string_view path, const ExtractionContext& ctx) const override;
};

class DiscBuilderWii : public DiscBuilderBase {
public:
  DiscBuilderWii(std::string_view outPath, bool dualLayer, FProgress progressCB);
  EBuildResult buildFromDirectory(std::string_view dirIn);
  static std::optional<uint64_t> CalculateTotalSizeRequired(std::string_view dirIn, bool& dualLayer);
};

class DiscMergerWii {
  DiscWii& m_sourceDisc;
  DiscBuilderWii m_builder;

public:
  DiscMergerWii(std::string_view outPath, DiscWii& sourceDisc, bool dualLayer, FProgress progressCB);
  EBuildResult mergeFromDirectory(std::string_view dirIn);
  static std::optional<uint64_t> CalculateTotalSizeRequired(DiscWii& sourceDisc, std::string_view dirIn, bool& dualLayer);
};

} // namespace nod
