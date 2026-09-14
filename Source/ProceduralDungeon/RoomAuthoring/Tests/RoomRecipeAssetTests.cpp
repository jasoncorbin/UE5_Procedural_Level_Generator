#include "Misc/AutomationTest.h"
#include "RectDungeon/RectRoomTypes.h"
#include "RoomAuthoring/RoomAuthorTypes.h"
#include "RoomAuthoring/RoomAuthorValidate.h"
#include "RoomAuthoring/RoomRecipeAsset.h"

#if WITH_DEV_AUTOMATION_TESTS

// No `using namespace` here either -- see the note at the top of RoomAuthorValidateTests.cpp.

namespace
{
	constexpr EAutomationTestFlags RoomRecipeTestFlags =
		EAutomationTestFlags::EditorContext |
		EAutomationTestFlags::CommandletContext |
		EAutomationTestFlags::EngineFilter;

	FRoomChamber MakeChamber(int32 X, int32 Y, int32 W, int32 L)
	{
		FRoomChamber C;
		C.GridX = X;
		C.GridY = Y;
		C.Width = W;
		C.Length = L;
		return C;
	}

	/** The same two-stacked-chamber room the pure tests use, built through reflection. */
	URoomRecipeAsset* MakeTwoChamberRecipe()
	{
		URoomRecipeAsset* Recipe = NewObject<URoomRecipeAsset>();
		Recipe->BoundingWidth = 5;
		Recipe->BoundingLength = 8;
		Recipe->bExitNorth = true;
		Recipe->bExitSouth = true;
		Recipe->bExitEast = false;
		Recipe->bExitWest = false;
		Recipe->Chambers.Add(MakeChamber(0, 0, 5, 4));
		Recipe->Chambers.Add(MakeChamber(0, 4, 5, 4));
		return Recipe;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomRecipeMakeLayoutWidens,
	"ProceduralDungeon.RoomAuthoring.Recipe.MakeLayoutWidensEveryFieldIntoTheLayout",
	RoomRecipeTestFlags)

bool FRoomRecipeMakeLayoutWidens::RunTest(const FString&)
{
	// The conversion is the ONE crossing of the reflection boundary, and it only ever widens --
	// same rule and same shape as URectRoomAsset::MakeSpec. A narrowing here would be invisible
	// until a room large enough to truncate showed up, which is not a failure anyone attributes
	// to a cast months later.
	URoomRecipeAsset* Recipe = MakeTwoChamberRecipe();

	RoomAuthor::FRoomLayout Layout;
	Recipe->MakeLayout(Layout);

	TestEqual(TEXT("bounding width crosses"), Layout.BoundingWidth, static_cast<int64>(5));
	TestEqual(TEXT("bounding length crosses"), Layout.BoundingLength, static_cast<int64>(8));
	TestEqual(TEXT("both chambers cross"), Layout.Chambers.Num(), 2);

	TestEqual(TEXT("chamber 1 keeps its origin"), Layout.Chambers[1].GridY, static_cast<int64>(4));
	TestEqual(TEXT("chamber 1 keeps its extent"), Layout.Chambers[1].Length, static_cast<int64>(4));

	TestTrue(TEXT("North maps to bExitNorth"),
		Layout.bExit[static_cast<int32>(RectGen::ERectSide::North)]);
	TestTrue(TEXT("South maps to bExitSouth"),
		Layout.bExit[static_cast<int32>(RectGen::ERectSide::South)]);
	TestFalse(TEXT("East maps to bExitEast"),
		Layout.bExit[static_cast<int32>(RectGen::ERectSide::East)]);
	TestFalse(TEXT("West maps to bExitWest"),
		Layout.bExit[static_cast<int32>(RectGen::ERectSide::West)]);

	// The four named flags and RectGen's ordering are two facts that have to agree, and nothing
	// but this makes them. Mis-map one and the room generates with its doorway on the wrong wall.
	for (int64 S = 0; S < RectGen::NumSides; ++S)
	{
		const RectGen::ERectSide Side = static_cast<RectGen::ERectSide>(S);
		TestTrue(FString::Printf(TEXT("side %lld agrees with bExitFor"), S),
			Layout.bExit[S] == Recipe->bExitFor(Side));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomRecipeConnectionsCrossTheBoundary,
	"ProceduralDungeon.RoomAuthoring.Recipe.AuthoredConnectionsCrossWithTheirToggle",
	RoomRecipeTestFlags)

bool FRoomRecipeConnectionsCrossTheBoundary::RunTest(const FString&)
{
	URoomRecipeAsset* Recipe = MakeTwoChamberRecipe();

	FRoomConnection Walled;
	Walled.ChamberA = 0;
	Walled.ChamberB = 1;
	Walled.Mode = ERoomConnectionMode::Walled;
	Recipe->Connections.Add(Walled);

	RoomAuthor::FRoomLayout Layout;
	Recipe->MakeLayout(Layout);

	TestEqual(TEXT("the authored connection crosses"), Layout.Connections.Num(), 1);
	if (Layout.Connections.Num() != 1) { return false; }
	TestTrue(TEXT("the walled toggle survives the crossing"),
		Layout.Connections[0].Mode == RoomAuthor::EConnectionMode::Walled);

	// And it actually suppresses the arch, rather than merely arriving intact.
	TArray<RoomAuthor::FSharedEdge> Edges;
	RoomAuthor::FindSharedEdges(Layout, Edges);
	TestEqual(TEXT("one shared edge"), Edges.Num(), 1);
	if (Edges.Num() != 1) { return false; }
	TestTrue(TEXT("the shared edge reads as walled"),
		RoomAuthor::ConnectionModeFor(Layout, Edges[0]) == RoomAuthor::EConnectionMode::Walled);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomRecipeValidatesEndToEnd,
	"ProceduralDungeon.RoomAuthoring.Recipe.ValidateRecipeAcceptsAValidRoomAndRefusesABadOne",
	RoomRecipeTestFlags)

bool FRoomRecipeValidatesEndToEnd::RunTest(const FString&)
{
	URoomRecipeAsset* Recipe = MakeTwoChamberRecipe();

	FString Error;
	const bool bAccepted = Recipe->ValidateRecipe(Error);
	TestTrue(FString::Printf(TEXT("a valid recipe is accepted (error was: %s)"), *Error),
		bAccepted);
	TestTrue(TEXT("no error text is left behind on success"), Error.IsEmpty());

	// Turning on the East exit is the refusal case from the pure tests, reached through the
	// asset this time -- so the wiring between the two layers is exercised, not just each layer.
	Recipe->bExitEast = true;
	TestFalse(TEXT("an unreachable exit is refused through the asset"),
		Recipe->ValidateRecipe(Error));
	TestTrue(TEXT("the refusal names the side"), Error.Contains(TEXT("East")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomRecipeDefaultsAreBuildable,
	"ProceduralDungeon.RoomAuthoring.Recipe.AFreshRecipeWithOneChamberValidates",
	RoomRecipeTestFlags)

bool FRoomRecipeDefaultsAreBuildable::RunTest(const FString&)
{
	// A newly created asset is what the author sees first. Its defaults -- a 5x5 bounding rect,
	// all four exits on -- must accept a single chamber filling the rect, or the widget opens
	// on a validation error nobody caused.
	URoomRecipeAsset* Recipe = NewObject<URoomRecipeAsset>();
	Recipe->Chambers.Add(MakeChamber(0, 0, Recipe->BoundingWidth, Recipe->BoundingLength));

	FString Error;
	const bool bOk = Recipe->ValidateRecipe(Error);
	TestTrue(FString::Printf(TEXT("the default room validates (error was: %s)"), *Error), bOk);

	TestEqual(TEXT("the default wall height is the Large tier"), Recipe->WallHeightUU, 570);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
