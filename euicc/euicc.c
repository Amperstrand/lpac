#include "euicc.private.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define ISD_R_AID "\xA0\x00\x00\x05\x59\x10\x10\xFF\xFF\xFF\xFF\x89\x00\x00\x01\x00"

#define APDU_EUICC_HEADER 0x80, 0xE2
#define APDU_CONTINUE_READ_HEADER 0x80, 0xC0, 0x00, 0x00

/* ISO/IEC 7816-4 5.4.1 channel coding (the same rule upstream wwand's sim.uc
 * implements as channel_cla): channels 0-3 ride the low bits of the first
 * interindustry CLA; channels 4-19 use a FURTHER interindustry CLA
 * (0x40 | channel - 4), keeping the first CLA's class bits (secure
 * messaging/chaining preserved by the caller's base). The old
 * `cla & 0xF0 | (channel & 0xF)` coded channels 4-15 outside SGP.22 Table
 * 47's '80'-'83' set and wrapped channels 16-19 onto OTHER channels
 * ('80'-'83' again) — the APDU then named a channel that was never opened.
 * Found by spec-lab A1-F1 (targets/lpac-sgp22/audits/A1.md). */
static uint8_t es10x_channel_cla(uint8_t base_cla, uint8_t channel) {
    if (channel < 4) {
        return (base_cla & 0xF0) | (channel & 0x03);
    }
    /* the further-CLA family mirrors the base's interindustry (0x40) or
     * proprietary (0xC0) half, per 7816-4 5.4.1's b8 rule */
    return (base_cla & 0x80) | 0x40 | ((base_cla & 0x20) ? 0x20 : 0) | (channel - 4);
}

static int es10x_transmit(struct euicc_ctx *ctx, struct apdu_response *response, struct apdu_request *req,
                          unsigned req_len) {
    req->cla = es10x_channel_cla(req->cla, ctx->apdu._internal.logic_channel);
    return euicc_apdu_transmit(ctx, response, req, req_len);
}

static int es10x_transmit_iter(struct euicc_ctx *ctx, struct apdu_request *req, unsigned req_len,
                               int (*callback)(struct apdu_response *response, void *userdata), void *userdata) {
    struct apdu_request *request = NULL;
    struct apdu_response response;

    if (es10x_transmit(ctx, &response, req, req_len) < 0) {
        return -1;
    }

    do {
        if (response.length > 0) {
            if (callback(&response, userdata) < 0) {
                return -1;
            }
        }

        euicc_apdu_response_free(&response);

        if (response.sw1 == SW1_LAST) {
            int ret;

            if ((ret = euicc_apdu_le(ctx, &request, APDU_CONTINUE_READ_HEADER, response.sw2)) < 0) {
                return -1;
            }

            if (es10x_transmit(ctx, &response, request, ret) < 0) {
                return -1;
            }

            continue;
        } else if ((response.sw1 & 0xF0) == SW1_OK) {
            return 0;
        }

        return -1;
    } while (1);
}

int es10x_command_buildrequest(struct euicc_ctx *ctx, struct apdu_request **request, uint8_t p1, uint8_t p2,
                               const uint8_t *der_req, unsigned req_len) {
    int ret;

    ret = euicc_apdu_lc(ctx, request, APDU_EUICC_HEADER, p1, p2, req_len);
    if (ret < 0)
        return ret;

    memcpy((*request)->data, der_req, req_len);

    return ret;
}

static int es10x_command_buildrequest_continue(struct euicc_ctx *ctx, uint8_t reqseq, struct apdu_request **request,
                                               const uint8_t *der_req, unsigned req_len) {
    return es10x_command_buildrequest(ctx, request, 0x11, reqseq, der_req, req_len);
}

static int es10x_command_buildrequest_last(struct euicc_ctx *ctx, uint8_t reqseq, struct apdu_request **request,
                                           const uint8_t *der_req, unsigned req_len) {
    return es10x_command_buildrequest(ctx, request, 0x91, reqseq, der_req, req_len);
}

int es10x_command_iter(struct euicc_ctx *ctx, const uint8_t *der_req, unsigned req_len,
                       int (*callback)(struct apdu_response *response, void *userdata), void *userdata) {
    int ret, reqseq;
    struct apdu_request *req;
    const uint8_t *req_ptr;

    reqseq = 0;
    req_ptr = der_req;
    while (req_len) {
        uint8_t rlen;
        if (req_len > ctx->es10x_mss) {
            rlen = ctx->es10x_mss;
            ret = es10x_command_buildrequest_continue(ctx, reqseq, &req, req_ptr, rlen);
        } else {
            rlen = req_len;
            ret = es10x_command_buildrequest_last(ctx, reqseq, &req, req_ptr, rlen);
        }
        req_len -= rlen;

        if (ret < 0)
            return -1;

        ret = es10x_transmit_iter(ctx, req, ret, callback, userdata);
        if (ret < 0)
            return -1;

        req_ptr += rlen;
        reqseq++;
    }

    return 0;
}

struct userdata_es10x_command {
    uint8_t *resp;
    unsigned resp_len;
};

