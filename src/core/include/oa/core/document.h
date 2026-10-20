#pragma once

#include "oa/core/geometry.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace oa {

// Permanent identifier of an object inside one plan file. Never reused.
using ObjectId = std::uint64_t;
constexpr ObjectId kNoId = 0;

enum class RoomType { Living, Bedroom, Kitchen, Bath, Garage, Deck, Balcony, Other };
enum class OpeningKind { Door, Window };
enum class RoofStyle { Gable, Hip };
// Direction framing members run (joists, rafters) or a ridge runs.
enum class Axis { X, Y };

inline Axis otherAxis(Axis a) { return a == Axis::X ? Axis::Y : Axis::X; }

// One storey. Elevation is the top of the floor surface; walls of the level
// stand on it, and the floor framing of the level above sits on top of them.
struct Level {
    ObjectId id = kNoId;
    std::string name;
    double elevation = 0.0;        // inches
    double wallHeight = 97.125;    // inches, plate to plate (92 5/8" stud + 3 plates)
    double floorThickness = 12.875;// inches, joist depth + subfloor of the level above
};

enum class WallKind { Exterior, Interior, RoomDivider };

// A named wall build-up, like "2x6 exterior": stud size and spacing, the
// layers on each face, and insulation. Walls of a type take their thickness
// and weight from it, and the framing view builds their studs from it.
struct WallType {
    ObjectId id = kNoId;
    std::string name;
    WallKind kind = WallKind::Exterior;
    double studDepth = 5.5;        // actual, inches (3.5 for 2x4, 5.5 for 2x6)
    double studSpacing = 16.0;     // on center, inches
    double interiorFinish = 0.5;   // gypsum board on the room side, inches
    double sheathing = 0.4375;     // structural panel, exterior walls only
    double siding = 0.75;          // exterior finish, exterior walls only
    bool insulated = true;
    std::string insulation = "R-21 batts";

    // Finished thickness. Interior walls have finish on both faces.
    double thickness() const;
    // Dead load per square foot of wall face: studs and plates, layers, insulation.
    double selfWeightPsf() const;
    bool isDivider() const { return kind == WallKind::RoomDivider; }
};

// Wall types a new plan starts with.
std::vector<WallType> defaultWallTypes();

struct Wall {
    ObjectId id = kNoId;
    ObjectId levelId = kNoId;
    Vec2 a, b;                 // centerline end points, inches
    ObjectId wallTypeId = kNoId; // kNoId: thickness and weight are set by hand
    double thickness = 5.5;    // inches; follows the wall type when it has one
    double height = 0.0;       // inches; 0 means the level's wall height
    bool bearing = true;
    double selfWeightPsf = 10.0; // dead load per square foot of wall face; follows the wall type
    bool roomDivider = false;  // an invisible wall that only separates rooms; follows the wall type

    Segment centerline() const { return {a, b}; }
    double length() const { return distance(a, b); }
};

struct Opening {
    ObjectId id = kNoId;
    ObjectId wallId = kNoId;
    OpeningKind kind = OpeningKind::Window;
    double offset = 0.0;     // inches from wall.a to the opening's center
    double width = 36.0;     // rough opening, inches
    double height = 48.0;    // inches
    double sillHeight = 36.0;// inches above the level's floor (0 for doors)
    std::string headerMemberId; // catalog member chosen for the header, empty until one is picked

    double topHeight() const { return sillHeight + height; }
};

// What a room's floor is built from. Most rooms sit on the level's floor
// framing; a room can have its own joists (a flush shower framed with
// deeper or closer joists), or a concrete slab (a garage or patio).
enum class FloorSystemKind { LevelFraming, Joists, Slab };

