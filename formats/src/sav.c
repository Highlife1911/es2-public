#include "es2/formats/sav.h"
#include <string.h>

#define TRY(call) do { es2_status status_=(call);if(status_!=ES2_OK)return status_; } while(0)
static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | (unsigned)p[1]<<8); }
static es2_status count(es2_reader *r,size_t *out) {
    int16_t n;TRY(es2_read_i16(r,&n));
    if(n<0)return es2_fail(r->error,ES2_ERR_COUNT,r->offset-2);
    *out=(size_t)n;return ES2_OK;
}
static es2_status pilot(es2_reader *r,es2_sav_pilot *out) {
    es2_sav_pilot p;size_t n;p.offset=r->offset;
    TRY(es2_read_span(r,4,&p.prefix));TRY(count(r,&n));
    if(!n)return es2_fail(r->error,ES2_ERR_STRING,r->offset-2);
    TRY(es2_read_span(r,n,&p.name));
    if(p.name.data[n-1]!=0)return es2_fail(r->error,ES2_ERR_STRING,r->offset-n);
    TRY(es2_read_span(r,25,&p.suffix));*out=p;return ES2_OK;
}
static es2_status loadout(es2_reader *r,es2_sav_loadout *out) {
    es2_sav_loadout l;l.offset=r->offset;
    TRY(es2_read_i16(r,&l.index));TRY(es2_read_span(r,76,&l.fixed));TRY(count(r,&l.count));
    TRY(es2_read_span(r,12*l.count,&l.items));*out=l;return ES2_OK;
}
es2_status es2_sav_parse(es2_span input,es2_sav *out,es2_error *e) {
    es2_sav s;es2_reader r;size_t i,start;
    if(!out)return es2_fail(e,ES2_ERR_ARGUMENT,0);
    memset(&s,0,sizeof s);s.input=input;TRY(es2_reader_init(&r,input,e));
    for(i=0;i<33;i++) {
        TRY(es2_read_u8(&r,&s.inventory[i].flag));TRY(count(&r,&s.inventory[i].count));
        TRY(es2_read_span(&r,10*s.inventory[i].count,&s.inventory[i].items));
    }
    TRY(es2_read_span(&r,22,&s.five_entry_bank));TRY(es2_read_span(&r,152,&s.selection));
    start=r.offset;
    for(i=0;i<36;i++){es2_sav_pilot p;TRY(pilot(&r,&p));}
    TRY(es2_subspan(input,start,r.offset-start,&s.roster,e));
    TRY(es2_read_span(&r,12,&s.roster_selection));TRY(es2_read_span(&r,4,&s.team));TRY(pilot(&r,&s.player));
    TRY(count(&r,&s.loadout_count));start=r.offset;
    for(i=0;i<s.loadout_count;i++){es2_sav_loadout l;TRY(loadout(&r,&l));}
    TRY(es2_subspan(input,start,r.offset-start,&s.loadouts,e));
    TRY(es2_read_span(&r,18,&s.nine_word_bank));TRY(es2_read_span(&r,4,&s.tally));
    s.variable_offset=r.offset;TRY(es2_read_span(&r,2000,&s.variables));
    TRY(es2_read_span(&r,22,&s.final_fields));s.reader_end=r.offset;
    TRY(es2_read_span(&r,es2_remaining(&r),&s.trailing));
    *out=s;es2_error_clear(e);return ES2_OK;
}
es2_status es2_sav_pilot_at(const es2_sav *s,size_t index,es2_sav_pilot *out,es2_error *e) {
    es2_reader r;es2_sav_pilot p;size_t i;
    if(!s || !out)return es2_fail(e,ES2_ERR_ARGUMENT,0);
    if(index>=36)return es2_fail(e,ES2_ERR_OFFSET,index);
    TRY(es2_reader_init(&r,s->roster,e));
    for(i=0;i<=index;i++)TRY(pilot(&r,&p));
    p.offset+=(size_t)(s->roster.data-s->input.data);*out=p;return ES2_OK;
}
es2_status es2_sav_loadout_at(const es2_sav *s,size_t index,es2_sav_loadout *out,es2_error *e) {
    es2_reader r;es2_sav_loadout l;size_t i;
    if(!s || !out)return es2_fail(e,ES2_ERR_ARGUMENT,0);
    if(index>=s->loadout_count)return es2_fail(e,ES2_ERR_OFFSET,index);
    TRY(es2_reader_init(&r,s->loadouts,e));
    for(i=0;i<=index;i++)TRY(loadout(&r,&l));
    l.offset+=(size_t)(s->loadouts.data-s->input.data);*out=l;return ES2_OK;
}

