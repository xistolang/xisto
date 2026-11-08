#include "xisto.h"
#include <ctype.h>

static const struct { const char *word; TokKind kind; } keywords[] = {
    {"function", TK_FUNCTION}, {"let", TK_LET}, {"const", TK_CONST},
    {"if", TK_IF}, {"else", TK_ELSE}, {"while", TK_WHILE}, {"for", TK_FOR},
    {"return", TK_RETURN}, {"true", TK_TRUE}, {"false", TK_FALSE}, {"new", TK_NEW},
    {"class", TK_CLASS}, {"this", TK_THIS}, {"async", TK_ASYNC}, {"await", TK_AWAIT},
    {"import", TK_IMPORT}, {"export", TK_EXPORT},
    {"switch", TK_SWITCH}, {"case", TK_CASE}, {"default", TK_DEFAULT}, {"break", TK_BREAK}, {"continue", TK_CONTINUE},
    {"enum", TK_ENUM}, {"try", TK_TRY}, {"catch", TK_CATCH}, {"throw", TK_THROW},
};

const char *tok_kind_name(TokKind k) {
    switch (k) {
    case TK_EOF: return "end of file";
    case TK_IDENT: return "identifier";
    case TK_NUMBER: return "number";
    case TK_STRING: return "string";
    case TK_FUNCTION: return "'function'";
    case TK_LET: return "'let'";
    case TK_CONST: return "'const'";
    case TK_IF: return "'if'";
    case TK_ELSE: return "'else'";
    case TK_WHILE: return "'while'";
    case TK_FOR: return "'for'";
    case TK_RETURN: return "'return'";
    case TK_TRUE: return "'true'";
    case TK_FALSE: return "'false'";
    case TK_NEW: return "'new'";
    case TK_CLASS: return "'class'";
    case TK_THIS: return "'this'";
    case TK_ASYNC: return "'async'";
    case TK_AWAIT: return "'await'";
    case TK_ARROW: return "'=>'";
    case TK_IMPORT: return "'import'";
    case TK_SWITCH: return "'switch'";
    case TK_CASE: return "'case'";
    case TK_DEFAULT: return "'default'";
    case TK_BREAK: return "'break'";
    case TK_CONTINUE: return "'continue'";
    case TK_QUESTION: return "'?'";
    case TK_AT: return "'@'";
    case TK_ENUM: return "'enum'";
    case TK_TRY: return "'try'";
    case TK_CATCH: return "'catch'";
    case TK_THROW: return "'throw'";
    case TK_TEMPLATE: return "template literal";
    case TK_EXPORT: return "'export'";
    case TK_LPAREN: return "'('";
    case TK_RPAREN: return "')'";
    case TK_LBRACE: return "'{'";
    case TK_RBRACE: return "'}'";
    case TK_LBRACKET: return "'['";
    case TK_RBRACKET: return "']'";
    case TK_COMMA: return "','";
    case TK_SEMI: return "';'";
    case TK_COLON: return "':'";
    case TK_DOT: return "'.'";
    case TK_ELLIPSIS: return "'...'";
    case TK_PLUS: return "'+'";
    case TK_MINUS: return "'-'";
    case TK_STAR: return "'*'";
    case TK_SLASH: return "'/'";
    case TK_PERCENT: return "'%'";
    case TK_ASSIGN: return "'='";
    case TK_PLUS_ASSIGN: return "'+='";
    case TK_MINUS_ASSIGN: return "'-='";
    case TK_STAR_ASSIGN: return "'*='";
    case TK_SLASH_ASSIGN: return "'/='";
    case TK_AMP: return "'&'";
    case TK_STARSTAR: return "'**'";
    case TK_CARET: return "'^'";
    case TK_TILDE: return "'~'";
    case TK_SHL: return "'<<'";
    case TK_SHR: return "'>>'";
    case TK_USHR: return "'>>>'";
    case TK_AMP_ASSIGN: return "'&='";
    case TK_PIPE_ASSIGN: return "'|='";
    case TK_CARET_ASSIGN: return "'^='";
    case TK_SHL_ASSIGN: return "'<<='";
    case TK_SHR_ASSIGN: return "'>>='";
    case TK_USHR_ASSIGN: return "'>>>='";
    case TK_PLUSPLUS: return "'++'";
    case TK_MINUSMINUS: return "'--'";
    case TK_EQ: return "'==='";
    case TK_NEQ: return "'!=='";
    case TK_LT: return "'<'";
    case TK_LE: return "'<='";
    case TK_GT: return "'>'";
    case TK_GE: return "'>='";
    case TK_AND: return "'&&'";
    case TK_OR: return "'||'";
    case TK_NOT: return "'!'";
    case TK_PIPE: return "'|'";
    }
    return "?";
}

