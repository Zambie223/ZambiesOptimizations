#pragma once

#include "CoreMinimal.h"
#include "Command/ChatCommandInstance.h"
#include "Command/CommandSender.h"
#include "Module/GameWorldModule.h"
#include "ZOChatCommand.generated.h"

/**
 * /zopt chat command, for hosts that have no server console (many rented servers). Runs on the server (SML executes
 * chat commands there). On a dedicated server only administrators may use it; in single player or on a listen server
 * only the host. Not replicated, so players without the mod never see an unknown actor.
 *   /zopt                      lists the switches: live value and the value saved in the settings file
 *   /zopt <switch> on|off      switches one at once and saves it to the settings file
 */
UCLASS(NotBlueprintable)
class AZambiesOptimizationsChatCommand : public AChatCommandInstance
{
	GENERATED_BODY()

public:
	AZambiesOptimizationsChatCommand();

	virtual EExecutionStatus ExecuteCommand_Implementation(UCommandSender* Sender, const TArray<FString>& Arguments, const FString& Label) override;

private:
	static bool IsAllowed(UCommandSender* Sender);
};

/**
 * Command sender for the development command ZOpt.DevChat (compiled only with ZOPT_DEV_COMMANDS=1): it counts as the
 * server console and writes chat replies to the log, so /zopt can be tested on a server with nobody connected. The
 * class exists in every build because UHT does not allow custom #if around a UCLASS; nothing uses it in a release.
 */
UCLASS()
class UZambiesOptimizationsDevSender : public UCommandSender
{
	GENERATED_BODY()

public:
	virtual FString GetSenderName() const override;
	virtual void SendChatMessage(const FString& Message, const FLinearColor PrefixColor) override;
};

/** Root world module: registers the chat command. */
UCLASS()
class UZambiesOptimizationsWorldModule : public UGameWorldModule
{
	GENERATED_BODY()

public:
	UZambiesOptimizationsWorldModule();
};
