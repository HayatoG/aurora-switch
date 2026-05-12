#include "input.hpp"

namespace aurora::input {
Module Log("aurora::input");
absl::flat_hash_map<Uint32, GameController> g_GameControllers;

namespace {
float g_scrollX = 0.f;
float g_scrollY = 0.f;
} // namespace

GameController* get_controller_for_player(uint32_t) noexcept { return nullptr; }
Sint32 get_instance_for_player(uint32_t) noexcept { return -1; }
SDL_JoystickID add_controller(SDL_JoystickID which) noexcept { return which; }
void remove_controller(Uint32) noexcept {}
Sint32 player_index(Uint32) noexcept { return -1; }
void set_player_index(Uint32, Sint32) noexcept {}
std::string controller_name(Uint32) noexcept { return {}; }
bool is_gamecube(Uint32) noexcept { return false; }
bool controller_has_rumble(Uint32) noexcept { return false; }
void controller_rumble(uint32_t, uint16_t, uint16_t, uint16_t) noexcept {}
uint32_t controller_count() noexcept { return 0; }
void initialize() noexcept {}
void persist_controller_for_player(uint32_t, const GameController*) noexcept {}

void set_mouse_scroll(float scrollX, float scrollY) noexcept {
  g_scrollX = scrollX;
  g_scrollY = scrollY;
}

void get_mouse_scroll(float* scrollX, float* scrollY) noexcept {
  if (scrollX != nullptr) {
    *scrollX = g_scrollX;
  }
  if (scrollY != nullptr) {
    *scrollY = g_scrollY;
  }
}

void shutdown() noexcept { g_GameControllers.clear(); }
} // namespace aurora::input
