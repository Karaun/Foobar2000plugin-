#include "common.h"
#include <regex>
#include <winhttp.h>
#include <cwctype>
#include <cstdio>
#pragma comment(lib, "Winhttp.lib")

namespace {
static const GUID guid_pro_lyrics = { 0xc0196480,0xe4a1,0x4129,{0xb5,0xe0,0xa8,0xd8,0xf3,0xe1,0xb4,0x0c} };
constexpr wchar_t kClassName[] = L"FoobarProLyricsPanel";
constexpr UINT WM_PRO_LYRICS_ONLINE = WM_APP + 0x341;

struct LyricLine { double time=-1; std::wstring text; };
struct OnlineLyricsResult {
    std::string path;
    std::wstring lyrics;
    std::wstring status;
    bool shouldCache = false;
};
struct HttpResult { DWORD status = 0; std::string body; };
struct SearchCandidate {
    std::wstring title, artist, album, synced, plain;
    double duration = 0;
};

static std::string percent_decode(std::string s){
    std::string o; o.reserve(s.size());
    auto hex=[](char c)->int{ if(c>='0'&&c<='9')return c-'0'; if(c>='a'&&c<='f')return c-'a'+10; if(c>='A'&&c<='F')return c-'A'+10; return -1; };
    for(size_t i=0;i<s.size();++i){ if(s[i]=='%'&&i+2<s.size()){ int a=hex(s[i+1]),b=hex(s[i+2]); if(a>=0&&b>=0){o.push_back((char)((a<<4)|b)); i+=2; continue;} } o.push_back(s[i]); }
    return o;
}
static std::wstring native_path(const char* p){
    if(!p) return {}; std::string s=p;
    if(s.rfind("file://",0)==0) s=s.substr(7);
    s=percent_decode(s);
    if(s.size()>=3&&s[0]=='/'&&isalpha((unsigned char)s[1])&&s[2]==':') s.erase(s.begin());
    std::replace(s.begin(),s.end(),'/','\\'); return pro_utf8_to_wide(s.c_str());
}
static std::wstring stem_lrc(std::wstring p){ size_t slash=p.find_last_of(L"\\/"); size_t dot=p.find_last_of(L'.'); if(dot!=std::wstring::npos&&(slash==std::wstring::npos||dot>slash)) p.resize(dot); return p+L".lrc"; }
static std::wstring read_text(const std::wstring& path){
    std::ifstream f(path,std::ios::binary); if(!f) return {}; std::string b((std::istreambuf_iterator<char>(f)),{}); if(b.empty()) return {};
    if(b.size()>=3&&(unsigned char)b[0]==0xEF&&(unsigned char)b[1]==0xBB&&(unsigned char)b[2]==0xBF)b.erase(0,3);
    int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,b.data(),(int)b.size(),nullptr,0); UINT cp=CP_UTF8; DWORD flags=MB_ERR_INVALID_CHARS;
    if(n<=0){ cp=CP_ACP; flags=0; n=MultiByteToWideChar(cp,flags,b.data(),(int)b.size(),nullptr,0); }
    if(n<=0) return {}; std::wstring w((size_t)n,L'\0'); MultiByteToWideChar(cp,flags,b.data(),(int)b.size(),w.data(),n); return w;
}
static bool write_utf8_text(const std::wstring& path,const std::wstring& text){
    FILE* f=nullptr; if(_wfopen_s(&f,path.c_str(),L"wb")!=0||!f)return false;
    std::string u8=pro_wide_to_utf8(text); bool ok=fwrite(u8.data(),1,u8.size(),f)==u8.size(); fclose(f); return ok;
}
static std::wstring cache_path_for_track(const std::string& foobarPath){
    std::wstring np=native_path(foobarPath.c_str()); if(np.empty())return {};
    size_t slash=np.find_last_of(L"\\/"); if(slash==std::wstring::npos)return {};
    std::wstring dir=np.substr(0,slash+1); std::wstring file=np.substr(slash+1);
    std::wstring lyricsDir=dir+L"lyrics"; CreateDirectoryW(lyricsDir.c_str(),nullptr);
    return lyricsDir+L"\\"+stem_lrc(file);
}
static std::vector<LyricLine> parse_lyrics(const std::wstring& src){
    std::vector<LyricLine> out; std::wistringstream ss(src); std::wstring line; double offset=0;
    std::wregex ts(LR"(\[(\d{1,2}):(\d{2})(?:[\.:](\d{1,3}))?\])");
    while(std::getline(ss,line)){
        if(!line.empty()&&line.back()==L'\r')line.pop_back();
        if(line.rfind(L"[offset:",0)==0){ try{ auto e=line.find(L']'); offset=std::stod(line.substr(8,e-8))/1000.0; }catch(...){} continue; }
        std::wsregex_iterator it(line.begin(),line.end(),ts), end; std::vector<double> times; size_t last=0;
        for(;it!=end;++it){ int m=std::stoi((*it)[1].str()), s=std::stoi((*it)[2].str()); std::wstring fs=(*it)[3].str(); double frac=0; if(!fs.empty()){ double v=std::stod(fs); frac=v/(fs.size()==3?1000.0:100.0); } times.push_back(m*60+s+frac+offset); last=(size_t)(it->position()+it->length()); }
        std::wstring txt=line.substr(last); while(!txt.empty()&&iswspace(txt.front()))txt.erase(txt.begin());
        if(!times.empty()){ for(double t:times) out.push_back({t,txt}); }
        else if(!line.empty()&&line[0]!=L'[') out.push_back({-1,line});
    }
    bool synced=std::any_of(out.begin(),out.end(),[](auto&x){return x.time>=0;});
    if(synced){ out.erase(std::remove_if(out.begin(),out.end(),[](auto&x){return x.time<0;}),out.end()); std::sort(out.begin(),out.end(),[](auto&a,auto&b){return a.time<b.time;}); }
    return out;
}

