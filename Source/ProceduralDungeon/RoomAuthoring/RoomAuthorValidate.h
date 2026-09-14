// The rules a multi-chamber room must satisfy before it can be saved into the library.
//
// INSIDE the determinism boundary: int64 only, no reflection, no UObject, no engine
// subsystems. That is not stylistic -- every function here has to be callable from a headless
// -nullrhi automation run, which is what makes these rules a BLOCKING gate under
// .claude/docs/coding-standards.md rather than something a human notices in the viewport.
//
// Every function reports refusal the way URectRoomAsset::ValidatePieces does: false, plus a
// sentence in OutError saying what is wrong. OutError is NEVER written on success -- a caller
// that checks the string instead of the bool must not be handed a stale one.

#pragma once

#include "CoreMinimal.h"
#include "RoomAuthoring/RoomAuthorTypes.h"

namespace RoomAuthor
{
	/**
	 * Does every chamber have positive extents and lie wholly inside the bounding rect?
	 *
	 * Checked before anything else, because the two rules after it are meaningless without it:
	 * a zero-width chamber overlaps nothing and abuts nothing, so it would sail through both.
	 */
	bool ValidateChambersInBounds(const FRoomLayout& Layout, FString& OutError);

	/**
	 * Do any two chambers share a tile?
	 *
	 * Half-open bounds, so chambers that merely ABUT are not overlapping -- that case is the
	 * point of the whole feature and is detected as a connection instead.
	 */
	bool ValidateChambersDoNotOverlap(const FRoomLayout& Layout, FString& OutError);

	/**
	 * Every pair of chambers sharing a wall run of at least one whole tile, with that run's
	 * extent in room tile coordinates.
	 *
	 * Requires a run of >= 1 TILE, not >= 1 unit of overlap, and that is what separates the
	 * three cases the tests pin: chambers face to face share a run and connect; chambers
	 * meeting only at a corner have a run of length zero and do not; chambers set diagonally
	 * with a gap are not adjacent on any axis at all.
	 *
	 * Assumes the layout has already passed ValidateChambersDoNotOverlap. Overlapping chambers
	 * produce runs that describe nothing real, which is why the caller order in
	 * ValidateRoomLayout is fixed rather than incidental.
	 *
	 * Appends in ascending (A, B) index order so the result is stable run to run -- the widget
	 * lists these, and a list that reshuffles itself is a list nobody can toggle reliably.
	 */
	void FindSharedEdges(const FRoomLayout& Layout, TArray<FSharedEdge>& OutEdges);

	/**
	 * The author's decision for a detected edge. Arch when unstated (Decision 7).
	 *
	 * Replaced IsArched(), which could only answer a two-state question. "Not arched" now
	 * covers both a wall that stands and a wall that is gone, and those emit opposite
	 * geometry -- a caller that collapses them back to a bool reintroduces exactly the
	 * ambiguity this returns an enum to avoid.
	 */
	EConnectionMode ConnectionModeFor(const FRoomLayout& Layout, const FSharedEdge& Edge);

	/**
	 * Shrink-wrap the bounding rect onto the chambers, and move the result to the origin.
	 *
	 * The bounding rect is the room's contract with the dungeon solver -- it is what the
	 * packer reserves, and its MIDPOINTS are where exits land. Authoring it as a second,
	 * independently typed pair of numbers meant the two could disagree, and the failure was
	 * silent in the worst way: a bounding rect one tile larger than its chambers puts every
	 * declared exit on an edge no wall reaches, so the room refuses with a message about
	 * exits when the mistake was about size.
	 *
	 * Derived rather than merely validated, because there is no case where a footprint LARGER
	 * than its floor is wanted: the extra strip is dungeon volume nothing can stand in, and
	 * any exit on it is invalid by ValidateExitsReachMidpoints anyway.
	 *
	 * Normalises as well as fits. Fitting only the maxima would leave a dead strip along the
	 * west and south edges whenever the lowest chamber sits off the origin, which re-creates
	 * the same unreachable-exit failure from the other direction.
	 *
	 * Guarantees, on a layout with at least one chamber: every chamber lies inside the
	 * bounding rect, and at least one chamber is flush against each of its four edges.
	 * A layout with no chambers is left untouched -- there is nothing to fit to, and
	 * ValidateChambersInBounds owns that refusal.
	 */
	void FitBoundsToChambers(FRoomLayout& Layout);

