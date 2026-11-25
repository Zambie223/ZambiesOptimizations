// Skeletal mesh tick switch on a dedicated server (developed as TickOpt's "experiment 2").
//
// TickProfiler on a late-game save (Windows DS, no players): SkeletalMeshComponent was the most expensive class,
// about 2600 component ticks per frame at about 1.5 us each, 4 ms of a 30 ms frame, all on the game thread. The engine
// already disables the tick on a dedicated server for meshes set to OnlyTickPoseWhenRendered
// (USkeletalMeshComponent::OnRegister); the ones left tick for other reasons.
//
//   ZOpt.SkelReport [n]       read-only: ticking meshes grouped by owner class and settings
//   Meshes that simulate physics or cloth are never switched off. Train platforms, truck stations and railroad
//   vehicles are never switched off either.
//
// TickOpt 0.1.0 removed the testing commands TickOpt.SkelTick (manual group switch) and TickOpt.SkelDocking (include the
// docking-related classes).
//
// Automatic switch. Off unless switched on with:
//   ZOpt.SkelBuildables 1     meshes on buildables (train platforms, truck stations, railroad vehicles excepted)
//   ZOpt.SkelUnpossessed 1    meshes on pawns without a controller (offline players' bodies)
// The saved setting is the Mod Config file (ZOConfig.cpp sets these console variables from it at startup); the
// console changes them live until restart (0 switches every mesh it switched off back on). TickOpt 0.0.3-0.1.0 read
// Engine.ini [SystemSettings] and launch parameters instead; dropped with the move to Mod Config. Every 10 s a sweep switches off the ticks of the chosen groups (this also catches meshes built
// later and ones the game switched back on) and switches a mesh back on when it no longer belongs to a chosen group (a
// pawn that gets a controller again). Measured on a late-game save with one player: buildables 43.3 -> 37.8 ms of frame
// work, buildables + unpossessed 43.3 -> 35.5 ms (Windows DS); 58 -> 48.5 ms (Linux DS).

#include "ZOSkelMesh.h"

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Buildables/FGBuildable.h"
#include "Buildables/FGBuildableDockingStation.h"
#include "Buildables/FGBuildableTrainPlatform.h"
#include "FGRailroadVehicle.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"
#include "ZambiesOptimizationsModule.h"
#include "UObject/UObjectIterator.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "HAL/PlatformTime.h"
#include "UObject/UObjectHash.h"

namespace
{
	enum class ESkelGroup : uint8 { NoAnim, Unpossessed, Buildables };

	// Train platforms, truck stations and railroad vehicles run load/unload sequences whose animation may be read back on
	// the server (dock-1/dock-2: with them switched off, 2 of 4 segments completed far fewer train transfers). Always
	// left alone.
	bool IsDockingRelated(const AActor* Owner)
	{
		return Owner && (Owner->IsA<AFGBuildableTrainPlatform>() || Owner->IsA<AFGBuildableDockingStation>() || Owner->IsA<AFGRailroadVehicle>());
	}

	bool IsTicking(const USkeletalMeshComponent* Mesh, const UWorld* World)
	{
		return Mesh && Mesh->GetWorld() == World && Mesh->IsRegistered() && Mesh->IsComponentTickEnabled() && !Mesh->IsTemplate();
	}

	// Group membership from the owner and anim instance only (cheap).
	bool InGroupMembership(const USkeletalMeshComponent* Mesh, const ESkelGroup Group)
	{
		const AActor* Owner = Mesh->GetOwner();
		if (IsDockingRelated(Owner))
		{
			return false;
		}
		const bool bNoAnim = Mesh->AnimScriptInstance == nullptr && Mesh->GetPostProcessInstance() == nullptr;
		const APawn* Pawn = Cast<APawn>(Owner);
		const bool bUnpossessed = Pawn && Pawn->GetController() == nullptr;
		const bool bBuildable = Owner && Owner->IsA<AFGBuildable>();
		switch (Group)
		{
		case ESkelGroup::NoAnim: return bNoAnim;
		case ESkelGroup::Unpossessed: return bUnpossessed;
		case ESkelGroup::Buildables: return bBuildable;
		}
		return false;
	}

