#include "recomp_support.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>

#include "funcs.h"

namespace {

constexpr uint32_t rdram_start = 0x80000000;
constexpr uint32_t rdram_end = 0x80800000;
constexpr uint32_t save_command_address = 0x8012359C;
constexpr uint32_t save_result_address = 0x8011FCC8;
constexpr uint32_t save_argument_address = 0x8011D6DC;
constexpr uint32_t controller_queue_address = 0x8011D0F0;
constexpr uint32_t save_menu_choice_address = 0x8011D6D9;
constexpr uint32_t controller_rumble_state_address = 0x80128C2C;

constexpr uint32_t game_code = 0x4E345745; // N4WE
constexpr uint16_t company_code = 0x3458;
constexpr size_t game_name_size = 16;
constexpr size_t extension_size = 4;
constexpr size_t save_payload_size = 0x100;
constexpr int controller_pak_file_count = 16;
constexpr int controller_pak_capacity = 123 * 0x100;

constexpr int pfs_ok = 0;
constexpr int pfs_err_invalid = 5;
constexpr int pfs_err_bad_data = 6;
constexpr int pfs_data_full = 7;
constexpr int pfs_dir_full = 8;
constexpr int pfs_err_exist = 9;
constexpr int pfs_err_device = 11;

constexpr std::array<uint8_t, 8> save_magic{
    '4', '0', 'W', 'P', 'A', 'K', '0', '1',
};
constexpr std::array<uint8_t, 4> game_code_bytes{0x4E, 0x34, 0x57, 0x45};
constexpr std::array<uint8_t, 2> company_code_bytes{0x34, 0x58};
constexpr size_t host_save_size = save_magic.size() + game_code_bytes.size() +
    company_code_bytes.size() + game_name_size + extension_size + save_payload_size;

struct ControllerPakNote {
    std::filesystem::path path;
    bool present = false;
    std::array<uint8_t, game_name_size> game_name{};
    std::array<uint8_t, extension_size> extension{};
    std::array<uint8_t, save_payload_size> payload{};
};

ControllerPakNote controller_pak;
std::mutex controller_pak_mutex;

bool valid_rdram_range(uint32_t address, size_t size) {
    const uint64_t start = address;
    const uint64_t end = start + size;
    return start >= rdram_start && end >= start && end <= rdram_end;
}

uint32_t read_u32(uint8_t* rdram, uint32_t address) {
    return static_cast<uint32_t>(MEM_W(0, S32(address)));
}

void write_u32(uint8_t* rdram, uint32_t address, uint32_t value) {
    MEM_W(0, S32(address)) = static_cast<int32_t>(value);
}

void write_u16(uint8_t* rdram, uint32_t address, uint16_t value) {
    MEM_H(0, S32(address)) = static_cast<int16_t>(value);
}

uint8_t read_u8(uint8_t* rdram, uint32_t address) {
    return MEM_BU(0, S32(address));
}

void write_u8(uint8_t* rdram, uint32_t address, uint8_t value) {
    MEM_B(0, S32(address)) = static_cast<int8_t>(value);
}

template <size_t Size>
bool read_bytes(uint8_t* rdram, uint32_t address, std::array<uint8_t, Size>& output) {
    if (!valid_rdram_range(address, Size)) {
        return false;
    }
    for (size_t index = 0; index < Size; ++index) {
        output[index] = read_u8(rdram, address + static_cast<uint32_t>(index));
    }
    return true;
}

template <size_t Size>
bool write_bytes(uint8_t* rdram, uint32_t address,
                 const std::array<uint8_t, Size>& input) {
    if (!valid_rdram_range(address, Size)) {
        return false;
    }
    for (size_t index = 0; index < Size; ++index) {
        write_u8(rdram, address + static_cast<uint32_t>(index), input[index]);
    }
    return true;
}

bool read_host_bytes(std::ifstream& input, uint8_t* output, size_t size) {
    input.read(reinterpret_cast<char*>(output), static_cast<std::streamsize>(size));
    return input.gcount() == static_cast<std::streamsize>(size);
}

bool load_controller_pak_note() {
    std::error_code file_error;
    if (!std::filesystem::exists(controller_pak.path, file_error) || file_error) {
        return false;
    }
    if (std::filesystem::file_size(controller_pak.path, file_error) != host_save_size ||
        file_error) {
        std::fprintf(stderr, "Ignoring invalid Controller Pak save: %s\n",
            controller_pak.path.c_str());
        return false;
    }

    std::ifstream input{controller_pak.path, std::ios::binary};
    std::array<uint8_t, save_magic.size()> magic{};
    std::array<uint8_t, game_code_bytes.size()> stored_game_code{};
    std::array<uint8_t, company_code_bytes.size()> stored_company_code{};
    ControllerPakNote loaded;

    if (!input ||
        !read_host_bytes(input, magic.data(), magic.size()) ||
        !read_host_bytes(input, stored_game_code.data(), stored_game_code.size()) ||
        !read_host_bytes(input, stored_company_code.data(), stored_company_code.size()) ||
        !read_host_bytes(input, loaded.game_name.data(), loaded.game_name.size()) ||
        !read_host_bytes(input, loaded.extension.data(), loaded.extension.size()) ||
        !read_host_bytes(input, loaded.payload.data(), loaded.payload.size()) ||
        magic != save_magic || stored_game_code != game_code_bytes ||
        stored_company_code != company_code_bytes) {
        std::fprintf(stderr, "Ignoring invalid Controller Pak save: %s\n",
            controller_pak.path.c_str());
        return false;
    }

    controller_pak.present = true;
    controller_pak.game_name = loaded.game_name;
    controller_pak.extension = loaded.extension;
    controller_pak.payload = loaded.payload;
    return true;
}

bool persist_controller_pak_note() {
    if (!controller_pak.present || controller_pak.path.empty()) {
        return false;
    }

    std::filesystem::path temporary_path = controller_pak.path;
    temporary_path += ".tmp";
    std::ofstream output{temporary_path, std::ios::binary | std::ios::trunc};
    if (!output) {
        return false;
    }

    output.write(reinterpret_cast<const char*>(save_magic.data()), save_magic.size());
    output.write(reinterpret_cast<const char*>(game_code_bytes.data()),
        game_code_bytes.size());
    output.write(reinterpret_cast<const char*>(company_code_bytes.data()),
        company_code_bytes.size());
    output.write(reinterpret_cast<const char*>(controller_pak.game_name.data()),
        controller_pak.game_name.size());
    output.write(reinterpret_cast<const char*>(controller_pak.extension.data()),
        controller_pak.extension.size());
    output.write(reinterpret_cast<const char*>(controller_pak.payload.data()),
        controller_pak.payload.size());
    output.flush();
    output.close();

    if (!output) {
        std::error_code cleanup_error;
        std::filesystem::remove(temporary_path, cleanup_error);
        return false;
    }

    std::error_code rename_error;
    std::filesystem::rename(temporary_path, controller_pak.path, rename_error);
    if (rename_error) {
        std::error_code cleanup_error;
        std::filesystem::remove(temporary_path, cleanup_error);
        return false;
    }
    return true;
}

bool remove_controller_pak_note() {
    std::error_code remove_error;
    std::filesystem::remove(controller_pak.path, remove_error);
    return !remove_error;
}

bool metadata_matches(uint8_t* rdram, uint16_t requested_company,
                      uint32_t requested_game, uint32_t game_name_address,
                      uint32_t extension_address) {
    if (!controller_pak.present || requested_company != company_code ||
        requested_game != game_code) {
        return false;
    }

    if (game_name_address != 0) {
        std::array<uint8_t, game_name_size> requested_name{};
        if (!read_bytes(rdram, game_name_address, requested_name) ||
            requested_name != controller_pak.game_name) {
            return false;
        }
    }
    if (extension_address != 0) {
        std::array<uint8_t, extension_size> requested_extension{};
        if (!read_bytes(rdram, extension_address, requested_extension) ||
            requested_extension != controller_pak.extension) {
            return false;
        }
    }
    return true;
}

int initialize_controller_pak(uint8_t* rdram, uint32_t pfs_address, int channel) {
    constexpr size_t pfs_size = 0x68;
    if (!valid_rdram_range(pfs_address, pfs_size)) {
        return pfs_err_invalid;
    }

    // The PC runtime can provide persistent storage and SDL rumble at once,
    // even though an original controller can hold only one accessory.
    write_u32(rdram, pfs_address + 0x00, 9); // PFS_INITIALIZED | PFS_MOTOR_INITIALIZED
    write_u32(rdram, pfs_address + 0x04, controller_queue_address);
    write_u32(rdram, pfs_address + 0x08, static_cast<uint32_t>(channel));
    for (uint32_t offset = 0x0C; offset < 0x4C; ++offset) {
        write_u8(rdram, pfs_address + offset, 0);
    }
    write_u32(rdram, pfs_address + 0x4C, 0);
    write_u32(rdram, pfs_address + 0x50, controller_pak_file_count);
    write_u32(rdram, pfs_address + 0x54, 8);
    write_u32(rdram, pfs_address + 0x58, 16);
    write_u32(rdram, pfs_address + 0x5C, 24);
    write_u32(rdram, pfs_address + 0x60, 5);
    write_u8(rdram, pfs_address + 0x64, 1);
    write_u8(rdram, pfs_address + 0x65, 0);
    if (channel >= 0 && channel < 4) {
        write_u8(rdram, controller_rumble_state_address + static_cast<uint32_t>(channel), 1);
    }
    return pfs_ok;
}

int write_file_state(uint8_t* rdram, int file_number, uint32_t state_address) {
    constexpr size_t state_size = 0x20;
    if (!controller_pak.present || file_number != 0 ||
        !valid_rdram_range(state_address, state_size)) {
        return pfs_err_invalid;
    }

    write_u32(rdram, state_address + 0x00, save_payload_size);
    write_u32(rdram, state_address + 0x04, game_code);
    write_u16(rdram, state_address + 0x08, company_code);
    if (!write_bytes(rdram, state_address + 0x0A, controller_pak.extension) ||
        !write_bytes(rdram, state_address + 0x0E, controller_pak.game_name)) {
        return pfs_err_invalid;
    }
    return pfs_ok;
}

int read_write_note(uint8_t* rdram, int file_number, uint8_t flag, int offset,
                    int size, uint32_t buffer_address) {
    if (!controller_pak.present || file_number != 0 ||
        (flag != 0 && flag != 1) || size <= 0 || (size & 0x1F) != 0 ||
        offset < 0 || (offset & 0x1F) != 0 ||
        static_cast<uint64_t>(offset) + static_cast<uint64_t>(size) > save_payload_size ||
        !valid_rdram_range(buffer_address, static_cast<size_t>(size))) {
        return pfs_err_invalid;
    }

    if (flag == 0) {
        for (int index = 0; index < size; ++index) {
            write_u8(rdram, buffer_address + static_cast<uint32_t>(index),
                controller_pak.payload[static_cast<size_t>(offset + index)]);
        }
        std::printf("Controller Pak: loaded 40 Winks save data.\n");
        return pfs_ok;
    }

    const auto previous_payload = controller_pak.payload;
    for (int index = 0; index < size; ++index) {
        controller_pak.payload[static_cast<size_t>(offset + index)] =
            read_u8(rdram, buffer_address + static_cast<uint32_t>(index));
    }
    if (!persist_controller_pak_note()) {
        controller_pak.payload = previous_payload;
        std::fprintf(stderr, "Controller Pak: could not write %s\n",
            controller_pak.path.c_str());
        return pfs_err_bad_data;
    }

    std::printf("Controller Pak: saved 40 Winks data to %s\n",
        controller_pak.path.c_str());
    return pfs_ok;
}

} // namespace

