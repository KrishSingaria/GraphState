#pragma once

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "gate/patch.hpp"
#include "gate/rules.hpp"
#include "lang/facts.hpp"
#include "lang/language.hpp"
#include "memory/session_ctx.hpp"

namespace gs {
class Engine;
}

namespace gs::gate {

struct Buffer {
  std::optional<std::string> pre;
  std::optional<std::string> post;
  lang::Language lang = lang::Language::Unknown;
  std::vector<lang::ByteRange> edited_ranges;
  lang::FileFacts pre_facts;
  lang::FileFacts post_facts;
  bool is_new = false;
  bool is_deleted = false;
  std::string moved_to;
};

class Gate {
public:
  explicit Gate(Engine& engine);
  ~Gate();

  PatchResult propose(const PatchRequest& req, const memory::SessionCtx& session = {});

  const RulesEngine& rules_engine() const { return rules_engine_; }
  RulesEngine& rules_engine() { return rules_engine_; }

private:
  Engine& engine_;
  RulesEngine rules_engine_;

  // Phase helpers
  void p0_preflight(const PatchRequest& req,
                    std::map<std::string, Buffer>& ws,
                    std::vector<Violation>& violations,
                    std::vector<Violation>& warnings);

  void p1_edit(const PatchRequest& req,
               std::map<std::string, Buffer>& ws,
               std::vector<Violation>& violations);

  void p2_syntax(std::map<std::string, Buffer>& ws,
                 std::vector<Violation>& violations,
                 std::vector<Violation>& warnings);

  void p3_symbol_diff(std::map<std::string, Buffer>& ws,
                      std::vector<FileChange>& changes,
                      std::vector<std::string>& targets,
                      std::vector<Violation>& warnings);

  void p4_blast_radius(const std::map<std::string, Buffer>& ws,
                       const std::vector<std::string>& targets,
                       std::vector<Violation>& violations,
                       std::vector<Violation>& warnings);

  void p5_quality_rules(const std::map<std::string, Buffer>& ws,
                        std::vector<Violation>& violations,
                        std::vector<Violation>& warnings);

  void p6_write(const std::string& patch_id,
                const PatchRequest& req,
                const memory::SessionCtx& session,
                std::map<std::string, Buffer>& ws,
                const std::vector<FileChange>& changes);

  void record_rejected(const std::string& patch_id,
                       const PatchRequest& req,
                       const memory::SessionCtx& session,
                       const std::string& failed_phase,
                       const std::vector<Violation>& violations,
                       const std::map<std::string, Buffer>& ws);

  std::vector<nlohmann::json> fetch_lessons(const std::vector<std::string>& targets,
                                           const std::map<std::string, Buffer>& ws);
};

} // namespace gs::gate