void lexer_init(Lexer *lx, const char *src) {
    lx->src = src;
    lx->pos = 0;
    lx->line = 1;
    lx->col = 1;
}

static char peek(Lexer *lx, size_t ahead) { return lx->src[lx->pos + ahead]; }

/* The escape after a backslash at q (q points after it), appended to out as UTF-8; returns how many characters
   it took, or 0 with *bad set when malformed. As JavaScript: \n \t \r \b \f \v \0, \xHH, \uHHHH (a surrogate
   pair \uD83D\uDE00 is one code point; a lone surrogate becomes U+FFFD), \u{H...}, a backslash before a line
   break continues the line, and any other character stands for itself. NUL is kept as C0 80 inside the
   compiler (its strings are C strings); the emitters write a real 0 byte (xs_lit_len, xs_lit_bytes). */
static int hexv(char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; }
static void put_cp(Buf *out, uint32_t c) {
    if (c == 0) { buf_putc(out, (char)0xC0); buf_putc(out, (char)0x80); return; }
    if (c >= 0xD800 && c <= 0xDFFF) c = 0xFFFD;
    if (c < 0x80) buf_putc(out, (char)c);
    else if (c < 0x800) { buf_putc(out, (char)(0xC0 | c >> 6)); buf_putc(out, (char)(0x80 | (c & 0x3F))); }
    else if (c < 0x10000) { buf_putc(out, (char)(0xE0 | c >> 12)); buf_putc(out, (char)(0x80 | (c >> 6 & 0x3F))); buf_putc(out, (char)(0x80 | (c & 0x3F))); }
    else { buf_putc(out, (char)(0xF0 | c >> 18)); buf_putc(out, (char)(0x80 | (c >> 12 & 0x3F))); buf_putc(out, (char)(0x80 | (c >> 6 & 0x3F))); buf_putc(out, (char)(0x80 | (c & 0x3F))); }
}
static int hex_run(const char *q, int n, uint32_t *v) {
    *v = 0;
    for (int i = 0; i < n; i++) { int h = hexv(q[i]); if (h < 0) return 0; *v = *v << 4 | (uint32_t)h; }
    return n;
}
int xs_unescape(const char *q, Buf *out, const char **bad) {
    uint32_t v, lo;
    switch (q[0]) {
    case 'n': buf_putc(out, '\n'); return 1;
    case 't': buf_putc(out, '\t'); return 1;
    case 'r': buf_putc(out, '\r'); return 1;
    case 'b': buf_putc(out, '\b'); return 1;
    case 'f': buf_putc(out, '\f'); return 1;
    case 'v': buf_putc(out, '\v'); return 1;
    case '0':
        if (q[1] >= '0' && q[1] <= '9') { *bad = "octal escapes are not allowed (strict mode): use \\x or \\u"; return 0; }
        put_cp(out, 0);
        return 1;
    case 'x':
        if (!hex_run(q + 1, 2, &v)) { *bad = "\\x takes two hex digits"; return 0; }
        put_cp(out, v);
        return 3;
    case 'u':
        if (q[1] == '{') {
            int n = 0;
            v = 0;
            while (hexv(q[2 + n]) >= 0 && n < 8) { v = v << 4 | (uint32_t)hexv(q[2 + n]); n++; }
            if (!n || q[2 + n] != '}' || v > 0x10FFFF) { *bad = "\\u{...} takes 1 to 6 hex digits, at most 10FFFF"; return 0; }
            put_cp(out, v);
            return n + 3;
        }
        if (!hex_run(q + 1, 4, &v)) { *bad = "\\u takes four hex digits, or \\u{...}"; return 0; }
        if (v >= 0xD800 && v <= 0xDBFF && q[5] == '\\' && q[6] == 'u' && hex_run(q + 7, 4, &lo) && lo >= 0xDC00 && lo <= 0xDFFF) {
            put_cp(out, 0x10000 + ((v - 0xD800) << 10) + (lo - 0xDC00));
            return 11;
        }
        put_cp(out, v);
        return 5;
    case '\r': return q[1] == '\n' ? 2 : 1; /* line continuation */
    case '\n': return 1;
    case 0: *bad = "a backslash at the end"; return 0;
    default: { /* \\ \' \" \` \$ and any other character: itself (a whole UTF-8 sequence) */
        int n = 1;
        while ((q[n] & 0xC0) == 0x80) n++;
        for (int i = 0; i < n; i++) buf_putc(out, q[i]);
        return n;
    }
    }
}

