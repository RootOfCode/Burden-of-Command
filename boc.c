/* ============================================================
   BURDEN OF COMMAND — A WWI Trench Management Tycoon
   Extended Edition

   Compile:  cc -O2 -o boc boc.c && ./boc
   Requires: POSIX terminal, UTF-8 locale, 80x24 min.
   ============================================================ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <termios.h>
#include <unistd.h>

/* ═══════════════════════════════════════════════════════════
   TERMINAL / ANSI
   ═══════════════════════════════════════════════════════════ */

#define ESC "\x1b"
static void at(int r,int c)  { printf(ESC"[%d;%dH",r,c); }
static void cls(void)        { printf(ESC"[2J"ESC"[H");   }
static void cur_off(void)    { printf(ESC"[?25l");         }
static void cur_on(void)     { printf(ESC"[?25h");         }
static void fg(int c)        { printf(ESC"[%dm",c);        }
static void attr_bold(void)  { printf(ESC"[1m");           }
static void attr_dim(void)   { printf(ESC"[2m");           }
static void attr_rst(void)   { printf(ESC"[0m");           }

enum { BLK=30,RED=31,GRN=32,YEL=33,BLU=34,MAG=35,CYN=36,WHT=37,GRY=90 };

/* Box-drawing bytes (UTF-8) */
#define BOX_TL "\xe2\x95\x94"  /* ╔ */
#define BOX_TR "\xe2\x95\x97"  /* ╗ */
#define BOX_BL "\xe2\x95\x9a"  /* ╚ */
#define BOX_BR "\xe2\x95\x9d"  /* ╝ */
#define BOX_V  "\xe2\x95\x91"  /* ║ */
#define BOX_H  "\xe2\x95\x90"  /* ═ */
#define BOX_LM "\xe2\x95\xa0"  /* ╠ */
#define BOX_RM "\xe2\x95\xa3"  /* ╣ */
#define BOX_TM "\xe2\x95\xa6"  /* ╦ */
#define BOX_BM "\xe2\x95\xa9"  /* ╩ */
#define BOX_XX "\xe2\x95\xac"  /* ╬ */
#define BOX_HT "\xe2\x94\x80"  /* ─ */
#define BOX_VT "\xe2\x94\x82"  /* │ */

/* ── Raw mode ── */
static struct termios s_orig;
static void raw_on(void){
    struct termios t; tcgetattr(STDIN_FILENO,&s_orig);
    t=s_orig; t.c_lflag &= ~(unsigned)(ECHO|ICANON);
    t.c_cc[VMIN]=1; t.c_cc[VTIME]=0;
    tcsetattr(STDIN_FILENO,TCSANOW,&t);
}
static void raw_off(void){ tcsetattr(STDIN_FILENO,TCSANOW,&s_orig); }

/* ── Keys ── */
typedef enum {
    KEY_NONE=0,KEY_UP,KEY_DOWN,KEY_LEFT,KEY_RIGHT,
    KEY_ENTER,KEY_ESC,KEY_SPACE,
    KEY_A,KEY_B,KEY_C,KEY_D,KEY_E,KEY_F,KEY_G,KEY_H,
    KEY_I,KEY_J,KEY_K,KEY_L,KEY_M,KEY_N,KEY_O,KEY_P,
    KEY_Q,KEY_R,KEY_S,KEY_T,KEY_U,KEY_V,KEY_W,KEY_X,
    KEY_Y,KEY_Z,
    KEY_0,KEY_1,KEY_2,KEY_3,KEY_4,KEY_5,KEY_6,KEY_7,KEY_8,KEY_9,
} Key;

static Key getch(void){
    unsigned char c=0;
    if(read(STDIN_FILENO,&c,1)<=0) return KEY_NONE;
    if(c==27){
        unsigned char s[2]={0,0};
        if(read(STDIN_FILENO,&s[0],1)<=0) return KEY_ESC;
        if(s[0]=='['&&read(STDIN_FILENO,&s[1],1)>0){
            switch(s[1]){case'A':return KEY_UP; case'B':return KEY_DOWN;
                         case'C':return KEY_RIGHT; case'D':return KEY_LEFT;}
        }
        return KEY_ESC;
    }
    if(c=='\r'||c=='\n') return KEY_ENTER;
    if(c==' ') return KEY_SPACE;
    if(c>='a'&&c<='z') return (Key)(KEY_A+c-'a');
    if(c>='A'&&c<='Z') return (Key)(KEY_A+c-'A');
    if(c>='0'&&c<='9') return (Key)(KEY_0+c-'0');
    return KEY_NONE;
}

/* ── Utilities ── */
static int clamp(int x,int a,int b){return x<a?a:x>b?b:x;}
static int rng_bool(float p){return (float)rand()/RAND_MAX<p;}
static int rng_range(int lo,int hi){return lo+rand()%(hi-lo+1);}

static void make_bar(char *out,int v,int mx,int w){
    int f=(int)((float)w*clamp(v,0,mx)/(mx>0?mx:1)+0.5f),e=w-f,i;
    for(i=0;i<f;i++) out[i]='#';
    for(i=0;i<e;i++) out[f+i]='.';
    out[w]='\0';
}
static void ppad(const char *s,int w){
    int n=(int)strlen(s);
    if(n>=w){fwrite(s,1,w,stdout);}
    else{fputs(s,stdout); for(int i=n;i<w;i++) putchar(' ');}
}
/* draw a full-width box horizontal rule at row r */

/* ═══════════════════════════════════════════════════════════
   DATA TABLES
   ═══════════════════════════════════════════════════════════ */

/* ── Tasks ── */
typedef enum{TASK_STANDBY=0,TASK_PATROL,TASK_RAID,TASK_REPAIR,TASK_REST,TASK_COUNT}Task;
typedef struct{
    const char *name; int color;
    int fat_delta,mor_delta,ammo_cost,tools_gain;
    const char *desc;
}TaskDef;
static const TaskDef TASK_DEFS[TASK_COUNT]={
    {"STANDBY",WHT,  -5, 0,1,0,"Hold position. Conserves fatigue."},
    {"PATROL", CYN, +10,+3,3,0,"Scout no-man's-land. Boosts morale, costs ammo."},
    {"RAID",   RED, +20,+4,6,0,"Strike enemy lines. High risk, high morale reward."},
    {"REPAIR", YEL,  +5, 0,1,2,"Fix trench works. Generates tools each turn."},
    {"REST",   GRN, -15,+5,1,0,"Stand down. Recovers fatigue and morale quickly."},
};
static const Task ORDER_OPTS[TASK_COUNT]={TASK_STANDBY,TASK_PATROL,TASK_RAID,TASK_REPAIR,TASK_REST};

/* ── Personalities ── */
typedef enum{PERS_STEADFAST=0,PERS_BRAVE,PERS_COWARDLY,PERS_DRUNKARD,PERS_COUNT}Personality;
typedef struct{const char *name; float mul; const char *effect_str;}PersDef;
static const PersDef PERS_DEFS[PERS_COUNT]={
    {"Steadfast",1.00f,"Consistent. No penalty or bonus."},
    {"Brave",    1.20f,"+20% to all fatigue/morale changes."},
    {"Cowardly", 0.70f,"-30% effectiveness; poor under fire."},
    {"Drunkard", 0.85f,"-15% effectiveness; unpredictable."},
};

/* ── Difficulty ── */
typedef enum{DIFF_EASY=0,DIFF_NORMAL,DIFF_HARD,DIFF_IRONMAN,DIFF_COUNT}Difficulty;
typedef struct{
    const char *name, *subtitle;
    int   color;
    float food_mul;      /* multiplier on food consumed */
    float event_mul;     /* multiplier on harmful event probabilities */
    float morale_mul;    /* multiplier on morale drains */
    float score_mul_x10; /* score multiplier ×10 stored as int (1.0 = 10) */
    int   save_allowed;  /* 0 = ironman, no mid-run load */
}DiffDef;
static const DiffDef DIFF_DEFS[DIFF_COUNT]={
    {"GREEN FIELDS",  "Resources plentiful. Events rare.     Recommended for newcomers.",
     GRN, 0.6f,0.6f,0.7f, 8,1},
    {"INTO THE MUD",  "Balanced. The intended experience.    Recommended difficulty.",
     YEL, 1.0f,1.0f,1.0f,10,1},
    {"NO MAN'S LAND", "Scarce supplies. Brutal events.       For veterans of the line.",
     RED, 1.4f,1.5f,1.3f,14,1},
    {"GOD HELP US",   "One life. No loading. Maximum stakes. For the truly committed.",
     MAG, 1.6f,1.8f,1.5f,20,0},
};

/* ── Weather ── */
typedef enum{WEATHER_CLEAR=0,WEATHER_RAIN,WEATHER_FOG,WEATHER_SNOW,WEATHER_STORM,WEATHER_COUNT}Weather;
typedef struct{const char *label; int color;}WeatherDef;
static const WeatherDef WEATHER_DEFS[WEATHER_COUNT]={
    {"Clear  ",CYN},{"Rainy  ",BLU},{"Foggy  ",WHT},{"Snowing",WHT},{"Storm  ",MAG},
};
typedef struct{
    int fat_per_turn;   /* extra fatigue per squad per turn */
    int food_extra;     /* extra food consumed per turn */
    int agg_drift;      /* aggression adjustment per turn */
    float raid_mul;     /* multiply enemy raid probability */
    const char *note;
}WeatherEffect;
static const WeatherEffect WEATHER_FX[WEATHER_COUNT]={
    { 0,0, 0, 1.00f,"No effect on operations."},
    { 3,1, 0, 1.00f,"Mud. +fatigue, +food. Movement impaired."},
    { 1,0, 0, 1.50f,"Low visibility. Enemy raids more likely."},
    { 4,2,+2, 0.75f,"Bitter cold. +fatigue, +food, slightly safer."},
    { 6,1,+4, 1.20f,"Storm. Heavy fatigue. Enemy emboldened."},
};
static Weather weather_next(Weather w){
    static const Weather L[10]={0,0,0,0,WEATHER_RAIN,WEATHER_RAIN,WEATHER_FOG,WEATHER_CLEAR,WEATHER_STORM,WEATHER_CLEAR};
    Weather r[10]; memcpy(r,L,sizeof(r));
    r[0]=r[1]=r[2]=r[3]=w;
    return r[rand()%10];
}

/* ── Morale levels ── */
typedef struct{int threshold;const char *label;int color;}MorLevel;
static const MorLevel MOR_LEVELS[]={
    {80,"EXCELLENT",GRN},{65,"GOOD",GRN},{45,"FAIR",YEL},{25,"POOR",RED},{0,"CRITICAL",RED},
};
#define MOR_LEVEL_COUNT 5
static const char *mor_label(int m){for(int i=0;i<MOR_LEVEL_COUNT;i++) if(m>=MOR_LEVELS[i].threshold) return MOR_LEVELS[i].label; return"CRITICAL";}
static int mor_color(int m){return m>=65?GRN:m>=45?YEL:RED;}

/* ── Raid resistance ── */
static const float RAID_RESIST[TASK_COUNT]={0.30f,0.55f,0.80f,0.40f,0.10f};

/* ── Random events ── */
typedef struct{float base_prob;float agg_divisor;}RandEventProb;
typedef enum{
    REVT_ARTILLERY=0,REVT_ENEMY_RAID,REVT_GAS,REVT_MAIL,REVT_RATS,
    REVT_INFLUENZA,REVT_SGT_BREAKDOWN,REVT_SUPPLY_CONVOY,REVT_REINFORCE,
    REVT_SNIPER,REVT_FRATERNIZE,REVT_HERO,REVT_FRIENDLY_FIRE,REVT_CACHE,
    REVT_COUNT
}RandEventId;
static const RandEventProb RAND_PROBS[REVT_COUNT]={
    {0,350.f},{0,500.f},{0.035f,0},{0.10f,0},{0.07f,0},
    {0.04f,0},{0.03f,0},{0.08f,0},{0.04f,0},
    {0,600.f},     /* SNIPER:       agg/600 */
    {0.012f,0},    /* FRATERNIZE:   1.2% (clear/fog only) */
    {0.05f,0},     /* HERO:         5% */
    {0.008f,0},    /* FRIENDLY_FIRE:0.8% */
    {0,700.f},     /* CACHE:        agg/700 (find enemy stores) */
};

/* ── Command Actions ── */
typedef enum{
    CMD_RUM=0,CMD_LETTERS,CMD_MEDICAL,CMD_REPRIMAND,
    CMD_SPEECH,CMD_RATIONS_EXTRA,CMD_CEREMONY,CMD_LEAVE,
    CMD_COUNT
}CmdActionId;
typedef struct{
    const char *name;
    int  cp_cost,food_cost,meds_cost;
    const char *desc;
    const char *effect;
}CmdActionDef;
static const CmdActionDef CMD_DEFS[CMD_COUNT]={
    {"Rum Ration",       1,5,0, "Issue the rum ration to selected squad.",
                                "+15 morale to selected squad. -5 food."},
    {"Write Letters",    1,0,0, "Help the men write letters home tonight.",
                                "+10 morale to selected squad."},
    {"Medical Triage",   1,0,5, "Emergency field dressing and rest rotation.",
                                "-25 fatigue on selected squad. -5 meds."},
    {"Inspect & Reprimand",0,0,0,"Snap inspection. Discipline tightened.",
                                "-8 fatigue but -5 morale. No CP cost."},
    {"Officer's Speech", 2,0,0, "Address the entire company personally.",
                                "+8 morale to all squads."},
    {"Emergency Rations",2,20,0,"Break into emergency food reserve.",
                                "-15 fatigue to all squads. -20 food."},
    {"Medal Ceremony",   3,0,0, "Formal commendation ceremony at H.Q.",
                                "+20 morale to selected squad. +1 medal."},
    {"Compassionate Leave",2,0,0,"Send one exhausted man to the rear.",
                                "-1 man but +15 morale. Rare mercy."},
};

