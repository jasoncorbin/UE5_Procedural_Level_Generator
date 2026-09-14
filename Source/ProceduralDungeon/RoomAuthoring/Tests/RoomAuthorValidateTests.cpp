#include "Misc/AutomationTest.h"
#include "RectDungeon/RectRoomTypes.h"
#include "RoomAuthoring/RoomAuthorTypes.h"
#include "RoomAuthoring/RoomAuthorValidate.h"

#if WITH_DEV_AUTOMATION_TESTS

// DELIBERATELY NO `using namespace`. RectGen and RoomAuthor both name a tile grid, both talk
// about sides, and both will grow constants -- so every symbol below is qualified.
//
// Level_Creator_1's copy of this note added that its module built with bUseUnity = false,
// which helped. THIS MODULE BUILDS WITH UNITY ON, so qualifying matters more here, not less:
// unity is precisely the thing that merges several .cpp files into one translation unit, and
// an unqualified namespace pull is how two same-named symbols land in one and MSVC reports
// C2872. Qualifying is what makes that stay true no matter what either namespace grows next.

namespace
{
	constexpr EAutomationTestFlags RoomAuthorTestFlags =
		EAutomationTestFlags::EditorContext |
		EAutomationTestFlags::CommandletContext |
		EAutomationTestFlags::EngineFilter;

	/** A chamber, spelled out positionally so the fixtures below read as rectangles. */
	RoomAuthor::FChamberRect Chamber(int64 X, int64 Y, int64 W, int64 L)
	{
		RoomAuthor::FChamberRect C;
		C.GridX = X;
		C.GridY = Y;
		C.Width = W;
		C.Length = L;
		return C;
	}

	/**
	 * Two chambers stacked inside a 5 x 8 bounding rect, sharing their full 5-tile width, with
	 * exits on North and South only.
	 *
	 * East and West are OFF on purpose, and the reason is worth stating because it looks like an
	 * oversight: the bounding rect's Length is 8, an EVEN edge, so its East/West doorway is two
	 * tiles wide and spans room tiles 3..5 -- which straddles the seam between the two chambers.
	 * Neither one covers the whole doorway, so declaring those exits would (correctly) be
	 * refused. This fixture is the ACCEPTED case; the refusal has its own test.
	 */
	RoomAuthor::FRoomLayout TwoChamberLayout()
	{
		RoomAuthor::FRoomLayout Layout;
		Layout.BoundingWidth = 5;
		Layout.BoundingLength = 8;
		Layout.bExit[static_cast<int32>(RectGen::ERectSide::North)] = true;
		Layout.bExit[static_cast<int32>(RectGen::ERectSide::South)] = true;
		Layout.Chambers.Add(Chamber(0, 0, 5, 4));
		Layout.Chambers.Add(Chamber(0, 4, 5, 4));
		return Layout;
	}
}

// --- Bounds and overlap -------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomAuthorRejectsChamberOutsideBounds,
	"ProceduralDungeon.RoomAuthoring.Layout.ChamberOutsideTheBoundingRectIsRejected",
	RoomAuthorTestFlags)

