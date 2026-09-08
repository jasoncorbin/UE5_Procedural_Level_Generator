// The editable record of an authoring session: a bounding rect, its chambers, and the author's
// decision about each shared edge.
//
// OUTSIDE the determinism boundary. int32 UPROPERTYs are legal here because reflection is
// int32-native, and every one of them WIDENS to int64 in MakeLayout() -- never the reverse.
// Same rule, same reason, same shape as URectRoomAsset::MakeSpec.
//
// This is the RECIPE, not the room. The room the generator places is the URectRoomAsset and the
// baked prefab written alongside it (design spec section 5); this exists so the widget can
// reopen a saved room and keep editing it. A hand-tweak made after generation lands in the
// bake and not here -- known and accepted, spelled out in the spec.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "RoomAuthoring/DungeonKitSet.h"
#include "RoomAuthoring/RoomAuthorTypes.h"
#include "RoomAuthoring/RoomAuthorValidate.h"
#include "UObject/SoftObjectPath.h"
#include "RoomRecipeAsset.generated.h"

/**
 * A side of a rect, reflected.
 *
 * The reflected twin of RectGen::ERectSide, and the ORDER MUST MATCH IT -- North, East, South,
 * West. That ordering is not cosmetic: RoomAuthor::FRoomLayout::bExit is indexed by the pure
 * enum's value, and so is every per-side graph parameter array in RoomAuthorTools. The two are
 * still mapped by an explicit switch rather than a cast, so a divergence is a compile error
 * instead of a room with its doors on the wrong walls.
 *
 * Exists because the pure enum cannot be reflected -- it lives inside the determinism boundary
 * and a UENUM there would drag UObject in with it.
 */
UENUM(BlueprintType)
enum class ERoomSide : uint8
{
	North = 0   UMETA(DisplayName = "North"),
	East  = 1   UMETA(DisplayName = "East"),
	South = 2   UMETA(DisplayName = "South"),
	West  = 3   UMETA(DisplayName = "West"),
};

/**
 * One chamber: where it sits on the room's tile grid, and what it is dressed with.
 *
 * EVERY PIECE SLOT BELOW IS AN OVERRIDE. Empty means inherit from the room's KitSet, which is
 * where a room's pieces now come from; a slot is filled only when this one chamber should
 * differ from the set. That inversion is the point of kit sets: before it, every chamber
 * carried its own copy of the wall, corner and floor, so fixing a wrong piece meant editing
 * every room that had it. The single rule lives in UDungeonKitSet::PickClass, and the
 * Resolve*() accessors on URoomRecipeAsset are the only supported way to read these.
 *
 * The piece slots are SOFT paths, not TSubclassOf. A recipe is a design-time record that the
 * widget reads to rebuild a session -- loading it must not drag every wall, corner and floor
 * Blueprint in the kit into memory, which a hard reference in an array of chambers would do the
 * moment the asset registry touched the file. The widget resolves them when it generates.
 *
 * Untemplated FSoftClassPath rather than TSoftClassPtr<AActor> to match DA_PieceTable's Pieces
 * array, which is where these values come from. The cost is real: the AActor base is NOT
 * enforced by the type, so a non-actor class assigned here is refused at resolve time rather
 * than at edit time.
 */
USTRUCT(BlueprintType)
struct FRoomChamber
{
	GENERATED_BODY()

