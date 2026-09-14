#include "RectDungeon/RectRoomTools.h"

const TCHAR* URectRoomTools::RoomLibraryRoot()
{
	return TEXT("/Game/RectDungeon/Rooms");
}

FString URectRoomTools::RoomTypeFolder(FName RoomType)
{
	if (RoomType.IsNone() || RoomType.ToString().IsEmpty())
	{
		return FString();
	}
	return FString(RoomLibraryRoot()) / RoomType.ToString();
}

const TCHAR* URectRoomTools::BakedPiecePrefix()   { return TEXT("Piece_"); }
const TCHAR* URectRoomTools::BakedFixturePrefix() { return TEXT("Fixture_"); }
