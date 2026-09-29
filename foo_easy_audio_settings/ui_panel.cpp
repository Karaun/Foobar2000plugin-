#include "common.h"

namespace eas {
namespace {

static const GUID guid_audio_control_panel =
{ 0x2bfcf7a1, 0x62ad, 0x4f77, { 0x9c, 0x4a, 0x21, 0x3e, 0x91, 0xb5, 0x44, 0x71 } };

constexpr wchar_t kPanelWindowClass[] =
    L"FoobarEasyAudioSettingsDUIControlPanel";

enum PanelControlId : int {
    IDC_PANEL_TITLE = 2200,
    IDC_PANEL_TRACK,
    IDC_PANEL_TECH,
    IDC_PANEL_ORDER,
    IDC_PANEL_VOLUME,
    IDC_PANEL_VOLUME_TEXT,
    IDC_PANEL_OUTPUT,
    IDC_PANEL_DECODER,
    IDC_PANEL_MORE,
    IDC_PANEL_REFRESH
};

struct PanelOutputChoice {
    std::wstring name;
    GUID output{};
    GUID device{};
    bool current = false;
};

class OutputEnumPanel : public output_device_list_callback {
public:
    void onDevice(
        const char* fullName,
        const GUID& output,
        const GUID& device) override {

        PanelOutputChoice c;
        c.name = utf8_to_wide(fullName);
        c.output = output;
        c.device = device;
        values.push_back(c);
    }

    std::vector<PanelOutputChoice> values;
};

std::wstring playback_format_panel(const char* script) {
    try {
        titleformat_object::ptr obj;
        titleformat_compiler::get()->compile_safe(obj, script);

        pfc::string8 out;

        if (!playback_control::get()->playback_format_title(
                nullptr, out, obj, nullptr,
                playback_control::display_level_all)) {
            return L"—";
        }

        auto w = utf8_to_wide(out.c_str());
        return w.empty() ? L"—" : w;
    } catch (...) {
        return L"—";
    }
}

std::string extension_from_path_panel(const char* path) {
    if (!path) return {};

    const char* slash1 = strrchr(path, '/');
    const char* slash2 = strrchr(path, '\\');
    const char* slash = slash1 > slash2 ? slash1 : slash2;
    const char* dot = strrchr(path, '.');

    if (!dot || (slash && dot < slash) || dot[1] == 0)
        return {};

    return std::string(dot + 1);
}

static COLORREF morandiAccent() { return RGB(111, 145, 159); }
static COLORREF morandiDeep() { return RGB(79, 112, 126); }
static COLORREF morandiMuted() { return RGB(116, 133, 140); }
static COLORREF cardBorder() { return RGB(219, 227, 230); }
static COLORREF cardFill() { return RGB(250, 251, 251); }

class audio_control_panel_instance : public ui_element_instance {
public:
    audio_control_panel_instance(
        HWND parent,
        ui_element_config::ptr cfg,
        ui_element_instance_callback_ptr callback)
        : m_callback(callback) {

        (void)cfg;

        register_class();

        m_hwnd = CreateWindowExW(
            0, kPanelWindowClass, L"",
            WS_CHILD | WS_VISIBLE |
            WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
            0, 0, 360, 350,
            parent, nullptr,
            core_api::get_my_instance(), this);

        if (!m_hwnd)
            throw std::runtime_error(
                "Could not create Easy Audio Settings DUI panel");
    }

    ~audio_control_panel_instance() {
        if (m_hwnd && IsWindow(m_hwnd))
            DestroyWindow(m_hwnd);

        m_hwnd = nullptr;
    }

    HWND get_wnd() override { return m_hwnd; }
    void set_configuration(ui_element_config::ptr) override {}

    ui_element_config::ptr get_configuration() override {
        return ui_element_config::g_create_empty(
            guid_audio_control_panel);
    }

    GUID get_guid() override { return guid_audio_control_panel; }
    GUID get_subclass() override { return ui_element_subclass_utility; }

    ui_element_min_max_info get_min_max_info() override {
        ui_element_min_max_info info;
        info.m_min_width = 300;
        info.m_min_height = 430;
        return info;
    }