extern "C" void dispatch_edl_decode(uint8_t* rdram, recomp_context* ctx) {
    // The original function changes $ra in two JAL delay slots so each decoder
    // returns directly to the epilogue. Direct C calls cannot express that flow.
    ctx->r29 = ADD32(ctx->r29, -0x18);
    MEM_W(0x10, ctx->r29) = ctx->r16;
    MEM_W(0x14, ctx->r29) = ctx->r31;

    ctx->r16 = ADD32(ctx->r4, 0);
    func_80013A7C(rdram, ctx);

    if (MEM_W(0x1C, ctx->r16) == 0) {
        const gpr encoding = MEM_W(0x10, ctx->r16);
        ctx->r3 = encoding;
        ctx->r4 = ADD32(ctx->r16, 0);
        ctx->r2 = ADD32(0, 3);

        if (encoding == 0) {
            ctx->r31 = ADD32(ctx->r31, 0x0C);
            func_80013988(rdram, ctx);
        } else if (encoding == 3) {
            ctx->r4 = MEM_W(0x04, ctx->r16);
            ctx->r5 = MEM_W(0x00, ctx->r16);
            func_8003ADE4(rdram, ctx);
        } else {
            ctx->r31 = ADD32(ctx->r31, 0x14);
            func_80012EF0(rdram, ctx);
        }
    }

    ctx->r31 = MEM_W(0x14, ctx->r29);
    ctx->r16 = MEM_W(0x10, ctx->r29);
    ctx->r29 = ADD32(ctx->r29, 0x18);
}