	// Membership, and never a mesh that simulates physics or cloth. The physics check asks every body of the mesh, so
	// it runs last (linux-1: a sweep with it first took about 9.5 ms on the Linux DS).
	bool InGroup(const USkeletalMeshComponent* Mesh, const ESkelGroup Group)
	{
		return InGroupMembership(Mesh, Group) && Mesh->GetClothingSimulation() == nullptr && !Mesh->IsSimulatingPhysics();
	}

	FString TickOptionName(const USkeletalMeshComponent* Mesh)
	{
		const UEnum* Enum = StaticEnum<EVisibilityBasedAnimTickOption>();
		return Enum ? Enum->GetNameStringByValue(static_cast<int64>(Mesh->VisibilityBasedAnimTickOption)) : TEXT("?");
	}

	FAutoConsoleCommandWithWorldArgsAndOutputDevice GSkelReport(
		TEXT("ZOpt.SkelReport"),
		TEXT("ZOpt.SkelReport [n] - ticking skeletal meshes grouped by owner class, anim instance class and tick settings (read-only)."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
		{
			const int32 Limit = Args.Num() > 0 ? FMath::Clamp(FCString::Atoi(*Args[0]), 1, 500) : 40;
			TMap<FString, int32> Groups;
			int32 Total = 0, Ticking = 0, NoAnim = 0, Unpossessed = 0, Buildables = 0, AnyThread = 0, Physics = 0;
			for (TObjectIterator<USkeletalMeshComponent> It; It; ++It)
			{
				USkeletalMeshComponent* Mesh = *It;
				if (!Mesh || Mesh->GetWorld() != World || Mesh->IsTemplate())
				{
					continue;
				}
				++Total;
				if (!IsTicking(Mesh, World))
				{
					continue;
				}
				++Ticking;
				const AActor* Owner = Mesh->GetOwner();
				const UAnimInstance* Anim = Mesh->AnimScriptInstance;
				const bool bPhysics = Mesh->IsSimulatingPhysics() || Mesh->GetClothingSimulation() != nullptr;
				Physics += bPhysics ? 1 : 0;
				NoAnim += InGroup(Mesh, ESkelGroup::NoAnim) ? 1 : 0;
				Unpossessed += InGroup(Mesh, ESkelGroup::Unpossessed) ? 1 : 0;
				Buildables += InGroup(Mesh, ESkelGroup::Buildables) ? 1 : 0;
				AnyThread += Mesh->PrimaryComponentTick.bRunOnAnyThread ? 1 : 0;
				const FString Key = FString::Printf(TEXT("%s | anim %s | %s | %s%s%s%s"),
					Owner ? *Owner->GetClass()->GetName() : TEXT("(no owner)"),
					Anim ? *Anim->GetClass()->GetName() : TEXT("none"),
					*TickOptionName(Mesh),
					Mesh->PrimaryComponentTick.bRunOnAnyThread ? TEXT("any thread") : TEXT("game thread"),
					Mesh->bEnableUpdateRateOptimizations ? TEXT(", URO") : TEXT(""),
					Mesh->LeaderPoseComponent.IsValid() ? TEXT(", follower") : TEXT(""),
					bPhysics ? TEXT(", physics/cloth") : TEXT(""));
				++Groups.FindOrAdd(Key);
			}
			Groups.ValueSort([](const int32 A, const int32 B) { return A > B; });
			Ar.Log(FString::Printf(TEXT("Skeletal meshes in this world: %d, ticking %d (any thread %d; no anim instance %d, unpossessed pawns %d, buildables %d, physics/cloth %d)"),
				Total, Ticking, AnyThread, NoAnim, Unpossessed, Buildables, Physics));
			int32 Shown = 0;
			for (const TPair<FString, int32>& Pair : Groups)
			{
				if (Shown++ >= Limit)
				{
					break;
				}
				Ar.Log(FString::Printf(TEXT("%6d  %s"), Pair.Value, *Pair.Key));
			}
		}));
}

