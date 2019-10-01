#pragma once

#include "nod/DiscBase.hpp"

namespace nod {
class DiscBuilderGCN;

class DiscGCN : public DiscBase {
  friend class DiscMergerGCN;
  DiscBuilderGCN makeMergeBuilder(SystemStringView outPath, FProgress progressCB);

public:
  DiscGCN(std::unique_ptr<IDiscIO>&& dio, bool& err);
  bool extractDiscHeaderFiles(SystemStringView path, const ExtractionContext& ctx) const override;
};

class DiscBuilderGCN : public DiscBuilderBase {
  friend class DiscMergerGCN;

public:
  DiscBuilderGCN(SystemStringView outPath, FProgress progressCB);
  EBuildResult buildFromDirectory(SystemStringView dirIn);
  static std::optional<uint64_t> CalculateTotalSizeRequired(SystemStringView dirIn);
};

class DiscMergerGCN {
  DiscGCN& m_sourceDisc;
  DiscBuilderGCN m_builder;

public:
  DiscMergerGCN(SystemStringView outPath, DiscGCN& sourceDisc, FProgress progressCB);
  EBuildResult mergeFromDirectory(SystemStringView dirIn);
  static std::optional<uint64_t> CalculateTotalSizeRequired(DiscGCN& sourceDisc, SystemStringView dirIn);
};

} // namespace nod
