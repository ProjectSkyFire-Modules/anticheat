<p align="center">
  <img src="assets/skyfire_transp.png" alt="Project SkyFire logo" width="520">
</p>

# SkyFire Anticheat module

[Project SkyFire](https://www.projectskyfire.org/) · [GPL 3.0](LICENSE.md) · [Copyright and attribution](COPYRIGHT.md)

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

### Module configuration serial

`Anticheat.ConfVersion` is checked by the module at startup and on configuration
reload. It is independent of worldserver's `ConfVersion`; the module never sets
or changes the core serial. `.anticheat status` displays installed and expected
module serials.

A missing, invalid or older serial produces an update warning. A newer serial
warns that the module binary and config may not match. Like the core's version
check, these warnings do not stop startup, disable monitoring or rewrite files.
Merge changes from `anticheat.conf.dist`, preserve your settings, then update the
serial and reload. Merely changing the serial does not update a configuration.

Module maintainers must bump the `YYYYMMDDNN` serial in `src/ModuleConfig.h` and
`conf/anticheat.conf.dist` together when changing the configuration template.

The default log-only mode needs no SQL updates. Optional database history requires
the module schema described below. Removing the module and reconfiguring the core
removes the feature; retained reports remain available in the character database.

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
- Optional client-clock acceleration monitoring compares elapsed client time
  against a server monotonic-time window. It tolerates 32-bit timestamp wrap and
  batching, and resets on long gaps, backward timestamps and movement exemptions.
  Enable `Anticheat.ClientClock=1` to test it. It defaults off pending live testing;
  it never increases the allowed movement budget or rejects a packet.
- Reports are rate-limited independently per player and detector. Counts are
  per login session and saturate rather than overflowing. Logout clears them;
  configuration reload resets movement baselines while retaining counts.
- Log messages use category `anticheat` at WARN level. Configure a dedicated
  core log appender if desired; the core's logger configuration controls output.
- Speed logs include distance, remaining distance allowance, elapsed server time
  and latency. Clock logs include client/server elapsed time and latency. These
  details accompany the summary log; the existing database row format is unchanged.
- Administrators can opt into live chat alerts for their current login session.
  Delivery runs on the world thread and rechecks administrator security and RBAC.
  A shared queue holds at most 100 alerts, drops the oldest on overflow, expires
  entries after ten seconds and delivers at most five per second per subscriber.
  These limits affect chat only, not the existing report log/database path.

No packet rejection, teleport correction, jail, kick or ban is performed. A report
is evidence to investigate, not proof of cheating. Speed checks use a conservative
ceiling and do not detect every movement exploit: brief bursts within slack,
vertical displacement, backward/reset clock manipulation, collision bypass, manipulated
transport membership, controlled creatures, battleground boundaries, and movement
during exemption windows need further work. Server flight capability currently
raises the speed ceiling; it is not a separate authorization validator.

## Commands

Commands require administrator security and the existing `server info` RBAC
permission. Console can run status, history and top; player/clear require a session.

| Command | Result |
| --- | --- |
| `.anticheat status` | Enabled state, enabled detectors and tracked player count |
| `.anticheat player` | Reports for the selected online player, or yourself |
| `.anticheat clear` | Clear that player's session reports and reset its baseline; preserve database history |
| `.anticheat history <GUID>` | Latest 10 persisted reports for a character's numeric low GUID, including offline characters |
| `.anticheat top` | Top ten current-session report totals, with per-detector counts; available in console |
| `.anticheat alerts on\|off` | Subscribe/unsubscribe to administrator chat alerts for this login session |

`Anticheat.Alerts=1` allows subscriptions but does not automatically subscribe
anyone. Logout removes the subscription. Set it to 0 to stop alert delivery
globally. Configuration reload clears queued alerts; subscriptions remain for
the current session and resume if monitoring and alerts are re-enabled.

## Optional persistent history

1. Import `sql/pending_updates/characters/create_anticheat_reports.sql` into the
   character database. The file is idempotent and does not modify gameplay tables.
   Module SQL is not automatically imported by the core's updater.
2. Set `Anticheat.DatabaseReports=1` in `anticheat.conf` and reload configuration.
3. Confirm `.anticheat status` shows `Database=1`. Missing columns or an unavailable
   schema disable persistence and produce a startup/reload error; log-only
   monitoring remains available.

The module queues one INSERT per cooldown-qualified report using the core's
asynchronous database worker. The movement callback does not wait for SQL. Each
row records the event's UTC Unix timestamp, character/account IDs, map, detector,
opcode, position (integer thousandths of a yard) and latency. It contains no IP,
chat text or credentials. Detector IDs are stable: 0=speed, 1=fly, 2=waterwalk,
3=client-clock. The clock detector uses the existing schema; no new SQL is needed.

History survives relog and worldserver restart, but pending asynchronous writes
can be lost in a crash or database outage. Check the core SQL log for write
failures. `Database=1` means the schema passed the last config-time check; it is
not a live database health probe. History queries are explicit administrator
operations and return at most ten rows, using the character/history index.

No automatic retention or deletion is enabled. Operators should set a retention
policy appropriate to their server and maintain the module table separately.
Disable persistence before removing its table. Reimporting the initial SQL never
deletes rows, and `.anticheat clear` does not delete stored evidence.

## Tests

Independent detector tests (no database or core build required):

```sh
cmake -S tests -B tests/build
cmake --build tests/build --config Release
ctest --test-dir tests/build -C Release --output-on-failure
```

Tests cover normal running, sustained excessive speed, batching, zero elapsed
time, resets, stalls, clock regression, invalid samples and bounded idle credit.
Additional cases cover client-clock acceleration, timestamp wraparound, batching,
report argument boundaries and configuration serial compatibility.
Alert tests cover capacity, burst limits, expiration, clearing and clock regression;
evidence tests verify values from the actual detector sample.
They do not validate the worldserver adapter or packet behavior; build the module
with the core and follow [the live test checklist](doc/Testing.md).

## Attribution and license

Inspired by the feature proposal in [SkyFire PR #1191](https://github.com/ProjectSkyfire/SkyFire_548/pull/1191)
by acidmanifesto (M'Dic). The standalone implementation replaces its embedded
manager, calculations and punitive actions rather than copying them into the core.
Licensed under GPL v3; see [LICENSE.md](LICENSE.md) and [COPYRIGHT.md](COPYRIGHT.md).
