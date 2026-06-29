#include "cpu.h"
#include "interrupt.h"
#include <stdio.h>
#include <string.h>

op_fn main_opcodes[256];
op_fn cb_opcodes[256];
const u8 main_cycles[256]={1,3,2,2,1,1,2,1,5,2,2,2,1,1,2,1,1,3,2,2,1,1,2,1,3,2,2,2,1,1,2,1,2,3,2,2,1,1,2,1,2,2,2,2,1,1,2,1,2,3,2,2,3,3,3,1,2,2,2,2,1,1,2,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,2,1,1,1,1,1,1,1,2,1,1,1,1,1,1,1,2,1,1,1,1,1,1,1,2,1,1,1,1,1,1,1,2,1,1,1,1,1,1,1,2,1,1,1,1,1,1,1,2,1,1,1,1,1,1,1,2,1,2,2,3,1,3,3,3,2,2,1,3,1,3,3,2,2,2,2,3,1,3,3,2,2,2,1,3,1,3,1,2,2,3,2,2,1,1,3,2,2,4,1,4,1,1,1,2,2,3,2,2,1,1,3,2,2,3,2,4,1,1,1,2,2};
const u8 cb_cycles[256]={2,2,2,2,2,2,4,2,2,2,2,2,2,2,4,2,2,2,2,2,2,2,4,2,2,2,2,2,2,2,4,2,2,2,2,2,2,2,4,2,2,2,2,2,2,2,4,2,2,2,2,2,2,2,4,2,2,2,2,2,2,2,4,2,2,2,2,2,2,2,3,2,2,2,2,2,2,2,3,2,2,2,2,2,2,2,3,2,2,2,2,2,2,2,3,2,2,2,2,2,2,2,3,2,2,2,2,2,2,2,3,2,2,2,2,2,2,2,3,2,2,2,2,2,2,2,3,2,2,2,2,2,2,2,4,2,2,2,2,2,2,2,4,2,2,2,2,2,2,2,4,2,2,2,2,2,2,2,4,2,2,2,2,2,2,2,4,2,2,2,2,2,2,2,4,2,2,2,2,2,2,2,4,2,2,2,2,2,2,2,4,2,2,2,2,2,2,2,4,2,2,2,2,2,2,2,4,2,2,2,2,2,2,2,4,2,2,2,2,2,2,2,4,2,2,2,2,2,2,2,4,2,2,2,2,2,2,2,4,2,2,2,2,2,2,2,4,2,2,2,2,2,2,2,4,2};


static inline u8 gr(cpu_t*c,mem_t*m,int r){
    switch(r){case 0:return c->b;case 1:return c->c;case 2:return c->d;case 3:return c->e;case 4:return c->h;case 5:return c->l;case 6:return mem_read(m,cpu_hl(c));case 7:return c->a;default:return 0;}
}
static inline void sr(cpu_t*c,mem_t*m,int r,u8 v){
    switch(r){case 0:c->b=v;break;case 1:c->c=v;break;case 2:c->d=v;break;case 3:c->e=v;break;case 4:c->h=v;break;case 5:c->l=v;break;case 6:mem_write(m,cpu_hl(c),v);break;case 7:c->a=v;break;}
}

void cpu_init(cpu_t*c){memset(c,0,sizeof(*c));cpu_set_af(c,0x01B0);cpu_set_bc(c,0x0013);cpu_set_de(c,0x00D8);cpu_set_hl(c,0x014D);c->sp=0xFFFE;c->pc=0x0100;}
void cpu_init_boot(cpu_t*c){memset(c,0,sizeof(*c));c->c=0x14;}
int cpu_service_interrupt(cpu_t*c,mem_t*m){u8 p=interrupt_get_pending(m);if(!p)return 0;int b=0;while(!(p&1)){b++;p>>=1;}c->ime=0;c->halted=0;c->sp-=2;mem_write16(m,c->sp,c->pc);c->pc=0x40+(b<<3);m->io[0x0F]&=~(1<<b);return 20;}
int cpu_step(cpu_t*c,mem_t*m){
    bool halt_bug_fetch=c->halt_bug;
    c->halt_bug=0;
    if(c->halted){
        if(interrupt_pending(m)){
            c->halted=0;
            if(c->ime)return cpu_service_interrupt(c,m);
        }else return 1;
    }
    if(c->stopped)return 1;
    if(c->ime&&interrupt_pending(m))return cpu_service_interrupt(c,m);
    bool enable_ime_after=c->ime_scheduled;
    u8 op=mem_read(m,c->pc);
    if(!halt_bug_fetch)c->pc++;
    if(op==0xCB){
        u8 cb=mem_read(m,c->pc++);
        cb_opcodes[cb](c,m);
        if(enable_ime_after){c->ime=1;c->ime_scheduled=0;}
        return cb_cycles[cb];
    }
    main_opcodes[op](c,m);
    if(enable_ime_after){c->ime=1;c->ime_scheduled=0;}
    return main_cycles[op];
}
#define ALU(name,code) static void alu_##name(cpu_t*c,u8 v){code}
ALU(add,{u16 r=c->a+v;cpu_set_z(c,!(r&0xFF));cpu_set_n(c,0);cpu_set_h(c,((c->a&0xF)+(v&0xF))>0xF);cpu_set_c(c,r>0xFF);c->a=r&0xFF;})
ALU(adc,{u8 cy=cpu_get_c(c)?1:0;u16 r=c->a+v+cy;cpu_set_z(c,!(r&0xFF));cpu_set_n(c,0);cpu_set_h(c,((c->a&0xF)+(v&0xF)+cy)>0xF);cpu_set_c(c,r>0xFF);c->a=r&0xFF;})
ALU(sub,{u16 r=c->a-v;cpu_set_z(c,!(r&0xFF));cpu_set_n(c,1);cpu_set_h(c,(c->a&0xF)<(v&0xF));cpu_set_c(c,c->a<v);c->a=r&0xFF;})
ALU(sbc,{u8 cy=cpu_get_c(c)?1:0;u16 r=c->a-v-cy;cpu_set_z(c,!(r&0xFF));cpu_set_n(c,1);cpu_set_h(c,(c->a&0xF)<((v&0xF)+cy));cpu_set_c(c,c->a<(u16)(v+cy));c->a=r&0xFF;})
ALU(and,{c->a&=v;cpu_set_z(c,!c->a);cpu_set_n(c,0);cpu_set_h(c,1);cpu_set_c(c,0);})
ALU(or,{c->a|=v;cpu_set_z(c,!c->a);cpu_set_n(c,0);cpu_set_h(c,0);cpu_set_c(c,0);})
ALU(xor,{c->a^=v;cpu_set_z(c,!c->a);cpu_set_n(c,0);cpu_set_h(c,0);cpu_set_c(c,0);})
ALU(cp,{u16 r=c->a-v;cpu_set_z(c,!(r&0xFF));cpu_set_n(c,1);cpu_set_h(c,(c->a&0xF)<(v&0xF));cpu_set_c(c,c->a<v);})

