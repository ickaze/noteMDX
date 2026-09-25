#include "notemdx/compiler.hpp"
#include <filesystem>
#include <iomanip>
#include <algorithm>
#include <map>
#include <optional>
#ifndef _WIN32
#include <unistd.h>
#endif
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <iconv.h>
#include <cerrno>
#endif

namespace fs = std::filesystem;
namespace {
std::string convert(const std::string& input, bool utf8_to_cp932) {
    if (input.empty()) return {};
#ifdef _WIN32
    UINT from = utf8_to_cp932 ? CP_UTF8 : 932;
    UINT to = utf8_to_cp932 ? 932 : CP_UTF8;
    int len = MultiByteToWideChar(from, MB_ERR_INVALID_CHARS, input.data(), static_cast<int>(input.size()), nullptr, 0);
    if (!len) throw std::runtime_error("Invalid input encoding.");
    std::wstring w(static_cast<std::size_t>(len), L'\0');
    MultiByteToWideChar(from, MB_ERR_INVALID_CHARS, input.data(), static_cast<int>(input.size()), w.data(), len);
    BOOL substituted = FALSE;
    DWORD flags = utf8_to_cp932 ? WC_NO_BEST_FIT_CHARS : WC_ERR_INVALID_CHARS;
    int size = WideCharToMultiByte(to,flags,w.data(),len,nullptr,0,nullptr,utf8_to_cp932 ? &substituted : nullptr);
    if (!size || substituted) throw std::runtime_error("Text cannot be represented in CP932.");
    std::string out(static_cast<std::size_t>(size),'\0');
    WideCharToMultiByte(to,flags,w.data(),len,out.data(),size,nullptr,utf8_to_cp932 ? &substituted : nullptr);
    if (substituted) throw std::runtime_error("Text cannot be represented in CP932.");
    return out;
#else
    iconv_t cd = iconv_open(utf8_to_cp932 ? "CP932" : "UTF-8",utf8_to_cp932 ? "UTF-8" : "CP932");
    if(cd == reinterpret_cast<iconv_t>(-1)) throw std::runtime_error("iconv encoding is unavailable.");
    std::string out(input.size()*4+16,'\0');
    char* src=const_cast<char*>(input.data()); std::size_t left=input.size();
    char* dst=out.data(); std::size_t available=out.size();
    std::size_t status=iconv(cd,&src,&left,&dst,&available); iconv_close(cd);
    if(status==static_cast<std::size_t>(-1) || left) throw std::runtime_error("Invalid encoding or character outside CP932.");
    out.resize(out.size()-available); return out;
#endif
}
std::string read_file(const fs::path& path) {
    std::ifstream in(path,std::ios::binary);
    if(!in) throw std::runtime_error("Cannot open input: "+path.u8string());
    std::string s;
    char buffer[8192];
    while(in) { in.read(buffer,sizeof buffer); s.append(buffer,static_cast<std::size_t>(in.gcount())); if(s.size()>8*1024*1024) throw std::runtime_error("Input exceeds 8 MiB."); }
    if(!in.eof()) throw std::runtime_error("Input read failed.");
    return s;
}
std::string decode(const std::string& bytes,const std::string& encoding) {
    if(encoding=="utf8") return convert(bytes.rfind("\xef\xbb\xbf",0)==0 ? bytes.substr(3) : bytes,true);
    if(encoding=="cp932") { convert(bytes,false); return bytes; }
    if(bytes.rfind("\xef\xbb\xbf",0)==0) return convert(bytes.substr(3),true);
    // BOM-less input is CP932 by default; this avoids ambiguous kana conversions.
    convert(bytes,false); return bytes;
}
std::string number_list(const std::vector<int>& values, bool ranges=true) {
    if(values.empty()) return "none";
    std::string result;
    for(std::size_t i=0;i<values.size();++i) {
        if(!result.empty()) result+=", ";
        result+=std::to_string(values[i]);
        std::size_t end=i;
        while(ranges && end+1<values.size() && values[end+1]==values[end]+1) ++end;
        if(end>i) result+="-"+std::to_string(values[end]);
        i=end;
    }
    return result;
}
std::string step_text(const notemdx::StepCount& value) {
    return value.overflow?"overflow":std::to_string(value.value);
}
void print_statistics(const notemdx::Statistics& stats) {
    auto row=[](const std::string& label,const std::string& value) {
        std::cout << std::left << std::setw(11) << label << " : " << value << '\n';
    };
    row("Title",stats.title.empty()?"(untitled)":convert(stats.title,false));
    row("FM voices",number_list(stats.fm_used,false));
    for(const auto& bank:stats.pcm_banks)
        row("PCM bank"+std::to_string(bank.bank),number_list(bank.used));
    std::size_t width=6;
    for(const auto& track:stats.tracks) {
        width=std::max(width,step_text(track.total_steps).size());
        if(track.loop_steps) width=std::max(width,step_text(*track.loop_steps).size());
    }
    auto tracks=[&](int first,const std::string& name) {
        std::cout << std::left << std::setw(11) << name+" track" << " :";
        for(int i=first;i<first+8;++i) std::cout << ' ' << std::right << std::setw(static_cast<int>(width)) << "ABCDEFGHPQRSTUVW"[i];
        std::cout << '\n';
        for(bool loop:{false,true}) {
            std::cout << std::left << std::setw(11) << (loop?"Loop steps":"Total steps") << " :";
            for(int i=first;i<first+8;++i) {
                const auto& t=stats.tracks[i];
                const auto value=loop?(t.loop_steps?step_text(*t.loop_steps):"-"):step_text(t.total_steps);
                std::cout << ' ' << std::right << std::setw(static_cast<int>(width)) << value;
            }
            std::cout << '\n';
        }
    };
    tracks(0,"OPM"); tracks(8,"PCM");
    std::cout << std::left;
}

bool color_diagnostics() {
#ifdef _WIN32
    const HANDLE handle=GetStdHandle(STD_ERROR_HANDLE);DWORD mode=0;
    return GetConsoleMode(handle,&mode) && SetConsoleMode(handle,mode|ENABLE_VIRTUAL_TERMINAL_PROCESSING);
#else
    return isatty(STDERR_FILENO)!=0;
#endif
}
void diagnostic_source(const notemdx::Diagnostic& d,const std::map<std::string,std::string>& sources,int mode) {
    auto found=sources.find(d.file);if(found==sources.end()) return;
    std::size_t begin=0;
    for(int line=1;line<d.line;++line) {
        begin=found->second.find('\n',begin);
        if(begin==std::string::npos) return;
        ++begin;
    }
    const auto end=found->second.find('\n',begin);
    auto line=found->second.substr(begin,end==std::string::npos?end:end-begin);
    if(!line.empty() && line.back()=='\r') line.pop_back();
    const auto at=std::min<std::size_t>(d.column>0?d.column-1:0,line.size());
    const auto before=convert(line.substr(0,at),false),after=convert(line.substr(at),false);
    std::cerr << "Source      : ";
    if(mode==0 && color_diagnostics()) std::cerr << before << "\x1b[31m" << after << "\x1b[0m\n";
    else std::cerr << before << " -> " << after << '\n';
}

void usage() {
    std::cout << "NoteMDX 0.8\n"
              << "Usage: notemdx [--encoding cp932|utf8|auto] [--ex-pcm] [--reverse-octave]\n"
              << "               [-iABC | --mute ABC] [--output output.mdx] input.mml\n"
              << "               [--compress none|rests|notes] [--opt dvqpt012|*|none]\n"
              << "NOTE switches: -mN -x -p -r -iABC -b -c[n] -z[dvqpt012] -t[name] -w[name] -v[0|1] -1 -e.\n"
              << "-l/-o: PCM maps are unsupported. -mN: file budget in KiB (default 64).\n"
              << "auto: UTF-8 BOM, otherwise CP932. -r removes old MDX on conversion error; -e saves banks.\n";
}
int run(const std::vector<std::string>& args) {
    fs::path input,output; std::string encoding="auto"; notemdx::Options options;
    bool remove_on_error=false, beep_on_error=false, pcm_map=false, pcm_merge=false;
    std::optional<int> verbose;
    std::optional<std::string> tone_output,wave_output;
    for(std::size_t i=1;i<args.size();++i) {
        auto value=[&]() { if(++i>=args.size()) throw std::runtime_error("Missing option value."); return args[i]; };
        const auto a=args[i];
        if(a=="--help" || a=="-h") { usage(); return 0; }
        if(a=="--version") { std::cout << "NoteMDX 0.8 (revision 3)\n"; return 0; }
        if(a=="--compress") {
            const auto v=value();
            if(v=="none") options.compression=-1;
            else if(v=="rests") options.compression=0;
            else if(v=="notes") options.compression=1;
            else throw std::runtime_error("--compress expects none, rests, or notes.");
        }
        else if(a=="--opt") {auto v=value();options.optimization=v=="none"?"":v;}
        else if(a=="--output") output=fs::u8path(value());
        else if(a=="--encoding") encoding=value();
        else if(a=="--ex-pcm") options.ex_pcm=true;
        else if(a=="--reverse-octave") options.reverse_octave=true;
        else if(a=="--mute") options.muted_channels=value();
        // Preserve the earlier output alias only when distinguishable from
        // NOTE's no-argument -o. --output is the unambiguous form.
        else if(a=="-o" && i+1<args.size() && !args[i+1].empty() && args[i+1][0]!='-' &&
                (!input.empty() || (i+2<args.size() && !args[i+2].empty() && args[i+2][0]!='-') || fs::u8path(args[i+1]).extension()==".mdx" || fs::u8path(args[i+1]).extension()==".MDX")) output=fs::u8path(value());
        else if(a.size()>1 && a[0]=='-' && a[1]!='-') {
            for(std::size_t j=1;j<a.size();++j) {
                const char flag=a[j];
                if(flag=='x') options.reverse_octave=true;
                else if(flag=='p') options.ex_pcm=true;
                else if(flag=='r') remove_on_error=true;
                else if(flag=='b') beep_on_error=true;
                else if(flag=='l') pcm_map=true;
                else if(flag=='o') pcm_merge=true;
                else if(flag=='1') {} // Parser already stops at its first fatal error.
                else if(flag=='e') options.save_banks_on_error=true;
                else if(flag=='c') {
                    options.compression=0;
                    if(j+1<a.size() && a[j+1]=='n') {options.compression=1;++j;}
                }
                else if(flag=='v') {
                    verbose=0;
                    if(j+1<a.size() && (a[j+1]=='0' || a[j+1]=='1')) verbose=a[++j]-'0';
                    else if(j+1<a.size() && a[j+1]>='2' && a[j+1]<='9') throw std::runtime_error("-v expects 0 or 1.");
                }
                else if(flag=='m') {
                    std::size_t n=0,begin=j+1;
                    while(j+1<a.size() && a[j+1]>='0' && a[j+1]<='9') {
                        n=n*10+static_cast<unsigned>(a[++j]-'0');
                        if(n>65536) throw std::runtime_error("-m accepts 1..65536 KiB.");
                    }
                    if(j+1==begin || n==0) throw std::runtime_error("-m requires a positive KiB count.");
                    options.max_output_bytes=n*1024;
                }
                else if(flag=='i') {
                    options.muted_channels=a.substr(j+1);
                    if(options.muted_channels.empty()) options.muted_channels=value();
                    break;
                }
                else if(flag=='z') {options.optimization=j+1==a.size()?"*":a.substr(j+1);break;}
                else if(flag=='t' || flag=='w') {
                    auto name=a.substr(j+1);
                    if(name.empty()) name=flag=='t'?"tone.bin":"wave.bin";
                    (flag=='t'?tone_output:wave_output)=name;break;
                }
                else throw std::runtime_error(std::string("Unknown option: -")+flag);
            }
        }
        else if(!a.empty() && a[0]=='-') throw std::runtime_error("Unknown option: "+a);
        else if(input.empty()) input=fs::u8path(a);
        else throw std::runtime_error("Only one input file is accepted per invocation.");
    }
    if(pcm_map || pcm_merge) std::cerr << "warning: -l/-o PCM map support is not implemented; pcmuse.map is not read, written, or merged.\n";
    if(pcm_merge && !pcm_map) std::cerr << "warning: NOTE -o requires -l.\n";
    options.save_parent=(fs::current_path()/"command-line").u8string();
    if(tone_output) options.save_tone_filename=convert(*tone_output,true);
    if(wave_output) options.save_wave_filename=convert(*wave_output,true);
    if(verbose) options.progress=[](char channel) {std::cout << "Converting  : " << channel << '\n';};
    if(input.empty()) { usage(); return 2; }
    if(encoding!="auto" && encoding!="cp932" && encoding!="utf8") throw std::runtime_error("Unknown encoding.");
    for(char c:options.muted_channels) if(std::string("ABCDEFGHPQRSTUVWabcdefghpqrstuvw").find(c)==std::string::npos) throw std::runtime_error("Invalid --mute channel.");
    if(!fs::exists(input) && !input.has_extension()) {
        auto mml=input; mml+=".mml"; auto mus=input; mus+=".mus";
        if(fs::exists(mml)) input=mml; else input=mus;
    }
    input=fs::absolute(input).lexically_normal();
    if(output.empty()) { output=input; output.replace_extension(".mdx"); }
    output=fs::absolute(output).lexically_normal();
    if(input==output || (fs::exists(output) && fs::equivalent(input,output))) throw std::runtime_error("Input and output must differ.");
    std::vector<fs::path> inputs{input};
    std::map<std::string,std::string> sources;
    options.include_loader=[&](const std::string& requested,const std::string& parent,notemdx::Source& child,std::string& error) {
        try {
            fs::path path=fs::u8path(parent).parent_path()/fs::u8path(convert(requested,false));
            path=fs::weakly_canonical(path);
            inputs.push_back(path);
            if(path==output || (fs::exists(output) && fs::equivalent(path,output))) throw std::runtime_error("Output must not overwrite an included source.");
            child={path.u8string(),decode(read_file(path),encoding)}; sources[child.name]=child.text; return true;
        } catch(const std::exception& e) { error=e.what(); return false; }
    };
    options.binary_loader=[&](const std::string& requested,const std::string& parent,std::vector<std::uint8_t>& bytes,std::string& error) {
        try {
            const auto path=fs::weakly_canonical(fs::u8path(parent).parent_path()/fs::u8path(convert(requested,false)));
            inputs.push_back(path); const auto raw=read_file(path); bytes.assign(raw.begin(),raw.end());return true;
        } catch(const std::exception& e) {error=e.what();return false;}
    };
    sources[input.u8string()]=decode(read_file(input),encoding);
    auto result=notemdx::compile({input.u8string(),sources[input.u8string()]},options);
    for(const auto& d:result.diagnostics) {
        std::cerr << d.file << '(' << d.line << ',' << d.column << "): "
                  << (d.severity==notemdx::Severity::error ? "error" : "warning");
        if(d.channel) std::cerr << " [" << d.channel << ']';
        std::cerr << ": " << d.message << '\n';
        if(verbose && d.severity==notemdx::Severity::error) diagnostic_source(d,sources,*verbose);
    }
    const bool failed=!result.ok();
    if(failed && beep_on_error) {
#ifdef _WIN32
        Beep(750,120);
#else
        std::cerr << '\a';
#endif
    }
    if(failed && remove_on_error && fs::exists(output)) {
        for(const auto& path:inputs) if(fs::weakly_canonical(path)==fs::weakly_canonical(output) ||
            (fs::exists(path) && fs::equivalent(path,output))) throw std::runtime_error("-r must not remove an input file.");
        if(!fs::is_regular_file(output)) throw std::runtime_error("-r output is not a regular file.");
        fs::remove(output);
        std::cout << "Removed     : " << output.u8string() << '\n';
    }
    if(failed && result.recovery_files.empty()) return 1;
    struct Pending {fs::path path, temporary; const std::vector<std::uint8_t>* bytes;};
    std::vector<Pending> outputs;
    if(!failed) outputs.push_back({output,{},&result.mdx});
    for(const auto& file:failed?result.recovery_files:result.auxiliary_files) {
        auto path=fs::absolute(fs::u8path(file.parent).parent_path()/fs::u8path(convert(file.name,false))).lexically_normal();
        outputs.push_back({path,{},&file.bytes});
    }
    auto same=[](const fs::path& a,const fs::path& b) {
        return fs::weakly_canonical(a)==fs::weakly_canonical(b) ||
            (fs::exists(a) && fs::exists(b) && fs::equivalent(a,b));
    };
    for(std::size_t i=0;i<outputs.size();++i) {
        auto& item=outputs[i];item.temporary=item.path;item.temporary+=".notemdx.tmp";
        for(const auto& path:inputs) if(same(item.path,path)) throw std::runtime_error("Output must not overwrite an input: "+path.u8string());
        for(std::size_t j=0;j<i;++j) if(same(item.path,outputs[j].path)) throw std::runtime_error("Output paths collide.");
        if(fs::exists(item.path) && !fs::is_regular_file(item.path)) throw std::runtime_error("Output is not a regular file.");
        if(fs::exists(item.temporary)) throw std::runtime_error("Temporary file exists: "+item.temporary.u8string());
    }
    for(const auto& item:outputs) {
        for(const auto& path:inputs) if(same(item.temporary,path)) throw std::runtime_error("Temporary path collides with an input.");
        for(const auto& other:outputs) if(same(item.temporary,other.path)) throw std::runtime_error("Temporary path collides with an output.");
    }
    // Complete conversion and stage every file before replacing any output.
    std::vector<fs::path> staged;
    try {
        for(const auto& item:outputs) {
            std::ofstream out(item.temporary,std::ios::binary|std::ios::trunc);
            if(!out) throw std::runtime_error("Cannot stage output: "+item.path.u8string());
            staged.push_back(item.temporary);
            out.write(reinterpret_cast<const char*>(item.bytes->data()),static_cast<std::streamsize>(item.bytes->size()));
            out.close(); if(!out) throw std::runtime_error("Output write failed.");
        }
        for(const auto& item:outputs) {
#ifdef _WIN32
            if(!MoveFileExW(item.temporary.c_str(),item.path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
                throw std::runtime_error("Could not replace output: "+item.path.u8string());
#else
            fs::rename(item.temporary,item.path);
#endif
            std::cout << item.path.u8string() << " (" << item.bytes->size() << " bytes)\n";
        }
    } catch(...) {
        for(const auto& path:staged) {std::error_code ec;fs::remove(path,ec);}
        throw;
    }
    if(!failed) print_statistics(result.statistics);
    return failed?1:0;
}
}
#ifdef _WIN32
int wmain(int argc,wchar_t** argv) {
    SetConsoleOutputCP(CP_UTF8);
    try {
        std::vector<std::string> args;
        for(int i=0;i<argc;++i) args.push_back(fs::path(argv[i]).u8string());
        return run(args);
    } catch(const std::exception& e) { std::cerr << "error: " << e.what() << '\n'; return 2; }
}
#else
int main(int argc,char** argv) {
    try { return run(std::vector<std::string>(argv,argv+argc)); }
    catch(const std::exception& e) { std::cerr << "error: " << e.what() << '\n'; return 2; }
}
#endif
