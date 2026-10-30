#include "es2/shell/prepare.h"
#include "es2/shell/bytes.h"
#include "es2/formats/msn.h"
#include "equipment.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Independent implementation of the byte contracts in vshell_preparation.md.
 * Mutable authoring records simplify sparse overlays; serialized data is always
 * read/written explicitly, never cast to a C structure. */
static uint16_t random_word(es2_shell_rng *r) {
    uint16_t x=(uint16_t)(r->words[r->a]+r->words[r->b]);
    r->words[r->b]=x; r->a=(uint8_t)((r->a+1u)%56u); r->b=(uint8_t)((r->b+1u)%56u);
    return x;
}
es2_status es2_shell_rng_init(es2_span table, uint16_t seed, es2_shell_rng *out, es2_error *e) {
    es2_shell_rng r; size_t i;
    if (!out || !table.data || table.size!=112) return es2_fail(e,ES2_ERR_ARGUMENT,0);
    for(i=0;i<56;i++) r.words[i]=es2_le16(table.data+2*i);
    r.a=24; r.b=55;
    if(seed<=32768u) for(i=0;i<seed;i++) (void)random_word(&r);
    *out=r; es2_error_clear(e); return ES2_OK;
}
es2_status es2_shell_rng_draw(es2_shell_rng *r, int16_t bound, uint16_t *out, es2_error *e) {
    int n=bound;
    if(!r || !out || !n || r->a>=56 || r->b>=56) return es2_fail(e,ES2_ERR_ARGUMENT,0);
    if(n<0) n=-n;
    *out=(uint16_t)((random_word(r)&32767u)%(unsigned)n);
    es2_error_clear(e); return ES2_OK;
}

