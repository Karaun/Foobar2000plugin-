#include "common.h"

namespace {

static const GUID guid_pro_transport =
{ 0x67bfb0fe, 0xa34c, 0x4e09, { 0xa7,0x19,0xd5,0x5b,0xb4,0x51,0x42,0x2a } };

constexpr wchar_t kClassName[] = L"FoobarProTransportPanel";

enum class HitTarget {
    None,
    Previous,
    Play,
    Pause,
    Stop,
    Next,
    Sequential,
    CrossFile,
    Seek
};

static COLORREF accent()      { return RGB(111, 145, 159); }  // #6F919F
static COLORREF accentDeep()  { return RGB(79, 112, 126); }   // #4F707E
static COLORREF softBlue()    { return RGB(232, 240, 242); }  // #E8F0F2
static COLORREF muted()       { return RGB(116, 133, 140); }  // #74858C
static COLORREF divider()     { return RGB(219, 227, 230); }  // #DBE3E6
static COLORREF cardFill()    { return RGB(250, 251, 251); }

static COLORREF blend(COLORREF a, COLORREF b, int pctB) {
    pctB = pctB < 0 ? 0 : (pctB > 100 ? 100 : pctB);
    const int pctA = 100 - pctB;
    return RGB(
        (GetRValue(a) * pctA + GetRValue(b) * pctB) / 100,
        (GetGValue(a) * pctA + GetGValue(b) * pctB) / 100,
        (GetBValue(a) * pctA + GetBValue(b) * pctB) / 100);
}

static std::wstring lower_copy(std::wstring s) {
    for (auto& ch : s)
        ch = (wchar_t)towlower(ch);
    return s;
}

static std::wstring format_time(double seconds) {
    if (!(seconds >= 0)) seconds = 0;

    int total = (int)std::floor(seconds + 0.5);
    int minutes = total / 60;
    int secs = total % 60;

    wchar_t buf[32]{};
    swprintf_s(buf, L"%d:%02d", minutes, secs);
    return buf;
}

class pro_transport_instance : public ui_element_instance {
public:
    pro_transport_instance(
        HWND parent,
        ui_element_config::ptr,
        ui_element_instance_callback_ptr cb)
        : m_callback(cb) {

        register_class();

        m_hwnd = CreateWindowExW(
            0, kClassName, L"",
            WS_CHILD | WS_VISIBLE |
            WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
            0, 0, 760, 122,
            parent, nullptr,
            core_api::get_my_instance(),
            this);

        if (!m_hwnd)
            throw std::runtime_error("Could not create Pro Transport panel");

        SetTimer(m_hwnd, 1, 200, nullptr);
    }

    ~pro_transport_instance() {
        if (m_font) DeleteObject(m_font);
        if (m_fontSmall) DeleteObject(m_fontSmall);
        if (m_fontBold) DeleteObject(m_fontBold);

        if (m_hwnd && IsWindow(m_hwnd))
            DestroyWindow(m_hwnd);
    }

    HWND get_wnd() override { return m_hwnd; }
    void set_configuration(ui_element_config::ptr) override {}

    ui_element_config::ptr get_configuration() override {
        return ui_element_config::g_create_empty(guid_pro_transport);
    }

    GUID get_guid() override { return guid_pro_transport; }
    GUID get_subclass() override { return ui_element_subclass_utility; }

    ui_element_min_max_info get_min_max_info() override {
        ui_element_min_max_info i;
        i.m_min_width = 500;
        i.m_min_height = 112;
        return i;
    }

