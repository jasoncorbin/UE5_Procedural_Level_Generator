#include "RoomAuthoring/RoomAuthorTools.h"

#include "RectDungeon/RectRoomTools.h"
#include "RoomAuthoring/RoomAuthorValidate.h"

#include "Components/AudioComponent.h"
#include "Components/ChildActorComponent.h"
#include "Components/DecalComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/LightComponentBase.h"
#include "Components/StaticMeshComponent.h"
// UFXSystemComponent, the shared base of Niagara's UNiagaraComponent and Cascade's
// UParticleSystemComponent, is declared here rather than in Components/. Including it costs no
// new module dependency -- both it and the header live in Engine -- which is why the fixture
// test below can recognise a Niagara flame without this module depending on Niagara.
#include "Particles/ParticleSystemComponent.h"
#include "Engine/Brush.h"
#include "Engine/Polys.h"
#include "Model.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "PCGComponent.h"
#include "PCGGraph.h"
#include "PCGVolume.h"
#include "Helpers/PCGHelpers.h"
#include "StructUtils/PropertyBag.h"
#include "UObject/UnrealType.h"

#if WITH_EDITOR
#include "ActorFactories/ActorFactory.h"
#include "AssetRegistry/ARFilter.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Modules/ModuleManager.h"
#include "Builders/CubeBuilder.h"
#include "Components/BrushComponent.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#endif

namespace
{
	// Every RoomAuthor:: name below is qualified deliberately. This module builds with
	// bUseUnity = false precisely because an unqualified using-directive can collide two
	// same-named symbols once translation units are merged -- see Level_Creator_1.Build.cs.

	/** Graph parameter names, indexed by RectGen::ERectSide (North=0, East=1, South=2, West=3). */
	const TCHAR* OpeningLoParam[RectGen::NumSides] = {
		TEXT("openingLoNorth"), TEXT("openingLoEast"),
		TEXT("openingLoSouth"), TEXT("openingLoWest") };
	const TCHAR* OpeningHiParam[RectGen::NumSides] = {
		TEXT("openingHiNorth"), TEXT("openingHiEast"),
		TEXT("openingHiSouth"), TEXT("openingHiWest") };
	const TCHAR* FrameParam[RectGen::NumSides] = {
		TEXT("frameNorth"), TEXT("frameEast"), TEXT("frameSouth"), TEXT("frameWest") };

	/**
	 * The prefix every DA_PieceTable_* asset shares.
	 *
	 * A NAME convention rather than a class check: BPDA_PieceTable is a Blueprint class, so its
	 * instances have no native type this module can name without hard-coding a Blueprint path --
	 * which is exactly the coupling the piece tables exist to avoid.
	 */
	const TCHAR* PieceTablePrefix = TEXT("DA_PieceTable_");

	/**
	 * The volume's brush is a 200-uu cube (PCGToolset builds one the same way), so a scale of 1
	 * is a 100 uu half-extent. Everything below converts chamber tiles into that scale.
	 */
	constexpr double BrushHalfExtentUU = 100.0;

	/** Strip a UE object-reference decoration -- Class'/Game/X.Y' -- down to the bare path. */
	FString BarePath(const FString& In)
	{
		FString S = In.TrimStartAndEnd();
		if (S.IsEmpty() || S == TEXT("None")) { return FString(); }

		const int32 Quote = S.Find(TEXT("'"));
		if (Quote != INDEX_NONE)
		{
			S = S.Mid(Quote + 1);
			S.RemoveFromEnd(TEXT("'"));
		}
		return S;
	}

	/**
	 * Every element of an array UPROPERTY, exported to text and stripped to a path.
	 *
	 * Text export rather than a typed cast because the two arrays this reads are typed
	 * differently -- Pieces is TArray<FSoftClassPath>, Meshes is a soft object array -- and a
	 * reader that knew both types would have to be edited every time a table gained a column.
	 * Returns false when the property is missing, which is a table the widget must not silently
	 * present as empty.
	 */
	bool ReadPathArray(const UObject* Object, const TCHAR* PropertyName, TArray<FString>& Out)
	{
		if (Object == nullptr) { return false; }

		FArrayProperty* Array = FindFProperty<FArrayProperty>(Object->GetClass(), PropertyName);
		if (Array == nullptr || Array->Inner == nullptr) { return false; }

		FScriptArrayHelper Helper(Array, Array->ContainerPtrToValuePtr<void>(Object));
		for (int32 I = 0; I < Helper.Num(); ++I)
		{
			FString Text;
			Array->Inner->ExportTextItem_Direct(Text, Helper.GetRawPtr(I), nullptr, nullptr, PPF_None);
			Out.Add(BarePath(Text));
		}
		return true;
	}

	/** One scalar UPROPERTY, exported to text. Empty when absent. */
	FString ReadScalarAsString(const UObject* Object, const TCHAR* PropertyName)
	{
		if (Object == nullptr) { return FString(); }

		FProperty* Property = FindFProperty<FProperty>(Object->GetClass(), PropertyName);
		if (Property == nullptr) { return FString(); }

		FString Text;
		Property->ExportTextItem_Direct(
			Text, Property->ContainerPtrToValuePtr<void>(Object), nullptr, nullptr, PPF_None);
		return Text.TrimStartAndEnd();
	}

#if WITH_EDITOR
	/** Every DA_PieceTable_* asset under AuthoringRoot(), loaded, in asset-name order. */
	TArray<UObject*> LoadPieceTables()
	{
		TArray<UObject*> Tables;

		IAssetRegistry* Registry = IAssetRegistry::Get();
		if (Registry == nullptr) { return Tables; }

		TArray<FAssetData> Assets;
		Registry->GetAssetsByPath(
			FName(URoomAuthorTools::AuthoringRoot()), Assets, /*bRecursive=*/false);

		Assets.Sort([](const FAssetData& A, const FAssetData& B)
		{
			return A.AssetName.LexicalLess(B.AssetName);
		});

		for (const FAssetData& Asset : Assets)
		{
			if (!Asset.AssetName.ToString().StartsWith(PieceTablePrefix)) { continue; }
			if (UObject* Loaded = Asset.GetAsset()) { Tables.Add(Loaded); }
		}
		return Tables;
	}

	/** The loaded table whose Role field matches, or null. */
	UObject* FindPieceTable(const FString& Role)
	{
		for (UObject* Table : LoadPieceTables())
		{
			if (ReadScalarAsString(Table, TEXT("Role")).Equals(Role, ESearchCase::IgnoreCase))
			{
				return Table;
			}
		}
		return nullptr;
	}

	/** The first resolvable entry of a role's table -- the slots the panel does not expose. */
	FString FirstPathForRole(const FString& Role, bool bWantMeshes)
	{
		const TArray<FRoomAuthorPieceOption> Options =
			URoomAuthorTools::GetPieceOptions(Role, bWantMeshes);
		return Options.Num() > 0 ? Options[0].Path : FString();
	}
#endif // WITH_EDITOR

