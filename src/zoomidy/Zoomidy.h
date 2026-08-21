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

    /// Read from the render thread on every frame, and written from the server thread when the
    /// settings form is applied. The config holds nothing but naturally aligned scalars, so the
    /// worst a race can produce is one frame built from a mix of old and new settings — never a
    /// torn value. That is not worth a lock on the FOV hot path.
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
