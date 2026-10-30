/* window-lint: allow-file inherits console: Bazel test helper; the child shares the windowless console of its parent (execv: non-Windows branch) */
/* Local script integration uses the pinned Python and system PowerShell.
   The scripts are Bazel data files, resolved through the runfiles manifest. */
#include <process.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int resolve(const char *name,char path[4096]) {
    FILE *f=fopen(name,"rb");
    if(f) { fclose(f); snprintf(path,4096,"%s",name); return 1; }
    const char *manifest=getenv("RUNFILES_MANIFEST_FILE");
    f=manifest?fopen(manifest,"rb"):NULL;
    if(!f) return 0;
    char key[1024],line[8192]; snprintf(key,sizeof(key),"_main/%s ",name);
    while(fgets(line,sizeof(line),f)) if(!strncmp(line,key,strlen(key))) {
        line[strcspn(line,"\r\n")]=0;
        snprintf(path,4096,"%s",line+strlen(key)); fclose(f); return 1;
    }
    fclose(f); return 0;
}
int main(int argc,char **argv) {
    if(argc!=6)return 2;
    char paths[4][4096];
    for(unsigned i=0;i<4;++i) if(!resolve(argv[i+2],paths[i])) {
        fprintf(stderr,"Missing fixture input: %s\n",argv[i+2]); return 1;
    }
    const char *args[]={argv[1],"-B",paths[0],paths[1],paths[2],paths[3],NULL};
    intptr_t result=_spawnv(_P_WAIT,argv[1],args);return result<0?1:(int)result;
}
