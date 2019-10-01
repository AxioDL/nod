#pragma once

#include "nod/DiscBase.hpp"

namespace nod {
class DiscBuilderWii;

class DiscWii : public DiscBase {
public:
  DiscWii(std::unique_ptr<IDiscIO>&& dio, bool& err);
  DiscBuilderWii makeMergeBuilder(SystemStringView outPath, bool dualLayer, FProgress progressCB);
  bool extractDiscHeaderFiles(SystemStringView path, const ExtractionContext& ctx) const override;
};

class DiscBuilderWii : public DiscBuilderBase {
public:
  DiscBuilderWii(SystemStringView outPath, bool dualLayer, FProgress progressCB);
  EBuildResult buildFromDirectory(SystemStringView dirIn);
  static std::optional<uint64_t> CalculateTotalSizeRequired(SystemStringView dirIn, bool& dualLayer);
};

class DiscMergerWii {
  DiscWii& m_sourceDisc;
  DiscBuilderWii m_builder;

public:
  DiscMergerWii(SystemStringView outPath, DiscWii& sourceDisc, bool dualLayer, FProgress progressCB);
  EBuildResult mergeFromDirectory(SystemStringView dirIn);
  static std::optional<uint64_t> CalculateTotalSizeRequired(DiscWii& sourceDisc, SystemStringView dirIn, bool& dualLayer);
};

} // namespace nod
