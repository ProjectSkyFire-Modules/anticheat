# SkyFire Anticheat module

Optional, report-only movement monitoring for Project SkyFire 5.4.8. Requires
SkyFire_548 main commit `79bd1b3fdf` or later with the movement module hooks.
This is the initial standalone implementation, not a complete port of PR #1191.

## Install

From your core source directory:

```sh
git clone https://github.com/ProjectSkyFire-Modules/anticheat.git modules/mod-anticheat
```

Reconfigure CMake with `MODULES=ON` and `MOD_ANTICHEAT=ON`, build worldserver,
then run INSTALL. Use the exact folder name `mod-anticheat` for loader discovery.
Copy the installed `anticheat.conf.dist` to `anticheat.conf` beside the actual
`worldserver.conf` supplied to worldserver. Restart worldserver or use the core's
configuration reload command. Missing config disables monitoring.

The module needs no SQL updates. Removing it and reconfiguring the core removes
the feature; no gameplay tables or core configuration entries are altered.

## Current behavior

- Horizontal speed monitoring uses a bounded distance budget, server monotonic
  time and server speed values. Client timestamps and jump speed are not trusted.
- A grace period resets measurements around login, map changes, teleports,
  server speed changes, splines and knockback impulses.
- Transports, vehicles, taxis, active splines and high-latency sessions are
  excluded. Staff accounts are excluded unless `Anticheat.CheckStaff=1`.
- Optional fly and water-walking flags are compared with aura authorization.
  These experimental checks default off: script-granted abilities without a
  corresponding aura may trigger reports.
- Reports are rate-limited independently per player and detector. Counts are
  per login session and saturate rather than overflowing. Logout clears them;
  configuration reload resets movement baselines while retaining counts.
- Log messages use category `anticheat` at WARN level. Configure a dedicated
  core log appender if desired; the core's logger configuration controls output.

No packet rejection, teleport correction, jail, kick or ban is performed. A report
is evidence to investigate, not proof of cheating. Speed checks use a conservative
ceiling and do not detect every movement exploit: brief bursts within slack,
vertical displacement, client clock manipulation, collision bypass, manipulated
transport membership, controlled creatures, battleground boundaries, and movement
during exemption windows need further work. Server flight capability currently
raises the speed ceiling; it is not a separate authorization validator.

## Commands

Commands require administrator security and the existing `server info` RBAC
permission. Console can run `anticheat status`; player commands require a session.

| Command | Result |
| --- | --- |
| `.anticheat status` | Enabled state, enabled detectors and tracked player count |
| `.anticheat player` | Reports for the selected online player, or yourself |
| `.anticheat clear` | Clear that player's session reports and reset its baseline |

## Tests

Independent detector tests (no database or core build required):

```sh
cmake -S tests -B tests/build
cmake --build tests/build --config Release
ctest --test-dir tests/build -C Release --output-on-failure
```

Tests cover normal running, sustained excessive speed, batching, zero elapsed
time, resets, stalls, clock regression, invalid samples and bounded idle credit.
They do not validate the worldserver adapter or packet behavior; build the module
with the core and follow [the live test checklist](doc/Testing.md).

## Attribution and license

Inspired by the feature proposal in [SkyFire PR #1191](https://github.com/ProjectSkyfire/SkyFire_548/pull/1191)
by acidmanifesto (M'Dic). The standalone implementation replaces its embedded
manager, calculations and punitive actions rather than copying them into the core.
Licensed under GPL v3; see [LICENSE.md](LICENSE.md).