	/** Tile coordinates of this chamber's minimum corner, in the room's own frame. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chamber|Shape")
	int32 GridX = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chamber|Shape")
	int32 GridY = 0;

	/** Extent in 400 uu tiles. A side needs 3 to carry an opening -- see ValidateOpeningSpan. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chamber|Shape", meta = (ClampMin = "1"))
	int32 Width = 3;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chamber|Shape", meta = (ClampMin = "1"))
	int32 Length = 3;

	/**
	 * The chamber this one hangs off, or -1 for the root.
	 *
	 * DEFAULTS TO -1, and that default is the migration story: a recipe written before
	 * attachments existed loads with every chamber a root, so each keeps the GridX/GridY it
	 * was saved with and nothing is silently re-positioned underneath it.
	 *
	 * Must be less than this chamber's own index -- see RoomAuthor::ValidateAttachments.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chamber|Attachment")
	int32 AttachParent = INDEX_NONE;

	/** Which side OF THE PARENT this chamber sits against. Ignored when AttachParent is -1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chamber|Attachment")
	ERoomSide AttachSide = ERoomSide::North;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chamber|Pieces",
		meta = (MetaClass = "/Script/Engine.Actor"))
	FSoftClassPath WallCls;

	/**
	 * Four corners, named rather than an array of four.
	 *
	 * The spec's widget offers each corner individually (section 2), and a fixed-size C array
	 * cannot be exposed to Blueprint at all -- URectRoomAsset::Corners carries that scar in its
	 * own comment. Four named fields read correctly in the Details panel and cost nothing.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chamber|Pieces",
		meta = (MetaClass = "/Script/Engine.Actor"))
	FSoftClassPath CornerNWCls;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chamber|Pieces",
		meta = (MetaClass = "/Script/Engine.Actor"))
	FSoftClassPath CornerNECls;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chamber|Pieces",
		meta = (MetaClass = "/Script/Engine.Actor"))
	FSoftClassPath CornerSECls;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chamber|Pieces",
		meta = (MetaClass = "/Script/Engine.Actor"))
	FSoftClassPath CornerSWCls;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chamber|Pieces",
		meta = (MetaClass = "/Script/Engine.Actor"))
	FSoftClassPath FloorCls;

	/**
	 * A STATIC MESH, not an actor class -- the odd one out, and deliberately so.
	 *
	 * The emitter's ceiling row is a Static Mesh Spawner, not Spawn Actor, so a class path here
	 * would name something the graph cannot place. DA_PieceTable_Ceiling carries mesh paths for
	 * exactly the same reason and is where this value comes from.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chamber|Pieces",
		meta = (AllowedClasses = "/Script/Engine.StaticMesh"))
	FSoftObjectPath CeilingMesh;

	/**
	 * How thickly to scatter props inside this chamber. 0 is bare.
	 *
	 * Read by no validation rule -- it moves nothing structural, so the pure layer never sees
	 * it. It is the one float on this asset, which is why the pure layer stays float-free.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chamber|Dressing",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float PropDensity = 0.0f;
};

/**
 * The author's decision about one shared edge.
 *
 * Keyed by chamber PAIR, not by an index into a detected-edge list: that list is rebuilt from
 * scratch whenever a chamber moves, and an index into it would silently re-point at a different
 * edge the moment a chamber was inserted or removed.
 *
 * Mode defaults to Arch. Decision 7 of the design spec: shared edges auto-arch, and this
 * toggle is what turns one into solid wall or removes the wall entirely. A pair with no entry
 * in the list is therefore arched, so the widget only has to write an entry when the author
 * disagrees with the default.
 */
UENUM(BlueprintType)
enum class ERoomConnectionMode : uint8
{
	/** Solid wall. The shared edge cuts nothing. */
	Walled = 0    UMETA(DisplayName = "Walled"),

	/** One tile, centred on the shared run, framed by the gateway piece. */
	Arch = 1      UMETA(DisplayName = "Arch"),

	/**
	 * The whole shared run, unframed: the wall between the two chambers is removed, corner
	 * pieces included, so they read as one space. This is how a non-rectangular room is
	 * authored -- two abutting rectangles become one L instead of two rooms and a doorway.
	 */
	Open = 2      UMETA(DisplayName = "Open (wall removed)"),
};

USTRUCT(BlueprintType)
struct FRoomConnection
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Connection", meta = (ClampMin = "0"))
	int32 ChamberA = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Connection", meta = (ClampMin = "0"))
	int32 ChamberB = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Connection")
	ERoomConnectionMode Mode = ERoomConnectionMode::Arch;
};

