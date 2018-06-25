//
// Created by cpasjuste on 01/12/16.
//

#include "c2dui.h"
#include "video.h"

using namespace c2d;
using namespace c2dui;

extern bool snes9x_height_extended;

PSNESVideo::PSNESVideo(C2DUIGuiMain *gui, void **_pixels, int *_pitch, const c2d::Vector2f &size)
        : C2DUIVideo(gui, _pixels, _pitch, size) {
    ui = gui;
}

#ifdef __SWITCH__

void PSNESVideo::unlock() {

    unsigned short *tex_buf = (unsigned short *) pixels;
    int x, y, w, h, cx, cy;
    unsigned int p, r, g, b;
    u32 fb_w, fb_h;
    u32 pixel;

    Vector2f screen = ui->getRenderer()->getSize();
    s32 vw = (s32) (screen.x / getScale().x);
    s32 vh = (s32) (screen.y / getScale().y);

    gfxConfigureResolution(vw, vh);
    u32 *fb_buf = (u32 *) gfxGetFramebuffer(&fb_w, &fb_h);

    w = (int) getSize().x;
    h = (int) getSize().y;
    cx = (fb_w - w) / 2;
    cy = (fb_h - h) / 2;
    if (!snes9x_height_extended) {
        cy = (int) ((float) cy * 1.065f);
    }

    //printf("res:%ix%i | fb:%ix%i | tex:%ix%i | scale:%fx%f\n",
    //       vw, vh, fb_w, fb_h, (int) getSize().x, (int) getSize().y, getScale().x, getScale().y);

    int maxY = snes9x_height_extended ? h : (int) ((float) h * 0.935f);

    for (y = 0; y < maxY; y++) {
        for (x = 0; x < w; x++) {

            p = tex_buf[y * w + x];
            r = ((p & 0xf800) >> 11) << 3;
            g = ((p & 0x07e0) >> 5) << 2;
            b = (p & 0x001f) << 3;
            pixel = RGBA8_MAXALPHA(r, g, b);

            fb_buf[(u32) gfxGetFramebufferDisplayOffset((u32) x + cx, (u32) y + cy)] = pixel;
        }
    }

    gfxFlushBuffers();
    gfxSwapBuffers();
    gfxWaitForVsync();
}

#endif

void PSNESVideo::updateScaling() {

    int scale_mode = 0;
    const char *scale_string = ui->getConfig()->getOption(
            ui->getConfig()->getOptions(true), C2DUIOption::Index::ROM_SCALING)->getValue();
    if (strcmp(scale_string, "3X") == 0) {
        scale_mode = 2;
    } else if (strcmp(scale_string, "FIT") == 0) {
        scale_mode = 3;
    } else if (strcmp(scale_string, "FIT 4:3") == 0) {
        scale_mode = 4;
    } else if (strcmp(scale_string, "FULL") == 0) {
        scale_mode = 5;
    }

    Vector2f screen = ui->getRenderer()->getSize();
    Vector2f scale_max;
    float sx = 1, sy = 1;

    scale_max.x = screen.x / getSize().x;
    scale_max.y = screen.y / getSize().y;

    switch (scale_mode) {

        case 2: // 3x (2x software scaling already applied)
            sx = sy = std::min(scale_max.x, 1.5f);
            if (sy > scale_max.y) {
                sx = sy = std::min(scale_max.y, 1.5f);
            }
            break;

        case 3: // fit
            sx = sy = scale_max.y;
            if (sx > scale_max.x) {
                sx = sy = scale_max.x;
            }
            break;

        case 4: // fit 4:3
            sy = scale_max.y;
            sx = std::min(scale_max.x, (sy * getSize().y * 1.33f) / getSize().x);
            break;

        case 5: // fullscreen
            sx = scale_max.x;
            sy = scale_max.y;
            break;

        default:
            break;
    }

    setOriginCenter();
    // remove snes9x border if needed
    float posY = snes9x_height_extended ? screen.y / 2 : (screen.y / 2) * 1.065f;
    float scaleY = snes9x_height_extended ? sy : sy * 1.065f;
    setPosition(screen.x / 2, posY);
    setScale(sx, scaleY);

#ifdef __SWITCH__
    // clear fb before changing res/rot
    clear();
#endif
}
