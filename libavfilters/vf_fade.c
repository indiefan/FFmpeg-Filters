/*
 * video fade filter
 * copyright (c) 2010 Brandon Mintern
 * based heavily on vf_negate.c which is copyright (c) 2007 Bobby Bingham
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/*
 # A few usage examples follow, usable too as test scenarios.

 # Fade in first 30 frames of video
 ffmpeg -i input.avi -vfilters fade=in:0:30 output.avi

 # Fade out last 45 frames of a 200-frame video
 ffmpeg -i input.avi -vfilters fade=out:155:45 output.avi

 # Fade in first 25 frames and fade out last 25 frames of a 1000-frame video
 ffmpeg -i input.avi -vfilters "fade=in:0:25, fade=out:975:25" output.avi

 # Make first 5 frames black, then fade in from frame 5-24
 ffmpeg -i input.avi -vfilters "fade=in:5:20" output.avi
*/

#include "avfilter.h"

typedef struct
{
    int factor, fade_per_frame;
    unsigned int frame_index, start_frame, stop_frame;
    int hsub, vsub, bpp;
} FadeContext;

static av_cold int init(AVFilterContext *ctx, const char *args, void *opaque)
{
    FadeContext *fade = ctx->priv;
    unsigned int frames;
    char in_out[4];

    if(args && sscanf(args, " %3[^:]:%u:%u", in_out,
                   &fade->start_frame, &frames) == 3) {
        frames = frames ? frames : 1;
        fade->fade_per_frame = (1 << 16) / frames;
        if (!strcmp(in_out, "in"))
            fade->factor = 0;
        else if (!strcmp(in_out, "out")) {
            fade->fade_per_frame = -fade->fade_per_frame;
            fade->factor = (1 << 16);
        }
        else {
            av_log(ctx, AV_LOG_ERROR,
                "init() 1st arg must be 'in' or 'out':'%s'\n", in_out);
            return -1;
        }
        fade->stop_frame = fade->start_frame + frames;
        return 0;
    }
    av_log(ctx, AV_LOG_ERROR,
           "init() expected 3 arguments '(in|out):#:#':'%s'\n", args);
    return -1;
}

static int query_formats(AVFilterContext *ctx)
{
    enum PixelFormat pix_fmts[] = {
        PIX_FMT_YUV444P,  PIX_FMT_YUV422P,  PIX_FMT_YUV420P,
        PIX_FMT_YUV411P,  PIX_FMT_YUV410P,
        PIX_FMT_YUVJ444P, PIX_FMT_YUVJ422P, PIX_FMT_YUVJ420P,
        PIX_FMT_YUV440P,  PIX_FMT_YUVJ440P,
        PIX_FMT_RGB24,    PIX_FMT_BGR24,
        PIX_FMT_NONE
    };

    avfilter_set_common_formats(ctx, avfilter_make_format_list(pix_fmts));
    return 0;
}

static int config_props(AVFilterLink *link)
{
    FadeContext *fade = link->dst->priv;
    avcodec_get_chroma_sub_sample(link->format, &fade->hsub, &fade->vsub);
    if (link->format == PIX_FMT_RGB24 || link->format == PIX_FMT_BGR24)
        fade->bpp = 3;
    else
        fade->bpp = 1;
    return 0;
}

static AVFilterBufferRef *get_video_buffer(AVFilterLink *inlink, int perms, int w, int h)
{
    AVFilterBufferRef *picref = avfilter_get_video_buffer(inlink->dst->outputs[0],
                                                       perms, w, h);
    return picref;
}

static void start_frame(AVFilterLink *link, AVFilterBufferRef *picref)
{
    AVFilterBufferRef *outpicref = avfilter_ref_buffer(picref, ~0);

    link->dst->outputs[0]->out_buf = outpicref;

    avfilter_start_frame(link->dst->outputs[0], outpicref);
}

static void end_frame(AVFilterLink *link)
{
    FadeContext *fade = link->dst->priv;

    avfilter_end_frame(link->dst->outputs[0]);
    avfilter_unref_buffer(link->cur_buf);

    if (fade->frame_index >= fade->start_frame &&
        fade->frame_index <= fade->stop_frame)
        fade->factor += fade->fade_per_frame;
    fade->factor = av_clip_uint16(fade->factor);
    fade->frame_index++;
}

static void draw_slice(AVFilterLink *link, int y, int h, int slice_dir)
{
    FadeContext *fade = link->dst->priv;
    AVFilterBufferRef *outpic = link->dst->outputs[0]->out_buf;
    uint8_t *p;
    int i, j, plane;

    if (fade->factor < 65536) {
        /* luma or rgb plane */
        for (i = 0; i < h; i++) {
            p = outpic->data[0] + (y+i) * outpic->linesize[0];
            for (j = 0; j < link->w * fade->bpp; j++) {
                /* fade->factor is using 16 lower-order bits for decimal
                 * places. 32768 = 1 << 15, it is an integer representation
                 * of 0.5 and is for rounding. */
                *p = (*p * fade->factor + 32768) >> 16;
                p++;
            }
        }

        if (outpic->data[0] && outpic->data[1]) {
            /* chroma planes */
            for (plane = 1; plane < 3; plane++) {
                for (i = 0; i < h >> fade->vsub; i++) {
                    p = outpic->data[plane] + ((y+i) >> fade->vsub) * outpic->linesize[plane];
                    for (j = 0; j < link->w >> fade->hsub; j++) {
                        /* 8421367 = ((128 << 1) + 1) << 15. It is an integer
                         * representation of 128.5. The .5 is for rounding
                         * purposes. */
                        *p = ((*p - 128) * fade->factor + 8421367) >> 16;
                        p++;
                    }
                }
            }
        }
    }

    avfilter_draw_slice(link->dst->outputs[0], y, h, slice_dir);
}

AVFilter avfilter_vf_fade =
{
    .name      = "fade",

    .init      = init,

    .priv_size = sizeof(FadeContext),

    .query_formats = query_formats,

    .inputs    = (AVFilterPad[]) {{ .name            = "default",
                                    .type            = CODEC_TYPE_VIDEO,
                                    .get_video_buffer= get_video_buffer,
                                    .start_frame     = start_frame,
                                    .end_frame       = end_frame,
                                    .draw_slice      = draw_slice,
                                    .config_props    = config_props,
                                    .min_perms       = AV_PERM_READ | AV_PERM_WRITE, },
                                  { .name = NULL}},
    .outputs   = (AVFilterPad[]) {{ .name            = "default",
                                    .type            = CODEC_TYPE_VIDEO, },
                                  { .name = NULL}},
};

