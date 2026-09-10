#include "Components/AudioComponent.h"
#include "Components/BillboardComponent.h"
#include "Components/BoxComponent.h"
#include "Components/DecalComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Misc/AutomationTest.h"
#include "Particles/ParticleSystemComponent.h"
#include "RectDungeon/RectRoomTypes.h"
#include "RoomAuthoring/DungeonKitSet.h"
#include "RoomAuthoring/RoomAuthorTools.h"
#include "RoomAuthoring/RoomAuthorTypes.h"
#include "RoomAuthoring/RoomAuthorValidate.h"
#include "RoomAuthoring/RoomRecipeAsset.h"

#if WITH_DEV_AUTOMATION_TESTS

// DELIBERATELY NO `using namespace`, for the reason spelled out at the top of
// RoomAuthorValidateTests.cpp -- and it applies with more force here than it did in
// Level_Creator_1, because this module builds with unity ON.
//
// SCOPE. These cover the parts of URoomAuthorTools that are pure -- display names, the
// interior arch's narrowing, and where a chamber's volume goes. The PCG half (spawning a
// volume, writing its parameters, destroying what it spawned) needs a live editor world and
// is covered by the visual evidence recorded in production/session-state/active.md, per the
// coding standards' split between BLOCKING automated tests and ADVISORY visual ones.

namespace
{
	constexpr EAutomationTestFlags RoomAuthorToolsTestFlags =
		EAutomationTestFlags::EditorContext |
		EAutomationTestFlags::CommandletContext |
		EAutomationTestFlags::EngineFilter;
}

// --- Display names ------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomAuthorDisplayNameStripsBlueprintDecoration,
	"ProceduralDungeon.RoomAuthoring.Tools.DisplayNameStripsBlueprintDecoration",
	RoomAuthorToolsTestFlags)

bool FRoomAuthorDisplayNameStripsBlueprintDecoration::RunTest(const FString&)
{
	TestEqual(TEXT("A piece class path loses its package, its BP_COMP_ prefix and its _C"),
		URoomAuthorTools::DisplayNameForPath(
			TEXT("/Game/Fantastic_Dungeon_Pack/blueprints/modular/comps/wall/PivotEdge/")
			TEXT("BP_COMP_Wall_01_E_straight_large.BP_COMP_Wall_01_E_straight_large_C")),
		FString(TEXT("Wall_01_E_straight_large")));

	TestEqual(TEXT("A ceiling MESH path loses its MOD_ prefix instead"),
		URoomAuthorTools::DisplayNameForPath(
			TEXT("/Game/Fantastic_Dungeon_Pack/meshes/modular/floor/oneSided/")
			TEXT("MOD_Floor_01_O_straight_med.MOD_Floor_01_O_straight_med")),
		FString(TEXT("Floor_01_O_straight_med")));

	TestEqual(TEXT("A path with no object suffix still yields the asset name"),
		URoomAuthorTools::DisplayNameForPath(TEXT("/Game/Anything/BP_COMP_Gateway_01_large")),
		FString(TEXT("Gateway_01_large")));

	TestEqual(TEXT("An empty path yields an empty name rather than a stray separator"),
		URoomAuthorTools::DisplayNameForPath(FString()), FString());

	// The underscored middle is the only thing telling one corner variant from another, so it
	// has to survive: concave and convex differ nowhere else.
	TestEqual(TEXT("The variant-bearing middle survives"),
		URoomAuthorTools::DisplayNameForPath(
			TEXT("/Game/X/BP_COMP_Wall_01_E_corner_convex_large.BP_COMP_Wall_01_E_corner_convex_large_C")),
		FString(TEXT("Wall_01_E_corner_convex_large")));

	return true;
}

// --- The interior arch is one tile ---------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomAuthorInteriorArchIsOneTile,
	"ProceduralDungeon.RoomAuthoring.Tools.InteriorArchIsAlwaysOneTile",
	RoomAuthorToolsTestFlags)

bool FRoomAuthorInteriorArchIsOneTile::RunTest(const FString&)
{
	int64 Lo = 0, Hi = 0;

	// A one-tile span is already what the gateway piece wants. Untouched.
	URoomAuthorTools::NarrowToSingleTile(3, 4, Lo, Hi);
	TestEqual(TEXT("A one-tile span survives unchanged, Lo"), Lo, static_cast<int64>(3));
	TestEqual(TEXT("A one-tile span survives unchanged, Hi"), Hi, static_cast<int64>(4));

	// Design spec 4b: gatewayCls is one cell wide and the kit ships no wide gateway, so a
	// framed two-tile span would place one arch with bare gap either side.
	URoomAuthorTools::NarrowToSingleTile(3, 5, Lo, Hi);
	TestEqual(TEXT("A two-tile span narrows to one tile, Lo"), Lo, static_cast<int64>(3));
	TestEqual(TEXT("A two-tile span narrows to one tile, Hi"), Hi, static_cast<int64>(4));
	TestEqual(TEXT("The result is exactly one tile wide"), Hi - Lo, static_cast<int64>(1));

	// The narrowed tile must stay INSIDE the span it came from, because that span is the one
	// ValidateOpeningSpan already cleared of corner cells. Any subset of a legal span is legal;
	// a tile chosen from anywhere else would need re-validating.
	for (int64 Width = 1; Width <= 6; ++Width)
	{
		const int64 SpanLo = 2;
		const int64 SpanHi = SpanLo + Width;
		URoomAuthorTools::NarrowToSingleTile(SpanLo, SpanHi, Lo, Hi);

		TestTrue(TEXT("The narrowed tile starts inside the source span"),
			Lo >= SpanLo && Lo < SpanHi);
		TestTrue(TEXT("The narrowed tile ends inside the source span"),
			Hi > SpanLo && Hi <= SpanHi);
		TestEqual(TEXT("The narrowed span is one tile at every width"),
			Hi - Lo, static_cast<int64>(1));
	}

	// Hi <= Lo is the canonical "no opening", and narrowing it must not invent one.
	URoomAuthorTools::NarrowToSingleTile(-1, -1, Lo, Hi);
	TestEqual(TEXT("No opening stays no opening, Lo"), Lo, static_cast<int64>(-1));
	TestEqual(TEXT("No opening stays no opening, Hi"), Hi, static_cast<int64>(-1));

	URoomAuthorTools::NarrowToSingleTile(3, 3, Lo, Hi);
	TestEqual(TEXT("An empty span is spelled -1/-1 on the way out, Lo"), Lo, static_cast<int64>(-1));
	TestEqual(TEXT("An empty span is spelled -1/-1 on the way out, Hi"), Hi, static_cast<int64>(-1));

	return true;
}

