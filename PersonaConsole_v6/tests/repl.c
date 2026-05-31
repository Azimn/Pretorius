/* repl.c — interactive harness for the persona engine (v2).
 * Commands:
 *   :dump            — full state dump (drives, mood, embodiment, affect, plan)
 *   :trace           — sparkline trace of last 64 turns
 *   :plan            — most recent UtterancePlan
 *   :save            — flush state/memory/relation
 *   :user <id>       — switch interlocutor
 *   :delay           — toggle whether we actually sleep the schedule delay
 *   :quit            — exit
 */
#define _DEFAULT_SOURCE 1
#include "persona.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>

static void rstrip(char *s){
    size_t L = strlen(s);
    while (L && (s[L-1] == '\n' || s[L-1] == '\r' || s[L-1] == ' ')) s[--L] = 0;
}

int main(int argc, char **argv){
    if (argc < 2){
        fprintf(stderr, "usage: %s <character_dir> [user_id]\n", argv[0]);
        return 1;
    }
    char char_dir_buf[256];
    const char *char_dir = char_dir_buf;
    char user_id[64];
    snprintf(char_dir_buf, sizeof(char_dir_buf), "%s", argv[1]);
    snprintf(user_id, sizeof(user_id), "%s", argc > 2 ? argv[2] : "Someone");

    static Engine eng;  /* large struct — put on .bss */
    int rc = persona_open(&eng, char_dir);
    if (rc != 0){
        fprintf(stderr, "persona_open failed: %d\n", rc);
        return 1;
    }
    persona_set_user(&eng, user_id);

    fprintf(stderr, "[persona-repl] %s loaded; today=%s; speaking with %s\n",
            eng.identity.character_name,
            eng.todays.entries[eng.state.today_index].label,
            user_id);
    fprintf(stderr, "[type :help for commands]\n\n");

    int use_delay = 0;
    char line[1024];
    char out[1024];

    for (;;){
        printf("> ");
        fflush(stdout);
        if (!fgets(line, sizeof(line), stdin)) break;
        rstrip(line);
        if (!line[0]) continue;

        if (line[0] == ':'){
            if (!strcmp(line, ":quit") || !strcmp(line, ":q")) break;
            if (!strcmp(line, ":dump"))  { persona_debug_dump(&eng); continue; }
            if (!strcmp(line, ":trace")) { persona_trace_dump(&eng); continue; }
            if (!strcmp(line, ":plan"))  { persona_plan_dump(&eng);  continue; }
            if (!strcmp(line, ":save"))  { persona_save(&eng); fprintf(stderr, "[saved]\n"); continue; }
            if (!strcmp(line, ":delay")) { use_delay = !use_delay;
                                            fprintf(stderr, "[delay=%s]\n", use_delay ? "on":"off"); continue; }
            if (!strncmp(line, ":user ", 6)){
                size_t L = strlen(line + 6);
                if (L >= sizeof(user_id)) L = sizeof(user_id) - 1;
                memcpy(user_id, line + 6, L); user_id[L] = 0;
                persona_set_user(&eng, user_id);
                fprintf(stderr, "[switched to %s, disposition=%d, tags=0x%02x]\n",
                        user_id, eng.relation.disposition, eng.relation.tags);
                continue;
            }
            if (!strncmp(line, ":load ", 6)){
                static Engine next;
                int lrc;
                persona_save(&eng);
                persona_close(&eng);
                lrc = persona_open(&next, line + 6);
                if (lrc != 0){
                    fprintf(stderr, "[load failed: %d]\n", lrc);
                    lrc = persona_open(&eng, char_dir);
                    if (lrc != 0) return 1;
                } else {
                    eng = next;
                    snprintf(char_dir_buf, sizeof(char_dir_buf), "%s", line + 6);
                }
                persona_set_user(&eng, user_id);
                fprintf(stderr, "[loaded %s; today=%s; lm=%lu bytes; speaking with %s]\n",
                        eng.identity.character_name,
                        eng.todays.entries[eng.state.today_index].label,
                        (unsigned long)eng.lm_size,
                        user_id);
                continue;
            }
            if (!strcmp(line, ":help")){
                fprintf(stderr, "  :dump   full state dump\n"
                                "  :trace  sparkline of last 64 turns\n"
                                "  :plan   most recent UtterancePlan\n"
                                "  :save   flush state\n"
                                "  :user X switch interlocutor\n"
                                "  :load X switch cartridge/directory\n"
                                "  :delay  toggle response delay\n"
                                "  :quit\n");
                continue;
            }
            fprintf(stderr, "[unknown command]\n"); continue;
        }

        int n = persona_process_input(&eng, user_id, line, out, sizeof(out));
        if (n < 0) { fprintf(stderr, "[process_input error]\n"); continue; }

        if (use_delay && eng.scheduled_delay_ms > 0) usleep(eng.scheduled_delay_ms * 1000);
        printf("%s: %s\n\n",
               eng.identity.character_name[0] ? eng.identity.character_name : "engine",
               out);
    }
    persona_save(&eng);
    persona_close(&eng);
    fprintf(stderr, "[exit]\n");
    return 0;
}
