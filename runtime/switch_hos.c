/* GPL-2.0-or-later */
/* Switch HOS storage: title RomFS for read-only game data, HOS SaveData for
 * writable state.  Outside __SWITCH__ this file compiles to no-ops so host
 * builds and tests are unaffected. */
#include "switch_hos.h"

#ifdef __SWITCH__

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <switch.h>

int switch_romfs_active = 0;
int switch_save_active = 0;
const char *switch_save_reason = "";

void switch_hos_commit(void){
    if(!switch_save_active)return;
    fsdevCommitDevice("save");
}

/* Preselected user; an all-zero uid selects common save data when no account
 * is available. */
static AccountUid preselected_user(void){
    AccountUid uid={0};
    Result rc=accountInitialize(AccountServiceType_Application);
    if(R_SUCCEEDED(rc)){
        rc=accountGetPreselectedUser(&uid);
        accountExit();
    }
    return R_SUCCEEDED(rc)?uid:(AccountUid){0};
}

void switch_hos_init(char *data_dir,size_t data_dir_size,char *save_dir,size_t save_dir_size){
    switch_romfs_active=0;
    switch_save_active=0;
    switch_save_reason="";
    /* The SD card stays reachable for the homebrew layout; harmless otherwise. */
    fsdevMountSdmc();
    /* Only an installed title owns a RomFS.  Under hbmenu there is none, so the
       caller's SD-card directories are left exactly as they were. */
    if(R_SUCCEEDED(romfsMountSelf("romfs"))&&chdir("romfs:/")==0){
        /* The runtime appends "/name" itself, so the prefix carries no trailing
           slash: paths become romfs:/mes.arc, save:/kisaku-slot-.... */
        snprintf(data_dir,data_dir_size,"romfs:");
        switch_romfs_active=1;
        if(R_SUCCEEDED(fsdevMountSaveData("save",FS_SAVEDATA_CURRENT_APPLICATIONID,preselected_user()))){
            snprintf(save_dir,save_dir_size,"save:");
            switch_save_active=1;
            switch_save_reason="save:";
            return;
        }
        /* SaveData unavailable: keep saving on the SD card, but never inside the
           read-only RomFS.  Create the directory here so a first run cannot fail
           on a missing path. */
        mkdir("sdmc:/switch/kisaku",0755);
        snprintf(save_dir,save_dir_size,"sdmc:/switch/kisaku");
        switch_save_reason="sdmc:/switch/kisaku";
    }
}

#else /* !__SWITCH__ */

int switch_romfs_active = 0;
int switch_save_active = 0;
const char *switch_save_reason = "";

void switch_hos_init(char *data_dir, size_t data_dir_size, char *save_dir, size_t save_dir_size){
    (void)data_dir;(void)data_dir_size;(void)save_dir;(void)save_dir_size;
}

void switch_hos_commit(void){}

#endif /* __SWITCH__ */
