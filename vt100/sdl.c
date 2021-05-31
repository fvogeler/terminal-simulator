#include "vt100.h"
#include "xsdl.h"

static SDL_Window *window;
static SDL_Renderer *renderer;
static SDL_Texture *screentex;
static u32 userevent;
static u8 fb[TERMHEIGHT + 1][137];
static u8 a[TERMHEIGHT + 1];
static u8 capslock;
static void (*meta) (void);

static SDL_Window *sound_window = NULL;
static SDL_Renderer *sound_renderer;
static u8 sound_buffer[800];
static int sound_pointer = 0;

#define SERIAL_WIDTH 800
#define SERIAL_SCALE 5
static SDL_Window *serial_window = NULL;
static SDL_Renderer *serial_renderer;
static u8 rx_buffer[SERIAL_WIDTH / SERIAL_SCALE];
static u8 tx_buffer[SERIAL_WIDTH / SERIAL_SCALE];
static int rx_pointer = 0;
static int tx_pointer = 0;

#define EVENT_DISPLAY (userevent + 0)
#define EVENT_RENDER  (userevent + 1)
#define EVENT_SOUND   (userevent + 2)
#define EVENT_RX      (userevent + 3)
#define EVENT_TX      (userevent + 4)

void sdl_sound (u8 *data, int size)
{
  SDL_Event ev;
  SDL_zero (ev);
  ev.type = EVENT_SOUND;
  ev.user.data1 = malloc (size);
  ev.user.data2 = malloc (sizeof size);
  memcpy (ev.user.data1, data, size);
  *(int *)ev.user.data2 = size;
  SDL_PushEvent (&ev);
}

void sdl_rx (u8 *data, int size)
{
  SDL_Event ev;
  SDL_zero (ev);
  ev.type = EVENT_RX;
  ev.user.data1 = malloc (size);
  ev.user.data2 = malloc (sizeof size);
  memcpy (ev.user.data1, data, size);
  *(int *)ev.user.data2 = size;
  SDL_PushEvent (&ev);
}

void sdl_tx (u8 *data, int size)
{
  SDL_Event ev;
  SDL_zero (ev);
  ev.type = EVENT_TX;
  ev.user.data1 = malloc (size);
  ev.user.data2 = malloc (sizeof size);
  memcpy (ev.user.data1, data, size);
  *(int *)ev.user.data2 = size;
  SDL_PushEvent (&ev);
}

static void draw_sound (u8 *data, int *size)
{
  int i;

  if (sound_window == NULL) {
    SDL_CreateWindowAndRenderer (sizeof sound_buffer, 256, 0,
				 &sound_window, &sound_renderer);
    SDL_SetWindowTitle (sound_window, "VT100 Sound");
  }

  SDL_SetRenderDrawColor (sound_renderer, 0, 0, 0, 255);
  SDL_RenderClear (sound_renderer);
  SDL_SetRenderDrawColor (sound_renderer, 0, 255, 0, 255);
  for (i = 0; i < *size; i++) {
    sound_buffer[sound_pointer] = data[i];
    if (sound_buffer[sound_pointer] > 3)
      sound_buffer[sound_pointer] -= rand() & 3;
    sound_pointer++;
    sound_pointer %= sizeof sound_buffer;
  }
  for (i = 0; i < sizeof sound_buffer; i++)
    SDL_RenderDrawPoint (sound_renderer, i, 255 - sound_buffer[i]);
  SDL_SetRenderDrawColor (sound_renderer, 100, 100, 100, 255);
  for (i = 0; i < sizeof sound_buffer; i += 48)
    SDL_RenderDrawLine (sound_renderer, i, 0, i, 255);
  for (i = 0; i < 256; i += 48)
    SDL_RenderDrawLine (sound_renderer, 0, i, sizeof sound_buffer - 1, i);
  SDL_RenderPresent (sound_renderer);
  free (data);
  free (size);
}

