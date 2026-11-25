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

	/** One switch: its key in the settings file, the console variable it sets and a short description for chat. Keys
	 *  must match the config asset (Tools/create_config_asset.py, verify_config_asset.py). Every switch defaults to off. */
	struct FSwitchDef
	{
		const TCHAR* Key;
		const TCHAR* ConsoleVariable;
		const TCHAR* Description;
	};
	const FSwitchDef Switches[] = {
		{ TEXT("SkelBuildables"), TEXT("ZOpt.SkelBuildables"), TEXT("skip machine animation updates on the server") },
		{ TEXT("SkelUnpossessed"), TEXT("ZOpt.SkelUnpossessed"), TEXT("skip animation updates on offline players' bodies") },
		{ TEXT("SkipRepGraphDump"), TEXT("ZOpt.SkipRepGraphDump"), TEXT("skip the replication debug dump (freeze on some rejoins)") },
	};

	TWeakObjectPtr<UConfigPropertySection> GRootSection;

	FConfigId GetConfigId()
	{
		FConfigId Id;
		Id.ModReference = ModReference;
		return Id;
	}

	UConfigPropertyBool* FindFileProperty(const FSwitchDef& Switch)
	{
		UConfigPropertySection* Root = GRootSection.Get();
		const TObjectPtr<UConfigProperty>* Found = Root ? Root->SectionProperties.Find(FString(Switch.Key)) : nullptr;
		return Found ? Cast<UConfigPropertyBool>(Found->Get()) : nullptr;
	}

	/** True when the switch was last set with a priority above SetByCode (only the console), which a file value cannot
	 *  override. */
	bool IsHeldByConsole(const IConsoleVariable* Var)
	{
		return (static_cast<uint32>(Var->GetFlags()) & ECVF_SetByMask) > static_cast<uint32>(ECVF_SetByCode);
	}

	/** Sets every switch from the file. SetByCode outranks Engine.ini and launch parameters (no longer supported routes),
	 *  and is outranked by the console, so a live console change is not undone by this. Switches last set from the
	 *  console are skipped (the engine would ignore the value and log a warning) and marked "(console)" in the summary. */
	void ApplyFromFile(const TCHAR* Source)
	{
		if (!GRootSection.IsValid())
		{
			return;
		}
		FString Summary;
		for (const FSwitchDef& Switch : Switches)
		{
			const UConfigPropertyBool* Bool = FindFileProperty(Switch);
			IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(Switch.ConsoleVariable);
			if (!Bool || !Var)
			{
				UE_LOG(LogZOpt, Warning, TEXT("Zambie's Optimizations: settings file has no usable '%s' (or %s is missing); left off"), Switch.Key, Switch.ConsoleVariable);
				continue;
			}
			const bool bHeldByConsole = IsHeldByConsole(Var);
			if (!bHeldByConsole)
			{
				Var->Set(Bool->Value ? TEXT("1") : TEXT("0"), ECVF_SetByCode);
			}
			Summary += FString::Printf(TEXT("%s%s %d%s"), Summary.IsEmpty() ? TEXT("") : TEXT(", "), Switch.Key, Var->GetInt(),
				bHeldByConsole ? TEXT(" (console)") : TEXT(""));
		}
		UE_LOG(LogZOpt, Display, TEXT("Zambie's Optimizations: settings from the %s: %s"), Source, *Summary);
	}
}

namespace ZOpt::Config
{
	void DescribeSwitches(TArray<FSwitchState>& OutSwitches)
	{
		for (const FSwitchDef& Switch : Switches)
		{
			FSwitchState& State = OutSwitches.AddDefaulted_GetRef();
			State.Key = Switch.Key;
			State.Description = Switch.Description;
			const IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(Switch.ConsoleVariable);
			State.bLive = Var && Var->GetInt() != 0;
			if (const UConfigPropertyBool* Bool = FindFileProperty(Switch))
			{
				State.bHasFileValue = true;
				State.bFileValue = Bool->Value;
			}
		}
	}

	bool SetSwitch(const FString& Key, bool bOn, FString& OutKey, bool& bOutSaved, FString& OutError)
	{
		bOutSaved = false;
		for (const FSwitchDef& Switch : Switches)
		{
			if (!Key.Equals(Switch.Key, ESearchCase::IgnoreCase))
			{
				continue;
			}
			OutKey = Switch.Key;
			IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(Switch.ConsoleVariable);
			if (!Var)
			{
				OutError = FString::Printf(TEXT("%s is missing; nothing changed."), Switch.ConsoleVariable);
				return false;
			}
			if (UConfigPropertyBool* Bool = FindFileProperty(Switch))
			{
				Bool->Value = bOn;
				// MarkDirty queues SML's save of the file and fires the section's change event, which re-applies the file.
				Bool->MarkDirty();
				bOutSaved = true;
			}
			// The re-applied file value is ignored for a variable last set from the console (console outranks SetByCode),
			// so make sure the change takes effect now.
			if ((Var->GetInt() != 0) != bOn)
			{
				Var->Set(bOn ? TEXT("1") : TEXT("0"), ECVF_SetByConsole);
			}
			return true;
		}
		FString Known;
		for (const FSwitchDef& Switch : Switches)
		{
			Known += FString::Printf(TEXT("%s%s"), Known.IsEmpty() ? TEXT("") : TEXT(", "), Switch.Key);
		}
		OutError = FString::Printf(TEXT("Unknown switch '%s'. Switches: %s."), *Key, *Known);
		return false;
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