struct RoomFloor {
    FloorSystemKind kind = FloorSystemKind::LevelFraming;
    std::string joists = "2x10 @ 16\" o.c."; // description of the joists, for Joists
    bool customJoistAxis = false;          // false: joists run the way the level's framing does
    Axis joistAxis = Axis::Y;
    double deadPsf = 10.0;                 // joists, subfloor and finish
    double livePsf = 0.0;                  // 0: the room type's IRC live load
    double slabThickness = 4.0;            // inches, for Slab
    std::string slabNote = "3000 psi concrete on 4\" compacted gravel";
    double drop = 0.0;                     // how far the floor surface sits below the level's floor, inches
};

struct Room {
    ObjectId id = kNoId;
    ObjectId levelId = kNoId;
    std::string name;
    RoomType type = RoomType::Living;
    Polygon outline;
    RoomFloor floor;
};

// A dimension line on the plan from `a` to `b`, drawn `offset` inches to
// the left of a to b (negative: to the right).
struct Dimension {
    ObjectId id = kNoId;
    ObjectId levelId = kNoId;
    Vec2 a, b;
    double offset = 24.0;
    bool automatic = false; // made by "Dimension exterior walls", replaced when that runs again

    double length() const { return distance(a, b); }
};

// A spot elevation on the site, relative to the first floor (0 = the
// lowest level's floor). The terrain surface runs between these points.
// Where dimensions to walls land: the outside face of the studs, or the
// middle of the studs.
enum class DimensionTo { FramingEdge, FramingCenter };

struct ElevationPoint {
    ObjectId id = kNoId;
    Vec2 position;
    double elevation = -12.0;
};

struct Terrain {
    std::vector<ElevationPoint> points;
    double contourInterval = 12.0; // inches between contour lines
};

// The floor structure of a level, bearing on walls and beams of the level
// below. Joists run along `joistAxis`; supports are found perpendicular to it.
struct FloorPlatform {
    ObjectId id = kNoId;
    ObjectId levelId = kNoId;
    Rect area;
    Axis joistAxis = Axis::Y;
    double deadPsf = 10.0;
    double livePsf = 40.0;
};

// How a roof is framed. Rafters bear on every wall below them through
// ceiling joists and can use a structural ridge; trusses span from eave to
// eave, so the ceiling hung from their bottom chords bears on the eave walls only.
enum class RoofFraming { Rafters, Trusses };

// Settings shared by every kind of roof: slope, overhang, framing, the
// ceiling below it and its design loads.
struct RoofSettings {
    double pitch = 6.0;          // rise in 12
    double overhang = 18.0;      // inches, horizontal, past the eave bearing line
    double deadPsf = 15.0;       // on the roof surface
    RoofFraming framing = RoofFraming::Rafters;
    bool hasCeiling = true;      // ceiling joists or truss bottom chords carrying a ceiling
    double ceilingDeadPsf = 10.0;
    double atticLivePsf = 10.0;  // IRC: 10 psf uninhabitable attic without storage
    bool customSnow = false;     // use snowPsf instead of the snow calculated from the site
    double snowPsf = 0.0;        // balanced roof snow on the horizontal projection, psf

    double slopeRadians() const { return std::atan(pitch / 12.0); }
};

// A rectangular gable or hip roof bearing on the walls of `levelId`. The
// footprint is measured to the bearing lines (wall centerlines); overhang
// extends beyond it on the eave sides (and on all sides for a hip roof).
struct Roof : RoofSettings {
    ObjectId id = kNoId;
    ObjectId levelId = kNoId;
    RoofStyle style = RoofStyle::Gable;
    Rect footprint;
    Axis ridgeAxis = Axis::X;
    bool structuralRidge = false;// ridge beam carrying rafters, versus ridge board with ties

    bool hasRidgeBeam() const {
        return structuralRidge && style == RoofStyle::Gable && framing == RoofFraming::Rafters;
    }
};

