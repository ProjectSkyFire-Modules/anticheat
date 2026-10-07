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

Before adding enforcement, the module needs broader client testing, an explicit
correction strategy, a retention policy, tighter authoritative movement
permissions, and review of transport/vehicle and forced movement edge cases.
