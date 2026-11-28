#include "enable.h"

#include "class_guard.h"
#include "main.h"
#include <lpac/utils.h>

#include <euicc/es10c.h>

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static int applet_main(int argc, char **argv) {
    int ret;
    const char *param;
    int force = 0;
    bool refreshflag = false;

    if (argc < 2) {
        printf("Usage: %s [iccid/aid] [refreshflag]\n", argv[0]);
        printf("\t[refreshflag]: optional\n");
        return -1;
    }

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--force") == 0) {
            force = 1;
        }
    }
    param = argv[1];

    if (argc > 2)
        refreshflag = str_to_bool(argv[2]) == true;

    {
        const char *class_str = NULL;
        int guard = profile_class_check(param, force, &class_str);
        if (guard == 1) {
            fprintf(stderr, "refusing: %s is a Provisioning Profile (SGP.22 3.2.4: not selectable; --force overrides)\n", param);
            return -1;
        }
    }
    ret = es10c_enable_profile(&euicc_ctx, param, refreshflag);

    if (ret) {
        const char *reason;
        switch (ret) {
        case 1:
            reason = "iccid or aid not found";
            break;
        case 2:
            reason = "profile not in disabled state";
            break;
        case 3:
            reason = "disallowed by policy";
            break;
        case 4:
            reason = "wrong profile reenabling";
            break;
        case -1:
            reason = "internal error, maybe illegal iccid/aid coding";
            break;
        default:
            reason = "unknown";
            break;
        }
        jprint_error("es10c_enable_profile", reason);
        return -1;
    }

    jprint_success(NULL);

    return 0;
}

struct applet_entry applet_profile_enable = {
    .name = "enable",
    .main = applet_main,
};
