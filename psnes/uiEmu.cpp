//
// Created by cpasjuste on 01/06/18.
//

#include <snes9x.h>
#include <memmap.h>
#include <apu/apu.h>
#include <controls.h>
#include <filter/blit.h>
#include <conffile.h>
#include <display.h>
#include <sys/stat.h>
#include <cheats.h>

#include "c2dui.h"
#include "uiEmu.h"
#include "video.h"

#ifdef __PSP2__

#include <psp2/io/dirent.h>
#include <psp2/kernel/threadmgr.h>

#define mkdir(x, y) sceIoMkdir(x, 0777)
#endif

using namespace c2d;
using namespace c2dui;

static C2DUIGuiMain *_ui;

typedef void (*Blitter)(uint8 *, int, uint8 *, int, int, int);

enum {
    VIDEOMODE_BLOCKY = 0,
    VIDEOMODE_TV,
    VIDEOMODE_SMOOTH,
    VIDEOMODE_SUPEREAGLE,
    VIDEOMODE_2XSAI,
    VIDEOMODE_SUPER2XSAI,
    VIDEOMODE_EPX,
    VIDEOMODE_HQ2X
};

static uint8 *gfx_snes_buffer;
static uint8 *gfx_video_buffer;
static int snes9x_prev_width = 0, snes9x_prev_height = 0;
bool snes9x_height_extended = false;

static const char *s9x_base_dir = nullptr;

static char default_dir[PATH_MAX + 1];

#ifdef __PSP2__
static const char dirNames[13][32] =
        {
                "ux0:/data/psnes/",             // DEFAULT_DIR
                "ux0:/data/psnes/",             // HOME_DIR
                "ux0:/data/psnes/",             // ROMFILENAME_DIR
                "ux0:/data/psnes/roms",          // ROM_DIR
                "ux0:/data/psnes/sram",         // SRAM_DIR
                "ux0:/data/psnes/saves",        // SNAPSHOT_DIR
                "ux0:/data/psnes/screenshots",  // SCREENSHOT_DIR
                "ux0:/data/psnes/spc",          // SPC_DIR
                "ux0:/data/psnes/cheat",        // CHEAT_DIR
                "ux0:/data/psnes/patch",        // PATCH_DIR
                "ux0:/data/psnes/bios",         // BIOS_DIR
                "ux0:/data/psnes/log",          // LOG_DIR
                ""
        };
#else
static const char dirNames[13][32] =
        {
                "",             // DEFAULT_DIR
                "",             // HOME_DIR
                "",             // ROMFILENAME_DIR
                "roms",          // ROM_DIR
                "sram",         // SRAM_DIR
                "saves",        // SNAPSHOT_DIR
                "screenshots",  // SCREENSHOT_DIR
                "spc",          // SPC_DIR
                "cheat",        // CHEAT_DIR
                "patch",        // PATCH_DIR
                "bios",         // BIOS_DIR
                "log",          // LOG_DIR
                ""
        };
#endif

static int make_snes9x_dirs(void);

#ifndef NOSOUND

static void S9xAudioCallback(void *userdata, Uint8 *stream, int len) {

    //printf("S9xAudioCallback\n");
    _ui->getAudio()->lock();
    S9xMixSamples(stream, len >> (Settings.SixteenBitSound ? 1 : 0));
    _ui->getAudio()->unlock();
}

#else
static uint8 dummy_audio_buffer[128];
#endif

static void S9xSamplesAvailable(void *data) {

    //printf("S9xSamplesAvailable\n");
#ifdef NOSOUND
    S9xMixSamples(dummy_audio_buffer, 128);
    S9xFinalizeSamples();
#else
    _ui->getAudio()->lock();
    S9xFinalizeSamples();
    _ui->getAudio()->unlock();
#endif
}

PSNESGuiEmu::PSNESGuiEmu(C2DUIGuiMain *ui) : C2DUIGuiEmu(ui) {

    printf("PSNESGuiEmu()\n");
    _ui = ui;
}

