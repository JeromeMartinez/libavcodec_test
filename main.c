/*
 * Copyright © 2024, Lynne
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Usage: dec_tx_test <input> <vulkan device> <hw decode 0|1> [encode 0|1] [frames]
 *
 * Decodes the first packet of <input> repeatedly (a decode throughput test).
 * Software-decoded frames are uploaded into Vulkan frames; hardware-decoded
 * frames are used as is. With encode=1, every frame is also encoded with
 * ffv1_vulkan and the encoder is drained at the end so the timing is complete.
 */

#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <libavutil/avutil.h>
#include <libavutil/dict.h>
#include <libavutil/time.h>
#include <libavutil/pixdesc.h>
#include <libavutil/hwcontext.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>

#define DEFAULT_MAX_FRAMES 1000

typedef struct EncStats {
    int64_t packets;
    int64_t bytes;
} EncStats;

/* Formats the Vulkan frames context / ffv1_vulkan want instead of the decoder's. */
static enum AVPixelFormat remap_pixfmt(enum AVPixelFormat fmt)
{
    switch (fmt) {
    case AV_PIX_FMT_YUV420P:
        return AV_PIX_FMT_NV12;
    case AV_PIX_FMT_GBRAP16:
        return AV_PIX_FMT_RGBA64;
    case AV_PIX_FMT_GBRP10:
        return AV_PIX_FMT_X2BGR10;
    case AV_PIX_FMT_RGB48LE:
    case AV_PIX_FMT_RGB48BE:
        return AV_PIX_FMT_GBRP16;
    case AV_PIX_FMT_BGR0:
        return AV_PIX_FMT_RGB0;
    default:
        return fmt;
    }
}

static const char *fmt_name(int fmt)
{
    const char *name = av_get_pix_fmt_name((enum AVPixelFormat)fmt);
    return name ? name : "none";
}

static double seconds_since(int64_t start)
{
    return (double)(av_gettime() - start) / 1000000.0;
}

/*
 * Get the next decoded frame, feeding the same packet as many times as the
 * decoder asks for. Handles decoders with delay (EAGAIN on receive) and
 * decoders whose input queue is full (EAGAIN on send) correctly.
 */
static int decode_next(AVCodecContext *dec, const AVPacket *pkt, AVFrame *frame)
{
    for (;;) {
        int ret = avcodec_receive_frame(dec, frame);
        if (ret != AVERROR(EAGAIN))
            return ret;

        ret = avcodec_send_packet(dec, pkt);
        if (ret < 0) {
            /* EAGAIN here would mean both directions are blocked: an API error. */
            fprintf(stderr, "Error submitting a packet for decoding: %s\n",
                    av_err2str(ret));
            return ret;
        }
    }
}

