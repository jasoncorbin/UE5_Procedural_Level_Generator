#include "RoomAuthoring/DungeonKitSet.h"

namespace
{
	// The six assets every shipping room in Level_Creator_1 was built from, established by
	// enumerating comps/ from disk and extracting the class references out of all eight
	// DA_RectRoom_*.uasset binaries. Byte-identical across Chamber, DeadEnd, Hall, Hub,
	// Junction, Passage, Boss and Start -- there is exactly one coherent room-building set in
	// this pack and every working room already uses it.
	//
	// Shipped as CONSTRUCTOR DEFAULTS rather than as a DA_KitSet_* asset so this layer is
	// testable with no editor running and no content to author first. A data asset naming a
	// different set is a later convenience, not a prerequisite.
	//
	// Every one of these was checked to exist in this project's Content/ before being written
	// here. A path in a constructor that resolves to nothing is a room that generates missing
	// its walls, which is not a failure anything reports.
	const TCHAR* const KitWall     = TEXT("/Game/Fantastic_Dungeon_Pack/blueprints/modular/comps/wall/PivotEdge/BP_COMP_Wall_01_E_straight_large.BP_COMP_Wall_01_E_straight_large_C");
	const TCHAR* const KitWallWide = TEXT("/Game/Fantastic_Dungeon_Pack/blueprints/modular/comps/wall/PivotEdge/BP_COMP_Wall_01_E_straight_large_wide.BP_COMP_Wall_01_E_straight_large_wide_C");
	const TCHAR* const KitCorner   = TEXT("/Game/Fantastic_Dungeon_Pack/blueprints/modular/comps/wall/PivotEdge/BP_COMP_Wall_01_E_corner_large.BP_COMP_Wall_01_E_corner_large_C");
	const TCHAR* const KitFloor    = TEXT("/Game/Fantastic_Dungeon_Pack/blueprints/modular/comps/floor/BP_COMP_Floor_01_O_base.BP_COMP_Floor_01_O_base_C");

	// The narrow door is BP_COMP_Door_01_large_2, WITH the _2. BP_COMP_Door_01_large also
	// exists and is a different asset; Level_Creator_1's notes recorded the wrong one three
	// separate times.
	const TCHAR* const KitDoor     = TEXT("/Game/Fantastic_Dungeon_Pack/blueprints/modular/comps/gateway/BP_COMP_Door_01_large_2.BP_COMP_Door_01_large_2_C");

	// Door_Walled is the SEALED FILLER, and it is in the WallCap slot for that reason. It is
	// the asset that produced the sealed-doorway defect when it was used as a passable door.
	const TCHAR* const KitWallCap  = TEXT("/Game/Fantastic_Dungeon_Pack/blueprints/modular/comps/gateway/BP_COMP_Door_Walled_01_large.BP_COMP_Door_Walled_01_large_C");
}

UDungeonKitSet::UDungeonKitSet()
	: Wall(KitWall)
	, WallWide(KitWallWide)
	, Corner(KitCorner)
	, Floor(KitFloor)
	, Door(KitDoor)
	, WallCap(KitWallCap)
	, ValidatedBy(TEXT("Level_Creator_1 shipping rooms, census 2026-09-07: all eight DA_RectRoom_* assets reference this identical set"))
{
	// CeilingMesh is left empty deliberately. See its comment in the header.
}

bool UDungeonKitSet::ValidateKit(FString& OutError) const
{
	struct FRoleSlot
	{
		const TCHAR* Name;
		const FSoftClassPath* Path;
	};

	const FRoleSlot Slots[] = {
		{ TEXT("Wall"),     &Wall     },
		{ TEXT("WallWide"), &WallWide },
		{ TEXT("Corner"),   &Corner   },
		{ TEXT("Floor"),    &Floor    },
		{ TEXT("Door"),     &Door     },
		{ TEXT("WallCap"),  &WallCap  },
	};

	for (const FRoleSlot& Slot : Slots)
	{
		if (!Slot.Path->IsValid())
		{
			OutError = FString::Printf(
				TEXT("Kit set '%s' has no %s. Every structural role must name a piece; a room ")
				TEXT("built from an incomplete set generates with that piece simply missing, ")
				TEXT("which nothing downstream reports."),
				*KitName.ToString(), Slot.Name);
			return false;
		}
	}

	return true;
}
