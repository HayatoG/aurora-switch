#pragma once

#include <aurora/rmlui.hpp>
#ifdef AURORA_PLATFORM_SWITCH
#include <RmlUi/Core/SystemInterface.h>
#else
#include <RmlUi_Platform_SDL.h>
#endif

namespace aurora::rmlui {

#ifdef AURORA_PLATFORM_SWITCH
class SystemInterface_Aurora : public Rml::SystemInterface {
#else
class SystemInterface_Aurora : public SystemInterface_SDL {
#endif
public:
  SystemInterface_Aurora();
  ~SystemInterface_Aurora() override = default;
#ifdef AURORA_PLATFORM_SWITCH
  double GetElapsedTime() override;
#endif
  bool LogMessage(Rml::Log::Type type, const Rml::String& message) override;
  void ActivateKeyboard(Rml::Vector2f caret_position, float line_height) override;
  void DeactivateKeyboard() override;

  // Custom API
  void SetInputType(InputType type) noexcept;

private:
  InputType mTextInputType = InputType::Text;
  InputType mActiveTextInputType = InputType::Text;
  bool mHasActiveTextInputType = false;
};

} // namespace aurora::rmlui
