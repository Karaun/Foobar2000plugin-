#include "common.h"
#include "resource.h"

// Windows 10 SDK GDI+ uses legacy min/max internally.
#ifndef min
#define min(a,b) (((a) < (b)) ? (a) : (b))
#define PRO_GDIPLUS_TEMP_MIN 1
#endif
#ifndef max
#define max(a,b) (((a) > (b)) ? (a) : (b))
#define PRO_GDIPLUS_TEMP_MAX 1
#endif
#include <gdiplus.h>
#ifdef PRO_GDIPLUS_TEMP_MIN
#undef min
#undef PRO_GDIPLUS_TEMP_MIN
#endif
#ifdef PRO_GDIPLUS_TEMP_MAX
#undef max
#undef PRO_GDIPLUS_TEMP_MAX
#endif
#pragma comment(lib, "Gdiplus.lib")

namespace {

static const GUID guid_pro_nowplaying =
{ 0x4aa0b0e1, 0x7876, 0x4f6e, { 0x8d,0x46,0xa4,0x8f,0x17,0x3c,0xb1,0x92 } };

constexpr wchar_t kClassName[] = L"FoobarProNowPlayingPanel";

static std::wstring fmt_time(double s) {
    if (!(s >= 0)) s = 0;
    int total = (int)std::floor(s + 0.5);
    wchar_t b[32]{};
    swprintf_s(b, L"%d:%02d", total / 60, total % 60);
    return b;
}

static COLORREF blend(COLORREF a, COLORREF b, int pctB) {
    pctB = pctB < 0 ? 0 : (pctB > 100 ? 100 : pctB);
    const int pctA = 100 - pctB;
    return RGB(
        (GetRValue(a) * pctA + GetRValue(b) * pctB) / 100,
        (GetGValue(a) * pctA + GetGValue(b) * pctB) / 100,
        (GetBValue(a) * pctA + GetBValue(b) * pctB) / 100);
}

class pro_nowplaying_instance :
    public ui_element_instance,
    public now_playing_album_art_notify {
public:
    pro_nowplaying_instance(
        HWND parent,
        ui_element_config::ptr,
        ui_element_instance_callback_ptr cb)
        : m_callback(cb) {

        register_class();

        Gdiplus::GdiplusStartupInput input;
        Gdiplus::GdiplusStartup(&m_gdiplusToken, &input, nullptr);

        m_hwnd = CreateWindowExW(
            0, kClassName, L"",
            WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
            0, 0, 480, 300,
            parent, nullptr, core_api::get_my_instance(), this);

        if (!m_hwnd)
            throw std::runtime_error("Could not create Pro Now Playing panel");

        try {
            auto mgr = now_playing_album_art_notify_manager::get();
            mgr->add(this);
            set_art(mgr->current());
        } catch (...) {}

        // Passive display: one refresh per second is enough for the time bar.
        SetTimer(m_hwnd, 1, 1000, nullptr);
    }

    ~pro_nowplaying_instance() {
        try {
            now_playing_album_art_notify_manager::get()->remove(this);
        } catch (...) {}

        if (m_cover) DeleteObject(m_cover);
        if (m_fontTitle) DeleteObject(m_fontTitle);
        if (m_fontArtist) DeleteObject(m_fontArtist);
        if (m_fontNormal) DeleteObject(m_fontNormal);
        if (m_fontSmall) DeleteObject(m_fontSmall);
        if (m_icon) DestroyIcon(m_icon);

        if (m_gdiplusToken)
            Gdiplus::GdiplusShutdown(m_gdiplusToken);

        if (m_hwnd && IsWindow(m_hwnd))
            DestroyWindow(m_hwnd);
    }

    HWND get_wnd() override { return m_hwnd; }
    void set_configuration(ui_element_config::ptr) override {}

    ui_element_config::ptr get_configuration() override {
        return ui_element_config::g_create_empty(guid_pro_nowplaying);
    }

    GUID get_guid() override { return guid_pro_nowplaying; }
    GUID get_subclass() override { return ui_element_subclass_utility; }

    ui_element_min_max_info get_min_max_info() override {
        ui_element_min_max_info i;
        i.m_min_width = 340;
        i.m_min_height = 210;
        return i;
    }

    void notify(const GUID& what, t_size, const void*, t_size) override {
        if (what == ui_element_notify_colors_changed ||
            what == ui_element_notify_font_changed) {
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
    }

    void on_album_art(album_art_data::ptr data) override {
        set_art(data);
    }

private:
    HWND m_hwnd{};
    ui_element_instance_callback_ptr m_callback;

    HFONT m_fontTitle{};
    HFONT m_fontArtist{};
    HFONT m_fontNormal{};
    HFONT m_fontSmall{};

    HBITMAP m_cover{};
    HICON m_icon{};
    ULONG_PTR m_gdiplusToken{};

    COLORREF bg() const {
        return m_callback.is_valid()
            ? (COLORREF)m_callback->query_std_color(ui_color_background)
            : RGB(246, 248, 249);
    }

    COLORREF text() const {
        return m_callback.is_valid()
            ? (COLORREF)m_callback->query_std_color(ui_color_text)
            : RGB(48, 61, 67);
    }

    static COLORREF accent() { return RGB(111, 145, 159); }      // #6F919F
    static COLORREF accentDeep() { return RGB(79, 112, 126); }   // #4F707E
    static COLORREF muted() { return RGB(116, 133, 140); }       // #74858C
    static COLORREF line() { return RGB(219, 227, 230); }        // #DBE3E6

    static void register_class() {
        static bool once = false;
        if (once) return;

        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.hInstance = core_api::get_my_instance();
        wc.lpfnWndProc = wnd_proc;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = kClassName;

        // No CS_HREDRAW / CS_VREDRAW: they cause unnecessary full-window redraws.
        wc.style = 0;

        RegisterClassExW(&wc);
        once = true;
    }

    void init_fonts() {
        if (m_fontTitle) return;

        m_fontTitle = CreateFontW(
            -27, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0,
            L"Microsoft YaHei UI");

        m_fontArtist = CreateFontW(
            -18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0,
            L"Microsoft YaHei UI");

        m_fontNormal = CreateFontW(
            -16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0,
            L"Microsoft YaHei UI");

        m_fontSmall = CreateFontW(
            -13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0,
            L"Microsoft YaHei UI");

        m_icon = (HICON)LoadImageW(
            core_api::get_my_instance(),
            MAKEINTRESOURCEW(IDI_PRO_APP),
            IMAGE_ICON, 28, 28, LR_DEFAULTCOLOR);
    }

    void set_art(album_art_data::ptr data) {
        if (m_cover) {
            DeleteObject(m_cover);
            m_cover = nullptr;
        }

        if (data.is_valid() && data->size() > 0) {
            HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, data->size());
            if (h) {
                void* p = GlobalLock(h);
                if (p) {
                    memcpy(p, data->data(), data->size());
                    GlobalUnlock(h);
                }

                IStream* stream = nullptr;
                if (SUCCEEDED(CreateStreamOnHGlobal(h, TRUE, &stream)) && stream) {
                    std::unique_ptr<Gdiplus::Bitmap> bmp(
                        Gdiplus::Bitmap::FromStream(stream, FALSE));

                    if (bmp && bmp->GetLastStatus() == Gdiplus::Ok) {
                        bmp->GetHBITMAP(
                            Gdiplus::Color(255, 255, 255),
                            &m_cover);
                    }

                    stream->Release();
                } else {
                    GlobalFree(h);
                }
            }
        }

        if (m_hwnd)
            InvalidateRect(m_hwnd, nullptr, FALSE);
    }

    static void fill_rect(HDC dc, const RECT& r, COLORREF c) {
        HBRUSH b = CreateSolidBrush(c);
        FillRect(dc, &r, b);
        DeleteObject(b);
    }

    static void fill_round_rect(
        HDC dc, const RECT& r, int radius, COLORREF c) {

        HBRUSH b = CreateSolidBrush(c);
        HPEN p = CreatePen(PS_NULL, 0, c);

        HGDIOBJ oldB = SelectObject(dc, b);
        HGDIOBJ oldP = SelectObject(dc, p);

        RoundRect(
            dc, r.left, r.top, r.right, r.bottom,
            radius, radius);

        SelectObject(dc, oldP);
        SelectObject(dc, oldB);

        DeleteObject(p);
        DeleteObject(b);
    }

    void draw_text(
        HDC dc, const std::wstring& s,
        RECT r, HFONT font, COLORREF color,
        UINT flags) {

        HGDIOBJ old = SelectObject(dc, font);
        SetTextColor(dc, color);
        SetBkMode(dc, TRANSPARENT);
        DrawTextW(dc, s.c_str(), -1, &r, flags);
        SelectObject(dc, old);
    }

    void paint_cover(HDC dc, RECT r) {
        // soft card behind cover
        RECT shadow = r;
        OffsetRect(&shadow, 3, 4);
        fill_round_rect(
            dc, shadow, 14,
            blend(bg(), RGB(120, 130, 135), 16));

        fill_round_rect(dc, r, 14, RGB(255, 255, 255));

        RECT inner = r;
        InflateRect(&inner, -4, -4);

        if (m_cover) {
            BITMAP bm{};
            GetObject(m_cover, sizeof(bm), &bm);

            HDC mem = CreateCompatibleDC(dc);
            HGDIOBJ old = SelectObject(mem, m_cover);

            const double sx =
                (double)(inner.right - inner.left) / bm.bmWidth;
            const double sy =
                (double)(inner.bottom - inner.top) / bm.bmHeight;

            const double scale = sx < sy ? sx : sy;
            const int dw = (int)(bm.bmWidth * scale);
            const int dh = (int)(bm.bmHeight * scale);

            const int x =
                inner.left +
                ((inner.right - inner.left) - dw) / 2;

            const int y =
                inner.top +
                ((inner.bottom - inner.top) - dh) / 2;

            SetStretchBltMode(dc, HALFTONE);
            StretchBlt(
                dc, x, y, dw, dh,
                mem, 0, 0,
                bm.bmWidth, bm.bmHeight,
                SRCCOPY);

            SelectObject(mem, old);
            DeleteDC(mem);
        } else {
            RECT placeholder = inner;
            fill_round_rect(
                dc, placeholder, 10,
                blend(bg(), accent(), 12));

            if (m_icon) {
                const int iw = 58;
                const int ih = 58;
                DrawIconEx(
                    dc,
                    inner.left + (inner.right - inner.left - iw) / 2,
                    inner.top + (inner.bottom - inner.top - ih) / 2,
                    m_icon, iw, ih, 0, nullptr, DI_NORMAL);
            }
        }
    }

    void paint(HDC dc) {
        init_fonts();

        RECT rc{};
        GetClientRect(m_hwnd, &rc);

        const int w = rc.right - rc.left;
        const int h = rc.bottom - rc.top;

        fill_rect(dc, rc, bg());

        if (w <= 0 || h <= 0) return;

        const int pad = 18;

        // Horizontal card layout, tuned for the right-side panel in user's layout.
        int coverSide = h - pad * 2;
        if (coverSide > 230) coverSide = 230;
        if (coverSide < 150) coverSide = 150;
        if (coverSide > w / 2) coverSide = w / 2;

        RECT cover{
            pad, pad,
            pad + coverSide,
            pad + coverSide
        };

        paint_cover(dc, cover);

        const int left = cover.right + 24;
        const int right = w - pad;

        if (right <= left + 40) return;

        // brand
        if (m_icon) {
            DrawIconEx(
                dc, left, pad + 1,
                m_icon, 22, 22, 0, nullptr, DI_NORMAL);
        }

        RECT brand{
            left + 30, pad - 1,
            right, pad + 26
        };

        draw_text(
            dc, L"NOW PLAYING", brand,
            m_fontSmall, accentDeep(),
            DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        std::wstring title =
            pro_format("$if2(%title%,%filename%)");

        if (title.empty())
            title = L"当前没有播放音乐";

        std::wstring artist =
            pro_format("$if2(%artist%,未知艺术家)");

        std::wstring album =
            pro_format("$if2(%album%,未知专辑)");

        RECT titleRc{
            left, pad + 38,
            right, pad + 80
        };

        draw_text(
            dc, title, titleRc,
            m_fontTitle, text(),
            DT_LEFT | DT_VCENTER |
            DT_SINGLELINE | DT_END_ELLIPSIS);

        RECT artistRc{
            left, pad + 82,
            right, pad + 110
        };

        draw_text(
            dc, artist, artistRc,
            m_fontArtist, accentDeep(),
            DT_LEFT | DT_VCENTER |
            DT_SINGLELINE | DT_END_ELLIPSIS);

        RECT albumRc{
            left, pad + 111,
            right, pad + 137
        };

        draw_text(
            dc, album, albumRc,
            m_fontNormal, muted(),
            DT_LEFT | DT_VCENTER |
            DT_SINGLELINE | DT_END_ELLIPSIS);

        std::wstring tech =
            pro_format(
                "$if2($info(codec),?)"
                "  ·  $if2($info(samplerate),?) Hz"
                "  ·  $if2($info(bitspersample),?) bit");

        RECT techRc{
            left, pad + 140,
            right, pad + 166
        };

        draw_text(
            dc, tech, techRc,
            m_fontSmall,
            blend(muted(), bg(), 12),
            DT_LEFT | DT_VCENTER |
            DT_SINGLELINE | DT_END_ELLIPSIS);

        auto pc = playback_control::get();
        const double pos =
            pc->is_playing()
            ? pc->playback_get_position()
            : 0;

        const double len =
            pc->is_playing()
            ? pc->playback_get_length()
            : 0;

        const int barY =
            ((pad + 184) < (h - 44))
            ? (h - 44)
            : (pad + 184);

        RECT trackBar{
            left, barY,
            right, barY + 4
        };

        fill_round_rect(dc, trackBar, 2, line());

        if (len > 0) {
            double ratio = pos / len;
            if (ratio < 0) ratio = 0;
            if (ratio > 1) ratio = 1;

            RECT progress = trackBar;
            progress.right =
                progress.left +
                (int)((progress.right - progress.left) * ratio);

            if (progress.right > progress.left)
                fill_round_rect(dc, progress, 2, accent());
        }

        RECT timeRc{
            left, barY + 9,
            right, barY + 31
        };

        draw_text(
            dc,
            fmt_time(pos) + L"  /  " + fmt_time(len),
            timeRc, m_fontSmall, muted(),
            DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }

    void paint_double_buffered(HDC target) {
        RECT rc{};
        GetClientRect(m_hwnd, &rc);

        const int w = rc.right - rc.left;
        const int h = rc.bottom - rc.top;

        if (w <= 0 || h <= 0) return;

        HDC mem = CreateCompatibleDC(target);
        HBITMAP bmp =
            CreateCompatibleBitmap(target, w, h);

        HGDIOBJ old = SelectObject(mem, bmp);

        paint(mem);

        BitBlt(
            target, 0, 0, w, h,
            mem, 0, 0, SRCCOPY);

        SelectObject(mem, old);
        DeleteObject(bmp);
        DeleteDC(mem);
    }

    static LRESULT CALLBACK wnd_proc(
        HWND h, UINT m, WPARAM w, LPARAM l) {

        auto* self =
            (pro_nowplaying_instance*)
            GetWindowLongPtrW(h, GWLP_USERDATA);

        if (m == WM_NCCREATE) {
            auto* cs = (CREATESTRUCTW*)l;
            self =
                (pro_nowplaying_instance*)
                cs->lpCreateParams;

            SetWindowLongPtrW(
                h, GWLP_USERDATA, (LONG_PTR)self);

            if (self) self->m_hwnd = h;
        }

        if (!self)
            return DefWindowProcW(h, m, w, l);

        switch (m) {
        case WM_TIMER:
            // no erase + 1 Hz = stable passive display
            InvalidateRect(h, nullptr, FALSE);
            return 0;

        case WM_ERASEBKGND:
            // painting is fully double-buffered
            return 1;

        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC dc = BeginPaint(h, &ps);
            self->paint_double_buffered(dc);
            EndPaint(h, &ps);
            return 0;
        }

        case WM_DESTROY:
            KillTimer(h, 1);
            return 0;
        }

        return DefWindowProcW(h, m, w, l);
    }
};

class pro_nowplaying_element : public ui_element {
public:
    GUID get_guid() override {
        return guid_pro_nowplaying;
    }

    GUID get_subclass() override {
        return ui_element_subclass_utility;
    }

    void get_name(pfc::string_base& out) override {
        out = u8"Pro Now Playing Display";
    }

    ui_element_instance_ptr instantiate(
        HWND p,
        ui_element_config::ptr c,
        ui_element_instance_callback_ptr cb) override {

        return new service_impl_t<pro_nowplaying_instance>(
            p, c, cb);
    }

    ui_element_config::ptr
    get_default_configuration() override {
        return ui_element_config::g_create_empty(
            guid_pro_nowplaying);
    }

    ui_element_children_enumerator_ptr
    enumerate_children(ui_element_config::ptr) override {
        return nullptr;
    }

    bool get_description(pfc::string_base& out) override {
        out =
            u8"纯展示型正在播放面板：封面、歌曲、艺术家、专辑、"
            u8"音频参数与被动进度显示；双缓冲绘制减少闪屏，不包含重复播放控制按钮。";
        return true;
    }
};

static service_factory_single_t<pro_nowplaying_element> g_factory;

} // namespace