int PSNESGuiEmu::run(C2DUIRomList::Rom *rom) {

    strncpy(default_dir, getUi()->getConfig()->getHomePath()->c_str(), PATH_MAX);
    s9x_base_dir = default_dir;

    memset(&Settings, 0, sizeof(Settings));
    S9xLoadConfigFiles(nullptr, 0);

    // override S9xLoadConfigFiles
    Settings.MouseMaster = FALSE;
    Settings.SuperScopeMaster = FALSE;
    Settings.JustifierMaster = FALSE;
    Settings.MultiPlayer5Master = FALSE;
    Settings.FrameTimePAL = 20000;
    Settings.FrameTimeNTSC = 16667;
    Settings.SixteenBitSound = TRUE;
    Settings.Stereo = TRUE;
#ifdef __SWITCH__
    Settings.SoundPlaybackRate = 48000;
#else
    Settings.SoundPlaybackRate = 32000;
#endif
    Settings.SoundInputRate = 32000;
    Settings.Transparency = TRUE;
    Settings.AutoDisplayMessages = TRUE;
    Settings.InitialInfoStringTimeout = 120;
    Settings.HDMATimingHack = 100;
    Settings.BlockInvalidVRAMAccessMaster = TRUE;
    Settings.StopEmulation = TRUE;
    Settings.WrongMovieStateProtection = TRUE;
    Settings.DumpStreamsMaxFrames = -1;
    Settings.StretchScreenshots = 1;
    Settings.SnapshotScreenshots = TRUE;
    Settings.SkipFrames = AUTO_FRAMERATE;
    Settings.TurboSkipFrames = 15;
    Settings.CartAName[0] = 0;
    Settings.CartBName[0] = 0;
    Settings.SupportHiRes = TRUE;

    CPU.Flags = 0;

    make_snes9x_dirs();

    if (!Memory.Init() || !S9xInitAPU()) {
        printf("Could not initialize Snes9x Memory.\n");
        stop();
        return -1;
    }

    if (!S9xInitSound(100, 0)) {
        printf("Could not initialize Snes9x Sound.\n");
        stop();
        return -1;
    }
    S9xSetSoundMute(TRUE);

    S9xUnmapAllControls();
    S9xSetController(0, CTL_JOYPAD, 0, 0, 0, 0);
    S9xMapButton(0, S9xGetCommandT("Joypad1 Up"), false);
    S9xMapButton(1, S9xGetCommandT("Joypad1 Down"), false);
    S9xMapButton(2, S9xGetCommandT("Joypad1 Left"), false);
    S9xMapButton(3, S9xGetCommandT("Joypad1 Right"), false);
    S9xMapButton(4, S9xGetCommandT("Joypad1 A"), false);
    S9xMapButton(5, S9xGetCommandT("Joypad1 B"), false);
    S9xMapButton(6, S9xGetCommandT("Joypad1 X"), false);
    S9xMapButton(7, S9xGetCommandT("Joypad1 Y"), false);
    S9xMapButton(8, S9xGetCommandT("Joypad1 L"), false);
    S9xMapButton(9, S9xGetCommandT("Joypad1 R"), false);
    S9xMapButton(10, S9xGetCommandT("Joypad1 Start"), false);
    S9xMapButton(11, S9xGetCommandT("Joypad1 Select"), false);
    S9xSetController(1, CTL_JOYPAD, 1, 0, 0, 0);
    S9xMapButton(12, S9xGetCommandT("Joypad2 Up"), false);
    S9xMapButton(13, S9xGetCommandT("Joypad2 Down"), false);
    S9xMapButton(14, S9xGetCommandT("Joypad2 Left"), false);
    S9xMapButton(15, S9xGetCommandT("Joypad2 Right"), false);
    S9xMapButton(16, S9xGetCommandT("Joypad2 A"), false);
    S9xMapButton(17, S9xGetCommandT("Joypad2 B"), false);
    S9xMapButton(18, S9xGetCommandT("Joypad2 X"), false);
    S9xMapButton(19, S9xGetCommandT("Joypad2 Y"), false);
    S9xMapButton(20, S9xGetCommandT("Joypad2 L"), false);
    S9xMapButton(21, S9xGetCommandT("Joypad2 R"), false);
    S9xMapButton(22, S9xGetCommandT("Joypad2 Start"), false);
    S9xMapButton(23, S9xGetCommandT("Joypad2 Select"), false);
    S9xReportControllers();

    uint32 saved_flags = CPU.Flags;

#ifdef GFX_MULTI_FORMAT
    S9xSetRenderPixelFormat(RGB565);
#endif

    char file[512];
    snprintf(file, 511, "%s%s", getUi()->getConfig()->getRomPath(0), rom->path);
    if (!Memory.LoadROM(file)) {
        printf("Could not open ROM: %s\n", file);
        stop();
        return -1;
    }

    //S9xDeleteCheats();
    //S9xCheatsEnable();
    Memory.LoadSRAM(S9xGetFilename(".srm", SRAM_DIR));
    // TODO
    /*
    if (Settings.ApplyCheats) {
        if (!S9xLoadCheatFile(S9xGetFilename(".bml", CHEAT_DIR)))
            S9xLoadCheatFile(S9xGetFilename(".cht", CHEAT_DIR));
    }
    //S9xParseArgsForCheats(argv, argc);
    */

    CPU.Flags = saved_flags;
    Settings.StopEmulation = FALSE;

    // Initialize filters
    S9xBlitFilterInit();
    S9xBlit2xSaIFilterInit();
    S9xBlitHQ2xFilterInit();

    GFX.Pitch = SNES_WIDTH * 2 * 2;
    gfx_snes_buffer = (uint8 *) malloc(GFX.Pitch * ((SNES_HEIGHT_EXTENDED + 4) * 2));
    memset(gfx_snes_buffer, 0, GFX.Pitch * ((SNES_HEIGHT_EXTENDED + 4) * 2));
    GFX.Screen = (uint16 *) (gfx_snes_buffer + (GFX.Pitch * 2 * 2));

    C2DUIVideo *video = new PSNESVideo(
            getUi(), (void **) &gfx_video_buffer, nullptr, Vector2f(SNES_WIDTH * 2, SNES_HEIGHT_EXTENDED * 2));
    setVideo(video);
    memset(gfx_video_buffer, 0, SNES_WIDTH * 2 * SNES_HEIGHT_EXTENDED * 2 * 2);

    S9xGraphicsInit();
    S9xSetSoundMute(FALSE);

    return C2DUIGuiEmu::run(rom);
}