    void notify(
        const GUID& what,
        t_size,
        const void*,
        t_size) override {

        if (what == ui_element_notify_colors_changed ||
            what == ui_element_notify_font_changed) {
            update_theme();
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
    }

private:
    HWND m_hwnd = nullptr;
    ui_element_instance_callback_ptr m_callback;

    HFONT m_font = nullptr;
    HFONT m_font_small = nullptr;
    HFONT m_font_bold = nullptr;

    HBRUSH m_bg_brush = nullptr;
    HBRUSH m_card_brush = nullptr;

    COLORREF m_bg = RGB(243, 246, 247);
    COLORREF m_text = RGB(51, 68, 75);

    std::vector<PanelOutputChoice> m_outputs;
    std::string m_last_path;

    static void register_class() {
        static bool done = false;
        if (done) return;

        INITCOMMONCONTROLSEX icc{
            sizeof(icc),
            ICC_BAR_CLASSES | ICC_STANDARD_CLASSES
        };

        InitCommonControlsEx(&icc);

        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = wnd_proc;
        wc.hInstance = core_api::get_my_instance();
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = kPanelWindowClass;
        wc.style = 0;
        wc.hbrBackground = nullptr;

        RegisterClassExW(&wc);
        done = true;
    }

    HWND ctl(int id) const {
        return m_hwnd ? GetDlgItem(m_hwnd, id) : nullptr;
    }

    void set_text(int id, const std::wstring& text) {
        if (HWND h = ctl(id))
            SetWindowTextW(h, text.c_str());
    }

    HWND make_static(
        int id,
        const wchar_t* text,
        HFONT font = nullptr) {

        HWND c = CreateWindowExW(
            0, L"STATIC", text,
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            0, 0, 10, 10,
            m_hwnd,
            reinterpret_cast<HMENU>(
                static_cast<INT_PTR>(id)),
            core_api::get_my_instance(),
            nullptr);

        SendMessageW(
            c, WM_SETFONT,
            reinterpret_cast<WPARAM>(
                font ? font : m_font),
            TRUE);

        return c;
    }

    HWND make_button(
        int id,
        const wchar_t* text) {

        HWND c = CreateWindowExW(
            0, L"BUTTON", text,
            WS_CHILD | WS_VISIBLE |
            WS_TABSTOP | BS_PUSHBUTTON,
            0, 0, 10, 10,
            m_hwnd,
            reinterpret_cast<HMENU>(
                static_cast<INT_PTR>(id)),
            core_api::get_my_instance(),
            nullptr);

        SendMessageW(
            c, WM_SETFONT,
            reinterpret_cast<WPARAM>(m_font),
            TRUE);

        return c;
    }

    HWND make_combo(int id) {
        HWND c = CreateWindowExW(
            0, WC_COMBOBOXW, L"",
            WS_CHILD | WS_VISIBLE |
            WS_TABSTOP |
            CBS_DROPDOWNLIST |
            WS_VSCROLL,
            0, 0, 10, 300,
            m_hwnd,
            reinterpret_cast<HMENU>(
                static_cast<INT_PTR>(id)),
            core_api::get_my_instance(),
            nullptr);

        SendMessageW(
            c, WM_SETFONT,
            reinterpret_cast<WPARAM>(m_font),
            TRUE);

        return c;
    }

    void update_theme() {
        COLORREF bg = RGB(243, 246, 247);
        COLORREF text = RGB(51, 68, 75);

        if (m_callback.is_valid()) {
            bg = static_cast<COLORREF>(
                m_callback->query_std_color(
                    ui_color_background));

            text = static_cast<COLORREF>(
                m_callback->query_std_color(
                    ui_color_text));
        }

        m_bg = bg;
        m_text = text;

        if (m_bg_brush)
            DeleteObject(m_bg_brush);

        if (m_card_brush)
            DeleteObject(m_card_brush);

        m_bg_brush =
            CreateSolidBrush(m_bg);

        m_card_brush =
            CreateSolidBrush(cardFill());
    }

    void create_controls() {
        m_font = CreateFontW(
            -14, 0, 0, 0,
            FW_NORMAL,
            FALSE, FALSE, FALSE,
            DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE,
            L"Microsoft YaHei UI");

        m_font_small = CreateFontW(
            -12, 0, 0, 0,
            FW_NORMAL,
            FALSE, FALSE, FALSE,
            DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE,
            L"Microsoft YaHei UI");

        m_font_bold = CreateFontW(
            -18, 0, 0, 0,
            FW_SEMIBOLD,
            FALSE, FALSE, FALSE,
            DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE,
            L"Microsoft YaHei UI");

        update_theme();

        make_static(
            IDC_PANEL_TITLE,
            L"音频设置",
            m_font_bold);

        make_static(
            IDC_PANEL_TRACK,
            L"当前没有播放音乐");

        make_static(
            IDC_PANEL_TECH,
            L"等待播放文件",
            m_font_small);

        make_combo(IDC_PANEL_ORDER);

        HWND volume = CreateWindowExW(
            0, TRACKBAR_CLASSW, L"",
            WS_CHILD | WS_VISIBLE |
            WS_TABSTOP |
            TBS_HORZ | TBS_NOTICKS |
            TBS_TRANSPARENTBKGND,
            0, 0, 10, 30,
            m_hwnd,
            reinterpret_cast<HMENU>(
                static_cast<INT_PTR>(
                    IDC_PANEL_VOLUME)),
            core_api::get_my_instance(),
            nullptr);

        SendMessageW(
            volume, TBM_SETRANGE,
            TRUE, MAKELPARAM(0, 100));

        make_static(
            IDC_PANEL_VOLUME_TEXT,
            L"0.0 dB",
            m_font_small);

        make_combo(IDC_PANEL_OUTPUT);

        make_static(
            IDC_PANEL_DECODER,
            L"自动（等待播放）");

        make_button(
            IDC_PANEL_MORE,
            L"详细设置");

        make_button(
            IDC_PANEL_REFRESH,
            L"刷新");

        SetTimer(m_hwnd, 1, 1000, nullptr);

        layout_controls();
        refresh_all();
    }

    void layout_controls() {
        RECT rc{};
        GetClientRect(m_hwnd, &rc);

        const int w =
            std::max(300, static_cast<int>(rc.right - rc.left));

        const int pad = 14;
        const int inner = w - pad * 2;
        const int cardX = 10;
        const int cardW = w - 20;
        const int cardInnerX = cardX + 16;
        const int cardInnerW = cardW - 32;

        // Header: deliberately separated from all cards.
        MoveWindow(
            ctl(IDC_PANEL_TITLE),
            pad, 12,
            inner, 24,
            TRUE);

        MoveWindow(
            ctl(IDC_PANEL_TRACK),
            pad, 42,
            inner, 19,
            TRUE);

        MoveWindow(
            ctl(IDC_PANEL_TECH),
            pad, 63,
            inner, 17,
            TRUE);

        // Card 1 — playback / volume
        // Labels are painted by paint_panel(); controls begin well below them.
        const int card1Y = 94;
        MoveWindow(
            ctl(IDC_PANEL_ORDER),
            cardInnerX, card1Y + 45,
            std::max(120, cardInnerW), 220,
            TRUE);

        const int sliderY = card1Y + 92;
        const int valueW = 72;
        const int sliderW = std::max(86, cardInnerW - valueW - 12);

        MoveWindow(
            ctl(IDC_PANEL_VOLUME),
            cardInnerX, sliderY,
            sliderW, 28,
            TRUE);

        MoveWindow(
            ctl(IDC_PANEL_VOLUME_TEXT),
            cardInnerX + sliderW + 10,
            sliderY + 3,
            valueW, 20,
            TRUE);

        // Card 2 — output device. No extra label is drawn over the combo.
        const int card2Y = 232;
        MoveWindow(
            ctl(IDC_PANEL_OUTPUT),
            cardInnerX, card2Y + 38,
            std::max(120, cardInnerW), 220,
            TRUE);

        // Card 3 — decoder. The static already contains “当前解码器：…”,
        // so it gets its own dedicated row.
        const int card3Y = 320;
        MoveWindow(
            ctl(IDC_PANEL_DECODER),
            cardInnerX, card3Y + 38,
            std::max(120, cardInnerW), 24,
            TRUE);

        const int btnGap = 8;
        const int btnW = std::max(90, (cardInnerW - btnGap) / 2);
        const int btnY = card3Y + 68;

        MoveWindow(
            ctl(IDC_PANEL_MORE),
            cardInnerX, btnY,
            btnW, 30,
            TRUE);

        MoveWindow(
            ctl(IDC_PANEL_REFRESH),
            cardInnerX + btnW + btnGap,
            btnY,
            btnW, 30,
            TRUE);
    }

    bool get_output_manager_v2(
        output_manager_v2::ptr& out) {

        try {
            output_manager::ptr base =
                output_manager::get();

            return base->service_query_t(out);
        } catch (...) {
            return false;
        }
    }

    void refresh_track() {
        auto pc = playback_control::get();

        if (!pc->is_playing()) {
            set_text(
                IDC_PANEL_TRACK,
                L"当前没有播放音乐");

            set_text(
                IDC_PANEL_TECH,
                L"等待播放文件");

            set_text(
                IDC_PANEL_DECODER,
                L"自动（等待播放）");

            m_last_path.clear();
        } else {
            std::wstring title =
                playback_format_panel(
                    "$if2(%title%,%filename%)");

            std::wstring artist =
                playback_format_panel(
                    "$if2(%artist%,未知艺术家)");

            set_text(
                IDC_PANEL_TRACK,
                title + L"  ·  " + artist);

            std::wstring codec =
                playback_format_panel(
                    "$if2($info(codec),未知格式)");

            std::wstring sr =
                playback_format_panel(
                    "$if2($info(samplerate),?)");

            std::wstring bps =
                playback_format_panel(
                    "$if2($info(bitspersample),?)");

            set_text(
                IDC_PANEL_TECH,
                codec +
                L"  ·  " +
                sr +
                L" Hz  ·  " +
                bps +
                L" bit");
        }

        float db = pc->get_volume();
        int pos = 0;

        if (db > -100.0f) {
            db = std::max(
                -50.0f,
                std::min(0.0f, db));

            pos = static_cast<int>(
                std::lround(
                    (db + 50.0f) * 2.0f));
        }

        if (HWND slider =
                ctl(IDC_PANEL_VOLUME)) {

            SendMessageW(
                slider, TBM_SETPOS,
                TRUE, pos);
        }

        wchar_t buf[64]{};

        if (pc->get_volume() <= -99.0f)
            wcscpy_s(buf, L"静音");
        else
            swprintf_s(
                buf, L"%.1f dB",
                pc->get_volume());

        set_text(
            IDC_PANEL_VOLUME_TEXT,
            buf);
    }

    void refresh_order() {
        HWND combo =
            ctl(IDC_PANEL_ORDER);

        if (!combo) return;

        SendMessageW(
            combo, CB_RESETCONTENT,
            0, 0);

        try {
            auto pm =
                playlist_manager::get();

            const t_size count =
                pm->playback_order_get_count();

            const t_size active =
                pm->playback_order_get_active();

            for (t_size i = 0;
                 i < count;
                 ++i) {

                std::wstring name =
                    utf8_to_wide(
                        pm->playback_order_get_name(i));

                SendMessageW(
                    combo, CB_ADDSTRING,
                    0,
                    reinterpret_cast<LPARAM>(
                        name.c_str()));
            }

            if (active < count) {
                SendMessageW(
                    combo, CB_SETCURSEL,
                    static_cast<WPARAM>(active),
                    0);
            }
        } catch (...) {}
    }

    void apply_order() {
        HWND combo =
            ctl(IDC_PANEL_ORDER);

        if (!combo) return;

        const int index =
            static_cast<int>(
                SendMessageW(
                    combo,
                    CB_GETCURSEL,
                    0, 0));

        if (index < 0) return;

        try {
            playlist_manager::get()
                ->playback_order_set_active(
                    static_cast<t_size>(index));
        } catch (...) {}
    }

    void refresh_outputs() {
        HWND combo =
            ctl(IDC_PANEL_OUTPUT);

        if (!combo) return;

        SendMessageW(
            combo, CB_RESETCONTENT,
            0, 0);

        m_outputs.clear();

        output_manager_v2::ptr manager;

        if (!get_output_manager_v2(manager)) {
            SendMessageW(
                combo, CB_ADDSTRING,
                0,
                reinterpret_cast<LPARAM>(
                    L"输出设备由 foobar2000 管理"));

            SendMessageW(
                combo, CB_SETCURSEL,
                0, 0);

            EnableWindow(combo, FALSE);
            return;
        }

        try {
            OutputEnumPanel cb;
            manager->listDevices(cb);

            outputCoreConfig_t config{};
            output_manager::get()
                ->getCoreConfig(config);

            m_outputs =
                std::move(cb.values);

            int current = -1;

            for (size_t i = 0;
                 i < m_outputs.size();
                 ++i) {

                auto& c = m_outputs[i];

                c.current =
                    IsEqualGUID(
                        c.output,
                        config.m_output) &&
                    IsEqualGUID(
                        c.device,
                        config.m_device);

                std::wstring label =
                    c.current
                    ? L"✓ " + c.name
                    : c.name;

                SendMessageW(
                    combo, CB_ADDSTRING,
                    0,
                    reinterpret_cast<LPARAM>(
                        label.c_str()));

                if (c.current)
                    current =
                        static_cast<int>(i);
            }

            if (m_outputs.empty()) {
                SendMessageW(
                    combo, CB_ADDSTRING,
                    0,
                    reinterpret_cast<LPARAM>(
                        L"未找到输出设备"));

                current = 0;
            }

            if (current < 0 &&
                !m_outputs.empty())
                current = 0;

            SendMessageW(
                combo, CB_SETCURSEL,
                current, 0);

            EnableWindow(
                combo,
                !m_outputs.empty());
        } catch (...) {
            SendMessageW(
                combo, CB_ADDSTRING,
                0,
                reinterpret_cast<LPARAM>(
                    L"读取输出设备失败"));

            SendMessageW(
                combo, CB_SETCURSEL,
                0, 0);
        }
    }

    void refresh_decoder() {
        metadb_handle_ptr now;

        if (!playback_control::get()
                 ->get_now_playing(now) ||
            now.is_empty()) {

            set_text(
                IDC_PANEL_DECODER,
                L"当前解码器：自动（等待播放）");

            return;
        }

        const char* path =
            now->get_path();

        if (!path || !*path) return;
        if (m_last_path == path) return;

        m_last_path = path;

        GUID used = pfc::guid_null;

        try {
            auto probe =
                input_manager::get()->open(
                    input_decoder::class_guid,
                    file::ptr(),
                    path,
                    false,
                    fb2k::noAbort,
                    &used);

            (void)probe;
        } catch (...) {
            used = pfc::guid_null;
        }

        std::wstring name =
            L"自动（核心选择）";

        try {
            input_manager::ptr base =
                input_manager::get();

            input_manager_v3::ptr manager3;

            if (base->service_query_t(
                    manager3)) {

                pfc::list_t<
                    input_entry::ptr> enabled;

                manager3->get_enabled_inputs(
                    enabled);

                const std::string ext =
                    extension_from_path_panel(
                        path);

                for (t_size i = 0;
                     i < enabled.get_count();
                     ++i) {

                    input_entry::ptr entry =
                        enabled[i];

                    if (!entry->is_our_path(
                            path,
                            ext.c_str()))
                        continue;

                    input_entry_v2::ptr v2;

                    if (!entry
                             ->service_query_t(v2))
                        continue;

                    if (!IsEqualGUID(
                            used,
                            pfc::guid_null) &&
                        IsEqualGUID(
                            used,
                            v2->get_guid())) {

                        name =
                            utf8_to_wide(
                                v2->get_name());

                        break;
                    }
                }
            }
        } catch (...) {}

        set_text(
            IDC_PANEL_DECODER,
            L"当前解码器： " + name);
    }

    void refresh_all() {
        refresh_track();
        refresh_order();
        refresh_outputs();

        m_last_path.clear();
        refresh_decoder();
    }

    void apply_output() {
        HWND combo =
            ctl(IDC_PANEL_OUTPUT);

        if (!combo) return;

        int index =
            static_cast<int>(
                SendMessageW(
                    combo,
                    CB_GETCURSEL,
                    0, 0));

        if (index < 0 ||
            static_cast<size_t>(index) >=
                m_outputs.size())
            return;

        output_manager_v2::ptr manager;

        if (!get_output_manager_v2(manager))
            return;

        try {
            const auto& c =
                m_outputs[
                    static_cast<size_t>(
                        index)];

            manager->setCoreConfigDevice(
                c.output,
                c.device);

            refresh_outputs();
        } catch (...) {}
    }

    static void fill_round(
        HDC dc,
        const RECT& r,
        int radius,
        COLORREF fill,
        COLORREF border) {

        HBRUSH brush =
            CreateSolidBrush(fill);

        HPEN pen =
            CreatePen(
                PS_SOLID, 1,
                border);

        HGDIOBJ oldBrush =
            SelectObject(dc, brush);

        HGDIOBJ oldPen =
            SelectObject(dc, pen);

        RoundRect(
            dc,
            r.left, r.top,
            r.right, r.bottom,
            radius, radius);

        SelectObject(dc, oldPen);
        SelectObject(dc, oldBrush);

        DeleteObject(pen);
        DeleteObject(brush);
    }

    static void draw_label(
        HDC dc,
        HFONT font,
        const wchar_t* text,
        RECT r,
        COLORREF color) {

        HGDIOBJ old =
            SelectObject(dc, font);

        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, color);

        DrawTextW(
            dc, text, -1,
            &r,
            DT_LEFT |
            DT_VCENTER |
            DT_SINGLELINE);

        SelectObject(dc, old);
    }

