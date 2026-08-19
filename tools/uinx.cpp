// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 ViudiraTech
// By JiTianYu391

// Uinx Language

#include "uinx/compiler.hpp"
#include "uinx/lexer.hpp"
#include "uinx/parser.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iostream>
#include <set>
#include <sstream>

using namespace uinx;

#ifndef UINX_SOURCE_STDLIB_DIR
    #define UINX_SOURCE_STDLIB_DIR "stdlib"
#endif
#ifndef UINX_SOURCE_RUNTIME_LIB
    #define UINX_SOURCE_RUNTIME_LIB "libuinx_runtime.a"
#endif

namespace {

struct Manifest {
    std::string name{"app"};
    std::string version{"0.2.0"};
    std::string entry{"src/main.ux"};
    std::string kind{"bin"};
    std::string std_mode{"full"};
    std::unordered_map<std::string, std::string> path_dependencies;
};

std::filesystem::path tool_path;

std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
        return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::string unquote(std::string value) {
    value = trim(std::move(value));
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
        return value.substr(1, value.size() - 2);
    return value;
}

struct SourceSelection {
    std::string std_mode;
    bool runtime{true};
    std::set<std::string> disabled_dependencies;
};

SourceSelection source_selection(const std::filesystem::path& root, const Manifest& manifest) {
    SourceSelection selection{manifest.std_mode, true, {}};
    std::ifstream source(root / manifest.entry);
    if (!source)
        return selection;

    std::string line;
    while (std::getline(source, line)) {
        const auto slash = line.find("//");
        const auto hash = line.find('#');
        const auto comment = std::min(slash == std::string::npos ? line.size() : slash,
                                      hash == std::string::npos ? line.size() : hash);
        line.resize(comment);
        line = trim(std::move(line));
        if (line.empty())
            continue;

        bool disabled = false;
        std::string name;
        if (line == "no_std;" || line == "no_std") {
            disabled = true;
            name = "std";
        } else if (line.rfind("need ", 0) == 0) {
            name = trim(line.substr(5));
        } else if (line.rfind("dontneed ", 0) == 0) {
            disabled = true;
            name = trim(line.substr(9));
        } else {
            break;
        }
        if (!name.empty() && name.back() == ';')
            name.pop_back();
        name = trim(std::move(name));

        if (name == "std")
            selection.std_mode = disabled ? "none" : "full";
        else if (!disabled && (name == "core" || name == "alloc" || name == "minimal"))
            selection.std_mode = name;
        else if (name == "runtime")
            selection.runtime = !disabled;
        else if (disabled && !name.empty())
            selection.disabled_dependencies.insert(name);
    }
    return selection;
}

bool read_manifest(const std::filesystem::path& path, Manifest& manifest) {
    std::ifstream file(path);
    if (!file)
        return false;
    std::string line;
    std::string section;
    while (std::getline(file, line)) {
        const auto hash = line.find('#');
        if (hash != std::string::npos)
            line.resize(hash);
        line = trim(std::move(line));
        if (line.empty())
            continue;
        if (line.front() == '[' && line.back() == ']') {
            section = line.substr(1, line.size() - 2);
            continue;
        }
        const auto equals = line.find('=');
        if (equals == std::string::npos)
            continue;
        const std::string key = trim(line.substr(0, equals));
        const std::string value = trim(line.substr(equals + 1));
        if (section == "package") {
            if (key == "name")
                manifest.name = unquote(value);
            else if (key == "version")
                manifest.version = unquote(value);
            else if (key == "entry")
                manifest.entry = unquote(value);
            else if (key == "kind")
                manifest.kind = unquote(value);
            else if (key == "std")
                manifest.std_mode = unquote(value);
        } else if (section == "dependencies") {
            const auto path_pos = value.find("path");
            if (path_pos == std::string::npos)
                continue;
            const auto quote1 = value.find('"', path_pos);
            const auto quote2 = quote1 == std::string::npos ? quote1 : value.find('"', quote1 + 1);
            if (quote1 != std::string::npos && quote2 != std::string::npos) {
                manifest.path_dependencies[key] = value.substr(quote1 + 1, quote2 - quote1 - 1);
            }
        }
    }
    return true;
}

std::filesystem::path find_root() {
    auto path = std::filesystem::current_path();
    for (;;) {
        if (std::filesystem::exists(path / "uinx.toml"))
            return path;
        if (path == path.root_path())
            break;
        path = path.parent_path();
    }
    return {};
}

std::uint64_t fnv1a(std::string_view value) {
    std::uint64_t hash = 1469598103934665603ull;
    for (const char raw : value) {
        const auto c = static_cast<unsigned char>(raw);
        hash ^= c;
        hash *= 1099511628211ull;
    }
    return hash;
}

std::vector<std::filesystem::path> ux_files_under(const std::filesystem::path& directory) {
    std::vector<std::filesystem::path> files;
    if (!std::filesystem::exists(directory))
        return files;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(directory)) {
        if (entry.is_regular_file() && entry.path().extension() == ".ux")
            files.push_back(entry.path());
    }
    std::sort(files.begin(), files.end());
    return files;
}

