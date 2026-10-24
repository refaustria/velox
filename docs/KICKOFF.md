# Kickoff prompt

Paste this into Claude Code the first time you start it in this repository.

---

You are the project lead for this repository. Read `CLAUDE.md` completely before doing anything else; it defines your role, the model routing for subagents, the environment and the roadmap.

Then do the following, in order:

1. Create `ROADMAP.md` from section 5 of `CLAUDE.md`, expanding each milestone into concrete acceptance criteria. Create `STATUS.md` with the sections: Done, In progress, Blocked, Next, Open questions for the human. Create `docs/tasks/TEMPLATE.md` (title, goal, scope: files/modules allowed, acceptance criteria, verification steps, report back) and empty `docs/adr/` and `infra/` directories.
2. Dispatch `architect` to write ADR 0001: project and module structure for an Unreal Engine 5.8 arena FPS (Source layout, GAS usage, networking model, test strategy, what to adopt from Lyra and what not). Wait for it, read the summary, and write the M0 task files from its recommendations.
3. Dispatch `dev` agents, in parallel where independent, for M0: check the machine (OS version, disk, GPU/driver, free ports), install the Linux toolchain Unreal needs (clang, cmake, build-essential, git-lfs, mono if required), set up Git LFS attributes, write the CI build script, and set up the nginx vhost for `games.inceptum.at` with a placeholder page and a Let's Encrypt certificate (verify DNS with `dig` first; if DNS does not resolve to this machine, record it under Open questions and skip the certificate).
4. Installing Unreal Engine 5.8 itself needs my Epic account. Do not try to work around that. Put the exact download/install steps you need me to run, or the credentials you need, under Open questions in `STATUS.md` and stop at a clean point.

Rules that apply from now on: never write game code yourself, always delegate to `dev`, `dev-hard` or `architect` according to `CLAUDE.md`; keep the build green; update `STATUS.md` before you stop; ask me rather than guess whenever a decision is not covered by `CLAUDE.md` or an ADR.

Work in English. Commit as you go on a branch `m0-infrastructure` and tell me when M0 is ready for review.
