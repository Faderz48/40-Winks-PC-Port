#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <vector>

#include "recomp_support.h"

extern "C" void func_80013A7C(uint8_t*, recomp_context*) {}
extern "C" void func_80013988(uint8_t*, recomp_context*) {}
extern "C" void func_8003ADE4(uint8_t*, recomp_context*) {}
extern "C" void func_80012EF0(uint8_t*, recomp_context*) {}

namespace {

constexpr uint32_t command_address = 0x8012359C;
constexpr uint32_t result_address = 0x8011FCC8;
constexpr uint32_t argument_address = 0x8011D6DC;
constexpr uint32_t pfs_address = 0x80100000;
constexpr uint32_t max_files_address = 0x80100100;
constexpr uint32_t used_files_address = 0x80100104;
constexpr uint32_t file_number_address = 0x80100108;
constexpr uint32_t file_state_address = 0x80100200;
constexpr uint32_t game_name_address = 0x80100300;
constexpr uint32_t extension_address = 0x80100320;
constexpr uint32_t write_buffer_address = 0x80100400;
constexpr uint32_t read_buffer_address = 0x80100500;
constexpr uint32_t save_menu_choice_address = 0x8011D6D9;
constexpr uint32_t game_code = 0x4E345745;
constexpr uint16_t company_code = 0x3458;

bool check(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "Controller Pak test failed: %s\n", message);
    }
    return condition;
}

uint32_t read_word(uint8_t* rdram, uint32_t address) {
    return static_cast<uint32_t>(MEM_W(0, S32(address)));
}

uint16_t read_half(uint8_t* rdram, uint32_t address) {
    return MEM_HU(0, S32(address));
}

uint8_t read_byte(uint8_t* rdram, uint32_t address) {
    return MEM_BU(0, S32(address));
}

void write_word(uint8_t* rdram, uint32_t address, uint32_t value) {
    MEM_W(0, S32(address)) = static_cast<int32_t>(value);
}

void write_byte(uint8_t* rdram, uint32_t address, uint8_t value) {
    MEM_B(0, S32(address)) = static_cast<int8_t>(value);
}

void clear_arguments(uint8_t* rdram) {
    for (uint32_t index = 0; index < 7; ++index) {
        write_word(rdram, argument_address + index * 4, 0);
    }
}

void set_argument(uint8_t* rdram, uint32_t index, uint32_t value) {
    write_word(rdram, argument_address + index * 4, value);
}

int run_command(uint8_t* rdram, uint8_t command) {
    recomp_context context{};
    write_byte(rdram, command_address, command);
    if (dispatch_controller_pak_command(rdram, &context) != 1) {
        return -100;
    }
    return static_cast<int32_t>(read_word(rdram, result_address));
}

template <size_t Size>
void write_bytes(uint8_t* rdram, uint32_t address,
                 const std::array<uint8_t, Size>& bytes) {
    for (size_t index = 0; index < Size; ++index) {
        write_byte(rdram, address + static_cast<uint32_t>(index), bytes[index]);
    }
}

} // namespace

