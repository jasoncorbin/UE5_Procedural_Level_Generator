// The room-authoring tool's presentation layer: the Blueprint-facing door into
// RoomAuthoring/, and the only place that knows how a chamber becomes a PCG volume.
//
// OUTSIDE the determinism boundary. int32 and float appear freely here because reflection is
// int32-native and PCG's parameters are floats; every one of them widens into
// RoomAuthor::FRoomLayout's int64s through URoomRecipeAsset::MakeLayout on the way in.
//
// This library restates NO rule that RoomAuthorValidate.h already encodes. ValidateRecipe()
// forwards; GenerateRoom() calls the same validator before it spawns anything and refuses on
// failure. The one piece of geometry decided here rather than there is the interior arch's
// narrowing to a single tile (design spec 4b), and it is derived FROM the validated span
// rather than recomputed beside it -- see NarrowToSingleTile.
//
// Mirrors RectDungeon/RectRoomTools.h in shape on purpose: a UBlueprintFunctionLibrary of
// grouped setters and getters, because a widget graph cannot write a BlueprintReadOnly
// UPROPERTY and should not be the place a MarkPackageDirty() is remembered or forgotten.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "RoomAuthoring/RoomAuthorTypes.h"
#include "RoomAuthoring/RoomRecipeAsset.h"
#include "RoomAuthorTools.generated.h"

class AActor;
class UActorComponent;
class APCGVolume;

/**
 * One row of a DA_PieceTable_<Role>, resolved and ready for a combo box.
 *
 * DisplayName is derived, never authored: a table row is a class path and nothing else, so a
 * table gains a piece by gaining a path. Path is what goes to PCG.
 */
USTRUCT(BlueprintType)
struct FRoomAuthorPieceOption
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Room Authoring")
	FString DisplayName;

	UPROPERTY(BlueprintReadOnly, Category = "Room Authoring")
	FString Path;
};

