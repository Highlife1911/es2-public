/* window-lint: allow-file inherits console: Bazel test helper; the child shares the windowless console of its parent (execv: non-Windows branch) */
/* Host-script fixture driver. Original executables are never test data. */
#include <process.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int locate(const char *name, char path[4096])
{
    FILE *file = fopen(name, "rb");
    const char *manifest;
    char line[8192], prefix[2048];
    if (file) { fclose(file); snprintf(path,4096,"%s",name); return 1; }
    manifest = getenv("RUNFILES_MANIFEST_FILE");
    if (!manifest || !(file = fopen(manifest,"rb"))) return 0;
    snprintf(prefix,sizeof prefix,"_main/%s ",name);
    while (fgets(line,sizeof line,file)) {
        if (!strncmp(line,prefix,strlen(prefix))) {
            line[strcspn(line,"\r\n")]=0;
            snprintf(path,4096,"%s",line+strlen(prefix)); fclose(file); return 1;
        }
    }
    fclose(file); return 0;
}
int main(int argc, char **argv)
{
    char test[4096], runner[4096], mec[4096]; intptr_t status;
    const char *args[6];
    if (argc != 5 || !locate(argv[2],test) || !locate(argv[3],runner) || !locate(argv[4],mec)) return 2;
    args[0]=argv[1];args[1]="-B";args[2]=test;args[3]=runner;args[4]=mec;args[5]=NULL;
    status=_spawnv(_P_WAIT,argv[1],args);
    return status < 0 ? 1 : (int)status;
}
