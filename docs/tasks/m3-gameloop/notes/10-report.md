# M3-10 menus — agent report (2026-10-29)
build.sh 69 tests in the worktree (+ `Velox.UI.MenuMath`, `Velox.UI.MenuWidgets`), 0 failures, no warnings (`20261029-221734-3979251`). Standalone start → `L_MainMenu` with `VeloxMenuGameMode`, menu in viewport. `velox.menu.FtMenus` PASS (18 key rows). Connect to a headless server on port 7860: welcomed, HUD created. Connect to a dead port: after the 20 s engine timeout `VeloxMenu network failure type=ConnectionTimeout`, back in `L_MainMenu` with the status. `velox.menu.FtEscape quit` PASS. `settings-restart.sh` OK.
- `UVeloxGameInstance`: `ConnectToServer` (normalise, `ClientTravel(BuildConnectURL)`, save `LastConnectAddress` on start), `HostLocal`, `ReturnToFrontEnd(Message)`, `ConsumeMenuMessage`; binds `OnNetworkFailure`/`OnTravelFailure` (client drivers only); engine returns to `GameDefaultMap`. Config `DefaultServerPort` 7777, `HostLocalURL`.
- `GameDefaultMap` = `/Game/Maps/L_MainMenu` (bootstrap script, `AVeloxMenuGameMode`, `AVeloxMenuPlayerController` UI-only, registers IMC_Default for the key list). `MapsToCook` += L_MainMenu.
- Widgets on `UVeloxUserWidget` (+ `MakeButton`/`GetButtonLabel`): main menu (address, Connect, Host, Settings, Quit, status), settings (name, sensitivity, invert, FOV, auto-switch, crosshair, 3 volumes, window mode, resolution, VSync, frame limit, AA, key list with click-to-rebind, Apply/Defaults/Back), pause menu (Resume, Settings, Disconnect).
- Escape: `BindMenuInput(UEnhancedInputComponent*)` from `SetupInputComponent`, config `MenuInputAction` = IA_Menu. `ApplyUIInputMode()`: cursor + GameAndUI while `IsInIntermission()`, restored on `ClientReset`/menu close.
- `GetCurrentKeyProfile` → `GetActiveKeyProfile` (deprecation fixed).
## Open
- **`?Name=` ignored by the server**: `UPendingNetGame` overrides it with `ULocalPlayer::GetNickname()`. Fix: `ULocalPlayer` subclass overriding `GetGameLoginOptions()` → `Name=<PlayerName>`, `LocalPlayerClassName` in DefaultEngine.ini.
- Not live-tested: mouse-click rebind through Slate, host local travel, layout (needs the Mac).
- For 12: menu cursor, Escape in a match, rebind by click, wrong-IP message (~20 s). Commands `velox.menu.FtMenus/FtEscape/Connect/Disconnect/Dump`.
- Merged into m3-gameloop with M3-10 (ea57949); merged build 73 tests, 0 failures (`Saved/CI/20261029-223110-4001558`); FtScoreboard, FtEscape, FtMenus PASS.
