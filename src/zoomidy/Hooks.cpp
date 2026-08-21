#include "zoomidy/Hooks.h"

#include "ll/api/memory/Hook.h"

#include "mc/client/renderer/game/ItemInHandRenderer.h"
#include "mc/client/renderer/game/LevelRendererPlayer.h"
#include "mc/deps/minecraft_renderer/game/ItemContextFlags.h"

#include "zoomidy/ZoomState.h"
#include "zoomidy/Zoomidy.h"

class BaseActorRenderContext;

namespace zoomidy {

/// The renderer asks for the field of view once per frame, right before building the camera.
/// Dividing the answer is the whole zoom: everything else in this mod exists to decide what the
/// divisor should be.
LL_TYPE_INSTANCE_HOOK(
    ZoomidyFovHook,
    ll::memory::HookPriority::Normal,
    LevelRendererPlayer,
    &LevelRendererPlayer::getFov,
    float,
    float a,
    bool  enableVariableFOV
) {
    float const fov = origin(a, enableVariableFOV);

    double const divisor = ZoomState::getInstance().currentDivisor();
    if (divisor <= 1.0) {
        return fov;
    }
    return static_cast<float>(static_cast<double>(fov) / divisor);
}

/// Skipping the original call drops the whole first-person arm, held item and offhand item for
/// the frame. The hand is a screen-space overlay rather than part of the world, so a zoomed
/// camera would otherwise leave it filling a quarter of the view at its normal size.
LL_TYPE_INSTANCE_HOOK(
    ZoomidyHandHook,
    ll::memory::HookPriority::Normal,
    ItemInHandRenderer,
    &ItemInHandRenderer::renderFirstPerson,
    void,
    ::BaseActorRenderContext& renderContext,
    ::ItemContextFlags        itemFlags
) {
    if (Zoomidy::getInstance().getConfig().view.hideHand && ZoomState::getInstance().isEngaged()) {
        return;
    }
    origin(renderContext, itemFlags);
}

void registerHooks() {
    ll::memory::HookRegistrar<ZoomidyFovHook, ZoomidyHandHook>::hook();
}

void unregisterHooks() {
    ll::memory::HookRegistrar<ZoomidyFovHook, ZoomidyHandHook>::unhook();
}

} // namespace zoomidy
