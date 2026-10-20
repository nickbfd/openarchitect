#include "oa/core/serialization.h"

#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

using nlohmann::json;

namespace oa {

NLOHMANN_JSON_SERIALIZE_ENUM(RoomType, {
    {RoomType::Living, "living"},
    {RoomType::Bedroom, "bedroom"},
    {RoomType::Kitchen, "kitchen"},
    {RoomType::Bath, "bath"},
    {RoomType::Garage, "garage"},
    {RoomType::Deck, "deck"},
    {RoomType::Balcony, "balcony"},
    {RoomType::Other, "other"},
})
NLOHMANN_JSON_SERIALIZE_ENUM(OpeningKind, {{OpeningKind::Door, "door"}, {OpeningKind::Window, "window"}})
NLOHMANN_JSON_SERIALIZE_ENUM(RoofStyle, {{RoofStyle::Gable, "gable"}, {RoofStyle::Hip, "hip"}})
NLOHMANN_JSON_SERIALIZE_ENUM(RoofFraming, {{RoofFraming::Rafters, "rafters"}, {RoofFraming::Trusses, "trusses"}})
NLOHMANN_JSON_SERIALIZE_ENUM(WallKind, {{WallKind::Exterior, "exterior"}, {WallKind::Interior, "interior"},
                                       {WallKind::RoomDivider, "roomDivider"}})
NLOHMANN_JSON_SERIALIZE_ENUM(Axis, {{Axis::X, "x"}, {Axis::Y, "y"}})

// Points are written as [x, y] to keep files compact and readable.
void to_json(json& j, const Vec2& v) { j = json::array({v.x, v.y}); }
void from_json(const json& j, Vec2& v) {
    v.x = j.at(0).get<double>();
    v.y = j.at(1).get<double>();
}

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Rect, min, max)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Level, id, name, elevation, wallHeight, floorThickness)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(WallType, id, name, kind, studDepth, studSpacing,
                                                interiorFinish, sheathing, siding, insulated, insulation)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Wall, id, levelId, a, b, wallTypeId, thickness, height,
                                                bearing, selfWeightPsf, roomDivider)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Opening, id, wallId, kind, offset, width, height,
                                                sillHeight, headerMemberId)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Room, id, levelId, name, type, outline)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(FloorPlatform, id, levelId, area, joistAxis, deadPsf,
                                                livePsf)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Roof, id, levelId, style, footprint, ridgeAxis, pitch,
                                                overhang, deadPsf, framing, structuralRidge, hasCeiling,
                                                ceilingDeadPsf, atticLivePsf, customSnow, snowPsf)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(RoofPlane, id, levelId, a, b, outline, highBearing, run,
                                                pitch, overhang, deadPsf, framing, hasCeiling, ceilingDeadPsf,
                                                atticLivePsf, customSnow, snowPsf)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Post, id, levelId, position, memberId, height)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Beam, id, levelId, a, b, memberId)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Site, groundSnowPsf, exposureCe, thermalCt, importanceIs)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Document, name, site, levels, wallTypes, walls, openings,
                                                rooms, platforms, roofs, roofPlanes, beams, posts, nextId)

namespace {

// Upgrades a file one schema version at a time. Add a case per new version.
void migrate(json& root, int fromVersion) {
    for (int v = fromVersion; v < kPlanSchemaVersion; ++v) {
        switch (v) {
        default: break;
        }
    }
    root["schemaVersion"] = kPlanSchemaVersion;
}

} // namespace

std::string documentToJson(const Document& doc) {
    json root;
    root["format"] = kPlanFormatTag;
    root["schemaVersion"] = kPlanSchemaVersion;
    root["document"] = doc;
    return root.dump(2);
}

Document documentFromJson(const std::string& text) {
    json root;
    try {
        root = json::parse(text);
    } catch (const json::parse_error& e) {
        throw FileError(std::string("The file is not valid JSON: ") + e.what());
    }
    if (!root.is_object() || root.value("format", "") != kPlanFormatTag)
        throw FileError("The file is not an OpenArchitect plan.");

    const int version = root.value("schemaVersion", 0);
    if (version < 1) throw FileError("The plan file has no valid schema version.");
    if (version > kPlanSchemaVersion)
        throw FileError("The plan was saved by a newer version of OpenArchitect (schema " +
                        std::to_string(version) + "). Please update the app.");
    if (version < kPlanSchemaVersion) migrate(root, version);

    try {
        Document doc = root.at("document").get<Document>();
        // Guard against hand-edited files that reuse ids.
        ObjectId maxId = 0;
        auto scan = [&maxId](const auto& v) {
            for (const auto& o : v) maxId = std::max(maxId, o.id);
        };
        scan(doc.levels); scan(doc.walls); scan(doc.openings); scan(doc.rooms);
        scan(doc.platforms); scan(doc.roofs); scan(doc.roofPlanes); scan(doc.beams);
        scan(doc.wallTypes); scan(doc.posts);
        doc.nextId = std::max(doc.nextId, maxId + 1);
        doc.upgrade();
        return doc;
    } catch (const json::exception& e) {
        throw FileError(std::string("The plan file is damaged: ") + e.what());
    }
}

void saveDocument(const Document& doc, const std::filesystem::path& path) {
    // Write beside the target and swap in, so a failed save never destroys
    // the previous file.
    auto tmp = path;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) throw FileError("Cannot write " + path.string());
        out << documentToJson(doc);
        if (!out) throw FileError("Failed while writing " + path.string());
    }
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        std::filesystem::remove(path, ec);
        std::filesystem::rename(tmp, path, ec);
        if (ec) throw FileError("Cannot replace " + path.string() + ": " + ec.message());
    }
}

Document loadDocument(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw FileError("Cannot open " + path.string());
    std::stringstream ss;
    ss << in.rdbuf();
    return documentFromJson(ss.str());
}

} // namespace oa
