#include "settings_window.h"

namespace eas {
namespace {

constexpr wchar_t kWindowClass[] = L"FoobarEasyAudioSettingsWindow";

enum ControlId : int {
    IDC_TITLE = 100,
    IDC_TRACK,
    IDC_TECH,
    IDC_PREV,
    IDC_PLAYPAUSE,
    IDC_NEXT,
    IDC_STOP,
    IDC_VOLUME,
    IDC_VOLUME_TEXT,
    IDC_ORDER,
    IDC_OUTPUT,
    IDC_DECODER,
    IDC_DECODER_NOTE,
    IDC_PREFS,
    IDC_DECODING_PREFS,
    IDC_REFRESH,
    IDC_SECTION_PLAY,
    IDC_SECTION_OUTPUT,
    IDC_SECTION_DECODER
};

struct OutputChoice {
    std::wstring name;
    GUID output{};
    GUID device{};
    bool current = false;
};

struct DecoderChoice {
    std::wstring name;
    GUID guid{};
    GUID preferences{};
    bool current = false;
};

HWND g_window = nullptr;
HFONT g_font = nullptr;
HFONT g_font_bold = nullptr;
HBRUSH g_background = nullptr;
HBRUSH g_panel = nullptr;

std::vector<OutputChoice> g_outputs;
std::vector<DecoderChoice> g_decoders;
std::string g_last_decoder_path;

COLORREF kBg = RGB(243, 246, 247);       // #F3F6F7
COLORREF kPanel = RGB(250, 251, 251);    // #FAFBFB
COLORREF kText = RGB(51, 68, 75);        // #33444B
COLORREF kMuted = RGB(120, 136, 142);    // #78888E

HWND ctl(int id) {
    return g_window ? GetDlgItem(g_window, id) : nullptr;
}

void set_text(int id, const std::wstring& text) {
    if (HWND h = ctl(id)) SetWindowTextW(h, text.c_str());
}

std::wstring playback_format(const char* script) {
    try {
        titleformat_object::ptr obj;
        titleformat_compiler::get()->compile_safe(obj, script);
        pfc::string8 out;
        if (!playback_control::get()->playback_format_title(
                nullptr, out, obj, nullptr, playback_control::display_level_all)) {
            return L"—";
        }
        auto w = utf8_to_wide(out.c_str());
        return w.empty() ? L"—" : w;
    } catch (...) {
        return L"—";
    }
}

void open_preferences() {
    HWND main = core_api::get_main_window();
    if (main) SetForegroundWindow(main);
    MessageBoxW(
        g_window,
        L"foobar2000 高级首选项快捷键：Ctrl+P\n\n"
        L"输出：Playback → Output\n"
        L"解码器优先级：Playback → Decoding\n"
        L"DSP：Playback → DSP Manager\n"
        L"ReplayGain：Playback",
        L"高级设置导航",
        MB_OK | MB_ICONINFORMATION
    );
}

void open_decoding_preferences() {
    open_preferences();
    MessageBoxW(
        g_window,
        L"在首选项左侧进入：\n\nPlayback → Decoding\n\n"
        L"foobar2000 的 Decoder Priority 由核心统一管理。"
        L"本组件会显示当前实际解码器和可用解码器，但不会伪造一个不生效的切换选项。",
        L"解码器优先级",
        MB_OK | MB_ICONINFORMATION
    );
}

void refresh_track() {
    auto pc = playback_control::get();

    if (!pc->is_playing()) {
        set_text(IDC_TRACK, L"当前没有播放音乐");
        set_text(IDC_TECH, L"选择一首歌曲后，这里会显示格式、采样率、位深和码率。");
    } else {
        std::wstring title = playback_format("$if2(%title%,%filename%)");
        std::wstring artist = playback_format("$if2(%artist%,未知艺术家)");
        std::wstring album = playback_format("$if2(%album%,未知专辑)");
        set_text(IDC_TRACK, title + L"  ·  " + artist + L"  ·  " + album);

        std::wstring codec = playback_format("$if2($info(codec),未知格式)");
        std::wstring sr = playback_format("$if2($info(samplerate),?)");
        std::wstring bps = playback_format("$if2($info(bitspersample),?)");
        std::wstring br = playback_format("$if2($info(bitrate),?)");
        set_text(IDC_TECH, codec + L"  ·  " + sr + L" Hz  ·  " + bps + L" bit  ·  " + br + L" kbps");
    }

    set_text(IDC_PLAYPAUSE, pc->is_playing() && !pc->is_paused() ? L"暂停" : L"播放");

    float db = pc->get_volume();
    int pos = 0;
    if (db > -100.0f) {
        db = std::max(-50.0f, std::min(0.0f, db));
        pos = static_cast<int>(std::lround((db + 50.0f) * 2.0f));
    }
    if (HWND slider = ctl(IDC_VOLUME)) {
        SendMessageW(slider, TBM_SETPOS, TRUE, pos);
    }

    wchar_t buf[64]{};
    if (pc->get_volume() <= -99.0f) {
        wcscpy_s(buf, L"静音");
    } else {
        swprintf_s(buf, L"%.1f dB", pc->get_volume());
    }
    set_text(IDC_VOLUME_TEXT, buf);
}

void refresh_playback_order() {
    HWND combo = ctl(IDC_ORDER);
    if (!combo) return;

    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    try {
        auto pm = playlist_manager::get();
        const t_size count = pm->playback_order_get_count();
        const t_size active = pm->playback_order_get_active();

        for (t_size i = 0; i < count; ++i) {
            std::wstring name = utf8_to_wide(pm->playback_order_get_name(i));
            SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name.c_str()));
        }
        if (active < count) SendMessageW(combo, CB_SETCURSEL, static_cast<WPARAM>(active), 0);
    } catch (...) {
        SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"读取失败"));
        SendMessageW(combo, CB_SETCURSEL, 0, 0);
    }
}

