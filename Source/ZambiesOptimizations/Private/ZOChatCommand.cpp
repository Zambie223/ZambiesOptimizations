#include "ZOChatCommand.h"

#include "Command/CommandSender.h"
#include "Engine/World.h"
#include "FGPlayerController.h"
#include "Misc/CoreMisc.h"
#include "Server/FGDSAuthenticationTypes.h"
#include "ZambiesOptimizationsModule.h"
#include "ZOConfig.h"
#if ZOPT_DEV_COMMANDS
#include "Command/ChatCommandLibrary.h"
#include "GameFramework/PlayerState.h"
#include "HAL/IConsoleManager.h"
#include "Player/SMLRemoteCallObject.h"
#endif

namespace
{
	const TCHAR* OnOff(const bool bOn)
	{
		return bOn ? TEXT("on") : TEXT("off");
	}

	/** Accepts on/off, 1/0, true/false and yes/no. */
	bool ParseOnOff(const FString& Text, bool& bOutOn)
	{
		const FString Lower = Text.ToLower();
		if (Lower == TEXT("on") || Lower == TEXT("1") || Lower == TEXT("true") || Lower == TEXT("yes"))
		{
			bOutOn = true;
			return true;
		}
		if (Lower == TEXT("off") || Lower == TEXT("0") || Lower == TEXT("false") || Lower == TEXT("no"))
		{
			bOutOn = false;
			return true;
		}
		return false;
	}

	void ListSwitches(UCommandSender* Sender)
	{
		TArray<ZOpt::Config::FSwitchState> Switches;
		ZOpt::Config::DescribeSwitches(Switches);
		for (const ZOpt::Config::FSwitchState& Switch : Switches)
		{
			const FString Saved = !Switch.bHasFileValue ? FString(TEXT("no settings file"))
				: FString::Printf(TEXT("saved %s"), OnOff(Switch.bFileValue));
			Sender->SendChatMessage(FString::Printf(TEXT("%s: %s (%s) - %s"), *Switch.Key, OnOff(Switch.bLive), *Saved, *Switch.Description),
				Switch.bLive ? FLinearColor::Green : FLinearColor::White);
		}
		Sender->SendChatMessage(TEXT("/zopt <switch> on|off switches one now and saves it."), FLinearColor::White);
	}
}

AZambiesOptimizationsChatCommand::AZambiesOptimizationsChatCommand()
{
	CommandName = TEXT("zopt");
	Usage = FText::FromString(TEXT("/zopt [<switch> on|off]"));
	MinNumberOfArguments = 0;
	bOnlyUsableByPlayer = false;
	bReplicates = false;
}

bool AZambiesOptimizationsChatCommand::IsAllowed(UCommandSender* Sender)
{
	if (!Sender->IsPlayerSender())
	{
		return true; // server console
	}
	const AFGPlayerController* Player = Sender->GetPlayer();
	if (!Player)
	{
		return false;
	}
	const UWorld* World = Player->GetWorld();
	if (World && World->GetNetMode() == NM_DedicatedServer)
	{
		return Player->GetDSPrivilegeLevel() == EPrivilegeLevel::Administrator;
	}
	return Player->IsLocalController(); // the host in single player or on a listen server
}

EExecutionStatus AZambiesOptimizationsChatCommand::ExecuteCommand_Implementation(UCommandSender* Sender, const TArray<FString>& Arguments, const FString& Label)
{
	if (!IsAllowed(Sender))
	{
		Sender->SendChatMessage(TEXT("/zopt is limited to server administrators (or the host)."), FLinearColor::Red);
		return EExecutionStatus::INSUFFICIENT_PERMISSIONS;
	}
	if (!IsRunningDedicatedServer())
	{
		Sender->SendChatMessage(TEXT("Zambie's Optimizations only acts on a dedicated server; nothing to switch here."), FLinearColor::Yellow);
		return EExecutionStatus::COMPLETED;
	}
	if (Arguments.Num() == 0 || Arguments[0].Equals(TEXT("list"), ESearchCase::IgnoreCase))
	{
		ListSwitches(Sender);
		return EExecutionStatus::COMPLETED;
	}
	bool bOn = false;
	if (Arguments.Num() != 2 || !ParseOnOff(Arguments[1], bOn))
	{
		PrintCommandUsage(Sender);
		return EExecutionStatus::BAD_ARGUMENTS;
	}
	FString Key;
	FString Error;
	bool bSaved = false;
	if (!ZOpt::Config::SetSwitch(Arguments[0], bOn, Key, bSaved, Error))
	{
		Sender->SendChatMessage(Error, FLinearColor::Red);
		return EExecutionStatus::BAD_ARGUMENTS;
	}
	UE_LOG(LogZOpt, Display, TEXT("Zambie's Optimizations: /zopt by %s: %s %s%s"), *Sender->GetSenderName(), *Key, OnOff(bOn), bSaved ? TEXT(" (saved)") : TEXT(" (not saved: no settings file)"));
	Sender->SendChatMessage(bSaved
		? FString::Printf(TEXT("%s is now %s, and saved."), *Key, OnOff(bOn))
		: FString::Printf(TEXT("%s is now %s until restart (the settings file is not available, so it was not saved)."), *Key, OnOff(bOn)),
		FLinearColor::Green);
	return EExecutionStatus::COMPLETED;
}

