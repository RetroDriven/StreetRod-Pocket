/* Street Rod entry point. Original DOS files are supplied by the user. */
#include "of.h"
#include "engine/mem.h"
#include "engine/modules.h"
#include "engine/host.h"
#include "engine/platform/ega.h"
#include "engine/platform/platform.h"
#include "engine/game/game.h"
#include <stdio.h>
#include <unistd.h>
#include "pocket_exit_screen.h"
#include "srp_save_id.h"

__attribute__((noreturn)) void srp_pocket_quit_now(void) {
    printf("[SRP-EXIT] Pocket quit handler invoked\n");
    host_shutdown();
    srp_show_exit_screen();
    for (;;) usleep(100000);
}


int main(void) {

    uint32_t sr_slot=0, se_slot=0;
    int sr_found=of_file_slot_find("SR.EXE", &sr_slot)==0 && sr_slot==7;
    int se_found=of_file_slot_find("SRSE.EXE", &se_slot)==0 && se_slot==7;
    printf("[SRP-INSTANCE] APF slot7 discovery SR=%d SE=%d\n",sr_found,se_found);
    srp_save_set_instance(sr_found&&!se_found?SRP_GAME_ORIGINAL:
                          se_found&&!sr_found?SRP_GAME_SE:SRP_GAME_UNRESOLVED);
    of_file_slot_register(4, "LIB1");
    of_file_slot_register(5, "LIB2");
    of_file_slot_register(6, "HOT_DATA");
    of_file_slot_register(7, "SR.EXE");

    of_video_init();
    printf("RetroDriven Street Rod Pocket v1.0.0: loading selected DOS game executable\n");
    char error[256] = {0};
    if(!mem_load_exe("SR.EXE", error, sizeof(error))) {
        printf("DOS EXE LOAD FAILED: %s\n",error);
        host_init(".",1,true);
        host_fatal("Selected DOS executable unavailable (original SR.EXE or SE SRSE.EXE): %s",error);
    }
    printf("SR.EXE loaded, memory image %lu bytes\n", (unsigned long)mem_image_size);
    if(!host_init(".",1,true))host_fatal("Pocket host initialization failed");
    ega_init();
    modules_init();
    char *argv[] = {"streetrod",NULL};
    printf("Initializing original Street Rod game state\n");
    platform_main_init(1,argv);
    printf("Entering original Street Rod game loop\n");
    int rc = game_main();
    /* Return to a framebuffer exit screen when the DOS game exits. */
    printf("[SRP-EXIT] game returned rc=%d\n",rc);
    srp_pocket_quit_now();
}
