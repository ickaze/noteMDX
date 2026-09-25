#include "notemdx/compiler.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>

namespace notemdx {
namespace {
using Bytes = std::vector<std::uint8_t>;
const std::string channels = "ABCDEFGHPQRSTUVW";
struct Location { std::string file; int line = 1; int column = 1; };
struct Unit { char c; Location loc; };
using Text = std::vector<Unit>;
struct Failure {};
struct Wave { bool loop=false; int loop_point=0; std::vector<int> data; };
struct Directive { std::string name; std::string value; Location loc; };
bool space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
bool digit(char c) { return c >= '0' && c <= '9'; }
std::string trim(std::string s) {
    while (!s.empty() && space(s.back())) s.pop_back();
    std::size_t p = 0;
    while (p < s.size() && space(s[p])) ++p;
    return s.substr(p);
}
std::string lower(std::string s) {
    for (char& c : s) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    return s;
}
std::string plain(const Text& t) { std::string s; for (const auto& u : t) s += u.c; return s; }
Text slice(const Text& t, std::size_t a, std::size_t b) { return Text(t.begin() + a, t.begin() + b); }


struct Compiler;
struct Reader {
    Compiler& owner;
    const Text& text;
    std::size_t pos = 0;
    char channel = 0;
    Location location() const;
    [[noreturn]] void fail(const std::string& message) const;
    void ws() { while (pos < text.size() && space(text[pos].c)) ++pos; }
    char peek() { ws(); return pos < text.size() ? text[pos].c : '\0'; }
    bool take(char c) { if (peek() == c) { ++pos; return true; } return false; }
    bool match(const std::string& s) {
        ws(); if (pos + s.size() > text.size()) return false;
        for (std::size_t i = 0; i < s.size(); ++i) if (text[pos + i].c != s[i]) return false;
        pos += s.size(); return true;
    }
    void require(char c) { if (!take(c)) fail(std::string("Expected '") + c + "'."); }
    int number(int lo, int hi, bool radix = false, bool bounded_radix = false);
    int accidental(int initial) {
        int value=initial; bool first=true;
        while(true) {
            if(take('+')) {if(first) value=0;++value;}
            else if(take('-')) {if(first) value=0;--value;}
            else if(take('=') || take('"')) value=0;
            else break;
            first=false;
        }
        return value;
    }
};

struct Compiler {
    Options opt;
    Result result;
    std::array<Text, 16> tracks;
    std::vector<Directive> directives;
    std::map<std::string, Text> macros;
    std::map<int, Bytes> voices;
    std::set<int> used_voices;
    Bytes tone_bank = Bytes(7168,0), wave_bank = Bytes(131968,0);
    std::vector<Bytes> loaded_waves;
    std::string save_tone, save_wave, save_tone_parent, save_wave_parent;
    std::string title;
    std::string pdx;
    bool ex = false;
    bool listed = true;
    bool overwrite = false;
    bool wave_memory = false;
    int tone_offset = 0;
    std::size_t source_bytes = 0;
    std::size_t expanded_bytes = 0;
    std::vector<std::string> include_stack;

