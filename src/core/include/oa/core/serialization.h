#pragma once

#include "oa/core/document.h"

#include <filesystem>
#include <stdexcept>
#include <string>

namespace oa {

// Plan files (.oaplan) are JSON with a format tag and a schema version.
// Older files are migrated step by step on load; files from a newer version
// of the app are refused rather than silently losing data.
//
// The plan describes a zip container for plans and layouts; this first
// version stores the JSON document directly and will move into the
// container once embedded assets (textures, symbols) exist.
constexpr int kPlanSchemaVersion = 1;
constexpr const char* kPlanFormatTag = "openarchitect-plan";
constexpr const char* kPlanExtension = ".oaplan";

struct FileError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

std::string documentToJson(const Document& doc);
Document documentFromJson(const std::string& text);

void saveDocument(const Document& doc, const std::filesystem::path& path);
Document loadDocument(const std::filesystem::path& path);

} // namespace oa