#define F static void
F op_n(cpu_t*c,mem_t*m){(void)c;(void)m;}
F op_ld_bc_i16(cpu_t*c,mem_t*m){u16 v=mem_read(m,c->pc)|(u16)mem_read(m,c->pc+1)<<8;c->pc+=2;cpu_set_bc(c,v);}
F op_ld_de_i16(cpu_t*c,mem_t*m){u16 v=mem_read(m,c->pc)|(u16)mem_read(m,c->pc+1)<<8;c->pc+=2;cpu_set_de(c,v);}
F op_ld_hl_i16(cpu_t*c,mem_t*m){u16 v=mem_read(m,c->pc)|(u16)mem_read(m,c->pc+1)<<8;c->pc+=2;cpu_set_hl(c,v);}
F op_ld_sp_i16(cpu_t*c,mem_t*m){u16 v=mem_read(m,c->pc)|(u16)mem_read(m,c->pc+1)<<8;c->pc+=2;c->sp=v;}
F op_ld_bcp_a(cpu_t*c,mem_t*m){mem_write(m,cpu_bc(c),c->a);}
F op_ld_dep_a(cpu_t*c,mem_t*m){mem_write(m,cpu_de(c),c->a);}
F op_ld_a_bcp(cpu_t*c,mem_t*m){c->a=mem_read(m,cpu_bc(c));}
F op_ld_a_dep(cpu_t*c,mem_t*m){c->a=mem_read(m,cpu_de(c));}
F op_ldi_hlp_a(cpu_t*c,mem_t*m){mem_write(m,cpu_hl(c),c->a);cpu_set_hl(c,cpu_hl(c)+1);}
F op_ldi_a_hlp(cpu_t*c,mem_t*m){c->a=mem_read(m,cpu_hl(c));cpu_set_hl(c,cpu_hl(c)+1);}
F op_ldd_hlp_a(cpu_t*c,mem_t*m){mem_write(m,cpu_hl(c),c->a);cpu_set_hl(c,cpu_hl(c)-1);}
F op_ldd_a_hlp(cpu_t*c,mem_t*m){c->a=mem_read(m,cpu_hl(c));cpu_set_hl(c,cpu_hl(c)-1);}
F op_inc_bc(cpu_t*c,mem_t*m){(void)m;cpu_set_bc(c,cpu_bc(c)+1);}
F op_dec_bc(cpu_t*c,mem_t*m){(void)m;cpu_set_bc(c,cpu_bc(c)-1);}
F op_inc_de(cpu_t*c,mem_t*m){(void)m;cpu_set_de(c,cpu_de(c)+1);}
F op_dec_de(cpu_t*c,mem_t*m){(void)m;cpu_set_de(c,cpu_de(c)-1);}
F op_inc_hl(cpu_t*c,mem_t*m){(void)m;cpu_set_hl(c,cpu_hl(c)+1);}
F op_dec_hl(cpu_t*c,mem_t*m){(void)m;cpu_set_hl(c,cpu_hl(c)-1);}
F op_inc_sp(cpu_t*c,mem_t*m){(void)m;c->sp++;}
F op_dec_sp(cpu_t*c,mem_t*m){(void)m;c->sp--;}
F op_rlca(cpu_t*c,mem_t*m){(void)m;u8 b=(c->a>>7)&1;c->a=(c->a<<1)|b;cpu_set_z(c,0);cpu_set_n(c,0);cpu_set_h(c,0);cpu_set_c(c,b);}
F op_rla(cpu_t*c,mem_t*m){(void)m;u8 cy=cpu_get_c(c)?1:0,nb=(c->a>>7)&1;c->a=(c->a<<1)|cy;cpu_set_z(c,0);cpu_set_n(c,0);cpu_set_h(c,0);cpu_set_c(c,nb);}
F op_rrca(cpu_t*c,mem_t*m){(void)m;u8 b=c->a&1;c->a=(c->a>>1)|(b<<7);cpu_set_z(c,0);cpu_set_n(c,0);cpu_set_h(c,0);cpu_set_c(c,b);}
F op_rra(cpu_t*c,mem_t*m){(void)m;u8 cy=cpu_get_c(c)?1:0,nb=c->a&1;c->a=(c->a>>1)|(cy<<7);cpu_set_z(c,0);cpu_set_n(c,0);cpu_set_h(c,0);cpu_set_c(c,nb);}
F op_daa(cpu_t*c,mem_t*m){(void)m;u16 a=c->a;if(!cpu_get_n(c)){if(cpu_get_h(c)||(a&0xF)>9)a+=6;if(cpu_get_c(c)||a>0x9F)a+=0x60;}else{if(cpu_get_h(c))a=(a-6)&0xFF;if(cpu_get_c(c))a-=0x60;}c->a&=0xFF;cpu_set_z(c,!c->a);cpu_set_h(c,0);cpu_set_c(c,cpu_get_c(c)||a>0xFF?!!(a&~0xFF):c->a>0x99);}
F op_cpl(cpu_t*c,mem_t*m){(void)m;c->a=~c->a;cpu_set_n(c,1);cpu_set_h(c,1);}
F op_ccf(cpu_t*c,mem_t*m){(void)m;cpu_set_n(c,0);cpu_set_h(c,0);cpu_set_c(c,!cpu_get_c(c));}
F op_scf(cpu_t*c,mem_t*m){(void)m;cpu_set_n(c,0);cpu_set_h(c,0);cpu_set_c(c,1);}
F op_push_af(cpu_t*c,mem_t*m){c->sp-=2;mem_write16(m,c->sp,cpu_af(c));}
F op_push_bc(cpu_t*c,mem_t*m){c->sp-=2;mem_write16(m,c->sp,cpu_bc(c));}
F op_push_de(cpu_t*c,mem_t*m){c->sp-=2;mem_write16(m,c->sp,cpu_de(c));}
F op_push_hl(cpu_t*c,mem_t*m){c->sp-=2;mem_write16(m,c->sp,cpu_hl(c));}
F op_pop_bc(cpu_t*c,mem_t*m){u16 v=mem_read(m,c->sp)|(u16)mem_read(m,c->sp+1)<<8;c->sp+=2;cpu_set_bc(c,v);}
F op_pop_de(cpu_t*c,mem_t*m){u16 v=mem_read(m,c->sp)|(u16)mem_read(m,c->sp+1)<<8;c->sp+=2;cpu_set_de(c,v);}
F op_pop_hl(cpu_t*c,mem_t*m){u16 v=mem_read(m,c->sp)|(u16)mem_read(m,c->sp+1)<<8;c->sp+=2;cpu_set_hl(c,v);}
F op_pop_af(cpu_t*c,mem_t*m){u16 v=mem_read(m,c->sp)|(u16)mem_read(m,c->sp+1)<<8;c->sp+=2;cpu_set_af(c,v);}
F op_ld_i16_sp(cpu_t*c,mem_t*m){u16 a=mem_read(m,c->pc)|(u16)mem_read(m,c->pc+1)<<8;c->pc+=2;mem_write(m,a,c->sp&0xFF);mem_write(m,a+1,c->sp>>8);}
F op_ld_cp_a(cpu_t*c,mem_t*m){mem_write(m,0xFF00+c->c,c->a);}
F op_ld_a_cp(cpu_t*c,mem_t*m){c->a=mem_read(m,0xFF00+c->c);}
F op_ldh_i_a(cpu_t*c,mem_t*m){u8 o=mem_read(m,c->pc++);mem_write(m,0xFF00+o,c->a);}
F op_ldh_a_i(cpu_t*c,mem_t*m){u8 o=mem_read(m,c->pc++);c->a=mem_read(m,0xFF00+o);}
F op_ld_i16_a(cpu_t*c,mem_t*m){u16 a=mem_read(m,c->pc)|(u16)mem_read(m,c->pc+1)<<8;c->pc+=2;mem_write(m,a,c->a);}
F op_ld_a_i16(cpu_t*c,mem_t*m){u16 a=mem_read(m,c->pc)|(u16)mem_read(m,c->pc+1)<<8;c->pc+=2;c->a=mem_read(m,a);}
F op_jr(cpu_t*c,mem_t*m){s8 o=(s8)mem_read(m,c->pc++);c->pc+=o;}
F op_jr_nz(cpu_t*c,mem_t*m){s8 o=(s8)mem_read(m,c->pc++);if(!cpu_get_z(c))c->pc+=o;}
F op_jr_z(cpu_t*c,mem_t*m){s8 o=(s8)mem_read(m,c->pc++);if(cpu_get_z(c))c->pc+=o;}
F op_jr_nc(cpu_t*c,mem_t*m){s8 o=(s8)mem_read(m,c->pc++);if(!cpu_get_c(c))c->pc+=o;}
F op_jr_c(cpu_t*c,mem_t*m){s8 o=(s8)mem_read(m,c->pc++);if(cpu_get_c(c))c->pc+=o;}
F op_jp(cpu_t*c,mem_t*m){c->pc=mem_read(m,c->pc)|(u16)mem_read(m,c->pc+1)<<8;}
F op_jp_nz(cpu_t*c,mem_t*m){u16 a=mem_read(m,c->pc)|(u16)mem_read(m,c->pc+1)<<8;c->pc+=2;if(!cpu_get_z(c))c->pc=a;}
F op_jp_z(cpu_t*c,mem_t*m){u16 a=mem_read(m,c->pc)|(u16)mem_read(m,c->pc+1)<<8;c->pc+=2;if(cpu_get_z(c))c->pc=a;}
F op_jp_nc(cpu_t*c,mem_t*m){u16 a=mem_read(m,c->pc)|(u16)mem_read(m,c->pc+1)<<8;c->pc+=2;if(!cpu_get_c(c))c->pc=a;}
F op_jp_c(cpu_t*c,mem_t*m){u16 a=mem_read(m,c->pc)|(u16)mem_read(m,c->pc+1)<<8;c->pc+=2;if(cpu_get_c(c))c->pc=a;}
F op_jp_hl(cpu_t*c,mem_t*m){(void)m;c->pc=cpu_hl(c);}
F op_call(cpu_t*c,mem_t*m){u16 a=mem_read(m,c->pc)|(u16)mem_read(m,c->pc+1)<<8;c->pc+=2;c->sp-=2;mem_write16(m,c->sp,c->pc);c->pc=a;}
F op_call_nz(cpu_t*c,mem_t*m){u16 a=mem_read(m,c->pc)|(u16)mem_read(m,c->pc+1)<<8;c->pc+=2;if(!cpu_get_z(c)){c->sp-=2;mem_write16(m,c->sp,c->pc);c->pc=a;}}
F op_call_z(cpu_t*c,mem_t*m){u16 a=mem_read(m,c->pc)|(u16)mem_read(m,c->pc+1)<<8;c->pc+=2;if(cpu_get_z(c)){c->sp-=2;mem_write16(m,c->sp,c->pc);c->pc=a;}}
F op_call_nc(cpu_t*c,mem_t*m){u16 a=mem_read(m,c->pc)|(u16)mem_read(m,c->pc+1)<<8;c->pc+=2;if(!cpu_get_c(c)){c->sp-=2;mem_write16(m,c->sp,c->pc);c->pc=a;}}
F op_call_c(cpu_t*c,mem_t*m){u16 a=mem_read(m,c->pc)|(u16)mem_read(m,c->pc+1)<<8;c->pc+=2;if(cpu_get_c(c)){c->sp-=2;mem_write16(m,c->sp,c->pc);c->pc=a;}}
F op_ret(cpu_t*c,mem_t*m){c->pc=mem_read(m,c->sp)|(u16)mem_read(m,c->sp+1)<<8;c->sp+=2;}
F op_ret_nz(cpu_t*c,mem_t*m){if(!cpu_get_z(c)){c->pc=mem_read(m,c->sp)|(u16)mem_read(m,c->sp+1)<<8;c->sp+=2;}}
F op_ret_z(cpu_t*c,mem_t*m){if(cpu_get_z(c)){c->pc=mem_read(m,c->sp)|(u16)mem_read(m,c->sp+1)<<8;c->sp+=2;}}
F op_ret_nc(cpu_t*c,mem_t*m){if(!cpu_get_c(c)){c->pc=mem_read(m,c->sp)|(u16)mem_read(m,c->sp+1)<<8;c->sp+=2;}}
F op_ret_c(cpu_t*c,mem_t*m){if(cpu_get_c(c)){c->pc=mem_read(m,c->sp)|(u16)mem_read(m,c->sp+1)<<8;c->sp+=2;}}
F op_reti(cpu_t*c,mem_t*m){c->pc=mem_read(m,c->sp)|(u16)mem_read(m,c->sp+1)<<8;c->sp+=2;c->ime=1;}
F op_ei(cpu_t*c,mem_t*m){(void)m;c->ime_scheduled=1;}
F op_di(cpu_t*c,mem_t*m){(void)m;c->ime=0;c->ime_scheduled=0;}
F op_halt(cpu_t*c,mem_t*m){if(!c->ime&&interrupt_pending(m))c->halt_bug=1;else c->halted=1;}
F op_stop(cpu_t*c,mem_t*m){(void)m;c->pc++;c->stopped=1;}
F op_add_sp_r8(cpu_t*c,mem_t*m){s8 o=(s8)mem_read(m,c->pc++);u16 s=c->sp;u16 r=s+o;cpu_set_z(c,0);cpu_set_n(c,0);cpu_set_h(c,((s&0xF)+(o&0xF))>0xF);cpu_set_c(c,((s&0xFF)+(u8)o)>0xFF);c->sp=r;}
F op_ld_hl_sp_r8(cpu_t*c,mem_t*m){s8 o=(s8)mem_read(m,c->pc++);u16 s=c->sp;u16 r=s+o;cpu_set_z(c,0);cpu_set_n(c,0);cpu_set_h(c,((s&0xF)+(o&0xF))>0xF);cpu_set_c(c,((s&0xFF)+(u8)o)>0xFF);cpu_set_hl(c,r);}
F op_ld_sp_hl(cpu_t*c,mem_t*m){(void)m;c->sp=cpu_hl(c);}
F op_und(cpu_t*c,mem_t*m){(void)m;fprintf(stderr,"Undefined op at PC=%04X\n",c->pc-1);c->halted=1;}
F op_add_hl_bc(cpu_t*c,mem_t*m){(void)m;u16 h=cpu_hl(c);u32 r=h+cpu_bc(c);cpu_set_n(c,0);cpu_set_h(c,((h&0xFFF)+(cpu_bc(c)&0xFFF))>0xFFF);cpu_set_c(c,r>0xFFFF);cpu_set_hl(c,r);}
F op_add_hl_de(cpu_t*c,mem_t*m){(void)m;u16 h=cpu_hl(c);u32 r=h+cpu_de(c);cpu_set_n(c,0);cpu_set_h(c,((h&0xFFF)+(cpu_de(c)&0xFFF))>0xFFF);cpu_set_c(c,r>0xFFFF);cpu_set_hl(c,r);}
F op_add_hl_hl(cpu_t*c,mem_t*m){(void)m;u16 h=cpu_hl(c);u32 r=h+h;cpu_set_n(c,0);cpu_set_h(c,((h&0xFFF)*2)>0xFFF);cpu_set_c(c,r>0xFFFF);cpu_set_hl(c,r);}
F op_add_hl_sp(cpu_t*c,mem_t*m){(void)m;u16 h=cpu_hl(c);u32 r=h+c->sp;cpu_set_n(c,0);cpu_set_h(c,((h&0xFFF)+(c->sp&0xFFF))>0xFFF);cpu_set_c(c,r>0xFFFF);cpu_set_hl(c,r);}