/* ---- Serialization: one emitter per source, run twice. A sink without
 * memory only counts, so validation and sizing precede every output byte. */
typedef struct sink { uint8_t *out; size_t at; } sink;
static void put_bytes(sink *k,const uint8_t *p,size_t n) {
    if(k->out && n)memcpy(k->out+k->at,p,n);
    k->at+=n;
}
static void put_word(sink *k,uint16_t x) {
    uint8_t b[2];b[0]=(uint8_t)x;b[1]=(uint8_t)(x>>8);put_bytes(k,b,2);
}
static void put_span(sink *k,es2_span s) { put_bytes(k,s.data,s.size); }
static void put_pilot(sink *k,const es2_sav_pilot *p) {
    put_span(k,p->prefix);put_word(k,(uint16_t)p->name.size);
    put_span(k,p->name);put_span(k,p->suffix);
}
static void put_loadout(sink *k,const es2_sav_loadout *l) {
    put_word(k,(uint16_t)l->index);put_span(k,l->fixed);
    put_word(k,(uint16_t)l->count);put_span(k,l->items);
}

static int overlaps(const void *a,size_t n,const void *b,size_t m) {
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    if(!a || !b || !n || !m)return 0;
    return x<=y?y-x<n:x-y<m;
}
static int span_overlaps(es2_span s,const void *p,size_t n) { return overlaps(s.data,s.size,p,n); }
static int pilot_overlaps(const es2_sav_pilot *q,const void *p,size_t n) {
    return q && (overlaps(q,sizeof *q,p,n) || span_overlaps(q->prefix,p,n) ||
        span_overlaps(q->name,p,n) || span_overlaps(q->suffix,p,n));
}
static int pilots_overlap(const es2_sav_pilot *q,size_t count,const void *p,size_t n) {
    size_t i;
    if(!q)return 0;
    if(overlaps(q,count*sizeof *q,p,n))return 1;
    for(i=0;i<count;i++)if(pilot_overlaps(&q[i],p,n))return 1;
    return 0;
}
static int loadouts_overlap(const es2_sav_loadout *l,size_t count,const void *p,size_t n) {
    size_t i;
    if(!l || count>8)return 0; /* oversized lists are rejected by validation */
    if(overlaps(l,count*sizeof *l,p,n))return 1;
    for(i=0;i<count;i++)if(span_overlaps(l[i].fixed,p,n) || span_overlaps(l[i].items,p,n))return 1;
    return 0;
}
static int edit_overlaps(const es2_sav_edit *d,const void *p,size_t n) {
    return overlaps(d,sizeof *d,p,n) || overlaps(d->variables,d->variables?2000:0,p,n) ||
        overlaps(d->inventory_flags,d->inventory_flags?33:0,p,n) ||
        pilots_overlap(d->roster,36,p,n) ||
        overlaps(d->roster_selection,d->roster_selection?12:0,p,n) ||
        overlaps(d->team,d->team?4:0,p,n) || pilot_overlaps(d->player,p,n) ||
        (d->set_loadouts && loadouts_overlap(d->loadouts,d->loadout_count,p,n)) ||
        overlaps(d->nine_word_bank,d->nine_word_bank?18:0,p,n);
}
static int image_overlaps(const es2_sav_image *m,const void *p,size_t n) {
    size_t i;
    if(overlaps(m,sizeof *m,p,n))return 1;
    for(i=0;i<33;i++)if(span_overlaps(m->inventory[i].items,p,n))return 1;
    return span_overlaps(m->five_entry_bank,p,n) || span_overlaps(m->selection,p,n) ||
        pilots_overlap(m->roster,36,p,n) || span_overlaps(m->roster_selection,p,n) ||
        span_overlaps(m->team,p,n) || pilot_overlaps(m->player,p,n) ||
        loadouts_overlap(m->loadouts,m->loadout_count,p,n) ||
        span_overlaps(m->nine_word_bank,p,n) || span_overlaps(m->tally,p,n) ||
        span_overlaps(m->variables,p,n) || span_overlaps(m->final_fields,p,n) ||
        span_overlaps(m->trailing,p,n);
}

