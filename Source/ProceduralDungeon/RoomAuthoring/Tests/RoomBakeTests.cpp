#include "Misc/AutomationTest.h"
#include "RectDungeon/RectRoomTypes.h"
#include "RoomAuthoring/DungeonKitSet.h"
#include "RoomAuthoring/RoomAuthorTools.h"
#include "RoomAuthoring/RoomAuthorTypes.h"
#include "RoomAuthoring/RoomRecipeAsset.h"

#if WITH_DEV_AUTOMATION_TESTS

// DELIBERATELY NO `using namespace` -- see the note at the top of RoomAuthorValidateTests.cpp.
//
// SCOPE. These cover the bake's REFUSALS, which are the half that can run headlessly. The
// success path needs a live authoring world with generated actors standing in it, and a test
// that writes into the room library and then fails mid-run is how a throwaway asset gets
// committed -- so it stays visual-evidence territory, exactly as it was in Level_Creator_1.

namespace
{
	constexpr EAutomationTestFlags RoomBakeTestFlags =
		EAutomationTestFlags::EditorContext |
		EAutomationTestFlags::CommandletContext |
		EAutomationTestFlags::EngineFilter;

	/**
	 * A recipe that is refusable for the reason under test AND unbakeable in shape.
	 *
	 * The second half is a BACKSTOP, not belt and braces. Headlessly the open level may well be
	 * the authoring level, in which case a fixture that refused only for the reason under test
	 * could sail past that guard and drive a real bake into /Game/RectDungeon/Rooms. Giving it a
	 * chamber-free layout means no test in this file can write an asset even if the guard it is
	 * exercising stops firing.
	 */
	URoomRecipeAsset* MakeUnbakeableRecipe()
	{
		URoomRecipeAsset* Recipe = NewObject<URoomRecipeAsset>();
		Recipe->RoomName = TEXT("TestRoom");
		Recipe->RoomType = TEXT("TestType");
		Recipe->KitSet = NewObject<UDungeonKitSet>();
		Recipe->BoundingWidth = 5;
		Recipe->BoundingLength = 5;
		Recipe->bExitNorth = true;
		Recipe->bExitEast = false;
		Recipe->bExitSouth = false;
		Recipe->bExitWest = false;
		Recipe->Chambers.Reset();   // <- no chambers: never bakeable
		return Recipe;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBakeRefusesWithoutARecipe,
	"ProceduralDungeon.RoomAuthoring.Bake.RefusesWithNoRecipeAtAll",
	RoomBakeTestFlags)

bool FBakeRefusesWithoutARecipe::RunTest(const FString&)
{
	TArray<FString> Paths;
	FString Status;
	TestFalse(TEXT("a null recipe is refused"),
		URoomAuthorTools::BakeAuthoredRoom(nullptr, nullptr, Paths, Status));
	TestTrue(TEXT("the refusal says so"), Status.Contains(TEXT("REFUSED")));
	TestEqual(TEXT("nothing is reported as written"), Paths.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBakeRefusesAnUnfilablePath,
	"ProceduralDungeon.RoomAuthoring.Bake.RefusesARoomItCannotFile",
	RoomBakeTestFlags)

bool FBakeRefusesAnUnfilablePath::RunTest(const FString&)
{
	// Name and type are PATH COMPONENTS. A room missing either would be written somewhere
	// nobody will browse to again, and this bake writes one asset per exit -- so an empty
	// component scatters a whole set of them.
	TArray<FString> Paths;
	FString Status;

	URoomRecipeAsset* NoName = MakeUnbakeableRecipe();
	NoName->RoomName = NAME_None;
	TestFalse(TEXT("an unnamed room is refused"),
		URoomAuthorTools::BakeAuthoredRoom(nullptr, NoName, Paths, Status));
	TestTrue(TEXT("the refusal names the problem"), Status.Contains(TEXT("no name")));
	TestEqual(TEXT("nothing written"), Paths.Num(), 0);

	URoomRecipeAsset* NoType = MakeUnbakeableRecipe();
	NoType->RoomType = NAME_None;
	TestFalse(TEXT("an untyped room is refused"),
		URoomAuthorTools::BakeAuthoredRoom(nullptr, NoType, Paths, Status));
	TestTrue(TEXT("the refusal names the problem"), Status.Contains(TEXT("no type")));
	TestEqual(TEXT("nothing written"), Paths.Num(), 0);

	return true;
}

/**
 * The exit-fill guard asks the EDGE's parity, and asks it per side.
 *
 * A doorway on an odd edge is one tile and takes the narrow pair; an even edge is two tiles and
 * takes the wide pair. A room with an odd width and an even length genuinely needs both, and
 * that asymmetry is the case a single "does this room need doors" flag cannot express.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBakeRefusesMissingExitFills,
	"ProceduralDungeon.RoomAuthoring.Bake.RefusesARoomWhoseExitFillsAreMissing",
	RoomBakeTestFlags)

bool FBakeRefusesMissingExitFills::RunTest(const FString&)
{
	TArray<FString> Paths;
	FString Status;

	// A 5-wide room exiting North: the North edge runs along X and is 5 tiles, which is odd, so
	// it needs the NARROW pair.
	//
	// Emptying the kit's Door is what it takes to reach this refusal now. A recipe with no kit
	// set at all falls back to the built-in census kit, which HAS a door -- so the naive
	// fixture, a recipe with nothing set anywhere, sails past this guard. That fallback is the
	// point: it is what lets a room bake without an authored kit asset first.
	URoomRecipeAsset* Narrow = MakeUnbakeableRecipe();
	Narrow->KitSet = NewObject<UDungeonKitSet>();
	Narrow->KitSet->Door = FSoftClassPath();
	TestFalse(TEXT("a room whose kit has no narrow door is refused"),
		URoomAuthorTools::BakeAuthoredRoom(nullptr, Narrow, Paths, Status));
	TestTrue(TEXT("the refusal names Door"), Status.Contains(TEXT("Door")));

	// The same room with an intact kit gets past the narrow check and fails later instead,
	// which is what proves the kit is consulted here rather than the raw slot.
	URoomRecipeAsset* Kitted = MakeUnbakeableRecipe();
	TestFalse(TEXT("still refused, but not for a missing Door"),
		URoomAuthorTools::BakeAuthoredRoom(nullptr, Kitted, Paths, Status));
	TestFalse(TEXT("the kit set satisfied the narrow pair"),
		Status.Contains(TEXT("Door is not set")));

	// And with NO kit set at all, the built-in census fills the same gap -- the case
	// DA_RoomRecipe_Sample_TwoChamber hit, where a pre-kit-set recipe could not bake.
	URoomRecipeAsset* Legacy = MakeUnbakeableRecipe();
	Legacy->KitSet = nullptr;
	TestFalse(TEXT("a legacy recipe is still refused for its shape"),
		URoomAuthorTools::BakeAuthoredRoom(nullptr, Legacy, Paths, Status));
	TestFalse(TEXT("but no longer for a missing Door"), Status.Contains(TEXT("Door is not set")));

	// An EVEN edge needs the wide pair, and the kit set deliberately has no wide door -- the
	// census contained none, so guessing one is exactly what UDungeonKitSet refuses to do.
	URoomRecipeAsset* Wide = MakeUnbakeableRecipe();
	Wide->BoundingWidth = 4;          // even North edge
	TestFalse(TEXT("a room with an even exit edge and no wide door is refused"),
		URoomAuthorTools::BakeAuthoredRoom(nullptr, Wide, Paths, Status));
	TestTrue(TEXT("the refusal names DoorWide"), Status.Contains(TEXT("DoorWide")));
	TestEqual(TEXT("nothing written"), Paths.Num(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBakeRefusesARoomWithNoExits,
	"ProceduralDungeon.RoomAuthoring.Bake.RefusesARoomWithNoExits",
	RoomBakeTestFlags)

bool FBakeRefusesARoomWithNoExits::RunTest(const FString&)
{
	// One piece is baked PER EXIT, so a room with none would write nothing at all while
	// reporting success -- a silent no-op is the worst possible outcome for a bake.
	URoomRecipeAsset* Recipe = MakeUnbakeableRecipe();
	Recipe->bExitNorth = false;
	Recipe->bExitEast = false;
	Recipe->bExitSouth = false;
	Recipe->bExitWest = false;

	TArray<FString> Paths;
	FString Status;
	TestFalse(TEXT("a room with no exits is refused"),
		URoomAuthorTools::BakeAuthoredRoom(nullptr, Recipe, Paths, Status));
	TestTrue(TEXT("the refusal explains why"), Status.Contains(TEXT("no exits")));
	TestEqual(TEXT("nothing written"), Paths.Num(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBakeRefusesAnInvalidLayout,
	"ProceduralDungeon.RoomAuthoring.Bake.RefusesALayoutTheValidatorRejects",
	RoomBakeTestFlags)

bool FBakeRefusesAnInvalidLayout::RunTest(const FString&)
{
	// The validator owns every layout rule and every sentence. The bake must not re-check or
	// paraphrase them -- it forwards, so a fault reads identically wherever it is discovered.
	URoomRecipeAsset* Recipe = MakeUnbakeableRecipe();
	Recipe->DoorCls = FSoftClassPath(TEXT("/Game/X/BP_D.BP_D_C"));
	Recipe->WallCapCls = FSoftClassPath(TEXT("/Game/X/BP_C.BP_C_C"));

	FString ValidatorSays;
	const bool bValid = Recipe->ValidateRecipe(ValidatorSays);
	TestFalse(TEXT("the fixture really is invalid"), bValid);

	TArray<FString> Paths;
	FString Status;
	TestFalse(TEXT("the bake refuses it too"),
		URoomAuthorTools::BakeAuthoredRoom(nullptr, Recipe, Paths, Status));
	TestTrue(TEXT("and repeats the validator's own sentence"),
		Status.Contains(ValidatorSays));
	TestEqual(TEXT("nothing written"), Paths.Num(), 0);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
