#include "AudioDeviceLister.hpp"

#include <pipewire/pipewire.h>

#include <cstring>
#include <ctime>

namespace wiremic::platform {

namespace {

struct ListerContext {
  struct pw_thread_loop* loop{nullptr};
  struct pw_registry* registry{nullptr};
  struct spa_hook registryListener{};
  struct spa_hook coreListener{};
  std::vector<AudioInputDevice> devices;
  int pendingSync{-1};
  bool done{false};
};

void OnGlobal(void* data, uint32_t id, uint32_t /*permissions*/,
              const char* type, uint32_t /*version*/,
              const struct spa_dict* props) {
  auto* ctx = static_cast<ListerContext*>(data);
  if (!props || std::strcmp(type, PW_TYPE_INTERFACE_Node) != 0) return;

  const char* mediaClass = spa_dict_lookup(props, PW_KEY_MEDIA_CLASS);
  if (!mediaClass || std::strcmp(mediaClass, "Audio/Source") != 0) return;

  AudioInputDevice device;
  device.id = std::to_string(id);

  const char* nodeName = spa_dict_lookup(props, PW_KEY_NODE_NAME);
  device.name = nodeName ? nodeName : device.id;

  const char* nodeDescription =
      spa_dict_lookup(props, PW_KEY_NODE_DESCRIPTION);
  device.description = nodeDescription ? nodeDescription : device.name;

  const char* defaultFlag = spa_dict_lookup(props, "node.default");
  device.isDefault = defaultFlag && std::strcmp(defaultFlag, "true") == 0;

  ctx->devices.push_back(std::move(device));
}

void OnCoreDone(void* data, uint32_t id, int seq) {
  auto* ctx = static_cast<ListerContext*>(data);
  if (id == PW_ID_CORE && seq == ctx->pendingSync) {
    ctx->done = true;
    pw_thread_loop_signal(ctx->loop, false);
  }
}

}  // namespace

std::vector<AudioInputDevice> AudioDeviceLister::ListInputDevices(
    int timeoutMs) {
  pw_init(nullptr, nullptr);

  ListerContext ctx;
  ctx.loop = pw_thread_loop_new("wiremic-device-lister", nullptr);
  if (!ctx.loop) return {};

  pw_thread_loop_lock(ctx.loop);

  struct pw_context* context =
      pw_context_new(pw_thread_loop_get_loop(ctx.loop), nullptr, 0);
  if (!context) {
    pw_thread_loop_unlock(ctx.loop);
    pw_thread_loop_destroy(ctx.loop);
    return {};
  }

  struct pw_core* core = pw_context_connect(context, nullptr, 0);
  if (!core) {
    pw_thread_loop_unlock(ctx.loop);
    pw_context_destroy(context);
    pw_thread_loop_destroy(ctx.loop);
    return {};
  }

  static const struct pw_core_events coreEvents = [] {
    struct pw_core_events events{};
    events.version = PW_VERSION_CORE_EVENTS;
    events.done = &OnCoreDone;
    return events;
  }();
  pw_core_add_listener(core, &ctx.coreListener, &coreEvents, &ctx);

  ctx.registry = pw_core_get_registry(core, PW_VERSION_REGISTRY, 0);

  static const struct pw_registry_events registryEvents = [] {
    struct pw_registry_events events{};
    events.version = PW_VERSION_REGISTRY_EVENTS;
    events.global = &OnGlobal;
    return events;
  }();
  pw_registry_add_listener(ctx.registry, &ctx.registryListener,
                            &registryEvents, &ctx);

  ctx.pendingSync = pw_core_sync(core, PW_ID_CORE, 0);

  if (pw_thread_loop_start(ctx.loop) < 0) {
    pw_thread_loop_unlock(ctx.loop);
    pw_context_destroy(context);
    pw_thread_loop_destroy(ctx.loop);
    return {};
  }

  struct timespec startTime;
  clock_gettime(CLOCK_MONOTONIC, &startTime);
  const uint64_t deadlineNs = static_cast<uint64_t>(timeoutMs) * 1000000ULL;

  while (!ctx.done) {
    pw_thread_loop_timed_wait(ctx.loop, 1);

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    const uint64_t elapsedNs =
        static_cast<uint64_t>(now.tv_sec - startTime.tv_sec) * 1000000000ULL +
        static_cast<uint64_t>(now.tv_nsec - startTime.tv_nsec);
    if (elapsedNs >= deadlineNs) break;
  }

  pw_thread_loop_unlock(ctx.loop);
  pw_thread_loop_stop(ctx.loop);

  pw_proxy_destroy(reinterpret_cast<struct pw_proxy*>(ctx.registry));
  pw_core_disconnect(core);
  pw_context_destroy(context);
  pw_thread_loop_destroy(ctx.loop);

  return ctx.devices;
}

}  // namespace wiremic::platform
