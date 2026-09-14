#include "Misc/AutomationTest.h"
#include "RectDungeon/RectRoomTypes.h"
#include "RoomAuthoring/RoomAuthorTypes.h"

#if WITH_DEV_AUTOMATION_TESTS

// DELIBERATELY NO `using namespace` -- see the note at the top of RoomAuthorValidateTests.cpp.

namespace
{
	constexpr EAutomationTestFlags BakeFrameTestFlags =
		EAutomationTestFlags::EditorContext |
		EAutomationTestFlags::CommandletContext |
		EAutomationTestFlags::EngineFilter;
}

/**
 * The rebasing must reproduce a real room's arrow positions exactly.
 *
 * 1_Room1 is 10 x 10 tiles and was read out of the asset itself: its entrance arrow sits at
 * (0,0,0) and its other three at (4000,0), (2000,2000) and (2000,-2000). Those four numbers are
 * the whole contract this arithmetic exists to satisfy, so they are asserted against rather than
 * described. If the frame is ever wrong, it is wrong HERE, not in a Blueprint nobody can diff.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBakeFrameReproducesAMeasuredRoom,
	"ProceduralDungeon.RoomAuthoring.BakeFrame.RebasingReproducesTheMeasuredRoomArrows",
	BakeFrameTestFlags)

bool FBakeFrameReproducesAMeasuredRoom::RunTest(const FString&)
{
	const int64 W = 10, L = 10;

	// Entering from the West, which is the orientation 1_Room1 was authored in.
	const RectGen::ERectSide Entrance = RectGen::ERectSide::West;

	auto RebasedMidpoint = [&](RectGen::ERectSide Side, int64& X, int64& Y)
	{
		int64 Ax = 0, Ay = 0;
		RoomAuthor::ExitMidpointUU(W, L, Side, Ax, Ay);
		RoomAuthor::RebaseToEntranceUU(W, L, Entrance, Ax, Ay, X, Y);
	};

	int64 X = 0, Y = 0;

	RebasedMidpoint(RectGen::ERectSide::West, X, Y);
	TestEqual(TEXT("the entrance lands on the origin, X"), X, static_cast<int64>(0));
	TestEqual(TEXT("the entrance lands on the origin, Y"), Y, static_cast<int64>(0));

	RebasedMidpoint(RectGen::ERectSide::East, X, Y);
	TestEqual(TEXT("the far exit lands at 4000 along +X"), X, static_cast<int64>(4000));
	TestEqual(TEXT("the far exit is centred on Y"), Y, static_cast<int64>(0));

	RebasedMidpoint(RectGen::ERectSide::North, X, Y);
	TestEqual(TEXT("the north exit lands half way along"), X, static_cast<int64>(2000));
	TestEqual(TEXT("the north exit lands at +2000 across"), Y, static_cast<int64>(2000));

	RebasedMidpoint(RectGen::ERectSide::South, X, Y);
	TestEqual(TEXT("the south exit lands half way along"), X, static_cast<int64>(2000));
	TestEqual(TEXT("the south exit lands at -2000 across"), Y, static_cast<int64>(-2000));

	return true;
}

/**
 * Whichever exit is chosen as the entrance, that exit lands on the origin and the body extends
 * along +X.
 *
 * This is the property that makes one baked piece per exit work at all. Asserted for every side
 * of an ASYMMETRIC room, because a square one cannot tell a swap of the two axes from a correct
 * answer -- which is exactly the bug a 10x10 fixture would hide.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBakeFrameEveryEntranceLandsOnTheOrigin,
	"ProceduralDungeon.RoomAuthoring.BakeFrame.EveryEntranceLandsOnTheOriginFacingPlusX",
	BakeFrameTestFlags)

bool FBakeFrameEveryEntranceLandsOnTheOrigin::RunTest(const FString&)
{
	const int64 W = 7, L = 4;   // deliberately not square, and one odd / one even

	const RectGen::ERectSide Sides[] = {
		RectGen::ERectSide::North, RectGen::ERectSide::East,
		RectGen::ERectSide::South, RectGen::ERectSide::West };

	for (const RectGen::ERectSide Entrance : Sides)
	{
		int64 Ax = 0, Ay = 0;
		RoomAuthor::ExitMidpointUU(W, L, Entrance, Ax, Ay);

		int64 X = 0, Y = 0;
		RoomAuthor::RebaseToEntranceUU(W, L, Entrance, Ax, Ay, X, Y);
		TestEqual(*FString::Printf(TEXT("%s entrance lands on the origin, X"),
			RoomAuthor::SideName(Entrance)), X, static_cast<int64>(0));
		TestEqual(*FString::Printf(TEXT("%s entrance lands on the origin, Y"),
			RoomAuthor::SideName(Entrance)), Y, static_cast<int64>(0));

		// The whole room must sit in X >= 0 and be symmetric about Y = 0.
		int64 Along = 0, Across = 0;
		RoomAuthor::BakedExtentTiles(W, L, Entrance, Along, Across);

		const int64 CornersX[4] = { 0, W * RectGen::TileUU, 0, W * RectGen::TileUU };
		const int64 CornersY[4] = { 0, 0, L * RectGen::TileUU, L * RectGen::TileUU };
		int64 MinX = MAX_int64, MaxX = MIN_int64, MinY = MAX_int64, MaxY = MIN_int64;
		for (int32 I = 0; I < 4; ++I)
		{
			int64 Cx = 0, Cy = 0;
			RoomAuthor::RebaseToEntranceUU(W, L, Entrance, CornersX[I], CornersY[I], Cx, Cy);
			MinX = FMath::Min(MinX, Cx); MaxX = FMath::Max(MaxX, Cx);
			MinY = FMath::Min(MinY, Cy); MaxY = FMath::Max(MaxY, Cy);
		}

		TestEqual(*FString::Printf(TEXT("%s: the room starts at X 0"),
			RoomAuthor::SideName(Entrance)), MinX, static_cast<int64>(0));
		TestEqual(*FString::Printf(TEXT("%s: the room reaches its along-extent"),
			RoomAuthor::SideName(Entrance)), MaxX, Along * RectGen::TileUU);
		TestEqual(*FString::Printf(TEXT("%s: the room is centred across Y"),
			RoomAuthor::SideName(Entrance)), MinY, -MaxY);
		TestEqual(*FString::Printf(TEXT("%s: the across-extent is the other dimension"),
			RoomAuthor::SideName(Entrance)), MaxY - MinY, Across * RectGen::TileUU);
	}

	return true;
}

/**
 * Rebasing is rigid: it moves the room without resizing or reflecting it.
 *
 * A quarter turn plus a translation preserves distance. Checking that directly is what would
 * catch a sign error that happens to look plausible on a symmetric fixture -- a reflection also
 * sends the entrance to the origin, and would put every room's exits on the wrong hand.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBakeFrameIsRigid,
	"ProceduralDungeon.RoomAuthoring.BakeFrame.RebasingPreservesDistancesAndHandedness",
	BakeFrameTestFlags)

bool FBakeFrameIsRigid::RunTest(const FString&)
{
	const int64 W = 6, L = 9;
	const RectGen::ERectSide Sides[] = {
		RectGen::ERectSide::North, RectGen::ERectSide::East,
		RectGen::ERectSide::South, RectGen::ERectSide::West };

	// THREE points, and the cross product of two EDGES between them.
	//
	// Not the cross product of the position vectors: that is invariant under rotation about the
	// origin, and this transform translates before it rotates, so it would report a mirror on
	// three sides out of four while the arithmetic was perfectly correct. Edge vectors are
	// differences, so the translation cancels and what is left measures handedness alone.
	const int64 Px = 800,  Py = 1200;
	const int64 Qx = 2000, Qy = 400;
	const int64 Sx = 1200, Sy = 2400;

	for (const RectGen::ERectSide Entrance : Sides)
	{
		int64 Rpx = 0, Rpy = 0, Rqx = 0, Rqy = 0, Rsx = 0, Rsy = 0;
		RoomAuthor::RebaseToEntranceUU(W, L, Entrance, Px, Py, Rpx, Rpy);
		RoomAuthor::RebaseToEntranceUU(W, L, Entrance, Qx, Qy, Rqx, Rqy);
		RoomAuthor::RebaseToEntranceUU(W, L, Entrance, Sx, Sy, Rsx, Rsy);

		const int64 D0 = (Qx - Px) * (Qx - Px) + (Qy - Py) * (Qy - Py);
		const int64 D1 = (Rqx - Rpx) * (Rqx - Rpx) + (Rqy - Rpy) * (Rqy - Rpy);
		TestEqual(*FString::Printf(TEXT("%s preserves squared distance"),
			RoomAuthor::SideName(Entrance)), D1, D0);

		const int64 Cross0 = (Qx - Px) * (Sy - Py) - (Qy - Py) * (Sx - Px);
		const int64 Cross1 = (Rqx - Rpx) * (Rsy - Rpy) - (Rqy - Rpy) * (Rsx - Rpx);
		TestEqual(*FString::Printf(TEXT("%s preserves the signed area, so nothing is mirrored"),
			RoomAuthor::SideName(Entrance)), Cross1, Cross0);
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
