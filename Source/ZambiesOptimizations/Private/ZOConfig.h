// The settings file, through SML's Mod Config: Configs/ZambiesOptimizations.cfg next to the game's FactoryGame folder
// (on a dedicated server: in its install folder), plus the in-game Mods menu wherever the game has a user interface.
// Every machine reads only its own file; nothing here is replicated. The optimizations act only on a dedicated server,
// so a player's own copy of the file changes nothing on the server they join.
//
// Each setting is mirrored by a ZOpt.* console variable. The file sets it at startup (and again when the file changes,
// e.g. from the Mods menu or /zopt); the console changes it live until restart without touching the file.
#pragma once

#include "CoreMinimal.h"
#include "Module/GameInstanceModule.h"
#include "ZOConfig.generated.h"

/** Root game instance module: registers the Mod Configuration and applies it to the ZOpt.* console variables. */
UCLASS()
class UZambiesOptimizationsGameInstanceModule : public UGameInstanceModule
{
	GENERATED_BODY()

public:
	UZambiesOptimizationsGameInstanceModule();

	virtual void DispatchLifecycleEvent(ELifecyclePhase Phase) override;

private:
	UFUNCTION()
	void OnConfigValueChanged();
};

namespace ZOpt::Config
{
	/** One switch as the /zopt chat command shows it. */
	struct FSwitchState
	{
		FString Key;
		FString Description;
		bool bLive = false;
		/** False when the settings file is not registered (the switch then exists only as a console variable). */
		bool bHasFileValue = false;
		bool bFileValue = false;
	};

	void DescribeSwitches(TArray<FSwitchState>& OutSwitches);

	/**
	 * Switches one setting (key matched without regard to case): saves it to the settings file and applies it at once.
	 * Returns false with OutError for an unknown key. bOutSaved is false when the settings file is not registered; the
	 * change then lasts until restart.
	 */
	bool SetSwitch(const FString& Key, bool bOn, FString& OutKey, bool& bOutSaved, FString& OutError);
}