    void paint_panel(HDC dc) {
        RECT rc{};
        GetClientRect(m_hwnd, &rc);

        FillRect(dc, &rc, m_bg_brush);

        const int w = rc.right - rc.left;

        RECT accentLine{ 14, 84, 96, 87 };
        HBRUSH accentBrush = CreateSolidBrush(morandiAccent());
        FillRect(dc, &accentLine, accentBrush);
        DeleteObject(accentBrush);

        // Three independent cards with generous vertical spacing.
        RECT card1{ 10, 94,  w - 10, 222 };
        RECT card2{ 10, 232, w - 10, 310 };
        RECT card3{ 10, 320, w - 10, 424 };

        fill_round(dc, card1, 16, cardFill(), cardBorder());
        fill_round(dc, card2, 16, cardFill(), cardBorder());
        fill_round(dc, card3, 16, cardFill(), cardBorder());

        // Section headings
        draw_label(dc, m_font, L"播放与音量",
            RECT{ 24, 101, 150, 123 }, morandiDeep());
        draw_label(dc, m_font, L"输出设备",
            RECT{ 24, 239, 150, 261 }, morandiDeep());
        draw_label(dc, m_font, L"解码器",
            RECT{ 24, 327, 150, 349 }, morandiDeep());

        // Only Card 1 needs secondary labels. They sit above—not on top of—controls.
        draw_label(dc, m_font_small, L"播放模式",
            RECT{ 24, 121, 100, 139 }, morandiMuted());
        draw_label(dc, m_font_small, L"音量",
            RECT{ 24, 167, 90, 185 }, morandiMuted());
    }

