#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "memory.h"
#include "cpu.h"
#include "timer.h"
#include "joypad.h"
#include "interrupt.h"
#include "ppu.h"
#include "dbg.h"
#include "apu.h"

uint32_t dbg_mask;

typedef struct {
    mem_t mem; cpu_t cpu; timer_t timer; joypad_t joypad; ppu_t ppu; apu_t apu;
    SDL_Window *w; SDL_Renderer *r; SDL_Texture *t;
    SDL_AudioStream *audio;
    gb_mode_t requested_mode;
    bool rom_loaded, quit, paused;
    int menu_sel; u32 fc, fps_ts;
} gb_t;

static void re(gb_t*g){mem_init(&g->mem);cpu_init(&g->cpu);timer_init(&g->timer);joypad_init(&g->joypad);ppu_init(&g->ppu);apu_init(&g->apu);g->paused=0;}

static const char*mode_name(gb_mode_t mode){
    switch(mode){
        case GB_MODE_DMG:return "dmg";
        case GB_MODE_CGB:return "cgb";
        case GB_MODE_SGB:return "sgb";
        case GB_MODE_AUTO:
        default:return "auto";
    }
}

static bool parse_mode_arg(const char*s,gb_mode_t*out){
    if(!strcmp(s,"dmg")){*out=GB_MODE_DMG;return true;}
    if(!strcmp(s,"cgb")){*out=GB_MODE_CGB;return true;}
    if(!strcmp(s,"sgb")){*out=GB_MODE_SGB;return true;}
    return false;
}

static void lr(gb_t*g,const char*p){
    re(g);g->mem.joypad=&g->joypad;g->mem.timer=&g->timer;g->mem.apu=&g->apu;g->mem.forced_mode=g->requested_mode;
    g->mem.hram[0x36]=0xC9;
    if(!mem_load_rom(&g->mem,p))return;
    g->mem.boot_on=1;cpu_init_boot(&g->cpu);
    g->rom_loaded=1;
    fprintf(stderr,"ROM loaded=1 title='%s' mode=%s%s\n",g->mem.rom_title,mode_name(g->mem.active_mode),g->mem.forced_mode!=GB_MODE_AUTO?" (forced)":"");
}

static void rc(void*u,const char*const*fl,int fi){(void)fi;if(fl&&fl[0])lr((gb_t*)u,fl[0]);}

