#pragma once

#include <filesystem>

#include "ll/api/mod/NativeMod.h"

#include "zoomidy/Config.h"

namespace zoomidy {

class Zoomidy {
public:
    static Zoomidy& getInstance();

    Zoomidy() : mSelf(*ll::mod::NativeMod::current()) {}

    [[nodiscard]] ll::mod::NativeMod& getSelf() const { return mSelf; }

    [[nodiscard]] Config const& getConfig() const { return mConfig; }

    /// Replaces the whole config and writes it back to disk.
    void applyConfig(Config const& config);

    [[nodiscard]] std::filesystem::path getConfigPath() const;

    bool loadConfig();
    bool saveConfig() const;

    bool load();
    bool enable();
    bool disable();

private:
    ll::mod::NativeMod& mSelf;
    Config              mConfig{};
};

} // namespace zoomidy
