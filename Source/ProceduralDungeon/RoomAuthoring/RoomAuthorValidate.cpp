#include "RoomAuthoring/RoomAuthorValidate.h"

namespace RoomAuthor
{
	namespace
	{
		/** The overlap of two half-open ranges, as a half-open range. Empty when Hi <= Lo. */
		FORCEINLINE void RangeOverlap(int64 ALo, int64 AHi, int64 BLo, int64 BHi,
		                              int64& OutLo, int64& OutHi)
		{
			OutLo = FMath::Max(ALo, BLo);
			OutHi = FMath::Min(AHi, BHi);
		}

		/**
		 * Does this chamber's wall reach the given doorway on the given side of the ROOM?
		 *
		 * Two conditions, and both matter. The chamber must sit ON that face of the bounding
		 * rect -- a chamber one tile in from the North edge has a wall, but it is an interior
		 * wall with sealed void behind it. And it must cover the whole doorway span.
		 */
		bool ChamberCoversRoomExit(const FChamberRect& C, const FRoomLayout& Layout,
		                           RectGen::ERectSide Side, int64 DoorLo, int64 DoorHi)
		{
			switch (Side)
			{
			case RectGen::ERectSide::North:
				return C.MaxY() == Layout.BoundingLength && C.MinX() <= DoorLo && C.MaxX() >= DoorHi;
			case RectGen::ERectSide::South:
				return C.MinY() == 0 && C.MinX() <= DoorLo && C.MaxX() >= DoorHi;
			case RectGen::ERectSide::East:
				return C.MaxX() == Layout.BoundingWidth && C.MinY() <= DoorLo && C.MaxY() >= DoorHi;
			case RectGen::ERectSide::West:
				return C.MinX() == 0 && C.MinY() <= DoorLo && C.MaxY() >= DoorHi;
			}
			return false;
		}

		/** The doorway span on one side of the BOUNDING rect, in room tiles. */
		void RoomExitTileSpan(const FRoomLayout& Layout, RectGen::ERectSide Side,
		                      int64& OutLo, int64& OutHi)
		{
			const bool bAlongX = (Side == RectGen::ERectSide::North
			                   || Side == RectGen::ERectSide::South);
			ExitTileSpan(bAlongX ? Layout.BoundingWidth : Layout.BoundingLength, OutLo, OutHi);
		}

		/** Which chamber covers this exit, or INDEX_NONE. */
		int64 FindChamberForExit(const FRoomLayout& Layout, RectGen::ERectSide Side)
		{
			int64 DoorLo = 0, DoorHi = 0;
			RoomExitTileSpan(Layout, Side, DoorLo, DoorHi);

			for (int32 I = 0; I < Layout.Chambers.Num(); ++I)
			{
				if (ChamberCoversRoomExit(Layout.Chambers[I], Layout, Side, DoorLo, DoorHi))
				{
					return static_cast<int64>(I);
				}
			}
			return INDEX_NONE;
		}
	}

	bool ValidateChambersInBounds(const FRoomLayout& Layout, FString& OutError)
	{
		if (Layout.BoundingWidth < 1 || Layout.BoundingLength < 1)
		{
			OutError = FString::Printf(
				TEXT("The bounding rect is %lld x %lld tiles. Both must be at least 1."),
				Layout.BoundingWidth, Layout.BoundingLength);
			return false;
		}

		if (Layout.Chambers.Num() == 0)
		{
			OutError = TEXT("The room has no chambers. A room needs at least one.");
			return false;
		}

		for (int32 I = 0; I < Layout.Chambers.Num(); ++I)
		{
			const FChamberRect& C = Layout.Chambers[I];

			if (C.Width < 1 || C.Length < 1)
			{
				OutError = FString::Printf(
					TEXT("Chamber %d is %lld x %lld tiles. Both must be at least 1."),
					I, C.Width, C.Length);
				return false;
			}

			if (C.MinX() < 0 || C.MinY() < 0
			 || C.MaxX() > Layout.BoundingWidth || C.MaxY() > Layout.BoundingLength)
			{
				OutError = FString::Printf(
					TEXT("Chamber %d spans tiles X %lld..%lld, Y %lld..%lld, which leaves the ")
					TEXT("%lld x %lld bounding rect. Every chamber must lie inside it."),
					I, C.MinX(), C.MaxX(), C.MinY(), C.MaxY(),
					Layout.BoundingWidth, Layout.BoundingLength);
				return false;
			}
		}

		return true;
	}

