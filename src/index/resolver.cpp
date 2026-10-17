#include "index/resolver.hpp"
#include "index/fact_store.hpp"
#include "core/config.hpp"

#include <algorithm>

namespace gs::index {

namespace {

static constexpr std::string_view COMMON_NAMES_DATA[] = {
  "get", "set", "add", "run", "update", "close", "open", "read", "write",
  "save", "load", "append", "extend", "pop", "items", "keys", "values",
  "copy", "clear", "find", "format", "join", "split", "strip", "replace",
  "send", "start", "stop", "init", "main", "call", "apply", "map",
  "filter", "reduce", "push", "then", "catch", "next", "emit", "log",
  "info", "warn", "error", "debug", "render", "handle", "process",
  "execute", "parse", "build", "create", "delete", "remove", "insert",
  "reset", "test"
};

std::string file_of_symbol(const lang::SymbolFact* sym) {
  if (!sym) return "";
  size_t sep = sym->id.find("::");
  if (sep == std::string::npos) return "";
  return sym->id.substr(0, sep);
}

std::string build_submodule_spec(std::string_view spec, std::string_view name) {
  if (spec.empty()) return std::string(name);
  if (spec.ends_with('.')) {
    return std::string(spec) + std::string(name);
  }
  return std::string(spec) + "." + std::string(name);
}

} // namespace

Resolver::Resolver() {
  init_common_names();
}

Resolver::Resolver(std::vector<std::string> python_source_roots)
    : python_source_roots_(std::move(python_source_roots)) {
  init_common_names();
}

Resolver::Resolver(const core::Config& config)
    : python_source_roots_(config.python.source_roots) {
  init_common_names();
}

void Resolver::init_common_names() {
  for (auto s : COMMON_NAMES_DATA) {
    common_names_.insert(s);
  }
}

void Resolver::set_python_source_roots(std::vector<std::string> roots) {
  python_source_roots_ = std::move(roots);
}

const std::vector<std::string>& Resolver::python_source_roots() const {
  return python_source_roots_;
}

bool Resolver::is_common_name(std::string_view name) const {
  return common_names_.contains(name);
}

std::string Resolver::resolve_python_module(
    const FactStore& store,
    std::string_view from_file,
    std::string_view import_source) const {
  if (import_source.empty()) return "";

  // Check relative imports
  size_t k = 0;
  while (k < import_source.size() && import_source[k] == '.') {
    ++k;
  }

  if (k > 0) {
    // Relative import
    std::string_view rest = import_source.substr(k);
    std::string base_dir;
    size_t last_slash = from_file.find_last_of('/');
    if (last_slash != std::string_view::npos) {
      base_dir = std::string(from_file.substr(0, last_slash));
    }

    // Ascend k - 1 levels
    for (size_t i = 1; i < k; ++i) {
      if (base_dir.empty()) {
        return ""; // Escaped workspace
      }
      size_t pos = base_dir.find_last_of('/');
      if (pos == std::string::npos) {
        base_dir.clear();
      } else {
        base_dir = base_dir.substr(0, pos);
      }
    }

    if (rest.empty()) {
      std::string cand = base_dir.empty() ? "__init__.py" : (base_dir + "/__init__.py");
      if (store.get(cand)) return cand;
      return "";
    }

    std::string subpath(rest);
    std::replace(subpath.begin(), subpath.end(), '.', '/');
    std::string prefix = base_dir.empty() ? "" : (base_dir + "/");

    std::string c1 = prefix + subpath + ".py";
    if (store.get(c1)) return c1;
    std::string c2 = prefix + subpath + "/__init__.py";
    if (store.get(c2)) return c2;
    return "";
  }

  // Absolute import
  std::string subpath(import_source);
  std::replace(subpath.begin(), subpath.end(), '.', '/');

  for (const auto& root_dir : python_source_roots_) {
    std::string prefix = root_dir.empty() ? "" : (root_dir + "/");
    std::string c1 = prefix + subpath + ".py";
    if (store.get(c1)) return c1;
    std::string c2 = prefix + subpath + "/__init__.py";
    if (store.get(c2)) return c2;
  }

  return "";
}

std::string Resolver::resolve_ts_module(
    const FactStore& store,
    std::string_view from_file,
    std::string_view import_source) const {
  if (import_source.empty()) return "";

  // Bare specifiers are external
  if (!import_source.starts_with("./") && !import_source.starts_with("../") && !import_source.starts_with("/")) {
    return "";
  }

  std::string base;
  if (import_source.starts_with("/")) {
    base = std::string(import_source.substr(1));
  } else {
    size_t last_slash = from_file.find_last_of('/');
    if (last_slash != std::string_view::npos) {
      base = std::string(from_file.substr(0, last_slash)) + "/" + std::string(import_source);
    } else {
      base = std::string(import_source);
    }
  }

  // Normalize path segments (. and ..)
  std::vector<std::string> segments;
  size_t start = 0;
  while (start < base.size()) {
    size_t end = base.find('/', start);
    if (end == std::string::npos) end = base.size();
    std::string seg = base.substr(start, end - start);
    start = end + 1;
    if (seg.empty() || seg == ".") continue;
    if (seg == "..") {
      if (!segments.empty()) {
        segments.pop_back();
      } else {
        return ""; // Escaped workspace
      }
    } else {
      segments.push_back(std::move(seg));
    }
  }

  std::string norm;
  for (size_t i = 0; i < segments.size(); ++i) {
    if (i > 0) norm += "/";
    norm += segments[i];
  }

  // ESM-in-TS checks
  if (norm.ends_with(".js")) {
    std::string stem = norm.substr(0, norm.size() - 3);
    if (store.get(stem + ".ts")) return stem + ".ts";
    if (store.get(stem + ".tsx")) return stem + ".tsx";
    if (store.get(stem + ".mts")) return stem + ".mts";
  } else if (norm.ends_with(".jsx")) {
    std::string stem = norm.substr(0, norm.size() - 4);
    if (store.get(stem + ".tsx")) return stem + ".tsx";
  } else if (norm.ends_with(".mjs")) {
    std::string stem = norm.substr(0, norm.size() - 4);
    if (store.get(stem + ".mts")) return stem + ".mts";
  }

  // Probing order
  const std::string exts[] = {
    "", ".ts", ".tsx", ".d.ts", ".js", ".jsx", ".mjs", ".cjs",
    "/index.ts", "/index.tsx", "/index.js"
  };

  for (const auto& ext : exts) {
    std::string cand = norm + ext;
    if (store.get(cand)) return cand;
  }

  return "";
}

std::unordered_map<std::string, Resolver::ImportBinding>
Resolver::build_import_map(const FactStore& store, const lang::FileFacts& facts) const {
  std::unordered_map<std::string, ImportBinding> imports_by_name;
  for (const auto& imp : facts.imports) {
    std::string target = (facts.lang == lang::Language::Python)
        ? resolve_python_module(store, facts.file, imp.spec)
        : resolve_ts_module(store, facts.file, imp.spec);

    if (imp.kind == lang::ImportFact::Module) {
      std::string alias = imp.spec;
      size_t dot = alias.find_last_of('.');
      if (dot != std::string::npos) alias = alias.substr(dot + 1);
      if (!imp.names.empty() && !imp.names[0].second.empty()) {
        alias = imp.names[0].second;
      }
      if (!target.empty()) {
        imports_by_name[alias] = {target, "", true};
        if (!imp.spec.empty() && imp.spec != alias) {
          imports_by_name[imp.spec] = {target, "", true};
        }
      }
    } else if (imp.kind == lang::ImportFact::TsNamespace) {
      if (!imp.names.empty() && !target.empty()) {
        imports_by_name[imp.names[0].second] = {target, "", true};
      }
    } else if (facts.lang == lang::Language::Python && imp.kind == lang::ImportFact::FromNames) {
      for (const auto& [orig, alias] : imp.names) {
        std::string sub_spec = build_submodule_spec(imp.spec, orig);
        std::string sub_target = resolve_python_module(store, facts.file, sub_spec);
        if (!sub_target.empty()) {
          imports_by_name[alias] = {sub_target, "", true};
        } else if (!target.empty()) {
          imports_by_name[alias] = {target, orig, false};
        }
      }
    } else {
      if (!target.empty()) {
        for (const auto& [orig, alias] : imp.names) {
          imports_by_name[alias] = {target, orig, false};
        }
      }
    }
  }
  return imports_by_name;
}

const lang::SymbolFact* Resolver::find_class_symbol(
    const FactStore& store,
    const lang::FileFacts& facts,
    std::string_view class_name) const {
  auto imports = build_import_map(store, facts);
  return find_class_symbol(store, facts, class_name, imports);
}

const lang::SymbolFact* Resolver::find_class_symbol(
    const FactStore& store,
    const lang::FileFacts& facts,
    std::string_view class_name,
    const std::unordered_map<std::string, ImportBinding>& imports) const {
  // 1. Same file lookup
  const auto* sym = facts.find(class_name);
  if (sym && sym->kind == lang::SymbolKind::Class) {
    return sym;
  }

  // 2. Check imports
  std::string cname(class_name);
  auto it = imports.find(cname);
  if (it != imports.end()) {
    if (!it->second.is_whole_module) {
      const auto* target_facts = store.get(it->second.target_file);
      if (target_facts) {
        const auto* imp_sym = target_facts->find(it->second.orig_name);
        if (imp_sym && imp_sym->kind == lang::SymbolKind::Class) {
          return imp_sym;
        }
      }
    }
  }

  // 3. Dotted class name (module.ClassName or pkg.module.ClassName)
  size_t dot = cname.find('.');
  if (dot != std::string::npos) {
    // Try split at first dot
    std::string mod1 = cname.substr(0, dot);
    std::string cls1 = cname.substr(dot + 1);
    auto mit = imports.find(mod1);
    if (mit != imports.end() && mit->second.is_whole_module) {
      const auto* target_facts = store.get(mit->second.target_file);
      if (target_facts) {
        const auto* mod_sym = target_facts->find(cls1);
        if (mod_sym && mod_sym->kind == lang::SymbolKind::Class) {
          return mod_sym;
        }
      }
    }

    // Try split at last dot if different (e.g. pkg.subpkg.BaseClass)
    size_t last_dot = cname.rfind('.');
    if (last_dot != dot) {
      std::string mod2 = cname.substr(0, last_dot);
      std::string cls2 = cname.substr(last_dot + 1);
      auto mit2 = imports.find(mod2);
      if (mit2 != imports.end() && mit2->second.is_whole_module) {
        const auto* target_facts = store.get(mit2->second.target_file);
        if (target_facts) {
          const auto* mod_sym = target_facts->find(cls2);
          if (mod_sym && mod_sym->kind == lang::SymbolKind::Class) {
            return mod_sym;
          }
        }
      }
    }
  }

  // 4. Fallback search by qualname across store
  auto syms = store.find_symbols_by_qualname(class_name);
  for (const auto* s : syms) {
    if (s && s->kind == lang::SymbolKind::Class) {
      return s;
    }
  }

  return nullptr;
}

std::string Resolver::find_constructor(
    const FactStore& store,
    const lang::SymbolFact* class_sym) const {
  std::unordered_set<const lang::SymbolFact*> visited;
  return find_constructor_impl(store, class_sym, visited, 0);
}

std::string Resolver::find_constructor_impl(
    const FactStore& store,
    const lang::SymbolFact* class_sym,
    std::unordered_set<const lang::SymbolFact*>& visited,
    int depth) const {
  if (!class_sym || depth > 32) return "";
  if (!visited.insert(class_sym).second) {
    return ""; // Cycle detected
  }

  std::string file = file_of_symbol(class_sym);
  const auto* file_facts = store.get(file);

  std::string ctor_name = (file_facts && file_facts->lang == lang::Language::Python) ? "__init__" : "constructor";

  if (file_facts) {
    std::string qual = class_sym->qualname + "." + ctor_name;
    const auto* ctor = file_facts->find(qual);
    if (ctor && (ctor->kind == lang::SymbolKind::Constructor ||
                 ctor->kind == lang::SymbolKind::Method ||
                 ctor->kind == lang::SymbolKind::Function)) {
      return ctor->id;
    }
  }

  // Search bases
  for (const auto& base_name : class_sym->bases) {
    const lang::SymbolFact* base_cls = nullptr;
    if (file_facts) {
      base_cls = find_class_symbol(store, *file_facts, base_name);
    } else {
      auto syms = store.find_symbols_by_qualname(base_name);
      for (const auto* s : syms) {
        if (s && s->kind == lang::SymbolKind::Class) {
          base_cls = s;
          break;
        }
      }
    }

    if (base_cls) {
      std::string ctor_id = find_constructor_impl(store, base_cls, visited, depth + 1);
      if (!ctor_id.empty()) return ctor_id;
    }
  }

  return "";
}

const lang::SymbolFact* Resolver::find_method_in_class_hierarchy(
    const FactStore& store,
    const lang::SymbolFact* class_sym,
    std::string_view method_name) const {
  std::unordered_set<const lang::SymbolFact*> visited;
  return find_method_in_class_hierarchy_impl(store, class_sym, method_name, visited, 0);
}

const lang::SymbolFact* Resolver::find_method_in_class_hierarchy_impl(
    const FactStore& store,
    const lang::SymbolFact* class_sym,
    std::string_view method_name,
    std::unordered_set<const lang::SymbolFact*>& visited,
    int depth) const {
  if (!class_sym || depth > 32) return nullptr;
  if (!visited.insert(class_sym).second) {
    return nullptr; // Cycle detected
  }

  std::string file = file_of_symbol(class_sym);
  const auto* file_facts = store.get(file);
  if (file_facts) {
    std::string qual = class_sym->qualname + "." + std::string(method_name);
    const auto* m = file_facts->find(qual);
    if (m && (m->kind == lang::SymbolKind::Method ||
              m->kind == lang::SymbolKind::Function ||
              m->kind == lang::SymbolKind::Constructor)) {
      return m;
    }
  }

  // Depth-first search in bases
  for (const auto& base_name : class_sym->bases) {
    const lang::SymbolFact* base_cls = nullptr;
    if (file_facts) {
      base_cls = find_class_symbol(store, *file_facts, base_name);
    } else {
      auto syms = store.find_symbols_by_qualname(base_name);
      for (const auto* s : syms) {
        if (s && s->kind == lang::SymbolKind::Class) {
          base_cls = s;
          break;
        }
      }
    }

    if (base_cls) {
      const auto* m = find_method_in_class_hierarchy_impl(store, base_cls, method_name, visited, depth + 1);
      if (m) return m;
    }
  }

  return nullptr;
}

std::vector<ResolvedImport> Resolver::resolve_imports_for_file(
    const FactStore& store,
    const lang::FileFacts& facts) const {
  std::vector<ResolvedImport> results;

  for (const auto& imp : facts.imports) {
    std::string target = (facts.lang == lang::Language::Python)
        ? resolve_python_module(store, facts.file, imp.spec)
        : resolve_ts_module(store, facts.file, imp.spec);

    if (facts.lang == lang::Language::Python && imp.kind == lang::ImportFact::FromNames) {
      // Check each imported name to see if it is a submodule
      for (const auto& [orig, alias] : imp.names) {
        std::string sub_spec = build_submodule_spec(imp.spec, orig);
        std::string sub_target = resolve_python_module(store, facts.file, sub_spec);
        if (!sub_target.empty()) {
          ResolvedImport ri;
          ri.src_file = facts.file;
          ri.target_file = sub_target;
          ri.whole_module = true;
          ri.alias = alias;
          ri.line = imp.line;
          results.push_back(std::move(ri));
        } else if (!target.empty()) {
          ResolvedImport ri;
          ri.src_file = facts.file;
          ri.target_file = target;
          ri.names = {orig};
          ri.whole_module = false;
          ri.alias = alias;
          ri.line = imp.line;
          results.push_back(std::move(ri));
        }
      }
      continue;
    }

    if (target.empty()) continue;

    ResolvedImport ri;
    ri.src_file = facts.file;
    ri.target_file = target;
    ri.line = imp.line;

    if (imp.kind == lang::ImportFact::Module) {
      ri.whole_module = true;
      ri.alias = imp.spec;
      size_t dot = ri.alias.find_last_of('.');
      if (dot != std::string::npos) ri.alias = ri.alias.substr(dot + 1);
    } else if (imp.kind == lang::ImportFact::TsNamespace) {
      ri.whole_module = true;
      if (!imp.names.empty()) ri.alias = imp.names[0].second;
    } else {
      ri.whole_module = false;
      for (const auto& [orig, alias] : imp.names) {
        ri.names.push_back(orig);
        if (ri.alias.empty()) ri.alias = alias;
      }
    }
    results.push_back(std::move(ri));
  }

  return results;
}

std::vector<ResolvedImport> Resolver::resolve_imports(
    const FactStore& store,
    std::string_view rel_path) const {
  const auto* facts = store.get(rel_path);
  if (!facts) return {};
  return resolve_imports_for_file(store, *facts);
}

std::vector<ResolvedInherit> Resolver::resolve_inherits_for_file(
    const FactStore& store,
    const lang::FileFacts& facts) const {
  std::vector<ResolvedInherit> results;
  auto imports = build_import_map(store, facts);
  for (const auto& sym : facts.symbols) {
    if (sym.kind == lang::SymbolKind::Class || sym.kind == lang::SymbolKind::Interface) {
      for (const auto& base_name : sym.bases) {
        const auto* base_sym = find_class_symbol(store, facts, base_name, imports);
        if (base_sym) {
          results.push_back({sym.id, base_sym->id, "exact"});
        }
      }
    }
  }
  return results;
}

std::vector<ResolvedInherit> Resolver::resolve_inherits(
    const FactStore& store,
    std::string_view rel_path) const {
  const auto* facts = store.get(rel_path);
  if (!facts) return {};
  return resolve_inherits_for_file(store, *facts);
}

std::vector<ResolvedCall> Resolver::resolve_calls(
    const FactStore& store,
    const lang::FileFacts& facts) const {
  std::vector<ResolvedCall> results;

  // Build local import map
  auto imports_by_name = build_import_map(store, facts);

  // Pre-index local symbols
  std::unordered_map<std::string, const lang::SymbolFact*> local_funcs;
  std::unordered_map<std::string, const lang::SymbolFact*> local_classes;
  for (const auto& sym : facts.symbols) {
    if (sym.kind == lang::SymbolKind::Function || sym.kind == lang::SymbolKind::Arrow) {
      if (sym.qualname.find('.') == std::string::npos) {
        local_funcs[sym.qualname] = &sym;
      }
    } else if (sym.kind == lang::SymbolKind::Class) {
      local_classes[sym.qualname] = &sym;
    }
  }

  for (const auto& call : facts.calls) {
    const lang::SymbolFact* caller = store.find_symbol_by_id(call.caller_id);

    // STEP 1: Local Scope Closure Filtering
    if (call.shape == lang::CalleeShape::Name && caller != nullptr) {
      bool is_local = std::find(caller->locals.begin(), caller->locals.end(), call.name) != caller->locals.end();
      bool is_module_level = std::find(facts.module_names.begin(), facts.module_names.end(), call.name) != facts.module_names.end();
      if (is_local && !is_module_level) {
        continue;
      }
    }

    // STEP 2: Module-level Functions
    if (call.shape == lang::CalleeShape::Name) {
      auto it = local_funcs.find(call.name);
      if (it != local_funcs.end()) {
        results.push_back({
          .caller_id = call.caller_id,
          .target_id = it->second->id,
          .line = static_cast<int>(call.line),
          .col = static_cast<int>(call.col),
          .pos_args = call.pos_args,
          .kw_names = call.kw_names,
          .has_star = call.has_star,
          .has_dstar = call.has_dstar,
          .confidence = "exact",
          .call_kind = "plain",
          .call_text = call.text,
          .call = &call
        });
        continue;
      }
    }

    // STEP 3: Class Constructors
    if ((facts.lang == lang::Language::Python && call.shape == lang::CalleeShape::Name) ||
        call.shape == lang::CalleeShape::New) {
      auto it = local_classes.find(call.name);
      if (it != local_classes.end()) {
        std::string ctor_id = find_constructor(store, it->second);
        if (!ctor_id.empty()) {
          results.push_back({
            .caller_id = call.caller_id,
            .target_id = ctor_id,
            .line = static_cast<int>(call.line),
            .col = static_cast<int>(call.col),
            .pos_args = call.pos_args,
            .kw_names = call.kw_names,
            .has_star = call.has_star,
            .has_dstar = call.has_dstar,
            .confidence = "exact",
            .call_kind = "constructor",
            .call_text = call.text,
            .call = &call
          });
          continue;
        }
      }
    }

    // STEP 4: Imported Symbols
    if (call.shape == lang::CalleeShape::Name || call.shape == lang::CalleeShape::New) {
      auto it = imports_by_name.find(call.name);
      if (it != imports_by_name.end() && !it->second.is_whole_module) {
        auto target_facts = store.get(it->second.target_file);
        if (target_facts) {
          const lang::SymbolFact* sym = target_facts->find(it->second.orig_name);
          if (sym) {
            if (sym->kind == lang::SymbolKind::Class) {
              std::string ctor_id = find_constructor(store, sym);
              if (!ctor_id.empty()) {
                results.push_back({
                  .caller_id = call.caller_id,
                  .target_id = ctor_id,
                  .line = static_cast<int>(call.line),
                  .col = static_cast<int>(call.col),
                  .pos_args = call.pos_args,
                  .kw_names = call.kw_names,
                  .has_star = call.has_star,
                  .has_dstar = call.has_dstar,
                  .confidence = "exact",
                  .call_kind = "constructor",
                  .call_text = call.text,
                  .call = &call
                });
                continue;
              }
            } else if (sym->kind == lang::SymbolKind::Function || sym->kind == lang::SymbolKind::Arrow) {
              results.push_back({
                .caller_id = call.caller_id,
                .target_id = sym->id,
                .line = static_cast<int>(call.line),
                .col = static_cast<int>(call.col),
                .pos_args = call.pos_args,
                .kw_names = call.kw_names,
                .has_star = call.has_star,
                .has_dstar = call.has_dstar,
                .confidence = "exact",
                .call_kind = "plain",
                .call_text = call.text,
                .call = &call
              });
              continue;
            }
          }
        }
      }
    }

    // STEP 5: Member Attributes (Attr)
    if (call.shape == lang::CalleeShape::Attr && !call.receiver.empty()) {
      // Case 5A: Receiver is whole module
      auto it = imports_by_name.find(call.receiver);
      if (it != imports_by_name.end() && it->second.is_whole_module) {
        auto target_facts = store.get(it->second.target_file);
        if (target_facts) {
          const lang::SymbolFact* sym = target_facts->find(call.name);
          if (sym) {
            if (sym->kind == lang::SymbolKind::Class) {
              std::string ctor_id = find_constructor(store, sym);
              if (!ctor_id.empty()) {
                results.push_back({
                  .caller_id = call.caller_id,
                  .target_id = ctor_id,
                  .line = static_cast<int>(call.line),
                  .col = static_cast<int>(call.col),
                  .pos_args = call.pos_args,
                  .kw_names = call.kw_names,
                  .has_star = call.has_star,
                  .has_dstar = call.has_dstar,
                  .confidence = "exact",
                  .call_kind = "constructor",
                  .call_text = call.text,
                  .call = &call
                });
                continue;
              }
            } else {
              results.push_back({
                .caller_id = call.caller_id,
                .target_id = sym->id,
                .line = static_cast<int>(call.line),
                .col = static_cast<int>(call.col),
                .pos_args = call.pos_args,
                .kw_names = call.kw_names,
                .has_star = call.has_star,
                .has_dstar = call.has_dstar,
                .confidence = "exact",
                .call_kind = "plain",
                .call_text = call.text,
                .call = &call
              });
              continue;
            }
          }
        }
      }

      // Case 5B: Receiver is known class
      const lang::SymbolFact* cls = find_class_symbol(store, facts, call.receiver, imports_by_name);
      if (cls) {
        const lang::SymbolFact* m = find_method_in_class_hierarchy(store, cls, call.name);
        if (m) {
          std::string kind = "unbound";
          if (m->is_static || std::find(m->decorators.begin(), m->decorators.end(), "staticmethod") != m->decorators.end()) {
            kind = "plain";
          } else if (std::find(m->decorators.begin(), m->decorators.end(), "classmethod") != m->decorators.end()) {
            kind = "classmethod";
          }
          results.push_back({
            .caller_id = call.caller_id,
            .target_id = m->id,
            .line = static_cast<int>(call.line),
            .col = static_cast<int>(call.col),
            .pos_args = call.pos_args,
            .kw_names = call.kw_names,
            .has_star = call.has_star,
            .has_dstar = call.has_dstar,
            .confidence = "exact",
            .call_kind = kind,
            .call_text = call.text,
            .call = &call
          });
          continue;
        }
      }
    }

    // STEP 6: Instance Methods (SelfAttr / ClsAttr)
    if ((call.shape == lang::CalleeShape::SelfAttr || call.shape == lang::CalleeShape::ClsAttr) && caller != nullptr) {
      if (!caller->class_qualname.empty()) {
        const lang::SymbolFact* cls = facts.find(caller->class_qualname);
        if (cls) {
          const lang::SymbolFact* m = find_method_in_class_hierarchy(store, cls, call.name);
          if (m) {
            std::string kind = (call.shape == lang::CalleeShape::ClsAttr) ? "classmethod" : "instance";
            results.push_back({
              .caller_id = call.caller_id,
              .target_id = m->id,
              .line = static_cast<int>(call.line),
              .col = static_cast<int>(call.col),
              .pos_args = call.pos_args,
              .kw_names = call.kw_names,
              .has_star = call.has_star,
              .has_dstar = call.has_dstar,
              .confidence = "exact",
              .call_kind = kind,
              .call_text = call.text,
              .call = &call
            });
            continue;
          }
        }
      }
    }

    // STEP 7: Super Attributes (SuperAttr)
    if (call.shape == lang::CalleeShape::SuperAttr && caller != nullptr) {
      if (!caller->class_qualname.empty()) {
        const lang::SymbolFact* cls = facts.find(caller->class_qualname);
        if (cls && !cls->bases.empty()) {
          const lang::SymbolFact* base_cls = find_class_symbol(store, facts, cls->bases[0], imports_by_name);
          if (base_cls) {
            const lang::SymbolFact* m = find_method_in_class_hierarchy(store, base_cls, call.name);
            if (m) {
              results.push_back({
                .caller_id = call.caller_id,
                .target_id = m->id,
                .line = static_cast<int>(call.line),
                .col = static_cast<int>(call.col),
                .pos_args = call.pos_args,
                .kw_names = call.kw_names,
                .has_star = call.has_star,
                .has_dstar = call.has_dstar,
                .confidence = "exact",
                .call_kind = "instance",
                .call_text = call.text,
                .call = &call
              });
              continue;
            }
          }
        }
      }
    }

    // STEP 8: Single-Match Workspace Heuristic
    if ((call.shape == lang::CalleeShape::Attr || call.shape == lang::CalleeShape::Other) &&
        call.name.size() >= 4 && !is_common_name(call.name)) {
      std::string target_id;
      bool single_found = false;

      auto it = symbols_by_name_.find(call.name);
      if (it != symbols_by_name_.end()) {
        if (it->second.size() == 1) {
          target_id = it->second[0];
          single_found = true;
        }
      } else {
        auto syms = store.find_symbols_by_name(call.name);
        std::vector<const lang::SymbolFact*> callable_syms;
        for (const auto* s : syms) {
          if (s && (s->kind == lang::SymbolKind::Function ||
                    s->kind == lang::SymbolKind::Method ||
                    s->kind == lang::SymbolKind::Arrow)) {
            callable_syms.push_back(s);
          }
        }
        if (callable_syms.size() == 1) {
          target_id = callable_syms[0]->id;
          single_found = true;
        }
      }

      if (single_found && !target_id.empty()) {
        results.push_back({
          .caller_id = call.caller_id,
          .target_id = target_id,
          .line = static_cast<int>(call.line),
          .col = static_cast<int>(call.col),
          .pos_args = call.pos_args,
          .kw_names = call.kw_names,
          .has_star = call.has_star,
          .has_dstar = call.has_dstar,
          .confidence = "heuristic",
          .call_kind = "instance",
          .call_text = call.text,
          .call = &call
        });
        continue;
      }
    }
  }

  return results;
}

std::vector<ResolvedCall> Resolver::resolve_file(
    const FactStore& store,
    std::string_view rel_path) const {
  const auto* facts = store.get(rel_path);
  if (!facts) return {};
  return resolve_calls(store, *facts);
}

std::vector<std::string> Resolver::dependents_of(std::string_view file) const {
  std::vector<std::string> res;
  auto it = reverse_imports_.find(std::string(file));
  if (it != reverse_imports_.end()) {
    res.assign(it->second.begin(), it->second.end());
    std::sort(res.begin(), res.end());
  }
  return res;
}

void Resolver::update_dependencies(
    std::string_view file,
    const std::vector<lang::ImportFact>& imports,
    const FactStore& store) {
  std::string f(file);
  auto it = forward_imports_.find(f);
  if (it != forward_imports_.end()) {
    for (const auto& target : it->second) {
      reverse_imports_[target].erase(f);
    }
    forward_imports_.erase(it);
  }

  bool is_py = f.ends_with(".py");
  for (const auto& imp : imports) {
    if (is_py && imp.kind == lang::ImportFact::FromNames) {
      for (const auto& [orig, alias] : imp.names) {
        (void)alias;
        std::string sub_spec = build_submodule_spec(imp.spec, orig);
        std::string sub_target = resolve_python_module(store, f, sub_spec);
        if (!sub_target.empty() && sub_target != f) {
          forward_imports_[f].insert(sub_target);
          reverse_imports_[sub_target].insert(f);
        } else {
          std::string target = resolve_python_module(store, f, imp.spec);
          if (!target.empty() && target != f) {
            forward_imports_[f].insert(target);
            reverse_imports_[target].insert(f);
          }
        }
      }
    } else {
      std::string target = is_py ? resolve_python_module(store, f, imp.spec)
                                 : resolve_ts_module(store, f, imp.spec);
      if (!target.empty() && target != f) {
        forward_imports_[f].insert(target);
        reverse_imports_[target].insert(f);
      }
    }
  }
}

void Resolver::update_dependencies(
    std::string_view file,
    const std::vector<lang::ImportFact>& imports) {
  if (store_) {
    update_dependencies(file, imports, *store_);
  }
}

void Resolver::remove_file(std::string_view file) {
  std::string f(file);
  auto it = forward_imports_.find(f);
  if (it != forward_imports_.end()) {
    for (const auto& target : it->second) {
      reverse_imports_[target].erase(f);
    }
    forward_imports_.erase(it);
  }
  reverse_imports_.erase(f);
}

void Resolver::update_file(std::string_view file, const FactStore& store) {
  store_ = &store;
  std::string f(file);
  const auto* facts = store.get(file);
  if (!facts) {
    remove_file(file);
    return;
  }

  update_dependencies(file, facts->imports, store);

  // Remove stale symbols belonging to this file from symbols_by_name_
  for (auto it = symbols_by_name_.begin(); it != symbols_by_name_.end(); ) {
    auto& ids = it->second;
    std::erase_if(ids, [&](const std::string& id) {
      return id.starts_with(f + "::");
    });
    if (ids.empty()) {
      it = symbols_by_name_.erase(it);
    } else {
      ++it;
    }
  }

  // Register current callable symbols
  for (const auto& sym : facts->symbols) {
    if (sym.kind == lang::SymbolKind::Function ||
        sym.kind == lang::SymbolKind::Method ||
        sym.kind == lang::SymbolKind::Arrow) {
      symbols_by_name_[sym.name].push_back(sym.id);
    }
  }
}

void Resolver::rebuild_indices(const FactStore& store) {
  store_ = &store;
  symbols_by_name_.clear();
  forward_imports_.clear();
  reverse_imports_.clear();
  for (const auto& file : store.all_files()) {
    const auto* facts = store.get(file);
    if (!facts) continue;
    update_dependencies(file, facts->imports, store);
    for (const auto& sym : facts->symbols) {
      if (sym.kind == lang::SymbolKind::Function ||
          sym.kind == lang::SymbolKind::Method ||
          sym.kind == lang::SymbolKind::Arrow) {
        symbols_by_name_[sym.name].push_back(sym.id);
      }
    }
  }
}

} // namespace gs::index