int main(int argc,char**argv){
    gb_t gb={0};
    const char*rom_path=NULL;
    gb_mode_t requested_mode=GB_MODE_AUTO;
    for(int i=1;i<argc;i++){
        if(!strcmp(argv[i],"--mode")||!strcmp(argv[i],"-m")){
            if(i+1>=argc||!parse_mode_arg(argv[i+1],&requested_mode)){
                fprintf(stderr,"Usage: %s [--mode dmg|cgb|sgb] [rom.gb|rom.gbc]\n",argv[0]);
                return 1;
            }
            i++;
            continue;
        }
        if(argv[i][0]=='-'&&argv[i][1]){
            fprintf(stderr,"Unknown option: %s\nUsage: %s [--mode dmg|cgb|sgb] [rom.gb|rom.gbc]\n",argv[i],argv[0]);
            return 1;
        }
        rom_path=argv[i];
    }
    gb.requested_mode=requested_mode;
    cpu_init_opcodes();re(&gb);gb.mem.joypad=&gb.joypad;gb.mem.timer=&gb.timer;gb.mem.apu=&gb.apu;gb.mem.forced_mode=gb.requested_mode;dbg_init();
    if(!SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO))return 1;
    gb.w=SDL_CreateWindow("PurpleGB",480,432,SDL_WINDOW_RESIZABLE);
    if(!gb.w){SDL_Quit();return 1;}
    gb.r=SDL_CreateRenderer(gb.w,NULL);
    if(!gb.r){SDL_DestroyWindow(gb.w);SDL_Quit();return 1;}
    gb.t=SDL_CreateTexture(gb.r,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,160,144);
    if(!gb.t){SDL_DestroyRenderer(gb.r);SDL_DestroyWindow(gb.w);SDL_Quit();return 1;}
    /* Set up SDL audio stream for APU output */
    SDL_AudioSpec spec={SDL_AUDIO_S16LE,2,44100};
    gb.audio=SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,&spec,NULL,NULL);
    if(gb.audio)SDL_ResumeAudioStreamDevice(gb.audio);

    if(rom_path)lr(&gb,rom_path);
    else{SDL_DialogFileFilter f={"Game Boy ROMs","gb;gbc"};SDL_ShowOpenFileDialog(rc,&gb,gb.w,&f,1,NULL,0);}
    
    gb.fps_ts=SDL_GetTicks();
    while(!gb.quit){
        u32 fs=SDL_GetTicks();SDL_Event e;
        while(SDL_PollEvent(&e))switch(e.type){
            case SDL_EVENT_QUIT:gb.quit=1;break;
            case SDL_EVENT_DROP_FILE:if(e.drop.data)lr(&gb,e.drop.data);break;
            case SDL_EVENT_KEY_DOWN:
                if(e.key.key==SDLK_ESCAPE&&gb.rom_loaded){gb.paused=!gb.paused;gb.menu_sel=0;}
                if(gb.paused){
                    if(e.key.key==SDLK_UP)gb.menu_sel=(gb.menu_sel+2)%3;
                    if(e.key.key==SDLK_DOWN)gb.menu_sel=(gb.menu_sel+1)%3;
                    if(e.key.key==SDLK_RETURN){gb.paused=0;if(gb.menu_sel==1){SDL_DialogFileFilter f={"Game Boy ROMs","gb;gbc"};SDL_ShowOpenFileDialog(rc,&gb,gb.w,&f,1,NULL,0);}else if(gb.menu_sel==2)gb.quit=1;}
                }
                joypad_handle_event(&gb.joypad,&gb.mem,&e);break;
            case SDL_EVENT_KEY_UP:joypad_handle_event(&gb.joypad,&gb.mem,&e);break;
            default:break;
        }
        if(!gb.paused&&gb.rom_loaded){
            int tc=0;
            while(tc<70224){
                int cy=cpu_step(&gb.cpu,&gb.mem);
                mem_dma_tick(&gb.mem, cy);
                ppu_tick(&gb.ppu,&gb.mem,cy);timer_tick(&gb.timer,&gb.mem,cy);apu_tick(&gb.apu,cy);tc+=cy;
            }
        }
        if(gb.mem.cart_cgb) ppu_decode_cgb_palettes(&gb.ppu,&gb.mem);
        if(gb.paused&&gb.rom_loaded){
            u32*fb=gb.ppu.framebuffer;
            for(int y=0;y<144;y++)for(int x=0;x<160;x++){u32 c=fb[y*160+x];fb[y*160+x]=0xFF000000|((u32)(((c>>16)&0xFF)*0.5f))<<16|((u32)(((c>>8)&0xFF)*0.5f))<<8|(u32)((c&0xFF)*0.5f);}
            for(int y=40;y<110;y++)for(int x=40;x<120;x++)fb[y*160+x]=0xFF444444;
        }
        SDL_UpdateTexture(gb.t,NULL,gb.ppu.framebuffer,160*4);
        SDL_RenderTexture(gb.r,gb.t,NULL,NULL);SDL_RenderPresent(gb.r);
        /* Push APU audio samples to SDL stream */
        if(gb.audio){
            s16 buf[4096];
            int n=apu_get_samples(&gb.apu,buf,4096);
            if(n>0)SDL_PutAudioStreamData(gb.audio,buf,n*sizeof(s16));
        }
        gb.fc++; u32 nw = SDL_GetTicks();
        if(nw-gb.fps_ts>=1000){char t[128];snprintf(t,sizeof(t),"PurpleGB — %s [%u FPS]",gb.rom_loaded?gb.mem.rom_title:"(no ROM)",gb.fc);SDL_SetWindowTitle(gb.w,t);gb.fps_ts=nw;gb.fc=0;}
        u32 el=SDL_GetTicks()-fs;if(el<16)SDL_Delay(16-el);
    }
    SDL_DestroyAudioStream(gb.audio);SDL_DestroyTexture(gb.t);SDL_DestroyRenderer(gb.r);SDL_DestroyWindow(gb.w);
    SDL_Quit();mem_save_sram(&gb.mem);apu_cleanup(&gb.apu);free(gb.mem.rom);free(gb.mem.eram);return 0;
}