	bool ValidateChambersDoNotOverlap(const FRoomLayout& Layout, FString& OutError)
	{
		for (int32 I = 0; I < Layout.Chambers.Num(); ++I)
		{
			for (int32 J = I + 1; J < Layout.Chambers.Num(); ++J)
			{
				const FChamberRect& A = Layout.Chambers[I];
				const FChamberRect& B = Layout.Chambers[J];

				int64 XLo = 0, XHi = 0, YLo = 0, YHi = 0;
				RangeOverlap(A.MinX(), A.MaxX(), B.MinX(), B.MaxX(), XLo, XHi);
				RangeOverlap(A.MinY(), A.MaxY(), B.MinY(), B.MaxY(), YLo, YHi);

				if (XHi > XLo && YHi > YLo)
				{
					OutError = FString::Printf(
						TEXT("Chambers %d and %d overlap over tiles X %lld..%lld, Y %lld..%lld. ")
						TEXT("Chambers may abut, but they may not share a tile."),
						I, J, XLo, XHi, YLo, YHi);
					return false;
				}
			}
		}
		return true;
	}

	void FindSharedEdges(const FRoomLayout& Layout, TArray<FSharedEdge>& OutEdges)
	{
		OutEdges.Reset();

		for (int32 I = 0; I < Layout.Chambers.Num(); ++I)
		{
			for (int32 J = I + 1; J < Layout.Chambers.Num(); ++J)
			{
				const FChamberRect& A = Layout.Chambers[I];
				const FChamberRect& B = Layout.Chambers[J];

				// The four ways two rects can be face to face. Only one can ever produce a run
				// longer than zero: a pair meeting on two of these at once meets at a corner,
				// and a corner meeting has a run of exactly zero tiles on both. Written as four
				// independent tests rather than an if/else chain so that fact is checked rather
				// than assumed.
				struct FCandidate
				{
					bool bTouching;
					RectGen::ERectSide SideOfA;
					bool bAlongX;
				};
				const FCandidate Candidates[4] = {
					{ A.MaxX() == B.MinX(), RectGen::ERectSide::East,  false },
					{ A.MinX() == B.MaxX(), RectGen::ERectSide::West,  false },
					{ A.MaxY() == B.MinY(), RectGen::ERectSide::North, true  },
					{ A.MinY() == B.MaxY(), RectGen::ERectSide::South, true  },
				};

				for (const FCandidate& Cand : Candidates)
				{
					if (!Cand.bTouching) { continue; }

					int64 RunLo = 0, RunHi = 0;
					if (Cand.bAlongX)
					{
						RangeOverlap(A.MinX(), A.MaxX(), B.MinX(), B.MaxX(), RunLo, RunHi);
					}
					else
					{
						RangeOverlap(A.MinY(), A.MaxY(), B.MinY(), B.MaxY(), RunLo, RunHi);
					}

					// A run of zero tiles is two chambers meeting at a corner, not sharing a
					// wall. There is nothing to cut an arch into.
					if (RunHi - RunLo < 1) { continue; }

					FSharedEdge Edge;
					Edge.ChamberA = static_cast<int64>(I);
					Edge.ChamberB = static_cast<int64>(J);
					Edge.SideOfA = Cand.SideOfA;
					Edge.SpanLo = RunLo;
					Edge.SpanHi = RunHi;
					OutEdges.Add(Edge);
				}
			}
		}
	}

	EConnectionMode ConnectionModeFor(const FRoomLayout& Layout, const FSharedEdge& Edge)
	{
		for (const FAuthoredConnection& Conn : Layout.Connections)
		{
			const bool bSamePair =
				   (Conn.ChamberA == Edge.ChamberA && Conn.ChamberB == Edge.ChamberB)
				|| (Conn.ChamberA == Edge.ChamberB && Conn.ChamberB == Edge.ChamberA);
			if (bSamePair) { return Conn.Mode; }
		}
		// Decision 7: shared edges auto-arch. Silence means arch.
		return EConnectionMode::Arch;
	}

