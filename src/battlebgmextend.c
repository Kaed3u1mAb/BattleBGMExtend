/* BattleBGMExtend: game-thread-only rules and music arbitration.
 * Resolve IL2CPP metadata at runtime. No game RVAs or private audio are shipped.
 * All retained managed objects use pointer-sized Unity 6 GC handles.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "MinHook.h"
#include "bgm_layout.h"
#include "il2cpp_api.h"

static API api;
static HMODULE game;
static HANDLE map_handle;
static BGM_CONFIG *cfg;
static DWORD permission_tls=TLS_OUT_OF_INDEXES;
static DWORD main_thread;
static bool busy,suspended;
static void *stage_pointer;
static uintptr_t stage_handle;
static BGM_RULE rules[BGM_MAX_RULES];
static int keywords[BGM_MAX_RULES],from_keywords[BGM_MAX_RULES];
/* Roots keep per-instance transition state safe across moving GC and waves. */
typedef struct {uintptr_t root;uint32_t normal,over,pending;} UnitState;
static UnitState states[512];
static unsigned state_count;
static bool round_open,start_checked,end_waiting;
static void reset_tracking(void)
{
 for(unsigned i=0;i<state_count;i++)api.unroot(states[i].root);
 memset(states,0,sizeof(states));state_count=0;round_open=start_checked=end_waiting=false;
}
static unsigned rule_count;
static uint32_t generation;
static bool rules_valid;
static DWORD retry_after[BGM_MAX_RULES];
static uintptr_t owned_instance;
static void *f_stage_instance,*f_stage_model,*f_phase,*f_previous_phase,*f_bom_instance,*f_sound_instance;
static void *f_buff_detail,*f_battle_current,*k_keywords,*k_ally;
static void *f_passive_owner,*f_rodion_gained;
static void *m_origin_id,*m_buff_stack,*m_has_buff,*m_models,*m_try_create,*m_load_bank,*m_bank_loaded;
static void *m_start_add,*m_stop_battle,*m_stop_add,*m_start_battle;
static void *m_scene_battle,*m_stop_sound;
static int (*event_valid)(uintptr_t);
static int (*event_release)(uintptr_t);
static int (*event_playback)(uintptr_t,int *);
static void unlock(bool restore);

static void report(int error,const char *text)
{
 if(!cfg || (cfg->error==error&&!strcmp(cfg->message,text)))return;
 cfg->error=error;snprintf(cfg->message,sizeof(cfg->message),"%s",text);
 InterlockedIncrement((LONG *)&cfg->sequence);
}
static bool enabled(void)
{return cfg&&cfg->enabled&&(uint32_t)(GetTickCount()-cfg->heartbeat)<=BGM_TIMEOUT_MS;}
static bool own_call(void)
{return permission_tls!=TLS_OUT_OF_INDEXES&&TlsGetValue(permission_tls)!=NULL;}
static void permit(bool value)
{TlsSetValue(permission_tls,value?(void *)1:NULL);}
static void *read_ref(void *obj,void *field)
{void *p=NULL;if(obj&&field)api.read(obj,field,&p);return p;}
static int read_int(void *obj,void *field)
{int n=0;if(obj&&field)api.read(obj,field,&n);return n;}
static void *singleton(void *field)
{void *p=NULL;if(field)api.static_value(field,&p);return p;}
static bool call(void *method,void *obj,void **args,void **result)
{
 if(!method)return false;
 void *exception=NULL,*value=api.invoke(method,obj,args,&exception);
 if(result)*result=value;
 if(exception){
  char msg[256];snprintf(msg,sizeof(msg),"Managed exception: %s",api.method_name(method));
  report(20,msg);return false;
 }
 return true;
}
static bool bool_call(void *method,void *obj,void **args,bool *value)
{void *r=NULL;if(!call(method,obj,args,&r)||!r)return false;*value=*(bool *)api.unbox(r);return true;}
/* WAIT is an animation/production pause inside combat, not a scene exit. */
static int effective_phase(void *sc)
{
 if(!sc)return 24;
 int phase=read_int(sc,f_phase);
 return phase==0?read_int(sc,f_previous_phase):phase;
}
static bool battle_context(void)
{
 bool in_battle=false;
 if(!bool_call(m_scene_battle,NULL,NULL,&in_battle)||!in_battle)return false;
 void *sc=singleton(f_stage_instance),*model=read_ref(sc,f_stage_model);
 int phase=effective_phase(sc);cfg->phase=phase;
 return model&&model==stage_pointer&&!suspended&&phase>=2&&phase<=20;
}
/* Every intercepted music path rechecks lifecycle; a late result/scene request
 * must pass even if the UI Update callback has not run since combat ended. */
static bool block_game(void)
{
 if(own_call()||!cfg||!cfg->locked||!enabled())return false;
 if(!battle_context()){unlock(false);return false;}
 InterlockedIncrement((LONG *)&cfg->blocked);return true;
}
typedef int (*NativeStopFn)(uintptr_t,int);
typedef int (*NativeReleaseFn)(uintptr_t);
typedef int (*NativeParameterFn)(uintptr_t,const char *,float,int);
static NativeStopFn original_native_stop;
static NativeReleaseFn original_native_release;
static NativeParameterFn original_native_parameter;
/* Timeline code can call FMOD directly. Check only native state here: audio
 * callbacks are not necessarily attached IL2CPP/Unity main threads. */
