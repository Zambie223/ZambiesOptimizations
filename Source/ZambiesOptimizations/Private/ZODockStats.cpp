// Gameplay check for experiment 2: do train platforms, truck stations and drone stations keep working while their
// skeletal mesh ticks are switched off? Read-only. Once started, it samples every platform/station once per second and
// counts state changes:
//   trains : platform docking status entering Complete (one finished load/unload sequence), with the duration of each
//            sequence (from leaving None to entering Complete) and the platforms that have been in a sequence > 120 s
//   trucks : a docking station's docked actor appearing (arrival) and going away (departure)
//   drones : a drone station's drone status entering Docked (arrival) and Takeoff (departure)
//
//   ZOpt.DockStats          counts since the last reset, plus the current state distribution
//   ZOpt.DockStats reset    starts sampling (if needed) and zeroes the counts

#include "CoreMinimal.h"
#include "Buildables/FGBuildableDockingStation.h"
#include "Buildables/FGBuildableDroneStation.h"
#include "Buildables/FGBuildableTrainPlatform.h"
#include "Containers/Ticker.h"
#include "EngineUtils.h"
#include "FGDroneStationInfo.h"
#include "HAL/IConsoleManager.h"
#include "ZambiesOptimizationsModule.h"

namespace
{
	struct FDockCounts
	{
		int32 TrainComplete = 0, TruckArrive = 0, TruckLeave = 0, DroneArrive = 0, DroneTakeoff = 0, Samples = 0;
		double SequenceSecondsSum = 0.0, SequenceSecondsMax = 0.0;
		int32 SequencesTimed = 0;
	};

	// Train platform -> time its current sequence started (left ETPDS_None); only sequences seen starting are timed.
	TMap<TWeakObjectPtr<AActor>, double> GSequenceStart;

	FDockCounts GDock;
	TMap<TWeakObjectPtr<AActor>, uint8> GLastState;
	TWeakObjectPtr<UWorld> GDockWorld;
	FTSTicker::FDelegateHandle GDockHandle;
	double GDockResetTime = 0.0;

	template <typename T, typename F>
	void ForEach(UWorld* World, F&& Fn)
	{
		for (TActorIterator<T> It(World); It; ++It)
		{
			if (IsValid(*It))
			{
				Fn(*It);
			}
		}
	}

	// Returns the previous state, or Current when the actor was not seen before (so a first sighting never counts).
	uint8 Remember(AActor* Actor, const uint8 Current)
	{
		uint8& Slot = GLastState.FindOrAdd(Actor, Current);
		const uint8 Previous = Slot;
		Slot = Current;
		return Previous;
	}

	bool Sample(float)
	{
		UWorld* World = GDockWorld.Get();
		if (!World)
		{
			return true;
		}
		++GDock.Samples;
		ForEach<AFGBuildableTrainPlatform>(World, [](AFGBuildableTrainPlatform* Platform)
		{
			const uint8 Now = static_cast<uint8>(Platform->GetDockingStatus());
			const uint8 Before = Remember(Platform, Now);
			const uint8 None = static_cast<uint8>(ETrainPlatformDockingStatus::ETPDS_None);
			const uint8 Complete = static_cast<uint8>(ETrainPlatformDockingStatus::ETPDS_Complete);
			// a sequence starts when a platform leaves None, or leaves Complete for the next wagon's sequence
			if (Now != Before && (Before == None || Before == Complete) && Now != None && Now != Complete)
			{
				GSequenceStart.Add(Platform, FPlatformTime::Seconds());
			}
			if (Now != Before && Now == Complete)
			{
				++GDock.TrainComplete;
				if (const double* Start = GSequenceStart.Find(Platform))
				{
					const double Seconds = FPlatformTime::Seconds() - *Start;
					GDock.SequenceSecondsSum += Seconds;
					GDock.SequenceSecondsMax = FMath::Max(GDock.SequenceSecondsMax, Seconds);
					++GDock.SequencesTimed;
					GSequenceStart.Remove(Platform);
				}
			}
			if (Now == None)
			{
				GSequenceStart.Remove(Platform);
			}
		});
		ForEach<AFGBuildableDockingStation>(World, [](AFGBuildableDockingStation* Station)
		{
			const uint8 Now = Station->GetDockedActor() ? 1 : 0;
			const uint8 Before = Remember(Station, Now);
			GDock.TruckArrive += (Now == 1 && Before == 0) ? 1 : 0;
			GDock.TruckLeave += (Now == 0 && Before == 1) ? 1 : 0;
		});
		ForEach<AFGDroneStationInfo>(World, [](AFGDroneStationInfo* Info)
		{
			const uint8 Now = static_cast<uint8>(Info->GetDroneStatus());
			const uint8 Before = Remember(Info, Now);
			if (Now != Before)
			{
				GDock.DroneArrive += Now == static_cast<uint8>(EDroneStatus::EDS_DOCKED) ? 1 : 0;
				GDock.DroneTakeoff += Now == static_cast<uint8>(EDroneStatus::EDS_TAKEOFF) ? 1 : 0;
			}
		});
		return true;
	}

