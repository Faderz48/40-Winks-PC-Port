#pragma once

#include "recomp.h"

#ifdef __cplusplus
extern "C" {
#endif

void yield_self(uint8_t* rdram);
void dispatch_edl_decode(uint8_t* rdram, recomp_context* ctx);
void configure_controller_pak_storage(const char* data_path);
void activate_virtual_controller_accessory(uint8_t* rdram, recomp_context* ctx);
int dispatch_controller_pak_command(uint8_t* rdram, recomp_context* ctx);
void dispatch_debug_level_command(uint8_t* rdram, recomp_context* ctx);

#ifdef __cplusplus
}
#endif