UCLASS()
class PROCEDURALDUNGEON_API URoomAuthorTools : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// ---------------------------------------------------------------- paths and conventions

	/**
	 * The folder the widget scans for DA_PieceTable_* and the folder the authoring level lives
	 * in. A code constant rather than a widget field for the same reason
	 * URectRoomTools::RoomLibraryRoot() is one: two copies of a path can disagree.
	 */
	static const TCHAR* AuthoringRoot();

	/** The ONLY level this tool will generate into. See GenerateRoom's refusal. */
	static const TCHAR* AuthoringLevelPath();

	/** The emitter. One graph, run once per chamber with that chamber's parameters. */
	static const TCHAR* RoomGenGraphPath();

	/**
	 * Every actor this tool spawns carries this tag, PCG volumes included.
	 *
	 * The sweep tag, not a bookkeeping list: PCG-spawned actors outlive the volume that
	 * spawned them (design spec 4b, measured -- 56 survived), so cleanup cannot rely on the
	 * volume still knowing about them. A tag survives everything, including a level reload.
	 */
	static const FName& AuthoringTag();

	// ------------------------------------------------------------------------- piece tables

	/**
	 * The Role field of every DA_PieceTable_* asset under AuthoringRoot(), sorted.
	 *
	 * Scanned, never listed: "adding a piece to the game must stay 'add an entry to a table'"
	 * extends to adding a whole ROLE, which is why nothing here names Wall or Corner.
	 */
	UFUNCTION(BlueprintCallable, Category = "Room Authoring|Pieces")
	static TArray<FString> GetPieceRoles();

	/**
	 * Every resolvable entry of the table whose Role matches, in table order.
	 *
	 * DA_PieceTable's Pieces array is TArray<FSoftClassPath> and does NOT enforce an AActor
	 * base, and its Meshes array is not type-enforced either -- so every row is LOADED and
	 * type-checked here, and a row that fails to resolve is skipped rather than handed to PCG
	 * as a path that will silently spawn nothing.
	 *
	 * @param Role         the table's Role value, from GetPieceRoles().
	 * @param bWantMeshes  read Meshes rather than Pieces. The ceiling row is a Static Mesh
	 *                     Spawner, so its table carries meshes -- see FRoomChamber::CeilingMesh.
	 */
	UFUNCTION(BlueprintCallable, Category = "Room Authoring|Pieces")
	static TArray<FRoomAuthorPieceOption> GetPieceOptions(const FString& Role, bool bWantMeshes);

	/**
	 * Just the display names of GetPieceOptions, for a combo box's option list.
	 *
	 * A combo box holds strings and hands one back, so the panel never carries a path at all --
	 * it asks PathForDisplayName() at commit time. That is what keeps "add a piece" to "add a
	 * table row": no widget field, anywhere, holds a piece path.
	 */
	UFUNCTION(BlueprintCallable, Category = "Room Authoring|Pieces")
	static TArray<FString> GetPieceDisplayNames(const FString& Role, bool bWantMeshes);

	/** The path behind a display name, or empty when the table no longer offers it. */
	UFUNCTION(BlueprintCallable, Category = "Room Authoring|Pieces")
	static FString PathForDisplayName(const FString& Role, bool bWantMeshes,
	                                  const FString& DisplayName);

	/** DisplayNameForPath, reachable from a widget graph -- the reverse trip, on load. */
	UFUNCTION(BlueprintCallable, Category = "Room Authoring|Pieces",
		meta = (DisplayName = "Display Name For Path"))
	static FString GetDisplayNameForPath(const FString& ObjectPath);

	/**
	 * The RoomType folders that already exist under the room library.
	 *
	 * Offered as presets beside a free-text box, never INSTEAD of one: URectRoomAsset::RoomType
	 * is free-form by design, and inventing a room type must not require a C++ change.
	 */
	UFUNCTION(BlueprintCallable, Category = "Room Authoring|Recipe")
	static TArray<FString> GetKnownRoomTypes();

	/**
	 * BP_COMP_Wall_01_E_straight_large_C -> Wall_01_E_straight_large.
	 *
	 * Pure and static so it is testable without an editor. Strips a leading BP_COMP_ or MOD_
	 * and a trailing _C; leaves the rest alone, because the underscored middle is the only
	 * thing telling one corner variant from another.
	 */
	static FString DisplayNameForPath(const FString& ObjectPath);

	// ------------------------------------------------------------------------- recipe state

	/**
	 * A transient recipe for the panel to edit. Stage 5 is what writes one to disk.
	 *
	 * Transient on purpose: an authoring session that has not been saved yet must not leave a
	 * half-built asset in the content browser for the library scan to find.
	 */
	UFUNCTION(BlueprintCallable, Category = "Room Authoring|Recipe")
	static URoomRecipeAsset* NewWorkingRecipe();

	/** Write every Room-panel field at once. Does nothing on a null recipe. */
	UFUNCTION(BlueprintCallable, Category = "Room Authoring|Recipe")
	static void SetRoomFields(URoomRecipeAsset* Recipe, FName RoomName, FName RoomType,
	                          int32 BoundingWidth, int32 BoundingLength,
	                          bool bExitNorth, bool bExitEast, bool bExitSouth, bool bExitWest,
	                          int32 Seed, int32 Weight);

	/** Read every Room-panel field at once. Leaves the outputs untouched on a null recipe. */
	UFUNCTION(BlueprintCallable, Category = "Room Authoring|Recipe")
	static void GetRoomFields(const URoomRecipeAsset* Recipe, FName& RoomName, FName& RoomType,
	                          int32& BoundingWidth, int32& BoundingLength,
	                          bool& bExitNorth, bool& bExitEast, bool& bExitSouth, bool& bExitWest,
	                          int32& Seed, int32& Weight);

	/**
	 * Write the working recipe to disk as DA_RoomRecipe_<RoomName>, under
	 * Content/RectDungeon/Rooms/<RoomType>/. Step 4 of the design spec's section 5, and ONLY
	 * step 4.
	 *
	 * The other three steps of Save to Library -- collecting the placed actors, writing the
	 * BP_Room_<Name> prefab, and writing DA_Room_<Name> with the BakedRoomYawSteps/Offset that
	 * land the prefab body on the room frame -- are Stage 5 proper and are deliberately absent.
	 * This is the part with no design left in it: the recipe is already a UDataAsset and this
	 * only gives it a package. A room saved with this can be re-opened for editing; it is NOT
	 * yet placeable by the dungeon generator, because nothing has baked its geometry.
	 *
	 * Overwrites an existing recipe of the same name in place, so the asset keeps its identity
	 * and anything referencing it survives a re-save.
	 *
	 * REFUSES on an unnamed room or an untyped one -- both are part of the path, and a recipe
	 * saved to Rooms//DA_RoomRecipe_ is a file nobody will find again.
	 */
	UFUNCTION(BlueprintCallable, Category = "Room Authoring|Recipe")
	static bool SaveRecipeToLibrary(const URoomRecipeAsset* Recipe, FString& OutSavedPath,
	                                FString& OutStatus);

	/**
	 * Every recipe the library holds, as "<RoomType>/<RoomName>" labels in a stable order.
	 *
	 * Pairs with LoadRecipeFromLibrary exactly the way GetPieceDisplayNames pairs with
	 * PathForDisplayName: the label a combo shows and the asset it opens come out of ONE scan
	 * in ONE order. Two independent walks is how a combo starts opening the wrong room.
	 *
	 * Derived from the package PATH, not from each asset's own fields, so listing the library
	 * never loads it. A hand-moved recipe therefore lists where it is FILED rather than what it
	 * calls itself, and LoadRecipeFromLibrary reports that disagreement when it opens one.
	 */
	UFUNCTION(BlueprintCallable, Category = "Room Authoring|Recipe")
	static TArray<FString> GetLibraryRecipeLabels();

	/**
	 * Open a saved recipe INTO the working recipe, overwriting it in place.
	 *
	 * In place, rather than handing back a fresh object, because the panel holds WorkingRecipe
	 * and its Construct guard now depends on that pointer staying valid. Returning a new object
	 * would leave the widget's field and the loaded room as two different rooms for as long as
	 * any caller forgot to reassign. NewWorkingRecipe stays the only thing that mints one.
	 *
	 * REFUSES before it copies anything, never half-way through, so a failed Load leaves the
	 * room you were editing exactly as it was: an unknown label and an asset that will not load
	 * are both refused with the working recipe untouched.
	 *
	 * A recipe that does NOT validate is loaded anyway, and the problem is reported in
	 * OutStatus instead. Refusing it would be the wrong call: Save already refuses to write an
	 * unbuildable room, so anything invalid in the library got there by hand -- and this panel
	 * is the only thing that could repair it. Refusing to open the one asset that needs fixing
	 * is how a broken recipe becomes permanent.
	 */
	UFUNCTION(BlueprintCallable, Category = "Room Authoring|Recipe")
	static bool LoadRecipeFromLibrary(const FString& Label, URoomRecipeAsset* Working,
	                                  FString& OutStatus);

	/**
	 * BakeAuthoredRoom and ApplyExitContractToAsset are DELIBERATELY ABSENT -- step 6.
	 *
	 * Level_Creator_1 declared them here and wrote a URectRoomAsset plus a baked prefab
	 * beside it. This project's rooms are Master_Room_C children, so the bake's OUTPUT half
	 * is a rewrite against a different contract rather than a port. Its INPUT half -- the
	 * validated layout, the resolved openings, kit-set piece resolution -- is all present and
	 * tested below.
	 */

	/**
	 * Does this component emit something a baked static mesh cannot reproduce?
	 *
	 * True for light, particle/Niagara, audio and decal components. An actor carrying any of
	 * them is carried through the bake ALIVE, as a child actor, instead of being flattened to
	 * its meshes -- see CollectPlacedMeshes.
	 *
	 * Public and static for the same reason ApplyExitContractToAsset is: the bake's success
	 * path needs a live authoring world and cannot run headlessly, so the DECISION is prised
	 * out to where an automation test can reach it. This is the whole of the rule; the loop
	 * over an actor's components around it is glue.
	 *
	 * Deliberately an allow-list. Asking "is this not a static mesh" instead would promote
	 * every kit piece with a collision box or a billboard to a spawned actor, which is the cost
	 * baking exists to remove.
	 */
	static bool IsLiveFixtureComponent(const UActorComponent* Component);

	/**
	 * Shrink-wrap the bounding rect onto the chambers and move the room to the origin.
	 *
	 * RoomAuthor::FitBoundsToChambers, reachable from a widget graph, writing the fitted rect
	 * and the shifted chamber origins back into the recipe.
	 *
	 * The panel calls this on every push, which is what makes BoundingWidth/BoundingLength a
	 * READOUT rather than a field. They were editable through Stage 4 and the two numbers could
	 * disagree with the chambers; a bounding rect one tile larger than its floor then refused
	 * with a message about exits, because that is genuinely the first rule it broke. Deriving
	 * the rect removes the state that made that possible rather than improving the message.
	 *
	 * Safe on a null or chamber-less recipe: does nothing.
	 */
	UFUNCTION(BlueprintCallable, Category = "Room Authoring|Recipe")
	static void FitBoundsToChambers(URoomRecipeAsset* Recipe);

	/**
	 * Append a chamber and return its index, or INDEX_NONE on a null recipe.
	 *
	 * The new chamber inherits the selected chamber's dressing when there is one, so adding a
	 * second chamber to a room does not produce one dressed in nothing.
	 */
	UFUNCTION(BlueprintCallable, Category = "Room Authoring|Chambers")
	static int32 AddChamber(URoomRecipeAsset* Recipe, int32 CopyDressingFrom);

	/**
	 * Remove a chamber, and every connection opinion that named it.
	 *
	 * The re-indexing is the point: FRoomConnection is keyed by chamber INDEX, so removing
	 * chamber 1 would otherwise leave an opinion about "chambers 0 and 2" pointing at a
	 * different pair than the author meant.
	 */
	/**
	 * Remove a chamber AND everything attached to it, however deep.
	 *
	 * Cascading is not a convenience. A child derives its position from its parent, so a child
	 * whose parent is gone has nothing to derive from -- keeping it would mean silently
	 * re-rooting it at some coordinate nobody chose, and a chamber that quietly relocates is
	 * worse than one that leaves with its parent.
	 *
	 * Call GetDescendantCount FIRST if the panel should warn. Chamber indices below the removal
	 * are unchanged; everything above shifts down, and attachments and connections are re-keyed
	 * through one shared map so the two index spaces cannot drift apart.
	 */
	UFUNCTION(BlueprintCallable, Category = "Room Authoring|Chambers")
	static void RemoveChamber(URoomRecipeAsset* Recipe, int32 Index);

	/** How many chambers would ALSO go if this one were removed. 0 means it is a leaf. */
	UFUNCTION(BlueprintCallable, Category = "Room Authoring|Chambers")
	static int32 GetDescendantCount(const URoomRecipeAsset* Recipe, int32 Index);

	// -------------------------------------------------------------------------- attachment

	/**
	 * Attach a chamber to a side of an earlier one. ParentIndex of -1 makes it a root.
	 *
	 * A parent at or above this chamber's own index is refused by making it a ROOT rather than
	 * by storing something the validator will reject later -- the panel writes through here on
	 * a combo change, and a refusal the author only meets on the next Regenerate is a refusal
	 * about a decision they have already stopped thinking about.
	 *
	 * Writes intent only. Nothing moves until ApplyAttachments runs.
	 */
	UFUNCTION(BlueprintCallable, Category = "Room Authoring|Attachment")
	static void SetChamberAttachment(URoomRecipeAsset* Recipe, int32 Index,
	                                 int32 ParentIndex, ERoomSide Side);

	/** Read one chamber's attachment. bIsRoot is the only reliable test -- Side means nothing on a root. */
	UFUNCTION(BlueprintCallable, Category = "Room Authoring|Attachment")
	static void GetChamberAttachment(const URoomRecipeAsset* Recipe, int32 Index,
	                                 int32& ParentIndex, ERoomSide& Side, bool& bIsRoot);

	/**
	 * The free (chamber, side) slots, as combo box lines, in a stable order.
	 *
	 * ForChamberIndex = -1 asks on behalf of a chamber not added yet. Otherwise only chambers
	 * BELOW it are offered, and the slot it already occupies stays listed so its own attachment
	 * remains selectable.
	 *
	 * A side is unavailable for TWO reasons, and the second is the one the widget got wrong on
	 * its own (2026-08-31): something is already attached there, OR it is a chamber's own back
	 * side, facing its parent. Offering a back side put one chamber straight on top of another.
	 */
	UFUNCTION(BlueprintCallable, Category = "Room Authoring|Attachment")
	static TArray<FString> GetAttachTargetLabels(const URoomRecipeAsset* Recipe,
	                                             int32 ForChamberIndex);

	/** Turn a selected line from GetAttachTargetLabels back into a parent and a side. */
	UFUNCTION(BlueprintCallable, Category = "Room Authoring|Attachment")
	static bool ResolveAttachTarget(const URoomRecipeAsset* Recipe, int32 ForChamberIndex,
	                                int32 OptionIndex, int32& ParentIndex, ERoomSide& Side);

	/**
	 * Position every attached chamber from its parent, then fit the bounding rect.
	 *
	 * What the panel calls on every push, in place of FitBoundsToChambers -- it does that too,
	 * as its last step. Roots keep the coordinates they have, so a recipe written before
	 * attachments existed passes through unchanged apart from the fit.
	 *
	 * Returns false with OutError only on a structurally impossible attachment; ordinary
	 * geometry problems are still ValidateRecipe's to report.
	 */
	UFUNCTION(BlueprintCallable, Category = "Room Authoring|Attachment")
	static bool ApplyAttachments(URoomRecipeAsset* Recipe, FString& OutError);

	UFUNCTION(BlueprintCallable, Category = "Room Authoring|Chambers")
	static int32 GetChamberCount(const URoomRecipeAsset* Recipe);

	/** One line per chamber for the list box: index, origin and extent. */
	UFUNCTION(BlueprintCallable, Category = "Room Authoring|Chambers")
	static TArray<FString> GetChamberLabels(const URoomRecipeAsset* Recipe);

	UFUNCTION(BlueprintCallable, Category = "Room Authoring|Chambers")
	static void SetChamberShape(URoomRecipeAsset* Recipe, int32 Index,
	                            int32 GridX, int32 GridY, int32 Width, int32 Length);

	UFUNCTION(BlueprintCallable, Category = "Room Authoring|Chambers")
	static void GetChamberShape(const URoomRecipeAsset* Recipe, int32 Index,
	                            int32& GridX, int32& GridY, int32& Width, int32& Length);

	/**
	 * Write the seven dressing picks and the prop density in one call.
	 *
	 * Paths, not TSubclassOf: they come straight out of a combo box that got them from
	 * GetPieceOptions, and FRoomChamber holds soft paths precisely so a recipe does not drag
	 * the whole kit into memory. An empty path clears the slot.
	 */
	UFUNCTION(BlueprintCallable, Category = "Room Authoring|Chambers")
	static void SetChamberPieces(URoomRecipeAsset* Recipe, int32 Index,
	                             const FString& WallPath,
	                             const FString& CornerNWPath, const FString& CornerNEPath,
	                             const FString& CornerSEPath, const FString& CornerSWPath,
	                             const FString& FloorPath, const FString& CeilingMeshPath,
	                             float PropDensity);

	UFUNCTION(BlueprintCallable, Category = "Room Authoring|Chambers")
	static void GetChamberPieces(const URoomRecipeAsset* Recipe, int32 Index,
	                             FString& WallPath,
	                             FString& CornerNWPath, FString& CornerNEPath,
	                             FString& CornerSEPath, FString& CornerSWPath,
	                             FString& FloorPath, FString& CeilingMeshPath,
	                             float& PropDensity);

	// --------------------------------------------------------------------------- connections

	/**
	 * One line per DETECTED shared edge, in RoomAuthor::FindSharedEdges' stable order.
	 *
	 * Detected, not stored: the recipe holds only the pairs the author disagreed with, so this
	 * is recomputed from the chambers every time one moves. The stable ordering is what lets
	 * the panel's Nth toggle keep meaning the Nth edge.
	 */
	UFUNCTION(BlueprintCallable, Category = "Room Authoring|Connections")
	static TArray<FString> GetConnectionLabels(const URoomRecipeAsset* Recipe);

	/**
	 * True for each detected edge that is ARCHED, same order and length as the labels.
	 *
	 * An Open edge reads FALSE here, exactly as a walled one does. The two bool arrays are a
	 * pair and mean nothing apart: read GetConnectionOpen alongside this one.
	 */
	UFUNCTION(BlueprintCallable, Category = "Room Authoring|Connections")
	static TArray<bool> GetConnectionArches(const URoomRecipeAsset* Recipe);

	/** True for each detected edge whose wall is REMOVED, same order as the labels. */
	UFUNCTION(BlueprintCallable, Category = "Room Authoring|Connections")
	static TArray<bool> GetConnectionOpen(const URoomRecipeAsset* Recipe);

	/**
	 * Write the mode of each detected edge, positionally, from the panel's two checkbox columns.
	 *
	 * Both columns in ONE call, deliberately. Three modes do not fit in one bool, and two
	 * independent setters would let the panel push "not arched" for a row it means to leave
	 * Open and silently walls it up -- a mode lost between two nodes of a widget graph is
	 * invisible until someone counts the gateways.
	 *
	 * Open wins where both are ticked: it is the more destructive of the two and the one the
	 * author had to go out of their way to ask for.
	 *
	 * Extra entries are ignored and missing ones left alone, so a panel with a fixed number of
	 * toggle rows can hand its whole array over without knowing how many edges were detected.
	 */
	UFUNCTION(BlueprintCallable, Category = "Room Authoring|Connections")
	static void SetConnectionStates(URoomRecipeAsset* Recipe, const TArray<bool>& Arches,
	                                const TArray<bool>& Opens);

	// --------------------------------------------------------------------------- validation

	/**
	 * RoomAuthor::ValidateRoomLayout, reachable from a widget graph.
	 *
	 * Forwarding, not reimplementing: URoomRecipeAsset::ValidateRecipe is not a UFUNCTION
	 * (the pure layer must stay reflection-free), and this is the one line that bridges that.
	 */
	UFUNCTION(BlueprintCallable, Category = "Room Authoring")
	static bool ValidateRecipe(const URoomRecipeAsset* Recipe, FString& OutError);

	// --------------------------------------------------------------------------- generation

	/**
	 * Validate, clear, then emit one PCG volume per chamber.
	 *
	 * REFUSES rather than generating when the layout is invalid, and OutStatus carries the
	 * validator's own sentence -- the widget shows that sentence and does not compose its own.
	 *
	 * REFUSES when the open level is not AuthoringLevelPath(). Authoring drops several dozen
	 * actors into a level and then deletes them again; doing that to whatever map the author
	 * happened to have open is not a risk worth a convenience.
	 *
	 * Clears FIRST, unconditionally. PCG-spawned actors outlive their volume, so a regenerate
	 * that skipped this would stack a second room exactly on top of the first -- which looks
	 * almost identical until you count.
	 */
	UFUNCTION(BlueprintCallable, Category = "Room Authoring",
		meta = (WorldContext = "WorldContextObject"))
	static bool GenerateRoom(UObject* WorldContextObject, URoomRecipeAsset* Recipe,
	                         FString& OutStatus);

	/**
	 * Destroy every authoring volume and everything PCG spawned for one, and report how many.
	 *
	 * Two passes, and both are needed. UPCGComponent::CleanupLocalImmediate is the supported
	 * way and releases the component's managed resources; the tag sweep afterwards catches
	 * anything a previous session orphaned by deleting a volume outright, which the supported
	 * way cannot see because the component that managed it is already gone.
	 */
	UFUNCTION(BlueprintCallable, Category = "Room Authoring",
		meta = (WorldContext = "WorldContextObject"))
	static int32 ClearAuthoringActors(UObject* WorldContextObject);

	/** How many actors PCG has generated into the authoring level. The leak test's counter. */
	UFUNCTION(BlueprintCallable, Category = "Room Authoring",
		meta = (WorldContext = "WorldContextObject"))
	static int32 CountGeneratedActors(UObject* WorldContextObject);

	/** True when the open level is the authoring level. OutLevel names whatever IS open. */
	UFUNCTION(BlueprintCallable, Category = "Room Authoring",
		meta = (WorldContext = "WorldContextObject"))
	static bool IsAuthoringLevelOpen(UObject* WorldContextObject, FString& OutLevel);

	// ------------------------------------------------------- pure helpers, testable headless

	/**
	 * The single tile an INTERIOR arch occupies, given the validated opening span.
	 *
	 * Design spec 4b: gatewayCls is one cell wide and the kit ships no wide gateway, so a
	 * framed two-tile span would place one arch with ~200 uu of bare gap either side. The
	 * parity rule that produces two-tile spans is a contract for EXTERIOR exits, where the
	 * dungeon generator fills the slot; an interior arch is authored body geometry nothing
	 * fills, so its width is free.
	 *
	 * Derived FROM the validated span rather than recomputed from the shared run: any subset
	 * of a legal span is legal, so narrowing can never walk an opening into a corner cell.
	 * Lower of the two tiles when the span is even, which is arbitrary but must be the same
	 * arbitrary choice for both chambers -- and is, because both are narrowed from the same
	 * room-space span.
	 */
	static void NarrowToSingleTile(int64 TileLo, int64 TileHi, int64& OutLo, int64& OutHi);

	/**
	 * Where a chamber's PCG volume goes, in the authoring level's world frame.
	 *
	 * The BOUNDING rect is centred on the authoring origin, not the first chamber: a room's
	 * frame is its bounding rect (that is what the solver reserves), and centring on a chamber
	 * would move the whole room every time a chamber was added.
	 */
	static FVector ChamberCentreUU(const URoomRecipeAsset* Recipe, int32 Index);
};