namespace
{
	// The switches. Off by default; see the header comment for the ways to set them.
	int32 GCVarBuildables = 0;
	int32 GCVarUnpossessed = 0;

	bool GAutoBuildables = false;
	bool GAutoUnpossessed = false;
	TSet<TWeakObjectPtr<USkeletalMeshComponent>> GAutoOff;
	FTSTicker::FDelegateHandle GSweepHandle;
	int32 GLastLoggedOff = -1;

	UWorld* FindGameWorld()
	{
		if (!GEngine)
		{
			return nullptr;
		}
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			UWorld* World = Context.World();
			if (World && Context.WorldType == EWorldType::Game && World->AreActorsInitialized())
			{
				return World;
			}
		}
		return nullptr;
	}

	bool InAutoGroups(const USkeletalMeshComponent* Mesh)
	{
		return (GAutoBuildables && InGroup(Mesh, ESkelGroup::Buildables)) || (GAutoUnpossessed && InGroup(Mesh, ESkelGroup::Unpossessed));
	}

	// For meshes already switched off: their physics/cloth state was checked when they were switched off.
	bool StillInAutoGroups(const USkeletalMeshComponent* Mesh)
	{
		return (GAutoBuildables && InGroupMembership(Mesh, ESkelGroup::Buildables)) || (GAutoUnpossessed && InGroupMembership(Mesh, ESkelGroup::Unpossessed));
	}

	// Sweep timing, summarised in the log every 30 sweeps (5 minutes).
	struct FSweepStats { int32 Count = 0; double TotalMs = 0, MaxMs = 0, BackMs = 0, GatherMs = 0, MainMs = 0; };
	FSweepStats GSweepStats;

	bool Sweep(float)
	{
		GAutoBuildables = GCVarBuildables != 0;
		GAutoUnpossessed = GCVarUnpossessed != 0;
		const bool bActive = GAutoBuildables || GAutoUnpossessed;
		if (!bActive && GAutoOff.Num() == 0)
		{
			return true;
		}
		UWorld* World = FindGameWorld();
		if (!World)
		{
			GAutoOff.Reset();
			GLastLoggedOff = -1;
			return true;
		}
		const double Start = FPlatformTime::Seconds();
		int32 SwitchedOff = 0, BackOn = 0;

		// Meshes switched off earlier that left their group (a pawn that got a controller again, or a switch that was
		// turned off) tick again.
		for (auto It = GAutoOff.CreateIterator(); It; ++It)
		{
			USkeletalMeshComponent* Mesh = It->Get();
			if (!Mesh || Mesh->GetWorld() != World)
			{
				It.RemoveCurrent();
			}
			else if (!StillInAutoGroups(Mesh))
			{
				Mesh->SetComponentTickEnabled(true);
				It.RemoveCurrent();
				++BackOn;
			}
		}
		if (!bActive)
		{
			UE_LOG(LogZOpt, Display, TEXT("Zambie's Optimizations: skeletal mesh tick switch off: %d meshes switched back on, %d still off"), BackOn, GAutoOff.Num());
			GLastLoggedOff = -1;
			return true;
		}

		const double AfterBack = FPlatformTime::Seconds();
		TArray<UObject*> Meshes;
		GetObjectsOfClass(USkeletalMeshComponent::StaticClass(), Meshes, true, RF_ClassDefaultObject | RF_ArchetypeObject, EInternalObjectFlags::Garbage);
		const double AfterGather = FPlatformTime::Seconds();
		for (UObject* Object : Meshes)
		{
			USkeletalMeshComponent* Mesh = static_cast<USkeletalMeshComponent*>(Object);
			if (IsTicking(Mesh, World) && InAutoGroups(Mesh))
			{
				Mesh->SetComponentTickEnabled(false);
				GAutoOff.Add(Mesh);
				++SwitchedOff;
			}
		}

		const double End = FPlatformTime::Seconds();
		const double Ms = (End - Start) * 1000.0;
		FSweepStats& St = GSweepStats;
		++St.Count; St.TotalMs += Ms; St.MaxMs = FMath::Max(St.MaxMs, Ms);
		St.BackMs += (AfterBack - Start) * 1000.0; St.GatherMs += (AfterGather - AfterBack) * 1000.0; St.MainMs += (End - AfterGather) * 1000.0;
		if (St.Count >= 30)
		{
			UE_LOG(LogZOpt, Display, TEXT("Zambie's Optimizations: last %d sweeps: avg %.2f ms (re-check %.2f, gather %.2f, scan %.2f), max %.2f ms; %d meshes off"),
				St.Count, St.TotalMs / St.Count, St.BackMs / St.Count, St.GatherMs / St.Count, St.MainMs / St.Count, St.MaxMs, GAutoOff.Num());
			St = FSweepStats();
		}
		// Log the first sweep of a world and any sweep that changed something noticeably; others stay quiet.
		if (GLastLoggedOff < 0 || SwitchedOff >= 20 || BackOn >= 20 || Ms > 5.0)
		{
			UE_LOG(LogZOpt, Display, TEXT("Zambie's Optimizations: skeletal mesh sweep: %d switched off, %d back on, %d off in total (of %d meshes), %.2f ms"),
				SwitchedOff, BackOn, GAutoOff.Num(), Meshes.Num(), Ms);
			GLastLoggedOff = GAutoOff.Num();
		}
		else
		{
			UE_LOG(LogZOpt, Verbose, TEXT("Zambie's Optimizations: skeletal mesh sweep: %d switched off, %d back on, %d off in total, %.2f ms"), SwitchedOff, BackOn, GAutoOff.Num(), Ms);
		}
		return true;
	}

	// A change from the console applies at once instead of at the next sweep (before the sweeper starts: nothing).
	void OnSwitchChanged(IConsoleVariable*)
	{
		if (GSweepHandle.IsValid())
		{
			UE_LOG(LogZOpt, Display, TEXT("Zambie's Optimizations: skeletal mesh tick switch: buildables %d, unpossessed pawns %d"), GCVarBuildables != 0 ? 1 : 0, GCVarUnpossessed != 0 ? 1 : 0);
			Sweep(0.0f);
		}
	}

	FAutoConsoleVariableRef CVarSkelBuildables(
		TEXT("ZOpt.SkelBuildables"), GCVarBuildables,
		TEXT("1 = switch off the skeletal mesh ticks of buildables on this dedicated server (train platforms, truck stations and railroad vehicles excepted). 0 = off (default)."),
		FConsoleVariableDelegate::CreateStatic(&OnSwitchChanged), ECVF_Default);

	FAutoConsoleVariableRef CVarSkelUnpossessed(
		TEXT("ZOpt.SkelUnpossessed"), GCVarUnpossessed,
		TEXT("1 = switch off the skeletal mesh ticks of pawns without a controller (offline players' bodies) on this dedicated server. 0 = off (default)."),
		FConsoleVariableDelegate::CreateStatic(&OnSwitchChanged), ECVF_Default);
}

namespace ZOpt
{
	void StartSkelSweeper()
	{
		// The switches are set from the settings file later (ZOConfig.cpp, at game instance post-initialization, before
		// the world loads); that change logs the values in effect. Until then both are 0 and the sweep does nothing.
		UE_LOG(LogZOpt, Display, TEXT("Zambie's Optimizations: skeletal mesh tick sweeper started (every 10 s; switches ZOpt.SkelBuildables / ZOpt.SkelUnpossessed)"));
		GSweepHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&Sweep), 10.0f);
	}

	void StopSkelSweeper()
	{
		if (GSweepHandle.IsValid())
		{
			FTSTicker::GetCoreTicker().RemoveTicker(GSweepHandle);
			GSweepHandle.Reset();
		}
	}
}
