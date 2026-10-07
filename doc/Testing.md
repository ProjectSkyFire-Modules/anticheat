# Initial module validation

The module is report-only. Do not interpret reports as grounds for sanctions.

Check config serial handling at startup and reload: matching serial has no
warning; absent, invalid, negative and older serials warn about an outdated file;
a newer serial warns about a binary/config mismatch. Warnings must not alter
settings or the core's `ConfVersion`. Status must display both serials. The
standalone tests cover the serial comparison boundaries.

1. Build and INSTALL the core with the module enabled. Confirm the startup log
   says the module is enabled and `.anticheat status` succeeds as administrator.
2. Use a normal player account for movement tests. To monitor a staff account,
   set `Anticheat.CheckStaff=1` and reload the world configuration.
3. Walk, run, strafe, jump, swim, mount and dismount. Test stairs and falling.
   Inspect `.anticheat player` and the `anticheat` log for unexpected reports.
4. Test legitimate flight, taxis, boats, elevators, vehicles, knockbacks and
   spell-driven jumps. Check that measurements resume after the grace period.
5. Test near/far teleports, portals, summon, death/resurrection and relogging.
   Teleport destinations must not become speed reports. Relog clears counters.
6. Test speed buffs and their expiration, as well as delayed/batched packets.
   High-latency and long-stall samples should reset rather than accuse a player.
7. Run the standalone tests to verify excessive displacement is detected without
   needing a modified client. If performing a live movement violation test in an
   isolated environment, verify reports repeat no faster than the configured
   cooldown and no kick, ban or correction occurs.
8. Confirm ordinary accounts cannot use operator commands. Console status/history
   must work without a player session. Player/clear must be unavailable there.
9. Reload with monitoring disabled; confirm reports stop. Re-enable and verify
   the baseline is fresh. Remove anticheat.conf and reload: monitoring must disable.
10. Configure with `MOD_ANTICHEAT=OFF`, then with the module folder absent. Build
    each configuration and verify worldserver still starts normally.

Record the core revision, module revision, configuration, latency and movement
type alongside any report. Reports omit chat contents, credentials and IPs.

## Persistent history

- Import the pending module character SQL twice into a disposable database and
  confirm existing rows are preserved. Enable `Anticheat.DatabaseReports` and
  check the status output.
- Generate a report and allow the asynchronous SQL worker to finish. Query
  `.anticheat history <low GUID>` and verify timestamp, detector, map, position
  and latency. Relog and restart worldserver; history must remain.
- Verify `.anticheat clear` resets session counters without deleting history.
- Confirm invalid GUIDs (negative, nonnumeric, zero, above 4294967295) do not
  query history. An unknown valid GUID should return no rows.
- On a disposable database, test a missing schema: persistence must disable with
  an error, while log-only monitoring works. Restore the schema and reload.
- With persistence disabled, confirm no INSERTs occur. A temporary database
  outage must not block movement; failures should appear in the SQL log.

## Client-clock reports and session overview

- Merge the new template keys and current serial, set
  `Anticheat.ClientClock=1`, then reload. Status must show it enabled. Allow login
  grace and at least one full clock window before expecting any measurement.
- Play normally with stable and variable latency. No clock reports should occur.
  Repeat teleport, taxi and relog tests; each must reset the clock baseline.
- Standalone tests cover accelerated timestamps, 32-bit wrap and batched timing.
  Do not enable enforcement based on the new detector; none is implemented.
- Run `.anticheat top` in game and `anticheat top` in console. Verify descending
  totals, deterministic GUID order for ties, and at most ten current-session
  entries. Clearing a session or logging out must remove its reports from this
  view without removing database history. Ordinary accounts must be denied.
- Verify persisted clock reports display as `client-clock` in history. Their
  detector ID is 3; old IDs and rows must remain unchanged.

## Administrator alerts and evidence

- Merge the current config template. As administrator, run `.anticheat alerts on`.
  A second administrator who has not subscribed must receive no chat alerts.
- Generate report-only detections. Verify chat messages identify the character
  GUID, detector, count and map; speed and clock details should appear in the
  anticheat log. Database history should work with both basic and evidence schemas.
- Verify `.anticheat alerts off`, logout, and global `Anticheat.Alerts=0` stop
  delivery. After relog, an administrator must opt in again. Console and ordinary
  accounts must not be able to subscribe. Remove administrator security or the
  server-info RBAC permission and verify delivery stops.
- Under a report burst, chat must remain limited to five messages per second.
  The standalone tests check bounded storage, oldest-entry eviction and expiry.
  Alerts may be dropped under load; investigate using logs and database history.
- Reload while reports are queued: old queued alerts must be discarded. Confirm
  movement continues and no kick, ban, correction or synchronous SQL is introduced
  into movement handling by the alert feature.

## Traversal detectors and persistent evidence (serial 2026100604)

Enable one of `Anticheat.Teleport`, `Anticheat.Jump`, or `Anticheat.Climb` at a
time. They are report-only heuristics and all default off. Keep staff checking
enabled if using a GM test account, but use normal movement and turn off GM flight
when testing grounded jump/climb behavior.

- Test walking/running, ordinary jumping, jumping onto stairs, steep hills,
  swimming, boats/elevators and vehicles. Test spell jumps and knockbacks,
  flight transitions, death/resurrection, and near/far teleports. Investigate
  any reports during authorized movement before adjusting thresholds.
- Test multiple consecutive normal jumps: a verified landing must prevent a
  repeated-airborne-jump report. Missing or unavailable terrain must not produce
  climb/jump reports. Query rate stays bounded even during packet bursts.
- Test `.anticheat player`, `.anticheat top`, alerts and history with new detector
  IDs. Confirm all seven detector counts are displayed and `.anticheat clear`
  resets all their baselines/counters while preserving history.
- Run standalone traversal tests for synthetic violations. A live traversal
  report should contain measurements and configured limits in the log.
- Apply the evidence migration to a disposable old schema, reapply it, and verify
  preexisting reports survive. Test fresh installation too. Run the module against
  a basic schema to verify the fallback still writes reports without evidence.
- With the evidence column present and config reloaded, confirm saved measurements
  appear in history after relog and restart. Older rows have no evidence to show.

The climb heuristic targets rapid steep ascent, not every climb exploit. Jump
reports can miss events between terrain samples, and lack of a report is not proof
that movement was valid. Teleport detection covers horizontal displacement only.

Before adding enforcement, the module needs broader client testing, an explicit
correction strategy, a retention policy, tighter authoritative movement
permissions, and review of transport/vehicle and forced movement edge cases.