	/** The editor world, whichever object the widget handed us. */
	UWorld* EditorWorld(UObject* WorldContextObject)
	{
		if (UWorld* FromContext = GEngine
			? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull)
			: nullptr)
		{
			return FromContext;
		}
#if WITH_EDITOR
		return GEditor ? GEditor->GetEditorWorldContext(true).World() : nullptr;
#else
		return nullptr;
#endif
	}

	/**
	 * Write one graph parameter, dispatching on the bag's OWN declared type.
	 *
	 * Reading the type rather than assuming it: the graph declares width as an integer and
	 * propDensity as a number, and whether those are Int32/Int64 and Float/Double is the
	 * graph's business, not this file's. A guess that got it wrong would return TypeMismatch
	 * and leave the previous value in place -- which generates a room, just not the one asked
	 * for, and that is the failure mode hardest to see in a screenshot.
	 */
	bool SetIntParam(UPCGGraphInstance* Instance, const TCHAR* Name, int64 Value)
	{
		if (Instance == nullptr) { return false; }

		const FName ParamName(Name);
		const FPropertyBagPropertyDesc* Desc =
			Instance->ParametersOverrides.Parameters.FindPropertyDescByName(ParamName);
		if (Desc == nullptr) { return false; }

		if (Desc->ValueType == EPropertyBagPropertyType::Int64)
		{
			return Instance->SetGraphParameter<int64>(ParamName, Value)
				== EPropertyBagResult::Success;
		}
		return Instance->SetGraphParameter<int32>(ParamName, static_cast<int32>(Value))
			== EPropertyBagResult::Success;
	}

	bool SetRealParam(UPCGGraphInstance* Instance, const TCHAR* Name, double Value)
	{
		if (Instance == nullptr) { return false; }

		const FName ParamName(Name);
		const FPropertyBagPropertyDesc* Desc =
			Instance->ParametersOverrides.Parameters.FindPropertyDescByName(ParamName);
		if (Desc == nullptr) { return false; }

		if (Desc->ValueType == EPropertyBagPropertyType::Double)
		{
			return Instance->SetGraphParameter<double>(ParamName, Value)
				== EPropertyBagResult::Success;
		}
		return Instance->SetGraphParameter<float>(ParamName, static_cast<float>(Value))
			== EPropertyBagResult::Success;
	}

	bool SetStringParam(UPCGGraphInstance* Instance, const TCHAR* Name, const FString& Value)
	{
		if (Instance == nullptr) { return false; }
		return Instance->SetGraphParameter<FString>(FName(Name), Value)
			== EPropertyBagResult::Success;
	}

	bool SetBoolParam(UPCGGraphInstance* Instance, const TCHAR* Name, bool Value)
	{
		if (Instance == nullptr) { return false; }
		return Instance->SetGraphParameter<bool>(FName(Name), Value)
			== EPropertyBagResult::Success;
	}

	/** Half-open range intersection, as used for the connection labels. */
	FString DescribeEdge(const RoomAuthor::FSharedEdge& Edge)
	{
		return FString::Printf(
			TEXT("Chamber %lld %s <-> Chamber %lld   (%lld tile%s)"),
			Edge.ChamberA, RoomAuthor::SideName(Edge.SideOfA), Edge.ChamberB,
			Edge.SpanTiles(), Edge.SpanTiles() == 1 ? TEXT("") : TEXT("s"));
	}

	/** The chambers' detected edges, or an empty list when the layout cannot produce any. */
	void DetectEdges(const URoomRecipeAsset* Recipe, RoomAuthor::FRoomLayout& OutLayout,
	                 TArray<RoomAuthor::FSharedEdge>& OutEdges)
	{
		OutEdges.Reset();
		if (Recipe == nullptr) { return; }

		Recipe->MakeLayout(OutLayout);

		// Overlapping chambers produce runs that describe nothing real -- FindSharedEdges says
		// so in its own comment -- so a layout that has not passed the earlier rules gets an
		// empty list rather than a misleading one.
		FString Ignored;
		if (!RoomAuthor::ValidateChambersInBounds(OutLayout, Ignored)) { return; }
		if (!RoomAuthor::ValidateChambersDoNotOverlap(OutLayout, Ignored)) { return; }

		RoomAuthor::FindSharedEdges(OutLayout, OutEdges);
	}
}

const TCHAR* URoomAuthorTools::AuthoringRoot()
{
	return TEXT("/Game/RectDungeon/Authoring");
}

const TCHAR* URoomAuthorTools::AuthoringLevelPath()
{
	return TEXT("/Game/RectDungeon/Authoring/L_RoomAuthoring");
}

const TCHAR* URoomAuthorTools::RoomGenGraphPath()
{
	return TEXT("/Game/PCG_Test/PCG_RoomGen_v3.PCG_RoomGen_v3");
}

const FName& URoomAuthorTools::AuthoringTag()
{
	static const FName Tag(TEXT("RoomAuthoring"));
	return Tag;
}

// ---------------------------------------------------------------------------- piece tables

TArray<FString> URoomAuthorTools::GetPieceRoles()
{
	TArray<FString> Roles;
#if WITH_EDITOR
	for (const UObject* Table : LoadPieceTables())
	{
		const FString Role = ReadScalarAsString(Table, TEXT("Role"));
		if (!Role.IsEmpty()) { Roles.AddUnique(Role); }
	}
	Roles.Sort();
#endif
	return Roles;
}

TArray<FRoomAuthorPieceOption> URoomAuthorTools::GetPieceOptions(const FString& Role,
                                                                bool bWantMeshes)
{
	TArray<FRoomAuthorPieceOption> Options;
#if WITH_EDITOR
	UObject* Table = FindPieceTable(Role);
	if (Table == nullptr) { return Options; }

	TArray<FString> Paths;
	ReadPathArray(Table, bWantMeshes ? TEXT("Meshes") : TEXT("Pieces"), Paths);

	for (const FString& Path : Paths)
	{
		if (Path.IsEmpty()) { continue; }

		// The tables do not enforce a base type, so every row is resolved before it is offered.
		// A row that will not load is skipped rather than handed to PCG, which would spawn
		// nothing and report nothing.
		if (bWantMeshes)
		{
			if (LoadObject<UStaticMesh>(nullptr, *Path) == nullptr) { continue; }
		}
		else
		{
			UClass* Loaded = LoadObject<UClass>(nullptr, *Path);
			if (Loaded == nullptr || !Loaded->IsChildOf(AActor::StaticClass())) { continue; }
		}

		FRoomAuthorPieceOption Option;
		Option.Path = Path;
		Option.DisplayName = DisplayNameForPath(Path);

		// Two rows whose class names differ only in package would otherwise present the panel
		// with two identical entries and no way to tell which one it picked.
		int32 Suffix = 2;
		const FString Base = Option.DisplayName;
		while (Options.ContainsByPredicate([&Option](const FRoomAuthorPieceOption& Existing)
			{ return Existing.DisplayName == Option.DisplayName; }))
		{
			Option.DisplayName = FString::Printf(TEXT("%s (%d)"), *Base, Suffix++);
		}

		Options.Add(Option);
	}
#endif
	return Options;
}

TArray<FString> URoomAuthorTools::GetPieceDisplayNames(const FString& Role, bool bWantMeshes)
{
	TArray<FString> Names;
	for (const FRoomAuthorPieceOption& Option : GetPieceOptions(Role, bWantMeshes))
	{
		Names.Add(Option.DisplayName);
	}
	return Names;
}

FString URoomAuthorTools::PathForDisplayName(const FString& Role, bool bWantMeshes,
                                             const FString& DisplayName)
{
	for (const FRoomAuthorPieceOption& Option : GetPieceOptions(Role, bWantMeshes))
	{
		if (Option.DisplayName == DisplayName) { return Option.Path; }
	}
	return FString();
}

FString URoomAuthorTools::GetDisplayNameForPath(const FString& ObjectPath)
{
	return DisplayNameForPath(ObjectPath);
}

TArray<FString> URoomAuthorTools::GetKnownRoomTypes()
{
	TArray<FString> Types;
#if WITH_EDITOR
	if (IAssetRegistry* Registry = IAssetRegistry::Get())
	{
		TArray<FString> Folders;
		Registry->EnumerateSubPaths(FString(URectRoomTools::RoomLibraryRoot()),
			[&Folders](FString SubPath)
			{
				Folders.Add(MoveTemp(SubPath));
				return true;
			}, /*bRecurse=*/false);

		for (const FString& Folder : Folders)
		{
			FString Leaf = Folder;
			int32 Slash = INDEX_NONE;
			if (Leaf.FindLastChar(TEXT('/'), Slash)) { Leaf = Leaf.Mid(Slash + 1); }
			if (!Leaf.IsEmpty()) { Types.AddUnique(Leaf); }
		}
		Types.Sort();
	}
#endif
	return Types;
}

FString URoomAuthorTools::DisplayNameForPath(const FString& ObjectPath)
{
	if (ObjectPath.IsEmpty()) { return FString(); }

	FString Name = ObjectPath;

	// /Game/Path/Asset.Asset_C -> Asset_C, and /Game/Path/Asset -> Asset.
	int32 Dot = INDEX_NONE;
	if (Name.FindLastChar(TEXT('.'), Dot)) { Name = Name.Mid(Dot + 1); }
	int32 Slash = INDEX_NONE;
	if (Name.FindLastChar(TEXT('/'), Slash)) { Name = Name.Mid(Slash + 1); }

	Name.RemoveFromEnd(TEXT("_C"));
	if (!Name.RemoveFromStart(TEXT("BP_COMP_")))
	{
		Name.RemoveFromStart(TEXT("MOD_"));
	}
	return Name;
}

// ---------------------------------------------------------------------------- recipe state

URoomRecipeAsset* URoomAuthorTools::NewWorkingRecipe()
{
	URoomRecipeAsset* Recipe = NewObject<URoomRecipeAsset>(
		GetTransientPackage(), URoomRecipeAsset::StaticClass(), NAME_None, RF_Transient);
	return Recipe;
}

void URoomAuthorTools::SetRoomFields(URoomRecipeAsset* Recipe, FName RoomName, FName RoomType,
                                     int32 BoundingWidth, int32 BoundingLength,
                                     bool bExitNorth, bool bExitEast,
                                     bool bExitSouth, bool bExitWest,
                                     int32 Seed, int32 Weight)
{
	if (Recipe == nullptr) { return; }

	Recipe->RoomName = RoomName;
	Recipe->RoomType = RoomType;
	Recipe->BoundingWidth = FMath::Max(1, BoundingWidth);
	Recipe->BoundingLength = FMath::Max(1, BoundingLength);
	Recipe->bExitNorth = bExitNorth;
	Recipe->bExitEast = bExitEast;
	Recipe->bExitSouth = bExitSouth;
	Recipe->bExitWest = bExitWest;
	Recipe->Seed = static_cast<int64>(Seed);
	Recipe->Weight = FMath::Max(1, Weight);
	Recipe->MarkPackageDirty();
}

