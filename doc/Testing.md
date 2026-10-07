# Initial module validation

The module is report-only. Do not interpret reports as grounds for sanctions.

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
8. Confirm ordinary accounts cannot use operator commands. Console status must
   work without a player session. Player/clear commands must be unavailable there.
9. Reload with monitoring disabled; confirm reports stop. Re-enable and verify
   the baseline is fresh. Remove anticheat.conf and reload: monitoring must disable.
10. Configure with `MOD_ANTICHEAT=OFF`, then with the module folder absent. Build
    each configuration and verify worldserver still starts normally.

Record the core revision, module revision, configuration, latency and movement
type alongside any report. Reports omit chat contents, credentials and IPs.

Before adding enforcement, the module needs broader client testing, an explicit
correction strategy, persistent report policy, tighter authoritative movement
permissions, and review of transport/vehicle and forced movement edge cases.
