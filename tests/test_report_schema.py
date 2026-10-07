#!/usr/bin/env python3
# This file is part of Project SkyFire https://www.projectskyfire.org.
# See LICENSE.md file for Copyright information
"""Test module SQL in an isolated schema. Password may be supplied in MYSQL_PWD."""
import argparse
from pathlib import Path
import re
import subprocess
import uuid


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mysql", default="mysql")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", default="3306")
    parser.add_argument("--user", default="root")
    args = parser.parse_args()
    scratch = "anticheat_schema_test_" + uuid.uuid4().hex[:12]
    assert re.fullmatch(r"anticheat_schema_test_[a-f0-9]{12}", scratch)
    command = [args.mysql, "--host=" + args.host, "--port=" + args.port,
               "--user=" + args.user, "--batch", "--skip-column-names"]

    def run(sql, database=None):
        cmd = command + (["--database=" + database] if database else [])
        result = subprocess.run(cmd, input=sql, text=True, capture_output=True)
        if result.returncode:
            raise RuntimeError(result.stderr)
        return result.stdout.strip()

    sql_dir = Path(__file__).resolve().parents[1] / "sql/pending_updates/characters"
    create = (sql_dir / "create_anticheat_reports.sql").read_text(encoding="utf8")
    upgrade = (sql_dir / "extend_anticheat_report_evidence.sql").read_text(encoding="utf8")
    legacy = create.replace("  `evidence` varchar(512) NOT NULL DEFAULT '',\n", "")
    assert legacy != create
    run("CREATE DATABASE `" + scratch + "`;")
    try:
        run(legacy, scratch)
        run("INSERT INTO mod_anticheat_reports "
            "(event_time,guid,account,map,detector,opcode,x_milli,y_milli,z_milli,latency) "
            "VALUES (123,42,7,0,0,1,-1000,2000,3000,50);", scratch)
        run(upgrade, scratch)
        run(upgrade, scratch)
        assert run("SELECT CONCAT(guid,':',event_time,':',evidence) FROM mod_anticheat_reports;", scratch) == "42:123:"
        evidence = "distance_yd=100.000 limit_yd=50.000 elapsed_ms=100"
        run("UPDATE mod_anticheat_reports SET evidence=X'" + evidence.encode().hex() + "';", scratch)
        assert run("SELECT evidence FROM mod_anticheat_reports;", scratch) == evidence
        run(create, scratch)
        assert run("SELECT COUNT(*) FROM mod_anticheat_reports;", scratch) == "1"
        run("DROP TABLE mod_anticheat_reports;", scratch)
        run(create, scratch)
        run(upgrade, scratch)
        assert run("SELECT COUNT(*) FROM information_schema.columns "
                   "WHERE table_schema=DATABASE() AND table_name='mod_anticheat_reports';", scratch) == "12"
        print("Fresh install, repeatable upgrade, preserved reports and evidence round-trip passed.")
    finally:
        run("DROP DATABASE `" + scratch + "`;")
        print("Scratch database removed.")


if __name__ == "__main__":
    main()
