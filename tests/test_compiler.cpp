#include "notemdx/compiler.hpp"
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>
using Bytes=std::vector<std::uint8_t>;
int assertions=0;
void check(bool condition,const std::string& message) {
    ++assertions;
    if(!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
notemdx::Result good(const std::string& text,notemdx::Options opt={}) {
    auto r=notemdx::compile({"test.mml",text},opt);
    if(!r.ok()) for(const auto& d:r.diagnostics) std::cerr << d.line << ':' << d.column << ' ' << d.message << '\n';
    check(r.ok(),"compile success: "+text); return r;
}
void bad(const std::string& text) {
    auto r=notemdx::compile({"bad.mml",text});
    check(!r.ok() && r.mdx.empty() && !r.diagnostics.empty(),"reject: "+text);
}
std::size_t base(const Bytes& b) {
    std::size_t i=0; while(i+2<b.size() && !(b[i]==13 && b[i+1]==10 && b[i+2]==26)) ++i;
    i+=3; while(i<b.size() && b[i]) ++i; return i+1;
}
int word(const Bytes& b,std::size_t p) { return b.at(p)*256+b.at(p+1); }
Bytes track(const notemdx::Result& r,int c=0,int count=9) {
    auto p=base(r.mdx); auto a=p+word(r.mdx,p+2+c*2);
    auto b=p+(c+1==count ? word(r.mdx,p) : word(r.mdx,p+4+c*2));
    return Bytes(r.mdx.begin()+a,r.mdx.begin()+b);
}
void eq(const Bytes& a,const Bytes& b,const std::string& name) {
    if(a!=b) { std::cerr << name << " actual:"; for(auto n:a) std::cerr << ' ' << std::hex << int(n); std::cerr << std::dec << '\n'; }
    check(a==b,name);
}
const std::string voice="@0={31,0,3,6,0,28,0,1,0,0,0,30,0,3,6,0,24,0,1,0,0,0,28,18,3,9,11,9,0,0,0,0,0,26,0,15,10,0,0,0,1,0,0,0,3,0,15}\n";
int main() {
    eq(track(good("A o0d+%1 o8d%256 r%256\n")),{0x80,0,0xdf,255,127,127,0xf1,0},"pitch and duration boundaries");
    eq(track(good("A o4l4 c4. d4.. e4^8 f2~8\n")),{0xad,71,0xaf,83,0xb1,71,0xb2,71,0xf1,0},"length arithmetic");
    eq(track(good("A n0.4 n0.%48 n0. n0^ n0,.\n")),{0x80,47,0x80,47,0x80,71,0x80,95,0x80,71,0xf1,0},"numbered notes");
    eq(track(good("A c4&\nA c4\n")),{0xf7,0xad,47,0xad,47,0xf1,0},"tie across lines");
    eq(track(good("A v4[c V1]3\n")),{0xfb,4,0xf6,3,0,0xad,47,0xfb,5,0xf5,0xff,0xf9,0xf1,0},"compile-time V in native loop");
    eq(track(good("A [c/d]3\n")),{0xf6,3,0,0xad,47,0xf4,0,3,0xaf,47,0xf5,0xff,0xf6,0xf1,0},"last-pass escape");
    eq(track(good("A [[c]2/d]2\n")),{0xf6,2,0,0xf6,2,0,0xad,47,0xf5,0xff,0xfb,0xf4,0,3,0xaf,47,0xf5,0xff,0xf0,0xf1,0},"nested repeat");
    eq(track(good("A v4[c)]2\n")),{0xfb,4,0xf6,2,0,0xad,47,0xf9,0xf5,0xff,0xfa,0xf1,0},"runtime volume in loop");
    eq(track(good("A Lc4\n")),{0xad,47,0xf1,0xff,0xfb},"backward infinite loop");
    auto r=good("#title \"Test title\"\n#pcmfile \"SAMPLE\"\nA c\n");
    check(std::string(r.mdx.begin(),r.mdx.begin()+10)=="Test title","title");
    check(base(r.mdx)==20,"header base");
    check(word(r.mdx,base(r.mdx)+2)==20,"9-channel table");
    auto v=good(voice+"A @0 c\n");
    auto off=base(v.mdx)+word(v.mdx,base(v.mdx));
    eq(Bytes(v.mdx.begin()+off,v.mdx.end()),{0,3,15,1,0,1,1,28,9,24,0,31,28,30,26,0,18,0,0,3,3,3,15,6,185,6,10},"voice register groups");
    eq(track(good("i=\"c8d8\"\nA i\ni=\"e8\"\nA i\n")),{0xad,23,0xaf,23,0xb1,23,0xf1,0},"source-order macros");
    eq(track(good("i0=\"c8\"\nj=\"i0d8\"\nA j\n")),{0xad,23,0xaf,23,0xf1,0},"nested macros");
    eq(track(good("A $FLAT{b} b b= $NORMAL b\n")),{0xb7,47,0xb8,47,0xb8,47,0xf1,0},"key signatures");
    eq(track(good("#sharp \"f\"\nA f\n#natural\nA f\n")),{0xb3,47,0xb2,47,0xf1,0},"directive order");
    eq(track(good("A ?o5 l8 v15 c? c\n")),{0xb9,23,0xf1,0},"suppressed output keeps parser state");
    eq(track(good("A y$20,$ABc4\n")),{0xfe,32,171,0xad,47,0xf1,0},"hex value stops after two digits");
    eq(track(good(voice+"i=\"v3\"\n@@0=\"i\"\nA @0c\ni=\"v7\"\nA @0d\n")),{0xfd,0,0xfb,3,0xad,47,0xfd,0,0xfb,7,0xaf,47,0xf1,0},"tone macro source-order environment");
    auto parts=good("CBA |c:d:e|\n");
    eq(track(parts,0),{0xad,47,0xf1,0},"channel order A");
    eq(track(parts,2),{0xb1,47,0xf1,0},"channel order C");
    auto chord=good("ABC `ceg`8 #4\n");
    eq(track(chord,1),{0xb1,23,0xb1,47,0xf1,0},"chord reuse");
    auto pcm=good("#ex-pcm\nP @2 F4 n0,4\nW @255 F6 n95,4\n");
    eq(track(pcm,0,16),{0xe8,0xf1,0},"EX declaration");
    eq(track(pcm,15,16),{0xfd,255,0xed,6,0xdf,47,0xf1,0},"PCM bank and channel W");
    eq(track(good("A @v127 v0 q8 @q0 @q128 D-1 y$20,%11_000_101\n")),{0xfb,128,0xfb,0,0xf8,8,0xf8,8,0xf8,128,0xf3,255,255,0xfe,32,197,0xf1,0},"control operands");
    eq(track(good("A t120 @t0 $FO10 MD4\n")),{0xff,216,0xff,0,0xe7,1,10,0xe9,4,0xf1,0},"tempo and control");
    eq(track(good(voice+"@@0=\"v12\"\nA SMOF @0 SMON @0 c\n")),{0xfd,0,0xfd,0,0xfb,12,0xad,47,0xf1,0},"tone macros");
    eq(track(good("#nlist\ngarbage here\n#list\nA c /* comment\nA d * comment\n")),{0xad,47,0xaf,47,0xf1,0},"NOTE comments");
    notemdx::Options opts;
    opts.include_loader=[](const std::string& request,const std::string&,notemdx::Source& out,std::string&) { out={request,"i=\"c\"\n"}; return true; };
    eq(track(good("#include \"part.mml\"\nA i\n",opts)),{0xad,47,0xf1,0},"include callback");
    opts.muted_channels="A";
    eq(track(good("A BAD INPUT\n",opts)),{0xf1,0},"muted channel");
    for(const auto* s:{"A c7","A c%0","A c%257","A o0c","A [c","A c]","A [c]0","A ?c","A q0","A @1c","Q c","A t0","A L","A LLc","A LCc","A AP0,1,1c","A y$100,1","A D32768","#unknown 1","i=\"i\"\nA i","A [c/d/e]2","A Lt120","A $9","A o0 `d`4","A MP0,0,1c","A MA4,2,1c","A MP1,2,128c","A MH0,0,0,0,0,4,0c","A {cde}%2","A {{cd}4e}4","A L[/c]1","A GL6144,1c4","A c4_r4"}) bad(s);
    eq(track(good("A MP1,2,-24 c4 MPOF MPON\n")),{0xec,1,0,4,0xe8,0,0xad,47,0xec,128,0xec,129,0xf1,0},"negative square LFO and switches");
    eq(track(good("A MP0,7,-17 MA2,7,-17\n")),{0xec,0,0,28,0xfe,0xca,0xeb,2,0,14,0xfe,0xca,0xf1,0},"LFO signed division truncates toward zero");
    eq(track(good("A {cd}%3\n")),{0xad,1,0xaf,0,0xf1,0},"tuplet half-boundary rounding");
    eq(track(good("A [{cde}%10]2\n")),{0xf6,2,0,0xad,2,0xaf,3,0xb1,2,0xf5,0xff,0xf7,0xf1,0},"tuplet inside native repeat");
    eq(track(good("A c%7_D-17\n")),{0xf2,0xfd,0x93,0xad,6,0xf1,0},"portamento signed division");
    eq(track(good("A GL-32,6 D4 c4\n")),{0xf3,0,4,0xf3,0xff,0xe4,0xf2,5,0x55,0xf7,0xad,5,0xf3,0,4,0xad,41,0xf1,0},"glide restores detune");
    eq(track(good("A GL-32,6 n45,4 {cd}4\n")),{0xad,47,0xad,23,0xaf,23,0xf1,0},"glide excludes numbered notes and tuplets");
    eq(track(good("A @q129 c%256 @q192 c%256\n")),{0xf8,127,0xad,255,0xf8,64,0xad,255,0xf1,0},"NOTE extended quantize encoding");
    eq(track(good("A v8 x3 c%1 d%1 @x64 e%1 f%1\n")),{0xfb,8,0xfb,3,0xad,0,0xfb,8,0xaf,0,0xfb,64,0xb1,0,0xfb,8,0xb2,0,0xf1,0},"NOTE temporary volume encoding");
    auto unused=good(voice+"A r4\n");
    check(base(unused.mdx)+word(unused.mdx,base(unused.mdx))==unused.mdx.size(),"unused FM voice omitted");
    const std::string wave="#wavemem\n@w0={1,0,1,2,3,0}\n";
    eq(track(good("#wavemem\n@w0={0,0,1,3}\nA AP0,1,1 a%48\n")),{0xfc,1,0xf7,0xb6,0,0xfc,3,0xb6,46,0xf1,0},"non-looping wave holds final value");
    eq(track(good("#wavemem\n@w0={1,0,7}\n@w1={1,0,-3}\nA DT0,2,1 TD1,2,1 c%1 DTOF TDOF\n")),{0xf3,0,7,0xf3,0,4,0xad,0,0xf3,0,0,0xf1,0},"combined detune contributions and independent stop");
    eq(track(good("A {c_e d_D-1}%10\n")),{0xf2,0x33,0x33,0xad,4,0xf2,0xff,0xcd,0xaf,4,0xf1,0},"tuplet portamento uses allocated duration");
    eq(track(good(wave+"A Q128 AP0,4,1 c8 APOF\n")),{0xfc,1,0xf7,0xad,3,0xfc,2,0xf7,0xad,3,0xfc,3,0xad,3,0xfc,0,11,0xfc,3,0xf1,0},"Q divides sounding and silent portions");
    eq(track(good(voice+"#wavemem\n@w0={0,0,0}\nA @0 TLM-1 TL0,1,1 c%1 TLOF\n")),{0xfd,0,0xfe,0x60,28,0xfe,0x70,24,0xfe,0x68,9,0xad,0,0xfe,0x60,28,0xfe,0x70,24,0xfe,0x68,9,0xf1,0},"TL automatic modulator mask and source OP order");
    eq(track(good("A v8 @x0 c%1 @x1 c%1 t120 c%1\n")),{0xfb,8,0xfb,0,0xad,0,0xfb,1,0xad,0,0xff,216,0xfb,8,0xad,0,0xf1,0},"temporary volume restoration occurs at next ordinary note");
    for(const std::string& text : {
        std::string("@w0={0,0,1}\nA c"),
        std::string("#wavemem\n@w0={1,2,1,2}\nA c"),
        std::string("#wavemem\n@w0={0,0}\nA c"),
        std::string("#wavemem\n@w0={0,0,32768}\nA c"),
        std::string("A AP0,1,1 c"),
        std::string("@k0={0,1}\nA c"),
        std::string("A KS0 c"),
        wave+"A AP0,0,1 c",
        wave+"A AP0,1,3 c",
        wave+"A AP0,1,1 {cd}4",
        wave+"A TL0,1,1 c",
        wave+"A @0 TL0,1,1 c\n"+voice,
        wave+"A AP0,1,0 c%3\n@w0={0,0,1}\nA c"}) bad(text);
    std::string map="@k0={";
    for(int i=0;i<96;++i) {if(i) map+=",";map+=i==45?"1":"0";}
    map+="}\n";
    std::string voice1=voice;voice1.replace(1,1,"1");
    eq(track(good(voice+voice1+map+"@@1=\"v2\"\nA @0 KS0 c4d4e4 KSOF\n")),{0xfd,1,0xad,47,0xfd,0,0xaf,47,0xb1,47,0xf1,0},"key map switches voices without tone macros or redundant selects");
    eq(track(good(wave+"A AP0,2,2 APD-2 c4d4e4 APOF\n")),{0xfc,1,0xad,47,0xaf,47,0xfc,2,0xb1,47,0xfc,3,0xf1,0},"negative delay is ignored in asynchronous note mode");
    eq(track(good(wave+"A AP0,2,2 APD2 c4d4e4 APOF\n")),
       track(good(wave+"A AP0,2,2 APD0 c4d4e4 APOF\n")),"positive delay is ignored in note mode, per g02");
    eq(track(good("#compress 1\nA c%128&c%128 d%1&d%256 r%1 r%127 r%1\n")),
       {0xad,255,0xf7,0xaf,0,0xaf,255,127,0,0xf1,0},"compression limits and tie removal");
    eq(track(good("#compress 1\nA c4& L c4\n")),
       {0xf7,0xad,47,0xad,47,0xf1,0xff,0xfb},"compression must not cross infinite-loop entry");
    eq(track(good("#opt \"*\"\nA D0 D0 c4 L D0 D0 c4\n")),
       {0xf3,0,0,0xad,47,0xf3,0,0,0xad,47,0xf1,0xff,0xf8},"optimization keeps loop-entry state");
    eq(track(good("#opt \"d\"\nA D0 D0 v8 v8 c4\n")),
       {0xf3,0,0,0xfb,8,0xfb,8,0xad,47,0xf1,0},"optimization selectors");
    bad("#opt \"bad\"\nA c");
    bad("#load-tone \"missing.bin\"\nA c");
    auto banks=good(voice+"#wavemem\n@w0={1,0,-32768,32767}\n#save-tone\n#save-wave\nA @0 c\n");
    check(banks.auxiliary_files.size()==2,"module returns auxiliary banks");
    check(banks.auxiliary_files[0].name=="tone.bin" && banks.auxiliary_files[0].bytes.size()==7168,"default tone bank");
    check(banks.auxiliary_files[1].name=="wave.bin" && banks.auxiliary_files[1].bytes.size()==131968,"default wave bank");
    notemdx::Options bank_options;
    bank_options.binary_loader=[&](const std::string& name,const std::string& parent,std::vector<std::uint8_t>& bytes,std::string&) {
        check(parent=="test.mml","binary loader parent");
        bytes=banks.auxiliary_files[name=="tone.bin"?0:1].bytes;return true;
    };
    auto reloaded=good("#load-tone \"tone.bin\"\n#wavemem\n#load-wave \"wave.bin\"\n#save-tone\n#save-wave\nA @0 c\n",bank_options);
    eq(reloaded.mdx,banks.mdx,"binary tone round trip");
    eq(reloaded.auxiliary_files[1].bytes,banks.auxiliary_files[1].bytes,"binary wave round trip");
    bank_options.binary_loader=[](const std::string&,const std::string&,std::vector<std::uint8_t>& bytes,std::string&) {bytes={0};return true;};
    auto invalid_bank=notemdx::compile({"test.mml","#save-tone\n#load-tone \"bad.bin\"\nA c"},bank_options);
    check(!invalid_bank.ok() && invalid_bank.mdx.empty() && invalid_bank.auxiliary_files.empty(),"invalid bank yields no partial outputs");
    eq(track(good(" A q99\n\tA q99\n0 garbage\n% ignored\nA c4\n")),
       {0xad,47,0xf1,0},"invalid first byte comments out an ordinary line");
    eq(track(good(" #include \"missing\"\n\t@0 = {not a voice\n i=\"q99\"\nA c4\n")),
       {0xad,47,0xf1,0},"indented directives definitions and macros are comments");
    eq(track(good("#wavemem\n@w0={\n\t0,0,\n 1,3\n}\nA AP0,1,1 c%2\n")),
       {0xfc,1,0xf7,0xad,0,0xfc,3,0xad,0,0xf1,0},"wave definition continuations retain leading whitespace");
    check(good(voice+"A @0 c4\n").ok(),"indented multiline voice parameters remain active");
    eq(track(good("A c4 Bad y$zz,0 [ $Z99\nA d4\n")),
       {0xad,47,0xaf,47,0xf1,0},"invalid uppercase comments out only the rest of its physical line");
    eq(track(good("A c4 HIgnored\nA d4\n")),
       {0xad,47,0xaf,47,0xf1,0},"uppercase H inside a track is a comment, not a channel");
    eq(track(good("A SA c4\n")),{0xef,0,0xad,47,0xf1,0},"sync channel operand is not a comment");
    eq(track(good("A $FO12 c4\n")),{0xe7,1,12,0xad,47,0xf1,0},"dollar commands retain uppercase names");
    bad("#BAD\nA c4");bad("A $B");
    eq(track(good("A d++4 d--4 d+\"4 d\"+4 d+-4 d=-4\n")),
       {0xb1,47,0xad,47,0xaf,47,0xb0,47,0xaf,47,0xae,47,0xf1,0},"accidentals accumulate and natural resets");
    eq(track(good("#sharp \"d\"\nA d4 d+4 d++4 d\"4 d\"+4\n")),
       {0xb0,47,0xb0,47,0xb1,47,0xaf,47,0xb0,47,0xf1,0},"explicit accidentals override key signature before accumulation");
    auto chord_acc=good("AB `d++ d--`4\n");
    eq(track(chord_acc,0),{0xb1,47,0xf1,0},"double sharp in chord");
    eq(track(chord_acc,1),{0xad,47,0xf1,0},"double flat in chord");
    eq(track(good("A `d+\"`4\n")),{0xaf,47,0xf1,0},"natural after sharp in chord");
    eq(track(good("A c4_d++\n")),track(good("A c4_e\n")),"repeated accidentals in portamento target");
    eq(track(good(voice+"@@0=\"v8\"Cymbal\nA @0 c4\n")),
       {0xfd,0,0xfb,8,0xad,47,0xf1,0},"tone macro ends at closing quote");
    eq(track(good("i=\"c4\"Bass\nA i d4\n")),{0xad,47,0xaf,47,0xf1,0},"ordinary macro ends at closing quote");
    eq(track(good("A r4&r4 c4\n")),{47,47,0xad,47,0xf1,0},"rest ties do not emit F7");
    eq(track(good("#wavemem\n@w0={1,0,1}\nA q7 AP0,96,0 c%36 APOF\n")),
       {0xf8,7,0xf8,8,0xfc,1,0xad,31,3,0xf8,7,0xfc,3,0xf1,0},"wave gate rounds fractional ticks upward");
    eq(track(good("#wavemem\n@w0={1,0,1}\nA q4 AP0,96,0 [c4]2 APOF\n")),
       {0xf8,4,0xf8,8,0xf6,2,0,0xfc,1,0xad,23,23,0xf5,0xff,0xf8,0xf8,4,0xfc,3,0xf1,0},"wave quantize precedes repeat entry");
    notemdx::Options optimized;
    optimized.compression=0;optimized.optimization="*";
    eq(track(good("A r%1 r%2 p1 p2 c4\n",optimized)),
       {2,0xfc,2,0xad,47,0xf1,0},"host overrides rest compression and pending pan");
    eq(track(good("A v3 r4 v4 c4\n",optimized)),
       {0xfb,4,47,0xad,47,0xf1,0},"optimized volume assignment spans a rest");
    eq(track(good("#opt\n#compress 1\nA r%1 r%2 p1 p1 c4&c4\n",optimized)),
       {2,0xfc,1,0xf7,0xad,47,0xad,47,0xf1,0},"host settings take precedence over directives");
    eq(track(good("A r%1 p1 r%2 p1 r%3 c4\n",optimized)),
       {0,0xfc,1,1,2,0xad,47,0xf1,0},"removed parameter commands still break rest compression");
    eq(track(good("A MPOF c4 MPOF c4\n",optimized)),
       {0xec,0x80,0xad,47,0xec,0x80,0xad,47,0xf1,0},"uninitialized LFO differs from an initialized disabled LFO");
    eq(track(good("A MP2,3,24 c4 MPOF c4 MP2,3,24 c4 MP2,3,24 c4\n",optimized)),
       {0xec,2,0,6,8,0,0xad,47,0xec,0x80,0xad,47,0xec,0x81,0xad,47,0xad,47,0xf1,0},"reuse LFO parameters and restart with ON");
    eq(track(good("A MP2,3,24 c4 MPOF c4 MPOF c4\n",optimized)),
       {0xec,2,0,6,8,0,0xad,47,0xec,0x80,0xad,47,0xad,47,0xf1,0},"redundant OFF removed only after initialization");
    optimized.compression=9;
    check(!notemdx::compile({"bad.mml","A c"},optimized).ok(),"reject invalid host compression");
    optimized.compression=0;optimized.optimization="bad";
    check(!notemdx::compile({"bad.mml","A c"},optimized).ok(),"reject invalid host optimization");
    auto loc=notemdx::compile({"location.mml","A c\nA q9\n"});
    check(!loc.ok() && loc.diagnostics[0].line==2 && loc.diagnostics[0].channel=='A',"error location");
    for(int n=1;n<=256;++n) {
        auto rr=good("A n0,%"+std::to_string(n));
        check(track(rr)[1]==n-1,"duration round trip");
    }
    auto stats=good("#title 'Statistics'\nA r4 L [c8/d16]3\nB r8 C {cde}4\nP [n0 @2 n1]3\n").statistics;
    check(stats.title=="Statistics", "statistics title");
    check(stats.tracks[0].total_steps.value==144 && stats.tracks[0].loop_steps->value==96, "repeat escape timing");
    check(stats.tracks[1].total_steps.value==72 && stats.tracks[1].loop_steps->value==48, "tuplet pseudo loop timing");
    check(!stats.tracks[2].loop_steps && stats.tracks[2].total_steps.value==0, "empty track statistics");
    check(stats.pcm_banks.size()==2 && stats.pcm_banks[0].bank==0 && stats.pcm_banks[0].used==std::vector<int>{0}, "initial PCM bank");
    check(stats.pcm_banks[1].bank==2 && stats.pcm_banks[1].used==std::vector<int>({0,1}) && stats.pcm_banks[1].unused.size()==94, "bank state across repeat");
    stats=good("P L n0 @2 n1\n").statistics;
    check(stats.pcm_banks[1].used==std::vector<int>({0,1}), "bank inherited on infinite loop");
    stats=good("A [[r%1/r%2]3/r%4]2\nB [r%1/C r%2]1\n").statistics;
    check(stats.tracks[0].total_steps.value==18, "nested repeat escape count");
    check(!stats.tracks[1].loop_steps && stats.tracks[1].total_steps.value==1, "unreached C is absent");
    stats=good("#compress\nA r%1 C r%2\n").statistics;
    check(stats.tracks[0].loop_steps->value==2 && stats.tracks[0].total_steps.value==3, "C compression boundary");
    std::string huge="A ";for(int i=0;i<10;++i)huge+='[';huge+="r%1";for(int i=0;i<10;++i)huge+="]255";
    stats=good(huge).statistics;
    check(stats.tracks[0].total_steps.overflow, "huge repeat statistics saturate without expansion");
    stats=good(voice+voice1+"A @1 c\n").statistics;
    check(stats.fm_used==std::vector<int>{1} && stats.fm_unused==std::vector<int>{0}, "FM used and unused definitions");
    auto failed_stats=notemdx::compile({"failure.mml", "A c\nB q9\n"});
    check(!failed_stats.ok() && failed_stats.statistics.tracks[0].total_steps.value==0, "failed result clears partial statistics");
    notemdx::Options legacy_options;
    legacy_options.save_tone_filename="host-tone.bin";
    legacy_options.save_wave_filename="host-wave.bin";
    legacy_options.save_parent="host/options.mml";
    std::string progressed;
    legacy_options.progress=[&](char channel) {progressed+=channel;};
    auto legacy_banks=good(voice+"#save-tone ignored.bin\nA @0 c\n",legacy_options);
    check(legacy_banks.auxiliary_files.size()==2 && legacy_banks.auxiliary_files[0].name=="host-tone.bin" && legacy_banks.auxiliary_files[1].name=="host-wave.bin", "host save names override source");
    check(legacy_banks.auxiliary_files[0].parent=="host/options.mml" && progressed=="ABCDEFGHP", "host save base and progress callback");
    legacy_options.save_banks_on_error=true;
    auto recovery=notemdx::compile({"bad.mml",voice+"A q9\n"},legacy_options);
    check(!recovery.ok() && recovery.mdx.empty() && recovery.auxiliary_files.empty() && recovery.recovery_files.size()==2, "explicit bank recovery without partial MDX");
    check(recovery.recovery_files[0].bytes==legacy_banks.auxiliary_files[0].bytes, "recovery retains registered voice data");
    legacy_options.save_banks_on_error=false;
    recovery=notemdx::compile({"bad.mml",voice+"A q9\n"},legacy_options);
    check(recovery.recovery_files.empty(), "no recovery files by default");
    notemdx::Options large_options;
    large_options.max_output_bytes=256*1024;
    std::string near_limit="P ";
    for(int i=0;i<65494;++i) near_limit+="r%1";
    auto boundary=notemdx::compile({"boundary.mml",near_limit});
    check(boundary.ok() && boundary.mdx.size()==65536, "default 64 KiB file budget is inclusive");
    boundary=notemdx::compile({"boundary.mml",near_limit+"r%1"});
    check(!boundary.ok() && boundary.mdx.empty(), "default budget rejects 65537-byte file");
    boundary=notemdx::compile({"boundary.mml",near_limit+"r%1r%1r%1"},large_options);
    check(boundary.ok() && boundary.mdx.size()==65539 && word(boundary.mdx,base(boundary.mdx))==65535, "maximum voice start offset remains representable beyond file boundary");
    boundary=notemdx::compile({"boundary.mml",near_limit+"r%1r%1r%1r%1"},large_options);
    check(!boundary.ok() && boundary.diagnostics.back().message=="Voice data offset exceeds 16 bits.", "last PCM track must not wrap voice pointer");
    large_options.max_output_bytes=64*1024*1024+1;
    check(!notemdx::compile({"bad-limit.mml","A c"},large_options).ok(), "reject file budget above API limit");
    std::cout << assertions << " assertions passed\n";
}
