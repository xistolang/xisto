#!/usr/bin/env python3
"""The runtime as a library: from the runtime headers (written as one translation unit, `static` everywhere),
make the two halves of a split build.

  impl  compiled once per configuration and cached: every function an external definition, every global
        defined here.
  decl  what a program includes: the same functions as C99 inline definitions with external linkage (the
        compiler may inline them; at -O0 it only calls them, so they cost nothing to compile) and the
        globals as extern declarations.

The addresses of functions (type descriptors compare them) and the globals are then the same in both units.
Usage: rtsplit.py impl|decl FILE  (writes to stdout). Only top-level declarations change, also inside #define
bodies (the macros that generate a family of functions); function bodies are left alone.
"""
import re
import sys

mode, path = sys.argv[1], sys.argv[2]
src = open(path).read()
n = len(src)

# the storage macro of the runtime's always-inlined helpers (XS_NOINLINE functions are rewritten one by one,
# since programs use that macro for their own functions too)
MACROS = {
    "impl": {
        "#define XS_AI __attribute__((always_inline)) static inline": "#define XS_AI __attribute__((always_inline)) extern inline",
    },
    "decl": {
        "#define XS_AI __attribute__((always_inline)) static inline": "#define XS_AI __attribute__((always_inline)) inline",
    },
}


def is_function(head):
    """head: text from `static` up to the declarator's end; a function unless the first `(` opens `(*name)`"""
    m = re.search(r"[=;{(\[]", head)
    if not m or m.group(0) != "(":
        return False
    return not re.match(r"\(\s*\*", head[m.start():])


def body_end(j):
    """j at a `{`: the index just past its matching `}` (strings, characters and comments skipped)"""
    d = 0
    while j < n:
        if src.startswith("//", j):
            j = src.find("\n", j)
            continue
        if src.startswith("/*", j):
            j = src.find("*/", j + 2) + 2
            continue
        ch = src[j]
        if ch == '"' or ch == "'":
            k = j + 1
            while k < n and src[k] != ch:
                k += 2 if src[k] == "\\" else 1
            j = k + 1
            continue
        if ch == "{":
            d += 1
        elif ch == "}":
            d -= 1
            if d == 0:
                return j + 1
        j += 1
    return n


STATIC_LOCAL = re.compile(r"(?:^|[;{}])\s*(?:\\\s*)*static\b", re.M)


def prototype(head):
    """an external declaration for a function the library defines"""
    head = re.sub(r"\bXS_NOINLINE\b", "__attribute__((noinline))", head, count=1)
    head = re.sub(r"\bXS_AI\b", "", head)
    head = re.sub(r"__attribute__\(\(\s*always_inline\s*\)\)", "", head)
    head = re.sub(r"\bstatic\s+inline\b|\binline\s+static\b|\bstatic\b", "", head, count=1)
    return head.rstrip().rstrip(";").rstrip() + ";"


def rewrite(text, func):
    decl = text.split("=")[0]
    # read-only data: `const T x`, or a pointer that is itself const (`T *const p`); `const char *p` changes
    const = bool(re.search(r"\bconst\b", decl)) and ("*" not in decl or re.search(r"\*\s*const\b", decl) is not None)
    if func:
        inline = re.search(r"\bstatic\s+inline\b|\binline\s+static\b", text) is not None
        if mode == "impl":
            if inline:
                return re.sub(r"\bstatic\s+inline\b|\binline\s+static\b", "extern inline", text, count=1)
            return re.sub(r"\bstatic\s+", "", text, count=1)
        if inline:
            return re.sub(r"\bstatic\s+inline\b|\binline\s+static\b", "inline", text, count=1)
        return re.sub(r"\bstatic\b", "inline", text, count=1)
    if const and not re.search(r"\bxs_type\b", decl):
        return text  # read-only data: a copy in each unit is fine (a type descriptor is one object: its address is used)
    if mode == "impl":
        return re.sub(r"\bstatic\s+", "", text, count=1)
    t = re.sub(r"\bstatic\s+", "extern ", text, count=1)  # declared here, defined in the library
    k = t.find("=")
    if k >= 0:
        t = t[:k].rstrip() + ";"
    return t


DROPPED = set()   # functions the program unit only declares (found by a first pass)


def fname(head):
    m = re.search(r"([\w#]+)\s*\(", re.sub(r"__attribute__\s*\(\([^)]*\)\)", "", head))
    return m.group(1) if m else ""


