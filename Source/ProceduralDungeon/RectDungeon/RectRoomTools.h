// The room library's shared conventions: where authored rooms are filed, and how a bake names
// the nodes it owns.
//
// A DELIBERATE SUBSET of Level_Creator_1's RectDungeon/RectRoomTools.h. That file is a 236/695
// line library whose bulk is BakeRoom and ClearBakedGeometry -- the half that writes geometry
// into a Blueprint. THAT HALF IS BEING REWRITTEN, not ported: it emits URectRoomAsset children,
// and this project's contract is Master_Room. What is here is the part that is pure convention
// and would be wrong to state twice.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "RectRoomTools.generated.h"

UCLASS()
class PROCEDURALDUNGEON_API URectRoomTools : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * The room library root -- the folder the authoring tool files saved rooms into.
	 *
	 * A code constant rather than a setting: one path, read by both the generator and the
	 * builder panel, and a second copy of it could silently disagree with the first.
	 */
	static const TCHAR* RoomLibraryRoot();

	/**
	 * <RoomLibraryRoot>/<RoomType>, or EMPTY when RoomType is empty.
	 *
	 * Empty rather than the root itself, so a room with no type is refused by the caller
	 * instead of being filed loose at the top of the library where nothing scans for it.
	 */
	static FString RoomTypeFolder(FName RoomType);

	/**
	 * The name prefixes a bake gives the component nodes it owns.
	 *
	 * These are the ONLY thing separating what a bake owns from what a person added to the
	 * prefab by hand, so every bake names through here rather than formatting the literal
	 * itself. Change a prefix and every previously-baked Blueprint's nodes stop being
	 * recognised as bake-owned -- they survive the next clear and the room doubles.
	 */
	static const TCHAR* BakedPiecePrefix();
	static const TCHAR* BakedFixturePrefix();
};
