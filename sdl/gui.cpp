/*****************************************************************************
  BeagleSNES - Super Nintendo Entertainment System (TM) emulator for the
  BeagleBoard-xM platform.

  See CREDITS file to find the copyright owners of this file.

  GUI front-end code (c) Copyright 2013 Andrew Henderson (hendersa@icculus.org)

  BeagleSNES homepage: http://www.beaglesnes.org/
  
  BeagleSNES is derived from the Snes9x open source emulator:  
  Snes9x homepage: http://www.snes9x.com/

  Permission to use, copy, modify and/or distribute BeagleSNES in both binary
  and source form, for non-commercial purposes, is hereby granted without
  fee, providing that this license information and copyright notice appear
  with all copies and any derived work.

  This software is provided 'as-is', without any express or implied
  warranty. In no event shall the authors be held liable for any damages
  arising from the use of this software or it's derivatives.

  BeagleSNES is freeware for PERSONAL USE only. Commercial users should
  seek permission of the copyright holders first. Commercial use includes,
  but is not limited to, charging money for BeagleSNES or software derived 
  from BeagleSNES, including BeagleSNES or derivatives in commercial game 
  bundles, and/or using BeagleSNES as a promotion for your commercial 
  product.

  The copyright holders request that bug fixes and improvements to the code
  should be forwarded to them so everyone can benefit from the modifications
  in future versions.

  Super NES and Super Nintendo Entertainment System are trademarks of
  Nintendo Co., Limited and its subsidiary companies.
 *****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <SDL.h>
#include <SDL/SDL_image.h>
#include <SDL/SDL_ttf.h>
#include <SDL/SDL_mixer.h>

#include "gui.h"

#if defined(BEAGLEBONE_BLACK)
// USB hotplugging work around hack
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/usbdevice_fs.h>
#endif

#define GRADIENT_Y_POS 55

#define TARGET_FPS 40
#define TIME_PER_FRAME (1000000 / TARGET_FPS)

#define SELECT_BUTTON 8
#define START_BUTTON 9

/* AWH: DEBUG - Comment in for NTSC overscan markers on screen */
/*#define OVERSCAN_TEST 1*/

static int i, retVal, done;
static const char *joystickPath[NUM_JOYSTICKS*2] = {
#if defined(BEAGLEBONE_BLACK)
  /* USB device paths for gamepads plugged into the USB host port */
  "/dev/input/by-path/platform-musb-hdrc.1.auto-usb-0:1:1.0-joystick",
  "DUMMY",
  /* USB device paths for gamepads plugged into a USB hub */
  "/dev/input/by-path/platform-musb-hdrc.1.auto-usb-0:1.1:1.0-joystick",
  "/dev/input/by-path/platform-musb-hdrc.1.auto-usb-0:1.2:1.0-joystick" };
#elif defined(BEAGLEBOARD_XM)
  "/dev/input/by-path/platform-ehci-omap.0-usb-0:2.2:1.0-joystick", 
  "/dev/input/by-path/platform-ehci-omap.0-usb-0:2.4:1.0-joystick" };
#endif
static SDL_Joystick *joystick[NUM_JOYSTICKS] = {NULL, NULL};
int beagleSNESDeviceMap[NUM_JOYSTICKS] = {-1, -1};
int beagleSNESControllerPresent[NUM_JOYSTICKS] = {0, 0};
 

#define JOYSTICK_PLUGGED 1
#define JOYSTICK_UNPLUGGED 0

/* Bitmask of which joysticks are plugged in (1) and which aren't (0) */
static int joystickState = 0;
static int elapsedTime;
static struct timeval startTime, endTime;
static int pressDirection = 0;

gameInfo_t *gameInfo;
int totalGames;

void beagleSNESResetJoysticks(void) {
  joystickState = 0;
  beagleSNESCheckJoysticks();
}

