#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "hook/hook.h"
static WCHAR config_path[MAX_PATH];
static void log_event(const char *event) { (void)event; }
#include "../rng_hooks.h"
static FILE *open_input(const char *path) {
    FILE *f=fopen(path,"rb");
    if(!f) {
        const char *manifest=getenv("RUNFILES_MANIFEST_FILE");
        FILE *mf=manifest?fopen(manifest,"rb"):NULL;
        char entry[4096],key[1024];snprintf(key,sizeof(key),"_main/%s ",path);
        if(mf) {
            while(fgets(entry,sizeof(entry),mf)) if(!strncmp(entry,key,strlen(key))) {
                entry[strcspn(entry,"\r\n")]=0;f=fopen(entry+strlen(key),"rb");break;
            }
            fclose(mf);
        }
    }
    return f;
}
/* Hooks whose compare mode is refused have no documented compare selection. */
static int compare_refused(unsigned i) { const es2_hook_extension *x=function_hook_extension(i); return x && x->compare_unsupported; }
int main(int argc,char **argv) {
    if(argc!=2 && argc!=3) return 1;
    FILE *f=open_input(argv[1]);
    if(!f) {fprintf(stderr,"Missing registry name file: %s\n",argv[1]);return 2;}
    unsigned count=0;char line[128];
    while(fgets(line,sizeof(line),f)) {
        line[strcspn(line,"\r\n")]=0;
        if(count>=FUNCTION_HOOK_COUNT || strcmp(line,function_hook_descriptor(count)->name)) return 3;
        ++count;
    }
    fclose(f);if(count!=FUNCTION_HOOK_COUNT) return 4;
    unsigned index=0;
#define ES2_HOOK(hook_name,address,descriptor,extension,group) \
    if(strcmp(function_hook_descriptor(index)->name,#hook_name) || function_hook_descriptor(index)->rva != address || \
       function_hook_descriptor(index)!=&descriptor || function_hook_extension(index)!=(extension)) return 5; \
    ++index;
#include "../hook_registry.h"
#undef ES2_HOOK
    for(unsigned i=0;i<count;++i) for(unsigned j=0;j<i;++j)
        if(!strcmp(function_hook_descriptor(i)->name,function_hook_descriptor(j)->name) ||
            function_hook_descriptor(i)->rva==function_hook_descriptor(j)->rva) return 6;
    /* hooks.md arrives as the second argument (a data input of the Bazel
       test); a direct run may name it through ES2_HOOK_DOCS instead. */
    const char *docs_path=argc==3 ? argv[2] : getenv("ES2_HOOK_DOCS");
    if(!docs_path || !*docs_path) {
        printf("PASS: registry %u names, RVAs, descriptors, extensions and PowerShell names agree\n",count);
        return 0;
    }
    f=open_input(docs_path);if(!f)return 7;
    /* The marker and selection lines grow by about 25 bytes per hook. */
    static char expected[65536]="<!-- H4 registry names: ",selection[65536]="-Hooks \"",doc_line[65536];
    for(unsigned i=0;i<count;++i) {
        const char *name=function_hook_descriptor(i)->name;
        if(i) {strcat(expected,",");strcat(selection,",");}
        strcat(expected,name);strcat(selection,name);strcat(selection,"=replaced");
    }
    strcat(expected," -->");strcat(selection,"\"");
    unsigned marker_count=0,selection_count=0,compare_count[FUNCTION_HOOK_COUNT]={0};
    while(fgets(doc_line,sizeof(doc_line),f)) {
        doc_line[strcspn(doc_line,"\r\n")]=0;
        if(!strcmp(doc_line,expected))++marker_count;
        if(!strcmp(doc_line,selection))++selection_count;
        for(unsigned i=ES2_HOOK_INDEX_euler_matrix;i<count;++i) {
            if(compare_refused(i)) continue;
            char compare[128];snprintf(compare,sizeof(compare),"-Hooks \"%s=compare\"",function_hook_descriptor(i)->name);
            if(!strcmp(doc_line,compare))++compare_count[i];
        }
    }
    fclose(f);if(marker_count!=1||selection_count!=1)return 8;
    for(unsigned i=ES2_HOOK_INDEX_euler_matrix;i<count;++i)if(!compare_refused(i)&&compare_count[i]!=1)return 9;
    printf("PASS: registry %u names, RVAs, descriptors, extensions, PowerShell names and documented selections agree\n",count);
    return 0;
}
