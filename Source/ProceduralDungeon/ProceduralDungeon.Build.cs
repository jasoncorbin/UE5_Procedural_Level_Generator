// Fill out your copyright notice in the Description page of Project Settings.

using UnrealBuildTool;

public class ProceduralDungeon : ModuleRules
{
	public ProceduralDungeon(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// This module has no Public/Private split, so the module root is not on the
		// include path by default. Added so the ported authoring headers resolve as
		// "RoomAuthoring/RoomAuthorTypes.h" rather than by fragile relative paths.
		PublicIncludePaths.Add(ModuleDirectory);

		// PCG is the room-authoring tool's emission layer (RoomAuthoring/RoomAuthorTools.cpp):
		// one APCGVolume per chamber running PCG_RoomGen_v3, with the chamber's parameters
		// written onto the volume's graph instance. A RUNTIME dependency, not an editor one --
		// PCG ships a runtime module and the plugin is EnabledByDefault in 5.8, so no .uproject
		// entry is needed. The editor-only half of that file (spawning the volume's brush) is
		// guarded by WITH_EDITOR and leans on UnrealEd, which is added below.
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput", "PCG" });

		PrivateDependencyModuleNames.AddRange(new string[] {  });

		// Editor-only, for the room bake (RoomAuthoring's bake step): creating a Blueprint asset
		// and adding SCS component nodes needs UnrealEd, and registering the new asset needs
		// AssetRegistry. Guarded so a packaged build never pulls the editor in -- the bake is a
		// content-authoring step, and its OUTPUT is what ships.
		// AssetTools is the room library's filing step: moving a baked room into its RoomType
		// folder goes through AssetTools::RenameAssets rather than a raw rename, so references
		// are fixed up and a redirector is left behind for any soft pointer to the old path.
		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[] { "UnrealEd", "AssetRegistry", "AssetTools" });
		}

		// NOTE: v2's copy of this file sets bUseUnity = false. That is deliberately NOT carried
		// across. Its reason was three generator namespaces (DungeonGen, RoomGen, RectGen) that
		// shared constant names by design, which could collide as C2872 in a merged translation
		// unit. Of those, only RectGen is being ported here -- and only one enum and six
		// constants of it -- so the collision the flag guarded against cannot occur in v1.

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