void beagleSNESCheckJoysticks(void) {
  bool update = false;
  bool restartJoysticks = false;
  int oldJoystickState = joystickState;
  joystickState = 0;
  char tempBuf[128];

  /* Check if the joysticks currently exist */
#if defined(BEAGLEBONE_BLACK)
  for (i=0; i < (NUM_JOYSTICKS*2); i++) {
#else
  for (i=0; i < NUM_JOYSTICKS; i++) {
#endif
    update = false;
    retVal = readlink(joystickPath[i], tempBuf, sizeof(tempBuf)-1);
    if ((retVal == -1) /* No joystick */ && 
      ((oldJoystickState >> i) & 0x1) /* Joystick was plugged in */ )
    {
      update = true;
      restartJoysticks = true;
      fprintf(stderr, "Player %d Joystick has been unplugged\n", i+1);
    }
    else if ((retVal != -1) /* Joystick exists */ &&
      !((oldJoystickState >> i) & 0x1) /* Joystick was not plugged in */ ) 
    {
      joystickState |= (JOYSTICK_PLUGGED << i); /* Joystick is now plugged in */
      update = true;
      restartJoysticks = true;
      fprintf(stderr, "Player %d Joystick has been plugged in\n", i+1);
    }
    else 
      joystickState |= ((oldJoystickState >> i) & 0x1) << i;

    if (update) {
#if defined(BEAGLEBONE_BLACK)
    if (retVal != -1) {
      /* Are we updating the gamepad in the USB host port? */
      if (i < NUM_JOYSTICKS) {
        beagleSNESDeviceMap[tempBuf[retVal - 1] - '0'] = i;
        beagleSNESControllerPresent[i] = 1;
      /* Are we updating gamepads plugged into a USB hub? */
      } else {
        beagleSNESDeviceMap[tempBuf[retVal - 1] - '0'] = (i-NUM_JOYSTICKS);
        beagleSNESControllerPresent[(i-NUM_JOYSTICKS)] = 1;
      }     
    }
    else
    {
      /* Are we updating the gamepad in the USB host port? */
      if (i < NUM_JOYSTICKS) {
        beagleSNESDeviceMap[i] = -1;
        beagleSNESControllerPresent[i] = 0;
      /* Are we updating gamepads plugged into a USB hub? */
      } else {
        beagleSNESDeviceMap[(i-NUM_JOYSTICKS)] = -1;
        beagleSNESControllerPresent[(i-NUM_JOYSTICKS)] = 0;
      }
    }
#else
    if (retVal != -1) {
      beagleSNESDeviceMap[tempBuf[retVal - 1] - '0'] = i;
      beagleSNESControllerPresent[i] = 1;
    }
    else {
      beagleSNESDeviceMap[i] = -1;
      beagleSNESControllerPresent[i] = 0;
    }
#endif
    } /* End "update" check */
  }

  /* Restart the joystick subsystem */
  if (restartJoysticks) {
    /* Shut down the joystick subsystem */
    SDL_QuitSubSystem(SDL_INIT_JOYSTICK);

    /* Turn the joystick system back on if we have at least one joystick */
    if (joystickState) {
      SDL_InitSubSystem(SDL_INIT_JOYSTICK);
      for (i=0; i < NUM_JOYSTICKS; i++) {
        joystick[i] = SDL_JoystickOpen(i);
      }
      SDL_JoystickEventState(SDL_ENABLE);
    }
  }
}

void handleJoystickEvent(SDL_Event *event)
{
//fprintf(stderr, "handleJoystickEvent()\n");
  switch(event->type) {
    case SDL_JOYBUTTONDOWN:
    case SDL_JOYBUTTONUP:
//fprintf(stderr, "Button jbutton: %d\n", event->jbutton.which);
      if ((beagleSNESDeviceMap[event->jbutton.which] == 0) && // Gamepad 0
        event->type == SDL_JOYBUTTONDOWN)
      {
        if (acceptButton()) {
          switch(event->jbutton.button)
          {
            case SELECT_BUTTON:
            case START_BUTTON:
              playSelectSnd();
              done = 1;
          }
        }
      }
      break;
    case SDL_JOYAXISMOTION:
      //fprintf(stderr, "SDL_JOYSAXISMOTION for device %d\n", event->jaxis.which);
      if ((beagleSNESDeviceMap[event->jaxis.which] == 0) && // Gamepad 0
        (event->jaxis.axis)) { // Axis 1 (up and down)
        if (event->jaxis.value < 0)
          pressDirection = -1;
          //shiftSelectedGameUp();
        else if (event->jaxis.value > 0)
          pressDirection = 1;
          //shiftSelectedGameDown();
        else if (event->jaxis.value == 0)
          pressDirection = 0;
      }
      break;
  }
}

static void loadGameConfig(void) {
  FILE *config = fopen("games.cfg", "r");
  char buffer[128];
  int currentGame = 0;
  int currentLine = 0;
  int length = 0;

  if (!config) {
    fprintf(stderr, "Unable to open game configuration file game.cfg\n");
    return;
  }

  /* TODO: Make the number of games in the menu variable */
  fgets(buffer, 127, config);
  totalGames = atoi(buffer);
  gameInfo = (gameInfo_t *)calloc(totalGames, sizeof(gameInfo_t));
  for (currentGame = 0; currentGame < totalGames; currentGame++) {
    fgets(gameInfo[currentGame].gameTitle, 63, config);
    length = strlen(gameInfo[currentGame].gameTitle);
    gameInfo[currentGame].gameTitle[length-1] = '\0';

    fgets(gameInfo[currentGame].romFile, 127, config);
    length = strlen(gameInfo[currentGame].romFile);
    gameInfo[currentGame].romFile[length-1] = '\0';
    fgets(buffer, 127, config);
    length = strlen(buffer);
    buffer[length-1] = '\0';
    sprintf(gameInfo[currentGame].boxImage, "boxes/%s", buffer);

    for (currentLine = 0; currentLine < 5; currentLine++) {
      fgets(gameInfo[currentGame].infoText[currentLine], 127, config);
      length = strlen(gameInfo[currentGame].infoText[currentLine]);
      gameInfo[currentGame].infoText[currentLine][length-1] = '\0';

    }
    fgets(gameInfo[currentGame].dateText, 31, config);
    length = strlen(gameInfo[currentGame].dateText);
    gameInfo[currentGame].dateText[length-1] = '\0';

    fgets(gameInfo[currentGame].genreText, 63, config);
    length = strlen(gameInfo[currentGame].genreText);
    gameInfo[currentGame].genreText[length-1] = '\0';

  }

}

static void quit(int rc)
{
  TTF_Quit();
  SDL_Quit();
  exit(rc);
}

int doGui(void)
{
  SDL_Surface *screen;
  int i, k, r, g, b;
  Uint16 pixel;
  SDL_Event event;
  SDL_Rect srcRect;

  SDL_Surface *logo;

  SDL_Surface *gradient;
  SDL_Rect gradientRect = {0, GRADIENT_Y_POS, 0, 0};
  SDL_Rect logoRect = {55, 30, 0, 0};
#if defined(BEAGLEBONE_BLACK)
  static SDL_Rect backgroundRect = {0, 80, 720, 480-80};
#else
  static SDL_Rect backgroundRect = {0, 80, 350, 480-80}; 
#endif 
  // NTSC SDL_Rect overscanLeft={0,0,54, 482};
  // NTSC SDL_Rect overscanRight={720-54,0,54,482};
  // NTSC SDL_Rect overscanTop={0,0,720, 24};
  // NTSC SDL_Rect overscanBottom={0, 482-24,720,24};

  /* Initialize SDL */
  if ( SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_JOYSTICK) < 0 ) {
    fprintf(stderr, "Couldn't initialize SDL: %s\n",SDL_GetError());
    return(1);
  }

  /* Set video mode */
#if defined(BEAGLEBONE_BLACK)
  if ( (screen=SDL_SetVideoMode(720,480,16,0)) == NULL ) {
    fprintf(stderr, "Couldn't set 720x480x16 video mode: %s\n",
#else
  if ( (screen=SDL_SetVideoMode(640,480,16,0)) == NULL ) {
    fprintf(stderr, "Couldn't set 640x480x16 video mode: %s\n",
#endif
      SDL_GetError());
    quit(2);
  }
  /* Shut off the mouse pointer */
  SDL_ShowCursor(SDL_DISABLE);

  logo = IMG_Load("gfx/logo_trans.png");
  gradient = IMG_Load("gfx/gradient.png");

  /* Get SOMETHING on the screen ASAP */
  done = 0;
  SDL_FillRect(screen, NULL, 0x0);
  SDL_BlitSurface(gradient, NULL, screen, &gradientRect);
  SDL_BlitSurface(logo, NULL, screen, &logoRect);
  SDL_UpdateRect(screen, 0, 0, 0, 0);

  /* Init font engine */
  if(TTF_Init()==-1) {
    fprintf(stderr, "TTF_Init: %s\n", TTF_GetError());
    quit(2);
  }

  loadGameConfig();
  loadInstruct();
  loadGameList();
  loadGameInfo();

  renderGameList(screen);
  renderGameInfo(screen, currentSelectedGameIndex());
  renderInstruct(screen, beagleSNESControllerPresent[0]);
  SDL_UpdateRect(screen, 0, 0, 0, 0);

  loadAudio();

  /* Check for joysticks */
  beagleSNESCheckJoysticks();

  while ( !done ) {
    gettimeofday(&startTime, NULL);

    renderGameList(screen);
    renderGameInfo(screen, currentSelectedGameIndex());
    renderInstruct(screen, beagleSNESControllerPresent[0]);

#ifdef OVERSCAN_TEST
    SDL_FillRect(screen, &overscanLeft, 0xF800);
    SDL_FillRect(screen, &overscanRight, 0xF800);
    SDL_FillRect(screen, &overscanTop, 0xF800);
    SDL_FillRect(screen, &overscanBottom, 0xF800);
#endif

    incrementGameListFrame();

    // Update panel regions
    // NTSC SDL_UpdateRect(screen, 0, 80, 350, 482 - 80 - 25);
    // NTSC SDL_UpdateRect(screen, 350, 80, 720-350, 482 - 80); 
#if 0 // AWH
    SDL_UpdateRect(screen, 0, 80, 310, 480 - 80);
    SDL_UpdateRect(screen, 310, 80, 640 - 310, 480 - 80); 
#endif
#if defined(BEAGLEBONE_BLACK)
    SDL_UpdateRect(screen, 0, 80, 720, 480 - 80);
#else
    SDL_UpdateRect(screen, 0, 80, 640, 480 - 80);
#endif

    /* Check for events */
    while ( SDL_PollEvent(&event) ) {
      switch (event.type) {
        case SDL_MOUSEBUTTONDOWN:
          break;

        case SDL_JOYBUTTONDOWN:
        case SDL_JOYBUTTONUP:
        case SDL_JOYAXISMOTION:
          handleJoystickEvent(&event);
          break;

        case SDL_KEYDOWN:
          switch (event.key.keysym.sym) {
            case SDLK_UP:
              shiftSelectedGameUp();
              break;
            case SDLK_DOWN:
              shiftSelectedGameDown();
              break;
            default:
              /* Any other key press quits the app... */
              done = 1;
          } // End keydown switch
          break;

        case SDL_QUIT:
          done = 1;
          break;
        default:
          break;
      } // End eventtype switch
    } // End PollEvent loop

    if (pressDirection == -1)
      shiftSelectedGameUp();
    else if (pressDirection == 1)
      shiftSelectedGameDown();

    beagleSNESCheckJoysticks();
    gettimeofday(&endTime, NULL);
    elapsedTime = ((endTime.tv_sec - startTime.tv_sec) * 1000000) + 
      (endTime.tv_usec - startTime.tv_usec);
    if (elapsedTime < TIME_PER_FRAME)
      usleep(TIME_PER_FRAME - elapsedTime);
  } // End while loop

  fadeAudio();
  // Fade out
  for (i = 0; i < 16; i++) {
    SDL_LockSurface(screen);
    for (k = 0; k < (screen->w * screen->h); k++) {
      pixel = ((Uint16 *)(screen->pixels))[k];
      r = (((pixel & 0xF800) >> 11) - 2) << 11;
      g = (((pixel & 0x07E0) >> 5) - 4) << 5;
      b = (pixel & 0x001F) - 2;
      if (r < 0) r = 0;
      if (g < 0) g = 0;
      if (b < 0) b = 0;
      ((Uint16 *)(screen->pixels))[k] = r | g | b; 
    }
    SDL_UnlockSurface(screen);
#if defined(BEAGLEBONE_BLACK)
    SDL_UpdateRect(screen, 0, 0, 720, 480);
#else
    SDL_UpdateRect(screen, 0, 0, 640, 480);
#endif
  }

  // This is our cleanup
  usleep(1000000); // Give the audio fadeout a little time
  SDL_CloseAudio();
  /* AWH: These subsystems will be renabled in the Snes9x init code */
  SDL_QuitSubSystem(SDL_INIT_AUDIO | SDL_INIT_JOYSTICK);

  return(currentSelectedGameIndex());
}

