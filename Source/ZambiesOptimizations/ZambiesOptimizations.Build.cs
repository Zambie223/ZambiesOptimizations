using UnrealBuildTool;

public class ZambiesOptimizations : ModuleRules
{
	public ZambiesOptimizations(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		CppStandard = CppStandardVersion.Cpp20;

		// FactoryGame transitive dependencies (same subset as TickProfiler; the SML hook headers pull some of them in).
		PublicDependencyModuleNames.AddRange(new string[] {
			"Core", "CoreUObject",
			"Engine",
			"DeveloperSettings",
			"PhysicsCore",
			"InputCore",
			"GeometryCollectionEngine",
			"AnimGraphRuntime",
			"AssetRegistry",
			"NavigationSystem",
			"AIModule",
			"GameplayTasks",
			"SlateCore", "Slate", "UMG",
			"RenderCore",
			"CinematicCamera",
			"Foliage",
			"EnhancedInput",
			"NetCore",
			"GameplayTags"
		});

		// Header stubs
		PublicDependencyModuleNames.AddRange(new string[] {
			"DummyHeaders",
		});

		if (Target.Type == TargetRules.TargetType.Editor) {
			PublicDependencyModuleNames.AddRange(new string[] {"AnimGraph"});
		}
		PublicDependencyModuleNames.AddRange(new string[] {"FactoryGame", "SML"});

		// Development aids (ZOpt.DevChat). 1 only for test builds: set to 0 for every public release (the release check
		// scans the zips for "ZOpt.DevChat", and a dev build logs a warning at startup).
		PublicDefinitions.Add("ZOPT_DEV_COMMANDS=0");
	}
}