    explicit Compiler(const Options& options) : opt(options), ex(options.ex_pcm) {}
    [[noreturn]] void fail(const Location& l, const std::string& s, char channel = 0) {
        result.diagnostics.push_back({Severity::error, l.file, l.line, l.column, channel, s});
        throw Failure();
    }
    void warn(const Location& l, const std::string& s) {
        result.diagnostics.push_back({Severity::warning, l.file, l.line, l.column, 0, s});
    }
    void budget(std::size_t n, const Location& l) {
        if (n > opt.max_expanded_bytes || expanded_bytes > opt.max_expanded_bytes - n)
            fail(l, "Expanded source exceeds the configured limit.");
        expanded_bytes += n;
    }
    Text make_text(const std::string& s, const Location& l) {
        Text t;
        for (std::size_t i = 0; i < s.size(); ++i) t.push_back({s[i], {l.file,l.line,l.column + static_cast<int>(i)}});
        return t;
    }
    void marker(const Directive& d) {
        directives.push_back(d);
        auto t = make_text("\1" + std::to_string(directives.size() - 1) + ";", d.loc);
        for (auto& track : tracks) { budget(t.size(), d.loc); track.insert(track.end(), t.begin(), t.end()); }
    }
    std::string string_arg(std::string s) {
        s = trim(s); std::string out; char quote = 0;
        for (char c : s) {
            if (quote) { if (c == quote) quote = 0; else out += c; }
            else if (c == '\'' || c == '"') quote = c;
            else if (space(c) || c == ';' || c == '*') break;
            else out += c;
        }
        return out;
    }
    std::string macro_arg(const std::string& value) {
        const auto s=trim(value);
        if(!s.empty() && (s[0]=='"' || s[0]=='\'')) {
            const auto end=s.find(s[0],1);
            return s.substr(1,end==std::string::npos?end:end-1);
        }
        return string_arg(s);
    }
    Text expand(const Text& text, int depth = 0, const std::map<std::string,Text>* environment = nullptr) {
        const auto& env = environment ? *environment : macros;
        if (depth > 8) fail(text.empty() ? Location() : text.front().loc, "Macro nesting exceeds 8 levels.");
        Text out;
        for (std::size_t i = 0; i < text.size();) {
            char c = text[i].c;
            // Recognize multi-letter commands before single-letter macro names.
            bool protected_command = false;
            for (const auto* name : {"$NATURAL", "$NORMAL", "$SHARP", "$FLAT", "$FO",
                    "SMON", "SMOF", "MPON", "MPOF", "MAON", "MAOF", "MHON", "MHOF", "MHR",
                    "GLON", "GLOF", "APON", "APOF", "APD", "APL", "DTON", "DTOF", "DTD", "DTS", "DTL",
                    "TDON", "TDOF", "TDD", "TDS", "TDL", "VMON", "VMOF", "VMD", "VMS", "VML",
                    "MVON", "MVOF", "MVD", "MVS", "MVL", "KMON", "KMOF", "KMD", "KML",
                    "TLON", "TLOF", "TLD", "TLM", "TLT", "TLL", "YAON", "YAOF", "YAD", "YAL",
                    "KSON", "KSOF", "TR", "VO", "MD", "MP", "MA", "MH", "GL", "AP", "DT", "TD",
                    "VM", "MV", "KM", "TL", "YA", "KS", "zc", "zw"}) {
                std::size_t len = std::string(name).size();
                if (i + len <= text.size() && plain(slice(text,i,i+len)) == name) {
                    out.insert(out.end(),text.begin()+i,text.begin()+i+len); i += len;
                    protected_command = true; break;
                }
            }
            if (protected_command) continue;
            bool explicit_macro = c == '$';
            bool implicit_macro = std::string("IJNOXZhijmsu").find(c) != std::string::npos ||
                (static_cast<unsigned char>(c) >= 0xA6 && static_cast<unsigned char>(c) <= 0xDD && static_cast<unsigned char>(c) != 0xB0);
            if(c>='A' && c<='Z' && !implicit_macro && std::string("DCFKLQSVW").find(c)==std::string::npos) break;
            // Preserve the channel operand of the sync command.
            if(c=='S' && i+1<text.size()) {
                std::size_t next=i+1;while(next<text.size() && space(text[next].c)) ++next;
                if(next<text.size() && channels.find(text[next].c)!=std::string::npos) {
                    out.insert(out.end(),text.begin()+i,text.begin()+next+1);i=next+1;continue;
                }
            }
            // Parse y's two numeric operands here; do not interpret hex digits as macros.
            if (c == 'y') {
                Reader numeric{*this,text,i+1};
                numeric.number(0,255,true); numeric.require(','); numeric.number(0,255,true,true);
                out.insert(out.end(),text.begin()+i,text.begin()+numeric.pos); i=numeric.pos;
                continue;
            }
            if (c == ';' || c == '*' || (c == '/' && i+1 < text.size() && (text[i+1].c == '/' || text[i+1].c == '*'))) break;
            if (!explicit_macro && !implicit_macro) { out.push_back(text[i++]); continue; }
            std::size_t begin = i;
            if (explicit_macro) ++i;
            if (i >= text.size()) fail(text[begin].loc, "Missing macro name.");
            char name = text[i++].c;
            if (std::string("IJKLMNOXYZabcdefghijklmnopqrstuvwxyz").find(name) == std::string::npos &&
                !(static_cast<unsigned char>(name) >= 0xA6 && static_cast<unsigned char>(name) <= 0xDD && static_cast<unsigned char>(name) != 0xB0))
                fail(text[begin].loc, "Invalid macro name.");
            std::string key(1, name);
            while (i < text.size() && digit(text[i].c)) key += text[i++].c;
            if (key.size() > 1 && (key.size() > 3 || std::stoi(key.substr(1)) > 19))
                fail(text[begin].loc, "Macro index must be 0..19.");
            auto it = env.find(key);
            if (it != env.end()) {
                auto value = expand(it->second, depth + 1, environment);
                budget(value.size(), text[begin].loc);
                out.insert(out.end(), value.begin(), value.end());
            }
        }
        return out;
    }
    void read_source(const Source& source, int depth);
    void definition(const Text& statement);
    void load_bank(bool wave, const std::string& name, const Location& loc);
    void distribute(const std::string& selected, const Text& text);
    Result run(const Source& source);
};

Location Reader::location() const {
    return text.empty() ? Location() : text[std::min(pos, text.size() - 1)].loc;
}
void Reader::fail(const std::string& message) const { owner.fail(location(), message, channel); }
int Reader::number(int lo, int hi, bool radix, bool bounded_radix) {
    ws(); int sign = 1;
    if (take('-')) sign = -1; else take('+');
    int base = 10;
    if (radix && take('$')) base = 16; else if (radix && take('%')) base = 2;
    std::int64_t n = 0; bool any = false; int digits = 0;
    while (pos < text.size()) {
        char c = text[pos].c;
        if (base == 2 && c == '_') { ++pos; continue; }
        int d = digit(c) ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
        if (d < 0 || d >= base) break;
        any = true; ++pos; n = n * base + d; ++digits;
        if (n > 2147483648LL) fail("Integer is too large.");
        if (bounded_radix && ((base == 16 && digits == 2) || (base == 2 && digits == 8))) break;
    }
    if (!any) fail("Expected an integer.");
    n *= sign;
    if (n < lo || n > hi) fail("Value must be " + std::to_string(lo) + ".." + std::to_string(hi) + ".");
    return static_cast<int>(n);
}

void Compiler::definition(const Text& statement) {
    Reader r{*this, statement}; r.require('@');
    if (r.take('@')) {
        int n = r.number(0, 255) + tone_offset;
        if (n < 0 || n > 255) r.fail("Tone number plus offset is outside 0..255.");
        r.require('=');
        marker({"tone-macro", std::to_string(n) + " " + macro_arg(plain(slice(statement, r.pos, statement.size()))), r.location()});
        return;
    }
    if (r.peek() == 'w' || r.peek() == 'k') {
        const bool wave=r.take('w'); if (!wave) r.require('k');
        const int id=r.number(0,wave?127:7);
        if (wave && !wave_memory) r.fail("Wave definitions require #wavemem.");
        r.require('='); r.require('{');
        std::vector<int> data;
        while (r.peek() && r.peek()!='}') {
            int value=r.number(wave?-32768:0,wave?(r.peek()=='$'?65535:32767):255,true);
            if (wave && value>32767) value-=65536;
            data.push_back(value);
            if (data.size()>(wave?514U:96U)) r.fail("Too many definition elements.");
            if (!r.take(',')) break;
        }
        r.require('}'); if(r.peek()) r.fail("Unexpected definition suffix.");
        if (wave) {
            if (data.size()<3 || data[0]<0 || data[0]>1 || data[1]<0 ||
                (data[0] && data[1]>=static_cast<int>(data.size())-2)) r.fail("Invalid wave type, data, or loop point.");
        } else if (data.size()!=96) r.fail("Key map requires 96 entries.");
        std::string value=std::to_string(id);
        for(int n:data) value+=","+std::to_string(n);
        if(wave) {
            const auto offset=128+id*1030;
            wave_bank[id]=255;
            std::fill(wave_bank.begin()+offset,wave_bank.begin()+offset+1030,static_cast<std::uint8_t>(0));
            auto word=[&](int at,int v) {wave_bank[at]=static_cast<std::uint8_t>(v>>8);wave_bank[at+1]=static_cast<std::uint8_t>(v);};
            word(offset,static_cast<int>(data.size())-2); word(offset+2,data[0]); word(offset+4,data[1]);
            for(std::size_t i=2;i<data.size();++i) word(offset+6+static_cast<int>(i-2)*2,data[i]);
        }
        marker({wave?"wave":"key-map",value,r.location()});
        return;
    }
    int n = r.number(0,255) + tone_offset;
    if (n < 0 || n > 255) r.fail("Tone number plus offset is outside 0..255.");
    r.require('='); r.require('{');
    std::array<int,47> v{};
    constexpr int limits[] = {31,31,31,15,15,127,3,15,7,3,1};
    for (int i = 0; i < 47; ++i) {
        int hi = i < 44 ? limits[i % 11] : i == 46 ? 15 : 7;
        v[i] = r.number(0,hi);
        if (i < 46) r.require(',');
    }
    r.take(','); r.require('}');
    if (r.peek()) r.fail("Unexpected data after voice definition.");
    if (voices.count(n) && !overwrite) r.fail("Duplicate voice definition; use #overwrite to replace it.");
    Bytes b{static_cast<std::uint8_t>(n), static_cast<std::uint8_t>((v[45] << 3) | v[44]), static_cast<std::uint8_t>(v[46])};
    // NOTE definitions are M1, C1, M2, C2; MDX register groups are M1, M2, C1, C2.
    for (int group = 0; group < 6; ++group) for (int op = 0; op < 4; ++op) {
        constexpr int operator_order[] = {0, 2, 1, 3};
        const int* p = v.data() + operator_order[op] * 11;
        int x = group == 0 ? (p[8] << 4) | p[7] : group == 1 ? p[5] :
                group == 2 ? (p[6] << 6) | p[0] : group == 3 ? (p[10] << 7) | p[1] :
                group == 4 ? (p[9] << 6) | p[2] : (p[4] << 4) | p[3];
        b.push_back(static_cast<std::uint8_t>(x));
    }
    tone_bank[n]=128; std::copy(b.begin(),b.end(),tone_bank.begin()+256+n*27);
    voices[n] = std::move(b);
    std::string snapshot=std::to_string(n);
    for (int byte:voices[n]) snapshot+=","+std::to_string(byte);
    marker({"voice-data",snapshot,r.location()});
}

void Compiler::load_bank(bool wave,const std::string& name,const Location& loc) {
    if(name.empty()) fail(loc,"Binary file name is required.");
    if(!opt.binary_loader) fail(loc,"#load-* requires a binary_loader callback.");
    Bytes bytes; std::string error;
    if(!opt.binary_loader(name,loc.file,bytes,error)) fail(loc,"Binary load failed: "+error);
    if(bytes.size()!=(wave?131968U:7168U)) fail(loc,"Invalid NOTE binary bank size.");
    if(wave) {
        if(!wave_memory) fail(loc,"#load-wave requires #wavemem.");
        for(int id=0;id<128;++id) if(bytes[id]) {
            const int at=128+id*1030;
            auto word=[&](int n){return bytes[n]*256+bytes[n+1];};
            const int size=word(at),loop=word(at+2),point=word(at+4);
            if(size<1 || size>512 || loop>1 || (loop && point>=size)) fail(loc,"Invalid wave bank record.");
        }
        wave_bank=bytes; loaded_waves.push_back(std::move(bytes));
        marker({"wave-bank",std::to_string(loaded_waves.size()-1),loc});
    } else {
        voices.clear(); marker({"clear-voices","",loc}); tone_bank=bytes;
        for(int id=0;id<256;++id) if(bytes[id]) {
            const int at=256+id*27;
            if(bytes[at]!=id || bytes[at+1]>63 || bytes[at+2]>15) fail(loc,"Invalid tone bank record.");
            voices[id]=Bytes(bytes.begin()+at,bytes.begin()+at+27);
            std::string value=std::to_string(id);
            for(int b:voices[id]) value+=","+std::to_string(b);
            marker({"voice-data",value,loc});
        }
    }
}

void Compiler::read_source(const Source& source, int depth) {
    if (depth > 16) fail({source.name,1,1}, "Include nesting exceeds 16 levels.");
    if (std::find(include_stack.begin(), include_stack.end(), source.name) != include_stack.end())
        fail({source.name,1,1}, "Recursive include detected.");
    if (source.text.size() > opt.max_source_bytes || source_bytes > opt.max_source_bytes - source.text.size())
        fail({source.name,1,1}, "Source size exceeds the configured limit.");
    source_bytes += source.text.size(); include_stack.push_back(source.name);
    Text pending; int braces = 0;
    std::size_t start = 0; int line_number = 0;
    while (start < source.text.size()) {
        ++line_number;
        auto end = source.text.find('\n', start);
        if (end == std::string::npos) end = source.text.size();
        std::string raw = source.text.substr(start, end - start); start = end + 1;
        auto stripped = trim(raw); Location loc{source.name,line_number,1};
        if (stripped.empty()) continue;
        // NOTE dispatches ordinary lines by their first byte. Whitespace and
        // unknown identifiers are comments; definition continuations are exempt.
        if(pending.empty()) {
            const unsigned char first=static_cast<unsigned char>(raw[0]);
            const bool macro=std::string("IJKLMNOXYZabcdefghijklmnopqrstuvwxyz").find(static_cast<char>(first))!=std::string::npos ||
                (first>=0xA6 && first<=0xDD && first!=0xB0);
            if(first!='#' && first!='@' && channels.find(static_cast<char>(first))==std::string::npos && !macro) continue;
        }
        if (stripped[0] == '#' && pending.empty()) {
            auto split = stripped.find_first_of(" \t\r");
            std::string name = lower(stripped.substr(1, split == std::string::npos ? std::string::npos : split - 1));
            std::string value = split == std::string::npos ? "" : stripped.substr(split + 1);
            if (name == "list") { listed = true; continue; }
            if (name == "nlist") { listed = false; continue; }
            if (!listed) continue;
            if (name == "title") title = string_arg(value);
            else if (name == "pcmfile") pdx = string_arg(value);
            else if (name == "include") {
                if (!opt.include_loader) fail(loc,"#include requires an include_loader callback.");
                Source child; std::string error;
                if (!opt.include_loader(string_arg(value), source.name, child, error)) fail(loc,"Include failed: " + error);
                read_source(child, depth + 1);
            }
            else if (name == "load-tone" || name == "load-wave") load_bank(name=="load-wave",string_arg(value),loc);
            else if (name == "save-tone") {save_tone=string_arg(value);if(save_tone.empty()) save_tone="tone.bin";save_tone_parent=source.name;}
            else if (name == "save-wave") {save_wave=string_arg(value);if(save_wave.empty()) save_wave="wave.bin";save_wave_parent=source.name;}
            else if (name == "pcmlist") warn(loc,"#pcmlist is unsupported; pcmuse.map is not generated.");
            else if (name == "ex-pcm") ex = true;
            else if (name == "wavemem") wave_memory = true;
            else if (name == "overwrite") overwrite = true;
            else if (name == "toneofs") {
                auto t = make_text(value,loc); Reader r{*this,t}; tone_offset = r.number(-255,255);
                if (r.peek() && r.peek() != ';') r.fail("Unexpected directive argument.");
                marker({name,value,loc});
            }
            else if (name == "play") warn(loc,"#play is not executed. Playback is the host application's responsibility.");
            else if (name == "remove" || name == "beep" || name == "ver") warn(loc,"#" + name + " is ignored; no host-side action is performed.");
            else if (name == "tps" || name == "tps-all" || name == "detune" || name == "flat" || name == "sharp" ||
                     name == "natural" || name == "normal" || name == "octave-rev" || name == "coder" || name == "ncoder" || name == "noreturn" || name == "glide" || name == "reste" || name == "nreste" || name == "cont" || name == "wcmd" || name == "compress" || name == "opt")
                marker({name, value, loc});
            else fail(loc,"Unsupported directive #" + name + ". See docs/COMPATIBILITY.md.");
            continue;
        }
        if (!listed) continue;
        // In NOTE, /* and * are end-of-line comments, not C-style blocks.
        // Quotes on macro-definition lines protect their contents until expansion.
        bool macro_line = stripped.find('=') != std::string::npos && stripped.find('"') != std::string::npos &&
                          (stripped[0] != '@' || stripped.rfind("@@",0) == 0);
        Text t; char quote = 0;
        for (std::size_t i = 0; i < raw.size(); ++i) {
            unsigned char c = static_cast<unsigned char>(raw[i]);
            if (c == 0 || c == 1 || (c < 32 && c != '\r' && c != '\t')) fail({source.name,line_number,static_cast<int>(i+1)},"Invalid control byte in source.");
            if (macro_line && (c == '"' || c == '\'')) { if (!quote) quote = static_cast<char>(c); else if (quote == c) quote = 0; }
            if (!quote && (c == ';' || c == '*' || (c == '/' && i+1 < raw.size() && (raw[i+1] == '/' || raw[i+1] == '*')))) break;
            t.push_back({static_cast<char>(c),{source.name,line_number,static_cast<int>(i+1)}});
            if ((c >= 0x81 && c <= 0x9F) || (c >= 0xE0 && c <= 0xFC)) {
                if (++i >= raw.size()) fail(loc,"Incomplete CP932 character.");
                t.push_back({raw[i],{source.name,line_number,static_cast<int>(i+1)}});
            }
        }
        if (trim(plain(t)).empty()) continue;
        if (!pending.empty() || trim(plain(t))[0] == '@') {
            bool tm = trim(plain(t)).rfind("@@",0) == 0 && pending.empty();
            if (tm) { definition(t); continue; }
            for (const auto& u : t) { if (u.c == '{') ++braces; if (u.c == '}') --braces; }
            pending.insert(pending.end(), t.begin(), t.end()); pending.push_back({'\n',loc});
            if (braces == 0) { definition(pending); pending.clear(); }
            else if (braces < 0) fail(loc,"Unmatched voice definition brace.");
            continue;
        }
        Reader r{*this,t}; r.ws();
        if (r.pos + 1 < t.size()) {
            auto eq = plain(t).find('=', r.pos);
            // Macro definitions have a single name and optional decimal index.
            if (eq != std::string::npos) {
                auto key = trim(plain(slice(t,r.pos,eq)));
                bool valid = !key.empty() && (std::string("IJKLMNOXYZabcdefghijklmnopqrstuvwxyz").find(key[0]) != std::string::npos ||
                    (static_cast<unsigned char>(key[0]) >= 0xA6 && static_cast<unsigned char>(key[0]) <= 0xDD && static_cast<unsigned char>(key[0]) != 0xB0));
                for (std::size_t k = 1; k < key.size(); ++k) if (!digit(key[k])) valid = false;
                if (valid) {
                    if (key.size() > 1 && (key.size() > 3 || std::stoi(key.substr(1)) > 19)) fail(loc,"Macro index must be 0..19.");
                    auto value = macro_arg(plain(slice(t,eq+1,t.size())));
                    macros[key] = make_text(value,loc);
                    marker({"macro",key + " " + value,loc}); continue;
                }
            }
        }
        std::string selected;
        while (r.pos < t.size() && channels.find(t[r.pos].c) != std::string::npos) selected += t[r.pos++].c;
        if (selected.empty()) r.fail("Expected a channel identifier, definition, or directive.");
        std::sort(selected.begin(),selected.end()); selected.erase(std::unique(selected.begin(),selected.end()),selected.end());
        auto body = expand(slice(t,r.pos,t.size()));
        distribute(selected,body);
    }
    if (!pending.empty()) fail(pending.front().loc,"Unterminated voice definition.");
    include_stack.pop_back();
}

void Compiler::distribute(const std::string& selected, const Text& text) {
    // Chords are left to the per-channel parser; retain the rank as a marker.
    for (std::size_t rank = 0; rank < selected.size(); ++rank) {
        auto ci = channels.find(selected[rank]); Text out;
        for (std::size_t i = 0; i < text.size();) {
            if (text[i].c != '|') { out.push_back(text[i++]); continue; }
            auto loc = text[i++].loc; std::size_t begin = i; std::size_t part = 0;
            bool closed = false;
            while (i < text.size()) {
                if (text[i].c == ':' || text[i].c == '|') {
                    if (part == rank) out.insert(out.end(),text.begin()+begin,text.begin()+i);
                    if (text[i].c == '|') { ++i; closed = true; break; }
                    ++part; begin = i + 1;
                }
                ++i;
            }
            if (!closed) fail(loc,"Unterminated channel-selection block.");
        }
        Directive d{"rank",std::to_string(rank),text.empty() ? Location() : text.front().loc};
        directives.push_back(d);
        auto m = make_text("\1" + std::to_string(directives.size()-1) + ";",d.loc);
        // Rank markers are only needed for chord lines; avoid breaking note ties.
        bool chord = std::any_of(out.begin(),out.end(),[](const Unit& u) { return u.c == '`' || static_cast<unsigned char>(u.c) == 0xA2 || u.c == '#'; });
        if (chord) out.insert(out.begin(),m.begin(),m.end());
        out.push_back({'\n',d.loc}); budget(out.size(),d.loc);
        tracks[ci].insert(tracks[ci].end(),out.begin(),out.end());
    }
}

struct Track : Reader {
    Bytes out;
    int index;
    int octave = 4;
    int default_length = 48;
    int transpose = 0;
    int global_transpose = 0;
    int detune_offset = 0;
    int tone_offset = 0;
    int volume = 8;
    int detune = 0;
    int glide_value = 0;
    int glide_steps = 1;
    bool glide_on = false;
    bool glide_restarted = false;
    bool glide_first_only = false;
    bool previous_tie = false;
    bool no_return = false;
    bool temporary_volume = false;
    bool one_note_active = false;
    int saved_volume_byte = 8;
    int temporary_base = 8;
    int volume_offset = 0;
    int tone = -1;
    bool fine_volume = false;
    bool reverse = false;
    bool transpose_pcm = false;
    bool coder = false;
    bool tone_macros_on = true;
    bool suppress = false;
    int rank = 0;
    int compression=-1;
    bool allow_compression=true;
    std::string optimization;
    std::map<int,Bytes> output_state;
    std::map<int,std::size_t> pending_controls;
    std::map<int,Bytes> lfo_parameters;
    std::set<int> initialized_lfos;
    std::map<int,bool> lfo_parameters_ready;
    std::vector<bool> lfo_repeat_escape;
    std::size_t last_begin=0,last_end=0;
    int last_pitch=-2,last_length=0;
    int last_opcode=-1;
    std::size_t last_command_begin=0;
    bool last_tie=false;
    int loop_depth = 0;
    int tuplet_depth = 0;
    int pseudo_loop = -1;
    int infinite_loop = -1;
    std::size_t time_events = 0;
    std::size_t loop_time_events = 0;
    int tone_depth = 0;
    std::map<char,int> accidentals;
    std::map<int,std::string> tone_macros;
    std::map<std::string,Text> local_macros;
    std::string chord;
    struct Effect {
        bool on=false;
        int wave=0, interval=1, next_interval=1, mode=0, delay=0, scale=1;
        int position=0, remaining=1, waiting=0, note_count=0;
        std::int64_t applied_value=0;
        bool fresh=true, dirty=true, finished=false;
    };
    std::array<Effect,8> effects;
    std::map<int,Wave> waves;
    std::map<int,Bytes> local_voices;
    std::map<int,std::vector<int>> key_maps;
    int key_map=0;
    bool key_map_on=false;
    int pan=3, quantize=8, hardware_quantize=8, wave_quantize=0;
    int rest_effect=0, reset_on_rest=0, continuity=0, wave_commands=0;
    int tl_mask=-1, tl_tone=-1, pms_ams=0;
    bool previous_rest=false;
    struct TimedEvent { std::size_t begin; std::size_t end; int pitch; bool tie; bool muted; bool port=false; int delta=0; };
    std::vector<TimedEvent> tuplet_events;
    // Tuplet remainders follow the rounded cumulative boundaries observed in p09_tupl.
    Track(Compiler& c, const Text& t, int channel_index) : Reader{c,t,0,channels[channel_index]}, index(channel_index), reverse(c.opt.reverse_octave), compression(c.opt.compression.value_or(-1)), optimization(c.opt.optimization.value_or("")) {}
    void emit(std::initializer_list<int> bytes) {
        if (suppress) return;
        if(bytes.size()) {
            const int opcode=*bytes.begin();
            if(opcode>=0xE0 && opcode!=0xF7) last_pitch=-2;
            const char key=opcode==0xF3?'d':opcode==0xFB?'v':opcode==0xF8?'q':opcode==0xFC?'p':opcode==0xFD?'t':opcode==0xE9?'0':opcode==0xEC?'1':opcode==0xEB?'2':0;
            Bytes value;for(int b:bytes) value.push_back(static_cast<std::uint8_t>(b));
            // LFO initializers restart phase and must be retained even if equal.
            if(key && ((opcode!=0xEC && opcode!=0xEB) || bytes.size()==2)) {
                if(optimization.find(key)!=std::string::npos &&
                   !((opcode==0xEC || opcode==0xEB) && value[1]==0x80 && !initialized_lfos.count(opcode))) {
                    // Keep the last assignment before the next sounding note.
                    // Rest bytes do not consume these parameter values.
                    auto pending=pending_controls.find(opcode);
                    if(pending!=pending_controls.end() && pending->second+value.size()<=out.size()) {
                        std::copy(value.begin(),value.end(),out.begin()+pending->second);
                        output_state[opcode]=value;return;
                    }
                    if(output_state.count(opcode) && output_state[opcode]==value) return;
                    pending_controls[opcode]=out.size();
                }
                if((opcode==0xEC || opcode==0xEB) && value[1]==0x80) pending_controls.erase(opcode);
                output_state[opcode]=std::move(value);
            }
            if((opcode==0xEC || opcode==0xEB) && bytes.size()>2) {
                output_state[opcode]=Bytes{static_cast<std::uint8_t>(opcode),0x81};pending_controls.erase(opcode);
            }
            if(opcode<0xE0) for(auto& ready:lfo_parameters_ready) ready.second=true;
            if(opcode>=0x80 && opcode<0xE0) pending_controls.clear();
            if(opcode==0xF6) {
                lfo_repeat_escape.push_back(false);
                for(auto& ready:lfo_parameters_ready) ready.second=false;
            }
            if(opcode==0xF4) {
                if(!lfo_repeat_escape.empty()) lfo_repeat_escape.back()=true;
                lfo_parameters.clear();lfo_parameters_ready.clear();
            }
            if(opcode==0xF5 && !lfo_repeat_escape.empty()) {
                if(lfo_repeat_escape.back()) {lfo_parameters.clear();lfo_parameters_ready.clear();}
                lfo_repeat_escape.pop_back();
            }
            if(opcode==0xF2) {output_state.erase(0xF3);pending_controls.erase(0xF3);}
            if(opcode==0xF9 || opcode==0xFA) {output_state.erase(0xFB);pending_controls.erase(0xFB);}
            if(opcode==0xFE || opcode==0xF6 || opcode==0xF5 || opcode==0xF4) {output_state.clear();pending_controls.clear();}
        }
        if (out.size() + bytes.size() > owner.opt.max_output_bytes) fail("Track exceeds output size limit.");
        last_command_begin=out.size();last_opcode=bytes.size()?*bytes.begin():-1;
        for (int b : bytes) out.push_back(static_cast<std::uint8_t>(b));
    }
    void emit_word(int opcode,int value) { emit({opcode,(value >> 8) & 255,value & 255}); }
    int length(bool long_ok = false);
    int pitch(bool numbered = false);
    void write_note(int p,int n,bool tie,bool final_segment = true);
    void apply(const Directive& d);
    void lfo(int opcode);
    bool wave_command();
    bool effects_active() const;
    void effect_value(int kind, bool restore=false);
    void effect_advance(int kind, bool output, bool force=false);
    void wave_note(int pitch,int duration,bool tie);
    void prepare_note(int pitch);
    void set_quantize(int value);
    int output_volume(bool include_wave=false) const {
        const auto value=static_cast<std::int64_t>(volume)+volume_offset+
            (include_wave && effects[3].on?effects[3].applied_value:0)+(include_wave && effects[4].on?effects[4].applied_value:0);
        const int clipped=static_cast<int>(std::clamp<std::int64_t>(value,0,fine_volume?127:15));
        return fine_volume?255-clipped:clipped;
    }
    void tuplet();
    void musical_note(int pitch, int duration, bool tie, bool port, int delta, int target, int target_length, bool numbered);
    int volume_byte() const { return fine_volume ? 255-volume : volume; }
    void set_temporary_volume(int value);
    void parse(char stop = 0);
    void run();
};

int Track::length(bool long_ok) {
    auto term = [&]() {
        int n = default_length;
        if (take('%')) n = number(1,65535);
        else if (digit(peek())) {
            int d = number(1,192);
            if (192 % d) fail("Length denominator must divide 192 exactly.");
            n = 192 / d;
        }
        int add = n;
        while (take('.')) { add /= 2; n += add; }
        return n;
    };
    std::int64_t n = term();
    while (peek() == '^' || peek() == '~') { char op = peek(); ++pos; int next = term(); n += op == '^' ? next : -next; }
    if (n < 1 || n > (long_ok ? 65535 : 256)) fail("Note length is outside the supported range.");
    return static_cast<int>(n);
}
int Track::pitch(bool numbered) {
    int p;
    if (numbered) {
        p = number(-24,119);
        if (take(',')) {}
        else if (peek() == '.' && pos + 1 < text.size() && (digit(text[pos+1].c) || text[pos+1].c == '%')) ++pos;
    } else {
        char c = peek();
        auto n = std::string("c d ef g a b").find(c);
        if (n == std::string::npos || c == ' ') fail("Expected a note c..b.");
        ++pos; int a = accidental(accidentals[c]);
        p = octave * 12 + static_cast<int>(n) - 3 + a;
    }
    p += transpose + ((index < 8 || transpose_pcm) ? global_transpose : 0);
    if (p < 0 || p > 95) fail("Output note is outside o0d+..o8d (0..95).");
    return p;
}
void Track::set_temporary_volume(int value) {
    temporary_base=value;
    saved_volume_byte=volume_byte();
    temporary_volume=true; one_note_active=true;
    emit({0xFB,value});
}
void Track::write_note(int p,int n,bool tie,bool final_segment) {
    const auto begin=out.size();
    if (!suppress) ++time_events;
    auto event=[&](int pitch,int duration,bool tied) {
        const bool merge=!suppress && allow_compression && !tuplet_depth && !effects_active() &&
            compression>=0 && last_pitch==pitch && last_end==out.size() &&
            (pitch<0 ? !last_tie && !tied : compression==1 && last_tie) &&
            last_length+duration<=(pitch<0?128:256);
        if(merge) { duration+=last_length;out.resize(last_begin); }
        last_begin=out.size();
        if(tied && pitch>=0) emit({0xF7});
        if(pitch>=0) emit({0x80+pitch,duration-1});else emit({duration-1});
        last_end=out.size();last_pitch=pitch;last_length=duration;last_tie=tied;
    };
    if(p>=0) event(p,n,tie);
    else {bool first=true;while(n>0) {int part=std::min(n,128);event(-1,part,tie&&first);n-=part;first=false;}}
    if (tuplet_depth) tuplet_events.push_back({begin,out.size(),p,tie,suppress});
    if (p>=0 && final_segment && temporary_volume) {
        one_note_active=false;
    }
    previous_tie=tie; previous_rest=p<0;
}
void Track::lfo(int opcode) {
    if (match("OF")) { emit({opcode,0x80}); return; }
    if (match("ON")) { emit({opcode,0x81}); return; }
    const int wave=number(0,opcode==0xEC ? 7 : 3);
    require(','); const int quarter=number(1,65535);
    require(','); const int amplitude=number(-32768,32767);
    const int shape=wave&3;
    int period=quarter;
    int change=amplitude;
    if (shape==0) {
        period=quarter*4;
        change=amplitude*(opcode==0xEC ? 128 : 64)/quarter;
    } else if (shape==2) {
        period=quarter*2;
        change=amplitude*(opcode==0xEC ? 256 : 128)/quarter;
    } else if (shape==1 || opcode==0xEB) {
        period=quarter*2;
        change=amplitude*256;
    }
    if (period>65535 || change < -32768 || change > 32767)
        fail("LFO period or converted amplitude exceeds its 16-bit operand.");
    const Bytes parameters{static_cast<std::uint8_t>(wave),static_cast<std::uint8_t>(period>>8),static_cast<std::uint8_t>(period),
                           static_cast<std::uint8_t>(change>>8),static_cast<std::uint8_t>(change)};
    if(!suppress && optimization.find(opcode==0xEC?'1':'2')!=std::string::npos && lfo_parameters_ready[opcode] && lfo_parameters[opcode]==parameters) {
        emit({opcode,0x81});return;
    }
    if(!suppress) {initialized_lfos.insert(opcode);lfo_parameters[opcode]=parameters;lfo_parameters_ready[opcode]=false;}
    emit({opcode,wave,(period>>8)&255,period&255,(change>>8)&255,change&255});
}
void Track::musical_note(int p,int n,bool tie,bool port,int delta,int target,int target_length,bool numbered) {
    allow_compression=!port && !glide_on && !effects_active() && !tuplet_depth;
    prepare_note(p);
    if (effects_active() && !port && !(glide_on && p>=0 && index<8 && !numbered && !tuplet_depth && n>=glide_steps)) {
        wave_note(p,n,tie); return;
    }
    if (port && tuplet_depth) {
        write_note(p,n,tie); tuplet_events.back().port=true; tuplet_events.back().delta=delta; return;
    }
    if (port) {
        const int slope=delta*256/n;
        if (slope>=-32768 && slope<=32767) emit_word(0xF2,slope);
        if (target_length>n) {
            write_note(p,n,true,false);
            write_note(target,target_length-n,tie);
        } else write_note(p,n,tie);
        return;
    }
    if (p>=0 && index<8 && !numbered && !tuplet_depth && glide_on && n>=glide_steps &&
        !(glide_first_only && previous_tie)) {
        const int slope=-glide_value*256/glide_steps;
        if (slope < -32768 || slope > 32767) fail("Glide slope exceeds signed 16-bit range.");
        const bool by_note=(glide_value%64==0 && p+glide_value/64>=0 && p+glide_value/64<=95);
        const int start_note=by_note ? p+glide_value/64 : p;
        if (!by_note || glide_restarted) {
            const int start_detune=detune+glide_value;
            if (start_detune < -32768 || start_detune > 32767) fail("Glide detune exceeds signed 16-bit range.");
            emit_word(0xF3,start_detune);
        }
        emit_word(0xF2,slope);
        write_note(start_note,glide_steps,n>glide_steps || tie,n==glide_steps);
        if (!by_note || glide_restarted) emit_word(0xF3,detune);
        if (n>glide_steps) write_note(p,n-glide_steps,tie);
        return;
    }
    write_note(p,n,tie);
}
void Track::tuplet() {
    if (tuplet_depth) fail("Nested tuplets are not supported.");
    ++tuplet_depth;
    tuplet_events.clear();
    parse('}'); require('}');
    --tuplet_depth;
    const int total=length(true);
    const auto count=tuplet_events.size();
    if (count==0 || count>32) fail("Tuplet must contain 1..32 notes/rests.");
    if (total<static_cast<int>(count)) fail("Tuplet cannot assign fewer than one tick per note.");
    Bytes replaced;
    std::size_t cursor=0;
    for (std::size_t i=0;i<count;++i) {
        const auto& event=tuplet_events[i];
        replaced.insert(replaced.end(),out.begin()+cursor,out.begin()+event.begin);
        // Nearest cumulative boundary; remainders are spread across the group.
        const int a=static_cast<int>((i*total+count/2)/count);
        const int b=static_cast<int>(((i+1)*total+count/2)/count);
        int ticks=b-a;
        if (ticks>256) fail("An individual tuplet note exceeds 256 ticks; split the group.");
        if (!event.muted) {
            if (event.port) {
                const int slope=event.delta*256/ticks;
                if (slope>=-32768 && slope<=32767) {
                    replaced.push_back(0xF2); replaced.push_back(static_cast<std::uint8_t>((slope>>8)&255)); replaced.push_back(static_cast<std::uint8_t>(slope&255));
                }
            }
            if (event.tie && event.pitch>=0) replaced.push_back(0xF7);
            if (event.pitch>=0) {
                replaced.push_back(static_cast<std::uint8_t>(0x80+event.pitch));
                replaced.push_back(static_cast<std::uint8_t>(ticks-1));
            } else while (ticks>0) {
                const int part=std::min(ticks,128); replaced.push_back(static_cast<std::uint8_t>(part-1)); ticks-=part;
            }
        }
        cursor=event.end;
    }
    replaced.insert(replaced.end(),out.begin()+cursor,out.end());
    if (replaced.size()>owner.opt.max_output_bytes) fail("Tuplet exceeds output limit.");
    out=std::move(replaced);
    tuplet_events.clear();
}
bool Track::effects_active() const {
    return std::any_of(effects.begin(),effects.end(),[](const Effect& effect){return effect.on;});
}
void Track::prepare_note(int p) {
    if (p<0) return;
    if (temporary_volume && !one_note_active) { emit({0xFB,output_volume()}); temporary_volume=false; }
    if (index<8 && key_map_on) {
        const auto found=key_maps.find(key_map);
        if(found==key_maps.end()) fail("Undefined key map.");
        const int selected=found->second.at(static_cast<std::size_t>(p));
        if(tone!=selected) {
            if(last_opcode==0xFD && last_command_begin+2==out.size()) out.resize(last_command_begin);
            tone=selected; emit({0xFD,tone});
        }
        if(!suppress) owner.used_voices.insert(tone);
    }
}
void Track::set_quantize(int value) {
    quantize=value;
    if(!effects_active()) {emit({0xF8,value&255}); hardware_quantize=value;}
}
bool Track::wave_command() {
    static const std::array<std::string,8> names={"AP","DT","TD","VM","MV","KM","TL","YA"};
    for(int kind=0;kind<8;++kind) {
        if(!match(names[kind])) continue;
        auto& effect=effects[kind];
        if(match("OF")) {
            effect.on=false;
            if(!effects_active() && quantize!=8) {emit({0xF8,quantize&255});hardware_quantize=quantize;}
            effect.applied_value=0;
            if(!((kind==1 && effects[2].on)||(kind==2 && effects[1].on)||
                 (kind==3 && effects[4].on)||(kind==4 && effects[3].on))) effect_value(kind,true);
        } else if(match("ON")) {
            if(!waves.count(effect.wave)) fail("Undefined wave.");
            effect.on=true;effect.fresh=true;
        } else if(take('D')) effect.delay=number(-255,32767);
        else if(take('L')) effect.next_interval=number(1,32767);
        else if((kind==1||kind==2||kind==3||kind==4) && take('S')) {effect.scale=number(-32768,32767);if(!effect.scale) effect.scale=1;}
        else if(kind==6 && take('M')) {
            tl_mask=number(-1,15);
            if(tl_mask<0) {
                const int selected=tl_tone>=0?tl_tone:tone;
                if(!local_voices.count(selected)) fail("TLM-1 requires a previously defined and selected voice.");
                const int alg=local_voices.at(selected)[1]&7;
                tl_mask=15^(alg<=3?8:alg==4?10:alg<=6?14:15);
            }
        }
        else if(kind==6 && take('T')) {tl_tone=number(0,255);if(!local_voices.count(tl_tone)) fail("TLT requires a defined voice.");}
        else {
            effect.wave=number(0,127);require(',');effect.interval=effect.next_interval=number(1,32767);
            require(',');effect.mode=number(0,2);
            if(!waves.count(effect.wave)) fail("Undefined wave.");
            if(kind==6 && !local_voices.count(tl_tone>=0?tl_tone:tone)) fail("TL requires a selected, defined voice.");
            effect.on=true;effect.fresh=true;
        }
        if(effects_active() && hardware_quantize!=8) {emit({0xF8,8});hardware_quantize=8;}
        return true;
    }
    return false;
}
void Track::effect_value(int kind,bool restore) {
    auto& effect=effects[kind];
    int value=0;
    if(!restore) {
        const auto& wave=waves.at(effect.wave);
        value=wave.data.at(static_cast<std::size_t>(effect.position));
    }
    if(kind==0) emit({0xFC,restore?pan:std::clamp(value,0,3)});
    else if(kind==1||kind==2) {
        effect.applied_value=static_cast<std::int64_t>(value)*effect.scale;
        const auto result=static_cast<std::int64_t>(detune)+(effects[1].on?effects[1].applied_value:0)+(effects[2].on?effects[2].applied_value:0);
        if(result< -32768 || result>32767) fail("Wave detune exceeds signed 16-bit range.");
        emit_word(0xF3,static_cast<int>(result));
    } else if(kind==3||kind==4) {
        effect.applied_value=static_cast<std::int64_t>(value)*effect.scale;
        const bool fine=fine_volume && !one_note_active;
        const auto result=static_cast<std::int64_t>(one_note_active?temporary_base:volume)+(one_note_active?0:volume_offset)+(effects[3].on?effects[3].applied_value:0)+(effects[4].on?effects[4].applied_value:0);
        const int clipped=static_cast<int>(std::clamp<std::int64_t>(result,0,fine?127:15));
        emit({0xFB,fine?255-clipped:clipped});
    } else if(kind==5) {
        if(index>=8) fail("KM requires an FM channel.");
        emit({0xFE,0x38+index,restore?pms_ams:value&255});
    } else if(kind==6) {
        if(index>=8) fail("TL requires an FM channel.");
        const int selected=tl_tone>=0?tl_tone:tone;
        if(!local_voices.count(selected)) fail("TL requires a selected, defined voice.");
        const auto& voice=local_voices.at(selected);
        const int algorithm=voice[1]&7;
        const int carriers=algorithm<=3?8:algorithm==4?10:algorithm<=6?14:15;
        const int mask=tl_mask<0 ? 15^carriers : tl_mask;
        constexpr int order[]={0,2,1,3};
        for(int op=0;op<4;++op) if(mask&(1<<op)) {
            int level=std::clamp(static_cast<int>(voice[7+order[op]])-value,0,127);
            emit({0xFE,0x60+index+8*order[op],level});
        }
    } else if(!restore) {
        emit({0xFE,(value>>8)&255,value&255});
    }
    effect.dirty=false;
}
void Track::effect_advance(int kind,bool output,bool force) {
    auto& effect=effects[kind];
    const auto& wave=waves.at(effect.wave);
    if(effect.waiting>0) return;
    if(!effect.finished) {
        const int before=wave.data.at(static_cast<std::size_t>(effect.position));
        ++effect.position;
        if(effect.position>=static_cast<int>(wave.data.size())) {
            if(wave.loop) effect.position=wave.loop_point;
            else {effect.position=static_cast<int>(wave.data.size())-1;effect.finished=true;}
        }
        if(!wave.loop && effect.position==static_cast<int>(wave.data.size())-1) effect.finished=true;
        effect.interval=effect.next_interval;effect.remaining=effect.interval;
        if(output) {if(force || wave.data.at(static_cast<std::size_t>(effect.position))!=before) effect_value(kind);}
        else effect.dirty=true;
    }
}
void Track::wave_note(int p,int duration,bool tie) {
    if(tuplet_depth) fail("Wave effects are not allowed inside tuplets.");

    const bool old_tie=previous_tie;
    const bool old_rest=previous_rest;
    int gate=duration;
    if(p>=0) {
        if(wave_quantize>0) gate=duration*wave_quantize/256;
        else if(wave_quantize<0) gate=std::min(duration,-wave_quantize);
        else if(quantize<0) gate=std::max(0,duration+quantize);
        else gate=std::clamp((duration*quantize+7)/8,0,duration);
    }
    const int delay_gate=gate;
    if(tie) gate=duration;
    if(p<0) gate=0;
    if(hardware_quantize!=8) {emit({0xF8,8});hardware_quantize=8;}
    const bool eligible=p>=0 || rest_effect==2;
    const bool new_note=continuity!=0 || !old_tie;
    std::array<int,8> order{0,1,2,3,4,5,6,7};
    std::stable_partition(order.begin(),order.end(),[&](int k){return effects[k].mode==2;});
    for(int kind:order) {
        auto& effect=effects[kind]; if(!effect.on) continue;
        const auto& wave=waves.at(effect.wave);

        const bool negative_sync=effect.mode==1 && effect.delay<0;
        const bool reset=negative_sync || effect.fresh || (effect.mode==1 && eligible && new_note && !(continuity==2 && old_rest));
        if(reset) {
            effect.position=0;effect.interval=effect.next_interval;effect.remaining=effect.interval;
            effect.finished=!wave.loop && wave.data.size()==1;effect.note_count=1;
            effect.waiting=effect.mode==2?0:effect.delay>=0?effect.delay:effect.mode==1?std::max(0,delay_gate+effect.delay):0;
            if(negative_sync && continuity==0 && tie) effect.waiting=duration+1;
            effect.fresh=false;
            if(eligible) {if(effect.waiting) effect_value(kind,true);else effect_value(kind);}
            else effect.dirty=true;
        } else if(effect.mode==2 && eligible && new_note) {
            if(++effect.note_count>effect.interval) {effect.note_count=1;effect_advance(kind,true);}
        } else if(eligible && effect.dirty && effect.waiting==0) effect_value(kind);
    }
    int elapsed=0;
    while(elapsed<duration) {
        const bool sounding=p>=0 && elapsed<gate;
        const bool output=sounding || (p>=0?rest_effect>=1:rest_effect==2);
        int span=duration-elapsed;
        if(sounding) span=std::min(span,gate-elapsed);
        if(output) for(const auto& effect:effects) if(effect.on && effect.mode!=2) {
            if(effect.waiting>0) span=std::min(span,effect.waiting);
            else if(!effect.finished) {
                // Equal adjacent raw samples form one segment. Clipped values
                // still keep their original boundaries (e.g. VM with @x).
                const auto& wave=waves.at(effect.wave);
                int until=effect.remaining,phase=effect.position;
                while(until<span) {
                    int next=phase+1;
                    if(next>=static_cast<int>(wave.data.size())) {
                        if(!wave.loop) {until=span;break;} next=wave.loop_point;
                    }
                    if(wave.data[next]!=wave.data[effect.position]) break;
                    phase=next;until+=effect.next_interval;
                }
                span=std::min(span,until);
            }
        }
        if(span<=0) fail("Wave scheduler failed to advance.");
        const bool chunk_tie=sounding && (elapsed+span<gate || tie);
        write_note(sounding?p:-1,span,chunk_tie,false);
        // Phase continues silently in asynchronous mode 0. Synchronous mode
        // freezes outside its enabled note/rest interval.
        for(int kind=0;kind<8;++kind) {
            auto& effect=effects[kind];
            if(!effect.on || effect.mode==2 || (!output && effect.mode!=0)) continue;
            int left=span;
            while(left>0) {
                if(effect.waiting>0) {
                    int step=std::min(left,effect.waiting);left-=step;effect.waiting-=step;
                    if(effect.waiting==0) {if(output) effect_value(kind);else effect.dirty=true;}
                } else if(effect.finished) left=0;
                else {
                    int step=std::min(left,effect.remaining);left-=step;effect.remaining-=step;
                    if(effect.remaining==0) effect_advance(kind,output,left==0 && elapsed+span==duration);
                }
            }
        }
        elapsed+=span;
        if(p>=0 && elapsed==gate && gate<duration && reset_on_rest)
            for(int kind=0;kind<8;++kind) if(effects[kind].on) effect_value(kind,true);
    }
    if(p>=0 && temporary_volume) one_note_active=false;
    previous_tie=tie;previous_rest=p<0;
}

void Track::apply(const Directive& d) {
    auto t = owner.make_text(d.value,d.loc); Reader r{owner,t}; r.channel = channel;
    auto arg = [&](int lo,int hi) { int n = r.number(lo,hi); if (r.peek() && r.peek() != ';') r.fail("Unexpected directive argument."); return n; };
    if (d.name == "tps") global_transpose = arg(-24,24);
    else if (d.name == "tps-all") transpose_pcm = true;
    else if (d.name == "detune") detune_offset = arg(-32768,32767);
    else if (d.name == "toneofs") tone_offset = arg(-255,255);
    else if (d.name == "octave-rev") reverse = !reverse;
    else if (d.name == "coder") coder = true;
    else if (d.name == "ncoder") coder = false;
    else if (d.name == "noreturn") no_return = true;
    else if (d.name == "glide") glide_first_only = trim(d.value).empty() ? false : arg(0,1) != 0;
    else if (d.name == "reste") { rest_effect=trim(d.value).empty()?1:arg(0,1)+1; reset_on_rest=0; }
    else if (d.name == "nreste") { rest_effect=0; reset_on_rest=trim(d.value).empty()?0:arg(0,1); }
    else if (d.name == "cont") continuity=trim(d.value).empty()?0:arg(0,2);
    else if (d.name == "compress") {const int n=trim(d.value).empty()?0:arg(0,1);compression=owner.opt.compression.value_or(n);last_pitch=-2;}
    else if (d.name == "opt") {
        optimization=owner.string_arg(d.value);if(optimization=="*") optimization="dvqpt012";
        if(optimization.find_first_not_of("dvqpt012")!=std::string::npos) r.fail("Invalid #opt selector.");
        if(owner.opt.optimization) optimization=*owner.opt.optimization;
        pending_controls.clear();
        last_pitch=-2;
    }
    else if (d.name == "wcmd") wave_commands=trim(d.value).empty()?0:arg(0,2);
    else if (d.name == "clear-voices") local_voices.clear();
    else if (d.name == "wave-bank") {
        const auto& bytes=owner.loaded_waves.at(static_cast<std::size_t>(arg(0,static_cast<int>(owner.loaded_waves.size())-1)));
        waves.clear();
        for(int id=0;id<128;++id) if(bytes[id]) {
            const int at=128+id*1030; auto word=[&](int n){return bytes[n]*256+bytes[n+1];};
            Wave w;w.loop=word(at+2)!=0;w.loop_point=word(at+4);
            for(int i=0;i<word(at);++i) {int value=word(at+6+i*2);w.data.push_back(value>=32768?value-65536:value);}
            waves[id]=std::move(w);
        }
        for(const auto& e:effects) if(e.on && (!waves.count(e.wave) || e.position>=static_cast<int>(waves.at(e.wave).data.size())))
            fail("Loaded bank invalidates an active wave; stop it before loading.");
    }
    else if (d.name == "voice-data") {
        const int id=r.number(0,255); Bytes bytes;
        while(r.take(',')) bytes.push_back(static_cast<std::uint8_t>(r.number(0,255)));
        local_voices[id]=std::move(bytes);
    }
    else if (d.name == "wave" || d.name == "key-map") {
        const int id=r.number(0,d.name=="wave"?127:7); std::vector<int> values;
        while(r.take(',')) values.push_back(r.number(-32768,32767));
        if (d.name=="wave") { Wave w; w.loop=values[0]!=0; w.loop_point=values[1]; w.data.assign(values.begin()+2,values.end());
            for(const auto& effect:effects) if(effect.on && effect.wave==id && effect.position>=static_cast<int>(w.data.size()))
                fail("Active wave redefinition invalidates its phase; stop the effect before shortening the wave.");
            waves[id]=std::move(w);
            for(auto& effect:effects) if(effect.wave==id) effect.applied_value=0;
        }
        else key_maps[id]=std::move(values);
    }
    else if (d.name == "rank") rank = arg(0,15);
    else if (d.name == "macro") {
        auto split = d.value.find(' ');
        local_macros[d.value.substr(0,split)] = owner.make_text(d.value.substr(split+1),d.loc);
    }
    else if (d.name == "tone-macro") {
        int n = r.number(0,255); r.ws(); tone_macros[n] = plain(slice(t,r.pos,t.size()));
    }
    else {
        auto keys = owner.string_arg(d.value);
        if (keys.empty() && (d.name == "natural" || d.name == "normal")) keys = "cdefgab";
        for (char c : keys) {
            if (space(c)) continue;
            if (std::string("cdefgab").find(c) == std::string::npos) r.fail("Key signature requires cdefgab.");
            accidentals[c] = d.name == "flat" ? -1 : d.name == "sharp" ? 1 : 0;
        }
    }
}

void Track::parse(char stop) {
    while (peek()) {
        if (stop && peek() == stop) return;
        if (match("\1")) { int id = number(0,static_cast<int>(owner.directives.size()-1)); require(';'); apply(owner.directives[id]); continue; }
        if (take('!')) { pos = text.size(); return; }
        if (take('?')) { if (tuplet_depth) fail("? inside a tuplet is not supported."); suppress = !suppress; continue; }
        if (wave_command()) continue;
        if (match("zc")) { continuity=number(0,2); continue; }
        if (match("zw")) { wave_commands=number(0,2); continue; }
        if (match("KS")) {
            if (match("ON")) key_map_on=true;
            else if (match("OF")) key_map_on=false;
            else { key_map=number(0,7); key_map_on=true; }
            if (key_map_on && !key_maps.count(key_map)) fail("Undefined key map.");
            continue;
        }
        if (match("MP")) { lfo(0xEC); continue; }
        if (match("MA")) { lfo(0xEB); continue; }
        if (match("MH")) {
            if (match("OF")) emit({0xEA,0x80});
            else if (match("ON")) emit({0xEA,0x81});
            else if (take('R')) { emit({0xFE,1,2}); emit({0xFE,1,0}); }
            else {
                int wave=number(0,3); require(','); int frequency=number(0,255); require(',');
                int pmd=number(0,127); require(','); int amd=number(0,127); require(',');
                int pms=number(0,7); require(','); int ams=number(0,3); require(','); int sync=number(0,1);
                pms_ams=(pms<<4)|ams; emit({0xEA,(sync<<6)|wave,frequency,128|pmd,amd,pms_ams});
            }
            continue;
        }
        if (match("GL")) {
            if (match("ON")) {glide_on=true;glide_restarted=true;}
            else if (match("OF")) glide_on=false;
            else { glide_value=number(-6144,6144); require(','); glide_steps=number(1,255); glide_on=true;glide_restarted=false; }
            continue;
        }
        if (match("SMON")) { tone_macros_on = true; continue; }
        if (match("SMOF")) { tone_macros_on = false; continue; }
        if (match("TR")) { transpose = number(-48,48); continue; }
        if (match("VO")) { volume_offset = number(-127,127); continue; }
        if (match("MD")) { emit({0xE9,number(0,255)}); continue; }
        bool key_command = false;
        for (const auto* name : {"$FLAT", "$SHARP", "$NORMAL", "$NATURAL"}) {
            if (match(name)) {
                std::string keys;
                if (take('{')) {
                    while (peek() && peek() != '}') { keys += peek(); ++pos; }
                    require('}');
                } else if (std::string(name) == "$FLAT" || std::string(name) == "$SHARP") fail("Key signature requires braces.");
                else keys = "cdefgab";
                for (char c : keys) {
                    if (std::string("cdefgab").find(c) == std::string::npos) fail("Key signature requires cdefgab.");
                    accidentals[c] = std::string(name) == "$FLAT" ? -1 : std::string(name) == "$SHARP" ? 1 : 0;
                }
                key_command = true; break;
            }
        }
        if (key_command) continue;
        if (match("$FO")) { emit({0xE7,1,number(0,255)}); continue; }
        if (take('[')) {
            if (tuplet_depth) fail("Repeats are not allowed inside tuplets.");
            if (++loop_depth > 32) fail("Repeat nesting exceeds 32.");
            const std::size_t begin = out.size();
            const int saved_infinite = infinite_loop;
            const auto begin_events = time_events;
            const bool muted_repeat = suppress;
            emit({0xF6,0,0});
            parse('/');
            const auto first_pass_events = time_events;
            std::size_t escape = std::numeric_limits<std::size_t>::max();
            if (take('/')) { escape=out.size(); emit({0xF4,0,0}); parse(']'); }
            require(']');
            const int count = digit(peek()) ? number(1,255) : 2;
            if (count==1 && escape!=std::numeric_limits<std::size_t>::max()) time_events=first_pass_events;
            if (time_events<begin_events) fail("Invalid repeat timing state.");
            if (infinite_loop != saved_infinite) fail("L inside a finite repeat is not supported.");
            if (muted_repeat != suppress) fail("A repeat must not cross a ? boundary.");
            const std::size_t end = out.size();
            if (!suppress) {
                const int back = static_cast<int>(begin)-static_cast<int>(end);
                if (back < -32768) fail("Repeat body exceeds signed 16-bit branch range.");
                out[begin+1]=static_cast<std::uint8_t>(count);
                if (escape != std::numeric_limits<std::size_t>::max()) {
                    const auto forward=end-escape-2;
                    if (forward>32767) fail("Repeat escape exceeds signed 16-bit range.");
                    out[escape+1]=static_cast<std::uint8_t>(forward>>8);
                    out[escape+2]=static_cast<std::uint8_t>(forward);
                }
                emit_word(0xF5,back);
            }
            --loop_depth; continue;
        }
        if (peek() == ']' && stop == '/') return;
        if (take('{')) { tuplet(); continue; }
        if (take('L')) {
            if (tuplet_depth) fail("L is not allowed inside a tuplet.");
            if (pseudo_loop >= 0 || infinite_loop >= 0 || suppress) fail("Invalid or duplicate L loop point.");
            output_state.clear();pending_controls.clear();last_pitch=-2;
            for(auto& ready:lfo_parameters_ready) ready.second=false;
            infinite_loop = static_cast<int>(out.size()); loop_time_events = time_events; continue;
        }
        if (take('C')) { if (tuplet_depth) fail("C is not allowed inside a tuplet."); if (infinite_loop >= 0 || pseudo_loop >= 0) fail("C and L cannot be combined or repeated."); last_pitch=-2; pseudo_loop = static_cast<int>(out.size()); continue; }
        if (take('o')) { octave = number(-2,10); continue; }
        if (peek() == '<' || peek() == '>') { int d = peek() == '>' ? 1 : -1; ++pos; octave += reverse ? -d : d; if (octave < -2 || octave > 10) fail("Octave outside -2..10."); continue; }
        if (take('l')) { default_length = length(); continue; }
        if (take('t')) {
            int bpm = number(19,4882);
            // OPM 4 MHz, timer B /1024, quarter = 48 ticks.
            int b = 256 - 4882812 / (bpm * 1000);
            emit({0xFF,std::clamp(b,0,255)}); continue;
        }
        if (take('q')) { set_quantize(number(1,8)); continue; }
        if (take('Q')) { wave_quantize=number(-256,256); continue; }
        if (take('k')) { emit({0xF0,number(0,255)}); continue; }
        if (take('D')) { int d = number(-32768,32767) + detune_offset; if (d < -32768 || d > 32767) fail("Detune plus offset overflows signed 16-bit."); if (!(wave_commands==2 && (effects[1].on||effects[2].on))) detune=d;
            if (!(wave_commands && (effects[1].on||effects[2].on))) emit_word(0xF3,d);
            continue; }
        if (take('p')) { int v=number(0,3); if (!(wave_commands==2 && effects[0].on)) pan=v; if (!(wave_commands && effects[0].on)) emit({0xFC,v}); continue; }
        if (take('v')) { int v=number(0,15); if (!(wave_commands==2 && (effects[3].on||effects[4].on))) {volume=v; fine_volume=false;} if (!temporary_volume && !(wave_commands && (effects[3].on||effects[4].on))) emit({0xFB,output_volume(true)}); continue; }
        if (take('V')) { int change=number(-32768,32767); if (!(wave_commands==2 && (effects[3].on||effects[4].on))) volume = std::clamp(volume + change,0,fine_volume ? 127 : 15); if (!(wave_commands && (effects[3].on||effects[4].on))) emit({0xFB,output_volume()}); continue; }
        if (peek() == '(' || peek() == ')') {
            char c = peek(); ++pos; int n = digit(peek()) ? number(0,255) : 1;
            if (!(wave_commands && (effects[3].on||effects[4].on))) for (int i=0; i<n; ++i) emit({c == ')' ? 0xF9 : 0xFA});
            continue;
        }
        if (take('@')) {
            if (take('t')) emit({0xFF,number(0,255)});
            else if (take('q')) { int q = number(0,192); set_quantize(q ? -q : 8); }
            else if (take('v')) { int v=number(0,127); if (!(wave_commands==2 && (effects[3].on||effects[4].on))) {volume=v; fine_volume=true;} if (!temporary_volume && !(wave_commands && (effects[3].on||effects[4].on))) emit({0xFB,output_volume(true)}); }
            else if (take('x')) {
                // NOTE p02_ctrl writes the raw @x operand (unlike @v).
                // Retained for NOTE compatibility; documented as a reference quirk.
                int value=number(0,127); set_temporary_volume(volume_offset ? std::clamp(value+volume_offset,0,15) : value);
            }
            else {
                int n = number(0,255) + (index < 8 ? tone_offset : 0);
                if (n < 0 || n > 255) fail("Tone number plus offset is outside 0..255.");
                tone = n; emit({0xFD,n}); if (index < 8 && !suppress) owner.used_voices.insert(n);
                if (index < 8 && tone_macros_on && tone_macros.count(n)) {
                    if (tone_depth >= 8) fail("Tone macro recursion exceeds 8 levels.");
                    auto expanded = owner.expand(owner.make_text(tone_macros[n],location()),0,&local_macros);
                    // Reuse channel state by temporarily executing a copied parser.
                    Track sub(owner,expanded,index);
                    sub.initialized_lfos=initialized_lfos;sub.lfo_parameters=lfo_parameters;sub.lfo_parameters_ready=lfo_parameters_ready;
                    sub.compression=compression;sub.optimization=optimization;sub.output_state=output_state;
                    sub.octave=octave; sub.default_length=default_length; sub.transpose=transpose;
                    sub.global_transpose=global_transpose; sub.detune_offset=detune_offset; sub.tone_offset=tone_offset;
                    sub.detune=detune; sub.glide_value=glide_value; sub.glide_steps=glide_steps; sub.glide_on=glide_on;sub.glide_restarted=glide_restarted;
                    sub.glide_first_only=glide_first_only; sub.previous_tie=previous_tie; sub.no_return=no_return;
                    sub.effects=effects; sub.waves=waves; sub.local_voices=local_voices; sub.key_maps=key_maps; sub.key_map=key_map; sub.key_map_on=key_map_on;
                    sub.pan=pan; sub.quantize=quantize; sub.hardware_quantize=hardware_quantize; sub.wave_quantize=wave_quantize;
                    sub.rest_effect=rest_effect; sub.reset_on_rest=reset_on_rest; sub.continuity=continuity; sub.wave_commands=wave_commands;
                    sub.tl_mask=tl_mask; sub.tl_tone=tl_tone; sub.pms_ams=pms_ams; sub.previous_rest=previous_rest;
                    sub.one_note_active=one_note_active; sub.temporary_volume=temporary_volume; sub.saved_volume_byte=saved_volume_byte; sub.temporary_base=temporary_base;
                    sub.tuplet_depth=tuplet_depth; sub.loop_depth=loop_depth; sub.rank=rank; sub.chord=chord;
                    sub.volume=volume; sub.volume_offset=volume_offset; sub.tone=tone; sub.fine_volume=fine_volume;
                    sub.reverse=reverse; sub.transpose_pcm=transpose_pcm; sub.coder=coder; sub.tone_macros_on=tone_macros_on;
                    sub.suppress=suppress; sub.local_macros=local_macros; sub.accidentals=accidentals; sub.tone_macros=tone_macros; sub.tone_depth=tone_depth+1;
                    sub.parse();
                    if (sub.infinite_loop >= 0 || sub.pseudo_loop >= 0 || sub.suppress != suppress) fail("Loop points / unmatched ? in tone macro are not supported.");
                    if (out.size()+sub.out.size() > owner.opt.max_output_bytes) fail("Tone macro exceeds output limit.");
                    for (auto event:sub.tuplet_events) {
                        event.begin+=out.size(); event.end+=out.size(); tuplet_events.push_back(event);
                    }
                    out.insert(out.end(),sub.out.begin(),sub.out.end());
                    detune=sub.detune; glide_value=sub.glide_value; glide_steps=sub.glide_steps; glide_on=sub.glide_on;glide_restarted=sub.glide_restarted;
                    glide_first_only=sub.glide_first_only; previous_tie=sub.previous_tie; no_return=sub.no_return;
                    effects=sub.effects; waves=sub.waves; local_voices=sub.local_voices; key_maps=sub.key_maps; key_map=sub.key_map; key_map_on=sub.key_map_on;
                    pan=sub.pan; quantize=sub.quantize; hardware_quantize=sub.hardware_quantize; wave_quantize=sub.wave_quantize;
                    rest_effect=sub.rest_effect; reset_on_rest=sub.reset_on_rest; continuity=sub.continuity; wave_commands=sub.wave_commands;
                    tl_mask=sub.tl_mask; tl_tone=sub.tl_tone; pms_ams=sub.pms_ams; previous_rest=sub.previous_rest;
                    initialized_lfos=sub.initialized_lfos;lfo_parameters=sub.lfo_parameters;lfo_parameters_ready=sub.lfo_parameters_ready;
                    output_state=sub.output_state;pending_controls.clear();last_pitch=-2;
                    one_note_active=sub.one_note_active; temporary_volume=sub.temporary_volume; saved_volume_byte=sub.saved_volume_byte; temporary_base=sub.temporary_base; chord=sub.chord;
                    time_events += sub.time_events;
                    octave=sub.octave; default_length=sub.default_length; transpose=sub.transpose;
                    volume=sub.volume; volume_offset=sub.volume_offset; fine_volume=sub.fine_volume;
                    tone=sub.tone; reverse=sub.reverse; accidentals=sub.accidentals; tone_macros_on=sub.tone_macros_on;
                }
            }
            continue;
        }
        if (take('x')) { set_temporary_volume(std::clamp(number(0,15)+volume_offset,0,15)); continue; }
        if (take('K')) {
            if (index >= 8) fail("K requires an FM channel.");
            for (int op=0;op<4;++op) emit({0xFE,0xE0+index+op*8,0xFF});
            if (!no_return && tone>=0) { emit({0xFD,tone}); if (!suppress) owner.used_voices.insert(tone); }
            continue;
        }
        if (take('y')) { int r = number(0,255,true); require(','); int v = number(0,255,true,true); emit({0xFE,r,v}); continue; }
        if (take('w')) { if (index != 7) fail("Noise w is supported on channel H only."); int f = digit(peek()) ? number(0,31)+128 : 0; emit({0xED,f}); continue; }
        if (take('F')) { if (index < 8) fail("F is a PCM-channel command."); emit({0xED,number(0,31)}); continue; }
        if (take('S')) { auto ch = channels.find(peek()); int target; if (ch != std::string::npos && peek()) { target=static_cast<int>(ch); ++pos; } else target=number(0,15); if (target > 8 && !owner.ex) fail("Sync destination requires #ex-pcm."); emit({0xEF,target}); continue; }
        if (take('W')) { emit({0xEE}); continue; }
        if (peek() == '`' || static_cast<unsigned char>(peek()) == 0xA2 || peek() == '#') {
            if (take('#')) {}
            else {
                char end = peek() == '`' ? '`' : static_cast<char>(0xA3); ++pos; std::size_t begin=pos;
                while (pos < text.size() && text[pos].c != end) ++pos;
                if (pos == text.size()) fail("Unterminated chord.");
                chord=plain(slice(text,begin,pos)); ++pos;
            }
            int n=length();
            auto ct=owner.make_text(chord,location()); Reader cr{owner,ct};
            int member=0, relative=0, selected_pitch=-2;
            while (cr.peek()) {
                if (cr.take('>')) { relative += reverse ? -1 : 1; continue; }
                if (cr.take('<')) { relative += reverse ? 1 : -1; continue; }
                char c=cr.peek(); ++cr.pos;
                if (c!='r' && std::string("cdefgab").find(c)==std::string::npos) cr.fail("Chord accepts notes, rests, < and > only.");
                int a=cr.accidental(accidentals[c]);
                if(member++==rank) {
                    selected_pitch=c=='r' ? -1 : (octave+relative)*12+static_cast<int>(std::string("c d ef g a b").find(c))-3+a+transpose+((index<8||transpose_pcm)?global_transpose:0);
                    if(c!='r' && (selected_pitch<0 || selected_pitch>95)) cr.fail("Chord note is outside MDX range.");
                }
            }
            if(selected_pitch==-2 && coder) selected_pitch=-1;
            if(selected_pitch < -2 || selected_pitch > 95) fail("Chord pitch outside MDX range.");
            bool tie=take('&'); if(selected_pitch!=-2) musical_note(selected_pitch,n,tie,false,0,selected_pitch,n,false); continue;
        }
        if (peek() == 'r' || peek() == 'n' || std::string("cdefgab").find(peek()) != std::string::npos) {
            char c = peek(); int p;
            if (c == 'r') { ++pos; p=-1; }
            else if (c == 'n') { ++pos; p=pitch(true); }
            else p=pitch();
            int n=length();
            bool port=false; int delta=0; int target=p; int target_length=n;
            if (take('_')) {
                if (p<0 || index>=8) fail("Portamento requires an FM note.");
                port=true;
                if (take('D')) delta=number(-6144,6144);
                else {
                    while (peek()=='o' || peek()=='<' || peek()=='>') {
                        if (take('o')) octave=number(-2,10);
                        else { int step=peek()=='>' ? 1 : -1; ++pos; octave+=reverse ? -step : step; }
                    }
                    target=pitch(); delta=(target-p)*64;
                    if (digit(peek()) || peek()=='%' || peek()=='.' || peek()=='^' || peek()=='~') {
                        if (tuplet_depth) fail("Target length is not allowed inside tuplets.");
                        target_length=std::max(n,length());
                    }
                }
            }
            bool tie=take('&');
            musical_note(p,n,tie,port,delta,target,target_length,c=='n'); continue;
        }
        if(peek()>='A' && peek()<='Z') {while(pos<text.size() && text[pos].c!='\n') ++pos;continue;}
        fail("Unsupported or misplaced MML near '" + plain(slice(text,pos,std::min(text.size(),pos+12))) + "'. See docs/COMPATIBILITY.md.");
    }
    if (stop) fail("Unterminated repeat.");
}
void Track::run() {
    if (index == 0 && owner.ex) emit({0xE8});
    parse();
    if (suppress) fail("Unclosed ? suppression range.");
    if (infinite_loop >= 0) {
        if (time_events == loop_time_events) fail("Infinite loop has no timed note or rest.");
        // Offset is relative to the byte immediately following the operand.
        int displacement=infinite_loop-static_cast<int>(out.size())-3;
        if(displacement < -32768) fail("Infinite-loop displacement exceeds signed 16-bit.");
        emit_word(0xF1,displacement);
    } else emit({0xF1,0});
}
// Summaries compose without expanding repeats. Bank -1 denotes the incoming
// bank; this preserves notes before a bank change across repeated iterations.
struct SequenceStatistics {
    StepCount ticks;
    std::optional<StepCount> tail;
    int bank = -1;
    std::map<int,std::set<int>> notes;
};
StepCount add_steps(StepCount a, StepCount b) {
    const auto max=std::numeric_limits<std::uint64_t>::max();
    if(a.overflow || b.overflow || max-a.value<b.value) return {max,true};
    return {a.value+b.value,false};
}
SequenceStatistics concatenate(SequenceStatistics a,const SequenceStatistics& b) {
    a.ticks=add_steps(a.ticks,b.ticks);
    if(a.tail) a.tail=add_steps(*a.tail,b.ticks); else a.tail=b.tail;
    for(const auto& entry:b.notes) {
        auto& target=a.notes[entry.first<0?a.bank:entry.first];
        target.insert(entry.second.begin(),entry.second.end());
    }
    if(b.bank>=0) a.bank=b.bank;
    return a;
}
SequenceStatistics repeat_statistics(SequenceStatistics a,unsigned count) {
    SequenceStatistics result;
    while(count) {
        if(count&1) result=concatenate(std::move(result),a);
        count>>=1;
        if(count) a=concatenate(a,a);
    }
    return result;
}
SequenceStatistics scan_statistics(const Bytes& bytes,std::size_t& p,int marker,bool pcm) {
    SequenceStatistics result;
    while(p<bytes.size()) {
        if(static_cast<int>(p)==marker && !result.tail) result.tail=StepCount{};
        const auto op=bytes[p];
        if(op==0xF1 || op==0xF4 || op==0xF5) break;
        SequenceStatistics item;
        if(op<0x80) {item.ticks.value=op+1; ++p;}
        else if(op<0xE0) {
            item.ticks.value=bytes.at(p+1)+1;
            if(pcm) item.notes[-1].insert(op-0x80);
            p+=2;
        } else if(op==0xF6) {
            const unsigned count=bytes.at(p+1); p+=3;
            auto prefix=scan_statistics(bytes,p,marker,pcm);
            if(bytes.at(p)==0xF4) {
                p+=3;
                auto suffix=scan_statistics(bytes,p,marker,pcm);
                item=concatenate(repeat_statistics(concatenate(prefix,suffix),count-1),prefix);
            } else item=repeat_statistics(prefix,count);
            p+=3; // F5 and its displacement
        } else {
            if(op==0xFD && pcm) item.bank=bytes.at(p+1);
            if(op==0xEC || op==0xEB || op==0xEA) p+=bytes.at(p+1)>=0x80?2:6;
            else if(op==0xF3 || op==0xF2 || op==0xFE || op==0xE7) p+=3;
            else if(op==0xFF || op==0xFD || op==0xFC || op==0xFB || op==0xF8 || op==0xF0 || op==0xEF || op==0xED || op==0xE9) p+=2;
            else ++p;
        }
        result=concatenate(std::move(result),item);
    }
    return result;
}
void collect_statistics(Statistics& result,const Track& track,std::map<int,std::set<int>>& pcm) {
    std::size_t p=0;
    const int marker=track.infinite_loop>=0?track.infinite_loop:track.pseudo_loop;
    auto summary=scan_statistics(track.out,p,marker,track.index>=8);
    result.tracks[track.index]={summary.ticks,summary.tail};
    // A bank selected at the end of L is inherited on the next pass. Since
    // bank changes are assignments, one extra pass covers the stable state.
    if(track.index>=8 && track.infinite_loop>=0) {
        p=static_cast<std::size_t>(track.infinite_loop);
        auto next=scan_statistics(track.out,p,-1,true);
        summary=concatenate(std::move(summary),next);
    }
    for(const auto& entry:summary.notes) {
        auto& target=pcm[entry.first<0?0:entry.first];
        target.insert(entry.second.begin(),entry.second.end());
    }
}

Result Compiler::run(const Source& source) {
    try {
        if (opt.max_output_bytes < 64 || opt.max_output_bytes > 64 * 1024 * 1024) fail({source.name,1,1},"Output limit must be 64..67108864 bytes.");
        if(opt.compression && (*opt.compression < -1 || *opt.compression>1)) fail({source.name,1,1},"Invalid compression option.");
        if(opt.optimization) {
            if(*opt.optimization=="*") *opt.optimization="dvqpt012";
            if(opt.optimization->find_first_not_of("dvqpt012")!=std::string::npos) fail({source.name,1,1},"Invalid optimization option.");
        }
        read_source(source,0);
        for (std::size_t i=9; i<16; ++i) {
            // Global directive markers do not make a channel active.
            Text actual; const auto& t=tracks[i];
            for(std::size_t p=0;p<t.size();++p) {
                if(t[p].c=='\1') { while(p<t.size() && t[p].c!=';') ++p; }
                else actual.push_back(t[p]);
            }
            if(!ex && !trim(plain(actual)).empty()) fail(actual.front().loc,"Channels Q..W require #ex-pcm.",channels[i]);
        }
        const int count=ex?16:9; Bytes body((count+1)*2,0);
        std::map<int,std::set<int>> pcm_statistics;
        for(int i=0;i<count;++i) {
            if(body.size()>65535) fail({source.name,1,1},"MDX offset exceeds 16 bits.");
            body[2+i*2]=static_cast<std::uint8_t>(body.size()>>8); body[3+i*2]=static_cast<std::uint8_t>(body.size());
            if(opt.progress) opt.progress(channels[i]);
            Track track(*this,tracks[i],i);
            bool muted=opt.muted_channels.find(channels[i])!=std::string::npos || opt.muted_channels.find(static_cast<char>(channels[i]+'a'-'A'))!=std::string::npos;
            if(muted) { if(i==0 && ex) track.emit({0xE8}); track.emit({0xF1,0}); } else track.run();
            collect_statistics(result.statistics,track,pcm_statistics);
            body.insert(body.end(),track.out.begin(),track.out.end());
            if(body.size()>opt.max_output_bytes) fail({source.name,1,1},"MDX body exceeds output limit.");
        }
        for(int n:used_voices) if(!voices.count(n)) fail({source.name,1,1},"FM voice @"+std::to_string(n)+" is selected but not defined.");
        // The voice table starts at a 16-bit body-relative offset, but its
        // contents may extend beyond 64 KiB (NOTE K01TAIL reference).
        if(body.size()>65535) fail({source.name,1,1},"Voice data offset exceeds 16 bits.");
        body[0]=static_cast<std::uint8_t>(body.size()>>8); body[1]=static_cast<std::uint8_t>(body.size());
        for (int n:used_voices) { const auto& voice=voices.at(n); body.insert(body.end(),voice.begin(),voice.end()); }
        for(const auto* s:{&title,&pdx}) if(s->find('\0')!=std::string::npos || s->find('\x1a')!=std::string::npos) fail({source.name,1,1},"Invalid header control character.");
        Bytes mdx(title.begin(),title.end()); mdx.insert(mdx.end(),{13,10,26}); mdx.insert(mdx.end(),pdx.begin(),pdx.end()); mdx.push_back(0);
        mdx.insert(mdx.end(),body.begin(),body.end());
        if(mdx.size()>opt.max_output_bytes) fail({source.name,1,1},"MDX file exceeds output limit.");
        result.mdx=std::move(mdx);
        result.statistics.title=title;
        result.statistics.fm_used.assign(used_voices.begin(),used_voices.end());
        for(const auto& voice:voices) if(!used_voices.count(voice.first)) result.statistics.fm_unused.push_back(voice.first);
        for(const auto& entry:pcm_statistics) {
            PcmBankStatistics bank; bank.bank=entry.first;
            bank.used.assign(entry.second.begin(),entry.second.end());
            for(int n=0;n<96;++n) if(!entry.second.count(n)) bank.unused.push_back(n);
            result.statistics.pcm_banks.push_back(std::move(bank));
        }

    } catch(const Failure&) { result.mdx.clear(); result.auxiliary_files.clear(); result.statistics={}; }
    if(opt.save_tone_filename) {
        save_tone=opt.save_tone_filename->empty()?"tone.bin":*opt.save_tone_filename;
        save_tone_parent=opt.save_parent.empty()?source.name:opt.save_parent;
    }
    if(opt.save_wave_filename) {
        save_wave=opt.save_wave_filename->empty()?"wave.bin":*opt.save_wave_filename;
        save_wave_parent=opt.save_parent.empty()?source.name:opt.save_parent;
    }
    if(result.ok() || opt.save_banks_on_error) {
        auto& destination=result.ok()?result.auxiliary_files:result.recovery_files;
        if(!save_tone.empty()) destination.push_back({save_tone,save_tone_parent,tone_bank});
        if(!save_wave.empty()) destination.push_back({save_wave,save_wave_parent,wave_bank});
    }
    return std::move(result);
}
} // namespace
bool Result::ok() const {
    return !mdx.empty() && std::none_of(diagnostics.begin(),diagnostics.end(),[](const Diagnostic& d) { return d.severity==Severity::error; });
}
Result compile(const Source& source,const Options& options) { return Compiler(options).run(source); }
} // namespace notemdx