static char advance(Lexer *lx) {
    char c = lx->src[lx->pos++];
    if (c == '\n') {
        lx->line++;
        lx->col = 1;
    } else {
        lx->col++;
    }
    return c;
}

static void skip_space_and_comments(Lexer *lx) {
    for (;;) {
        char c = peek(lx, 0);
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            advance(lx);
        } else if (c == '/' && peek(lx, 1) == '/') {
            while (peek(lx, 0) && peek(lx, 0) != '\n') advance(lx);
        } else if (c == '/' && peek(lx, 1) == '*') {
            Pos start = {lx->line, lx->col};
            advance(lx);
            advance(lx);
            while (peek(lx, 0) && !(peek(lx, 0) == '*' && peek(lx, 1) == '/')) advance(lx);
            if (!peek(lx, 0)) xs_error(start, NULL, "unterminated block comment");
            advance(lx);
            advance(lx);
        } else {
            return;
        }
    }
}

static Token make(Lexer *lx, TokKind kind, Pos pos, size_t start) {
    Token t = {0};
    t.kind = kind;
    t.pos = pos;
    t.start = lx->src + start;
    t.len = (int)(lx->pos - start);
    return t;
}

/* A '/' where an expression starts is a regular expression literal: the pattern runs to the next '/'
   outside a [class] and not escaped; the flags are the letters after it. */
bool lexer_regex(Lexer *lx, const char *slash, char **pattern, char **flags) {
    size_t i = (size_t)(slash - lx->src) + 1;
    bool in_class = false;
    for (;;) {
        char c = lx->src[i];
        if (!c || c == '\n') return false;
        if (c == '\\') { if (!lx->src[i + 1] || lx->src[i + 1] == '\n') return false; i += 2; continue; }
        if (c == '[') in_class = true;
        else if (c == ']') in_class = false;
        else if (c == '/' && !in_class) break;
        i++;
    }
    size_t p0 = (size_t)(slash - lx->src) + 1;
    *pattern = xs_strndup(lx->src + p0, i - p0);
    size_t f0 = ++i;
    while (isalpha((unsigned char)lx->src[i])) i++;
    *flags = xs_strndup(lx->src + f0, i - f0);
    lx->col += (int)(i - lx->pos);
    lx->pos = i;
    return true;
}

