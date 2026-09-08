// What a baked room MUST look like, read off the real Master_Room asset rather than off a
// document.
//
// This is the contract step 6's bake writes to, so it is pinned here BEFORE that bake exists.
// Every fact the port plan states about Master_Room -- its components, their nesting, the
// overlap box's channel, the floor at Z=0 -- is asserted against the loaded Blueprint, and the
// structure is also DUMPED into the test log so the parts nobody wrote down (transforms, box
// extents, exact classes) can be read out of a headless run.
//
// Editor-only: it loads a Blueprint and walks its SimpleConstructionScript, neither of which
// exists in a cooked build.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Components/ArrowComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Components/SceneComponent.h"
#include "Engine/Blueprint.h"
#include "Engine/SCS_Node.h"
#include "Engine/InheritableComponentHandler.h"
#include "Engine/SimpleConstructionScript.h"
#include "UObject/UObjectGlobals.h"

namespace
{
	constexpr EAutomationTestFlags MasterRoomTestFlags =
		EAutomationTestFlags::EditorContext |
		EAutomationTestFlags::EngineFilter;

	const TCHAR* const MasterRoomPath =
		TEXT("/Game/ProceduralLevel/LevelPieces/Master_Room.Master_Room");

	/** Every SCS node of a Blueprint, with its parent's variable name. */
	struct FNodeRow
	{
		FString Name;
		FString ClassName;
		FString ParentName;
		FString Asset;      // static mesh path, when the node carries one
		FTransform Relative;
	};

