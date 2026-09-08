// Pure types for the room-authoring tool: a bounding rect, the chambers inside it, and the
// openings cut between them.
//
// INSIDE the determinism boundary: int64 only, no reflection, no floats, no engine types
// beyond TArray. The reflection layer is RoomAuthoring/RoomRecipeAsset.h, whose int32
// UPROPERTYs widen into these on the way in and never the reverse.
//
// Sides, tile size and doorway parity are RectGen's, deliberately. A second "equivalent"
// definition of where a doorway sits is exactly the defect RectRoomTypes.h warns about: the
// authoring tool's outer gateway has to land on the SAME geometry a room's exit slot is cut
// at, or the door that fills it lands on a wall.

#pragma once

#include "CoreMinimal.h"
#include "RectDungeon/RectRoomTypes.h"

namespace RoomAuthor
{
	/**
	 * A chamber: an axis-aligned rect on the room's own 400 uu tile grid.
	 *
	 * Held as origin plus extent rather than as two corners because that is what the widget
	 * edits and what the PCG volume needs. The MinX()/MaxX() accessors below are the only
	 * place the half-open convention is spelled out.
	 */
	struct FChamberRect
	{
		int64 GridX = 0;
		int64 GridY = 0;
		int64 Width = 1;    // in tiles, along X
		int64 Length = 1;   // in tiles, along Y

		/**
		 * The chamber this one hangs off, or INDEX_NONE for the root.
		 *
		 * When set, GridX/GridY are DERIVED, not authored -- DeriveChamberPositions computes
		 * them from the parent's rect and this side, and overwrites whatever was there. When
		 * INDEX_NONE they are authored and nothing touches them, which is also what makes a
		 * recipe saved before attachments existed keep working: every chamber loads as a root
		 * and holds the coordinates it was saved with.
		 *
		 * MUST be less than this chamber's own index. That single rule is what makes a cycle
		 * unrepresentable rather than merely unlikely, and it is why deriving positions is a
		 * plain ascending walk instead of a graph traversal with a visited set.
		 */
		int64 ParentIndex = INDEX_NONE;

		/** Which side OF THE PARENT this chamber sits against. Meaningless on a root. */
		RectGen::ERectSide AttachSide = RectGen::ERectSide::North;

		/** True when this chamber's position is authored rather than derived. */
		bool IsRoot() const { return ParentIndex == INDEX_NONE; }

		/**
		 * Bounds are HALF-OPEN: [Min, Max). A chamber at GridX 0 of Width 2 occupies tiles 0
		 * and 1 and its MaxX() is 2, so the chamber starting at tile 2 abuts it exactly.
		 *
		 * Getting this closed instead would make every abutting pair read as a one-tile
		 * overlap, and the overlap check would refuse every multi-chamber room there is.
		 */
		int64 MinX() const { return GridX; }
		int64 MaxX() const { return GridX + Width; }
		int64 MinY() const { return GridY; }
		int64 MaxY() const { return GridY + Length; }

		/** How many tiles this chamber presents to the given side. */
		int64 TileCountOnSide(RectGen::ERectSide Side) const
		{
			return (Side == RectGen::ERectSide::North || Side == RectGen::ERectSide::South)
				? Width : Length;
		}

		/** The tile index this chamber's side-local axis starts at, in ROOM tile coordinates. */
		int64 SideOriginTile(RectGen::ERectSide Side) const
		{
			return (Side == RectGen::ERectSide::North || Side == RectGen::ERectSide::South)
				? GridX : GridY;
		}
	};

	/** Whether an opening leads outside the room or between two of its chambers. */
	enum class EOpeningKind : uint8
	{
		Exterior,   // on the bounding rect at an exit midpoint. Emits NOTHING -- see spec 1a
		Interior,   // a shared edge between two chambers. Emits the arch
		Merged,     // a shared edge removed ENTIRELY. Emits nothing; the two chambers read as
		            // one space. Spans the whole shared run, so unlike the two above it may
		            // legally reach a corner cell -- see ValidateOpeningSpan's caller.
	};

	/**
	 * What the author wants done with one shared edge.
	 *
	 * Three states rather than the original bool, because "remove the wall" and "cut a doorway
	 * through the wall" are different geometry, not different amounts of the same geometry. An
	 * arch is a one-tile hole in a wall that still stands; Open deletes the wall run outright
	 * so two abutting rectangles become one L-shaped space. Nothing downstream can infer which
	 * was meant from a span alone -- a full-run arch and an Open edge produce the same tiles
	 * and different pieces.
	 *
	 * Arch is the default, preserving Decision 7: a detected shared edge auto-arches, and the
	 * author's toggle is what turns it into something else.
	 */
	enum class EConnectionMode : uint8
	{
		Walled = 0,   // solid wall. Cuts nothing.
		Arch   = 1,   // one tile, centred on the shared run, framed by the gateway piece.
		Open   = 2,   // the WHOLE shared run, unframed. The wall is gone, corners included.
	};

	/**
	 * One opening, resolved down to the tiles it actually occupies.
	 *
	 * Carries a SPAN rather than a single index because the shipping planner's doorway is
	 * sized to its edge's parity -- one tile on an odd edge, two on an even one (see
	 * RectGen::ExitHalfWidthUU). An "opening index" that could only ever name one tile would
	 * be unable to describe the doorway on every even edge in the library.
	 *
	 * TileLo/TileHi are CHAMBER-LOCAL, half-open, along the chamber's own side axis. Chamber-
	 * local rather than room-local because that is the frame the 1..N-2 corner rule is stated
	 * in, and the frame the PCG graph's per-side opening parameter is read in.
	 */
	struct FChamberOpening
	{
		int64 ChamberIndex = 0;
		RectGen::ERectSide Side = RectGen::ERectSide::South;
		int64 TileLo = 0;
		int64 TileHi = 0;
		EOpeningKind Kind = EOpeningKind::Exterior;