class OutputEnum : public output_device_list_callback {
public:
    void onDevice(const char* fullName, const GUID& output, const GUID& device) override {
        OutputChoice c;
        c.name = utf8_to_wide(fullName);
        c.output = output;
        c.device = device;
        values.push_back(c);
    }
    std::vector<OutputChoice> values;
};

bool get_output_manager_v2(output_manager_v2::ptr& out) {
    try {
        output_manager::ptr base = output_manager::get();
        return base->service_query_t(out);
    } catch (...) {
        return false;
    }
}

void refresh_outputs() {
    HWND combo = ctl(IDC_OUTPUT);
    if (!combo) return;

    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    g_outputs.clear();

    output_manager_v2::ptr manager;
    if (!get_output_manager_v2(manager)) {
        SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"当前版本不支持设备切换 API"));
        SendMessageW(combo, CB_SETCURSEL, 0, 0);
        EnableWindow(combo, FALSE);
        return;
    }

    try {
        OutputEnum cb;
        manager->listDevices(cb);

        outputCoreConfig_t config{};
        output_manager::get()->getCoreConfig(config);

        g_outputs = std::move(cb.values);
        int currentIndex = -1;

        for (size_t i = 0; i < g_outputs.size(); ++i) {
            auto& c = g_outputs[i];
            c.current = IsEqualGUID(c.output, config.m_output) && IsEqualGUID(c.device, config.m_device);
            std::wstring display = c.current ? L"✓ " + c.name : c.name;
            SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(display.c_str()));
            if (c.current) currentIndex = static_cast<int>(i);
        }

        if (g_outputs.empty()) {
            SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"未找到输出设备"));
            currentIndex = 0;
        }
        if (currentIndex < 0 && !g_outputs.empty()) currentIndex = 0;
        SendMessageW(combo, CB_SETCURSEL, currentIndex, 0);
        EnableWindow(combo, !g_outputs.empty());
    } catch (...) {
        SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"读取输出设备失败"));
        SendMessageW(combo, CB_SETCURSEL, 0, 0);
        EnableWindow(combo, FALSE);
    }
}

