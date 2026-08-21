#include "zoomidy/Zoomidy.h"

#include "ll/api/Config.h"
#include "ll/api/mod/RegisterHelper.h"

#include "zoomidy/CameraDrift.h"
#include "zoomidy/Command.h"
#include "zoomidy/Hooks.h"
#include "zoomidy/Input.h"
#include "zoomidy/ZoomState.h"

namespace zoomidy {

Zoomidy& Zoomidy::getInstance() {
    static Zoomidy instance;
    return instance;
}

std::filesystem::path Zoomidy::getConfigPath() const { return getSelf().getConfigDir() / "config.json"; }

bool Zoomidy::loadConfig() {
    Config fresh{};
    auto const path = getConfigPath();

    try {
        // Returns false when the file was missing or written by an older version; in both cases
        // it has already merged what it could and the merged result is worth writing back.
        bool const upToDate = ll::config::loadConfig(fresh, path);
        mConfig             = fresh;
        if (!upToDate) {
            return ll::config::saveConfig(mConfig, path);
        }
        return true;
    } catch (std::exception const& e) {
        getSelf().getLogger().error("Failed to read {}: {}", path.string(), e.what());
        getSelf().getLogger().warn("Falling back to the default settings.");
        mConfig = Config{};
        return false;
    }
}

bool Zoomidy::saveConfig() const {
    try {
        return ll::config::saveConfig(mConfig, getConfigPath());
    } catch (std::exception const& e) {
        getSelf().getLogger().error("Failed to write {}: {}", getConfigPath().string(), e.what());
        return false;
    }
}

void Zoomidy::applyConfig(Config const& config) {
    mConfig = config;
    saveConfig();
}

bool Zoomidy::load() {
    loadConfig();
    return true;
}

bool Zoomidy::enable() {
    registerHooks();
    drift::registerHook();
    registerInputListeners();
    registerCommand();
    getSelf().getLogger().info("Zoomidy ready.");
    return true;
}

bool Zoomidy::disable() {
    unregisterCommand();
    unregisterInputListeners();
    drift::unregisterHook();
    unregisterHooks();
    ZoomState::getInstance().reset();
    return true;
}

} // namespace zoomidy

LL_REGISTER_MOD(zoomidy::Zoomidy, zoomidy::Zoomidy::getInstance());