static int iter_es10x_command(struct apdu_response *response, void *userdata) {
    struct userdata_es10x_command *ud = (struct userdata_es10x_command *)userdata;
    uint8_t *new_response_data;

    new_response_data = realloc(ud->resp, ud->resp_len + response->length);
    if (!new_response_data) {
        return -1;
    }
    ud->resp = new_response_data;
    memcpy(ud->resp + ud->resp_len, response->data, response->length);
    ud->resp_len += response->length;
    return 0;
}

int es10x_command(struct euicc_ctx *ctx, uint8_t **resp, unsigned *resp_len, const uint8_t *der_req, unsigned req_len) {
    int ret = 0;
    struct userdata_es10x_command ud;

    *resp = NULL;
    *resp_len = 0;
    memset(&ud, 0, sizeof(ud));

    ret = es10x_command_iter(ctx, der_req, req_len, iter_es10x_command, &ud);
    if (ret < 0) {
        free(ud.resp);
        return -1;
    }

    *resp = ud.resp;
    *resp_len = ud.resp_len;
    return 0;
}

int euicc_init(struct euicc_ctx *ctx) {
    int ret;

    if (ctx->aid == NULL) {
        ctx->aid = (const uint8_t *)ISD_R_AID;
        ctx->aid_len = sizeof(ISD_R_AID) - 1;
    }

    if (ctx->es10x_mss == 0) {
        /* SGP.22 v2.2 section 2.5.5 (Segmented Bound Profile Package):
         *   "Each segment of this list that is up to 255 bytes is transported
         *    in one APDU. Larger TLVs are sent in blocks of 255 bytes for the
         *    first blocks and a last block that MAY be shorter."
         * The first SBPP segment (BPP tag/length + initialiseSecureChannelRequest,
         * typically 183 bytes = 5-byte header + 178-byte BF23) MUST therefore fit
         * in a single APDU; an MSS of 120 splits it, and spec-conforming eUICCs
         * (SGP.22 section 2.5.6: the ProfileInstallationResult is created "right
         * after the first BPP's TLV executed with error") reject the partial
         * signature with invalidSignature, surfacing in lpac as the
         * es10b_load_bound_profile_package "unknown,unknown" failure
         * (estkme-group/lpac#474, hardware-reproduced on 9eSIM v2).
         *
         * 250 rather than 255: the Windows PC/SC backend narrows the total
         * transmit length (5-byte header + data) through a uint8_t, so 255
         * data bytes become a 4-byte transfer and authentication fails
         * (estkme-group/lpac#473). 250 keeps every driver transportable while
         * covering all observed initial-segment sizes. Transports that can
         * carry the full 255 (e.g. at_csim) may raise it via
         * LPAC_CUSTOM_ES10X_MSS.
         */
        /* 255: the spec's own block boundary (SGP.22 2.5.5: "Larger TLVs are
         * sent in blocks of 255 bytes"), and the body buffer's exact capacity.
         * The estkme-group#473 Windows-PC/SC uint8 total-length truncation
         * (5-byte header + 255 data = 260 → 4) does not affect this fork's
         * deployed transports (at_csim hex-encodes; stdio/AT have no uint8
         * cap); a PC/SC build should set LPAC_CUSTOM_ES10X_MSS=250 until
         * upstream fixes the driver. The prior 250 was a global workaround
         * for a transport-local bug — the spec-lab B2 residue, now closed. */
        ctx->es10x_mss = 255;
    }

    ret = ctx->apdu.interface->connect(ctx);
    if (ret < 0) {
        return -1;
    }

    ret = ctx->apdu.interface->logic_channel_open(ctx, ctx->aid, ctx->aid_len);
    if (ret < 0) {
        ctx->apdu.interface->disconnect(ctx);
        return -1;
    }

    ctx->apdu._internal.logic_channel = ret;

    return 0;
}

void euicc_fini(struct euicc_ctx *ctx) {
    ctx->apdu.interface->logic_channel_close(ctx, ctx->apdu._internal.logic_channel);
    ctx->apdu.interface->disconnect(ctx);
    ctx->apdu._internal.logic_channel = 0;
}

void euicc_http_cleanup(struct euicc_ctx *ctx) {
    free(ctx->http._internal.transaction_id_http);
    free(ctx->http._internal.transaction_id_bin);
    free(ctx->http._internal.b64_euicc_challenge);
    free(ctx->http._internal.b64_euicc_info_1);
    es10b_authenticate_server_param_free(ctx->http._internal.authenticate_server_param);
    free(ctx->http._internal.authenticate_server_param);
    free(ctx->http._internal.b64_authenticate_server_response);
    es10b_prepare_download_param_free(ctx->http._internal.prepare_download_param);
    free(ctx->http._internal.prepare_download_param);
    free(ctx->http._internal.b64_prepare_download_response);
    free(ctx->http._internal.b64_bound_profile_package);
    free(ctx->http._internal.b64_cancel_session_response);
    memset(&ctx->http._internal, 0, sizeof(ctx->http._internal));
}
