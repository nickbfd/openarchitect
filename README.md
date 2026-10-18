# OpenArchitect

OpenArchitect is a free, open source residential design application in the spirit of Chief Architect: you draw walls, doors, windows, floors and roofs as building objects in a 2D plan, and a live 3D model stays in sync. It also does what most design tools leave to a separate program: it takes the vertical (gravity) loads from roofs and floors down through the walls and beams to the foundation, shows the PLF on every wall and beam, and recommends beams and headers that work.

Windows and Linux are first-class platforms. macOS is built and tested in CI on every change.

> Status: early development (phase 0 to 1 of the roadmap). The load engine is tested against hand calculations; the editors are a first working version.

## What works today

- **2D plan editor**: walls (with chaining, angle and end snapping), doors, windows, beams, floor framing, rooms and gable or hip roofs, on multiple levels, with the level below shown for reference. Select, move, stretch, delete, and undo or redo anything.
- **3D view**: the same model in 3D with openings cut into walls, floors and roofs. Orbit, pan, zoom.
- **Properties panel**: edit any object, the levels, and the site snow inputs. Lengths accept feet and inches as you would type them (`12' 6 1/2"`).
- **Vertical load takedown**: roof dead, roof live and snow loads; ceiling and attic loads; floor dead and live loads. Results are PLF per load case on each wall and beam, point loads where beams and ridge beams bear, and warnings wherever a load has no support beneath it.
- **Beam and header sizing**: every member in the catalog (sawn lumber in 1 to 4 plies, 4x and 6x, glulam, generic LVL) checked for bending, shear, deflection and bearing, shallowest passing member first, with a copyable calc report.
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
| S | Select (drag to move, drag a wall end to stretch) |
| W | Wall (click, click, keep clicking; right-click or Esc to stop; Ctrl disables snapping) |
| D / N | Door / Window (click on a wall) |
| B | Beam |
| J | Floor framing (drag a rectangle; joists run the short way) |
| R | Roof (drag a rectangle; or Tools, Roof over this level's walls) |
| M | Room |
| L | Show loads on the plan |
| F | Fit to window |
| F2 / F3 | Plan / 3D |

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
