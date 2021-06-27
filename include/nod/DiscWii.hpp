#pragma once

#include "nod/DiscBase.hpp"

namespace nod {
class DiscBuilderWii;

class DiscWii : public DiscBase {
public:
  DiscWii(std::unique_ptr<IDiscIO>&& dio, bool& err, Codepage_t codepage = CP_US_ASCII);
  DiscBuilderWii makeMergeBuilder(SystemStringView outPath, bool dualLayer, FProgress progressCB, Codepage_t codepage);
  bool extractDiscHeaderFiles(SystemStringView path, const ExtractionContext& ctx) const override;
};

class DiscBuilderWii : public DiscBuilderBase {
public:
  DiscBuilderWii(SystemStringView outPath, bool dualLayer, FProgress progressCB, Codepage_t codepage = CP_US_ASCII);
  EBuildResult buildFromDirectory(SystemStringView dirIn);
  static std::optional<uint64_t> CalculateTotalSizeRequired(SystemStringView dirIn, bool& dualLayer, Codepage_t codepage = CP_US_ASCII);
};

class DiscMergerWii {
  DiscWii& m_sourceDisc;
  DiscBuilderWii m_builder;

public:
  DiscMergerWii(SystemStringView outPath, DiscWii& sourceDisc, bool dualLayer, FProgress progressCB, Codepage_t codepage = CP_US_ASCII);
  EBuildResult mergeFromDirectory(SystemStringView dirIn);
  static std::optional<uint64_t> CalculateTotalSizeRequired(DiscWii& sourceDisc, SystemStringView dirIn, bool& dualLayer, Codepage_t codepage = CP_US_ASCII);
};

} // namespace nod
