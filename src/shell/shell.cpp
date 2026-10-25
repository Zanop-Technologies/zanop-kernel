#include "shell.hpp"
#include "../terminal.hpp"
#include "../fs/fs.hpp"
#include "../process.hpp"
#include "../syscall.hpp"
#include "../memory/allocator.hpp"
#include "../signals.hpp"
namespace {
char line[256];
char current_directory[48] = "/";
bool equal(const char* a, const char* b) { size_t i=0; while(a[i] && b[i] && a[i]==b[i]) ++i; return a[i]==b[i]; }
size_t length(const char* text) { size_t n=0; while(text[n]) ++n; return n; }
void copy(char* out,const char* in,size_t capacity) { size_t i=0; while(i+1<capacity && in[i]) { out[i]=in[i]; ++i; } out[i]=0; }
void out(const char* text) { Terminal::write(text); }
void output(const char* text) { out(text); out("\n"); }
void resolve(const char* name,char* result,size_t capacity) {
    if(!name || !name[0]) { copy(result,current_directory,capacity); return; }
    if(name[0]=='/') { copy(result,name,capacity); return; }
    size_t i=0; const size_t cwd=length(current_directory);
    while(i<cwd && i+1<capacity) { result[i]=current_directory[i]; ++i; }
    if(cwd>1 && i+1<capacity) result[i++]='/';
    for(size_t j=0;name[j] && i+1<capacity;++j) result[i++]=name[j];
    result[i]=0;
}
void list(const char* path) {
    const size_t count=FS::directory_entry_count(path);
    FS::DirectoryEntry entry;
    for(size_t i=0;i<count;++i) if(FS::directory_entry(path,i,entry)) {
        out(entry.is_directory?"[dir] ":"      "); output(entry.name);
    }
}
void number(unsigned value) { char digits[12]; size_t n=0; do { digits[n++]=static_cast<char>('0'+value%10); value/=10; } while(value && n<sizeof(digits)); while(n) Terminal::put(digits[--n]); }
void command() {
    size_t start=0; while(line[start]==' ') ++start;
    if(!line[start]) return;
    if(equal(line+start,"help")) { output("help ls [path] cd [path] pwd cat <file> mkdir <path> touch <file> rm <file> rmdir <dir> write <file> <text> ps free clear echo <text>"); return; }
    if(equal(line+start,"pwd")) { output(current_directory); return; }
    if(equal(line+start,"ls")) { list(current_directory); return; }
    if(line[start]=='l' && line[start+1]=='s' && line[start+2]==' ') { char path[48]; resolve(line+start+3,path,sizeof(path)); list(path); return; }
    if(equal(line+start,"cd")) { copy(current_directory,"/",sizeof(current_directory)); return; }
    if(line[start]=='c' && line[start+1]=='d' && line[start+2]==' ') {
        char path[48]; resolve(line+start+3,path,sizeof(path));
        if(line[start+3]=='.' && line[start+4]=='.') {
            size_t n=length(current_directory);
            while(n>1 && current_directory[n-1]!='/') --n;
            if(n>1) --n;
            current_directory[n]=0;
        } else if(FS::is_directory(path)) copy(current_directory,path,sizeof(current_directory));
        else output("cd: not a directory");
        return;
    }
    if(line[start]=='e' && line[start+1]=='c' && line[start+2]=='h' && line[start+3]=='o' && line[start+4]==' ') { output(line+start+5); return; }
    if(line[start]=='c' && line[start+1]=='a' && line[start+2]=='t' && line[start+3]==' ') { char path[48]; resolve(line+start+4,path,sizeof(path)); const char* content=FS::read_file(path); if(content) output(content); else output("cat: file not found"); return; }
    if(line[start]=='m' && line[start+1]=='k' && line[start+2]=='d' && line[start+3]=='i' && line[start+4]=='r' && line[start+5]==' ') { char path[48]; resolve(line+start+6,path,sizeof(path)); output(FS::create_directory(path)?"created":"mkdir failed"); return; }
    if(line[start]=='t' && line[start+1]=='o' && line[start+2]=='u' && line[start+3]=='c' && line[start+4]=='h' && line[start+5]==' ') { char path[48]; resolve(line+start+6,path,sizeof(path)); output(FS::create_file(path,"")?"created":"touch failed"); return; }
    if(line[start]=='r' && line[start+1]=='m' && line[start+2]==' ') { char path[48]; resolve(line+start+3,path,sizeof(path)); output(FS::remove_file(path)?"removed":"rm failed"); return; }
    if(line[start]=='r' && line[start+1]=='m' && line[start+2]=='d' && line[start+3]=='i' && line[start+4]=='r' && line[start+5]==' ') { char path[48]; resolve(line+start+6,path,sizeof(path)); output(FS::remove_directory(path)?"removed":"rmdir failed"); return; }
    if(line[start]=='w' && line[start+1]=='r' && line[start+2]=='i' && line[start+3]=='t' && line[start+4]=='e' && line[start+5]==' ') {
        const char* args=line+start+6; size_t n=0; while(args[n] && args[n]!=' ') ++n;
        if(!args[n]) { output("usage: write <file> <text>"); return; }
        char name[48]; size_t k=n<sizeof(name)-1?n:sizeof(name)-1; for(size_t i=0;i<k;++i) name[i]=args[i]; name[k]=0;
        char path[48]; resolve(name,path,sizeof(path)); const char* text=args+n+1;
        output(FS::write_file(path,reinterpret_cast<const uint8_t*>(text),length(text))?"written":"write failed"); return;
    }
    if(equal(line+start,"ps")) { Process::ProcessInfo info; for(size_t i=0;Process::info(i,info);++i) { number(info.pid); out(" "); output(info.name); } return; }
    if(equal(line+start,"free")) { out("heap free: "); number(static_cast<unsigned>(Memory::free_bytes())); out(" bytes; fs free: "); number(static_cast<unsigned>(FS::free_bytes())); output(" bytes"); return; }
    if(equal(line+start,"clear")) { VGA::writer.clear_screen(); return; }
    output("unknown command; type help");
}
}
namespace Shell {
void run() {
    Process::init(); Signals::init(); Terminal::init();
    output("Zanop terminal ready (type help)");
    while(true) { out("zanop:"); out(current_directory); out("$ "); const size_t count=Terminal::read_line(line,sizeof(line)); if(count) command(); Process::yield(); }
}
}