static bool protect_event(uintptr_t handle)
{return handle&&handle==owned_instance&&cfg&&cfg->locked&&enabled()&&!own_call();}
static int hooked_native_stop(uintptr_t handle,int mode)
{if(protect_event(handle)){InterlockedIncrement((LONG *)&cfg->blocked);return 0;}return original_native_stop(handle,mode);}
static int hooked_native_release(uintptr_t handle)
{if(protect_event(handle)){InterlockedIncrement((LONG *)&cfg->blocked);return 0;}return original_native_release(handle);}
static int hooked_native_parameter(uintptr_t handle,const char *name,float value,int ignore)
{if(protect_event(handle)){InterlockedIncrement((LONG *)&cfg->blocked);return 0;}return original_native_parameter(handle,name,value,ignore);}

typedef void (*InstanceVoid)(void *,const void *);
typedef void (*InstanceBoolVoid)(void *,bool,const void *);
typedef void (*StaticIntVoid)(int,const void *);
typedef void (*StaticTwoIntVoid)(int,int,const void *);
typedef void (*StaticTwoBoolVoid)(bool,bool,const void *);
typedef void (*StaticIntBoolVoid)(int,bool,const void *);
typedef void (*ApplyFn)(void *,bool,const void *);
typedef void (*ChangeFn)(void *,uintptr_t,bool,void *,const void *);
typedef void (*CurrentChangeFn)(void *,uintptr_t,bool,float,void *,const void *);
typedef void (*AddFn)(void *,int,bool,const void *);
typedef bool (*StaticStringBool)(void *,const void *);
typedef bool (*StaticIntBool)(int,const void *);
typedef bool (*ParameterFn)(void *,void *,float,bool,const void *);
typedef void (*EventFn)(uintptr_t,const void *);
typedef void (*StaticVoid)(const void *);
typedef void (*SecondFn)(void *,bool,uintptr_t,const void *);
static InstanceVoid original_update,original_release;
static InstanceBoolVoid original_sound_stop,original_current_stop;
static StaticIntVoid original_set_change;
static StaticTwoIntVoid original_battle_start;
static StaticTwoBoolVoid original_battle_stop;
static StaticIntBoolVoid original_add_stop;
static ApplyFn original_apply;
static ChangeFn original_change;
static CurrentChangeFn original_current_change;
static AddFn original_add;
static StaticStringBool original_by_name,original_interlude,original_interlude_path;
static StaticIntBool original_resume;
static ParameterFn original_parameter;
static EventFn original_choice;
static SecondFn original_second;
static StaticVoid original_reset;

static void hooked_apply(void *data,bool force,const void *mi)
{if(!block_game())original_apply(data,force,mi);}
static void hooked_change(void *self,uintptr_t event,bool force,void *data,const void *mi)
{if(!block_game())original_change(self,event,force,data,mi);}
static void hooked_current_change(void *self,uintptr_t event,bool force,float volume,void *data,const void *mi)
{if(!block_game())original_current_change(self,event,force,volume,data,mi);}
static void hooked_sound_stop(void *s,bool fade,const void *mi)
{if(!block_game())original_sound_stop(s,fade,mi);}
static void hooked_current_stop(void *s,bool fade,const void *mi)
{if(!block_game())original_current_stop(s,fade,mi);}
static void hooked_release(void *s,const void *mi)
{if(!block_game())original_release(s,mi);}
static void hooked_set_change(int i,const void *mi)
{if(!block_game())original_set_change(i,mi);}
static void hooked_battle_start(int i,int offset,const void *mi)
{if(!block_game())original_battle_start(i,offset,mi);}
static void hooked_battle_stop(bool release,bool fade,const void *mi)
{if(!block_game())original_battle_stop(release,fade,mi);}
static void hooked_add_stop(int mode,bool release,const void *mi)
{if(!block_game())original_add_stop(mode,release,mi);}
static void hooked_add(void *path,int mode,bool release,const void *mi)
{if(!block_game())original_add(path,mode,release,mi);}
static bool hooked_by_name(void *name,const void *mi)
{return block_game()?true:original_by_name(name,mi);}
static bool hooked_resume(int i,const void *mi)
{return block_game()?true:original_resume(i,mi);}
static bool hooked_interlude(void *name,const void *mi)
{return block_game()?true:original_interlude(name,mi);}
static bool hooked_interlude_path(void *path,const void *mi)
{return block_game()?true:original_interlude_path(path,mi);}
static bool hooked_parameter(void *s,void *name,float value,bool ignore,const void *mi)
{return block_game()?true:original_parameter(s,name,value,ignore,mi);}
static void hooked_choice(uintptr_t event,const void *mi)
{if(!block_game())original_choice(event,mi);}
static void hooked_second(void *s,bool inherence,uintptr_t event,const void *mi)
{if(!block_game())original_second(s,inherence,event,mi);}
static void hooked_reset(const void *mi)
{if(!block_game())original_reset(mi);}

