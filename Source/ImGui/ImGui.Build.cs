// Distributed under the MIT License (MIT) (see accompanying LICENSE file)

using System.IO;
using UnrealBuildTool;

public class ImGui : ModuleRules
{
#if WITH_FORWARDED_MODULE_RULES_CTOR
	public ImGui(ReadOnlyTargetRules Target) : base(Target)
#else
	public ImGui(TargetInfo Target)
#endif
	{
		
		PublicIncludePaths.AddRange(
			new string[] {
				Path.Combine(ModuleDirectory, "../ThirdParty/ImGuiLibrary/Include"),
                Path.Combine(ModuleDirectory, "Public")
				// ... add public include paths required here ...
			}
			);
				
		
		PrivateIncludePaths.AddRange(
			new string[] {
                Path.Combine(ModuleDirectory, "Private"),
                Path.Combine(ModuleDirectory, "../ThirdParty/ImGuiLibrary/Private")
				// ... add other private include paths required here ...
			}
			);
			
		
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"Projects"
				// ... add other public dependencies that you statically link with here ...
			}
			);
			
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
				"InputCore",
				"Slate",
				"SlateCore"
				// ... add private dependencies that you statically link with here ...	
			}
			);
		
		
		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
				// ... add any modules that your module loads dynamically here ...
			}
			);
	}
}
