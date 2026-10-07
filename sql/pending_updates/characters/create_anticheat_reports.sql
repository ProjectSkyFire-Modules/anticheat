-- This file is part of Project SkyFire https://www.projectskyfire.org.
-- See LICENSE.md file for Copyright information
-- Optional module-owned report history. Apply to the CHARACTER database.
-- Reapplying this file preserves existing reports.
CREATE TABLE IF NOT EXISTS `mod_anticheat_reports` (
  `id` bigint unsigned NOT NULL AUTO_INCREMENT,
  `event_time` bigint unsigned NOT NULL,
  `guid` int unsigned NOT NULL,
  `account` int unsigned NOT NULL,
  `map` int unsigned NOT NULL,
  `detector` tinyint unsigned NOT NULL,
  `opcode` smallint unsigned NOT NULL,
  `x_milli` int NOT NULL,
  `y_milli` int NOT NULL,
  `z_milli` int NOT NULL,
  `latency` int unsigned NOT NULL,
  PRIMARY KEY (`id`),
  KEY `guid_history` (`guid`,`id`),
  KEY `event_time` (`event_time`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