// One flat sloped roof plane, drawn on its own: a shed roof, a porch roof,
// or one face of a roof built plane by plane. Its baseline from `a` to `b`
// is where it bears on its eave wall or beam, at the plate; the plane rises
// to the left of that line. `outline` is the edge of the roof surface in
// plan, overhangs included, so every side can be moved on its own.
struct RoofPlane : RoofSettings {
    ObjectId id = kNoId;
    ObjectId levelId = kNoId;
    Vec2 a, b;
    Polygon outline;
    // True when the high side bears on a wall or beam (a shed roof); false
    // when it leans on other planes at a ridge or hip with a ridge board, so
    // the eave carries all of it, like each side of a gable.
    bool highBearing = true;
    double run = 0.0; // only read from older files that had no outline

    RoofPlane() {
        pitch = 3.0;
        hasCeiling = false;
    }

    Vec2 along() const { return (b - a).normalized(); }
    Vec2 upslope() const { return along().perp(); }
    // Horizontal distance uphill from the baseline (negative in the eave overhang).
    double upFrom(Vec2 p) const { return dot(p - a, upslope()); }
    // Distance along the baseline from `a`.
    double alongFrom(Vec2 p) const { return dot(p - a, along()); }
    // Height of the roof surface above the plate line at a plan point.
    double riseAt(Vec2 p) const { return upFrom(p) * pitch / 12.0; }
    // The outline's extent across the slope, measured along the baseline.
    double width() const;
    // Farthest the outline reaches uphill, and downhill past the baseline.
    double runLength() const;
    double eaveOverhang() const;
    // A rectangular plane: `length` along the baseline, rising `run`, with
    // `overhang` past the baseline.
    static Polygon rectangle(Vec2 a, Vec2 b, double run, double overhang);
};

// A post standing on `levelId`, carrying the beam ends above it down to
// whatever is beneath. `memberId` names the post size from the post catalog.
struct Post {
    ObjectId id = kNoId;
    ObjectId levelId = kNoId;
    Vec2 position;
    std::string memberId = "dfl-no2-4x4";
    double height = 0.0; // inches; 0 means the level's wall height
};

// A dropped or flush beam on `levelId`, carrying what sits on top of it from
// the level above (floor framing, walls). `memberId` names the catalog member
// chosen for it, empty until one is picked.
struct Beam {
    ObjectId id = kNoId;
    ObjectId levelId = kNoId;
    Vec2 a, b;
    std::string memberId;

    Segment centerline() const { return {a, b}; }
    double length() const { return distance(a, b); }
};

// Site design inputs for snow (ASCE 7).
struct Site {
    double groundSnowPsf = 30.0; // pg
    double exposureCe = 1.0;
    double thermalCt = 1.1;      // cold, ventilated attic
    double importanceIs = 1.0;
};

struct Document {
    std::string name = "Untitled";
    Site site;
    std::vector<Level> levels;
    std::vector<WallType> wallTypes;
    std::vector<Wall> walls;
    std::vector<Opening> openings;
    std::vector<Room> rooms;
    std::vector<FloorPlatform> platforms;
    std::vector<Roof> roofs;
    std::vector<RoofPlane> roofPlanes;
    std::vector<Beam> beams;
    std::vector<Post> posts;
    std::vector<Dimension> dimensions;
    DimensionTo dimensionTo = DimensionTo::FramingEdge;
    Terrain terrain;
    ObjectId nextId = 1;

    ObjectId newId() { return nextId++; }

    // A new plan with two levels, the way most houses start.
    static Document createDefault();

    template <class T> static T* findIn(std::vector<T>& v, ObjectId id) {
        auto it = std::find_if(v.begin(), v.end(), [id](const T& o) { return o.id == id; });
        return it == v.end() ? nullptr : &*it;
    }
    template <class T> static const T* findIn(const std::vector<T>& v, ObjectId id) {
        auto it = std::find_if(v.begin(), v.end(), [id](const T& o) { return o.id == id; });
        return it == v.end() ? nullptr : &*it;
    }

