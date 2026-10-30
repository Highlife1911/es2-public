/* window-lint: allow-file inherits console: Bazel test helper; the child shares the windowless console of its parent (execv: non-Windows branch) */
/* Launches the differential harness (a data file of the group tests) with
 * the test's own arguments and environment, so one built harness serves
 * every per-group and probe target. argv[1] is the harness' runfiles path;
 * on Windows the runfiles tree is a manifest, so it is resolved there first. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <process.h>

static char *resolve(const char *name) {
    const char *manifest=getenv("RUNFILES_MANIFEST_FILE");
    char line[16384],key[4096];
    if(!manifest||!*manifest) return NULL;
    if(snprintf(key,sizeof key,"_main/%s ",name)>=(int)sizeof key) return NULL;
    FILE *stream=fopen(manifest,"rb");
    if(!stream) return NULL;
    size_t key_size=strlen(key); char *out=NULL;
    while(fgets(line,sizeof line,stream)) {
        if(strncmp(line,key,key_size)) continue;
        size_t n=strcspn(line+key_size,"\r\n");
        out=(char *)malloc(n+1);
        if(out) { memcpy(out,line+key_size,n); out[n]=0; }
        break;
    }
    fclose(stream);
    return out;
}

int main(int argc,char **argv) {
    if(argc<2) { fputs("usage: launch <harness> [arguments...]\n",stderr); return 2; }
    char *resolved=resolve(argv[1]);
    if(resolved) argv[1]=resolved;
    int status=(int)_spawnv(_P_WAIT,argv[1],(const char *const *)(argv+1));
    if(status==-1) { perror(argv[1]); return 2; }
    return status;
}