void URoomAuthorTools::GetRoomFields(const URoomRecipeAsset* Recipe, FName& RoomName,
                                     FName& RoomType, int32& BoundingWidth, int32& BoundingLength,
                                     bool& bExitNorth, bool& bExitEast,
                                     bool& bExitSouth, bool& bExitWest,
                                     int32& Seed, int32& Weight)
{
	if (Recipe == nullptr) { return; }

	RoomName = Recipe->RoomName;
	RoomType = Recipe->RoomType;
	BoundingWidth = Recipe->BoundingWidth;
	BoundingLength = Recipe->BoundingLength;
	bExitNorth = Recipe->bExitNorth;
	bExitEast = Recipe->bExitEast;
	bExitSouth = Recipe->bExitSouth;
	bExitWest = Recipe->bExitWest;
	Seed = static_cast<int32>(Recipe->Seed);
	Weight = Recipe->Weight;
}

namespace
{
	/**
	 * The one place the recipe library's root lives.
	 *
	 * Save files into it, the listing scans it, and the authored bake writes both of its assets
	 * into it. Deferred to URectRoomTools rather than spelled again here because recipes and the
	 * rooms they bake into share a folder BY DESIGN -- DA_RoomRecipe_X, DA_Room_X and BP_Room_X
	 * sit together under Rooms/<RoomType>/. Two literals that must agree, in two modules, with
	 * nothing comparing them, is how a room gets saved somewhere the generator never scans.
	 */
	const TCHAR* RecipeLibraryRoot() { return URectRoomTools::RoomLibraryRoot(); }

	/** DA_RoomRecipe_<RoomName> -- the prefix Save writes and the listing strips back off. */
	const TCHAR* RecipeAssetPrefix = TEXT("DA_RoomRecipe_");

#if WITH_EDITOR
	/**
	 * One scan of the library, producing the labels and the object paths they resolve to in a
	 * single pass and a single order.
	 *
	 * Reads the asset registry only -- no recipe is loaded to be listed, so opening the combo
	 * on a large library costs nothing. Both outputs are always the same length; index i of one
	 * belongs to index i of the other, and callers rely on that rather than re-deriving a path
	 * from a label.
	 */
	void CollectLibraryRecipes(TArray<FString>& OutLabels, TArray<FString>& OutPaths)
	{
		OutLabels.Reset();
		OutPaths.Reset();

		FAssetRegistryModule& Module =
			FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));

		FARFilter Filter;
		Filter.ClassPaths.Add(URoomRecipeAsset::StaticClass()->GetClassPathName());
		Filter.PackagePaths.Add(FName(RecipeLibraryRoot()));
		Filter.bRecursivePaths = true;

		TArray<FAssetData> Found;
		Module.Get().GetAssets(Filter, Found);

		// Sorted by package name so the combo's order is stable across sessions. The registry's
		// own order is discovery order, which changes when assets are added or the cache is
		// rebuilt, and a combo whose rows move between sessions is a combo that gets misclicked.
		Found.Sort([](const FAssetData& A, const FAssetData& B)
		{
			return A.PackageName.LexicalLess(B.PackageName);
		});

		for (const FAssetData& Data : Found)
		{
			// /Game/RectDungeon/Rooms/<Type>/DA_RoomRecipe_<Name> -> "<Type>/<Name>", which is
			// the shape Save files by. Derived from the path rather than the asset's own fields
			// on purpose -- see the header.
			FString Type = Data.PackagePath.ToString();
			Type.RemoveFromStart(RecipeLibraryRoot());
			Type.RemoveFromStart(TEXT("/"));

			FString Name = Data.AssetName.ToString();
			Name.RemoveFromStart(RecipeAssetPrefix);

			OutLabels.Add(Type.IsEmpty() ? Name : FString::Printf(TEXT("%s/%s"), *Type, *Name));
			OutPaths.Add(Data.GetSoftObjectPath().ToString());
		}
	}
#endif
}

bool URoomAuthorTools::SaveRecipeToLibrary(const URoomRecipeAsset* Recipe, FString& OutSavedPath,
                                          FString& OutStatus)
{
#if WITH_EDITOR
	if (Recipe == nullptr)
	{
		OutStatus = TEXT("REFUSED - there is no recipe to save.");
		return false;
	}

	const FString RoomName = Recipe->RoomName.ToString();
	const FString RoomType = Recipe->RoomType.ToString();

	// Both are path components. An empty one does not produce a badly named asset -- it
	// produces an asset at a path nobody will ever browse to again.
	if (RoomName.IsEmpty() || RoomName == TEXT("None"))
	{
		OutStatus = TEXT("REFUSED - the room has no name, and the name is part of its path.");
		return false;
	}
	if (RoomType.IsEmpty() || RoomType == TEXT("None"))
	{
		OutStatus = TEXT("REFUSED - the room has no type, and the type is the folder it files under.");
		return false;
	}

	// Refuse to file an unbuildable room. Save is the boundary the library scan reads from,
	// and a recipe that cannot generate is one the next session opens only to be refused by.
	FString LayoutError;
	if (!Recipe->ValidateRecipe(LayoutError))
	{
		OutStatus = FString::Printf(TEXT("REFUSED - %s"), *LayoutError);
		return false;
	}

	const FString AssetName = FString::Printf(TEXT("%s%s"), RecipeAssetPrefix, *RoomName);
	const FString PackageName = FString::Printf(TEXT("%s/%s/%s"),
		RecipeLibraryRoot(), *RoomType, *AssetName);

	UPackage* Package = CreatePackage(*PackageName);
	if (Package == nullptr)
	{
		OutStatus = FString::Printf(TEXT("REFUSED - could not create the package %s."), *PackageName);
		return false;
	}
	Package->FullyLoad();

	// Overwrite in place when it already exists, so the asset keeps its identity and anything
	// already referencing it survives the re-save. A NewObject over a live asset would leave
	// every existing reference pointing at the object it replaced.
	URoomRecipeAsset* Saved = FindObject<URoomRecipeAsset>(Package, *AssetName);
	const bool bIsNew = (Saved == nullptr);
	if (bIsNew)
	{
		Saved = NewObject<URoomRecipeAsset>(Package, URoomRecipeAsset::StaticClass(),
			FName(*AssetName), RF_Public | RF_Standalone);
	}
	if (Saved == nullptr)
	{
		OutStatus = FString::Printf(TEXT("REFUSED - could not create %s."), *AssetName);
		return false;
	}

	// Property-for-property off the reflection data, so a field added to the recipe is carried
	// without this function being edited -- a hand-written field list is how a saved room
	// silently loses whatever was added last. Both objects are the same class, so a straight
	// per-property copy is exact; only the VALUES cross, never the working recipe's transient
	// object flags.
	for (TFieldIterator<FProperty> It(URoomRecipeAsset::StaticClass()); It; ++It)
	{
		It->CopyCompleteValue_InContainer(Saved, Recipe);
	}

	if (bIsNew) { FAssetRegistryModule::AssetCreated(Saved); }
	Saved->MarkPackageDirty();

	const FString FileName = FPackageName::LongPackageNameToFilename(
		PackageName, FPackageName::GetAssetPackageExtension());

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_NoError;

	if (!UPackage::SavePackage(Package, Saved, *FileName, SaveArgs))
	{
		OutStatus = FString::Printf(TEXT("REFUSED - %s did not write to disk."), *FileName);
		return false;
	}

	OutSavedPath = PackageName;
	OutStatus = FString::Printf(TEXT("OK - %s %s. Geometry is NOT baked yet."),
		bIsNew ? TEXT("wrote") : TEXT("updated"), *PackageName);
	return true;
#else
	OutStatus = TEXT("REFUSED - room authoring is an editor-only tool.");
	return false;
#endif
}

TArray<FString> URoomAuthorTools::GetLibraryRecipeLabels()
{
#if WITH_EDITOR
	TArray<FString> Labels;
	TArray<FString> Paths;
	CollectLibraryRecipes(Labels, Paths);
	return Labels;
#else
	return TArray<FString>();
#endif
}