    void notify(
        const GUID& what,
        t_size,
        const void*,
        t_size) override {

        if (what == ui_element_notify_colors_changed ||
            what == ui_element_notify_font_changed) {
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
    }

private:
    HWND m_hwnd{};
    ui_element_instance_callback_ptr m_callback;

    HFONT m_font{};
    HFONT m_fontSmall{};
    HFONT m_fontBold{};

    HitTarget m_hover = HitTarget::None;
    HitTarget m_pressed = HitTarget::None;

    bool m_seeking = false;
    double m_seekPreview = 0;

    RECT m_prev{};
    RECT m_play{};
    RECT m_pause{};
    RECT m_stop{};
    RECT m_next{};
    RECT m_seq{};
    RECT m_cross{};
    RECT m_seek{};

    COLORREF bg() const {
        return m_callback.is_valid()
            ? (COLORREF)m_callback->query_std_color(ui_color_background)
            : RGB(244, 247, 248);
    }

    COLORREF text() const {
        return m_callback.is_valid()
            ? (COLORREF)m_callback->query_std_color(ui_color_text)
            : RGB(52, 73, 94);
    }

    static void register_class() {
        static bool once = false;
        if (once) return;

        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.hInstance = core_api::get_my_instance();
        wc.lpfnWndProc = wnd_proc;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = kClassName;
        wc.style = 0;

        RegisterClassExW(&wc);
        once = true;
    }

    void init_fonts() {
        if (m_font) return;

        m_font = CreateFontW(
            -15, 0, 0, 0, FW_NORMAL,
            FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, 0, 0,
            CLEARTYPE_QUALITY, 0,
            L"Microsoft YaHei UI");

        m_fontSmall = CreateFontW(
            -12, 0, 0, 0, FW_NORMAL,
            FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, 0, 0,
            CLEARTYPE_QUALITY, 0,
            L"Microsoft YaHei UI");

        m_fontBold = CreateFontW(
            -16, 0, 0, 0, FW_SEMIBOLD,
            FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, 0, 0,
            CLEARTYPE_QUALITY, 0,
            L"Microsoft YaHei UI");
    }

    static void fill_rect(HDC dc, const RECT& r, COLORREF c) {
        HBRUSH b = CreateSolidBrush(c);
        FillRect(dc, &r, b);
        DeleteObject(b);
    }

    static void fill_round(
        HDC dc,
        const RECT& r,
        int radius,
        COLORREF fill,
        COLORREF border) {

        HBRUSH b = CreateSolidBrush(fill);
        HPEN p = CreatePen(PS_SOLID, 1, border);

        HGDIOBJ oldB = SelectObject(dc, b);
        HGDIOBJ oldP = SelectObject(dc, p);

        RoundRect(
            dc,
            r.left, r.top,
            r.right, r.bottom,
            radius, radius);

        SelectObject(dc, oldP);
        SelectObject(dc, oldB);

        DeleteObject(p);
        DeleteObject(b);
    }

    void draw_text_line(
        HDC dc,
        const std::wstring& s,
        RECT r,
        HFONT f,
        COLORREF color,
        UINT flags) {

        HGDIOBJ old = SelectObject(dc, f);
        SetTextColor(dc, color);
        SetBkMode(dc, TRANSPARENT);

        DrawTextW(
            dc, s.c_str(), -1,
            &r, flags);

        SelectObject(dc, old);
    }

    void layout() {
        RECT rc{};
        GetClientRect(m_hwnd, &rc);

        const int w = rc.right - rc.left;

        const int buttonSize = 34;
        const int gap = 8;

        const int groupW =
            buttonSize * 5 + gap * 4;

        const int centerX = w / 2;
        const int x0 = centerX - groupW / 2;
        const int y = 35;

        m_prev = RECT{
            x0, y,
            x0 + buttonSize, y + buttonSize };

        m_play = RECT{
            m_prev.right + gap, y,
            m_prev.right + gap + buttonSize, y + buttonSize };

        m_pause = RECT{
            m_play.right + gap, y,
            m_play.right + gap + buttonSize, y + buttonSize };

        m_stop = RECT{
            m_pause.right + gap, y,
            m_pause.right + gap + buttonSize, y + buttonSize };

        m_next = RECT{
            m_stop.right + gap, y,
            m_stop.right + gap + buttonSize, y + buttonSize };

        const int modeW = 72;
        const int modeGap = 6;

        m_cross = RECT{
            w - 14 - modeW,
            14,
            w - 14,
            42 };

        m_seq = RECT{
            m_cross.left - modeGap - modeW,
            14,
            m_cross.left - modeGap,
            42 };

        const int timeW = 48;

        m_seek = RECT{
            14 + timeW + 8,
            92,
            w - 14 - timeW - 8,
            100 };
    }

    HitTarget hit_test(int x, int y) const {
        POINT p{ x, y };

        if (PtInRect(&m_prev, p)) return HitTarget::Previous;
        if (PtInRect(&m_play, p)) return HitTarget::Play;
        if (PtInRect(&m_pause, p)) return HitTarget::Pause;
        if (PtInRect(&m_stop, p)) return HitTarget::Stop;
        if (PtInRect(&m_next, p)) return HitTarget::Next;
        if (PtInRect(&m_seq, p)) return HitTarget::Sequential;
        if (PtInRect(&m_cross, p)) return HitTarget::CrossFile;

        RECT seekHit = m_seek;
        InflateRect(&seekHit, 0, 8);

        if (PtInRect(&seekHit, p))
            return HitTarget::Seek;

        return HitTarget::None;
    }

    void draw_button_base(
        HDC dc,
        const RECT& r,
        HitTarget target,
        bool active = false) {

        COLORREF fill = cardFill();
        COLORREF border = divider();

        if (active) {
            fill = softBlue();
            border = accent();
        }

        if (m_hover == target) {
            fill = blend(fill, softBlue(), 58);
            border = accent();
        }

        if (m_pressed == target) {
            fill = blend(fill, accent(), 28);
            border = accentDeep();
        }

        fill_round(
            dc, r, 17,
            fill, border);
    }

    static void draw_triangle(
        HDC dc,
        POINT a,
        POINT b,
        POINT c,
        COLORREF color) {

        POINT pts[3]{ a, b, c };

        HBRUSH brush =
            CreateSolidBrush(color);

        HGDIOBJ old =
            SelectObject(dc, brush);

        Polygon(dc, pts, 3);

        SelectObject(dc, old);
        DeleteObject(brush);
    }

    void draw_transport_icons(HDC dc) {
        const COLORREF icon = accentDeep();

        draw_button_base(
            dc, m_prev,
            HitTarget::Previous);

        draw_button_base(
            dc, m_play,
            HitTarget::Play,
            playback_control::get()->is_playing() &&
            !playback_control::get()->is_paused());

        draw_button_base(
            dc, m_pause,
            HitTarget::Pause,
            playback_control::get()->is_paused());

        draw_button_base(
            dc, m_stop,
            HitTarget::Stop);

        draw_button_base(
            dc, m_next,
            HitTarget::Next);

        // Previous
        {
            const int cx = (m_prev.left + m_prev.right) / 2;
            const int cy = (m_prev.top + m_prev.bottom) / 2;

            HPEN pen = CreatePen(PS_SOLID, 2, icon);
            HGDIOBJ old = SelectObject(dc, pen);

            MoveToEx(dc, cx - 7, cy - 7, nullptr);
            LineTo(dc, cx - 7, cy + 7);

            SelectObject(dc, old);
            DeleteObject(pen);

            draw_triangle(
                dc,
                POINT{ cx + 6, cy - 8 },
                POINT{ cx - 5, cy },
                POINT{ cx + 6, cy + 8 },
                icon);
        }

        // Play
        {
            const int cx = (m_play.left + m_play.right) / 2;
            const int cy = (m_play.top + m_play.bottom) / 2;

            draw_triangle(
                dc,
                POINT{ cx - 5, cy - 9 },
                POINT{ cx + 9, cy },
                POINT{ cx - 5, cy + 9 },
                icon);
        }

        // Pause
        {
            const int cx = (m_pause.left + m_pause.right) / 2;
            const int cy = (m_pause.top + m_pause.bottom) / 2;

            RECT a{ cx - 7, cy - 9, cx - 2, cy + 9 };
            RECT b{ cx + 2, cy - 9, cx + 7, cy + 9 };

            fill_rect(dc, a, icon);
            fill_rect(dc, b, icon);
        }

        // Stop
        {
            const int cx = (m_stop.left + m_stop.right) / 2;
            const int cy = (m_stop.top + m_stop.bottom) / 2;

            RECT square{
                cx - 7, cy - 7,
                cx + 7, cy + 7 };

            fill_rect(dc, square, icon);
        }

        // Next
        {
            const int cx = (m_next.left + m_next.right) / 2;
            const int cy = (m_next.top + m_next.bottom) / 2;

            draw_triangle(
                dc,
                POINT{ cx - 6, cy - 8 },
                POINT{ cx + 5, cy },
                POINT{ cx - 6, cy + 8 },
                icon);

            HPEN pen =
                CreatePen(PS_SOLID, 2, icon);

            HGDIOBJ old =
                SelectObject(dc, pen);

            MoveToEx(dc, cx + 7, cy - 7, nullptr);
            LineTo(dc, cx + 7, cy + 7);

            SelectObject(dc, old);
            DeleteObject(pen);
        }
    }

    int find_sequential_order() const {
        try {
            auto pm = playlist_manager::get();
            const t_size count = pm->playback_order_get_count();

            for (t_size i = 0; i < count; ++i) {
                const std::wstring name =
                    lower_copy(
                        pro_transport_utf8_to_wide(
                            pm->playback_order_get_name(i)));

                if (name.find(L"default") != std::wstring::npos ||
                    name.find(L"normal") != std::wstring::npos ||
                    name.find(L"顺序") != std::wstring::npos ||
                    name.find(L"默认") != std::wstring::npos) {
                    return (int)i;
                }
            }

            return count > 0 ? 0 : -1;
        } catch (...) {
            return -1;
        }
    }

    int find_cross_file_order() const {
        try {
            auto pm = playlist_manager::get();
            const t_size count = pm->playback_order_get_count();

            int fallback = -1;
            const int seq = find_sequential_order();

            for (t_size i = 0; i < count; ++i) {
                const std::wstring name =
                    lower_copy(
                        pro_transport_utf8_to_wide(
                            pm->playback_order_get_name(i)));

                if ((name.find(L"shuffle") != std::wstring::npos &&
                     name.find(L"track") != std::wstring::npos) ||
                    name.find(L"random") != std::wstring::npos ||
                    name.find(L"随机") != std::wstring::npos ||
                    name.find(L"跨文件") != std::wstring::npos) {
                    return (int)i;
                }

                if ((int)i != seq && fallback < 0)
                    fallback = (int)i;
            }

            return fallback;
        } catch (...) {
            return -1;
        }
    }

    int active_order() const {
        try {
            return (int)playlist_manager::get()
                ->playback_order_get_active();
        } catch (...) {
            return -1;
        }
    }

    std::wstring active_order_name() const {
        try {
            auto pm = playlist_manager::get();

            const t_size active =
                pm->playback_order_get_active();

            const t_size count =
                pm->playback_order_get_count();

            if (active < count) {
                return pro_transport_utf8_to_wide(
                    pm->playback_order_get_name(active));
            }
        } catch (...) {}

        return {};
    }

    void set_order(int index) {
        if (index < 0) return;

        try {
            playlist_manager::get()
                ->playback_order_set_active(
                    (t_size)index);
        } catch (...) {}
    }

    void draw_mode_pill(
        HDC dc,
        const RECT& r,
        const wchar_t* label,
        HitTarget target,
        bool active) {

        COLORREF fill =
            active ? softBlue() : cardFill();

        COLORREF border =
            active ? accent() : divider();

        if (m_hover == target) {
            fill = blend(fill, softBlue(), 52);
            border = accent();
        }

        if (m_pressed == target) {
            fill = blend(fill, accent(), 24);
            border = accentDeep();
        }

        fill_round(
            dc, r, 13,
            fill, border);

        draw_text_line(
            dc, label, r,
            active ? m_fontBold : m_fontSmall,
            active ? accentDeep() : muted(),
            DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    double playback_position() const {
        try {
            return playback_control::get()
                ->playback_get_position();
        } catch (...) {
            return 0;
        }
    }

    double playback_length() const {
        try {
            return playback_control::get()
                ->playback_get_length();
        } catch (...) {
            return 0;
        }
    }

    double position_from_x(int x) const {
        const double len = playback_length();
        if (!(len > 0)) return 0;

        int width = m_seek.right - m_seek.left;
        if (width <= 0) return 0;

        double ratio =
            (double)(x - m_seek.left) /
            (double)width;

        if (ratio < 0) ratio = 0;
        if (ratio > 1) ratio = 1;

        return ratio * len;
    }

    void draw_seek(HDC dc) {
        const double len = playback_length();

        double pos =
            m_seeking
            ? m_seekPreview
            : playback_position();

        double ratio = 0;

        if (len > 0) {
            ratio = pos / len;
            if (ratio < 0) ratio = 0;
            if (ratio > 1) ratio = 1;
        }

        RECT base = m_seek;
        fill_round(
            dc, base, 4,
            divider(), divider());

        RECT progress = base;
        progress.right =
            progress.left +
            (int)(
                (progress.right -
                 progress.left) *
                ratio);

        if (progress.right > progress.left) {
            fill_round(
                dc, progress, 4,
                accent(), accent());
        }

        const int thumbX =
            m_seek.left +
            (int)(
                (m_seek.right -
                 m_seek.left) *
                ratio);

        RECT thumb{
            thumbX - 6,
            (m_seek.top + m_seek.bottom) / 2 - 6,
            thumbX + 6,
            (m_seek.top + m_seek.bottom) / 2 + 6
        };

        fill_round(
            dc, thumb, 6,
            accentDeep(),
            accentDeep());

        RECT leftTime{
            14, 81,
            m_seek.left - 8, 110 };

        RECT rightTime{
            m_seek.right + 8, 81,
            14 + (m_seek.right - m_seek.left) + 104, 110 };

        RECT rc{};
        GetClientRect(m_hwnd, &rc);

        rightTime.right = rc.right - 14;

        draw_text_line(
            dc, format_time(pos),
            leftTime,
            m_fontSmall, muted(),
            DT_RIGHT |
            DT_VCENTER |
            DT_SINGLELINE);

        draw_text_line(
            dc, format_time(len),
            rightTime,
            m_fontSmall, muted(),
            DT_LEFT |
            DT_VCENTER |
            DT_SINGLELINE);
    }

    void paint(HDC dc) {
        init_fonts();
        layout();

        RECT rc{};
        GetClientRect(m_hwnd, &rc);

        fill_rect(dc, rc, bg());

        RECT card{
            6, 6,
            rc.right - 6,
            rc.bottom - 6
        };

        fill_round(
            dc, card, 18,
            cardFill(), divider());

        RECT title{
            18, 12,
            rc.right / 2 - 120,
            38
        };

        draw_text_line(
            dc, L"播放控制",
            title,
            m_fontBold,
            text(),
            DT_LEFT |
            DT_VCENTER |
            DT_SINGLELINE);

        const int seq = find_sequential_order();
        const int cross = find_cross_file_order();
        const int active = active_order();

        draw_mode_pill(
            dc, m_seq,
            L"顺序播放",
            HitTarget::Sequential,
            active == seq);

        draw_mode_pill(
            dc, m_cross,
            L"跨文件",
            HitTarget::CrossFile,
            active == cross);

        draw_transport_icons(dc);
        draw_seek(dc);

        std::wstring modeName =
            active_order_name();

        if (!modeName.empty()) {
            RECT modeRc{
                18, 46,
                m_prev.left - 12,
                69
            };

            draw_text_line(
                dc,
                L"当前模式 · " + modeName,
                modeRc,
                m_fontSmall,
                muted(),
                DT_LEFT |
                DT_VCENTER |
                DT_SINGLELINE |
                DT_END_ELLIPSIS);
        }
    }

    void paint_buffered(HDC target) {
        RECT rc{};
        GetClientRect(m_hwnd, &rc);

        const int w = rc.right - rc.left;
        const int h = rc.bottom - rc.top;

        if (w <= 0 || h <= 0)
            return;

        HDC mem =
            CreateCompatibleDC(target);

        HBITMAP bmp =
            CreateCompatibleBitmap(
                target, w, h);

        HGDIOBJ old =
            SelectObject(mem, bmp);

        paint(mem);

        BitBlt(
            target,
            0, 0, w, h,
            mem, 0, 0,
            SRCCOPY);

        SelectObject(mem, old);
        DeleteObject(bmp);
        DeleteDC(mem);
    }

    void do_action(HitTarget target) {
        auto pc = playback_control::get();

        try {
            switch (target) {
            case HitTarget::Previous:
                pc->previous();
                break;

            case HitTarget::Play:
                if (!pc->is_playing() ||
                    pc->is_paused()) {
                    pc->play_or_pause();
                }
                break;

            case HitTarget::Pause:
                if (pc->is_playing() &&
                    !pc->is_paused()) {
                    pc->play_or_pause();
                }
                break;

            case HitTarget::Stop:
                pc->stop();
                break;

            case HitTarget::Next:
                pc->next();
                break;

            case HitTarget::Sequential:
                set_order(
                    find_sequential_order());
                break;

            case HitTarget::CrossFile:
                set_order(
                    find_cross_file_order());
                break;

            default:
                break;
            }
        } catch (...) {}

        InvalidateRect(
            m_hwnd, nullptr, FALSE);
    }

    void seek_to_preview() {
        try {
            auto pc =
                playback_control::get();

            if (pc->playback_can_seek())
                pc->playback_seek(
                    m_seekPreview);
        } catch (...) {}
    }

    void begin_seek(int x) {
        try {
            if (!playback_control::get()
                    ->playback_can_seek())
                return;
        } catch (...) {
            return;
        }

        m_seeking = true;
        m_seekPreview =
            position_from_x(x);

        SetCapture(m_hwnd);
        InvalidateRect(
            m_hwnd, nullptr, FALSE);
    }

    void update_seek(int x) {
        if (!m_seeking) return;

        m_seekPreview =
            position_from_x(x);

        InvalidateRect(
            m_hwnd, nullptr, FALSE);
    }

    void finish_seek(int x) {
        if (!m_seeking) return;

        m_seekPreview =
            position_from_x(x);

        seek_to_preview();

        m_seeking = false;

        if (GetCapture() == m_hwnd)
            ReleaseCapture();

        InvalidateRect(
            m_hwnd, nullptr, FALSE);
    }

    void update_hover(int x, int y) {
        if (m_seeking) return;

        HitTarget hit =
            hit_test(x, y);

        if (hit != m_hover) {
            m_hover = hit;

            InvalidateRect(
                m_hwnd, nullptr, FALSE);
        }

        TRACKMOUSEEVENT tme{};
        tme.cbSize = sizeof(tme);
        tme.dwFlags = TME_LEAVE;
        tme.hwndTrack = m_hwnd;
        TrackMouseEvent(&tme);
    }

    static LRESULT CALLBACK wnd_proc(
        HWND h,
        UINT m,
        WPARAM w,
        LPARAM l) {

        auto* self =
            (pro_transport_instance*)
            GetWindowLongPtrW(
                h, GWLP_USERDATA);

        if (m == WM_NCCREATE) {
            auto* cs =
                (CREATESTRUCTW*)l;

            self =
                (pro_transport_instance*)
                cs->lpCreateParams;

            SetWindowLongPtrW(
                h, GWLP_USERDATA,
                (LONG_PTR)self);

            if (self)
                self->m_hwnd = h;
        }

        if (!self)
            return DefWindowProcW(
                h, m, w, l);

        switch (m) {
        case WM_TIMER:
            if (!self->m_seeking) {
                InvalidateRect(
                    h, nullptr, FALSE);
            }
            return 0;

        case WM_MOUSEMOVE:
            if (self->m_seeking)
                self->update_seek(
                    GET_X_LPARAM(l));
            else
                self->update_hover(
                    GET_X_LPARAM(l),
                    GET_Y_LPARAM(l));
            return 0;

        case WM_MOUSELEAVE:
            self->m_hover =
                HitTarget::None;

            InvalidateRect(
                h, nullptr, FALSE);
            return 0;

        case WM_LBUTTONDOWN: {
            const int x =
                GET_X_LPARAM(l);

            const int y =
                GET_Y_LPARAM(l);

            HitTarget hit =
                self->hit_test(x, y);

            if (hit == HitTarget::Seek) {
                self->begin_seek(x);
                return 0;
            }

            self->m_pressed = hit;

            if (hit != HitTarget::None)
                SetCapture(h);

            InvalidateRect(
                h, nullptr, FALSE);

            return 0;
        }

        case WM_LBUTTONUP: {
            const int x =
                GET_X_LPARAM(l);

            const int y =
                GET_Y_LPARAM(l);

            if (self->m_seeking) {
                self->finish_seek(x);
                return 0;
            }

            HitTarget released =
                self->hit_test(x, y);

            HitTarget pressed =
                self->m_pressed;

            self->m_pressed =
                HitTarget::None;

            if (GetCapture() == h)
                ReleaseCapture();

            if (released == pressed &&
                pressed != HitTarget::None) {
                self->do_action(pressed);
            } else {
                InvalidateRect(
                    h, nullptr, FALSE);
            }

            return 0;
        }

        case WM_CAPTURECHANGED:
            if (!self->m_seeking) {
                self->m_pressed =
                    HitTarget::None;
            }
            return 0;

        case WM_SETCURSOR:
            if (LOWORD(l) == HTCLIENT) {
                SetCursor(
                    LoadCursorW(
                        nullptr, IDC_HAND));
                return TRUE;
            }
            break;

        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC dc =
                BeginPaint(h, &ps);

            self->paint_buffered(dc);

            EndPaint(h, &ps);
            return 0;
        }

        case WM_DESTROY:
            KillTimer(h, 1);
            return 0;
        }

        return DefWindowProcW(
            h, m, w, l);
    }
};

class pro_transport_element : public ui_element {
public:
    GUID get_guid() override {
        return guid_pro_transport;
    }

    GUID get_subclass() override {
        return ui_element_subclass_utility;
    }

    void get_name(
        pfc::string_base& out) override {
        out = u8"Pro Transport";
    }

    ui_element_instance_ptr instantiate(
        HWND p,
        ui_element_config::ptr c,
        ui_element_instance_callback_ptr cb) override {

        return new service_impl_t<
            pro_transport_instance>(
                p, c, cb);
    }

    ui_element_config::ptr
    get_default_configuration() override {

        return ui_element_config::
            g_create_empty(
                guid_pro_transport);
    }

    ui_element_children_enumerator_ptr
    enumerate_children(
        ui_element_config::ptr) override {
        return nullptr;
    }

    bool get_description(
        pfc::string_base& out) override {

        out =
            u8"莫兰迪蓝播放控制面板：上一曲、播放、暂停、停止、下一曲、"
            u8"顺序播放、跨文件/随机曲目模式，以及支持点击与拖动的播放进度条。";

        return true;
    }
};

static service_factory_single_t<
    pro_transport_element>
    g_factory;

} // namespace
