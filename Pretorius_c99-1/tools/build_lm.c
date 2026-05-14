/* build_lm.c — offline n-gram LM builder for the Pretorius engine.
 *
 *   build_lm <corpus.txt> <order> <output.lm>
 *
 * Reads UTF-8 (we treat as bytes), normalizes to lowercase ASCII,
 * walks the text, counts n-grams up to <order>, writes binary LM file
 * consumed by ngram_lm_load().
 *
 * Hash table: open-addressing linear probing on FNV-1a.
 * Output: sorted-by-hash arrays per order, for binary search at runtime.
 */
#include "ngram_lm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define MAX_ORDER PE_LM_MAX_ORDER

typedef struct { uint64_t hash; uint32_t count; } Entry;

/* Open-addressing hash table for accumulation. */
typedef struct {
    Entry *slots;
    uint32_t cap;
    uint32_t used;
} HTable;

static void ht_init(HTable *h, uint32_t initial_cap){
    h->cap = initial_cap;
    h->used = 0;
    h->slots = (Entry*)calloc(initial_cap, sizeof(Entry));
}

static void ht_grow(HTable *h){
    uint32_t old_cap = h->cap;
    Entry *old = h->slots;
    uint32_t i;
    h->cap *= 2;
    h->slots = (Entry*)calloc(h->cap, sizeof(Entry));
    h->used = 0;
    for (i = 0; i < old_cap; i++){
        if (old[i].count){
            uint64_t k = old[i].hash;
            uint32_t idx = (uint32_t)(k % h->cap);
            while (h->slots[idx].count){
                if (h->slots[idx].hash == k){ break; }
                idx = (idx + 1) % h->cap;
            }
            h->slots[idx].hash = k;
            h->slots[idx].count = old[i].count;
            h->used++;
        }
    }
    free(old);
}

static void ht_bump(HTable *h, uint64_t key){
    uint32_t idx;
    if (h->used * 2 >= h->cap) ht_grow(h);
    idx = (uint32_t)(key % h->cap);
    while (h->slots[idx].count){
        if (h->slots[idx].hash == key){ h->slots[idx].count++; return; }
        idx = (idx + 1) % h->cap;
    }
    h->slots[idx].hash = key;
    h->slots[idx].count = 1;
    h->used++;
}

static uint64_t fnv1a(const uint8_t *ids, int n){
    uint64_t h = 0xCBF29CE484222325ULL;
    int i;
    for (i = 0; i < n; i++){
        h ^= (uint64_t)ids[i];
        h *= 0x100000001B3ULL;
    }
    return h;
}

static int cmp_entry(const void *a, const void *b){
    uint64_t ha = ((const Entry*)a)->hash;
    uint64_t hb = ((const Entry*)b)->hash;
    if (ha < hb) return -1;
    if (ha > hb) return  1;
    return 0;
}

/* Compact: discard empty slots, sort, return new array + count. */
static Entry *compact_and_sort(HTable *h, uint32_t *out_count){
    Entry *arr = (Entry*)malloc(sizeof(Entry) * h->used);
    uint32_t n = 0, i;
    for (i = 0; i < h->cap; i++){
        if (h->slots[i].count) arr[n++] = h->slots[i];
    }
    qsort(arr, n, sizeof(Entry), cmp_entry);
    *out_count = n;
    return arr;
}

int main(int argc, char **argv){
    FILE *fin, *fout;
    long fsz;
    char *corpus;
    int order;
    int n, i;
    uint8_t alphabet[256];
    int vocab_n = 0;
    HTable tables[MAX_ORDER + 1];
    uint8_t window[MAX_ORDER];
    int filled = 0;
    uint64_t total_unigram = 0;
    uint32_t magic = PE_LM_MAGIC;
    uint16_t version = PE_LM_VERSION;
    uint8_t  order_b, reserved = 0;

    if (argc != 4){
        fprintf(stderr, "usage: %s <corpus.txt> <order:2..5> <out.lm>\n", argv[0]);
        return 1;
    }
    order = atoi(argv[2]);
    if (order < 2 || order > MAX_ORDER){
        fprintf(stderr, "order must be 2..%d\n", MAX_ORDER); return 1;
    }

    fin = fopen(argv[1], "rb");
    if (!fin){ perror(argv[1]); return 1; }
    fseek(fin, 0, SEEK_END); fsz = ftell(fin); fseek(fin, 0, SEEK_SET);
    corpus = (char*)malloc((size_t)fsz);
    if (!corpus){ fclose(fin); return 1; }
    if (fread(corpus, 1, (size_t)fsz, fin) != (size_t)fsz){
        fprintf(stderr, "short read\n"); free(corpus); fclose(fin); return 1;
    }
    fclose(fin);

    /* Build alphabet: in-vocabulary = lowercase a-z, 0-9, space, basic punct. */
    for (i = 0; i < 256; i++) alphabet[i] = 0xFF;
    {
        const char *vocab = "abcdefghijklmnopqrstuvwxyz0123456789 .,;:!?'-\"";
        const char *p;
        for (p = vocab; *p; p++) alphabet[(unsigned char)*p] = (uint8_t)vocab_n++;
        /* Map uppercase to lowercase IDs so input case doesn't matter. */
        for (i = 'A'; i <= 'Z'; i++) alphabet[i] = alphabet[i - 'A' + 'a'];
        /* Map newline/tab/CR to space. */
        alphabet[(unsigned char)'\n'] = alphabet[(unsigned char)' '];
        alphabet[(unsigned char)'\r'] = alphabet[(unsigned char)' '];
        alphabet[(unsigned char)'\t'] = alphabet[(unsigned char)' '];
    }

    for (n = 1; n <= order; n++) ht_init(&tables[n], 4096);

    /* Walk corpus, count n-grams. */
    for (i = 0; i < (int)fsz; i++){
        unsigned char c = (unsigned char)corpus[i];
        uint8_t id = alphabet[c];
        int k;
        if (id == 0xFF){ filled = 0; continue; }
        if (filled < order) window[filled++] = id;
        else {
            memmove(window, window + 1, (size_t)(order - 1));
            window[order - 1] = id;
        }
        /* Count all suffix n-grams ending at this position. */
        for (k = 1; k <= filled; k++){
            uint64_t h = fnv1a(window + (filled - k), k);
            ht_bump(&tables[k], h);
            if (k == 1) total_unigram++;
        }
    }
    free(corpus);

    /* Write output. */
    fout = fopen(argv[3], "wb");
    if (!fout){ perror(argv[3]); return 1; }
    fwrite(&magic, 4, 1, fout);
    fwrite(&version, 2, 1, fout);
    order_b = (uint8_t)order;
    fwrite(&order_b, 1, 1, fout);
    fwrite(&reserved, 1, 1, fout);
    fwrite(alphabet, 256, 1, fout);

    for (n = 1; n <= order; n++){
        uint32_t cnt;
        Entry *arr = compact_and_sort(&tables[n], &cnt);
        fwrite(&cnt, 4, 1, fout);
        if (cnt) fwrite(arr, sizeof(Entry), cnt, fout);
        free(arr);
        free(tables[n].slots);
    }
    fwrite(&total_unigram, 8, 1, fout);
    fclose(fout);

    /* Final byte size for log. */
    {
        FILE *f2 = fopen(argv[3], "rb");
        if (f2){
            fseek(f2, 0, SEEK_END);
            fprintf(stderr, "wrote %s (%ld bytes, order %d, %llu unigrams)\n",
                    argv[3], ftell(f2), order, (unsigned long long)total_unigram);
            fclose(f2);
        }
    }
    return 0;
}