    void paint_buffered(HDC target) {
        RECT rc{};
        GetClientRect(m_hwnd, &rc);

        const int w =
            rc.right - rc.left;

        const int h =
            rc.bottom - rc.top;

        if (w <= 0 || h <= 0)
            return;

        HDC mem =
            CreateCompatibleDC(target);

        HBITMAP bmp =
            CreateCompatibleBitmap(
                target, w, h);

        HGDIOBJ old =
            SelectObject(mem, bmp);

        paint_panel(mem);

        BitBlt(
            target,
            0, 0, w, h,
            mem, 0, 0,
            SRCCOPY);

        SelectObject(mem, old);

        DeleteObject(bmp);
        DeleteDC(mem);
    }

    static LRESULT CALLBACK wnd_proc(
        HWND hwnd,
        UINT msg,
        WPARAM wp,
        LPARAM lp) {

        auto* self =
            reinterpret_cast<
                audio_control_panel_instance*>(
                GetWindowLongPtrW(
                    hwnd,
                    GWLP_USERDATA));

        if (msg == WM_NCCREATE) {
            auto* cs =
                reinterpret_cast<
                    CREATESTRUCTW*>(lp);

            self =
                static_cast<
                    audio_control_panel_instance*>(
                    cs->lpCreateParams);

            SetWindowLongPtrW(
                hwnd,
                GWLP_USERDATA,
                reinterpret_cast<
                    LONG_PTR>(self));

            if (self)
                self->m_hwnd = hwnd;
        }

        if (!self)
            return DefWindowProcW(
                hwnd, msg, wp, lp);

        switch (msg) {
        case WM_CREATE:
            self->create_controls();
            return 0;

        case WM_SIZE:
            self->layout_controls();
            InvalidateRect(
                hwnd, nullptr, FALSE);
            return 0;

        case WM_TIMER:
            self->refresh_track();
            self->refresh_decoder();
            return 0;

        case WM_COMMAND: {
            const int id =
                LOWORD(wp);

            const int code =
                HIWORD(wp);

            if (id == IDC_PANEL_MORE &&
                code == BN_CLICKED) {

                show_audio_settings_window();
            }
            else if (
                id == IDC_PANEL_REFRESH &&
                code == BN_CLICKED) {

                self->refresh_all();
            }
            else if (
                id == IDC_PANEL_OUTPUT &&
                code == CBN_SELCHANGE) {

                self->apply_output();
            }
            else if (
                id == IDC_PANEL_ORDER &&
                code == CBN_SELCHANGE) {

                self->apply_order();
            }

            return 0;
        }

        case WM_HSCROLL:
            if (reinterpret_cast<HWND>(lp) ==
                self->ctl(
                    IDC_PANEL_VOLUME)) {

                int pos =
                    static_cast<int>(
                        SendMessageW(
                            self->ctl(
                                IDC_PANEL_VOLUME),
                            TBM_GETPOS,
                            0, 0));

                float db =
                    static_cast<float>(
                        pos) / 2.0f -
                    50.0f;

                try {
                    playback_control::get()
                        ->set_volume(db);
                } catch (...) {}

                self->refresh_track();
                return 0;
            }
            break;

        case WM_CTLCOLORSTATIC: {
            HDC dc =
                reinterpret_cast<HDC>(wp);

            HWND child =
                reinterpret_cast<HWND>(lp);

            const int id =
                GetDlgCtrlID(child);

            SetTextColor(
                dc, self->m_text);

            if (id == IDC_PANEL_TITLE ||
                id == IDC_PANEL_TRACK ||
                id == IDC_PANEL_TECH) {

                SetBkColor(
                    dc, self->m_bg);

                return reinterpret_cast<
                    LRESULT>(
                    self->m_bg_brush);
            }

            SetBkColor(
                dc, cardFill());

            return reinterpret_cast<
                LRESULT>(
                self->m_card_brush);
        }

        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC dc =
                BeginPaint(
                    hwnd, &ps);

            self->paint_buffered(dc);

            EndPaint(
                hwnd, &ps);

            return 0;
        }

        case WM_DESTROY:
            KillTimer(hwnd, 1);

            if (self->m_font) {
                DeleteObject(
                    self->m_font);

                self->m_font =
                    nullptr;
            }

            if (self->m_font_small) {
                DeleteObject(
                    self->m_font_small);

                self->m_font_small =
                    nullptr;
            }

            if (self->m_font_bold) {
                DeleteObject(
                    self->m_font_bold);

                self->m_font_bold =
                    nullptr;
            }

            if (self->m_bg_brush) {
                DeleteObject(
                    self->m_bg_brush);

                self->m_bg_brush =
                    nullptr;
            }

            if (self->m_card_brush) {
                DeleteObject(
                    self->m_card_brush);

                self->m_card_brush =
                    nullptr;
            }

            return 0;
        }