	/**
	 * Is every chamber's attachment structurally sound?
	 *
	 * Two rules, and the second is the load-bearing one: a parent index must be in range, and
	 * it must be STRICTLY LESS than the chamber that names it. Ascending parents make a cycle
	 * unrepresentable, which is what lets DeriveChamberPositions be a single forward pass
	 * rather than a traversal that has to detect one. Checked rather than assumed because a
	 * recipe is an on-disk asset and can be hand-edited.
	 *
	 * Chamber 0 is always a root -- it has no lower index to attach to.
	 */
	bool ValidateAttachments(const FRoomLayout& Layout, FString& OutError);

	/**
	 * Position every attached chamber from its parent, then shrink-wrap the room.
	 *
	 * The rule, integer arithmetic throughout, division truncating toward zero:
	 *
	 *     North:  Cx = Px + (Pw - Cw)/2      Cy = Py + Pl
	 *     South:  Cx = Px + (Pw - Cw)/2      Cy = Py - Cl
	 *     East:   Cx = Px + Pw               Cy = Py + (Pl - Cl)/2
	 *     West:   Cx = Px - Cw               Cy = Py + (Pl - Cl)/2
	 *
	 * The child is centred on the parent's side with its edge flush against it, so the two
	 * always share a wall run of at least one tile and FindSharedEdges detects a connection
	 * without the author declaring one.
	 *
	 * SOUTH AND WEST PRODUCE NEGATIVE COORDINATES, deliberately and routinely. Nothing clamps
	 * them: FitBoundsToChambers runs last and normalises the whole room back to the origin.
	 * Which is why chamber 0 is where authoring STARTS and not a coordinate it keeps -- attach
	 * something to its south and the entire room shifts.
	 *
	 * Roots are left exactly as they are. A layout with no attachments is therefore unchanged
	 * except for the fit, which is what keeps recipes written before attachments existed valid.
	 *
	 * Assumes ValidateAttachments has passed; with ascending parents one forward pass places
	 * every parent before any child that names it.
	 */
	void DeriveChamberPositions(FRoomLayout& Layout);

	/**
	 * Is this chamber's side already spoken for?
	 *
	 * TWO ways it can be, and missing the second is what let the panel offer an attachment
	 * that dropped one chamber straight onto another (found 2026-08-31):
	 *
	 *   1. Some other chamber is already attached to this chamber on that side.
	 *   2. It is the side facing this chamber's OWN parent -- the back side. A chamber
	 *      attached to its parent's North sits against that parent on its own South, and that
	 *      South is not free for anything else.
	 *
	 * Out of range indices report taken, so a caller cannot turn a bad index into an offer.
	 */
	bool IsSideTaken(const FRoomLayout& Layout, int64 ChamberIndex, RectGen::ERectSide Side);

	/**
	 * Does every declared exit have a chamber whose wall reaches that side's midpoint?
	 *
	 * THE CONNECTION POINT, per the design spec's section 1 and RectGen::ExitCentreUU: this is
	 * where the dungeon generator joins one room to the next and later fills a door. An exit
	 * declared with no chamber behind it produces a room the solver will happily connect to
	 * and the player cannot walk through.
	 *
	 * One chamber must cover the WHOLE doorway. Two chambers each covering half of it would
	 * put a wall junction in the middle of the opening, which is a hole in the fabric rather
	 * than a doorway.
	 */
	bool ValidateExitsReachMidpoints(const FRoomLayout& Layout, FString& OutError);

	/**
	 * Is a half-open opening span legal on a chamber side of SideTileCount tiles?
	 *
	 * Legal is [1, SideTileCount - 1): tiles 0 and SideTileCount-1 are the corner cells, and a
	 * corner piece occupies the whole cell. Cutting an opening into one does not widen the
	 * doorway -- it deletes a corner and opens the two walls that meet there.
	 *
	 * Takes a SPAN rather than an index because a doorway on an even edge is two tiles wide
	 * (RectGen::ExitHalfWidthUU). A rule stated only for single tiles cannot see the case where
	 * the first tile is legal and the second one is a corner.
	 */
	bool ValidateOpeningSpan(int64 TileLo, int64 TileHi, int64 SideTileCount, FString& OutError);

