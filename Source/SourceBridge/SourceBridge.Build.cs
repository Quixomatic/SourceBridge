using UnrealBuildTool;

public class SourceBridge : ModuleRules
{
	public SourceBridge(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"UnrealEd",
			"Slate",
			"SlateCore",
			"RenderCore",
			"RHI",
			"MeshDescription",
			"StaticMeshDescription",
			"Landscape",
			"Foliage",
			"EditorStyle",
			"ToolMenus",
			"InputCore",
			"PropertyEditor",
			"AssetRegistry",
			"WorkspaceMenuStructure",
			"ApplicationCore",
			"DesktopPlatform",
			"BSPUtils",
			"ProceduralMeshComponent",
			"ImageWrapper",
			"GraphEditor"
		});
	}
}