std::filesystem::path stdlib_root() {
    std::vector<std::filesystem::path> candidates;
    if (!tool_path.empty()) {
        const auto bin = std::filesystem::weakly_canonical(tool_path).parent_path();
        candidates.push_back(bin / "../lib/uinx/stdlib");
        candidates.push_back(bin / "../share/uinx/stdlib");
    }
    if (const char* env = std::getenv("UINX_STDLIB"))
        candidates.emplace_back(env);
    candidates.emplace_back(UINX_SOURCE_STDLIB_DIR);
    for (const auto& candidate : candidates) {
        std::error_code error;
        if (std::filesystem::is_directory(candidate, error))
            return std::filesystem::weakly_canonical(candidate, error);
    }
    return {};
}

std::filesystem::path runtime_library() {
    std::vector<std::filesystem::path> candidates;
    if (!tool_path.empty()) {
        const auto bin = std::filesystem::weakly_canonical(tool_path).parent_path();
        candidates.push_back(bin / "../lib/libuinx_runtime.a");
        candidates.push_back(bin / "libuinx_runtime.a");
    }
    if (const char* env = std::getenv("UINX_RUNTIME_LIB"))
        candidates.emplace_back(env);
    candidates.emplace_back(UINX_SOURCE_RUNTIME_LIB);
    for (const auto& candidate : candidates) {
        std::error_code error;
        if (std::filesystem::is_regular_file(candidate, error))
            return std::filesystem::weakly_canonical(candidate, error);
    }
    return {};
}

void append_stdlib_sources(std::vector<std::filesystem::path>& out, std::string_view mode) {
    if (mode == "none" || mode == "no_std")
        return;
    const auto root = stdlib_root();
    if (root.empty())
        return;
    auto append_layer = [&](std::string_view layer) {
        auto files = ux_files_under(root / layer);
        out.insert(out.end(), files.begin(), files.end());
    };
    append_layer("core");
    if (mode == "core")
        return;
    append_layer("alloc");
    if (mode == "alloc")
        return;
    append_layer("minimal");
    if (mode == "minimal")
        return;
    append_layer("std");
}

bool collect_dependency_sources(const std::filesystem::path& root,
                                const Manifest& manifest,
                                std::vector<std::filesystem::path>& out,
                                std::set<std::filesystem::path>& visited,
                                std::string& error,
                                const std::set<std::string>* disabled_dependencies = nullptr) {
    for (const auto& [name, relative] : manifest.path_dependencies) {
        if (disabled_dependencies && disabled_dependencies->contains(name))
            continue;
        std::error_code ec;
        auto dep_root = std::filesystem::weakly_canonical(root / relative, ec);
        if (ec || dep_root.empty()) {
            error = "cannot resolve dependency '" + name + "' at " + (root / relative).string();
            return false;
        }
        if (!visited.insert(dep_root).second)
            continue;
        Manifest dep;
        if (!read_manifest(dep_root / "uinx.toml", dep)) {
            error = "dependency '" + name + "' has no readable uinx.toml";
            return false;
        }
        auto sources = ux_files_under(dep_root / "src");
        sources.erase(std::remove_if(sources.begin(),
                                     sources.end(),
                                     [&](const auto& source) {
                                         return dep.kind == "bin" && source.filename() == "main.ux";
                                     }),
                      sources.end());
        out.insert(out.end(), sources.begin(), sources.end());
        if (!collect_dependency_sources(dep_root, dep, out, visited, error, nullptr))
            return false;
    }
    return true;
}

bool collect_project_sources(const std::filesystem::path& root,
                             const Manifest& manifest,
                             std::vector<std::filesystem::path>& out,
                             bool include_std = true) {
    out.clear();
    const auto entry = root / manifest.entry;
    if (!std::filesystem::exists(entry)) {
        std::cerr << "entry source does not exist: " << entry << '\n';
        return false;
    }
    out.push_back(entry);
    auto local = ux_files_under(root / "src");
    for (const auto& source : local)
        if (std::filesystem::weakly_canonical(source) != std::filesystem::weakly_canonical(entry))
            out.push_back(source);
    const auto selection = source_selection(root, manifest);
    std::set<std::filesystem::path> visited{std::filesystem::weakly_canonical(root)};
    std::string error;
    if (!collect_dependency_sources(
            root, manifest, out, visited, error, &selection.disabled_dependencies)) {
        std::cerr << error << '\n';
        return false;
    }
    if (include_std)
        append_stdlib_sources(out, selection.std_mode);
    return true;
}

