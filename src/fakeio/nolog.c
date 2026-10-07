/*
 * nolog.so — preloaded only into the cabinet's `layout` helper (see layout-quiet).
 *
 * `layout` places the cabinet's windows (start, sidebar, window switcher, loading overlay) and
 * starts their programs. On the modern runtime it crashes inside the cabinet's liblogging while
 * formatting one of its log lines, which can leave the sidebar unstarted or the windows unmoved.
 * Its log lines are not needed, so Logger::Log does nothing in this one program.
 */
void _ZN6Logger3LogEPKcS1_mmNS_9LOG_LEVELENS_10LOG_READERENS_8LOG_TYPEES1_S1_bS1_z(void) {}