void PSNESGuiEmu::stop() {

    S9xSetSoundMute(TRUE);
    Settings.StopEmulation = TRUE;

    Memory.SaveSRAM(S9xGetFilename(".srm", SRAM_DIR));
    //S9xResetSaveTimer(FALSE);
    //S9xSaveCheatFile(S9xGetFilename(".bml", CHEAT_DIR));

    S9xBlitFilterDeinit();
    S9xBlit2xSaIFilterDeinit();
    S9xBlitHQ2xFilterDeinit();

    S9xUnmapAllControls();
    S9xGraphicsDeinit();
    Memory.Deinit();
    S9xDeinitAPU();

    free(gfx_snes_buffer);
    snes9x_prev_width = 0;
    snes9x_prev_height = 0;
    snes9x_height_extended = false;

    C2DUIGuiEmu::stop();

#ifndef NOSOUND
    getUi()->removeAudio();
#endif
}

int PSNESGuiEmu::update() {

    if (!isPaused()) {
        S9xMainLoop();
        S9xSetSoundMute(FALSE);
    } else {
        S9xSetSoundMute(TRUE);
    }

    Input::Player *players = getUi()->getInput()->update();

    // process menu
    if ((players[0].state & Input::Key::KEY_START)
        && (players[0].state & Input::Key::KEY_COIN)) {
        pause();
        return UI_KEY_SHOW_MEMU_ROM;
    } else if ((players[0].state & Input::Key::KEY_START)
               && (players[0].state & Input::Key::KEY_FIRE5)) {
        pause();
        return UI_KEY_SHOW_MEMU_STATE;
    } else if (players[0].state & EV_RESIZE) {
        // useful for sdl resize event for example
        getVideo()->updateScaling();
    }

    S9xReportButton(0, (players[0].state & Input::Key::KEY_UP) > 0);
    S9xReportButton(1, (players[0].state & Input::Key::KEY_DOWN) > 0);
    S9xReportButton(2, (players[0].state & Input::Key::KEY_LEFT) > 0);
    S9xReportButton(3, (players[0].state & Input::Key::KEY_RIGHT) > 0);
    S9xReportButton(4, (players[0].state & Input::Key::KEY_FIRE1) > 0);
    S9xReportButton(5, (players[0].state & Input::Key::KEY_FIRE2) > 0);
    S9xReportButton(6, (players[0].state & Input::Key::KEY_FIRE3) > 0);
    S9xReportButton(7, (players[0].state & Input::Key::KEY_FIRE4) > 0);
    S9xReportButton(8, (players[0].state & Input::Key::KEY_FIRE5) > 0);
    S9xReportButton(9, (players[0].state & Input::Key::KEY_FIRE6) > 0);
    S9xReportButton(10, (players[0].state & Input::Key::KEY_START) > 0);
    S9xReportButton(11, (players[0].state & Input::Key::KEY_COIN) > 0);

    S9xReportButton(12, (players[1].state & Input::Key::KEY_UP) > 0);
    S9xReportButton(13, (players[1].state & Input::Key::KEY_DOWN) > 0);
    S9xReportButton(14, (players[1].state & Input::Key::KEY_LEFT) > 0);
    S9xReportButton(15, (players[1].state & Input::Key::KEY_RIGHT) > 0);
    S9xReportButton(16, (players[1].state & Input::Key::KEY_FIRE1) > 0);
    S9xReportButton(17, (players[1].state & Input::Key::KEY_FIRE2) > 0);
    S9xReportButton(18, (players[1].state & Input::Key::KEY_FIRE3) > 0);
    S9xReportButton(19, (players[1].state & Input::Key::KEY_FIRE4) > 0);
    S9xReportButton(20, (players[1].state & Input::Key::KEY_FIRE5) > 0);
    S9xReportButton(21, (players[1].state & Input::Key::KEY_FIRE6) > 0);
    S9xReportButton(22, (players[1].state & Input::Key::KEY_START) > 0);
    S9xReportButton(23, (players[1].state & Input::Key::KEY_COIN) > 0);

    return 0;
}

