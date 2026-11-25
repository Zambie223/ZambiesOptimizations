#include "ZambiesOptimizationsModule.h"

#include "ZOSkelMesh.h"

DEFINE_LOG_CATEGORY(LogZOpt);

// Zambie's Optimizations: a bundle of separate performance optimizations, each behind its own switch.
//
// Rules for every optimization in this bundle:
//  - Off until switched on. A switch is a setting in the Mod Config file (ZOConfig.cpp: Configs/ZambiesOptimizations.cfg,
//    or the in-game Mods menu) mirrored by a ZOpt.* console variable for live changes until restart.
//  - Installs nothing while switched off. Code hooks are installed only when the switch is first turned on, so a
//    game patch that breaks one optimization's hooks cannot crash servers that never switched it on.
//  - Runs only where it helps (a dedicated-server optimization does nothing on a client or in single player) and never
//    requires players to install the mod.
//
// Current optimizations: the skeletal mesh tick switch (ZOSkelMesh.cpp) and the replication debug dump switch
// (ZORepGraph.cpp). Switches from chat: /zopt (ZOChatCommand.cpp, administrators). Read-only tools: ZOpt.SkelReport,
// ZOpt.DockStats (ZODockStats.cpp).
//
// History: this mod was developed as "TickOpt" (Tick Optimizer) up to 0.1.0 and renamed before its first public
// release. TickOpt 0.1.0 removed experiment 1 (an FApp::IsMultithreadServer hook): on this game
// no actor or component tick is marked to run off the game thread on a dedicated server, so it changed nothing.

void FZambiesOptimizationsModule::StartupModule()
{
#if ZOPT_DEV_COMMANDS
	UE_LOG(LogZOpt, Warning, TEXT("Zambie's Optimizations: development build (ZOPT_DEV_COMMANDS=1, ZOpt.DevChat): not for release"));
#endif
#if !WITH_EDITOR
	if (!IsRunningDedicatedServer())
	{
		UE_LOG(LogZOpt, Display, TEXT("Zambie's Optimizations: not a dedicated server, nothing to do."));
		return;
	}
	ZOpt::StartSkelSweeper();
#endif
}

void FZambiesOptimizationsModule::ShutdownModule()
{
#if !WITH_EDITOR
	ZOpt::StopSkelSweeper();
#endif
}

IMPLEMENT_MODULE(FZambiesOptimizationsModule, ZambiesOptimizations)