bool URoomAuthorTools::LoadRecipeFromLibrary(const FString& Label, URoomRecipeAsset* Working,
                                             FString& OutStatus)
{
#if WITH_EDITOR
	if (Working == nullptr)
	{
		OutStatus = TEXT("REFUSED - there is no working recipe to load into.");
		return false;
	}

	TArray<FString> Labels;
	TArray<FString> Paths;
	CollectLibraryRecipes(Labels, Paths);

	const int32 Index = Labels.IndexOfByKey(Label);
	if (Index == INDEX_NONE)
	{
		OutStatus = FString::Printf(
			TEXT("REFUSED - the library has no recipe called '%s'."), *Label);
		return false;
	}

	// Everything that can fail is done BEFORE the first byte is copied, so a refusal leaves the
	// room already being edited exactly as it was. Loading into a half-overwritten recipe would
	// destroy unsaved work to report a failure.
	URoomRecipeAsset* Stored = LoadObject<URoomRecipeAsset>(nullptr, *Paths[Index]);
	if (Stored == nullptr)
	{
		OutStatus = FString::Printf(TEXT("REFUSED - %s would not load."), *Paths[Index]);
		return false;
	}

	// The same reflection loop Save writes with, for the same reason: a field added to the
	// recipe crosses without this function being edited. A hand-written field list is how a
	// loaded room silently loses whatever was added last.
	for (TFieldIterator<FProperty> It(URoomRecipeAsset::StaticClass()); It; ++It)
	{
		It->CopyCompleteValue_InContainer(Working, Stored);
	}

	OutStatus = FString::Printf(TEXT("OK - opened %s."), *Label);

	// Two things are reported rather than refused, because the panel is the only tool that can
	// repair either one and refusing to open it would make the damage permanent.
	//
	// First: the label comes from the PATH and the fields come from the asset, so a hand-moved
	// or hand-renamed recipe can say two different things about what it is. Saving it again
	// files it by its FIELDS, silently leaving the original behind at the old path.
	const FString FiledLabel = Working->RoomType.IsNone()
		? Working->RoomName.ToString()
		: FString::Printf(TEXT("%s/%s"), *Working->RoomType.ToString(), *Working->RoomName.ToString());
	if (FiledLabel != Label)
	{
		OutStatus += FString::Printf(
			TEXT("  WARNING - it is filed at '%s' but calls itself '%s'; saving will write the "
			     "second and leave the first behind."), *Label, *FiledLabel);
	}

	// Second: Save refuses to write an unbuildable room, so anything invalid here was edited by
	// hand. Say so plainly -- the author needs to know the room they just opened will not
	// generate before they wonder why.
	FString LayoutError;
	if (!Working->ValidateRecipe(LayoutError))
	{
		OutStatus += FString::Printf(TEXT("  WARNING - it does not validate: %s"), *LayoutError);
	}

	return true;
#else
	OutStatus = TEXT("REFUSED - room authoring is an editor-only tool.");
	return false;
#endif
}

// -------------------------------------------------------------------------- authored bake

#if WITH_EDITOR
namespace
{
	/** One static mesh at one place, in the room's own frame. */
	struct FAuthoredMesh
	{
		UStaticMesh* Mesh = nullptr;
		FTransform Xf = FTransform::Identity;
	};

	/**
	 * One whole actor at one place, in the room's own frame -- a torch, a brazier, a sound
	 * emitter: anything a flattened static mesh cannot reproduce.
	 *
	 * The CLASS, not the instance. The bake writes a UChildActorComponent template pointing at
	 * it, so the prefab spawns a real live actor of that class and the prop keeps its lights,
	 * its particles and whatever else its own Blueprint does.
	 */
	struct FAuthoredFixture
	{
		TSubclassOf<AActor> Class;
		FTransform Xf = FTransform::Identity;
	};

	/**
	 * Does this actor have to come through the bake ALIVE, rather than as flattened meshes?
	 *
	 * Yes exactly when it carries a component that emits something no static mesh can: light,
	 * particles, sound, or a projected decal. A torch prop is the case this exists for -- it is
	 * a mesh plus a point light plus a Niagara flame, and harvesting only its mesh bakes a cold
	 * torch that looks right in the viewport and lights nothing, which is precisely the defect
	 * found in BP_RectRoom_hall on 2026-09-07.
	 *
	 * An ALLOW-LIST, not a deny-list, and that direction is the whole design. Promoting every
	 * actor that merely has some non-mesh component would sweep up billboards, arrows, and the
	 * collision boxes on ordinary kit pieces, turning a room of flattened walls back into a
	 * room of spawned actors -- the exact cost baking exists to avoid. Four component families
	 * earn an actor its life; everything else still flattens.
	 */
	bool ActorCarriesLiveComponents(const AActor* Actor)
	{
		if (Actor == nullptr) { return false; }

		TInlineComponentArray<UActorComponent*> Components(Actor);
		for (const UActorComponent* Component : Components)
		{
			if (URoomAuthorTools::IsLiveFixtureComponent(Component)) { return true; }
		}
		return false;
	}

	/**
	 * Every mesh standing in the level, expressed relative to RoomOrigin.
	 *
	 * Reads the LIVE components rather than each actor's class template, because the whole
	 * point of baking the level is to capture what is actually there -- including anything
	 * nudged by hand since the last Regenerate.
	 *
	 * BOTH tags are walked, and that is not belt-and-braces. PCG splits its output across two
	 * kinds of actor: the wall, corner and gateway pieces become separate actors wearing
	 * DefaultPCGActorTag, but the floor and the scattered props are INSTANCED COMPONENTS living
	 * on the PCG volume itself, which wears only the authoring tag. Measured 2026-09-01 on a
	 * 7x7 room: 77 tagged actors, and ISM_MOD_Floor_01_O_straight_med / ISM_SM_PROP_barrel /
	 * ISM_SM_PROP_chest sitting on the volume. Collecting only the tagged actors bakes a room
	 * with no floor and no props, and nothing about the result says a thing is missing.
	 *
	 * Instanced components are expanded one entry per instance. A HISM carrying forty walls is
	 * a single component, and treating it as one mesh would bake one wall and silently lose
	 * thirty-nine; the count in the status line is what makes that visible either way.
	 *
	 * Actors that ActorCarriesLiveComponents claims go to OutFixtures INSTEAD, whole and
	 * unflattened -- never to both, or the prefab would carry the fixture's mesh twice, once
	 * cold from the harvest and once inside the live child actor.
	 */
	void CollectPlacedMeshes(UWorld* World, const FVector& RoomOrigin,
	                         TArray<FAuthoredMesh>& Out, TArray<FAuthoredFixture>& OutFixtures)
	{
		// Relative to the room's min corner. A pure translation, so only the location moves --
		// rotation and scale are already what the generator will want.
		auto ToRoomFrame = [&RoomOrigin](const FTransform& WorldXf)
		{
			FTransform Local = WorldXf;
			Local.SetTranslation(WorldXf.GetTranslation() - RoomOrigin);
			return Local;
		};

		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Actor = *It;
			if (Actor == nullptr) { continue; }
			if (!Actor->Tags.Contains(PCGHelpers::DefaultPCGActorTag) &&
			    !Actor->Tags.Contains(URoomAuthorTools::AuthoringTag()))
			{
				continue;
			}

			// A live fixture is taken at the ACTOR's transform, not its mesh component's: the
			// child actor the bake writes reconstructs the whole prop from its class, so what
			// has to be preserved is where the prop stands, and every component's placement
			// within it comes back from the class itself.
			if (ActorCarriesLiveComponents(Actor))
			{
				OutFixtures.Add({ Actor->GetClass(), ToRoomFrame(Actor->GetActorTransform()) });
				continue;
			}

			TInlineComponentArray<UStaticMeshComponent*> Components(Actor);
			for (UStaticMeshComponent* SMC : Components)
			{
				if (SMC == nullptr || SMC->GetStaticMesh() == nullptr) { continue; }

				if (const UInstancedStaticMeshComponent* ISM =
					Cast<UInstancedStaticMeshComponent>(SMC))
				{
					for (int32 I = 0; I < ISM->GetInstanceCount(); ++I)
					{
						FTransform InstanceXf;
						if (ISM->GetInstanceTransform(I, InstanceXf, /*bWorldSpace=*/true))
						{
							Out.Add({ SMC->GetStaticMesh(), ToRoomFrame(InstanceXf) });
						}
					}
					continue;
				}

				Out.Add({ SMC->GetStaticMesh(), ToRoomFrame(SMC->GetComponentTransform()) });
			}
		}
	}
}
#endif

bool URoomAuthorTools::IsLiveFixtureComponent(const UActorComponent* Component)
{
	if (Component == nullptr) { return false; }

	// ULightComponentBase rather than ULightComponent: the base is what SkyLight and the
	// ordinary point/spot/rect lights share, and matching on the narrower class would let a
	// sky light through as flattenable, which it is not -- it has no mesh to flatten.
	//
	// UFXSystemComponent covers Niagara and Cascade in one test. Naming UNiagaraComponent
	// directly would mean a Niagara module dependency for a single IsA, and would silently
	// stop recognising anything built on the older system.
	return Component->IsA<ULightComponentBase>()
	    || Component->IsA<UFXSystemComponent>()
	    || Component->IsA<UAudioComponent>()
	    || Component->IsA<UDecalComponent>();
}

// ---------------------------------------------------------------------------- the bake
//
// ApplyExitContractToAsset and BakeAuthoredRoom are DELIBERATELY ABSENT, not lost. In
// Level_Creator_1 they occupied these 365 lines and wrote a URectRoomAsset plus a baked
// prefab beside it. This project's rooms are Master_Room_C children -- exit arrows under an
// Exits Folder, an Overlap_Box on the RoomOverlap channel, floor at Z=0, origin at the
// entrance -- so the OUTPUT half of the bake is a rewrite against a different contract
// rather than a port, and it is step 6 of the port plan.
//
// Everything the bake consumed is already here and tested: the validated layout, the
// resolved openings, and the kit-set piece resolution below.