int fetch(const std::filesystem::path& root, const Manifest& manifest) {
    std::ofstream lock(root / "uinx.lock", std::ios::trunc);
    if (!lock) {
        std::cerr << "cannot write uinx.lock\n";
        return 1;
    }
    lock << "# Uinx lockfile v1\n[[package]]\nname = \"" << manifest.name << "\"\nversion = \""
         << manifest.version << "\"\n";
    std::set<std::filesystem::path> visited;
    std::function<bool(const std::filesystem::path&, const Manifest&)> write_deps;
    write_deps = [&](const std::filesystem::path& owner_root, const Manifest& owner) {
        for (const auto& [name, relative] : owner.path_dependencies) {
            std::error_code ec;
            const auto dep_root = std::filesystem::weakly_canonical(owner_root / relative, ec);
            if (ec || !visited.insert(dep_root).second)
                continue;
            const auto manifest_path = dep_root / "uinx.toml";
            std::ifstream file(manifest_path, std::ios::binary);
            if (!file) {
                std::cerr << "dependency '" << name << "' missing manifest: " << manifest_path
                          << '\n';
                return false;
            }
            std::ostringstream contents;
            contents << file.rdbuf();
            Manifest dependency;
            if (!read_manifest(manifest_path, dependency))
                return false;
            lock << "\n[[package]]\nname = \"" << name << "\"\nversion = \"" << dependency.version
                 << "\"\nsource = \"path+" << dep_root.string() << "\"\nchecksum = \"" << std::hex
                 << fnv1a(contents.str()) << std::dec << "\"\n";
            if (!write_deps(dep_root, dependency))
                return false;
        }
        return true;
    };
    if (!write_deps(root, manifest))
        return 1;
    std::cout << "resolved " << visited.size() << " path dependencies\n";
    return 0;
}

int build_project(const std::filesystem::path& root,
                  const Manifest& manifest,
                  EmitKind emit,
                  bool release,
                  std::filesystem::path* output_path = nullptr) {
    std::vector<std::filesystem::path> sources;
    if (!collect_project_sources(root, manifest, sources))
        return 1;
    CompileOptions options;
    options.emit = emit;
    options.opt_level = release ? 3 : 0;
    const auto directory = root / "target" / (release ? "release" : "debug");
    std::filesystem::create_directories(directory);
    if (emit == EmitKind::Executable)
        options.output = directory / manifest.name;
    else if (emit == EmitKind::Object)
        options.output = directory / (manifest.name + ".o");
    const auto selection = source_selection(root, manifest);
    if (emit == EmitKind::Executable && selection.runtime && selection.std_mode != "none" &&
        selection.std_mode != "no_std") {
        const auto runtime = runtime_library();
        if (!runtime.empty())
            options.linker_args.push_back(runtime.string());
    }
    Compiler compiler(&std::cerr);
    auto result = compiler.compile_files(sources, options);
    if (output_path)
        *output_path = result.output;
    return result.success ? 0 : 1;
}

int format_file(const std::filesystem::path& path, bool check) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        std::cerr << "cannot open " << path << '\n';
        return 1;
    }

    std::ostringstream original_buffer;
    original_buffer << file.rdbuf();
    const std::string original = original_buffer.str();

    std::istringstream input(original);
    std::ostringstream formatted;
    std::vector<std::size_t> indentation{0};
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        while (!line.empty() && (line.back() == ' ' || line.back() == '\t'))
            line.pop_back();

        const auto first = line.find_first_not_of(" \t");
        if (first == std::string::npos) {
            formatted << '\n';
            continue;
        }

        std::size_t width = 0;
        for (std::size_t i = 0; i < first; ++i)
            width += line[i] == '\t' ? 4u : 1u;
        while (indentation.size() > 1 && width < indentation.back())
            indentation.pop_back();
        if (width > indentation.back())
            indentation.push_back(width);

        const std::size_t level = indentation.size() - 1;
        formatted << std::string(level * 4, ' ') << line.substr(first) << '\n';
    }

    const std::string output = formatted.str();
    if (check) {
        if (original != output) {
            std::cerr << path << ": needs formatting\n";
            return 1;
        }
        return 0;
    }

    std::ofstream destination(path, std::ios::binary | std::ios::trunc);
    destination << output;
    return destination ? 0 : 1;
}