	template <typename EnumType>
	FString Distribution(const TMap<uint8, int32>& Counts)
	{
		const UEnum* Enum = StaticEnum<EnumType>();
		FString Out;
		for (const TPair<uint8, int32>& Pair : Counts)
		{
			Out += FString::Printf(TEXT("%s%s %d"), Out.IsEmpty() ? TEXT("") : TEXT(", "),
				Enum ? *Enum->GetNameStringByValue(Pair.Key) : *FString::FromInt(Pair.Key), Pair.Value);
		}
		return Out;
	}

	FAutoConsoleCommandWithWorldArgsAndOutputDevice GDockStats(
		TEXT("ZOpt.DockStats"),
		TEXT("ZOpt.DockStats [reset] - train/truck/drone station activity counted once per second since the last reset (read-only)."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
		{
			if (Args.Num() > 0 && Args[0] == TEXT("reset"))
			{
				if (!GDockHandle.IsValid())
				{
					GDockHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&Sample), 1.0f);
				}
				GDockWorld = World;
				GDock = FDockCounts();
				GLastState.Reset();
				GSequenceStart.Reset();
				GDockResetTime = FPlatformTime::Seconds();
				Ar.Log(TEXT("Dock stats reset; sampling once per second."));
				return;
			}
			if (!GDockHandle.IsValid())
			{
				Ar.Log(TEXT("Not sampling. Start with: ZOpt.DockStats reset"));
				return;
			}
			const double Minutes = FMath::Max((FPlatformTime::Seconds() - GDockResetTime) / 60.0, 1e-6);
			Ar.Log(FString::Printf(TEXT("Dock stats over %.1f min (%d samples): train transfers completed %d (%.2f/min), trucks arrived %d / left %d, drones arrived %d (%.2f/min) / took off %d (%.2f/min)"),
				Minutes, GDock.Samples, GDock.TrainComplete, GDock.TrainComplete / Minutes, GDock.TruckArrive, GDock.TruckLeave,
				GDock.DroneArrive, GDock.DroneArrive / Minutes, GDock.DroneTakeoff, GDock.DroneTakeoff / Minutes));
			int32 Long = 0;
			const double NowSeconds = FPlatformTime::Seconds();
			for (const TPair<TWeakObjectPtr<AActor>, double>& Pair : GSequenceStart)
			{
				Long += (Pair.Key.IsValid() && NowSeconds - Pair.Value > 120.0) ? 1 : 0;
			}
			Ar.Log(FString::Printf(TEXT("Train sequences timed %d: avg %.1f s, max %.1f s; platforms in a sequence for more than 120 s now: %d"),
				GDock.SequencesTimed, GDock.SequencesTimed > 0 ? GDock.SequenceSecondsSum / GDock.SequencesTimed : 0.0, GDock.SequenceSecondsMax, Long));
			TMap<uint8, int32> Trains, Drones;
			ForEach<AFGBuildableTrainPlatform>(World, [&Trains](AFGBuildableTrainPlatform* P) { ++Trains.FindOrAdd(static_cast<uint8>(P->GetDockingStatus())); });
			ForEach<AFGDroneStationInfo>(World, [&Drones](AFGDroneStationInfo* I) { ++Drones.FindOrAdd(static_cast<uint8>(I->GetDroneStatus())); });
			Ar.Log(FString::Printf(TEXT("Now: train platforms %s"), *Distribution<ETrainPlatformDockingStatus>(Trains)));
			Ar.Log(FString::Printf(TEXT("Now: drone stations %s"), *Distribution<EDroneStatus>(Drones)));
		}));
}