/* ── Trench Upgrades ── */
typedef enum{
    UPG_DUCKBOARDS=0,UPG_SANDBAG,UPG_DUGOUT,UPG_PERISCOPE,
    UPG_LEWIS_NEST,UPG_SUMP,UPG_SIGNAL_WIRE,UPG_RUM_STORE,
    UPG_COUNT
}UpgradeId;
typedef struct{
    const char *name;
    int  tools_cost;
    const char *desc;
    const char *passive;
}UpgradeDef;
static const UpgradeDef UPG_DEFS[UPG_COUNT]={
    {"Duckboards",       8, "Raised wooden walkways over the mud.",
                            "Rain/storm fatigue penalty halved."},
    {"Sandbag Revetments",12,"Reinforced firing bay and parapets.",
                            "Artillery casualties reduced by 1."},
    {"Officers' Dugout", 15,"Reinforced shelter for command staff.",
                            "All squads +1 morale per turn passively."},
    {"Fire-Step Periscope",10,"Mirror periscope for safe observation.",
                            "PATROL grants +2 extra morale per turn."},
    {"Lewis Gun Nest",   20,"Sandbagged emplacement for the Lewis gun.",
                            "+15% raid resistance for all squads."},
    {"Trench Sump",       6,"Drainage channel under the fire-step.",
                            "Influenza and trench-foot chance halved."},
    {"Signal Wire",       8,"Buried telephone line to support trench.",
                            "+1 command point per AM turn."},
    {"Rum Store",        10,"Locked crate of medicinal rum, properly logged.",
                            "Rum Ration costs 0 CP (still costs food)."},
};

/* ── HQ Dispatches — binary orders ── */
typedef struct{
    int turn;
    const char *title;
    const char *body[6];           /* NULL-terminated order text */
    const char *comply_label;
    const char *defy_label;
    const char *comply_result;
    const char *defy_result;
    /* effects on comply */
    int cy_agg,cy_all_mor,cy_ammo,cy_food;
    int cy_force_raid;             /* 1 = assign weakest-morale squad to RAID */
    int cy_all_standby;            /* 1 = force all squads to STANDBY */
    int cy_lose_men;               /* men lost from largest squad */
    int cy_medals;
    /* effects on defy */
    int df_agg,df_all_mor;
}HqDispatch;

static const HqDispatch HQ_DISPATCHES[]={
{
    5,
    "BRIGADE ORDER — MANDATORY NIGHT RAID",
    {"Brigade HQ requires one section to advance at 0300 and destroy",
     "the enemy wire-cutting position at grid ref C-4.",
     "Capture of prisoners would be desirable but is not required.",
     "Failure to execute this order will be viewed most seriously.",
     "Acknowledge receipt by runner. Captain Thorne to confirm personally.",
     NULL},
    "COMPLY — Assign a squad to raid",
    "DEFY  — Decline the order",
    "The raid is launched. Intelligence value noted by Brigade. Agg falls.",
    "Brigade marks you as 'obstructive'. Enemy grows bolder. Morale hits.",
    -8, 0, -10, 0, 1,0,0,0,
    +14,-5
},{
    16,
    "HQ ORDER — RATION REDUCTION EFFECTIVE IMMEDIATELY",
    {"Supply lines between the railhead and the forward position",
     "are under sustained enemy interdiction fire.",
     "All forward units are to reduce their daily ration draw by",
     "one quarter, effective at 0600 tomorrow.",
     "The men are expected to bear this with good grace.",
     NULL},
    "COMPLY — Reduce rations",
    "DEFY  — Maintain full rations",
    "Rations cut. Food reserves fall sharply. Men are hungry but obedient.",
    "HQ notes your refusal. Next convoy will be delayed. Men keep their food.",
    0,-3,-5,-25, 0,0,0,0,
    +8,0
},{
    28,
    "ORDER — SECONDMENT TO 3RD PIONEER BATTALION",
    {"The Pioneer Corps requires experienced infantry for deep tunneling",
     "operations in the Messines sector. Two men from your company",
     "are to report to 3rd Pioneer Bn HQ by 0600 hours tomorrow.",
     "Selection is at the Company Commander's discretion.",
     "This is not a request. Acknowledge by 2200 tonight.",
     NULL},
    "COMPLY — Send two men",
    "DEFY  — Refuse the transfer",
    "Two men depart. Pioneers send ammunition in reciprocity. Company shaken.",
    "You protect your men. Brigade is furious. Enemy senses weakness.",
    0,-4,+8,0, 0,0,2,0,
    +12,-6
},{
    44,
    "INTELLIGENCE DISPATCH — GERMAN OFFENSIVE EXPECTED",
    {"Brigade Intelligence reports German forces massing along a",
     "3-kilometre front including your sector.",
     "All units are to assume defensive posture immediately.",
     "Ammunition is being pre-positioned at forward dumps.",
     "All sections to STANDBY. Conserve all resources. Acknowledge.",
     NULL},
    "COMPLY — Stand all squads by",
    "DEFY  — Maintain current posture",
    "All squads to STANDBY. Ammo delivered. Enemy can't find an opening.",
    "Men prefer their tasks. Enemy offensive hits a disorganised sector.",
    +5,0,+20,0, 0,1,0,0,
    +10,+5
},{
    60,
    "COMMENDATION — VICTORIA CROSS NOMINATION REQUESTED",
    {"Brigade Commander writes personally: the conduct of the men",
     "of this sector has been noted with admiration at Corps level.",
     "One man is to be forwarded for commendation.",
     "Submit name by runner at first light.",
     "This is an honour that reflects upon the whole company.",
     NULL},
    "COMPLY — Submit a nomination",
    "DEFY  — Decline the honour",
    "Ceremony held at Brigade. The men are proud beyond words.",
    "Word gets around that the Captain blocked the honour. Morale suffers.",
    0,+10,0,0, 0,0,0,2,
    0,-5
},
};
#define HQ_DISPATCH_COUNT (int)(sizeof(HQ_DISPATCHES)/sizeof(HQ_DISPATCHES[0]))

/* ── Initial squad roster ── */
typedef struct{const char *name;int men,maxm,mor,fat;const char *sgt_name;Personality sgt_pers;}SquadInitDef;
static const SquadInitDef SQUAD_INIT[]={
    {"Alpha",  7,8,68,25,"Sgt. Harris",PERS_BRAVE    },
    {"Bravo",  8,8,72,15,"Sgt. Moore", PERS_STEADFAST},
    {"Charlie",5,8,44,55,"Sgt. Lewis", PERS_DRUNKARD },
    {"Delta",  6,8,61,40,"Sgt. Bell",  PERS_COWARDLY },
};
#define SQUAD_INIT_COUNT 4

static const char *INIT_MSGS[]={
    "Welcome, Captain Thorne. God help us.",
    "ORDERS: Hold the line for 6 weeks.",
    "Supply convoy expected in ~3 turns.",
    "Intelligence: Expect shelling tonight.",
};
#define INIT_MSG_COUNT 4

/* ─── Layout constants (80×23) ─── */
#define TW 80
#define DIV 44
#define LW 42
#define RW 35

/* ═══════════════════════════════════════════════════════════
   CODEX — 14 lore entries
   ═══════════════════════════════════════════════════════════ */

typedef struct{const char *title;int title_color;const char *lines[12];}CodexEntry;
static const CodexEntry CODEX[]={
{"THE WESTERN FRONT",YEL,
 {"The Western Front stretches 700 kilometres from the North Sea to Switzerland.",
  "Since the failure of the Schlieffen Plan in 1914, both sides entrenched.",
  "Progress is measured in metres and paid for in thousands of lives.",
  "You command a company sector near Passchendaele, Flanders, Belgium.",
  "The mud here is legendary. Men have drowned in unmarked shell craters.",
  "Your orders arrive from Brigade HQ twelve miles behind the line.",
  "They have not visited the front in three months.",
  "The Somme claimed 57,000 British casualties on its first day alone.",
  "The Third Ypres offensive (Passchendaele) began July 31st, 1917.",
  "Haig believes attrition will break Germany. The men are not consulted.",
  NULL}},
{"TRENCH WARFARE",CYN,
 {"The front-line trench: a ditch six feet deep and two feet wide.",
  "Duckboards line the floor. Walls shored with corrugated iron.",
  "A fire-step lets men peer over the parapet at stand-to.",
  "Behind: support trenches, reserve trenches, communication lines.",
  "The smell: mud, cordite, decay, latrine, wet wool. Indescribable.",
  "Trench foot afflicts men standing in water for days without relief.",
  "Rats as large as cats devour rations and gnaw at the sleeping.",
  "Average life expectancy of a second lieutenant at the front: six weeks.",
  "Between attacks: fatigues, carrying parties, digging, sandbagging, waiting.",
  "A man can go mad from the waiting as easily as from the shells.",
  NULL}},
{"GAS WARFARE",GRN,
 {"April 1915: the Germans first used chlorine gas at Ypres.",
  "The yellow-green cloud drifted over Allied trenches at dusk.",
  "Men described it as lungs full of fire — drowning from inside.",
  "Phosgene followed: colourless, faintly of cut hay. Deadlier.",
  "Mustard gas, introduced July 1917, blisters skin and blinds eyes.",
  "Mustard gas has no immediate smell. Men often don't know until too late.",
  "A well-drilled unit with Small Box Respirators can survive most attacks.",
  "Without medical supplies a gas attack leaves lasting casualties.",
  "Gas masks fog in cold air and suffocate under exertion.",
  "The sound of the gas alarm — phosgene rattles — haunts veterans for life.",
  NULL}},
{"CAPTAIN ALISTAIR THORNE",YEL,
 {"Captain Alistair Thorne, age 29. 11th Battalion, East Lancashire Regiment.",
  "Born in Preston. Joined Kitchener's New Army voluntarily in August 1914.",
  "Commissioned after the Somme. Three of his original platoon survived.",
  "He does not speak of the Somme. He does not need to.",
  "He writes to his mother every Sunday. Half the letters are returned.",
  "He was mentioned in dispatches once, at Beaumont Wood.",
  "He keeps a photograph of his sister Agnes inside his breast pocket.",
  "His wristwatch stopped during a barrage in April. He has not replaced it.",
  "He still believes in what he is doing. He is no longer certain why.",
  "He is, in all practical terms, the last man standing in his draft.",
  NULL}},
{"SGT. HARRIS — BRAVE",RED,
 {"Sergeant Thomas Harris, age 34. Former coal miner from Wigan, Lancashire.",
  "Volunteered August 1914, driven by genuine and uncomplicated patriotism.",
  "Was at the Gallipoli landings. Does not discuss what he saw there.",
  "His bravery borders on recklessness — he volunteers for every night raid.",
  "The men of Alpha Section follow him without hesitation.",
  "He would die for any one of them. They all know it.",
  "A recommendation for the Military Medal is pending at Brigade.",
  "He reads no books. He plays no cards. He sharpens his bayonet each evening.",
  "His hands tremble slightly at breakfast. He pours his tea very carefully.",
  "He does not mention the trembling. Neither do the men.",
  NULL}},
{"SGT. MOORE — STEADFAST",GRN,
 {"Sergeant William Moore, age 41. Regular Army since 1897.",
  "Fought in the Second Boer War, the Northwest Frontier, France since 1914.",
  "The most experienced soldier in the company by a considerable margin.",
  "He is the calm eye at the centre of every storm.",
  "Even under the heaviest barrage he moves deliberately, checking his men.",
  "He has four daughters at home in Dorset. He writes each of them weekly.",
  "He has never been wounded. The men consider this extraordinary luck.",
  "His vice: strong tea, guarded jealously from all appropriation.",
  "He has seen enough officers come and go that he does not form attachments.",
  "He respects Captain Thorne. This is rare and means a great deal.",
  NULL}},
{"SGT. LEWIS — DRUNKARD",YEL,
 {"Sergeant Owen Lewis, age 38. Former schoolteacher from Cardiff, Wales.",
  "Joined 1915, commissioned briefly as a second lieutenant.",
  "Demoted following an incident involving a rum store and a court of inquiry.",
  "The incident does not appear in any accessible official record.",
  "He was a fine soldier and a finer man once. The rum ration found him.",
  "He hides bottles inside the sandbag walls. The men pretend not to notice.",
  "On good mornings he recites Keats and Wilfred Owen to the section.",
  "The men listen. They do not always understand. They listen anyway.",
  "He is terrified of dying and more terrified that he deserves to.",
  "He has not had a full night's sleep since the winter of 1915.",
  NULL}},
{"SGT. BELL — COWARDLY",MAG,
 {"Sergeant Arthur Bell, age 26. Bank clerk from Lambeth, London.",
  "Conscripted under the Military Service Act in January 1916.",
  "He did not want to come. He stated this clearly to his tribunal.",
  "His sergeant's stripes came after two better men died in one week.",
  "He freezes under concentrated fire. His men have begun to notice.",
  "He is not a villain. He is a man violently miscast by history.",
  "Privately he writes detailed letters cataloguing his fear.",
  "He sends them to no one. He keeps them in a tin under his bedroll.",
  "He prays every night. He is no longer certain anyone is listening.",
  "He would be good at his former job. He was very good at his former job.",
  NULL}},
{"THE ENEMY — IMPERIAL GERMAN ARMY",RED,
 {"The German Imperial Army: professional, adaptive, formidably supplied.",
  "Their Sturmtruppen (stormtrooper) tactics, developed in 1917, are new.",
  "Infiltration: small groups bypass strongpoints to strike the rear.",
  "German artillery is heavy, accurate, and seemingly inexhaustible.",
  "You rarely see them. They are voices in darkness and metal in daylight.",
  "Some prisoners are boys of sixteen. The war does not discriminate.",
  "German soldiers call no-man's-land 'Niemandsland'. The word is the same.",
  "The German soldier opposite you has the same rations, same rats,",
  "same fear. His officers told him the same lies. He holds his line.",
  "This does not make him your enemy any less. It makes it worse.",
  NULL}},
{"MEDICAL CARE IN THE FIELD",CYN,
 {"Regimental Aid Post: the first stop for casualties. One Medical Officer.",
  "Stretcher-bearers carry wounded through open ground under active fire.",
  "Casualty Clearing Stations are five miles back — hours of agony away.",
  "Shell shock (neurasthenia) is poorly understood. Most are sent back.",
  "Morphia is scarce. Many wounded receive rum, a pad, and a quiet word.",
  "Your medical supplies: dressings, anti-gas equipment, and morphia.",
  "Run out and gas attacks become catastrophic. Disease spreads unchecked.",
  "Trench foot: preventable with dry socks and regular foot inspections.",
  "Sgt. Harris insists on foot inspection. Alpha has no trench foot.",
  "It is the small disciplines that keep men alive.",
  NULL}},
{"WEAPONS & MATERIEL",YEL,
 {"Lee-Enfield Mk III: 15 rounds per minute in trained hands. Reliable.",
  "Pattern 1907 bayonet: 17 inches of steel. The last resort. Used often.",
  "Mills bomb No. 5: essential for raids and close defence.",
  "Lewis gun: light machine gun, 47-round drum. The section's backbone.",
  "Stokes mortar: short range, high arc. Extraordinarily effective in trenches.",
  "18-pounder field gun: workhorse of British artillery support.",
  "Ammo represents rounds, bombs, and mortar shells combined.",
  "Running low costs patrols their effectiveness. Men feel naked without it.",
  "Ammo for a RAID costs twice the daily patrol rate. Plan accordingly.",
  "A well-supplied squad on PATROL maintains morale and sector awareness.",
  NULL}},
{"DAILY LIFE — THE LONG WAIT",GRY,
 {"Stand-to at dawn and dusk: every man at the fire-step, rifle ready.",
  "Between attacks: fatigues, carrying parties, repairs, digging, waiting.",
  "Bully beef and hard tack biscuit. The ration arrives cold and hours late.",
  "Rum is issued at the officer's discretion. Always deeply appreciated.",
  "Entertainment: cards, dog-eared novels, crown-and-anchor, concert parties.",
  "The trench newspaper — the 'Wipers Times' — circulates when found.",
  "'Wipers' is what the Tommies call Ypres. Home is everywhere else.",
  "Mail from home is the single greatest boost to morale in the trenches.",
  "A letter from a girl, a mother, a brother: proof that the world persists.",
  "The men fight less for king and country than for the man beside them.",
  NULL}},
{"COMMAND & THE OFFICER'S BURDEN",CYN,
 {"The company commander stands between his men and a distant, abstract war.",
  "His orders come from men who have not seen the front in months.",
  "His decisions — which squad rests, who raids, who is sacrificed — are real.",
  "Command Points represent the finite reserve of personal authority.",
  "A captain can inspire, cajole, comfort, threaten — but not endlessly.",
  "Issue rum. Write letters for the illiterate. Hold a medal ceremony.",
  "Every act of personal leadership costs something from the commander.",
  "The men do not need a general. They need a captain who knows their names.",
  "Thorne knows all of them. He knows their wives' names. Their children's.",
  "He will carry that weight whether they survive or not.",
  NULL}},
{"TRENCH ENGINEERING",MAG,
 {"The British soldier spent far more time digging than fighting.",
  "Duckboards, drainage sumps, fire-steps: each built by hand and bayonet.",
  "Sandbag revetments can absorb shell fragments and reduce casualties.",
  "A well-built dugout provides shelter and raises the spirits of all who rest.",
  "The Lewis gun nest: a sandbagged emplacement for the section's weapon.",
  "Signal wire buried in the trench floor allowed communication under fire.",
  "Trench periscopes — mirrors on a stick — let men observe safely.",
  "Every tool spent on engineering is a tool not spent on fighting.",
  "The best-maintained trench is the one the men are most willing to defend.",
  "Build early. Build well. The mud will take everything else.",
  NULL}},
};
#define CODEX_COUNT (int)(sizeof(CODEX)/sizeof(CODEX[0]))

