# Changelog

Changes after the first public release are listed here.

## 0.2.0 (2026-11-25)
- New switch `SkipRepGraphDump` (console `ZOpt.SkipRepGraphDump`): skips the engine's full replication graph debug dump when it meets an actor that can no longer be sent to players. On a live server that dump wrote 135 MB to the log and froze the server for 3.6 seconds when a player joined. Off by default.
- New `/zopt` chat command for server administrators: `/zopt` lists the switches with their current and saved values; `/zopt <switch> on|off` switches one at once and saves it to the settings file. For hosts without a working server console.
- Settings files from 0.1.0 keep their values; the new switch is added as off.
- README: the new switch and command, and a note that a host's web panel console may not pass commands to the game.

## 0.1.0 downloads (2026-11-25)
- Prebuilt 0.1.0 zips for Linux and Windows dedicated servers are now on the [Releases page](https://github.com/Zambie223/ZambiesOptimizations/releases). The mod itself is unchanged.
- README: the installation section now links to the downloads.