namespace
{
	/**
	 * Every (chamber, side) a new or re-parented chamber could legally attach to.
	 *
	 * One walk feeding both the labels and their meaning, because a combo box and the thing it
	 * resolves to are only safe while they are generated from the same pass in the same order.
	 * The same shape as GetConnectionLabels / GetConnectionArches, for the same reason.
	 *
	 * ForChamberIndex == INDEX_NONE asks on behalf of a chamber not added yet, so every
	 * existing chamber is eligible. Otherwise only chambers BELOW it are -- the ascending-parent
	 * rule -- and the slot it currently occupies stays on the list, or editing a chamber's other
	 * properties would silently offer no way to keep the attachment it already has.
	 */
	void CollectAttachTargets(const URoomRecipeAsset* Recipe, int32 ForChamberIndex,
	                          TArray<FString>& OutLabels, TArray<int32>& OutParents,
	                          TArray<ERoomSide>& OutSides)
	{
		OutLabels.Reset();
		OutParents.Reset();
		OutSides.Reset();
		if (Recipe == nullptr) { return; }

		RoomAuthor::FRoomLayout Layout;
		Recipe->MakeLayout(Layout);

		const bool bEditing = Recipe->Chambers.IsValidIndex(ForChamberIndex);
		const int32 MaxParent = bEditing ? ForChamberIndex : Recipe->Chambers.Num();

		for (int32 P = 0; P < MaxParent; ++P)
		{
			for (int64 S = 0; S < RectGen::NumSides; ++S)
			{
				const RectGen::ERectSide Side = static_cast<RectGen::ERectSide>(S);

				bool bTaken = RoomAuthor::IsSideTaken(Layout, P, Side);

				// ...except by the very chamber we are asking on behalf of.
				if (bTaken && bEditing)
				{
					const FRoomChamber& Self = Recipe->Chambers[ForChamberIndex];
					if (Self.AttachParent == P
					 && URoomRecipeAsset::ToRectSide(Self.AttachSide) == Side)
					{
						bTaken = false;
					}
				}

				if (bTaken) { continue; }

				OutLabels.Add(FString::Printf(TEXT("Chamber %d  %s"), P, RoomAuthor::SideName(Side)));
				OutParents.Add(P);
				OutSides.Add(URoomRecipeAsset::FromRectSide(Side));
			}
		}
	}
}

void URoomAuthorTools::SetChamberAttachment(URoomRecipeAsset* Recipe, int32 Index,
                                            int32 ParentIndex, ERoomSide Side)
{
	if (Recipe == nullptr || !Recipe->Chambers.IsValidIndex(Index)) { return; }

	// Clamped here rather than trusted, because this is the one entry point a widget combo
	// writes through. ValidateAttachments would catch a bad parent later, but only as a
	// refusal on the next Regenerate -- by which time the author has moved on.
	if (ParentIndex >= Index || ParentIndex < 0)
	{
		Recipe->Chambers[Index].AttachParent = INDEX_NONE;
	}
	else
	{
		Recipe->Chambers[Index].AttachParent = ParentIndex;
		Recipe->Chambers[Index].AttachSide = Side;
	}

	Recipe->MarkPackageDirty();
}

void URoomAuthorTools::GetChamberAttachment(const URoomRecipeAsset* Recipe, int32 Index,
                                            int32& ParentIndex, ERoomSide& Side, bool& bIsRoot)
{
	ParentIndex = INDEX_NONE;
	Side = ERoomSide::North;
	bIsRoot = true;

	if (Recipe == nullptr || !Recipe->Chambers.IsValidIndex(Index)) { return; }

	const FRoomChamber& C = Recipe->Chambers[Index];
	ParentIndex = C.AttachParent;
	Side = C.AttachSide;
	bIsRoot = (C.AttachParent == INDEX_NONE);
}

TArray<FString> URoomAuthorTools::GetAttachTargetLabels(const URoomRecipeAsset* Recipe,
                                                        int32 ForChamberIndex)
{
	TArray<FString> Labels;
	TArray<int32> Parents;
	TArray<ERoomSide> Sides;
	CollectAttachTargets(Recipe, ForChamberIndex, Labels, Parents, Sides);
	return Labels;
}

bool URoomAuthorTools::ResolveAttachTarget(const URoomRecipeAsset* Recipe, int32 ForChamberIndex,
                                           int32 OptionIndex, int32& ParentIndex, ERoomSide& Side)
{
	ParentIndex = INDEX_NONE;
	Side = ERoomSide::North;

	TArray<FString> Labels;
	TArray<int32> Parents;
	TArray<ERoomSide> Sides;
	CollectAttachTargets(Recipe, ForChamberIndex, Labels, Parents, Sides);

	if (!Parents.IsValidIndex(OptionIndex)) { return false; }

	ParentIndex = Parents[OptionIndex];
	Side = Sides[OptionIndex];
	return true;
}

bool URoomAuthorTools::ApplyAttachments(URoomRecipeAsset* Recipe, FString& OutError)
{
	if (Recipe == nullptr || Recipe->Chambers.Num() == 0) { return true; }

	RoomAuthor::FRoomLayout Layout;
	Recipe->MakeLayout(Layout);

	if (!RoomAuthor::ValidateAttachments(Layout, OutError)) { return false; }

	RoomAuthor::DeriveChamberPositions(Layout);

	Recipe->BoundingWidth  = static_cast<int32>(Layout.BoundingWidth);
	Recipe->BoundingLength = static_cast<int32>(Layout.BoundingLength);

	for (int32 I = 0; I < Recipe->Chambers.Num() && Layout.Chambers.IsValidIndex(I); ++I)
	{
		Recipe->Chambers[I].GridX = static_cast<int32>(Layout.Chambers[I].GridX);
		Recipe->Chambers[I].GridY = static_cast<int32>(Layout.Chambers[I].GridY);
	}

	Recipe->MarkPackageDirty();
	OutError.Reset();
	return true;
}

void URoomAuthorTools::FitBoundsToChambers(URoomRecipeAsset* Recipe)
{
	if (Recipe == nullptr || Recipe->Chambers.Num() == 0) { return; }

	RoomAuthor::FRoomLayout Layout;
	Recipe->MakeLayout(Layout);

	RoomAuthor::FitBoundsToChambers(Layout);

	// MakeLayout only ever widens, so this is the one place that narrows back. The values are
	// tile counts and offsets bounded by the chambers they came from, so int32 cannot lose
	// anything a room could legitimately hold.
	Recipe->BoundingWidth  = static_cast<int32>(Layout.BoundingWidth);
	Recipe->BoundingLength = static_cast<int32>(Layout.BoundingLength);

	for (int32 I = 0; I < Recipe->Chambers.Num() && Layout.Chambers.IsValidIndex(I); ++I)
	{
		Recipe->Chambers[I].GridX = static_cast<int32>(Layout.Chambers[I].GridX);
		Recipe->Chambers[I].GridY = static_cast<int32>(Layout.Chambers[I].GridY);
	}

	Recipe->MarkPackageDirty();
}

int32 URoomAuthorTools::AddChamber(URoomRecipeAsset* Recipe, int32 CopyDressingFrom)
{
	if (Recipe == nullptr) { return INDEX_NONE; }

	FRoomChamber Chamber;
	if (Recipe->Chambers.IsValidIndex(CopyDressingFrom))
	{
		Chamber = Recipe->Chambers[CopyDressingFrom];
		Chamber.GridX = 0;
		Chamber.GridY = 0;

		// Copy the DRESSING, never the place. Position was already reset above; attachment is
		// the same kind of fact and was missed when it was added (measured 2026-08-31): the
		// copy inherited its source's parent AND side, so GetAttachTargetLabels then exempted
		// that slot as "the one this chamber already occupies" and offered an occupied side
		// back to the author. A new chamber has no place until someone gives it one.
		Chamber.AttachParent = INDEX_NONE;
		Chamber.AttachSide = ERoomSide::North;
	}

	const int32 Index = Recipe->Chambers.Add(Chamber);
	Recipe->MarkPackageDirty();
	return Index;
}

int32 URoomAuthorTools::GetDescendantCount(const URoomRecipeAsset* Recipe, int32 Index)
{
	if (Recipe == nullptr || !Recipe->Chambers.IsValidIndex(Index)) { return 0; }

	TArray<bool> bDoomed;
	bDoomed.Init(false, Recipe->Chambers.Num());
	bDoomed[Index] = true;

	// One ascending pass is enough because a parent always has a lower index than its child,
	// so a chamber's parent has already been decided by the time it is reached.
	int32 Count = 0;
	for (int32 I = Index + 1; I < Recipe->Chambers.Num(); ++I)
	{
		const int32 Parent = Recipe->Chambers[I].AttachParent;
		if (bDoomed.IsValidIndex(Parent) && bDoomed[Parent])
		{
			bDoomed[I] = true;
			++Count;
		}
	}
	return Count;
}