typedef struct record { uint8_t b[164]; uint8_t *children; size_t child_count; } record;
typedef struct list { record *v; size_t n, capacity; } list;
typedef struct text_record { int16_t id; es2_span text; } text_record;
typedef struct context {
    list lists[17];
    text_record *texts; size_t text_count;
    uint16_t header[10];
    es2_shell_state state;
    es2_error *error;
    es2_status status;
} context;
typedef struct writer { es2_shell_buffer b; size_t capacity; es2_status status; } writer;
static void put(writer *w, const void *p, size_t n) {
    size_t z; uint8_t *q;
    if(w->status!=ES2_OK) return;
    if(n>SIZE_MAX-w->b.size) {w->status=ES2_ERR_CAPACITY;return;}
    z=w->b.size+n;
    if(z>w->capacity) {
        size_t cap=z<256?256:z;
        if(cap<=SIZE_MAX/2) cap*=2;
        q=(uint8_t *)realloc(w->b.data,cap);
        if(!q) {w->status=ES2_ERR_CAPACITY;return;}
        w->b.data=q;w->capacity=cap;
    }
    if(n) memcpy(w->b.data+w->b.size,p,n);
    w->b.size=z;
}
static void word(writer *w, uint16_t x) {uint8_t p[2];es2_put16(p,x);put(w,p,2);}
static void dword(writer *w, uint32_t x) {uint8_t p[4];es2_put32(p,x);put(w,p,4);}
static void fail(context *c, es2_status status, size_t offset) {
    if(c->status==ES2_OK) c->status=es2_fail(c->error,status,offset);
}
static int lookup(const list *l, int16_t id, int compact) {
    size_t i; int result=0;
    if(id==-1) return -1;
    for(i=0;i<l->n;i++) {
        int16_t have=es2_le16s(l->v[i].b);
        if(have==id) return compact?result:(int)i;
        if(!compact || have!=-1) result++;
    }
    return -1;
}
static int dependency(const context *c, int16_t id) {
    size_t i;
    if(id==-1) return 1;
    for(i=0;i<c->lists[0].n;i++) if(es2_le16s(c->lists[0].v[i].b)==id) return 1;
    return 0;
}
static uint16_t draw(context *c, int16_t bound) {
    uint16_t x=0;
    if(c->status==ES2_OK) c->status=es2_shell_rng_draw(&c->state.rng,bound,&x,c->error);
    return x;
}
static void remap(context *c, uint8_t *p, size_t off, size_t family) {
    es2_put16(p+off,(uint16_t)lookup(&c->lists[family-1],es2_le16s(p+off),1));
}
static int string_index(context *c, int16_t id) {
    size_t i; int compact=0;
    if(id==-1) return -1;
    for(i=0;i<c->text_count;i++) {
        if(c->texts[i].id==id) return compact;
        if(c->texts[i].id!=-1) compact++;
    }
    return -1;
}
static void remap_string(context *c, uint8_t *p, size_t off) {
    es2_put16(p+off,(uint16_t)string_index(c,es2_le16s(p+off)));
}
static int range_contains(const uint8_t *r, int value) {
    return value>=(int)es2_le16(r+6) && value<=(int)es2_le16(r+8);
}
static int select_condition(context *c, int16_t selector) {
    size_t i,j; list *l=&c->lists[0];
    for(i=0;i<l->n;i++) {
        uint8_t *p=l->v[i].b;
        if(es2_le16s(p+4)!=3 || es2_le16s(p+6)!=selector) continue;
        {uint16_t x=draw(c,es2_le16s(p+10));es2_put16(p+12,x);
        if(c->status!=ES2_OK) return -1;
        for(j=0;j<l->n;j++) {
            uint8_t *q=l->v[j].b;
            if(es2_le16s(q+4)==2 && es2_le16s(q+2)==es2_le16s(p) && range_contains(q,x)) return (int)j;
        }}
    }
    return -1;
}
static const record *copy_source(context *c, size_t family, int16_t selector) {
    int index=select_condition(c,selector); size_t i; const list *l=&c->lists[family-1];
    if(index>=0) {
        int16_t id=es2_le16s(c->lists[0].v[index].b);
        for(i=0;i<l->n;i++) if(es2_le16s(l->v[i].b+2)==id) return &l->v[i];
    }
    fail(c,ES2_ERR_NOT_FOUND,family);return NULL;
}
static void overlay(uint8_t *dst,const uint8_t *src,size_t first,size_t last,int sentinel) {
    size_t p;for(p=first;p<=last;p+=2) if(es2_le16s(src+p)!=sentinel) {dst[p]=src[p];dst[p+1]=src[p+1];}
}
static void merge(size_t f,uint8_t *d,const uint8_t *s) {
    switch(f) {
    case 10: overlay(d,s,6,24,-1);overlay(d,s,28,76,-1);overlay(d,s,78,78,0);overlay(d,s,80,80,-1);break;
    case 11: overlay(d,s,6,6,-1);overlay(d,s,8,8,0);overlay(d,s,10,28,-1);break;
    case 12: overlay(d,s,8,46,0);overlay(d,s,48,72,-1);overlay(d,s,76,114,-1);
        overlay(d,s,116,134,5);overlay(d,s,136,136,2);overlay(d,s,138,140,-1);overlay(d,s,142,142,100);break;
    case 13: overlay(d,s,8,46,0);overlay(d,s,48,52,-1);overlay(d,s,56,98,-1);overlay(d,s,100,100,100);break;
    case 14: overlay(d,s,8,12,-1);overlay(d,s,16,58,-1);overlay(d,s,60,60,100);break;
    case 15: overlay(d,s,8,20,-1);break;
    case 16: overlay(d,s,6,44,0);overlay(d,s,46,118,-1);overlay(d,s,122,162,-1);break;
    default: break;
    }
}
static void retain(context *c,size_t f,record *r,int replace) {
    list *l=&c->lists[f-1];int index=replace?lookup(l,es2_le16s(r->b),0):-1;
    if(index>=0) {
        if(f>=10 && f<=16) merge(f,l->v[index].b,r->b);
        else {free(l->v[index].children);l->v[index]=*r;r->children=NULL;}
    } else if(l->n<l->capacity) {l->v[l->n++]=*r;r->children=NULL;}
    else fail(c,ES2_ERR_CAPACITY,f);
}
static int condition(context *c,record *r) {
    uint8_t *p=r->b;int dep=es2_le16s(p+2),type=es2_le16s(p+4);size_t i;
    if(type==2) {
        int value=-1;
        for(i=0;i<c->lists[0].n;i++) {
            uint8_t *q=c->lists[0].v[i].b;
            if(es2_le16s(q)!=dep) continue;
            if(es2_le16s(q+4)==1) {value=es2_le16s(q+8);break;}
            if(es2_le16s(q+4)==3) {value=-99;break;}
        }
        return value==-99 || range_contains(p,value);
    }
    if(!dependency(c,(int16_t)dep)) return 0;
    if(type==1) {es2_put16(p+8,draw(c,es2_le16s(p+6)));return 1;}
    if(type==3) return 1;
    if(type==0) {
        int index=es2_le16s(p+6),left,right=es2_le16s(p+10),yes=0;
        if(index<0 || index>=1000) {fail(c,ES2_ERR_OFFSET,6);return 0;}
        left=c->state.variables[index];
        switch(es2_le16s(p+8)) {
        case 281:yes=left==right;break;case 282:yes=left!=right;break;
        case 283:yes=left<right;break;case 284:yes=left<=right;break;
        case 285:yes=left>right;break;case 286:yes=left>=right;break;
        default:break;
        }
        es2_put16(p+8,(uint16_t)yes);return yes;
    }
    return 0;
}
static void environment(context *c,const uint8_t *p) {
    size_t i;
    if(!dependency(c,es2_le16s(p))) return;
    for(i=0;i<10;i++) if(es2_le16(p+2+2*i)!=(i==0?1:0)) c->header[i]=es2_le16(p+2+2*i);
    for(i=0;i<30;i++) if(es2_le16s(p+22+2*i)!=-1) c->state.reset_selectors[i]=es2_le16s(p+22+2*i);
}
static void transform(context *c,size_t f,record *r) {
    uint8_t *p=r->b;const record *source=NULL;size_t j;
    if((f==6 || f==7 || f==8 || (f>=12 && f<=15)) && es2_le16s(p+4)!=-1) {
        source=copy_source(c,f,es2_le16s(p+4));if(!source) return;
        switch(f) {
        case 6:memcpy(p+10,source->b+10,12);break;
        case 7:memcpy(p+8,source->b+8,2);break;
        case 8:free(r->children);r->child_count=source->child_count;
            r->children=(uint8_t *)malloc(r->child_count*2+1);
            if(!r->children) {fail(c,ES2_ERR_CAPACITY,f);return;}
            if(r->child_count) memcpy(r->children,source->children,r->child_count*2);
            es2_put16(p+8,(uint16_t)r->child_count);break;
        case 12:memcpy(p+8,source->b+8,66);memcpy(p+76,source->b+76,68);break;
        case 13:memcpy(p+8,source->b+8,46);memcpy(p+56,source->b+56,46);break;
        case 14:memcpy(p+8,source->b+8,6);memcpy(p+16,source->b+16,46);break;
        case 15:memcpy(p+8,source->b+8,14);break;
        default:break;
        }
    }
    if(f==6 && es2_le16(p+8)) {
        int a=lookup(&c->lists[5],es2_le16s(p+10),0),b=lookup(&c->lists[5],es2_le16s(p+14),0);
        if(a<0 || b<0) {fail(c,ES2_ERR_NOT_FOUND,f);return;}
        for(j=10;j<22;j+=4) es2_put32(p+j,es2_le32(c->lists[5].v[a].b+j)+es2_le32(c->lists[5].v[b].b+j));
    }
    if(source) return;
    switch(f) {
    case 4:
        for(j=2;j<=140;j+=2) remap_string(c,p,j);
        if(es2_le16s(p+142)!=-1) {
            int a=lookup(&c->lists[2],es2_le16s(p+142),0);
            if(a<0) {fail(c,ES2_ERR_NOT_FOUND,142);return;}
            es2_put16(p+142,es2_le16(c->lists[2].v[a].b+6));
        }break;
    case 9:remap(c,p,8,6);if(es2_le16s(p+6)==0)remap(c,p,10,6);break;
    case 10:for(j=10;j<=24;j+=2)remap(c,p,j,9);for(j=68;j<=76;j+=2)remap_string(c,p,j);break;
    case 11:remap(c,p,6,10);for(j=10;j<=28;j+=2)remap(c,p,j,10);break;
    case 12:remap(c,p,70,6);remap(c,p,72,7);remap(c,p,138,10);remap(c,p,140,10);break;
    case 13:remap(c,p,48,6);remap(c,p,50,7);remap(c,p,96,10);remap(c,p,98,10);break;
    case 14:remap(c,p,10,6);remap(c,p,12,7);remap(c,p,56,10);remap(c,p,58,10);break;
    case 15:remap(c,p,12,6);remap(c,p,14,8);remap(c,p,20,10);
        if(es2_le16s(p+16)>=1 && es2_le16s(p+16)<=3)remap(c,p,18,(size_t)es2_le16s(p+16)+11);break;
    case 16:remap(c,p,50,6);remap(c,p,52,7);remap(c,p,54,8);remap(c,p,118,10);
        for(j=56;j<=94;j+=2) if(es2_le16s(p+46)>=0 && es2_le16s(p+46)<=2)remap(c,p,j,(size_t)es2_le16s(p+46)+12);
        for(j=96;j<=114;j+=2)remap(c,p,j,15);break;
    case 17:remap(c,p,10,6);remap(c,p,12,8);remap_string(c,p,14);
        if(es2_le16s(p+6)==0)remap(c,p,8,16);
        else if(es2_le16s(p+6)>=1 && es2_le16s(p+6)<=3)remap(c,p,8,(size_t)es2_le16s(p+6)+11);break;
    default:break;
    }
}
static void read_family(context *c,const es2_msn *msn,size_t f) {
    const es2_msn_list *input=&msn->lists[f-1];size_t j,offset=0;
    for(j=0;j<input->count && c->status==ES2_OK;j++) {
        record r;size_t z=input->stride;int pass;
        memset(&r,0,sizeof r);
        if(f==8) z=10;
        memcpy(r.b,input->records.data+offset,z);offset+=z;
        if(f==8) {
            size_t k;r.child_count=es2_le16(r.b+8);r.children=(uint8_t *)malloc(2*r.child_count+1);
            if(!r.children) {fail(c,ES2_ERR_CAPACITY,f);return;}
            for(k=0;k<r.child_count;k++) es2_put16(r.children+2*k,(uint16_t)lookup(&c->lists[5],es2_le16s(input->records.data+offset+2*k),1));
            offset+=2*r.child_count;
        }
        if(f==1) pass=condition(c,&r);
        else pass=dependency(c,es2_le16s(r.b+((f==4 || f==17)?0:2)));
        if(pass && c->status==ES2_OK) {
            if(f!=1)transform(c,f,&r);
            if(c->status==ES2_OK)retain(c,f,&r,!(f==1 || f==4 || f==17));
        }
        free(r.children);
    }
}
static es2_status read_language(context *c,es2_span bytes) {
    es2_reader r;int16_t count;size_t i;
    es2_reader_init(&r,bytes,c->error);es2_read_i16(&r,&count);
    if(r.status!=ES2_OK)return r.status;
    if(count<0)return es2_fail(c->error,ES2_ERR_COUNT,0);
    c->texts=(text_record *)calloc((size_t)count+1,sizeof *c->texts);
    if(!c->texts)return es2_fail(c->error,ES2_ERR_CAPACITY,0);
    for(i=0;i<(size_t)count;i++) {
        int16_t id=0,dep=0,n=0;es2_span text={0};const uint8_t *end;size_t j;
        es2_read_i16(&r,&id);es2_read_i16(&r,&dep);es2_skip(&r,2);es2_read_i16(&r,&n);
        if(r.status!=ES2_OK)return r.status;
        if(n<1)return es2_fail(c->error,ES2_ERR_STRING,r.offset-2);
        es2_read_span(&r,(size_t)n,&text);if(r.status!=ES2_OK)return r.status;
        end=(const uint8_t *)memchr(text.data,0,text.size);
        if(!end)return es2_fail(c->error,ES2_ERR_STRING,r.offset-text.size);
        if(!dependency(c,dep))continue;
        text.size=(size_t)(end-text.data)+1;
        for(j=0;j<c->text_count;j++)if(id!=-1 && c->texts[j].id==id)break;
        if(j==c->text_count)c->text_count++;
        c->texts[j].id=id;c->texts[j].text=text;
    }
    return es2_reader_end(&r);
}
static void pairs(writer *w,const uint8_t *p,size_t offset) {
    size_t i;for(i=0;i<10;i++)put(w,p+offset+4*i,2);
    for(i=0;i<10;i++)put(w,p+offset+4*i+2,2);
}
/* MISSION.STR framing of 0x418014: body size, count, then per retained
 * string its length, bytes and one extra zero byte. */