///////////////////////////////////////////////////////////////////////////////
// Functions called by Snes9x below
///////////////////////////////////////////////////////////////////////////////

/**
 * Called just before Snes9x begins to render an SNES screen.
 * Use this function if you should prepare before drawing, otherwise let it empty.
 */
bool8 S9xInitUpdate() {
    return TRUE;
}

/**
 * Called once a complete SNES screen has been rendered into the GFX.Screen memory buffer,
 * now is your chance to copy the SNES rendered screen to the host computer's screen memory.
 * The problem is that you have to cope with different sized SNES rendered screens:
 * 256*224, 256*239, 512*224, 512*239, 512*448 and 512*478.
 */
bool8 S9xDeinitUpdate(int width, int height) {

    Blitter blit = nullptr;
    int effect = _ui->getConfig()->getValue(C2DUIOption::ROM_SHADER, true);
    // for video.cpp scaling
    snes9x_height_extended = (height == 239 || height == 478);
    C2DUIVideo *video = _ui->getUiEmu()->getVideo();
    if (snes9x_prev_height != height) {
        printf("video->updateScaling()\n");
        video->updateScaling();
    }

    if (effect == VIDEOMODE_BLOCKY || effect == VIDEOMODE_TV || effect == VIDEOMODE_SMOOTH) {
        if ((width <= SNES_WIDTH) && ((snes9x_prev_width != width) || (snes9x_prev_height != height))) {
            printf("S9xBlitClearDelta\n");
            S9xBlitClearDelta();
        }
    }

    if (width <= SNES_WIDTH) {
        if (height > SNES_HEIGHT_EXTENDED) {
            blit = S9xBlitPixSimple2x1;
        } else {
            switch (effect) {
                case VIDEOMODE_TV:
                    blit = S9xBlitPixTV2x2;
                    break;
                case VIDEOMODE_SMOOTH:
                    blit = S9xBlitPixSmooth2x2;
                    break;
                case VIDEOMODE_SUPEREAGLE:
                    blit = S9xBlitPixSuperEagle16;
                    break;
                case VIDEOMODE_2XSAI:
                    blit = S9xBlitPix2xSaI16;
                    break;
                case VIDEOMODE_SUPER2XSAI:
                    blit = S9xBlitPixSuper2xSaI16;
                    break;
                case VIDEOMODE_EPX:
                    blit = S9xBlitPixEPX16;
                    break;
                case VIDEOMODE_HQ2X:
                    blit = S9xBlitPixHQ2x16;
                    break;
                default:
                    blit = S9xBlitPixSimple2x2;
                    break;
            }
        }
    } else if (height <= SNES_HEIGHT_EXTENDED) {
        switch (effect) {
            default:
                blit = S9xBlitPixSimple1x2;
                break;
            case VIDEOMODE_TV:
                blit = S9xBlitPixTV1x2;
                break;
        }
    } else {
        blit = S9xBlitPixSimple1x1;
    }

    blit((uint8 *) GFX.Screen, GFX.Pitch, gfx_video_buffer, video->pitch, width, height);

    if (height < snes9x_prev_height) {
        int p = video->pitch >> 2;
        for (int y = SNES_HEIGHT * 2; y < SNES_HEIGHT_EXTENDED * 2; y++) {
            auto *d = (uint32 *) (gfx_video_buffer + y * video->pitch);
            for (int x = 0; x < p; x++)
                *d++ = 0;
        }
    }

    snes9x_prev_width = width;
    snes9x_prev_height = height;

    video->unlock();
#ifdef __SWITCH__
    _ui->getRenderer()->flip(false);
#else
    _ui->getRenderer()->flip();
#endif

    return TRUE;
}

