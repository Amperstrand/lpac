#include "class_guard.h"
#include "main.h"

#include <euicc/es10c.h>
#include <euicc/tostr.h>
#include <lpac/utils.h>

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static int id_match(const struct es10c_profile_info_list *p, const char *id) {
    if (!id || !p)
        return 0;
    return (p->iccid && strcmp(p->iccid, id) == 0) ||
           (p->isdpAid && strcmp(p->isdpAid, id) == 0);
}

int profile_class_check(const char *id, int allow_force, const char **class_out) {
    struct es10c_profile_info_list *profiles = NULL;
    struct es10c_profile_info_list *rptr;
    int found = 0;

    if (es10c_get_profiles_info(&euicc_ctx, &profiles)) {
        return -1;
    }

    for (rptr = profiles; rptr; rptr = rptr->next) {
        if (id_match(rptr, id)) {
            found = 1;
            if (class_out) {
                *class_out = euicc_profileclass2str(rptr->profileClass);
            }
            if (rptr->profileClass == ES10C_PROFILE_CLASS_PROVISIONING && !allow_force) {
                es10c_profile_info_list_free_all(profiles);
                return 1;
            }
            break;
        }
    }

    es10c_profile_info_list_free_all(profiles);
    return found ? 0 : 0; /* not found → let the card's own error speak */
}