extern "C" void configure_controller_pak_storage(const char* data_path) {
    std::lock_guard lock{controller_pak_mutex};
    controller_pak = {};
    controller_pak.path = std::filesystem::path{data_path} / "40-winks-controller-pak.note";

    if (load_controller_pak_note()) {
        std::printf("Controller Pak: loaded persistent note %s\n",
            controller_pak.path.c_str());
    } else {
        std::printf("Controller Pak: ready for a new save at %s\n",
            controller_pak.path.c_str());
    }
}

extern "C" void activate_virtual_controller_accessory(uint8_t* rdram,
                                                        recomp_context* ctx) {
    std::lock_guard lock{controller_pak_mutex};
    int controller = static_cast<int8_t>(read_u8(rdram, 0x8011D2DE));
    if (controller < 0 || controller >= 4) {
        controller = 0;
    }

    const uint32_t controller_index = static_cast<uint32_t>(controller);
    const uint32_t pfs_address = 0x8011D138 + controller_index * 0x68;
    const int result = initialize_controller_pak(rdram, pfs_address, controller);
    if (result == pfs_ok) {
        write_u8(rdram, 0x801276B4 + controller_index, 1);
        write_u8(rdram, 0x80128C2C + controller_index, 1);
        // The original detector clears this transient menu result before probing.
        // Leaving it nonzero makes the save UI report that no pak is inserted.
        write_u8(rdram, save_menu_choice_address, 0);
        write_u32(rdram, save_result_address, pfs_ok);
        write_u8(rdram, 0x8009133F, 0);
        ctx->r2 = 1;
    } else {
        write_u32(rdram, save_result_address, static_cast<uint32_t>(result));
        ctx->r2 = 0;
    }
}