	bool ValidateAttachments(const FRoomLayout& Layout, FString& OutError)
	{
		for (int32 I = 0; I < Layout.Chambers.Num(); ++I)
		{
			const FChamberRect& C = Layout.Chambers[I];
			if (C.IsRoot()) { continue; }

			if (C.ParentIndex < 0 || C.ParentIndex >= Layout.Chambers.Num())
			{
				OutError = FString::Printf(
					TEXT("Chamber %d is attached to chamber %lld, which does not exist."),
					I, C.ParentIndex);
				return false;
			}

			if (C.ParentIndex >= static_cast<int64>(I))
			{
				OutError = FString::Printf(
					TEXT("Chamber %d is attached to chamber %lld. A chamber may only attach to ")
					TEXT("one added BEFORE it, so that no chain of attachments can close into a ")
					TEXT("loop with no fixed position to start from."),
					I, C.ParentIndex);
				return false;
			}
		}
		return true;
	}

	void DeriveChamberPositions(FRoomLayout& Layout)
	{
		// One forward pass. Parents are guaranteed to have a lower index than their children
		// (ValidateAttachments), so a parent is always already positioned by the time the
		// child that names it is reached.
		for (int32 I = 0; I < Layout.Chambers.Num(); ++I)
		{
			FChamberRect& C = Layout.Chambers[I];
			if (C.IsRoot()) { continue; }
			if (!Layout.Chambers.IsValidIndex(static_cast<int32>(C.ParentIndex))) { continue; }

			const FChamberRect& P = Layout.Chambers[static_cast<int32>(C.ParentIndex)];

			// Centring uses integer division, which truncates toward zero. That is a CHOICE,
			// not an accident: it is deterministic for the negative results a child wider than
			// its parent produces, and it keeps every chamber origin on a whole tile. A
			// rounded or floored variant would disagree with this one on exactly the odd/even
			// pairings the kit is full of.
			switch (C.AttachSide)
			{
			case RectGen::ERectSide::North:
				C.GridX = P.GridX + (P.Width - C.Width) / 2;
				C.GridY = P.GridY + P.Length;
				break;
			case RectGen::ERectSide::South:
				C.GridX = P.GridX + (P.Width - C.Width) / 2;
				C.GridY = P.GridY - C.Length;
				break;
			case RectGen::ERectSide::East:
				C.GridX = P.GridX + P.Width;
				C.GridY = P.GridY + (P.Length - C.Length) / 2;
				break;
			case RectGen::ERectSide::West:
				C.GridX = P.GridX - C.Width;
				C.GridY = P.GridY + (P.Length - C.Length) / 2;
				break;
			}
		}

		// South and West attachments put chambers at negative coordinates. Normalising is not
		// tidying -- an unnormalised room has its bounding rect in the wrong place, and every
		// exit is measured from that rect's midpoints.
		FitBoundsToChambers(Layout);
	}

	bool IsSideTaken(const FRoomLayout& Layout, int64 ChamberIndex, RectGen::ERectSide Side)
	{
		if (!Layout.Chambers.IsValidIndex(static_cast<int32>(ChamberIndex))) { return true; }

		// 1. Something is already hanging off this chamber on that side.
		for (const FChamberRect& Other : Layout.Chambers)
		{
			if (Other.ParentIndex == ChamberIndex && Other.AttachSide == Side) { return true; }
		}

		// 2. It is this chamber's own back side -- the one against its parent. Attached on the
		// parent's North means sitting against it on our own South.
		const FChamberRect& C = Layout.Chambers[static_cast<int32>(ChamberIndex)];
		if (!C.IsRoot() && OppositeSide(C.AttachSide) == Side) { return true; }

		return false;
	}

	void FitBoundsToChambers(FRoomLayout& Layout)
	{
		if (Layout.Chambers.Num() == 0) { return; }

		int64 MinX = Layout.Chambers[0].MinX();
		int64 MinY = Layout.Chambers[0].MinY();
		int64 MaxX = Layout.Chambers[0].MaxX();
		int64 MaxY = Layout.Chambers[0].MaxY();

		for (const FChamberRect& C : Layout.Chambers)
		{
			MinX = FMath::Min(MinX, C.MinX());
			MinY = FMath::Min(MinY, C.MinY());
			MaxX = FMath::Max(MaxX, C.MaxX());
			MaxY = FMath::Max(MaxY, C.MaxY());
		}

		// Move first, then measure. Shifting the chambers is what makes the fitted rect start
		// at the origin, and a rect measured before the shift would describe the old frame.
		for (FChamberRect& C : Layout.Chambers)
		{
			C.GridX -= MinX;
			C.GridY -= MinY;
		}

		Layout.BoundingWidth  = MaxX - MinX;
		Layout.BoundingLength = MaxY - MinY;
	}

