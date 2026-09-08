#include "Misc/AutomationTest.h"
#include "RectDungeon/RectRoomTypes.h"
#include "RoomAuthoring/DungeonKitSet.h"
#include "RoomAuthoring/RoomRecipeAsset.h"

#if WITH_DEV_AUTOMATION_TESTS

// No `using namespace` here either -- see the note at the top of RoomAuthorValidateTests.cpp.

namespace
{
	constexpr EAutomationTestFlags KitSetTestFlags =
		EAutomationTestFlags::EditorContext |
		EAutomationTestFlags::CommandletContext |
		EAutomationTestFlags::EngineFilter;

	const TCHAR* const SomeOtherWall =
		TEXT("/Game/Fantastic_Dungeon_Pack/blueprints/modular/comps/wall/PivotEdge/BP_COMP_Wall_01_E_straight_large_wide.BP_COMP_Wall_01_E_straight_large_wide_C");
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKitSetShipsTheCensusSet,
	"ProceduralDungeon.RoomAuthoring.KitSet.ConstructorDefaultsFillEveryStructuralRole",
	KitSetTestFlags)

bool FKitSetShipsTheCensusSet::RunTest(const FString&)
{
	// The point of shipping the census as constructor defaults rather than as a DA_KitSet_*
	// asset is that this layer is usable with no editor and no content authored first. If that
	// is true, a freshly constructed set validates.
	const UDungeonKitSet* Kit = NewObject<UDungeonKitSet>();

	FString Error;
	TestTrue(TEXT("a default-constructed kit set validates"), Kit->ValidateKit(Error));
	TestTrue(TEXT("no error is written on success"), Error.IsEmpty());

	// Spot-check the one pairing that has actually gone wrong before. Door_Walled is the SEALED
	// filler; putting it in Door is the sealed-doorway defect, and this asserts the two slots
	// hold different assets rather than merely that both are non-empty.
	TestNotEqual(TEXT("Door and WallCap are different assets"),
		Kit->Door.ToString(), Kit->WallCap.ToString());
	TestTrue(TEXT("WallCap is the Door_Walled sealed filler"),
		Kit->WallCap.ToString().Contains(TEXT("BP_COMP_Door_Walled_01_large")));
	TestTrue(TEXT("Door is the passable door, with the _2 suffix"),
		Kit->Door.ToString().Contains(TEXT("BP_COMP_Door_01_large_2")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKitSetCeilingShipsEmpty,
	"ProceduralDungeon.RoomAuthoring.KitSet.CeilingMeshShipsEmptyAndDoesNotBlockValidation",
	KitSetTestFlags)

bool FKitSetCeilingShipsEmpty::RunTest(const FString&)
{
	// CeilingMesh is deliberately unfilled: the census carried no ceiling, and a guessed path
	// is worse than an empty slot because it looks deliberate. This pins BOTH halves of that --
	// it really is empty, and its emptiness really does not fail the kit.
	const UDungeonKitSet* Kit = NewObject<UDungeonKitSet>();

	TestFalse(TEXT("CeilingMesh ships empty"), Kit->CeilingMesh.IsValid());

	FString Error;
	TestTrue(TEXT("an empty ceiling does not fail validation"), Kit->ValidateKit(Error));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKitSetMissingRoleIsRefused,
	"ProceduralDungeon.RoomAuthoring.KitSet.AnEmptyStructuralRoleIsRefusedByName",
	KitSetTestFlags)

bool FKitSetMissingRoleIsRefused::RunTest(const FString&)
{
	UDungeonKitSet* Kit = NewObject<UDungeonKitSet>();
	Kit->Corner = FSoftClassPath();

	FString Error;
	TestFalse(TEXT("a set with no corner is refused"), Kit->ValidateKit(Error));
	TestTrue(TEXT("the refusal names the empty role"), Error.Contains(TEXT("Corner")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRecipeInheritsFromKitSet,
	"ProceduralDungeon.RoomAuthoring.KitSet.AnEmptyChamberSlotInheritsFromTheKit",
	KitSetTestFlags)

bool FRecipeInheritsFromKitSet::RunTest(const FString&)
{
	// The inversion kit sets exist for: a chamber that says nothing gets the set's piece, so
	// editing the set fixes every room that uses it.
	URoomRecipeAsset* Recipe = NewObject<URoomRecipeAsset>();
	Recipe->KitSet = NewObject<UDungeonKitSet>();
	Recipe->Chambers.Add(FRoomChamber());

	const FRoomChamber& Chamber = Recipe->Chambers[0];

	TestTrue(TEXT("an un-overridden chamber slot is empty on the chamber itself"),
		Chamber.WallCls.ToString().IsEmpty());
	TestEqual(TEXT("...but resolves to the kit's wall"),
		Recipe->ResolveWall(Chamber).ToString(), Recipe->KitSet->Wall.ToString());
	TestEqual(TEXT("...and the kit's floor"),
		Recipe->ResolveFloor(Chamber).ToString(), Recipe->KitSet->Floor.ToString());

	// One kit asset backs all four corners. Un-overridden NW and SE resolving equal is the
	// intended behaviour, not a mapping bug -- the emitter rotates the piece.
	TestEqual(TEXT("NW resolves to the kit's corner"),
		Recipe->ResolveCorner(Chamber, RectGen::ERectCornerIndex::NW).ToString(),
		Recipe->KitSet->Corner.ToString());
	TestEqual(TEXT("SE resolves to the same corner asset"),
		Recipe->ResolveCorner(Chamber, RectGen::ERectCornerIndex::SE).ToString(),
		Recipe->KitSet->Corner.ToString());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRecipeOverrideBeatsKitSet,
	"ProceduralDungeon.RoomAuthoring.KitSet.AFilledChamberSlotOverridesTheKit",
	KitSetTestFlags)

bool FRecipeOverrideBeatsKitSet::RunTest(const FString&)
{
	URoomRecipeAsset* Recipe = NewObject<URoomRecipeAsset>();
	Recipe->KitSet = NewObject<UDungeonKitSet>();
	Recipe->Chambers.Add(FRoomChamber());
	Recipe->Chambers[0].WallCls = FSoftClassPath(SomeOtherWall);
	Recipe->Chambers[0].CornerSWCls = FSoftClassPath(SomeOtherWall);

	const FRoomChamber& Chamber = Recipe->Chambers[0];

	TestEqual(TEXT("the override wins for the wall"),
		Recipe->ResolveWall(Chamber).ToString(), FString(SomeOtherWall));
	TestEqual(TEXT("overriding one corner does not disturb the others"),
		Recipe->ResolveCorner(Chamber, RectGen::ERectCornerIndex::NW).ToString(),
		Recipe->KitSet->Corner.ToString());
	TestEqual(TEXT("...while the overridden corner takes the override"),
		Recipe->ResolveCorner(Chamber, RectGen::ERectCornerIndex::SW).ToString(),
		FString(SomeOtherWall));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRecipeWithNoKitSetKeepsItsOwnValues,
	"ProceduralDungeon.RoomAuthoring.KitSet.ARecipeWithNoKitSetResolvesToItsOwnSlots",
	KitSetTestFlags)

bool FRecipeWithNoKitSetKeepsItsOwnValues::RunTest(const FString&)
{
	// The migration story, and the fallback that makes an authored kit asset optional.
	//
	// A recipe saved before kit sets existed has a null KitSet and its own chamber pieces. Those
	// must keep resolving to exactly what was saved -- the same guarantee AttachParent's -1
	// default gives attachments -- while the slots it never had fall back to the built-in
	// census kit rather than to nothing.
	//
	// That fallback is not a convenience. Without it, a null KitSet resolves every unfilled slot
	// to an empty path, and since a pre-kit-set recipe carries no exit fills at all, EVERY such
	// room is refused by the bake for a missing Door -- which is exactly what
	// DA_RoomRecipe_Sample_TwoChamber did.
	URoomRecipeAsset* Recipe = NewObject<URoomRecipeAsset>();
	Recipe->Chambers.Add(FRoomChamber());
	Recipe->Chambers[0].WallCls = FSoftClassPath(SomeOtherWall);

	TestTrue(TEXT("no kit set is a legal state"), Recipe->KitSet == nullptr);
	TestEqual(TEXT("the chamber's own value still wins"),
		Recipe->ResolveWall(Recipe->Chambers[0]).ToString(), FString(SomeOtherWall));

	const UDungeonKitSet* Census = GetDefault<UDungeonKitSet>();
	TestEqual(TEXT("an unset slot falls back to the built-in kit"),
		Recipe->ResolveFloor(Recipe->Chambers[0]).ToString(), Census->Floor.ToString());
	TestEqual(TEXT("and so do the exit fills a legacy recipe never had"),
		Recipe->ResolveDoor().ToString(), Census->Door.ToString());
	TestEqual(TEXT("including the cap, which is not the door"),
		Recipe->ResolveWallCap().ToString(), Census->WallCap.ToString());
	TestNotEqual(TEXT("door and cap stay distinct through the fallback"),
		Recipe->ResolveDoor().ToString(), Recipe->ResolveWallCap().ToString());

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