Token lexer_next(Lexer *lx) {
    skip_space_and_comments(lx);
    Pos pos = {lx->line, lx->col};
    size_t start = lx->pos;
    char c = peek(lx, 0);

    if (!c) return make(lx, TK_EOF, pos, start);

    if (isalpha((unsigned char)c) || c == '_' || c == '$') {
        while (isalnum((unsigned char)peek(lx, 0)) || peek(lx, 0) == '_' || peek(lx, 0) == '$') advance(lx);
        Token t = make(lx, TK_IDENT, pos, start);
        t.str = xs_strndup(t.start, (size_t)t.len);
        for (size_t i = 0; i < sizeof keywords / sizeof keywords[0]; i++) {
            if (strcmp(t.str, keywords[i].word) == 0) {
                t.kind = keywords[i].kind;
                break;
            }
        }
        return t;
    }

    if (isdigit((unsigned char)c) || (c == '.' && isdigit((unsigned char)peek(lx, 1)))) {
        if (c == '0' && (peek(lx, 1) == 'x' || peek(lx, 1) == 'X')) {
            advance(lx);
            advance(lx);
            while (isxdigit((unsigned char)peek(lx, 0)) || peek(lx, 0) == '_') advance(lx);
        } else if (c == '0' && strchr("bBoO", peek(lx, 1)) && peek(lx, 1)) { /* 0b1010, 0o755 */
            advance(lx);
            advance(lx);
            while (isdigit((unsigned char)peek(lx, 0)) || peek(lx, 0) == '_') advance(lx);
        } else {
            while (isdigit((unsigned char)peek(lx, 0)) || peek(lx, 0) == '_') advance(lx);
            if (peek(lx, 0) == '.') {
                advance(lx);
                while (isdigit((unsigned char)peek(lx, 0)) || peek(lx, 0) == '_') advance(lx);
            }
            if (peek(lx, 0) == 'e' || peek(lx, 0) == 'E') {
                advance(lx);
                if (peek(lx, 0) == '+' || peek(lx, 0) == '-') advance(lx);
                if (!isdigit((unsigned char)peek(lx, 0))) xs_error(pos, NULL, "malformed number literal");
                while (isdigit((unsigned char)peek(lx, 0))) advance(lx);
            }
        }
        if (peek(lx, 0) == 'n') { /* 123n: a BigInt literal */
            Token t = make(lx, TK_NUMBER, pos, start);
            bool radix = t.len > 1 && t.start[0] == '0' && strchr("xXoObB", t.start[1]);
            for (int i = 0; i < t.len && !radix; i++)
                if (t.start[i] == '.' || t.start[i] == 'e' || t.start[i] == 'E') xs_error(pos, NULL, "a BigInt literal is an integer: no '.' or exponent");
            if (!radix && t.len > 1 && t.start[0] == '0') xs_error(pos, NULL, "a BigInt literal has no leading zero");
            char *clean = xs_alloc((size_t)t.len + 1);
            int n = 0;
            for (int i = 0; i < t.len; i++)
                if (t.start[i] != '_') clean[n++] = t.start[i];
            clean[n] = 0;
            advance(lx);
            t.len++;
            t.is_bigint = true;
            t.str = clean;
            return t;
        }
        Token t = make(lx, TK_NUMBER, pos, start);
        /* strip numeric separators */
        char *clean = xs_alloc((size_t)t.len + 1);
        int n = 0;
        for (int i = 0; i < t.len; i++)
            if (t.start[i] != '_') clean[n++] = t.start[i];
        clean[n] = 0;
        if (clean[0] == '0' && strchr("bBoO", clean[1]) && clean[1]) { /* binary / octal: an int */
            int base = clean[1] == 'b' || clean[1] == 'B' ? 2 : 8;
            char *end;
            unsigned long long v = strtoull(clean + 2, &end, base);
            if (*end || end == clean + 2) xs_error(pos, NULL, "malformed %s literal", base == 2 ? "binary" : "octal");
            t.num = (double)v;
            if (v <= 9007199254740991ULL) { t.ival = (long long)v; t.is_int_lit = true; } /* an int is a safe integer of JS (XB-075) */
            return t;
        }
        t.num = strtod(clean, NULL);
        if (!strpbrk(clean, ".eE") || (clean[0] == '0' && (clean[1] == 'x' || clean[1] == 'X'))) {
            char *end;
            long long v = strtoll(clean, &end, 0);
            if (*end == 0 && v <= 9007199254740991LL) { /* larger: a number literal, rounded as JS rounds it (XB-075) */
                t.ival = v;
                t.is_int_lit = true;
            }
        }
        return t;
    }

    if (c == '`') { /* template literal: keep the raw text; the parser splits it into parts */
        advance(lx);
        int depth = 0;
        for (;;) {
            char d = peek(lx, 0);
            if (!d) xs_error(pos, NULL, "unterminated template literal");
            if (d == '`' && depth == 0) break;
            if (d == '\\') { advance(lx); if (peek(lx, 0)) advance(lx); continue; }
            if (d == '$' && peek(lx, 1) == '{') { advance(lx); advance(lx); depth++; continue; }
            if (d == '{' && depth) depth++;
            if (d == '}' && depth) depth--;
            if ((d == '"' || d == '\'') && depth) { /* a string inside ${...} */
                char q = advance(lx);
                while (peek(lx, 0) && peek(lx, 0) != q) { if (peek(lx, 0) == '\\') advance(lx); advance(lx); }
                if (peek(lx, 0)) advance(lx);
                continue;
            }
            advance(lx);
        }
        Token t = make(lx, TK_TEMPLATE, pos, start);
        t.str = xs_strndup(t.start + 1, (size_t)t.len - 1); /* without the opening backtick */
        advance(lx);                                          /* closing backtick */
        return t;
    }
    if (c == '"' || c == '\'') {
        char quote = advance(lx);
        Buf b = {0};
        for (;;) {
            char d = peek(lx, 0);
            if (!d || d == '\n') xs_error(pos, NULL, "unterminated string literal");
            if (d == quote) {
                advance(lx);
                break;
            }
            if (d == '\\') {
                Pos p = {lx->line, lx->col};
                advance(lx);
                const char *bad = NULL;
                int n = xs_unescape(lx->src + lx->pos, &b, &bad);
                if (!n) xs_error(p, bad, "malformed escape sequence in string literal");
                while (n--) advance(lx);
            } else {
                buf_putc(&b, advance(lx));
            }
        }
        Token t = make(lx, TK_STRING, pos, start);
        t.str = b.data ? b.data : xs_strndup("", 0);
        return t;
    }

    advance(lx);
    char n1 = peek(lx, 0), n2 = peek(lx, 1);
    switch (c) {
    case '(': return make(lx, TK_LPAREN, pos, start);
    case ')': return make(lx, TK_RPAREN, pos, start);
    case '{': return make(lx, TK_LBRACE, pos, start);
    case '}': return make(lx, TK_RBRACE, pos, start);
    case '[': return make(lx, TK_LBRACKET, pos, start);
    case ']': return make(lx, TK_RBRACKET, pos, start);
    case ',': return make(lx, TK_COMMA, pos, start);
    case ';': return make(lx, TK_SEMI, pos, start);
    case ':': return make(lx, TK_COLON, pos, start);
    case '.':
        if (n1 == '.' && peek(lx, 1) == '.') { advance(lx); advance(lx); return make(lx, TK_ELLIPSIS, pos, start); }
        return make(lx, TK_DOT, pos, start);
    case '@': return make(lx, TK_AT, pos, start); /* decorators */
    case '?':
        if (n1 == '?' || n1 == '.') xs_error(pos, "there is no undefined or null in xisto, so '\?\?' and '?.' do not exist", "'?%c' is not supported", n1);
        return make(lx, TK_QUESTION, pos, start);
    case '+':
        if (n1 == '+') { advance(lx); return make(lx, TK_PLUSPLUS, pos, start); }
        if (n1 == '=') { advance(lx); return make(lx, TK_PLUS_ASSIGN, pos, start); }
        return make(lx, TK_PLUS, pos, start);
    case '-':
        if (n1 == '-') { advance(lx); return make(lx, TK_MINUSMINUS, pos, start); }
        if (n1 == '=') { advance(lx); return make(lx, TK_MINUS_ASSIGN, pos, start); }
        return make(lx, TK_MINUS, pos, start);
    case '*':
        if (n1 == '*') { advance(lx); return make(lx, TK_STARSTAR, pos, start); }
        if (n1 == '=') { advance(lx); return make(lx, TK_STAR_ASSIGN, pos, start); }
        return make(lx, TK_STAR, pos, start);
    case '/':
        if (n1 == '=') { advance(lx); return make(lx, TK_SLASH_ASSIGN, pos, start); }
        return make(lx, TK_SLASH, pos, start);
    case '%': return make(lx, TK_PERCENT, pos, start);
    case '=':
        if (n1 == '=' && n2 == '=') { advance(lx); advance(lx); return make(lx, TK_EQ, pos, start); }
        if (n1 == '=') xs_error(pos, "xisto has no type coercion, so '==' and '===' would mean the same thing; use '==='", "'==' is not supported");
        if (n1 == '>') { advance(lx); return make(lx, TK_ARROW, pos, start); }
        return make(lx, TK_ASSIGN, pos, start);
    case '!':
        if (n1 == '=' && n2 == '=') { advance(lx); advance(lx); return make(lx, TK_NEQ, pos, start); }
        if (n1 == '=') xs_error(pos, "use '!=='", "'!=' is not supported");
        return make(lx, TK_NOT, pos, start);
    case '<':
        if (n1 == '=') { advance(lx); return make(lx, TK_LE, pos, start); }
        if (n1 == '<' && n2 == '=') { advance(lx); advance(lx); return make(lx, TK_SHL_ASSIGN, pos, start); }
        if (n1 == '<') { advance(lx); return make(lx, TK_SHL, pos, start); }
        return make(lx, TK_LT, pos, start);
    case '>':
        if (n1 == '=') { advance(lx); return make(lx, TK_GE, pos, start); }
        return make(lx, TK_GT, pos, start);
    case '&':
        if (n1 == '&') { advance(lx); return make(lx, TK_AND, pos, start); }
        if (n1 == '=') { advance(lx); return make(lx, TK_AMP_ASSIGN, pos, start); }
        return make(lx, TK_AMP, pos, start);
    case '|':
        if (n1 == '|') { advance(lx); return make(lx, TK_OR, pos, start); }
        if (n1 == '=') { advance(lx); return make(lx, TK_PIPE_ASSIGN, pos, start); }
        return make(lx, TK_PIPE, pos, start);
    case '^':
        if (n1 == '=') { advance(lx); return make(lx, TK_CARET_ASSIGN, pos, start); }
        return make(lx, TK_CARET, pos, start);
    case '~': return make(lx, TK_TILDE, pos, start);
    }
    xs_error(pos, NULL, "unexpected character '%c'", c);
    return make(lx, TK_EOF, pos, start);
}