static int receive_packets(AVCodecContext *enc, AVPacket *pkt, EncStats *st)
{
    for (;;) {
        int ret = avcodec_receive_packet(enc, pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            return 0;
        if (ret < 0) {
            fprintf(stderr, "Error receiving encoded packet: %s\n", av_err2str(ret));
            return ret;
        }
        st->packets++;
        st->bytes += pkt->size;
        av_packet_unref(pkt);
    }
}

/*
 * Send one frame (or NULL to flush) and collect every packet that is ready.
 * The frame is submitted exactly once.
 */
static int encode_frame(AVCodecContext *enc, const AVFrame *frame,
                        AVPacket *pkt, EncStats *st)
{
    int ret;

    while ((ret = avcodec_send_frame(enc, frame)) == AVERROR(EAGAIN)) {
        /* Encoder input queue is full: take one packet out, then retry. */
        ret = avcodec_receive_packet(enc, pkt);
        if (ret < 0) {
            fprintf(stderr, "Error receiving encoded packet: %s\n",
                    av_err2str(ret == AVERROR(EAGAIN) ? AVERROR_BUG : ret));
            return ret == AVERROR(EAGAIN) ? AVERROR_BUG : ret;
        }
        st->packets++;
        st->bytes += pkt->size;
        av_packet_unref(pkt);
    }
    if (ret < 0) {
        fprintf(stderr, "Error sending frame for encoding: %s\n", av_err2str(ret));
        return ret;
    }

    /* When flushing, avcodec_receive_packet() blocks until all output is out. */
    return receive_packets(enc, pkt, st);
}

int main(int argc, const char **argv)
{
    AVFormatContext *in_ctx = NULL;
    AVCodecContext *in_avctx = NULL, *out_avctx = NULL;
    AVBufferRef *hw_dev_ref = NULL, *hwfc_ref = NULL;
    AVPacket *pkt = NULL, *out_pkt = NULL;
    AVFrame *frame = NULL, *temp = NULL, *hw_frame = NULL;
    SwsContext *swc = NULL;
    AVDictionary *opts = NULL;
    const AVCodec *in_dec = NULL;
    const AVDictionaryEntry *e = NULL;
    EncStats st = { 0 };
    enum AVPixelFormat upload_fmt = AV_PIX_FMT_NONE;
    int hw_decoding, do_hw_decode, do_encode, max_frames = DEFAULT_MAX_FRAMES;
    int frames_done = 0;
    int64_t time_start;
    double secs;
    int ret, sid;

    if (argc < 4) {
        fprintf(stderr,
                "Usage: %s <input> <vulkan device> <hw decode 0|1> [encode 0|1] [frames]\n"
                "  vulkan device: index or name, \"\" for the default device\n"
                "  frames:        number of frames to process (default %d)\n",
                argv[0], DEFAULT_MAX_FRAMES);
        return 1;
    }
    do_hw_decode = !strcmp(argv[3], "1");
    do_encode    = argc > 4 && !strcmp(argv[4], "1");
    if (argc > 5) {
        max_frames = atoi(argv[5]);
        if (max_frames <= 0) {
            fprintf(stderr, "Invalid frame count: %s\n", argv[5]);
            return 1;
        }
    }

    av_log_set_level(AV_LOG_VERBOSE);

    /* Input */
    ret = avformat_open_input(&in_ctx, argv[1], NULL, NULL);
    if (ret < 0) {
        fprintf(stderr, "Error opening input file %s: %s\n", argv[1], av_err2str(ret));
        goto end;
    }

    ret = avformat_find_stream_info(in_ctx, NULL);
    if (ret < 0) {
        fprintf(stderr, "Error reading stream info: %s\n", av_err2str(ret));
        goto end;
    }

    ret = sid = av_find_best_stream(in_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, &in_dec, 0);
    if (ret < 0) {
        fprintf(stderr, "Error finding a decodable video stream in %s: %s\n",
                argv[1], av_err2str(ret));
        goto end;
    }

    in_avctx = avcodec_alloc_context3(in_dec);
    if (!in_avctx) {
        ret = AVERROR(ENOMEM);
        fprintf(stderr, "Error allocating decoder context\n");
        goto end;
    }

    ret = avcodec_parameters_to_context(in_avctx, in_ctx->streams[sid]->codecpar);
    if (ret < 0) {
        fprintf(stderr, "Error using codec parameters: %s\n", av_err2str(ret));
        goto end;
    }
    in_avctx->pkt_timebase = in_ctx->streams[sid]->time_base;

    /* Vulkan device */
    ret = av_hwdevice_ctx_create(&hw_dev_ref, AV_HWDEVICE_TYPE_VULKAN,
                                 argv[2][0] ? argv[2] : NULL, NULL, 0);
    if (ret < 0) {
        fprintf(stderr, "Error creating Vulkan device: %s\n", av_err2str(ret));
        goto end;
    }

    if (do_hw_decode) {
        /* The default get_format picks the Vulkan hwaccel when one exists. */
        in_avctx->hw_device_ctx = av_buffer_ref(hw_dev_ref);
        if (!in_avctx->hw_device_ctx) {
            ret = AVERROR(ENOMEM);
            goto end;
        }
    }

    ret = avcodec_open2(in_avctx, in_dec, NULL);
    if (ret < 0) {
        fprintf(stderr, "Error opening decoder: %s\n", av_err2str(ret));
        goto end;
    }

    av_dump_format(in_ctx, 0, argv[1], 0);

    pkt      = av_packet_alloc();
    out_pkt  = av_packet_alloc();
    frame    = av_frame_alloc();
    temp     = av_frame_alloc();
    hw_frame = av_frame_alloc();
    if (!pkt || !out_pkt || !frame || !temp || !hw_frame) {
        ret = AVERROR(ENOMEM);
        fprintf(stderr, "Error allocating packets/frames\n");
        goto end;
    }

    /* Read the first packet of the chosen stream. */
    for (;;) {
        ret = av_read_frame(in_ctx, pkt);
        if (ret < 0) {
            fprintf(stderr, "Error reading packet: %s\n", av_err2str(ret));
            goto end;
        }
        if (pkt->stream_index == sid)
            break;
        av_packet_unref(pkt);
    }

    /* Probe: decode one frame to learn what the decoder really outputs. */
    ret = decode_next(in_avctx, pkt, frame);
    if (ret < 0) {
        fprintf(stderr, "Error decoding probe frame: %s\n", av_err2str(ret));
        goto end;
    }

    hw_decoding = frame->hw_frames_ctx != NULL;
    if (hw_decoding) {
        printf("Hardware decoding (%s)\n", fmt_name(frame->format));
        hwfc_ref = av_buffer_ref(frame->hw_frames_ctx);
        if (!hwfc_ref) {
            ret = AVERROR(ENOMEM);
            goto end;
        }
        upload_fmt = ((AVHWFramesContext *)hwfc_ref->data)->sw_format;
    } else {
        AVHWFramesContext *hwfc;

        if (do_hw_decode)
            printf("No Vulkan hwaccel for %s, falling back to software decoding\n",
                   in_dec->name);
        upload_fmt = remap_pixfmt((enum AVPixelFormat)frame->format);
        printf("Software decoding (%s), uploading as %s\n",
               fmt_name(frame->format), fmt_name(upload_fmt));

        hwfc_ref = av_hwframe_ctx_alloc(hw_dev_ref);
        if (!hwfc_ref) {
            ret = AVERROR(ENOMEM);
            goto end;
        }

        hwfc = (AVHWFramesContext *)hwfc_ref->data;
        hwfc->format    = AV_PIX_FMT_VULKAN;
        hwfc->sw_format = upload_fmt;
        hwfc->width     = frame->width;
        hwfc->height    = frame->height;

        ret = av_hwframe_ctx_init(hwfc_ref);
        if (ret < 0) {
            fprintf(stderr, "Error creating Vulkan frames context for %s: %s\n",
                    fmt_name(upload_fmt), av_err2str(ret));
            goto end;
        }

        if (frame->format != upload_fmt) {
            swc = sws_alloc_context();
            if (!swc) {
                ret = AVERROR(ENOMEM);
                goto end;
            }
        }
    }
    av_frame_unref(frame);

    /* Encoder (only when requested) */
    if (do_encode) {
        const AVCodec *out_enc = avcodec_find_encoder_by_name("ffv1_vulkan");
        if (!out_enc) {
            ret = AVERROR_ENCODER_NOT_FOUND;
            fprintf(stderr, "ffv1_vulkan encoder not available in this FFmpeg build\n");
            goto end;
        }

        out_avctx = avcodec_alloc_context3(out_enc);
        if (!out_avctx) {
            ret = AVERROR(ENOMEM);
            goto end;
        }

        out_avctx->time_base     = av_make_q(1, 1);
        out_avctx->width         = ((AVHWFramesContext *)hwfc_ref->data)->width;
        out_avctx->height        = ((AVHWFramesContext *)hwfc_ref->data)->height;
        out_avctx->pix_fmt       = AV_PIX_FMT_VULKAN;
        out_avctx->sw_pix_fmt    = upload_fmt;
        out_avctx->hw_frames_ctx = av_buffer_ref(hwfc_ref);
        out_avctx->hw_device_ctx = av_buffer_ref(hw_dev_ref);
        if (!out_avctx->hw_frames_ctx || !out_avctx->hw_device_ctx) {
            ret = AVERROR(ENOMEM);
            goto end;
        }

        av_dict_set(&opts, "level", "3", 0);
        av_dict_set(&opts, "async_depth", "3", 0);
        ret = avcodec_open2(out_avctx, out_enc, &opts);
        if (ret < 0) {
            fprintf(stderr, "Error initializing encoder: %s\n", av_err2str(ret));
            goto end;
        }
        while ((e = av_dict_iterate(opts, e)))
            fprintf(stderr, "Warning: encoder option '%s' was not used\n", e->key);
    }

    av_log_set_level(AV_LOG_INFO);

    printf("%s %d frames\n", do_encode ? "Decoding and encoding" : "Decoding",
           max_frames);

    time_start = av_gettime();

    for (int i = 0; i < max_frames; i++) {
        AVFrame *src;

        ret = decode_next(in_avctx, pkt, frame);
        if (ret < 0) {
            fprintf(stderr, "Error decoding frame %d: %s\n", i, av_err2str(ret));
            goto end;
        }
        src = frame;

        if (!hw_decoding) {
            if (swc) {
                /* Reuse the conversion buffer unless the upload still holds it. */
                if (!temp->buf[0] || !av_frame_is_writable(temp)) {
                    av_frame_unref(temp);
                    temp->width  = frame->width;
                    temp->height = frame->height;
                    temp->format = upload_fmt;
                    ret = av_frame_get_buffer(temp, 0);
                    if (ret < 0) {
                        fprintf(stderr, "Error allocating temporary frame: %s\n",
                                av_err2str(ret));
                        goto end;
                    }
                }

                ret = sws_scale_frame(swc, temp, frame);
                if (ret < 0) {
                    fprintf(stderr, "Error converting frame: %s\n", av_err2str(ret));
                    goto end;
                }
                src = temp;
            }

            ret = av_hwframe_get_buffer(hwfc_ref, hw_frame, 0);
            if (ret < 0) {
                fprintf(stderr, "Error allocating hardware frame: %s\n", av_err2str(ret));
                goto end;
            }

            ret = av_hwframe_transfer_data(hw_frame, src, 0);
            if (ret < 0) {
                fprintf(stderr, "Error uploading frame: %s\n", av_err2str(ret));
                goto end;
            }
            ret = av_frame_copy_props(hw_frame, frame);
            if (ret < 0)
                goto end;
            src = hw_frame;
        }

        if (do_encode) {
            src->pts = i;
            ret = encode_frame(out_avctx, src, out_pkt, &st);
            if (ret < 0)
                goto end;
        }

        av_frame_unref(hw_frame);
        av_frame_unref(frame);

        frames_done = i + 1;
        secs = seconds_since(time_start);
        printf("\rFrames done: %d, fmt: %s, fps: %.2f", frames_done,
               fmt_name(hw_decoding ? upload_fmt : in_avctx->pix_fmt),
               secs > 0 ? frames_done / secs : 0.0);
        fflush(stdout);
    }

    /* Drain the encoder so the timing includes all frames in flight. */
    if (do_encode) {
        ret = encode_frame(out_avctx, NULL, out_pkt, &st);
        if (ret < 0)
            goto end;
    }

    secs = seconds_since(time_start);
    printf("\nTime = %.3f s; fps = %.2f\n", secs,
           secs > 0 ? frames_done / secs : 0.0);
    if (do_encode)
        printf("Encoded %" PRId64 " packets, %" PRId64 " bytes (%.1f MiB/frame)\n",
               st.packets, st.bytes,
               st.packets ? (double)st.bytes / st.packets / (1024.0 * 1024.0) : 0.0);
    ret = 0;

end:
    if (ret < 0 && frames_done)
        printf("\n");
    sws_free_context(&swc);
    av_dict_free(&opts);
    av_frame_free(&hw_frame);
    av_frame_free(&temp);
    av_frame_free(&frame);
    av_packet_free(&out_pkt);
    av_packet_free(&pkt);
    avcodec_free_context(&out_avctx);
    avcodec_free_context(&in_avctx);
    av_buffer_unref(&hwfc_ref);
    av_buffer_unref(&hw_dev_ref);
    avformat_close_input(&in_ctx);

    if (ret < 0) {
        fprintf(stderr, "Failed: %s\n", av_err2str(ret));
        return 1;
    }
    return 0;
}