int generate_docs(const std::vector<std::filesystem::path>& sources,
                  const std::filesystem::path& output_path) {
    std::ofstream output(output_path);
    if (!output)
        return 1;
    output << "# API documentation\n\nGenerated by `uinx doc`.\n\n";
    for (const auto& source_path : sources) {
        std::ifstream source_file(source_path, std::ios::binary);
        if (!source_file)
            return 1;
        std::ostringstream buffer;
        buffer << source_file.rdbuf();
        const std::string source = buffer.str();
        Diagnostics diagnostics(&std::cerr);
        Lexer lexer(source_path.string(), source, diagnostics);
        Parser parser(lexer.lex(), diagnostics);
        auto module = parser.parse_module(source_path.string());
        if (diagnostics.has_errors())
            return 1;
        bool wrote_file = false;
        for (const auto& item : module.items) {
            if (auto function = std::get_if<ast::FunctionDecl>(&item)) {
                if (!function->is_pub)
                    continue;
                if (!wrote_file) {
                    output << "## " << source_path.filename().string() << "\n\n";
                    wrote_file = true;
                }
                output << "### `func " << function->name << "`\n\n"
                       << "- safety: " << (function->is_unsafe ? "unsafe" : "safe") << "\n"
                       << "- async: " << (function->is_async ? "yes" : "no") << "\n"
                       << "- ABI: `" << function->abi << "`\n"
                       << "- returns: `" << function->return_type.str() << "`\n\n";
            } else if (auto structure = std::get_if<ast::StructDecl>(&item)) {
                if (!structure->is_pub)
                    continue;
                if (!wrote_file) {
                    output << "## " << source_path.filename().string() << "\n\n";
                    wrote_file = true;
                }
                output << "### `struct " << structure->name << "`\n\n";
                for (const auto& field : structure->fields)
                    output << "- `" << field.name << ": " << field.type.str() << "`\n";
                output << '\n';
            } else if (auto trait = std::get_if<ast::TraitDecl>(&item)) {
                if (!trait->is_pub)
                    continue;
                if (!wrote_file) {
                    output << "## " << source_path.filename().string() << "\n\n";
                    wrote_file = true;
                }
                output << "### `trait " << trait->name << "`\n\n";
                for (const auto& method : trait->methods)
                    output << "- `" << method.name << "(...) -> " << method.return_type.str()
                           << "`\n";
                output << '\n';
            }
        }
    }
    return output ? 0 : 1;
}