	bool ValidateExitsReachMidpoints(const FRoomLayout& Layout, FString& OutError)
	{
		for (int64 S = 0; S < RectGen::NumSides; ++S)
		{
			const RectGen::ERectSide Side = static_cast<RectGen::ERectSide>(S);
			if (!Layout.bExit[S]) { continue; }

			if (FindChamberForExit(Layout, Side) == INDEX_NONE)
			{
				int64 DoorLo = 0, DoorHi = 0;
				RoomExitTileSpan(Layout, Side, DoorLo, DoorHi);
				OutError = FString::Printf(
					TEXT("The %s exit is declared, but no single chamber's wall reaches the ")
					TEXT("%s edge across tiles %lld..%lld. That is where the dungeon generator ")
					TEXT("connects, so the exit would open onto sealed void."),
					SideName(Side), SideName(Side), DoorLo, DoorHi);
				return false;
			}
		}
		return true;
	}

	bool ValidateOpeningSpan(int64 TileLo, int64 TileHi, int64 SideTileCount, FString& OutError)
	{
		if (TileHi <= TileLo)
		{
			OutError = FString::Printf(
				TEXT("An opening spans tiles %lld..%lld, which is empty."), TileLo, TileHi);
			return false;
		}

		// Tiles 0 and SideTileCount-1 are the corner cells. A side of 2 tiles or fewer is all
		// corner and can carry no opening at all -- stated here rather than left to fall out of
		// the bounds test, because "1..-1 is empty" is not an error message anyone can act on.
		if (SideTileCount < 3)
		{
			OutError = FString::Printf(
				TEXT("A chamber side of %lld tile(s) is entirely corner cells and cannot carry ")
				TEXT("an opening. It needs at least 3."), SideTileCount);
			return false;
		}

		if (TileLo < 1 || TileHi > SideTileCount - 1)
		{
			OutError = FString::Printf(
				TEXT("An opening spans tiles %lld..%lld on a side of %lld tiles, which runs ")
				TEXT("into a corner cell. Openings must lie within tiles 1..%lld -- a corner ")
				TEXT("piece fills its whole cell, so cutting into one removes the corner ")
				TEXT("instead of widening the doorway."),
				TileLo, TileHi, SideTileCount, SideTileCount - 2);
			return false;
		}

		return true;
	}

	bool ValidateOpeningIndex(int64 OpeningIndex, int64 SideTileCount, FString& OutError)
	{
		// -1 is the graph's "no opening on this side". Nothing to place, nothing to refuse.
		if (OpeningIndex == INDEX_NONE) { return true; }

		return ValidateOpeningSpan(OpeningIndex, OpeningIndex + 1, SideTileCount, OutError);
	}