	/**
	 * ValidateOpeningSpan for a single-tile opening, matching the PCG graph's per-side opening
	 * INDEX parameter (-1 = no opening, see the design spec's section 4).
	 *
	 * -1 is accepted: a side with no opening cannot have an illegal one.
	 */
	bool ValidateOpeningIndex(int64 OpeningIndex, int64 SideTileCount, FString& OutError);

	/**
	 * Resolve the layout down to the openings each chamber must actually cut, exterior and
	 * interior alike, and refuse any that lands in a corner cell.
	 *
	 * Exterior openings come from the bounding rect's declared exits; interior ones from the
	 * shared edges the author has not walled off. An interior opening yields TWO entries, one
	 * per chamber, because each chamber cuts its own side and each has to satisfy the corner
	 * rule in its own frame.
	 *
	 * The corner rule is applied to Exterior and Interior openings and DELIBERATELY NOT to
	 * Merged ones. An Open edge spans its whole shared run, and when that run is a chamber's
	 * entire side the span reaches both of that chamber's corner cells by construction --
	 * removing those corner pieces is what the mode is for, so refusing it would make Open
	 * unusable on exactly the layouts it exists to serve.
	 */
	bool CollectOpenings(const FRoomLayout& Layout, const TArray<FSharedEdge>& SharedEdges,
	                     TArray<FChamberOpening>& OutOpenings, FString& OutError);

	/**
	 * The v1 limit: at most ONE opening per chamber side.
	 *
	 * The PCG graph carries one opening-index parameter per side (design spec section 4), so a
	 * chamber needing two openings on one side cannot be emitted at all. Refusing the layout is
	 * the honest failure; the alternative is a room that generates quietly missing a doorway.
	 *
	 * Lifting this means variable-length opening lists, which is PCG's documented weak spot --
	 * so it waits for a room that actually needs it rather than being designed for in advance.
	 */
	bool ValidateOneOpeningPerChamberSide(const TArray<FChamberOpening>& Openings,
	                                      FString& OutError);

	/**
	 * Refuse a chamber whose openings meet at one of its own corners.
	 *
	 * Measured 2026-08-31 (docs/pcg-open-mode-measurement.md): removing ONE wall seals cleanly,
	 * and it seals for a reason worth stating precisely -- not because the emitter reasons about
	 * corners, but because the PERPENDICULAR wall run terminates at that corner and emits an
	 * edge cap (edgeL / edgeR) at the exact coordinate the deleted corner piece vacated. 29 of
	 * 29 columns blocked at both corners, at both heights, with positive controls.
	 *
	 * That mechanism has a hole in it. When BOTH sides meeting at a corner are open, neither
	 * run terminates there, so no cap is emitted and nothing fills the cell. The kit's
	 * convex corner piece exists but the graph has no path that places it.
	 *
	 * UNMEASURED, not proven broken -- and refused for exactly that reason. A room that
	 * generates with a hole in it is the failure this whole validation layer exists to prevent,
	 * and non-negotiable 5 does not let an argument stand in for a sweep. Measure that case and
	 * this rule can be relaxed to whatever the measurement actually supports.
	 *
	 * Only Merged openings can reach a corner cell at all -- Exterior and Interior ones are held
	 * off the corners by ValidateOpeningSpan -- so in practice this fires only on two adjacent
	 * Open connections.
	 */
	bool ValidateOpenCornersAreCapped(const FRoomLayout& Layout,
	                                  const TArray<FChamberOpening>& Openings, FString& OutError);

	/**
	 * Every rule above, in the one order they are valid in.
	 *
	 * The order is load-bearing and not merely tidy: overlap detection is meaningless on
	 * out-of-bounds chambers, shared-edge detection is meaningless on overlapping ones, and the
	 * opening rules are meaningless without the edges. Stops at the first failure so OutError
	 * names the root problem rather than the first of its consequences.
	 *
	 * OutSharedEdges and OutOpenings are the working results -- the widget needs both to draw
	 * its Connections list, and recomputing them in the caller would be a second copy of rules
	 * that have to agree with these.
	 */
	bool ValidateRoomLayout(const FRoomLayout& Layout,
	                        TArray<FSharedEdge>& OutSharedEdges,
	                        TArray<FChamberOpening>& OutOpenings,
	                        FString& OutError);

} // namespace RoomAuthor