static int sized(es2_span s,size_t n) { return s.size==n && (!n || s.data); }
/* Loader-safe domain of replacement records, see sav.h. */
static es2_status check_pilot(const es2_sav_pilot *p,size_t which,es2_error *e) {
    if(!p)return es2_fail(e,ES2_ERR_ARGUMENT,which);
    if(!sized(p->prefix,4) || !sized(p->suffix,25) || !p->name.data ||
        p->name.size<1 || p->name.size>30)return es2_fail(e,ES2_ERR_LENGTH,which);
    if(p->name.data[p->name.size-1] || memchr(p->name.data,0,p->name.size-1))
        return es2_fail(e,ES2_ERR_STRING,which);
    return ES2_OK;
}
static es2_status check_loadouts(const es2_sav_loadout *l,size_t n,es2_error *e) {
    unsigned bays=0;size_t i,j;
    if(n>8 || (n && !l))return es2_fail(e,ES2_ERR_COUNT,n);
    for(i=0;i<n;i++) {
        unsigned capacity,slots=0;
        if(l[i].index<0 || l[i].index>7 || (bays&(1u<<l[i].index)))return es2_fail(e,ES2_ERR_OFFSET,i);
        bays|=1u<<l[i].index;
        if(!sized(l[i].fixed,76))return es2_fail(e,ES2_ERR_LENGTH,i);
        capacity=le16(l[i].fixed.data+74);
        if(capacity>10 || l[i].count>capacity)return es2_fail(e,ES2_ERR_COUNT,i);
        if(!sized(l[i].items,12*l[i].count))return es2_fail(e,ES2_ERR_LENGTH,i);
        for(j=0;j<l[i].count;j++) {
            const uint8_t *r=l[i].items.data+12*j;unsigned slot=le16(r),item=le16(r+2);
            if(slot>=capacity || (slots&(1u<<slot)) || item>32)return es2_fail(e,ES2_ERR_OFFSET,i);
            slots|=1u<<slot;
        }
    }
    return ES2_OK;
}
static es2_status check_edit(const es2_sav_edit *d,es2_error *e) {
    size_t i;
    if(d->roster)for(i=0;i<36;i++)TRY(check_pilot(&d->roster[i],i,e));
    if(d->player)TRY(check_pilot(d->player,36,e));
    if(d->set_loadouts)TRY(check_loadouts(d->loadouts,d->loadout_count,e));
    return ES2_OK;
}
static es2_status check_image(const es2_sav_image *m,es2_error *e) {
    size_t i,j;
    for(i=0;i<33;i++) {
        const es2_sav_inventory *c=&m->inventory[i];
        if(c->count>32767)return es2_fail(e,ES2_ERR_COUNT,i);
        if(!sized(c->items,10*c->count))return es2_fail(e,ES2_ERR_LENGTH,i);
        for(j=0;j<c->count;j++)if(le16(c->items.data+10*j)!=i)return es2_fail(e,ES2_ERR_OFFSET,i);
    }
    if(!sized(m->five_entry_bank,22) || !sized(m->selection,152) || !sized(m->roster_selection,12) ||
        !sized(m->team,4) || !sized(m->nine_word_bank,18) || !sized(m->tally,4) ||
        !sized(m->variables,2000) || !sized(m->final_fields,22) || !sized(m->trailing,m->trailing.size))
        return es2_fail(e,ES2_ERR_LENGTH,0);
    if(!m->roster)return es2_fail(e,ES2_ERR_ARGUMENT,0);
    for(i=0;i<36;i++)TRY(check_pilot(&m->roster[i],i,e));
    TRY(check_pilot(m->player,36,e));
    return check_loadouts(m->loadouts,m->loadout_count,e);
}

