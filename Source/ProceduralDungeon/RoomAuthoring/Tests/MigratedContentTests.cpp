// Does the migrated authoring content actually load in this project?
//
// A copied .uasset that names a class from the project it came from loads as NULL and says
// nothing useful about why. These are the assets the room-authoring tool cannot run without, so
// the question gets asked here rather than discovered when a button does nothing.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Engine/Blueprint.h"
#include "RoomAuthoring/RoomAuthorTools.h"
#include "RoomAuthoring/RoomRecipeAsset.h"
#include "UObject/UObjectGlobals.h"

namespace
{
	constexpr EAutomationTestFlags MigratedTestFlags =
		EAutomationTestFlags::EditorContext |
		EAutomationTestFlags::EngineFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMigratedAuthoringContentLoads,
	"ProceduralDungeon.RoomAuthoring.Content.TheAuthoringAssetsLoadInThisProject",
	MigratedTestFlags)

bool FMigratedAuthoringContentLoads::RunTest(const FString&)
{
	// The emitter. GenerateRoom refuses outright when this does not load, so a missing graph is
	// at least loud -- but only at the moment someone presses Regenerate.
	TestNotNull(TEXT("the PCG emitter loads"),
		LoadObject<UObject>(nullptr, URoomAuthorTools::RoomGenGraphPath()));

	// The piece tables. These are scanned rather than named, so a table that failed to load is
	// simply a role that vanishes from the panel with no error at all.
	const TArray<FString> Roles = URoomAuthorTools::GetPieceRoles();
	TestTrue(TEXT("piece-table roles are discoverable"), Roles.Num() > 0);
	AddInfo(FString::Printf(TEXT("roles found: %s"), *FString::Join(Roles, TEXT(", "))));

	for (const FString& Role : Roles)
	{
		const TArray<FRoomAuthorPieceOption> Options =
			URoomAuthorTools::GetPieceOptions(Role, /*bWantMeshes=*/false);
		const TArray<FRoomAuthorPieceOption> Meshes =
			URoomAuthorTools::GetPieceOptions(Role, /*bWantMeshes=*/true);
		AddInfo(FString::Printf(TEXT("  %-16s %d class row(s), %d mesh row(s)"),
			*Role, Options.Num(), Meshes.Num()));
	}

	// The sample recipe. THIS is the one that needed a CoreRedirect: it was authored against
	// Level_Creator_1's module and names /Script/Level_Creator_1.RoomRecipeAsset inside. Without
	// the redirect it loads as null, which is indistinguishable from the file being absent.
	URoomRecipeAsset* Sample = LoadObject<URoomRecipeAsset>(nullptr,
		TEXT("/Game/RectDungeon/Authoring/DA_RoomRecipe_Sample_TwoChamber.DA_RoomRecipe_Sample_TwoChamber"));
	if (TestNotNull(TEXT("the sample recipe loads through the CoreRedirect"), Sample))
	{
		AddInfo(FString::Printf(TEXT("sample recipe: %s / %s, %dx%d, %d chamber(s)"),
			*Sample->RoomName.ToString(), *Sample->RoomType.ToString(),
			Sample->BoundingWidth, Sample->BoundingLength, Sample->Chambers.Num()));

		// It came from a project where every chamber carried its own pieces, so it should still
		// validate here -- the kit set is an addition, not a requirement.
		FString Error;
		if (!TestTrue(TEXT("the sample recipe still validates"), Sample->ValidateRecipe(Error)))
		{
			AddError(Error);
		}
	}

	// The widget and the authoring level, which step 7 rewires.
	TestNotNull(TEXT("EUW_RoomAuthor loads"), LoadObject<UBlueprint>(nullptr,
		TEXT("/Game/RectDungeon/Authoring/EUW_RoomAuthor.EUW_RoomAuthor")));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
