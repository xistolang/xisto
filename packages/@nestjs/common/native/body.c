/* @Body("user"): the JSON text of one field of the request body (or "" when the body has no such field). */
static void xn_nest_write(xs_sb *sb, xs_jv *v) {
    switch (v->kind) {
    case XJ_NULL: xs_sb_put(sb, "null", 4); return;
    case XJ_BOOL: xs_sb_put(sb, v->b ? "true" : "false", v->b ? 4 : 5); return;
    case XJ_NUM: xs_sb_num(sb, v->num); return;
    case XJ_STR: { xs_str *s = xs_str_new(v->str, v->slen); xs_sb_str(sb, s); xs_release(s); return; }
    case XJ_ARR:
        xs_sb_put(sb, "[", 1);
        for (size_t i = 0; i < v->n; i++) { if (i) xs_sb_put(sb, ",", 1); xn_nest_write(sb, v->items[i]); }
        xs_sb_put(sb, "]", 1);
        return;
    case XJ_OBJ:
        xs_sb_put(sb, "{", 1);
        for (size_t i = 0; i < v->n; i++) {
            if (i) xs_sb_put(sb, ",", 1);
            xs_str *k = xs_str_new(v->keys[i], strlen(v->keys[i]));
            xs_sb_str(sb, k);
            xs_release(k);
            xs_sb_put(sb, ":", 1);
            xn_nest_write(sb, v->items[i]);
        }
        xs_sb_put(sb, "}", 1);
        return;
    }
}
XS_NOINLINE xs_str *xn__nestjs_common_bodyField(xs_str *body, xs_str *key) {
    if (body->len == 0) return xs_str_new("", 0);
    xs_jarena_reset();
    xs_jv *v = xs_json_parse(body);
    xs_jv *f = v->kind == XJ_OBJ ? xs_json_field_opt(v, key->data) : NULL;
    if (!f || f->kind == XJ_NULL) { xs_jv_free(v); return xs_str_new("", 0); }
    xs_sb sb = {0};
    xn_nest_write(&sb, f);
    xs_jv_free(v);
    return xs_sb_finish(&sb);
}