static void draw_serial (void)
{
  int i;

  if (serial_window == NULL) {
    SDL_CreateWindowAndRenderer (SERIAL_WIDTH, 256, 0,
				 &serial_window, &serial_renderer);
    SDL_SetWindowTitle (serial_window, "VT100 Serial");
  }

  SDL_SetRenderDrawColor (serial_renderer, 0, 0, 0, 255);
  SDL_RenderClear (serial_renderer);
  SDL_SetRenderDrawColor (serial_renderer, 0, 255, 0, 255);
  for (i = 1; i < sizeof rx_buffer; i++) {
    SDL_RenderDrawLine (serial_renderer,
			SERIAL_SCALE * (i - 1), 20 * rx_buffer[i - 1] + 10,
			SERIAL_SCALE * (i - 1), 20 * rx_buffer[i] + 10);
    SDL_RenderDrawLine (serial_renderer,
			SERIAL_SCALE * (i - 1), 20 * rx_buffer[i] + 10,
			SERIAL_SCALE * i, 20 * rx_buffer[i] + 10);
  }
  for (i = 1; i < sizeof tx_buffer; i++)
    ;
  SDL_SetRenderDrawColor (serial_renderer, 100, 100, 100, 255);
  for (i = 0; i < SERIAL_WIDTH; i += 48)
    SDL_RenderDrawLine (serial_renderer, i, 0, i, 255);
  for (i = 0; i < 256; i += 48)
    SDL_RenderDrawLine (serial_renderer, 0, i, SERIAL_WIDTH - 1, i);
  SDL_RenderPresent (serial_renderer);
}

static void draw_rx (u8 *data, int *size)
{
  int i;
  for (i = 0; i < *size; i++) {
    rx_buffer[rx_pointer] = data[i];
    if (rx_buffer[rx_pointer] > 3)
      rx_buffer[rx_pointer] -= rand() & 3;
    rx_pointer++;
    rx_pointer %= sizeof rx_buffer;
  }
  draw_serial ();
  free (data);
  free (size);
}

static void draw_tx (u8 *data, int *size)
{
  int i;
  for (i = 0; i < *size; i++) {
    tx_buffer[tx_pointer] = data[i];
    if (tx_buffer[tx_pointer] > 3)
      tx_buffer[tx_pointer] -= rand() & 3;
    tx_pointer++;
    tx_pointer %= sizeof tx_buffer;
  }
  draw_serial ();
  free (data);
  free (size);
}

void draw_line (int scroll, int attr, int y, u8 *data)
{
  int i;
  if ((attr | scroll) != a[y] || memcmp (data, fb[y], 80) != 0) {
    a[y] = attr | scroll;
    for (i = 0; i < 132; i++)
      fb[y][i] = data[i];
  }
}

void sdl_refresh (struct draw *data)
{
  SDL_Event ev;
  SDL_zero (ev);
  ev.type = EVENT_DISPLAY;
  ev.user.data1 = data;
  SDL_PushEvent (&ev);
}

static struct draw draw_data;

void sdl_render (int brightness, int columns)
{
  SDL_Event ev;
  draw_data.columns = columns;
  draw_data.brightness = brightness;
  draw_data.renderer = renderer;
  SDL_zero (ev);
  ev.type = EVENT_RENDER;
  ev.user.data1 = &draw_data;
  SDL_PushEvent (&ev);
}

static void draw (struct draw *data)
{
  int scroll;
  int x, y, yy;
  void *pixels;
  u8 *dest;
  int pitch;
  int w;

  if (SDL_LockTexture (screentex, NULL, &pixels, &pitch) != 0) {
    LOG (SDL, "LockTexture error: %s", SDL_GetError ());
    exit (1);
  }

  dest = pixels;
  scroll = data->scroll;
  for (y = yy = 0; y < 240; y++) {
    w = ((a[yy] & 0x60) != 0x60) ? 2 : 1;
    for (x = 0; x < data->columns / w; x++)
      dest = render_video (dest, fb[yy][x], (a[yy] & 0x60) != 0x60, scroll, data);
    dest += 2 * pitch - sizeof (u32) * 800;
    if (data->columns == 132)
      dest += (800 - 132 * 6) * sizeof (u32);
    if ((a[yy] & 0x40) == 0x40 || (y & 1)) {
      scroll++;
      if (scroll == ((a[yy] & 0x60) == 0x20 ? 5 : 10)) {
	yy++;
	if ((a[yy] & 0x60) == 0)
	  scroll = 5;
	else
	  scroll = 0;
      }
    }
  }
  SDL_UnlockTexture (screentex);
  SDL_RenderCopy (renderer, screentex, NULL, NULL);
  SDL_RenderPresent (renderer);
}

static void toggle_fullscreen (void)
{
  Uint32 flags = SDL_GetWindowFlags (window);
  flags ^= SDL_WINDOW_FULLSCREEN_DESKTOP;
  SDL_SetWindowFullscreen (window, flags);
  if (flags & SDL_WINDOW_FULLSCREEN_DESKTOP)
    SDL_ShowCursor (SDL_DISABLE);
  else
    SDL_ShowCursor (SDL_ENABLE);
}

