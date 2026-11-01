# M6-04 report: heartbeat, online settings, map registry, ProjectVersion

Result: done. Build (VeloxEditor, Velox) green, all automation tests green incl. Velox.Online.HeartbeatJson and Velox.Online.MapRegistry. Server/Client targets skipped by build.sh (installed engine).

Golden heartbeat (L_ItemTest listen host, -ServerId=test -ServerName=T, 2 bots, host spectator excluded):

    {"schema":1,"id":"test","name":"T","port":8443,"map":"L_ItemTest","map_display_name":"Item test","mode":"FFA","phase":"Live","time_left_s":598,"players":0,"max_players":16,"bots":2,"spectators":0,"version":"0.6.0","net_version":"4065349793","password":false}

LogNetVersion: before (older main-checkout log, ProjectVersion 1.0.0.0): checksum 3779376671; now "Velox 0.6.0 ... Checksum: 4065349793".

Accessors: IsABot()/IsOnlyASpectator() are the engine's APlayerState ones; nothing added to PlayerState/GameState. Host spectator = spectator PlayerState whose owner is a local PlayerController, only when -nullrhi is on the command line (the VeloxServerKind helper from task 02 does not exist on this branch; swap to IsVeloxHeadlessHost() later).

Delegates: FGameModeEvents::OnGameModePostLoginEvent / OnGameModeLogoutEvent (static engine events, no GameMode edit) and AVeloxGameState::OnPhaseChanged (rebound when the GameState changes); debounce 2 s via a 0.1 s core ticker on real time.

Notes
- DISPLAY_NAME in dm1_layout.py was not touched; the registry is the runtime home, the script constant must be kept in sync by hand.
- Phase in the 90 s stub run was Countdown then Live (2 bots reach MinPlayersToStart), not Warmup.
- Dedicated nettest servers (no -ServerId) get one Warning per minute "VeloxHeartbeat failed code=0 url=...:8790" because bHeartbeatEnabled defaults true and nothing listens. Suggested: soak scripts pass -ini:Game:[/Script/Velox.VeloxOnlineSettings]:bHeartbeatEnabled=False (outside this task's scope).
- Packaged Linux builds need the HTTP module's curl/ssl libs staged (task 01/02 to confirm).