#define ALU_OP(name,func) F op_##name##_b(cpu_t*c,mem_t*m){func(c,gr(c,m,0));}F op_##name##_c(cpu_t*c,mem_t*m){func(c,gr(c,m,1));}F op_##name##_d(cpu_t*c,mem_t*m){func(c,gr(c,m,2));}F op_##name##_e(cpu_t*c,mem_t*m){func(c,gr(c,m,3));}F op_##name##_h(cpu_t*c,mem_t*m){func(c,gr(c,m,4));}F op_##name##_l(cpu_t*c,mem_t*m){func(c,gr(c,m,5));}F op_##name##_hl(cpu_t*c,mem_t*m){func(c,mem_read(m,cpu_hl(c)));}F op_##name##_a(cpu_t*c,mem_t*m){func(c,c->a);}F op_##name##_i(cpu_t*c,mem_t*m){u8 v=mem_read(m,c->pc++);func(c,v);}
ALU_OP(add,alu_add)ALU_OP(adc,alu_adc)ALU_OP(sub,alu_sub)ALU_OP(sbc,alu_sbc)ALU_OP(and,alu_and)ALU_OP(or,alu_or)ALU_OP(xor,alu_xor)ALU_OP(cp,alu_cp)

F op_ld_rr(cpu_t*c,mem_t*m){u8 o=mem_read(m,c->pc-1);int d=(o>>3)&7,s=o&7;(void)sr(c,m,d,gr(c,m,s));}
#define LDIM(n,rn) F op_ld_##rn(cpu_t*c,mem_t*m){u8 v=mem_read(m,c->pc++);sr(c,m,n,v);}
LDIM(0,b)LDIM(1,c)LDIM(2,d)LDIM(3,e)LDIM(4,h)LDIM(5,l)LDIM(6,h_)LDIM(7,a)