static void strings_out(context *c,writer *s) {
    size_t i;uint8_t zero=0;
    dword(s,0);word(s,(uint16_t)c->text_count);
    for(i=0;i<c->text_count;i++) {
        word(s,(uint16_t)c->texts[i].text.size);
        put(s,c->texts[i].text.data,c->texts[i].text.size);put(s,&zero,1);
    }
    if(s->status==ES2_OK)es2_put32(s->b.data,(uint32_t)(s->b.size-4));
}
static es2_status serialize(context *c,es2_shell_output *out) {
    writer w={0},s={0},v={0};size_t f,i;
    for(i=0;i<10;i++)word(&w,c->header[i]);
    for(f=6;f<=17;f++) {
        list *l=&c->lists[f-1];size_t n=0;
        for(i=0;i<l->n;i++)if(f==17 || es2_le16s(l->v[i].b)!=-1)n++;
        word(&w,(uint16_t)n);
        for(i=0;i<l->n;i++) {
            record *r=&l->v[i];uint8_t *p=r->b;if(f!=17 && es2_le16s(p)==-1)continue;
            switch(f) {
            case 6:put(&w,p+10,12);break;case 7:put(&w,p+8,2);break;
            case 8:word(&w,(uint16_t)r->child_count);put(&w,r->children,2*r->child_count);break;
            case 9:put(&w,p+6,6);break;
            case 10:put(&w,p+6,20);pairs(&w,p,28);put(&w,p+68,14);break;
            case 11:put(&w,p+6,24);break;
            case 12:put(&w,p+8,66);pairs(&w,p,76);put(&w,p+116,28);break;
            case 13:put(&w,p+8,46);pairs(&w,p,56);put(&w,p+96,6);break;
            case 14:put(&w,p+8,6);pairs(&w,p,16);put(&w,p+56,6);break;
            case 15:put(&w,p+8,14);break;
            case 16:put(&w,p+6,114);pairs(&w,p,122);put(&w,p+162,2);break;
            case 17:put(&w,p+2,14);pairs(&w,p,18);break;
            default:break;
            }
        }
    }
    {size_t n=0;list *l=&c->lists[3];
    if(l->n)for(i=2;i<=20;i+=2)if(es2_le16s(l->v[0].b+i)!=-1)n++;
    word(&w,(uint16_t)n);if(n)put(&w,l->v[0].b+2,n*2);}
    strings_out(c,&s);
    for(i=0;i<1000;i++)word(&v,(uint16_t)c->state.variables[i]);
    out->script=w.b;out->strings=s.b;out->variables=v.b;
    return w.status!=ES2_OK?w.status:(s.status!=ES2_OK?s.status:v.status);
}
static es2_status make_player(const es2_shell_options *o,es2_shell_buffer *out,es2_error *e) {
    es2_mec mec;size_t required=0,i,j;writer w={0};es2_status status;
    if(o->player_template.data && o->loadout)return es2_fail(e,ES2_ERR_ARGUMENT,0);
    if(o->player_template.data) {
        status=es2_mec_parse(o->player_template,&mec,e);if(status!=ES2_OK)return status;
        status=es2_mec_write(&mec,NULL,0,5,NULL,0,&required,e);if(status!=ES2_OK)return status;
        out->data=(uint8_t *)malloc(required);if(!out->data)return es2_fail(e,ES2_ERR_CAPACITY,0);
        out->size=required;return es2_mec_write(&mec,NULL,0,5,out->data,required,&required,e);
    }
    if(!o->loadout || !o->loadout->actors || o->loadout->count>32767 || !o->loadout->count)
        return es2_fail(e,ES2_ERR_ARGUMENT,0);
    word(&w,0);word(&w,(uint16_t)o->loadout->count);
    for(i=0;i<o->loadout->count;i++) {
        const es2_shell_actor *a=&o->loadout->actors[i];
        if(a->capacity>10 || (a->capacity && (!a->items || !a->secondary))) {free(w.b.data);return es2_fail(e,ES2_ERR_ARGUMENT,i);}
        word(&w,a->pilot[0]);word(&w,a->pilot[1]);word(&w,(uint16_t)a->vehicle);word(&w,(uint16_t)a->capacity);
        for(j=0;j<a->capacity;j++)word(&w,(uint16_t)a->items[j]);
        for(j=0;j<a->capacity;j++)word(&w,a->secondary[j]);
        word(&w,0);for(j=0;j<33;j++)word(&w,a->condition[j]);
    }
    word(&w,33);put(&w,o->loadout->flags,33);*out=w.b;
    if(w.status!=ES2_OK)return es2_fail(e,w.status,0);
    return es2_mec_parse((es2_span){out->data,out->size},&mec,e);
}
static es2_status find(const es2_vfs *v,const char *path,es2_span *out,es2_error *e) {
    es2_vfs_copy copy;es2_status status=es2_vfs_find(v,path,&copy,e);
    if(status==ES2_OK)*out=copy.payload;return status;
}
static int stem_valid(const char *s) {
    size_t n=0;if(!s)return 0;
    for(;*s;s++,n++)if(!((*s>='A'&&*s<='Z')||(*s>='a'&&*s<='z')||(*s>='0'&&*s<='9')||*s=='_'))return 0;
    return n>0 && n<32;
}
static es2_status mission_name(const es2_vfs *v,const es2_shell_options *o,char *name,int16_t *group_id,es2_error *e) {
    es2_span b={0};es2_dat career;es2_dat_career_group group;int16_t index;
    es2_status status;const char *lang=o->language?o->language:"GER";char path[64];size_t n;
    if(strcmp(lang,"ENG") && strcmp(lang,"FRE") && strcmp(lang,"GER"))return es2_fail(e,ES2_ERR_ARGUMENT,0);
    status=find(v,"GAM/CAREER.DAT",&b,e);
    if(status!=ES2_OK)return status;
    status=es2_dat_parse(ES2_DAT_CAREER,b,0,&career,e);if(status!=ES2_OK)return status;
    if(o->group<0 || o->position<0)return es2_fail(e,ES2_ERR_ARGUMENT,0);
    status=es2_dat_career_group_at(&career,(size_t)o->group,&group,e);if(status!=ES2_OK)return status;
    *group_id=group.id;
    if(o->mission) {
        if(!stem_valid(o->mission))return es2_fail(e,ES2_ERR_ARGUMENT,0);
        strcpy(name,o->mission);return ES2_OK;
    }
    status=es2_dat_career_index_at(&group,(size_t)o->position,&index,e);if(status!=ES2_OK)return status;
    snprintf(path,sizeof path,"%s/MISSIONS.BIN",lang);status=find(v,path,&b,e);if(status!=ES2_OK)return status;
    if(b.size<8 || es2_le32(b.data)>(b.size-8)/2 || index<0 || (uint32_t)index>=es2_le32(b.data))return es2_fail(e,ES2_ERR_COUNT,0);
    {size_t base=8+(size_t)es2_le32(b.data)*2,off=es2_le16(b.data+8+2*(size_t)index);const uint8_t *p,*end;
    if(es2_le32(b.data+4)!=b.size-base || off>=b.size-base)return es2_fail(e,ES2_ERR_OFFSET,4);
    p=b.data+base+off;end=(const uint8_t *)memchr(p,0,b.size-base-off);
    if(!end)return es2_fail(e,ES2_ERR_STRING,base+off);
    n=(size_t)(end-p);if(n<9 || n>=40 || memcmp(p,"MSN\\",4) || memcmp(end-4,".MSN",4))return es2_fail(e,ES2_ERR_STRING,base+off);
    n-=8;memcpy(name,p+4,n);name[n]=0;}
    return stem_valid(name)?ES2_OK:es2_fail(e,ES2_ERR_STRING,0);
}
void es2_shell_output_free(es2_shell_output *out) {
    if(!out)return;
    free(out->script.data);free(out->strings.data);free(out->variables.data);free(out->player.data);memset(out,0,sizeof *out);
}
/* 0x40f68c from the stored state: no activation pass (0x410923 runs inside
 * the preparation, not here), so the projection takes the team words and
 * active bytes as the SAV holds them. */