/**
 * Called at the end of screen refresh if GFX.DoInterlace && GFX.InterlaceFrame == 0 is true (?).
 */
bool8 S9xContinueUpdate(int width, int height) {
    return TRUE;
}

void _splitpath(const char *path, char *drive, char *dir, char *fname, char *ext) {

    *drive = 0;

    const char *slash = strrchr(path, SLASH_CHAR);
    const char *dot = strrchr(path, '.');

    if (dot && slash && dot < slash) {
        dot = nullptr;
    }

    if (!slash) {
        *dir = 0;

        strcpy(fname, path);

        if (dot) {
            fname[dot - path] = 0;
            strcpy(ext, dot + 1);
        } else {
            *ext = 0;
        }
    } else {
        strcpy(dir, path);
        dir[slash - path] = 0;

        strcpy(fname, slash + 1);

        if (dot) {
            fname[dot - slash - 1] = 0;
            strcpy(ext, dot + 1);
        } else {
            *ext = 0;
        }
    }
}

void _makepath(char *path, const char *, const char *dir, const char *fname, const char *ext) {

    if (dir && *dir) {
        strcpy(path, dir);
        strcat(path, SLASH_STR);
    } else {
        *path = 0;
    }

    strcat(path, fname);

    if (ext && *ext) {
        strcat(path, ".");
        strcat(path, ext);
    }
}

/**
 * Display port-specific usage information
 */
void S9xExtraUsage() {
}

/**
 * Parse port-specific arguments
 */
void S9xParseArg(char **argv, int &i, int argc) {
}

/**
 * Parse port-specific config
 */