static void mkwindow (SDL_Window **window, SDL_Renderer **renderer,
		      char *title, int width, int height)
{
  if (SDL_CreateWindowAndRenderer (width, height, 0, window, renderer) < 0)
    //panic("SDL_CreateWindowAndRenderer failed: %s\n", SDL_GetError ());
    ;
  SDL_SetWindowTitle (*window, title);
  SDL_SetHint (SDL_HINT_RENDER_SCALE_QUALITY, "linear");
  SDL_SetHint ("SDL_VIDEO_MINIMIZE_ON_FOCUS_LOSS", "0");
}

void sdl_capslock (u8 code)
{
  capslock = code;
}

static void altmode (void)
{
  key_down (0x2A);
  SDL_Delay (50);
  key_up (0x2A);
}

static void nothing (void)
{
}

void sdl_init (int scale, int full)
{
  SDL_Init (SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_AUDIO);
  mkwindow (&window, &renderer, "VT100", WIDTH*scale, HEIGHT*scale);
  if (full)
    toggle_fullscreen ();
  screentex = SDL_CreateTexture (renderer, SDL_PIXELFORMAT_RGB888,
                                 SDL_TEXTUREACCESS_STREAMING, WIDTH, HEIGHT);
  userevent = SDL_RegisterEvents (5);
  memset (a, 0, sizeof a);

  meta = nothing;

  draw_data.brightness = 0x10;
  draw_data.columns = 80;
  draw_data.renderer = renderer;
  reset_render (&draw_data);
}

static int special_key (SDL_KeyboardEvent *ev)
{
  switch (ev->keysym.scancode) {
  case SDL_SCANCODE_F11:
    if (ev->state != SDL_PRESSED)
      return 1;
    if (SDL_GetModState () & KMOD_CTRL)
      exit (0);
    else
      toggle_fullscreen ();
    return 1;
  case SDL_SCANCODE_LALT:
  case SDL_SCANCODE_RALT:
  case SDL_SCANCODE_LGUI:
  case SDL_SCANCODE_RGUI:
    switch (ev->state) {
    case SDL_PRESSED: meta = altmode; break;
    case SDL_RELEASED: meta = nothing; break;
    }
    return 1;
  default:
    return 0;
  }  
}

