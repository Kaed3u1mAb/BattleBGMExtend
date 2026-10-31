#define BGM_NO_DLLMAIN
#include "../battlebgmextend.c"
#include <assert.h>

static BGM_CONFIG config;
static bool scene=true,active_buff=true,create_ok=true,bank_loaded=true;
static int phase=14,identity=11016,play_state=0,starts,stops,restores,releases,bank_loads,original_hits;
static uintptr_t current_event;
static void *model=(void *)0x1000;
static void *sc=(void *)0x2000,*sound=(void *)0x3000,*bom=(void *)0x4000;
static void *unit=(void *)0x5000,*detail=(void *)0x6000,*ally_class=(void *)0x7000;
static unsigned tests;
static void *list_obj=(void *)0x8100;
static int model_count=1,round_begin_hits,round_condition_hits,round_end_hits;
static bool end_converts,round_ready,passive_gained;
static void *passive_owner;
static int passive_calls,upgrade_calls;
static bool upgraded_reset_seen;
static int failed_start;
static int parameter_calls,parameter_fail_call;
static uintptr_t parameter_event;
static uintptr_t released_root;
static void *mock_fail_method;
static void *mock_fail_once_method;
static char last_short_name[128];
#define CHECK(X) do{assert(X);tests++;}while(0)
static bool yes=true,normal_buff=false;
static int buff_stack=9,previous_phase=14;
static void *invoke(const void *m,void *obj,void **args,void **exception)
{
 *exception=NULL;
 if(m==mock_fail_once_method){mock_fail_once_method=NULL;*exception=(void *)1;return NULL;}
 if(m==mock_fail_method){*exception=(void *)1;return NULL;}
 if(m==(void *)31)return &model_count;
 if(m==(void *)32){assert(*(int *)args[0]==0);return unit;}
 if(m==m_models)return list_obj;
 if(m==m_scene_battle)return &scene;
 if(m==m_origin_id){assert(obj==unit);return &identity;}
 if(m==m_has_buff){assert(obj==detail);return *(int *)args[0]==2116?&normal_buff:&active_buff;}
 if(m==m_buff_stack)return &buff_stack;
 if(m==m_bank_loaded)return &bank_loaded;
 if(m==m_load_bank){assert(strstr(args[0],"BGM_Default_S8_3"));bank_loads++;return NULL;}
 if(m==m_try_create){assert(!strncmp(args[0],"event:/BGM/Battle/",18));*(uintptr_t *)args[1]=create_ok?77:0;return &create_ok;}
 if(m==m_stop_battle){stops++;current_event=0;return NULL;}
 if(m==m_stop_sound)return NULL;
 if(m==m_start_add){
  starts++;snprintf(last_short_name,sizeof(last_short_name),"%s",(char *)args[0]);
  current_event=failed_start&&strcmp(args[0],"Battle_Cp10_Boss_10")?0:100+starts;
  return NULL;
 }
 if(m==m_stop_add){stops++;current_event=0;return NULL;}
 if(m==m_start_battle){restores++;current_event=44;return NULL;}
 assert(!"unexpected managed invocation");return &yes;
}
static const char *method_name(const void *m){(void)m;return "mock";}
static void *unbox(void *p){return p;}
static void read(void *obj,void *field,void *value)
{
 if(field==f_stage_model){assert(obj==sc);*(void **)value=model;}
 else if(field==f_previous_phase){assert(obj==sc);*(int *)value=previous_phase;}
 else if(field==f_phase){assert(obj==sc);*(int *)value=phase;}
 else if(field==f_buff_detail){assert(obj==unit);*(void **)value=detail;}
 else if(field==f_passive_owner)*(void **)value=passive_owner;
 else if(field==f_rodion_gained)*(bool *)value=passive_gained;
 else assert(!"unknown field");
}
static void stat(void *field,void *value)
{
 if(field==f_stage_instance)*(void **)value=sc;
 else if(field==f_bom_instance)*(void **)value=bom;
 else if(field==f_sound_instance)*(void **)value=sound;
 else if(field==f_battle_current)*(uintptr_t *)value=current_event;
 else assert(!"unknown static field");
}
static uintptr_t root(void *p,bool pin){assert(p&&pin);return UINT64_C(0x1234567800000001);}
static void unroot(uintptr_t h){released_root=h;}
static void *root_target(uintptr_t h){assert(h);return unit;}
static void *klass(void *obj){return obj==unit?ally_class:(void *)0x8888;}
static bool assignable(void *base,void *k){return base==k;}
static void *virtual_method(void *obj,const void *m){assert(obj==unit);return (void *)m;}
static void *lookup_method(void *k,const char *name,int argc)
{(void)k;(void)argc;return (void *)(uintptr_t)(!strcmp(name,"get_Count")?31:32);}
static void round_begin_mock(void *s,const void *mi){(void)s;(void)mi;round_begin_hits++;}
static void round_conditions_mock(void *s,const void *mi){(void)s;(void)mi;round_condition_hits++;buff_stack=0;}
static bool round_wait_mock(void *s,const void *mi){(void)s;(void)mi;return round_ready;}
static void round_end_mock(void *s,const void *mi)
{(void)s;(void)mi;round_end_hits++;if(end_converts){normal_buff=false;active_buff=true;}}
static void passive_end_mock(void *self,int timing,const void *mi)
{assert(self==(void *)0x9000&&timing==27&&mi==(void *)0x9001);passive_calls++;}
static void upgrade_end_mock(void *self,int timing,const void *mi)
{
 upgrade_calls++;passive_gained=false;upgraded_reset_seen=true;
 hooked_rodion_end(self,timing,mi);
}
static void *string_new(const char *s){return (void *)s;}
static int valid(uintptr_t event){return event!=0;}
static int release(uintptr_t event){assert(event==77);releases++;return 0;}
static int playback(uintptr_t event,int *state){assert(event);*state=play_state;return 0;}
static int parameter(uintptr_t event,const char *name,float value,int ignore)
{
 assert(event&&!strcmp(name,"Ryoshu")&&value==1.0f&&ignore==1);
 parameter_event=event;parameter_calls++;
 return parameter_calls==parameter_fail_call?1:0;
}
static void original_end_mock(void *s,const void *mi){(void)s;(void)mi;assert(!cfg->locked);original_hits++;}
static void original_set_mock(int i,const void *mi){(void)i;(void)mi;original_hits++;}
static volatile int forwarded_args;
__attribute__((noinline)) static void buff_target(void *self,void *u,int keyword,int stack,int turn,int round,int src,int timing,void *giver,int over_stack,int over_turn,const void *mi)
{
 assert(self==detail&&u==unit&&keyword==2115&&stack==2&&turn==3&&round==0&&src==4&&timing==5&&giver==unit&&over_stack==6&&over_turn==7&&mi==(void *)0x90);
 forwarded_args++;
}
__attribute__((noinline)) static void setter_target(int i,const void *mi)
{original_set_mock(i,mi);}
static volatile int native_stops,native_releases;
__attribute__((noinline)) static int stop_target(uintptr_t handle,int mode)
{assert(handle&&mode==1);native_stops++;return 0;}
__attribute__((noinline)) static int release_target(uintptr_t handle)
{assert(handle);native_releases++;return 0;}
static void reset(void)
{
 reset_tracking();
 memset(&config,0,sizeof(config));cfg=&config;cfg->enabled=1;cfg->heartbeat=GetTickCount();
 cfg->active_rule=-1;cfg->generation=2;
 scene=true;phase=14;previous_phase=14;identity=11016;active_buff=true;create_ok=true;bank_loaded=true;play_state=0;
 model=(void *)0x1000;stage_pointer=model;stage_handle=UINT64_C(0x1234567800000001);
 passive_gained=false;passive_owner=unit;passive_calls=upgrade_calls=0;upgraded_reset_seen=false;
 suspended=busy=false;normal_buff=false;buff_stack=9;rules_valid=true;generation=2;rule_count=3;
 owned_instance=current_event=0;starts=stops=restores=releases=bank_loads=original_hits=failed_start=0;mock_fail_method=NULL;end_converts=false;round_ready=true;round_begin_hits=round_condition_hits=round_end_hits=0;
 memset(rules,0,sizeof(rules));memset(retry_after,0,sizeof(retry_after));
 parameter_calls=parameter_fail_call=0;parameter_event=0;mock_fail_once_method=NULL;
 for(unsigned i=0;i<3;i++){
  rules[i].personality=11016;rules[i].priority=100+(int)i*100;keywords[i]=2115;
  snprintf(rules[i].id,48,"rule_%u",i);
  snprintf(rules[i].path,128,"event:/BGM/Battle/%s",i?"Other":"Battle_Cp10_Boss_10");
 }
 main_thread=GetCurrentThreadId();permit(false);
}
static void setup(void)
{
 api.invoke=invoke;api.unbox=unbox;api.method_name=method_name;api.read=read;api.static_value=stat;
 api.root=root;api.unroot=unroot;api.root_target=root_target;api.object_class=klass;api.assignable=assignable;
 api.method=lookup_method;api.virtual_method=virtual_method;api.string_new=string_new;
 f_stage_instance=(void *)1;f_bom_instance=(void *)2;f_sound_instance=(void *)3;
 f_stage_model=(void *)4;f_phase=(void *)5;f_buff_detail=(void *)6;f_battle_current=(void *)7;f_previous_phase=(void *)10;f_passive_owner=(void *)8;f_rodion_gained=(void *)9;
 m_scene_battle=(void *)11;m_origin_id=(void *)12;m_has_buff=(void *)13;m_bank_loaded=(void *)14;
 m_load_bank=(void *)15;m_try_create=(void *)16;m_stop_battle=(void *)17;m_stop_sound=(void *)18;
 m_start_add=(void *)19;m_stop_add=(void *)20;m_start_battle=(void *)21;m_buff_stack=(void *)22;
 k_ally=ally_class;event_valid=valid;event_release=release;event_playback=playback;
 original_native_parameter=parameter;
 permission_tls=TlsAlloc();assert(permission_tls!=TLS_OUT_OF_INDEXES);
 original_end[0]=original_end_mock;
 original_round_begin=round_begin_mock;original_round_conditions=round_conditions_mock;original_round_end=round_end_mock;original_round_wait=round_wait_mock;
 m_models=(void *)23;original_rodion_end=passive_end_mock;original_rodion_end_upgrade=upgrade_end_mock;
}
int main(void)
{
 setup();reset();
 CHECK(matches(unit,0));identity=11116;CHECK(!matches(unit,0));identity=11016;
 active_buff=false;CHECK(!matches(unit,0));active_buff=true;CHECK(!matches((void *)0x9999,0));
 CHECK(switch_rule(0)&&cfg->locked&&cfg->priority==100&&starts==1);
 CHECK(!strcmp(last_short_name,"Battle_Cp10_Boss_10")&&releases==1);
 CHECK(!switch_rule(0)&&starts==1);
 rules[1].priority=100;CHECK(!switch_rule(1));rules[1].priority=50;CHECK(!switch_rule(1));
 rules[1].priority=200;CHECK(switch_rule(1)&&cfg->priority==200&&starts==2);
 rules[0].priority=150;CHECK(!switch_rule(0));
 CHECK(block_game());permit(true);CHECK(!block_game());permit(false);
 CHECK(cfg->blocked==1);active_buff=false;CHECK(block_game());
 unlock(true);CHECK(!cfg->locked&&restores==1&&current_event==44);
 reset();create_ok=false;CHECK(!switch_rule(0)&&!cfg->locked&&stops==0&&cfg->error==21);
 reset();mock_fail_method=m_try_create;CHECK(!switch_rule(0)&&stops==0&&!own_call());
 reset();CHECK(switch_rule(0));failed_start=1;CHECK(!switch_rule(1)&&cfg->locked&&cfg->priority==100&&current_event&&cfg->error==21);
 reset();play_state=2;CHECK(!switch_rule(0)&&!cfg->locked&&restores==1);
 /* PLAYING alone does not make this score audible. Initialize both the
  * preflight instance and actual battle instance; fail before stopping the
  * previous music when the parameter cannot be set. */
 reset();strcpy(rules[0].path,"event:/BGM/Battle/Battle_Cp9_Boss_10");
 CHECK(switch_rule(0)&&parameter_calls==2&&parameter_event==owned_instance);
 CHECK(strstr(cfg->message,"Ryoshu=1 (Battle_1_1)")!=NULL);
 reset();strcpy(rules[0].path,"event:/BGM/Battle/Battle_Cp9_Boss_10");parameter_fail_call=1;
 CHECK(!switch_rule(0)&&!cfg->locked&&stops==0&&releases==1&&cfg->error==21);
 reset();strcpy(rules[0].path,"event:/BGM/Battle/Battle_Cp9_Boss_10");parameter_fail_call=2;
 CHECK(!switch_rule(0)&&!cfg->locked&&restores==1&&cfg->error==21);
 reset();CHECK(switch_rule(0)&&parameter_calls==0); /* Other tracks untouched. */
 strcpy(rules[1].path,"event:/BGM/Battle/Battle_Cp9_Boss_10");parameter_fail_call=1;
 CHECK(!switch_rule(1)&&cfg->locked&&cfg->priority==100&&starts==1);
 reset();strcpy(rules[0].path,"event:/BGM/Battle/Battle_Cp9_Boss_10");CHECK(switch_rule(0));
 /* Force the new score to throw once, then reinitialize the restored score. */
 mock_fail_once_method=m_start_add;
 CHECK(!switch_rule(1)&&cfg->locked&&cfg->priority==100&&parameter_calls==3);
 CHECK(parameter_event==owned_instance&&cfg->error==21);
 reset();CHECK(switch_rule(0));phase=21;CHECK(!block_game()&&!cfg->locked);
 reset();CHECK(switch_rule(0));end0(sc,NULL);CHECK(suspended&&!cfg->locked&&original_hits==1);
 reset();CHECK(switch_rule(0));cfg->heartbeat=GetTickCount()-BGM_TIMEOUT_MS-1;
 CHECK(!block_game());CHECK(!sync_stage()&&!cfg->locked&&restores==1);
 reset();CHECK(switch_rule(0));scene=false;CHECK(!block_game()&&!cfg->locked);
 reset();CHECK(switch_rule(0));model=(void *)0x1111;
 CHECK(sync_stage()&&stage_pointer==model&&released_root==UINT64_C(0x1234567800000001)&&!cfg->locked);
 reset();strcpy(rules[0].bank,"BGM_Default_S8_3");bank_loaded=false;
 CHECK(switch_rule(0)&&bank_loads==2);
 reset();rules[0].priority=INT32_MIN;CHECK(switch_rule(0)&&cfg->priority==INT32_MIN);
 rules[1].priority=INT32_MAX;CHECK(switch_rule(1)&&cfg->priority==INT32_MAX);
 /* Round-start predicates never fire from frame or buff callbacks. */
 reset();rules[0].condition=BGM_START_STACK_EQ;rules[0].threshold=9;
 CHECK(!matches(unit,0));CHECK(matches_event(unit,0,1));
 buff_stack=8;CHECK(!matches_event(unit,0,1));buff_stack=10;CHECK(!matches_event(unit,0,1));
 buff_stack=9;CHECK(!matches_event(unit,0,2));
 rules[0].condition=BGM_START_NO_BUFF;active_buff=false;
 CHECK(!matches(unit,0));CHECK(matches_event(unit,0,1));
 active_buff=true;CHECK(!matches_event(unit,0,1));
 /* Transition is per unit and per round, and only applies at end. */
 reset();rules[0].condition=BGM_END_TRANSITION;from_keywords[0]=2116;
 active_buff=true;round_open=true;
 CHECK(!matches(unit,0));CHECK(!matches_event(unit,0,2));
 reset_tracking();round_open=true;active_buff=false;normal_buff=true;
 CHECK(!matches(unit,0));CHECK(state_count==1);
 normal_buff=false;active_buff=true;
 CHECK(!matches(unit,0));CHECK(!matches_event(unit,0,1));
 CHECK(matches_event(unit,0,2));
 active_buff=false;CHECK(matches_event(unit,0,2));
 reset_tracking();round_open=true;CHECK(!matches_event(unit,0,2));
 reset_tracking();round_open=true;normal_buff=true;active_buff=false;matches(unit,0);
 identity=11116;normal_buff=false;active_buff=true;CHECK(!matches_event(unit,0,2));
 identity=11016;normal_buff=true;active_buff=false;matches(unit,0);
 model=(void *)0x1111;CHECK(sync_stage()&&state_count==0&&!round_open);
 round_open=true;normal_buff=true;active_buff=false;matches(unit,0);
 CHECK(state_count==1);clear_stage();CHECK(!state_count&&!round_open&&!stage_handle);
 /* Drive the actual lifecycle wrappers: test before consumption and no
  * duplicate start evaluation; original sequencing always runs once. */
 reset();rule_count=1;rules[0].condition=BGM_START_STACK_EQ;rules[0].threshold=9;
 hooked_round_begin(sc,NULL);CHECK(round_begin_hits==1&&round_open&&!cfg->locked);
 hooked_round_conditions(sc,NULL);CHECK(round_condition_hits==1&&buff_stack==0&&cfg->locked);
 unlock(false);hooked_round_conditions(sc,NULL);CHECK(!cfg->locked&&round_condition_hits==2);
 hooked_round_end(sc,NULL);CHECK(round_end_hits==1&&end_waiting);
 CHECK(hooked_round_wait(sc,NULL)&&!round_open&&!state_count);
 reset();rule_count=1;rules[0].condition=BGM_START_NO_BUFF;active_buff=false;
 scan_units(0);CHECK(!cfg->locked);
 hooked_round_begin(sc,NULL);CHECK(!cfg->locked);
 hooked_round_conditions(sc,NULL);CHECK(cfg->locked);
 reset();rule_count=1;rules[0].condition=BGM_END_TRANSITION;from_keywords[0]=2116;
 normal_buff=true;active_buff=false;hooked_round_begin(sc,NULL);
 CHECK(!cfg->locked&&state_count==1);end_converts=true;
 hooked_round_end(sc,NULL);CHECK(!cfg->locked&&end_waiting);
 round_ready=false;CHECK(!hooked_round_wait(sc,NULL)&&round_open&&!cfg->locked);
 round_ready=true;CHECK(hooked_round_wait(sc,NULL)&&cfg->locked&&!state_count&&!round_open&&round_end_hits==1);
 reset();rule_count=1;rules[0].condition=BGM_END_TRANSITION;from_keywords[0]=2116;
 active_buff=true;hooked_round_begin(sc,NULL);hooked_round_end(sc,NULL);hooked_round_wait(sc,NULL);
 CHECK(!cfg->locked); /* A pre-existing overheat is not a conversion. */
 reset();rule_count=1;rules[0].condition=BGM_END_TRANSITION;from_keywords[0]=2116;
 normal_buff=true;active_buff=false;hooked_round_begin(sc,NULL);
 end0(sc,NULL);CHECK(!state_count&&!round_open&&suspended);
 /* Real Rodion rule: use the game's conversion flag even if the
  * zero-stack target is absent from HasActivatedBuff / no sample was seen. */
 reset();rule_count=1;identity=10916;rules[0].personality=10916;
 rules[0].condition=BGM_END_TRANSITION;
 strcpy(rules[0].keyword,"FutureEyeOffRodion");strcpy(rules[0].from_keyword,"FutureEyeOnRodion");
 round_open=true;normal_buff=true;active_buff=false;matches(unit,0);
 normal_buff=false;active_buff=true;CHECK(!matches_event(unit,0,2)); /* State alone cannot prove conversion. */
 normal_buff=active_buff=false;passive_gained=true;
 hooked_rodion_end_upgrade((void *)0x9000,27,(void *)0x9001);
 CHECK(upgrade_calls==1&&passive_calls==1&&upgraded_reset_seen&&!passive_gained);
 CHECK(!cfg->locked&&matches_event(unit,0,2));
 hooked_round_end(sc,NULL);CHECK(end_waiting&&!cfg->locked);
 CHECK(hooked_round_wait(sc,NULL)&&cfg->locked); /* Capture survived upgraded flag clear. */
 reset_tracking();unlock(false);memset(retry_after,0,sizeof(retry_after));
 hooked_round_begin(sc,NULL);hooked_round_end(sc,NULL);hooked_round_wait(sc,NULL);
 CHECK(!cfg->locked); /* No flag carried into next round. */
 /* Lower-tier passive and late attachment: official flag is sufficient. */
 reset_tracking();passive_gained=true;round_open=false;
 hooked_rodion_end((void *)0x9000,27,(void *)0x9001);
 CHECK(round_open&&matches_event(unit,0,2));
 reset_tracking();identity=10915;
 hooked_rodion_end((void *)0x9000,27,(void *)0x9001);
 CHECK(!state_count&&!round_open); /* Wrong identity ignored. */
 identity=10916;passive_owner=(void *)0x9999;
 hooked_rodion_end((void *)0x9000,27,(void *)0x9001);
 CHECK(!state_count&&!round_open); /* Enemy/non-ally ignored. */
 passive_owner=unit;passive_gained=false;
 hooked_rodion_end((void *)0x9000,27,(void *)0x9001);
 CHECK(!state_count); /* Merely already overheated is not a conversion. */
 /* Current game's inlined end wait: do NOT call hooked_round_wait. */
 reset();rule_count=1;identity=10916;rules[0].personality=10916;
 rules[0].condition=BGM_END_TRANSITION;
 strcpy(rules[0].keyword,"FutureEyeOffRodion");strcpy(rules[0].from_keyword,"FutureEyeOnRodion");
 active_buff=normal_buff=false;round_open=true;passive_gained=true;
 phase=0;previous_phase=17; /* Actual animation wait. */
 hooked_rodion_end_upgrade((void *)0x9000,27,(void *)0x9001);
 CHECK(state_count==1&&round_open);hooked_round_end(sc,NULL);
 finish_round_after_advance();CHECK(!cfg->locked&&end_waiting);
 previous_phase=18;finish_round_after_advance();CHECK(cfg->locked&&!end_waiting&&!state_count);
 CHECK(block_game()); /* WAIT / previous AFTER_ROUND_END retains lock. */
 previous_phase=21;CHECK(!block_game()&&!cfg->locked); /* Result phase still unlocks. */
 /* Real MinHook verifies the Win64 12-argument buff detour and direct NULL
  * MethodInfo music calls; mock the game objects, not the native hook path. */
 assert(MH_Initialize()==MH_OK);
 assert(MH_CreateHook(buff_target,hooked_buff,(void **)&original_buff)==MH_OK);
 assert(MH_CreateHook(setter_target,hooked_set_change,(void **)&original_set_change)==MH_OK);
 assert(MH_CreateHook(stop_target,hooked_native_stop,(void **)&original_native_stop)==MH_OK);
 assert(MH_CreateHook(release_target,hooked_native_release,(void **)&original_native_release)==MH_OK);
 assert(MH_EnableHook(buff_target)==MH_OK);assert(MH_EnableHook(setter_target)==MH_OK);
 assert(MH_EnableHook(stop_target)==MH_OK);assert(MH_EnableHook(release_target)==MH_OK);
 reset();rule_count=1;
 BuffFn volatile entry=buff_target;
 entry(detail,unit,2115,2,3,0,4,5,unit,6,7,(void *)0x90);
 CHECK(forwarded_args==1&&cfg->locked&&cfg->changes==1);
 StaticIntVoid volatile set_entry=setter_target;set_entry(2,NULL);
 CHECK(original_hits==0&&cfg->blocked==1);
 NativeStopFn volatile native_stop_entry=stop_target;NativeReleaseFn volatile native_release_entry=release_target;
 native_stop_entry(owned_instance,1);native_release_entry(owned_instance);
 CHECK(native_stops==0&&native_releases==0);
 native_stop_entry(999,1);native_release_entry(999);CHECK(native_stops==1&&native_releases==1);
 permit(true);native_stop_entry(owned_instance,1);native_release_entry(owned_instance);permit(false);
 CHECK(native_stops==2&&native_releases==2);
 cfg->enabled=0;set_entry(2,NULL);CHECK(original_hits==1);
 assert(MH_Uninitialize()==MH_OK);TlsFree(permission_tls);
 printf("Native music policy/hook checks passed: %u\n",tests);return 0;
}
