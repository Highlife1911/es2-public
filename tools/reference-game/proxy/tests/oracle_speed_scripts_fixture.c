/* window-lint: allow-file inherits console: Bazel test helper; the child shares the windowless console of its parent (execv: non-Windows branch) */
#include <process.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int resolve(const char *name,char path[4096]) {
    FILE *f=fopen(name,"rb");if(f){fclose(f);snprintf(path,4096,"%s",name);return 1;}
    const char *manifest=getenv("RUNFILES_MANIFEST_FILE");f=manifest?fopen(manifest,"rb"):NULL;if(!f)return 0;
    char key[1024],line[8192];snprintf(key,sizeof(key),"_main/%s ",name);
    while(fgets(line,sizeof(line),f))if(!strncmp(line,key,strlen(key))){line[strcspn(line,"\r\n")]=0;snprintf(path,4096,"%s",line+strlen(key));fclose(f);return 1;}
    fclose(f);return 0;
}
int main(int argc,char **argv) {
    if(argc!=4)return 2;char script[4096],runner[4096];
    if(!resolve(argv[2],script)||!resolve(argv[3],runner))return 2;
    const char *args[]={argv[1],"-B",script,runner,NULL};
    intptr_t status=_spawnv(_P_WAIT,argv[1],args);return status<0?1:(int)status;
}
