 

#ifndef SRP_POCKET_EXIT_SCREEN_H
#define SRP_POCKET_EXIT_SCREEN_H
#include <stdint.h>
#include <string.h>

static const uint8_t srp_exit_font[26][5] = {
      { 0x7Eu, 0x11u, 0x11u, 0x11u, 0x7Eu },
      { 0x7Fu, 0x49u, 0x49u, 0x49u, 0x36u },
      { 0x3Eu, 0x41u, 0x41u, 0x41u, 0x22u },
      { 0x7Fu, 0x41u, 0x41u, 0x22u, 0x1Cu },
      { 0x7Fu, 0x49u, 0x49u, 0x49u, 0x41u },
      { 0x7Fu, 0x09u, 0x09u, 0x09u, 0x01u },
      { 0x3Eu, 0x41u, 0x49u, 0x49u, 0x7Au },
      { 0x7Fu, 0x08u, 0x08u, 0x08u, 0x7Fu },
      { 0x00u, 0x41u, 0x7Fu, 0x41u, 0x00u },
      { 0x20u, 0x40u, 0x41u, 0x3Fu, 0x01u },
      { 0x7Fu, 0x08u, 0x14u, 0x22u, 0x41u },
      { 0x7Fu, 0x40u, 0x40u, 0x40u, 0x40u },
      { 0x7Fu, 0x02u, 0x0Cu, 0x02u, 0x7Fu },
      { 0x7Fu, 0x04u, 0x08u, 0x10u, 0x7Fu },
      { 0x3Eu, 0x41u, 0x41u, 0x41u, 0x3Eu },
      { 0x7Fu, 0x09u, 0x09u, 0x09u, 0x06u },
      { 0x3Eu, 0x41u, 0x51u, 0x21u, 0x5Eu },
      { 0x7Fu, 0x09u, 0x19u, 0x29u, 0x46u },
      { 0x46u, 0x49u, 0x49u, 0x49u, 0x31u },
      { 0x01u, 0x01u, 0x7Fu, 0x01u, 0x01u },
      { 0x3Fu, 0x40u, 0x40u, 0x40u, 0x3Fu },
      { 0x1Fu, 0x20u, 0x40u, 0x20u, 0x1Fu },
      { 0x3Fu, 0x40u, 0x38u, 0x40u, 0x3Fu },
      { 0x63u, 0x14u, 0x08u, 0x14u, 0x63u },
      { 0x07u, 0x08u, 0x70u, 0x08u, 0x07u },
      { 0x61u, 0x51u, 0x49u, 0x45u, 0x43u },
};

/* Scale-2 pixels; bounds are fixed to the Pocket's 320x240 surface. */
static void srp_exit_text(uint8_t *fb, int y, const char *s, uint8_t color) {
    int count = (int)strlen(s);
    int x = (320 - count * 12) / 2;
    for (int i = 0; i < count; ++i, x += 12) {
        char c = s[i];
        if (c < 'A' || c > 'Z') continue;
        const uint8_t *columns = srp_exit_font[c-'A'];
        for (int dx = 0; dx < 5; ++dx) {
            for (int dy = 0; dy < 7; ++dy) {
                if (!(columns[dx] & (1u << dy))) continue;
                int px = x + dx * 2;
                int py = y + dy * 2;
                if (px < 0 || px >= 319 || py < 0 || py >= 239) continue;
                fb[py*320 + px]       = color;
                fb[py*320 + px + 1]   = color;
                fb[(py+1)*320 + px]   = color;
                fb[(py+1)*320 + px+1] = color;
            }
        }
    }
}

static void srp_show_exit_screen(void) {
    of_video_set_display_mode(1); /* framebuffer, not debug console */
    of_video_palette(0, 0x0B1018u);  
    of_video_palette(1, 0xE7EAF0u);  
    of_video_palette(2, 0xED4F44u);  
    uint8_t *fb = of_video_surface();
    if (!fb) return;
    memset(fb, 0, 320u * 240u);
    for (int x=48; x<272; ++x) {
        fb[59*320+x] = 2;
        fb[60*320+x] = 2;
    }
    srp_exit_text(fb, 78, "RETRODRIVEN", 2);
    srp_exit_text(fb, 104, "STREET ROD", 1);
    srp_exit_text(fb, 146, "GAME SESSION ENDED", 1);
    srp_exit_text(fb, 178, "PRESS ANALOGUE FOR MENU", 1);
    srp_exit_text(fb, 203, "QUIT OR RESTART CORE", 1);
    of_video_flip();
}
#endif