std::string extension_from_path(const char* path) {
    if (!path) return {};
    const char* lastSlash1 = strrchr(path, '/');
    const char* lastSlash2 = strrchr(path, '\\');
    const char* slash = lastSlash1 > lastSlash2 ? lastSlash1 : lastSlash2;
    const char* dot = strrchr(path, '.');
    if (!dot || (slash && dot < slash) || dot[1] == 0) return {};
    return std::string(dot + 1);
}

void refresh_decoders() {
    HWND combo = ctl(IDC_DECODER);
    if (!combo) return;
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    g_decoders.clear();

    metadb_handle_ptr now;
    if (!playback_control::get()->get_now_playing(now)) {
        g_last_decoder_path.clear();
        SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"自动（等待播放文件）"));
        SendMessageW(combo, CB_SETCURSEL, 0, 0);
        return;
    }

    const char* path = now->get_path();
    if (!path || !*path) {
        g_last_decoder_path.clear();
        SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"自动"));
        SendMessageW(combo, CB_SETCURSEL, 0, 0);
        return;
    }

    g_last_decoder_path = path;

    GUID used = pfc::guid_null;
    try {
        // Ask the core which decoder it would choose under the user's Decoder Priority.
        auto probe = input_manager::get()->open(
            input_decoder::class_guid, file::ptr(), path, false, fb2k::noAbort, &used);
        (void)probe;
    } catch (...) {
        used = pfc::guid_null;
    }

    try {
        input_manager::ptr base = input_manager::get();
        input_manager_v3::ptr manager3;
        if (!base->service_query_t(manager3)) {
            SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"自动（核心管理）"));
            SendMessageW(combo, CB_SETCURSEL, 0, 0);
            return;
        }

        pfc::list_t<input_entry::ptr> enabled;
        manager3->get_enabled_inputs(enabled);
        const std::string ext = extension_from_path(path);

        int currentIndex = -1;
        for (t_size i = 0; i < enabled.get_count(); ++i) {
            input_entry::ptr entry = enabled[i];
            bool supports = false;
            try {
                supports = entry->is_our_path(path, ext.c_str());
            } catch (...) {
                supports = false;
            }
            if (!supports) continue;

            input_entry_v2::ptr v2;
            if (!entry->service_query_t(v2)) continue;

            DecoderChoice d;
            d.name = utf8_to_wide(v2->get_name());
            d.guid = v2->get_guid();
            d.preferences = v2->get_preferences_guid();
            d.current = !IsEqualGUID(used, pfc::guid_null) && IsEqualGUID(used, d.guid);

            const int index = static_cast<int>(g_decoders.size());
            g_decoders.push_back(d);

            std::wstring display = d.current ? L"✓ " + d.name : d.name;
            SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(display.c_str()));
            if (d.current) currentIndex = index;
        }

        if (g_decoders.empty()) {
            SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"自动（由 foobar2000 核心选择）"));
            currentIndex = 0;
        } else if (currentIndex < 0) {
            currentIndex = 0;
        }
        SendMessageW(combo, CB_SETCURSEL, currentIndex, 0);
    } catch (...) {
        SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"自动（读取解码器失败）"));
        SendMessageW(combo, CB_SETCURSEL, 0, 0);
    }
}

void refresh_decoder_if_track_changed() {
    metadb_handle_ptr now;
    std::string path;
    if (playback_control::get()->get_now_playing(now) && now.is_valid() && now->get_path()) {
        path = now->get_path();
    }
    if (path != g_last_decoder_path) {
        refresh_decoders();
    }
}

void refresh_all() {
    refresh_track();
    refresh_playback_order();
    refresh_outputs();
    refresh_decoders();
}

