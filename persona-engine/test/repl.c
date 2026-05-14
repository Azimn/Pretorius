/* repl.c — interactive harness for the persona engine.
 * Commands:
 *   :dump            — full state dump
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
    const char *char_dir = argv[1];
    char user_id[64];
    snprintf(user_id, sizeof(user_id), "%s", argc > 2 ? argv[2] : "anon");

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
            if (!strcmp(line, ":dump")) { persona_debug_dump(&eng); continue; }
            if (!strcmp(line, ":save")) { persona_save(&eng); fprintf(stderr, "[saved]\n"); continue; }
            if (!strcmp(line, ":delay")){ use_delay = !use_delay;
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
            if (!strcmp(line, ":help")){
                fprintf(stderr, "  :dump   full state dump\n"
                                "  :save   flush state\n"
                                "  :user X switch interlocutor\n"
                                "  :delay  toggle response delay\n"
                                "  :quit\n");
                continue;
            }
            fprintf(stderr, "[unknown command]\n"); continue;
        }

        int n = persona_process_input(&eng, user_id, line, out, sizeof(out));
        if (n < 0) { fprintf(stderr, "[process_input error]\n"); continue; }

        if (use_delay && eng.scheduled_delay_ms > 0) usleep(eng.scheduled_delay_ms * 1000);
        printf("Pretorius: %s\n\n", out);
    }
    persona_save(&eng);
    fprintf(stderr, "[exit]\n");
    return 0;
}