/* ═══════════════════════════════════════════════════════════
   HISTORICAL EVENTS
   ═══════════════════════════════════════════════════════════ */

typedef struct{
    int turn; const char *text;
    int agg_delta,all_mor_delta,all_fat_delta,set_weather;
}HistEvent;

static const HistEvent HIST_EVENTS[]={
    { 2,"HQ runner: 'Hold sector at all costs. No retreat. Acknowledge.'",+5, 0, 0,-1},
    { 7,"End of first week. The rain has arrived. The mud is inescapable.",+3,-3,+5,WEATHER_RAIN},
    {14,"A chaplain visits the line. He is nineteen, fresh from seminary.", 0,+4, 0,-1},
    {21,"Haig communique: 'continued pressure'. The men find this darkly amusing.",0,0,0,-1},
    {28,"A German deserter is escorted through your sector. He is sixteen.",-5,+2, 0,-1},
    {35,"Six days of rain. The mud has become the primary tactical obstacle.",0,-3,+6,WEATHER_RAIN},
    {42,"Half the campaign. A relief column passes — they are not for you.",+5,-5, 0,-1},
    {49,"Word: a major offensive has failed eight miles north.",+8,-4, 0,-1},
    {56,"Rumour: peace negotiations in Zurich. No one believes it. Everyone wants to.",0,+5,0,-1},
    {63,"The men have stopped flinching at close shell-bursts. A bad sign.",+3, 0, 0,-1},
    {70,"Final week. The men are lean, hollow-eyed, and unbreakable.", 0,+6,-5,-1},
    {77,"Relief units spotted 3 miles behind the line. Almost there.", 0,+8,-8,-1},
};
#define HIST_EVENT_COUNT (int)(sizeof(HIST_EVENTS)/sizeof(HIST_EVENTS[0]))

/* ═══════════════════════════════════════════════════════════
   GAME STRUCTURES
   ═══════════════════════════════════════════════════════════ */

#define MAX_SQUADS  8
#define MAX_MSGS    7
#define MAX_EVQ    16
#define MAX_DIARY  128
#define MSG_LEN    72

typedef struct{char name[32];Personality pers;int ok;}Sgt;

typedef struct{
    char name[16];
    int  men,maxm,mor,fat,sick;
    Task task;
    int  has_sgt;
    Sgt  sgt;
    int  lore_idx;
    /* stats */
    int  raids_repelled,men_lost,turns_alive;
}Squad;

typedef enum{EV_NONE=0,EV_SUPPLY,EV_REINFORCE}EvType;
typedef struct{
    int at; EvType type;
    int food,ammo,meds,tools; /* supply */
    int men;                   /* reinforce */
}SchedEv;

typedef struct{int turn,half_am;char text[MSG_LEN];}DiaryEntry;

typedef enum{OVER_NONE=0,OVER_WIN,OVER_LOSE,OVER_MUTINY}OverState;

typedef struct{
    /* time */
    int turn,maxt,half_am;
    /* environment */
    Weather weather; int agg;
    /* resources */
    int food,ammo,meds,tools;
    /* squads */
    Squad  squads[MAX_SQUADS]; int squad_count;
    /* messages & diary */
    char   msgs[MAX_MSGS][MSG_LEN]; int msg_count;
    DiaryEntry diary[MAX_DIARY];    int diary_count;
    /* event queue */
    SchedEv evq[MAX_EVQ]; int ev_count;
    /* historical fired bitmask */
    int hist_fired;
    /* HQ dispatches */
    int dispatch_pending;  /* -1=none, else index */
    int dispatch_done;     /* bitmask fired */
    int dispatch_comply;   /* bitmask complied */
    int convoy_delayed;    /* turns to delay next convoy (defy dispatch 2) */
    /* UI */
    int sel,orders_mode,osel;
    /* command & upgrades */
    int cmd_points;  /* 0-3 (or 4 with signal wire) */
    int upgrades;    /* bitmask UpgradeId */
    /* campaign */
    Difficulty difficulty;
    int score,medals;
    /* patrol_forced — all standby (dispatch 4 comply) */
    int forced_standby_turns;
    /* end state */
    OverState over;
}GameState;

static GameState G;

/* ═══════════════════════════════════════════════════════════
   SAVE SYSTEM — 3 slots, versioned
   ═══════════════════════════════════════════════════════════ */

#define SAVE_MAGIC   0xB0C1917
#define SAVE_VERSION 4
#define SAVE_SLOTS   3

static const char *SAVE_NAMES[SAVE_SLOTS]={"boc_s0.bin","boc_s1.bin","boc_s2.bin"};

typedef struct{
    int magic,version,used;
    int turn,week,men_alive,overall_morale;
    Difficulty diff; OverState over;
    char slot_label[64];
}SaveMeta;
typedef struct{SaveMeta meta;GameState gs;}SaveFile;