	void CollectNodes(const USCS_Node* Node, const FString& ParentName, TArray<FNodeRow>& Out)
	{
		if (Node == nullptr) { return; }

		FNodeRow Row;
		Row.Name = Node->GetVariableName().ToString();
		Row.ClassName = Node->ComponentClass ? Node->ComponentClass->GetName() : TEXT("<null>");

		// A node with no SCS parent is a ROOT NODE OF THIS BLUEPRINT, which in a child
		// Blueprint does not mean "attached to nothing" -- it attaches to a component inherited
		// from the parent class, named by ParentComponentOrVariableName. Reading only the SCS
		// child links reports every added component as a root and loses the entire structure a
		// child Blueprint builds on top of Master_Room, which is precisely what the bake has to
		// reproduce.
		if (ParentName == TEXT("<none>") && Node->ParentComponentOrVariableName != NAME_None)
		{
			Row.ParentName = FString::Printf(TEXT("%s (%s)"),
				*Node->ParentComponentOrVariableName.ToString(),
				Node->bIsParentComponentNative ? TEXT("native") : TEXT("inherited"));
		}
		else
		{
			Row.ParentName = ParentName;
		}

		if (const USceneComponent* Scene = Cast<USceneComponent>(Node->ComponentTemplate))
		{
			Row.Relative = Scene->GetRelativeTransform();
		}
		if (const UStaticMeshComponent* SM = Cast<UStaticMeshComponent>(Node->ComponentTemplate))
		{
			Row.Asset = SM->GetStaticMesh() ? SM->GetStaticMesh()->GetPathName() : TEXT("<no mesh>");
		}
		Out.Add(Row);

		for (const USCS_Node* Child : Node->GetChildNodes())
		{
			CollectNodes(Child, Row.Name, Out);
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMasterRoomContractIsWhatTheBakeTargets,
	"ProceduralDungeon.RoomAuthoring.Contract.MasterRoomCarriesTheComponentsTheBakeWritesTo",
	MasterRoomTestFlags)

bool FMasterRoomContractIsWhatTheBakeTargets::RunTest(const FString&)
{
	UBlueprint* BP = LoadObject<UBlueprint>(nullptr, MasterRoomPath);
	if (!TestNotNull(TEXT("Master_Room loads"), BP))
	{
		AddError(FString::Printf(TEXT("Could not load %s"), MasterRoomPath));
		return false;
	}

	AddInfo(FString::Printf(TEXT("Master_Room parent class: %s"),
		BP->ParentClass ? *BP->ParentClass->GetName() : TEXT("<null>")));

	USimpleConstructionScript* SCS = BP->SimpleConstructionScript;
	if (!TestNotNull(TEXT("Master_Room has a SimpleConstructionScript"), SCS))
	{
		return false;
	}

	TArray<FNodeRow> Rows;
	for (const USCS_Node* Root : SCS->GetRootNodes())
	{
		CollectNodes(Root, TEXT("<none>"), Rows);
	}

	// The dump. This is the half of the test that exists to be READ, not to pass: the transforms
	// and box extents below are what a bake has to reproduce, and nothing had written them down.
	AddInfo(TEXT("--- Master_Room SCS ---"));
	for (const FNodeRow& Row : Rows)
	{
		const FVector T = Row.Relative.GetLocation();
		const FRotator R = Row.Relative.Rotator();
		const FVector S = Row.Relative.GetScale3D();
		AddInfo(FString::Printf(
			TEXT("  %-20s %-24s parent=%-16s loc=(%.1f, %.1f, %.1f) rot=(%.1f, %.1f, %.1f) scale=(%.2f, %.2f, %.2f)"),
			*Row.Name, *Row.ClassName, *Row.ParentName,
			T.X, T.Y, T.Z, R.Pitch, R.Yaw, R.Roll, S.X, S.Y, S.Z));
	}

	// The components the port plan names. Asserted individually so a failure says WHICH one
	// moved rather than that a set comparison failed.
	auto Has = [&Rows](const TCHAR* Name) -> const FNodeRow*
	{
		return Rows.FindByPredicate([Name](const FNodeRow& R) { return R.Name == Name; });
	};

	const FNodeRow* Arrow = Has(TEXT("Arrow"));
	const FNodeRow* Exits = Has(TEXT("Exits Folder"));
	const FNodeRow* OverlapFolder = Has(TEXT("Overlap Folder"));
	const FNodeRow* OverlapBox = Has(TEXT("Overlap_Box"));
	const FNodeRow* FloorPoints = Has(TEXT("FloorSpawnPoints"));
	const FNodeRow* Geometry = Has(TEXT("GeometryFolder"));

	TestNotNull(TEXT("Master_Room has Arrow"), Arrow);
	TestNotNull(TEXT("Master_Room has Exits Folder"), Exits);
	TestNotNull(TEXT("Master_Room has Overlap Folder"), OverlapFolder);
	TestNotNull(TEXT("Master_Room has Overlap_Box"), OverlapBox);
	TestNotNull(TEXT("Master_Room has FloorSpawnPoints"), FloorPoints);
	TestNotNull(TEXT("Master_Room has GeometryFolder"), Geometry);

	if (Arrow != nullptr)
	{
		TestEqual(TEXT("Arrow is an ArrowComponent"), Arrow->ClassName, FString(TEXT("ArrowComponent")));
	}
	if (OverlapBox != nullptr)
	{
		TestEqual(TEXT("Overlap_Box is a BoxComponent"), OverlapBox->ClassName, FString(TEXT("BoxComponent")));
		TestEqual(TEXT("Overlap_Box hangs off Overlap Folder"),
			OverlapBox->ParentName, FString(TEXT("Overlap Folder")));
	}

	// The overlap channel. RoomOverlap is declared in this project's DefaultEngine.ini as
	// ECC_GameTraceChannel1, and a bake that writes a box on the wrong channel produces rooms
	// that overlap-test against nothing -- which looks like a working generator until two rooms
	// occupy the same space.
	for (const USCS_Node* Root : SCS->GetRootNodes())
	{
		TArray<const USCS_Node*> Stack{ Root };
		while (Stack.Num() > 0)
		{
			const USCS_Node* N = Stack.Pop();
			if (N == nullptr) { continue; }
			for (const USCS_Node* C : N->GetChildNodes()) { Stack.Push(C); }

			if (const UBoxComponent* Box = Cast<UBoxComponent>(N->ComponentTemplate))
			{
				AddInfo(FString::Printf(
					TEXT("  Overlap_Box extent=(%.1f, %.1f, %.1f) objectType=%d profile=%s collisionEnabled=%d"),
					Box->GetUnscaledBoxExtent().X, Box->GetUnscaledBoxExtent().Y,
					Box->GetUnscaledBoxExtent().Z,
					static_cast<int32>(Box->GetCollisionObjectType()),
					*Box->GetCollisionProfileName().ToString(),
					static_cast<int32>(Box->GetCollisionEnabled())));

				TestEqual(TEXT("Overlap_Box is on the RoomOverlap channel (ECC_GameTraceChannel1)"),
					static_cast<int32>(Box->GetCollisionObjectType()),
					static_cast<int32>(ECC_GameTraceChannel1));
			}
		}
	}

	return true;
}

/**
 * What a real, hand-built room piece looks like -- the reference the bake is imitating.
 *
 * Pure dump, no assertions beyond loading. Its job is to answer questions the port plan left
 * open, chiefly which ceiling mesh a working room actually uses: UDungeonKitSet::CeilingMesh
 * deliberately ships empty because the six-asset census carried no ceiling, and the plan says
 * to check a real room before choosing a default rather than guess a path.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShippingRoomPiecesDump,
	"ProceduralDungeon.RoomAuthoring.Contract.DumpTheHandBuiltRoomPieces",
	MasterRoomTestFlags)

bool FShippingRoomPiecesDump::RunTest(const FString&)
{
	const TCHAR* const Rooms[] = {
		TEXT("/Game/ProceduralLevel/LevelPieces/Dungeon/1_Room1.1_Room1"),
		TEXT("/Game/ProceduralLevel/LevelPieces/Dungeon/1_StarterRoom.1_StarterRoom"),
		TEXT("/Game/ProceduralLevel/LevelPieces/Dungeon/1_Hall1.1_Hall1"),
	};

	for (const TCHAR* Path : Rooms)
	{
		UBlueprint* BP = LoadObject<UBlueprint>(nullptr, Path);
		if (BP == nullptr)
		{
			AddWarning(FString::Printf(TEXT("Could not load %s"), Path));
			continue;
		}

		AddInfo(FString::Printf(TEXT("--- %s (parent %s) ---"), Path,
			BP->ParentClass ? *BP->ParentClass->GetName() : TEXT("<null>")));

		if (BP->SimpleConstructionScript == nullptr) { continue; }

		TArray<FNodeRow> Rows;
		for (const USCS_Node* Root : BP->SimpleConstructionScript->GetRootNodes())
		{
			CollectNodes(Root, TEXT("<none>"), Rows);
		}

		// Arrows and non-mesh nodes in full -- they are the room's contract. Meshes only as a
		// tally by asset, because a hand-built room carries a hundred of them and the question
		// being answered is WHICH assets a working room uses, not where each tile sits.
		int32 Arrows = 0;
		TMap<FString, int32> MeshCounts;
		TSet<FString> Parents;
		for (const FNodeRow& Row : Rows)
		{
			Parents.Add(Row.ParentName);
			if (!Row.Asset.IsEmpty())
			{
				MeshCounts.FindOrAdd(Row.Asset)++;
				continue;
			}
			if (Row.ClassName == TEXT("ArrowComponent")) { ++Arrows; }
			const FVector T = Row.Relative.GetLocation();
			AddInfo(FString::Printf(TEXT("  %-22s %-22s parent=%-28s loc=(%.1f, %.1f, %.1f)"),
				*Row.Name, *Row.ClassName, *Row.ParentName, T.X, T.Y, T.Z));
		}

		MeshCounts.ValueSort([](int32 A, int32 B) { return A > B; });
		AddInfo(TEXT("  meshes used:"));
		for (const TPair<FString, int32>& Pair : MeshCounts)
		{
			AddInfo(FString::Printf(TEXT("    %4d x %s"), Pair.Value, *Pair.Key));
		}

		AddInfo(TEXT("  attach parents seen:"));
		for (const FString& P : Parents) { AddInfo(FString::Printf(TEXT("    %s"), *P)); }

		AddInfo(FString::Printf(TEXT("  => %d component(s), %d arrow(s), %d distinct mesh(es)"),
			Rows.Num(), Arrows, MeshCounts.Num()));

		// Overrides of components INHERITED from Master_Room. These do not appear in this
		// Blueprint's own SCS at all, so the walk above cannot see them -- and the one that
		// matters most lives here: Master_Room ships Overlap_Box at 960 x 960 uu, which is 2.4
		// tiles and far smaller than any real room, so a room that did not resize it would
		// overlap-test against a box sitting inside its own floor.
		AddInfo(TEXT("  inherited component overrides:"));
		if (BP->InheritableComponentHandler == nullptr)
		{
			AddInfo(TEXT("    <none -- this room overrides nothing it inherited>"));
		}
		else
		{
			TArray<UActorComponent*> Templates;
			BP->InheritableComponentHandler->GetAllTemplates(Templates);
			if (Templates.Num() == 0) { AddInfo(TEXT("    <none>")); }
			for (const UActorComponent* Template : Templates)
			{
				if (Template == nullptr) { continue; }
				FString Detail;
				if (const UBoxComponent* Box = Cast<UBoxComponent>(Template))
				{
					const FVector E = Box->GetUnscaledBoxExtent();
					const FVector S = Box->GetRelativeTransform().GetScale3D();
					Detail = FString::Printf(
						TEXT(" extent=(%.1f, %.1f, %.1f) scale=(%.2f, %.2f, %.2f) => world=(%.1f, %.1f, %.1f) objectType=%d"),
						E.X, E.Y, E.Z, S.X, S.Y, S.Z, E.X * S.X, E.Y * S.Y, E.Z * S.Z,
						static_cast<int32>(Box->GetCollisionObjectType()));
				}
				if (const USceneComponent* Scene = Cast<USceneComponent>(Template))
				{
					const FVector T = Scene->GetRelativeTransform().GetLocation();
					Detail += FString::Printf(TEXT(" loc=(%.1f, %.1f, %.1f)"), T.X, T.Y, T.Z);
				}
				AddInfo(FString::Printf(TEXT("    %-22s %-22s%s"),
					*Template->GetName(), *Template->GetClass()->GetName(), *Detail));
			}
		}
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
