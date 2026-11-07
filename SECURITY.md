# Security policy

r3i6nos is a one-maintainer hobby project and has not yet booted on a real R36S, so there are no releases to support:
only the latest commit on `main` gets fixes.

## Reporting a problem

Please report a vulnerability **privately**: open the repository's **Security** tab and choose **Report a vulnerability**
(GitHub private vulnerability reporting). Do not open a public issue or pull request for it. Say what you found, how to
reproduce it, and what it lets someone do. Expect a reply in about a week; this is best effort, with no deadline promised.
If there is no such button, open a public issue that says only "I have a security report" and no details, and I will arrange a private channel.

## What counts

Things worth reporting:

- A host script that can write to or erase the wrong disk (`scripts/flash-macos.sh`, `add_games_partition.py`, the `inject-*` scripts), or that skips its safety checks.
- A boot-time script (`deb_watcher.py`, `loveos-session`, `run_app.sh`, `r3i6nos-poweroff`) that can be made to run something it should not,
  write outside its folders, or touch the games card (`EASYROMS`) beyond reading it, for example through a crafted file name or `.deb`.
- A secret, credential or personal data committed to the repository or its history.
- A build or CI weakness: an unpinned download, a missing checksum, a workflow with more access than it needs.
- Anything that makes the device talk to a network at boot (the project promises no telemetry, no network at boot, no accounts).

## What does not

- **Games and `.deb` files you put on the card are trusted code.** LÖVE games and native apps run on the device without a sandbox, by design. Only install what you trust.
- Weaknesses in LÖVE, SDL, Debian, the Linux 4.4 vendor kernel or the Mali driver themselves: report those upstream. The stock kernel is old and unpatched; this project reuses it for now (see `docs/hardware-findings.md`).
- Physical access to the device or its card.

## Handling

A confirmed report is fixed on `main`, recorded in `CHANGELOG.md` without exploit details until the fix is out, and the reporter is credited unless they prefer not to be.