void S9xParsePortConfig(ConfigFile &conf, int pass) {
}

static int make_snes9x_dirs() {

    if (strlen(s9x_base_dir) + 1 + sizeof(dirNames[0]) > PATH_MAX + 1)
        return (-1);

    mkdir(s9x_base_dir, 0755);

    for (int i = 0; i < LAST_DIR; i++) {
        if (dirNames[i][0]) {
            char s[PATH_MAX + 1];
            snprintf(s, PATH_MAX + 1, "%s%s%s", s9x_base_dir, SLASH_STR, dirNames[i]);
            mkdir(s, 0755);
        }
    }

    return (0);
}

/**
 * Called when Snes9x wants to know the directory dirtype.
 */
const char *S9xGetDirectory(s9x_getdirtype dirtype) {

    static char s[PATH_MAX + 1];

    if (dirNames[dirtype][0])
        snprintf(s, PATH_MAX + 1, "%s%s%s", s9x_base_dir, SLASH_STR, dirNames[dirtype]);
    else {
        switch (dirtype) {

            case DEFAULT_DIR:
                strncpy(s, s9x_base_dir, PATH_MAX + 1);
                s[PATH_MAX] = 0;
                break;

            case HOME_DIR:
                strncpy(s, s9x_base_dir, PATH_MAX + 1);
                s[PATH_MAX] = 0;
                break;

            case ROMFILENAME_DIR:
                strncpy(s, Memory.ROMFilename, PATH_MAX + 1);
                s[PATH_MAX] = 0;

                for (int i = (int) strlen(s); i >= 0; i--) {
                    if (s[i] == SLASH_CHAR) {
                        s[i] = 0;
                        break;
                    }
                }
                break;

            default:
                s[0] = 0;
                break;
        }
    }

    return (s);
}

/**
 * When Snes9x needs to know the name of the cheat/IPS file and so on, this function is called.
 * Check extension and dirtype, and return the appropriate filename.
 * The current ports return the ROM file path with the given extension.
 */
const char *S9xGetFilename(const char *ex, s9x_getdirtype dirtype) {

    static char s[PATH_MAX + 1];
    char drive[_MAX_DRIVE + 1], dir[_MAX_DIR + 1], fname[_MAX_FNAME + 1], ext[_MAX_EXT + 1];

    _splitpath(Memory.ROMFilename, drive, dir, fname, ext);
    snprintf(s, PATH_MAX + 1, "%s%s%s%s", S9xGetDirectory(dirtype), SLASH_STR, fname, ex);

    return (s);
}

/**
 * Almost the same as S9xGetFilename function, but used for saving SPC files etc.
 * So you have to take care not to delete the previously saved file, by increasing
 * the number of the filename; romname.000.spc, romname.001.spc, ...
 */
const char *S9xGetFilenameInc(const char *ex, s9x_getdirtype dirtype) {

    static char s[PATH_MAX + 1];
    char drive[_MAX_DRIVE + 1], dir[_MAX_DIR + 1], fname[_MAX_FNAME + 1], ext[_MAX_EXT + 1];

    unsigned int i = 0;
    const char *d;
    struct stat buf = {};

    _splitpath(Memory.ROMFilename, drive, dir, fname, ext);
    d = S9xGetDirectory(dirtype);

    do
        snprintf(s, PATH_MAX + 1, "%s%s%s.%03d%s", d, SLASH_STR, fname, i++, ex);
    while (stat(s, &buf) == 0 && i < 1000);

    return (s);

}

/**
 * Called when Snes9x wants to know the name of the ROM image.
 * Typically, extract the filename from path and return it.
 */
const char *S9xBasename(const char *f) {

    const char *p;

    if ((p = strrchr(f, '/')) != NULL || (p = strrchr(f, '\\')) != NULL)
        return (p + 1);

    return (f);
}

/**
 * If your port can match Snes9x's built-in LoadFreezeFile/SaveFreezeFile command
 * (see controls.cpp), you may choose to use this function. Otherwise return NULL.
 */
