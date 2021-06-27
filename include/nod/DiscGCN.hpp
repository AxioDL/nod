#pragma once

#include "nod/DiscBase.hpp"

namespace nod {
class DiscBuilderGCN;

class DiscGCN : public DiscBase {
  friend class DiscMergerGCN;
  DiscBuilderGCN makeMergeBuilder(SystemStringView outPath, FProgress progressCB, Codepage_t codepage);

public:
  DiscGCN(std::unique_ptr<IDiscIO>&& dio, bool& err, Codepage_t codepage = CP_US_ASCII);
  bool extractDiscHeaderFiles(SystemStringView path, const ExtractionContext& ctx) const override;
};

class DiscBuilderGCN : public DiscBuilderBase {
  friend class DiscMergerGCN;

public:
  DiscBuilderGCN(SystemStringView outPath, FProgress progressCB, Codepage_t codepage = CP_US_ASCII);
  EBuildResult buildFromDirectory(SystemStringView dirIn);
  static std::optional<uint64_t> CalculateTotalSizeRequired(SystemStringView dirIn, Codepage_t codepage = CP_US_ASCII);
};

class DiscMergerGCN {
  DiscGCN& m_sourceDisc;
  DiscBuilderGCN m_builder;

public:
  DiscMergerGCN(SystemStringView outPath, DiscGCN& sourceDisc, FProgress progressCB, Codepage_t codepage = CP_US_ASCII);
  EBuildResult mergeFromDirectory(SystemStringView dirIn);
  static std::optional<uint64_t> CalculateTotalSizeRequired(DiscGCN& sourceDisc, SystemStringView dirIn, Codepage_t codepage = CP_US_ASCII);
};

} // namespace nod