static int save_meta_read(int slot,SaveMeta *m){
    FILE *f=fopen(SAVE_NAMES[slot],"rb");
    if(!f) return 0;
    int ok=(fread(m,sizeof(*m),1,f)==1&&m->magic==SAVE_MAGIC&&m->version==SAVE_VERSION);
    fclose(f); return ok&&m->used;
}
static int save_slot(int slot){
    static const char *mns[]={"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
    static const int md[]={31,28,31,30,31,30,31,31,30,31,30,31};
    int days=(G.turn-1)/2,mo=0,d=days;
    while(mo<11&&d>=md[mo]){d-=md[mo];mo++;}
    char dstr[24]; snprintf(dstr,sizeof(dstr),"%2d %.3s 1917",d+1,mns[mo]);
    SaveFile sf;
    sf.meta.magic=SAVE_MAGIC; sf.meta.version=SAVE_VERSION; sf.meta.used=1;
    sf.meta.turn=G.turn; sf.meta.week=1+(G.turn-1)/14;
    sf.meta.men_alive=0;
    for(int i=0;i<G.squad_count;i++) sf.meta.men_alive+=G.squads[i].men;
    sf.meta.overall_morale=0;
    for(int i=0;i<G.squad_count;i++) sf.meta.overall_morale+=G.squads[i].mor;
    if(G.squad_count) sf.meta.overall_morale/=G.squad_count;
    sf.meta.diff=G.difficulty; sf.meta.over=G.over;
    snprintf(sf.meta.slot_label,sizeof(sf.meta.slot_label),
             "Wk %d  |  %s  |  %d men  |  %s",
             sf.meta.week,dstr,sf.meta.men_alive,DIFF_DEFS[G.difficulty].name);
    sf.gs=G;
    FILE *f=fopen(SAVE_NAMES[slot],"wb");
    if(!f) return 0;
    int ok=(fwrite(&sf,sizeof(sf),1,f)==1);
    fclose(f); return ok;
}
static int load_slot(int slot){
    SaveFile sf;
    FILE *f=fopen(SAVE_NAMES[slot],"rb"); if(!f) return 0;
    int ok=(fread(&sf,sizeof(sf),1,f)==1); fclose(f);
    if(!ok||sf.meta.magic!=SAVE_MAGIC||sf.meta.version!=SAVE_VERSION||!sf.meta.used) return 0;
    G=sf.gs; return 1;
}

/* ═══════════════════════════════════════════════════════════
   HELPERS / QUERIES
   ═══════════════════════════════════════════════════════════ */

static void add_msg(const char *msg){
    int cap=MAX_MSGS-1;
    memmove(&G.msgs[1],&G.msgs[0],(size_t)cap*MSG_LEN);
    strncpy(G.msgs[0],msg,MSG_LEN-1); G.msgs[0][MSG_LEN-1]='\0';
    if(G.msg_count<MAX_MSGS) G.msg_count++;
}
static void diary_add(const char *text){
    if(G.diary_count>=MAX_DIARY){
        memmove(&G.diary[0],&G.diary[1],sizeof(DiaryEntry)*(MAX_DIARY-1));
        G.diary_count=MAX_DIARY-1;
    }
    DiaryEntry *e=&G.diary[G.diary_count++];
    e->turn=G.turn; e->half_am=G.half_am;
    strncpy(e->text,text,MSG_LEN-1); e->text[MSG_LEN-1]='\0';
}
static void log_msg(const char *msg){add_msg(msg);diary_add(msg);}

static int overall_mor(void){
    if(!G.squad_count) return 0;
    int s=0; for(int i=0;i<G.squad_count;i++) s+=G.squads[i].mor;
    return s/G.squad_count;
}
static int total_men(void){
    int s=0; for(int i=0;i<G.squad_count;i++) s+=G.squads[i].men;
    return s;
}
static int curr_week(void){return 1+(G.turn-1)/14;}
static void dat_str(char *out){
    static const char *mns[]={"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
    static const int md[]={31,28,31,30,31,30,31,31,30,31,30,31};
    int days=(G.turn-1)/2,m=0,d=days;
    while(m<11&&d>=md[m]){d-=md[m];m++;}
    sprintf(out,"%2d %s 1917",d+1,mns[m]);
}
static int upg_has(UpgradeId u){return (G.upgrades>>(int)u)&1;}
static int cp_max(void){return upg_has(UPG_SIGNAL_WIRE)?4:3;}

static int calc_score(void){
    float mul=(float)DIFF_DEFS[G.difficulty].score_mul_x10/10.0f;
    int s=G.turn*5+total_men()*20+overall_mor()*3+G.medals*50;
    if(G.over==OVER_WIN) s+=500;
    if(G.over==OVER_MUTINY) s-=200;
    return (int)(s*mul)>0?(int)(s*mul):0;
}
static const char *score_grade(int sc){
    if(sc>=1200) return "S+";
    if(sc>=900)  return "S ";
    if(sc>=650)  return "A ";
    if(sc>=450)  return "B ";
    if(sc>=250)  return "C ";
    if(sc>=100)  return "D ";
    return "F ";
}

/* ═══════════════════════════════════════════════════════════
   INITIALISATION
   ═══════════════════════════════════════════════════════════ */

static void new_game(Difficulty diff){
    memset(&G,0,sizeof(G));
    G.difficulty=diff;
    G.turn=1; G.maxt=84; G.half_am=1;
    G.weather=WEATHER_CLEAR; G.agg=40;
    G.food=80; G.ammo=60; G.meds=30; G.tools=40;
    G.cmd_points=2; G.dispatch_pending=-1;

    G.squad_count=SQUAD_INIT_COUNT;
    for(int i=0;i<SQUAD_INIT_COUNT;i++){
        const SquadInitDef *d=&SQUAD_INIT[i];
        Squad *s=&G.squads[i];
        strncpy(s->name,d->name,sizeof(s->name)-1);
        s->men=d->men; s->maxm=d->maxm;
        s->mor=d->mor; s->fat=d->fat;
        s->task=TASK_STANDBY; s->has_sgt=1;
        strncpy(s->sgt.name,d->sgt_name,sizeof(s->sgt.name)-1);
        s->sgt.pers=d->sgt_pers; s->sgt.ok=1;
        s->lore_idx=4+i;
    }
    for(int i=0;i<INIT_MSG_COUNT;i++) log_msg(INIT_MSGS[i]);
    G.ev_count=1;
    G.evq[0]=(SchedEv){.at=3,.type=EV_SUPPLY,.food=25,.ammo=20,.meds=8,.tools=5};
}

/* ═══════════════════════════════════════════════════════════
   RENDER — primitives
   ═══════════════════════════════════════════════════════════ */

static void vbar(void){fg(BLU);attr_bold();fputs(BOX_V,stdout);attr_rst();}

static void hline(int r,const char *kind){
    at(r,1);fg(BLU);attr_bold();
    int sp=!strcmp(kind,"split"),jo=!strcmp(kind,"join"),cl=!strcmp(kind,"close-r");
    int hm=sp||jo||cl;
    const char *lc,*rc,*mc="";
    if     (!strcmp(kind,"top")){lc=BOX_TL;rc=BOX_TR;}
    else if(!strcmp(kind,"bot")){lc=BOX_BL;rc=BOX_BR;}
    else{lc=BOX_LM;rc=BOX_RM;}
    if(sp) mc=BOX_TM; else if(jo) mc=BOX_XX; else if(cl) mc=BOX_BM;
    fputs(lc,stdout);
    for(int c=2;c<TW;c++) fputs((hm&&c==DIV)?mc:BOX_H,stdout);
    fputs(rc,stdout);
    attr_rst();fflush(stdout);
}

static void render_resources(void){
    typedef struct{const char *lb;int *v;int mx;int bc;int mi;}ResRow;
    ResRow rr[]={{"Food:",&G.food,100,GRN,0},{"Ammo:",&G.ammo,100,YEL,1},
                 {"Meds:",&G.meds, 50,CYN,2},{"Tools:",&G.tools,50,MAG,3}};
    for(int i=0;i<4;i++){
        char b[12]; make_bar(b,*rr[i].v,rr[i].mx,10);
        at(4+i,1);vbar();at(4+i,2);
        attr_bold();fg(WHT);printf(" %-6s",rr[i].lb);attr_rst();
        fg(rr[i].bc);printf("[%s]",b);attr_rst();
        printf(" %3d/%3d",*rr[i].v,rr[i].mx);
        for(int j=24;j<LW;j++) putchar(' ');
        at(4+i,DIV);vbar();at(4+i,DIV+1);
        int mi=G.msg_count-1-rr[i].mi;
        if(mi>=0&&mi<G.msg_count&&G.msgs[mi][0]){
            fg(CYN);fputs(" \xe2\x80\xba ",stdout);attr_rst();
            fg(WHT);ppad(G.msgs[mi],RW-3);attr_rst();
        } else for(int j=0;j<RW;j++) putchar(' ');
        at(4+i,TW);vbar();
    }
    /* Row 8: morale + msg 4 */
    int m=overall_mor(); char b[12]; make_bar(b,m,100,8);
    at(8,1);vbar();at(8,2);
    attr_bold();fg(WHT);fputs(" Moral",stdout);attr_rst();
    fg(mor_color(m));printf("[%s] %3d%% %-9s",b,m,mor_label(m));attr_rst();
    for(int i=27;i<LW;i++) putchar(' ');
    at(8,DIV);vbar();at(8,DIV+1);
    int mi4=G.msg_count>4?G.msg_count-5:-1;
    if(mi4>=0&&G.msgs[mi4][0]){fg(CYN);fputs(" \xe2\x80\xba ",stdout);attr_rst();fg(WHT);ppad(G.msgs[mi4],RW-3);attr_rst();}
    else for(int i=0;i<RW;i++) putchar(' ');
    at(8,TW);vbar();fflush(stdout);
}

static void render_squads_map(void){
    char abar[12],aln0[RW+2],aln1[RW+2];
    make_bar(abar,G.agg,100,9);
    snprintf(aln0,sizeof(aln0)," Aggrn:[%s] %d%%",abar,G.agg);
    int ex=G.agg>70?clamp(G.agg/7,0,14):0;
    char exs[16]={0}; for(int i=0;i<ex;i++) exs[i]='!';
    snprintf(aln1,sizeof(aln1)," %s%s",exs,G.agg>70?" ATTACK LIKELY":"");
    /* Upgrades summary for map panel */
    char upln[RW+2]=" Upg:";
    static const char *ushort[UPG_COUNT]={"DB","SB","DG","PR","LG","SU","SW","RS"};
    for(int i=0;i<UPG_COUNT;i++){
        if(upg_has((UpgradeId)i)){strcat(upln,"[");strcat(upln,ushort[i]);strcat(upln,"]");}
    }
    const char *map[10]={
        "                                   ",
        " " BOX_HT BOX_HT BOX_HT BOX_HT BOX_HT BOX_HT BOX_HT BOX_HT BOX_HT BOX_HT
          BOX_HT BOX_HT BOX_HT BOX_HT BOX_HT BOX_HT BOX_HT BOX_HT BOX_HT BOX_HT
          BOX_HT BOX_HT BOX_HT BOX_HT BOX_HT BOX_HT BOX_HT BOX_HT BOX_HT BOX_HT
          BOX_HT BOX_HT " ",
        "  [A]  [B]  [C]  [D]   Sectors  ",
        " " BOX_HT BOX_HT BOX_HT BOX_HT BOX_HT BOX_HT BOX_HT BOX_HT BOX_HT BOX_HT
          BOX_HT BOX_HT BOX_HT BOX_HT BOX_HT BOX_HT BOX_HT BOX_HT BOX_HT BOX_HT
          BOX_HT BOX_HT BOX_HT BOX_HT BOX_HT BOX_HT BOX_HT BOX_HT BOX_HT BOX_HT
          BOX_HT BOX_HT " ",
        "    " BOX_TL BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_TR "                 ",
        "    " BOX_V "   H.Q.   " BOX_V "  ~~No Man's~~  ",
        "    " BOX_BL BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_BR "  ~~  Land   ~~  ",
        "                                   ",
        aln0, aln1,
    };
    /* replace row 7 with upgrades if any owned */
    if(G.upgrades) map[7]=upln;
    for(int r=11;r<=20;r++){
        int mi=r-11,si=(r-11)/2,ss=(r-11)%2;
        Squad *sq=si<G.squad_count?&G.squads[si]:NULL;
        at(r,1);vbar();at(r,2);
        if(!sq){
            for(int i=0;i<LW;i++) putchar(' ');
        } else if(ss==0){
            int sel=(si==G.sel);
            char mb[10]; make_bar(mb,sq->mor,100,7);
            fg(WHT);fputs(sel?" \xe2\x96\xb6 ":"   ",stdout);attr_rst();
            if(sel){attr_bold();} fg(sel?YEL:WHT);
            char nm[8]; snprintf(nm,8,"%-7s",sq->name); fputs(nm,stdout); attr_rst();
            fg(GRY);printf("%d/%d ",sq->men,sq->maxm);attr_rst();
            fg(mor_color(sq->mor));printf("[%s]",mb);attr_rst();
            putchar(' ');
            fg(TASK_DEFS[sq->task].color);attr_bold();
            char ts[8]; snprintf(ts,8,"%-7s",TASK_DEFS[sq->task].name); fputs(ts,stdout); attr_rst();
            for(int i=32;i<LW;i++) putchar(' ');
        } else {
            if(G.orders_mode&&si==G.sel){
                int pv=(G.osel+TASK_COUNT-1)%TASK_COUNT,nx=(G.osel+1)%TASK_COUNT;
                fg(YEL);attr_bold();fputs("  \xe2\x96\xba ",stdout);attr_rst();
                fg(GRY);ppad(TASK_DEFS[ORDER_OPTS[pv]].name,7);attr_rst();putchar(' ');
                fg(CYN);attr_bold();printf("[%-7s]",TASK_DEFS[ORDER_OPTS[G.osel]].name);attr_rst();putchar(' ');
                fg(GRY);ppad(TASK_DEFS[ORDER_OPTS[nx]].name,7);attr_rst();
                for(int i=30;i<LW;i++) putchar(' ');
            } else {
                char sl[LW+2];
                if(sq->has_sgt) snprintf(sl,sizeof(sl),"   %s (%s)",sq->sgt.name,PERS_DEFS[sq->sgt.pers].name);
                else snprintf(sl,sizeof(sl),"   No Sergeant");
                int fc=sq->fat<40?GRN:sq->fat<70?YEL:RED;
                attr_dim();fg(GRY);ppad(sl,33);attr_rst();
                fputs("Fat:",stdout);fg(fc);printf("%2d%%",sq->fat);attr_rst();
                for(int i=40;i<LW;i++) putchar(' ');
            }
        }
        at(r,DIV);vbar();at(r,DIV+1);
        fg(GRY);ppad(map[mi],RW);attr_rst();
        at(r,TW);vbar();
    }
    fflush(stdout);
}

static void render(void){
    cls(); char dstr[24];
    hline(1,"top");
    at(2,1);vbar();at(2,2);
    fg(YEL);attr_bold();fputs(" BURDEN OF COMMAND",stdout);attr_rst();
    fg(GRY);fputs("  " BOX_VT "  ",stdout);attr_rst();
    dat_str(dstr);fg(WHT);fputs(dstr,stdout);attr_rst();
    fg(G.half_am?CYN:MAG);printf(" %s",G.half_am?"AM":"PM");attr_rst();
    fg(GRY);fputs("  " BOX_VT "  ",stdout);attr_rst();
    fg(WEATHER_DEFS[G.weather].color);fputs(WEATHER_DEFS[G.weather].label,stdout);attr_rst();
    fg(GRY);printf("  " BOX_VT "  Wk %d/6  T %d/%d",curr_week(),G.turn,G.maxt);attr_rst();
    /* Command points */
    fg(GRY);fputs("  " BOX_VT " CP:",stdout);attr_rst();
    for(int i=0;i<cp_max();i++){
        if(i<G.cmd_points){fg(YEL);attr_bold();fputs("\xe2\x97\x8f",stdout);attr_rst();}
        else{fg(GRY);attr_dim();fputs("\xe2\x97\x8b",stdout);attr_rst();}
    }
    /* Difficulty tag */
    fg(DIFF_DEFS[G.difficulty].color);attr_dim();
    printf(" [%s]",DIFF_DEFS[G.difficulty].name);attr_rst();
    at(2,TW);vbar();fflush(stdout);

    hline(3,"split");
    render_resources();
    hline(9,"join");
    at(10,2);attr_bold();fg(WHT);ppad(" SQUADS",LW);attr_rst();
    at(10,DIV+1);attr_bold();fg(WHT);ppad(" SECTOR MAP",RW);attr_rst();
    at(10,1);vbar();at(10,DIV);vbar();at(10,TW);vbar();fflush(stdout);
    render_squads_map();
    hline(21,"close-r");
    at(22,1);vbar();at(22,2);
    fg(YEL);
    const char *ctrl=G.orders_mode
        ?" [\xe2\x86\x90\xe2\x86\x92] Cycle  [ENTER] Confirm  [ESC] Cancel orders"
        :" [SPC] End Turn  [O] Orders  [C] Command  [I] Intel  [D] Dossier  [ESC] Menu";
    ppad(ctrl,TW-2);attr_rst();
    at(22,TW);vbar();fflush(stdout);
    hline(23,"bot");
    at(24,1);fflush(stdout);
}

/* ═══════════════════════════════════════════════════════════
   SCREEN HELPERS — box drawing utilities
   ═══════════════════════════════════════════════════════════ */

/* Draw a full-width framed box from r1 to r2 cols c1 to c2 */
static void draw_box(int r1,int c1,int r2,int c2){
    at(r1,c1);fg(BLU);attr_bold();
    fputs(BOX_TL,stdout);
    for(int i=c1+1;i<c2;i++) fputs(BOX_H,stdout);
    fputs(BOX_TR,stdout);
    for(int r=r1+1;r<r2;r++){
        at(r,c1);fputs(BOX_V,stdout);
        at(r,c2);fputs(BOX_V,stdout);
    }
    at(r2,c1);fputs(BOX_BL,stdout);
    for(int i=c1+1;i<c2;i++) fputs(BOX_H,stdout);
    fputs(BOX_BR,stdout);
    attr_rst();fflush(stdout);
}
static void draw_hline_mid(int r,int c1,int c2){
    at(r,c1);fg(BLU);attr_bold();
    fputs(BOX_LM,stdout);
    for(int i=c1+1;i<c2;i++) fputs(BOX_H,stdout);
    fputs(BOX_RM,stdout);
    attr_rst();
}

/* Generic menu: returns choice index, -1 on ESC */
typedef struct{const char *text;int disabled;const char *hint;}MenuItem;
static int run_menu(const char *title,const MenuItem *items,int n,int sel0){
    int sel=sel0;
    for(;;){
        cls();
        int bw=56,br=(TW-bw)/2+1;
        draw_box(4,br,4+n*2+3,br+bw-1);
        at(4,br+2);fg(YEL);attr_bold();ppad(title,bw-4);attr_rst();
        draw_hline_mid(5,br,br+bw-1);
        for(int i=0;i<n;i++){
            int row=6+i*2;
            at(row,br+2);
            if(items[i].disabled){attr_dim();fg(GRY);printf("    %s",items[i].text);attr_rst();}
            else if(i==sel){fg(YEL);attr_bold();printf("  \xe2\x96\xb6 %s",items[i].text);attr_rst();}
            else{fg(WHT);printf("    %s",items[i].text);attr_rst();}
            if(items[i].hint&&i==sel){
                at(row+1,br+4);attr_dim();fg(GRY);fputs(items[i].hint,stdout);attr_rst();
            }
        }
        at(6+n*2,br+2);fg(GRY);
        fputs("[\xe2\x86\x91\xe2\x86\x93] Nav   [ENTER] Select   [ESC] Back",stdout);attr_rst();
        fflush(stdout);
        Key k=getch();
        if(k==KEY_ESC) return -1;
        if(k==KEY_UP)  {do{sel=(sel+n-1)%n;}while(items[sel].disabled);}
        if(k==KEY_DOWN){do{sel=(sel+1)%n;}while(items[sel].disabled);}
        if(k==KEY_ENTER&&!items[sel].disabled) return sel;
    }
}

/* ═══════════════════════════════════════════════════════════
   SCREEN: DIFFICULTY SELECTION
   ═══════════════════════════════════════════════════════════ */

static Difficulty screen_difficulty(void){
    int sel=1; /* default normal */
    for(;;){
        cls();
        draw_box(3,10,12,70);
        at(3,12);fg(YEL);attr_bold();fputs(" SELECT DIFFICULTY  \xe2\x94\x82  Choose your burden",stdout);attr_rst();
        draw_hline_mid(4,10,70);
        for(int i=0;i<DIFF_COUNT;i++){
            int row=5+i*2;
            at(row,13);
            int s=(i==sel);
            if(s){fg(DIFF_DEFS[i].color);attr_bold();printf("  \xe2\x96\xb6 %-16s",DIFF_DEFS[i].name);}
            else {fg(GRY);printf("    %-16s",DIFF_DEFS[i].name);}
            attr_rst();
            fg(s?WHT:GRY);attr_dim();fputs(DIFF_DEFS[i].subtitle,stdout);attr_rst();
        }
        at(13,13);fg(GRY);
        fputs("[\xe2\x86\x91\xe2\x86\x93] Choose   [ENTER] Confirm",stdout);attr_rst();
        fflush(stdout);
        Key k=getch();
        if(k==KEY_ESC) return DIFF_NORMAL;
        if(k==KEY_UP)   sel=(sel+DIFF_COUNT-1)%DIFF_COUNT;
        if(k==KEY_DOWN) sel=(sel+1)%DIFF_COUNT;
        if(k==KEY_ENTER) return (Difficulty)sel;
    }
}

/* ═══════════════════════════════════════════════════════════
   SCREEN: HQ DISPATCH MODAL
   ═══════════════════════════════════════════════════════════ */

/* Returns 1=comply, 0=defy */
static int screen_hq_dispatch(int idx){
    const HqDispatch *d=&HQ_DISPATCHES[idx];
    int sel=0;
    for(;;){
        cls();
        draw_box(2,4,22,77);
        at(2,6);fg(RED);attr_bold();printf(" \xe2\x9a\xa0  %s  \xe2\x9a\xa0 ",d->title);attr_rst();
        draw_hline_mid(3,4,77);
        int r=5;
        for(int i=0;d->body[i]&&r<=13;i++,r++){
            at(r,7);fg(WHT);fputs(d->body[i],stdout);attr_rst();
        }
        at(14,4);fg(BLU);attr_bold();fputs(BOX_LM,stdout);
        for(int i=5;i<77;i++) fputs(BOX_H,stdout);
        fputs(BOX_RM,stdout);attr_rst();
        /* Two options */
        at(15,7);
        if(sel==0){fg(GRN);attr_bold();printf("  \xe2\x96\xb6 %s",d->comply_label);}
        else{fg(GRY);printf("    %s",d->comply_label);}
        attr_rst();
        at(16,9);attr_dim();fg(GRY);fputs(d->comply_result,stdout);attr_rst();
        at(17,7);
        if(sel==1){fg(RED);attr_bold();printf("  \xe2\x96\xb6 %s",d->defy_label);}
        else{fg(GRY);printf("    %s",d->defy_label);}
        attr_rst();
        at(18,9);attr_dim();fg(GRY);fputs(d->defy_result,stdout);attr_rst();
        draw_hline_mid(20,4,77);
        at(21,7);fg(YEL);fputs("[\xe2\x86\x91\xe2\x86\x93] Choose   [ENTER] Execute order",stdout);attr_rst();
        fflush(stdout);
        Key k=getch();
        if(k==KEY_UP||k==KEY_DOWN) sel=1-sel;
        if(k==KEY_ENTER) return 1-sel; /* 0=defy, 1=comply */
    }
}

/* ═══════════════════════════════════════════════════════════
   SCREEN: COMMAND ACTIONS
   ═══════════════════════════════════════════════════════════ */

static void screen_command(void){
    int sel=0;
    for(;;){
        cls();
        draw_box(2,5,22,75);
        at(2,7);fg(YEL);attr_bold();
        printf(" COMMAND ACTIONS  \xe2\x94\x82  Captain Thorne  \xe2\x94\x82  CP: %d/%d",G.cmd_points,cp_max());
        attr_rst();
        draw_hline_mid(3,5,75);

        for(int i=0;i<CMD_COUNT;i++){
            const CmdActionDef *cd=&CMD_DEFS[i];
            int row=4+i*2;
            /* determine if affordable */
            int can=(G.cmd_points>=cd->cp_cost&&G.food>=cd->food_cost&&G.meds>=cd->meds_cost);
            /* rum store: rum ration costs 0 CP */
            if(i==CMD_RUM&&upg_has(UPG_RUM_STORE)) can=(G.food>=cd->food_cost);
            int s=(i==sel);
            at(row,8);
            if(!can){attr_dim();fg(GRY);}
            else if(s){fg(YEL);attr_bold();}
            else fg(WHT);
            printf("%s %-22s",s?"\xe2\x96\xb6 ":"  ",cd->name);attr_rst();
            /* cost chips */
            if(cd->cp_cost>0&&!(i==CMD_RUM&&upg_has(UPG_RUM_STORE))){
                fg(YEL);printf("[%dCP]",cd->cp_cost);attr_rst();}
            if(cd->food_cost>0){fg(GRN);printf("[-%dF]",cd->food_cost);attr_rst();}
            if(cd->meds_cost>0){fg(CYN);printf("[-%dM]",cd->meds_cost);attr_rst();}
            if(s){at(row+1,10);attr_dim();fg(GRY);fputs(cd->effect,stdout);attr_rst();}
        }

        draw_hline_mid(20,5,75);
        at(21,8);fg(YEL);
        printf("[\xe2\x86\x91\xe2\x86\x93] Select  [ENTER] Execute on %s Squad  [ESC] Back",
               G.sel<G.squad_count?G.squads[G.sel].name:"???");
        attr_rst();fflush(stdout);

        Key k=getch();
        if(k==KEY_ESC||k==KEY_Q) break;
        if(k==KEY_UP)   sel=(sel+CMD_COUNT-1)%CMD_COUNT;
        if(k==KEY_DOWN) sel=(sel+1)%CMD_COUNT;
        if(k==KEY_ENTER){
            const CmdActionDef *cd=&CMD_DEFS[sel];
            int rum_free=(sel==CMD_RUM&&upg_has(UPG_RUM_STORE));
            int can=(G.cmd_points>=(rum_free?0:cd->cp_cost)&&G.food>=cd->food_cost&&G.meds>=cd->meds_cost);
            if(!can) continue;
            /* consume resources */
            if(!rum_free) G.cmd_points-=cd->cp_cost;
            G.food-=cd->food_cost; G.meds-=cd->meds_cost;
            Squad *sq=G.sel<G.squad_count?&G.squads[G.sel]:NULL;
            char msg[MSG_LEN];
            switch(sel){
            case CMD_RUM:
                if(sq){sq->mor=clamp(sq->mor+15,0,100);
                snprintf(msg,MSG_LEN,"Rum ration issued to %s Sq. Morale up.",sq->name);log_msg(msg);}
                break;
            case CMD_LETTERS:
                if(sq){sq->mor=clamp(sq->mor+10,0,100);
                snprintf(msg,MSG_LEN,"Letters written. %s Sq morale improves.",sq->name);log_msg(msg);}
                break;
            case CMD_MEDICAL:
                if(sq){sq->fat=clamp(sq->fat-25,0,100);
                snprintf(msg,MSG_LEN,"Medical triage: %s Sq fatigue reduced.",sq->name);log_msg(msg);}
                break;
            case CMD_REPRIMAND:
                if(sq){sq->fat=clamp(sq->fat-8,0,100);sq->mor=clamp(sq->mor-5,0,100);
                snprintf(msg,MSG_LEN,"Snap inspection: %s Sq disciplined.",sq->name);log_msg(msg);}
                break;
            case CMD_SPEECH:
                for(int i=0;i<G.squad_count;i++) G.squads[i].mor=clamp(G.squads[i].mor+8,0,100);
                log_msg("Capt. Thorne addresses the company. Morale rises across the line.");
                break;
            case CMD_RATIONS_EXTRA:
                for(int i=0;i<G.squad_count;i++) G.squads[i].fat=clamp(G.squads[i].fat-15,0,100);
                log_msg("Emergency rations issued. All squads recover fatigue.");
                break;
            case CMD_CEREMONY:
                if(sq){sq->mor=clamp(sq->mor+20,0,100);G.medals++;
                snprintf(msg,MSG_LEN,"Medal ceremony held for %s Sq. Commendation awarded.",sq->name);log_msg(msg);}
                break;
            case CMD_LEAVE:
                if(sq&&sq->men>1){sq->men--;sq->mor=clamp(sq->mor+15,0,100);
                snprintf(msg,MSG_LEN,"Compassionate leave: %s Sq loses 1 man, gains heart.",sq->name);log_msg(msg);}
                break;
            }
            /* refresh after action */
            fflush(stdout);
        }
    }
}

/* ═══════════════════════════════════════════════════════════
   SCREEN: TRENCH UPGRADES
   ═══════════════════════════════════════════════════════════ */

static void screen_upgrades(void){
    int sel=0;
    for(;;){
        cls();
        draw_box(2,4,22,77);
        at(2,6);fg(MAG);attr_bold();
        printf(" TRENCH ENGINEERING  \xe2\x94\x82  Tools available: %d",G.tools);attr_rst();
        draw_hline_mid(3,4,77);

        for(int i=0;i<UPG_COUNT;i++){
            const UpgradeDef *ud=&UPG_DEFS[i];
            int owned=upg_has((UpgradeId)i);
            int can=(!owned&&G.tools>=ud->tools_cost);
            int row=4+i*2;
            at(row,7);
            if(owned){fg(GRN);attr_bold();printf("  [BUILT] %-20s",ud->name);attr_rst();}
            else if(i==sel&&can){fg(YEL);attr_bold();printf("\xe2\x96\xb6 [%2d\xe2\x8c\x96] %-20s",ud->tools_cost,ud->name);attr_rst();}
            else if(i==sel){fg(RED);printf("\xe2\x96\xb6 [%2d\xe2\x8c\x96] %-20s",ud->tools_cost,ud->name);attr_rst();}
            else{fg(GRY);attr_dim();printf("  [%2d\xe2\x8c\x96] %-20s",ud->tools_cost,ud->name);attr_rst();}
            if(i==sel){
                fg(WHT);attr_dim();fputs(ud->passive,stdout);attr_rst();
                at(row+1,29);attr_dim();fg(GRY);fputs(ud->desc,stdout);attr_rst();
            } else if(!owned){
                fg(GRY);attr_dim();fputs(ud->passive,stdout);attr_rst();
            }
        }

        draw_hline_mid(20,4,77);
        at(21,7);fg(YEL);
        fputs("[\xe2\x86\x91\xe2\x86\x93] Select  [ENTER] Build  [ESC] Back",stdout);attr_rst();
        fflush(stdout);

        Key k=getch();
        if(k==KEY_ESC||k==KEY_Q) break;
        if(k==KEY_UP)   sel=(sel+UPG_COUNT-1)%UPG_COUNT;
        if(k==KEY_DOWN) sel=(sel+1)%UPG_COUNT;
        if(k==KEY_ENTER){
            UpgradeId uid=(UpgradeId)sel;
            if(!upg_has(uid)&&G.tools>=UPG_DEFS[uid].tools_cost){
                G.tools-=UPG_DEFS[uid].tools_cost;
                G.upgrades|=(1<<(int)uid);
                char msg[MSG_LEN];
                snprintf(msg,MSG_LEN,"Built: %s. %s",UPG_DEFS[uid].name,UPG_DEFS[uid].passive);
                log_msg(msg);
            }
        }
    }
}

/* ═══════════════════════════════════════════════════════════
   SCREEN: INTEL
   ═══════════════════════════════════════════════════════════ */

static void screen_intel(void){
    cls();
    draw_box(1,2,24,79);
    at(1,4);fg(CYN);attr_bold();
    char dstr[24]; dat_str(dstr);
    printf(" INTELLIGENCE REPORT  \xe2\x94\x82  %s  \xe2\x94\x82  Turn %d",dstr,G.turn);attr_rst();
    /* vertical divider at col 40 */
    for(int r=2;r<=23;r++){at(r,40);fg(BLU);attr_bold();fputs(BOX_V,stdout);attr_rst();}
    /* ── Left panel ── */
    at(2,3);fg(BLU);attr_bold();
    fputs(BOX_LM,stdout);for(int i=3;i<39;i++)fputs(BOX_H,stdout);fputs(BOX_XX,stdout);attr_rst();
    at(3,3);fg(WHT);attr_bold();fputs(" EVENT QUEUE",stdout);attr_rst();
    at(4,3);fg(GRY);for(int i=0;i<37;i++) putchar('-');attr_rst();
    int lr=5;
    /* supply/reinforce events */
    int shown=0;
    for(int i=0;i<G.ev_count&&lr<=10;i++){
        SchedEv *e=&G.evq[i];
        int eta=e->at-G.turn;
        at(lr++,4);
        if(e->type==EV_SUPPLY){
            fg(GRN);printf(" Supply convoy :  T%-3d  (in %d turn%s)",e->at,eta,eta==1?"":"s");attr_rst();
        } else if(e->type==EV_REINFORCE){
            fg(CYN);printf(" Reinforcements:  T%-3d  (in %d turn%s)",e->at,eta,eta==1?"":"s");attr_rst();
        }
        shown++;
    }
    if(!shown){at(lr++,4);fg(GRY);fputs(" No events queued.",stdout);attr_rst();}
    /* Upcoming HQ dispatches */
    at(12,3);fg(YEL);attr_bold();fputs(" UPCOMING HQ ORDERS",stdout);attr_rst();
    at(13,3);fg(GRY);for(int i=0;i<37;i++) putchar('-');attr_rst();
    int dr=14;
    for(int i=0;i<HQ_DISPATCH_COUNT&&dr<=17;i++){
        if(G.dispatch_done&(1<<i)) continue;
        int eta=HQ_DISPATCHES[i].turn-G.turn;
        if(eta<0) continue;
        at(dr++,4);fg(YEL);
        printf(" T%-3d (in %d): %.30s",HQ_DISPATCHES[i].turn,eta,HQ_DISPATCHES[i].title);
        attr_rst();
    }
    /* Upcoming historic events */
    at(18,3);fg(MAG);attr_bold();fputs(" FIELD INTELLIGENCE",stdout);attr_rst();
    at(19,3);fg(GRY);for(int i=0;i<37;i++) putchar('-');attr_rst();
    int hr=20;
    for(int i=0;i<HIST_EVENT_COUNT&&hr<=22;i++){
        if(G.hist_fired&(1<<i)) continue;
        int eta=HIST_EVENTS[i].turn-G.turn;
        if(eta<0||eta>10) continue;
        at(hr++,4);fg(GRY);attr_dim();
        printf(" T%-3d: %.34s",HIST_EVENTS[i].turn,HIST_EVENTS[i].text);
        attr_rst();
    }

    /* ── Right panel ── */
    at(2,41);fg(BLU);attr_bold();
    fputs(BOX_XX,stdout);for(int i=42;i<79;i++)fputs(BOX_H,stdout);fputs(BOX_RM,stdout);attr_rst();
    at(3,42);fg(WHT);attr_bold();fputs(" ENEMY INTELLIGENCE",stdout);attr_rst();
    at(4,42);fg(GRY);for(int i=0;i<37;i++) putchar('-');attr_rst();
    char abar[12]; make_bar(abar,G.agg,100,14);
    at(5,42);fg(WHT);printf(" Aggression: ");fg(G.agg>70?RED:G.agg>40?YEL:GRN);
    printf("%3d%% [%s]",G.agg,abar);attr_rst();
    at(6,42);fg(GRY);
    printf(" Trend: ");
    fg(G.agg>60?RED:GRN);
    fputs(G.agg>70?"ATTACK PROBABLE":G.agg>45?"ELEVATED":"QUIET",stdout);attr_rst();
    at(7,42);fg(WHT);printf(" Weather: ");fg(WEATHER_DEFS[G.weather].color);
    fputs(WEATHER_DEFS[G.weather].label,stdout);attr_rst();
    at(8,42);fg(GRY);attr_dim();fputs(" ",stdout);fputs(WEATHER_FX[G.weather].note,stdout);attr_rst();

    at(10,42);fg(WHT);attr_bold();fputs(" RESOURCE FORECAST (next turn)",stdout);attr_rst();
    at(11,42);fg(GRY);for(int i=0;i<37;i++) putchar('-');attr_rst();
    /* compute projected deltas */
    float fm=(float)DIFF_DEFS[G.difficulty].food_mul;
    int men=total_men();
    int fc=(int)(clamp(men/6,1,99)*fm)+WEATHER_FX[G.weather].food_extra;
    int ac=0; for(int i=0;i<G.squad_count;i++) ac+=TASK_DEFS[G.squads[i].task].ammo_cost;
    int tg=0; for(int i=0;i<G.squad_count;i++) if(G.squads[i].task==TASK_REPAIR) tg+=2;
    if(upg_has(UPG_DUGOUT)) tg=0; /* dugout doesn't affect tools forecast */
    at(12,42);fg(WHT);printf(" Food  : %3d ",G.food);
    fg(G.food-fc<15?RED:GRN);printf("--> ~%3d  (-%d)",clamp(G.food-fc,0,100),fc);attr_rst();
    at(13,42);fg(WHT);printf(" Ammo  : %3d ",G.ammo);
    fg(G.ammo-ac<10?RED:YEL);printf("--> ~%3d  (-%d)",clamp(G.ammo-ac,0,100),ac);attr_rst();
    at(14,42);fg(WHT);printf(" Meds  : %3d ",G.meds);
    fg(GRY);printf("--> ~%3d  (  0)",G.meds);attr_rst();
    at(15,42);fg(WHT);printf(" Tools : %3d ",G.tools);
    fg(tg>0?GRN:GRY);printf("--> ~%3d  (+%d)",clamp(G.tools+tg,0,50),tg);attr_rst();

    at(17,42);fg(WHT);attr_bold();fputs(" TRENCH STATUS",stdout);attr_rst();
    at(18,42);fg(GRY);for(int i=0;i<37;i++) putchar('-');attr_rst();
    int ur=19;
    for(int i=0;i<UPG_COUNT&&ur<=22;i++){
        at(ur++,42);
        if(upg_has((UpgradeId)i)){fg(GRN);printf(" \xe2\x9c\x93 %-22s",UPG_DEFS[i].name);}
        else{fg(GRY);attr_dim();printf("   %-22s",UPG_DEFS[i].name);}
        attr_rst();
    }

    at(24,4);fg(YEL);fputs("[ESC] Back to command",stdout);attr_rst();
    fflush(stdout);
    getch();
}

/* ═══════════════════════════════════════════════════════════
   SCREEN: SQUAD DOSSIER
   ═══════════════════════════════════════════════════════════ */

static void screen_dossier(int idx){
    if(idx<0||idx>=G.squad_count) return;
    Squad *sq=&G.squads[idx];
    for(;;){
        cls();
        draw_box(1,2,23,79);
        at(1,4);fg(YEL);attr_bold();
        printf(" DOSSIER: %s Section  \xe2\x94\x82  %s  \xe2\x94\x82  Sgt: %s",
               sq->name,PERS_DEFS[sq->sgt.pers].name,sq->has_sgt?sq->sgt.name:"None");
        attr_rst();
        /* vertical divider */
        for(int r=2;r<=22;r++){at(r,40);fg(BLU);attr_bold();fputs(BOX_V,stdout);attr_rst();}
        at(2,3);fg(BLU);attr_bold();fputs(BOX_LM,stdout);
        for(int i=3;i<39;i++){fputs(BOX_H,stdout);} fputs(BOX_XX,stdout);
        for(int i=41;i<79;i++){fputs(BOX_H,stdout);} fputs(BOX_RM,stdout); attr_rst();

        /* Left: squad stats */
        char lb[12];
        at(3,3);fg(WHT);attr_bold();fputs(" SQUAD STATUS",stdout);attr_rst();
        at(4,3);fg(GRY);for(int i=0;i<37;i++) putchar('-');attr_rst();
        make_bar(lb,sq->men,sq->maxm,12);
        at(5,3);fg(WHT);printf(" Strength : %d/%d [%s]",sq->men,sq->maxm,lb);attr_rst();
        make_bar(lb,sq->mor,100,12);
        at(6,3);fg(WHT);printf(" Morale   : %3d%% [%s] ",sq->mor,lb);
        fg(mor_color(sq->mor));fputs(mor_label(sq->mor),stdout);attr_rst();
        make_bar(lb,sq->fat,100,12);
        int fc=sq->fat<40?GRN:sq->fat<70?YEL:RED;
        at(7,3);fg(WHT);printf(" Fatigue  : %3d%% [%s]",sq->fat,lb);attr_rst();
        at(8,3);fg(WHT);printf(" Task     : ");
        fg(TASK_DEFS[sq->task].color);attr_bold();fputs(TASK_DEFS[sq->task].name,stdout);attr_rst();
        attr_dim();fg(GRY);printf("  %s",TASK_DEFS[sq->task].desc);attr_rst();
        at(9,3);fg(GRY);for(int i=0;i<37;i++) putchar('-');attr_rst();
        at(10,3);fg(WHT);attr_bold();fputs(" COMBAT RECORD",stdout);attr_rst();
        at(11,3);fg(GRY);printf(" Raids repelled : ");fg(GRN);printf("%d",sq->raids_repelled);attr_rst();
        at(12,3);fg(GRY);printf(" Men lost       : ");fg(RED);printf("%d",sq->men_lost);attr_rst();
        at(13,3);fg(GRY);printf(" Turns in line  : ");fg(CYN);printf("%d",sq->turns_alive);attr_rst();
        at(14,3);fg(GRY);for(int i=0;i<37;i++) putchar('-');attr_rst();
        at(15,3);fg(WHT);attr_bold();fputs(" COMMENDATIONS",stdout);attr_rst();
        at(16,3);
        if(sq->raids_repelled>=4){fg(YEL);attr_bold();fputs(" Military Medal (Acts of Gallantry)",stdout);}
        else if(sq->raids_repelled>=2){fg(WHT);fputs(" Mentioned in Dispatches",stdout);}
        else{attr_dim();fg(GRY);fputs(" None awarded yet.",stdout);}
        attr_rst();
        at(17,3);fg(GRY);for(int i=0;i<37;i++) putchar('-');attr_rst();
        at(18,3);fg(WHT);printf(" Sgt. status  : ");
        if(!sq->has_sgt){fg(RED);fputs("No sergeant.",stdout);}
        else if(!sq->sgt.ok){fg(RED);fputs("Shell shock. Unfit for duty.",stdout);}
        else{fg(GRN);printf("%s  (%s)",sq->sgt.name,PERS_DEFS[sq->sgt.pers].name);}
        attr_rst();
        at(19,3);fg(GRY);printf(" Pers. effect : ");
        fg(WHT);attr_dim();fputs(PERS_DEFS[sq->has_sgt?sq->sgt.pers:PERS_STEADFAST].effect_str,stdout);attr_rst();
        (void)fc;

        /* Right: lore */
        at(3,41);fg(WHT);attr_bold();fputs(" PERSONNEL FILE",stdout);attr_rst();
        at(4,41);fg(GRY);for(int i=0;i<38;i++) putchar('-');attr_rst();
        if(sq->lore_idx>=0&&sq->lore_idx<CODEX_COUNT){
            const CodexEntry *ce=&CODEX[sq->lore_idx];
            at(5,41);fg(ce->title_color);attr_bold();fputs(ce->title,stdout);attr_rst();
            at(6,41);fg(GRY);for(int i=0;i<38;i++) putchar('-');attr_rst();
            int rr=7;
            for(int i=0;ce->lines[i]&&rr<=21;i++,rr++){
                at(rr,41);fg(WHT);attr_dim();
                int len=(int)strlen(ce->lines[i]);
                if(len>37){char tmp[38];strncpy(tmp,ce->lines[i],37);tmp[37]='\0';fputs(tmp,stdout);}
                else fputs(ce->lines[i],stdout);
                attr_rst();
            }
        }

        at(22,3);fg(BLU);attr_bold();fputs(BOX_LM,stdout);
        for(int i=3;i<79;i++) fputs(BOX_H,stdout);
        fputs(BOX_RM,stdout);attr_rst();
        at(22,5);fg(YEL);
        printf(" [1-4] Switch   [\xe2\x86\x90\xe2\x86\x92] Prev/Next   [ESC] Back");attr_rst();
        fflush(stdout);

        Key k=getch();
        if(k==KEY_ESC||k==KEY_Q) break;
        if(k==KEY_LEFT&&idx>0) idx--;
        else if(k==KEY_RIGHT&&idx<G.squad_count-1) idx++;
        else if(k==KEY_1&&G.squad_count>=1) idx=0;
        else if(k==KEY_2&&G.squad_count>=2) idx=1;
        else if(k==KEY_3&&G.squad_count>=3) idx=2;
        else if(k==KEY_4&&G.squad_count>=4) idx=3;
        sq=&G.squads[idx];
    }
}

/* ═══════════════════════════════════════════════════════════
   SCREEN: CODEX
   ═══════════════════════════════════════════════════════════ */

static void screen_codex(int start){
    int idx=clamp(start,0,CODEX_COUNT-1);
    for(;;){
        const CodexEntry *ce=&CODEX[idx];
        cls(); draw_box(1,2,23,79);
        at(1,4);fg(YEL);attr_bold();
        printf(" CODEX  \xe2\x94\x82  %d/%d  \xe2\x94\x82  %-38s",idx+1,CODEX_COUNT,ce->title);attr_rst();
        draw_hline_mid(2,2,79);
        at(4,5);fg(ce->title_color);attr_bold();fputs(ce->title,stdout);attr_rst();
        at(5,5);fg(GRY);for(int i=0;i<72;i++) putchar('-');attr_rst();
        int r=7;
        for(int i=0;ce->lines[i]&&r<=20;i++,r++){at(r,5);fg(WHT);fputs(ce->lines[i],stdout);attr_rst();}
        draw_hline_mid(21,2,79);
        at(22,5);fg(YEL);fputs("[\xe2\x86\x90] Prev  [\xe2\x86\x92] Next  [1-9] Jump  [ESC] Back",stdout);attr_rst();
        fflush(stdout);
        Key k=getch();
        if(k==KEY_ESC||k==KEY_Q) break;
        if(k==KEY_LEFT||k==KEY_UP)    idx=(idx+CODEX_COUNT-1)%CODEX_COUNT;
        if(k==KEY_RIGHT||k==KEY_DOWN) idx=(idx+1)%CODEX_COUNT;
        if(k>=KEY_1&&k<=KEY_9){int ji=k-KEY_1; if(ji<CODEX_COUNT) idx=ji;}
    }
}

/* ═══════════════════════════════════════════════════════════
   SCREEN: FIELD DIARY
   ═══════════════════════════════════════════════════════════ */

static void screen_diary(void){
    int scroll=0;
    static const char *mns[]={"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
    static const int md[]={31,28,31,30,31,30,31,31,30,31,30,31};
    for(;;){
        cls(); draw_box(1,2,23,79);
        at(1,4);fg(YEL);attr_bold();
        printf(" FIELD DIARY  \xe2\x94\x82  11th East Lancashire Regt.  \xe2\x94\x82  %d entries",G.diary_count);
        attr_rst(); draw_hline_mid(2,2,79);
        int vis=16,total=G.diary_count;
        for(int i=0;i<vis;i++){
            int ei=total-1-(scroll+i);
            at(3+i,3);
            if(ei>=0&&ei<total){
                DiaryEntry *e=&G.diary[ei];
                int days=(e->turn-1)/2,mo=0,d=days;
                while(mo<11&&d>=md[mo]){d-=md[mo];mo++;}
                fg(GRY);printf(" T%-3d %s %2d %-3s  ",e->turn,e->half_am?"AM":"PM",d+1,mns[mo]);attr_rst();
                fg(WHT);ppad(e->text,TW-22);attr_rst();
            }
            at(3+i,2);fg(BLU);attr_bold();fputs(BOX_V,stdout);attr_rst();
            at(3+i,79);fg(BLU);attr_bold();fputs(BOX_V,stdout);attr_rst();
        }
        draw_hline_mid(20,2,79);
        at(21,4);fg(GRY);printf(" Entries %d-%d of %d ",total-scroll,total-(scroll+vis-1)>0?total-(scroll+vis-1):1,total);attr_rst();
        at(22,4);fg(YEL);fputs("[\xe2\x86\x91\xe2\x86\x93] Scroll   [ESC] Back",stdout);attr_rst();
        fflush(stdout);
        Key k=getch();
        if(k==KEY_ESC||k==KEY_Q) break;
        if(k==KEY_DOWN&&scroll+vis<total) scroll++;
        if(k==KEY_UP&&scroll>0) scroll--;
    }
}

/* ═══════════════════════════════════════════════════════════
   SCREEN: SAVE / LOAD SLOT PICKER
   ═══════════════════════════════════════════════════════════ */

static int screen_save_load(int is_save){
    SaveMeta meta[SAVE_SLOTS]; int valid[SAVE_SLOTS];
    for(int i=0;i<SAVE_SLOTS;i++) valid[i]=save_meta_read(i,&meta[i]);
    char texts[SAVE_SLOTS][72];
    MenuItem items[SAVE_SLOTS];
    for(int i=0;i<SAVE_SLOTS;i++){
        if(valid[i]) snprintf(texts[i],sizeof(texts[i]),"Slot %d  |  %s",i+1,meta[i].slot_label);
        else         snprintf(texts[i],sizeof(texts[i]),"Slot %d  |  (empty)",i+1);
        items[i].text=texts[i];
        items[i].disabled=(!is_save&&!valid[i]);
        items[i].hint=NULL;
    }
    const char *title=is_save?"SAVE GAME — Choose a slot":"LOAD GAME — Choose a slot";
    return run_menu(title,items,SAVE_SLOTS,0);
}

/* ═══════════════════════════════════════════════════════════
   SCREEN: CREDITS
   ═══════════════════════════════════════════════════════════ */

static void screen_credits(void){
    cls(); draw_box(2,8,22,73);
    typedef struct{int r,c,col,bold;const char *t;}SL;
    static const SL L[]={
        {3,22,YEL,1,"B U R D E N   O F   C O M M A N D"},
        {4,24,GRY,0,"A WWI Trench Management Tycoon — v3"},
        {6,11,CYN,1,"HISTORICAL CONTEXT"},
        {7,11,WHT,0,"Set during the Third Battle of Ypres (Passchendaele), 1917."},
        {8,11,WHT,0,"The 11th East Lancashire Regiment is fictionalised but based"},
        {9,11,WHT,0,"on the 'Pals Battalions' raised by Lord Derby in 1914-15."},
        {11,11,CYN,1,"THE NUMBERS"},
        {12,11,WHT,0,"The First World War killed approximately 17 million people."},
        {13,11,WHT,0,"A further 20 million were wounded. The war reshaped the world."},
        {14,11,WHT,0,"Every statistic was a person. Every person had a name."},
        {16,11,CYN,1,"ON REMEMBRANCE"},
        {17,11,GRY,0,"This game is offered as a small act of memory."},
        {18,11,GRY,0,"The men who served in the trenches of the Western Front"},
        {19,11,GRY,0,"were ordinary people placed in extraordinary circumstances."},
        {20,11,GRY,0,"They endured. Many did not return. None should be forgotten."},
        {22,28,GRN,1,"Press any key to return."},
    };
    for(int i=0;i<(int)(sizeof(L)/sizeof(L[0]));i++){
        at(L[i].r,L[i].c); if(L[i].bold){attr_bold();} fg(L[i].col);
        fputs(L[i].t,stdout); attr_rst();
    }
    fflush(stdout); getch();
}

/* ═══════════════════════════════════════════════════════════
   SCREEN: END
   ═══════════════════════════════════════════════════════════ */

static void screen_end(void){
    cls();
    int men=total_men(),maxm=0;
    for(int i=0;i<G.squad_count;i++) maxm+=G.squads[i].maxm;
    int pct=maxm?(int)(100.f*men/maxm):0;
    int score=calc_score();
    const char *grade=score_grade(score);

    typedef struct{OverState s;int col;const char *banner;const char *line1;}EndDef;
    static const EndDef ENDS[]={
        {OVER_WIN,   GRN,"      ARMISTICE!  YOUR UNIT HAS BEEN RELIEVED.      ",
                        "Captain Thorne — you endured the unendurable."},
        {OVER_LOSE,  RED,"          YOUR COMPANY HAS BEEN ANNIHILATED.         ",
                        "The mud of Flanders claimed them all."},
        {OVER_MUTINY,MAG,"              THE MEN HAVE MUTINIED.                  ",
                        "Despair consumed what artillery could not."},
    };
    int ei=G.over==OVER_WIN?0:G.over==OVER_LOSE?1:2;
    const EndDef *ed=&ENDS[ei];

    at(3,8);fg(ed->col);attr_bold();
    fputs(BOX_TL,stdout);for(int i=0;i<62;i++)fputs(BOX_H,stdout);fputs(BOX_TR,stdout);
    at(4,8);printf(BOX_V" %s "BOX_V,ed->banner);
    at(5,8);fputs(BOX_BL,stdout);for(int i=0;i<62;i++)fputs(BOX_H,stdout);fputs(BOX_BR,stdout);
    attr_rst();
    at(7,10);fg(ed->col);fputs(ed->line1,stdout);attr_rst();
    at(8,10);fg(WHT);
    if(G.over==OVER_WIN) printf("%d/%d men survived (%d%%). The Western Front will not forget them.",men,maxm,pct);
    else if(G.over==OVER_LOSE) printf("Fell on Week %d, Turn %d of %d.",curr_week(),G.turn,G.maxt);
    else printf("%d/%d men turned against their officers.",men,maxm);
    attr_rst();

    /* Grade box */
    at(10,10);fg(CYN);attr_bold();printf("FINAL SCORE: %d",score);attr_rst();
    at(10,30);fg(YEL);attr_bold();printf("GRADE: %s",grade);attr_rst();
    at(10,42);fg(DIFF_DEFS[G.difficulty].color);
    printf("[%s]",DIFF_DEFS[G.difficulty].name);attr_rst();

    at(12,10);fg(WHT);attr_bold();fputs("CAMPAIGN REPORT",stdout);attr_rst();
    at(13,10);fg(GRY);for(int i=0;i<58;i++) putchar('-');attr_rst();
    at(14,10);fg(WHT);printf(" Turns survived  : %d / %d",G.turn,G.maxt);attr_rst();
    at(15,10);fg(WHT);printf(" Men remaining   : %d / %d  (%d%%)",men,maxm,pct);attr_rst();
    at(16,10);fg(WHT);printf(" Overall morale  : %d%%  %-9s",overall_mor(),mor_label(overall_mor()));attr_rst();
    at(17,10);fg(WHT);printf(" Medals earned   : %d",G.medals);attr_rst();
    at(18,10);fg(WHT);printf(" Upgrades built  : %d / %d",__builtin_popcount(G.upgrades),UPG_COUNT);attr_rst();

    at(20,10);fg(CYN);attr_bold();fputs("SQUAD SUMMARY",stdout);attr_rst();
    for(int i=0;i<G.squad_count&&i<4;i++){
        Squad *s=&G.squads[i];
        at(21+i,10);fg(WHT);
        printf(" %-8s  %d/%d men  Mor:%3d%%  Raids repelled:%2d  Lost:%2d",
               s->name,s->men,s->maxm,s->mor,s->raids_repelled,s->men_lost);
        attr_rst();
    }
    at(23,10);fg(GRY);fputs("Press any key...",stdout);attr_rst();
    fflush(stdout); getch();
}

/* ═══════════════════════════════════════════════════════════
   MENUS — main and pause
   ═══════════════════════════════════════════════════════════ */

static int screen_main_menu(void){
    int any_save=0;
    for(int i=0;i<SAVE_SLOTS;i++){SaveMeta m;if(save_meta_read(i,&m)){any_save=1;break;}}
    static int sel=0;
    for(;;){
        cls();
        at(2,18);fg(YEL);attr_bold();
        fputs(BOX_TL,stdout);for(int i=0;i<44;i++)fputs(BOX_H,stdout);fputs(BOX_TR,stdout);
        at(3,18);printf(BOX_V"  B U R D E N   O F   C O M M A N D      "BOX_V);
        at(4,18);printf(BOX_V"     A  W W I  T r e n c h  T y c o o n  "BOX_V);
        at(5,18);fputs(BOX_BL,stdout);for(int i=0;i<44;i++)fputs(BOX_H,stdout);fputs(BOX_BR,stdout);attr_rst();
        at(6,22);fg(GRY);fputs("11th East Lancashire Regt.  \xe2\x94\x82  Passchendaele  \xe2\x94\x82  1917",stdout);attr_rst();
        MenuItem items[]={
            {"New Campaign",     0,NULL},
            {"Load Game",        !any_save,NULL},
            {"Codex & Lore",     0,NULL},
            {"Credits",          0,NULL},
            {"Quit",             0,NULL},
        };
        int n=(int)(sizeof(items)/sizeof(items[0]));
        int bw=40,br=(TW-bw)/2+1;
        draw_box(8,br,8+n*2+2,br+bw-1);
        for(int i=0;i<n;i++){
            int row=9+i*2;
            at(row,br+2);
            if(items[i].disabled){attr_dim();fg(GRY);printf("    %s",items[i].text);attr_rst();}
            else if(i==sel){fg(YEL);attr_bold();printf("  \xe2\x96\xb6 %s",items[i].text);attr_rst();}
            else{fg(WHT);printf("    %s",items[i].text);attr_rst();}
            int rr=row+1;
            at(rr,br+2);for(int j=0;j<bw-4;j++) putchar(' ');
        }
        at(9+n*2,br+4);fg(GRY);fputs("[\xe2\x86\x91\xe2\x86\x93] Nav   [ENTER] Select",stdout);attr_rst();
        fflush(stdout);
        Key k=getch();
        if(k==KEY_UP)   {do{sel=(sel+n-1)%n;}while(items[sel].disabled);}
        if(k==KEY_DOWN) {do{sel=(sel+1)%n;}while(items[sel].disabled);}
        if(k==KEY_Q||k==KEY_ESC) return 4;
        if(k==KEY_ENTER&&!items[sel].disabled) return sel;
    }
}

/* Returns: 0=resume,1=save,2=load,3=codex,4=diary,5=dossier,6=upgrades,7=quit */
static int screen_pause_menu(void){
    int ironman=(G.difficulty==DIFF_IRONMAN);
    MenuItem items[]={
        {"Resume",            0,NULL},
        {"Save Game",         0,NULL},
        {"Load Game",         ironman,"Ironman: no loading."},
        {"Codex & Lore",      0,NULL},
        {"Field Diary",       0,NULL},
        {"Squad Dossier",     0,NULL},
        {"Trench Upgrades",   0,NULL},
        {"Quit to Main Menu", 0,NULL},
    };
    int r=run_menu("PAUSED  \xe2\x94\x82  11th East Lancashire Regt.",items,8,0);
    return r<0?0:r;
}

/* ═══════════════════════════════════════════════════════════
   GAME LOGIC
   ═══════════════════════════════════════════════════════════ */

static Squad *rand_squad(void){return &G.squads[rand()%G.squad_count];}
static Squad *weakest_sq(void){
    int wi=0;
    for(int i=1;i<G.squad_count;i++) if(G.squads[i].mor<G.squads[wi].mor) wi=i;
    return &G.squads[wi];
}
static Squad *largest_sq(void){
    int li=0;
    for(int i=1;i<G.squad_count;i++) if(G.squads[i].men>G.squads[li].men) li=i;
    return &G.squads[li];
}

static void process_evq(void){
    char msg[MSG_LEN]; int keep=0;
    for(int i=0;i<G.ev_count;i++){
        SchedEv *e=&G.evq[i];
        /* handle convoy delay from dispatch 2 defy */
        if(G.convoy_delayed>0&&e->type==EV_SUPPLY){e->at++;G.convoy_delayed--;continue;}
        if(e->at!=G.turn){G.evq[keep++]=*e;continue;}
        switch(e->type){
        case EV_SUPPLY:
            G.food =clamp(G.food +e->food, 0,100);
            G.ammo =clamp(G.ammo +e->ammo, 0,100);
            G.meds =clamp(G.meds +e->meds, 0, 50);
            G.tools=clamp(G.tools+e->tools,0, 50);
            snprintf(msg,MSG_LEN,"Supply arrived! +%df +%da +%dm +%dt",e->food,e->ammo,e->meds,e->tools);
            log_msg(msg); break;
        case EV_REINFORCE:
            if(G.squad_count>0){
                Squad *sq=&G.squads[0];
                int added=clamp(e->men,0,sq->maxm-sq->men); sq->men+=added;
                snprintf(msg,MSG_LEN,"%d reinforcements join %s Section!",added,sq->name);
                log_msg(msg);
            }
            break;
        default: break;
        }
    }
    G.ev_count=keep;
}

static void apply_dispatch(int idx,int comply){
    char msg[MSG_LEN];
    const HqDispatch *d=&HQ_DISPATCHES[idx];
    G.dispatch_done|=(1<<idx);
    if(comply){
        G.dispatch_comply|=(1<<idx);
        G.agg=clamp(G.agg+d->cy_agg,5,95);
        for(int i=0;i<G.squad_count;i++) G.squads[i].mor=clamp(G.squads[i].mor+d->cy_all_mor,0,100);
        G.ammo=clamp(G.ammo+d->cy_ammo,0,100);
        G.food=clamp(G.food+d->cy_food,0,100);
        if(d->cy_force_raid){Squad *sq=weakest_sq();sq->task=TASK_RAID;}
        if(d->cy_all_standby){for(int i=0;i<G.squad_count;i++) G.squads[i].task=TASK_STANDBY;G.forced_standby_turns=1;}
        if(d->cy_lose_men){Squad *sq=largest_sq();sq->men=clamp(sq->men-d->cy_lose_men,1,sq->maxm);}
        G.medals+=d->cy_medals;
        snprintf(msg,MSG_LEN,"COMPLY: %s",d->comply_result);
    } else {
        G.agg=clamp(G.agg+d->df_agg,5,95);
        for(int i=0;i<G.squad_count;i++) G.squads[i].mor=clamp(G.squads[i].mor+d->df_all_mor,0,100);
        /* Dispatch 2 defy: delay next convoy */
        if(idx==1) G.convoy_delayed=3;
        snprintf(msg,MSG_LEN,"DEFY: %s",d->defy_result);
    }
    log_msg(msg);
}

static void process_historical(void){
    for(int i=0;i<HIST_EVENT_COUNT;i++){
        if(G.hist_fired&(1<<i)) continue;
        if(G.turn<HIST_EVENTS[i].turn) continue;
        G.hist_fired|=(1<<i);
        const HistEvent *he=&HIST_EVENTS[i];
        log_msg(he->text);
        G.agg=clamp(G.agg+he->agg_delta,5,95);
        for(int j=0;j<G.squad_count;j++){
            G.squads[j].mor=clamp(G.squads[j].mor+he->all_mor_delta,0,100);
            G.squads[j].fat=clamp(G.squads[j].fat+he->all_fat_delta,0,100);
        }
        if(he->set_weather>=0) G.weather=(Weather)he->set_weather;
    }
    /* Check dispatches */
    for(int i=0;i<HQ_DISPATCH_COUNT;i++){
        if(G.dispatch_done&(1<<i)) continue;
        if(G.turn>=HQ_DISPATCHES[i].turn&&G.dispatch_pending<0) G.dispatch_pending=i;
    }
}

static void apply_weather_effects(void){
    const WeatherEffect *fx=&WEATHER_FX[G.weather];
    float dm=(float)DIFF_DEFS[G.difficulty].event_mul;
    int fat_add=(int)(fx->fat_per_turn*dm+0.5f);
    /* Duckboards halves rain/storm fatigue */
    if(upg_has(UPG_DUCKBOARDS)&&(G.weather==WEATHER_RAIN||G.weather==WEATHER_STORM))
        fat_add/=2;
    for(int i=0;i<G.squad_count;i++)
        G.squads[i].fat=clamp(G.squads[i].fat+fat_add,0,100);
    G.agg=clamp(G.agg+fx->agg_drift,5,95);
}

static void consume(void){
    float fm=(float)DIFF_DEFS[G.difficulty].food_mul;
    float rum_save=upg_has(UPG_RUM_STORE)?0.9f:1.0f;
    int men=total_men();
    int fc=(int)(clamp(men/6,1,99)*fm*rum_save+0.5f)+WEATHER_FX[G.weather].food_extra;
    int ac=0; for(int i=0;i<G.squad_count;i++) ac+=TASK_DEFS[G.squads[i].task].ammo_cost;
    G.food=clamp(G.food-fc,0,100);
    G.ammo=clamp(G.ammo-ac,0,100);
    if(G.food<15){
        float mm=(float)DIFF_DEFS[G.difficulty].morale_mul;
        for(int i=0;i<G.squad_count;i++) G.squads[i].mor=clamp(G.squads[i].mor-(int)(5*mm),0,100);
        add_msg("CRITICAL: Food exhausted — morale falling!");
    }
    if(G.ammo<10) add_msg("WARNING: Ammunition nearly depleted!");
}

static void update_squads(void){
    float mm=(float)DIFF_DEFS[G.difficulty].morale_mul;
    for(int i=0;i<G.squad_count;i++){
        Squad *sq=&G.squads[i]; sq->turns_alive++;
        float pm=sq->has_sgt?PERS_DEFS[sq->sgt.pers].mul:1.0f;
        const TaskDef *td=&TASK_DEFS[sq->task];
        /* fatigue */
        sq->fat=clamp(sq->fat+(int)(td->fat_delta*pm),0,100);
        /* morale */
        int mor_d=(int)(td->mor_delta*pm);
        /* periscope bonus on patrol */
        if(sq->task==TASK_PATROL&&upg_has(UPG_PERISCOPE)) mor_d+=2;
        /* dugout passive */
        if(upg_has(UPG_DUGOUT)) mor_d+=1;
        sq->mor=clamp(sq->mor+mor_d,0,100);
        /* low ammo patrol penalty */
        if(sq->task==TASK_PATROL&&G.ammo<20)
            sq->mor=clamp(sq->mor-(int)(4*mm),0,100);
        /* tools gain on repair */
        if(sq->task==TASK_REPAIR) G.tools=clamp(G.tools+td->tools_gain,0,50);
        /* high-fatigue morale drain */
        if(sq->fat>80) sq->mor=clamp(sq->mor-(int)(4*mm),0,100);
        /* forced standby clears next turn */
        if(G.forced_standby_turns>0) sq->task=TASK_STANDBY;
        /* desertion */
        if(sq->mor<10&&rng_bool(0.18f)&&sq->men>1){
            sq->men--; sq->men_lost++;
            char msg[MSG_LEN];
            snprintf(msg,MSG_LEN,"%s Sq: desertion. Mor %d.",sq->name,sq->mor);
            log_msg(msg);
        }
    }
    if(G.forced_standby_turns>0) G.forced_standby_turns--;
}

static int has_supply_queued(void){for(int i=0;i<G.ev_count;i++) if(G.evq[i].type==EV_SUPPLY) return 1;return 0;}
static void push_ev(SchedEv ev){if(G.ev_count<MAX_EVQ) G.evq[G.ev_count++]=ev;}

static void random_events(void){
    char msg[MSG_LEN];
    float em=(float)DIFF_DEFS[G.difficulty].event_mul;
    float mm=(float)DIFF_DEFS[G.difficulty].morale_mul;
    float probs[REVT_COUNT];
    for(int i=0;i<REVT_COUNT;i++){
        const RandEventProb *p=&RAND_PROBS[i];
        float base=p->agg_divisor>0?G.agg/p->agg_divisor:p->base_prob;
        /* apply weather multiplier to raid-related events */
        float wm=(i==REVT_ENEMY_RAID)?WEATHER_FX[G.weather].raid_mul:1.0f;
        probs[i]=base*em*wm;
    }

    /* ARTILLERY */
    if(rng_bool(probs[REVT_ARTILLERY])){
        Squad *sq=rand_squad(); int cas=rng_range(1,2);
        if(upg_has(UPG_SANDBAG)) cas=clamp(cas-1,0,cas);
        if(sq->men>cas){
            sq->men-=cas; sq->men_lost+=cas;
            sq->mor=clamp(sq->mor-(int)(cas*8*mm),0,100);
            snprintf(msg,MSG_LEN,"ARTILLERY! %s Sq: %d casualt%s.",sq->name,cas,cas==1?"y":"ies");
            log_msg(msg);
        }
    }
    /* ENEMY RAID */
    if(rng_bool(probs[REVT_ENEMY_RAID])){
        Squad *sq=rand_squad();
        float res=RAID_RESIST[sq->task];
        if(upg_has(UPG_LEWIS_NEST)) res+=0.15f;
        if(rng_bool(res)){
            sq->mor=clamp(sq->mor+6,0,100);
            sq->raids_repelled++;
            if(sq->raids_repelled==2) G.medals++;
            if(sq->raids_repelled==4) G.medals++;
            snprintf(msg,MSG_LEN,"%s Sq repelled enemy raid! Morale up.",sq->name);
        } else {
            if(sq->men>1){sq->men--;sq->men_lost++;}
            sq->mor=clamp(sq->mor-(int)(12*mm),0,100);
            snprintf(msg,MSG_LEN,"%s Sq: enemy raid broke through! 1 KIA.",sq->name);
        }
        log_msg(msg);
    }
    /* GAS */
    if(rng_bool(probs[REVT_GAS])){
        Squad *sq=rand_squad();
        if(G.meds>=5){G.meds-=5;snprintf(msg,MSG_LEN,"GAS ATTACK on %s Sector. Meds used (-5).",sq->name);}
        else{int cas=rng_range(1,2);sq->men=clamp(sq->men-cas,0,sq->maxm);sq->men_lost+=cas;
             snprintf(msg,MSG_LEN,"GAS ATTACK — no meds! %d man/men lost.",cas);}
        log_msg(msg);
    }
    /* MAIL */
    if(rng_bool(probs[REVT_MAIL])){
        Squad *sq=rand_squad();
        sq->mor=clamp(sq->mor+rng_range(4,10),0,100);
        snprintf(msg,MSG_LEN,"Mail from home cheers %s Squad.",sq->name); log_msg(msg);
    }
    /* RATS */
    if(rng_bool(probs[REVT_RATS])){
        int lost=rng_range(2,9); G.food=clamp(G.food-lost,0,100);
        snprintf(msg,MSG_LEN,"Rats in the stores! %d rations lost.",lost); log_msg(msg);
    }
    /* INFLUENZA */
    if(rng_bool(probs[REVT_INFLUENZA]*(upg_has(UPG_SUMP)?0.5f:1.0f))){
        Squad *sq=rand_squad();
        if(sq->men>1){sq->men--;sq->men_lost++;}
        snprintf(msg,MSG_LEN,"Influenza: %s Sq loses 1 man.",sq->name); log_msg(msg);
    }
    /* SGT BREAKDOWN */
    if(rng_bool(probs[REVT_SGT_BREAKDOWN])){
        Squad *sq=rand_squad();
        if(sq->has_sgt&&sq->sgt.ok){
            sq->sgt.ok=0; sq->mor=clamp(sq->mor-(int)(8*mm),0,100);
            snprintf(msg,MSG_LEN,"%s has broken down. %s Sq morale falls.",sq->sgt.name,sq->name);
            log_msg(msg);
        }
    }
    /* SUPPLY CONVOY */
    if(rng_bool(probs[REVT_SUPPLY_CONVOY])&&!has_supply_queued()){
        int eta=G.turn+rng_range(3,7);
        SchedEv ev={.at=eta,.type=EV_SUPPLY,.food=rng_range(10,29),.ammo=rng_range(10,24),
                    .meds=rng_range(3,10),.tools=rng_range(3,8)};
        push_ev(ev);
        snprintf(msg,MSG_LEN,"HQ: Supply convoy en route. ETA %d turns.",eta-G.turn);
        log_msg(msg);
    }
    /* REINFORCE */
    if(rng_bool(probs[REVT_REINFORCE])){
        int eta=G.turn+rng_range(5,12);
        SchedEv ev={.at=eta,.type=EV_REINFORCE,.men=rng_range(1,3)};
        push_ev(ev);
        snprintf(msg,MSG_LEN,"HQ: Reinforcements en route. ETA %d turns.",eta-G.turn);
        log_msg(msg);
    }
    /* SNIPER */
    if(rng_bool(probs[REVT_SNIPER])){
        Squad *sq=rand_squad();
        if(sq->men>1&&(sq->task==TASK_STANDBY||sq->task==TASK_PATROL)){
            sq->men--; sq->men_lost++;
            sq->mor=clamp(sq->mor-(int)(10*mm),0,100);
            snprintf(msg,MSG_LEN,"Sniper! %s Sq: 1 man killed. Take cover.",sq->name);
            log_msg(msg);
        }
    }
    /* FRATERNIZATION */
    if(rng_bool(probs[REVT_FRATERNIZE])&&(G.weather==WEATHER_CLEAR||G.weather==WEATHER_FOG)){
        for(int i=0;i<G.squad_count;i++) G.squads[i].mor=clamp(G.squads[i].mor+6,0,100);
        G.agg=clamp(G.agg-8,5,95);
        log_msg("Brief fraternization across no-man's-land. Both sides stand down. Agg falls.");
    }
    /* HERO MOMENT */
    if(rng_bool(probs[REVT_HERO])){
        Squad *sq=rand_squad();
        sq->mor=clamp(sq->mor+10,0,100);
        sq->raids_repelled++;
        snprintf(msg,MSG_LEN,"Heroism: a man in %s Sq distinguishes himself. Morale surges.",sq->name);
        log_msg(msg);
    }
    /* FRIENDLY FIRE */
    if(rng_bool(probs[REVT_FRIENDLY_FIRE])){
        Squad *sq=rand_squad();
        if(sq->men>1){sq->men--;sq->men_lost++;}
        sq->mor=clamp(sq->mor-(int)(15*mm),0,100);
        snprintf(msg,MSG_LEN,"Friendly fire incident! %s Sq: 1 man lost. Morale badly shaken.",sq->name);
        log_msg(msg);
    }
    /* FOUND CACHE */
    if(rng_bool(probs[REVT_CACHE])){
        int found=rng_range(5,15); G.ammo=clamp(G.ammo+found,0,100);
        snprintf(msg,MSG_LEN,"Patrol finds abandoned enemy cache! +%d ammo.",found);
        log_msg(msg);
    }

    G.weather=weather_next(G.weather);
    G.agg=clamp(G.agg+(rand()%13)-6,5,95);
}

static void check_over(void){
    if(G.turn>=G.maxt)       {G.over=OVER_WIN;  return;}
    if(total_men()==0)        {G.over=OVER_LOSE; return;}
    for(int i=0;i<G.squad_count;i++) if(G.squads[i].mor>=5) return;
    G.over=OVER_MUTINY;
}

static void end_turn(void){
    /* Regenerate command points */
    if(G.half_am){
        int bonus=upg_has(UPG_SIGNAL_WIRE)?1:0;
        G.cmd_points=clamp(G.cmd_points+1+bonus,0,cp_max());
    }
    process_evq();
    process_historical();  /* may set dispatch_pending */
    apply_weather_effects();
    consume();
    update_squads();
    random_events();
    G.turn++;
    G.half_am=!G.half_am;
    G.score=calc_score();
    check_over();
}

/* ═══════════════════════════════════════════════════════════
   INPUT HANDLER — returns 0=continue,1=open pause,2=quit
   ═══════════════════════════════════════════════════════════ */

static int handle(Key key){
    int ns=G.squad_count,no=TASK_COUNT;
    if(G.orders_mode){
        switch(key){
        case KEY_LEFT:case KEY_UP:   G.osel=(G.osel+no-1)%no; break;
        case KEY_RIGHT:case KEY_DOWN:G.osel=(G.osel+1)%no;    break;
        case KEY_ENTER:{
            Squad *sq=G.sel<ns?&G.squads[G.sel]:NULL;
            if(sq){Task t=ORDER_OPTS[G.osel];sq->task=t;
                char m[MSG_LEN];
                snprintf(m,MSG_LEN,"Orders: %s Sq assigned %s.",sq->name,TASK_DEFS[t].name);
                log_msg(m);}
            G.orders_mode=0; break;}
        case KEY_ESC:G.orders_mode=0; break;
        default: break;
        }
    } else {
        switch(key){
        case KEY_UP:case KEY_LEFT:  G.sel=(G.sel+ns-1)%ns; break;
        case KEY_DOWN:case KEY_RIGHT:G.sel=(G.sel+1)%ns;   break;
        case KEY_SPACE: end_turn(); break;
        case KEY_O: G.orders_mode=1; G.osel=0; break;
        case KEY_C: screen_command();  break;
        case KEY_I: screen_intel();    break;
        case KEY_D: screen_dossier(G.sel); break;
        case KEY_Q: return 2;
        case KEY_ESC: return 1;
        default: break;
        }
    }
    return 0;
}

/* ═══════════════════════════════════════════════════════════
   MAIN
   ═══════════════════════════════════════════════════════════ */

int main(void){
    srand((unsigned)time(NULL));
    raw_on(); cur_off();

main_menu:;
    int choice=screen_main_menu();
    if(choice==4) goto done;
    if(choice==2){screen_codex(0); goto main_menu;}
    if(choice==3){screen_credits(); goto main_menu;}
    if(choice==1){
        int slot=screen_save_load(0);
        if(slot<0) goto main_menu;
        if(!load_slot(slot)){add_msg("Failed to load."); goto main_menu;}
    } else {
        Difficulty d=screen_difficulty();
        new_game(d);
    }

    /* main game loop */
    while(!G.over){
        render();
        /* show pending HQ dispatch before accepting input */
        if(G.dispatch_pending>=0){
            int idx=G.dispatch_pending;
            G.dispatch_pending=-1;
            int comply=screen_hq_dispatch(idx);
            apply_dispatch(idx,comply);
        }
        Key k=getch();
        int r=handle(k);
        if(r==2) goto main_menu;
        if(r==1){
            int pm=screen_pause_menu();
            switch(pm){
            case 0: break;
            case 1:{int slot=screen_save_load(1);
                    if(slot>=0){if(save_slot(slot))add_msg("Game saved.");else add_msg("Save failed.");}break;}
            case 2:{int slot=screen_save_load(0);
                    if(slot>=0&&!load_slot(slot)){add_msg("Load failed.");} break;}
            case 3: screen_codex(0);        break;
            case 4: screen_diary();         break;
            case 5: screen_dossier(G.sel);  break;
            case 6: screen_upgrades();      break;
            case 7: goto main_menu;
            }
        }
    }
    if(G.over) screen_end();
    goto main_menu;

done:
    cls(); cur_on(); raw_off();
    printf("\nBurden of Command — Thank you for playing.\n"
           "Auf Wiedersehen, Captain Thorne.\n\n");
    fflush(stdout);
    return 0;
}
