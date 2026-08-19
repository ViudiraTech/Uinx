// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 ViudiraTech
// By JiTianYu391

#include "uinx/compiler.hpp"

#include <iostream>
#include <sstream>
#include <unordered_map>

using namespace uinx;
static std::string esc(std::string_view s) {
    std::string o;
    for (char c : s) {
        if (c == '"')
            o += "\\\"";
        else if (c == '\\')
            o += "\\\\";
        else if (c == '\n')
            o += "\\n";
        else if (c == '\r')
            o += "\\r";
        else
            o += c;
    }
    return o;
}
static std::string field(const std::string& j, const std::string& key) {
    auto p = j.find("\"" + key + "\"");
    if (p == std::string::npos)
        return {};
    p = j.find(':', p);
    if (p == std::string::npos)
        return {};
    p = j.find('"', p);
    if (p == std::string::npos)
        return {};
    ++p;
    std::string o;
    bool e = false;
    for (; p < j.size(); ++p) {
        char c = j[p];
        if (e) {
            if (c == 'n')
                o += '\n';
            else if (c == 'r')
                o += '\r';
            else if (c == 't')
                o += '\t';
            else
                o += c;
            e = false;
        } else if (c == '\\')
            e = true;
        else if (c == '"')
            break;
        else
            o += c;
    }
    return o;
}
static std::string id_field(const std::string& j) {
    auto p = j.find("\"id\"");
    if (p == std::string::npos)
        return "null";
    p = j.find(':', p);
    if (p == std::string::npos)
        return "null";
    ++p;
    while (p < j.size() && std::isspace(static_cast<unsigned char>(j[p])))
        ++p;
    if (p < j.size() && j[p] == '"') {
        auto e = j.find('"', p + 1);
        return e == std::string::npos ? "null" : j.substr(p, e - p + 1);
    }
    auto e = p;
    while (e < j.size() && (std::isdigit(static_cast<unsigned char>(j[e])) || j[e] == '-'))
        ++e;
    return j.substr(p, e - p);
}
static void send(const std::string& s) {
    std::cout << "Content-Length: " << s.size() << "\r\n\r\n" << s << std::flush;
}
static void diagnostics(const std::string& uri, const std::string& text) {
    Compiler c;
    CompileOptions o;
    o.emit = EmitKind::Check;
    auto r = c.compile_source(uri, text, o);
    std::ostringstream j;
    j << "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/"
         "publishDiagnostics\",\"params\":{\"uri\":\""
      << esc(uri) << "\",\"diagnostics\":[";
    for (std::size_t i = 0; i < r.diagnostics.size(); ++i) {
        auto& d = r.diagnostics[i];
        if (i)
            j << ',';
        j << "{\"range\":{\"start\":{\"line\":" << (d.range.begin.line ? d.range.begin.line - 1 : 0)
          << ",\"character\":" << (d.range.begin.column ? d.range.begin.column - 1 : 0)
          << "},\"end\":{\"line\":" << (d.range.end.line ? d.range.end.line - 1 : 0)
          << ",\"character\":" << (d.range.end.column ? d.range.end.column - 1 : 0)
          << "}},\"severity\":"
          << (d.level == DiagLevel::Error     ? 1
              : d.level == DiagLevel::Warning ? 2
                                              : 3)
          << ",\"code\":\"" << esc(d.code) << "\",\"source\":\"uinx\",\"message\":\""
          << esc(d.message) << "\"}";
    }
    j << "]}}";
    send(j.str());
}
int main() {
    std::unordered_map<std::string, std::string> docs;
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.rfind("Content-Length:", 0) != 0)
            continue;
        std::size_t len = std::stoul(line.substr(15));
        while (std::getline(std::cin, line) && line != "\r" && !line.empty()) {
        }
        std::string body(len, '\0');
        std::cin.read(body.data(), static_cast<std::streamsize>(len));
        std::string method = field(body, "method"), id = id_field(body);
        if (method == "initialize") {
            send("{\"jsonrpc\":\"2.0\",\"id\":" + id +
                 ",\"result\":{\"capabilities\":{\"textDocumentSync\":1,\"hoverProvider\":true}}}");
        } else if (method == "shutdown") {
            send("{\"jsonrpc\":\"2.0\",\"id\":" + id + ",\"result\":null}");
        } else if (method == "exit")
            return 0;
        else if (method == "textDocument/didOpen" || method == "textDocument/didChange") {
            std::string uri = field(body, "uri"), text = field(body, "text");
            if (!uri.empty()) {
                docs[uri] = text;
                diagnostics(uri, text);
            }
        } else if (method == "textDocument/hover") {
            send("{\"jsonrpc\":\"2.0\",\"id\":" + id +
                 ",\"result\":{\"contents\":{\"kind\":\"markdown\",\"value\":\"Uinx symbol "
                 "information is provided by compiler diagnostics and semantic analysis.\"}}}");
        } else if (id != "null")
            send("{\"jsonrpc\":\"2.0\",\"id\":" + id + ",\"result\":null}");
    }
    return 0;
}
