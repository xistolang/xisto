/* xisto fmt: deterministic layout without touching tokens.
   Re-indents every line by bracket depth (2 spaces), indents `case` bodies inside switch, gives
   continuation lines (starting with '.', '?', ':', '&&', '||', '+') one extra level, strips trailing
   whitespace, collapses runs of blank lines, and ends the file with one newline. Strings, template
   literals and comments are never changed inside; lines inside a multi-line template literal are left
   exactly as they are. The output is a fixed point: formatting it again changes nothing. */
#include "xisto.h"

/* kind: '{' '(' '[' ; is_switch: a switch body; indents: only the last bracket left open on a line adds a
   level, so `f((x) => {` indents its body once, like prettier */
typedef struct { char kind; bool is_switch; bool indents; } Frame;

static int indent_level(Frame *stack, int depth) {
    int n = 0;
    for (int i = 0; i < depth; i++) n += stack[i].indents;
    return n;
}

static bool starts_with(const char *s, const char *p) { return strncmp(s, p, strlen(p)) == 0; }

static bool is_ident_char(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '$'; }

static bool starts_with_word(const char *s, const char *w) {
    size_t n = strlen(w);
    return strncmp(s, w, n) == 0 && !is_ident_char(s[n]);
}

/* Does this line leave a template literal or a backslash-continued string open? Then its trailing
   whitespace belongs to the literal, and the next line must be kept verbatim. */
static bool ends_inside_literal(const char *c, bool in_block_comment) {
    for (; *c; c++) {
        if (in_block_comment) { if (c[0] == '*' && c[1] == '/') { in_block_comment = false; c++; } continue; }
        if (c[0] == '/' && c[1] == '/') return false;
        if (c[0] == '/' && c[1] == '*') { in_block_comment = true; c++; continue; }
        if (*c == '"' || *c == '\'') {
            char q = *c++;
            while (*c && *c != q) { if (*c == '\\') { if (!c[1]) return true; c++; } c++; }
            if (!*c) return false;
            continue;
        }
        if (*c == '`') {
            c++;
            int inner = 0;
            while (*c) {
                if (*c == '\\' && c[1]) { c += 2; continue; }
                if (inner == 0 && *c == '`') break;
                if (c[0] == '$' && c[1] == '{') { inner++; c += 2; continue; }
                if (inner > 0 && *c == '{') inner++;
                if (inner > 0 && *c == '}') inner--;
                c++;
            }
            if (!*c) return true;
        }
    }
    return false;
}