bool FRoomAuthorRejectsChamberOutsideBounds::RunTest(const FString&)
{
	RoomAuthor::FRoomLayout Layout;
	Layout.BoundingWidth = 4;
	Layout.BoundingLength = 4;
	Layout.Chambers.Add(Chamber(2, 0, 4, 2));   // spans X 2..6 inside a rect 4 wide

	FString Error;
	TestFalse(TEXT("a chamber running past the bounding rect is refused"),
		RoomAuthor::ValidateChambersInBounds(Layout, Error));
	TestTrue(TEXT("the refusal names the bounding rect"), Error.Contains(TEXT("bounding rect")));

	// The same chamber pulled back inside is fine -- otherwise the test above would pass for a
	// validator that refused everything.
	Layout.Chambers[0] = Chamber(0, 0, 4, 2);
	TestTrue(TEXT("the same chamber inside the rect is accepted"),
		RoomAuthor::ValidateChambersInBounds(Layout, Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomAuthorRejectsOverlappingChambers,
	"ProceduralDungeon.RoomAuthoring.Layout.OverlappingChambersAreRejected",
	RoomAuthorTestFlags)

bool FRoomAuthorRejectsOverlappingChambers::RunTest(const FString&)
{
	RoomAuthor::FRoomLayout Layout;
	Layout.BoundingWidth = 6;
	Layout.BoundingLength = 6;
	Layout.Chambers.Add(Chamber(0, 0, 4, 4));   // X 0..4, Y 0..4
	Layout.Chambers.Add(Chamber(2, 2, 4, 4));   // X 2..6, Y 2..6 -- shares tiles 2..4 both ways

	FString Error;
	TestFalse(TEXT("chambers sharing tiles are refused"),
		RoomAuthor::ValidateChambersDoNotOverlap(Layout, Error));
	TestTrue(TEXT("the refusal names both chambers"),
		Error.Contains(TEXT("Chambers 0 and 1")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomAuthorAllowsAbuttingChambers,
	"ProceduralDungeon.RoomAuthoring.Layout.AbuttingChambersAreNotAnOverlap",
	RoomAuthorTestFlags)

bool FRoomAuthorAllowsAbuttingChambers::RunTest(const FString&)
{
	// The half-open bounds convention, pinned. Read the extents as closed and every abutting
	// pair in the library reads as a one-tile overlap, which would refuse the entire feature.
	RoomAuthor::FRoomLayout Layout;
	Layout.BoundingWidth = 6;
	Layout.BoundingLength = 3;
	Layout.Chambers.Add(Chamber(0, 0, 3, 3));
	Layout.Chambers.Add(Chamber(3, 0, 3, 3));

	FString Error;
	TestTrue(TEXT("chambers that merely abut do not overlap"),
		RoomAuthor::ValidateChambersDoNotOverlap(Layout, Error));
	return true;
}

// --- Shared-edge detection ----------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomAuthorFindsAdjacentSharedEdge,
	"ProceduralDungeon.RoomAuthoring.SharedEdge.FaceToFaceChambersShareARun",
	RoomAuthorTestFlags)

bool FRoomAuthorFindsAdjacentSharedEdge::RunTest(const FString&)
{
	RoomAuthor::FRoomLayout Layout;
	Layout.BoundingWidth = 6;
	Layout.BoundingLength = 3;
	Layout.Chambers.Add(Chamber(0, 0, 3, 3));   // X 0..3
	Layout.Chambers.Add(Chamber(3, 0, 3, 3));   // X 3..6

	TArray<RoomAuthor::FSharedEdge> Edges;
	RoomAuthor::FindSharedEdges(Layout, Edges);

	TestEqual(TEXT("exactly one shared edge"), Edges.Num(), 1);
	if (Edges.Num() != 1) { return false; }

	TestEqual(TEXT("A is the lower index"), Edges[0].ChamberA, static_cast<int64>(0));
	TestEqual(TEXT("B is the higher index"), Edges[0].ChamberB, static_cast<int64>(1));
	TestTrue(TEXT("the run lies on A's East side"),
		Edges[0].SideOfA == RectGen::ERectSide::East);
	TestEqual(TEXT("the run starts at tile 0"), Edges[0].SpanLo, static_cast<int64>(0));
	TestEqual(TEXT("the run ends at tile 3"), Edges[0].SpanHi, static_cast<int64>(3));
	TestEqual(TEXT("the run is 3 tiles long"), Edges[0].SpanTiles(), static_cast<int64>(3));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomAuthorIgnoresCornerOnlyContact,
	"ProceduralDungeon.RoomAuthoring.SharedEdge.ChambersTouchingOnlyAtACornerDoNotShareARun",
	RoomAuthorTestFlags)

bool FRoomAuthorIgnoresCornerOnlyContact::RunTest(const FString&)
{
	// A(X 0..2, Y 0..2) and B(X 2..4, Y 2..4) satisfy A.MaxX == B.MinX AND A.MaxY == B.MinY at
	// once. Both are true and neither is a shared wall: the run is zero tiles long on each
	// axis. This is the case a naive "are they adjacent on X?" test reports as a connection.
	RoomAuthor::FRoomLayout Layout;
	Layout.BoundingWidth = 4;
	Layout.BoundingLength = 4;
	Layout.Chambers.Add(Chamber(0, 0, 2, 2));
	Layout.Chambers.Add(Chamber(2, 2, 2, 2));

	TArray<RoomAuthor::FSharedEdge> Edges;
	RoomAuthor::FindSharedEdges(Layout, Edges);
	TestEqual(TEXT("a corner meeting is not a shared edge"), Edges.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomAuthorIgnoresDiagonalChambers,
	"ProceduralDungeon.RoomAuthoring.SharedEdge.DiagonalChambersWithAGapAreNotAdjacent",
	RoomAuthorTestFlags)

bool FRoomAuthorIgnoresDiagonalChambers::RunTest(const FString&)
{
	RoomAuthor::FRoomLayout Layout;
	Layout.BoundingWidth = 5;
	Layout.BoundingLength = 5;
	Layout.Chambers.Add(Chamber(0, 0, 2, 2));   // X 0..2, Y 0..2
	Layout.Chambers.Add(Chamber(3, 3, 2, 2));   // X 3..5, Y 3..5 -- a tile of void between them

	TArray<RoomAuthor::FSharedEdge> Edges;
	RoomAuthor::FindSharedEdges(Layout, Edges);
	TestEqual(TEXT("chambers set diagonally with a gap share nothing"), Edges.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomAuthorSharesPartialRun,
	"ProceduralDungeon.RoomAuthoring.SharedEdge.PartiallyOverlappingChambersShareOnlyTheOverlap",
	RoomAuthorTestFlags)

bool FRoomAuthorSharesPartialRun::RunTest(const FString&)
{
	// Offset chambers: the run is the intersection, not either chamber's own extent. The
	// archway is placed at the midpoint of THIS run, which is why the extent has to be right.
	RoomAuthor::FRoomLayout Layout;
	Layout.BoundingWidth = 6;
	Layout.BoundingLength = 8;
	Layout.Chambers.Add(Chamber(0, 0, 3, 5));   // X 0..3, Y 0..5
	Layout.Chambers.Add(Chamber(3, 2, 3, 6));   // X 3..6, Y 2..8

	TArray<RoomAuthor::FSharedEdge> Edges;
	RoomAuthor::FindSharedEdges(Layout, Edges);

	TestEqual(TEXT("one shared edge"), Edges.Num(), 1);
	if (Edges.Num() != 1) { return false; }
	TestEqual(TEXT("the run starts where the overlap starts"),
		Edges[0].SpanLo, static_cast<int64>(2));
	TestEqual(TEXT("the run ends where the overlap ends"),
		Edges[0].SpanHi, static_cast<int64>(5));
	return true;
}

// --- Exit midpoints -----------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomAuthorRejectsExitWithNoChamber,
	"ProceduralDungeon.RoomAuthoring.Exits.AnExitWithNoChamberAtItsMidpointIsRejected",
	RoomAuthorTestFlags)

bool FRoomAuthorRejectsExitWithNoChamber::RunTest(const FString&)
{
	// The chamber stops two tiles short of the North edge, so the North exit would open onto
	// the sealed void the design spec calls an acceptable dark corner -- acceptable to look at,
	// not to walk through.
	RoomAuthor::FRoomLayout Layout;
	Layout.BoundingWidth = 5;
	Layout.BoundingLength = 5;
	Layout.bExit[static_cast<int32>(RectGen::ERectSide::North)] = true;
	Layout.Chambers.Add(Chamber(0, 0, 5, 3));

	FString Error;
	TestFalse(TEXT("an exit with nothing behind it is refused"),
		RoomAuthor::ValidateExitsReachMidpoints(Layout, Error));
	TestTrue(TEXT("the refusal names the side"), Error.Contains(TEXT("North")));

	// South, on the same layout, is reachable -- so the rule is per side and not blanket.
	RoomAuthor::FRoomLayout SouthOnly = Layout;
	SouthOnly.bExit[static_cast<int32>(RectGen::ERectSide::North)] = false;
	SouthOnly.bExit[static_cast<int32>(RectGen::ERectSide::South)] = true;
	TestTrue(TEXT("the South exit on the same layout is accepted"),
		RoomAuthor::ValidateExitsReachMidpoints(SouthOnly, Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomAuthorRejectsExitSplitAcrossTwoChambers,
	"ProceduralDungeon.RoomAuthoring.Exits.ADoorwaySplitBetweenTwoChambersIsRejected",
	RoomAuthorTestFlags)

bool FRoomAuthorRejectsExitSplitAcrossTwoChambers::RunTest(const FString&)
{
	// Both chambers reach the East face and between them they cover the whole doorway -- but
	// the seam runs through the middle of it. That is a wall junction inside the opening, not a
	// doorway, so one chamber must cover the whole span on its own.
	RoomAuthor::FRoomLayout Layout = TwoChamberLayout();
	Layout.bExit[static_cast<int32>(RectGen::ERectSide::East)] = true;

	FString Error;
	TestFalse(TEXT("a doorway straddling the seam between two chambers is refused"),
		RoomAuthor::ValidateExitsReachMidpoints(Layout, Error));
	TestTrue(TEXT("the refusal names the side"), Error.Contains(TEXT("East")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomAuthorExitSpanMatchesThePlanner,
	"ProceduralDungeon.RoomAuthoring.Exits.TheDoorwaySpanAgreesWithTheShippingPlanner",
	RoomAuthorTestFlags)

bool FRoomAuthorExitSpanMatchesThePlanner::RunTest(const FString&)
{
	// The authoring tool's outer gateway has to land exactly where RectGen::PlanRoom puts its
	// exit slot, or the generator's door-fill lands on a wall (design spec section 6). This
	// pins the agreement rather than trusting that two formulas stay equal.
	for (int64 Count = 3; Count <= 12; ++Count)
	{
		int64 Lo = 0, Hi = 0;
		RoomAuthor::ExitTileSpan(Count, Lo, Hi);

		TestEqual(FString::Printf(TEXT("count %lld: lo agrees with ExitSpanLoUU"), Count),
			Lo * RectGen::TileUU, RectGen::ExitSpanLoUU(Count));
		TestEqual(FString::Printf(TEXT("count %lld: hi agrees with ExitSpanHiUU"), Count),
			Hi * RectGen::TileUU, RectGen::ExitSpanHiUU(Count));

		// Parity: one tile on an odd edge, two on an even one. Never a fraction of one.
		const int64 Expected = (Count % 2 != 0) ? 1 : 2;
		TestEqual(FString::Printf(TEXT("count %lld: doorway is %lld tile(s)"), Count, Expected),
			Hi - Lo, Expected);
	}
	return true;
}

// --- Opening indices ----------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomAuthorRejectsCornerOpeningIndices,
	"ProceduralDungeon.RoomAuthoring.Openings.IndicesZeroAndNMinusOneAreRejected",
	RoomAuthorTestFlags)

bool FRoomAuthorRejectsCornerOpeningIndices::RunTest(const FString&)
{
	const int64 SideTiles = 5;   // legal indices are 1, 2, 3
	FString Error;

	TestFalse(TEXT("index 0 is a corner cell"),
		RoomAuthor::ValidateOpeningIndex(0, SideTiles, Error));
	TestFalse(TEXT("index N-1 is a corner cell"),
		RoomAuthor::ValidateOpeningIndex(SideTiles - 1, SideTiles, Error));

	TestTrue(TEXT("index 1 is legal"), RoomAuthor::ValidateOpeningIndex(1, SideTiles, Error));
	TestTrue(TEXT("the midpoint index is legal"),
		RoomAuthor::ValidateOpeningIndex(2, SideTiles, Error));
	TestTrue(TEXT("index N-2 is legal"),
		RoomAuthor::ValidateOpeningIndex(SideTiles - 2, SideTiles, Error));

	// -1 is the graph's "no opening on this side". A side with no opening cannot have an
	// illegal one, and refusing it here would refuse every closed side in the library.
	TestTrue(TEXT("-1 means no opening and is accepted"),
		RoomAuthor::ValidateOpeningIndex(INDEX_NONE, SideTiles, Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomAuthorRejectsTwoTileOpeningRunningIntoACorner,
	"ProceduralDungeon.RoomAuthoring.Openings.ATwoTileOpeningEndingInACornerIsRejected",
	RoomAuthorTestFlags)

bool FRoomAuthorRejectsTwoTileOpeningRunningIntoACorner::RunTest(const FString&)
{
	// The case a rule stated only for single indices cannot see: tile 3 is legal on a side of
	// 5, and the span 3..5 starts there and still runs off the end into the corner cell.
	FString Error;
	TestFalse(TEXT("a two-tile opening ending in the far corner is refused"),
		RoomAuthor::ValidateOpeningSpan(3, 5, 5, Error));
	TestTrue(TEXT("a two-tile opening clear of both corners is accepted"),
		RoomAuthor::ValidateOpeningSpan(2, 4, 6, Error));

	// A side of 3 has exactly one non-corner cell; a side of 2 has none at all.
	TestTrue(TEXT("the single middle tile of a 3-tile side is legal"),
		RoomAuthor::ValidateOpeningSpan(1, 2, 3, Error));
	TestFalse(TEXT("a side of 2 tiles is all corner and carries no opening"),
		RoomAuthor::ValidateOpeningSpan(1, 2, 2, Error));
	return true;
}

// --- The v1 one-opening-per-side limit ----------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomAuthorRejectsTwoOpeningsOnOneChamberSide,
	"ProceduralDungeon.RoomAuthoring.Openings.TwoOpeningsOnOneChamberSideAreRejected",
	RoomAuthorTestFlags)

bool FRoomAuthorRejectsTwoOpeningsOnOneChamberSide::RunTest(const FString&)
{
	// One wide chamber along the South of the room with two chambers abutting its North face.
	// Both connections are individually legal -- each lands clear of every corner -- and the
	// layout is still unbuildable, because the emitter carries one opening index per side.
	RoomAuthor::FRoomLayout Layout;
	Layout.BoundingWidth = 9;
	Layout.BoundingLength = 6;
	Layout.Chambers.Add(Chamber(0, 0, 9, 3));   // X 0..9, Y 0..3
	Layout.Chambers.Add(Chamber(0, 3, 4, 3));   // X 0..4, Y 3..6
	Layout.Chambers.Add(Chamber(5, 3, 4, 3));   // X 5..9, Y 3..6

	TArray<RoomAuthor::FSharedEdge> Edges;
	TArray<RoomAuthor::FChamberOpening> Openings;
	FString Error;

	RoomAuthor::FindSharedEdges(Layout, Edges);
	TestEqual(TEXT("both chambers connect to the wide one"), Edges.Num(), 2);

	// Every opening is individually legal: the failure is the COUNT on one side, not a bad
	// placement, and a test that could not tell those apart would pass for the wrong reason.
	TestTrue(TEXT("both openings pass the corner rule"),
		RoomAuthor::CollectOpenings(Layout, Edges, Openings, Error));

	TestFalse(TEXT("two openings on chamber 0's North side are refused"),
		RoomAuthor::ValidateOneOpeningPerChamberSide(Openings, Error));
	TestTrue(TEXT("the refusal names the chamber and side"),
		Error.Contains(TEXT("Chamber 0")) && Error.Contains(TEXT("North")));

	// Walling one of the two is a legal fix, and proves the rule counts OPENINGS rather than
	// shared edges.
	RoomAuthor::FAuthoredConnection Walled;
	Walled.ChamberA = 0;
	Walled.ChamberB = 2;
	Walled.Mode = RoomAuthor::EConnectionMode::Walled;
	Layout.Connections.Add(Walled);

	Openings.Reset();
	TestTrue(TEXT("collecting again succeeds"),
		RoomAuthor::CollectOpenings(Layout, Edges, Openings, Error));
	TestTrue(TEXT("walling one connection resolves the conflict"),
		RoomAuthor::ValidateOneOpeningPerChamberSide(Openings, Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomAuthorWalledEdgeEmitsNoOpening,
	"ProceduralDungeon.RoomAuthoring.Openings.AWalledSharedEdgeCutsNoOpening",
	RoomAuthorTestFlags)

bool FRoomAuthorWalledEdgeEmitsNoOpening::RunTest(const FString&)
{
	RoomAuthor::FRoomLayout Layout = TwoChamberLayout();

	TArray<RoomAuthor::FSharedEdge> Edges;
	TArray<RoomAuthor::FChamberOpening> Openings;
	FString Error;
	RoomAuthor::FindSharedEdges(Layout, Edges);

	TestEqual(TEXT("the two chambers share one edge"), Edges.Num(), 1);
	if (Edges.Num() != 1) { return false; }

	TestTrue(TEXT("an unstated connection arches by default"),
		RoomAuthor::ConnectionModeFor(Layout, Edges[0]) == RoomAuthor::EConnectionMode::Arch);
	TestTrue(TEXT("openings collect"),
		RoomAuthor::CollectOpenings(Layout, Edges, Openings, Error));
	// Two exterior (North, South) plus two interior halves of the one shared edge.
	TestEqual(TEXT("four openings while the edge is arched"), Openings.Num(), 4);

	RoomAuthor::FAuthoredConnection Walled;
	Walled.ChamberA = 0;
	Walled.ChamberB = 1;
	Walled.Mode = RoomAuthor::EConnectionMode::Walled;
	Layout.Connections.Add(Walled);

	TestTrue(TEXT("the toggle is honoured"),
		RoomAuthor::ConnectionModeFor(Layout, Edges[0]) == RoomAuthor::EConnectionMode::Walled);

	Openings.Reset();
	TestTrue(TEXT("openings collect with the edge walled"),
		RoomAuthor::CollectOpenings(Layout, Edges, Openings, Error));
	TestEqual(TEXT("only the two exterior openings remain"), Openings.Num(), 2);
	return true;
}

// --- The accepted case --------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomAuthorAcceptsValidTwoChamberRoom,
	"ProceduralDungeon.RoomAuthoring.Layout.AValidTwoChamberRoomIsAccepted",
	RoomAuthorTestFlags)

bool FRoomAuthorAcceptsValidTwoChamberRoom::RunTest(const FString&)
{
	// A passing case matters as much as the failures: every rule above could be satisfied by a
	// validator that refused everything, and this is the test that would catch one.
	RoomAuthor::FRoomLayout Layout = TwoChamberLayout();

	TArray<RoomAuthor::FSharedEdge> Edges;
	TArray<RoomAuthor::FChamberOpening> Openings;
	FString Error;

	const bool bOk = RoomAuthor::ValidateRoomLayout(Layout, Edges, Openings, Error);
	TestTrue(FString::Printf(TEXT("the layout is accepted (error was: %s)"), *Error), bOk);
	if (!bOk) { return false; }

	TestTrue(TEXT("no error text is left behind on success"), Error.IsEmpty());
	TestEqual(TEXT("one shared edge between the two chambers"), Edges.Num(), 1);
	TestEqual(TEXT("two exterior openings and two interior halves"), Openings.Num(), 4);

	// The interior arch sits at the midpoint of the shared 5-tile run: tile 2 of 0..4, on both
	// chambers, because their side origins agree here.
	int32 InteriorCount = 0;
	for (const RoomAuthor::FChamberOpening& O : Openings)
	{
		if (O.Kind != RoomAuthor::EOpeningKind::Interior) { continue; }
		++InteriorCount;
		TestEqual(TEXT("the arch starts at chamber-local tile 2"), O.TileLo, static_cast<int64>(2));
		TestEqual(TEXT("the arch is one tile wide"), O.TileHi - O.TileLo, static_cast<int64>(1));
		TestNotEqual(TEXT("an interior opening names the chamber across from it"),
			O.OtherChamber, static_cast<int64>(INDEX_NONE));
	}
	TestEqual(TEXT("one interior opening per chamber"), InteriorCount, 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomAuthorStopsAtTheFirstFailure,
	"ProceduralDungeon.RoomAuthoring.Layout.ValidationReportsTheRootProblemNotItsConsequence",
	RoomAuthorTestFlags)

bool FRoomAuthorStopsAtTheFirstFailure::RunTest(const FString&)
{
	// Chambers that overlap ALSO produce nonsense shared edges. The order in ValidateRoomLayout
	// is what makes the reported error the overlap rather than whatever the overlap caused, so
	// it is pinned here rather than left as an accident of how the function reads.
	RoomAuthor::FRoomLayout Layout;
	Layout.BoundingWidth = 6;
	Layout.BoundingLength = 6;
	Layout.Chambers.Add(Chamber(0, 0, 4, 4));
	Layout.Chambers.Add(Chamber(2, 2, 4, 4));

	TArray<RoomAuthor::FSharedEdge> Edges;
	TArray<RoomAuthor::FChamberOpening> Openings;
	FString Error;

	TestFalse(TEXT("the layout is refused"),
		RoomAuthor::ValidateRoomLayout(Layout, Edges, Openings, Error));
	TestTrue(TEXT("the reported problem is the overlap"), Error.Contains(TEXT("overlap")));
	return true;
}

// --- Derived bounds -----------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomAuthorFitBoundsWrapsTheChambers,
	"ProceduralDungeon.RoomAuthoring.Bounds.FittingShrinkWrapsTheBoundingRect",
	RoomAuthorTestFlags)

bool FRoomAuthorFitBoundsWrapsTheChambers::RunTest(const FString&)
{
	// The exact failure that made bounds derived: a rect two tiles larger than its floor.
	// Every declared exit then sits on an edge no wall reaches, and the room refuses with a
	// message about exits when the mistake was about size.
	RoomAuthor::FRoomLayout Layout;
	Layout.BoundingWidth = 9;
	Layout.BoundingLength = 9;
	Layout.Chambers.Add(Chamber(0, 0, 7, 3));
	Layout.Chambers.Add(Chamber(2, 3, 3, 4));

	RoomAuthor::FitBoundsToChambers(Layout);

	TestEqual(TEXT("the rect wraps the chambers' width"), Layout.BoundingWidth, static_cast<int64>(7));
	TestEqual(TEXT("the rect wraps the chambers' length"), Layout.BoundingLength, static_cast<int64>(7));

	// Fitting must not disturb chambers that were already correctly placed.
	TestEqual(TEXT("chamber 0 keeps its origin"), Layout.Chambers[0].GridX, static_cast<int64>(0));
	TestEqual(TEXT("chamber 1 keeps its origin"), Layout.Chambers[1].GridX, static_cast<int64>(2));

	// And the whole point: what refused before now validates, unchanged in every other way.
	Layout.bExit[static_cast<int32>(RectGen::ERectSide::North)] = true;
	Layout.bExit[static_cast<int32>(RectGen::ERectSide::South)] = true;

	TArray<RoomAuthor::FSharedEdge> Edges;
	TArray<RoomAuthor::FChamberOpening> Openings;
	FString Error;
	TestTrue(TEXT("the fitted layout validates"),
		RoomAuthor::ValidateRoomLayout(Layout, Edges, Openings, Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomAuthorFitBoundsNormalisesToTheOrigin,
	"ProceduralDungeon.RoomAuthoring.Bounds.FittingMovesTheRoomToTheOrigin",
	RoomAuthorTestFlags)

bool FRoomAuthorFitBoundsNormalisesToTheOrigin::RunTest(const FString&)
{
	// Fitting only the MAXIMA would leave a 3x2 dead strip along the west and south edges,
	// and an exit on either would open onto it -- the same unreachable-exit failure from the
	// other direction. Normalising is what closes that, so it is pinned separately.
	RoomAuthor::FRoomLayout Layout;
	Layout.BoundingWidth = 1;
	Layout.BoundingLength = 1;
	Layout.Chambers.Add(Chamber(3, 2, 5, 5));

	RoomAuthor::FitBoundsToChambers(Layout);

	TestEqual(TEXT("the chamber moves to the origin in X"), Layout.Chambers[0].GridX, static_cast<int64>(0));
	TestEqual(TEXT("the chamber moves to the origin in Y"), Layout.Chambers[0].GridY, static_cast<int64>(0));
	TestEqual(TEXT("the rect is the chamber's own width"), Layout.BoundingWidth, static_cast<int64>(5));
	TestEqual(TEXT("the rect is the chamber's own length"), Layout.BoundingLength, static_cast<int64>(5));

	// The guarantee that makes exits safe: a chamber flush against all four edges.
	for (int64 S = 0; S < RectGen::NumSides; ++S)
	{
		Layout.bExit[S] = true;
	}
	TArray<RoomAuthor::FSharedEdge> Edges;
	TArray<RoomAuthor::FChamberOpening> Openings;
	FString Error;
	TestTrue(TEXT("all four exits are reachable after fitting"),
		RoomAuthor::ValidateRoomLayout(Layout, Edges, Openings, Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomAuthorFitBoundsToleratesAnEmptyRoom,
	"ProceduralDungeon.RoomAuthoring.Bounds.FittingAChamberlessRoomChangesNothing",
	RoomAuthorTestFlags)

bool FRoomAuthorFitBoundsToleratesAnEmptyRoom::RunTest(const FString&)
{
	// The panel calls this on every push, including before the first chamber exists. There is
	// nothing to fit to, and collapsing the rect to 0x0 would trade one refusal for a worse one.
	RoomAuthor::FRoomLayout Layout;
	Layout.BoundingWidth = 5;
	Layout.BoundingLength = 5;

	RoomAuthor::FitBoundsToChambers(Layout);

	TestEqual(TEXT("width is untouched"), Layout.BoundingWidth, static_cast<int64>(5));
	TestEqual(TEXT("length is untouched"), Layout.BoundingLength, static_cast<int64>(5));
	return true;
}

// --- Open connections ---------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomAuthorOpenEdgeSpansTheWholeRun,
	"ProceduralDungeon.RoomAuthoring.Openings.AnOpenSharedEdgeSpansTheWholeRun",
	RoomAuthorTestFlags)

bool FRoomAuthorOpenEdgeSpansTheWholeRun::RunTest(const FString&)
{
	// An arch is a hole in a wall that still stands; Open removes the wall. The difference is
	// visible only in the SPAN, so that is what this pins.
	RoomAuthor::FRoomLayout Layout = TwoChamberLayout();

	TArray<RoomAuthor::FSharedEdge> Edges;
	RoomAuthor::FindSharedEdges(Layout, Edges);
	TestEqual(TEXT("the two chambers share one edge"), Edges.Num(), 1);
	if (Edges.Num() != 1) { return false; }
	TestEqual(TEXT("the run is the full 5-tile width"), Edges[0].SpanTiles(), static_cast<int64>(5));

	RoomAuthor::FAuthoredConnection Open;
	Open.ChamberA = 0;
	Open.ChamberB = 1;
	Open.Mode = RoomAuthor::EConnectionMode::Open;
	Layout.Connections.Add(Open);

	TestTrue(TEXT("the mode reads back"),
		RoomAuthor::ConnectionModeFor(Layout, Edges[0]) == RoomAuthor::EConnectionMode::Open);

	TArray<RoomAuthor::FChamberOpening> Openings;
	FString Error;
	TestTrue(TEXT("openings collect"),
		RoomAuthor::CollectOpenings(Layout, Edges, Openings, Error));

	int32 MergedCount = 0;
	for (const RoomAuthor::FChamberOpening& O : Openings)
	{
		if (O.Kind != RoomAuthor::EOpeningKind::Merged) { continue; }
		++MergedCount;
		// Both chambers are 5 wide and aligned, so each sees the run as its own full side --
		// corner cells included. An arch on the same edge would be tiles 2..3.
		TestEqual(TEXT("the merged span starts at the chamber's first tile"), O.TileLo, static_cast<int64>(0));
		TestEqual(TEXT("the merged span ends at the chamber's last tile"), O.TileHi, static_cast<int64>(5));
	}
	TestEqual(TEXT("one merged opening per chamber"), MergedCount, 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomAuthorOpenEdgeIsExemptFromTheCornerRule,
	"ProceduralDungeon.RoomAuthoring.Openings.AnOpenEdgeMayReachACornerCell",
	RoomAuthorTestFlags)

bool FRoomAuthorOpenEdgeIsExemptFromTheCornerRule::RunTest(const FString&)
{
	// The corner rule is an ARCH rule. Removing a wall is expected to take the corner pieces at
	// the ends of the run with it, so the identical span must be refused as an arch and
	// accepted as Open -- otherwise Open is unusable on the commonest case there is, a run
	// covering a chamber's entire side.
	RoomAuthor::FRoomLayout Layout = TwoChamberLayout();

	TArray<RoomAuthor::FSharedEdge> Edges;
	TArray<RoomAuthor::FChamberOpening> Openings;
	FString Error;
	RoomAuthor::FindSharedEdges(Layout, Edges);
	if (Edges.Num() != 1) { return false; }

	RoomAuthor::FAuthoredConnection Conn;
	Conn.ChamberA = 0;
	Conn.ChamberB = 1;
	Conn.Mode = RoomAuthor::EConnectionMode::Open;
	Layout.Connections.Add(Conn);

	TestTrue(TEXT("a full-side Open span is accepted"),
		RoomAuthor::CollectOpenings(Layout, Edges, Openings, Error));

	// The control: that same span, 0..5 on a 5-tile side, is exactly what the corner rule
	// exists to refuse. If this passes, the test above proved nothing.
	FString SpanError;
	TestFalse(TEXT("the same span is refused as an arch"),
		RoomAuthor::ValidateOpeningSpan(0, 5, 5, SpanError));
	TestTrue(TEXT("and the refusal is about corners"), SpanError.Contains(TEXT("corner")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomAuthorRefusesTwoOpenSidesMeetingAtACorner,
	"ProceduralDungeon.RoomAuthoring.Openings.TwoOpenSidesMeetingAtACornerAreRefused",
	RoomAuthorTestFlags)

bool FRoomAuthorRefusesTwoOpenSidesMeetingAtACorner::RunTest(const FString&)
{
	// Measured 2026-08-31: ONE removed wall seals, because the perpendicular run terminates at
	// the corner and emits an edge cap where the deleted corner piece was. Two removed walls
	// meeting at a corner leave no run to terminate. Unmeasured, therefore refused.
	//
	// A 3x3 middle chamber with a neighbour below it and another to its west, both Open. Both
	// runs cover the middle chamber's full side, so both reach its south-west corner.
	RoomAuthor::FRoomLayout Layout;
	Layout.Chambers.Add(Chamber(3, 3, 3, 3));   // 0: the middle chamber
	Layout.Chambers.Add(Chamber(3, 0, 3, 3));   // 1: below it, sharing its full South side
	Layout.Chambers.Add(Chamber(0, 3, 3, 3));   // 2: west of it, sharing its full West side
	RoomAuthor::FitBoundsToChambers(Layout);

	RoomAuthor::FAuthoredConnection OpenSouth;
	OpenSouth.ChamberA = 0; OpenSouth.ChamberB = 1;
	OpenSouth.Mode = RoomAuthor::EConnectionMode::Open;
	Layout.Connections.Add(OpenSouth);

	RoomAuthor::FAuthoredConnection OpenWest;
	OpenWest.ChamberA = 0; OpenWest.ChamberB = 2;
	OpenWest.Mode = RoomAuthor::EConnectionMode::Open;
	Layout.Connections.Add(OpenWest);

	TArray<RoomAuthor::FSharedEdge> Edges;
	TArray<RoomAuthor::FChamberOpening> Openings;
	FString Error;

	TestFalse(TEXT("the layout is refused"),
		RoomAuthor::ValidateRoomLayout(Layout, Edges, Openings, Error));
	TestTrue(TEXT("the refusal names the corner"), Error.Contains(TEXT("south-west")));

	// The control, and it is the whole point of the test: arching ONE of the two is a legal
	// fix. If this also refused, the rule would be rejecting Open mode rather than rejecting
	// the specific unmeasured geometry.
	Layout.Connections[1].Mode = RoomAuthor::EConnectionMode::Arch;

	Edges.Reset();
	Openings.Reset();
	TestTrue(TEXT("arching one of the two is accepted"),
		RoomAuthor::ValidateRoomLayout(Layout, Edges, Openings, Error));
	return true;
}

// --- Attachment placement ------------------------------------------------------------------

namespace
{
	/** A chamber attached to a side of an earlier one. */
	RoomAuthor::FChamberRect Attached(int64 W, int64 L, int64 Parent, RectGen::ERectSide Side)
	{
		RoomAuthor::FChamberRect C;
		C.Width = W;
		C.Length = L;
		C.ParentIndex = Parent;
		C.AttachSide = Side;
		return C;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomAuthorDerivesTheAttachedChambersPosition,
	"ProceduralDungeon.RoomAuthoring.Attachment.AChildIsCentredAndFlushOnItsParentsSide",
	RoomAuthorTestFlags)

bool FRoomAuthorDerivesTheAttachedChambersPosition::RunTest(const FString&)
{
	// The shipped 7x7 L, authored the new way: a 7x3 base with a 3x4 on its North. The panel
	// produced exactly (2, 3) for the child on 2026-08-31, so this pins the arithmetic that
	// interaction depends on.
	RoomAuthor::FRoomLayout Layout;
	Layout.Chambers.Add(Chamber(0, 0, 7, 3));
	Layout.Chambers.Add(Attached(3, 4, 0, RectGen::ERectSide::North));

	RoomAuthor::DeriveChamberPositions(Layout);

	TestEqual(TEXT("the child is centred on its parent's width"),
		Layout.Chambers[1].GridX, static_cast<int64>(2));
	TestEqual(TEXT("the child sits flush on the parent's north edge"),
		Layout.Chambers[1].GridY, static_cast<int64>(3));
	TestEqual(TEXT("the room is 7 wide"), Layout.BoundingWidth, static_cast<int64>(7));
	TestEqual(TEXT("the room is 7 long"), Layout.BoundingLength, static_cast<int64>(7));

	// Flush plus centred means they always share a run, so a connection is detected without
	// the author declaring one. That is the property the whole interaction rests on.
	TArray<RoomAuthor::FSharedEdge> Edges;
	RoomAuthor::FindSharedEdges(Layout, Edges);
	TestEqual(TEXT("attaching produces a shared edge on its own"), Edges.Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomAuthorNormalisesAwayNegativeAttachments,
	"ProceduralDungeon.RoomAuthoring.Attachment.SouthAndWestAttachmentsAreNormalised",
	RoomAuthorTestFlags)

bool FRoomAuthorNormalisesAwayNegativeAttachments::RunTest(const FString&)
{
	// Attaching South or West puts a chamber at negative coordinates. Nothing clamps it -- the
	// fit moves the whole room back to the origin instead, which is why chamber 0 is where
	// authoring STARTS and not a coordinate it keeps.
	RoomAuthor::FRoomLayout Layout;
	Layout.Chambers.Add(Chamber(0, 0, 5, 5));
	Layout.Chambers.Add(Attached(5, 3, 0, RectGen::ERectSide::South));

	RoomAuthor::DeriveChamberPositions(Layout);

	TestEqual(TEXT("the child ends up at the origin"),
		Layout.Chambers[1].GridY, static_cast<int64>(0));
	TestEqual(TEXT("and the ROOT is what moved"),
		Layout.Chambers[0].GridY, static_cast<int64>(3));
	TestEqual(TEXT("the room is 5 wide"), Layout.BoundingWidth, static_cast<int64>(5));
	TestEqual(TEXT("the room grew to 8 long"), Layout.BoundingLength, static_cast<int64>(8));

	// No negative coordinate survives, on either axis, whatever was attached where.
	for (const RoomAuthor::FChamberRect& C : Layout.Chambers)
	{
		TestTrue(TEXT("no chamber is left at a negative X"), C.GridX >= 0);
		TestTrue(TEXT("no chamber is left at a negative Y"), C.GridY >= 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomAuthorLeavesRootsAlone,
	"ProceduralDungeon.RoomAuthoring.Attachment.ARecipeWithNoAttachmentsIsUnmoved",
	RoomAuthorTestFlags)

bool FRoomAuthorLeavesRootsAlone::RunTest(const FString&)
{
	// The migration case: every recipe saved before attachments existed loads with all its
	// chambers as roots. Their authored coordinates must survive untouched, or opening an old
	// room silently rearranges it.
	RoomAuthor::FRoomLayout Layout;
	Layout.Chambers.Add(Chamber(0, 0, 7, 3));
	Layout.Chambers.Add(Chamber(2, 3, 3, 4));

	RoomAuthor::DeriveChamberPositions(Layout);

	TestEqual(TEXT("the second chamber keeps its authored X"),
		Layout.Chambers[1].GridX, static_cast<int64>(2));
	TestEqual(TEXT("the second chamber keeps its authored Y"),
		Layout.Chambers[1].GridY, static_cast<int64>(3));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomAuthorRefusesAnAttachmentToALaterChamber,
	"ProceduralDungeon.RoomAuthoring.Attachment.AParentMustHaveALowerIndex",
	RoomAuthorTestFlags)

bool FRoomAuthorRefusesAnAttachmentToALaterChamber::RunTest(const FString&)
{
	// Ascending parents are what make a cycle unrepresentable, which is what lets derivation
	// be one forward pass. Checked rather than assumed, because a recipe is an on-disk asset.
	RoomAuthor::FRoomLayout Layout;
	Layout.Chambers.Add(Attached(3, 3, 1, RectGen::ERectSide::North));
	Layout.Chambers.Add(Chamber(0, 0, 3, 3));

	FString Error;
	TestFalse(TEXT("attaching to a later chamber is refused"),
		RoomAuthor::ValidateAttachments(Layout, Error));
	TestTrue(TEXT("the refusal explains the ordering rule"), Error.Contains(TEXT("BEFORE it")));

	FString OtherError;
	Layout.Chambers[0].ParentIndex = 7;
	TestFalse(TEXT("a parent that does not exist is refused"),
		RoomAuthor::ValidateAttachments(Layout, OtherError));
	TestTrue(TEXT("and says so"), OtherError.Contains(TEXT("does not exist")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomAuthorCountsAChambersOwnBackSideAsTaken,
	"ProceduralDungeon.RoomAuthoring.Attachment.AChambersBackSideIsNotFree",
	RoomAuthorTestFlags)

bool FRoomAuthorCountsAChambersOwnBackSideAsTaken::RunTest(const FString&)
{
	// THE 2026-08-31 DEFECT. The widget tracked only "is something attached to this side" and
	// so offered chamber 1's South -- the side already against chamber 0 -- which dropped a
	// third chamber straight on top of chamber 0.
	RoomAuthor::FRoomLayout Layout;
	Layout.Chambers.Add(Chamber(0, 0, 5, 5));
	Layout.Chambers.Add(Attached(5, 5, 0, RectGen::ERectSide::North));
	RoomAuthor::DeriveChamberPositions(Layout);

	TestTrue(TEXT("chamber 0's North is taken by the chamber attached to it"),
		RoomAuthor::IsSideTaken(Layout, 0, RectGen::ERectSide::North));
	TestTrue(TEXT("chamber 1's South is taken because it faces its own parent"),
		RoomAuthor::IsSideTaken(Layout, 1, RectGen::ERectSide::South));

	// The controls: everything else really is still free, or the rule would just be refusing
	// attachment altogether.
	TestFalse(TEXT("chamber 1's North is free"),
		RoomAuthor::IsSideTaken(Layout, 1, RectGen::ERectSide::North));
	TestFalse(TEXT("chamber 0's South is free"),
		RoomAuthor::IsSideTaken(Layout, 0, RectGen::ERectSide::South));
	TestFalse(TEXT("chamber 0's East is free"),
		RoomAuthor::IsSideTaken(Layout, 0, RectGen::ERectSide::East));

	// And the defect's actual consequence: taking that back side overlaps chamber 0 exactly.
	Layout.Chambers.Add(Attached(5, 5, 1, RectGen::ERectSide::South));
	RoomAuthor::DeriveChamberPositions(Layout);
	FString Error;
	TestFalse(TEXT("attaching to a back side overlaps, and is refused"),
		RoomAuthor::ValidateChambersDoNotOverlap(Layout, Error));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
