/*
 * The # key as a hotkey (Turbo on a QWERTZ keyboard).
 *
 * The framework's config reader cuts every line at its first '#' (runner/src/util.c,
 * NextLineStripComments), so "Turbo = #" -- which the launcher's hotkey editor writes for that key,
 * from SDL_GetKeyName -- reads as unbound, and no other name reaches SDLK_HASH: the config is read
 * before SDL knows the keyboard layout. Upstream report: docs/bugs-and-todos.md §3, item 11.
 *
 * Without touching the framework: an SDL event filter turns the # key into F13 (no Mac or PC
 * keyboard here has one) before the host, the launcher and the hotkey editor see it. Pressing # in
 * the hotkey editor then writes "F13", which the reader keeps. A config that still says "= #" is
 * rewritten once to "= F13" and its [KeyMap] reloaded.
 */
#include "soe_keys.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "desktop/config.h"
#include "desktop/sdl_compat.h"

#if SNESRECOMP_SDL3
static bool SDLCALL HashKeyFilter(void *user, SDL_Event *e)
{
    (void)user;
    if ((e->type == SDL_EVENT_KEY_DOWN || e->type == SDL_EVENT_KEY_UP) && e->key.key == SDLK_HASH)
        e->key.key = SDLK_F13;
    return true;
}
#else
static int SDLCALL HashKeyFilter(void *user, SDL_Event *e)
{
    (void)user;
    if ((e->type == SDL_KEYDOWN || e->type == SDL_KEYUP) && e->key.keysym.sym == SDLK_HASH)
        e->key.keysym.sym = SDLK_F13;
    return 1;
}
#endif

/* "Turbo = #", "Turbo = Shift+#", "Load = F1,#": a '#' that is a whole key name (after '=', '+' or
 * ',', nothing but spaces after it on the line) becomes "F13". A '#' after a value is a comment. */
static int MigrateLine(const char *line, char *out, size_t size)
{
    const char *eq = strchr(line, '='), *hash = eq ? strchr(eq, '#') : NULL;
    if (!hash || line[0] == '#') return 0;
    const char *p = hash - 1;
    while (p > eq && (*p == ' ' || *p == '\t')) p--;
    if (*p != '=' && *p != '+' && *p != ',') return 0;
    for (const char *q = hash + 1; *q && *q != '\n' && *q != '\r'; q++)
        if (*q != ' ' && *q != '\t') return 0;
    snprintf(out, size, "%.*sF13\n", (int)(hash - line), line);
    return 1;
}

static void MigrateConfig(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return;
    char *text = NULL, line[512];
    size_t length = 0, changed = 0;
    int in_keymap = 0;
    while (fgets(line, sizeof line, f)) {
        char fixed[600];
        const char *use = line;
        if (line[0] == '[') in_keymap = !strncmp(line, "[KeyMap]", 8);
        else if (in_keymap && MigrateLine(line, fixed, sizeof fixed)) use = fixed, changed++;
        size_t n = strlen(use);
        char *grown = realloc(text, length + n + 1);
        if (!grown) { free(text); fclose(f); return; }
        text = grown;
        memcpy(text + length, use, n + 1);
        length += n;
    }
    fclose(f);
    if (changed && (f = fopen(path, "wb"))) {
        fwrite(text, 1, length, f);
        fclose(f);
        fprintf(stderr, "[keys] %s: %zu hotkey(s) on the # key now read F13 (the # key sends F13)\n", path, changed);
        ConfigReloadKeyMap(path);
    }
    free(text);
}

void SoeKeysInstall(void)
{
    SDL_SetEventFilter(HashKeyFilter, NULL);
    MigrateConfig("config.ini");
}