char *xs_format(const char *src) {
    Buf out = {0};
    Frame stack[512];
    int depth = 0;
    bool in_block_comment = false;
    int template_depth = 0;    /* inside a template literal that spans lines (only outside ${}) */
    bool string_cont = false;  /* the previous line ended inside a quoted string with a backslash */
    int blank_run = 0;
    bool pending_switch = false;
    bool any_output = false;
    const char *line = src;
    while (*line) {
        const char *eol = strchr(line, '\n');
        size_t len = eol ? (size_t)(eol - line) : strlen(line);
        char *raw = xs_strndup(line, len);
        line = eol ? eol + 1 : line + len;
        /* strip trailing whitespace (and \r), except inside a multi-line template literal */
        size_t rl = strlen(raw);
        bool keep_tail = template_depth > 0 || string_cont || ends_inside_literal(raw, in_block_comment);
        if (!keep_tail)
            while (rl > 0 && (raw[rl - 1] == ' ' || raw[rl - 1] == '\t' || raw[rl - 1] == '\r')) raw[--rl] = 0;

        if (template_depth > 0 || string_cont) { /* inside a multi-line literal: keep the line verbatim */
            string_cont = false;
            buf_puts(&out, raw);
            buf_putc(&out, '\n');
        } else {
            char *t = raw;
            while (*t == ' ' || *t == '\t') t++;
            if (!*t) {
                if (any_output && blank_run == 0) buf_putc(&out, '\n');
                blank_run++;
                goto scan;
            }
            blank_run = 0;
            /* closing brackets at the start of the line belong to the outer level */
            int closers = 0;
            for (const char *c = t; *c == '}' || *c == ']' || *c == ')'; c++) closers++;
            int eff = depth - closers < 0 ? 0 : depth - closers;
            int level = indent_level(stack, eff);
            /* every switch body around the line adds a level for case bodies; case/default labels of the
               innermost switch sit one level above their statements */
            for (int i = 0; i < eff; i++) {
                if (!stack[i].is_switch) continue;
                bool label = i == eff - 1 && (starts_with_word(t, "case") || starts_with_word(t, "default"));
                if (!label) level++;
            }
            if (in_block_comment) {
                for (int i = 0; i < level; i++) buf_puts(&out, "  ");
                if (*t == '*') buf_putc(&out, ' ');
                buf_puts(&out, t);
            } else {
                bool cont = t[0] == '.' || (t[0] == '?' && t[1] != '?') || (t[0] == ':' ) || starts_with(t, "&&") || starts_with(t, "||") ||
                            (t[0] == '+' && t[1] == ' ');
                for (int i = 0; i < level + (cont ? 1 : 0); i++) buf_puts(&out, "  ");
                buf_puts(&out, t);
            }
            buf_putc(&out, '\n');
            any_output = true;
        }
    scan:;
        /* update the bracket state with this line's tokens */
        int depth_before = depth;
        for (const char *c = raw; *c; c++) {
            if (in_block_comment) {
                if (c[0] == '*' && c[1] == '/') { in_block_comment = false; c++; }
                continue;
            }
            if (template_depth > 0) {
                if (*c == '\\' && c[1]) { c++; continue; }
                if (*c == '`') { template_depth = 0; continue; }
                continue; /* ${...} spanning lines is rare; its braces are not counted */
            }
            if (c[0] == '/' && c[1] == '/') break;
            if (c[0] == '/' && c[1] == '*') { in_block_comment = true; c++; continue; }
            if (*c == '"' || *c == '\'') {
                char q = *c++;
                while (*c && *c != q) {
                    if (*c == '\\' && !c[1]) { string_cont = true; break; } /* "...\ at end of line: continues on the next */
                    if (*c == '\\' && c[1]) c++;
                    c++;
                }
                if (!*c) break;
                continue;
            }
            if (*c == '`') {
                /* a template literal: skip to its end on this line, counting ${ } nesting */
                c++;
                int inner = 0;
                bool closed = false;
                while (*c) {
                    if (*c == '\\' && c[1]) { c += 2; continue; }
                    if (inner == 0 && *c == '`') { closed = true; break; }
                    if (c[0] == '$' && c[1] == '{') { inner++; c += 2; continue; }
                    if (inner > 0 && *c == '{') inner++;
                    if (inner > 0 && *c == '}') inner--;
                    c++;
                }
                if (!closed) { template_depth = 1; break; }
                continue;
            }
            if (starts_with_word(c, "switch") && (c == raw || !is_ident_char(c[-1]))) pending_switch = true;
            if (*c == '{' || *c == '(' || *c == '[') {
                if (depth < 512) {
                    stack[depth].kind = *c;
                    stack[depth].indents = false;
                    stack[depth].is_switch = *c == '{' && pending_switch;
                    if (*c == '{') pending_switch = false;
                    depth++;
                }
            } else if (*c == '}' || *c == ')' || *c == ']') {
                if (depth > 0) depth--;
                if (depth < depth_before) depth_before = depth;
            }
        }
        /* of the brackets this line opened and left open, only the last one indents what follows */
        if (depth > depth_before) stack[depth - 1].indents = true;
        free(raw);
    }
    /* trim trailing blank lines, end with exactly one newline */
    while (out.len > 1 && out.data[out.len - 1] == '\n' && out.data[out.len - 2] == '\n') out.data[--out.len] = 0;
    if (out.len == 0) buf_puts(&out, "");
    else if (out.data[out.len - 1] != '\n') buf_putc(&out, '\n');
    return out.data ? out.data : xs_strndup("", 0);
}
