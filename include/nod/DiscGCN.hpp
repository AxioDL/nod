#pragma once

#include "nod/DiscBase.hpp"

namespace nod {
class DiscBuilderGCN;

class DiscGCN : public DiscBase {
  friend class DiscMergerGCN;
  DiscBuilderGCN makeMergeBuilder(std::string_view outPath, FProgress progressCB);

public:
  DiscGCN(std::unique_ptr<IDiscIO>&& dio, bool& err);
  bool extractDiscHeaderFiles(std::string_view path, const ExtractionContext& ctx) const override;
};

class DiscBuilderGCN : public DiscBuilderBase {
  friend class DiscMergerGCN;

public:
  DiscBuilderGCN(std::string_view outPath, FProgress progressCB);
  EBuildResult buildFromDirectory(std::string_view dirIn);
  static std::optional<uint64_t> CalculateTotalSizeRequired(std::string_view dirIn);
};

class DiscMergerGCN {
  DiscGCN& m_sourceDisc;
  DiscBuilderGCN m_builder;

public:
  DiscMergerGCN(std::string_view outPath, DiscGCN& sourceDisc, FProgress progressCB);
  EBuildResult mergeFromDirectory(std::string_view dirIn);
  static std::optional<uint64_t> CalculateTotalSizeRequired(DiscGCN& sourceDisc, std::string_view dirIn);
};

} // namespace nod