static bool current_is_owned(void)
{uintptr_t p=0;api.static_value(f_battle_current,&p);return p&&p==owned_instance;}
static void unlock(bool restore)
{
 bool locked=cfg->locked!=0;
 cfg->locked=0;cfg->priority=0;cfg->active_rule=-1;cfg->active_bgm[0]=0;
 if(locked&&owned_instance&&current_is_owned()){
  bool release=false;int mode=1;void *stop_args[]={&mode,&release};
  permit(true);call(m_stop_add,NULL,stop_args,NULL);permit(false);
  if(restore&&stage_pointer&&!suspended){
   int index=-1,offset=0;void *args[]={&index,&offset};
   permit(true);call(m_start_battle,NULL,args,NULL);permit(false);
  }
 }else if(locked&&owned_instance&&event_valid(owned_instance)){
  /* Do not let a direct player reset leave an orphaned audible event. */
  original_native_stop(owned_instance,1);
 }
 owned_instance=0;
}
static void clear_stage(void)
{
 unlock(false);reset_tracking();
 if(stage_handle)api.unroot(stage_handle);
 stage_handle=0;stage_pointer=NULL;
 memset(retry_after,0,sizeof(retry_after));
}
static bool sync_stage(void)
{
 bool in_battle=false;
 if(!bool_call(m_scene_battle,NULL,NULL,&in_battle))return false;
 void *sc=in_battle?singleton(f_stage_instance):NULL;
 void *model=read_ref(sc,f_stage_model);
 if(model!=stage_pointer){
  clear_stage();suspended=false;
  if(model){stage_handle=api.root(model,true);if(!stage_handle){report(4,"Cannot retain battle stage.");return false;}stage_pointer=model;}
 }
 int phase=effective_phase(sc);cfg->phase=phase;
 if(!enabled()){
  bool was_locked=cfg->locked!=0;unlock(in_battle&&phase>=2&&phase<=20);reset_tracking();
  if(was_locked)report(0,"Music lock released; game music control restored.");
  return false;
 }
 if(!model||suspended||phase<2||phase>20){unlock(false);return false;}
 return true;
}
static bool load_rules(void)
{
 uint32_t seq=cfg->generation;
 if(seq==generation)return rules_valid;
 if(!seq||(seq&1))return false;
 unsigned count=cfg->rule_count;
 if(count>BGM_MAX_RULES)return false;
 BGM_RULE snapshot[BGM_MAX_RULES];memcpy(snapshot,cfg->rules,sizeof(snapshot));MemoryBarrier();
 if(seq!=cfg->generation)return false;
 rules_valid=false;
 for(unsigned i=0;i<count;i++){
  BGM_RULE *r=&snapshot[i];
  if(r->personality<=0||!memchr(r->keyword,0,64)||!r->keyword[0]||
     !memchr(r->path,0,128)||strncmp(r->path,"event:/BGM/Battle/",18)||
     !memchr(r->id,0,48)||!r->id[0]||!memchr(r->bank,0,64)||
     r->condition<BGM_HAS_BUFF||r->condition>BGM_END_TRANSITION||r->threshold<0||
     !memchr(r->from_keyword,0,64)||(r->condition==BGM_END_TRANSITION&&!r->from_keyword[0])){
   report(4,"Invalid rule payload.");generation=seq;return false;
  }
  void *f=api.field(k_keywords,r->keyword);
  if(!f){char msg[256];snprintf(msg,sizeof(msg),"Unknown BUFF_UNIQUE_KEYWORD: %s",r->keyword);report(4,msg);generation=seq;return false;}
  api.static_value(f,&keywords[i]);
  if(r->condition==BGM_END_TRANSITION){
   f=api.field(k_keywords,r->from_keyword);
   if(!f){report(4,"Unknown transition source keyword.");generation=seq;return false;}
   api.static_value(f,&from_keywords[i]);
  }
 }
 reset_tracking();
 memcpy(rules,snapshot,sizeof(rules));rule_count=count;generation=seq;rules_valid=true;
 memset(retry_after,0,sizeof(retry_after));return true;
}
static bool preload_bank(unsigned index,void *sound)
{
 const char *bank=rules[index].bank;
 if(!*bank)return true;
 void *bank_name=api.string_new(bank);void *args[]={bank_name};bool loaded=false;
 if(!bank_name||!bool_call(m_bank_loaded,sound,args,&loaded))return false;
 if(loaded)return true;
 wchar_t exe[32768];DWORD size=GetModuleFileNameW(NULL,exe,32768);
 if(!size||size>=32768)return false;
 wchar_t *slash=wcsrchr(exe,L'\\');if(!slash)return false;*slash=0;
 char directory[32768];
 if(!WideCharToMultiByte(CP_UTF8,0,exe,-1,directory,sizeof(directory),NULL,NULL))return false;
 /* Local banks remain game assets. The game registers/owns both banks and
  * applies its own unload policy; never load a private copy with FMOD. */
 for(unsigned asset=0;asset<2;asset++){
  char path[33024];snprintf(path,sizeof(path),"%s/LimbusCompany_Data/StreamingAssets/Assets/Sound/FMODBuilds/Desktop/%s%s.bank",directory,bank,asset?"":".assets");
  void *text=api.string_new(path);bool samples=false,nonblock=false;void *a[]={text,&samples,&nonblock};
  if(!text||!call(m_load_bank,sound,a,NULL))return false;
 }
 return true;
}
/* Cp9 Boss 10 is a parameter-driven score. Ryoshu=0 (Battle_1_0)
 * runs a silent segment despite FMOD reporting PLAYING. Select the first
 * audible battle segment, not a boss-script-only default. Other scores keep
 * their original parameters. Use our native trampoline so an existing lock
 * cannot intercept initialization of a reused event or a rollback. */