/* Streams the writer image of a verified SAV with replacements: the bytes
 * VSHELL's save writer 0x40e933 emits, never a tail. Reads cannot fail
 * after parsing. */
static es2_status emit_save(sink *k,const es2_sav *s,const es2_sav_edit *d,es2_error *e) {
    size_t i;es2_reader r;
    for(i=0;i<33;i++) {
        uint8_t flag=d && d->inventory_flags?d->inventory_flags[i]:s->inventory[i].flag;
        put_bytes(k,&flag,1);put_word(k,(uint16_t)s->inventory[i].count);put_span(k,s->inventory[i].items);
    }
    put_span(k,s->five_entry_bank);
    if(d && d->set_position) {
        put_word(k,(uint16_t)d->group);put_word(k,(uint16_t)d->position);
        put_span(k,(es2_span){s->selection.data+4,s->selection.size-4});
    } else put_span(k,s->selection);
    TRY(es2_reader_init(&r,s->roster,e));
    for(i=0;i<36;i++) {
        es2_sav_pilot a;TRY(pilot(&r,&a));
        put_pilot(k,d && d->roster?&d->roster[i]:&a);
    }
    if(d && d->roster_selection)for(i=0;i<6;i++)put_word(k,d->roster_selection[i]);
    else put_span(k,s->roster_selection);
    if(d && d->team)for(i=0;i<2;i++)put_word(k,(uint16_t)d->team[i]);
    else put_span(k,s->team);
    put_pilot(k,d && d->player?d->player:&s->player);
    if(d && d->set_loadouts) {
        put_word(k,(uint16_t)d->loadout_count);
        for(i=0;i<d->loadout_count;i++)put_loadout(k,&d->loadouts[i]);
    } else {
        put_word(k,(uint16_t)s->loadout_count);TRY(es2_reader_init(&r,s->loadouts,e));
        for(i=0;i<s->loadout_count;i++){es2_sav_loadout a;TRY(loadout(&r,&a));put_loadout(k,&a);}
    }
    if(d && d->nine_word_bank)for(i=0;i<9;i++)put_word(k,(uint16_t)d->nine_word_bank[i]);
    else put_span(k,s->nine_word_bank);
    put_span(k,s->tally);
    if(d && d->variables)for(i=0;i<1000;i++)put_word(k,(uint16_t)d->variables[i]);
    else put_span(k,s->variables);
    put_span(k,s->final_fields);
    return ES2_OK;
}
static void emit_image(sink *k,const es2_sav_image *m) {
    size_t i;
    for(i=0;i<33;i++) {
        put_bytes(k,&m->inventory[i].flag,1);put_word(k,(uint16_t)m->inventory[i].count);
        put_span(k,m->inventory[i].items);
    }
    put_span(k,m->five_entry_bank);put_span(k,m->selection);
    for(i=0;i<36;i++)put_pilot(k,&m->roster[i]);
    put_span(k,m->roster_selection);put_span(k,m->team);put_pilot(k,m->player);
    put_word(k,(uint16_t)m->loadout_count);
    for(i=0;i<m->loadout_count;i++)put_loadout(k,&m->loadouts[i]);
    put_span(k,m->nine_word_bank);put_span(k,m->tally);put_span(k,m->variables);
    put_span(k,m->final_fields);put_span(k,m->trailing);
}

static int span_valid(es2_span s) { return s.data || !s.size; }
/* Bytes of previous beyond n: what a non-truncating rewrite of n bytes keeps. */
static es2_span stale_tail(es2_span previous,size_t n) {
    es2_span t={NULL,0};
    if(previous.size>n){t.data=previous.data+n;t.size=previous.size-n;}
    return t;
}
/* previous==NULL: the image is followed by save->trailing (es2_sav_write);
 * otherwise by the bytes of *previous beyond the image (es2_sav_write_over). */
