// One named asset per structural role: the set of pieces a room is actually built from.
//
// OUTSIDE the determinism boundary -- it is reflection and soft paths, and no validation rule
// reads it. The pure layer (RoomAuthoring/RoomAuthorTypes.h) never sees a piece path, which is
// what keeps it headless-testable.
//
// WHY THIS EXISTS. Before this, every chamber of every room carried its own copy of the wall,
// corner and floor to use. Fixing a wrong piece meant editing every room that had it. A kit set
// is named once and referenced, so editing the set fixes every room that uses it.
//
// ROLES ARE NAMED, AND WallCap IS NOT Door. That is the whole defence against a defect this
// pack invites by name: BP_COMP_Door_Walled_01_large is the sealed filler for an UNUSED exit,
// not a door anyone walks through. Using it as a passable door is what produced the
// sealed-doorway defect in Level_Creator_1's 2026-08-28 PCG work. Same asset, two roles,
// opposite correctness -- the kit's own naming (Door_Walled vs Door) does not make the
// distinction obvious, so the data model has to.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Misc/DateTime.h"
#include "UObject/SoftObjectPath.h"
#include "DungeonKitSet.generated.h"

/**
 * The pieces one room-building kit is made of.
 *
 * SOFT PATHS, not TSubclassOf, and deliberately against the kit-set spec's section 3.1 which
 * wrote TSubclassOf<AActor>. Two reasons, and the first is decisive: the consumer hands these
 * to PCG as string graph parameters, so a soft path is already the wanted form and needs no
 * conversion, while a TSubclassOf would be loaded and then flattened back to a string. The
 * second is that a hard class reference in a widely-referenced data asset pulls every wall,
 * corner and floor Blueprint in the kit into memory the moment the asset registry touches the
 * file -- the same no-hard-references property FRoomChamber's own piece slots were given.
 *
 * Untemplated FSoftClassPath rather than TSoftClassPtr<AActor> to match the piece slots these
 * values flow into and the DA_PieceTable arrays they came from. The cost is real and worth
 * stating: the AActor base is NOT enforced by the type, so a non-actor class assigned here is
 * refused at resolve time rather than at edit time.
 */
UCLASS(BlueprintType)
class PROCEDURALDUNGEON_API UDungeonKitSet : public UDataAsset
{
	GENERATED_BODY()

public:
	UDungeonKitSet();

	/** For the Details panel and error messages. Not an identity -- the asset path is that. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kit")
	FName KitName = TEXT("FantasticDungeon_E_Large");

	/**
	 * A straight wall run piece.
	 *
	 * The _E_ (PivotEdge) family, and that is forced rather than chosen: it is the only family
	 * in this pack carrying both concave and convex corners and a left run-end cap, so it is
	 * the only one that can close a room. Counts at the time of the census: Wall _E_ 41,
	 * _M_ 30, _O_ 31.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kit|Structure",
		meta = (MetaClass = "/Script/Engine.Actor"))
	FSoftClassPath Wall;

	/** The wide wall variant, used where an edge's parity calls for it. Large tier only. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kit|Structure",
		meta = (MetaClass = "/Script/Engine.Actor"))
	FSoftClassPath WallWide;

	/**
	 * The room corner. ONE asset serves all four corners -- the emitter rotates it.
	 *
	 * Exists only at the large tier. There is no _E_corner_med or _E_corner_small, which is
	 * the unstated reason the whole project is fixed at the 570 wall tier: at any other tier
	 * this family has no plain corner and no wide wall, so these rooms could not be built.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kit|Structure",
		meta = (MetaClass = "/Script/Engine.Actor"))
	FSoftClassPath Corner;

	/**
	 * The field floor tile.
	 *
	 * An _O_ (OneSided) piece sitting beside _E_ walls, which is a deliberate MIX of families
	 * and not an oversight. All ten floors in this pack are _O_; there is no _E_ floor. The
	 * "pick one family and stay in it" rule governs walls and corners only.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kit|Structure",
		meta = (MetaClass = "/Script/Engine.Actor"))
	FSoftClassPath Floor;

	/** PASSABLE. Sits in a wall and is walked through. Not WallCap. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kit|Exits",
		meta = (MetaClass = "/Script/Engine.Actor"))
	FSoftClassPath Door;

	/** SEALS an unused exit. Solid. NOT a door -- see this file's header comment. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kit|Exits",
		meta = (MetaClass = "/Script/Engine.Actor"))
	FSoftClassPath WallCap;

	/**
	 * A STATIC MESH, not an actor class -- the odd one out, and deliberately so. The emitter's
	 * ceiling row is a Static Mesh Spawner, not a Spawn Actor, so a class path here would name
	 * something the graph cannot place.
	 *
	 * SHIPS EMPTY, ON PURPOSE. The six-asset census that supplies every other default on this
	 * asset contains no ceiling: all eight shipping rooms were byte-identical in their piece
	 * references and not one of them named a ceiling mesh. A plausible-looking path guessed
	 * into a constructor is worse than an empty slot, because an empty slot is visibly
	 * unfilled while a wrong one generates rooms with the wrong lid and looks deliberate.
	 *
	 * Fill this in once a real room has been checked in the editor for what it uses.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kit|Structure",
		meta = (AllowedClasses = "/Script/Engine.StaticMesh"))
	FSoftObjectPath CeilingMesh;

	/**
	 * What proved this set builds sealed geometry. A set's authority is the point of having it.
	 *
	 * Free text rather than an enum: the evidence for one set is "eight shipping rooms use it",
	 * for the next it might be a ray-sweep run on a date. Both are answers to the same
	 * question and neither is a category.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kit|Provenance")
	FString ValidatedBy;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kit|Provenance")
	FDateTime ValidatedOn = FDateTime();

	/**
	 * Override wins when set; otherwise the kit's value; otherwise empty.
	 *
	 * The whole inheritance rule, in one place, so "empty means inherit" cannot be implemented
	 * two subtly different ways. An empty override is not a choice to have no piece -- there is
	 * no way to express that, and no room needs one.
	 */
	static FSoftClassPath PickClass(const FSoftClassPath& Override, const FSoftClassPath& FromKit)
	{
		return Override.IsValid() ? Override : FromKit;
	}

	static FSoftObjectPath PickObject(const FSoftObjectPath& Override, const FSoftObjectPath& FromKit)
	{
		return Override.IsValid() ? Override : FromKit;
	}

	/**
	 * Is every structural role filled?
	 *
	 * CeilingMesh is deliberately NOT required -- see its comment. A room with no ceiling
	 * generates; a room with no wall does not.
	 *
	 * @param OutError set when a role is empty. NEVER set on success.
	 */
	bool ValidateKit(FString& OutError) const;
};
