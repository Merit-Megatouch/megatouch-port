/*
 * dbdump — print the cabinet's encrypted databases (/var/merit/tournamaxx/database/NAME.db,
 * /var/merit/mymerit.db …) as SQL. Runs inside the loader sandbox:
 *
 *   scripts/loader.sh run /opt/fakeio/dbdump [--schema] FILE.db ...
 *
 * The cabinet's database library (libgendef_db.so) opens them with an encryption key: SQLite 2.8
 * through sqlite_open_crypt(path, 0, &err, 2, key), or SQLite 3 with "PRAGMA key=…". Each database
 * class supplies its key through a Key() method (books_db::Key(), coinin_settings_db::Key(), …).
 * The keys are not part of this project: this program calls those methods in the cabinet's
 * libraries and tries each key on the file, which is only read. (The library's own open function
 * is not used: when an open fails it moves the file aside as NAME.db.0 and creates an empty one.)
 * --schema prints only the CREATE statements; otherwise every table's rows follow as INSERTs.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef int (*cb_t)(void *, int, char **, char **);
static int (*s3_open)(const char *, void **);
static int (*s3_exec)(void *, const char *, cb_t, void *, char **);
static int (*s3_close)(void *);
static void *(*s2_open_crypt)(const char *, int, char **, int, const char *);
static int (*s2_exec)(void *, const char *, cb_t, void *, char **);
static void (*s2_close)(void *);
static const char *keys[32];
static int nkeys;

/* every database class's Key() in the cabinet's libraries (they return a constant string) */
static void collect_keys(void) {
    static const char *const where[][2] = {
        {"/usr/local/lib/libgendef_db.so", "_ZN17abstract_db_class3KeyEv"},
        {"/usr/local/lib/libgendef_db.so", "_ZN14db_misc_result15misc_results_db3KeyEv"},
        {"/usr/local/lib/libbooks.so", "_ZN8db_books8books_db3KeyEv"},
        {"/usr/local/lib/libmoney.so", "_ZN14money_settings18coinin_settings_db3KeyEv"},
        {"/usr/local/lib/libmoney.so", "_ZN22db_player_credit_vault22player_credit_vault_db3KeyEv"},
        {"/usr/local/lib/libnetwork.so", "_ZN7network14connections_db3KeyEv"},
        {"/usr/local/lib/libads.so", "_ZN10db_ad_data10ad_data_db3KeyEv"},
        {"/usr/local/lib/libami.so", "_ZN13xml_ami_books16xml_ami_books_db3KeyEv"},
        {"/usr/local/lib/libami.so", "_ZN12xml_rowelink15RoweLinkInfo_db3KeyEv"},
        {"/usr/local/lib/libttiface.so", "_ZN9db_ttunes15TouchTunesDb_db3KeyEv"},
    };
    for (unsigned i = 0; i < sizeof where / sizeof where[0]; i++) {
        void *h = dlopen(where[i][0], RTLD_NOW | RTLD_GLOBAL);
        const char *(*key)(void *) = h ? (const char *(*)(void *))dlsym(h, where[i][1]) : NULL;
        const char *k = key ? key(NULL) : NULL;
        if (!k) continue;
        int dup = 0;
        for (int j = 0; j < nkeys; j++) dup |= !strcmp(keys[j], k);
        if (!dup && nkeys < 32) keys[nkeys++] = k;
    }
}

struct db { void *h; int v3; };

static int run(struct db *d, const char *sql, cb_t cb, void *arg) {
    char *err = NULL;
    int rc = d->v3 ? s3_exec(d->h, sql, cb, arg, &err) : s2_exec(d->h, sql, cb, arg, &err);
    if (rc && err) fprintf(stderr, "dbdump: %s: %s\n", sql, err);
    return rc;
}

static void quote(const char *v) {
    if (!v) { fputs("NULL", stdout); return; }
    putchar('\'');
    for (; *v; v++) { if (*v == '\'') putchar('\''); putchar(*v); }
    putchar('\'');
}

struct rowctx { const char *table; };
static int print_row(void *arg, int n, char **val, char **col) {
    (void)col;
    printf("INSERT INTO \"%s\" VALUES(", ((struct rowctx *)arg)->table);
    for (int i = 0; i < n; i++) { if (i) putchar(','); quote(val[i]); }
    puts(");");
    return 0;
}

