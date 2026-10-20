# OpenArchitect

OpenArchitect is a free, open source residential design application in the spirit of Chief Architect: you draw walls, doors, windows, floors and roofs as building objects in a 2D plan, and a live 3D model stays in sync. It also does what most design tools leave to a separate program: it takes the vertical (gravity) loads from roofs and floors down through the walls and beams to the foundation, shows the PLF on every wall and beam, and recommends beams and headers that work.

Windows and Linux are first-class platforms. macOS is built and tested in CI on every change.

> Status: early development (phase 0 to 1 of the roadmap). The load engine is tested against hand calculations; the editors are a first working version.

## What works today

- **2D plan editor**: walls (with chaining, angle and end snapping), doors, windows, beams, posts, floor framing, gable or hip roofs, and single roof planes (shed and porch roofs, or a roof built plane by plane), on multiple levels, with the level below shown for reference. Select, move, stretch, delete, and undo or redo anything. Where objects are stacked (a beam over a window in a wall), click the same spot again, press Tab, or right-click to pick the one you want.
- **Roofs built from the walls, the Chief Architect way**: Roofs > **Build roof** makes the whole roof from the exterior walls. Every wall gets a sloped plane unless its properties say **Full gable wall** or **High side of a shed**, and each wall can have its own pitch and overhang, so one setup gives hip, gable, shed, saltbox, half-hip, and L- or U-shaped roofs with their hips, valleys and ridges. Shift-select walls and use **Change to gable walls** or **Change to hip walls**. A wall can also have an **upper pitch** that starts at a height above the plate, which makes a gambrel (steep, then shallow), a gull wing (shallow, then steep), a mansard (an upper pitch on every wall), or over a gable wall a half hip. **Dutch gable** walls hip up to a height and then stand up as a gable. With **Rebuild automatically** on, the roof follows the walls as you edit; reshaping a roof plane by hand turns that off so your change stays. The sample house's roof is built this way.
- **Roof framing in the roof properties**: each roof and roof plane has its rafter size (2x4 to 2x12 in DF-L, SYP, SPF or Hem-Fir) and spacing, or truss spacing, its **heel height** (automatic from the framing, or typed in for a raised energy heel), and its roof sheathing thickness (3/8" to 3/4") and material (OSB or plywood), set apart from the rafters. Heel height and sheathing set how high the roof sits on the walls. The plan labels each plane with its rafter size and spacing, the framing view in 3D shows the rafters, and the properties check the longest rafter in a plane against its span and loads, with a button to pick the smallest size that works. Build roof sets the defaults for new planes; an automatic rebuild keeps what you set on each plane.
- **Roof planes you can reshape**: drag any corner, or the handle in the middle of any edge, to change the width, depth, eave and rake overhangs. **Join roof planes** trims an edge to the hip, valley or ridge line where it meets another plane, like Chief Architect's tool of the same name.
- **Rooms from walls**: rooms fill the areas walls enclose and update as you edit walls, keeping their names. Room dividers are invisible walls that split an open area into rooms.
- **Grouped toolbar**: the tools sit in drop-down menus for Walls (exterior and interior wall types, room divider), Doors and Windows (common sizes like 3068 or 3040), Roofs, Framing, Dimensions and Terrain.
- **Typed lengths**: while drawing a wall, room divider or beam, start typing a length (`12'6`, `150`, `14' 3 1/2"`) and press Enter. The new piece runs toward the cursor, snapped to 15 degrees.
- **Door and window labels**: each opening is labeled with its size in the usual shorthand (3068 is 3'-0" by 6'-8"). Turn them off under View.
- **Dimensions**: draw your own with the Dimension tool, or use **Dimension exterior walls** for rows of opening, corner and overall dimensions around the outside. Walls measure to the outside edge of their framing or to its center (Dimensions menu). Select a dimension to get its nodes: drag a square to move that end to another point (it snaps to framing corners, openings, posts and beam ends, and joined dimensions follow), or drag the diamond or the line to move the whole string in or out.
- **Edit by dimension, the Chief Architect way**: select a wall and temporary dimensions (green) show the distance to the nearest parallel wall on each side; select a door or window and they show its distance to each end of its wall. Click any of those numbers, the selected wall's length, or a regular dimension, type a new value and press Enter, and the selection moves to suit. Walls joined at the ends stretch to follow when you move a wall or drag its end, a joined wall moves square to itself, and doors and windows on stretched walls keep their place. Choose which end moves when a wall's length changes in Properties. Exterior dimensions update as you edit (turn this off in the Dimensions menu).
- **Build new floor** (Tools): fills the empty level above, or adds one, with copies of the exterior walls below.
- **Box selection**: hold Shift and drag a box to select everything entirely inside it. With a tool picked first, only that kind is selected, so pick Windows and Shift-drag over the whole plan to select every window. Shift-click adds or removes one object. Edit what they share in Properties, drag one to move them all, or press Delete.
- **Terrain**: place elevation points on the ground level, and contours are drawn between them at the interval you set (every fifth one heavier and labeled). The ground shows in 3D.
- **Room floor systems**: each room can keep the level's floor framing, have its own joists (a flush shower framed deeper or dropped), or be a concrete slab (a garage or patio), with its own loads and a drop below the level's floor. Slabs show in 3D.
- **Wall types**: named build-ups (6" and 4" exterior, 4" and 6" interior, room divider) with stud size, stud spacing, insulation and finish layers. Walls take their thickness and weight from their type, and you can make your own types.
- **3D view**: the same model in 3D with windows and doors set into walls, floors, roofs and posts. Turn on **Show wall framing in 3D** to see studs at their spacing, plates, king and jack studs, headers, sills, cripples and insulation, and the rafters or truss top chords of every roof plane at their spacing. Click any object to select it and edit its properties. Orbit, pan, zoom.
- **Properties panel**: edit any object, the levels, and the site snow inputs. Lengths accept feet and inches as you would type them (`12' 6 1/2"`).
- **Vertical load takedown**: roof dead, roof live and snow loads (calculated from the site, or entered per roof); rafter or truss framing; ceiling and attic loads; floor dead and live loads. Wall properties show roof, ceiling, floor and wall-above loads separately, plus the wall's own weight and the total at its base. Results are PLF per load case on each wall and beam, point loads where beams and ridge beams bear, loads on posts, and warnings wherever a load has no support beneath it.
- **Beam, header and post sizing**: every member in the catalog (sawn lumber in 1 to 4 plies, 4x and 6x, glulam, generic LVL) checked for bending, shear, deflection and bearing, shallowest passing member first, in a table you can sort by any column, with a copyable calc report. Pick a member to assign it to the beam or to the header over a door or window; headers are limited to members that fit the wall's studs. Posts get an NDS column check and the smallest post that works.
- **Plan files** (`.oaplan`): versioned JSON, safe saves, and a sample house under Help.

See [docs/structural-method.md](docs/structural-method.md) for exactly how loads and members are calculated and what is assumed.

## Building

You need CMake 3.21+, a C++20 compiler (MSVC 2022, GCC 12+ or Clang 15+), Ninja, and Qt 6.4 or newer. The JSON and test libraries are found on the system or downloaded automatically.

```sh
cmake --preset default
cmake --build --preset default
ctest --preset default
./build/default/src/app/openarchitect examples/sample-house.oaplan
```

On Windows, run these from a "Developer PowerShell for VS 2022" with Qt's `bin` folder on `PATH` (or pass `-DCMAKE_PREFIX_PATH=C:/Qt/6.7.3/msvc2019_64`). On Ubuntu 24.04: `sudo apt install qt6-base-dev libgl1-mesa-dev ninja-build cmake g++`.

To build only the model and structural libraries and their tests (no Qt needed): `cmake --preset engine-only`.

## Using it

| Key | Tool |
| --- | --- |
| S or Space | Select (drag to move, drag a wall end to stretch; click again, Tab or right-click for stacked objects; Shift-drag a box to select several; arrow keys nudge 1", Shift+arrow 1'; double-click or Ctrl+E opens Properties) |
| W | Wall (click, click, keep clicking; type a length and press Enter, or press Tab for length and angle; right-click or Esc to stop; Ctrl disables snapping) |
| I | Interior wall |
| D / N | Door / Window (click on a wall) |
| B | Beam |
| J | Floor framing (drag a rectangle; joists run the short way) |
| R | Roof (drag a rectangle; or Roofs, Build roof to make it from the walls) |
| P | Roof plane (click both ends of the eave bearing line, then click the high side) |
| K | Join roof planes (click an edge, then the plane it should meet) |
| O | Post (click; snaps to beam and wall ends) |
| M | Room divider (an invisible wall that separates rooms) |
| X | Dimension (click two points, then where the line goes) |
| E | Elevation point (click on the ground level; set its elevation in Properties) |
| G | Show wall framing in 3D |
| L | Show loads on the plan |
| F | Fit to window |
| F2 / F3 | Plan / 3D |

Select a room to set its floor system in Properties.

Select a beam and press **Size beam…** in Properties, or a door or window in a bearing wall and press **Size header…**.

## Layout

```
src/core        building model, units, geometry, undo, plan files (no Qt)
src/structural  loads, snow, beam analysis, member catalog, sizing, takedown (no Qt)
src/app         Qt desktop application
tests           unit tests (Catch2)
docs            design and engineering notes
```

## License

GNU General Public License v3.0 or later. See [LICENSE](LICENSE).
