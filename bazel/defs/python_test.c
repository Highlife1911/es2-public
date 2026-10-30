/* window-lint: allow-file inherits console: Bazel test helper; the child shares the windowless console of its parent (execv: non-Windows branch) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif
static char *resolve(const char *name) {
    const char *manifest=getenv("RUNFILES_MANIFEST_FILE");FILE *stream;
    char line[16384],key[4096];size_t key_size;
    if(!manifest)return NULL;
    if(snprintf(key,sizeof key,"_main/%s ",name)>=(int)sizeof key)return NULL;
    key_size=strlen(key);stream=fopen(manifest,"rb");if(!stream)return NULL;
    while(fgets(line,sizeof line,stream)) {
        if(strncmp(line,key,key_size)==0) {
            size_t n=strcspn(line+key_size,"\r\n");char *out=(char *)malloc(n+1);
            if(out){memcpy(out,line+key_size,n);out[n]=0;}fclose(stream);return out;
        }
    }
    fclose(stream);return NULL;
}
int main(int argc,char **argv) {
    int i;
    if(argc<3){fputs("missing test interpreter/script\n",stderr);return 2;}
    for(i=3;i<argc;i++){char *path=resolve(argv[i]);if(path)argv[i]=path;}
#ifdef _WIN32
    return (int)_spawnv(_P_WAIT,argv[1],(const char *const *)(argv+1));
#else
    execv(argv[1],argv+1);perror("execv");return 2;
#endif
}