        return DefWindowProcW(
            hwnd, msg, wp, lp);
    }
};

class audio_control_panel_element :
    public ui_element {
public:
    GUID get_guid() override {
        return guid_audio_control_panel;
    }

    GUID get_subclass() override {
        return ui_element_subclass_utility;
    }

    void get_name(
        pfc::string_base& out) override {
        out = u8"音频设置面板";
    }

    ui_element_instance_ptr instantiate(
        HWND parent,
        ui_element_config::ptr cfg,
        ui_element_instance_callback_ptr callback) override {

        return new service_impl_t<
            audio_control_panel_instance>(
                parent, cfg, callback);
    }

    ui_element_config::ptr
    get_default_configuration()
    override {
        return ui_element_config::
            g_create_empty(
                guid_audio_control_panel);
    }

    ui_element_children_enumerator_ptr
    enumerate_children(
        ui_element_config::ptr)
    override {
        return nullptr;
    }

    bool get_description(
        pfc::string_base& out)
    override {
        out =
            u8"莫兰迪蓝卡片式音频设置面板：播放模式、音量、"
            u8"输出设备、当前解码器以及详细设置入口；不重复放置播放控制按钮。";

        return true;
    }
};

static service_factory_single_t<
    audio_control_panel_element>
    g_audio_control_panel_factory;

} // namespace
} // namespace eas