const char *S9xChooseFilename(bool8 read_only) {
    return nullptr;
}

/**
 * If your port can match Snes9x's built-in BeginRecordingMovie/LoadMovie command
 * (see controls.cpp), you may choose to use this function. Otherwise return NULL.
 */
const char *S9xChooseMovieFilename(bool8 read_only) {
    return nullptr;
}

/**
 * This function opens a freeze-game file. STREAM is defined as a gzFile if ZLIB is
 * defined else it's defined as FILE *. The read_only parameter is set to true when
 * reading a freeze-game file and false when writing a freeze-game file.
 * Open the file filepath and return its pointer file.
 */
bool8 S9xOpenSnapshotFile(const char *filename, bool8 read_only, STREAM*file) {

    if ((*file = OPEN_STREAM(filename, read_only ? "rb" : "wb")))
        return (TRUE);

    printf("S9xOpenSnapshotFile: %s (open failed, read only=%i)\n", filename, read_only);

    return (FALSE);
}

/**
 * This function closes the freeze-game file opened by S9xOpenSnapshotFile function.
 */
void S9xCloseSnapshotFile(STREAM file) {

    printf("S9xCloseSnapshotFile\n");
    CLOSE_STREAM(file);
}

/**
 * If your port can match Snes9x's built-in SoundChannelXXX command (see controls.cpp),
 * you may choose to use this function. Otherwise return NULL.
 * Basically, turn on/off the sound channel c (0-7), and turn on all channels if c is 8.
 */
void S9xToggleSoundChannel(int c) {

    static uint8 sound_switch = 255;

    if (c == 8)
        sound_switch = 255;
    else
        sound_switch ^= 1 << c;
    S9xSetSoundControl(sound_switch);
}

/**
 * If Settings.AutoSaveDelay is not zero, Snes9x calls this function when the contents of
 * the S-RAM has been changed. Typically, call Memory.SaveSRAM function from this function.
 */
void S9xAutoSaveSRAM() {

    Memory.SaveSRAM(S9xGetFilename(".srm", SRAM_DIR));
}

/**
 * Called at the end of S9xMainLoop function, when emulating one frame has been done.
 * You should adjust the frame rate in this function
 */
void S9xSyncSpeed() {

    if (Settings.SoundSync) {
        while (!S9xSyncSound()) {
#ifdef __SWITCH__
            svcSleepThread(1);
#elif __PSP2__
            sceKernelDelayThread(1);
#else
            usleep(0);
#endif
        }
    }

    if (Settings.DumpStreams)
        return;

#if !defined(__SWITCH__) && !defined(__PSP2__) // TODO
    if (Settings.HighSpeedSeek > 0)
        Settings.HighSpeedSeek--;

    if (Settings.TurboMode) {
        if ((++IPPU.FrameSkip >= Settings.TurboSkipFrames) && !Settings.HighSpeedSeek) {
            IPPU.FrameSkip = 0;
            IPPU.SkippedFrames = 0;
            IPPU.RenderThisFrame = TRUE;
        } else {
            IPPU.SkippedFrames++;
            IPPU.RenderThisFrame = FALSE;
        }

        return;
    }

    static struct timeval next1 = {0, 0};
    struct timeval now;

    while (gettimeofday(&now, NULL) == -1);

    // If there is no known "next" frame, initialize it now.
    if (next1.tv_sec == 0) {
        next1 = now;
        next1.tv_usec++;
    }

    // If we're on AUTO_FRAMERATE, we'll display frames always only if there's excess time.
    // Otherwise we'll display the defined amount of frames.
    unsigned limit = (Settings.SkipFrames == AUTO_FRAMERATE) ? (timercmp(&next1, &now, <) ? 10 : 1)
                                                             : Settings.SkipFrames;

    IPPU.RenderThisFrame = (++IPPU.SkippedFrames >= limit) ? TRUE : FALSE;

    if (IPPU.RenderThisFrame)
        IPPU.SkippedFrames = 0;
    else {
        // If we were behind the schedule, check how much it is.
        if (timercmp(&next1, &now, <)) {
            unsigned lag = (now.tv_sec - next1.tv_sec) * 1000000 + now.tv_usec - next1.tv_usec;
            if (lag >= 500000) {
                // More than a half-second behind means probably pause.
                // The next line prevents the magic fast-forward effect.
                next1 = now;
            }
        }
    }

    // Delay until we're completed this frame.
    // Can't use setitimer because the sound code already could be using it. We don't actually need it either.
    while (timercmp(&next1, &now, >)) {
        // If we're ahead of time, sleep a while.
        unsigned timeleft = (next1.tv_sec - now.tv_sec) * 1000000 + next1.tv_usec - now.tv_usec;
#ifdef __SWITCH__
        svcSleepThread(timeleft * 1000);
#elif __PSP2__
        sceKernelDelayThread(timeleft);
#else
        usleep(timeleft);
#endif
        while (gettimeofday(&now, NULL) == -1);
        // Continue with a while-loop because usleep() could be interrupted by a signal.
    }

    // Calculate the timestamp of the next frame.
    next1.tv_usec += Settings.FrameTime;
    if (next1.tv_usec >= 1000000) {
        next1.tv_sec += next1.tv_usec / 1000000;
        next1.tv_usec %= 1000000;
    }
#endif
}