static u8 keymap (SDL_Scancode key) {
  switch (key) {
  case SDL_SCANCODE_DELETE: return 0x03; // DELETE
  case SDL_SCANCODE_P: return 0x05;
  case SDL_SCANCODE_O: return 0x06;
  case SDL_SCANCODE_Y: return 0x07;
  case SDL_SCANCODE_T: return 0x08;
  case SDL_SCANCODE_W: return 0x09;
  case SDL_SCANCODE_Q: return 0x0A;
  case SDL_SCANCODE_RIGHT: return 0x10;
  case SDL_SCANCODE_RIGHTBRACKET: return 0x14;
  case SDL_SCANCODE_LEFTBRACKET: return 0x15;
  case SDL_SCANCODE_I: return 0x16;
  case SDL_SCANCODE_U: return 0x17;
  case SDL_SCANCODE_R: return 0x18;
  case SDL_SCANCODE_E: return 0x19;
  case SDL_SCANCODE_1: return 0x1A;
  case SDL_SCANCODE_LEFT: return 0x20;
  case SDL_SCANCODE_DOWN: return 0x22;
  case SDL_SCANCODE_PAUSE: return 0x23; // BREAK
  case SDL_SCANCODE_GRAVE: return 0x24;
  case SDL_SCANCODE_MINUS: return 0x25;
  case SDL_SCANCODE_9: return 0x26;
  case SDL_SCANCODE_7: return 0x27;
  case SDL_SCANCODE_4: return 0x28;
  case SDL_SCANCODE_3: return 0x29;
  case SDL_SCANCODE_ESCAPE: return 0x2A;
  case SDL_SCANCODE_UP: return 0x30;
  case SDL_SCANCODE_F3: return 0x31; // PF3
  case SDL_SCANCODE_F1: return 0x32; // PF1
  case SDL_SCANCODE_BACKSPACE: return 0x33; // BACKSPACE
  case SDL_SCANCODE_EQUALS: return 0x34;
  case SDL_SCANCODE_0: return 0x35;
  case SDL_SCANCODE_8: return 0x36;
  case SDL_SCANCODE_6: return 0x37;
  case SDL_SCANCODE_5: return 0x38;
  case SDL_SCANCODE_2: return 0x39;
  case SDL_SCANCODE_TAB: return 0x3A;
  case SDL_SCANCODE_KP_7: return 0x40;
  case SDL_SCANCODE_F4: return 0x41; // PF4
  case SDL_SCANCODE_F2: return 0x42; // PF2
  case SDL_SCANCODE_KP_0: return 0x43;
  case SDL_SCANCODE_F7: return 0x44; // LINE-FEED
  case SDL_SCANCODE_BACKSLASH: return 0x45;
  case SDL_SCANCODE_L: return 0x46;
  case SDL_SCANCODE_K: return 0x47;
  case SDL_SCANCODE_G: return 0x48;
  case SDL_SCANCODE_F: return 0x49;
  case SDL_SCANCODE_A: return 0x4A;
  case SDL_SCANCODE_KP_8: return 0x50;
  case SDL_SCANCODE_KP_ENTER: return 0x51;
  case SDL_SCANCODE_KP_2: return 0x52;
  case SDL_SCANCODE_KP_1: return 0x53;
  case SDL_SCANCODE_APOSTROPHE: return 0x55;
  case SDL_SCANCODE_SEMICOLON: return 0x56;
  case SDL_SCANCODE_J: return 0x57;
  case SDL_SCANCODE_H: return 0x58;
  case SDL_SCANCODE_D: return 0x59;
  case SDL_SCANCODE_S: return 0x5A;
  case SDL_SCANCODE_KP_PERIOD: return 0x60; // KEYPAD PERIOD
  case SDL_SCANCODE_KP_COMMA: return 0x61; // KEYPAD COMMA
  case SDL_SCANCODE_KP_5: return 0x62;
  case SDL_SCANCODE_KP_4: return 0x63;
  case SDL_SCANCODE_RETURN: return 0x64;
  case SDL_SCANCODE_PERIOD: return 0x65;
  case SDL_SCANCODE_COMMA: return 0x66;
  case SDL_SCANCODE_N: return 0x67;
  case SDL_SCANCODE_B: return 0x68;
  case SDL_SCANCODE_X: return 0x69;
  case SDL_SCANCODE_SCROLLLOCK: return 0x6A; // NO-SCROLL
  case SDL_SCANCODE_KP_9: return 0x70;
  case SDL_SCANCODE_KP_3: return 0x71;
  case SDL_SCANCODE_KP_6: return 0x72;
  case SDL_SCANCODE_KP_MINUS: return 0x73; // KEYPAD MINUS
  case SDL_SCANCODE_SLASH: return 0x75;
  case SDL_SCANCODE_M: return 0x76;
  case SDL_SCANCODE_SPACE: return 0x77;
  case SDL_SCANCODE_V: return 0x78;
  case SDL_SCANCODE_C: return 0x79;
  case SDL_SCANCODE_Z: return 0x7A;
  case SDL_SCANCODE_F9: return 0x7B; // SETUP
  case SDL_SCANCODE_RCTRL:
  case SDL_SCANCODE_LCTRL: return 0x7C;
  case SDL_SCANCODE_RSHIFT:
  case SDL_SCANCODE_LSHIFT: return 0x7D;
  case SDL_SCANCODE_CAPSLOCK: return capslock;
  default: return 0;
  }
}

void sdl_loop (void)
{
  SDL_Event ev;
  u8 code;

  while (SDL_WaitEvent(&ev) >= 0) {
    switch (ev.type) {
    case SDL_QUIT:
      exit (0);

    case SDL_KEYDOWN:
      if (ev.key.repeat)
        break;
      if (special_key (&ev.key))
        break;
      code = keymap (ev.key.keysym.scancode);
      if (code == 0)
        break;
      meta ();
      key_down (code);
      break;
    case SDL_KEYUP:
      if (special_key (&ev.key))
        break;
      key_up (keymap (ev.key.keysym.scancode));
      break;

    default:
      if (ev.type == EVENT_DISPLAY)
	draw (ev.user.data1);
      else if (ev.type == EVENT_RENDER)
	reset_render (ev.user.data1);
      else if (ev.type == EVENT_SOUND)
	draw_sound (ev.user.data1, ev.user.data2);
      else if (ev.type == EVENT_RX)
	draw_rx (ev.user.data1, ev.user.data2);
      else if (ev.type == EVENT_TX)
	draw_tx (ev.user.data1, ev.user.data2);
      break;
    }
  }
}
