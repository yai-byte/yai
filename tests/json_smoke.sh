#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TMP_DIR="$(mktemp -d)"

cleanup() {
  rm -rf "$TMP_DIR"
}
trap cleanup EXIT

cat > "$TMP_DIR/json_test.cpp" <<'CPP'
#include "yai.hpp"

#include <iostream>
#include <stdexcept>

static void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

int main() {
    const std::string text =
        "{"
        "\"schema_version\":1,"
        "\"name\":\"Demo \\\"App\\\"\","
        "\"summary\":\"line\\nnext\","
        "\"source\":{\"type\":\"github_release\",\"repo\":\"demo\"},"
        "\"packages\":["
        "{\"id\":\"one\",\"note\":\"brace } in string\"},"
        "{\"id\":\"two\",\"nested\":{\"ok\":\"yes\"}}"
        "],"
        "\"browser_download_url\":\"https://example.invalid/One.AppImage\","
        "\"browser_download_url\":\"https://example.invalid/Two.AppImage\""
        "}";

    require(json_find_int(text, "schema_version").value_or(0) == 1, "schema_version parse failed");
    require(json_find_string(text, "name").value_or("") == "Demo \"App\"", "string unescape failed");
    require(json_find_string(text, "summary").value_or("") == "line\nnext", "newline unescape failed");

    const std::optional<std::string> source = json_find_object(text, "source");
    require(source.has_value(), "source object not found");
    require(json_find_string(*source, "type").value_or("") == "github_release", "nested object string failed");

    const std::optional<std::string> packages = json_find_array(text, "packages");
    require(packages.has_value(), "packages array not found");
    const std::vector<std::string> objects = json_top_level_objects(*packages);
    require(objects.size() == 2, "top-level object count failed");
    require(json_find_string(objects[0], "id").value_or("") == "one", "first object failed");
    require(json_find_string(objects[1], "id").value_or("") == "two", "second object failed");
    require(json_find_object(objects[1], "nested").has_value(), "nested object extraction failed");

    const std::vector<std::string> urls = json_find_all_strings(text, "browser_download_url");
    require(urls.size() == 2, "all string collection failed");
    require(urls[1] == "https://example.invalid/Two.AppImage", "second repeated string failed");

    require(
        json_escape_string("quote\" slash\\ line\n tab\t") == "quote\\\" slash\\\\ line\\n tab\\t",
        "json string escaping failed");

    const std::string typed_values =
        "{"
        "\"enabled\": false,"
        "\"count\": 123,"
        "\"empty\": null,"
        "\"items\": [\"not a string field value\"],"
        "\"object\": {\"name\":\"nested\"},"
        "\"next\":\"real string\""
        "}";
    require(!json_find_string(typed_values, "enabled").has_value(), "boolean accepted as string");
    require(!json_find_string(typed_values, "count").has_value(), "number accepted as string");
    require(!json_find_string(typed_values, "empty").has_value(), "null accepted as string");
    require(!json_find_string(typed_values, "items").has_value(), "array accepted as string");
    require(!json_find_string(typed_values, "object").has_value(), "object accepted as string");
    require(json_find_string(typed_values, "next").value_or("") == "real string", "real string after typed values failed");

    const std::string escaped_nested =
        "["
        "{\"id\":\"escaped\",\"note\":\"quote \\\" slash \\\\ newline \\n brace } bracket ]\"},"
        "{\"id\":\"deep\",\"child\":{\"items\":[{\"name\":\"inner\"}]}}"
        "]";
    const std::vector<std::string> escaped_objects = json_top_level_objects(escaped_nested);
    require(escaped_objects.size() == 2, "escaped nested top-level object count failed");
    require(
        json_find_string(escaped_objects[0], "note").value_or("").find("brace } bracket ]") != std::string::npos,
        "escaped nested string failed");
    require(json_find_object(escaped_objects[1], "child").has_value(), "deep nested object failed");

    require(!json_find_array("{\"packages\": \"not array\"}", "packages").has_value(), "non-array accepted");
    require(!json_find_object("{\"source\": \"not object\"}", "source").has_value(), "non-object accepted");
    require(!json_find_string("{\"name\":\"unterminated}", "name").has_value(), "unterminated string accepted");
    require(!json_find_array("{\"packages\":[{\"id\":\"one\"}", "packages").has_value(), "unterminated array accepted");
    require(json_top_level_objects("[{\"id\":\"one\"").empty(), "unterminated object extracted");

    std::cout << "json smoke test passed\n";
    return 0;
}
CPP

g++ -std=c++17 -Wall -Wextra -Wpedantic -O2 -I"$ROOT/src" \
  -o "$TMP_DIR/json_test" \
  "$TMP_DIR/json_test.cpp" "$ROOT/src/json.cpp"

"$TMP_DIR/json_test"