/**
 * Called by Snes9x to poll for buttons that should be polled.
 */
bool S9xPollButton(uint32 id, bool *pressed) {
    return false;
}

/**
 * Called by Snes9x to poll for axises that should be polled.
 */
bool S9xPollAxis(uint32 id, int16 *value) {
    return false;
}

/**
 * Called by Snes9x to poll for poiters that should be polled.
 */
bool S9xPollPointer(uint32 id, int16 *x, int16 *y) {
    return false;
}

/**
 * Handle port-specific commands (?).
 */
void S9xHandlePortCommand(s9xcommand_t cmd, int16 data1, int16 data2) {
}

/**
 * S9xInitSound function calls this function to actually open the host sound device.
 */
bool8 S9xOpenSoundDevice(void) {

#ifdef NOSOUND
    printf("S9xOpenSoundDevice (dummy)\n");
    S9xSetSamplesAvailableCallback(S9xSamplesAvailable, nullptr);
#else
    printf("S9xOpenSoundDevice\n");
    Audio *audio = new C2DAudio(Settings.SoundPlaybackRate, 60, S9xAudioCallback);
    _ui->addAudio(audio);
    S9xSetSamplesAvailableCallback(S9xSamplesAvailable, nullptr);
#endif
    return TRUE;
}

/**
 * Called when some fatal error situation arises or when the “q” debugger command is used.
 */
void S9xExit() {
    _ui->getUiEmu()->stop();
}

/**
 * When Snes9x wants to display an error, information or warning message, it calls this function.
 * Check in messages.h for the types and individual message numbers that Snes9x currently passes as parameters.
 * The idea is display the message string so the user can see it, but you choose not to display anything at all,
 * or change the message based on the message number or message type.
 * Eventually all debug output will also go via this function, trace information already does.
 */
void S9xMessage(int type, int number, const char *message) {
    const int max = 36 * 3;
    static char buffer[max + 1];
    printf("[%i][%i]: %s\n", type, number, message);
    strncpy(buffer, message, max + 1);
    buffer[max] = 0;
    S9xSetInfoString(buffer);
}

/**
 * Used by Snes9x to ask the user for input.
 */
const char *S9xStringInput(const char *message) {
    /*
    static char buffer[256];

    printf("%s: ", message);
    fflush(stdout);

    if (fgets(buffer, sizeof(buffer) - 2, stdin)) {
        return buffer;
    }
    */
    return nullptr;
}

/**
 * Called when the SNES color palette has changed.
 * Use this function if your system should change its color palette to match the SNES's.
 * Otherwise let it empty.
 */
void S9xSetPalette() {
}
