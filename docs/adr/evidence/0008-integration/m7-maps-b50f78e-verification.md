# M7 maps integration verification (origin/m7-maps b50f78e), 2026-11-20

Host: headless VM, UE 5.8.4 installed engine (Server/Client targets cannot be built). Raw logs: /mnt/oldroot/velox-scratch/m7-integ-verify/.

| Check | Result |
|---|---|
| 1 build.sh | VeloxEditor OK, Velox (Game) OK, VeloxServer/VeloxClient SKIPPED (installed engine) |
| 2 automation | 144 tests; first run 1 fail (Velox.Items.ModeGating, stale test: Haste is disabled in Duel since P6/B3); after test fix 144/144 Success |
| 3 fts.sh | OK 21/21, all rows PASS incl. FtMapReachability for Sacrarium, Nemus, Orbita, Fornax, Culmen |
| 4 golden check | dm1, sacrarium, nemus, culmen, fornax, orbita: all byte-identical OK |
| 5 navlint | Sacrarium, Nemus, Culmen, Fornax, Orbita: clusters=0, lowclear=0, PASS |

Only code change: Source/Velox/Tests/VeloxItemTests.cpp ModeGating expects Haste disabled in Duel, enabled in FFA (Duel registry 0, FFA 2).
Not covered: origin/m7-maps moved by 2 commits (c79fead, a764a91, per-map soak bands) after b50f78e.