extern "C" int dispatch_controller_pak_command(uint8_t* rdram, recomp_context* ctx) {
    (void)ctx;
    const uint8_t command = read_u8(rdram, save_command_address);
    if (command < 3 || command > 12) {
        return 0;
    }

    std::lock_guard lock{controller_pak_mutex};
    std::array<uint32_t, 7> arguments{};
    for (size_t index = 0; index < arguments.size(); ++index) {
        arguments[index] = read_u32(rdram,
            save_argument_address + static_cast<uint32_t>(index * sizeof(uint32_t)));
    }

    int result = pfs_err_invalid;
    switch (command) {
        case 3: // osPfsNumFiles
            if (valid_rdram_range(arguments[1], sizeof(uint32_t)) &&
                valid_rdram_range(arguments[2], sizeof(uint32_t))) {
                write_u32(rdram, arguments[1], controller_pak_file_count);
                write_u32(rdram, arguments[2], controller_pak.present ? 1 : 0);
                result = pfs_ok;
            }
            break;
        case 4: // osPfsFreeBlocks
            if (valid_rdram_range(arguments[1], sizeof(uint32_t))) {
                write_u32(rdram, arguments[1], controller_pak_capacity -
                    (controller_pak.present ? save_payload_size : 0));
                result = pfs_ok;
            }
            break;
        case 5: // osPfsFileState
            result = write_file_state(rdram, static_cast<int>(arguments[1]), arguments[2]);
            break;
        case 6: // osPfsDeleteFile
            if (metadata_matches(rdram, static_cast<uint16_t>(arguments[1]), arguments[2],
                                 arguments[3], arguments[4])) {
                if (remove_controller_pak_note()) {
                    controller_pak.present = false;
                    controller_pak.game_name.fill(0);
                    controller_pak.extension.fill(0);
                    controller_pak.payload.fill(0);
                    std::printf("Controller Pak: deleted 40 Winks save data.\n");
                    result = pfs_ok;
                } else {
                    result = pfs_err_bad_data;
                }
            }
            break;
        case 7: // osPfsInitPak
            result = initialize_controller_pak(rdram, arguments[0],
                static_cast<int>(arguments[1]));
            break;
        case 8: // osPfsChecker
            result = pfs_ok;
            break;
        case 9: // osPfsReadWriteFile
            result = read_write_note(rdram, static_cast<int>(arguments[1]),
                static_cast<uint8_t>(arguments[2]), static_cast<int>(arguments[3]),
                static_cast<int>(arguments[4]), arguments[5]);
            break;
        case 10: { // osPfsAllocateFile
            if (!valid_rdram_range(arguments[6], sizeof(uint32_t))) {
                break;
            }
            write_u32(rdram, arguments[6], UINT32_MAX);
            if (arguments[1] == 0 || arguments[2] == 0 || arguments[5] == 0) {
                break;
            }
            if (arguments[5] > save_payload_size) {
                result = pfs_data_full;
                break;
            }
            if (controller_pak.present) {
                if (metadata_matches(rdram, static_cast<uint16_t>(arguments[1]),
                                     arguments[2], arguments[3], arguments[4])) {
                    write_u32(rdram, arguments[6], 0);
                    result = pfs_err_exist;
                } else {
                    result = pfs_dir_full;
                }
                break;
            }
            if (static_cast<uint16_t>(arguments[1]) != company_code ||
                arguments[2] != game_code ||
                !read_bytes(rdram, arguments[3], controller_pak.game_name) ||
                !read_bytes(rdram, arguments[4], controller_pak.extension)) {
                break;
            }
            controller_pak.present = true;
            controller_pak.payload.fill(0);
            if (!persist_controller_pak_note()) {
                controller_pak.present = false;
                result = pfs_err_bad_data;
                break;
            }
            write_u32(rdram, arguments[6], 0);
            std::printf("Controller Pak: allocated the 40 Winks save note.\n");
            result = pfs_ok;
            break;
        }
        case 11: // osPfsFindFile
            if (valid_rdram_range(arguments[5], sizeof(uint32_t))) {
                if (metadata_matches(rdram, static_cast<uint16_t>(arguments[1]),
                                     arguments[2], arguments[3], arguments[4])) {
                    write_u32(rdram, arguments[5], 0);
                    result = pfs_ok;
                } else {
                    write_u32(rdram, arguments[5], UINT32_MAX);
                }
            }
            break;
        case 12: // osMotorInit
            // Force the original accessory detector to try osPfsInitPak next.
            // initialize_controller_pak then enables both storage and rumble.
            result = pfs_err_device;
            break;
    }

    write_u32(rdram, save_result_address, static_cast<uint32_t>(result));
    return 1;
}
