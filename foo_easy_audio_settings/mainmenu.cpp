#include "common.h"

namespace {

static const GUID guid_easy_audio_settings =
{ 0x73dd7242, 0xb0f3, 0x48a1, { 0x90, 0x74, 0xa8, 0x19, 0x5b, 0x0f, 0x64, 0x4c } };

class mainmenu_easy_audio_settings : public mainmenu_commands {
public:
    t_uint32 get_command_count() override {
        return 1;
    }

    GUID get_command(t_uint32 index) override {
        if (index != 0) return pfc::guid_null;
        return guid_easy_audio_settings;
    }

    void get_name(t_uint32 index, pfc::string_base& out) override {
        if (index == 0) out = u8"音频设置";
    }

    bool get_description(t_uint32 index, pfc::string_base& out) override {
        if (index != 0) return false;
        out = u8"打开易用音频设置：输出设备、播放模式、音量和解码信息";
        return true;
    }

    GUID get_parent() override {
        return mainmenu_groups::view;
    }

    t_uint32 get_sort_priority() override {
        return mainmenu_commands::sort_priority_base;
    }

    void execute(t_uint32 index, service_ptr_t<service_base>) override {
        if (index == 0) eas::show_audio_settings_window();
    }
};

static mainmenu_commands_factory_t<mainmenu_easy_audio_settings> g_mainmenu_factory;

} // namespace