struct tables { char name[256][128]; int n; };
static int schema_row(void *arg, int n, char **val, char **col) {
    (void)n; (void)col;
    struct tables *t = arg;
    if (val[2]) printf("%s;\n", val[2]);
    if (val[0] && !strcmp(val[0], "table") && t->n < 256) snprintf(t->name[t->n++], 128, "%s", val[1]);
    return 0;
}

static int count_row(void *arg, int n, char **val, char **col) {
    (void)n; (void)col;
    *(int *)arg = val[0] ? atoi(val[0]) : 0;
    return 0;
}

int main(int argc, char **argv) {
    int schema_only = argc > 1 && !strcmp(argv[1], "--schema");
    if (argc < 2 + schema_only) { fprintf(stderr, "usage: dbdump [--schema] FILE.db ...\n"); return 2; }
    void *l3 = dlopen("/usr/lib/libsqlite3.so.0", RTLD_NOW | RTLD_GLOBAL);
    void *l2 = dlopen("/usr/local/lib/libsqlite.so.0", RTLD_NOW | RTLD_GLOBAL);
    if (!l3 || !l2) { fprintf(stderr, "dbdump: %s\n", dlerror()); return 1; }
    s3_open = (int (*)(const char *, void **))dlsym(l3, "sqlite3_open");
    s3_exec = (int (*)(void *, const char *, cb_t, void *, char **))dlsym(l3, "sqlite3_exec");
    s3_close = (int (*)(void *))dlsym(l3, "sqlite3_close");
    s2_open_crypt = (void *(*)(const char *, int, char **, int, const char *))dlsym(l2, "sqlite_open_crypt");
    s2_exec = (int (*)(void *, const char *, cb_t, void *, char **))dlsym(l2, "sqlite_exec");
    s2_close = (void (*)(void *))dlsym(l2, "sqlite_close");
    if (!s3_open || !s3_exec || !s2_open_crypt || !s2_exec) {
        fprintf(stderr, "dbdump: the cabinet's SQLite libraries lack a needed symbol\n");
        return 1;
    }
    collect_keys();
    if (!nkeys) { fprintf(stderr, "dbdump: no database keys found in the cabinet's libraries\n"); return 1; }
    int bad = 0;
    for (int i = 1 + schema_only; i < argc; i++) {
        if (access(argv[i], R_OK)) { fprintf(stderr, "dbdump: %s: not readable\n", argv[i]); bad = 1; continue; }
        struct db d = {0};
        int n = -1;
        for (int k = 0; k < nkeys && n < 0; k++) {
            char *err = NULL;                                /* SQLite 2.8 */
            d.v3 = 0;
            d.h = s2_open_crypt(argv[i], 0, &err, 2, keys[k]);
            if (d.h && s2_exec(d.h, "SELECT count(*) FROM sqlite_master", count_row, &n, NULL)) { s2_close(d.h); n = -1; }
            if (n >= 0) break;
            if (!s3_open(argv[i], &d.h)) {                   /* SQLite 3 */
                char pragma[300];
                snprintf(pragma, sizeof pragma, "PRAGMA key=%s", keys[k]);
                d.v3 = 1;
                if (s3_exec(d.h, pragma, NULL, NULL, NULL) ||
                    s3_exec(d.h, "SELECT count(*) FROM sqlite_master", count_row, &n, NULL)) {
                    s3_close(d.h);
                    n = -1;
                }
            }
        }
        if (n < 0) { fprintf(stderr, "dbdump: %s: none of the cabinet's keys opens it\n", argv[i]); bad = 1; continue; }
        printf("-- %s (SQLite %d, %d schema entries)\n", argv[i], d.v3 ? 3 : 2, n);
        struct tables *t = calloc(1, sizeof *t);
        run(&d, "SELECT type, name, sql FROM sqlite_master ORDER BY type DESC, name", schema_row, t);
        if (!schema_only)
            for (int k = 0; k < t->n; k++) {
                char sql[200];
                struct rowctx rc = {t->name[k]};
                snprintf(sql, sizeof sql, "SELECT * FROM \"%s\"", t->name[k]);
                run(&d, sql, print_row, &rc);
            }
        free(t);
        if (d.v3) s3_close(d.h); else s2_close(d.h);
        putchar('\n');
    }
    return bad;
}
