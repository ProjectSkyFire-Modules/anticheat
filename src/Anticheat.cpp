/*
* This file is part of Project SkyFire https://www.projectskyfire.org.
* See LICENSE.md file for Copyright information
*/
#include "MovementMonitor.h"
#include "AlertQueue.h"
#include "TraversalMonitor.h"
#include "ClientClockMonitor.h"
#include "ModuleConfig.h"
#include "ReportValues.h"
#include "Chat.h"
#include "Config.h"
#include "DatabaseEnv.h"
#include "Log.h"
#include "Map.h"
#include "MoveSpline.h"
#include "Opcodes.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "SpellAuraDefines.h"
#include "WorldSession.h"
#include "World.h"
#include <array>
#include <chrono>
#include <ctime>
#include <limits>
#include <mutex>
#include <sstream>
#include <iomanip>
#include <locale>
#include <cstring>
#include <unordered_map>

namespace
{
using Clock = std::chrono::steady_clock;
using Milliseconds = std::chrono::milliseconds;
uint64 Now() { return uint64(std::chrono::duration_cast<Milliseconds>(Clock::now().time_since_epoch()).count()); }

enum Detection { Speed = 0, Fly = 1, WaterWalk = 2, ClientClock = 3, Teleport = 4, Jump = 5, Climb = 6, DetectionCount = 7 };
char const* const Names[] = { "speed", "fly", "waterwalk", "client-clock", "teleport", "jump", "climb" };

bool IsPositionOpcode(uint16 opcode)
{
    switch (opcode)
    {
        case CMSG_MOVE_CHNG_TRANSPORT:
        case MSG_MOVE_FALL_LAND:
        case MSG_MOVE_HEARTBEAT:
        case MSG_MOVE_JUMP:
        case MSG_MOVE_SET_FACING:
        case MSG_MOVE_SET_PITCH:
        case MSG_MOVE_SET_RUN_MODE:
        case MSG_MOVE_SET_WALK_MODE:
        case MSG_MOVE_START_ASCEND:
        case MSG_MOVE_START_BACKWARD:
        case MSG_MOVE_START_DESCEND:
        case MSG_MOVE_START_FORWARD:
        case MSG_MOVE_START_PITCH_DOWN:
        case MSG_MOVE_START_PITCH_UP:
        case MSG_MOVE_START_STRAFE_LEFT:
        case MSG_MOVE_START_STRAFE_RIGHT:
        case MSG_MOVE_START_SWIM:
        case MSG_MOVE_START_TURN_LEFT:
        case MSG_MOVE_START_TURN_RIGHT:
        case MSG_MOVE_STOP:
        case MSG_MOVE_STOP_ASCEND:
        case MSG_MOVE_STOP_PITCH:
        case MSG_MOVE_STOP_STRAFE:
        case MSG_MOVE_STOP_SWIM:
        case MSG_MOVE_STOP_TURN:
            return true;
        default:
            return false;
    }
}

struct Settings
{
    uint32 configVersion = 0;
    bool enabled = false;
    bool staff = false;
    bool speed = true;
    bool fly = false;
    bool waterWalk = false;
    bool database = false;
    bool databaseEvidence = false;
    bool teleport = false, jump = false, climb = false;
    uint32 terrainInterval = 250;
    SkyFireAnticheat::TraversalLimits traversal;
    bool clientClock = false;
    bool alerts = true;
    double clockRatio = 1.5;
    uint32 clockWindow = 10000;
    uint32 clockSlack = 2000;
    double multiplier = 1.3;
    double slack = 10;
    uint32 grace = 5000;
    uint32 gap = 5000;
    uint32 latency = 1000;
    uint32 cooldown = 10000;
};

struct State
{
    SkyFireAnticheat::MovementMonitor movement;
    SkyFireAnticheat::ClientClockMonitor clock;
    SkyFireAnticheat::TraversalMonitor traversal;
    uint64 nextTerrainSample = 0;
    uint64 graceUntil = 0;
    std::array<uint32, DetectionCount> reports{};
    std::array<uint64, DetectionCount> lastReport{};
};

// Callbacks can run on different game threads; never retain player pointers.
std::mutex Mutex;
Settings Options;
std::unordered_map<uint64, State> Players;
std::unordered_map<uint32, uint64> AlertSubscribers;
SkyFireAnticheat::AlertQueue Alerts;

uint32 ReadDuration(char const* key, int fallback, int minimum, int maximum)
{
    return uint32(std::max(minimum, std::min(maximum, sConfigMgr->GetIntDefault(key, fallback))));
}

double ReadNumber(char const* key, float fallback, double minimum, double maximum)
{
    double value = sConfigMgr->GetFloatDefault(key, fallback);
    return std::isfinite(value) ? std::max(minimum, std::min(maximum, value)) : fallback;
}

class AnticheatWorld : public WorldScript
{
public:
    AnticheatWorld() : WorldScript("module_anticheat_world") { }
    void OnUpdate(uint32 diff) override
    {
        // Session access and chat delivery stay off movement/map threads.
        if (diff < alertTimer)
        {
            alertTimer -= diff;
            return;
        }
        alertTimer = 1000;
        std::vector<std::string> batch;
        std::unordered_map<uint32, uint64> subscribers;
        {
            std::lock_guard<std::mutex> lock(Mutex);
            if (!Options.enabled || !Options.alerts || AlertSubscribers.empty())
            {
                Alerts.Clear();
                return;
            }
            batch = Alerts.Take(Now());
            subscribers = AlertSubscribers;
        }
        if (batch.empty())
            return;
        for (auto const& subscriber : subscribers)
        {
            WorldSession* session = sWorld->FindSession(subscriber.first);
            if (!session || !session->GetPlayer() || !session->GetPlayer()->IsInWorld() ||
                session->GetPlayer()->GetGUID() != subscriber.second ||
                session->GetSecurity() < AccountTypes::SEC_ADMINISTRATOR ||
                !session->HasPermission(rbac::RBAC_PERM_COMMAND_SERVER_INFO))
                continue;
            ChatHandler handler(session);
            for (std::string const& message : batch)
                handler.SendSysMessage(message.c_str());
        }
    }
    void OnConfigLoad(bool /*reload*/) override
    {
        // Resolve against the main config, not the process working directory.
        std::string path = sConfigMgr->GetFilename();
        std::string::size_type slash = path.find_last_of("/\\");
        path = (slash == std::string::npos ? "" : path.substr(0, slash + 1)) + "anticheat.conf";
        bool loaded = sConfigMgr->LoadMore(path.c_str());
        Settings next;
        if (loaded)
            SkyFireAnticheat::ParsePositiveUint32(
                sConfigMgr->GetStringDefault("Anticheat.ConfVersion", "0").c_str(), next.configVersion);
        if (loaded)
        {
            auto compatibility = SkyFireAnticheat::CheckConfigVersion(next.configVersion);
            if (compatibility == SkyFireAnticheat::ConfigCompatibility::Outdated)
                SF_LOG_WARN("server.loading", "Anticheat config is outdated or missing its serial (installed %u, expected %u). "
                    "Merge anticheat.conf.dist into %s, preserve your settings, then reload. Do not only change the serial.",
                    next.configVersion, SkyFireAnticheat::ConfigVersion, path.c_str());
            else if (compatibility == SkyFireAnticheat::ConfigCompatibility::Newer)
                SF_LOG_WARN("server.loading", "Anticheat config serial %u is newer than this module (%u). "
                    "Update the module or use its matching configuration template: %s.",
                    next.configVersion, SkyFireAnticheat::ConfigVersion, path.c_str());
        }
        next.enabled = loaded && sConfigMgr->GetBoolDefault("Anticheat.Enable", true);
        next.staff = sConfigMgr->GetBoolDefault("Anticheat.CheckStaff", false);
        next.speed = sConfigMgr->GetBoolDefault("Anticheat.Speed", true);
        next.fly = sConfigMgr->GetBoolDefault("Anticheat.Fly", false);
        next.waterWalk = sConfigMgr->GetBoolDefault("Anticheat.WaterWalk", false);
        next.multiplier = ReadNumber("Anticheat.SpeedMultiplier", 1.3f, 1.0, 5.0);
        next.slack = ReadNumber("Anticheat.DistanceSlack", 10.0f, 1.0, 100.0);
        next.grace = ReadDuration("Anticheat.GraceMs", 5000, 1000, 60000);
        next.gap = ReadDuration("Anticheat.MaximumGapMs", 5000, 1000, 60000);
        next.latency = ReadDuration("Anticheat.MaximumLatencyMs", 1000, 100, 10000);
        next.cooldown = ReadDuration("Anticheat.ReportCooldownMs", 10000, 1000, 600000);
        next.clientClock = sConfigMgr->GetBoolDefault("Anticheat.ClientClock", false);
        next.alerts = sConfigMgr->GetBoolDefault("Anticheat.Alerts", true);
        next.clockRatio = ReadNumber("Anticheat.ClockRatio", 1.5f, 1.1, 5.0);
        next.clockWindow = ReadDuration("Anticheat.ClockWindowMs", 10000, 5000, 60000);
        next.clockSlack = ReadDuration("Anticheat.ClockSlackMs", 2000, 500, 10000);
        next.teleport = sConfigMgr->GetBoolDefault("Anticheat.Teleport", false);
        next.jump = sConfigMgr->GetBoolDefault("Anticheat.Jump", false);
        next.climb = sConfigMgr->GetBoolDefault("Anticheat.Climb", false);
        next.terrainInterval = ReadDuration("Anticheat.TerrainIntervalMs", 250, 100, 2000);
        next.traversal.maximumGap = next.gap;
        next.traversal.speedMultiplier = next.multiplier;
        next.traversal.distanceSlack = next.slack;
        next.traversal.teleportDistance = ReadNumber("Anticheat.TeleportDistance", 50, 20, 1000);
        next.traversal.jumpRise = ReadNumber("Anticheat.JumpRise", 2, 1, 20);
        next.traversal.climbRise = ReadNumber("Anticheat.ClimbRise", 5, 2, 30);
        next.traversal.climbSlope = ReadNumber("Anticheat.ClimbSlope", 3, 1.5, 10);
        if (loaded && sConfigMgr->GetBoolDefault("Anticheat.DatabaseReports", false))
        {
            QueryResult schema = CharacterDatabase.Query(
                "SELECT COUNT(*) FROM information_schema.columns WHERE table_schema=DATABASE() "
                "AND table_name='mod_anticheat_reports' AND column_name IN "
                "('id','event_time','guid','account','map','detector','opcode','x_milli','y_milli','z_milli','latency')");
            next.database = schema && schema->Fetch()[0].GetUInt64() == 11;
            if (!next.database)
                SF_LOG_ERROR("anticheat", "Database history disabled: import the module character SQL and reload configuration.");
            else
            {
                QueryResult evidence = CharacterDatabase.Query(
                    "SELECT COUNT(*) FROM information_schema.columns WHERE table_schema=DATABASE() "
                    "AND table_name='mod_anticheat_reports' AND column_name='evidence' "
                    "AND data_type='varchar' AND character_maximum_length>=512");
                next.databaseEvidence = evidence && evidence->Fetch()[0].GetUInt64() == 1;
                if (!next.databaseEvidence)
                    SF_LOG_WARN("anticheat", "Detailed database evidence unavailable: apply extend_anticheat_report_evidence.sql. Basic history remains enabled.");
            }
        }
        {
            std::lock_guard<std::mutex> lock(Mutex);
            Options = next;
            Alerts.Clear();
            for (auto& entry : Players)
            {
                entry.second.movement.Reset();
                entry.second.clock.Reset();
                entry.second.traversal.Reset();
                entry.second.nextTerrainSample = 0;
                entry.second.graceUntil = Now() + next.grace;
            }
        }
        SF_LOG_INFO("server.loading", "Anticheat module: %s (report-only; config %s)",
            next.enabled ? "enabled" : "disabled", loaded ? "loaded" : "missing or invalid");
    }
private:
    uint32 alertTimer = 1000;
};

void Grace(Player* player, uint32 duration = 0)
{
    std::lock_guard<std::mutex> lock(Mutex);
    auto itr = Players.find(player->GetGUID());
    if (itr == Players.end())
        return;
    itr->second.movement.Reset();
    itr->second.clock.Reset();
    itr->second.traversal.Reset();
    itr->second.nextTerrainSample = 0;
    itr->second.graceUntil = std::max(itr->second.graceUntil, Now() + std::max(duration, Options.grace));
}

class AnticheatPlayer : public PlayerScript
{
public:
    AnticheatPlayer() : PlayerScript("module_anticheat_player") { }
    void OnLogin(Player* player, bool /*firstLogin*/) override
    {
        std::lock_guard<std::mutex> lock(Mutex);
        State& state = Players[player->GetGUID()];
        state = State();
        state.graceUntil = Now() + Options.grace;
        AlertSubscribers.erase(player->GetSession()->GetAccountId());
    }
    void OnLogout(Player* player) override
    {
        std::lock_guard<std::mutex> lock(Mutex);
        Players.erase(player->GetGUID());
        AlertSubscribers.erase(player->GetSession()->GetAccountId());
    }
    void OnMapChanged(Player* player) override { Grace(player); }
    void OnMovementChanged(Player* player, Unit* mover, PlayerMovementChange /*change*/) override
    {
        if (player == mover)
            Grace(player);
    }
    void OnKnockback(Player* player, Unit* mover, float /*speedXY*/, float speedZ) override
    {
        if (player == mover)
        {
            // Server impulse, never client jump data. Allow ascent/descent plus settling time.
            double flightMs = std::isfinite(speedZ) ? std::abs(double(speedZ)) * 2000.0 / 19.2911 : 0;
            Grace(player, uint32(std::min(60000.0, flightMs + 5000.0)));
        }
    }
    void OnMovementApplied(Player* player, Unit* mover, MovementInfo const& movement, uint16 opcode) override
    {
        // These acknowledgements update metadata, not position. Do not count them as steps.
        if (!IsPositionOpcode(opcode))
            return;

        uint64 now = Now();
        std::array<bool, DetectionCount> emitted{};
        std::array<uint32, DetectionCount> counts{};
        std::array<std::string, DetectionCount> evidence;
        bool database = false;
        bool databaseEvidence = false;
        double distance = 0, allowance = 0;
        uint64 elapsed = 0, clockServer = 0;
        uint32 clockClient = 0;
        bool terrainRequested = false, groundKnown = false, grounded = false;
        {
            std::lock_guard<std::mutex> lock(Mutex);
            auto itr = Players.find(player->GetGUID());
            if (Options.enabled && (Options.jump || Options.climb) && itr != Players.end() &&
                now >= itr->second.graceUntil && now >= itr->second.nextTerrainSample &&
                mover == player && player->IsInWorld() && !player->IsBeingTeleported() &&
                !player->GetTransport() && !player->GetVehicle() && !player->IsInFlight() &&
                player->movespline->Finalized() && !player->IsInWater() && !player->CanFly() &&
                (Options.staff || player->GetSession()->GetSecurity() == AccountTypes::SEC_PLAYER) &&
                player->GetSession()->GetLatency() <= Options.latency)
            {
                itr->second.nextTerrainSample = now + Options.terrainInterval;
                terrainRequested = true;
            }
        }
        // Terrain/collision queries can be costly; never perform them under the shared mutex.
        // At most one per configured interval per eligible player, regardless of packet rate.
        if (terrainRequested)
        {
            float z = player->GetPositionZ();
            float height = player->GetMap()->GetHeight(player->GetPhaseMask(), player->GetPositionX(),
                player->GetPositionY(), z + 2.0f, true, 50.0f);
            groundKnown = std::isfinite(height) && height > INVALID_HEIGHT && height <= z + 1.5f;
            grounded = groundKnown && std::abs(z - height) <= 1.5f;
        }
        {
            std::lock_guard<std::mutex> lock(Mutex);
            if (!Options.enabled)
                return;
            auto itr = Players.find(player->GetGUID());
            if (itr == Players.end())
                return;
            State& state = itr->second;
            database = Options.database;
            databaseEvidence = Options.databaseEvidence;
            if (mover != player || !player->IsInWorld() || player->IsBeingTeleported() ||
                player->GetTransport() || player->GetVehicle() || player->IsInFlight() ||
                !player->movespline->Finalized() ||
                (!Options.staff && player->GetSession()->GetSecurity() > AccountTypes::SEC_PLAYER) ||
                player->GetSession()->GetLatency() > Options.latency)
            {
                state.movement.Reset();
                state.clock.Reset();
                state.traversal.Reset();
                state.nextTerrainSample = 0;
                state.graceUntil = now + Options.grace;
                return;
            }
            if (now < state.graceUntil)
                return;

            bool flightAura = player->HasAuraType(SPELL_AURA_FLY) ||
                player->HasAuraType(SPELL_AURA_MOD_INCREASE_MOUNTED_FLIGHT_SPEED) ||
                player->HasAuraType(SPELL_AURA_MOD_INCREASE_FLIGHT_SPEED);
            // Conservative speed ceiling: no client-provided speed or timestamps.
            double speed = std::max(player->GetSpeed(MOVE_RUN), player->GetSpeed(MOVE_SWIM));
            if (flightAura || player->CanFly())
                speed = std::max(speed, double(player->GetSpeed(MOVE_FLIGHT)));
            SkyFireAnticheat::TraversalResult traversal;
            if (Options.teleport || Options.jump || Options.climb)
            {
                SkyFireAnticheat::TraversalSample sample;
                sample.time = now;
                sample.x = player->GetPositionX();
                sample.y = player->GetPositionY();
                sample.z = player->GetPositionZ();
                sample.speed = speed;
                sample.jump = opcode == MSG_MOVE_JUMP;
                sample.groundSampled = terrainRequested;
                sample.groundKnown = groundKnown;
                sample.grounded = grounded;
                sample.terrainExempt = flightAura || player->CanFly() || player->IsInWater();
                traversal = state.traversal.Observe(sample, Options.traversal);
            }
            bool suspicious[DetectionCount] = {
                Options.speed && state.movement.Observe(now, player->GetPositionX(), player->GetPositionY(),
                    speed, Options.slack, Options.multiplier, Options.gap),
                Options.fly && !flightAura && !player->IsGameMaster() &&
                    (movement.flags & (MOVEMENTFLAG_FLYING | MOVEMENTFLAG_DISABLE_GRAVITY)),
                Options.waterWalk && !player->HasAuraType(SPELL_AURA_WATER_WALK) &&
                    !player->HasAuraType(SPELL_AURA_GHOST) && !player->IsGameMaster() &&
                    (movement.flags & MOVEMENTFLAG_WATERWALKING),
                Options.clientClock && state.clock.Observe(now, movement.time, Options.gap,
                    Options.clockWindow, Options.clockRatio, Options.clockSlack),
                Options.teleport && traversal.teleport,
                Options.jump && traversal.jump,
                Options.climb && traversal.climb
            };
            distance = state.movement.Distance();
            allowance = state.movement.Allowance();
            elapsed = state.movement.Elapsed();
            clockServer = state.clock.ServerElapsed();
            clockClient = state.clock.ClientElapsed();
            for (unsigned i = 0; i < DetectionCount; ++i)
                if (suspicious[i] && (!state.lastReport[i] || now - state.lastReport[i] >= Options.cooldown))
                {
                    state.lastReport[i] = now;
                    if (state.reports[i] != std::numeric_limits<uint32>::max())
                        ++state.reports[i];
                    emitted[i] = true;
                    counts[i] = state.reports[i];
                    std::ostringstream details;
                    details.imbue(std::locale::classic());
                    details << std::fixed << std::setprecision(3);
                    switch (i)
                    {
                        case Speed:
                            details << "distance_yd=" << distance << " allowance_yd=" << allowance
                                << " elapsed_ms=" << elapsed << " speed_yd_s=" << speed;
                            break;
                        case ClientClock:
                            details << "client_ms=" << clockClient << " server_ms=" << clockServer
                                << " ratio_limit=" << Options.clockRatio << " slack_ms=" << Options.clockSlack;
                            break;
                        case Teleport:
                            details << "distance_yd=" << traversal.horizontal << " limit_yd=" << traversal.allowance
                                << " elapsed_ms=" << traversal.elapsed;
                            break;
                        case Jump:
                            details << "repeat_jump_rise_yd=" << traversal.rise << " limit_yd=" << Options.traversal.jumpRise;
                            break;
                        case Climb:
                            details << "rise_yd=" << traversal.rise << " slope=" << traversal.slope
                                << " rise_limit=" << Options.traversal.climbRise << " slope_limit=" << Options.traversal.climbSlope;
                            break;
                        default:
                            details << "movement_flags=" << movement.flags << " flight_aura=" << flightAura;
                            break;
                    }
                    evidence[i] = details.str();
                    if (Options.alerts && !AlertSubscribers.empty())
                    {
                        std::ostringstream message;
                        message << "[Anticheat report-only] GUID=" << player->GetGUIDLow()
                            << " " << Names[i] << " count=" << counts[i] << " map=" << player->GetMapId();
                        Alerts.Push(now, message.str());
                    }
                }
        }
        // Avoid holding module state while logging. No chat content, credentials or IPs.
        for (unsigned i = 0; i < DetectionCount; ++i)
            if (emitted[i])
            {
                SF_LOG_WARN("anticheat", "Report-only: guid=%u detector=%s count=%u map=%u opcode=%u",
                    player->GetGUIDLow(), Names[i], counts[i], player->GetMapId(), uint32(opcode));
                SF_LOG_WARN("anticheat", "Evidence: guid=%u detector=%s latency=%u ms %s",
                    player->GetGUIDLow(), Names[i], player->GetSession()->GetLatency(), evidence[i].c_str());
                if (database)
                {
                    if (databaseEvidence)
                    {
                        std::string hex = SkyFireAnticheat::HexEvidence(evidence[i]);
                        CharacterDatabase.PExecute(
                            "INSERT INTO mod_anticheat_reports "
                            "(event_time,guid,account,map,detector,opcode,x_milli,y_milli,z_milli,latency,evidence) "
                            "VALUES (" UI64FMTD ",%u,%u,%u,%u,%u,%d,%d,%d,%u,X'%s')",
                            uint64(std::time(nullptr)), player->GetGUIDLow(), player->GetSession()->GetAccountId(),
                            player->GetMapId(), i, uint32(opcode),
                            SkyFireAnticheat::CoordinateMilli(player->GetPositionX()),
                            SkyFireAnticheat::CoordinateMilli(player->GetPositionY()),
                            SkyFireAnticheat::CoordinateMilli(player->GetPositionZ()), player->GetSession()->GetLatency(), hex.c_str());
                    }
                    else
                        CharacterDatabase.PExecute(
                            "INSERT INTO mod_anticheat_reports "
                            "(event_time,guid,account,map,detector,opcode,x_milli,y_milli,z_milli,latency) "
                            "VALUES (" UI64FMTD ",%u,%u,%u,%u,%u,%d,%d,%d,%u)",
                            uint64(std::time(nullptr)), player->GetGUIDLow(), player->GetSession()->GetAccountId(),
                            player->GetMapId(), i, uint32(opcode),
                            SkyFireAnticheat::CoordinateMilli(player->GetPositionX()),
                            SkyFireAnticheat::CoordinateMilli(player->GetPositionY()),
                            SkyFireAnticheat::CoordinateMilli(player->GetPositionZ()), player->GetSession()->GetLatency());
                }
            }
    }
};

class AnticheatCommands : public CommandScript
{
public:
    AnticheatCommands() : CommandScript("module_anticheat_commands") { }
    std::vector<ChatCommand> GetCommands() const override
    {
        static std::vector<ChatCommand> commands = {
            { "status", rbac::RBAC_PERM_COMMAND_SERVER_INFO, true, &Status, "" },
            { "player", rbac::RBAC_PERM_COMMAND_SERVER_INFO, false, &Reports, "" },
            { "history", rbac::RBAC_PERM_COMMAND_SERVER_INFO, true, &History, "" },
            { "top", rbac::RBAC_PERM_COMMAND_SERVER_INFO, true, &Top, "" },
            { "alerts", rbac::RBAC_PERM_COMMAND_SERVER_INFO, false, &SetAlerts, "" },
            { "clear", rbac::RBAC_PERM_COMMAND_SERVER_INFO, false, &Clear, "" }
        };
        return { { "anticheat", rbac::RBAC_PERM_COMMAND_SERVER_INFO, true, NULL, "", commands } };
    }
    static bool Admin(ChatHandler* handler)
    {
        if (!handler->GetSession() || handler->GetSession()->GetSecurity() >= AccountTypes::SEC_ADMINISTRATOR)
            return true;
        handler->SendSysMessage("Anticheat commands require an administrator.");
        handler->SetSentErrorMessage(true);
        return false;
    }
    static bool Status(ChatHandler* handler, char const* /*args*/)
    {
        if (!Admin(handler))
            return false;
        Settings options;
        uint32 tracked;
        {
            std::lock_guard<std::mutex> lock(Mutex);
            options = Options;
            tracked = uint32(Players.size());
        }
        handler->PSendSysMessage("Anticheat: %s, report-only. Tracked players: %u. Speed=%u Fly=%u Waterwalk=%u. Database=%u.",
            options.enabled ? "enabled" : "disabled", tracked, uint32(options.speed),
            uint32(options.fly), uint32(options.waterWalk), uint32(options.database));
        handler->PSendSysMessage("Module config serial: installed=%u expected=%u.",
            options.configVersion, SkyFireAnticheat::ConfigVersion);
        handler->PSendSysMessage("Client clock detection=%u (report-only).", uint32(options.clientClock));
        handler->PSendSysMessage("Teleport=%u Jump=%u Climb=%u Detailed database evidence=%u.",
            uint32(options.teleport), uint32(options.jump), uint32(options.climb), uint32(options.databaseEvidence));
        handler->PSendSysMessage("Administrator alerts available=%u; use .anticheat alerts on to subscribe.", uint32(options.alerts));
        return true;
    }
    static bool SetAlerts(ChatHandler* handler, char const* args)
    {
        if (!Admin(handler) || !handler->GetSession() || !handler->GetSession()->GetPlayer())
            return false;
        if (!args || (std::strcmp(args, "on") && std::strcmp(args, "off")))
        {
            handler->SendSysMessage("Usage: .anticheat alerts on|off (this login session only)");
            return false;
        }
        bool enable = !std::strcmp(args, "on");
        bool available;
        {
            std::lock_guard<std::mutex> lock(Mutex);
            available = Options.enabled && Options.alerts;
            uint32 account = handler->GetSession()->GetAccountId();
            if (enable && available)
                AlertSubscribers[account] = handler->GetSession()->GetPlayer()->GetGUID();
            else
                AlertSubscribers.erase(account);
            if (AlertSubscribers.empty())
                Alerts.Clear();
        }
        handler->SendSysMessage(enable ? (available ? "Anticheat alerts enabled for this session." :
            "Alerts unavailable: monitoring or Anticheat.Alerts is disabled.") : "Anticheat alerts disabled.");
        return true;
    }
    static bool Top(ChatHandler* handler, char const* /*args*/)
    {
        if (!Admin(handler))
            return false;
        struct Row
        {
            uint64 guid;
            uint64 total;
            std::array<uint32, DetectionCount> reports;
        };
        std::vector<Row> rows;
        {
            std::lock_guard<std::mutex> lock(Mutex);
            for (auto const& entry : Players)
            {
                uint64 total = 0;
                for (uint32 count : entry.second.reports)
                    total += count;
                if (total)
                    rows.push_back({entry.first, total, entry.second.reports});
            }
        }
        std::sort(rows.begin(), rows.end(), [](Row const& a, Row const& b)
        {
            return a.total != b.total ? a.total > b.total : a.guid < b.guid;
        });
        handler->SendSysMessage("Current-session reports only; counts are not a cheating verdict.");
        if (rows.empty())
            handler->SendSysMessage("No current-session reports.");
        for (std::size_t i = 0; i < std::min(rows.size(), std::size_t(10)); ++i)
            handler->PSendSysMessage("GUID=%u total=" UI64FMTD " speed=%u fly=%u waterwalk=%u clock=%u teleport=%u jump=%u climb=%u",
                GUID_LOPART(rows[i].guid), rows[i].total, rows[i].reports[Speed], rows[i].reports[Fly],
                rows[i].reports[WaterWalk], rows[i].reports[ClientClock], rows[i].reports[Teleport], rows[i].reports[Jump], rows[i].reports[Climb]);
        return true;
    }
    static bool History(ChatHandler* handler, char const* args)
    {
        if (!Admin(handler))
            return false;
        // Strict numeric low GUID, no names or SQL fragments. Usable for offline players.
        uint32 guid = 0;
        if (!SkyFireAnticheat::ParsePositiveUint32(args, guid))
        {
            handler->SendSysMessage("Usage: anticheat history <character low GUID>");
            return false;
        }
        bool database, databaseEvidence;
        {
            std::lock_guard<std::mutex> lock(Mutex);
            database = Options.database;
            databaseEvidence = Options.databaseEvidence;
        }
        if (!database)
        {
            handler->SendSysMessage("Database history is disabled or its schema is unavailable.");
            return true;
        }
        QueryResult result = CharacterDatabase.PQuery(
            "SELECT event_time,detector,map,opcode,x_milli,y_milli,z_milli,latency,%s FROM mod_anticheat_reports "
            "WHERE guid=%u ORDER BY id DESC LIMIT 10", databaseEvidence ? "evidence" : "''", uint32(guid));
        if (!result)
        {
            handler->SendSysMessage("No history returned. If a database error occurred, inspect the SQL log.");
            return true;
        }
        do
        {
            Field* fields = result->Fetch();
            uint8 detector = fields[1].GetUInt8();
            handler->PSendSysMessage("UTC epoch=" UI64FMTD " %s map=%u opcode=%u pos=(%.3f,%.3f,%.3f) latency=%u ms",
                fields[0].GetUInt64(), detector < DetectionCount ? Names[detector] : "unknown",
                fields[2].GetUInt32(), uint32(fields[3].GetUInt16()), fields[4].GetInt32() / 1000.0,
                fields[5].GetInt32() / 1000.0, fields[6].GetInt32() / 1000.0, fields[7].GetUInt32());
            std::string details = fields[8].GetString();
            if (!details.empty())
                handler->PSendSysMessage("Evidence: %s", details.c_str());
        } while (result->NextRow());
        return true;
    }
    static bool PlayerReport(ChatHandler* handler, bool clear)
    {
        if (!Admin(handler) || !handler->GetSession())
            return false;
        Player* player = handler->GetSession()->GetPlayer()->GetSelectedPlayer();
        if (!player)
            player = handler->GetSession()->GetPlayer();
        std::array<uint32, DetectionCount> counts{};
        {
            std::lock_guard<std::mutex> lock(Mutex);
            auto itr = Players.find(player->GetGUID());
            if (itr == Players.end())
            {
                handler->SendSysMessage("No session samples for this player.");
                return true;
            }
            counts = itr->second.reports;
            if (clear)
            {
                itr->second = State();
                itr->second.graceUntil = Now() + Options.grace;
            }
        }
        handler->PSendSysMessage("%s: speed=%u fly=%u waterwalk=%u clock=%u teleport=%u jump=%u climb=%u%s", player->GetName().c_str(),
            counts[Speed], counts[Fly], counts[WaterWalk], counts[ClientClock], counts[Teleport], counts[Jump], counts[Climb], clear ? " (cleared)" : "");
        return true;
    }
    static bool Reports(ChatHandler* handler, char const*) { return PlayerReport(handler, false); }
    static bool Clear(ChatHandler* handler, char const*) { return PlayerReport(handler, true); }
};
}

void Addmod_anticheatScripts()
{
    new AnticheatWorld();
    new AnticheatPlayer();
    new AnticheatCommands();
}
