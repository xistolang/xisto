/* exceljs (stream.xlsx.WorkbookWriter): the ZIP container of an .xlsx, written as a stream in constant memory.
   - Entries are deflated as they are written (LZ77 over a 32 KB window, fixed Huffman codes: RFC 1951) and closed
     with a data descriptor (general purpose bit 3), so nothing of an entry is kept once it is out: the memory is
     the window, the hash chains and the bytes not yet handed to the program (take()) or the file.
   - Rows are written straight into the deflater from the program's values (row()): no XML string per row.
   - One entry at a time, as a ZIP stream must be; ZIP32 only (an entry or the archive past 4 GiB is refused). */

typedef struct xn_exceljs_NativeZip {
    xs_hdr hdr;
    /* output: pending bytes for take(), or a file */
    uint8_t *out; size_t out_len, out_cap;
    FILE *file;
    uint64_t written;          /* bytes of the archive so far (offsets of the local headers) */
    /* bit writer of the current entry */
    uint64_t bitbuf; int bitcnt;
    /* deflate state: buf holds the history (up to 32 KB) followed by the input not yet compressed */
    uint8_t *buf; size_t buf_len;   /* buf_len: history + pending input */
    size_t hist;                    /* bytes of history at the front of buf */
    int64_t base;                   /* absolute position of buf[0] in the entry's data */
    int64_t *head;                  /* hash of 3 bytes -> last absolute position (-1) */
    int64_t *prev;                  /* absolute position & 32767 -> the previous position with the same hash */
    /* current entry */
    bool open;
    char *name;
    uint32_t crc;
    uint64_t usize, csize;
    uint64_t local_off;
    /* central directory, built as entries close */
    uint8_t *cd; size_t cd_len, cd_cap;
    int entries;
    uint16_t dos_time, dos_date;
} xn_exceljs_NativeZip;

#define XN_XZ_WIN 32768
#define XN_XZ_BLOCK 65536
#define XN_XZ_HBITS 15
#define XN_XZ_CHAIN 48
#define XN_XZ_NICE 128

static uint32_t xn_xz_crc_table[256];
static void xn_xz_crc_init(void) {
    if (xn_xz_crc_table[1]) return;
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        xn_xz_crc_table[i] = c;
    }
}
static uint32_t xn_xz_crc(uint32_t crc, const uint8_t *p, size_t n) {
    crc = ~crc;
    for (size_t i = 0; i < n; i++) crc = xn_xz_crc_table[(crc ^ p[i]) & 0xff] ^ (crc >> 8);
    return ~crc;
}

static void xn_xz_free(void *p) {
    xn_exceljs_NativeZip *z = p;
    if (z->file) fclose(z->file);
    free(z->out); free(z->buf); free(z->head); free(z->prev); free(z->name); free(z->cd);
    xs_obj_free(p);
}
static void xn_xz_print(void *p) { (void)p; fputs("NativeZip {}", stdout); }

static void xn_xz_emit(xn_exceljs_NativeZip *z, const void *p, size_t n) {
    z->written += n;
    if (z->file) {
        if (fwrite(p, 1, n, z->file) != n) xs_panic("exceljs: cannot write the file");
        return;
    }
    if (z->out_len + n > z->out_cap) {
        size_t cap = z->out_cap ? z->out_cap : 65536;
        while (cap < z->out_len + n) cap *= 2;
        z->out = realloc(z->out, cap);
        if (!z->out) xs_panic("out of memory");
        z->out_cap = cap;
    }
    memcpy(z->out + z->out_len, p, n);
    z->out_len += n;
}
static void xn_xz_le16(uint8_t *p, unsigned v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void xn_xz_le32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }

