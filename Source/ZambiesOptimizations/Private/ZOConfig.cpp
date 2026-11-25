#include "ZOConfig.h"

#include "Configuration/ConfigManager.h"
#include "Configuration/ModConfiguration.h"
#include "Configuration/Properties/ConfigPropertyBool.h"
#include "Configuration/Properties/ConfigPropertySection.h"
#include "Engine/GameInstance.h"
#include "HAL/IConsoleManager.h"
#include "ZambiesOptimizationsModule.h"

namespace
{
	const TCHAR* const ModReference = TEXT("ZambiesOptimizations");
	/** The Blueprint made by Tools/create_config_asset.py. */
	const TCHAR* const ConfigClassPath = TEXT("/ZambiesOptimizations/ZambiesOptimizationsConfiguration.ZambiesOptimizationsConfiguration_C");

	/** One switch: its key in the settings file and the console variable it sets. Keys must match the config asset
	 *  (Tools/create_config_asset.py, verify_config_asset.py). Every switch defaults to off. */
	struct FSwitchDef
	{
		const TCHAR* Key;
		const TCHAR* ConsoleVariable;
	};
	const FSwitchDef Switches[] = {
		{ TEXT("SkelBuildables"), TEXT("ZOpt.SkelBuildables") },
		{ TEXT("SkelUnpossessed"), TEXT("ZOpt.SkelUnpossessed") },
	};

	TWeakObjectPtr<UConfigPropertySection> GRootSection;

	FConfigId GetConfigId()
	{
		FConfigId Id;
		Id.ModReference = ModReference;
		return Id;
	}

	/** Sets every switch from the file. SetByCode outranks Engine.ini and launch parameters (no longer supported routes),
	 *  and is outranked by the console, so a live console change is not undone by this. */
	void ApplyFromFile(const TCHAR* Source)
	{
		UConfigPropertySection* Root = GRootSection.Get();
		if (!Root)
		{
			return;
		}
		FString Summary;
		for (const FSwitchDef& Switch : Switches)
		{
			const TObjectPtr<UConfigProperty>* Found = Root->SectionProperties.Find(FString(Switch.Key));
			const UConfigPropertyBool* Bool = Found ? Cast<UConfigPropertyBool>(Found->Get()) : nullptr;
			IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(Switch.ConsoleVariable);
			if (!Bool || !Var)
			{
				UE_LOG(LogZOpt, Warning, TEXT("Zambie's Optimizations: settings file has no usable '%s' (or %s is missing); left off"), Switch.Key, Switch.ConsoleVariable);
				continue;
			}
			Var->Set(Bool->Value ? TEXT("1") : TEXT("0"), ECVF_SetByCode);
			Summary += FString::Printf(TEXT("%s%s %d"), Summary.IsEmpty() ? TEXT("") : TEXT(", "), Switch.Key, Var->GetInt());
		}
		UE_LOG(LogZOpt, Display, TEXT("Zambie's Optimizations: settings from the %s: %s"), Source, *Summary);
	}
}

UZambiesOptimizationsGameInstanceModule::UZambiesOptimizationsGameInstanceModule()
{
	bRootModule = true;
}

void UZambiesOptimizationsGameInstanceModule::DispatchLifecycleEvent(const ELifecyclePhase Phase)
{
	// SML registers (and loads) configurations during INITIALIZATION, so ours is added during CONSTRUCTION.
	// Loaded here instead of in the constructor so the mod's content is mounted.
	if (Phase == ELifecyclePhase::CONSTRUCTION)
	{
		const TSoftClassPtr<UModConfiguration> ConfigClass{FSoftObjectPath(ConfigClassPath)};
		if (UClass* Loaded = ConfigClass.LoadSynchronous())
		{
			ModConfigurations.AddUnique(Loaded);
		}
		else
		{
			UE_LOG(LogZOpt, Error, TEXT("Zambie's Optimizations: could not load the Mod Configuration %s; every switch stays off (console only)"), ConfigClassPath);
		}
	}
	Super::DispatchLifecycleEvent(Phase);

	if (Phase == ELifecyclePhase::POST_INITIALIZATION)
	{
		UGameInstance* GameInstance = GetGameInstance();
		UConfigManager* Manager = GameInstance ? GameInstance->GetSubsystem<UConfigManager>() : nullptr;
		UConfigPropertySection* Root = Manager ? Manager->GetConfigurationRootSection(GetConfigId()) : nullptr;
		if (!Root)
		{
			UE_LOG(LogZOpt, Warning, TEXT("Zambie's Optimizations: settings file not registered; every switch stays off (console only)"));
			return;
		}
		GRootSection = Root;
		Root->OnPropertyValueChanged.AddUniqueDynamic(this, &UZambiesOptimizationsGameInstanceModule::OnConfigValueChanged);
		UE_LOG(LogZOpt, Display, TEXT("Zambie's Optimizations: settings file %s"), *(UConfigManager::GetConfigurationFolderPath() + ModReference + TEXT(".cfg")));
		ApplyFromFile(TEXT("settings file"));
	}
}

void UZambiesOptimizationsGameInstanceModule::OnConfigValueChanged()
{
	ApplyFromFile(TEXT("changed settings file"));
}