static bool initialize_score(uintptr_t event,const char *path)
{
 if(strcmp(path,"event:/BGM/Battle/Battle_Cp9_Boss_10"))return true;
 return original_native_parameter(event,"Ryoshu",1.0f,1)==0;
}
static bool switch_rule(unsigned index)
{
 BGM_RULE *r=&rules[index];
 if(cfg->locked&&r->priority<=cfg->priority)return false;
 DWORD now=GetTickCount();if(retry_after[index]&&(int32_t)(now-retry_after[index])<0)return false;
 retry_after[index]=now+3000;
 void *sound=singleton(f_sound_instance);if(!sound)return false;
 if(!preload_bank(index,sound))return false;
 void *path=api.string_new(r->path);uintptr_t test_instance=0;
 void *create_args[]={path,&test_instance};bool created=false;
 if(!path||!bool_call(m_try_create,sound,create_args,&created)||!created||!test_instance||!event_valid(test_instance)){
  if(test_instance)event_release(test_instance);
  char msg[256];snprintf(msg,sizeof(msg),"Cannot create BGM: %s; original music retained.",r->path);report(21,msg);return false;
 }
 bool initialized=initialize_score(test_instance,r->path);
 event_release(test_instance);
 if(!initialized){report(21,"Cannot initialize BGM score parameter Ryoshu=1; original music retained.");return false;}
 /* Use the battle player's own additional-BGM registry, callbacks, volume
  * policy and stage cleanup. Its short names are prefixed internally. */
 void *short_name=api.string_new(r->path+18);if(!short_name)return false;
 bool release=false,fade=false;int mode=1;void *stop[]={&release,&fade};void *add[]={short_name,&mode,&release};
 bool previously_locked=cfg->locked!=0;
 char previous_path[128]={0};if(previously_locked)snprintf(previous_path,sizeof(previous_path),"%s",cfg->active_bgm);
 permit(true);
 bool ok=call(m_stop_battle,NULL,stop,NULL);
 if(ok){bool nofade=false;void *a[]={&nofade};ok=call(m_stop_sound,sound,a,NULL);}
 if(ok)ok=call(m_start_add,NULL,add,NULL);
 uintptr_t event=0;api.static_value(f_battle_current,&event);int playback=2;
 ok=ok&&event&&event_valid(event)&&initialize_score(event,r->path)&&event_playback(event,&playback)==0&&playback!=2;
 permit(false);
 if(!ok){
  /* A failed switch never raises the priority floor. Release protection and
   * resume the original battle playlist so the game is not left silent. */
  permit(true);
  if(previously_locked&&previous_path[0]){
   void *old_name=api.string_new(previous_path+18);void *a[]={old_name,&mode,&release};
   bool restored=call(m_start_add,NULL,a,NULL);uintptr_t old=0;api.static_value(f_battle_current,&old);
   restored=restored&&old&&event_valid(old)&&initialize_score(old,previous_path)&&event_playback(old,&playback)==0&&playback!=2;
   if(restored){owned_instance=old;permit(false);report(21,"Higher-priority BGM failed; previous music lock retained.");return false;}
  }
  cfg->locked=0;cfg->priority=0;cfg->active_rule=-1;cfg->active_bgm[0]=0;owned_instance=0;
  int i=-1,offset=0;void *a[]={&i,&offset};call(m_start_battle,NULL,a,NULL);permit(false);
  report(21,"Battle player could not start BGM; resumed game playlist.");return false;
 }
 owned_instance=event;cfg->locked=1;cfg->priority=r->priority;cfg->active_rule=(int)index;
 snprintf(cfg->active_bgm,sizeof(cfg->active_bgm),"%s",r->path);cfg->changes++;
 char msg[256];snprintf(msg,sizeof(msg),"BGM locked: %s (priority %d); %.100s%s",r->id,r->priority,r->path,
  !strcmp(r->path,"event:/BGM/Battle/Battle_Cp9_Boss_10")?"; Ryoshu=1 (Battle_1_1)":"");report(0,msg);return true;
}
static bool identity_matches(void *unit,unsigned index)
{
 if(!unit||!api.assignable(k_ally,api.object_class(unit)))return false;
 void *result=NULL,*virtual_method=api.virtual_method(unit,m_origin_id);
 return call(virtual_method,unit,NULL,&result)&&result&&*(int *)api.unbox(result)==rules[index].personality;
}
static bool has_buff(void *unit,int keyword,bool *active)
{
 void *detail=read_ref(unit,f_buff_detail);if(!detail)return false;
 bool only_main=false;void *args[]={&keyword,&only_main};
 return bool_call(m_has_buff,detail,args,active);
}
static UnitState *state_for(void *unit)
{
 for(unsigned i=0;i<state_count;i++)if(api.root_target(states[i].root)==unit)return &states[i];
 if(state_count>=512){report(4,"Too many tracked battle units.");return NULL;}
 uintptr_t h=api.root(unit,true);if(!h)return NULL;
 UnitState *s=&states[state_count++];s->root=h;return s;
}
/* A source seen on this exact unit in this round arms the transition. An
 * already-overheated unit on injection or a new wave is only a baseline. */
