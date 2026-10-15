#include <doctest/doctest.h>
#include <string>
#include "lang/extractor.hpp"
#include "lang/language.hpp"
#include "lang/parser.hpp"

using namespace gs::lang;

TEST_CASE("TypeScript Extractor extracts symbols, interfaces, overloads, and calls") {
  std::string code = R"(
import { useState } from 'react';
import type { Config } from './config';

export interface User {
  id: string;
  name: string;
}

export class UserService {
  private serviceName: string;

  constructor(name: string) {
    this.serviceName = name;
  }

  public async fetchUser(id: string): Promise<User> {
    return { id, name: "Alice" };
  }

  public static createDefault(): UserService {
    return new UserService("default");
  }
}

export function processItem(x: number): number;
export function processItem(x: string): string;
export function processItem(x: any): any {
  return x;
}

export const formatUserName = (user: User, uppercase?: boolean): string => {
  return user.name;
};

export function main(): void {
  const service = new UserService("test");
  service.fetchUser("123");
  formatUserName({ id: "1", name: "Bob" });
}
)";

  TreePtr tree = parse_string(Language::TypeScript, code);
  REQUIRE(tree != nullptr);

  auto extractor = make_extractor(Language::TypeScript);
  REQUIRE(extractor != nullptr);

  FileFacts facts = extractor->extract("app.ts", code, tree.get());

  CHECK(facts.file == "app.ts");
  CHECK(facts.lang == Language::TypeScript);
  CHECK(facts.parse_ok);
  CHECK(facts.syntax_errors.empty());

  // Interface
  const SymbolFact* iface = facts.find("User");
  REQUIRE(iface != nullptr);
  CHECK(iface->kind == SymbolKind::Interface);
  CHECK(iface->is_exported == true);

  // Class
  const SymbolFact* cls = facts.find("UserService");
  REQUIRE(cls != nullptr);
  CHECK(cls->kind == SymbolKind::Class);
  CHECK(cls->is_exported == true);

  // Constructor
  const SymbolFact* ctor = facts.find("UserService.constructor");
  REQUIRE(ctor != nullptr);
  CHECK(ctor->kind == SymbolKind::Constructor);
  REQUIRE(ctor->sig.params.size() == 1);
  CHECK(ctor->sig.params[0].name == "name");

  // Method
  const SymbolFact* fetch_m = facts.find("UserService.fetchUser");
  REQUIRE(fetch_m != nullptr);
  CHECK(fetch_m->kind == SymbolKind::Method);
  CHECK(fetch_m->sig.is_async == true);
  CHECK(fetch_m->sig.return_type == "Promise<User>");

  // Static method
  const SymbolFact* static_m = facts.find("UserService.createDefault");
  REQUIRE(static_m != nullptr);
  CHECK(static_m->is_static == true);

  // Overloaded function
  const SymbolFact* proc = facts.find("processItem");
  REQUIRE(proc != nullptr);
  CHECK(proc->kind == SymbolKind::Function);
  CHECK(proc->sig.has_overloads == true);

  // Arrow function
  const SymbolFact* arrow = facts.find("formatUserName");
  REQUIRE(arrow != nullptr);
  CHECK(arrow->kind == SymbolKind::Arrow);
  CHECK(arrow->is_exported == true);
  REQUIRE(arrow->sig.params.size() == 2);
  CHECK(arrow->sig.params[0].name == "user");
  CHECK(arrow->sig.params[1].name == "uppercase");
  CHECK(arrow->sig.params[1].optional == true);

  // Calls
  bool found_new = false;
  bool found_fetch = false;
  for (const auto& c : facts.calls) {
    if (c.shape == CalleeShape::New && c.name == "UserService") {
      found_new = true;
    }
    if (c.shape == CalleeShape::Attr && c.name == "fetchUser") {
      found_fetch = true;
    }
  }
  CHECK(found_new);
  CHECK(found_fetch);

  // Imports
  bool found_react = false;
  bool found_config = false;
  for (const auto& imp : facts.imports) {
    if (imp.spec == "react") found_react = true;
    if (imp.spec == "./config") {
      found_config = true;
      CHECK(imp.is_type_only == true);
    }
  }
  CHECK(found_react);
  CHECK(found_config);
}
