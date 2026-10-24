# 02 — nginx vhost games.inceptum.at with placeholder page and TLS

**Milestone:** M0 Infrastructure
**Agent:** dev
**Status:** done
**Depends on:** —

## Goal
`https://games.inceptum.at` serves a static placeholder landing page for Velox with a valid Let's Encrypt certificate and auto-renewal; HTTP redirects to HTTPS. The config lives in the repo and is symlinked/copied into nginx.

## Scope — files/modules the agent may touch
- `infra/nginx/games.inceptum.at.conf` (new) — the server block(s)
- `infra/nginx/README.md` (new) — how it is installed and renewed
- `infra/www/index.html`, `infra/www/style.css` (new) — placeholder page, no JS frameworks
- System: `/etc/nginx/sites-available/games.inceptum.at` + symlink in `sites-enabled` (or `conf.d/` if that is how this nginx is organised — inspect first), `/var/www/games.inceptum.at/` (webroot, rsync from `infra/www/`), certbot. **Do not modify any existing nginx site or the main `nginx.conf`.** Do not touch the firewall (80/443 are already open and in use).

## Relevant ADRs / references
- `CLAUDE.md` section 2

## Acceptance criteria
- [ ] Before anything else: `dig +short games.inceptum.at` resolves (via CNAME) to this machine's public IP (`curl -s ifconfig.me`). If not, write the finding into the report and stop without requesting a certificate.
- [ ] Inspect how nginx is organised here (`ls /etc/nginx/sites-enabled /etc/nginx/conf.d`, `nginx -T | grep server_name`) and follow the existing convention. Check whether certbot (and the nginx plugin) is installed; install `certbot python3-certbot-nginx` via apt if missing.
- [ ] Server block: `server_name games.inceptum.at`, root `/var/www/games.inceptum.at`, `index index.html`, gzip on, sensible security headers (`X-Content-Type-Options`, `Referrer-Policy`, `X-Frame-Options`), a `/downloads/` location with `autoindex on` (empty for now), a `/status/` location serving JSON files with `Cache-Control: no-cache` (empty dir for now).
- [ ] Certificate via `sudo certbot --nginx -d games.inceptum.at --non-interactive --agree-tos -m bojan@referee.at --redirect`; renewal timer active (`systemctl list-timers | grep certbot`).
- [ ] Placeholder page: dark background, title "Velox", one-line tagline ("A fast arena FPS. Coming soon."), a short milestone list (M0–M7, from `ROADMAP.md` headings), links placeholder for downloads/status. Plain HTML + one CSS file. Must look fine on a phone.
- [ ] `curl -sI http://games.inceptum.at` → 301 to https; `curl -sI https://games.inceptum.at` → 200; `curl -s https://games.inceptum.at | grep -c Velox` ≥ 1.
- [ ] `infra/nginx/README.md` documents install (`sudo cp … && sudo ln -s … && sudo nginx -t && sudo systemctl reload nginx`), webroot sync, and renewal.

## Verification steps
1. `sudo nginx -t` ok, `sudo systemctl reload nginx` ok, existing sites still respond (pick one `server_name` from `nginx -T` and curl it with `-k -H Host:`).
2. The three curl checks above.
3. `sudo certbot certificates` lists games.inceptum.at.

## Report back
Standard format. Include the certbot output tail and the curl results.
Done 2026-10-24 by dev. Cert (ECDSA) valid until 2027-01-22, certbot.timer active. Repo conf is the post-certbot version as installed.
