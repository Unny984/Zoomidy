#include "zoomidy/Zoomidy.h"

#include <cstdint>
#include <filesystem>
#include <utility>

#include "ll/api/Config.h"
#include "ll/api/mod/RegisterHelper.h"

#include "zoomidy/CameraDrift.h"
#include "zoomidy/Command.h"
#include "zoomidy/Hooks.h"
#include "zoomidy/Input.h"
#include "zoomidy/ZoomState.h"

namespace zoomidy {

namespace {

using ConfigJson = nlohmann::ordered_json;

/// What `ll::config::loadConfig` is meant to do, reimplemented because on LeviLamina 26.10 it
/// cannot read a config file at all.
///
/// 26.10 brace-initialises the parsed document -- `auto data{ParseType<J>{}(*content)}` -- and
/// for nlohmann that selects the initializer-list constructor, wrapping the whole config in an
/// array. Everything afterwards is then looking at the wrapper rather than the config:
/// `contains("version")` is false, so the updater runs, and its `erase("version")` throws
/// `cannot use erase() with array`. No config file is ever readable, and every launch quietly
/// falls back to the defaults. 26.20 assigns instead of brace-initialising and is unaffected.
///
/// Returns whether the file was already up to date, as the original does.
bool readConfigFile(Config& config, std::filesystem::path const& path) {
    if (!std::filesystem::exists(path)) {
        ll::config::saveConfig(config, path);
    }

    auto const content = ll::file_utils::readFile(path);
    if (!content || content->empty()) {
        return false;
    }

    // Assigned, not brace-initialised. That is the whole difference.
    auto data = ConfigJson::parse(*content, nullptr, true, true);

    bool upToDate = true;
    if (!data.contains("version") || data["version"].get<int64_t>() != config.version) {
        upToDate = false;

        // Lay what the file does have over the defaults, so a config written by an older version
        // keeps the settings it names and picks up sensible values for the ones it does not.
        data.erase("version");
        auto patch = ll::reflection::serialize<ConfigJson>(config).value();
        patch.merge_patch(data);
        data = std::move(patch);
    }

    ll::reflection::deserialize(config, data).value();
    return upToDate;
}

} // namespace

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
        bool const upToDate = readConfigFile(fresh, path);
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