#define ICD(n) F op_inc_##n(cpu_t*c,mem_t*m){u8 v=n==6?mem_read(m,cpu_hl(c)):gr(c,m,n);u8 r=v+1;if(n==6)mem_write(m,cpu_hl(c),r);else sr(c,m,n,r);cpu_set_z(c,!r);cpu_set_n(c,0);cpu_set_h(c,!(v&0xF));}
#define DCD(n) F op_dec_##n(cpu_t*c,mem_t*m){u8 v=n==6?mem_read(m,cpu_hl(c)):gr(c,m,n);u8 r=v-1;if(n==6)mem_write(m,cpu_hl(c),r);else sr(c,m,n,r);cpu_set_z(c,!r);cpu_set_n(c,1);cpu_set_h(c,!(v&0xF));}
ICD(0)DCD(0)ICD(1)DCD(1)ICD(2)DCD(2)ICD(3)DCD(3)ICD(4)DCD(4)ICD(5)DCD(5)ICD(6)DCD(6)ICD(7)DCD(7)
#define RST(n) F op_rst_##n(cpu_t*c,mem_t*m){c->sp-=2;mem_write16(m,c->sp,c->pc);c->pc=n;}
RST(0x00)RST(0x08)RST(0x10)RST(0x18)RST(0x20)RST(0x28)RST(0x30)RST(0x38)

