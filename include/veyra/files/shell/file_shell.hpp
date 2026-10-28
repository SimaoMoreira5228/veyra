#ifndef VEYRA_FILES_FILE_SHELL_HPP
#define VEYRA_FILES_FILE_SHELL_HPP

#include <cstdint>
#include <filesystem>
#include <system_error>

namespace veyra {

enum class FileShellAction : std::uint8_t {
    open,
    reveal,
    trash,
};

std::error_code
perform_file_shell_action(FileShellAction action, const std::filesystem::path& path) noexcept;

}

namespace veyra::files {
using ::veyra::FileShellAction;
using ::veyra::perform_file_shell_action;
}

namespace veyra::files::shell {
using ::veyra::FileShellAction;
using ::veyra::perform_file_shell_action;
}

#endif
