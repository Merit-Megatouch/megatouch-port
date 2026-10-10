# The cabinet's console output, minus what is known to be harmless (scripts/loader.sh pipes it
# through here; the full output is kept in the console log). Anything not listed still shows, so a
# new kind of message is never hidden. MEGA_LOADER_VERBOSE=1 skips this filter.

# the cabinet's startup scripts probing hardware a PC doesn't have
/^root mountpoint .* doesn't exist$/ { next }
/^could not read label for device / { next }
/unknown directories method/ { next }
/^magread: no process killed$/ { next }
/^hid_init failed with return code / { next }
/^Starting remote diagnostics:/ { next }
/^Value of mixer control / { next }
/^(Got|Set) rlimit \[/ { next }
/^create symbolic link / { next }
# the language table printed at every start
/^-{20,}$/ { next }
/^ *LANGUAGE INFORMATION *$/ { next }
/^- (AVAILABLE|PLAYER SELECTABLE) LANGUAGES -$/ { next }
/^(INDEX|ID) +ABBREVIATION / { next }
/^ *[0-9]+ +[A-Z]{3} +[A-Z]+ +[a-z]+\/ / { next }
/^(DEFAULT|ACTIVE) LANGUAGE: / { next }
/^PLAYER SELECT OPTION: / { next }
# our stand-ins saying hello
/^\[fakeio\] (firmware started|board .* ready|hotkeys:)/ { next }
/^\[twfake\] reset$/ { next }
/^\[nx\] made generated code/ { next }
/^\[lantap\] eth0 connected/ { next }
# the credit-card reader crashes when it shuts down (there is no card reader); the report and
# its stack lines are expected
/^\[crash\] [A-Za-z0-9_.-]+ \(pid / { skipping = ($2 == "credit_card_reader") }
/^\[crash\]/ { if (skipping) next }
!/^\[crash\]/ { skipping = 0 }
/^ *$/ { next }
{ print; fflush() }