UZambiesOptimizationsWorldModule::UZambiesOptimizationsWorldModule()
{
	bRootModule = true;
	mChatCommands.Add(AZambiesOptimizationsChatCommand::StaticClass());
}

FString UZambiesOptimizationsDevSender::GetSenderName() const
{
	// Not the command's name: the release check scans the zips for "ZOpt.DevChat", and this class is in every build.
	return TEXT("ZOpt dev test sender");
}

void UZambiesOptimizationsDevSender::SendChatMessage(const FString& Message, const FLinearColor PrefixColor)
{
	UE_LOG(LogZOpt, Display, TEXT("Zambie's Optimizations: [chat reply] %s"), *Message);
}

#if ZOPT_DEV_COMMANDS
namespace
{
	FAutoConsoleCommandWithWorldArgsAndOutputDevice GDevChatCommand(
		TEXT("ZOpt.DevChat"),
		TEXT("[dev] ZOpt.DevChat <chat command without the slash> - runs it as the server console (replies go to the log). ZOpt.DevChat as <player name> <chat command> - runs it as that connected player through SML's server entry point for typed chat (permission checks included)."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
		{
			if (!World || Args.Num() < 1)
			{
				Ar.Log(TEXT("Usage: ZOpt.DevChat [as <player name>] <chat command without the slash>"));
				return;
			}
			if (Args[0].Equals(TEXT("as"), ESearchCase::IgnoreCase))
			{
				if (Args.Num() < 3)
				{
					Ar.Log(TEXT("Usage: ZOpt.DevChat as <player name> <chat command without the slash>"));
					return;
				}
				TArray<FString> Rest = Args;
				Rest.RemoveAt(0, 2);
				const FString CommandLine = FString::Join(Rest, TEXT(" "));
				for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
				{
					AFGPlayerController* Player = Cast<AFGPlayerController>(It->Get());
					if (!Player || !Player->PlayerState || !Player->PlayerState->GetPlayerName().Equals(Args[1], ESearchCase::IgnoreCase))
					{
						continue;
					}
					USMLRemoteCallObject* Remote = Cast<USMLRemoteCallObject>(Player->GetRemoteCallObjectOfClass(USMLRemoteCallObject::StaticClass()));
					if (!Remote)
					{
						Ar.Log(TEXT("That player has no SML remote call object."));
						return;
					}
					Remote->HandleChatCommand_Implementation(CommandLine);
					Ar.Log(FString::Printf(TEXT("Ran '/%s' as %s; the result is in LogChatCommand and that player's chat."), *CommandLine, *Args[1]));
					return;
				}
				Ar.Log(FString::Printf(TEXT("No connected player named '%s'."), *Args[1]));
				return;
			}
			AChatCommandSubsystem* Chat = AChatCommandSubsystem::Get(World);
			if (!Chat)
			{
				Ar.Log(TEXT("No chat command subsystem in this world."));
				return;
			}
			const FString CommandLine = FString::Join(Args, TEXT(" "));
			const EExecutionStatus Status = Chat->RunChatCommand(CommandLine, NewObject<UZambiesOptimizationsDevSender>());
			Ar.Log(FString::Printf(TEXT("Ran '/%s' as the server console: %s (replies in LogZOpt)."), *CommandLine, *StaticEnum<EExecutionStatus>()->GetNameStringByValue(static_cast<int64>(Status))));
		}));
}
#endif
