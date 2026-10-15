#include "index/fact_store.hpp"
#include "platform/platform.hpp"

#include <algorithm>
#include <cctype>
#include <unordered_set>

namespace gs::index {

namespace {

std::string to_lower_ascii(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  for (char c : s) {
    out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return out;
}

} // namespace

FactStore::FactStore(std::filesystem::path root) : root_(std::move(root)) {}

FactStore::FactStore(FactStore&& o) noexcept {
  std::unique_lock lock(o.mutex_);
  root_ = std::move(o.root_);
  case_insensitive_ = o.case_insensitive_;
  files_ = std::move(o.files_);
  symbol_index_ = std::move(o.symbol_index_);
  by_name_ = std::move(o.by_name_);
  by_qualname_ = std::move(o.by_qualname_);
  lower_files_ = std::move(o.lower_files_);
}

FactStore& FactStore::operator=(FactStore&& o) noexcept {
  if (this == &o) return *this;
  std::unique_lock lock_this(mutex_, std::defer_lock);
  std::unique_lock lock_o(o.mutex_, std::defer_lock);
  std::lock(lock_this, lock_o);

  root_ = std::move(o.root_);
  case_insensitive_ = o.case_insensitive_;
  files_ = std::move(o.files_);
  symbol_index_ = std::move(o.symbol_index_);
  by_name_ = std::move(o.by_name_);
  by_qualname_ = std::move(o.by_qualname_);
  lower_files_ = std::move(o.lower_files_);
  return *this;
}

void FactStore::set_root(std::filesystem::path root) {
  std::unique_lock lock(mutex_);
  root_ = std::move(root);
}

const std::filesystem::path& FactStore::root() const {
  std::shared_lock lock(mutex_);
  return root_;
}

void FactStore::set_case_insensitive(bool ci) {
  std::unique_lock lock(mutex_);
  case_insensitive_ = ci;
  lower_files_.clear();
  if (case_insensitive_) {
    for (const auto& [file, _] : files_) {
      lower_files_[to_lower_ascii(file)] = file;
    }
  }
}

bool FactStore::is_case_insensitive() const {
  std::shared_lock lock(mutex_);
  return case_insensitive_;
}

std::string FactStore::resolve_file_key_locked(std::string_view rel_path) const {
  std::string file(rel_path);
  if (files_.contains(file)) {
    return file;
  }
  if (case_insensitive_) {
    auto lit = lower_files_.find(to_lower_ascii(file));
    if (lit != lower_files_.end()) {
      return lit->second;
    }
  }
  return file;
}

void FactStore::unindex_file_locked(const std::string& file) {
  auto fit = files_.find(file);
  if (fit == files_.end()) {
    return;
  }

  const auto& symbols = fit->second.symbols;
  for (size_t i = 0; i < symbols.size(); ++i) {
    const auto& sym = symbols[i];
    symbol_index_.erase(sym.id);

    // Unindex from by_name_
    auto [n_beg, n_end] = by_name_.equal_range(sym.name);
    for (auto it = n_beg; it != n_end;) {
      if (it->second.first == file && it->second.second == i) {
        it = by_name_.erase(it);
      } else {
        ++it;
      }
    }

    // Unindex from by_qualname_
    auto [q_beg, q_end] = by_qualname_.equal_range(sym.qualname);
    for (auto it = q_beg; it != q_end;) {
      if (it->second.first == file && it->second.second == i) {
        it = by_qualname_.erase(it);
      } else {
        ++it;
      }
    }
  }

  if (case_insensitive_) {
    lower_files_.erase(to_lower_ascii(file));
  }
}

void FactStore::put(lang::FileFacts facts) {
  std::unique_lock lock(mutex_);
  std::string existing_key = resolve_file_key_locked(facts.file);
  unindex_file_locked(existing_key);
  if (existing_key != facts.file) {
    files_.erase(existing_key);
  }

  std::string file = facts.file;
  auto [fit, inserted] = files_.insert_or_assign(file, std::move(facts));
  (void)inserted;
  const auto& stored_facts = fit->second;

  for (size_t i = 0; i < stored_facts.symbols.size(); ++i) {
    const auto& sym = stored_facts.symbols[i];
    symbol_index_[sym.id] = {file, i};
    by_name_.emplace(sym.name, SymbolLocation{file, i});
    by_qualname_.emplace(sym.qualname, SymbolLocation{file, i});
  }

  if (case_insensitive_) {
    lower_files_[to_lower_ascii(file)] = file;
  }
}

void FactStore::erase(std::string_view rel_path) {
  std::unique_lock lock(mutex_);
  std::string file = resolve_file_key_locked(rel_path);
  unindex_file_locked(file);
  files_.erase(file);
}

void FactStore::clear() {
  std::unique_lock lock(mutex_);
  files_.clear();
  symbol_index_.clear();
  by_name_.clear();
  by_qualname_.clear();
  lower_files_.clear();
}

const lang::FileFacts* FactStore::get(std::string_view rel_path) const {
  std::shared_lock lock(mutex_);
  std::string file = resolve_file_key_locked(rel_path);
  auto fit = files_.find(file);
  if (fit == files_.end()) {
    return nullptr;
  }
  return &fit->second;
}

std::optional<lang::FileFacts> FactStore::get_copy(std::string_view rel_path) const {
  std::shared_lock lock(mutex_);
  std::string file = resolve_file_key_locked(rel_path);
  auto fit = files_.find(file);
  if (fit == files_.end()) {
    return std::nullopt;
  }
  return fit->second;
}

bool FactStore::contains(std::string_view rel_path) const {
  std::shared_lock lock(mutex_);
  std::string file = resolve_file_key_locked(rel_path);
  return files_.contains(file);
}

std::vector<std::string> FactStore::all_files() const {
  std::shared_lock lock(mutex_);
  std::vector<std::string> res;
  res.reserve(files_.size());
  for (const auto& [file, _] : files_) {
    res.push_back(file);
  }
  std::sort(res.begin(), res.end());
  return res;
}

std::vector<std::string> FactStore::files() const {
  return all_files();
}

size_t FactStore::file_count() const {
  std::shared_lock lock(mutex_);
  return files_.size();
}

size_t FactStore::total_symbols() const {
  std::shared_lock lock(mutex_);
  return symbol_index_.size();
}

const lang::SymbolFact* FactStore::symbol(std::string_view id) const {
  return find_symbol_by_id(id);
}

const lang::SymbolFact* FactStore::find_symbol_by_id(std::string_view id) const {
  std::shared_lock lock(mutex_);
  auto sit = symbol_index_.find(std::string(id));
  if (sit == symbol_index_.end()) {
    return nullptr;
  }
  const auto& [file, idx] = sit->second;
  auto fit = files_.find(file);
  if (fit == files_.end() || idx >= fit->second.symbols.size()) {
    return nullptr;
  }
  return &fit->second.symbols[idx];
}

std::vector<const lang::SymbolFact*> FactStore::find_symbols_by_name(std::string_view name) const {
  std::shared_lock lock(mutex_);
  std::vector<const lang::SymbolFact*> res;
  auto [beg, end] = by_name_.equal_range(std::string(name));
  for (auto it = beg; it != end; ++it) {
    const auto& [file, idx] = it->second;
    auto fit = files_.find(file);
    if (fit != files_.end() && idx < fit->second.symbols.size()) {
      res.push_back(&fit->second.symbols[idx]);
    }
  }
  return res;
}

std::vector<const lang::SymbolFact*> FactStore::find_symbols_by_qualname(std::string_view qualname) const {
  std::shared_lock lock(mutex_);
  std::vector<const lang::SymbolFact*> res;
  auto [beg, end] = by_qualname_.equal_range(std::string(qualname));
  for (auto it = beg; it != end; ++it) {
    const auto& [file, idx] = it->second;
    auto fit = files_.find(file);
    if (fit != files_.end() && idx < fit->second.symbols.size()) {
      res.push_back(&fit->second.symbols[idx]);
    }
  }
  return res;
}

std::vector<const lang::SymbolFact*> FactStore::find_symbols(std::string_view query, size_t limit) const {
  std::shared_lock lock(mutex_);
  if (limit == 0) return {};

  std::vector<const lang::SymbolFact*> res;
  res.reserve(std::min<size_t>(limit, 16));
  std::unordered_set<const lang::SymbolFact*> seen;

  auto add_symbol = [&](const lang::SymbolFact* s) -> bool {
    if (s && seen.insert(s).second) {
      res.push_back(s);
      return res.size() >= limit;
    }
    return false;
  };

  std::string q(query);

  // Tier 1: Exact ID
  auto sit = symbol_index_.find(q);
  if (sit != symbol_index_.end()) {
    const auto& [file, idx] = sit->second;
    auto fit = files_.find(file);
    if (fit != files_.end() && idx < fit->second.symbols.size()) {
      if (add_symbol(&fit->second.symbols[idx])) {
        return res;
      }
    }
  }

  // Tier 2: Exact Qualname
  auto [q_beg, q_end] = by_qualname_.equal_range(q);
  for (auto it = q_beg; it != q_end; ++it) {
    const auto& [file, idx] = it->second;
    auto fit = files_.find(file);
    if (fit != files_.end() && idx < fit->second.symbols.size()) {
      if (add_symbol(&fit->second.symbols[idx])) {
        return res;
      }
    }
  }

  // Tier 3: Exact Name
  auto [n_beg, n_end] = by_name_.equal_range(q);
  for (auto it = n_beg; it != n_end; ++it) {
    const auto& [file, idx] = it->second;
    auto fit = files_.find(file);
    if (fit != files_.end() && idx < fit->second.symbols.size()) {
      if (add_symbol(&fit->second.symbols[idx])) {
        return res;
      }
    }
  }

  return res;
}

bool FactStore::is_stale(std::string_view rel_path) const {
  std::filesystem::path r;
  {
    std::shared_lock lock(mutex_);
    r = root_;
  }
  return is_stale(r, rel_path);
}

bool FactStore::is_stale(const std::filesystem::path& root, std::string_view rel_path) const {
  uint64_t stored_size = 0;
  int64_t stored_mtime = 0;
  std::string exact_file;

  {
    std::shared_lock lock(mutex_);
    exact_file = resolve_file_key_locked(rel_path);
    auto fit = files_.find(exact_file);
    if (fit == files_.end()) {
      return true;
    }
    stored_size = fit->second.size;
    stored_mtime = fit->second.mtime_ns;
  } // Mutex unlocked before disk stat

  std::filesystem::path abs_path = root / platform::from_utf8(exact_file);
  auto st = platform::stat(abs_path);
  if (!st.exists || !st.regular) {
    return true;
  }

  if (st.size != stored_size || st.mtime_ns != stored_mtime) {
    return true;
  }

  return false;
}

} // namespace gs::index