def run():
    out = []
    i = 0

    depth = 0          # braces outside strings and comments
    in_macro = False   # inside a #define (continued lines)
    macro_depth = 0
    line_start = True
    while i < n:
        c = src[i]
        # comments and literals are copied as they are
        if src.startswith("//", i):
            j = src.find("\n", i)
            j = n if j < 0 else j
            out.append(src[i:j])
            i = j
            continue
        if src.startswith("/*", i):
            j = src.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append(src[i:j])
            i = j
            continue
        if c == '"' or c == "'":
            j = i + 1
            while j < n and src[j] != c:
                j += 2 if src[j] == "\\" else 1
            out.append(src[i: j + 1])
            i = j + 1
            line_start = False
            continue
        if c == "\n":
            if in_macro and (i == 0 or src[i - 1] != "\\"):
                in_macro = False
            out.append(c)
            i += 1
            line_start = True
            continue
        if line_start and c == "#":
            j = src.find("\n", i)
            j = n if j < 0 else j
            line = src[i:j]
            stripped = line.strip()
            for a, b in MACROS[mode].items():
                if stripped.startswith(a) and (len(stripped) == len(a) or not (stripped[len(a)].isalnum() or stripped[len(a)] == "_")):
                    line = line.replace(a, b, 1)
                    break
            if re.match(r"#\s*include\b", stripped) and not in_macro:  # system headers keep their own visibility
                line = "#pragma GCC visibility push(default)\n" + line + "\n#pragma GCC visibility pop"
            if re.match(r"#\s*define\b", stripped) and stripped.endswith("\\"):
                in_macro = True
                macro_depth = 0
            out.append(line)
            i = j
            line_start = False
            continue
        cur_depth = macro_depth if in_macro else depth
        k = i - 1
        while k >= 0 and src[k] in " \t":
            k -= 1
        after_decl = not in_macro and k >= 0 and src[k] == ";"  # `static int a; static int b;` on one line
        if cur_depth == 0 and (line_start or after_decl or (in_macro and src[i - 1] in " \t")) and re.match(r"(?:__attribute__\(\([^)]*\)\)\s*)*(?:static|XS_NOINLINE)\b", src[i:i + 200]):
            # a top-level declaration: a function up to its `{` or `;`, a variable up to its `;`
            j = i
            par = 0
            while j < n and not (par == 0 and src[j] in "{;=(["):
                j += 1
            func = is_function(src[i:j + 8])
            j = i
            par = 0
            br = 0
            while j < n:
                ch = src[j]
                if src.startswith("/*", j):  # a comment inside a declaration (a struct's fields): an apostrophe there
                    e = src.find("*/", j + 2)  # read as a character literal lost the braces (--dev broke on xs_lent)
                    j = n if e < 0 else e + 2
                    continue
                if src.startswith("//", j):
                    e = src.find("\n", j)
                    j = n if e < 0 else e
                    continue
                if ch == "(":
                    par += 1
                elif ch == ")":
                    par -= 1
                elif ch == "{" and func and par == 0:
                    break
                elif ch == "{":
                    br += 1
                elif ch == "}":
                    br -= 1
                elif ch == ";" and par == 0 and br == 0:
                    j += 1
                    break
                elif ch == '"' or ch == "'":
                    q = ch
                    j += 1
                    while j < n and src[j] != q:
                        j += 2 if src[j] == "\\\\" else 1
                j += 1
            head = src[i:j]
            noinline = re.match(r"(?:__attribute__\(\([^)]*\)\)\s*)*XS_NOINLINE\b", head) is not None
            # written out: `static __attribute__((noinline)) void f(...)` is as much a call as XS_NOINLINE
            hand_noinline = re.search(r"__attribute__\(\([^)]*\bnoinline\b", head) is not None
            if func and j < n and src[j] == "{":
                e = body_end(j)
                # the program unit gets only a declaration when inlining is not wanted (noinline) or not allowed:
                # C99 inline definitions may not keep state in static locals (each unit would get its own copy)
                # the program unit is a --dev build (-O0: nothing is inlined but always_inline): only declarations,
                # every body is the library's; parsing 130 KB of bodies per unit was most of an edit's compile time
                if mode == "decl":
                    DROPPED.add(fname(head))
                    out.append(prototype(head))
                    i = e
                    line_start = False
                    continue
            if func and mode == "decl" and fname(head) in DROPPED:
                out.append(prototype(head) if head.rstrip().endswith(";") else head)
            elif noinline:
                out.append(re.sub(r"\bXS_NOINLINE\b", "__attribute__((noinline))", head, count=1) if func else head)
            else:
                out.append(rewrite(head, func))
            i = j
            line_start = False
            continue
        if c == "{":
            if in_macro:
                macro_depth += 1
            else:
                depth += 1
        elif c == "}":
            if in_macro:
                macro_depth -= 1
            else:
                depth -= 1
        if c not in " \t":
            line_start = False
        out.append(c)
        i += 1

    return out


run()        # first pass: which bodies the program unit drops
out = run()
if mode == "decl":
    out.insert(0, "#ifndef XS_RT_DECL\n#define XS_RT_DECL 1 /* a split build's program unit: the library defines the rest */\n#endif\n")
# the library and the program end up in one executable: hidden symbols are addressed directly (adrp + add)
# instead of through the GOT, which the allocator's free lists and counters would pay on every access
out.insert(0, "#pragma GCC visibility push(hidden)\n")
out.append("\n#pragma GCC visibility pop\n")
sys.stdout.write("".join(out))
