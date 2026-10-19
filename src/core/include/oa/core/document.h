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

struct Wall {
    ObjectId id = kNoId;
    ObjectId levelId = kNoId;
    Vec2 a, b;                 // centerline end points, inches
    double thickness = 5.5;    // inches
    double height = 0.0;       // inches; 0 means the level's wall height
    bool bearing = true;
    double selfWeightPsf = 10.0; // dead load per square foot of wall face

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
};

struct Room {
    ObjectId id = kNoId;
    ObjectId levelId = kNoId;
    std::string name;
    RoomType type = RoomType::Living;
    Polygon outline;
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
// or one face of a roof built plane by plane. It bears along its low (eave)
// line from `a` to `b` and rises to the left of that line for `run` inches,
// where it bears on its high line. The overhang extends past the eave.
struct RoofPlane : RoofSettings {
    ObjectId id = kNoId;
    ObjectId levelId = kNoId;
    Vec2 a, b;
    double run = 120.0;

    RoofPlane() {
        pitch = 3.0;
        hasCeiling = false;
    }

    Vec2 upslope() const { return (b - a).normalized().perp(); }
    Segment eaveLine() const { return {a, b}; }
    Segment highLine() const { return {a + upslope() * run, b + upslope() * run}; }
    double eaveLength() const { return distance(a, b); }
    // Corners of the bearing area, or of the roof surface with `overhang` added.
    Polygon outline(bool withOverhang = false) const {
        const Vec2 down = upslope() * -(withOverhang ? overhang : 0.0);
        const Vec2 up = upslope() * run;
        return {a + down, b + down, b + up, a + up};
    }
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
    std::vector<Wall> walls;
    std::vector<Opening> openings;
    std::vector<Room> rooms;
    std::vector<FloorPlatform> platforms;
    std::vector<Roof> roofs;
    std::vector<RoofPlane> roofPlanes;
    std::vector<Beam> beams;
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
    Beam* beam(ObjectId id) { return findIn(beams, id); }
    const Beam* beam(ObjectId id) const { return findIn(beams, id); }

    // Levels sorted from lowest to highest elevation.
    std::vector<const Level*> levelsByElevation() const;
    // The level directly beneath, or nullptr for the lowest level.
    const Level* levelBelow(ObjectId levelId) const;
    const Level* levelAbove(ObjectId levelId) const;

    double wallHeight(const Wall& w) const;
    // The level an object is on (an opening's is its wall's), or kNoId.
    ObjectId levelOf(ObjectId id) const;
    bool contains(ObjectId id) const;
    std::vector<const Opening*> openingsIn(ObjectId wallId) const;

    // Removes any object by id, along with objects that depend on it
    // (openings in a removed wall, everything on a removed level).
    bool remove(ObjectId id);
};

const char* toString(RoomType t);
// IRC Table R301.5 uniform live load for a room type, psf.
double defaultLiveLoadPsf(RoomType t);

} // namespace oa