	bool CollectOpenings(const FRoomLayout& Layout, const TArray<FSharedEdge>& SharedEdges,
	                     TArray<FChamberOpening>& OutOpenings, FString& OutError)
	{
		OutOpenings.Reset();

		// Exterior first, so a room whose exit is unbuildable says so before it reports
		// anything about its interior.
		for (int64 S = 0; S < RectGen::NumSides; ++S)
		{
			const RectGen::ERectSide Side = static_cast<RectGen::ERectSide>(S);
			if (!Layout.bExit[S]) { continue; }

			const int64 CIndex = FindChamberForExit(Layout, Side);
			if (CIndex == INDEX_NONE)
			{
				// ValidateExitsReachMidpoints owns this message. Reaching here means the caller
				// skipped it; refuse rather than index off the end.
				OutError = FString::Printf(
					TEXT("The %s exit has no chamber behind it."), SideName(Side));
				return false;
			}

			const FChamberRect& C = Layout.Chambers[static_cast<int32>(CIndex)];

			int64 DoorLo = 0, DoorHi = 0;
			RoomExitTileSpan(Layout, Side, DoorLo, DoorHi);

			FChamberOpening Opening;
			Opening.ChamberIndex = CIndex;
			Opening.Side = Side;
			Opening.Kind = EOpeningKind::Exterior;
			Opening.OtherChamber = INDEX_NONE;
			Opening.TileLo = DoorLo - C.SideOriginTile(Side);
			Opening.TileHi = DoorHi - C.SideOriginTile(Side);

			FString SpanError;
			if (!ValidateOpeningSpan(Opening.TileLo, Opening.TileHi,
			                         C.TileCountOnSide(Side), SpanError))
			{
				OutError = FString::Printf(
					TEXT("The %s exit lands badly on chamber %lld: %s"),
					SideName(Side), CIndex, *SpanError);
				return false;
			}

			OutOpenings.Add(Opening);
		}

		for (const FSharedEdge& Edge : SharedEdges)
		{
			const EConnectionMode Mode = ConnectionModeFor(Layout, Edge);

			// A shared edge toggled to walled is not an opening. It is the toggle's entire job.
			if (Mode == EConnectionMode::Walled) { continue; }

			const bool bMerged = (Mode == EConnectionMode::Open);

			// Arch: the parity-sized doorway inside the run. Open: the run itself, entire.
			int64 OpenLo = Edge.SpanLo, OpenHi = Edge.SpanHi;
			if (!bMerged)
			{
				OpeningTileSpanWithin(Edge.SpanLo, Edge.SpanHi, OpenLo, OpenHi);
			}

			// One shared edge, two openings: each chamber cuts its own side, and each has to
			// clear its own corners. A run that sits mid-wall on the larger chamber can still
			// be hard against the corner of the smaller one.
			const int64 Pair[2] = { Edge.ChamberA, Edge.ChamberB };
			const RectGen::ERectSide Sides[2] = { Edge.SideOfA, OppositeSide(Edge.SideOfA) };

			for (int32 K = 0; K < 2; ++K)
			{
				const FChamberRect& C = Layout.Chambers[static_cast<int32>(Pair[K])];

				FChamberOpening Opening;
				Opening.ChamberIndex = Pair[K];
				Opening.Side = Sides[K];
				Opening.Kind = bMerged ? EOpeningKind::Merged : EOpeningKind::Interior;
				Opening.OtherChamber = Pair[1 - K];
				Opening.TileLo = OpenLo - C.SideOriginTile(Sides[K]);
				Opening.TileHi = OpenHi - C.SideOriginTile(Sides[K]);

				// The corner rule is an ARCH rule. A removed wall is expected to take the
				// corner pieces at the ends of its run with it -- that is the whole point --
				// so applying it here would refuse every Open edge whose run covers a
				// chamber's full side, which is the commonest case there is.
				if (!bMerged)
				{
					FString SpanError;
					if (!ValidateOpeningSpan(Opening.TileLo, Opening.TileHi,
					                         C.TileCountOnSide(Sides[K]), SpanError))
					{
						OutError = FString::Printf(
							TEXT("The connection between chambers %lld and %lld lands badly on ")
							TEXT("chamber %lld's %s side: %s"),
							Edge.ChamberA, Edge.ChamberB, Pair[K], SideName(Sides[K]), *SpanError);
						return false;
					}
				}

				OutOpenings.Add(Opening);
			}
		}

		return true;
	}

	bool ValidateOneOpeningPerChamberSide(const TArray<FChamberOpening>& Openings,
	                                      FString& OutError)
	{
		for (int32 I = 0; I < Openings.Num(); ++I)
		{
			for (int32 J = I + 1; J < Openings.Num(); ++J)
			{
				const FChamberOpening& A = Openings[I];
				const FChamberOpening& B = Openings[J];

				if (A.ChamberIndex != B.ChamberIndex || A.Side != B.Side) { continue; }

				OutError = FString::Printf(
					TEXT("Chamber %lld needs two openings on its %s side (tiles %lld..%lld and ")
					TEXT("%lld..%lld). The emitter carries one opening index per side, so this ")
					TEXT("layout cannot be built. Move one of the two, or split the chamber."),
					A.ChamberIndex, SideName(A.Side),
					A.TileLo, A.TileHi, B.TileLo, B.TileHi);
				return false;
			}
		}
		return true;
	}

