// Does a room the bake actually WROTE satisfy the contract the bake targets?
//
// Every other test in this module checks an input or a piece of arithmetic. This one opens the
// output. It is the only thing that can catch a bake that runs, reports success, writes assets,
// and produces rooms the generator cannot place -- which is a failure with no symptom until
// someone tries to build a dungeon out of them.
//
// Points at whatever is in the room library rather than at a fixture, so it checks the rooms
// that exist rather than ones it made itself. With an empty library it passes vacuously and
// says so, because a bake nobody has run yet is not a failure.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "AssetRegistry/ARFilter.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Components/ArrowComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/Blueprint.h"
#include "Engine/InheritableComponentHandler.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Modules/ModuleManager.h"
#include "RectDungeon/RectRoomTools.h"

namespace
{
	constexpr EAutomationTestFlags BakedRoomTestFlags =
		EAutomationTestFlags::EditorContext |
		EAutomationTestFlags::EngineFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBakedRoomsSatisfyTheContract,
	"ProceduralDungeon.RoomAuthoring.Contract.BakedRoomsSatisfyTheMasterRoomContract",
	BakedRoomTestFlags)

bool FBakedRoomsSatisfyTheContract::RunTest(const FString&)
{
	IAssetRegistry& Registry =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
	Registry.ScanPathsSynchronous({ URectRoomTools::RoomLibraryRoot() }, /*bForceRescan=*/true);

	FARFilter Filter;
	Filter.PackagePaths.Add(FName(URectRoomTools::RoomLibraryRoot()));
	Filter.bRecursivePaths = true;
	Filter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());

	TArray<FAssetData> Assets;
	Registry.GetAssets(Filter, Assets);

	int32 Checked = 0;
	for (const FAssetData& Asset : Assets)
	{
		if (!Asset.AssetName.ToString().StartsWith(TEXT("BP_Room_"))) { continue; }

		UBlueprint* BP = Cast<UBlueprint>(Asset.GetAsset());
		if (BP == nullptr) { continue; }
		++Checked;

		const FString Name = Asset.AssetName.ToString();

		// Derives from Master_Room, or the generator will not treat it as a room at all.
		const FString Parent = BP->ParentClass ? BP->ParentClass->GetName() : TEXT("<null>");
		TestEqual(*FString::Printf(TEXT("%s derives from Master_Room_C"), *Name),
			Parent, FString(TEXT("Master_Room_C")));

		if (BP->SimpleConstructionScript == nullptr)
		{
			AddError(FString::Printf(TEXT("%s has no SCS"), *Name));
			continue;
		}

		// Where did the bake hang its work? Every added node is a root of THIS Blueprint's SCS
		// and names its real parent through ParentComponentOrVariableName -- the distinction
		// that, missed, puts every exit arrow at the actor root where nothing looks for it.
		int32 Exits = 0, FloorPoints = 0, Geometry = 0, Misparented = 0;
		bool bEntranceAtOrigin = false;
		for (const USCS_Node* Node : BP->SimpleConstructionScript->GetAllNodes())
		{
			if (Node == nullptr) { continue; }
			const FString NodeName = Node->GetVariableName().ToString();
			const FString ParentName = Node->ParentComponentOrVariableName.ToString();

			if (NodeName.StartsWith(TEXT("Exit_")))
			{
				++Exits;
				if (ParentName != TEXT("Exits Folder")) { ++Misparented; }
				if (const USceneComponent* S = Cast<USceneComponent>(Node->ComponentTemplate))
				{
					if (S->GetRelativeLocation().SizeSquared() < 1.0) { bEntranceAtOrigin = true; }
				}
			}
			else if (NodeName.StartsWith(TEXT("FloorPoint_")))
			{
				++FloorPoints;
				if (ParentName != TEXT("FloorSpawnPoints")) { ++Misparented; }
			}
			else if (NodeName.StartsWith(TEXT("Piece_")) || NodeName.StartsWith(TEXT("Fixture_")))
			{
				++Geometry;
				if (ParentName != TEXT("GeometryFolder")) { ++Misparented; }
			}
		}

		AddInfo(FString::Printf(TEXT("%s: %d exit(s), %d floor point(s), %d geometry node(s)"),
			*Name, Exits, FloorPoints, Geometry));

		TestEqual(*FString::Printf(TEXT("%s attaches everything to the right folder"), *Name),
			Misparented, 0);
		TestTrue(*FString::Printf(TEXT("%s has at least one exit arrow"), *Name), Exits > 0);
		TestTrue(*FString::Printf(TEXT("%s has geometry"), *Name), Geometry > 0);

		// THE structural rule: the entrance is the piece's origin. A room pivoted anywhere else
		// drops part of itself onto the doorway when the generator spawns it at an exit.
		TestTrue(*FString::Printf(TEXT("%s puts an exit arrow on the origin"), *Name),
			bEntranceAtOrigin);

		// The footprint must have been resized off Master_Room's placeholder, and must still be
		// on the RoomOverlap channel.
		bool bSawBox = false;
		if (BP->InheritableComponentHandler != nullptr)
		{
			TArray<UActorComponent*> Templates;
			BP->InheritableComponentHandler->GetAllTemplates(Templates);
			for (const UActorComponent* T : Templates)
			{
				const UBoxComponent* Box = Cast<UBoxComponent>(T);
				if (Box == nullptr) { continue; }
				bSawBox = true;

				const FVector S = Box->GetRelativeTransform().GetScale3D();
				AddInfo(FString::Printf(TEXT("  %s Overlap_Box scale=(%.0f, %.0f, %.0f) loc=(%.0f, %.0f, %.0f) objectType=%d"),
					*Name, S.X, S.Y, S.Z,
					Box->GetRelativeLocation().X, Box->GetRelativeLocation().Y,
					Box->GetRelativeLocation().Z,
					static_cast<int32>(Box->GetCollisionObjectType())));

				TestEqual(*FString::Printf(TEXT("%s keeps Overlap_Box on RoomOverlap"), *Name),
					static_cast<int32>(Box->GetCollisionObjectType()),
					static_cast<int32>(ECC_GameTraceChannel1));
				TestTrue(*FString::Printf(TEXT("%s resized Overlap_Box off the placeholder"), *Name),
					!FMath::IsNearlyEqual(S.X, 30.0) || !FMath::IsNearlyEqual(S.Y, 30.0));
			}
		}
		TestTrue(*FString::Printf(TEXT("%s overrides Overlap_Box at all"), *Name), bSawBox);
	}

	if (Checked == 0)
	{
		AddInfo(TEXT("No baked rooms in the library yet -- nothing to check. Run a bake first."));
	}
	else
	{
		AddInfo(FString::Printf(TEXT("checked %d baked room(s)"), Checked));
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