/**
 * The narrowing must agree between the two chambers of one connection.
 *
 * Both sides derive from the same room-space run, so the tile they pick has to be the same
 * tile -- an arch cut one tile apart in the two walls is a hole into the void between them.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomAuthorInteriorArchAgreesAcrossBothChambers,
	"ProceduralDungeon.RoomAuthoring.Tools.InteriorArchAgreesAcrossBothChambers",
	RoomAuthorToolsTestFlags)

bool FRoomAuthorInteriorArchAgreesAcrossBothChambers::RunTest(const FString&)
{
	// Two chambers stacked in a 6 x 8 rect, sharing an EVEN 4-tile run at different origins:
	// chamber A starts at x0, chamber B at x1. The run's parity-sized opening is two tiles, so
	// this is exactly the case the narrowing exists for.
	RoomAuthor::FRoomLayout Layout;
	Layout.BoundingWidth = 6;
	Layout.BoundingLength = 8;

	RoomAuthor::FChamberRect A;
	A.GridX = 0; A.GridY = 0; A.Width = 5; A.Length = 4;
	RoomAuthor::FChamberRect B;
	B.GridX = 1; B.GridY = 4; B.Width = 4; B.Length = 4;
	Layout.Chambers.Add(A);
	Layout.Chambers.Add(B);

	TArray<RoomAuthor::FSharedEdge> Edges;
	TArray<RoomAuthor::FChamberOpening> Openings;
	FString Error;
	if (!TestTrue(TEXT("The fixture is a valid layout"),
		RoomAuthor::ValidateRoomLayout(Layout, Edges, Openings, Error)))
	{
		AddError(Error);
		return false;
	}

	TestEqual(TEXT("One shared edge is detected"), Edges.Num(), 1);

	// One interior connection yields one opening per chamber.
	int64 RoomTileA = MIN_int64;
	int64 RoomTileB = MIN_int64;
	for (const RoomAuthor::FChamberOpening& Opening : Openings)
	{
		if (Opening.Kind != RoomAuthor::EOpeningKind::Interior) { continue; }

		int64 Lo = 0, Hi = 0;
		URoomAuthorTools::NarrowToSingleTile(Opening.TileLo, Opening.TileHi, Lo, Hi);

		const RoomAuthor::FChamberRect& C =
			Layout.Chambers[static_cast<int32>(Opening.ChamberIndex)];
		const int64 RoomTile = Lo + C.SideOriginTile(Opening.Side);

		if (Opening.ChamberIndex == 0) { RoomTileA = RoomTile; }
		else { RoomTileB = RoomTile; }
	}

	TestTrue(TEXT("Chamber 0 got an interior opening"), RoomTileA != MIN_int64);
	TestTrue(TEXT("Chamber 1 got an interior opening"), RoomTileB != MIN_int64);
	TestEqual(TEXT("Both chambers cut the SAME room tile"), RoomTileA, RoomTileB);

	return true;
}

// --- Where a chamber's volume goes ---------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomAuthorChamberCentreIsRelativeToTheBoundingRect,
	"ProceduralDungeon.RoomAuthoring.Tools.ChamberCentreIsRelativeToTheBoundingRect",
	RoomAuthorToolsTestFlags)

bool FRoomAuthorChamberCentreIsRelativeToTheBoundingRect::RunTest(const FString&)
{
	URoomRecipeAsset* Recipe = NewObject<URoomRecipeAsset>();
	Recipe->BoundingWidth = 7;
	Recipe->BoundingLength = 7;

	// The design spec's sample: a 7x3 chamber along the south edge and a 3x4 north wing.
	FRoomChamber South;
	South.GridX = 0; South.GridY = 0; South.Width = 7; South.Length = 3;
	FRoomChamber North;
	North.GridX = 2; North.GridY = 3; North.Width = 3; North.Length = 4;
	Recipe->Chambers.Add(South);
	Recipe->Chambers.Add(North);

	// Room centre is (1400, 1400). The south chamber's own centre is (1400, 600).
	const FVector SouthCentre = URoomAuthorTools::ChamberCentreUU(Recipe, 0);
	TestEqual(TEXT("South chamber X"), SouthCentre.X, 0.0);
	TestEqual(TEXT("South chamber Y"), SouthCentre.Y, -800.0);

	// The north wing's own centre is (1400, 2000).
	const FVector NorthCentre = URoomAuthorTools::ChamberCentreUU(Recipe, 1);
	TestEqual(TEXT("North wing X"), NorthCentre.X, 0.0);
	TestEqual(TEXT("North wing Y"), NorthCentre.Y, 600.0);

	// The frame is the BOUNDING rect, not the first chamber: a single chamber filling the rect
	// sits on the origin, and that must not change when a second chamber appears.
	URoomRecipeAsset* Single = NewObject<URoomRecipeAsset>();
	Single->BoundingWidth = 6;
	Single->BoundingLength = 6;
	FRoomChamber Whole;
	Whole.GridX = 0; Whole.GridY = 0; Whole.Width = 6; Whole.Length = 6;
	Single->Chambers.Add(Whole);

	const FVector Centre = URoomAuthorTools::ChamberCentreUU(Single, 0);
	TestEqual(TEXT("A chamber filling the rect sits on the origin, X"), Centre.X, 0.0);
	TestEqual(TEXT("A chamber filling the rect sits on the origin, Y"), Centre.Y, 0.0);

	TestEqual(TEXT("An out-of-range chamber yields the origin rather than reading off the end"),
		URoomAuthorTools::ChamberCentreUU(Single, 7), FVector::ZeroVector);

	return true;
}

// --- Chamber list bookkeeping ---------------------------------------------------------------

/**
 * Removing a chamber must re-key the connection opinions that outlived it.
 *
 * FRoomConnection is keyed by chamber INDEX. Remove chamber 1 without re-keying and an opinion
 * about "chambers 0 and 2" silently starts describing a different pair -- which shows up as an
 * arch appearing in a wall the author never touched.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomAuthorRemovingAChamberReKeysConnections,
	"ProceduralDungeon.RoomAuthoring.Tools.RemovingAChamberReKeysConnections",
	RoomAuthorToolsTestFlags)

bool FRoomAuthorRemovingAChamberReKeysConnections::RunTest(const FString&)
{
	URoomRecipeAsset* Recipe = NewObject<URoomRecipeAsset>();
	Recipe->Chambers.SetNum(3);

	FRoomConnection ZeroOne;
	ZeroOne.ChamberA = 0; ZeroOne.ChamberB = 1; ZeroOne.Mode = ERoomConnectionMode::Walled;
	FRoomConnection ZeroTwo;
	ZeroTwo.ChamberA = 0; ZeroTwo.ChamberB = 2; ZeroTwo.Mode = ERoomConnectionMode::Walled;
	Recipe->Connections.Add(ZeroOne);
	Recipe->Connections.Add(ZeroTwo);

	URoomAuthorTools::RemoveChamber(Recipe, 1);

	TestEqual(TEXT("Two chambers remain"), Recipe->Chambers.Num(), 2);
	TestEqual(TEXT("The opinion naming the removed chamber is gone"), Recipe->Connections.Num(), 1);
	TestEqual(TEXT("The surviving opinion still names chamber 0"),
		Recipe->Connections[0].ChamberA, 0);
	TestEqual(TEXT("...and the chamber that was 2 is now 1"),
		Recipe->Connections[0].ChamberB, 1);

	// An index nobody has is a no-op, not a crash and not a silent shuffle.
	URoomAuthorTools::RemoveChamber(Recipe, 9);
	TestEqual(TEXT("Removing a chamber that does not exist changes nothing"),
		Recipe->Chambers.Num(), 2);

	return true;
}

/**
 * The connection toggles are written positionally against the DETECTED edge list, so a round
 * trip through Set/Get must come back in the same order it went in.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomAuthorConnectionTogglesRoundTrip,
	"ProceduralDungeon.RoomAuthoring.Tools.ConnectionTogglesRoundTrip",
	RoomAuthorToolsTestFlags)

bool FRoomAuthorConnectionTogglesRoundTrip::RunTest(const FString&)
{
	URoomRecipeAsset* Recipe = NewObject<URoomRecipeAsset>();
	Recipe->BoundingWidth = 7;
	Recipe->BoundingLength = 7;
	Recipe->bExitNorth = false;
	Recipe->bExitEast = false;
	Recipe->bExitSouth = false;
	Recipe->bExitWest = false;

	FRoomChamber South;
	South.GridX = 0; South.GridY = 0; South.Width = 7; South.Length = 3;
	FRoomChamber North;
	North.GridX = 2; North.GridY = 3; North.Width = 3; North.Length = 4;
	Recipe->Chambers.Add(South);
	Recipe->Chambers.Add(North);

	TestEqual(TEXT("One shared edge is detected"),
		URoomAuthorTools::GetConnectionLabels(Recipe).Num(), 1);

	// Decision 7 of the design spec: shared edges auto-arch, and a pair with no stored opinion
	// is arched. So the default reads back true before anything has been written.
	const TArray<bool> Defaults = URoomAuthorTools::GetConnectionArches(Recipe);
	TestEqual(TEXT("One arch state is reported"), Defaults.Num(), 1);
	TestTrue(TEXT("A detected edge with no stored opinion is arched"), Defaults[0]);

	URoomAuthorTools::SetConnectionStates(Recipe, { false }, { false });
	const TArray<bool> Walled = URoomAuthorTools::GetConnectionArches(Recipe);
	TestEqual(TEXT("Still one arch state"), Walled.Num(), 1);
	TestFalse(TEXT("The edge reads back walled"), Walled[0]);

	URoomAuthorTools::SetConnectionStates(Recipe, { true }, { false });
	TestTrue(TEXT("...and arched again, without accumulating a second opinion"),
		URoomAuthorTools::GetConnectionArches(Recipe)[0]);
	TestEqual(TEXT("One stored opinion, not two"), Recipe->Connections.Num(), 1);

	// A panel with a fixed number of toggle rows hands over its whole array; the extras are
	// about edges that do not exist and must be ignored rather than stored.
	URoomAuthorTools::SetConnectionStates(Recipe, { false, false, false, false },
		{ false, false, false, false });
	TestEqual(TEXT("Extra toggles do not become opinions about nothing"),
		Recipe->Connections.Num(), 1);

	return true;
}

/**
 * Overlapping chambers produce runs that describe nothing real, so the panel must show NO
 * connections rather than a list built from meaningless geometry.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomAuthorOverlappingChambersReportNoConnections,
	"ProceduralDungeon.RoomAuthoring.Tools.OverlappingChambersReportNoConnections",
	RoomAuthorToolsTestFlags)

bool FRoomAuthorOverlappingChambersReportNoConnections::RunTest(const FString&)
{
	URoomRecipeAsset* Recipe = NewObject<URoomRecipeAsset>();
	Recipe->BoundingWidth = 7;
	Recipe->BoundingLength = 7;
	Recipe->bExitNorth = false;
	Recipe->bExitEast = false;
	Recipe->bExitSouth = false;
	Recipe->bExitWest = false;

	FRoomChamber A;
	A.GridX = 0; A.GridY = 0; A.Width = 5; A.Length = 5;
	FRoomChamber B;
	B.GridX = 2; B.GridY = 2; B.Width = 5; B.Length = 5;
	Recipe->Chambers.Add(A);
	Recipe->Chambers.Add(B);

	TestEqual(TEXT("An overlapping layout lists no connections"),
		URoomAuthorTools::GetConnectionLabels(Recipe).Num(), 0);

	FString Error;
	TestFalse(TEXT("...and is refused"), URoomAuthorTools::ValidateRecipe(Recipe, Error));
	TestTrue(TEXT("...with a reason that names the overlap"), Error.Contains(TEXT("overlap")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomAuthorAddedChamberInheritsDressingNotPlace,
	"ProceduralDungeon.RoomAuthoring.Tools.AnAddedChamberInheritsDressingButNotItsPlace",
	RoomAuthorToolsTestFlags)

bool FRoomAuthorAddedChamberInheritsDressingNotPlace::RunTest(const FString&)
{
	// Measured 2026-08-31. Add copies the source chamber so the new one is not dressed in
	// nothing -- but it was copying the ATTACHMENT too. GetAttachTargetLabels then saw the
	// copy sitting in its source's slot, exempted that slot as "the one this chamber already
	// occupies", and offered an occupied side straight back to the author.
	URoomRecipeAsset* Recipe = NewObject<URoomRecipeAsset>();

	FRoomChamber Root;
	Root.Width = 5;
	Root.Length = 5;
	Root.WallCls = FSoftClassPath(TEXT("/Game/Kit/BP_Wall.BP_Wall_C"));
	Recipe->Chambers.Add(Root);

	const int32 Second = URoomAuthorTools::AddChamber(Recipe, 0);
	URoomAuthorTools::SetChamberAttachment(Recipe, Second, 0, ERoomSide::North);

	// Now add a THIRD, copying the one that IS attached. It must arrive unplaced.
	const int32 Third = URoomAuthorTools::AddChamber(Recipe, Second);

	TestEqual(TEXT("the copy is a root, not a squatter in its source's slot"),
		Recipe->Chambers[Third].AttachParent, INDEX_NONE);

	// The dressing is the part that SHOULD be inherited -- otherwise the reset went too far.
	TestEqual(TEXT("the copy keeps its source's wall"),
		Recipe->Chambers[Third].WallCls.ToString(), Root.WallCls.ToString());

	// And the consequence the defect actually had: chamber 0's North is taken by chamber 1, so
	// it must not be offered to chamber 2.
	const TArray<FString> Targets = URoomAuthorTools::GetAttachTargetLabels(Recipe, Third);
	for (const FString& Label : Targets)
	{
		TestFalse(FString::Printf(TEXT("an occupied slot is not offered: %s"), *Label),
			Label.Contains(TEXT("Chamber 0")) && Label.Contains(TEXT("North")));
	}
	TestTrue(TEXT("free slots are still offered"), Targets.Num() > 0);
	return true;
}

// --- Save refuses before it writes ----------------------------------------------------------

/**
 * SaveRecipeToLibrary must refuse a recipe it cannot file, and refuse it BEFORE touching disk.
 *
 * Every guard here is a path component or a buildability rule, and each one has the same
 * failure mode if it is dropped: an asset lands somewhere nobody will browse to again, or the
 * library scan finds a room that cannot generate. The package assertion is the load-bearing
 * one -- a refusal that has already called CreatePackage leaves a transient package sitting in
 * memory under the library's own path, which the content browser will then offer to save.
 *
 * The success path is NOT exercised here on purpose: it writes into /Game/RectDungeon/Rooms,
 * the folder the room library scans, and a test that leaves content behind when it fails
 * mid-run is how a throwaway recipe ends up committed.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomAuthorSaveRefusesWhatItCannotFile,
	"ProceduralDungeon.RoomAuthoring.Tools.SaveRefusesARecipeItCannotFile",
	RoomAuthorToolsTestFlags)

bool FRoomAuthorSaveRefusesWhatItCannotFile::RunTest(const FString&)
{
	FString SavedPath = TEXT("<untouched>");
	FString Status;

	TestFalse(TEXT("a null recipe is refused"),
		URoomAuthorTools::SaveRecipeToLibrary(nullptr, SavedPath, Status));
	TestTrue(TEXT("the refusal says so rather than reporting a save"),
		Status.StartsWith(TEXT("REFUSED")));
	TestEqual(TEXT("no path is reported for a save that did not happen"),
		SavedPath, FString(TEXT("<untouched>")));

	// A buildable room from here on, so every remaining refusal is about the FILING and not
	// about the layout -- except the last one, which is about the layout on purpose.
	URoomRecipeAsset* Recipe = NewObject<URoomRecipeAsset>();
	Recipe->bExitNorth = Recipe->bExitEast = Recipe->bExitSouth = Recipe->bExitWest = false;
	FRoomChamber Whole;
	Whole.GridX = 0; Whole.GridY = 0;
	Whole.Width = Recipe->BoundingWidth; Whole.Length = Recipe->BoundingLength;
	Recipe->Chambers.Add(Whole);

	FString LayoutError;
	if (!TestTrue(FString::Printf(TEXT("the fixture is buildable to begin with: %s"), *LayoutError),
		Recipe->ValidateRecipe(LayoutError)))
	{
		return false;
	}

	// An unnamed room. FName's default prints as "None", so the empty case and the literal
	// "None" case are the same case and both have to be caught.
	Recipe->RoomName = NAME_None;
	Recipe->RoomType = TEXT("SaveGuardTest");
	TestFalse(TEXT("an unnamed room is refused"),
		URoomAuthorTools::SaveRecipeToLibrary(Recipe, SavedPath, Status));
	TestTrue(FString::Printf(TEXT("the refusal names the missing NAME: %s"), *Status),
		Status.Contains(TEXT("name")));

	// An untyped room. The type is the folder, so an empty one files into Rooms//.
	Recipe->RoomName = TEXT("SaveGuardRoom");
	Recipe->RoomType = NAME_None;
	TestFalse(TEXT("an untyped room is refused"),
		URoomAuthorTools::SaveRecipeToLibrary(Recipe, SavedPath, Status));
	TestTrue(FString::Printf(TEXT("the refusal names the missing TYPE: %s"), *Status),
		Status.Contains(TEXT("type")));

	// Named, typed, and unbuildable: an exit on a side with no chamber against it. The status
	// must carry the VALIDATOR's own sentence -- the widget shows this string verbatim, and a
	// paraphrase composed here would drift from the rule it describes.
	Recipe->RoomType = TEXT("SaveGuardTest");
	Recipe->BoundingLength = Recipe->BoundingLength + 2;
	Recipe->bExitNorth = true;
	TestFalse(TEXT("an unbuildable room is refused"),
		URoomAuthorTools::SaveRecipeToLibrary(Recipe, SavedPath, Status));
	TestTrue(FString::Printf(TEXT("the refusal carries the validator's sentence: %s"), *Status),
		Status.Contains(TEXT("North")));

	// Nothing above was allowed to reach disk, or even to reach memory: the package the last
	// two cases would have written to must not exist.
	TestNull(TEXT("no package was created for any refused save"),
		FindPackage(nullptr, TEXT("/Game/RectDungeon/Rooms/SaveGuardTest/DA_RoomRecipe_SaveGuardRoom")));

	TestEqual(TEXT("no path is reported for any refused save"),
		SavedPath, FString(TEXT("<untouched>")));
	return true;
}

// --- Load refuses without destroying what you were editing --------------------------------

/**
 * A refused Load must leave the working recipe byte-for-byte as it was.
 *
 * This is the whole reason LoadRecipeFromLibrary resolves the label and loads the asset BEFORE
 * it copies anything. The obvious implementation -- clear the working recipe, then fill it --
 * destroys unsaved work in order to report a failure, and it fails in exactly the situation
 * where the author most needs their room back: a mistyped or stale label.
 *
 * The library is NOT written to here. Every case below is refused before disk is touched.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomAuthorLoadRefusalLeavesTheWorkingRecipeAlone,
	"ProceduralDungeon.RoomAuthoring.Tools.LoadRefusalLeavesTheWorkingRecipeAlone",
	RoomAuthorToolsTestFlags)

bool FRoomAuthorLoadRefusalLeavesTheWorkingRecipeAlone::RunTest(const FString&)
{
	FString Status;

	TestFalse(TEXT("a null working recipe is refused"),
		URoomAuthorTools::LoadRecipeFromLibrary(TEXT("Generic/Anything"), nullptr, Status));
	TestTrue(TEXT("the refusal says so rather than reporting a load"),
		Status.StartsWith(TEXT("REFUSED")));

	// A room in progress, with values nothing would produce by accident, so an assertion below
	// cannot pass against a freshly seeded recipe.
	URoomRecipeAsset* Working = NewObject<URoomRecipeAsset>();
	Working->RoomName = TEXT("UnsavedWorkInProgress");
	Working->RoomType = TEXT("MidEdit");
	Working->bExitNorth = Working->bExitEast = Working->bExitSouth = Working->bExitWest = false;
	FRoomChamber Whole;
	Whole.GridX = 0; Whole.GridY = 0;
	Whole.Width = Working->BoundingWidth; Whole.Length = Working->BoundingLength;
	Working->Chambers.Add(Whole);

	const FName NameBefore = Working->RoomName;
	const FName TypeBefore = Working->RoomType;
	const int32 ChambersBefore = Working->Chambers.Num();

	// A label no scan could have produced -- the listing derives labels from package paths, so
	// this one cannot collide with a real recipe however the library is filled.
	TestFalse(TEXT("an unknown label is refused"),
		URoomAuthorTools::LoadRecipeFromLibrary(
			TEXT("NoSuchType/NoSuchRoom_LoadGuardTest"), Working, Status));
	TestTrue(FString::Printf(TEXT("the refusal names the label it could not find: %s"), *Status),
		Status.Contains(TEXT("NoSuchRoom_LoadGuardTest")));

	TestEqual(TEXT("the in-progress room keeps its name"), Working->RoomName, NameBefore);
	TestEqual(TEXT("the in-progress room keeps its type"), Working->RoomType, TypeBefore);
	TestEqual(TEXT("the in-progress room keeps its chambers"),
		Working->Chambers.Num(), ChambersBefore);
	return true;
}

/**
 * Every label the listing offers must actually open.
 *
 * Stated over whatever the library happens to hold rather than against a named fixture, so the
 * test keeps its meaning when recipes are added or removed and does not become a reason not to
 * delete a throwaway room. It is the pairing that matters: GetLibraryRecipeLabels and
 * LoadRecipeFromLibrary run the same scan, and a label that lists but will not open means those
 * two walks have drifted -- which is the specific failure the shared scan exists to prevent.
 *
 * An empty library asserts nothing beyond the listing not lying about being empty. That is
 * honest: there is no round trip to check when there is nothing filed.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomAuthorEveryListedRecipeOpens,
	"ProceduralDungeon.RoomAuthoring.Tools.EveryListedRecipeOpens",
	RoomAuthorToolsTestFlags)

bool FRoomAuthorEveryListedRecipeOpens::RunTest(const FString&)
{
	const TArray<FString> Labels = URoomAuthorTools::GetLibraryRecipeLabels();

	// Logged, not asserted. An empty library is a legitimate state, but a test that quietly
	// checks nothing and reports Success is indistinguishable from one that checked everything
	// -- so the count goes in the log, where a reader can see what this run actually covered.
	AddInfo(FString::Printf(TEXT("the library listed %d recipe(s): %s"),
		Labels.Num(), *FString::Join(Labels, TEXT(", "))));

	URoomRecipeAsset* Working = NewObject<URoomRecipeAsset>();
	for (const FString& Label : Labels)
	{
		FString Status;
		if (!TestTrue(FString::Printf(TEXT("a listed recipe opens: %s -- %s"), *Label, *Status),
			URoomAuthorTools::LoadRecipeFromLibrary(Label, Working, Status)))
		{
			continue;
		}

		TestTrue(FString::Printf(TEXT("opening %s reports OK: %s"), *Label, *Status),
			Status.StartsWith(TEXT("OK")));
		TestFalse(FString::Printf(TEXT("%s arrives with a name"), *Label),
			Working->RoomName.IsNone());
		TestTrue(FString::Printf(TEXT("%s arrives with at least one chamber"), *Label),
			Working->Chambers.Num() > 0);
	}
	return true;
}

// --- The authored bake refuses before it writes -------------------------------------------

// --- The bake -----------------------------------------------------------------------------
//
// BakeRefusesARoomItCannotFile, BakeRefusesARoomWithNoExitFills and
// BakeStampsTheExitContractOntoTheAsset are NOT ported. All three assert against
// URectRoomAsset -- its exit-fill slots, its bGeometryIsAuthored flag -- and this project
// bakes Master_Room_C children instead. Porting tests for a contract that is being replaced
// would pin the wrong shape in place. They come back, rewritten, with the bake in step 6.

// --- Which actors survive the bake alive --------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomAuthorLiveFixtureIsAnAllowList,
	"ProceduralDungeon.RoomAuthoring.Tools.LiveFixtureComponentsAreAnAllowList",
	RoomAuthorToolsTestFlags)

bool FRoomAuthorLiveFixtureIsAnAllowList::RunTest(const FString&)
{
	// The bake flattens actors to static meshes. An actor carrying any of these four emits
	// something a mesh cannot -- light, particles, sound, a decal -- so it has to be carried
	// through whole instead. A torch prop is a mesh PLUS a point light PLUS a Niagara flame;
	// harvesting only its mesh is what produced a lit-looking, unlit room in BP_RectRoom_hall.
	TestTrue(TEXT("a point light makes an actor a live fixture"),
		URoomAuthorTools::IsLiveFixtureComponent(NewObject<UPointLightComponent>()));
	TestTrue(TEXT("a particle/Niagara system makes an actor a live fixture"),
		URoomAuthorTools::IsLiveFixtureComponent(NewObject<UParticleSystemComponent>()));
	TestTrue(TEXT("an audio emitter makes an actor a live fixture"),
		URoomAuthorTools::IsLiveFixtureComponent(NewObject<UAudioComponent>()));
	TestTrue(TEXT("a decal makes an actor a live fixture"),
		URoomAuthorTools::IsLiveFixtureComponent(NewObject<UDecalComponent>()));

	// THE WRONG IMPLEMENTATION THIS FAILS AGAINST is "anything that is not a static mesh".
	// Ordinary kit pieces carry collision primitives and editor billboards, and promoting them
	// would spawn a child actor per wall -- turning the one baked actor back into the crowd of
	// loose actors that baking exists to collapse.
	TestFalse(TEXT("a static mesh flattens, as it always did"),
		URoomAuthorTools::IsLiveFixtureComponent(NewObject<UStaticMeshComponent>()));
	TestFalse(TEXT("a collision box does not earn an actor its life"),
		URoomAuthorTools::IsLiveFixtureComponent(NewObject<UBoxComponent>()));
	TestFalse(TEXT("an editor billboard does not earn an actor its life"),
		URoomAuthorTools::IsLiveFixtureComponent(NewObject<UBillboardComponent>()));

	// Null is asked about in the collection loop's inner iteration, where a component array can
	// legitimately hold one.
	TestFalse(TEXT("a null component is not a live fixture"),
		URoomAuthorTools::IsLiveFixtureComponent(nullptr));

	return true;
}

// --- Kit-set resolution ---------------------------------------------------------------------

/**
 * The panel's getter must hand back the OVERRIDE, not the resolved piece.
 *
 * This is the one trap the kit-set change introduces, and it is invisible if you only look at
 * generated geometry, because a room built from over-specified chambers looks perfectly right.
 * The damage is that it stops following its kit: Get resolves, the author changes one unrelated
 * field, Set writes all seven back as explicit overrides, and from then on editing the kit set
 * no longer reaches this room. Kit sets exist to remove exactly that duplication.
 *
 * GenerateRoom is the opposite case and is asserted the other way round -- it must never see an
 * empty path. The two are covered together here so the difference cannot be quietly collapsed.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomAuthorPiecePanelShowsOverridesNotResolvedValues,
	"ProceduralDungeon.RoomAuthoring.Tools.ThePiecePanelShowsOverridesNotInheritedValues",
	RoomAuthorToolsTestFlags)

bool FRoomAuthorPiecePanelShowsOverridesNotResolvedValues::RunTest(const FString&)
{
	URoomRecipeAsset* Recipe = NewObject<URoomRecipeAsset>();
	Recipe->KitSet = NewObject<UDungeonKitSet>();
	Recipe->Chambers.Add(FRoomChamber());

	FString Wall, NW, NE, SE, SW, Floor, Ceiling;
	float Density = 0.0f;
	URoomAuthorTools::GetChamberPieces(Recipe, 0, Wall, NW, NE, SE, SW, Floor, Ceiling, Density);

	// Empty, even though the chamber resolves to a real wall. Empty is what "inherit" looks
	// like, and the panel has to be able to show it.
	TestTrue(TEXT("an un-overridden wall reads back empty"), Wall.IsEmpty());
	TestTrue(TEXT("an un-overridden floor reads back empty"), Floor.IsEmpty());
	TestTrue(TEXT("an un-overridden corner reads back empty"), NW.IsEmpty());

	// ...while the value GenerateRoom would hand PCG is the kit's, and is never empty.
	TestFalse(TEXT("but the resolved wall is not empty"),
		Recipe->ResolveWall(Recipe->Chambers[0]).ToString().IsEmpty());
	TestEqual(TEXT("and it is the kit's wall"),
		Recipe->ResolveWall(Recipe->Chambers[0]).ToString(), Recipe->KitSet->Wall.ToString());

	// The round trip must not manufacture overrides out of inherited values.
	URoomAuthorTools::SetChamberPieces(Recipe, 0, Wall, NW, NE, SE, SW, Floor, Ceiling, 0.25f);

	TestTrue(TEXT("a Get/Set round trip leaves the wall inherited"),
		Recipe->Chambers[0].WallCls.ToString().IsEmpty());
	TestTrue(TEXT("a Get/Set round trip leaves the floor inherited"),
		Recipe->Chambers[0].FloorCls.ToString().IsEmpty());
	TestEqual(TEXT("and the room still resolves to the kit after the round trip"),
		Recipe->ResolveWall(Recipe->Chambers[0]).ToString(), Recipe->KitSet->Wall.ToString());

	return true;
}

// --- The panel's "inherit" vocabulary -------------------------------------------------------

/**
 * What the panel shows beside an EMPTY slot must come from the kit, never from the chamber.
 *
 * ThePiecePanelShowsOverridesNotInheritedValues pins the getter's silence; this pins the other
 * half of the same contract. The panel has to render "(inherit - Wall_01_E_straight_large)"
 * somewhere, and the only safe source for that string is the kit itself: read the chamber to
 * build it and a Get/Set round trip starts writing inherited values back as overrides, which
 * is precisely the freeze kit sets exist to prevent.
 *
 * So this asserts the hostile case directly -- an override IS set, and the inherited name still
 * reports the kit's piece rather than the override's.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomAuthorInheritedNameComesFromTheKit,
	"ProceduralDungeon.RoomAuthoring.Tools.TheInheritedNameComesFromTheKitNotTheOverride",
	RoomAuthorToolsTestFlags)

bool FRoomAuthorInheritedNameComesFromTheKit::RunTest(const FString&)
{
	URoomRecipeAsset* Recipe = NewObject<URoomRecipeAsset>();
	Recipe->KitSet = NewObject<UDungeonKitSet>();
	Recipe->Chambers.Add(FRoomChamber());

	// Deliberately hostile: every slot the panel can show is overridden with something that is
	// NOT the kit's piece, so a lookup that reads the chamber cannot accidentally pass.
	Recipe->Chambers[0].WallCls = FSoftClassPath(
		TEXT("/Game/Nowhere/BP_COMP_Wall_Overridden.BP_COMP_Wall_Overridden_C"));
	Recipe->Chambers[0].FloorCls = FSoftClassPath(
		TEXT("/Game/Nowhere/BP_COMP_Floor_Overridden.BP_COMP_Floor_Overridden_C"));
	Recipe->Chambers[0].CornerNWCls = FSoftClassPath(
		TEXT("/Game/Nowhere/BP_COMP_Corner_Overridden.BP_COMP_Corner_Overridden_C"));

	TestEqual(TEXT("the inherited wall is the kit's wall, not the override"),
		URoomAuthorTools::GetInheritedPieceDisplayName(Recipe, TEXT("Wall")),
		URoomAuthorTools::DisplayNameForPath(Recipe->KitSet->Wall.ToString()));

	TestEqual(TEXT("the inherited floor is the kit's floor, not the override"),
		URoomAuthorTools::GetInheritedPieceDisplayName(Recipe, TEXT("Floor")),
		URoomAuthorTools::DisplayNameForPath(Recipe->KitSet->Floor.ToString()));

	// One kit asset backs all four corners -- the emitter rotates it -- so every corner slot
	// inherits the same name. Asserting all four keeps that from being quietly split later.
	const FString KitCorner =
		URoomAuthorTools::DisplayNameForPath(Recipe->KitSet->Corner.ToString());
	TestEqual(TEXT("NW inherits the kit's corner"),
		URoomAuthorTools::GetInheritedPieceDisplayName(Recipe, TEXT("CornerNW")), KitCorner);
	TestEqual(TEXT("NE inherits the kit's corner"),
		URoomAuthorTools::GetInheritedPieceDisplayName(Recipe, TEXT("CornerNE")), KitCorner);
	TestEqual(TEXT("SE inherits the kit's corner"),
		URoomAuthorTools::GetInheritedPieceDisplayName(Recipe, TEXT("CornerSE")), KitCorner);
	TestEqual(TEXT("SW inherits the kit's corner"),
		URoomAuthorTools::GetInheritedPieceDisplayName(Recipe, TEXT("CornerSW")), KitCorner);

	// The chamber is untouched by the question. Asking must never be a write.
	TestEqual(TEXT("asking for the inherited name leaves the override alone"),
		Recipe->Chambers[0].WallCls.ToString(),
		FString(TEXT("/Game/Nowhere/BP_COMP_Wall_Overridden.BP_COMP_Wall_Overridden_C")));

	return true;
}

/**
 * A slot the kit leaves empty must report empty, not a plausible-looking guess.
 *
 * CeilingMesh is the live case: UDungeonKitSet ships it empty on purpose, because the census
 * found no shipping room that named one. The panel therefore has to render "(inherit - none)"
 * rather than a piece name, and an unknown slot name has to be as harmless as it is silent.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomAuthorInheritedNameIsEmptyWhenTheKitIs,
	"ProceduralDungeon.RoomAuthoring.Tools.AnEmptyKitSlotInheritsAnEmptyName",
	RoomAuthorToolsTestFlags)

bool FRoomAuthorInheritedNameIsEmptyWhenTheKitIs::RunTest(const FString&)
{
	URoomRecipeAsset* Recipe = NewObject<URoomRecipeAsset>();
	Recipe->KitSet = NewObject<UDungeonKitSet>();

	TestTrue(TEXT("the kit really does ship without a ceiling"),
		Recipe->KitSet->CeilingMesh.ToString().IsEmpty());
	TestTrue(TEXT("so the ceiling inherits an empty name"),
		URoomAuthorTools::GetInheritedPieceDisplayName(Recipe, TEXT("CeilingMesh")).IsEmpty());

	TestTrue(TEXT("an unknown slot name inherits nothing rather than guessing"),
		URoomAuthorTools::GetInheritedPieceDisplayName(Recipe, TEXT("Doorframe")).IsEmpty());
	TestTrue(TEXT("a null recipe inherits nothing rather than crashing"),
		URoomAuthorTools::GetInheritedPieceDisplayName(nullptr, TEXT("Wall")).IsEmpty());

	return true;
}

/**
 * A recipe with NO kit set still inherits, from the class default.
 *
 * URoomRecipeAsset::EffectiveKit falls back to GetDefault<UDungeonKitSet>(), which carries the
 * six-asset census in its constructor -- that fallback is what makes a DA_KitSet_* asset a
 * convenience rather than a requirement. The panel must show the same thing the generator will
 * actually use, so the inherited name has to follow that fallback rather than going blank.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomAuthorInheritedNameFollowsTheNullKitFallback,
	"ProceduralDungeon.RoomAuthoring.Tools.ARoomWithNoKitInheritsTheBuiltInCensus",
	RoomAuthorToolsTestFlags)

bool FRoomAuthorInheritedNameFollowsTheNullKitFallback::RunTest(const FString&)
{
	URoomRecipeAsset* Recipe = NewObject<URoomRecipeAsset>();
	Recipe->KitSet = nullptr;

	TestEqual(TEXT("a kit-less room inherits the built-in census wall"),
		URoomAuthorTools::GetInheritedPieceDisplayName(Recipe, TEXT("Wall")),
		URoomAuthorTools::DisplayNameForPath(GetDefault<UDungeonKitSet>()->Wall.ToString()));

	TestFalse(TEXT("and that name is not empty, so the panel has something to show"),
		URoomAuthorTools::GetInheritedPieceDisplayName(Recipe, TEXT("Wall")).IsEmpty());

	return true;
}

/**
 * The room panel needs to CHANGE the kit, and URoomRecipeAsset::KitSet is BlueprintReadOnly.
 *
 * Read-only on purpose -- it is the room's identity, not a scratch field -- so the one way a
 * widget may move it goes through here, and the proof that it worked is that the chambers'
 * inherited values follow. Setting the pointer without the inheritance moving with it would be
 * a field that looks connected and is not.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomAuthorSettingTheKitMovesWhatChambersInherit,
	"ProceduralDungeon.RoomAuthoring.Tools.SettingTheKitMovesWhatChambersInherit",
	RoomAuthorToolsTestFlags)

bool FRoomAuthorSettingTheKitMovesWhatChambersInherit::RunTest(const FString&)
{
	URoomRecipeAsset* Recipe = NewObject<URoomRecipeAsset>();
	Recipe->Chambers.Add(FRoomChamber());

	UDungeonKitSet* Other = NewObject<UDungeonKitSet>();
	Other->Wall = FSoftClassPath(TEXT("/Game/Other/BP_COMP_Wall_Other.BP_COMP_Wall_Other_C"));

	URoomAuthorTools::SetRoomKitSet(Recipe, Other);

	TestEqual(TEXT("the room now points at the kit it was given"),
		ToRawPtr(Recipe->KitSet), Other);
	TestEqual(TEXT("and an un-overridden chamber resolves through it"),
		Recipe->ResolveWall(Recipe->Chambers[0]).ToString(), Other->Wall.ToString());
	TestEqual(TEXT("and the panel reports it as the inherited name"),
		URoomAuthorTools::GetInheritedPieceDisplayName(Recipe, TEXT("Wall")),
		URoomAuthorTools::DisplayNameForPath(Other->Wall.ToString()));

	// Clearing it is a legal move back to the built-in census, not a refusal.
	URoomAuthorTools::SetRoomKitSet(Recipe, nullptr);
	TestNull(TEXT("clearing the kit is allowed"), ToRawPtr(Recipe->KitSet));

	return true;
}

/**
 * The combo entry that MEANS inherit, and what it must not collide with.
 *
 * The piece panel fills each combo from GetPieceDisplayNames and reads it back through
 * PathForDisplayName, so "inherit" has to be expressible in that same vocabulary: an entry the
 * author can select, which reads back as the empty path that means inherit. The label is built
 * in C++ rather than concatenated in the widget graph precisely so this collision can be
 * asserted -- a label that happened to match a real display name would silently write that
 * piece as an override, which is the failure the entry exists to prevent.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomAuthorInheritLabelIsNotAPieceName,
	"ProceduralDungeon.RoomAuthoring.Tools.TheInheritEntryReadsBackAsInherit",
	RoomAuthorToolsTestFlags)

bool FRoomAuthorInheritLabelIsNotAPieceName::RunTest(const FString&)
{
	URoomRecipeAsset* Recipe = NewObject<URoomRecipeAsset>();
	Recipe->KitSet = NewObject<UDungeonKitSet>();

	const FString WallLabel = URoomAuthorTools::GetInheritedPieceLabel(Recipe, TEXT("Wall"));

	// It has to SAY what is inherited, or the author cannot tell one inherited slot from another.
	TestTrue(TEXT("the label carries the inherited piece name"),
		WallLabel.Contains(
			URoomAuthorTools::DisplayNameForPath(Recipe->KitSet->Wall.ToString())));

	// ...and it must round-trip to empty, which is what actually clears the override. An entry
	// that resolved to a real path would freeze the slot the moment the panel pushed it back.
	TestTrue(TEXT("selecting the inherit entry reads back as an empty path"),
		URoomAuthorTools::PathForDisplayName(TEXT("Wall"), false, WallLabel).IsEmpty());

	// A kit slot that ships empty still needs an entry to select -- otherwise a ceiling cannot
	// be returned to inheriting once it has been overridden.
	const FString CeilingLabel =
		URoomAuthorTools::GetInheritedPieceLabel(Recipe, TEXT("CeilingMesh"));
	TestFalse(TEXT("an empty kit slot still offers an inherit entry"), CeilingLabel.IsEmpty());
	TestTrue(TEXT("and that entry also reads back as an empty path"),
		URoomAuthorTools::PathForDisplayName(TEXT("Ceiling(StaticMesh)"), true,
		                                     CeilingLabel).IsEmpty());

	// The two must be distinguishable, or "inherits nothing" and "inherits a wall" look alike.
	TestNotEqual(TEXT("inheriting nothing does not read like inheriting a piece"),
		CeilingLabel, WallLabel);

	return true;
}

// --- The kit-set picker ---------------------------------------------------------------------

/**
 * "No kit set" is a real, reachable state and the picker must be able to say it.
 *
 * A room with no set resolves through the class default, which carries the census -- that is
 * where every room starts and it has to stay selectable once it has been left. So the list is
 * never empty, and its first entry means "the built-in census".
 *
 * The entry is asserted by its SHAPE rather than its wording: it is bracketed, and an asset
 * name cannot contain a bracket, so it cannot collide with a real kit set however the library
 * grows. That is the same guarantee GetInheritedPieceLabel relies on, and pinning the property
 * rather than the string keeps this test from breaking when the wording is tuned.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomAuthorKitPickerAlwaysOffersTheBuiltIn,
	"ProceduralDungeon.RoomAuthoring.Tools.TheKitPickerAlwaysOffersTheBuiltInCensus",
	RoomAuthorToolsTestFlags)

bool FRoomAuthorKitPickerAlwaysOffersTheBuiltIn::RunTest(const FString&)
{
	const TArray<FString> Labels = URoomAuthorTools::GetKitSetLabels();

	if (!TestTrue(TEXT("the picker always offers at least one entry"), Labels.Num() >= 1))
	{
		return false;
	}

	const FString BuiltIn = Labels[0];
	TestTrue(TEXT("the built-in entry is bracketed, so no asset name can collide with it"),
		BuiltIn.StartsWith(TEXT("(")) && BuiltIn.EndsWith(TEXT(")")));
	TestNull(TEXT("and it resolves to no kit set at all"),
		URoomAuthorTools::KitSetForLabel(BuiltIn));

	// A label nobody offers must not resolve to something arbitrary.
	TestNull(TEXT("an unknown label resolves to nothing"),
		URoomAuthorTools::KitSetForLabel(TEXT("DA_KitSet_NoSuchThing")));

	URoomRecipeAsset* Recipe = NewObject<URoomRecipeAsset>();
	Recipe->KitSet = nullptr;

	TestEqual(TEXT("a room with no kit reports the built-in entry"),
		URoomAuthorTools::LabelForRoomKitSet(Recipe), BuiltIn);
	TestEqual(TEXT("and so does no room at all"),
		URoomAuthorTools::LabelForRoomKitSet(nullptr), BuiltIn);

	// The panel selects by label, so what a room reports has to be something the list offers.
	// If it were not, SelectOption would fall back to index 0 and the next push would clear the
	// room's kit -- the same freeze the piece combos had.
	TestTrue(TEXT("what a room reports is an entry the picker offers"),
		Labels.Contains(URoomAuthorTools::LabelForRoomKitSet(Recipe)));

	return true;
}

/**
 * Every kit set the picker lists must open, and must report the label it was listed under.
 *
 * The round trip is the point. The panel resolves a label to an asset when the author picks
 * one, and turns an asset back into a label when it repaints; if those two disagree the combo
 * shows the wrong kit, or -- worse -- shows the built-in entry for a room that has a kit, and
 * the next push clears it.
 *
 * Shaped like EveryListedRecipeOpens: the count is logged rather than asserted, because an
 * empty kit library is a legitimate state and a test that silently checks nothing should still
 * say so out loud.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomAuthorEveryListedKitSetResolves,
	"ProceduralDungeon.RoomAuthoring.Tools.EveryListedKitSetResolvesAndRoundTrips",
	RoomAuthorToolsTestFlags)

bool FRoomAuthorEveryListedKitSetResolves::RunTest(const FString&)
{
	const TArray<FString> Labels = URoomAuthorTools::GetKitSetLabels();

	AddInfo(FString::Printf(TEXT("the picker listed %d entr(y/ies): %s"),
		Labels.Num(), *FString::Join(Labels, TEXT(", "))));

	URoomRecipeAsset* Recipe = NewObject<URoomRecipeAsset>();

	// Index 0 is the built-in entry and is covered by the test above; everything after it names
	// an asset.
	for (int32 Index = 1; Index < Labels.Num(); ++Index)
	{
		const FString& Label = Labels[Index];

		UDungeonKitSet* Kit = URoomAuthorTools::KitSetForLabel(Label);
		if (!TestNotNull(*FString::Printf(TEXT("a listed kit set opens: %s"), *Label), Kit))
		{
			continue;
		}

		URoomAuthorTools::SetRoomKitSet(Recipe, Kit);
		TestEqual(*FString::Printf(TEXT("%s reports the label it was listed under"), *Label),
			URoomAuthorTools::LabelForRoomKitSet(Recipe), Label);
	}

	// The loop above is vacuous until the project has a kit set asset, so this pins the
	// property that has to hold whether the library is empty or not: a room that HAS a kit must
	// never be reported as having none. If it were, the picker would show the built-in entry
	// and the next push would clear the room's kit -- the freeze the piece combos just had,
	// wearing a different hat. A transient kit is the sharpest case, because it is the one the
	// scan definitely cannot find.
	UDungeonKitSet* Unlisted = NewObject<UDungeonKitSet>();
	URoomAuthorTools::SetRoomKitSet(Recipe, Unlisted);

	const FString UnlistedLabel = URoomAuthorTools::LabelForRoomKitSet(Recipe);
	TestFalse(TEXT("a room with a kit never reports an empty label"), UnlistedLabel.IsEmpty());

	if (Labels.Num() > 0)
	{
		TestNotEqual(TEXT("and never reports the built-in entry, which would mean no kit"),
			UnlistedLabel, Labels[0]);
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