HWND make_static(HWND parent, int id, const wchar_t* text, int x, int y, int w, int h, DWORD extra = 0) {
    HWND c = CreateWindowExW(
        0, L"STATIC", text,
        WS_CHILD | WS_VISIBLE | SS_LEFT | extra,
        x, y, w, h, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        core_api::get_my_instance(), nullptr);
    SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
    return c;
}

HWND make_button(HWND parent, int id, const wchar_t* text, int x, int y, int w, int h) {
    HWND c = CreateWindowExW(
        0, L"BUTTON", text,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        x, y, w, h, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        core_api::get_my_instance(), nullptr);
    SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
    return c;
}

HWND make_combo(HWND parent, int id, int x, int y, int w, int h) {
    HWND c = CreateWindowExW(
        0, WC_COMBOBOXW, L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
        x, y, w, h, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        core_api::get_my_instance(), nullptr);
    SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
    return c;
}

void create_controls(HWND hwnd) {
    make_static(hwnd, IDC_TITLE, L"音频设置", 30, 22, 520, 36);
    SendMessageW(
        ctl(IDC_TITLE),
        WM_SETFONT,
        reinterpret_cast<WPARAM>(g_font_bold),
        TRUE);

    make_static(
        hwnd, IDC_TRACK,
        L"当前没有播放音乐",
        30, 64, 700, 24);

    make_static(
        hwnd, IDC_TECH,
        L"格式 / 采样率 / 位深 / 码率",
        30, 90, 700, 22);

    // Card 1: playback mode + volume
    make_static(
        hwnd, IDC_SECTION_PLAY,
        L"播放与音量",
        46, 137, 160, 24);

    make_static(
        hwnd, -1,
        L"播放模式",
        48, 176, 82, 22);

    make_combo(
        hwnd, IDC_ORDER,
        136, 169, 250, 250);

    make_static(
        hwnd, -1,
        L"音量",
        418, 176, 46, 22);

    HWND volume = CreateWindowExW(
        0, TRACKBAR_CLASSW, L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP |
        TBS_HORZ | TBS_NOTICKS,
        468, 165, 205, 38,
        hwnd,
        reinterpret_cast<HMENU>(
            static_cast<INT_PTR>(IDC_VOLUME)),
        core_api::get_my_instance(),
        nullptr);

    SendMessageW(
        volume, TBM_SETRANGE,
        TRUE, MAKELPARAM(0, 100));

    make_static(
        hwnd, IDC_VOLUME_TEXT,
        L"0.0 dB",
        681, 176, 70, 22);

    // Card 2: output
    make_static(
        hwnd, IDC_SECTION_OUTPUT,
        L"输出设备",
        46, 249, 160, 24);

    make_combo(
        hwnd, IDC_OUTPUT,
        48, 284, 682, 300);

    // Card 3: decoder
    make_static(
        hwnd, IDC_SECTION_DECODER,
        L"解码器",
        46, 355, 160, 24);

    make_combo(
        hwnd, IDC_DECODER,
        48, 390, 682, 300);

    make_static(
        hwnd, IDC_DECODER_NOTE,
        L"当前列表显示能处理正在播放文件的 Input Decoder；"
        L"最终优先级仍由 foobar2000 Playback → Decoding 管理。",
        48, 426, 682, 40);

    make_button(
        hwnd, IDC_DECODING_PREFS,
        L"解码器优先级…",
        30, 486, 150, 36);

    make_button(
        hwnd, IDC_PREFS,
        L"高级首选项…",
        192, 486, 132, 36);

    make_button(
        hwnd, IDC_REFRESH,
        L"刷新",
        336, 486, 90, 36);
}

