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
#include "apu.h"

typedef struct {
    mem_t mem; cpu_t cpu; timer_t timer; joypad_t joypad; ppu_t ppu; apu_t apu;
    SDL_Window *w; SDL_Renderer *r; SDL_Texture *t;
    SDL_Tray *tray; SDL_AudioStream *as;
    bool rom_loaded, quit, paused;
    int menu_sel; u32 fc, fps_ts; s16 abuf[2048];
} gb_t;

static void re(gb_t*g){mem_init(&g->mem);cpu_init(&g->cpu);timer_init(&g->timer);joypad_init(&g->joypad);ppu_init(&g->ppu);apu_init(&g->apu);g->paused=0;}

static void lr(gb_t*g,const char*p){
    if(!mem_load_rom(&g->mem,p))return;
    re(g);g->mem.joypad=&g->joypad;g->mem.timer=&g->timer;
    // Init HRAM: RET at $FFB6 (CALL target in VBlank handler)
    g->mem.hram[0x36] = 0xC9;
    if(!mem_load_rom(&g->mem,p))return;
    g->rom_loaded=1;
    if(g->as){SDL_DestroyAudioStream(g->as);g->as=NULL;}
    SDL_AudioSpec sp={0};sp.format=SDL_AUDIO_S16;sp.channels=2;sp.freq=SAMPLE_RATE;
    g->as=SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,&sp,NULL,NULL);
    if(g->as)SDL_ResumeAudioDevice(SDL_GetAudioStreamDevice(g->as));
    g->apu.stream=g->as;
}

static void rc(void*u,const char*const*fl,int fi){(void)fi;if(fl&&fl[0])lr((gb_t*)u,fl[0]);}
static void to(void*u,SDL_TrayEntry*e){(void)e;gb_t*g=(gb_t*)u;SDL_DialogFileFilter f={"Game Boy ROMs","gb;gbc"};SDL_ShowOpenFileDialog(rc,g,g->w,&f,1,NULL,0);}
static void tq(void*u,SDL_TrayEntry*e){(void)e;((gb_t*)u)->quit=1;}

int main(int argc,char**argv){
    gb_t gb={0};
    cpu_init_opcodes();re(&gb);gb.mem.joypad=&gb.joypad;gb.mem.timer=&gb.timer;
    if(!SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO))return 1;
    gb.w=SDL_CreateWindow("PurpleGB",480,432,SDL_WINDOW_RESIZABLE);
    if(!gb.w){SDL_Quit();return 1;}
    gb.r=SDL_CreateRenderer(gb.w,NULL);
    if(!gb.r){SDL_DestroyWindow(gb.w);SDL_Quit();return 1;}
    gb.t=SDL_CreateTexture(gb.r,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,160,144);
    if(!gb.t){SDL_DestroyRenderer(gb.r);SDL_DestroyWindow(gb.w);SDL_Quit();return 1;}

    SDL_Surface*ts=SDL_CreateSurface(16,16,SDL_PIXELFORMAT_INDEX8);
    if(ts){SDL_FillSurfaceRect(ts,NULL,0xFF00AAFF);gb.tray=SDL_CreateTray(ts,"PurpleGB");
        if(gb.tray){SDL_TrayMenu*m=SDL_CreateTrayMenu(gb.tray);SDL_TrayEntry*oe=SDL_InsertTrayEntryAt(m,-1,"Open ROM...",SDL_TRAYENTRY_BUTTON);SDL_SetTrayEntryCallback(oe,to,&gb);SDL_InsertTrayEntryAt(m,-1,NULL,0);SDL_TrayEntry*qe=SDL_InsertTrayEntryAt(m,-1,"Quit",SDL_TRAYENTRY_BUTTON);SDL_SetTrayEntryCallback(qe,tq,&gb);}
        SDL_DestroySurface(ts);}

    if(argc>1)lr(&gb,argv[1]);
    else{SDL_DialogFileFilter f={"Game Boy ROMs","gb;gbc"};SDL_ShowOpenFileDialog(rc,&gb,gb.w,&f,1,NULL,0);}
    
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
                ppu_tick(&gb.ppu,&gb.mem,cy);timer_tick(&gb.timer,&gb.mem,cy);apu_tick(&gb.apu,cy);mem_dma_tick(&gb.mem,cy);tc+=cy;
            }
            int sn=(int)(((u64)70224*SAMPLE_RATE)/T_CYCLES_PER_SEC);if(sn>512)sn=512;
            apu_generate(&gb.apu,gb.abuf,sn);if(gb.as)SDL_PutAudioStreamData(gb.as,gb.abuf,sn*sizeof(s16)*2);
        }

        if(gb.paused&&gb.rom_loaded){
            u32*fb=gb.ppu.framebuffer;
            for(int y=0;y<144;y++)for(int x=0;x<160;x++){u32 c=fb[y*160+x];fb[y*160+x]=0xFF000000|((u32)(((c>>16)&0xFF)*0.5f))<<16|((u32)(((c>>8)&0xFF)*0.5f))<<8|(u32)((c&0xFF)*0.5f);}
            for(int y=40;y<110;y++)for(int x=40;x<120;x++)fb[y*160+x]=0xFF444444;
        }
        // Dump tile data at $8000 and $9000 on first frame
        if (gb.fc == 1 && gb.rom_loaded) {
            fprintf(stderr, "TILE0@8000:");
            for (int i = 0; i < 16; i++) fprintf(stderr, " %02X", gb.mem.vram[i]);
            fprintf(stderr, "\nTILE0@9000:");
            for (int i = 0; i < 16; i++) fprintf(stderr, " %02X", gb.mem.vram[0x1000 + i]);
            fprintf(stderr, "\n");
        }
        SDL_UpdateTexture(gb.t,NULL,gb.ppu.framebuffer,160*4);
        gb.fc++; u32 nw = SDL_GetTicks();
        if(nw-gb.fps_ts>=1000){char t[128];snprintf(t,sizeof(t),"PurpleGB — %s [%u FPS]",gb.rom_loaded?gb.mem.rom_title:"(no ROM)",gb.fc);SDL_SetWindowTitle(gb.w,t);gb.fps_ts=nw;gb.fc=0;}
        u32 el=SDL_GetTicks()-fs;if(el<16)SDL_Delay(16-el);
    }
    if(gb.as)SDL_DestroyAudioStream(gb.as);if(gb.tray)SDL_DestroyTray(gb.tray);
    SDL_DestroyTexture(gb.t);SDL_DestroyRenderer(gb.r);SDL_DestroyWindow(gb.w);
    SDL_Quit();free(gb.mem.rom);free(gb.mem.eram);return 0;
}