void usage() {
    std::cout << "uinx <new|build|run|check|test|fmt|lint|doc|fetch|add> [args]\n";
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        usage();
        return 2;
    }
    tool_path = argv[0];
    const std::string command = argv[1];
    if (command == "help" || command == "--help" || command == "-h") {
        usage();
        return 0;
    }
    if (command == "version" || command == "--version" || command == "-V") {
        std::cout << "uinx 0.2.0\n";
        return 0;
    }
    if (command == "new") {
        if (argc < 3) {
            std::cerr << "uinx new <name> [--lib] [--freestanding]\n";
            return 2;
        }
        bool library = false;
        bool freestanding = false;
        for (int i = 3; i < argc; ++i) {
            if (std::string(argv[i]) == "--lib")
                library = true;
            else if (std::string(argv[i]) == "--freestanding" || std::string(argv[i]) == "--no-std")
                freestanding = true;
        }
        const std::filesystem::path root = argv[2];
        std::filesystem::create_directories(root / "src");
        const std::string entry = library ? "src/lib.ux" : "src/main.ux";
        std::ofstream(root / "uinx.toml")
            << "[package]\nname = \"" << root.filename().string()
            << "\"\nversion = \"0.1.0\"\nentry = \"" << entry << "\"\nkind = \""
            << (library ? "lib" : "bin") << "\"\nstd = \"" << (freestanding ? "none" : "full")
            << "\"\n\n[dependencies]\n";
        std::ofstream source(root / entry);
        if (freestanding)
            source << "dontneed std\n\n";
        if (library)
            source << "public func library_ready() -> bool:\n    return true\n";
        else
            source << "func main() -> i32:\n    return 0\n";
        std::cout << "created " << root << '\n';
        return 0;
    }

    const auto root = find_root();
    if (root.empty()) {
        std::cerr << "uinx.toml not found in this directory or parents\n";
        return 1;
    }
    Manifest manifest;
    if (!read_manifest(root / "uinx.toml", manifest)) {
        std::cerr << "cannot read manifest\n";
        return 1;
    }
    bool release = false;
    for (int i = 2; i < argc; ++i)
        if (std::string(argv[i]) == "--release")
            release = true;

    if (command == "fetch")
        return fetch(root, manifest);
    if (command == "build")
        return build_project(root,
                             manifest,
                             manifest.kind == "lib" ? EmitKind::Object : EmitKind::Executable,
                             release);
    if (command == "check")
        return build_project(root, manifest, EmitKind::Check, release);
    if (command == "run") {
        if (manifest.kind == "lib") {
            std::cerr << "cannot run a library package\n";
            return 2;
        }
        std::filesystem::path executable;
        const int result =
            build_project(root, manifest, EmitKind::Executable, release, &executable);
        if (result)
            return result;
        return std::system(("\"" + executable.string() + "\"").c_str());
    }
    if (command == "test") {
        const auto tests = ux_files_under(root / "tests");
        if (tests.empty()) {
            std::cout << "no tests directory or .ux tests\n";
            return 0;
        }
        std::vector<std::filesystem::path> base;
        if (!collect_project_sources(root, manifest, base))
            return 1;
        base.erase(std::remove_if(base.begin(),
                                  base.end(),
                                  [&](const auto& source) {
                                      return std::filesystem::weakly_canonical(source) ==
                                                 std::filesystem::weakly_canonical(
                                                     root / manifest.entry) &&
                                             manifest.kind == "bin";
                                  }),
                   base.end());
        std::filesystem::create_directories(root / "target/tests");
        int failed = 0;
        for (const auto& test : tests) {
            auto sources = base;
            sources.push_back(test);
            CompileOptions options;
            options.emit = EmitKind::Executable;
            options.output = root / "target/tests" / test.stem();
            const auto runtime = runtime_library();
            const auto selection = source_selection(root, manifest);
            if (!runtime.empty() && selection.runtime && selection.std_mode != "none")
                options.linker_args.push_back(runtime.string());
            Compiler compiler(&std::cerr);
            const auto result = compiler.compile_files(sources, options);
            if (!result.success ||
                std::system(("\"" + options.output.string() + "\"").c_str()) != 0)
                ++failed;
        }
        std::cout << (static_cast<int>(tests.size()) - failed) << "/" << tests.size()
                  << " tests passed\n";
        return failed ? 1 : 0;
    }
    if (command == "fmt") {
        bool check = false;
        for (int i = 2; i < argc; ++i)
            if (std::string(argv[i]) == "--check")
                check = true;
        int failed = 0;
        for (const auto& source : ux_files_under(root / "src"))
            failed += format_file(source, check) != 0;
        return failed ? 1 : 0;
    }
    if (command == "lint") {
        std::vector<std::filesystem::path> sources;
        if (!collect_project_sources(root, manifest, sources))
            return 1;
        CompileOptions options;
        options.emit = EmitKind::Check;
        Compiler compiler(&std::cerr);
        const auto result = compiler.compile_files(sources, options);
        if (result.success)
            std::cout << "lint: semantic, type, ownership and borrow checks passed\n";
        return result.success ? 0 : 1;
    }
    if (command == "doc") {
        std::vector<std::filesystem::path> sources;
        if (!collect_project_sources(root, manifest, sources, false))
            return 1;
        const auto directory = root / "target/doc";
        std::filesystem::create_directories(directory);
        const auto output = directory / "api.md";
        const int result = generate_docs(sources, output);
        if (!result)
            std::cout << output << '\n';
        return result;
    }
    if (command == "add") {
        if (argc < 5 || std::string(argv[3]) != "--path") {
            std::cerr << "uinx add <name> --path <directory>\n";
            return 2;
        }
        const std::string name = argv[2];
        const std::string path = argv[4];
        std::ifstream input(root / "uinx.toml");
        std::ostringstream buffer;
        buffer << input.rdbuf();
        std::string text = buffer.str();
        const std::string dependency = name + " = { path = \"" + path + "\" }\n";
        const auto section = text.find("[dependencies]");
        if (section == std::string::npos) {
            if (!text.empty() && text.back() != '\n')
                text += '\n';
            text += "\n[dependencies]\n" + dependency;
        } else {
            auto insert = text.find('\n', section);
            if (insert == std::string::npos) {
                text += '\n';
                insert = text.size() - 1;
            }
            ++insert;
            auto next = text.find("\n[", insert);
            if (next == std::string::npos)
                next = text.size();
            text.insert(next, dependency);
        }
        std::ofstream output(root / "uinx.toml", std::ios::trunc);
        output << text;
        if (!output)
            return 1;
        std::cout << "added path dependency " << name << '\n';
        return 0;
    }

    usage();
    return 2;
}