static bool is_rodion_transition(unsigned i)
{
 return rules[i].condition==BGM_END_TRANSITION&&rules[i].personality==10916&&
  !strcmp(rules[i].keyword,"FutureEyeOffRodion")&&!strcmp(rules[i].from_keyword,"FutureEyeOnRodion");
}
static void observe_transition(void *unit,unsigned i)
{
 /* Rodion's conversion can create a zero-stack status and happens inside a
  * loss callback. Use her passive's authoritative per-turn flag instead. */
 if(is_rodion_transition(i))return;
 if(!round_open||!identity_matches(unit,i))return;
 bool normal=false,over=false;
 if(!has_buff(unit,from_keywords[i],&normal)||!has_buff(unit,keywords[i],&over))return;
 UnitState *s=state_for(unit);if(!s)return;uint32_t bit=UINT32_C(1)<<i;
 if(over&&!(s->over&bit)&&(s->normal&bit))s->pending|=bit;
 if(normal)s->normal|=bit;
 if(over)s->over|=bit;else s->over&=~bit;
}
/* event: 0 = immediate state, 1 = round start, 2 = round end */
static bool matches_event(void *unit,unsigned i,int event)
{
 if(!identity_matches(unit,i))return false;
 BGM_RULE *r=&rules[i];bool active=false;
 if(r->condition==BGM_END_TRANSITION){
  observe_transition(unit,i);
  if(event!=2)return false;
  UnitState *s=state_for(unit);return s&&(s->pending&(UINT32_C(1)<<i));
 }
 if(r->condition==BGM_HAS_BUFF)return has_buff(unit,keywords[i],&active)&&active;
 if(event!=1)return false;
 if(r->condition==BGM_START_NO_BUFF)return has_buff(unit,keywords[i],&active)&&!active;
 void *detail=read_ref(unit,f_buff_detail),*result=NULL;bool main=false;
 void *args[]={&keywords[i],&main};
 return detail&&call(m_buff_stack,detail,args,&result)&&result&&*(int *)api.unbox(result)==r->threshold;
}
static bool matches(void *unit,unsigned i){return matches_event(unit,i,0);}
static void evaluate_unit(void *unit)
{
 int candidate=-1;
 for(unsigned i=0;i<rule_count;i++){
  bool matched=matches(unit,i); /* Always record transitions, even below the current priority. */
  if(!matched||(cfg->locked&&rules[i].priority<=cfg->priority))continue;
  if(candidate<0||rules[i].priority>rules[candidate].priority)candidate=(int)i;
 }
 if(candidate>=0)switch_rule((unsigned)candidate);
}
static void scan_units(int event)
{
 void *bom=singleton(f_bom_instance),*list=NULL;
 if(!bom||!call(m_models,bom,NULL,&list)||!list)return;
 void *k=api.object_class(list),*count_m=api.method(k,"get_Count",0),*at_m=api.method(k,"get_Item",1),*count=NULL;
 if(!call(count_m,list,NULL,&count)||!count)return;
 int n=*(int *)api.unbox(count),best=-1;if(n<0||n>512)return;
 for(int j=0;j<n;j++){
  void *unit=NULL;void *args[]={&j};if(!call(at_m,list,args,&unit))break;
  for(unsigned i=0;i<rule_count;i++){
   bool matched=matches_event(unit,i,event);
   if(!matched||(cfg->locked&&rules[i].priority<=cfg->priority))continue;
   if(best<0||rules[i].priority>rules[best].priority)best=(int)i;
  }
 }
 if(best>=0)switch_rule((unsigned)best);
}
typedef void (*BuffFn)(void *,void *,int,int,int,int,int,int,void *,int,int,const void *);
static BuffFn original_buff;
static void hooked_buff(void *self,void *unit,int keyword,int stack,int turn,int round,int src,int timing,void *giver,int over_stack,int over_turn,const void *mi)
{
 bool evaluate=!busy&&GetCurrentThreadId()==main_thread&&enabled();
 if(evaluate){busy=true;if(sync_stage()&&load_rules())evaluate_unit(unit);busy=false;}
 original_buff(self,unit,keyword,stack,turn,round,src,timing,giver,over_stack,over_turn,mi);
 if(!evaluate||busy)return;
 busy=true;if(sync_stage()&&load_rules())evaluate_unit(unit);busy=false;
}
static void hooked_update(void *self,const void *mi)
{
 main_thread=GetCurrentThreadId();
 original_update(self,mi);if(busy||!cfg)return;
 cfg->updates++;busy=true;
 if(sync_stage()&&load_rules())scan_units(0);
 busy=false;
}
static InstanceVoid original_round_begin,original_round_conditions,original_round_end;
typedef void (*FixedUpdateFn)(void *,float,const void *);
static FixedUpdateFn original_fixed_update;
typedef void (*PassiveRoundFn)(void *,int,const void *);
static PassiveRoundFn original_rodion_end,original_rodion_end_upgrade;
typedef bool (*RoundWaitFn)(void *,const void *);
static RoundWaitFn original_round_wait;
static bool prepare_event(void)
{
 if(busy||!cfg||!enabled())return false;
 main_thread=GetCurrentThreadId();busy=true;
 if(!sync_stage()||!load_rules()){busy=false;return false;}return true;
}
/* The upgraded passive clears _gainedFutureEyeOffThisTurn before calling
 * its base OnRoundEnd. Capture on entry to BOTH variants, before that clear.
 * Never infer conversion from the enemy's eyes or just possessing overheat. */
static void capture_rodion_conversion(void *self)
{
 if(!prepare_event())return;
 bool gained=false;api.read(self,f_rodion_gained,&gained);
 void *owner=read_ref(self,f_passive_owner);
 if(gained&&owner)for(unsigned i=0;i<rule_count;i++){
  if(!is_rodion_transition(i)||!identity_matches(owner,i))continue;
  round_open=true;UnitState *s=state_for(owner);if(!s)continue;
  uint32_t bit=UINT32_C(1)<<i;
  if(!(s->pending&bit))report(0,"Rodion ally conversion confirmed by passive; waiting for round end.");
  s->pending|=bit;
 }
 busy=false;
}
static void hooked_rodion_end(void *self,int timing,const void *mi)
{capture_rodion_conversion(self);original_rodion_end(self,timing,mi);}
static void hooked_rodion_end_upgrade(void *self,int timing,const void *mi)
{capture_rodion_conversion(self);original_rodion_end_upgrade(self,timing,mi);}
static void hooked_round_begin(void *self,const void *mi)
{
 if(prepare_event()){reset_tracking();round_open=true;scan_units(0);busy=false;}
 original_round_begin(self,mi);
}
static void hooked_round_conditions(void *self,const void *mi)
{
 /* Active buffs have advanced; inspect before start-event skills can consume
  * Hermes stacks. Never poll an absent-buff condition during initial setup. */
 if(prepare_event()){
  if(!round_open){reset_tracking();round_open=true;}
  if(!start_checked){start_checked=true;scan_units(1);}busy=false;
 }
 original_round_conditions(self,mi);
}
static void hooked_round_end(void *self,const void *mi)
{
 if(prepare_event()){end_waiting=round_open;scan_units(0);busy=false;}
 original_round_end(self,mi);
 if(prepare_event()){end_waiting=round_open;scan_units(0);busy=false;}
}
static bool hooked_round_wait(void *self,const void *mi)
{
 bool done=original_round_wait(self,mi);
 /* End callbacks may enqueue work that completes over later frames. Observe
  * until the game's own readiness test succeeds; never force progression. */
 if(done&&end_waiting&&prepare_event()){
  if(round_open)scan_units(2);
  reset_tracking();busy=false;
 }
 return done;
}