es2_status es2_shell_launch_files(es2_span save,es2_shell_buffer *player,es2_shell_buffer *variables,es2_error *e) {
    es2_shell_state state;es2_shell_actor actors[4];es2_shell_loadout load={0};es2_shell_options o;
    es2_shell_buffer mec={0},bank={0};es2_status status;size_t i;
    if(!player || !variables || player->data || variables->data)return es2_fail(e,ES2_ERR_ARGUMENT,0);
    memset(&state,0,sizeof state);
    status=es2_shell_restore_save(save,&state,e);if(status!=ES2_OK)return status;
    status=shell_equipment_project(&state.equipment,&load,actors,e);if(status!=ES2_OK)return status;
    memset(&o,0,sizeof o);o.loadout=&load;
    status=make_player(&o,&mec,e);if(status!=ES2_OK){free(mec.data);return status;}
    bank.data=(uint8_t *)malloc(2000);
    if(!bank.data){free(mec.data);return es2_fail(e,ES2_ERR_CAPACITY,0);}
    for(i=0;i<1000;i++){bank.data[2*i]=(uint8_t)state.variables[i];bank.data[2*i+1]=(uint8_t)((uint16_t)state.variables[i]>>8);}
    bank.size=2000;*player=mec;*variables=bank;es2_error_clear(e);return ES2_OK;
}
es2_status es2_shell_campaign_mission(const es2_vfs *v,int16_t group,int16_t position,
    const char *language,char *name,size_t capacity,int16_t *group_id,es2_error *e) {
    es2_shell_options o={0};char buffer[40];int16_t id;es2_status status;
    if(!v || !name || capacity<40 || !group_id)return es2_fail(e,ES2_ERR_ARGUMENT,0);
    o.group=group;o.position=position;o.language=language;
    status=mission_name(v,&o,buffer,&id,e);
    if(status==ES2_OK){strcpy(name,buffer);*group_id=id;}
    return status;
}
es2_status es2_shell_prepare(const es2_vfs *v,const es2_shell_options *o,
    es2_shell_state *state,es2_shell_output *out,es2_error *e) {
    context c;es2_shell_output result={0};es2_msn msn;es2_span input={0},language={0};
    char name[40],path[64];int16_t group_id=0;size_t i,j;es2_status status;
    if(!v || !o || !state || !out || state->rng.a>=56 || state->rng.b>=56 || out->script.data || out->strings.data || out->variables.data || out->player.data)
        return es2_fail(e,ES2_ERR_ARGUMENT,0);
    memset(&c,0,sizeof c);c.state=*state;c.error=e;c.header[0]=1;
    status=mission_name(v,o,name,&group_id,e);if(status!=ES2_OK)return status;
    snprintf(path,sizeof path,"MSN/%s.MSN",name);status=find(v,path,&input,e);if(status!=ES2_OK)return status;
    status=es2_msn_parse(input,&msn,e);if(status!=ES2_OK)return status;
    snprintf(path,sizeof path,"MSN/%s.%s",name,o->language?o->language:"GER");
    status=find(v,path,&language,e);if(status!=ES2_OK)return status;
    for(i=0;i<17;i++) {
        c.lists[i].capacity=msn.lists[i].count;
        c.lists[i].v=(record *)calloc(msn.lists[i].count+1,sizeof(record));
        if(!c.lists[i].v) {fail(&c,ES2_ERR_CAPACITY,i);break;}
    }
    if(o->mode==0)memset(c.state.variables,0,sizeof c.state.variables);
    if(o->mode==1) {c.state.variables[1]=o->group;c.state.variables[2]=o->position;}
    c.state.variables[3]=o->difficulty;
    for(i=4;i<=6;i++)c.state.variables[i]=(int16_t)draw(&c,12);
    if(c.status==ES2_OK)read_family(&c,&msn,1);
    if(c.status==ES2_OK)for(i=0;i<msn.lists[1].count;i++)environment(&c,msn.lists[1].records.data+82*i);
    if(c.status==ES2_OK)for(i=0;i<30;i++) {
        int index=c.state.reset_selectors[i];if(index==-1)continue;
        if(index<0 || index>=1000) {fail(&c,ES2_ERR_OFFSET,22+2*i);break;}
        c.state.variables[index]=0;
    }
    if(c.status==ES2_OK)c.status=read_language(&c,language);
    for(i=3;i<=17 && c.status==ES2_OK;i++) {
        if(i==5)continue;
        read_family(&c,&msn,i);
        if(i==16) {
            for(j=0;j<c.lists[14].n;j++) {uint8_t *p=c.lists[14].v[j].b;if(es2_le16s(p+16)==0)remap(&c,p,18,16);}
            for(j=0;j<c.lists[9].n;j++) {uint8_t *p=c.lists[9].v[j].b;int type=es2_le16s(p+6);
                if(type>=7 && type<=10)remap(&c,p,80,type==10?16:(size_t)type+5);}
        }
    }
    if(msn.lists[3].count && !c.lists[3].n)fail(&c,ES2_ERR_UNSUPPORTED,4);
    if(o->mode==1 && !c.lists[3].n)fail(&c,ES2_ERR_UNSUPPORTED,4);
    if(!o->player_template.data && !o->loadout && !c.state.equipment.initialized && c.status==ES2_OK)
        c.status=shell_equipment_default(v,o,&c.state.equipment,e);
    if(o->mode==1) {c.header[0]=(uint16_t)group_id;c.header[5]=0;c.header[6]=0;c.header[7]=(!o->player_template.data && !o->loadout && c.state.equipment.initialized)?c.state.equipment.player.rank:(uint16_t)o->player_value;}
    if(o->mode==0) {c.header[0]=1;c.header[5]=o->mode0_values[0];c.header[6]=o->mode0_values[1];c.header[7]=o->mode0_values[2];c.header[8]=0;c.header[9]=o->mode0_values[3];}
    c.header[2]=1;
    if(c.status==ES2_OK)c.status=serialize(&c,&result);
    if(c.status==ES2_OK && !o->player_template.data && !o->loadout) {
        es2_shell_actor actors[4];es2_shell_loadout load={0};es2_shell_options selected=*o;
        const uint8_t *authored[4]={0};size_t count=1,k;
        if(!c.lists[15].n)fail(&c,ES2_ERR_NOT_FOUND,16);
        else {
            const uint8_t *group=c.lists[15].v[0].b;
            while(count<20 && es2_le16s(group+56+2*count)!=-1)count++;
            if(count>4)fail(&c,ES2_ERR_COUNT,count);
            else for(i=0;i<count;i++) {
                int target=es2_le16s(group+56+2*i),index=0;
                for(k=0;k<c.lists[11].n;k++)if(es2_le16s(c.lists[11].v[k].b)!=-1) {
                    if(index++==target){authored[i]=c.lists[11].v[k].b;break;}
                }
            }
        }
        if(c.status==ES2_OK)c.status=shell_equipment_mission(v,o,&c.state.equipment,count,authored,e);
        if(c.status==ES2_OK)c.status=shell_equipment_project(&c.state.equipment,&load,actors,e);
        selected.loadout=&load;
        if(c.status==ES2_OK)c.status=make_player(&selected,&result.player,e);
    } else if(c.status==ES2_OK)c.status=make_player(o,&result.player,e);
    if(c.status==ES2_OK && o->mode==1 && c.lists[3].n) {
        /* 0x4134f2 on a copy of the first retained list-4 record: three
         * reference lists (10 at +2, 30 at +0x16, 30 at +0x52), each stored
         * after its count of entries other than -1, then the word at +0x8e. */
        static const size_t at[3]={2,0x16,0x52},n[3]={10,30,30};
        const uint8_t *r=c.lists[3].v[0].b;size_t k,out=0;
        for(i=0;i<3;i++) {
            uint16_t count=0;
            for(k=0;k<n[i];k++)if(es2_le16s(r+at[i]+2*k)!=-1)count++;
            es2_put16(result.briefing+out,count);memcpy(result.briefing+out+2,r+at[i],2*n[i]);out+=2+2*n[i];
        }
        memcpy(result.briefing+out,r+0x8e,2);result.has_briefing=1;
    }
    for(i=0;i<17;i++) {for(j=0;j<c.lists[i].n;j++)free(c.lists[i].v[j].children);free(c.lists[i].v);}
    free(c.texts);
    if(c.status!=ES2_OK) {es2_shell_output_free(&result);return es2_fail(e,c.status,e?e->offset:0);}
    *state=c.state;*out=result;es2_error_clear(e);return ES2_OK;
}