// CB helpers
static u8 cb_rlc(cpu_t*c,u8 v){u8 b=v>>7;u8 r=v<<1|b;cpu_set_z(c,!r);cpu_set_n(c,0);cpu_set_h(c,0);cpu_set_c(c,b);return r;}
static u8 cb_rrc(cpu_t*c,u8 v){u8 b=v&1;u8 r=v>>1|b<<7;cpu_set_z(c,!r);cpu_set_n(c,0);cpu_set_h(c,0);cpu_set_c(c,b);return r;}
static u8 cb_rl(cpu_t*c,u8 v){u8 cy=cpu_get_c(c),nb=v>>7;u8 r=v<<1|cy;cpu_set_z(c,!r);cpu_set_n(c,0);cpu_set_h(c,0);cpu_set_c(c,nb);return r;}
static u8 cb_rr(cpu_t*c,u8 v){u8 cy=cpu_get_c(c),nb=v&1;u8 r=v>>1|cy<<7;cpu_set_z(c,!r);cpu_set_n(c,0);cpu_set_h(c,0);cpu_set_c(c,nb);return r;}
static u8 cb_sla(cpu_t*c,u8 v){u8 b=v>>7;u8 r=v<<1;cpu_set_z(c,!r);cpu_set_n(c,0);cpu_set_h(c,0);cpu_set_c(c,b);return r;}
static u8 cb_sra(cpu_t*c,u8 v){u8 b=v&1;u8 r=(v>>1)|(v&0x80);cpu_set_z(c,!r);cpu_set_n(c,0);cpu_set_h(c,0);cpu_set_c(c,b);return r;}
static u8 cb_swap(cpu_t*c,u8 v){u8 r=(v<<4)|(v>>4);cpu_set_z(c,!r);cpu_set_n(c,0);cpu_set_h(c,0);cpu_set_c(c,0);return r;}
static u8 cb_srl(cpu_t*c,u8 v){u8 b=v&1;u8 r=v>>1;cpu_set_z(c,!r);cpu_set_n(c,0);cpu_set_h(c,0);cpu_set_c(c,b);return r;}

#define CBGEN(name) F cb_##name##_b(cpu_t*c,mem_t*m){c->b=cb_##name(c,c->b);}F cb_##name##_c(cpu_t*c,mem_t*m){c->c=cb_##name(c,c->c);}F cb_##name##_d(cpu_t*c,mem_t*m){c->d=cb_##name(c,c->d);}F cb_##name##_e(cpu_t*c,mem_t*m){c->e=cb_##name(c,c->e);}F cb_##name##_h(cpu_t*c,mem_t*m){c->h=cb_##name(c,c->h);}F cb_##name##_l(cpu_t*c,mem_t*m){c->l=cb_##name(c,c->l);}F cb_##name##_hl(cpu_t*c,mem_t*m){u8 v=mem_read(m,cpu_hl(c));mem_write(m,cpu_hl(c),cb_##name(c,v));}F cb_##name##_a(cpu_t*c,mem_t*m){c->a=cb_##name(c,c->a);}
CBGEN(rlc)CBGEN(rrc)CBGEN(rl)CBGEN(rr)CBGEN(sla)CBGEN(sra)CBGEN(swap)CBGEN(srl)

