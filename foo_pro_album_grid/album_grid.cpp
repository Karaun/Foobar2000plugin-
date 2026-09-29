#include "common.h"
#include <foobar2000/SDK/ole_interaction.h>

// GDI+ compatibility with NOMINMAX.
#ifndef min
#define min(a,b) (((a) < (b)) ? (a) : (b))
#define PRO_ALBUM_TEMP_MIN 1
#endif
#ifndef max
#define max(a,b) (((a) > (b)) ? (a) : (b))
#define PRO_ALBUM_TEMP_MAX 1
#endif
#include <gdiplus.h>
#ifdef PRO_ALBUM_TEMP_MIN
#undef min
#undef PRO_ALBUM_TEMP_MIN
#endif
#ifdef PRO_ALBUM_TEMP_MAX
#undef max
#undef PRO_ALBUM_TEMP_MAX
#endif

#pragma comment(lib, "Gdiplus.lib")
#pragma comment(lib, "Ole32.lib")
#pragma comment(lib, "Shlwapi.lib")

namespace {

static const GUID guid_pro_album_grid =
{ 0xf1d24562, 0x46d1, 0x4daf, { 0xb8,0x91,0x61,0xe0,0x13,0x7a,0xe5,0x2c } };

constexpr wchar_t kClassName[] = L"FoobarProAlbumGridPanel";

enum : int {
    IDC_SEARCH = 4101,
    IDC_REFRESH,
    IDC_BACK,
    IDC_ADD_ALL
};

struct AlbumEntry {
    std::wstring album;
    std::wstring artist;
    std::vector<metadb_handle_ptr> tracks;
    HBITMAP cover = nullptr;
    bool artAttempted = false;
};

struct AlbumHit {
    int albumIndex = -1;
    RECT rc{};
};

struct TrackHit {
    int trackIndex = -1;
    RECT rc{};
};

class AlbumDropSource final : public IDropSource {
public:
    AlbumDropSource() : m_ref(1) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (IsEqualIID(iid, IID_IUnknown) || IsEqualIID(iid, IID_IDropSource)) {
            *out = static_cast<IDropSource*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override {
        return static_cast<ULONG>(InterlockedIncrement(&m_ref));
    }

    ULONG STDMETHODCALLTYPE Release() override {
        LONG v = InterlockedDecrement(&m_ref);
        if (v == 0) delete this;
        return static_cast<ULONG>(v);
    }

    HRESULT STDMETHODCALLTYPE QueryContinueDrag(BOOL escapePressed, DWORD keyState) override {
        if (escapePressed) return DRAGDROP_S_CANCEL;
        if ((keyState & MK_LBUTTON) == 0) return DRAGDROP_S_DROP;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GiveFeedback(DWORD) override {
        return DRAGDROP_S_USEDEFAULTCURSORS;
    }

private:
    LONG m_ref;
};

static COLORREF accent() { return RGB(111, 145, 159); }       // #6F919F
static COLORREF accentDeep() { return RGB(79, 112, 126); }    // #4F707E
static COLORREF muted() { return RGB(116, 133, 140); }        // #74858C
static COLORREF divider() { return RGB(219, 227, 230); }      // #DBE3E6
static COLORREF cardFill() { return RGB(250, 251, 251); }
static COLORREF softBlue() { return RGB(232, 240, 242); }

static COLORREF blend(COLORREF a, COLORREF b, int pctB) {
    pctB = pctB < 0 ? 0 : (pctB > 100 ? 100 : pctB);
    const int pctA = 100 - pctB;
    return RGB(
        (GetRValue(a) * pctA + GetRValue(b) * pctB) / 100,
        (GetGValue(a) * pctA + GetGValue(b) * pctB) / 100,
        (GetBValue(a) * pctA + GetBValue(b) * pctB) / 100);
}

static std::wstring lower_copy(std::wstring s) {
    for (auto& c : s) c = static_cast<wchar_t>(towlower(c));
    return s;
}

static std::wstring format_handle(
    metadb_handle_ptr h,
    titleformat_object::ptr script) {

    if (h.is_empty() || script.is_empty()) return {};
    pfc::string8 out;
    try {
        h->format_title(nullptr, out, script, nullptr);
    } catch (...) {
        return {};
    }
    return pro_utf8_to_wide_album(out.c_str());
}

class pro_album_grid_instance : public ui_element_instance {
public:
    pro_album_grid_instance(
        HWND parent,
        ui_element_config::ptr,
        ui_element_instance_callback_ptr cb)
        : m_callback(cb) {

        register_class();

        Gdiplus::GdiplusStartupInput input;
        Gdiplus::GdiplusStartup(&m_gdiplusToken, &input, nullptr);

        const HRESULT oleHr = OleInitialize(nullptr);
        m_oleInitialized = (oleHr == S_OK || oleHr == S_FALSE);

        compile_titleformats();

        m_hwnd = CreateWindowExW(
            0, kClassName, L"",
            WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
            0, 0, 620, 500,
            parent, nullptr, core_api::get_my_instance(), this);

        if (!m_hwnd) {
            if (m_oleInitialized) OleUninitialize();
            throw std::runtime_error("Could not create Pro Album Grid");
        }

        create_children();
        reload_library();
        SetTimer(m_hwnd, 1, 120, nullptr);
    }

    ~pro_album_grid_instance() {
        for (auto& a : m_albums) {
            if (a.cover) DeleteObject(a.cover);
        }
        if (m_fontAlbum) DeleteObject(m_fontAlbum);
        if (m_fontArtist) DeleteObject(m_fontArtist);
        if (m_fontHeader) DeleteObject(m_fontHeader);
        if (m_fontTrack) DeleteObject(m_fontTrack);
        if (m_fontSmall) DeleteObject(m_fontSmall);
        if (m_gdiplusToken) Gdiplus::GdiplusShutdown(m_gdiplusToken);
        if (m_oleInitialized) OleUninitialize();
        if (m_hwnd && IsWindow(m_hwnd)) DestroyWindow(m_hwnd);
    }

    HWND get_wnd() override { return m_hwnd; }
    void set_configuration(ui_element_config::ptr) override {}

    ui_element_config::ptr get_configuration() override {
        return ui_element_config::g_create_empty(guid_pro_album_grid);
    }

    GUID get_guid() override { return guid_pro_album_grid; }
    GUID get_subclass() override { return ui_element_subclass_utility; }

    ui_element_min_max_info get_min_max_info() override {
        ui_element_min_max_info i;
        i.m_min_width = 340;
        i.m_min_height = 320;
        return i;
    }

    void notify(const GUID& what, t_size, const void*, t_size) override {
        if (what == ui_element_notify_colors_changed ||
            what == ui_element_notify_font_changed) {
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
    }

private:
    HWND m_hwnd{};
    HWND m_search{};
    HWND m_refresh{};
    HWND m_back{};
    HWND m_addAll{};
    ui_element_instance_callback_ptr m_callback;

    HFONT m_fontAlbum{};
    HFONT m_fontArtist{};
    HFONT m_fontHeader{};
    HFONT m_fontTrack{};
    HFONT m_fontSmall{};

    titleformat_object::ptr m_tfAlbum;
    titleformat_object::ptr m_tfArtist;
    titleformat_object::ptr m_tfTrackNo;
    titleformat_object::ptr m_tfTrackTitle;
    titleformat_object::ptr m_tfTrackDuration;

    ULONG_PTR m_gdiplusToken{};
    bool m_oleInitialized = false;

    std::vector<AlbumEntry> m_albums;
    std::vector<int> m_filtered;
    std::vector<AlbumHit> m_albumHits;
    std::vector<TrackHit> m_trackHits;

    int m_gridScrollY = 0;
    int m_detailScrollY = 0;
    int m_hoverAlbum = -1;
    int m_detailAlbum = -1;
    int m_selectedTrack = -1;
    int m_toolbarBottom = 52;

    int m_mouseDownAlbum = -1;
    POINT m_mouseDownPoint{};
    bool m_dragStarted = false;

    std::wstring m_statusText;
    ULONGLONG m_statusUntil = 0;

    COLORREF bg() const {
        return m_callback.is_valid()
            ? static_cast<COLORREF>(m_callback->query_std_color(ui_color_background))
            : RGB(246, 248, 249);
    }

    COLORREF text() const {
        return m_callback.is_valid()
            ? static_cast<COLORREF>(m_callback->query_std_color(ui_color_text))
            : RGB(48, 61, 67);
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
        wc.style = CS_DBLCLKS;

        RegisterClassExW(&wc);
        once = true;
    }

    void compile_titleformats() {
        auto c = titleformat_compiler::get();
        c->compile_safe(m_tfAlbum, "$if2(%album%,未标注专辑)");
        c->compile_safe(m_tfArtist, "$if2(%album artist%,$if2(%artist%,未知艺术家))");
        c->compile_safe(m_tfTrackNo, "$if2(%tracknumber%,)");
        c->compile_safe(m_tfTrackTitle, "$if2(%title%,%filename%)");
        c->compile_safe(m_tfTrackDuration, "$if2(%length%,)");
    }

    void init_fonts() {
        if (m_fontAlbum) return;

        m_fontHeader = CreateFontW(
            -21, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0,
            L"Microsoft YaHei UI");

        m_fontAlbum = CreateFontW(
            -15, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0,
            L"Microsoft YaHei UI");

        m_fontArtist = CreateFontW(
            -12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0,
            L"Microsoft YaHei UI");

        m_fontTrack = CreateFontW(
            -14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0,
            L"Microsoft YaHei UI");

        m_fontSmall = CreateFontW(
            -11, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0,
            L"Microsoft YaHei UI");
    }

    HWND create_button(int id, const wchar_t* label) {
        return CreateWindowExW(
            0, L"BUTTON", label,
            WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON,
            0, 0, 70, 30,
            m_hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
            core_api::get_my_instance(), nullptr);
    }

    void create_children() {
        m_search = CreateWindowExW(
            WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
            0, 0, 240, 30,
            m_hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SEARCH)),
            core_api::get_my_instance(), nullptr);

        SendMessageW(
            m_search, EM_SETCUEBANNER, TRUE,
            reinterpret_cast<LPARAM>(L"搜索专辑 / 艺术家"));

        m_refresh = create_button(IDC_REFRESH, L"刷新");
        m_back = create_button(IDC_BACK, L"← 返回专辑");
        m_addAll = create_button(IDC_ADD_ALL, L"添加全部");

        SetWindowPos(m_search, HWND_TOP, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        SetWindowPos(m_refresh, HWND_TOP, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

        init_fonts();
        SendMessageW(m_search, WM_SETFONT, reinterpret_cast<WPARAM>(m_fontTrack), TRUE);
        SendMessageW(m_refresh, WM_SETFONT, reinterpret_cast<WPARAM>(m_fontTrack), TRUE);
        SendMessageW(m_back, WM_SETFONT, reinterpret_cast<WPARAM>(m_fontTrack), TRUE);
        SendMessageW(m_addAll, WM_SETFONT, reinterpret_cast<WPARAM>(m_fontTrack), TRUE);

        ShowWindow(m_back, SW_HIDE);
        ShowWindow(m_addAll, SW_HIDE);
    }

    bool in_detail() const {
        return m_detailAlbum >= 0 &&
            static_cast<size_t>(m_detailAlbum) < m_albums.size();
    }

    void update_child_visibility() {
        const bool detail = in_detail();
        ShowWindow(m_search, detail ? SW_HIDE : SW_SHOW);
        ShowWindow(m_refresh, detail ? SW_HIDE : SW_SHOW);
        ShowWindow(m_back, detail ? SW_SHOW : SW_HIDE);
        ShowWindow(m_addAll, detail ? SW_SHOW : SW_HIDE);
    }

    void layout_children() {
        RECT rc{};
        GetClientRect(m_hwnd, &rc);
        const int w = rc.right - rc.left;

        if (!in_detail()) {
            // Narrow sidebars need two rows. Keeping title and search on one row
            // is what caused the previous visual collisions.
            const int pad = 14;
            const int btnW = 62;
            const int gap = 8;

            // Row 1: title is painted at left; Refresh button at right.
            MoveWindow(
                m_refresh,
                w - pad - btnW, 12,
                btnW, 28, TRUE);

            // Row 2: search gets the whole usable width.
            MoveWindow(
                m_search,
                pad, 50,
                std::max(150, w - pad * 2),
                30, TRUE);

            m_toolbarBottom = 80;
        } else {
            MoveWindow(m_back, 14, 14, 96, 28, TRUE);
            MoveWindow(m_addAll, w - 96, 14, 82, 28, TRUE);
            m_toolbarBottom = 44;
        }
    }

    void set_status(const std::wstring& s, DWORD ms = 1800) {
        m_statusText = s;
        m_statusUntil = GetTickCount64() + ms;
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }

    void free_covers() {
        for (auto& a : m_albums) {
            if (a.cover) {
                DeleteObject(a.cover);
                a.cover = nullptr;
            }
        }
    }

    void reload_library() {
        free_covers();
        m_albums.clear();
        m_filtered.clear();
        m_albumHits.clear();
        m_trackHits.clear();
        m_gridScrollY = 0;
        m_detailScrollY = 0;
        m_hoverAlbum = -1;
        m_detailAlbum = -1;
        m_selectedTrack = -1;

        if (!library_manager::get()->is_library_enabled()) {
            update_child_visibility();
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return;
        }

        pfc::list_t<metadb_handle_ptr> items;
        try {
            library_manager::get()->get_all_items(items);
        } catch (...) {
            update_child_visibility();
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return;
        }

        std::map<std::wstring, size_t> index;

        for (t_size i = 0; i < items.get_count(); ++i) {
            auto h = items[i];
            std::wstring album = format_handle(h, m_tfAlbum);
            std::wstring artist = format_handle(h, m_tfArtist);

            if (album.empty()) album = L"未标注专辑";
            if (artist.empty()) artist = L"未知艺术家";

            std::wstring key =
                lower_copy(artist) + L"\n" + lower_copy(album);

            auto it = index.find(key);
            if (it == index.end()) {
                AlbumEntry entry;
                entry.album = album;
                entry.artist = artist;
                entry.tracks.push_back(h);
                const size_t idx = m_albums.size();
                m_albums.push_back(std::move(entry));
                index.emplace(key, idx);
            } else {
                m_albums[it->second].tracks.push_back(h);
            }
        }

        std::sort(
            m_albums.begin(), m_albums.end(),
            [](const AlbumEntry& a, const AlbumEntry& b) {
                return lower_copy(a.artist + L"\n" + a.album) <
                    lower_copy(b.artist + L"\n" + b.album);
            });

        apply_filter();
        update_child_visibility();
        layout_children();
    }

    void apply_filter() {
        wchar_t buf[512]{};
        GetWindowTextW(m_search, buf, 512);
        const std::wstring needle = lower_copy(buf);

        m_filtered.clear();

        for (size_t i = 0; i < m_albums.size(); ++i) {
            std::wstring hay =
                lower_copy(m_albums[i].album + L" " + m_albums[i].artist);

            if (needle.empty() || hay.find(needle) != std::wstring::npos)
                m_filtered.push_back(static_cast<int>(i));
        }

        m_gridScrollY = 0;
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }

    struct GridMetrics {
        int width = 0;
        int height = 0;
        int gridTop = 108;
        int pad = 14;
        int gap = 12;
        int cols = 1;
        int cardW = 150;
        int cover = 126;
        int rowH = 190;
    };

    GridMetrics grid_metrics() const {
        RECT rc{};
        GetClientRect(m_hwnd, &rc);

        GridMetrics m;
        m.width = rc.right - rc.left;
        m.height = rc.bottom - rc.top;
        m.gridTop = std::max(108, m_toolbarBottom + 26);

        const int usable = std::max(120, m.width - m.pad * 2);
        m.cols = std::max(1, usable / 160);

        m.cardW =
            (usable - m.gap * (m.cols - 1)) / m.cols;

        if (m.cardW < 120) m.cardW = 120;

        m.cover = std::min(146, m.cardW - 20);
        m.rowH = m.cover + 76;
        return m;
    }

    int max_grid_scroll() const {
        const GridMetrics m = grid_metrics();
        const int rows =
            static_cast<int>((m_filtered.size() + m.cols - 1) / m.cols);
        const int footerH = 30;
        const int viewportH = std::max(0, m.height - m.gridTop - footerH);
        return std::max(0, rows * m.rowH - viewportH);
    }

    int max_detail_scroll() const {
        if (!in_detail()) return 0;

        RECT rc{};
        GetClientRect(m_hwnd, &rc);
        const int h = rc.bottom - rc.top;
        const int listTop = std::min(235, std::max(190, h / 2));
        const int rowsH = static_cast<int>(m_albums[m_detailAlbum].tracks.size()) * 34;
        return std::max(0, rowsH - (h - listTop - 18));
    }

    void clamp_scrolls() {
        m_gridScrollY = std::max(0, std::min(m_gridScrollY, max_grid_scroll()));
        m_detailScrollY = std::max(0, std::min(m_detailScrollY, max_detail_scroll()));
    }

    HBITMAP decode_bitmap(album_art_data::ptr data) {
        if (data.is_empty() || data->size() == 0) return nullptr;

        HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, data->size());
        if (!h) return nullptr;

        void* p = GlobalLock(h);
        if (p) {
            memcpy(p, data->data(), data->size());
            GlobalUnlock(h);
        }

        IStream* stream = nullptr;
        if (FAILED(CreateStreamOnHGlobal(h, TRUE, &stream)) || !stream) {
            GlobalFree(h);
            return nullptr;
        }

        std::unique_ptr<Gdiplus::Bitmap> bmp(
            Gdiplus::Bitmap::FromStream(stream, FALSE));

        HBITMAP result = nullptr;

        if (bmp && bmp->GetLastStatus() == Gdiplus::Ok) {
            bmp->GetHBITMAP(Gdiplus::Color(255,255,255), &result);
        }

        stream->Release();
        return result;
    }

    void load_cover(int albumIndex) {
        if (albumIndex < 0 || static_cast<size_t>(albumIndex) >= m_albums.size())
            return;

        auto& album = m_albums[albumIndex];
        if (album.artAttempted) return;

        album.artAttempted = true;

        if (album.tracks.empty()) return;

        try {
            pfc::list_t<metadb_handle_ptr> items;
            for (auto& t : album.tracks) items.add_item(t);

            pfc::list_t<GUID> ids;
            ids.add_item(album_art_ids::cover_front);

            auto extractor =
                album_art_manager_v2::get()->open(items, ids, fb2k::noAbort);

            auto data =
                extractor->query(album_art_ids::cover_front, fb2k::noAbort);

            album.cover = decode_bitmap(data);
        } catch (...) {
            album.cover = nullptr;
        }
    }

    void load_next_visible_cover() {
        if (in_detail()) {
            if (m_detailAlbum >= 0 &&
                !m_albums[m_detailAlbum].artAttempted) {
                load_cover(m_detailAlbum);
                InvalidateRect(m_hwnd, nullptr, FALSE);
            }
            return;
        }

        if (m_filtered.empty()) return;

        const GridMetrics m = grid_metrics();
        const int firstRow = m_gridScrollY / m.rowH;
        const int rowsVisible = (m.height - m.gridTop) / m.rowH + 2;
        int first = firstRow * m.cols;
        int last = first + rowsVisible * m.cols;

        first = std::max(0, first);
        last = std::min(last, static_cast<int>(m_filtered.size()));

        for (int i = first; i < last; ++i) {
            const int albumIndex = m_filtered[i];
            if (!m_albums[albumIndex].artAttempted) {
                load_cover(albumIndex);
                InvalidateRect(m_hwnd, nullptr, FALSE);
                return;
            }
        }
    }

    static void fill_rect(HDC dc, const RECT& r, COLORREF c) {
        HBRUSH b = CreateSolidBrush(c);
        FillRect(dc, &r, b);
        DeleteObject(b);
    }

    static void fill_round(HDC dc, const RECT& r, int radius, COLORREF c) {
        HBRUSH b = CreateSolidBrush(c);
        HPEN p = CreatePen(PS_NULL, 0, c);

        HGDIOBJ ob = SelectObject(dc, b);
        HGDIOBJ op = SelectObject(dc, p);

        RoundRect(dc, r.left, r.top, r.right, r.bottom, radius, radius);

        SelectObject(dc, op);
        SelectObject(dc, ob);

        DeleteObject(p);
        DeleteObject(b);
    }

    void draw_text(
        HDC dc,
        const std::wstring& s,
        RECT r,
        HFONT font,
        COLORREF color,
        UINT flags) {

        HGDIOBJ old = SelectObject(dc, font);
        SetTextColor(dc, color);
        SetBkMode(dc, TRANSPARENT);
        DrawTextW(dc, s.c_str(), -1, &r, flags);
        SelectObject(dc, old);
    }

    void draw_cover(HDC dc, const AlbumEntry& a, RECT r) {
        fill_round(dc, r, 12, RGB(238, 243, 244));

        RECT inner = r;
        InflateRect(&inner, -4, -4);

        if (a.cover) {
            BITMAP bm{};
            GetObject(a.cover, sizeof(bm), &bm);

            HDC mem = CreateCompatibleDC(dc);
            HGDIOBJ old = SelectObject(mem, a.cover);

            const double sx =
                static_cast<double>(inner.right - inner.left) / bm.bmWidth;
            const double sy =
                static_cast<double>(inner.bottom - inner.top) / bm.bmHeight;
            const double sc = sx < sy ? sx : sy;

            const int dw = static_cast<int>(bm.bmWidth * sc);
            const int dh = static_cast<int>(bm.bmHeight * sc);

            const int x = inner.left + ((inner.right - inner.left) - dw) / 2;
            const int y = inner.top + ((inner.bottom - inner.top) - dh) / 2;

            SetStretchBltMode(dc, HALFTONE);
            StretchBlt(
                dc, x, y, dw, dh,
                mem, 0, 0, bm.bmWidth, bm.bmHeight,
                SRCCOPY);

            SelectObject(mem, old);
            DeleteDC(mem);
        } else {
            draw_text(
                dc, L"♪", inner, m_fontHeader, accent(),
                DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
    }

    void draw_status(HDC dc, int w, int h) {
        std::wstring status;
        if (GetTickCount64() < m_statusUntil && !m_statusText.empty()) {
            status = m_statusText;
        } else if (!in_detail()) {
            status = L"单击专辑查看歌曲 · 按住封面拖到播放列表";
        } else {
            status = L"双击单曲加入播放列表 · “添加全部”加入整张专辑";
        }

        RECT footer{ 0, h - 30, w, h };
        fill_rect(dc, footer, bg());
        RECT sep{ 14, h - 30, w - 14, h - 29 };
        fill_rect(dc, sep, divider());
        RECT r{ 14, h - 27, w - 14, h - 4 };
        draw_text(dc, status, r, m_fontSmall, muted(),
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }

    void paint_grid(HDC dc, int w, int h) {
        m_albumHits.clear();
        m_trackHits.clear();

        GridMetrics m = grid_metrics();
        clamp_scrolls();

        const int footerH = 30;
        const int bodyTop = m.gridTop;
        const int bodyBottom = std::max(bodyTop, h - footerH);

        // Critical fix: album content is clipped to the scrollable body.
        // Scrolled rows can no longer paint behind the fixed search toolbar.
        const int savedDc = SaveDC(dc);
        IntersectClipRect(dc, 0, bodyTop, w, bodyBottom);

        if (!library_manager::get()->is_library_enabled()) {
            RECT empty{ 20, bodyTop + 10, w - 20, bodyBottom - 10 };
            draw_text(dc,
                L"媒体库尚未配置\n请先在 Preferences → Media Library 添加音乐目录",
                empty, m_fontAlbum, muted(),
                DT_CENTER | DT_VCENTER | DT_WORDBREAK);
        } else if (m_filtered.empty()) {
            RECT empty{ 20, bodyTop + 10, w - 20, bodyBottom - 10 };
            draw_text(dc, L"没有匹配的专辑",
                empty, m_fontAlbum, muted(),
                DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        } else {
            for (size_t vi = 0; vi < m_filtered.size(); ++vi) {
                const int row = static_cast<int>(vi) / m.cols;
                const int col = static_cast<int>(vi) % m.cols;
                const int x = m.pad + col * (m.cardW + m.gap);
                const int y = bodyTop + row * m.rowH - m_gridScrollY;

                if (y >= bodyBottom || y + m.rowH <= bodyTop) continue;

                const int albumIndex = m_filtered[vi];
                const AlbumEntry& a = m_albums[albumIndex];
                RECT card{ x, y, x + m.cardW, y + m.rowH - 8 };

                if (albumIndex == m_hoverAlbum) {
                    fill_round(dc, card, 14,
                        blend(bg(), RGB(229,235,238), 62));
                }

                const int coverX = x + (m.cardW - m.cover) / 2;
                RECT cover{ coverX, y + 8,
                    coverX + m.cover, y + 8 + m.cover };
                draw_cover(dc, a, cover);

                RECT albumRc{ x + 8, cover.bottom + 7,
                    x + m.cardW - 8, cover.bottom + 29 };
                draw_text(dc, a.album, albumRc, m_fontAlbum, text(),
                    DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

                RECT artistRc{ x + 8, cover.bottom + 30,
                    x + m.cardW - 8, cover.bottom + 49 };
                draw_text(dc, a.artist, artistRc, m_fontArtist, muted(),
                    DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

                RECT visible = card;
                if (visible.top < bodyTop) visible.top = bodyTop;
                if (visible.bottom > bodyBottom) visible.bottom = bodyBottom;
                if (visible.bottom > visible.top)
                    m_albumHits.push_back({ albumIndex, visible });
            }
        }

        RestoreDC(dc, savedDc);

        // Fixed chrome is painted after the scrolling body.
        RECT toolbar{ 10, 8, w - 10, 92 };
        fill_round(dc, toolbar, 14, cardFill());
        RECT title{ 18, 12, w - 92, 40 };
        draw_text(dc, L"专辑库", title, m_fontAlbum, accentDeep(),
            DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        draw_status(dc, w, h);
    }

    void paint_detail(HDC dc, int w, int h) {
        m_albumHits.clear();
        m_trackHits.clear();

        if (!in_detail()) return;

        auto& a = m_albums[m_detailAlbum];
        clamp_scrolls();

        RECT toolbar{ 10, 10, w - 10, 52 };
        fill_round(dc, toolbar, 14, cardFill());

        const int top = 66;
        const int coverSide = std::min(150, std::max(100, w / 3));

        RECT cover{
            18, top,
            18 + coverSide, top + coverSide
        };

        draw_cover(dc, a, cover);

        const int infoLeft = cover.right + 18;
        RECT albumRc{
            infoLeft, top + 4,
            w - 16, top + 42
        };

        draw_text(
            dc, a.album, albumRc, m_fontHeader, text(),
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

        RECT artistRc{
            infoLeft, top + 43,
            w - 16, top + 68
        };

        draw_text(
            dc, a.artist, artistRc, m_fontAlbum, accentDeep(),
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

        wchar_t countText[64]{};
        swprintf_s(
            countText, L"%zu 首歌曲 · 可拖动左侧封面到播放列表",
            a.tracks.size());

        RECT countRc{
            infoLeft, top + 72,
            w - 16, top + 104
        };

        draw_text(
            dc, countText, countRc, m_fontSmall, muted(),
            DT_LEFT | DT_TOP | DT_WORDBREAK);

        const int listTop = std::min(235, std::max(top + coverSide + 14, h / 2));

        RECT separator{ 16, listTop - 8, w - 16, listTop - 7 };
        fill_rect(dc, separator, divider());

        int y = listTop - m_detailScrollY;

        for (size_t i = 0; i < a.tracks.size(); ++i) {
            RECT row{ 14, y, w - 14, y + 32 };

            if (row.bottom >= listTop && row.top <= h - 26) {
                if (static_cast<int>(i) == m_selectedTrack) {
                    fill_round(dc, row, 9, softBlue());
                }

                const std::wstring no = format_handle(a.tracks[i], m_tfTrackNo);
                const std::wstring titleText = format_handle(a.tracks[i], m_tfTrackTitle);
                const std::wstring duration = format_handle(a.tracks[i], m_tfTrackDuration);

                RECT noRc{ row.left + 8, row.top, row.left + 42, row.bottom };
                RECT titleRc{ row.left + 44, row.top, row.right - 60, row.bottom };
                RECT durRc{ row.right - 56, row.top, row.right - 8, row.bottom };

                draw_text(
                    dc, no.empty() ? L"•" : no,
                    noRc, m_fontSmall, muted(),
                    DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

                draw_text(
                    dc, titleText,
                    titleRc, m_fontTrack,
                    static_cast<int>(i) == m_selectedTrack ? accentDeep() : text(),
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

                draw_text(
                    dc, duration,
                    durRc, m_fontSmall, muted(),
                    DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

                m_trackHits.push_back({ static_cast<int>(i), row });
            }

            y += 34;
        }

        // Allow drag from the large detail cover too.
        m_albumHits.push_back({ m_detailAlbum, cover });

        draw_status(dc, w, h);
    }

    void paint(HDC dc) {
        init_fonts();
        layout_children();

        RECT rc{};
        GetClientRect(m_hwnd, &rc);

        fill_rect(dc, rc, bg());

        const int w = rc.right - rc.left;
        const int h = rc.bottom - rc.top;

        if (in_detail()) paint_detail(dc, w, h);
        else paint_grid(dc, w, h);
    }

    void paint_buffered(HDC target) {
        RECT rc{};
        GetClientRect(m_hwnd, &rc);

        const int w = rc.right - rc.left;
        const int h = rc.bottom - rc.top;
        if (w <= 0 || h <= 0) return;

        HDC mem = CreateCompatibleDC(target);
        HBITMAP bmp = CreateCompatibleBitmap(target, w, h);
        HGDIOBJ old = SelectObject(mem, bmp);

        paint(mem);

        BitBlt(target, 0, 0, w, h, mem, 0, 0, SRCCOPY);

        SelectObject(mem, old);
        DeleteObject(bmp);
        DeleteDC(mem);
    }

    int album_hit_test(int x, int y) const {
        POINT p{ x, y };
        for (const auto& hit : m_albumHits) {
            if (PtInRect(&hit.rc, p))
                return hit.albumIndex;
        }
        return -1;
    }

    int track_hit_test(int x, int y) const {
        POINT p{ x, y };
        for (const auto& hit : m_trackHits) {
            if (PtInRect(&hit.rc, p))
                return hit.trackIndex;
        }
        return -1;
    }

    metadb_handle_list make_track_list(const std::vector<metadb_handle_ptr>& src) const {
        metadb_handle_list out;
        for (const auto& t : src) out.add_item(t);
        return out;
    }

    bool add_tracks_to_active_playlist(const std::vector<metadb_handle_ptr>& tracks) {
        if (tracks.empty()) return false;

        try {
            auto pm = playlist_manager::get();
            pm->active_playlist_fix();

            t_size playlist = pm->get_active_playlist();
            if (playlist == pfc_infinite) {
                playlist = pm->create_playlist_autoname();
                if (playlist == pfc_infinite) return false;
                pm->set_active_playlist(playlist);
            }

            metadb_handle_list list = make_track_list(tracks);

            pm->playlist_undo_backup(playlist);

            const bool ok =
                pm->playlist_add_items_filter(
                    playlist, list, true);

            if (ok) {
                set_status(
                    tracks.size() == 1
                        ? L"已加入当前播放列表"
                        : L"整张专辑已加入当前播放列表");
            }

            return ok;
        } catch (...) {
            set_status(L"加入播放列表失败");
            return false;
        }
    }

    void add_one_track(int trackIndex) {
        if (!in_detail()) return;
        const auto& tracks = m_albums[m_detailAlbum].tracks;
        if (trackIndex < 0 ||
            static_cast<size_t>(trackIndex) >= tracks.size())
            return;

        std::vector<metadb_handle_ptr> one{ tracks[trackIndex] };
        add_tracks_to_active_playlist(one);
    }

    void add_current_album() {
        if (!in_detail()) return;
        add_tracks_to_active_playlist(m_albums[m_detailAlbum].tracks);
    }

    void start_album_drag(int albumIndex) {
        if (albumIndex < 0 ||
            static_cast<size_t>(albumIndex) >= m_albums.size())
            return;

        if (!m_oleInitialized) {
            set_status(L"当前线程未启用 OLE 拖放");
            return;
        }

        try {
            metadb_handle_list list =
                make_track_list(m_albums[albumIndex].tracks);

            auto data =
                ole_interaction::get()->create_dataobject(list);

            auto* source = new AlbumDropSource();

            DWORD effect = DROPEFFECT_NONE;

            HRESULT hr = DoDragDrop(
                data.get_ptr(),
                source,
                DROPEFFECT_COPY,
                &effect);

            source->Release();

            if (hr == DRAGDROP_S_DROP) {
                set_status(L"专辑已拖放");
            } else if (hr != DRAGDROP_S_CANCEL) {
                set_status(L"拖放未完成");
            }
        } catch (...) {
            set_status(L"创建拖放数据失败");
        }
    }

    void enter_detail(int albumIndex) {
        if (albumIndex < 0 ||
            static_cast<size_t>(albumIndex) >= m_albums.size())
            return;

        m_detailAlbum = albumIndex;
        m_selectedTrack = -1;
        m_detailScrollY = 0;
        m_hoverAlbum = -1;

        load_cover(albumIndex);
        update_child_visibility();
        layout_children();
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }

    void leave_detail() {
        m_detailAlbum = -1;
        m_selectedTrack = -1;
        m_detailScrollY = 0;
        m_trackHits.clear();

        update_child_visibility();
        layout_children();
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }

    void on_mouse_move(int x, int y, WPARAM keyState) {
        if ((keyState & MK_LBUTTON) &&
            m_mouseDownAlbum >= 0 &&
            !m_dragStarted) {

            const int dx = x - m_mouseDownPoint.x;
            const int dy = y - m_mouseDownPoint.y;

            if (abs(dx) >= GetSystemMetrics(SM_CXDRAG) ||
                abs(dy) >= GetSystemMetrics(SM_CYDRAG)) {

                m_dragStarted = true;

                if (GetCapture() == m_hwnd)
                    ReleaseCapture();

                const int albumIndex = m_mouseDownAlbum;
                m_mouseDownAlbum = -1;

                start_album_drag(albumIndex);
                return;
            }
        }

        if (!in_detail() && !(keyState & MK_LBUTTON)) {
            const int hit = album_hit_test(x, y);

            if (hit != m_hoverAlbum) {
                m_hoverAlbum = hit;
                InvalidateRect(m_hwnd, nullptr, FALSE);
            }

            TRACKMOUSEEVENT tme{};
            tme.cbSize = sizeof(tme);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = m_hwnd;
            TrackMouseEvent(&tme);
        }
    }

    static LRESULT CALLBACK wnd_proc(
        HWND h, UINT m, WPARAM w, LPARAM l) {

        auto* self =
            reinterpret_cast<pro_album_grid_instance*>(
                GetWindowLongPtrW(h, GWLP_USERDATA));

        if (m == WM_NCCREATE) {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(l);
            self =
                static_cast<pro_album_grid_instance*>(
                    cs->lpCreateParams);

            SetWindowLongPtrW(
                h, GWLP_USERDATA,
                reinterpret_cast<LONG_PTR>(self));

            if (self) self->m_hwnd = h;
        }

        if (!self)
            return DefWindowProcW(h, m, w, l);

        switch (m) {
        case WM_SIZE:
            self->layout_children();
            self->clamp_scrolls();
            InvalidateRect(h, nullptr, FALSE);
            return 0;

        case WM_TIMER:
            self->load_next_visible_cover();
            if (GetTickCount64() >= self->m_statusUntil)
                self->m_statusText.clear();
            return 0;

        case WM_COMMAND:
            if (LOWORD(w) == IDC_SEARCH &&
                HIWORD(w) == EN_CHANGE) {
                self->apply_filter();
                return 0;
            }

            if (LOWORD(w) == IDC_REFRESH &&
                HIWORD(w) == BN_CLICKED) {
                self->reload_library();
                return 0;
            }

            if (LOWORD(w) == IDC_BACK &&
                HIWORD(w) == BN_CLICKED) {
                self->leave_detail();
                return 0;
            }

            if (LOWORD(w) == IDC_ADD_ALL &&
                HIWORD(w) == BN_CLICKED) {
                self->add_current_album();
                return 0;
            }
            break;

        case WM_MOUSEWHEEL: {
            const int delta = GET_WHEEL_DELTA_WPARAM(w);
            const int amount = delta / WHEEL_DELTA * 96;

            if (self->in_detail())
                self->m_detailScrollY -= amount;
            else
                self->m_gridScrollY -= amount;

            self->clamp_scrolls();
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        }

        case WM_LBUTTONDOWN: {
            SetFocus(h);

            const int albumIndex =
                self->album_hit_test(
                    GET_X_LPARAM(l), GET_Y_LPARAM(l));

            self->m_mouseDownAlbum = albumIndex;
            self->m_mouseDownPoint =
                POINT{ GET_X_LPARAM(l), GET_Y_LPARAM(l) };
            self->m_dragStarted = false;

            if (albumIndex >= 0)
                SetCapture(h);

            return 0;
        }

        case WM_MOUSEMOVE:
            self->on_mouse_move(
                GET_X_LPARAM(l),
                GET_Y_LPARAM(l),
                w);
            return 0;

        case WM_LBUTTONUP: {
            const int x = GET_X_LPARAM(l);
            const int y = GET_Y_LPARAM(l);

            if (GetCapture() == h)
                ReleaseCapture();

            if (!self->m_dragStarted) {
                if (self->in_detail()) {
                    const int track =
                        self->track_hit_test(x, y);

                    if (track >= 0) {
                        self->m_selectedTrack = track;
                        InvalidateRect(h, nullptr, FALSE);
                    }
                } else {
                    const int albumIndex =
                        self->album_hit_test(x, y);

                    if (albumIndex >= 0 &&
                        albumIndex == self->m_mouseDownAlbum) {
                        self->enter_detail(albumIndex);
                    }
                }
            }

            self->m_mouseDownAlbum = -1;
            self->m_dragStarted = false;
            return 0;
        }

        case WM_LBUTTONDBLCLK:
            if (self->in_detail()) {
                const int track =
                    self->track_hit_test(
                        GET_X_LPARAM(l),
                        GET_Y_LPARAM(l));

                if (track >= 0)
                    self->add_one_track(track);
            }
            return 0;

        case WM_MOUSELEAVE:
            if (!self->in_detail()) {
                self->m_hoverAlbum = -1;
                InvalidateRect(h, nullptr, FALSE);
            }
            return 0;


        case WM_CTLCOLOREDIT: {
            HDC dc = reinterpret_cast<HDC>(w);
            SetTextColor(dc, self->text());
            SetBkColor(dc, RGB(255,255,255));
            return reinterpret_cast<LRESULT>(GetStockObject(WHITE_BRUSH));
        }

        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC dc = BeginPaint(h, &ps);
            self->paint_buffered(dc);
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

class pro_album_grid_element : public ui_element {
public:
    GUID get_guid() override { return guid_pro_album_grid; }
    GUID get_subclass() override { return ui_element_subclass_utility; }

    void get_name(pfc::string_base& out) override {
        out = u8"Pro Album Browser";
    }

    ui_element_instance_ptr instantiate(
        HWND p,
        ui_element_config::ptr c,
        ui_element_instance_callback_ptr cb) override {

        return new service_impl_t<pro_album_grid_instance>(p, c, cb);
    }

    ui_element_config::ptr get_default_configuration() override {
        return ui_element_config::g_create_empty(guid_pro_album_grid);
    }

    ui_element_children_enumerator_ptr
    enumerate_children(ui_element_config::ptr) override {
        return nullptr;
    }

    bool get_description(pfc::string_base& out) override {
        out =
            u8"交互式专辑浏览器：封面网格、搜索、点击进入歌曲列表、"
            u8"添加专辑或单曲到当前播放列表，并支持把专辑直接拖到原生 Playlist View。";
        return true;
    }
};

static service_factory_single_t<pro_album_grid_element> g_factory;

} // namespace
