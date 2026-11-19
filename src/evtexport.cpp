// evtexport.cpp - Lab06, вариант 21: экспорт событий Windows в CSV и JSON
// STUDENT_ID = 90, SEED = 9730
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winevt.h>
#include <cstdio>
#include <cstdarg>
#include <cstdlib>
#include <string>
#include <vector>
#include <map>
#include <fstream>
#include <algorithm>

#pragma comment(lib, "wevtapi.lib")

// ---------------------------------------------------------------- утилиты
static FILE* g_log = nullptr;

static std::string ToUtf8(const std::wstring& w) {
    if (w.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

// Журнал: [чч:мм:сс.мс] сообщение -> консоль и файл lab06.log
static void Log(const wchar_t* fmt, ...) {
    SYSTEMTIME st; GetLocalTime(&st);
    wchar_t body[2048];
    va_list a; va_start(a, fmt);
    _vsnwprintf_s(body, _countof(body), _TRUNCATE, fmt, a);
    va_end(a);
    wchar_t line[2300];
    _snwprintf_s(line, _countof(line), _TRUNCATE, L"[%02d:%02d:%02d.%03d] %ls\n",
        st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, body);
    std::string u = ToUtf8(line);
    fwrite(u.data(), 1, u.size(), stdout);
    if (g_log) { fwrite(u.data(), 1, u.size(), g_log); fflush(g_log); }
}

// Ошибка API: функция, операция, код, текстовое описание
static void LogApiError(const wchar_t* func, const wchar_t* op, DWORD code) {
    wchar_t msg[512] = L"";
    DWORD n = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, 0, msg, _countof(msg), nullptr);
    while (n > 0 && (msg[n - 1] == L'\r' || msg[n - 1] == L'\n' || msg[n - 1] == L' ')) msg[--n] = 0;
    Log(L"ERROR func=%ls op=%ls code=%lu text=%ls", func, op, code, n ? msg : L"(нет описания)");
    if (code == ERROR_ACCESS_DENIED)
        Log(L"HINT недостаточно прав для чтения этого журнала (попробуйте Application)");
    else if (code == ERROR_EVT_CHANNEL_NOT_FOUND)
        Log(L"HINT журнал с таким именем не найден");
}

static double g_freq = 0;
static double NowMs() {
    LARGE_INTEGER c; QueryPerformanceCounter(&c);
    return (double)c.QuadPart * 1000.0 / g_freq;
}

// RAII для дескрипторов Event Log
struct EvtH {
    EVT_HANDLE h;
    explicit EvtH(EVT_HANDLE x = nullptr) : h(x) {}
    ~EvtH() { if (h) EvtClose(h); }
    EvtH(const EvtH&) = delete;
    EvtH& operator=(const EvtH&) = delete;
};

// ---------------------------------------------------------------- данные
struct EventRec {
    ULONGLONG recordId = 0;
    std::wstring timeUtc, provider, channel, computer, message;
    DWORD level = 0, eventId = 0;
};

static const wchar_t* LevelName(DWORD l) {
    switch (l) {
    case 1: return L"Critical";
    case 2: return L"Error";
    case 3: return L"Warning";
    case 4: return L"Information";
    case 5: return L"Verbose";
    default: return L"LogAlways";
    }
}

static ULONGLONG VarUInt(const EVT_VARIANT& v) {
    switch (v.Type & EVT_VARIANT_TYPE_MASK) {
    case EvtVarTypeByte:   return v.ByteVal;
    case EvtVarTypeUInt16: return v.UInt16Val;
    case EvtVarTypeUInt32: return v.UInt32Val;
    case EvtVarTypeUInt64: return v.UInt64Val;
    default: return 0;
    }
}
static std::wstring VarStr(const EVT_VARIANT& v) {
    if ((v.Type & EVT_VARIANT_TYPE_MASK) == EvtVarTypeString && v.StringVal) return v.StringVal;
    return L"";
}

static std::wstring FileTimeToIso(ULONGLONG ft) {
    FILETIME f; f.dwLowDateTime = (DWORD)(ft & 0xFFFFFFFF); f.dwHighDateTime = (DWORD)(ft >> 32);
    SYSTEMTIME st;
    if (!FileTimeToSystemTime(&f, &st)) return L"";
    wchar_t b[40];
    _snwprintf_s(b, _countof(b), _TRUNCATE, L"%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    return b;
}

// ---------------------------------------------------------------- Event Log
static bool RenderEvent(EVT_HANDLE ctx, EVT_HANDLE ev, EventRec& r) {
    DWORD used = 0, count = 0;
    std::vector<BYTE> buf(4096);
    if (!EvtRender(ctx, ev, EvtRenderEventValues, (DWORD)buf.size(), buf.data(), &used, &count)) {
        DWORD e = GetLastError();
        if (e != ERROR_INSUFFICIENT_BUFFER) { LogApiError(L"EvtRender", L"render event values", e); return false; }
        buf.resize(used);
        if (!EvtRender(ctx, ev, EvtRenderEventValues, (DWORD)buf.size(), buf.data(), &used, &count)) {
            LogApiError(L"EvtRender", L"render event values (retry)", GetLastError());
            return false;
        }
    }
    if (count < 7) { Log(L"ERROR EvtRender returned %lu values, expected 7", count); return false; }
    PEVT_VARIANT v = (PEVT_VARIANT)buf.data();
    r.provider = VarStr(v[0]);
    r.eventId = (DWORD)VarUInt(v[1]);
    r.level = (DWORD)VarUInt(v[2]);
    if ((v[3].Type & EVT_VARIANT_TYPE_MASK) == EvtVarTypeFileTime) r.timeUtc = FileTimeToIso(v[3].FileTimeVal);
    r.recordId = VarUInt(v[4]);
    r.channel = VarStr(v[5]);
    r.computer = VarStr(v[6]);
    return true;
}

// Текст сообщения; метаданные издателя кэшируются
static std::wstring FormatMessageText(std::map<std::wstring, EVT_HANDLE>& cache,
    const std::wstring& provider, EVT_HANDLE ev) {
    auto it = cache.find(provider);
    if (it == cache.end()) {
        EVT_HANDLE h = EvtOpenPublisherMetadata(nullptr, provider.c_str(), nullptr, 0, 0);
        if (!h) Log(L"WARN EvtOpenPublisherMetadata provider=%ls code=%lu (текст сообщения недоступен)",
            provider.c_str(), GetLastError());
        it = cache.emplace(provider, h).first;
    }
    if (!it->second) return L"";
    DWORD used = 0;
    EvtFormatMessage(it->second, ev, 0, 0, nullptr, EvtFormatMessageEvent, 0, nullptr, &used);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || used == 0) return L"";
    std::vector<wchar_t> b(used);
    if (!EvtFormatMessage(it->second, ev, 0, 0, nullptr, EvtFormatMessageEvent, used, b.data(), &used)) return L"";
    return std::wstring(b.data());
}

// ---------------------------------------------------------------- экспорт
static std::wstring CsvQuote(const std::wstring& s) {
    std::wstring o = L"\"";
    for (wchar_t c : s) { if (c == L'"') o += L"\"\""; else o += c; }
    o += L'"';
    return o;
}

static std::wstring JsonEscape(const std::wstring& s) {
    std::wstring o;
    for (wchar_t c : s) {
        switch (c) {
        case L'"':  o += L"\\\""; break;
        case L'\\': o += L"\\\\"; break;
        case L'\n': o += L"\\n";  break;
        case L'\r': o += L"\\r";  break;
        case L'\t': o += L"\\t";  break;
        default:
            if (c < 0x20) { wchar_t b[8]; swprintf_s(b, L"\\u%04x", (unsigned)c); o += b; }
            else o += c;
        }
    }
    return o;
}

static std::string BuildCsv(const std::vector<EventRec>& v) {
    std::wstring o;
    o += L"record_id,time_utc,provider,level,level_name,event_id,channel,computer,message\r\n";
    for (auto& e : v) {
        o += std::to_wstring(e.recordId) + L"," + CsvQuote(e.timeUtc) + L"," + CsvQuote(e.provider) + L","
            + std::to_wstring(e.level) + L"," + CsvQuote(LevelName(e.level)) + L","
            + std::to_wstring(e.eventId) + L"," + CsvQuote(e.channel) + L","
            + CsvQuote(e.computer) + L"," + CsvQuote(e.message) + L"\r\n";
    }
    return std::string("\xEF\xBB\xBF") + ToUtf8(o);   // UTF-8 с BOM (корректно открывается в Excel)
}

static std::string BuildJson(const std::vector<EventRec>& v) {
    std::wstring o = L"[\n";
    for (size_t i = 0; i < v.size(); i++) {
        const EventRec& e = v[i];
        o += L"  {\"record_id\": " + std::to_wstring(e.recordId)
            + L", \"time_utc\": \"" + JsonEscape(e.timeUtc) + L"\""
            + L", \"provider\": \"" + JsonEscape(e.provider) + L"\""
            + L", \"level\": " + std::to_wstring(e.level)
            + L", \"level_name\": \"" + LevelName(e.level) + L"\""
            + L", \"event_id\": " + std::to_wstring(e.eventId)
            + L", \"channel\": \"" + JsonEscape(e.channel) + L"\""
            + L", \"computer\": \"" + JsonEscape(e.computer) + L"\""
            + L", \"message\": \"" + JsonEscape(e.message) + L"\"}";
        o += (i + 1 < v.size()) ? L",\n" : L"\n";
    }
    o += L"]\n";
    return ToUtf8(o);
}

static bool WriteFileBytes(const std::wstring& path, const std::string& data) {
    std::ofstream f(path.c_str(), std::ios::binary | std::ios::trunc);
    if (!f) { Log(L"ERROR FILE_OPEN file=%ls", path.c_str()); return false; }
    f.write(data.data(), (std::streamsize)data.size());
    f.close();
    if (f.fail()) { Log(L"ERROR FILE_WRITE file=%ls", path.c_str()); return false; }
    Log(L"EXPORT file=%ls bytes=%zu", path.c_str(), data.size());
    return true;
}

// ---------------------------------------------------------------- параметры
struct Options {
    std::wstring log = L"Application", xpath, out = L"events", format = L"both", stats, config = L"default";
    int max = 100, level = 0, run = 1;
    bool message = true;
};

static void Usage() {
    wprintf(L"Использование: evtexport [--log Application] [--max 100] [--format csv|json|both]\n"
        L"                [--out имя_без_расширения] [--level 1..5] [--xpath \"запрос\"]\n"
        L"                [--no-message] [--stats файл.csv] [--config имя] [--run N]\n");
}

static bool ParseArgs(int argc, wchar_t** argv, Options& o) {
    for (int i = 1; i < argc; i++) {
        std::wstring a = argv[i];
        auto next = [&](std::wstring& dst) { if (i + 1 >= argc) return false; dst = argv[++i]; return true; };
        std::wstring val;
        if (a == L"--log") { if (!next(o.log)) return false; }
        else if (a == L"--out") { if (!next(o.out)) return false; }
        else if (a == L"--format") { if (!next(o.format)) return false; }
        else if (a == L"--xpath") { if (!next(o.xpath)) return false; }
        else if (a == L"--stats") { if (!next(o.stats)) return false; }
        else if (a == L"--config") { if (!next(o.config)) return false; }
        else if (a == L"--max") { if (!next(val)) return false; wchar_t* e; o.max = (int)wcstol(val.c_str(), &e, 10); if (*e || o.max <= 0) return false; }
        else if (a == L"--level") { if (!next(val)) return false; wchar_t* e; o.level = (int)wcstol(val.c_str(), &e, 10); if (*e || o.level < 0 || o.level > 5) return false; }
        else if (a == L"--run") { if (!next(val)) return false; o.run = _wtoi(val.c_str()); }
        else if (a == L"--no-message") o.message = false;
        else { Log(L"ERROR неизвестный параметр: %ls", a.c_str()); return false; }
    }
    if (o.format != L"csv" && o.format != L"json" && o.format != L"both") return false;
    return true;
}

// ---------------------------------------------------------------- main
int wmain(int argc, wchar_t** argv) {
    SetConsoleOutputCP(CP_UTF8);
    LARGE_INTEGER f; QueryPerformanceFrequency(&f); g_freq = (double)f.QuadPart;
    fopen_s(&g_log, "lab06.log", "ab");

    Options opt;
    if (!ParseArgs(argc, argv, opt)) { Log(L"ERROR некорректные параметры запуска"); Usage(); return 2; }
    if (opt.xpath.empty())
        opt.xpath = opt.level > 0 ? L"*[System[(Level>=1 and Level<=" + std::to_wstring(opt.level) + L")]]" : L"*";

    Log(L"START log=%ls max=%d format=%ls level=%d xpath=%ls", opt.log.c_str(), opt.max,
        opt.format.c_str(), opt.level, opt.xpath.c_str());

    int rc = 0;
    std::vector<EventRec> recs;
    std::map<std::wstring, EVT_HANDLE> metaCache;
    double tRead0 = NowMs();

    {
        EvtH query(EvtQuery(nullptr, opt.log.c_str(), opt.xpath.c_str(),
            EvtQueryChannelPath | EvtQueryReverseDirection));   // новые события первыми
        if (!query.h) {
            LogApiError(L"EvtQuery", L"open query", GetLastError());
            if (g_log) fclose(g_log);
            return 3;
        }
        Log(L"EVT_QUERY status=OK");

        static const wchar_t* paths[] = {
            L"Event/System/Provider/@Name", L"Event/System/EventID", L"Event/System/Level",
            L"Event/System/TimeCreated/@SystemTime", L"Event/System/EventRecordID",
            L"Event/System/Channel", L"Event/System/Computer" };
        EvtH ctx(EvtCreateRenderContext(_countof(paths), paths, EvtRenderContextValues));
        if (!ctx.h) {
            LogApiError(L"EvtCreateRenderContext", L"create render context", GetLastError());
            rc = 4;
        }
        else {
            while (rc == 0 && (int)recs.size() < opt.max) {
                EVT_HANDLE batch[16] = {};
                DWORD got = 0;
                DWORD want = (DWORD)std::min<size_t>(16, (size_t)opt.max - recs.size());
                if (!EvtNext(query.h, want, batch, 2000, 0, &got)) {
                    DWORD e = GetLastError();
                    if (e == ERROR_NO_MORE_ITEMS) { Log(L"EVT_NEXT end of log"); break; }
                    LogApiError(L"EvtNext", L"read events", e);
                    rc = 4; break;
                }
                for (DWORD i = 0; i < got; i++) {
                    EventRec r;
                    if (RenderEvent(ctx.h, batch[i], r)) {
                        if (opt.message) r.message = FormatMessageText(metaCache, r.provider, batch[i]);
                        recs.push_back(std::move(r));
                    }
                    EvtClose(batch[i]);
                }
            }
        }
    }
    for (auto& kv : metaCache) if (kv.second) EvtClose(kv.second);
    double readMs = NowMs() - tRead0;
    Log(L"READ count=%zu time_ms=%.3f", recs.size(), readMs);

    size_t csvBytes = 0, jsonBytes = 0;
    double expMs = 0;
    if (rc == 0) {
        double t0 = NowMs();
        if (opt.format != L"json") {
            std::string d = BuildCsv(recs); csvBytes = d.size();
            if (!WriteFileBytes(opt.out + L".csv", d)) rc = 5;
        }
        if (opt.format != L"csv") {
            std::string d = BuildJson(recs); jsonBytes = d.size();
            if (!WriteFileBytes(opt.out + L".json", d)) rc = 5;
        }
        expMs = NowMs() - t0;
        Log(L"EXPORT_DONE time_ms=%.3f", expMs);
    }

    if (rc == 0 && !opt.stats.empty()) {
        FILE* s = nullptr;
        if (_wfopen_s(&s, opt.stats.c_str(), L"ab") == 0 && s) {
            fseek(s, 0, SEEK_END);
            if (ftell(s) == 0) fputs("config,run,count,read_ms,export_ms,csv_bytes,json_bytes\n", s);
            fprintf(s, "%s,%d,%zu,%.3f,%.3f,%zu,%zu\n", ToUtf8(opt.config).c_str(), opt.run,
                recs.size(), readMs, expMs, csvBytes, jsonBytes);
            fclose(s);
        }
        else Log(L"ERROR STATS_OPEN file=%ls", opt.stats.c_str());
    }

    Log(L"END status=%ls rc=%d", rc == 0 ? L"OK" : L"FAIL", rc);
    if (g_log) fclose(g_log);
    return rc;
}