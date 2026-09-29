#pragma once
#include <eclipse.eclipse-menu/include/config.hpp>

namespace compat {
    struct EclipseToggler {
        struct EclipseSetting {
            ZStringView id;
            bool wasEnabled;

            EclipseSetting(ZStringView id)
                : id(id), wasEnabled(eclipse::config::getInternal<bool>(id, false)) {}

            ~EclipseSetting() {
                eclipse::config::setInternal<bool>(id, wasEnabled);
            }
        };

        EclipseSetting solidWaveTrail{"player.solidwavetrail"};
        EclipseSetting noVehicleParticles{"player.novehicleparticles"};
        EclipseSetting noTrail{"player.notrail"};
        EclipseSetting alwaysTrail{"player.alwaystrail"};
        EclipseSetting noWaveTrailBehind{"player.nowavetrailbehind"};
        EclipseSetting noShader{"level.noshader"};
        EclipseSetting noParticles{"level.noparticles"};
    };

    inline geode::Result<std::optional<EclipseToggler>> getEclipse() {
        if (geode::Loader::get()->getLoadedMod("eclipse.eclipse-menu")) {
            for (auto [id, name] : std::initializer_list<std::pair<std::string_view, std::string_view>>{
                {"level.noglow", "No Object Glow"},
                {"level.legacytrail", "Legacy Trail"},
                {"player.norobotfire", "No Robot Fire"}
            }) {
                if (eclipse::config::getInternal<bool>(id, false)) {
                    return Err("Please disable <co>{}</c> in <cj>Eclipse Menu</c> and restart the level to be able to take thumbnails.", name);
                }
            }

            return Ok(EclipseToggler{});
        }
        return Ok(std::nullopt);
    }
}