	bool ValidateOpenCornersAreCapped(const FRoomLayout& Layout,
	                                  const TArray<FChamberOpening>& Openings, FString& OutError)
	{
		for (int32 CI = 0; CI < Layout.Chambers.Num(); ++CI)
		{
			const FChamberRect& C = Layout.Chambers[CI];

			// At most one opening per side -- ValidateOneOpeningPerChamberSide runs first and
			// owns that, so a plain per-side slot is enough here.
			const FChamberOpening* OnSide[RectGen::NumSides] = { nullptr, nullptr, nullptr, nullptr };
			for (const FChamberOpening& O : Openings)
			{
				if (O.ChamberIndex != static_cast<int64>(CI)) { continue; }
				OnSide[static_cast<int32>(O.Side)] = &O;
			}

			// Each corner cell belongs to TWO sides at once, at a different local index on
			// each. North and South are indexed along X, East and West along Y -- which is why
			// the same corner is tile 0 of West and tile Width-1 of South.
			struct FCornerOfChamber
			{
				const TCHAR* Name;
				RectGen::ERectSide SideA; int64 IndexOnA;
				RectGen::ERectSide SideB; int64 IndexOnB;
			};
			const FCornerOfChamber Corners[4] = {
				{ TEXT("south-west"), RectGen::ERectSide::South, 0,
				                      RectGen::ERectSide::West,  0 },
				{ TEXT("south-east"), RectGen::ERectSide::South, C.Width - 1,
				                      RectGen::ERectSide::East,  0 },
				{ TEXT("north-west"), RectGen::ERectSide::North, 0,
				                      RectGen::ERectSide::West,  C.Length - 1 },
				{ TEXT("north-east"), RectGen::ERectSide::North, C.Width - 1,
				                      RectGen::ERectSide::East,  C.Length - 1 },
			};

			for (const FCornerOfChamber& K : Corners)
			{
				const FChamberOpening* OA = OnSide[static_cast<int32>(K.SideA)];
				const FChamberOpening* OB = OnSide[static_cast<int32>(K.SideB)];
				if (OA == nullptr || OB == nullptr) { continue; }

				const bool bAReaches = (K.IndexOnA >= OA->TileLo && K.IndexOnA < OA->TileHi);
				const bool bBReaches = (K.IndexOnB >= OB->TileLo && K.IndexOnB < OB->TileHi);
				if (!bAReaches || !bBReaches) { continue; }

				OutError = FString::Printf(
					TEXT("Chamber %d has openings on both its %s and %s sides, and they meet at ")
					TEXT("its %s corner. A removed wall is sealed by the edge cap the ")
					TEXT("PERPENDICULAR run emits where it terminates -- with both sides open ")
					TEXT("neither run terminates there, so nothing fills that cell. That case is ")
					TEXT("unmeasured, so it is refused rather than generated with a possible hole ")
					TEXT("in it. Leave one of the two connections arched."),
					CI, SideName(K.SideA), SideName(K.SideB), K.Name);
				return false;
			}
		}
		return true;
	}

	bool ValidateRoomLayout(const FRoomLayout& Layout,
	                        TArray<FSharedEdge>& OutSharedEdges,
	                        TArray<FChamberOpening>& OutOpenings,
	                        FString& OutError)
	{
		OutSharedEdges.Reset();
		OutOpenings.Reset();

		// First of all: a broken attachment means every position downstream was derived from
		// something that does not exist, so nothing below it would be describing the room.
		if (!ValidateAttachments(Layout, OutError))          { return false; }
		if (!ValidateChambersInBounds(Layout, OutError))     { return false; }
		if (!ValidateChambersDoNotOverlap(Layout, OutError)) { return false; }

		FindSharedEdges(Layout, OutSharedEdges);

		if (!ValidateExitsReachMidpoints(Layout, OutError)) { return false; }
		if (!CollectOpenings(Layout, OutSharedEdges, OutOpenings, OutError)) { return false; }
		if (!ValidateOneOpeningPerChamberSide(OutOpenings, OutError)) { return false; }
		if (!ValidateOpenCornersAreCapped(Layout, OutOpenings, OutError)) { return false; }

		OutError.Reset();
		return true;
	}

} // namespace RoomAuthor