void URoomAuthorTools::RemoveChamber(URoomRecipeAsset* Recipe, int32 Index)
{
	if (Recipe == nullptr || !Recipe->Chambers.IsValidIndex(Index)) { return; }

	// Removing a chamber removes everything hanging off it. A child whose parent is gone has
	// no position to derive -- keeping it would mean silently re-rooting it somewhere, and a
	// chamber that quietly relocates is worse than one that goes away with its parent.
	// GetDescendantCount is the same walk, and the widget can warn with it beforehand.
	const int32 Count = Recipe->Chambers.Num();
	TArray<bool> bDoomed;
	bDoomed.Init(false, Count);
	bDoomed[Index] = true;
	for (int32 I = Index + 1; I < Count; ++I)
	{
		const int32 Parent = Recipe->Chambers[I].AttachParent;
		if (bDoomed.IsValidIndex(Parent) && bDoomed[Parent]) { bDoomed[I] = true; }
	}

	// Old index -> new index for the survivors, INDEX_NONE for the doomed. Rebuilding through
	// one map is what keeps the two independent index spaces -- attachments and connections --
	// from being shifted by two different pieces of arithmetic that have to agree.
	TArray<int32> Remap;
	Remap.Init(INDEX_NONE, Count);
	int32 Next = 0;
	for (int32 I = 0; I < Count; ++I)
	{
		if (!bDoomed[I]) { Remap[I] = Next++; }
	}

	TArray<FRoomChamber> Survivors;
	Survivors.Reserve(Next);
	for (int32 I = 0; I < Count; ++I)
	{
		if (bDoomed[I]) { continue; }

		FRoomChamber C = Recipe->Chambers[I];
		if (C.AttachParent != INDEX_NONE && Remap.IsValidIndex(C.AttachParent))
		{
			// A survivor's parent is necessarily a survivor: a doomed parent dooms its child.
			C.AttachParent = Remap[C.AttachParent];
		}
		Survivors.Add(C);
	}
	Recipe->Chambers = MoveTemp(Survivors);

	// Connections are keyed by chamber index too, so every opinion naming a removed chamber is
	// gone and the rest move with their chambers.
	for (int32 I = Recipe->Connections.Num() - 1; I >= 0; --I)
	{
		FRoomConnection& Conn = Recipe->Connections[I];
		const bool bLostA = !Remap.IsValidIndex(Conn.ChamberA) || Remap[Conn.ChamberA] == INDEX_NONE;
		const bool bLostB = !Remap.IsValidIndex(Conn.ChamberB) || Remap[Conn.ChamberB] == INDEX_NONE;
		if (bLostA || bLostB)
		{
			Recipe->Connections.RemoveAt(I);
			continue;
		}
		Conn.ChamberA = Remap[Conn.ChamberA];
		Conn.ChamberB = Remap[Conn.ChamberB];
	}

	Recipe->MarkPackageDirty();
}

int32 URoomAuthorTools::GetChamberCount(const URoomRecipeAsset* Recipe)
{
	return Recipe != nullptr ? Recipe->Chambers.Num() : 0;
}

TArray<FString> URoomAuthorTools::GetChamberLabels(const URoomRecipeAsset* Recipe)
{
	TArray<FString> Labels;
	if (Recipe == nullptr) { return Labels; }

	for (int32 I = 0; I < Recipe->Chambers.Num(); ++I)
	{
		const FRoomChamber& C = Recipe->Chambers[I];
		Labels.Add(FString::Printf(TEXT("%d:  %dx%d at (%d, %d)"),
			I, C.Width, C.Length, C.GridX, C.GridY));
	}
	return Labels;
}

void URoomAuthorTools::SetChamberShape(URoomRecipeAsset* Recipe, int32 Index,
                                       int32 GridX, int32 GridY, int32 Width, int32 Length)
{
	if (Recipe == nullptr || !Recipe->Chambers.IsValidIndex(Index)) { return; }

	FRoomChamber& C = Recipe->Chambers[Index];
	C.GridX = GridX;
	C.GridY = GridY;
	C.Width = FMath::Max(1, Width);
	C.Length = FMath::Max(1, Length);
	Recipe->MarkPackageDirty();
}

void URoomAuthorTools::GetChamberShape(const URoomRecipeAsset* Recipe, int32 Index,
                                       int32& GridX, int32& GridY, int32& Width, int32& Length)
{
	if (Recipe == nullptr || !Recipe->Chambers.IsValidIndex(Index)) { return; }

	const FRoomChamber& C = Recipe->Chambers[Index];
	GridX = C.GridX;
	GridY = C.GridY;
	Width = C.Width;
	Length = C.Length;
}

void URoomAuthorTools::SetChamberPieces(URoomRecipeAsset* Recipe, int32 Index,
                                        const FString& WallPath,
                                        const FString& CornerNWPath, const FString& CornerNEPath,
                                        const FString& CornerSEPath, const FString& CornerSWPath,
                                        const FString& FloorPath, const FString& CeilingMeshPath,
                                        float PropDensity)
{
	if (Recipe == nullptr || !Recipe->Chambers.IsValidIndex(Index)) { return; }

	FRoomChamber& C = Recipe->Chambers[Index];
	C.WallCls = FSoftClassPath(WallPath);
	C.CornerNWCls = FSoftClassPath(CornerNWPath);
	C.CornerNECls = FSoftClassPath(CornerNEPath);
	C.CornerSECls = FSoftClassPath(CornerSEPath);
	C.CornerSWCls = FSoftClassPath(CornerSWPath);
	C.FloorCls = FSoftClassPath(FloorPath);
	C.CeilingMesh = FSoftObjectPath(CeilingMeshPath);
	C.PropDensity = FMath::Clamp(PropDensity, 0.0f, 1.0f);
	Recipe->MarkPackageDirty();
}

void URoomAuthorTools::GetChamberPieces(const URoomRecipeAsset* Recipe, int32 Index,
                                        FString& WallPath,
                                        FString& CornerNWPath, FString& CornerNEPath,
                                        FString& CornerSEPath, FString& CornerSWPath,
                                        FString& FloorPath, FString& CeilingMeshPath,
                                        float& PropDensity)
{
	if (Recipe == nullptr || !Recipe->Chambers.IsValidIndex(Index)) { return; }

	// RAW SLOTS, deliberately NOT resolved -- the opposite of what GenerateRoom does, and the
	// difference matters.
	//
	// This pair is the panel's editor: Get fills the combo boxes, the author changes one, Set
	// writes them all back. An empty slot here means "inherit from the kit set", and the panel
	// must show it empty so the author can see which pieces this chamber actually overrides.
	//
	// Resolving here would round-trip catastrophically. The panel would show the inherited
	// piece as though it were authored, and the very next Set would write all seven back as
	// explicit overrides -- silently converting a chamber that follows its kit into one frozen
	// against it, so a later edit to the set would no longer reach this room. That is precisely
	// the duplication kit sets exist to remove, reintroduced by a getter.
	const FRoomChamber& C = Recipe->Chambers[Index];
	WallPath = C.WallCls.ToString();
	CornerNWPath = C.CornerNWCls.ToString();
	CornerNEPath = C.CornerNECls.ToString();
	CornerSEPath = C.CornerSECls.ToString();
	CornerSWPath = C.CornerSWCls.ToString();
	FloorPath = C.FloorCls.ToString();
	CeilingMeshPath = C.CeilingMesh.ToString();
	PropDensity = C.PropDensity;
}

// ----------------------------------------------------------------------------- connections

TArray<FString> URoomAuthorTools::GetConnectionLabels(const URoomRecipeAsset* Recipe)
{
	TArray<FString> Labels;

	RoomAuthor::FRoomLayout Layout;
	TArray<RoomAuthor::FSharedEdge> Edges;
	DetectEdges(Recipe, Layout, Edges);

	for (const RoomAuthor::FSharedEdge& Edge : Edges) { Labels.Add(DescribeEdge(Edge)); }
	return Labels;
}

TArray<bool> URoomAuthorTools::GetConnectionArches(const URoomRecipeAsset* Recipe)
{
	TArray<bool> Arches;

	RoomAuthor::FRoomLayout Layout;
	TArray<RoomAuthor::FSharedEdge> Edges;
	DetectEdges(Recipe, Layout, Edges);

	for (const RoomAuthor::FSharedEdge& Edge : Edges)
	{
		Arches.Add(RoomAuthor::ConnectionModeFor(Layout, Edge) == RoomAuthor::EConnectionMode::Arch);
	}
	return Arches;
}

TArray<bool> URoomAuthorTools::GetConnectionOpen(const URoomRecipeAsset* Recipe)
{
	TArray<bool> Opens;

	RoomAuthor::FRoomLayout Layout;
	TArray<RoomAuthor::FSharedEdge> Edges;
	DetectEdges(Recipe, Layout, Edges);

	for (const RoomAuthor::FSharedEdge& Edge : Edges)
	{
		Opens.Add(RoomAuthor::ConnectionModeFor(Layout, Edge) == RoomAuthor::EConnectionMode::Open);
	}
	return Opens;
}

