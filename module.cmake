# This file is part of Project SkyFire https://www.projectskyfire.org.
# See LICENSE.md file for Copyright information
option(MOD_ANTICHEAT "Build the optional SkyFire movement monitor" ON)
set(MODULE_ENABLED ${MOD_ANTICHEAT})
if(MODULE_ENABLED AND NOT MODULE_DIR_NAME STREQUAL "mod-anticheat")
    message(FATAL_ERROR "Install anticheat in modules/mod-anticheat (required loader name)")
endif()