static void finish_round_after_advance(void)
{
 if(!end_waiting||!prepare_event())return;
 /* Current build inlines OnWaitEndRound in FixedUpdate. AFTER_ROUND_END
  * proves the real loop advanced, even when no standalone method was called. */
 if(cfg->phase>=18&&cfg->phase<=20){
  if(round_open)scan_units(2);
  reset_tracking();
 }
 busy=false;
}
static void hooked_fixed_update(void *self,float delta,const void *mi)
{
 finish_round_after_advance();
 original_fixed_update(self,delta,mi);
 finish_round_after_advance();
}

/* Unlock before exit/retry callbacks run; do not block their result music or
 * retain anything from an old Mirror Dungeon stage. */
static InstanceVoid original_end[5],original_start;
static void ending(unsigned index,void *self,const void *mi)
{suspended=true;unlock(false);reset_tracking();original_end[index](self,mi);}
#define END(N) static void end##N(void *s,const void *m){ending(N,s,m);}
END(0) END(1) END(2) END(3) END(4)
static void hooked_start(void *self,const void *mi)
{clear_stage();suspended=false;original_start(self,mi);}

static void *find_class(const char *ns,const char *name)
{
 size_t n=0;void **assemblies=api.assemblies(api.domain(),&n);if(!assemblies||n>4096)return NULL;
 for(size_t i=0;i<n;i++){void *k=api.klass(api.image(assemblies[i]),ns,name);if(k)return k;}return NULL;
}
static bool type_is(void *type,const char *expected)
{char *name=type?api.type_name(type):NULL;bool ok=name&&!strcmp(name,expected);if(name)api.free_mem(name);return ok;}
static void *field(void *klass,const char *name,const char *type)
{void *f=klass?api.field(klass,name):NULL;return f&&(!type||type_is(api.field_type(f),type))?f:NULL;}
static void *instance_field(void *klass)
{for(void *k=klass;k;k=api.parent(k)){void *f=api.field(k,"_instance");if(f)return f;}return NULL;}
static void *method(void *klass,const char *name,int argc,bool is_static,const char *result,const char **params)
{
 void *iterator=NULL,*m=NULL;
 while((m=(void *)api.methods(klass,&iterator))){
  uint32_t impl=0;
  if(strcmp(api.method_name(m),name)||api.argc(m)!=(unsigned)argc||!!(api.flags(m,&impl)&0x10)!=is_static||!type_is(api.result(m),result))continue;
  bool valid=true;for(int i=0;i<argc;i++)if(!type_is(api.param(m,i),params[i]))valid=false;
  if(valid)return m;
 }
 char msg[256];snprintf(msg,sizeof(msg),"Missing/signature mismatch: %s(%d)",name,argc);report(3,msg);return NULL;
}
static void *pointer(void *m)
{
 void *p=NULL;MEMORY_BASIC_INFORMATION info;if(m)memcpy(&p,m,sizeof(p));
 return p&&VirtualQuery(p,&info,sizeof(info))&&info.AllocationBase==game&&info.State==MEM_COMMIT&&
  !(info.Protect&(PAGE_GUARD|PAGE_NOACCESS))&&
  (info.Protect&(PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY))?p:NULL;
}
typedef struct {void *target,*detour;void **original;} Hook;
static Hook hooks[48];static unsigned hook_count;
static bool add_hook(void *klass,const char *name,int argc,bool is_static,const char *result,const char **params,void *detour,void **original,void **save)
{
 void *m=method(klass,name,argc,is_static,result,params);if(!m)return false;
 if(save)*save=m;
 void *p=pointer(m);if(!p||hook_count>=48){report(3,"Invalid hook pointer.");return false;}
 for(unsigned i=0;i<hook_count;i++)if(hooks[i].target==p){report(3,"Unexpected shared method implementation.");return false;}
 hooks[hook_count++]=(Hook){p,detour,original};return true;
}
#define I "System.Int32"
#define B "System.Boolean"
#define S "System.String"
#define F "System.Single"
#define V "System.Void"
#define EV "FMOD.Studio.EventInstance"
static bool install(void)
{
 hook_count=0;
#define CLASS(VAR,NS,NAME) void *VAR=find_class(NS,NAME);if(!VAR){report(2,"Missing class: " NS "." NAME);return false;}
 CLASS(passive,"","PassiveAbility");CLASS(rodion,"","PassiveAbility_1091602");CLASS(rodion_up,"","PassiveAbility_1091612");
 CLASS(sc,"","StageController");CLASS(unit,"","BattleUnitModel");CLASS(base,"","UnitModel");
 CLASS(ally,"","BattleUnitModel_Ally");CLASS(bom,"","BattleObjectManager");CLASS(buf,"","BuffDetail");
 CLASS(sound,"","SoundManager");CLASS(generator,"","SoundGenerator");CLASS(current,"LimbusCompany.SoundLibs","CurrentBGM");
 CLASS(battle,"","BattleSoundGenerator");CLASS(keyword,"","BUFF_UNIQUE_KEYWORD");
 CLASS(events,"UnityEngine.EventSystems","EventSystem");CLASS(utils,"Utils","UnityUtils");
 k_ally=ally;k_keywords=keyword;
#define FIELD(VAR,K,NAME,TYPE) if(!(VAR=field(K,NAME,TYPE))){report(4,"Missing field: " #K "." NAME);return false;}
 f_stage_instance=instance_field(sc);f_bom_instance=instance_field(bom);f_sound_instance=instance_field(sound);
 if(!f_stage_instance||!f_bom_instance||!f_sound_instance){report(4,"Missing singleton fields.");return false;}
 FIELD(f_passive_owner,passive,"_owner","BattleUnitModel");
 FIELD(f_rodion_gained,rodion,"_gainedFutureEyeOffThisTurn",B);
 FIELD(f_stage_model,sc,"_stageModel","StageModel");FIELD(f_phase,sc,"_phase","STAGE_PHASE");
 FIELD(f_previous_phase,sc,"_prevPhaseBeforeWaiting","STAGE_PHASE");
 FIELD(f_buff_detail,unit,"_buffDetail","BuffDetail");FIELD(f_battle_current,battle,"_currentInstance",EV);
#define METHOD(VAR,K,NAME,N,STATIC,RESULT,...) if(!(VAR=method(K,NAME,N,STATIC,RESULT,(const char *[]){__VA_ARGS__})))return false
 METHOD(m_origin_id,base,"GetOriginUnitID",0,false,I);
 METHOD(m_buff_stack,buf,"GetActivatedBuffStack",2,false,I,"BUFF_UNIQUE_KEYWORD",B);
 METHOD(m_has_buff,buf,"HasActivatedBuff",2,false,B,"BUFF_UNIQUE_KEYWORD",B);
 METHOD(m_models,bom,"GetModelList",0,false,"System.Collections.Generic.List<BattleUnitModel>");
 METHOD(m_try_create,sound,"TryCreateInstance",2,false,B,S,EV "&");
 METHOD(m_load_bank,sound,"LoadBankFromLocal",3,false,V,S,B,B);
 METHOD(m_bank_loaded,sound,"IsLoadedBank",1,false,B,S);
 METHOD(m_scene_battle,utils,"IsBattleScene",0,true,B);
 METHOD(m_stop_sound,sound,"StopBGM",1,false,V,B);
#define HOOK(K,NAME,N,STATIC,RESULT,DET,ORIG,SAVE,...) if(!add_hook(K,NAME,N,STATIC,RESULT,(const char *[]){__VA_ARGS__},DET,(void **)&ORIG,SAVE))return false
 HOOK(rodion,"OnRoundEnd",1,false,V,hooked_rodion_end,original_rodion_end,NULL,"BATTLE_EVENT_TIMING");
 HOOK(rodion_up,"OnRoundEnd",1,false,V,hooked_rodion_end_upgrade,original_rodion_end_upgrade,NULL,"BATTLE_EVENT_TIMING");
 HOOK(sc,"FixedUpdate",1,false,V,hooked_fixed_update,original_fixed_update,NULL,F);
 HOOK(sc,"StartRound",0,false,V,hooked_round_begin,original_round_begin,NULL);
 HOOK(sc,"StartRoundAfterAbnormalityChoice_Init",0,false,V,hooked_round_conditions,original_round_conditions,NULL);
 HOOK(sc,"OnEndRound",0,false,V,hooked_round_end,original_round_end,NULL);
 HOOK(sc,"OnWaitEndRound",0,false,B,hooked_round_wait,original_round_wait,NULL);
 HOOK(events,"Update",0,false,V,hooked_update,original_update,NULL);
 HOOK(buf,"RightAfterGetAnyBuff",10,false,V,hooked_buff,original_buff,NULL,"BattleUnitModel","BUFF_UNIQUE_KEYWORD",I,I,I,"ABILITY_SOURCE_TYPE","BATTLE_EVENT_TIMING","BattleUnitModel",I,I);
 HOOK(generator,"ApplyBGMData",2,true,V,hooked_apply,original_apply,NULL,"LimbusCompany.SoundLibs.IBGMData",B);
 HOOK(sound,"ChangeBGM",3,false,V,hooked_change,original_change,NULL,EV,B,"LimbusCompany.SoundLibs.IBGMData");
 HOOK(sound,"PlaySecondBGM",2,false,V,hooked_second,original_second,NULL,B,EV);
 HOOK(current,"ChangeBGM",4,false,V,hooked_current_change,original_current_change,NULL,EV,B,F,"LimbusCompany.SoundLibs.IBGMData");
 HOOK(sound,"StopBGM",1,false,V,hooked_sound_stop,original_sound_stop,NULL,B);
 HOOK(current,"StopBGM",1,false,V,hooked_current_stop,original_current_stop,NULL,B);
 HOOK(current,"Release",0,false,V,hooked_release,original_release,NULL);
 HOOK(current,"TrySetParameter",3,false,B,hooked_parameter,original_parameter,NULL,S,F,B);
 HOOK(battle,"SetChangeBGM",1,true,V,hooked_set_change,original_set_change,NULL,I);
 HOOK(battle,"ResetBGM",0,true,V,hooked_reset,original_reset,NULL);
 HOOK(battle,"StartBGM",2,true,V,hooked_battle_start,original_battle_start,&m_start_battle,I,I);
 HOOK(battle,"StopBGM",2,true,V,hooked_battle_stop,original_battle_stop,&m_stop_battle,B,B);
 HOOK(battle,"StartAddBGM",3,true,V,hooked_add,original_add,&m_start_add,S,"FMOD.Studio.STOP_MODE",B);
 HOOK(battle,"StopAddedBGM",2,true,V,hooked_add_stop,original_add_stop,&m_stop_add,"FMOD.Studio.STOP_MODE",B);
 HOOK(battle,"TryStartBattleBgmByName",1,true,B,hooked_by_name,original_by_name,NULL,S);
 HOOK(battle,"TryResumeBattleBgm",1,true,B,hooked_resume,original_resume,NULL,I);
 HOOK(battle,"TryStartInterludeBgmOverride",1,true,B,hooked_interlude,original_interlude,NULL,S);
 HOOK(battle,"TryStartInterludeBgmOverrideByEventPath",1,true,B,hooked_interlude_path,original_interlude_path,NULL,S);
 HOOK(battle,"PlayeAbChoiceBGM",1,true,V,hooked_choice,original_choice,NULL,EV);
 HOOK(sc,"EndStage",0,false,V,end0,original_end[0],NULL);
 HOOK(sc,"EndStageForcely",0,false,V,end1,original_end[1],NULL);
 HOOK(sc,"OnStageGiveUp",0,false,V,end2,original_end[2],NULL);
 HOOK(sc,"Clear",0,false,V,end3,original_end[3],NULL);
 HOOK(sc,"RetryStage",0,false,V,end4,original_end[4],NULL);
 HOOK(sc,"StartStageController",0,false,V,hooked_start,original_start,NULL);
 if(permission_tls==TLS_OUT_OF_INDEXES)permission_tls=TlsAlloc();
 if(permission_tls==TLS_OUT_OF_INDEXES){report(4,"Cannot allocate music scope.");return false;}
 HMODULE fmod=GetModuleHandleW(L"fmodstudio.dll");
 event_valid=(void *)GetProcAddress(fmod,"FMOD_Studio_EventInstance_IsValid");
 event_release=(void *)GetProcAddress(fmod,"FMOD_Studio_EventInstance_Release");
 event_playback=(void *)GetProcAddress(fmod,"FMOD_Studio_EventInstance_GetPlaybackState");
 if(!event_valid||!event_release||!event_playback){report(3,"Missing FMOD event API.");return false;}
 void *native_stop=(void *)GetProcAddress(fmod,"FMOD_Studio_EventInstance_Stop");
 void *native_parameter=(void *)GetProcAddress(fmod,"FMOD_Studio_EventInstance_SetParameterByName");
 if(!native_stop||!native_parameter){report(3,"Missing FMOD control API.");return false;}
 hooks[hook_count++]=(Hook){native_stop,hooked_native_stop,(void **)&original_native_stop};
 hooks[hook_count++]=(Hook){event_release,hooked_native_release,(void **)&original_native_release};
 hooks[hook_count++]=(Hook){native_parameter,hooked_native_parameter,(void **)&original_native_parameter};
 MH_STATUS status=MH_Initialize();
 if(status!=MH_OK&&status!=MH_ERROR_ALREADY_INITIALIZED){report(5,"MinHook initialization failed.");return false;}
 unsigned created=0;
 for(unsigned i=0;i<hook_count;i++){
  status=MH_CreateHook(hooks[i].target,hooks[i].detour,hooks[i].original);
  if(status!=MH_OK){cfg->mh_status=status;report(6,"Cannot create music hook.");goto fail;}
  created++;
 }
 for(unsigned i=0;i<hook_count;i++)MH_QueueEnableHook(hooks[i].target);
 status=MH_ApplyQueued();
 if(status!=MH_OK){cfg->mh_status=status;report(7,"Cannot enable music hooks.");goto fail;}
 cfg->hooked=1;report(0,"Ready (0.2.0 hotfix 2): Cp9 Boss 10 score initialization enabled.");return true;
fail:
 for(unsigned i=0;i<created;i++){MH_DisableHook(hooks[i].target);MH_RemoveHook(hooks[i].target);}return false;
}
static bool open_config(void)
{
 if(!map_handle){wchar_t name[96];swprintf(name,96,L"Local\\FaustLauncher_BattleBGMExtend_v2_%lu",GetCurrentProcessId());map_handle=OpenFileMappingW(FILE_MAP_ALL_ACCESS,FALSE,name);}
 if(map_handle&&!cfg)cfg=MapViewOfFile(map_handle,FILE_MAP_ALL_ACCESS,0,0,sizeof(*cfg));
 return cfg&&cfg->magic==BGM_MAGIC&&cfg->version==BGM_VERSION&&cfg->header_size==512;
}
static bool bind_api(void)
{
 FARPROC p[BGM_API_COUNT];
 for(unsigned i=0;i<BGM_API_COUNT;i++){
  p[i]=GetProcAddress(game,API_NAMES[i]);
  if(!p[i]&&cfg->aliases[i][0]&&memchr(cfg->aliases[i],0,64))p[i]=GetProcAddress(game,cfg->aliases[i]);
  if(!p[i])return false;
 }
 _Static_assert(sizeof(api)==sizeof(p),"API table");memcpy(&api,p,sizeof(api));return true;
}
static DWORD WINAPI worker(void *unused)
{
 (void)unused;
 for(;;){
  if(open_config()&&enabled()&&(game=GetModuleHandleW(L"GameAssembly.dll"))){
   if(!bind_api())report(1,"IL2CPP export mapping unavailable.");
   else {void *thread=api.attach(api.domain());bool ok=thread&&install();if(thread)api.detach(thread);if(ok)break;}
  }
  Sleep(1000);
 }
 return 0;
}
#ifndef BGM_NO_DLLMAIN
BOOL WINAPI DllMain(HINSTANCE instance,DWORD reason,LPVOID reserved)
{if(reason==DLL_PROCESS_ATTACH){DisableThreadLibraryCalls(instance);HANDLE t=CreateThread(NULL,0,worker,NULL,0,NULL);if(!t)return FALSE;CloseHandle(t);}return TRUE;}
#endif