UCLASS(BlueprintType)
class PROCEDURALDUNGEON_API URoomRecipeAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Room")
	FName RoomName = TEXT("AuthoredRoom");

	/**
	 * The kit this room is built from. Every piece slot on this asset and on its chambers is an
	 * OVERRIDE of what this names.
	 *
	 * A hard reference, and safely so: UDungeonKitSet holds nothing but soft paths, so pointing
	 * at one loads a small data asset and not a single Blueprint. That is exactly the property
	 * the soft slots were introduced to protect, and routing rooms through a set preserves it
	 * while removing the duplication.
	 *
	 * May be null. A recipe written before kit sets existed has no set and every piece slot
	 * filled, so it keeps resolving to precisely the values it was saved with -- the same
	 * migration story AttachParent's -1 default gives attachments.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Room")
	TObjectPtr<UDungeonKitSet> KitSet;

	/**
	 * Which folder under the room library this room is filed into, mirroring
	 * URectRoomAsset::RoomType. Free-form on purpose -- inventing a room type must not require
	 * a C++ change and an editor rebuild.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Room")
	FName RoomType;

	/** The rect the dungeon solver reserves. Every chamber lives inside it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Room|Shape", meta = (ClampMin = "1"))
	int32 BoundingWidth = 5;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Room|Shape", meta = (ClampMin = "1"))
	int32 BoundingLength = 5;

	/**
	 * 570, and in practice only 570.
	 *
	 * Decision 1: this tool is Large tier only, because the kit ships no Med or Small DOOR, and
	 * a room whose exits the generator cannot fill is not a room the library can hold. The
	 * clamp still admits the other two tiers rather than hard-coding one, so a future kit that
	 * adds the missing doors needs no change here.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Room|Shape",
		meta = (ClampMin = "170", ClampMax = "570"))
	int32 WallHeightUU = 570;

	/**
	 * int64 to match ARectDungeonGenerator::Seed, which is where a seed ends up being compared.
	 * Two seed fields of different widths is a truncation waiting to be blamed on the solver.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Room|Solver")
	int64 Seed = 1;

	/** Relative draw weight, carried through to URectRoomAsset::Weight on save. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Room|Solver", meta = (ClampMin = "1"))
	int32 Weight = 10;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Room|Exits")
	bool bExitNorth = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Room|Exits")
	bool bExitEast = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Room|Exits")
	bool bExitSouth = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Room|Exits")
	bool bExitWest = true;

	/**
	 * What the GENERATOR fills this room's exterior exits with. ADR-0014 D-5.
	 *
	 * Not body pieces, and not placed by the authoring tool -- the room emits BARE GAPS at its
	 * exterior exits (non-negotiable #1) and the dungeon generator fills them at assembly. These
	 * four are carried on the recipe purely so BakeAuthoredRoom can copy them onto the
	 * URectRoomAsset, which is where the generator reads them from.
	 *
	 * All four exist because which pair a room needs depends on the PARITY of the edges its
	 * exits sit on: an odd edge takes the one-tile Door/WallCap, an even edge the two-tile
	 * DoorWide/WallCapWide, and a room with an odd width and an even length needs both pairs.
	 * BakeAuthoredRoom refuses rather than guess, and asks URectRoomAsset which pairs apply
	 * instead of re-deriving the parity rule here.
	 *
	 * Door and cap are BOTH needed for any enabled exit, because whether that exit ends up
	 * connected or capped is not known until the dungeon solve finishes.
	 *
	 * DoorCls and WallCapCls now RESOLVE THROUGH THE KIT SET -- read them via ResolveDoor()
	 * and ResolveWallCap(), not directly. DoorWideCls and WallCapWideCls do not, and that is
	 * the honest state of things rather than an omission:
	 *
	 * The version of this comment carried over from Level_Creator_1 mapped these four onto the
	 * kit as Door/DoorWide/WallCap/WallCapWide =
	 *   BP_COMP_Door_01_large_2 / BP_COMP_Door_Walled_01_large /
	 *   BP_COMP_Wall_01_E_straight_large / BP_COMP_Wall_01_E_straight_large_wide.
	 *
	 * THREE OF THOSE FOUR ARE WRONG, and the first wrong one is a real defect rather than a
	 * naming quibble. The 2026-09-07 census of all eight DA_RectRoom_* assets found the roles
	 * to be:
	 *   Door      BP_COMP_Door_01_large_2            passable
	 *   WallCap   BP_COMP_Door_Walled_01_large       SEALED filler for an unused exit
	 *   Wall      BP_COMP_Wall_01_E_straight_large   a wall run piece, not a cap
	 *   WallWide  BP_COMP_Wall_01_E_straight_large_wide
	 *
	 * So Door_Walled sat in the DoorWide slot -- used as a passable door, which is exactly what
	 * produced the sealed-doorway defect in the 2026-08-28 PCG work. The two wall pieces sat in
	 * the cap slots, where they are wall runs rather than exit fillers.
	 *
	 * There is NO census asset for a wide door or a wide cap: every shipping room used the same
	 * six pieces and none of them was either. Rather than guess a plausible path, these two
	 * slots stay author-set with no kit fallback, and BakeAuthoredRoom refuses a room that needs
	 * a pair it has not been given. A room needs the wide pair only when an exit sits on an
	 * even-length edge.
	 *
	 * The narrow door is BP_COMP_Door_01_large_2, with the _2. BP_COMP_Door_01_large also
	 * exists, and Level_Creator_1's notes named it wrongly three separate times.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room|Exits",
		meta = (MetaClass = "/Script/Engine.Actor"))
	FSoftClassPath DoorCls;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room|Exits",
		meta = (MetaClass = "/Script/Engine.Actor"))
	FSoftClassPath DoorWideCls;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room|Exits",
		meta = (MetaClass = "/Script/Engine.Actor"))
	FSoftClassPath WallCapCls;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room|Exits",
		meta = (MetaClass = "/Script/Engine.Actor"))
	FSoftClassPath WallCapWideCls;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Room|Chambers")
	TArray<FRoomChamber> Chambers;

	/**
	 * Only the pairs the author has an OPINION about. A shared edge with no entry is arched.
	 *
	 * Not the detected-edge list -- that is derived, and storing derived data next to its source
	 * is what URectRoomAsset::BakeHash exists to police. RoomAuthor::FindSharedEdges rebuilds it
	 * from the chambers on every load.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Room|Chambers")
	TArray<FRoomConnection> Connections;

	/**
	 * Every piece this room is built from, override resolved against the kit set.
	 *
	 * These accessors exist so no caller reads a raw slot. Reading FRoomChamber::WallCls
	 * directly gets the OVERRIDE, which is empty on almost every chamber of a room that uses a
	 * set -- a caller that does it once and works by luck on a fully-overridden legacy recipe
	 * is the failure mode these are here to remove.
	 *
	 * A null KitSet resolves to the override alone, which is what a pre-kit-set recipe wants.
	 */
	FSoftClassPath ResolveWall(const FRoomChamber& Chamber) const
	{
		return UDungeonKitSet::PickClass(Chamber.WallCls, KitSet ? KitSet->Wall : FSoftClassPath());
	}

	FSoftClassPath ResolveFloor(const FRoomChamber& Chamber) const
	{
		return UDungeonKitSet::PickClass(Chamber.FloorCls, KitSet ? KitSet->Floor : FSoftClassPath());
	}

	/**
	 * The chamber's raw corner override for one slot. Empty means inherit.
	 *
	 * A switch over the four named fields rather than an array, because a fixed-size C array
	 * cannot be exposed to Blueprint at all -- which is why they are four named fields in the
	 * first place. Keeping the mapping here means the corner order is stated once.
	 */
	static const FSoftClassPath& CornerOverrideFor(const FRoomChamber& Chamber,
	                                               RectGen::ERectCornerIndex Corner)
	{
		switch (Corner)
		{
		case RectGen::ERectCornerIndex::NW: return Chamber.CornerNWCls;
		case RectGen::ERectCornerIndex::NE: return Chamber.CornerNECls;
		case RectGen::ERectCornerIndex::SE: return Chamber.CornerSECls;
		case RectGen::ERectCornerIndex::SW: return Chamber.CornerSWCls;
		}
		return Chamber.CornerNWCls;
	}

	/**
	 * A corner, by slot. One kit asset backs all four -- the emitter rotates it -- so an
	 * un-overridden NW and SE resolve to the same class, which is correct and not a bug.
	 */
	FSoftClassPath ResolveCorner(const FRoomChamber& Chamber,
	                             RectGen::ERectCornerIndex Corner) const
	{
		return UDungeonKitSet::PickClass(CornerOverrideFor(Chamber, Corner),
			KitSet ? KitSet->Corner : FSoftClassPath());
	}

	FSoftObjectPath ResolveCeilingMesh(const FRoomChamber& Chamber) const
	{
		return UDungeonKitSet::PickObject(Chamber.CeilingMesh,
			KitSet ? KitSet->CeilingMesh : FSoftObjectPath());
	}

	/** The passable door the GENERATOR fills a connected exterior exit with. */
	FSoftClassPath ResolveDoor() const
	{
		return UDungeonKitSet::PickClass(DoorCls, KitSet ? KitSet->Door : FSoftClassPath());
	}

	/** The solid filler the GENERATOR seals an unconnected exterior exit with. Not a door. */
	FSoftClassPath ResolveWallCap() const
	{
		return UDungeonKitSet::PickClass(WallCapCls, KitSet ? KitSet->WallCap : FSoftClassPath());
	}

	/**
	 * The one place the reflected side enum meets the pure one.
	 *
	 * A switch, not a cast of the underlying byte, so that reordering either enum is a compile
	 * error here rather than a room whose doors quietly moved to different walls.
	 */
	static RectGen::ERectSide ToRectSide(ERoomSide Side)
	{
		switch (Side)
		{
		case ERoomSide::North: return RectGen::ERectSide::North;
		case ERoomSide::East:  return RectGen::ERectSide::East;
		case ERoomSide::South: return RectGen::ERectSide::South;
		case ERoomSide::West:  return RectGen::ERectSide::West;
		}
		return RectGen::ERectSide::North;
	}

	/** ToRectSide the other way, for the widget-facing accessors. */
	static ERoomSide FromRectSide(RectGen::ERectSide Side)
	{
		switch (Side)
		{
		case RectGen::ERectSide::North: return ERoomSide::North;
		case RectGen::ERectSide::East:  return ERoomSide::East;
		case RectGen::ERectSide::South: return ERoomSide::South;
		case RectGen::ERectSide::West:  return ERoomSide::West;
		}
		return ERoomSide::North;
	}

	/** The one place the four named exit flags map onto RectGen::ERectSide's ordering. */
	bool bExitFor(RectGen::ERectSide Side) const
	{
		switch (Side)
		{
		case RectGen::ERectSide::North: return bExitNorth;
		case RectGen::ERectSide::East:  return bExitEast;
		case RectGen::ERectSide::South: return bExitSouth;
		case RectGen::ERectSide::West:  return bExitWest;
		}
		return false;
	}

	/**
	 * Reflection -> pure ints. The one conversion, and it only ever widens.
	 *
	 * Carries no piece paths: not one validation rule reads them, and a pure layer that held
	 * class paths would need UObject and stop being headless-testable. Never fails -- there is
	 * nothing to fail on, because widening cannot lose. The rules that CAN fail are
	 * RoomAuthor::ValidateRoomLayout's, and they are deliberately a separate call so a caller
	 * can convert once and validate repeatedly while the author drags a chamber around.
	 */
	void MakeLayout(RoomAuthor::FRoomLayout& OutLayout) const
	{
		OutLayout.BoundingWidth  = static_cast<int64>(BoundingWidth);
		OutLayout.BoundingLength = static_cast<int64>(BoundingLength);

		OutLayout.bExit[static_cast<int32>(RectGen::ERectSide::North)] = bExitNorth;
		OutLayout.bExit[static_cast<int32>(RectGen::ERectSide::East)]  = bExitEast;
		OutLayout.bExit[static_cast<int32>(RectGen::ERectSide::South)] = bExitSouth;
		OutLayout.bExit[static_cast<int32>(RectGen::ERectSide::West)]  = bExitWest;

		OutLayout.Chambers.Reset();
		OutLayout.Chambers.Reserve(Chambers.Num());
		for (const FRoomChamber& C : Chambers)
		{
			RoomAuthor::FChamberRect R;
			R.GridX  = static_cast<int64>(C.GridX);
			R.GridY  = static_cast<int64>(C.GridY);
			R.Width  = static_cast<int64>(C.Width);
			R.Length = static_cast<int64>(C.Length);
			R.ParentIndex = static_cast<int64>(C.AttachParent);
			R.AttachSide  = ToRectSide(C.AttachSide);
			OutLayout.Chambers.Add(R);
		}

		OutLayout.Connections.Reset();
		OutLayout.Connections.Reserve(Connections.Num());
		for (const FRoomConnection& Conn : Connections)
		{
			RoomAuthor::FAuthoredConnection A;
			A.ChamberA = static_cast<int64>(Conn.ChamberA);
			A.ChamberB = static_cast<int64>(Conn.ChamberB);

			// The one place the reflected enum meets the pure one. Written as a switch rather
			// than a cast of the underlying byte so that adding a mode to either side is a
			// compile error here instead of a silent mis-mapping.
			switch (Conn.Mode)
			{
			case ERoomConnectionMode::Walled: A.Mode = RoomAuthor::EConnectionMode::Walled; break;
			case ERoomConnectionMode::Arch:   A.Mode = RoomAuthor::EConnectionMode::Arch;   break;
			case ERoomConnectionMode::Open:   A.Mode = RoomAuthor::EConnectionMode::Open;   break;
			}

			OutLayout.Connections.Add(A);
		}
	}

	/**
	 * Convert, then run every layout rule. The call the widget's Save path makes.
	 *
	 * @param OutError set when the recipe is unbuildable. NEVER set on success.
	 */
	bool ValidateRecipe(FString& OutError) const
	{
		RoomAuthor::FRoomLayout Layout;
		MakeLayout(Layout);

		TArray<RoomAuthor::FSharedEdge> Edges;
		TArray<RoomAuthor::FChamberOpening> Openings;
		return RoomAuthor::ValidateRoomLayout(Layout, Edges, Openings, OutError);
	}
};