void URoomAuthorTools::SetConnectionStates(URoomRecipeAsset* Recipe, const TArray<bool>& Arches,
                                           const TArray<bool>& Opens)
{
	if (Recipe == nullptr) { return; }

	RoomAuthor::FRoomLayout Layout;
	TArray<RoomAuthor::FSharedEdge> Edges;
	DetectEdges(Recipe, Layout, Edges);

	// A row is only written when the panel actually supplied BOTH columns for it. A row past
	// the end of either array is a row the panel never displayed, and SetConnectionArches
	// learned the hard way (2026-08-30) that writing those clobbers the arch default before
	// the author has seen the edge at all.
	const int32 Rows = FMath::Min3(Edges.Num(), Arches.Num(), Opens.Num());

	for (int32 I = 0; I < Rows; ++I)
	{
		const RoomAuthor::FSharedEdge& Edge = Edges[I];

		// Open is the destructive choice and the one the author had to reach for, so it wins
		// a contradictory pair rather than being quietly downgraded to an arch.
		const ERoomConnectionMode Mode =
			  Opens[I]  ? ERoomConnectionMode::Open
			: Arches[I] ? ERoomConnectionMode::Arch
			            : ERoomConnectionMode::Walled;

		FRoomConnection* Existing = Recipe->Connections.FindByPredicate(
			[&Edge](const FRoomConnection& C)
			{
				return (C.ChamberA == Edge.ChamberA && C.ChamberB == Edge.ChamberB)
					|| (C.ChamberA == Edge.ChamberB && C.ChamberB == Edge.ChamberA);
			});

		if (Existing != nullptr)
		{
			Existing->Mode = Mode;
			continue;
		}

		FRoomConnection Conn;
		Conn.ChamberA = static_cast<int32>(Edge.ChamberA);
		Conn.ChamberB = static_cast<int32>(Edge.ChamberB);
		Conn.Mode = Mode;
		Recipe->Connections.Add(Conn);
	}

	Recipe->MarkPackageDirty();
}

// ------------------------------------------------------------------------------ validation

bool URoomAuthorTools::ValidateRecipe(const URoomRecipeAsset* Recipe, FString& OutError)
{
	if (Recipe == nullptr)
	{
		OutError = TEXT("There is no recipe to validate.");
		return false;
	}
	return Recipe->ValidateRecipe(OutError);
}

// -------------------------------------------------------------------------- pure geometry

void URoomAuthorTools::NarrowToSingleTile(int64 TileLo, int64 TileHi, int64& OutLo, int64& OutHi)
{
	if (TileHi <= TileLo)
	{
		OutLo = -1;
		OutHi = -1;
		return;
	}

	OutLo = TileLo + (TileHi - TileLo - 1) / 2;
	OutHi = OutLo + 1;
}

FVector URoomAuthorTools::ChamberCentreUU(const URoomRecipeAsset* Recipe, int32 Index)
{
	if (Recipe == nullptr || !Recipe->Chambers.IsValidIndex(Index)) { return FVector::ZeroVector; }

	const FRoomChamber& C = Recipe->Chambers[Index];
	const double Tile = static_cast<double>(RectGen::TileUU);

	const double ChamberX = (static_cast<double>(C.GridX) + 0.5 * C.Width) * Tile;
	const double ChamberY = (static_cast<double>(C.GridY) + 0.5 * C.Length) * Tile;
	const double RoomCentreX = 0.5 * Recipe->BoundingWidth * Tile;
	const double RoomCentreY = 0.5 * Recipe->BoundingLength * Tile;

	return FVector(ChamberX - RoomCentreX, ChamberY - RoomCentreY, 0.0);
}

// ------------------------------------------------------------------------------ generation

bool URoomAuthorTools::IsAuthoringLevelOpen(UObject* WorldContextObject, FString& OutLevel)
{
	UWorld* World = EditorWorld(WorldContextObject);
	if (World == nullptr)
	{
		OutLevel = TEXT("<no world>");
		return false;
	}

	OutLevel = World->GetOutermost()->GetName();
	return OutLevel == AuthoringLevelPath();
}

int32 URoomAuthorTools::CountGeneratedActors(UObject* WorldContextObject)
{
	UWorld* World = EditorWorld(WorldContextObject);
	if (World == nullptr) { return 0; }

	int32 Count = 0;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->Tags.Contains(PCGHelpers::DefaultPCGActorTag)) { ++Count; }
	}
	return Count;
}

int32 URoomAuthorTools::ClearAuthoringActors(UObject* WorldContextObject)
{
	UWorld* World = EditorWorld(WorldContextObject);
	if (World == nullptr) { return 0; }

	// Counted BEFORE pass 1, not during it. CleanupLocalImmediate destroys the actors PCG is
	// tracking without handing back a count, so a tally accumulated inside the passes below
	// misses every actor the supported path removed -- which is nearly all of them. Measured
	// 2026-08-30: a clear that removed 69 actors reported 2. Census the tag first, census it
	// again at the end, and report the difference.
	const int32 TaggedBefore = CountGeneratedActors(WorldContextObject);

	int32 VolumesDestroyed = 0;

	// Pass 1: the supported path. Releases each component's managed resources, which is what
	// destroys the actors PCG is still tracking.
	TArray<APCGVolume*> Volumes;
	for (TActorIterator<APCGVolume> It(World); It; ++It)
	{
		if (!It->Tags.Contains(AuthoringTag())) { continue; }
		Volumes.Add(*It);

		if (UPCGComponent* Component = It->PCGComponent)
		{
			Component->CleanupLocalImmediate(/*bRemoveComponents=*/true,
			                                 /*bCleanupLocalComponents=*/true);
		}
	}

	// Pass 2: the sweep. A volume deleted in an earlier session took its component's knowledge
	// of its own actors with it and left the actors standing (measured 2026-08-29: 56 of them),
	// so anything still wearing PCG's own generated-actor tag goes too. Nothing else in the
	// authoring level is allowed to wear that tag.
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (Actor == nullptr || Actor->IsA<APCGVolume>()) { continue; }
		if (!Actor->Tags.Contains(PCGHelpers::DefaultPCGActorTag)) { continue; }

		Actor->Destroy();
	}

	for (APCGVolume* Volume : Volumes)
	{
		if (IsValid(Volume))
		{
			Volume->Destroy();
			++VolumesDestroyed;
		}
	}

	// The volumes are counted separately because they never wore PCG's generated-actor tag --
	// they are what OWNS the generated actors, not one of them.
	const int32 TaggedAfter = CountGeneratedActors(WorldContextObject);
	return FMath::Max(0, TaggedBefore - TaggedAfter) + VolumesDestroyed;
}

