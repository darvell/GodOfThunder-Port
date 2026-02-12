// THOR - The God of Thunder
//Source code released to the public domain on March 27th, 2020.

#include <stdlib.h>
#include <stdio.h>
#include <dos.h>
#include <malloc.h>
#include <string.h>

#include "res_man.h"
#include "game_define.h"
#include "game_proto.h"

#define STAT_COLOR 206

extern ACTOR *thor;
extern THOR_INFO thor_info;
extern char *tmp_buff;
extern char far objects[NUM_OBJECTS][262];
extern unsigned int page[3];
extern volatile char key_flag[100];
extern int  key_fire,key_up,key_down,key_left,key_right,key_magic,key_select;
extern unsigned int display_page,draw_page;
extern char far *bg_pics;
extern int  restore_screen;
extern char hampic[4][262];
extern volatile unsigned int timer_cnt,extra_cnt;
extern char level_type,slow_mode;
extern struct sup setup;
extern int music_flag,sound_flag,pcsound_flag,boss_active;
extern char *options_yesno[];
extern int exit_flag;
extern char cheat;
char *options_onoff[]={"On","Off",NULL};
char *options_sound[]={"None","PC Speaker","Digitized",NULL};
char *options_skill[]={"Easy Enemies","Normal Enemies","Tough Enemies",NULL};
char *options_slow[]={"On  (slow computer)","Off (fast computer)",NULL};
#ifdef __llvm__
char *options_menu[]={"Save Game","Load Game",
                      "Die","Restart","Help","Debug","Settings","Quit",NULL};
#else
char *options_menu[]={"Sound/Music","Skill Level","Save Game","Load Game",
                      "Die","Turbo Mode","Help","Quit",NULL};
#endif
#ifdef __llvm__
char *options_quit[]={"Continue Game","Quit to Opening Screen","Quit to Desktop",NULL};
#else
char *options_quit[]={"Continue Game","Quit to Opening Screen","Quit to DOS",NULL};
#endif
extern char far *scr;
extern char last_setup[32];
#ifdef __llvm__
extern ACTOR actor[MAX_ACTORS];
extern int current_level, new_level;
char debug_god_mode = 0;
char debug_noclip_mode = 0;
#endif
//===========================================================================
//void status_panel(void){
//int i;
//int c[]={23,25,27,29,20};

//for(i=0;i<5;i++) xfillrectangle(0+i,0+i,320-i,48-i,PAGES,c[i]);
//xprint(8,6,"Health",PAGES,6);
//xfillrectangle(64,6,165,14,PAGES,0);
//display_health();
//xprint(8,16,"Jewels",PAGES,6);
//xprint(24,28,"Keys",PAGES,6);

