# Structural method

OpenArchitect calculates vertical (gravity) loads for US residential construction. Lateral wind and seismic design are out of scope. Results are preliminary design: they show the load path and a workable member, and they are meant to be reviewed by a licensed engineer wherever a jurisdiction requires it.

## Loads

All loads are stored unfactored, per case: dead (D), floor live (L), roof live (Lr) and snow (S).

| Source | Value | Reference |
| --- | --- | --- |
| Roof dead | Entered on the roof surface (default 15 psf), converted to horizontal projection by dividing by cos(slope) | |
| Roof live | 20 psf on horizontal projection | IRC R301.6 |
| Snow | Balanced roof snow `ps = Cs x 0.7 Ce Ct Is pg`, with the low-slope minimum under 15 degrees | ASCE 7-16 Ch. 7 |
| Ceiling | Default 10 psf dead, attic live 10 psf (no storage) or 20 psf (limited storage) | IRC Table R301.5 |
| Floor | Default 10 psf dead, 40 psf live (30 sleeping rooms, 40 decks, 60 balconies, 50 garages) | IRC Table R301.5 |
| Walls | Self weight per square foot of wall face times wall height (default 10 psf) | |

Not yet included: snow drifts, unbalanced snow, rain-on-snow, roof live load reduction for large tributary areas.

## How loads travel

1. **Roofs.** Gable roofs with a ridge board and ceiling ties put each half of the roof on its eave wall: tributary width = half the span + overhang. With a structural ridge beam, each rafter is a simple span from wall to ridge with a cantilevered overhang: the wall takes `(run + o)^2 / (2 run)` and the ridge beam takes `(run^2 - o^2) / (2 run)` from each side. The ridge beam's end reactions go down as point loads to the walls beneath its ends.
   Hip roofs divide the footprint by its straight skeleton (45 degree hips in plan for equal pitches), so each wall carries a triangular or trapezoidal load whose depth at any point is `min(s, length - s, width / 2)`, plus the overhang strip.
2. **Ceilings and floors** are one-way framing. Supports are the bearing walls and beams that run across the joists. Each support takes half the distance to the next support on each side; the first and last supports also take any cantilever to the edge. Where supports only cover part of the floor, the floor is split into strips and each strip is solved on its own.
3. **Beams** are solved as simple spans. Their end reactions become point loads on the wall or beam they bear on, or on whatever is beneath the post at that end.
4. **Walls** add their self weight and pass everything to the bearing walls and beams directly beneath, level by level, down to the foundation.

Anything with no support beneath it produces a warning that names the member and the unsupported length.

## Member checks

Each member is checked for each ASCE 7 ASD gravity combination: D; D + L; D + Lr; D + S; D + 0.75L + 0.75Lr; D + 0.75L + 0.75S. The NDS load duration factor CD for each combination is that of its shortest load (0.9, 1.0, 1.25, 1.15, 1.25, 1.15).

| Check | Rule |
| --- | --- |
| Bending | `M / S <= Fb'`, with Fb' = Fb x CD x CF x Cr (sawn), x CF (timbers), x CV (glulam), x (12/d)^0.136 capped at 1.0 (LVL). Cr = 1.15 for three or more plies. |
| Shear | `1.5 V / A <= Fv x CD`, with V taken at a distance d from the supports |
| Live deflection | span / 360 for members carrying floor live load, span / 240 for roof-only members |
| Total deflection | span / 240 for floor members, span / 180 for roof-only members |
| Bearing | Required bearing length = reaction / (b x Fc-perp), reported with the number of 1-1/2" studs needed |

Assumptions: dry service (CM = 1), normal temperature (Ct = 1), compression edge braced along its length (CL = 1), simple span, deflection from short-term E without creep. The beam's own weight is added to dead load.

Design values come from the NDS Supplement (2018) for sawn lumber (Tables 4A, 4B, 4D) and glulam (Table 5A, 24F-V4 Douglas Fir). LVL uses generic 2.0E values (Fb 2600, Fv 285, Fc-perp 750 psi, E 2.0 x 10^6 psi); manufacturers publish their own values, which should be confirmed before construction.

## Testing

`tests/test_beam.cpp` and `tests/test_takedown.cpp` check the engine against closed-form beam solutions and hand calculations for gable, hip, structural-ridge, floor and stacked-wall load paths. Add a test with its hand calculation whenever the method changes.
