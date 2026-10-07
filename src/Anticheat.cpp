/*
* This file is part of Project SkyFire https://www.projectskyfire.org.
* See LICENSE.md file for Copyright information
*/
#include "MovementMonitor.h"
#include "ModuleConfig.h"
#include "ReportValues.h"
#include "Chat.h"
#include "Config.h"
#include "DatabaseEnv.h"
#include "Log.h"
#include "MoveSpline.h"
#include "Opcodes.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "SpellAuraDefines.h"
#include "WorldSession.h"
#include <array>
#include <chrono>
#include <ctime>
#include <limits>
#include <mutex>
#include <unordered_map>

namespace
{
using Clock = std::chrono::steady_clock;
using Milliseconds = std::chrono::milliseconds;
uint64 Now() { return uint64(std::chrono::duration_cast<Milliseconds>(Clock::now().time_since_epoch()).count()); }

enum Detection { Speed, Fly, WaterWalk, DetectionCount };
char const* const Names[] = { "speed", "fly", "waterwalk" };

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
    uint64 graceUntil = 0;
    std::array<uint32, DetectionCount> reports{};
    std::array<uint64, DetectionCount> lastReport{};
};

// Callbacks can run on different game threads; never retain player pointers.
std::mutex Mutex;
Settings Options;
std::unordered_map<uint64, State> Players;

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
        if (loaded && sConfigMgr->GetBoolDefault("Anticheat.DatabaseReports", false))
        {
            QueryResult schema = CharacterDatabase.Query(
                "SELECT COUNT(*) FROM information_schema.columns WHERE table_schema=DATABASE() "
                "AND table_name='mod_anticheat_reports' AND column_name IN "
                "('id','event_time','guid','account','map','detector','opcode','x_milli','y_milli','z_milli','latency')");
            next.database = schema && schema->Fetch()[0].GetUInt64() == 11;
            if (!next.database)
                SF_LOG_ERROR("anticheat", "Database history disabled: import the module character SQL and reload configuration.");
        }
        {
            std::lock_guard<std::mutex> lock(Mutex);
            Options = next;
            for (auto& entry : Players)
            {
                entry.second.movement.Reset();
                entry.second.graceUntil = Now() + next.grace;
            }
        }
        SF_LOG_INFO("server.loading", "Anticheat module: %s (report-only; config %s)",
            next.enabled ? "enabled" : "disabled", loaded ? "loaded" : "missing or invalid");
    }
};

void Grace(Player* player, uint32 duration = 0)
{
    std::lock_guard<std::mutex> lock(Mutex);
    auto itr = Players.find(player->GetGUID());
    if (itr == Players.end())
        return;
    itr->second.movement.Reset();
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
    }
    void OnLogout(Player* player) override
    {
        std::lock_guard<std::mutex> lock(Mutex);
        Players.erase(player->GetGUID());
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
        bool database = false;
        {
            std::lock_guard<std::mutex> lock(Mutex);
            if (!Options.enabled)
                return;
            auto itr = Players.find(player->GetGUID());
            if (itr == Players.end())
                return;
            State& state = itr->second;
            database = Options.database;
            if (mover != player || !player->IsInWorld() || player->IsBeingTeleported() ||
                player->GetTransport() || player->GetVehicle() || player->IsInFlight() ||
                !player->movespline->Finalized() ||
                (!Options.staff && player->GetSession()->GetSecurity() > SEC_PLAYER) ||
                player->GetSession()->GetLatency() > Options.latency)
            {
                state.movement.Reset();
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
            bool suspicious[DetectionCount] = {
                Options.speed && state.movement.Observe(now, player->GetPositionX(), player->GetPositionY(),
                    speed, Options.slack, Options.multiplier, Options.gap),
                Options.fly && !flightAura && !player->IsGameMaster() &&
                    (movement.flags & (MOVEMENTFLAG_FLYING | MOVEMENTFLAG_DISABLE_GRAVITY)),
                Options.waterWalk && !player->HasAuraType(SPELL_AURA_WATER_WALK) &&
                    !player->HasAuraType(SPELL_AURA_GHOST) && !player->IsGameMaster() &&
                    (movement.flags & MOVEMENTFLAG_WATERWALKING)
            };
            for (unsigned i = 0; i < DetectionCount; ++i)
                if (suspicious[i] && (!state.lastReport[i] || now - state.lastReport[i] >= Options.cooldown))
                {
                    state.lastReport[i] = now;
                    if (state.reports[i] != std::numeric_limits<uint32>::max())
                        ++state.reports[i];
                    emitted[i] = true;
                    counts[i] = state.reports[i];
                }
        }
        // Avoid holding module state while logging. No chat content, credentials or IPs.
        for (unsigned i = 0; i < DetectionCount; ++i)
            if (emitted[i])
            {
                SF_LOG_WARN("anticheat", "Report-only: guid=%u detector=%s count=%u map=%u opcode=%u",
                    player->GetGUIDLow(), Names[i], counts[i], player->GetMapId(), uint32(opcode));
                if (database)
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
            { "clear", rbac::RBAC_PERM_COMMAND_SERVER_INFO, false, &Clear, "" }
        };
        return { { "anticheat", rbac::RBAC_PERM_COMMAND_SERVER_INFO, true, NULL, "", commands } };
    }
    static bool Admin(ChatHandler* handler)
    {
        if (!handler->GetSession() || handler->GetSession()->GetSecurity() >= SEC_ADMINISTRATOR)
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
        bool database;
        {
            std::lock_guard<std::mutex> lock(Mutex);
            database = Options.database;
        }
        if (!database)
        {
            handler->SendSysMessage("Database history is disabled or its schema is unavailable.");
            return true;
        }
        QueryResult result = CharacterDatabase.PQuery(
            "SELECT event_time,detector,map,opcode,x_milli,y_milli,z_milli,latency FROM mod_anticheat_reports "
            "WHERE guid=%u ORDER BY id DESC LIMIT 10", uint32(guid));
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
        handler->PSendSysMessage("%s: speed=%u fly=%u waterwalk=%u%s", player->GetName().c_str(),
            counts[Speed], counts[Fly], counts[WaterWalk], clear ? " (cleared)" : "");
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
