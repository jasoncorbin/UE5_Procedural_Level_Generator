// The rectangular room grammar: a tile size, four sides, and where a doorway sits on an edge.
//
// INSIDE the determinism boundary: int64 only, no reflection, no floats, no engine types
// beyond TArray.
//
// A DELIBERATE SUBSET of Level_Creator_1's RectDungeon/RectRoomTypes.h. That file carries the
// whole shipping room grammar -- wall tiers, piece roles, corner indices, placement structs,
// the solver's spec and plan types. None of it is reachable from the room-authoring tool, and
// the generator that would read it is not being ported: this project's Blueprint dungeon
// generator is the level generator of record. What is here is what RoomAuthoring actually
// names, which measured out at one enum and a handful of constants.
//
// The four Exit* helpers below live in Level_Creator_1's RectDungeon/RectRoomPlan.h, NOT in
// its RectRoomTypes.h. They are gathered here because RectRoomPlan is the pure planner for the
// dormant solver and does not come across; carrying its header just to reach four one-line
// functions would drag the solver's spec and plan types with it. Their bodies are unchanged --
// see the note on ExitSpanLoUU about why there must be exactly one definition of these.

#pragma once

#include "CoreMinimal.h"

namespace RectGen
{
	/** Every floor tile and wall segment is this long. The whole subsystem assumes it. */
	inline constexpr int64 TileUU = 400;

	enum class ERectSide : uint8 { North = 0, East = 1, South = 2, West = 3 };
	inline constexpr int64 NumSides = 4;

	/** Corner slots, in the order a room's four corner fields declare them. */
	enum class ERectCornerIndex : uint8 { NW = 0, NE = 1, SE = 2, SW = 3 };
	inline constexpr int64 NumCorners = 4;

	/**
	 * THE CONNECTION POINT: the midpoint of an edge of TileCount tiles, in the room's own
	 * local frame. Two rooms are joined by making these coincide -- NOT by making their
	 * doorway spans coincide.
	 *
	 * That distinction is the whole design. Doorway width depends on the edge's parity, so a
	 * parent and a child can legitimately have different widths; aligning endpoints would
	 * refuse every odd-to-even pairing, silently, by simply placing fewer rooms.
	 */
	FORCEINLINE int64 ExitCentreUU(int64 TileCount) { return TileCount * TileUU / 2; }

	/**
	 * Half the doorway's width, chosen so the opening always lands on WHOLE TILES.
	 *
	 * An odd edge's midpoint sits at a half-tile, so a one-tile door centres on it exactly. An
	 * even edge's midpoint sits on a tile boundary, so a two-tile door does. Matching the
	 * door's parity to the edge's is what removes the half-tile remainders entirely -- pick
	 * either width for both parities and one of them lands off the grid.
	 */
	FORCEINLINE int64 ExitHalfWidthUU(int64 TileCount)
	{
		return (TileCount % 2 != 0) ? (TileUU / 2) : TileUU;
	}

	/**
	 * The doorway's span along its own edge, in the room's local frame.
	 *
	 * Defined from the edge's MIDPOINT, not from a tile index, and widened to match the edge's
	 * parity so it always covers whole tiles: one tile on an odd edge, two on an even one.
	 *
	 * This replaced ExitTileIndex(), which returned (TileCount-1)/2 and therefore put a 1-tile
	 * door half a tile off centre on every even edge. Confirmed by the subsystem's first render
	 * on 2026-08-17: a 5x4 room's North/South doors were centred and its East/West doors were
	 * 200 uu low.
	 *
	 * MUST be the only definition of the door's position. The authoring tool's outer gateway
	 * has to land on the SAME geometry a room's exit slot is cut at, or a door fills a wall. A
	 * second, "equivalent" formula anywhere else is exactly the defect the span tests cannot
	 * see -- which is why RoomAuthor::ExitTileSpan divides these by TileUU rather than
	 * restating the midpoint rule in tiles.
	 */
	FORCEINLINE int64 ExitSpanLoUU(int64 TileCount)
	{
		return ExitCentreUU(TileCount) - ExitHalfWidthUU(TileCount);
	}
	FORCEINLINE int64 ExitSpanHiUU(int64 TileCount)
	{
		return ExitCentreUU(TileCount) + ExitHalfWidthUU(TileCount);
	}

} // namespace RectGen