void apply_output_selection() {
    HWND combo = ctl(IDC_OUTPUT);
    if (!combo) return;
    const int index = static_cast<int>(SendMessageW(combo, CB_GETCURSEL, 0, 0));
    if (index < 0 || static_cast<size_t>(index) >= g_outputs.size()) return;

    output_manager_v2::ptr manager;
    if (!get_output_manager_v2(manager)) return;

    try {
        const auto& c = g_outputs[static_cast<size_t>(index)];
        manager->setCoreConfigDevice(c.output, c.device);
        refresh_outputs();
    } catch (const std::exception& e) {
        std::wstring msg = L"切换输出设备失败：\n";
        msg += utf8_to_wide(e.what());
        MessageBoxW(g_window, msg.c_str(), L"输出设备", MB_OK | MB_ICONERROR);
    } catch (...) {
        MessageBoxW(g_window, L"切换输出设备失败。", L"输出设备", MB_OK | MB_ICONERROR);
    }
}

void apply_order_selection() {
    HWND combo = ctl(IDC_ORDER);
    if (!combo) return;
    const LRESULT index = SendMessageW(combo, CB_GETCURSEL, 0, 0);
    if (index == CB_ERR) return;
    try {
        auto pm = playlist_manager::get();
        if (static_cast<t_size>(index) < pm->playback_order_get_count()) {
            pm->playback_order_set_active(static_cast<t_size>(index));
        }
    } catch (...) {}
}

void show_decoder_selection_info() {
    HWND combo = ctl(IDC_DECODER);
    if (!combo) return;
    const int index = static_cast<int>(SendMessageW(combo, CB_GETCURSEL, 0, 0));
    if (index < 0 || static_cast<size_t>(index) >= g_decoders.size()) return;
    const auto& d = g_decoders[static_cast<size_t>(index)];
    if (d.current) return;

    MessageBoxW(
        g_window,
        (L"你选择查看的是：\n\n" + d.name +
         L"\n\n它确实可以处理当前文件，但 foobar2000 公共 SDK 没有通用的 Decoder Priority 写入接口。"
         L"\n点击“解码器优先级…”可在 foobar2000 原生页面把它调到更高优先级。").c_str(),
        L"解码器",
        MB_OK | MB_ICONINFORMATION
    );
}


void draw_card(HDC dc, const RECT& r) {
    HBRUSH brush = CreateSolidBrush(kPanel);
    HPEN pen = CreatePen(PS_SOLID, 1, RGB(222, 229, 232));

    HGDIOBJ oldBrush = SelectObject(dc, brush);
    HGDIOBJ oldPen = SelectObject(dc, pen);

    RoundRect(
        dc, r.left, r.top,
        r.right, r.bottom,
        18, 18);

    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);

    DeleteObject(pen);
    DeleteObject(brush);
}

void paint_settings_background(HWND hwnd, HDC dc) {
    RECT r{};
    GetClientRect(hwnd, &r);
    FillRect(dc, &r, g_background);

    RECT accentLine{ 30, 116, 104, 119 };
    HBRUSH accent = CreateSolidBrush(RGB(111, 145, 159));
    FillRect(dc, &accentLine, accent);
    DeleteObject(accent);

    draw_card(dc, RECT{ 26, 126, 752, 222 });
    draw_card(dc, RECT{ 26, 238, 752, 330 });
    draw_card(dc, RECT{ 26, 344, 752, 475 });
}

LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE:
        g_window = hwnd;
        g_background = CreateSolidBrush(kBg);
        g_panel = CreateSolidBrush(kPanel);

        g_font = CreateFontW(
            -16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");

        g_font_bold = CreateFontW(
            -25, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");

        create_controls(hwnd);
        SetTimer(hwnd, 1, 1000, nullptr);
        refresh_all();
        return 0;

    case WM_TIMER:
        refresh_track();
        refresh_decoder_if_track_changed();
        return 0;

    case WM_COMMAND: {
        const int id = LOWORD(wp);
        const int code = HIWORD(wp);
        auto pc = playback_control::get();

        if (id == IDC_PREV && code == BN_CLICKED) pc->previous();
        else if (id == IDC_PLAYPAUSE && code == BN_CLICKED) pc->play_or_pause();
        else if (id == IDC_NEXT && code == BN_CLICKED) pc->next();
        else if (id == IDC_STOP && code == BN_CLICKED) pc->stop();
        else if (id == IDC_PREFS && code == BN_CLICKED) open_preferences();
        else if (id == IDC_DECODING_PREFS && code == BN_CLICKED) open_decoding_preferences();
        else if (id == IDC_REFRESH && code == BN_CLICKED) refresh_all();
        else if (id == IDC_ORDER && code == CBN_SELCHANGE) apply_order_selection();
        else if (id == IDC_OUTPUT && code == CBN_SELCHANGE) apply_output_selection();
        else if (id == IDC_DECODER && code == CBN_SELCHANGE) show_decoder_selection_info();

        refresh_track();
        return 0;
    }

    case WM_HSCROLL:
        if (reinterpret_cast<HWND>(lp) == ctl(IDC_VOLUME)) {
            const int pos = static_cast<int>(SendMessageW(ctl(IDC_VOLUME), TBM_GETPOS, 0, 0));
            const float db = static_cast<float>(pos) / 2.0f - 50.0f;
            try {
                playback_control::get()->set_volume(db);
            } catch (...) {}
            refresh_track();
            return 0;
        }
        break;

    case WM_CTLCOLORSTATIC: {
        HDC dc = reinterpret_cast<HDC>(wp);
        HWND child = reinterpret_cast<HWND>(lp);
        const int id = GetDlgCtrlID(child);

        SetTextColor(dc, kText);

        if (id == IDC_TITLE ||
            id == IDC_TRACK ||
            id == IDC_TECH) {
            SetBkColor(dc, kBg);
            return reinterpret_cast<LRESULT>(g_background);
        }

        SetBkColor(dc, kPanel);
        return reinterpret_cast<LRESULT>(g_panel);
    }

    case WM_CTLCOLORBTN: {
        HDC dc = reinterpret_cast<HDC>(wp);
        SetTextColor(dc, kText);
        SetBkColor(dc, kPanel);
        return reinterpret_cast<LRESULT>(g_panel);
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(hwnd, &ps);
        paint_settings_background(hwnd, dc);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        KillTimer(hwnd, 1);
        if (g_font) { DeleteObject(g_font); g_font = nullptr; }
        if (g_font_bold) { DeleteObject(g_font_bold); g_font_bold = nullptr; }
        if (g_background) { DeleteObject(g_background); g_background = nullptr; }
        if (g_panel) { DeleteObject(g_panel); g_panel = nullptr; }
        g_outputs.clear();
        g_decoders.clear();
        g_last_decoder_path.clear();
        g_window = nullptr;
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wp, lp);
}

void register_window_class() {
    static bool done = false;
    if (done) return;

    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_BAR_CLASSES | ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = core_api::get_my_instance();
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kWindowClass;
    wc.style = 0;

    RegisterClassExW(&wc);
    done = true;
}

} // namespace

void show_audio_settings_window() {
    core_api::ensure_main_thread();

    if (g_window && IsWindow(g_window)) {
        ShowWindow(g_window, SW_RESTORE);
        SetForegroundWindow(g_window);
        refresh_all();
        return;
    }

    register_window_class();

    HWND owner = core_api::get_main_window();
    g_window = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        kWindowClass,
        L"音频设置 · Easy Audio Settings",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 800, 570,
        owner, nullptr, core_api::get_my_instance(), nullptr);

    if (!g_window) {
        MessageBoxW(owner, L"无法创建音频设置窗口。", L"Easy Audio Settings", MB_OK | MB_ICONERROR);
        return;
    }

    ShowWindow(g_window, SW_SHOW);
    UpdateWindow(g_window);
}

} // namespace eas
