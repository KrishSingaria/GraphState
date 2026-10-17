#!/usr/bin/env python3
"""Synthetic Codebase Generator for GraphState Benchmarking.

Generates scalable Python and TypeScript codebases with realistic cross-file
imports, functions, and call relationships.
"""

import argparse
import os
import random
import shutil

def generate_python_file(path, pkg_name, file_idx, funcs_per_file, other_modules):
    lines = [
        f'"""Synthetic Python module {pkg_name}.mod_{file_idx:04d}."""',
        ""
    ]

    # Pick 1-3 other modules to import from
    imported_funcs = []
    if other_modules:
        imports_count = min(len(other_modules), random.randint(1, 3))
        targets = random.sample(other_modules, imports_count)
        for t_pkg, t_idx in targets:
            func_name = f"helper_{t_idx:04d}_0"
            lines.append(f"from {t_pkg}.mod_{t_idx:04d} import {func_name}")
            imported_funcs.append(func_name)

    lines.append("")

    for fi in range(funcs_per_file):
        fname = f"helper_{file_idx:04d}_{fi}"
        lines.append(f"def {fname}(x: int, tag: str = 'item_{fi}') -> int:")
        lines.append(f'    """Synthetic worker function {fi}."""')
        lines.append(f"    acc = x + {fi + 1}")
        if imported_funcs and fi == 0:
            call_target = random.choice(imported_funcs)
            lines.append(f"    acc += {call_target}(acc)")
        lines.append("    return acc")
        lines.append("")

    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as fh:
        fh.write("\n".join(lines))

def generate_ts_file(path, file_idx, funcs_per_file, other_ts_files):
    lines = [
        f"/** Synthetic TypeScript module ts_mod_{file_idx:04d}. */",
        ""
    ]

    imported_funcs = []
    if other_ts_files:
        imports_count = min(len(other_ts_files), random.randint(1, 2))
        targets = random.sample(other_ts_files, imports_count)
        for t_idx in targets:
            func_name = f"tsHelper_{t_idx:04d}_0"
            lines.append(f'import {{ {func_name} }} from "./ts_mod_{t_idx:04d}";')
            imported_funcs.append(func_name)

    lines.append("")

    for fi in range(funcs_per_file):
        fname = f"tsHelper_{file_idx:04d}_{fi}"
        lines.append(f"export function {fname}(val: number): number {{")
        lines.append(f"  let result = val + {fi + 1};")
        if imported_funcs and fi == 0:
            call_target = random.choice(imported_funcs)
            lines.append(f"  result += {call_target}(result);")
        lines.append("  return result;")
        lines.append("}")
        lines.append("")

    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as fh:
        fh.write("\n".join(lines))

def main():
    parser = argparse.ArgumentParser(description="Synthetic Codebase Generator")
    parser.add_argument("--files", type=int, default=100, help="Total number of source files to generate")
    parser.add_argument("--funcs-per-file", type=int, default=10, help="Number of functions per file")
    parser.add_argument("--out", default="synthetic_project", help="Output directory")
    parser.add_argument("--clean", action="store_true", help="Remove output directory if it exists")
    args = parser.parse_args()

    out_dir = os.path.abspath(args.out)
    if args.clean and os.path.exists(out_dir):
        shutil.rmtree(out_dir)

    os.makedirs(out_dir, exist_ok=True)

    py_count = int(args.files * 0.8)
    ts_count = args.files - py_count

    print(f"Generating synthetic codebase at: {out_dir}")
    print(f"Files: {args.files} (Python: {py_count}, TypeScript: {ts_count})")
    print(f"Functions per file: {args.funcs_per_file} (~{args.files * args.funcs_per_file} total functions)")

    # 1. Generate Python packages
    packages = ["core", "services", "models", "utils", "api"]
    for pkg in packages:
        init_p = os.path.join(out_dir, pkg, "__init__.py")
        os.makedirs(os.path.dirname(init_p), exist_ok=True)
        with open(init_p, "w", encoding="utf-8") as fh:
            fh.write(f"# {pkg} package marker\n")

    created_py = []
    for i in range(py_count):
        pkg = packages[i % len(packages)]
        file_path = os.path.join(out_dir, pkg, f"mod_{i:04d}.py")
        generate_python_file(file_path, pkg, i, args.funcs_per_file, created_py)
        created_py.append((pkg, i))

    # 2. Generate TypeScript files under web/
    web_dir = os.path.join(out_dir, "web")
    os.makedirs(web_dir, exist_ok=True)
    created_ts = []
    for i in range(ts_count):
        file_path = os.path.join(web_dir, f"ts_mod_{i:04d}.ts")
        generate_ts_file(file_path, i, args.funcs_per_file, created_ts)
        created_ts.append(i)

    print(f"Done. Successfully generated {args.files} files.")

if __name__ == "__main__":
    main()
