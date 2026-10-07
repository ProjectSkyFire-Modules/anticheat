-- This file is part of Project SkyFire https://www.projectskyfire.org.
-- See LICENSE.md file for Copyright information
-- Apply after create_anticheat_reports.sql. Safe to reapply; preserves reports.
-- Uses metadata rather than ADD COLUMN IF NOT EXISTS for MySQL compatibility.
SET @anticheat_evidence_sql = IF(
  EXISTS (SELECT 1 FROM information_schema.columns WHERE table_schema = DATABASE()
          AND table_name = 'mod_anticheat_reports' AND column_name = 'evidence'),
  'SELECT 1',
  'ALTER TABLE `mod_anticheat_reports` ADD COLUMN `evidence` varchar(512) NOT NULL DEFAULT '''''
);
PREPARE anticheat_evidence_stmt FROM @anticheat_evidence_sql;
EXECUTE anticheat_evidence_stmt;
DEALLOCATE PREPARE anticheat_evidence_stmt;
SET @anticheat_evidence_sql = NULL;