#define CB_BIT(n) F cb_b##n##_b(cpu_t*c,mem_t*m){cpu_set_z(c,!(c->b&(1<<n)));cpu_set_n(c,0);cpu_set_h(c,1);}F cb_b##n##_c(cpu_t*c,mem_t*m){cpu_set_z(c,!(c->c&(1<<n)));cpu_set_n(c,0);cpu_set_h(c,1);}F cb_b##n##_d(cpu_t*c,mem_t*m){cpu_set_z(c,!(c->d&(1<<n)));cpu_set_n(c,0);cpu_set_h(c,1);}F cb_b##n##_e(cpu_t*c,mem_t*m){cpu_set_z(c,!(c->e&(1<<n)));cpu_set_n(c,0);cpu_set_h(c,1);}F cb_b##n##_h(cpu_t*c,mem_t*m){cpu_set_z(c,!(c->h&(1<<n)));cpu_set_n(c,0);cpu_set_h(c,1);}F cb_b##n##_l(cpu_t*c,mem_t*m){cpu_set_z(c,!(c->l&(1<<n)));cpu_set_n(c,0);cpu_set_h(c,1);}F cb_b##n##_hl(cpu_t*c,mem_t*m){u8 v=mem_read(m,cpu_hl(c));cpu_set_z(c,!(v&(1<<n)));cpu_set_n(c,0);cpu_set_h(c,1);}F cb_b##n##_a(cpu_t*c,mem_t*m){cpu_set_z(c,!(c->a&(1<<n)));cpu_set_n(c,0);cpu_set_h(c,1);}
CB_BIT(0)CB_BIT(1)CB_BIT(2)CB_BIT(3)CB_BIT(4)CB_BIT(5)CB_BIT(6)CB_BIT(7)

#define CB_SET(n) F cb_s##n##_b(cpu_t*c,mem_t*m){c->b|=1<<n;}F cb_s##n##_c(cpu_t*c,mem_t*m){c->c|=1<<n;}F cb_s##n##_d(cpu_t*c,mem_t*m){c->d|=1<<n;}F cb_s##n##_e(cpu_t*c,mem_t*m){c->e|=1<<n;}F cb_s##n##_h(cpu_t*c,mem_t*m){c->h|=1<<n;}F cb_s##n##_l(cpu_t*c,mem_t*m){c->l|=1<<n;}F cb_s##n##_hl(cpu_t*c,mem_t*m){u8 v=mem_read(m,cpu_hl(c));mem_write(m,cpu_hl(c),v|(1<<n));}F cb_s##n##_a(cpu_t*c,mem_t*m){c->a|=1<<n;}
CB_SET(0)CB_SET(1)CB_SET(2)CB_SET(3)CB_SET(4)CB_SET(5)CB_SET(6)CB_SET(7)

#define CB_RES(n) F cb_r##n##_b(cpu_t*c,mem_t*m){c->b&=~(1<<n);}F cb_r##n##_c(cpu_t*c,mem_t*m){c->c&=~(1<<n);}F cb_r##n##_d(cpu_t*c,mem_t*m){c->d&=~(1<<n);}F cb_r##n##_e(cpu_t*c,mem_t*m){c->e&=~(1<<n);}F cb_r##n##_h(cpu_t*c,mem_t*m){c->h&=~(1<<n);}F cb_r##n##_l(cpu_t*c,mem_t*m){c->l&=~(1<<n);}F cb_r##n##_hl(cpu_t*c,mem_t*m){u8 v=mem_read(m,cpu_hl(c));mem_write(m,cpu_hl(c),v&~(1<<n));}F cb_r##n##_a(cpu_t*c,mem_t*m){c->a&=~(1<<n);}
CB_RES(0)CB_RES(1)CB_RES(2)CB_RES(3)CB_RES(4)CB_RES(5)CB_RES(6)CB_RES(7)