static std::string url_encode_utf8(const std::wstring& w){
    static const char* hex="0123456789ABCDEF"; std::string in=pro_wide_to_utf8(w), out;
    for(unsigned char c:in){
        if((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='-'||c=='_'||c=='.'||c=='~') out.push_back((char)c);
        else { out.push_back('%'); out.push_back(hex[(c>>4)&15]); out.push_back(hex[c&15]); }
    }
    return out;
}
static HttpResult http_get_lrclib(const std::string& asciiPath){
    HttpResult ret;
    HINTERNET ses=WinHttpOpen(L"FoobarProLyrics/2.9.2 (LRCLIB client)",WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0);
    if(!ses)return ret;
    WinHttpSetTimeouts(ses,5000,5000,8000,8000);
    HINTERNET con=WinHttpConnect(ses,L"lrclib.net",INTERNET_DEFAULT_HTTPS_PORT,0);
    if(!con){WinHttpCloseHandle(ses);return ret;}
    std::wstring path(asciiPath.begin(),asciiPath.end());
    HINTERNET req=WinHttpOpenRequest(con,L"GET",path.c_str(),nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE);
    if(!req){WinHttpCloseHandle(con);WinHttpCloseHandle(ses);return ret;}
    const wchar_t* hdr=L"Accept: application/json\r\nLrclib-Client: FoobarProLyrics/2.9.2\r\n";
    BOOL ok=WinHttpSendRequest(req,hdr,(DWORD)-1L,WINHTTP_NO_REQUEST_DATA,0,0,0)&&WinHttpReceiveResponse(req,nullptr);
    if(ok){
        DWORD sz=sizeof(ret.status); WinHttpQueryHeaders(req,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&ret.status,&sz,WINHTTP_NO_HEADER_INDEX);
        for(;;){ DWORD avail=0; if(!WinHttpQueryDataAvailable(req,&avail)||avail==0)break; size_t old=ret.body.size(); ret.body.resize(old+avail); DWORD got=0; if(!WinHttpReadData(req,&ret.body[old],avail,&got))break; ret.body.resize(old+got); if(got==0)break; }
    }
    WinHttpCloseHandle(req);WinHttpCloseHandle(con);WinHttpCloseHandle(ses);return ret;
}
static void append_utf8_cp(std::string& out,unsigned cp){
    if(cp<=0x7F)out.push_back((char)cp);
    else if(cp<=0x7FF){out.push_back((char)(0xC0|(cp>>6)));out.push_back((char)(0x80|(cp&63)));}
    else if(cp<=0xFFFF){out.push_back((char)(0xE0|(cp>>12)));out.push_back((char)(0x80|((cp>>6)&63)));out.push_back((char)(0x80|(cp&63)));}
    else{out.push_back((char)(0xF0|(cp>>18)));out.push_back((char)(0x80|((cp>>12)&63)));out.push_back((char)(0x80|((cp>>6)&63)));out.push_back((char)(0x80|(cp&63)));}
}
static int hex4(const std::string& s,size_t p){int v=0;for(int i=0;i<4;i++){if(p+i>=s.size())return-1;char c=s[p+i];int h=(c>='0'&&c<='9')?c-'0':(c>='a'&&c<='f')?c-'a'+10:(c>='A'&&c<='F')?c-'A'+10:-1;if(h<0)return-1;v=(v<<4)|h;}return v;}
static std::wstring json_string_field(const std::string& j,const char* key){
    std::string needle=std::string("\"")+key+"\""; size_t p=j.find(needle); if(p==std::string::npos)return{}; p=j.find(':',p+needle.size()); if(p==std::string::npos)return{}; ++p; while(p<j.size()&&isspace((unsigned char)j[p]))++p; if(p>=j.size()||j.compare(p,4,"null")==0||j[p]!='\"')return{}; ++p;
    std::string o; for(;p<j.size();++p){char c=j[p];if(c=='\"')break;if(c!='\\'){o.push_back(c);continue;}if(++p>=j.size())break;char e=j[p];switch(e){case'\"':o.push_back('\"');break;case'\\':o.push_back('\\');break;case'/':o.push_back('/');break;case'b':o.push_back('\b');break;case'f':o.push_back('\f');break;case'n':o.push_back('\n');break;case'r':o.push_back('\r');break;case't':o.push_back('\t');break;case'u':{int hi=hex4(j,p+1);if(hi<0)break;p+=4;unsigned cp=(unsigned)hi;if(cp>=0xD800&&cp<=0xDBFF&&p+6<j.size()&&j[p+1]=='\\'&&j[p+2]=='u'){int lo=hex4(j,p+3);if(lo>=0xDC00&&lo<=0xDFFF){cp=0x10000+((cp-0xD800)<<10)+(lo-0xDC00);p+=6;}}append_utf8_cp(o,cp);break;}default:o.push_back(e);break;}}
    return pro_utf8_to_wide(o.c_str());
}
static double json_number_field(const std::string& j,const char* key){std::string n=std::string("\"")+key+"\"";size_t p=j.find(n);if(p==std::string::npos)return 0;p=j.find(':',p+n.size());if(p==std::string::npos)return 0;++p;while(p<j.size()&&isspace((unsigned char)j[p]))++p;try{return std::stod(j.substr(p));}catch(...){return 0;}}
static std::vector<std::string> json_array_objects(const std::string& j){
    std::vector<std::string> out; bool in=false,esc=false;int depth=0;size_t start=0;
    for(size_t i=0;i<j.size();++i){char c=j[i];if(in){if(esc)esc=false;else if(c=='\\')esc=true;else if(c=='\"')in=false;continue;}if(c=='\"'){in=true;continue;}if(c=='{'){if(depth++==0)start=i;}else if(c=='}'&&depth>0){if(--depth==0)out.push_back(j.substr(start,i-start+1));}}
    return out;
}
static std::wstring trim_copy(std::wstring s){
    while(!s.empty()&&iswspace(s.front()))s.erase(s.begin());
    while(!s.empty()&&iswspace(s.back()))s.pop_back();
    return s;
}

static std::wstring normalize(std::wstring s){
    std::wstring o;
    o.reserve(s.size());
    for(wchar_t c:s){
        if(c<128){
            if(iswalnum(c))o.push_back((wchar_t)towlower(c));
        }else if(!iswspace(c)&&
                 c!=L'·'&&c!=L'，'&&c!=L'。'&&c!=L'（'&&c!=L'）'&&
                 c!=L'【'&&c!=L'】'&&c!=L'《'&&c!=L'》'&&
                 c!=L'：'&&c!=L'；'&&c!=L'、'){
            o.push_back((wchar_t)towlower(c));
        }
    }
    return o;
}

static bool norm_eq(const std::wstring&a,const std::wstring&b){
    auto x=normalize(a),y=normalize(b);
    return !x.empty()&&x==y;
}

static bool norm_contains(const std::wstring&a,const std::wstring&b){
    auto x=normalize(a),y=normalize(b);
    return !x.empty()&&!y.empty()&&
        (x.find(y)!=std::wstring::npos||y.find(x)!=std::wstring::npos);
}

static bool has_any_word(const std::wstring&s,const std::vector<std::wstring>& words){
    std::wstring n=normalize(s);
    for(const auto&w:words){
        std::wstring nw=normalize(w);
        if(!nw.empty()&&n.find(nw)!=std::wstring::npos)return true;
    }
    return false;
}

static std::wstring strip_leading_track_number(std::wstring s){
    s=trim_copy(s);
    size_t i=0;
    while(i<s.size()&&iswdigit(s[i]))++i;
    if(i>0&&i<=3&&i<s.size()){
        size_t j=i;
        while(j<s.size()&&(iswspace(s[j])||s[j]==L'.'||s[j]==L'-'||s[j]==L'_'||s[j]==L'–'||s[j]==L'—'))++j;
        if(j>i)s=s.substr(j);
    }
    return trim_copy(s);
}

static bool version_tag_text(const std::wstring& inside){
    static const std::vector<std::wstring> kWords={
        L"live",L"remaster",L"remastered",L"remix",L"mix",L"edit",
        L"version",L"mono",L"stereo",L"acoustic",L"instrumental",
        L"karaoke",L"demo",L"radio",L"bonus",L"explicit",L"clean",
        L"现场",L"演唱会",L"重制",L"混音",L"伴奏",L"纯音乐",L"版本",L"试听"
    };
    return has_any_word(inside,kWords);
}

static std::wstring strip_version_tags(std::wstring s){
    s=strip_leading_track_number(s);
    const wchar_t opens[]={L'(',L'[',L'{',L'（',L'【'};
    const wchar_t closes[]={L')',L']',L'}',L'）',L'】'};
    for(int k=0;k<5;k++){
        size_t pos=0;
        while((pos=s.find(opens[k],pos))!=std::wstring::npos){
            size_t e=s.find(closes[k],pos+1);
            if(e==std::wstring::npos)break;
            std::wstring inside=s.substr(pos+1,e-pos-1);
            if(version_tag_text(inside))s.erase(pos,e-pos+1);
            else pos=e+1;
        }
    }

    std::wstring lower=s;
    std::transform(lower.begin(),lower.end(),lower.begin(),[](wchar_t c){return(wchar_t)towlower(c);});
    const std::vector<std::wstring> featMarks={L" feat. ",L" feat ",L" ft. ",L" ft ",L" featuring ",L" with ",L"（feat.",L"(feat."};
    size_t cut=std::wstring::npos;
    for(const auto&m:featMarks){
        size_t p=lower.find(m);
        if(p!=std::wstring::npos&&(cut==std::wstring::npos||p<cut))cut=p;
    }
    if(cut!=std::wstring::npos&&cut>0)s.resize(cut);
    return trim_copy(s);
}

static size_t levenshtein_distance(const std::wstring&a,const std::wstring&b){
    if(a.empty())return b.size();
    if(b.empty())return a.size();
    std::vector<size_t> prev(b.size()+1),cur(b.size()+1);
    for(size_t j=0;j<=b.size();++j)prev[j]=j;
    for(size_t i=1;i<=a.size();++i){
        cur[0]=i;
        for(size_t j=1;j<=b.size();++j){
            size_t cost=a[i-1]==b[j-1]?0:1;
            cur[j]=std::min(std::min(cur[j-1]+1,prev[j]+1),prev[j-1]+cost);
        }
        prev.swap(cur);
    }
    return prev[b.size()];
}

static double fuzzy_similarity(const std::wstring&a,const std::wstring&b){
    std::wstring x=normalize(a),y=normalize(b);
    if(x.empty()||y.empty())return 0.0;
    if(x==y)return 1.0;

    double best=0.0;
    size_t minLen=std::min(x.size(),y.size());
    size_t maxLen=std::max(x.size(),y.size());

    if(x.find(y)!=std::wstring::npos||y.find(x)!=std::wstring::npos){
        double ratio=maxLen?((double)minLen/(double)maxLen):0.0;
        best=std::max(best,0.76+0.20*ratio);
    }

    // Cap work for pathological metadata while retaining the useful prefix.
    if(x.size()>160)x.resize(160);
    if(y.size()>160)y.resize(160);
    maxLen=std::max(x.size(),y.size());
    if(maxLen>0){
        size_t d=levenshtein_distance(x,y);
        double lev=1.0-(double)d/(double)maxLen;
        if(lev<0)lev=0;
        best=std::max(best,lev);
    }
    return best;
}

static double title_similarity(const SearchCandidate&c,const std::wstring&t,const std::wstring&a){
    std::wstring tClean=strip_version_tags(t);
    std::wstring cClean=strip_version_tags(c.title);
    double best=std::max(fuzzy_similarity(c.title,t),fuzzy_similarity(cClean,tClean));

    // Some LRCLIB entries store "Artist - Title" in trackName.
    std::wstring cn=normalize(cClean),an=normalize(a);
    if(!an.empty()&&cn.size()>an.size()&&cn.find(an)==0){
        std::wstring stripped=cClean.substr(std::min(cClean.size(),a.size()));
        while(!stripped.empty()&&(iswspace(stripped.front())||stripped.front()==L'-'||stripped.front()==L'–'||stripped.front()==L'—'))stripped.erase(stripped.begin());
        best=std::max(best,fuzzy_similarity(stripped,tClean));
    }
    return best;
}

static int version_mismatch_penalty(const std::wstring&candidate,const std::wstring&requested){
    static const std::vector<std::wstring> kKinds={
        L"live",L"remix",L"acoustic",L"instrumental",L"karaoke",L"demo",
        L"现场",L"演唱会",L"混音",L"伴奏",L"纯音乐"
    };
    int p=0;
    for(const auto&k:kKinds){
        bool c=has_any_word(candidate,{k});
        bool r=has_any_word(requested,{k});
        if(c!=r)p+=7;
    }
    return std::min(21,p);
}

struct CandidateRank {
    int score=-999;
    double titleSim=0;
    double artistSim=0;
    double durationDiff=9999;
};

static CandidateRank candidate_rank(const SearchCandidate&c,const std::wstring&t,const std::wstring&a,const std::wstring&al,double dur){
    CandidateRank r;
    r.titleSim=title_similarity(c,t,a);
    r.artistSim=a.empty()?1.0:fuzzy_similarity(c.artist,a);
    double albumSim=al.empty()?0.0:fuzzy_similarity(c.album,al);

    int score=(int)std::lround(r.titleSim*58.0);
    if(!a.empty())score+=(int)std::lround(r.artistSim*24.0);
    if(!al.empty())score+=(int)std::lround(albumSim*6.0);

    if(dur>0&&c.duration>0){
        r.durationDiff=fabs(c.duration-dur);
        if(r.durationDiff<=2)score+=24;
        else if(r.durationDiff<=5)score+=18;
        else if(r.durationDiff<=10)score+=11;
        else if(r.durationDiff<=20)score+=5;
        else if(r.durationDiff<=45)score-=5;
        else if(r.durationDiff<=90)score-=16;
        else score-=30;
    }

    score-=version_mismatch_penalty(c.title,t);
    if(!c.synced.empty())score+=5;
    else if(!c.plain.empty())score+=1;

    r.score=score;
    return r;
}

static bool same_candidate(const SearchCandidate&a,const SearchCandidate&b){
    if(!norm_eq(a.title,b.title))return false;
    if(!norm_eq(a.artist,b.artist))return false;
    if(a.duration>0&&b.duration>0&&fabs(a.duration-b.duration)>1.0)return false;
    return true;
}

static void add_search_candidates(std::vector<SearchCandidate>&dest,const std::string&json){
    for(auto&o:json_array_objects(json)){
        SearchCandidate c;
        c.title=json_string_field(o,"trackName");
        c.artist=json_string_field(o,"artistName");
        c.album=json_string_field(o,"albumName");
        c.synced=json_string_field(o,"syncedLyrics");
        c.plain=json_string_field(o,"plainLyrics");
        c.duration=json_number_field(o,"duration");
        if(c.title.empty()||(c.synced.empty()&&c.plain.empty()))continue;
        bool dup=false;
        for(const auto&e:dest){if(same_candidate(e,c)){dup=true;break;}}
        if(!dup)dest.push_back(std::move(c));
    }
}

static bool request_candidates(const std::string&request,std::vector<SearchCandidate>&dest,bool&rateLimited){
    HttpResult h=http_get_lrclib(request);
    if(h.status==429){rateLimited=true;return false;}
    if(h.status!=200)return false;
    add_search_candidates(dest,h.body);
    return true;
}

static OnlineLyricsResult fetch_online_lyrics(const std::string& path,const std::wstring& title,const std::wstring& artist,const std::wstring& album,double duration){
    OnlineLyricsResult out;
    out.path=path;
    if(title.empty()){
        out.status=L"缺少歌曲标题，无法在线匹配";
        return out;
    }

    // Stage 1: LRCLIB signature lookup. Keep this strict to avoid wrong lyrics.
    if(!artist.empty()){
        std::ostringstream exact;
        exact<<"/api/get?track_name="<<url_encode_utf8(title)
             <<"&artist_name="<<url_encode_utf8(artist);
        if(!album.empty())exact<<"&album_name="<<url_encode_utf8(album);
        if(duration>0)exact<<"&duration="<<(int)std::lround(duration);

        HttpResult h=http_get_lrclib(exact.str());
        if(h.status==200){
            std::wstring sync=json_string_field(h.body,"syncedLyrics");
            std::wstring plain=json_string_field(h.body,"plainLyrics");
            out.lyrics=!sync.empty()?sync:plain;
            if(!out.lyrics.empty()){
                out.status=L"在线精确匹配 · LRCLIB";
                out.shouldCache=true;
                return out;
            }
        }
        if(h.status==429){
            out.status=L"在线歌词请求过快，请稍后重试";
            return out;
        }
    }

    std::vector<SearchCandidate> candidates;
    bool rateLimited=false;
    std::wstring cleanTitle=strip_version_tags(title);
    std::wstring cleanArtist=trim_copy(artist);

    // Stage 2: structured search with original metadata.
    {
        std::ostringstream q;
        q<<"/api/search?track_name="<<url_encode_utf8(title);
        if(!artist.empty())q<<"&artist_name="<<url_encode_utf8(artist);
        if(!album.empty())q<<"&album_name="<<url_encode_utf8(album);
        request_candidates(q.str(),candidates,rateLimited);
    }
    if(rateLimited){out.status=L"在线歌词请求过快，请稍后重试";return out;}

    // Stage 3: remove track number/version/remaster/live/feat tags and search again.
    if(cleanTitle!=title){
        Sleep(250);
        std::ostringstream q;
        q<<"/api/search?track_name="<<url_encode_utf8(cleanTitle);
        if(!cleanArtist.empty())q<<"&artist_name="<<url_encode_utf8(cleanArtist);
        request_candidates(q.str(),candidates,rateLimited);
        if(rateLimited){out.status=L"在线歌词请求过快，请稍后重试";return out;}
    }

    // Stage 4: global keyword fuzzy search. q searches across title/artist/album.
    // This catches spelling/metadata differences such as "Artist - Title",
    // localized album tags and minor punctuation differences.
    Sleep(250);
    {
        std::wstring query=cleanTitle;
        if(!cleanArtist.empty())query+=L" "+cleanArtist;
        std::ostringstream q;
        q<<"/api/search?q="<<url_encode_utf8(query);
        request_candidates(q.str(),candidates,rateLimited);
    }
    if(rateLimited){out.status=L"在线歌词请求过快，请稍后重试";return out;}

    // Stage 5: title-only global search only when earlier stages are sparse.
    if(candidates.size()<4&&cleanTitle.size()>=2){
        Sleep(250);
        std::ostringstream q;
        q<<"/api/search?q="<<url_encode_utf8(cleanTitle);
        request_candidates(q.str(),candidates,rateLimited);
        if(rateLimited){out.status=L"在线歌词请求过快，请稍后重试";return out;}
    }

    int best=-999;
    CandidateRank bestRank;
    SearchCandidate pick;
    for(const auto&c:candidates){
        CandidateRank rank=candidate_rank(c,title,artist,album,duration);
        if(rank.score>best){
            best=rank.score;
            bestRank=rank;
            pick=c;
        }
    }

    // Safety gates: fuzzy does not mean accepting unrelated lyrics.
    bool credible=false;
    if(best>=62&&bestRank.titleSim>=0.58)credible=true;
    if(best>=56&&bestRank.titleSim>=0.82&&bestRank.durationDiff<=10)credible=true;
    if(!artist.empty()&&bestRank.artistSim<0.22&&bestRank.durationDiff>5)credible=false;

    if(credible){
        out.lyrics=!pick.synced.empty()?pick.synced:pick.plain;
        if(!out.lyrics.empty()){
            int confidence=best;
            if(confidence<0)confidence=0;
            if(confidence>99)confidence=99;
            wchar_t buf[96]{};
            swprintf_s(buf,L"模糊匹配 · LRCLIB · %d%%",confidence);
            out.status=buf;
            out.shouldCache=true;
            return out;
        }
    }

    if(candidates.empty())out.status=L"未搜索到在线歌词候选";
    else out.status=L"找到候选，但相似度不足，已避免错误匹配";
    return out;
}

class pro_lyrics_instance : public ui_element_instance {
public:
    pro_lyrics_instance(HWND parent, ui_element_config::ptr, ui_element_instance_callback_ptr cb)
        : m_callback(cb), m_alive(std::make_shared<std::atomic_bool>(true)) {
        register_class();
        m_hwnd = CreateWindowExW(
            0, kClassName, L"",
            WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
            0, 0, 440, 340, parent, nullptr, core_api::get_my_instance(), this);
        if (!m_hwnd) throw std::runtime_error("Could not create lyrics panel");
        SetTimer(m_hwnd, 1, 250, nullptr);
    }

    ~pro_lyrics_instance() {
        m_alive->store(false);
        destroy_fonts();
        if (m_hwnd && IsWindow(m_hwnd)) DestroyWindow(m_hwnd);
    }

    HWND get_wnd() override { return m_hwnd; }
    void set_configuration(ui_element_config::ptr) override {}
    ui_element_config::ptr get_configuration() override {
        return ui_element_config::g_create_empty(guid_pro_lyrics);
    }
    GUID get_guid() override { return guid_pro_lyrics; }
    GUID get_subclass() override { return ui_element_subclass_utility; }

    ui_element_min_max_info get_min_max_info() override {
        ui_element_min_max_info i;
        i.m_min_width = 280;
        i.m_min_height = 210;
        return i;
    }

    void notify(const GUID& what, t_size, const void*, t_size) override {
        if (what == ui_element_notify_colors_changed || what == ui_element_notify_font_changed) {
            InvalidateRect(m_hwnd, nullptr, TRUE);
        }
    }

private:
    struct LineHit {
        int index = -1;
        RECT rc{};
    };

    HWND m_hwnd{};
    ui_element_instance_callback_ptr m_callback;

    HFONT m_font{};
    HFONT m_fontCurrent{};
    HFONT m_fontSmall{};
    HFONT m_fontTiny{};

    std::vector<LyricLine> m_lines;
    std::vector<LineHit> m_hits;
    std::string m_path;
    std::wstring m_source;
    std::wstring m_status;

    bool m_onlineFetching = false;
    std::shared_ptr<std::atomic_bool> m_alive;

    bool m_dragging = false;
    bool m_dragMoved = false;
    POINT m_dragStart{};
    int m_dragAnchorIndex = -1;
    int m_dragTargetIndex = -1;
    int m_hoverIndex = -1;

    std::wstring m_actionText;
    ULONGLONG m_actionUntil = 0;

    static constexpr int kHeaderHeight = 48;
    static constexpr int kFooterHeight = 34;
    static constexpr int kLineHeight = 44;

    COLORREF bg() const {
        return m_callback.is_valid()
            ? (COLORREF)m_callback->query_std_color(ui_color_background)
            : RGB(248, 249, 251);
    }

    COLORREF text() const {
        return m_callback.is_valid()
            ? (COLORREF)m_callback->query_std_color(ui_color_text)
            : RGB(38, 42, 48);
    }

    static COLORREF accent() { return RGB(111, 145, 159); }
    static COLORREF accentSoft() { return RGB(232, 240, 242); }
    static COLORREF muted() { return RGB(116, 133, 140); }
    static COLORREF faint() { return RGB(164, 177, 182); }
    static COLORREF divider() { return RGB(219, 227, 230); }

    static COLORREF blend(COLORREF a, COLORREF b, int pctB) {
        pctB = std::max(0, std::min(100, pctB));
        const int pctA = 100 - pctB;
        return RGB(
            (GetRValue(a) * pctA + GetRValue(b) * pctB) / 100,
            (GetGValue(a) * pctA + GetGValue(b) * pctB) / 100,
            (GetBValue(a) * pctA + GetBValue(b) * pctB) / 100
        );
    }

    static std::wstring format_time(double seconds) {
        if (seconds < 0) seconds = 0;
        int total = (int)std::lround(seconds);
        int m = total / 60;
        int s = total % 60;
        wchar_t buf[32]{};
        swprintf_s(buf, L"%d:%02d", m, s);
        return buf;
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
        wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;

        RegisterClassExW(&wc);
        once = true;
    }

    void init_fonts() {
        if (m_font) return;

        m_font = CreateFontW(
            -18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0,
            L"Microsoft YaHei UI");

        m_fontCurrent = CreateFontW(
            -23, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0,
            L"Microsoft YaHei UI");

        m_fontSmall = CreateFontW(
            -13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0,
            L"Microsoft YaHei UI");

        m_fontTiny = CreateFontW(
            -12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0,
            L"Microsoft YaHei UI");
    }

    void destroy_fonts() {
        if (m_font) { DeleteObject(m_font); m_font = nullptr; }
        if (m_fontCurrent) { DeleteObject(m_fontCurrent); m_fontCurrent = nullptr; }
        if (m_fontSmall) { DeleteObject(m_fontSmall); m_fontSmall = nullptr; }
        if (m_fontTiny) { DeleteObject(m_fontTiny); m_fontTiny = nullptr; }
    }

    static void fill_round_rect(HDC dc, const RECT& r, int radius, COLORREF color) {
        HBRUSH brush = CreateSolidBrush(color);
        HPEN pen = CreatePen(PS_NULL, 0, color);
        HGDIOBJ oldBrush = SelectObject(dc, brush);
        HGDIOBJ oldPen = SelectObject(dc, pen);
        RoundRect(dc, r.left, r.top, r.right, r.bottom, radius, radius);
        SelectObject(dc, oldPen);
        SelectObject(dc, oldBrush);
        DeleteObject(pen);
        DeleteObject(brush);
    }

    static void fill_rect(HDC dc, const RECT& r, COLORREF color) {
        HBRUSH brush = CreateSolidBrush(color);
        FillRect(dc, &r, brush);
        DeleteObject(brush);
    }

    void draw_text_line(
        HDC dc,
        const std::wstring& s,
        RECT r,
        HFONT font,
        COLORREF color,
        UINT flags = DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS) {

        HGDIOBJ old = SelectObject(dc, font);
        SetTextColor(dc, color);
        SetBkMode(dc, TRANSPARENT);
        DrawTextW(dc, s.c_str(), -1, &r, flags);
        SelectObject(dc, old);
    }

    void begin_online_fetch() {
        if (m_onlineFetching || m_path.empty()) return;

        std::wstring title = pro_format("$if2(%title%,%filename%)");
        std::wstring artist = pro_format("$if2(%artist%,)");
        std::wstring album = pro_format("$if2(%album%,)");
        double dur = playback_control::get()->playback_get_length();

        m_onlineFetching = true;
        m_status = L"正在在线匹配歌词…";

        std::string path = m_path;
        HWND hwnd = m_hwnd;
        auto alive = m_alive;

        std::thread([hwnd, alive, path, title, artist, album, dur]() {
            auto* r = new OnlineLyricsResult(
                fetch_online_lyrics(path, title, artist, album, dur));
            if (!alive->load() ||
                !PostMessageW(hwnd, WM_PRO_LYRICS_ONLINE, 0, (LPARAM)r)) {
                delete r;
            }
        }).detach();
    }

    void reset_interaction() {
        m_dragging = false;
        m_dragMoved = false;
        m_dragAnchorIndex = -1;
        m_dragTargetIndex = -1;
        m_hoverIndex = -1;
        m_hits.clear();
    }

    void reload_if_needed() {
        metadb_handle_ptr now;

        if (!playback_control::get()->get_now_playing(now) || now.is_empty()) {
            if (!m_path.empty()) {
                m_path.clear();
                m_lines.clear();
                m_source.clear();
                m_status.clear();
                m_onlineFetching = false;
                reset_interaction();
                InvalidateRect(m_hwnd, nullptr, TRUE);
            }
            return;
        }

        const char* p = now->get_path();
        if (!p || m_path == p) return;

        m_path = p;
        m_lines.clear();
        m_source.clear();
        m_status.clear();
        m_onlineFetching = false;
        reset_interaction();

        std::wstring embedded =
            pro_format("$if2(%lyrics%,%unsynced lyrics%)");

        if (!embedded.empty() && embedded != L"?") {
            m_source = embedded;
            m_status = L"内嵌歌词";
        }

        if (m_source.empty()) {
            std::wstring np = native_path(p);

            if (!np.empty()) {
                auto lrc = stem_lrc(np);
                m_source = read_text(lrc);

                if (!m_source.empty()) {
                    m_status = L"同目录 LRC";
                }

                if (m_source.empty()) {
                    size_t slash = np.find_last_of(L"\\/");
                    std::wstring dir =
                        slash == std::wstring::npos
                        ? L""
                        : np.substr(0, slash + 1);

                    std::wstring file =
                        slash == std::wstring::npos
                        ? np
                        : np.substr(slash + 1);

                    m_source =
                        read_text(dir + L"lyrics\\" + stem_lrc(file));

                    if (!m_source.empty()) {
                        m_status = L"本地歌词缓存";
                    }
                }
            }
        }

        if (!m_source.empty()) {
            m_lines = parse_lyrics(m_source);
        } else {
            begin_online_fetch();
        }

        InvalidateRect(m_hwnd, nullptr, TRUE);
    }

    int current_index(double pos) const {
        if (m_lines.empty()) return -1;
        if (m_lines.front().time < 0) return -1;

        int idx = 0;
        for (size_t i = 0; i < m_lines.size(); ++i) {
            if (m_lines[i].time <= pos) idx = (int)i;
            else break;
        }
        return idx;
    }

    int line_at_point(int x, int y) const {
        POINT p{ x, y };
        for (const auto& hit : m_hits) {
            if (PtInRect(&hit.rc, p)) return hit.index;
        }
        return -1;
    }

    void show_action(const std::wstring& textValue, DWORD durationMs = 1500) {
        m_actionText = textValue;
        m_actionUntil = GetTickCount64() + durationMs;
    }

    void seek_to_index(int idx) {
        if (idx < 0 || (size_t)idx >= m_lines.size()) return;
        if (m_lines[(size_t)idx].time < 0) return;

        auto pc = playback_control::get();
        if (!pc->playback_can_seek()) {
            show_action(L"当前音频不可跳转");
            return;
        }

        const double t = std::max(0.0, m_lines[(size_t)idx].time);
        pc->playback_seek(t);

        show_action(L"已跳转到 " + format_time(t));
        InvalidateRect(m_hwnd, nullptr, TRUE);
    }

    int drag_target_for_y(int y) const {
        if (m_dragAnchorIndex < 0 || m_lines.empty()) return -1;

        const int dy = y - m_dragStart.y;
        int shift = 0;

        if (std::abs(dy) >= 6) {
            if (dy < 0)
                shift = (-dy + kLineHeight / 2) / kLineHeight;
            else
                shift = -(dy + kLineHeight / 2) / kLineHeight;
        }

        int target = m_dragAnchorIndex + shift;
        target = std::max(0, std::min((int)m_lines.size() - 1, target));
        return target;
    }

    void start_drag(int x, int y) {
        if (m_lines.empty() || m_lines.front().time < 0) return;

        int hit = line_at_point(x, y);
        if (hit < 0) {
            const double pos =
                playback_control::get()->is_playing()
                ? playback_control::get()->playback_get_position()
                : 0;
            hit = current_index(pos);
        }

        if (hit < 0) return;

        m_dragging = true;
        m_dragMoved = false;
        m_dragStart = POINT{ x, y };
        m_dragAnchorIndex = hit;
        m_dragTargetIndex = hit;

        SetCapture(m_hwnd);
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }

    void update_drag(int x, int y) {
        if (!m_dragging) return;

        const int dx = x - m_dragStart.x;
        const int dy = y - m_dragStart.y;

        if (std::abs(dx) > 4 || std::abs(dy) > 4) {
            m_dragMoved = true;
        }

        int target = drag_target_for_y(y);
        if (target != m_dragTargetIndex) {
            m_dragTargetIndex = target;
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
    }

    void finish_drag(int x, int y) {
        if (!m_dragging) return;

        int target = m_dragTargetIndex;

        if (!m_dragMoved) {
            int hit = line_at_point(x, y);
            if (hit >= 0) target = hit;
        }

        m_dragging = false;
        m_dragMoved = false;
        m_dragAnchorIndex = -1;
        m_dragTargetIndex = -1;

        if (GetCapture() == m_hwnd) ReleaseCapture();

        seek_to_index(target);
    }

    void update_hover(int x, int y) {
        if (m_dragging) return;

        int idx = line_at_point(x, y);
        if (idx != m_hoverIndex) {
            m_hoverIndex = idx;
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }

        TRACKMOUSEEVENT tme{};
        tme.cbSize = sizeof(tme);
        tme.dwFlags = TME_LEAVE;
        tme.hwndTrack = m_hwnd;
        TrackMouseEvent(&tme);
    }

    void draw_header(HDC dc, int w) {
        const COLORREF panelBg = bg();

        RECT header{ 0, 0, w, kHeaderHeight };
        fill_rect(dc, header, panelBg);

        RECT titleRc{ 18, 8, 130, 39 };
        draw_text_line(
            dc, L"歌词", titleRc, m_fontCurrent, text(),
            DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        if (!m_status.empty()) {
            RECT sr{ 130, 10, w - 18, 38 };

            SIZE sz{};
            HGDIOBJ old = SelectObject(dc, m_fontTiny);
            GetTextExtentPoint32W(
                dc, m_status.c_str(), (int)m_status.size(), &sz);
            SelectObject(dc, old);

            const int measuredW = static_cast<int>(sz.cx);
            int desiredW = measuredW + 22;
            if (desiredW < 92) desiredW = 92;

            int availableW = w - 156;
            if (availableW < 92) availableW = 92;

            const int chipW =
                desiredW < availableW
                ? desiredW
                : availableW;

            RECT chip{ w - 18 - chipW, 11, w - 18, 36 };
            fill_round_rect(
                dc, chip, 12,
                blend(panelBg, RGB(219, 231, 235), 64));

            RECT chipText = chip;
            chipText.left += 10;
            chipText.right -= 10;

            draw_text_line(
                dc, m_status, chipText, m_fontTiny, muted(),
                DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        }

        RECT line{ 16, kHeaderHeight - 1, w - 16, kHeaderHeight };
        fill_rect(dc, line, divider());
    }

    void draw_footer(HDC dc, int w, int h, bool synced) {
        RECT separator{
            16,
            h - kFooterHeight,
            w - 16,
            h - kFooterHeight + 1
        };
        fill_rect(dc, separator, divider());

        std::wstring hint;

        if (GetTickCount64() < m_actionUntil && !m_actionText.empty()) {
            hint = m_actionText;
        } else if (m_dragging &&
                   m_dragTargetIndex >= 0 &&
                   (size_t)m_dragTargetIndex < m_lines.size()) {
            hint =
                L"松开跳转 · " +
                format_time(m_lines[(size_t)m_dragTargetIndex].time);
        } else if (m_hoverIndex >= 0 &&
                   (size_t)m_hoverIndex < m_lines.size() &&
                   m_lines[(size_t)m_hoverIndex].time >= 0) {
            hint =
                format_time(m_lines[(size_t)m_hoverIndex].time) +
                L" · 单击跳转 / 上下拖动定位";
        } else if (synced) {
            hint = L"单击歌词跳转 · 上下拖动歌词定位";
        } else {
            hint = L"当前歌词没有时间轴，无法按歌词跳转";
        }

        RECT footer{
            18,
            h - kFooterHeight + 4,
            w - 18,
            h - 4
        };

        draw_text_line(
            dc, hint, footer, m_fontTiny,
            synced ? muted() : faint(),
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }

    void paint_synced(HDC dc, int w, int h, int currentIdx) {
        const int top = kHeaderHeight + 5;
        const int bottom = h - kFooterHeight - 4;
        const int available = std::max(80, bottom - top);
        const int center = top + available / 2;

        int focusIdx = currentIdx;
        if (m_dragging && m_dragTargetIndex >= 0) {
            focusIdx = m_dragTargetIndex;
        }
        if (focusIdx < 0) focusIdx = 0;

        int visibleEachSide = std::max(2, available / kLineHeight / 2 + 1);
        int start = std::max(0, focusIdx - visibleEachSide);
        int end = std::min(
            (int)m_lines.size() - 1,
            focusIdx + visibleEachSide);

        m_hits.clear();

        int y = center - (focusIdx - start) * kLineHeight;

        for (int i = start; i <= end; ++i) {
            RECT row{
                18,
                y - 2,
                w - 18,
                y + kLineHeight - 3
            };

            bool isCurrent = (i == currentIdx);
            bool isTarget =
                m_dragging && (i == m_dragTargetIndex);
            bool isHover =
                !m_dragging && (i == m_hoverIndex);

            if (isTarget) {
                fill_round_rect(dc, row, 14, accentSoft());

                RECT bar{
                    row.left + 5,
                    row.top + 10,
                    row.left + 9,
                    row.bottom - 10
                };
                fill_round_rect(dc, bar, 3, accent());
            } else if (isHover) {
                fill_round_rect(
                    dc, row, 12,
                    blend(bg(), RGB(235, 237, 241), 50));
            }

            RECT textRc = row;
            textRc.left += 18;
            textRc.right -= 18;

            COLORREF c = muted();
            HFONT f = m_font;

            const int distance =
                std::abs(i - focusIdx);

            if (isTarget) {
                c = accent();
                f = m_fontCurrent;
            } else if (isCurrent) {
                c = accent();
                f = m_fontCurrent;
            } else if (distance == 1) {
                c = blend(text(), bg(), 48);
            } else if (distance == 2) {
                c = blend(text(), bg(), 66);
            } else {
                c = blend(text(), bg(), 76);
            }

            draw_text_line(
                dc,
                m_lines[(size_t)i].text.empty()
                    ? L"♪"
                    : m_lines[(size_t)i].text,
                textRc, f, c);

            if ((isTarget || isHover) &&
                m_lines[(size_t)i].time >= 0) {
                std::wstring stamp =
                    format_time(m_lines[(size_t)i].time);

                RECT timeRc{
                    row.right - 58,
                    row.top + 6,
                    row.right - 8,
                    row.top + 28
                };

                draw_text_line(
                    dc, stamp, timeRc,
                    m_fontTiny,
                    isTarget ? accent() : faint(),
                    DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
            }

            m_hits.push_back({ i, row });
            y += kLineHeight;
        }

        if (m_dragging && m_dragTargetIndex >= 0) {
            RECT marker{
                24,
                center - 1,
                w - 24,
                center + 1
            };
            fill_rect(dc, marker, blend(accent(), bg(), 58));
        }
    }

    void paint_plain(HDC dc, int w, int h) {
        m_hits.clear();

        const int top = kHeaderHeight + 10;
        const int bottom = h - kFooterHeight - 6;
        int y = top;

        for (size_t i = 0;
             i < m_lines.size() && y + 34 < bottom;
             ++i) {

            RECT row{
                22,
                y,
                w - 22,
                y + 32
            };

            draw_text_line(
                dc, m_lines[i].text,
                row, m_font,
                blend(text(), bg(), 32));

            y += 36;
        }
    }

    void paint(HDC dc) {
        init_fonts();
        reload_if_needed();

        RECT rc{};
        GetClientRect(m_hwnd, &rc);

        const int w = rc.right;
        const int h = rc.bottom;

        fill_rect(dc, rc, bg());
        draw_header(dc, w);

        if (m_lines.empty()) {
            RECT msg{
                24,
                kHeaderHeight + 20,
                w - 24,
                h - kFooterHeight - 14
            };

            draw_text_line(
                dc,
                m_onlineFetching
                    ? L"正在在线搜索并匹配歌词…"
                    : L"暂无歌词",
                msg,
                m_font,
                muted());

            draw_footer(dc, w, h, false);
            return;
        }

        const bool synced =
            !m_lines.empty() && m_lines.front().time >= 0;

        if (!synced) {
            paint_plain(dc, w, h);
            draw_footer(dc, w, h, false);
            return;
        }

        const double pos =
            playback_control::get()->is_playing()
            ? playback_control::get()->playback_get_position()
            : 0;

        int idx = current_index(pos);
        paint_synced(dc, w, h, idx);
        draw_footer(dc, w, h, true);
    }

    void accept_online(OnlineLyricsResult* r) {
        std::unique_ptr<OnlineLyricsResult> hold(r);

        m_onlineFetching = false;

        if (!r || r->path != m_path) return;

        m_status = r->status;

        if (!r->lyrics.empty()) {
            m_source = r->lyrics;
            m_lines = parse_lyrics(m_source);

            if (r->shouldCache) {
                auto cp = cache_path_for_track(m_path);

                if (!cp.empty() &&
                    write_utf8_text(cp, m_source)) {
                    m_status += L" · 已缓存";
                }
            }
        }

        reset_interaction();
        InvalidateRect(m_hwnd, nullptr, TRUE);
    }

    static LRESULT CALLBACK wnd_proc(
        HWND h, UINT m, WPARAM w, LPARAM l) {

        auto* self =
            (pro_lyrics_instance*)GetWindowLongPtrW(
                h, GWLP_USERDATA);

        if (m == WM_NCCREATE) {
            auto* cs = (CREATESTRUCTW*)l;
            self =
                (pro_lyrics_instance*)cs->lpCreateParams;

            SetWindowLongPtrW(
                h, GWLP_USERDATA, (LONG_PTR)self);

            if (self) self->m_hwnd = h;
        }

        if (!self) return DefWindowProcW(h, m, w, l);

        switch (m) {
        case WM_PRO_LYRICS_ONLINE:
            self->accept_online((OnlineLyricsResult*)l);
            return 0;

        case WM_TIMER:
            self->reload_if_needed();
            if (GetTickCount64() >= self->m_actionUntil) {
                self->m_actionText.clear();
            }
            InvalidateRect(h, nullptr, FALSE);
            return 0;

        case WM_LBUTTONDOWN:
            SetFocus(h);
            self->start_drag(GET_X_LPARAM(l), GET_Y_LPARAM(l));
            return 0;

        case WM_MOUSEMOVE:
            if (self->m_dragging) {
                self->update_drag(
                    GET_X_LPARAM(l),
                    GET_Y_LPARAM(l));
            } else {
                self->update_hover(
                    GET_X_LPARAM(l),
                    GET_Y_LPARAM(l));
            }
            return 0;

        case WM_LBUTTONUP:
            self->finish_drag(
                GET_X_LPARAM(l),
                GET_Y_LPARAM(l));
            return 0;

        case WM_LBUTTONDBLCLK: {
            int idx = self->line_at_point(
                GET_X_LPARAM(l),
                GET_Y_LPARAM(l));

            if (idx >= 0) self->seek_to_index(idx);
            return 0;
        }

        case WM_MOUSELEAVE:
            self->m_hoverIndex = -1;
            InvalidateRect(h, nullptr, FALSE);
            return 0;

        case WM_CAPTURECHANGED:
            if (self->m_dragging) {
                self->m_dragging = false;
                self->m_dragMoved = false;
                self->m_dragAnchorIndex = -1;
                self->m_dragTargetIndex = -1;
                InvalidateRect(h, nullptr, FALSE);
            }
            return 0;

        case WM_SETCURSOR:
            if (LOWORD(l) == HTCLIENT &&
                !self->m_lines.empty() &&
                self->m_lines.front().time >= 0) {
                SetCursor(LoadCursorW(nullptr, IDC_HAND));
                return TRUE;
            }
            break;

        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC dc = BeginPaint(h, &ps);
            self->paint(dc);
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

class pro_lyrics_element : public ui_element {
public:
    GUID get_guid() override { return guid_pro_lyrics; }
    GUID get_subclass() override {
        return ui_element_subclass_utility;
    }

    void get_name(pfc::string_base& out) override {
        out = u8"Pro Lyrics Interactive";
    }

    ui_element_instance_ptr instantiate(
        HWND p,
        ui_element_config::ptr c,
        ui_element_instance_callback_ptr cb) override {

        return new service_impl_t<pro_lyrics_instance>(
            p, c, cb);
    }

    ui_element_config::ptr
    get_default_configuration() override {
        return ui_element_config::g_create_empty(
            guid_pro_lyrics);
    }

    ui_element_children_enumerator_ptr
    enumerate_children(ui_element_config::ptr) override {
        return nullptr;
    }

    bool get_description(pfc::string_base& out) override {
        out =
            u8"专业交互式歌词：内嵌/本地 LRC + LRCLIB 在线自动匹配，"
            u8"支持当前行高亮、单击歌词跳转、上下拖动定位并松开跳转、"
            u8"鼠标悬停时间提示与本地缓存。";
        return true;
    }
};

static service_factory_single_t<pro_lyrics_element> g_factory;
}