int main() {
    std::vector<uint8_t> memory(8 * 1024 * 1024);
    uint8_t* rdram = memory.data();

    const auto unique_suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path test_directory =
        std::filesystem::temp_directory_path() /
        ("40-winks-controller-pak-test-" + std::to_string(unique_suffix));
    std::filesystem::create_directories(test_directory);
    const std::filesystem::path save_path =
        test_directory / "40-winks-controller-pak.note";

    configure_controller_pak_storage(test_directory.c_str());

    recomp_context accessory_context{};
    write_byte(rdram, save_menu_choice_address, 1);
    activate_virtual_controller_accessory(rdram, &accessory_context);
    if (!check(accessory_context.r2 == 1, "virtual accessory activation") ||
        !check(read_word(rdram, 0x8011D138) == 9, "virtual accessory PFS status") ||
        !check(read_byte(rdram, 0x801276B4) == 1, "virtual memory pak flag") ||
        !check(read_byte(rdram, 0x80128C2C) == 1, "virtual rumble flag") ||
        !check(read_byte(rdram, save_menu_choice_address) == 0,
               "accessory probe clears stale save-menu result")) {
        return 1;
    }

    clear_arguments(rdram);
    if (!check(run_command(rdram, 12) == 11, "motor probe falls through to memory pak")) {
        return 1;
    }

    clear_arguments(rdram);
    write_byte(rdram, save_menu_choice_address, 0x5A);
    set_argument(rdram, 0, pfs_address);
    set_argument(rdram, 1, 0);
    if (!check(run_command(rdram, 7) == 0, "pak initialization") ||
        !check(read_word(rdram, pfs_address) == 9, "combined pak and motor status") ||
        !check(read_word(rdram, pfs_address + 0x50) == 16, "directory size") ||
        !check(read_byte(rdram, save_menu_choice_address) == 0x5A,
               "pak initialization preserves menu state")) {
        return 1;
    }

    clear_arguments(rdram);
    set_argument(rdram, 0, pfs_address);
    set_argument(rdram, 1, max_files_address);
    set_argument(rdram, 2, used_files_address);
    if (!check(run_command(rdram, 3) == 0, "empty pak query") ||
        !check(read_word(rdram, max_files_address) == 16, "max file count") ||
        !check(read_word(rdram, used_files_address) == 0, "empty used count")) {
        return 1;
    }

    std::array<uint8_t, 16> game_name{};
    std::array<uint8_t, 4> extension{'A', 0, 0, 0};
    for (size_t index = 0; index < game_name.size(); ++index) {
        game_name[index] = static_cast<uint8_t>(index + 1);
    }
    write_bytes(rdram, game_name_address, game_name);
    write_bytes(rdram, extension_address, extension);

    clear_arguments(rdram);
    set_argument(rdram, 0, pfs_address);
    set_argument(rdram, 1, company_code);
    set_argument(rdram, 2, game_code);
    set_argument(rdram, 3, game_name_address);
    set_argument(rdram, 4, extension_address);
    set_argument(rdram, 5, 0x100);
    set_argument(rdram, 6, file_number_address);
    if (!check(run_command(rdram, 10) == 0, "note allocation") ||
        !check(read_word(rdram, file_number_address) == 0, "allocated file number") ||
        !check(std::filesystem::file_size(save_path) == 290, "host save size")) {
        return 1;
    }

    std::array<uint8_t, 0x100> payload{};
    for (size_t index = 0; index < payload.size(); ++index) {
        payload[index] = static_cast<uint8_t>((index * 37 + 11) & 0xFF);
    }
    write_bytes(rdram, write_buffer_address, payload);

    clear_arguments(rdram);
    set_argument(rdram, 0, pfs_address);
    set_argument(rdram, 1, 0);
    set_argument(rdram, 2, 1);
    set_argument(rdram, 3, 0);
    set_argument(rdram, 4, payload.size());
    set_argument(rdram, 5, write_buffer_address);
    if (!check(run_command(rdram, 9) == 0, "note write")) {
        return 1;
    }

    configure_controller_pak_storage(test_directory.c_str());

    clear_arguments(rdram);
    set_argument(rdram, 0, pfs_address);
    set_argument(rdram, 1, company_code);
    set_argument(rdram, 2, game_code);
    set_argument(rdram, 3, game_name_address);
    set_argument(rdram, 4, extension_address);
    set_argument(rdram, 5, file_number_address);
    if (!check(run_command(rdram, 11) == 0, "note lookup after reload") ||
        !check(read_word(rdram, file_number_address) == 0, "reloaded file number")) {
        return 1;
    }

    clear_arguments(rdram);
    set_argument(rdram, 0, pfs_address);
    set_argument(rdram, 1, 0);
    set_argument(rdram, 2, file_state_address);
    if (!check(run_command(rdram, 5) == 0, "file state") ||
        !check(read_word(rdram, file_state_address) == 0x100, "file size") ||
        !check(read_word(rdram, file_state_address + 4) == game_code, "game code") ||
        !check(read_half(rdram, file_state_address + 8) == company_code, "company code")) {
        return 1;
    }

    clear_arguments(rdram);
    set_argument(rdram, 0, pfs_address);
    set_argument(rdram, 1, 0);
    set_argument(rdram, 2, 0);
    set_argument(rdram, 3, 0);
    set_argument(rdram, 4, payload.size());
    set_argument(rdram, 5, read_buffer_address);
    if (!check(run_command(rdram, 9) == 0, "note read")) {
        return 1;
    }
    for (size_t index = 0; index < payload.size(); ++index) {
        if (!check(read_byte(rdram, read_buffer_address + static_cast<uint32_t>(index)) ==
                       payload[index],
                   "reloaded payload contents")) {
            return 1;
        }
    }

    clear_arguments(rdram);
    set_argument(rdram, 0, pfs_address);
    set_argument(rdram, 1, company_code);
    set_argument(rdram, 2, game_code);
    set_argument(rdram, 3, game_name_address);
    set_argument(rdram, 4, extension_address);
    if (!check(run_command(rdram, 6) == 0, "note deletion") ||
        !check(!std::filesystem::exists(save_path), "host save deletion")) {
        return 1;
    }

    std::error_code cleanup_error;
    std::filesystem::remove_all(test_directory, cleanup_error);
    std::printf("Controller Pak persistence test passed.\n");
    return 0;
}
