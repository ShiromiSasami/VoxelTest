// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class VoxelTest : ModuleRules
{
	public VoxelTest(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"AIModule",
			"StateTreeModule",
			"GameplayStateTreeModule",
			"UMG",
			"Slate",
			// Rendering extensions for voxel pass
			"RenderCore",
			"RHI",
			"Renderer"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { });

		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.AddRange(new string[] { "UnrealEd" });
		}

		PublicIncludePaths.AddRange(new string[] {
			"VoxelTest",
			"VoxelTest/Variant_Platforming",
			"VoxelTest/Variant_Platforming/Animation",
			"VoxelTest/Variant_Combat",
			"VoxelTest/Variant_Combat/AI",
			"VoxelTest/Variant_Combat/Animation",
			"VoxelTest/Variant_Combat/Gameplay",
			"VoxelTest/Variant_Combat/Interfaces",
			"VoxelTest/Variant_Combat/UI",
			"VoxelTest/Variant_SideScrolling",
			"VoxelTest/Variant_SideScrolling/AI",
			"VoxelTest/Variant_SideScrolling/Gameplay",
			"VoxelTest/Variant_SideScrolling/Interfaces",
			"VoxelTest/Variant_SideScrolling/UI",
			"VoxelTest/Public",
			"VoxelTest/Public/Rendering",
			"VoxelTest/Public/Rendering/Voxel"
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
