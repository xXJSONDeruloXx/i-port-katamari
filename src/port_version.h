/*
 * The port's release version — the single place it is written down.
 *
 * A field report is only actionable if it says which build produced it. The
 * first muOS log for the GL provider preflight was byte-identical to the log
 * from before the preflight existed, and there was no way to tell whether the
 * user was running the new release or an old one still sitting on the SD card.
 *
 * So the version is defined here and nowhere else:
 *
 *   - the loader prints it as its first trace line, and answers --version;
 *   - the launcher asks the binary (`katamari --version`) instead of carrying
 *     its own copy of the string, so a stale launcher cannot claim a version
 *     the binary is not;
 *   - the packager reads this header when it reports what it built.
 *
 * Bump it here when cutting a release; nothing else needs editing.
 */
#ifndef KATAMARI_PORT_VERSION_H
#define KATAMARI_PORT_VERSION_H

#define KATAMARI_PORT_VERSION "0.1.13"

#endif /* KATAMARI_PORT_VERSION_H */