static es2_status write_save(const es2_sav *s,const es2_sav_edit *edit,const es2_span *previous,
    uint8_t *out,size_t capacity,size_t *required,es2_error *e) {
    es2_sav verified;es2_span tail;sink k={NULL,0};size_t body;
    if(!s || !required || (!out && capacity) || (previous && !span_valid(*previous)))
        return es2_fail(e,ES2_ERR_ARGUMENT,0);
    if(overlaps(required,sizeof *required,s->input.data,s->input.size) ||
        overlaps(required,sizeof *required,s,sizeof *s) ||
        (previous && span_overlaps(*previous,required,sizeof *required)) ||
        (edit && edit_overlaps(edit,required,sizeof *required)))
        return es2_fail(e,ES2_ERR_OVERLAP,0);
    TRY(es2_sav_parse(s->input,&verified,e));
    if(edit)TRY(check_edit(edit,e));
    if(out && (overlaps(out,capacity,s->input.data,s->input.size) ||
        overlaps(out,capacity,s,sizeof *s) || overlaps(out,capacity,required,sizeof *required) ||
        (previous && span_overlaps(*previous,out,capacity)) ||
        (edit && edit_overlaps(edit,out,capacity))))
        return es2_fail(e,ES2_ERR_OVERLAP,0);
    TRY(emit_save(&k,&verified,edit,e));body=k.at;
    tail=previous?stale_tail(*previous,body):verified.trailing;
    *required=body+tail.size;
    if(!out){es2_error_clear(e);return ES2_OK;}
    if(capacity<*required)return es2_fail(e,ES2_ERR_CAPACITY,0);
    k.out=out;k.at=0;TRY(emit_save(&k,&verified,edit,e));put_span(&k,tail);
    es2_error_clear(e);return ES2_OK;
}
es2_status es2_sav_write(const es2_sav *s,const es2_sav_edit *edit,
    uint8_t *out,size_t capacity,size_t *required,es2_error *e) {
    return write_save(s,edit,NULL,out,capacity,required,e);
}
es2_status es2_sav_write_over(const es2_sav *s,const es2_sav_edit *edit,es2_span previous,
    uint8_t *out,size_t capacity,size_t *required,es2_error *e) {
    return write_save(s,edit,&previous,out,capacity,required,e);
}
es2_status es2_sav_overlay(es2_span image,es2_span previous,uint8_t *out,size_t capacity,
    size_t *required,es2_error *e) {
    es2_span tail;
    if(!required || (!out && capacity) || !span_valid(image) || !span_valid(previous))
        return es2_fail(e,ES2_ERR_ARGUMENT,0);
    if(span_overlaps(image,required,sizeof *required) || span_overlaps(previous,required,sizeof *required) ||
        (out && (span_overlaps(image,out,capacity) || span_overlaps(previous,out,capacity) ||
        overlaps(out,capacity,required,sizeof *required))))
        return es2_fail(e,ES2_ERR_OVERLAP,0);
    tail=stale_tail(previous,image.size);
    *required=image.size+tail.size;
    if(!out){es2_error_clear(e);return ES2_OK;}
    if(capacity<*required)return es2_fail(e,ES2_ERR_CAPACITY,0);
    if(image.size)memcpy(out,image.data,image.size);
    if(tail.size)memcpy(out+image.size,tail.data,tail.size);
    es2_error_clear(e);return ES2_OK;
}
es2_status es2_sav_compose(const es2_sav_image *m,uint8_t *out,size_t capacity,
    size_t *required,es2_error *e) {
    sink k={NULL,0};
    if(!m || !required || (!out && capacity))return es2_fail(e,ES2_ERR_ARGUMENT,0);
    if(image_overlaps(m,required,sizeof *required))return es2_fail(e,ES2_ERR_OVERLAP,0);
    TRY(check_image(m,e));
    if(out && (image_overlaps(m,out,capacity) || overlaps(out,capacity,required,sizeof *required)))
        return es2_fail(e,ES2_ERR_OVERLAP,0);
    emit_image(&k,m);*required=k.at;
    if(!out){es2_error_clear(e);return ES2_OK;}
    if(capacity<*required)return es2_fail(e,ES2_ERR_CAPACITY,0);
    k.out=out;k.at=0;emit_image(&k,m);
    es2_error_clear(e);return ES2_OK;
}
