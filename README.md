# Zambie's Optimizations (experimental)

A collection of performance optimizations for Satisfactory, each behind its own switch. Every switch is **off until you turn it on**, so installing the mod changes nothing by itself.

The current optimizations are for **dedicated servers**. On a late-game save they cut the server's work per frame by about 13% on Windows and 16% on Linux, which raised the tick rate by about 13% and 19%. In single player and on a player's own game they do nothing.

**Status: experimental (0.1.0).** Tested on game version 1.2.5.1 (build 520140) with SML 3.12.1, on Windows and Linux dedicated servers, with a late-game save and one player joined. It has not yet been tested with many players or on many saves. Back up your save before trying it.

## The optimizations

### Skeletal mesh updates on the server

A dedicated server draws nothing, but it still updates the animated skeletons ("skeletal meshes") of thousands of machines and of every offline player's body, every frame. On the late-game save this was the most expensive single kind of work on the server's main thread: about 2,600 updates per frame.

These two switches stop those updates on the server:

| Setting | Console variable | What it covers |
|---|---|---|
| `SkelBuildables` | `ZOpt.SkelBuildables` | Animated machines and other buildables. Train stations, freight platforms, truck stations and train vehicles are always left alone: with those included, trains loaded and unloaded noticeably fewer times in testing. |
| `SkelUnpossessed` | `ZOpt.SkelUnpossessed` | Pawns without a controller, which on a server means the bodies of players who are offline. |

Meshes that simulate physics or cloth are never touched. Every 10 seconds the mod checks again: it covers machines built since, and switches a body back on as soon as its player rejoins.

**What players see:** animations are drawn by each player's own game, so machines still animate for players. This only changes the server's copy, which nobody sees.

**Measured results.** Late-game save, one player joined. The numbers are the server's frame work (time spent per frame, excluding the tick-rate sleep), measured with [Tick Profiler](https://github.com/Zambie223/TickProfiler):

| Server | Without | Both switches on | Tick rate |
|---|---|---|---|
| Windows dedicated server | 43 ms | 37.5 ms | 23.5 -> 26.5 Hz |
| Linux dedicated server | 58 ms | 48.5 ms | 17.6 -> 20.9 Hz |

Trains, trucks and drones kept working normally in these tests (5 to 7 minute stretches with the switches on and off, alternating). Truck arrivals and drone flights per minute did not change. Train loads/unloads varied from stretch to stretch about as much with the switches off as with them on. Every load/unload took the same 26.5 seconds, and none got stuck. Your numbers will depend on your save, your host and how much of your factory is animated machinery.

## Installation

The current optimizations need the mod on the **server only**. Players do not need it.

**No prebuilt download is published yet.** The zips below are what a release will contain. Until then, the mod can be built from this source with the Satisfactory modding toolchain (see the [modding documentation](https://docs.ficsit.app/)).

| Server | Download | Extract into |
|---|---|---|
| Linux dedicated server (most rented hosts) | `ZambiesOptimizations-LinuxServer.zip` | `FactoryGame/Mods/ZambiesOptimizations/` in the server's install folder |
| Windows dedicated server | `ZambiesOptimizations-WindowsServer.zip` | `FactoryGame\Mods\ZambiesOptimizations\` in the server's install folder |

The folder must end up holding `ZambiesOptimizations.uplugin` directly. The server also needs SML 3.12.1 or newer. If your server already runs mods, it has SML. If it doesn't, your players need SML too, because a server running SML refuses clients without it.

## Switching optimizations on

1. **The settings file** (works on rented hosts that only give you a file manager): `FactoryGame/Configs/ZambiesOptimizations.cfg` in the server's install folder. The server creates it at its first start with the mod, with every switch set to `false`. **Stop the server**, change the switches you want to `true`, and start it again. Edit the file only while the server is stopped, because the server writes it when settings change.
   With both skeletal mesh switches on, the file looks like this (leave the `SML_ModVersion_DoNotChange` line as it is):
   ```json
   {
   	"SkelBuildables": true,
   	"SkelUnpossessed": true,
   	"SML_ModVersion_DoNotChange": "0.1.0"
   }
   ```
2. **Live, from the server console** (the Server Manager console in the game, or the server API's `RunCommand`): for example `ZOpt.SkelBuildables 1`. `0` switches back on every update the mod switched off, at once. A console change lasts until the server restarts and does not change the settings file.

The in-game Mods menu (Mod Options, Zambie's Optimizations) shows the same switches, but it changes the file of the game you open it in. Changing it on your own PC does not change the server you join.

The server log (`FactoryGame.log`) shows what is on after each start:
```
LogZOpt: Display: Zambie's Optimizations: settings from the settings file: SkelBuildables 1, SkelUnpossessed 1
```
It also logs a timing line every 5 minutes. On the test save the 10-second check took 1 to 2 ms on average; the first check after the save loads, which switches everything off, took about 7 ms.

## Commands

All of these are server console commands.

| Command | What it does |
|---|---|
| `ZOpt.SkelBuildables 0\|1`, `ZOpt.SkelUnpossessed 0\|1` | The switches, live until restart (see above). Without a value, it prints the current value. |
| `ZOpt.SkelReport [n]` | Read-only: lists the server's animated meshes that are still updating, grouped by owner. |
| `ZOpt.DockStats [reset]` | Read-only: counts train loads/unloads, truck arrivals and departures, and drone landings and takeoffs once per second since the last `reset`. Use it to check that logistics still work on your save. |

## Something wrong?

Switch the optimization off with its console variable set to `0` (for example `ZOpt.SkelBuildables 0`), or set it to `false` in the settings file and restart. If the problem goes away, please report what broke and on which machine or vehicle. Nothing is stored in your save, so removing the mod is always safe.

## Responsible AI use disclosure

Most of the code, tooling and documentation in this mod were written by an AI coding assistant (Claude, made by Anthropic, used through Claude Code), working under the maintainer's direction. The test runs and the measurements above were also run and analysed by that assistant. The maintainer set the requirements and made the design decisions. Not every line has been reviewed by a person. AI-written code can contain mistakes: review it before relying on it, and back up your saves.