		/** The chamber on the far side, or INDEX_NONE when this opening is exterior. */
		int64 OtherChamber = INDEX_NONE;
	};

	/**
	 * A detected shared edge between two chambers, before any arch/walled decision is made.
	 *
	 * Detection is a fact about the geometry; whether the shared edge becomes an archway is an
	 * authored choice held on the recipe. Keeping them apart is what lets the widget re-detect
	 * connections after a chamber moves without discarding the toggles that still apply.
	 */
	struct FSharedEdge
	{
		int64 ChamberA = 0;
		int64 ChamberB = 0;

		/** Which side OF A the shared run lies on. B's side is always the opposite one. */
		RectGen::ERectSide SideOfA = RectGen::ERectSide::South;

		/** The shared run in ROOM tile coordinates, half-open, along the edge's own axis. */
		int64 SpanLo = 0;
		int64 SpanHi = 0;

		int64 SpanTiles() const { return SpanHi - SpanLo; }
	};

	/**
	 * The author's decision about one shared edge, keyed by the pair it joins.
	 *
	 * Keyed by PAIR rather than by an index into the detected-edge list, because that list is
	 * rebuilt from scratch every time a chamber moves. An index would silently re-point at a
	 * different edge the moment a chamber was inserted; the pair survives the re-detection.
	 *
	 * Mode defaults to Arch -- Decision 7: shared edges auto-arch, and the toggle is what turns
	 * one into solid wall or into a removed wall. A pair with no entry here is therefore
	 * arched, which is why the widget can leave the list empty until the author disagrees with
	 * something.
	 */
	struct FAuthoredConnection
	{
		int64 ChamberA = 0;
		int64 ChamberB = 0;
		EConnectionMode Mode = EConnectionMode::Arch;
	};

	/**
	 * Everything the validator needs, converted out of reflection into pure ints.
	 *
	 * The style picks -- wall class, corner classes, ceiling mesh, prop density -- are
	 * deliberately absent. Not one validation rule reads them, and a pure library that carried
	 * class paths would need UObject and stop being headless-testable, which is the whole
	 * reason it is a separate layer.
	 */
	struct FRoomLayout
	{
		int64 BoundingWidth = 1;
		int64 BoundingLength = 1;

		/** Indexed by RectGen::ERectSide. */
		bool bExit[RectGen::NumSides] = { false, false, false, false };

		TArray<FChamberRect> Chambers;

		/** Only the pairs the author has an OPINION about. Absent means arched. */
		TArray<FAuthoredConnection> Connections;
	};

	/** The side facing the other way. North<->South, East<->West. */
	FORCEINLINE RectGen::ERectSide OppositeSide(RectGen::ERectSide Side)
	{
		switch (Side)
		{
		case RectGen::ERectSide::North: return RectGen::ERectSide::South;
		case RectGen::ERectSide::East:  return RectGen::ERectSide::West;
		case RectGen::ERectSide::South: return RectGen::ERectSide::North;
		case RectGen::ERectSide::West:  return RectGen::ERectSide::East;
		}
		return RectGen::ERectSide::South;
	}

	/** For error messages. Not for serialisation -- nothing round-trips through these. */
	FORCEINLINE const TCHAR* SideName(RectGen::ERectSide Side)
	{
		switch (Side)
		{
		case RectGen::ERectSide::North: return TEXT("North");
		case RectGen::ERectSide::East:  return TEXT("East");
		case RectGen::ERectSide::South: return TEXT("South");
		case RectGen::ERectSide::West:  return TEXT("West");
		}
		return TEXT("?");
	}

	/**
	 * The doorway's tile span on an edge of TileCount tiles, half-open, in that edge's own
	 * frame -- the shipping planner's exit slot, expressed in tiles instead of uu.
	 *
	 * Derived from RectGen::ExitSpanLoUU/HiUU rather than restated, so the authoring tool and
	 * the planner cannot drift apart. Both are exact multiples of TileUU by construction: an
	 * odd count centres a one-tile door on a half-tile midpoint, an even count centres a
	 * two-tile door on a tile boundary, and neither leaves a remainder.
	 */
	FORCEINLINE void ExitTileSpan(int64 TileCount, int64& OutLo, int64& OutHi)
	{
		OutLo = RectGen::ExitSpanLoUU(TileCount) / RectGen::TileUU;
		OutHi = RectGen::ExitSpanHiUU(TileCount) / RectGen::TileUU;
	}

	/**
	 * The doorway's tile span inside an arbitrary shared run [RunLo, RunHi), in the same room
	 * tile coordinates the run is given in.
	 *
	 * An interior connection sits at the midpoint of the run the two chambers actually share,
	 * not at the midpoint of either chamber -- two chambers of different lengths overlap
	 * off-centre, and centring on either one puts the arch partly inside a wall.
	 */
	FORCEINLINE void OpeningTileSpanWithin(int64 RunLo, int64 RunHi, int64& OutLo, int64& OutHi)
	{
		int64 LocalLo = 0, LocalHi = 0;
		ExitTileSpan(RunHi - RunLo, LocalLo, LocalHi);
		OutLo = RunLo + LocalLo;
		OutHi = RunLo + LocalHi;
	}

} // namespace RoomAuthor
