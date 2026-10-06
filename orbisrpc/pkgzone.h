/* pkgzone.h - name + cover lookup for homebrew titles on pkg-zone.com.
 *
 * Why this exists: Sony's TMDB endpoint only serves retail CUSA titles, so
 * homebrew (LAPY/HT0/AUR0/...) has no on-box source and no CDN entry. TMDB
 * cannot be asked about it at all -- the lookup is 404 by construction.
 *
 * Scope and caveats, deliberately narrow:
 *   - only for titles that are NOT CUSA*, so retail games never touch this
 *   - disabled by nothing: on by default, killed by cfg pkgzone_enabled=0
 *   - pkg-zone.com is a third-party page with no API contract. It is scraped,
 *     not queried, so a layout change breaks it silently. Every failure falls
 *     back to the raw title id rather than blocking the presence.
 *
 * TLS: pkg-zone.com chains to ISRG Root X1, which is already in the curated
 * bundle (scripts/make_ca_bundle.py), so no trust anchor is added. The cover
 * URL is derived from the title id rather than parsed out of the page. */
#ifndef ORBISRPC_PKGZONE_H
#define ORBISRPC_PKGZONE_H

#include <stddef.h>

#define PKGZONE_HOST "pkg-zone.com"
#define PKGZONE_MAX_HTML (192 * 1024)

/* True when this title id is worth asking pkg-zone.com about: anything that is
 * not a retail CUSA id. PS1/PS2 ids are excluded on purpose -- they are handled
 * separately and PKG-Zone's answers for them are less trustworthy. */
int pkgzone_wants(const char *title_id);

/* Pure HTML scrape: copy the first meaningful <h1> text into out and build the
 * cover URL for title_id. Returns 0 on success, -1 when the page has no usable
 * heading. No PS4 dependencies, so the host tests can drive it. */
int pkgzone_parse(const char *html, size_t len, const char *title_id,
                  char *out, size_t cap, char *url, size_t url_cap);

/* Full lookup (socket + TLS + scrape). Returns 0 on a hit. Any failure is
 * non-fatal: the caller keeps whatever name it already had. */
int pkgzone_resolve(const char *title_id, char *name, size_t name_cap,
                    char *url, size_t url_cap);

#endif /* ORBISRPC_PKGZONE_H */