bool URoomAuthorTools::GenerateRoom(UObject* WorldContextObject, URoomRecipeAsset* Recipe,
                                    FString& OutStatus)
{
#if WITH_EDITOR
	if (Recipe == nullptr)
	{
		OutStatus = TEXT("REFUSED - there is no recipe to generate.");
		return false;
	}

	FString OpenLevel;
	if (!IsAuthoringLevelOpen(WorldContextObject, OpenLevel))
	{
		OutStatus = FString::Printf(
			TEXT("REFUSED - the open level is %s. Authoring only ever writes to %s."),
			*OpenLevel, AuthoringLevelPath());
		return false;
	}

	UWorld* World = EditorWorld(WorldContextObject);

	// The validator owns every rule and every sentence. Nothing below re-checks a thing it
	// already checked, and the status line shows its words rather than a paraphrase.
	RoomAuthor::FRoomLayout Layout;
	Recipe->MakeLayout(Layout);

	TArray<RoomAuthor::FSharedEdge> Edges;
	TArray<RoomAuthor::FChamberOpening> Openings;
	FString Error;
	if (!RoomAuthor::ValidateRoomLayout(Layout, Edges, Openings, Error))
	{
		OutStatus = FString::Printf(TEXT("INVALID - %s"), *Error);
		return false;
	}

	UPCGGraph* Graph = LoadObject<UPCGGraph>(nullptr, RoomGenGraphPath());
	if (Graph == nullptr)
	{
		OutStatus = FString::Printf(TEXT("REFUSED - the emitter %s did not load."),
			RoomGenGraphPath());
		return false;
	}

	// The slots the panel does not expose. Read from the tables, never named here: a kit that
	// changes its cap or gateway piece changes a table row and nothing else.
	const FString EdgeLPath = FirstPathForRole(TEXT("EdgeL"), false);
	const FString EdgeRPath = FirstPathForRole(TEXT("EdgeR"), false);
	const FString GatewayPath = FirstPathForRole(TEXT("GatewayInterior"), false);

	// Unconditional, and FIRST. PCG-spawned actors outlive their volume, so a regenerate that
	// skipped this would stack a second room exactly on top of the last one.
	const int32 Cleared = ClearAuthoringActors(WorldContextObject);

	int32 Emitted = 0;
	for (int32 I = 0; I < Recipe->Chambers.Num(); ++I)
	{
		const FRoomChamber& Chamber = Recipe->Chambers[I];

		const FString Label = FString::Printf(TEXT("RoomAuthor_Chamber_%d"), I);

		FActorSpawnParameters SpawnParams;
		SpawnParams.Name = MakeUniqueObjectName(World, APCGVolume::StaticClass(), FName(Label),
			EUniqueObjectNameOptions::UniversallyUnique);
		SpawnParams.InitialActorLabel = Label;

		// AT THE FLOOR, not half way up the room. Measured 2026-09-01: the PCG graph builds the
		// body UPWARD from the volume's origin, so an origin lifted to the middle of the room
		// lifted the whole room with it -- the floor landed at world z 300 and everything
		// downstream that calls a room's own origin "the floor" was quietly wrong, this file's
		// authored bake included.
		APCGVolume* Volume = World->SpawnActor<APCGVolume>(
			ChamberCentreUU(Recipe, I), FRotator::ZeroRotator, SpawnParams);
		if (Volume == nullptr) { continue; }

		Volume->SetActorLabel(Label);
		Volume->Tags.Add(AuthoringTag());

		// The brush is what gives the volume real bounds; a volume spawned without one has
		// none, and PCG has nothing to place inside. Same construction PCGToolset uses.
		Volume->SetActorScale3D(FVector(
			Chamber.Width * 0.5 * RectGen::TileUU / BrushHalfExtentUU + 1.0,
			Chamber.Length * 0.5 * RectGen::TileUU / BrushHalfExtentUU + 1.0,
			(Recipe->WallHeightUU + 30) * 0.5 / BrushHalfExtentUU + 1.0));

		UCubeBuilder* CubeBuilder = NewObject<UCubeBuilder>(GetTransientPackage());
		CubeBuilder->X = 2.0f * BrushHalfExtentUU;
		CubeBuilder->Y = 2.0f * BrushHalfExtentUU;
		CubeBuilder->Z = 2.0f * BrushHalfExtentUU;
		CubeBuilder->Hollow = false;
		UActorFactory::CreateBrushForVolumeActor(Volume, CubeBuilder);

		// Lift the BOX -- not the actor -- so it wraps the body instead of straddling the floor.
		//
		// The cube builder centres its cube on the origin, and a volume actor's brush component
		// IS its root, so there is no component transform to offset: moving the brush any other
		// way moves the actor, and moving the actor moves the room. Translating the polys is
		// what separates "where the room is built from" (the origin, which must stay at the
		// floor) from "what the bounds cover" (the body, which is entirely above it).
		//
		// Measured before this existed: the box ran -400..+400 about the origin while the body
		// ran -16..+637, so the top 237uu of every room sat OUTSIDE its own PCG bounds. That is
		// the half of this bug that was invisible; the half that shows in the viewport is the
		// 400uu of empty box hanging below the floor.
		//
		// The shift is applied in the brush's own unscaled space, hence the divide: the actor's
		// Z scale is what turns a 100uu half-extent into the room's half-height.
		const double ScaleZ = (Recipe->WallHeightUU + 30) * 0.5 / BrushHalfExtentUU + 1.0;
		const double LocalShiftZ = (Recipe->WallHeightUU + 30) * 0.5 / ScaleZ;
		if (Volume->Brush != nullptr && Volume->Brush->Polys != nullptr)
		{
			for (FPoly& Poly : Volume->Brush->Polys->Element)
			{
				for (FVector3f& Vertex : Poly.Vertices)
				{
					Vertex.Z += static_cast<float>(LocalShiftZ);
				}
				Poly.Base.Z += static_cast<float>(LocalShiftZ);
			}
			Volume->Brush->BuildBound();
		}

		if (UBrushComponent* Brush = Volume->GetBrushComponent())
		{
			Brush->ReregisterComponent();
			Brush->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Brush->SetCanEverAffectNavigation(false);
		}

		UPCGComponent* Component = Volume->PCGComponent;
		if (Component == nullptr) { Volume->Destroy(); continue; }

		// Graph first: a graph change resets every parameter override, so setting parameters
		// before this point would silently throw them away.
		Component->SetGraphLocal(Graph);
		UPCGGraphInstance* Instance = Component->GetGraphInstance();

		SetIntParam(Instance, TEXT("width"), Chamber.Width);
		SetIntParam(Instance, TEXT("length"), Chamber.Length);
		SetIntParam(Instance, TEXT("wallHeightUU"), Recipe->WallHeightUU);

		// Per chamber, so two identically dressed chambers do not scatter identically, and
		// derived from the recipe's seed, so the room as a whole stays reproducible.
		SetIntParam(Instance, TEXT("seed"), Recipe->Seed + I);
		SetRealParam(Instance, TEXT("propDensity"), Chamber.PropDensity);

		// THROUGH Resolve*(), never off the chamber directly.
		//
		// A chamber's piece slots are OVERRIDES now: empty means "inherit from the recipe's
		// kit set". Level_Creator_1 read Chamber.WallCls here and was right to, because every
		// chamber carried its own full set. Reading it here would hand PCG an empty string for
		// every un-overridden slot -- which is almost all of them on a room that uses a kit --
		// and PCG spawns nothing for an empty path without complaining. The room would come out
		// as a floor plan with no walls and no error anywhere.
		//
		// This is the one behavioural difference between this file and the version it was
		// ported from, and it is why the accessors exist rather than the resolution being
		// written inline here.
		SetStringParam(Instance, TEXT("wallCls"), Recipe->ResolveWall(Chamber).ToString());
		SetStringParam(Instance, TEXT("cornerNWCls"),
			Recipe->ResolveCorner(Chamber, RectGen::ERectCornerIndex::NW).ToString());
		SetStringParam(Instance, TEXT("cornerNECls"),
			Recipe->ResolveCorner(Chamber, RectGen::ERectCornerIndex::NE).ToString());
		SetStringParam(Instance, TEXT("cornerSECls"),
			Recipe->ResolveCorner(Chamber, RectGen::ERectCornerIndex::SE).ToString());
		SetStringParam(Instance, TEXT("cornerSWCls"),
			Recipe->ResolveCorner(Chamber, RectGen::ERectCornerIndex::SW).ToString());
		SetStringParam(Instance, TEXT("floorCls"), Recipe->ResolveFloor(Chamber).ToString());
		SetStringParam(Instance, TEXT("ceilCls"), Recipe->ResolveCeilingMesh(Chamber).ToString());
		SetStringParam(Instance, TEXT("edgeLCls"), EdgeLPath);
		SetStringParam(Instance, TEXT("edgeRCls"), EdgeRPath);
		SetStringParam(Instance, TEXT("gatewayCls"), GatewayPath);

		// Every side starts closed. -1/-1 is the canonical spelling of "no opening" on both
		// sides of this boundary (RoomAuthorValidate.h and the graph agree on it).
		int64 Lo[RectGen::NumSides] = { -1, -1, -1, -1 };
		int64 Hi[RectGen::NumSides] = { -1, -1, -1, -1 };
		bool bFrame[RectGen::NumSides] = { false, false, false, false };

		for (const RoomAuthor::FChamberOpening& Opening : Openings)
		{
			if (Opening.ChamberIndex != I) { continue; }

			const int32 Side = static_cast<int32>(Opening.Side);
			if (Opening.Kind == RoomAuthor::EOpeningKind::Interior)
			{
				// Design spec 4b: the arch is one tile wide whatever the shared span is.
				NarrowToSingleTile(Opening.TileLo, Opening.TileHi, Lo[Side], Hi[Side]);
				bFrame[Side] = true;
			}
			else if (Opening.Kind == RoomAuthor::EOpeningKind::Merged)
			{
				// The wall is removed, so the span is the run ENTIRE -- not narrowed, and not
				// framed. A gateway here would draw a doorway standing in the middle of the
				// space the mode exists to open up.
				Lo[Side] = Opening.TileLo;
				Hi[Side] = Opening.TileHi;
				bFrame[Side] = false;
			}
			else
			{
				// Exterior: the parity-sized span, and NO frame. The dungeon generator fills
				// this slot with a door at assembly, and a frame here would draw it twice.
				Lo[Side] = Opening.TileLo;
				Hi[Side] = Opening.TileHi;
				bFrame[Side] = false;
			}
		}

		for (int32 Side = 0; Side < RectGen::NumSides; ++Side)
		{
			SetIntParam(Instance, OpeningLoParam[Side], Lo[Side]);
			SetIntParam(Instance, OpeningHiParam[Side], Hi[Side]);
			SetBoolParam(Instance, FrameParam[Side], bFrame[Side]);
		}

		Component->GenerateLocal(/*bForce=*/true);
		++Emitted;
	}

	OutStatus = FString::Printf(
		TEXT("OK - %d chamber%s emitted, %d connection%s, %d actor%s cleared first."),
		Emitted, Emitted == 1 ? TEXT("") : TEXT("s"),
		Edges.Num(), Edges.Num() == 1 ? TEXT("") : TEXT("s"),
		Cleared, Cleared == 1 ? TEXT("") : TEXT("s"));
	return true;
#else
	OutStatus = TEXT("REFUSED - room authoring is an editor-only tool.");
	return false;
#endif
}