void cpu_init_opcodes(void)
{
    for(int i=0;i<256;i++)main_opcodes[i]=op_und;
    main_opcodes[0x00]=op_n;main_opcodes[0x01]=op_ld_bc_i16;main_opcodes[0x02]=op_ld_bcp_a;main_opcodes[0x03]=op_inc_bc;
    main_opcodes[0x04]=op_inc_0;main_opcodes[0x05]=op_dec_0;main_opcodes[0x06]=op_ld_b;main_opcodes[0x07]=op_rlca;
    main_opcodes[0x08]=op_ld_i16_sp;main_opcodes[0x09]=op_add_hl_bc;main_opcodes[0x0A]=op_ld_a_bcp;main_opcodes[0x0B]=op_dec_bc;
    main_opcodes[0x0C]=op_inc_1;main_opcodes[0x0D]=op_dec_1;main_opcodes[0x0E]=op_ld_c;main_opcodes[0x0F]=op_rrca;
    main_opcodes[0x10]=op_stop;main_opcodes[0x11]=op_ld_de_i16;main_opcodes[0x12]=op_ld_dep_a;main_opcodes[0x13]=op_inc_de;
    main_opcodes[0x14]=op_inc_2;main_opcodes[0x15]=op_dec_2;main_opcodes[0x16]=op_ld_d;main_opcodes[0x17]=op_rla;
    main_opcodes[0x18]=op_jr;main_opcodes[0x19]=op_add_hl_de;main_opcodes[0x1A]=op_ld_a_dep;main_opcodes[0x1B]=op_dec_de;
    main_opcodes[0x1C]=op_inc_3;main_opcodes[0x1D]=op_dec_3;main_opcodes[0x1E]=op_ld_e;main_opcodes[0x1F]=op_rra;
    main_opcodes[0x20]=op_jr_nz;main_opcodes[0x21]=op_ld_hl_i16;main_opcodes[0x22]=op_ldi_hlp_a;main_opcodes[0x23]=op_inc_hl;
    main_opcodes[0x24]=op_inc_4;main_opcodes[0x25]=op_dec_4;main_opcodes[0x26]=op_ld_h;main_opcodes[0x27]=op_daa;
    main_opcodes[0x28]=op_jr_z;main_opcodes[0x29]=op_add_hl_hl;main_opcodes[0x2A]=op_ldi_a_hlp;main_opcodes[0x2B]=op_dec_hl;
    main_opcodes[0x2C]=op_inc_5;main_opcodes[0x2D]=op_dec_5;main_opcodes[0x2E]=op_ld_l;main_opcodes[0x2F]=op_cpl;
    main_opcodes[0x30]=op_jr_nc;main_opcodes[0x31]=op_ld_sp_i16;main_opcodes[0x32]=op_ldd_hlp_a;main_opcodes[0x33]=op_inc_sp;
    main_opcodes[0x34]=op_inc_6;main_opcodes[0x35]=op_dec_6;main_opcodes[0x36]=op_ld_h_;main_opcodes[0x37]=op_scf;
    main_opcodes[0x38]=op_jr_c;main_opcodes[0x39]=op_add_hl_sp;main_opcodes[0x3A]=op_ldd_a_hlp;main_opcodes[0x3B]=op_dec_sp;
    main_opcodes[0x3C]=op_inc_7;main_opcodes[0x3D]=op_dec_7;main_opcodes[0x3E]=op_ld_a;main_opcodes[0x3F]=op_ccf;
    for(int d=0;d<8;d++)for(int s=0;s<8;s++){int op=0x40|(d<<3)|s;if(op==0x76)main_opcodes[op]=op_halt;else main_opcodes[op]=op_ld_rr;}
    #define ALU_BASE(name,base) do{main_opcodes[base]=op_##name##_b;main_opcodes[base+1]=op_##name##_c;main_opcodes[base+2]=op_##name##_d;main_opcodes[base+3]=op_##name##_e;main_opcodes[base+4]=op_##name##_h;main_opcodes[base+5]=op_##name##_l;main_opcodes[base+6]=op_##name##_hl;main_opcodes[base+7]=op_##name##_a;}while(0)
    ALU_BASE(add,0x80);ALU_BASE(adc,0x88);ALU_BASE(sub,0x90);ALU_BASE(sbc,0x98);
    ALU_BASE(and,0xA0);ALU_BASE(xor,0xA8);ALU_BASE(or,0xB0);ALU_BASE(cp,0xB8);
    main_opcodes[0xC6]=op_add_i;main_opcodes[0xCE]=op_adc_i;main_opcodes[0xD6]=op_sub_i;main_opcodes[0xDE]=op_sbc_i;
    main_opcodes[0xE6]=op_and_i;main_opcodes[0xEE]=op_xor_i;main_opcodes[0xF6]=op_or_i;main_opcodes[0xFE]=op_cp_i;
    main_opcodes[0xC0]=op_ret_nz;main_opcodes[0xC1]=op_pop_bc;main_opcodes[0xC2]=op_jp_nz;main_opcodes[0xC3]=op_jp;
    main_opcodes[0xC4]=op_call_nz;main_opcodes[0xC5]=op_push_bc;main_opcodes[0xC7]=op_rst_0x00;main_opcodes[0xC8]=op_ret_z;
    main_opcodes[0xC9]=op_ret;main_opcodes[0xCA]=op_jp_z;main_opcodes[0xCC]=op_call_z;main_opcodes[0xCD]=op_call;
    main_opcodes[0xCF]=op_rst_0x08;main_opcodes[0xD0]=op_ret_nc;main_opcodes[0xD1]=op_pop_de;main_opcodes[0xD2]=op_jp_nc;
    main_opcodes[0xD4]=op_call_nc;main_opcodes[0xD5]=op_push_de;main_opcodes[0xD7]=op_rst_0x10;main_opcodes[0xD8]=op_ret_c;
    main_opcodes[0xD9]=op_reti;main_opcodes[0xDA]=op_jp_c;main_opcodes[0xDC]=op_call_c;main_opcodes[0xDF]=op_rst_0x18;
    main_opcodes[0xE0]=op_ldh_i_a;main_opcodes[0xE1]=op_pop_hl;main_opcodes[0xE2]=op_ld_cp_a;main_opcodes[0xE5]=op_push_hl;
    main_opcodes[0xE7]=op_rst_0x20;main_opcodes[0xE8]=op_add_sp_r8;main_opcodes[0xE9]=op_jp_hl;main_opcodes[0xEA]=op_ld_i16_a;
    main_opcodes[0xEF]=op_rst_0x28;main_opcodes[0xF0]=op_ldh_a_i;main_opcodes[0xF1]=op_pop_af;main_opcodes[0xF2]=op_ld_a_cp;
    main_opcodes[0xF3]=op_di;main_opcodes[0xF5]=op_push_af;main_opcodes[0xF7]=op_rst_0x30;main_opcodes[0xF8]=op_ld_hl_sp_r8;
    main_opcodes[0xF9]=op_ld_sp_hl;main_opcodes[0xFA]=op_ld_a_i16;main_opcodes[0xFB]=op_ei;main_opcodes[0xFF]=op_rst_0x38;

    #define CB_REG(name,base) do{cb_opcodes[base]=cb_##name##_b;cb_opcodes[base+1]=cb_##name##_c;cb_opcodes[base+2]=cb_##name##_d;cb_opcodes[base+3]=cb_##name##_e;cb_opcodes[base+4]=cb_##name##_h;cb_opcodes[base+5]=cb_##name##_l;cb_opcodes[base+6]=cb_##name##_hl;cb_opcodes[base+7]=cb_##name##_a;}while(0)
    CB_REG(rlc,0x00);CB_REG(rrc,0x08);CB_REG(rl,0x10);CB_REG(rr,0x18);
    CB_REG(sla,0x20);CB_REG(sra,0x28);CB_REG(swap,0x30);CB_REG(srl,0x38);

    static op_fn bt[8][8],st[8][8],rt[8][8];
    static int init=0;
    if(!init){
        op_fn *b=&bt[0][0],*s=&st[0][0],*r=&rt[0][0];
        b[0]=cb_b0_b;b[1]=cb_b0_c;b[2]=cb_b0_d;b[3]=cb_b0_e;b[4]=cb_b0_h;b[5]=cb_b0_l;b[6]=cb_b0_hl;b[7]=cb_b0_a;
        b[8]=cb_b1_b;b[9]=cb_b1_c;b[10]=cb_b1_d;b[11]=cb_b1_e;b[12]=cb_b1_h;b[13]=cb_b1_l;b[14]=cb_b1_hl;b[15]=cb_b1_a;
        b[16]=cb_b2_b;b[17]=cb_b2_c;b[18]=cb_b2_d;b[19]=cb_b2_e;b[20]=cb_b2_h;b[21]=cb_b2_l;b[22]=cb_b2_hl;b[23]=cb_b2_a;
        b[24]=cb_b3_b;b[25]=cb_b3_c;b[26]=cb_b3_d;b[27]=cb_b3_e;b[28]=cb_b3_h;b[29]=cb_b3_l;b[30]=cb_b3_hl;b[31]=cb_b3_a;
        b[32]=cb_b4_b;b[33]=cb_b4_c;b[34]=cb_b4_d;b[35]=cb_b4_e;b[36]=cb_b4_h;b[37]=cb_b4_l;b[38]=cb_b4_hl;b[39]=cb_b4_a;
        b[40]=cb_b5_b;b[41]=cb_b5_c;b[42]=cb_b5_d;b[43]=cb_b5_e;b[44]=cb_b5_h;b[45]=cb_b5_l;b[46]=cb_b5_hl;b[47]=cb_b5_a;
        b[48]=cb_b6_b;b[49]=cb_b6_c;b[50]=cb_b6_d;b[51]=cb_b6_e;b[52]=cb_b6_h;b[53]=cb_b6_l;b[54]=cb_b6_hl;b[55]=cb_b6_a;
        b[56]=cb_b7_b;b[57]=cb_b7_c;b[58]=cb_b7_d;b[59]=cb_b7_e;b[60]=cb_b7_h;b[61]=cb_b7_l;b[62]=cb_b7_hl;b[63]=cb_b7_a;
        s[0]=cb_s0_b;s[1]=cb_s0_c;s[2]=cb_s0_d;s[3]=cb_s0_e;s[4]=cb_s0_h;s[5]=cb_s0_l;s[6]=cb_s0_hl;s[7]=cb_s0_a;
        s[8]=cb_s1_b;s[9]=cb_s1_c;s[10]=cb_s1_d;s[11]=cb_s1_e;s[12]=cb_s1_h;s[13]=cb_s1_l;s[14]=cb_s1_hl;s[15]=cb_s1_a;
        s[16]=cb_s2_b;s[17]=cb_s2_c;s[18]=cb_s2_d;s[19]=cb_s2_e;s[20]=cb_s2_h;s[21]=cb_s2_l;s[22]=cb_s2_hl;s[23]=cb_s2_a;
        s[24]=cb_s3_b;s[25]=cb_s3_c;s[26]=cb_s3_d;s[27]=cb_s3_e;s[28]=cb_s3_h;s[29]=cb_s3_l;s[30]=cb_s3_hl;s[31]=cb_s3_a;
        s[32]=cb_s4_b;s[33]=cb_s4_c;s[34]=cb_s4_d;s[35]=cb_s4_e;s[36]=cb_s4_h;s[37]=cb_s4_l;s[38]=cb_s4_hl;s[39]=cb_s4_a;
        s[40]=cb_s5_b;s[41]=cb_s5_c;s[42]=cb_s5_d;s[43]=cb_s5_e;s[44]=cb_s5_h;s[45]=cb_s5_l;s[46]=cb_s5_hl;s[47]=cb_s5_a;
        s[48]=cb_s6_b;s[49]=cb_s6_c;s[50]=cb_s6_d;s[51]=cb_s6_e;s[52]=cb_s6_h;s[53]=cb_s6_l;s[54]=cb_s6_hl;s[55]=cb_s6_a;
        s[56]=cb_s7_b;s[57]=cb_s7_c;s[58]=cb_s7_d;s[59]=cb_s7_e;s[60]=cb_s7_h;s[61]=cb_s7_l;s[62]=cb_s7_hl;s[63]=cb_s7_a;
        r[0]=cb_r0_b;r[1]=cb_r0_c;r[2]=cb_r0_d;r[3]=cb_r0_e;r[4]=cb_r0_h;r[5]=cb_r0_l;r[6]=cb_r0_hl;r[7]=cb_r0_a;
        r[8]=cb_r1_b;r[9]=cb_r1_c;r[10]=cb_r1_d;r[11]=cb_r1_e;r[12]=cb_r1_h;r[13]=cb_r1_l;r[14]=cb_r1_hl;r[15]=cb_r1_a;
        r[16]=cb_r2_b;r[17]=cb_r2_c;r[18]=cb_r2_d;r[19]=cb_r2_e;r[20]=cb_r2_h;r[21]=cb_r2_l;r[22]=cb_r2_hl;r[23]=cb_r2_a;
        r[24]=cb_r3_b;r[25]=cb_r3_c;r[26]=cb_r3_d;r[27]=cb_r3_e;r[28]=cb_r3_h;r[29]=cb_r3_l;r[30]=cb_r3_hl;r[31]=cb_r3_a;
        r[32]=cb_r4_b;r[33]=cb_r4_c;r[34]=cb_r4_d;r[35]=cb_r4_e;r[36]=cb_r4_h;r[37]=cb_r4_l;r[38]=cb_r4_hl;r[39]=cb_r4_a;
        r[40]=cb_r5_b;r[41]=cb_r5_c;r[42]=cb_r5_d;r[43]=cb_r5_e;r[44]=cb_r5_h;r[45]=cb_r5_l;r[46]=cb_r5_hl;r[47]=cb_r5_a;
        r[48]=cb_r6_b;r[49]=cb_r6_c;r[50]=cb_r6_d;r[51]=cb_r6_e;r[52]=cb_r6_h;r[53]=cb_r6_l;r[54]=cb_r6_hl;r[55]=cb_r6_a;
        r[56]=cb_r7_b;r[57]=cb_r7_c;r[58]=cb_r7_d;r[59]=cb_r7_e;r[60]=cb_r7_h;r[61]=cb_r7_l;r[62]=cb_r7_hl;r[63]=cb_r7_a;
        init=1;
    }
    for(int b=0;b<8;b++)for(int r=0;r<8;r++){
        cb_opcodes[0x40+(b<<3)+r]=bt[b][r];
        cb_opcodes[0x80+(b<<3)+r]=rt[b][r];
        cb_opcodes[0xC0+(b<<3)+r]=st[b][r];
    }
}
