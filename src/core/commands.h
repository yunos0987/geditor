// Command registry: command names (as in docs/PLAN.md 4.4) and their
// implementations.
#pragma once

#include <span>
#include <string_view>

namespace geditor {

class Editor;

using CommandFn = void (*)(Editor&);

struct CommandInfo {
  const char* name;
  CommandFn fn;
};

std::span<const CommandInfo> all_commands();
const CommandInfo* find_command(std::string_view name);

}  // namespace geditor
