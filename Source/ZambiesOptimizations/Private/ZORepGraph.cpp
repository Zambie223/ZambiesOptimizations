// Replication graph debug dump switch on a dedicated server (ZOpt.SkipRepGraphDump).
//
// When the replication graph meets an actor that is no longer valid for replication (seen on a live server when a
// player rejoined while their old player state was being destroyed), UReplicationGraph's LogMoreInfoOnIsActorValidFailure
// (ReplicationGraph.cpp) logs an error and runs "Net.RepGraph.PrintAll": every replication node with every actor name.
// On a large save that was 135 MB of log written in about 2 s, inside a single 3.6 s server frame. The invalid actor is
// skipped either way; the dump is diagnostics only. It happens at most once per Net.RepGraph.LogDebugInfoPeriod
// seconds (engine default 300); 0 switches the dump (and its one error line) off.
//
// This switch sets Net.RepGraph.LogDebugInfoPeriod to 0 while it is on and puts the previous value back when it is
// switched off. No code hooks; dedicated servers only (the replication graph runs only there).

#include "HAL/IConsoleManager.h"
#include "Misc/CoreMisc.h"
#include "ZambiesOptimizationsModule.h"

namespace
{
	const TCHAR* const EngineVariableName = TEXT("Net.RepGraph.LogDebugInfoPeriod");

	int32 GCVarSkipRepGraphDump = 0;
	/** The engine variable's value before this switch first changed it; restored when the switch goes back to 0. */
	FString GOriginalPeriod;
	bool GPeriodChanged = false;

	void OnSkipRepGraphDumpChanged(IConsoleVariable*)
	{
		if (!IsRunningDedicatedServer())
		{
			return;
		}
		IConsoleVariable* Period = IConsoleManager::Get().FindConsoleVariable(EngineVariableName);
		if (!Period)
		{
			if (GCVarSkipRepGraphDump != 0)
			{
				UE_LOG(LogZOpt, Warning, TEXT("Zambie's Optimizations: %s not found (game update?); the replication dump switch has no effect"), EngineVariableName);
			}
			return;
		}
		if (GCVarSkipRepGraphDump != 0)
		{
			if (!GPeriodChanged)
			{
				GOriginalPeriod = Period->GetString();
				GPeriodChanged = true;
			}
			// SetByCode outranks Engine.ini and launch parameters; a value set from the console still wins.
			Period->Set(TEXT("0"), ECVF_SetByCode);
		}
		else if (GPeriodChanged)
		{
			Period->Set(*GOriginalPeriod, ECVF_SetByCode);
			GPeriodChanged = false;
		}
		UE_LOG(LogZOpt, Display, TEXT("Zambie's Optimizations: replication dump switch %d (%s = %s)"),
			GCVarSkipRepGraphDump != 0 ? 1 : 0, EngineVariableName, *Period->GetString());
	}

	FAutoConsoleVariableRef CVarSkipRepGraphDump(
		TEXT("ZOpt.SkipRepGraphDump"), GCVarSkipRepGraphDump,
		TEXT("1 = skip the replication graph's full debug dump (Net.RepGraph.PrintAll) when it meets an invalid actor, on this dedicated server. 0 = engine behaviour (default)."),
		FConsoleVariableDelegate::CreateStatic(&OnSkipRepGraphDumpChanged), ECVF_Default);
}
