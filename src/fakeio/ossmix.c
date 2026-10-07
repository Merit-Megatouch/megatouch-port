/*
 * ossmix — stand-in for OSS v4's mixer tool inside the loader sandbox.
 *
 * The loader sets the cabinet volume with `ossmix jack.green.front <0-100>` (Volume Control,
 * attract/game volumes) and saves it with `savemixer`. Here the value becomes the PulseAudio
 * volume of the cabinet's streams: the loader's own ("OSS start", from ossfake.so) and the
 * Unity games' ("FMOD Ex App"). PulseAudio's stream-restore remembers it per application, so
 * streams opened later start at the same volume. Values are kept in $MEGAIO_DIR/mixer.
 *   ossmix                     list the controls
 *   ossmix CONTROL             print a control's value
 *   ossmix CONTROL VALUE[:R]   set it (0-100; left:right accepted, the louder side is used)
 */
#include <pulse/pulseaudio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *controls[] = {"jack.green.front", "jack.fp-green.front", "vmix0-outvol", NULL};

static void state_path(char *out, size_t n) {
    const char *d = getenv("MEGAIO_DIR");
    snprintf(out, n, "%s/mixer", d ? d : "/var/merit/fakeio");
}

static double load_value(const char *control) {
    char p[512], name[64];
    double v, found = 75;
    state_path(p, sizeof p);
    FILE *f = fopen(p, "r");
    if (!f) return found;
    while (fscanf(f, "%63s %lf", name, &v) == 2) if (!strcmp(name, control)) found = v;
    fclose(f);
    return found;
}

static void save_value(const char *control, double value) {
    char p[512], t[520], name[64];
    double v;
    state_path(p, sizeof p);
    snprintf(t, sizeof t, "%s.new", p);
    FILE *in = fopen(p, "r"), *out = fopen(t, "w");
    if (!out) return;
    if (in) {
        while (fscanf(in, "%63s %lf", name, &v) == 2) if (strcmp(name, control)) fprintf(out, "%s %g\n", name, v);
        fclose(in);
    }
    fprintf(out, "%s %g\n", control, value);
    fclose(out);
    rename(t, p);
}

/* ---- apply to PulseAudio ---- */
static pa_mainloop *ml;
static pa_cvolume target;
static int pending, done;

static int ours(const pa_sink_input_info *i) {
    const char *app = pa_proplist_gets(i->proplist, PA_PROP_APPLICATION_NAME);
    const char *bin = pa_proplist_gets(i->proplist, PA_PROP_APPLICATION_PROCESS_BINARY);
    if (app && (!strncmp(app, "OSS ", 4) || !strncmp(app, "FMOD", 4))) return 1;
    return bin && (!strcmp(bin, "start") || !strcmp(bin, "LinuxPlayer"));
}

static void op_done(pa_context *c, int success, void *u) {
    (void)c; (void)success; (void)u;
    if (--pending == 0 && done) pa_mainloop_quit(ml, 0);
}

static void on_input(pa_context *c, const pa_sink_input_info *i, int eol, void *u) {
    (void)u;
    if (eol) {
        done = 1;
        if (pending == 0) pa_mainloop_quit(ml, 0);
        return;
    }
    if (!ours(i)) return;
    pa_cvolume v = target;
    v.channels = i->volume.channels;
    for (int k = 0; k < v.channels; k++) v.values[k] = target.values[0];
    pending++;
    pa_operation *o = pa_context_set_sink_input_volume(c, i->index, &v, op_done, NULL);
    if (o) pa_operation_unref(o); else pending--;
}

static void on_state(pa_context *c, void *u) {
    (void)u;
    switch (pa_context_get_state(c)) {
    case PA_CONTEXT_READY: {
        pa_operation *o = pa_context_get_sink_input_info_list(c, on_input, NULL);
        if (o) pa_operation_unref(o);
        break;
    }
    case PA_CONTEXT_FAILED: case PA_CONTEXT_TERMINATED: pa_mainloop_quit(ml, 1); break;
    default: break;
    }
}

static int apply(double value) {
    if (value < 0) value = 0;
    if (value > 100) value = 100;
    pa_cvolume_set(&target, 1, pa_sw_volume_from_linear(value / 100.0));
    ml = pa_mainloop_new();
    pa_context *c = pa_context_new(pa_mainloop_get_api(ml), "ossmix");
    pa_context_set_state_callback(c, on_state, NULL);
    if (pa_context_connect(c, NULL, PA_CONTEXT_NOFLAGS, NULL) < 0) return 1;
    int ret = 1;
    pa_mainloop_run(ml, &ret);
    pa_context_disconnect(c);
    pa_context_unref(c);
    pa_mainloop_free(ml);
    return ret;
}

int main(int argc, char **argv) {
    int a = 1;
    while (a < argc && argv[a][0] == '-') a++;          /* OSS options (-d dev, -q …) ignored */
    if (a >= argc) {
        for (int i = 0; controls[i]; i++) printf("%s %g (currently)\n", controls[i], load_value(controls[i]));
        return 0;
    }
    const char *control = argv[a];
    if (a + 1 >= argc) { printf("%g\n", load_value(control)); return 0; }
    double l = 0, r = -1;
    if (sscanf(argv[a + 1], "%lf:%lf", &l, &r) < 1) { fprintf(stderr, "ossmix: bad value %s\n", argv[a + 1]); return 1; }
    double v = r > l ? r : l;
    save_value(control, v);
    printf("Value of mixer control %s set to %g\n", control, v);
    if (!strncmp(control, "jack.", 5) || !strcmp(control, "vmix0-outvol")) apply(v);
    return 0;
}