/* Debrief selection, vshell_preparation.md "Debrief selection": the
 * alternate reader 0x41d072 evaluates list 1 against the given bank/RNG,
 * reads the language file and list 3, then keeps passing list-5 records in
 * a slot array where a failing record is overwritten by the next one. The
 * caller receives slot 0, as 0x41d8e7 does, and the rewritten MISSION.STR. */
void es2_shell_debrief_free(es2_shell_debrief *out) {
    if(!out)return;
    free(out->strings.data);memset(out,0,sizeof *out);
}
es2_status es2_shell_debrief_select(const es2_vfs *v,const char *mission,const char *language,
    const int16_t *variables,es2_shell_rng *rng,es2_shell_debrief *out,es2_error *e) {
    context c;es2_msn msn;es2_span input={0},text={0};char path[64];size_t i,j,slot=0;
    const es2_msn_list *five;uint8_t *slots=NULL;writer s={0};es2_status status;
    if(!v || !variables || !rng || !out || out->strings.data || rng->a>=56 || rng->b>=56 || !stem_valid(mission))
        return es2_fail(e,ES2_ERR_ARGUMENT,0);
    if(!language)language="GER";
    if(strcmp(language,"ENG") && strcmp(language,"FRE") && strcmp(language,"GER"))return es2_fail(e,ES2_ERR_ARGUMENT,0);
    memset(&c,0,sizeof c);c.error=e;c.state.rng=*rng;
    memcpy(c.state.variables,variables,sizeof c.state.variables);
    snprintf(path,sizeof path,"MSN/%s.MSN",mission);status=find(v,path,&input,e);if(status!=ES2_OK)return status;
    status=es2_msn_parse(input,&msn,e);if(status!=ES2_OK)return status;
    snprintf(path,sizeof path,"MSN/%s.%s",mission,language);status=find(v,path,&text,e);if(status!=ES2_OK)return status;
    five=&msn.lists[4];
    if(!five->count)return es2_fail(e,ES2_ERR_NOT_FOUND,5); /* null record pointer: 0x41d920 asserts */
    for(i=0;i<3;i+=2) {
        c.lists[i].capacity=msn.lists[i].count;
        c.lists[i].v=(record *)calloc(msn.lists[i].count+1,sizeof(record));
        if(!c.lists[i].v)fail(&c,ES2_ERR_CAPACITY,i);
    }
    slots=(uint8_t *)calloc(five->count,64);
    if(!slots)fail(&c,ES2_ERR_CAPACITY,5);
    if(c.status==ES2_OK)read_family(&c,&msn,1);
    if(c.status==ES2_OK)c.status=read_language(&c,text);
    if(c.status==ES2_OK)read_family(&c,&msn,3);
    for(j=0;j<five->count && c.status==ES2_OK;j++) {
        uint8_t *p=slots+64*slot;
        memcpy(p,five->records.data+64*j,64);
        if(!dependency(&c,es2_le16s(p)))continue;
        for(i=2;i<62;i+=2)if(es2_le16s(p+i)!=-1)remap_string(&c,p,i);
        if(es2_le16s(p+62)!=-1) {
            /* 0x416107 in compact mode; the result indexes the physical list. */
            int index=lookup(&c.lists[2],es2_le16s(p+62),1);
            if(index<0 || (size_t)index>=c.lists[2].n){fail(&c,ES2_ERR_NOT_FOUND,62);break;}
            es2_put16(p+62,es2_le16(c.lists[2].v[index].b+6));
        }
        slot++;
    }
    if(c.status==ES2_OK) {
        es2_shell_debrief result;memset(&result,0,sizeof result);
        for(i=0;i<30;i++){result.refs[i]=es2_le16s(slots+2+2*i);if(result.refs[i]!=-1)result.count++;}
        result.aux=es2_le16s(slots+62);result.passed=slot;
        strings_out(&c,&s);
        if(s.status!=ES2_OK)fail(&c,s.status,0);
        else {result.strings=s.b;s.b.data=NULL;*out=result;*rng=c.state.rng;}
    }
    for(i=0;i<17;i++){for(j=0;j<c.lists[i].n;j++)free(c.lists[i].v[j].children);free(c.lists[i].v);}
    free(c.texts);free(slots);free(s.b.data);
    if(c.status!=ES2_OK)return es2_fail(e,c.status,e?e->offset:0);
    es2_error_clear(e);return ES2_OK;
}