/* ---- bit writer (LSB first) ---- */
static void xn_xz_bits(xn_exceljs_NativeZip *z, uint32_t v, int n) {
    z->bitbuf |= (uint64_t)v << z->bitcnt;
    z->bitcnt += n;
    while (z->bitcnt >= 8) {
        uint8_t b = (uint8_t)z->bitbuf;
        xn_xz_emit(z, &b, 1);
        z->csize++;
        z->bitbuf >>= 8;
        z->bitcnt -= 8;
    }
}
static uint32_t xn_xz_rev(uint32_t v, int n) { uint32_t r = 0; for (int i = 0; i < n; i++) { r = (r << 1) | (v & 1); v >>= 1; } return r; }
/* a literal/length symbol with the fixed code */
static void xn_xz_sym(xn_exceljs_NativeZip *z, int s) {
    if (s < 144) xn_xz_bits(z, xn_xz_rev(0x30 + (uint32_t)s, 8), 8);
    else if (s < 256) xn_xz_bits(z, xn_xz_rev(0x190 + (uint32_t)(s - 144), 9), 9);
    else if (s < 280) xn_xz_bits(z, xn_xz_rev((uint32_t)(s - 256), 7), 7);
    else xn_xz_bits(z, xn_xz_rev(0xC0 + (uint32_t)(s - 280), 8), 8);
}
static const uint16_t xn_xz_lbase[29] = {3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
static const uint8_t xn_xz_lext[29] = {0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
static const uint16_t xn_xz_dbase[30] = {1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};
static const uint8_t xn_xz_dext[30] = {0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};
static void xn_xz_match(xn_exceljs_NativeZip *z, int len, int dist) {
    int li = 28;
    while (li > 0 && xn_xz_lbase[li] > len) li--;
    xn_xz_sym(z, 257 + li);
    if (xn_xz_lext[li]) xn_xz_bits(z, (uint32_t)(len - xn_xz_lbase[li]), xn_xz_lext[li]);
    int di = 29;
    while (di > 0 && xn_xz_dbase[di] > dist) di--;
    xn_xz_bits(z, xn_xz_rev((uint32_t)di, 5), 5);
    if (xn_xz_dext[di]) xn_xz_bits(z, (uint32_t)(dist - xn_xz_dbase[di]), xn_xz_dext[di]);
}

static uint32_t xn_xz_hash(const uint8_t *p) { return ((uint32_t)p[0] << 10 ^ (uint32_t)p[1] << 5 ^ p[2]) & ((1u << XN_XZ_HBITS) - 1); }
static void xn_xz_insert(xn_exceljs_NativeZip *z, size_t i) {
    uint32_t h = xn_xz_hash(z->buf + i);
    int64_t pos = z->base + (int64_t)i;
    z->prev[pos & (XN_XZ_WIN - 1)] = z->head[h];
    z->head[h] = pos;
}

/* compress buf[hist .. hist+n) as one fixed-Huffman block (not the last), matches reaching into the history */
static void xn_xz_block(xn_exceljs_NativeZip *z, size_t n) {
    if (n == 0) return;
    xn_xz_bits(z, 0, 1);  /* BFINAL = 0 */
    xn_xz_bits(z, 1, 2);  /* BTYPE = 01, fixed codes */
    size_t end = z->hist + n, i = z->hist;
    while (i < end) {
        int best = 0, bdist = 0;
        if (i + 3 <= end) {
            uint32_t h = xn_xz_hash(z->buf + i);
            int64_t pos = z->base + (int64_t)i;
            int64_t cand = z->head[h];
            int chain = XN_XZ_CHAIN;
            size_t maxlen = end - i < 258 ? end - i : 258;
            while (cand >= 0 && chain-- > 0) {
                int64_t d = pos - cand;
                if (d <= 0 || d > XN_XZ_WIN - 1 || cand < z->base) break;
                const uint8_t *a = z->buf + i, *b = z->buf + (size_t)(cand - z->base);
                if (b[best] == a[best]) {
                    size_t l = 0;
                    while (l < maxlen && a[l] == b[l]) l++;
                    if ((int)l > best) { best = (int)l; bdist = (int)d; if (l >= XN_XZ_NICE || l == maxlen) break; }
                }
                int64_t nx = z->prev[cand & (XN_XZ_WIN - 1)];
                if (nx >= cand) break;
                cand = nx;
            }
        }
        if (best >= 3) {
            xn_xz_match(z, best, bdist);
            for (int k = 0; k < best; k++) { if (i + k + 3 <= end) xn_xz_insert(z, i + (size_t)k); }
            i += (size_t)best;
        } else {
            xn_xz_sym(z, z->buf[i]);
            if (i + 3 <= end) xn_xz_insert(z, i);
            i++;
        }
    }
    xn_xz_sym(z, 256);  /* end of block */
    /* keep the last 32 KB as history */
    size_t keep = end < XN_XZ_WIN ? end : XN_XZ_WIN;
    memmove(z->buf, z->buf + end - keep, keep);
    z->base += (int64_t)(end - keep);
    z->hist = keep;
    z->buf_len = keep;
}

static void xn_xz_feed(xn_exceljs_NativeZip *z, const uint8_t *p, size_t n) {
    if (!z->open) xs_panic("exceljs: no entry is open");
    z->crc = xn_xz_crc(z->crc, p, n);
    z->usize += n;
    while (n > 0) {
        size_t room = z->hist + XN_XZ_BLOCK - z->buf_len;
        size_t k = n < room ? n : room;
        memcpy(z->buf + z->buf_len, p, k);
        z->buf_len += k; p += k; n -= k;
        if (z->buf_len - z->hist == XN_XZ_BLOCK) xn_xz_block(z, XN_XZ_BLOCK);
    }
}

static void xn_xz_dos_now(xn_exceljs_NativeZip *z) {
    time_t t = time(NULL);
    struct tm tm;
    localtime_r(&t, &tm);
    z->dos_time = (uint16_t)((tm.tm_hour << 11) | (tm.tm_min << 5) | (tm.tm_sec / 2));
    z->dos_date = (uint16_t)(((tm.tm_year - 80) << 9) | ((tm.tm_mon + 1) << 5) | tm.tm_mday);
}

/* new NativeZip(path): path "" keeps the bytes for take(); otherwise they go to the file */
XS_NOINLINE xn_exceljs_NativeZip *xn_exceljs_NativeZip_new(xs_str *path) {
    xn_xz_crc_init();
    xn_exceljs_NativeZip *z = xs_obj_alloc(sizeof *z, xn_xz_free, xn_xz_print);
    memset((char *)z + sizeof(xs_hdr), 0, sizeof *z - sizeof(xs_hdr));
    if (path && path->len > 0) {
        char *p = xs_strndup_(path->data, (size_t)path->len);
        z->file = fopen(p, "wb");
        free(p);
        if (!z->file) xs_panic("exceljs: cannot open the file for writing");
    }
    z->buf = malloc(XN_XZ_WIN + XN_XZ_BLOCK);
    z->head = malloc(sizeof(int64_t) << XN_XZ_HBITS);
    z->prev = malloc(sizeof(int64_t) * XN_XZ_WIN);
    if (!z->buf || !z->head || !z->prev) xs_panic("out of memory");
    xn_xz_dos_now(z);
    return z;
}

XS_NOINLINE void xn_exceljs_NativeZip_open(xn_exceljs_NativeZip *z, xs_str *name) {
    if (z->open) xs_panic("exceljs: an entry is already open");
    free(z->name);
    z->name = xs_strndup_(name->data, (size_t)name->len);
    z->open = true;
    z->crc = 0; z->usize = 0; z->csize = 0; z->bitbuf = 0; z->bitcnt = 0;
    z->buf_len = 0; z->hist = 0; z->base = 0;
    memset(z->head, 0xff, sizeof(int64_t) << XN_XZ_HBITS);
    z->local_off = z->written;
    size_t nl = strlen(z->name);
    uint8_t h[30];
    xn_xz_le32(h, 0x04034b50);
    xn_xz_le16(h + 4, 20);
    xn_xz_le16(h + 6, 0x0808);           /* data descriptor, UTF-8 name */
    xn_xz_le16(h + 8, 8);                /* deflate */
    xn_xz_le16(h + 10, z->dos_time);
    xn_xz_le16(h + 12, z->dos_date);
    xn_xz_le32(h + 14, 0); xn_xz_le32(h + 18, 0); xn_xz_le32(h + 22, 0);
    xn_xz_le16(h + 26, (unsigned)nl);
    xn_xz_le16(h + 28, 0);
    xn_xz_emit(z, h, 30);
    xn_xz_emit(z, z->name, nl);
}

XS_NOINLINE void xn_exceljs_NativeZip_text(xn_exceljs_NativeZip *z, xs_str *s) {
    xn_xz_feed(z, (const uint8_t *)s->data, (size_t)s->len);
}

/* ---- rows: values straight into the deflater ---- */
static void xn_xz_put(xn_exceljs_NativeZip *z, const char *s, size_t n) { xn_xz_feed(z, (const uint8_t *)s, n); }
static void xn_xz_puts(xn_exceljs_NativeZip *z, const char *s) { xn_xz_put(z, s, strlen(s)); }
static void xn_xz_col(char *o, int64_t c) { /* 0 -> A, 25 -> Z, 26 -> AA */
    char t[8]; int n = 0;
    c += 1;
    while (c > 0) { int64_t r = (c - 1) % 26; t[n++] = (char)('A' + r); c = (c - 1) / 26; }
    for (int i = 0; i < n; i++) o[i] = t[n - 1 - i];
    o[n] = 0;
}
static void xn_xz_escape(xn_exceljs_NativeZip *z, const char *s, size_t n) {
    size_t from = 0;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        const char *r = NULL;
        if (c == '&') r = "&amp;"; else if (c == '<') r = "&lt;"; else if (c == '>') r = "&gt;"; else if (c == '"') r = "&quot;";
        else if (c < 0x20 && c != '\t' && c != '\n' && c != '\r') r = "";   /* not allowed in XML 1.0 */
        if (r) { xn_xz_put(z, s + from, i - from); xn_xz_puts(z, r); from = i + 1; }
    }
    xn_xz_put(z, s + from, n - from);
}
static void xn_xz_num(char *o, double d) {
    if (d == (double)(int64_t)d && d > -1e15 && d < 1e15) { snprintf(o, 40, "%lld", (long long)d); return; }
    snprintf(o, 40, "%.17g", d);
    /* the shortest text that reads back as d */
    for (int prec = 1; prec <= 17; prec++) {
        char t[40];
        snprintf(t, sizeof t, "%.*g", prec, d);
        if (strtod(t, NULL) == d) { strcpy(o, t); return; }
    }
}

/* row(r, values, styles, dateStyle): <row r="r"> with a cell per value; null leaves the cell out. styles[i] is the
   style of cell i (0: none); a Date without a style gets dateStyle. A Date is written as its Excel serial number
   (days since 1899-12-30, UTC), as exceljs does. */
XS_NOINLINE void xn_exceljs_NativeZip_row(xn_exceljs_NativeZip *z, int64_t r, xs_arr_ref *values, xs_arr_int *styles, int64_t date_style, int64_t row_style) {
    char tmp[96];
    if (row_style > 0) snprintf(tmp, sizeof tmp, "<row r=\"%lld\" s=\"%lld\" customFormat=\"1\">", (long long)r, (long long)row_style);
    else snprintf(tmp, sizeof tmp, "<row r=\"%lld\">", (long long)r);
    xn_xz_puts(z, tmp);
    int64_t n = values ? values->len : 0;
    for (int64_t i = 0; i < n; i++) {
        void *v = values->data[i];
        int64_t st = styles && i < styles->len ? styles->data[i] : 0;
        if (st <= 0) st = row_style;
        char ref[16];
        xn_xz_col(ref, i);
        if (!v) {
            if (st > 0) { snprintf(tmp, sizeof tmp, "<c r=\"%s%lld\" s=\"%lld\"/>", ref, (long long)r, (long long)st); xn_xz_puts(z, tmp); }
            continue;
        }
        void (*f)(void *) = xs_type_of(v)->free;
        if (f == xs_Date_free && st <= 0) st = date_style;
        char sattr[24] = "";
        if (st > 0) snprintf(sattr, sizeof sattr, " s=\"%lld\"", (long long)st);
        if (f == xs_str_free) {
            xs_str *s = v;
            snprintf(tmp, sizeof tmp, "<c r=\"%s%lld\"%s t=\"inlineStr\"><is><t xml:space=\"preserve\">", ref, (long long)r, sattr);
            xn_xz_puts(z, tmp);
            xn_xz_escape(z, s->data, (size_t)s->len);
            xn_xz_puts(z, "</t></is></c>");
        } else if (f == xs_box_bool_free) {
            snprintf(tmp, sizeof tmp, "<c r=\"%s%lld\"%s t=\"b\"><v>%d</v></c>", ref, (long long)r, sattr, ((xs_box_i *)v)->v ? 1 : 0);
            xn_xz_puts(z, tmp);
        } else {
            double d;
            if (f == xs_box_int_free) d = (double)((xs_box_i *)v)->v;
            else if (f == xs_box_num_free) d = ((xs_box_d *)v)->v;
            else if (f == xs_Date_free) d = ((xs_Date *)v)->t / 86400000.0 + 25569.0;
            else xs_panic("exceljs: a cell value must be a string, number, boolean, Date or null");
            if (d != d) { if (st > 0) { snprintf(tmp, sizeof tmp, "<c r=\"%s%lld\"%s/>", ref, (long long)r, sattr); xn_xz_puts(z, tmp); } continue; }
            char num[40];
            xn_xz_num(num, d);
            snprintf(tmp, sizeof tmp, "<c r=\"%s%lld\"%s><v>%s</v></c>", ref, (long long)r, sattr, num);
            xn_xz_puts(z, tmp);
        }
    }
    xn_xz_puts(z, "</row>");
}

XS_NOINLINE void xn_exceljs_NativeZip_close(xn_exceljs_NativeZip *z) {
    if (!z->open) xs_panic("exceljs: no entry is open");
    xn_xz_block(z, z->buf_len - z->hist);
    /* the last block: an empty fixed block with BFINAL set, then to a byte */
    xn_xz_bits(z, 1, 1);
    xn_xz_bits(z, 1, 2);
    xn_xz_sym(z, 256);
    if (z->bitcnt > 0) xn_xz_bits(z, 0, 8 - z->bitcnt);
    if (z->usize > 0xffffffffu || z->csize > 0xffffffffu || z->written > 0xffffffffu) xs_panic("exceljs: the workbook is past 4 GiB (ZIP64 is not supported)");
    uint8_t d[16];
    xn_xz_le32(d, 0x08074b50);
    xn_xz_le32(d + 4, z->crc);
    xn_xz_le32(d + 8, (uint32_t)z->csize);
    xn_xz_le32(d + 12, (uint32_t)z->usize);
    xn_xz_emit(z, d, 16);
    size_t nl = strlen(z->name);
    if (z->cd_len + 46 + nl > z->cd_cap) {
        size_t cap = z->cd_cap ? z->cd_cap * 2 : 4096;
        while (cap < z->cd_len + 46 + nl) cap *= 2;
        z->cd = realloc(z->cd, cap);
        if (!z->cd) xs_panic("out of memory");
        z->cd_cap = cap;
    }
    uint8_t *c = z->cd + z->cd_len;
    xn_xz_le32(c, 0x02014b50);
    xn_xz_le16(c + 4, 20); xn_xz_le16(c + 6, 20);
    xn_xz_le16(c + 8, 0x0808); xn_xz_le16(c + 10, 8);
    xn_xz_le16(c + 12, z->dos_time); xn_xz_le16(c + 14, z->dos_date);
    xn_xz_le32(c + 16, z->crc); xn_xz_le32(c + 20, (uint32_t)z->csize); xn_xz_le32(c + 24, (uint32_t)z->usize);
    xn_xz_le16(c + 28, (unsigned)nl); xn_xz_le16(c + 30, 0); xn_xz_le16(c + 32, 0);
    xn_xz_le16(c + 34, 0); xn_xz_le16(c + 36, 0); xn_xz_le32(c + 38, 0);
    xn_xz_le32(c + 42, (uint32_t)z->local_off);
    memcpy(c + 46, z->name, nl);
    z->cd_len += 46 + nl;
    z->entries++;
    z->open = false;
}

XS_NOINLINE void xn_exceljs_NativeZip_finish(xn_exceljs_NativeZip *z) {
    if (z->open) xs_panic("exceljs: an entry is still open");
    uint64_t off = z->written;
    xn_xz_emit(z, z->cd, z->cd_len);
    uint8_t e[22];
    xn_xz_le32(e, 0x06054b50);
    xn_xz_le16(e + 4, 0); xn_xz_le16(e + 6, 0);
    xn_xz_le16(e + 8, (unsigned)z->entries); xn_xz_le16(e + 10, (unsigned)z->entries);
    xn_xz_le32(e + 12, (uint32_t)z->cd_len); xn_xz_le32(e + 16, (uint32_t)off);
    xn_xz_le16(e + 20, 0);
    xn_xz_emit(z, e, 22);
    if (z->file) { if (fclose(z->file) != 0) xs_panic("exceljs: cannot write the file"); z->file = NULL; }
}

/* the bytes written since the last take() (empty when writing to a file) */
XS_NOINLINE xs_Uint8Array *xn_exceljs_NativeZip_take(xn_exceljs_NativeZip *z) {
    xs_Uint8Array *b = xs_u8_raw((int64_t)z->out_len, false, false);
    if (z->out_len) memcpy(b->data, z->out, z->out_len);
    z->out_len = 0;
    if (z->out_cap > (1u << 20)) { free(z->out); z->out = NULL; z->out_cap = 0; }
    return b;
}
XS_NOINLINE int64_t xn_exceljs_NativeZip_pending(xn_exceljs_NativeZip *z) { return (int64_t)z->out_len; }
