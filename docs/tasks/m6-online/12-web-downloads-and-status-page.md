# 12 — Landing page: downloads from `latest.json`, live server status, instructions

**Milestone:** M6 Online
**Agent:** dev
**Status:** todo
**Depends on:** ADR 0007 §3 (downloads layout, `latest.json`, `servers.json` schemas from tasks 03/06); answers 45 (privacy line), 46/47 (platform wording) can be filled in later

## Goal
`https://games.inceptum.at` tells a visitor what Velox is, lets them download the client for their platform (links work without JavaScript from the static `downloads/index.html`; the front page enhances with `latest.json`), shows the live server list from `servers.json`, and gives the per-platform launch notes and a one-line privacy statement. Plain HTML/CSS/JS, no framework, no tracking, no external requests.

## Scope — files/modules the agent may touch
- `infra/www/index.html`, `infra/www/style.css`, `infra/www/app.js` (new), `infra/www/img/` (a logo/placeholder PNG ≤ 50 KB via LFS or an inline SVG — prefer SVG), `infra/www/README.md`
- `infra/deploy/publish-download.sh` (task 06) — only the `downloads/index.html` template if it lives here (`infra/www/downloads-index.template.html`)
- `infra/nginx/README.md` — the sync command is already there; add the `status/` ownership note if task 03 did not
- `docs/tasks/m6-online/notes/12-report.md`
(No nginx config change; deployment of the page is `sudo rsync -a infra/www/ /var/www/games.inceptum.at/` — stated as a root step, run only when the lead says the page is ready.)

## Relevant ADRs / references
- ADR 0007 §3 (downloads, `latest.json`, status), §2 (`servers.json` schema), §4 (platform matrix: Linux headless-tested, macOS ad-hoc signed, Windows exception), §5.7 (logs keep IPs 14 d)
- `infra/www/index.html` (today's placeholder), `infra/nginx/games.inceptum.at.conf` (CSP-friendly headers are not set; do not add headers in HTML — nginx owns them)

## Acceptance criteria
- [ ] `index.html`: title, two sentences on what Velox is (arena FPS, Q3 spirit, UE 5.8, open playtest), a **Downloads** block rendered from `/downloads/latest.json` (version, per-platform button with size and sha256 in a `<details>`), a fallback link to `/downloads/` when the fetch fails, a **Servers** block rendered from `/status/servers.json` (name, map display name, mode, players/max (+bots), phase, `updated_s_ago`; "no servers reported" when empty; refresh every 15 s while the tab is visible; `address` shown with a copy button for the direct-IP field), **How to play** notes per platform (Linux: extract, `./Velox.sh`, needs a Vulkan driver, glibc ≥ 2.28; macOS: Gatekeeper note per q47; Windows: "not yet available" until q46), **Privacy**: "The server logs connection IP addresses for 14 days to diagnose problems; the status page shows player counts only." (adjust to the answer to 45), a link to the GitHub repository if the lead confirms it is public (else omit).
- [ ] Works with JavaScript disabled: the Downloads block shows the static `/downloads/` link and the Servers block says so; no layout shift when JS fills the blocks (reserved min-heights).
- [ ] No external resources (fonts, scripts, images) — everything served from the vhost; `curl -s https://games.inceptum.at | grep -c "https://"` counts only our own links.
- [ ] Accessible basics: semantic headings, `lang="en"`, contrast ≥ 4.5:1 for text, buttons reachable by keyboard, `prefers-color-scheme` dark variant.
- [ ] `downloads-index.template.html` (used by `publish-download.sh`) renders a static table for the current and the previous version with sizes and sha256; validated with a fixture `latest.json`.
- [ ] Page weight ≤ 100 KB total; `app.js` ≤ 6 KB, handles fetch errors and a `schema` mismatch gracefully (shows "unsupported status format").
- [ ] Verified locally with `python3 -I -m http.server` serving `infra/www/` plus fixture `downloads/latest.json` and `status/servers.json` (copy the task 03 golden file); a headless check with `curl` for the static parts; the JS path checked by reading the DOM in a local browser if one is available, else by a 10-line Node/JSDOM-free assertion script (`node` may be absent — then a careful manual review and a screenshot request to the human).

## Verification steps
1. `python3 -I -m http.server 8793 --directory <scratch copy of infra/www with fixtures>` → `curl -s localhost:8793/ | head`; open the fixture page where a browser exists; otherwise document the manual review.
2. `du -sh infra/www`; `grep -o 'https\?://[^"]*' infra/www/index.html | sort -u` → own host only.
3. Deployment (only when told): `sudo rsync -a infra/www/ /var/www/games.inceptum.at/` (stated first); `curl -I https://games.inceptum.at/` → 200; `curl -s https://games.inceptum.at/status/servers.json | head -c 200`.

## Report back
Standard format plus: a text sketch of the page, the fixture files used, and the wording that depends on answers 45–47 (so the lead can swap it).

## Notes / outcome
(Filled by the agent or the lead after completion.)