//}
//===========================================================================
void display_health(void){
int b;

b=59+thor->health;
xfillrectangle(59,8,b,12,PAGES,32);
xfillrectangle(b,8,209,12,PAGES,STAT_COLOR);
}
//===========================================================================
void display_magic(void){
int b;

b=59+thor_info.magic;
xfillrectangle(59,20,b,24,PAGES,96);
xfillrectangle(b,20,209,24,PAGES,STAT_COLOR);
}
//===========================================================================
void display_jewels(void){
char s[21];
int x,l;

itoa(thor_info.jewels,s,10);
l=strlen(s);

if (l==1) x=70;
else if (l==2) x=66;
else x=62;

xfillrectangle(59,32,85,42,PAGES,STAT_COLOR);
xprint(x,32,s,PAGES,14);
}
//===========================================================================
void display_score(void){
char s[21];
int x,l;

ultoa(thor_info.score,s,10);

l=strlen(s);
x=276-(l*8);

xfillrectangle(223,32,279,42,PAGES,STAT_COLOR);
xprint(x,32,s,PAGES,14);
}
//===========================================================================
void display_keys(void){
char s[21];
int x,l;

itoa(thor_info.keys,s,10);
l=strlen(s);

if (l==1) x=150;
else if (l==2) x=146;
else x=142;

xfillrectangle(139,32,165,42,PAGES,STAT_COLOR);  //215
xprint(x,32,s,PAGES,14);
}
//===========================================================================
void display_item(void){

xfillrectangle(280,8,296,24,PAGES,STAT_COLOR);
if(thor_info.item){
 if(thor_info.item==7) xfput(282,8,PAGES,(char far *) objects[thor_info.object+10]);
 else xfput(282,8,PAGES,(char far *) objects[thor_info.item+25]);
}
}
//===========================================================================
int init_status_panel(void){
char far *sp;

sp=res_falloc_read("STATUS");
if(!sp) return 0;

xfarput(0,0,PAGES,sp);
//xfillrectangle(61,32,87,42,PAGES,STAT_COLOR);
//xfillrectangle(183,32,209,42,PAGES,STAT_COLOR);
//xfillrectangle(222,16,272,33,PAGES,STAT_COLOR);
display_item();
farfree(sp);
return 1;
}
//===========================================================================
void add_jewels(int num){
int n;

n=thor_info.jewels+num;
if(n>999) n=999;
else if(n<0) n=0;
thor_info.jewels=n;
display_jewels();
}
//===========================================================================
void add_score(int num){
long n;

n=thor_info.score+(long) num;
if(n>999999l) n=999999l;
else if(n<0) n=0;
thor_info.score=n;
display_score();
}
//===========================================================================
void add_magic(int num){
int n;

n=thor_info.magic+num;
if(n>150) n=150;
else if(n<0) n=0;
thor_info.magic=n;
display_magic();
}
//===========================================================================
void add_health(int num){
int n;

n=thor->health+num;
if(n>150) n=150;
else if(n<0) n=0;
thor->health=n;
display_health();
if(thor->health<1) exit_flag=2;
}
//===========================================================================
void add_keys(int num){
int n;

n=thor_info.keys+num;
if(n>99) n=99;
else if(n<0) n=0;
thor_info.keys=n;
display_keys();
}
//===========================================================================
void fill_health(void){

//while(thor->health<150){
//   if(!sound_playing()) play_sound(ANGEL,1);
   add_health(150);
//   got_pause(4);
//}
}
//===========================================================================
void fill_magic(void){

//while(thor_info.magic<150){
//   if(!sound_playing()) play_sound(ANGEL,1);
   add_magic(150);
//   got_pause(4);
//}
}
//===========================================================================
void fill_score(int num){

while(num){
   num--;
   play_sound(WOOP,1);
   add_score(1000);
   got_pause(8);
}
}
//===========================================================================
void score_for_inv(void){

while(thor->health){
   thor->health--;
   play_sound(WOOP,1);
   add_health(-1);
   add_score(10);
   got_pause(8);
}
while(thor_info.magic){
   thor_info.magic--;
   play_sound(WOOP,1);
   add_magic(-1);
   add_score(10);
   got_pause(8);
}
while(thor_info.jewels){
   thor_info.jewels--;
   play_sound(WOOP,1);
   add_jewels(-1);
   add_score(10);
   got_pause(8);
}
}
//===========================================================================
void boss_status(int health){
int rep,i,c;

if(health==-1){
  REPEAT(3){
    xfillrectangle(304,2,317,81,page[rep],0);
    xfillrectangle(305,3,316,80,page[rep],28);
    xfillrectangle(306,4,315,79,page[rep],26);
    xfillrectangle(307,5,314,78,page[rep],24);
  }
  health=100;
}
for(i=10;i>0;i--){
  if(i*10 > health) c=0;
  else c=32;
  REPEAT(3){
    xfillrectangle(308,7+(7*(10-i)),313,13+(7*(10-i)),page[rep],c);
  }
}
}
//===========================================================================
int select_option(char *option[],char *title,int ipos){
int num_opts,x1,y1,x2,y2,w,h;
int s,i,pic,pos,key,y,kf,ret;
unsigned int pg;
char **op;

// Episode 1: show_all_actors; Episodes 2-3: play_sound
if(g_episode == 1) show_all_actors();
else play_sound(WOOP,1);
num_opts=0;
w=strlen(title);
op=option;
while(*op){
  if(strlen(*op)>w) w=strlen(*op);
  num_opts++;
  op++;
}
if(w & 1) w++;
w=(w*8)+32;
s=w/16;
h=(num_opts*16)+32;
x1=(320-w)/2;
x2=(x1+w)-1;
y1=(192-h)/2;
y2=(y1+h)-1;
if(x1 & 1) x1++;
if(x2 & 1) x2++;

pg=display_page;

xfillrectangle(x1,y1,x2,y2,pg,215);

xfput(x1-16,y1-16,pg,(char far *) (bg_pics+(192*262)));
xfput(x2,y1-16,pg,(char far *) (bg_pics+(193*262)));
xfput(x1-16,y2,pg,(char far *) (bg_pics+(194*262)));
xfput(x2,y2,pg,(char far *) (bg_pics+(195*262)));
for(i=0;i<s;i++){
   xfput(x1+(i*16),y1-16,pg,(char far *) (bg_pics+(196*262)));
   xfput(x1+(i*16),y2,pg,(char far *) (bg_pics+(197*262)));
}
for(i=0;i<(num_opts+2);i++){
   xfput(x1-16,y1+(i*16),pg,(char far *) (bg_pics+(198*262)));
   xfput(x2,y1+(i*16),pg,(char far *) (bg_pics+(199*262)));
}
s=strlen(title)*8;
i=(320-s)/2;
xprint(i,y1+4,title,pg,54);

op=option;
for(i=0;i<num_opts;i++){
   xprint(x1+32,(y1+28)+(i*16),*op,pg,14);
   op++;
}

pos=ipos;
pic=0;
kf=0;
y=y1+24+(pos*16);
wait_not_response();
wait_not_key(UP);
wait_not_key(DOWN);
extra_cnt=0;
ret=0;

while(1){
    if(extra_cnt>15){
      kf=0;
      extra_cnt=0;
    }
    xfillrectangle(x1+8,y,x1+24,y+16,pg,215);
    y=y1+24+(pos*16);
    xput(x1+8,y,pg,hampic[pic]);
    pic++;
    if(pic>3){
      pic=0;
    }
    timer_cnt=0;
    while(timer_cnt<10) rotate_pal();
    key=get_response();
    if(key==ENTER || key==SPACE || key==key_fire || key==key_magic){
      hammer_smack(x1+8,y);
      ret=pos+1;
      break;
    }
    if(key==ESC) break;
    if(key_flag[UP]) key=UP;
    else if(key_flag[DOWN]) key=DOWN;
    if(key==UP || key==DOWN){
      if(!kf){
        if(key==UP){
          pos--;
          if(pos<0) pos=num_opts-1;
        }
        if(key==DOWN){
          pos++;
          if(pos>=num_opts) pos=0;
        }
        play_sound(WOOP,1);
        kf=1;
        extra_cnt=0;
      }
    }
    else{
      kf=0;
    }
}
wait_not_response();
restore_screen=1;
return ret;
}
//===========================================================================
int option_menu(void){

return select_option(options_menu,"Options Menu",0);
}
//===========================================================================
int ask_exit(void){

return select_option(options_quit,"Quit Game?",0);
}
//===========================================================================
int select_sound(void){
int ret,sel;

sel=0;
if(setup.pc_sound) sel=1;
else if(setup.dig_sound) sel=2;

ret=select_option(options_sound,"  Set Sound  ",sel);
d_restore();
if(!ret) return 0;
if(ret==1){
  setup.pc_sound=0;
  setup.dig_sound=0;
}
if(ret==2){
  setup.pc_sound=1;
  setup.dig_sound=0;
}
if(ret==3){
  if(!sound_flag){
    odin_speaks(2001,0);
    return 1;
  }
  setup.pc_sound=0;
  setup.dig_sound=1;
}
memcpy(last_setup,&setup,32);
return 1;
}
//===========================================================================
int select_music(void){
int ret;

if(!music_flag) return 1;
//if(!setup.music) return 1;
//if(!music_flag){
//  odin_speaks(2002,0);
//  return 1;
//}
ret=select_option(options_onoff,"Set Music",1-setup.music);
if(!ret) return 0;
if(ret==1){
  if(setup.music) return 1;
  setup.music=1;
  if(!boss_active) music_play(level_type,1);
  else music_play(4,1);

}
else if(ret==2){
  music_pause();
  setup.music=0;
}
memcpy(last_setup,&setup,32);
return 1;
}
//===========================================================================
int select_slow(void){
int ret;

ret=select_option(options_slow,"Fast Mode",1-slow_mode);
if(!ret) return 0;
if(ret==1) slow_mode=1;
if(ret==2) slow_mode=0;
setup.speed=slow_mode;
memcpy(last_setup,&setup,32);
return 1;
}
//===========================================================================
int select_scroll(void){
int ret;

ret=select_option(options_yesno,"Scroll Between Screens?",1-setup.scroll_flag);
if(!ret) return 0;
if(ret==1) setup.scroll_flag=1;
if(ret==2) setup.scroll_flag=0;
memcpy(last_setup,&setup,32);
return 1;
}
//===========================================================================
void select_fastmode(void){

if(select_slow()) select_scroll();
}
//===========================================================================
void select_skill(void){
int ret,sel;

sel=setup.skill;
ret=select_option(options_skill,"  Set Skill Level ",sel);
if(!ret) return;
if(ret) setup.skill=ret-1;
memcpy(last_setup,&setup,32);
}
//===========================================================================
void hammer_smack(int x,int y){
int i;

for(i=0;i<4;i++){
   xfillrectangle(x-4,y-4,x+16,y+16,display_page,215);
   x+=2;
   xput(x,y,display_page,hampic[0]);
   got_pause(3);
}

play_sound(CLANG,1);

for(i=0;i<4;i++){
   xfillrectangle(x,y,x+16,y+16,display_page,215);
   x-=2;
   xput(x,y,display_page,hampic[0]);
   got_pause(3);
}

}
//===========================================================================
#ifdef __llvm__
static int debug_pick_number(char *title, int cur, int min, int max, int step){
int orig,key,kf;
char s[21];
unsigned int pg;
int x1,y1,x2,y2;

orig=cur;
x1=60; y1=70; x2=260; y2=114;
pg=display_page;
kf=0;
extra_cnt=0;
wait_not_response();
wait_not_key(LEFT);
wait_not_key(RIGHT);

while(1){
    xfillrectangle(x1,y1,x2,y2,pg,215);
    xprint(x1+16,y1+8,title,pg,54);
    itoa(cur,s,10);
    xfillrectangle(x1+16,y1+24,x2-16,y1+36,pg,215);
    xprint(x1+16,y1+24,"< ",pg,14);
    xprint(x1+32,y1+24,s,pg,14);
    xprint(x2-32,y1+24," >",pg,14);
    timer_cnt=0;
    while(timer_cnt<6) rotate_pal();
    if(extra_cnt>10){
      kf=0;
      extra_cnt=0;
    }
    key=get_response();
    if(key==ENTER || key==SPACE || key==key_fire) break;
    if(key==ESC){ cur=orig; break; }
    if(key_flag[LEFT]) key=LEFT;
    else if(key_flag[RIGHT]) key=RIGHT;
    else if(key_flag[UP]) key=UP;
    else if(key_flag[DOWN]) key=DOWN;
    if(key==LEFT || key==RIGHT || key==UP || key==DOWN){
      if(!kf){
        if(key==RIGHT){ cur+=1; if(cur>max) cur=max; }
        else if(key==LEFT){ cur-=1; if(cur<min) cur=min; }
        else if(key==UP){ cur+=step; if(cur>max) cur=max; }
        else if(key==DOWN){ cur-=step; if(cur<min) cur=min; }
        play_sound(WOOP,1);
        kf=1;
        extra_cnt=0;
      }
    }
    else kf=0;
}
wait_not_response();
restore_screen=1;
return cur;
}
//===========================================================================
void debug_menu(void){
char *opts[] = {
    "Warp to Screen", "Set Health", "Set Magic",
    "Set Jewels", "Set Keys", "Set Score",
    "Give All Items", "Toggle God Mode",
    "Toggle Noclip", "Kill All Enemies", NULL
};
int sel, val, i;

sel = select_option(opts, "Debug Menu", 0);
d_restore();
switch(sel){
    case 1:
        val = debug_pick_number("Screen #", current_level, 0, 239, 10);
        d_restore();
        if(val != current_level){
            new_level = val;
            thor->x = 152;
            thor->y = 80;
            thor->last_x[0] = 152;
            thor->last_x[1] = 152;
            thor->last_y[0] = 80;
            thor->last_y[1] = 80;
        }
        break;
    case 2:
        val = debug_pick_number("Health", thor->health, 1, 150, 10);
        d_restore();
        thor->health = val; display_health();
        break;
    case 3:
        val = debug_pick_number("Magic", thor_info.magic, 0, 150, 10);
        d_restore();
        thor_info.magic = val; display_magic();
        break;
    case 4:
        val = debug_pick_number("Jewels", thor_info.jewels, 0, 999, 10);
        d_restore();
        thor_info.jewels = val; display_jewels();
        break;
    case 5:
        val = debug_pick_number("Keys", thor_info.keys, 0, 99, 10);
        d_restore();
        thor_info.keys = val; display_keys();
        break;
    case 6:
        val = debug_pick_number("Score", (int)thor_info.score, 0, 999999, 1000);
        d_restore();
        thor_info.score = (long)val; display_score();
        break;
    case 7:
        thor_info.inventory = 0x3f;
        display_item();
        break;
    case 8:
        debug_god_mode = !debug_god_mode;
        break;
    case 9:
        debug_noclip_mode = !debug_noclip_mode;
        break;
    case 10:
        for(i=3; i<MAX_ACTORS; i++){
            if(actor[i].used){
                actor_destroyed(&actor[i]);
            }
        }
        break;
}
}
//===========================================================================
int restart_episode(void){
int ret;
extern char *save_filename;

ret=select_option(options_yesno,"Restart Episode?",1);
d_restore();
if(ret!=1) return 0;
ret=select_option(options_yesno,"Are You Sure?",1);
d_restore();
if(ret!=1) return 0;
remove(save_filename);
return 1;
}
#endif
