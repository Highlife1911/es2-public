/* Shared private image loader. Configuration is supplied by each harness. */
static int load_image(const char *root) {
    char candidates[2][MAX_PATH],path[MAX_PATH];
    if(snprintf(candidates[0],MAX_PATH,"%s/VER95/" IN_VITRO_EXE,root)>=MAX_PATH ||
       snprintf(candidates[1],MAX_PATH,"%s/extracted/cd/VER95/" IN_VITRO_EXE,root)>=MAX_PATH) return fail("root too long");
    unsigned found=0; for(unsigned i=0;i<2;++i) if(GetFileAttributesA(candidates[i])!=INVALID_FILE_ATTRIBUTES) { strcpy(path,candidates[i]); ++found; }
    if(found!=1) return fail("ES2_DATA_ROOT must contain exactly one reference EXE in the two supported locations");
    HANDLE file=CreateFileA(path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
    if(file==INVALID_HANDLE_VALUE) return fail("open read-only EXE");
    HANDLE directory=CreateFileA(root,0,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,NULL,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS,NULL);
    char actual_root[MAX_PATH],actual_file[MAX_PATH];
    DWORD root_length=directory==INVALID_HANDLE_VALUE?0:GetFinalPathNameByHandleA(directory,actual_root,MAX_PATH,0);
    DWORD file_length=GetFinalPathNameByHandleA(file,actual_file,MAX_PATH,0);
    if(directory!=INVALID_HANDLE_VALUE) CloseHandle(directory);
    while(root_length && root_length<MAX_PATH && actual_root[root_length-1]=='\\') --root_length;
    if(!root_length || root_length>=MAX_PATH || !file_length || file_length>=MAX_PATH ||
       _strnicmp(actual_root,actual_file,root_length) || actual_file[root_length]!='\\') { CloseHandle(file); return fail("EXE resolves outside ES2_DATA_ROOT"); }
    LARGE_INTEGER size; if(!GetFileSizeEx(file,&size) || size.QuadPart!=IN_VITRO_FILE_SIZE) { CloseHandle(file); return fail("reference size"); }
    BYTE *bytes=malloc(IN_VITRO_FILE_SIZE); DWORD got;
    if(!bytes || !ReadFile(file,bytes,IN_VITRO_FILE_SIZE,&got,NULL) || got!=IN_VITRO_FILE_SIZE) { CloseHandle(file); free(bytes); return fail("read EXE"); }
    CloseHandle(file);
    /* Hash the exact buffer subsequently mapped (no check/reopen race). */
    HCRYPTPROV provider=0; HCRYPTHASH hash=0; BYTE digest[32]={0}; DWORD length=32;
    int ok=CryptAcquireContextW(&provider,NULL,NULL,PROV_RSA_AES,CRYPT_VERIFYCONTEXT) &&
        CryptCreateHash(provider,CALG_SHA_256,0,0,&hash) && CryptHashData(hash,bytes,IN_VITRO_FILE_SIZE,0) && CryptGetHashParam(hash,HP_HASHVAL,digest,&length,0);
    char hex[65]; static const char digits[]="0123456789abcdef";
    for(unsigned i=0;i<32;++i) { hex[i*2]=digits[digest[i]>>4]; hex[i*2+1]=digits[digest[i]&15]; } hex[64]=0;
    if(hash) CryptDestroyHash(hash); if(provider) CryptReleaseContext(provider,0);
    if(!ok || length!=32 || strcmp(hex,IN_VITRO_SHA256)) { free(bytes); return fail("SHA-256 gate"); }
    uint32_t pe=get32(bytes+0x3c); const BYTE *nt=bytes+pe,*opt=nt+24;
    uint32_t base=get32(opt+28), image_size=get32(opt+56),headers=get32(opt+60);
    if(base!=0x400000 || image_size!=IN_VITRO_IMAGE_SIZE || (uintptr_t)GetModuleHandleW(NULL)==base) { free(bytes); return fail("image bases/size"); }
    mapped=reserved ? VirtualAlloc(reserved,image_size,MEM_COMMIT,PAGE_READWRITE) : NULL;
    DWORD commit_error=mapped?ERROR_SUCCESS:GetLastError();
    if(mapped!=(BYTE *)(uintptr_t)base) {
        MEMORY_BASIC_INFORMATION info; VirtualQuery((void *)(uintptr_t)base,&info,sizeof(info));
        fprintf(stderr,"base collision: harness=%p reservation=%p allocation=%p region=%p size=%lu state=%lx type=%lx\n",GetModuleHandleW(NULL),reserved,info.AllocationBase,info.BaseAddress,(unsigned long)info.RegionSize,(unsigned long)info.State,(unsigned long)info.Type);
        free(bytes); SetLastError(reservation_error?reservation_error:commit_error);
        return fail("preferred image base unavailable");
    }
    memcpy(mapped,bytes,headers);
    const BYTE *section=opt+get16(nt+20);
    for(unsigned i=0;i<get16(nt+6);++i,section+=40) {
        uint32_t va=get32(section+12),count=get32(section+16),offset=get32(section+20);
        if(!extent(va,count,image_size)||!extent(offset,count,IN_VITRO_FILE_SIZE)) { free(bytes); return fail("section bounds"); }
        if(count) memcpy(mapped+va,bytes+offset,count);
    }
    printf("Mapped SHA-256 verified " IN_VITRO_LABEL " at 0x%08x; reloc RVA=0x%x size=0x%x; harness base=0x%08x\n",
        base,get32(opt+136),get32(opt+140),(unsigned)(uintptr_t)GetModuleHandleW(NULL));
    free(bytes);
    BYTE *stubs=VirtualAlloc(NULL,1024*16,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if(!stubs) return fail("import trap allocation");
    const BYTE *imports_dir=mapped+get32(mapped+pe+24+104);
    for(const BYTE *d=imports_dir;get32(d+12);d+=20) {
        uint32_t lookup=get32(d),iat=get32(d+16); if(!lookup) lookup=iat;
        const char *dll=(const char *)(mapped+get32(d+12));
        for(unsigned slot=0;get32(mapped+lookup+4*slot);++slot) {
            if(import_count>=1024) return fail("too many imports");
            uint32_t entry=get32(mapped+lookup+4*slot); unsigned id=import_count++;
            if(entry&0x80000000u) snprintf(imports[id],sizeof(imports[id]),"%s!#%u",dll,entry&0xffffu);
            else snprintf(imports[id],sizeof(imports[id]),"%s!%s",dll,(const char *)(mapped+entry+2));
            BYTE *s=stubs+16*id; s[0]=0x68; put32(s+1,id); s[5]=0xe8;
            put32(s+6,(uint32_t)(uintptr_t)import_trap-(uint32_t)(uintptr_t)(s+10)); s[10]=0xcc;
            put32(mapped+iat+4*slot,(uint32_t)(uintptr_t)s);
            if(!_stricmp(dll,"cw3220.dll") && !strcmp(imports[id]+strlen(dll)+1,"_memset")) put32(mapped+iat+4*slot,(uint32_t)(uintptr_t)es2_rt_memset);
            if(!_stricmp(dll,"cw3220.dll") && !strcmp(imports[id]+strlen(dll)+1,"_memcpy")) put32(mapped+iat+4*slot,(uint32_t)(uintptr_t)es2_rt_memcpy);
        }
    }
    DWORD previous; if(!VirtualProtect(stubs,1024*16,PAGE_EXECUTE_READ,&previous)) return fail("trap protection");
    first_import_trap=stubs;
    /* Only CODE is executable; globals/table data remain writable/private. */
    const BYTE *mapped_nt=mapped+pe; section=mapped_nt+24+get16(mapped_nt+20);
    for(unsigned i=0;i<get16(mapped_nt+6);++i,section+=40) if(get32(section+36)&0x20000000u) {
        if(!VirtualProtect(mapped+get32(section+12),get32(section+8),PAGE_EXECUTE_READ,&previous)) return fail("CODE protection");
    }
    FlushInstructionCache(GetCurrentProcess(),NULL,0);
    printf("All %u IAT entries replaced with named traps; no original entry point called\n",import_count);
    IN_VITRO_FPU_INIT();
    return 0;
}
