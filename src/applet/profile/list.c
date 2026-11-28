#include "list.h"
#include "main.h"

#include <euicc/es10c.h>
#include <euicc/tostr.h>
#include <lpac/utils.h>

#include "class_guard.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int applet_main(int argc, char **argv) {
    int show_all = 0;
    _cleanup_es10c_profile_info_list_ struct es10c_profile_info_list *profiles = NULL;
    struct es10c_profile_info_list *rptr;
    cJSON *jdata = NULL;

    if (es10c_get_profiles_info(&euicc_ctx, &profiles)) {
        jprint_error("es10c_get_profiles_info", NULL);
        return -1;
    }

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--all") == 0) {
            show_all = 1;
        }
    }

    jdata = cJSON_CreateArray();
    rptr = profiles;

    while (rptr) {
        cJSON *jprofile = NULL;

        /* SGP.22 3.2.4 (7259): "It SHALL not include the Provisioning
         * Profiles" in the End-User display — hidden unless --all. */
        if (!show_all && rptr->profileClass == ES10C_PROFILE_CLASS_PROVISIONING) {
            rptr = rptr->next;
            continue;
        }

        jprofile = cJSON_CreateObject();
        cJSON_AddStringOrNullToObject(jprofile, "iccid", rptr->iccid);
        cJSON_AddStringOrNullToObject(jprofile, "isdpAid", rptr->isdpAid);
        cJSON_AddStringOrNullToObject(jprofile, "profileState", euicc_profilestate2str(rptr->profileState));
        cJSON_AddStringOrNullToObject(jprofile, "profileNickname", rptr->profileNickname);
        cJSON_AddStringOrNullToObject(jprofile, "serviceProviderName", rptr->serviceProviderName);
        cJSON_AddStringOrNullToObject(jprofile, "profileName", rptr->profileName);
        cJSON_AddStringOrNullToObject(jprofile, "iconType", euicc_icontype2str(rptr->iconType));
        cJSON_AddStringOrNullToObject(jprofile, "icon", rptr->icon);
        cJSON_AddStringOrNullToObject(jprofile, "profileClass", euicc_profileclass2str(rptr->profileClass));
        cJSON_AddItemToArray(jdata, jprofile);

        rptr = rptr->next;
    }

    jprint_success(jdata);

    return 0;
}

struct applet_entry applet_profile_list = {
    .name = "list",
    .main = applet_main,
};