    Level* level(ObjectId id) { return findIn(levels, id); }
    const Level* level(ObjectId id) const { return findIn(levels, id); }
    Wall* wall(ObjectId id) { return findIn(walls, id); }
    const Wall* wall(ObjectId id) const { return findIn(walls, id); }
    Opening* opening(ObjectId id) { return findIn(openings, id); }
    const Opening* opening(ObjectId id) const { return findIn(openings, id); }
    Room* room(ObjectId id) { return findIn(rooms, id); }
    FloorPlatform* platform(ObjectId id) { return findIn(platforms, id); }
    Roof* roof(ObjectId id) { return findIn(roofs, id); }
    const Roof* roof(ObjectId id) const { return findIn(roofs, id); }
    RoofPlane* roofPlane(ObjectId id) { return findIn(roofPlanes, id); }
    const RoofPlane* roofPlane(ObjectId id) const { return findIn(roofPlanes, id); }
    // The settings of a roof or a roof plane.
    RoofSettings* roofSettings(ObjectId id);
    const RoofSettings* roofSettings(ObjectId id) const;
    WallType* wallType(ObjectId id) { return findIn(wallTypes, id); }
    const WallType* wallType(ObjectId id) const { return findIn(wallTypes, id); }
    Dimension* dimension(ObjectId id) { return findIn(dimensions, id); }
    const Dimension* dimension(ObjectId id) const { return findIn(dimensions, id); }
    ElevationPoint* elevationPoint(ObjectId id) { return findIn(terrain.points, id); }
    const ElevationPoint* elevationPoint(ObjectId id) const { return findIn(terrain.points, id); }
    // The lowest level, where the terrain is drawn and edited.
    const Level* groundLevel() const;
    // Live load on a room's floor: its own, or its type's IRC value.
    double roomLivePsf(const Room& r) const;
    Post* post(ObjectId id) { return findIn(posts, id); }
    const Post* post(ObjectId id) const { return findIn(posts, id); }
    // The first wall type of a kind, or nullptr.
    const WallType* firstWallType(WallKind kind) const;
    Beam* beam(ObjectId id) { return findIn(beams, id); }
    const Beam* beam(ObjectId id) const { return findIn(beams, id); }

    // Levels sorted from lowest to highest elevation.
    std::vector<const Level*> levelsByElevation() const;
    // The level directly beneath, or nullptr for the lowest level.
    const Level* levelBelow(ObjectId levelId) const;
    const Level* levelAbove(ObjectId levelId) const;

    double wallHeight(const Wall& w) const;
    double postHeight(const Post& p) const;
    // The level an object is on (an opening's is its wall's), or kNoId.
    ObjectId levelOf(ObjectId id) const;
    bool contains(ObjectId id) const;
    std::vector<const Opening*> openingsIn(ObjectId wallId) const;

    // Removes any object by id, along with objects that depend on it
    // (openings in a removed wall, everything on a removed level).
    bool remove(ObjectId id);

    // Brings derived data up to date: walls take their type's thickness and
    // weight, and plans from older versions get wall types and roof plane
    // outlines. Loading a file calls this.
    void upgrade();
    // upgrade(), then re-finds the rooms from the walls. Every edit calls this.
    void refresh();
};

// Height of the wall plates of a level, where roofs on it bear.
double plateHeight(const Document& doc, ObjectId levelId);
// Height of a roof plane's surface at a plan point.
double roofPlaneHeight(const Document& doc, const RoofPlane& plane, Vec2 p);
// Chief Architect's "join roof planes": the outline of `plane` with edge
// `edge` moved onto the line where it meets `other` (a hip, valley or
// ridge). Empty when the planes are parallel or the edge cannot reach.
Polygon joinRoofPlanes(const Document& doc, const RoofPlane& plane, int edge, const RoofPlane& other);

const char* toString(RoomType t);
// IRC Table R301.5 uniform live load for a room type, psf.
double defaultLiveLoadPsf(RoomType t);

} // namespace oa
