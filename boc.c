/* ================================================================
   BURDEN OF COMMAND — A WWI Trench Management Tycoon
   Cross-platform C (Windows + POSIX)

   Linux / macOS:
     cc -O2 -o boc boc.c && ./boc

   Windows (MinGW / MSYS2 / Git Bash):
     gcc -O2 -o boc.exe boc.c && boc.exe

   Windows (MSVC, Developer Command Prompt):
     cl /O2 /Fe:boc.exe boc.c

   Windows Terminal or any VT100-capable console required on Windows.
   Minimum terminal size: 80 columns × 24 rows, UTF-8 locale.
   ================================================================ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#  include <windows.h>   /* Console API, SetConsoleMode, GetStdHandle */
#  include <conio.h>     /* _getch()                                  */
#  ifdef _MSC_VER
#    include <intrin.h>  /* __popcnt() on MSVC                        */
#  endif
#else
#  include <termios.h>
#  include <unistd.h>
#endif

/* ================================================================
   TERMINAL / ANSI
   ================================================================ */

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

/* Box-drawing (UTF-8) */
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
#define SYM_UP "\xe2\x86\x91"  /* ↑ */
#define SYM_DN "\xe2\x86\x93"  /* ↓ */
#define SYM_EQ "\xe2\x80\x93"  /* – */
#define SYM_LF "\xe2\x86\x90"  /* ← */
#define SYM_RT "\xe2\x86\x92"  /* → */
#define SYM_BL "\xe2\x97\x8f"  /* ● */
#define SYM_CI "\xe2\x97\x8b"  /* ○ */
#define SYM_HT "\xe2\x8c\x96"  /* ⌖ (tools icon) */
#define SYM_CK "\xe2\x9c\x93"  /* ✓ */
#define SYM_WN "\xe2\x9a\xa0"  /* ⚠ */
#define SYM_SK "\xe2\x98\xa0"  /* ☠ */

/* ── Raw mode — cross-platform ── */
#ifdef _WIN32

static DWORD s_orig_in, s_orig_out;

static void raw_on(void){
    HANDLE hi = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE ho = GetStdHandle(STD_OUTPUT_HANDLE);
    GetConsoleMode(hi, &s_orig_in);
    GetConsoleMode(ho, &s_orig_out);
    /* Raw input: disable echo and line-buffering */
    SetConsoleMode(hi, ENABLE_EXTENDED_FLAGS);
    /* Enable ANSI / VT100 escape processing on stdout */
    SetConsoleMode(ho, s_orig_out
                       | ENABLE_VIRTUAL_TERMINAL_PROCESSING
                       | ENABLE_PROCESSED_OUTPUT);
    /* Switch console to UTF-8 so box-drawing chars render correctly */
    SetConsoleOutputCP(65001);
    SetConsoleCP(65001);
}
static void raw_off(void){
    SetConsoleMode(GetStdHandle(STD_INPUT_HANDLE),  s_orig_in);
    SetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), s_orig_out);
}

#else  /* POSIX */

static struct termios s_orig;
static void raw_on(void){
    struct termios t; tcgetattr(STDIN_FILENO,&s_orig);
    t=s_orig; t.c_lflag&=~(unsigned)(ECHO|ICANON);
    t.c_cc[VMIN]=1; t.c_cc[VTIME]=0;
    tcsetattr(STDIN_FILENO,TCSANOW,&t);
}
static void raw_off(void){ tcsetattr(STDIN_FILENO,TCSANOW,&s_orig); }

#endif /* _WIN32 */

typedef enum {
    KEY_NONE=0,KEY_UP,KEY_DOWN,KEY_LEFT,KEY_RIGHT,
    KEY_ENTER,KEY_ESC,KEY_SPACE,
    KEY_A,KEY_B,KEY_C,KEY_D,KEY_E,KEY_F,KEY_G,KEY_H,
    KEY_I,KEY_J,KEY_K,KEY_L,KEY_M,KEY_N,KEY_O,KEY_P,
    KEY_Q,KEY_R,KEY_S,KEY_T,KEY_U,KEY_V,KEY_W,KEY_X,
    KEY_Y,KEY_Z,
    KEY_0,KEY_1,KEY_2,KEY_3,KEY_4,KEY_5,KEY_6,KEY_7,KEY_8,KEY_9,
} Key;

/* getch — cross-platform single-keypress reader.
 *
 * POSIX:   ESC [ X VT100 sequences for arrow keys.
 * Windows: _getch() returns 0xE0 then a scan code for extended keys.
 *          Arrow scan codes: Up=72, Down=80, Left=75, Right=77.
 */
static Key getch(void){
#ifdef _WIN32
    int c = _getch();
    /* Extended key: 0xE0 (arrows/ins/del/etc) or 0x00 (F-keys) prefix */
    if(c==0xE0||c==0x00){
        int c2=_getch();
        switch(c2){
            case 72: return KEY_UP;
            case 80: return KEY_DOWN;
            case 75: return KEY_LEFT;
            case 77: return KEY_RIGHT;
        }
        return KEY_NONE;
    }
    if(c==27)  return KEY_ESC;
    if(c=='\r'||c=='\n') return KEY_ENTER;
    if(c==' ') return KEY_SPACE;
    if(c>='a'&&c<='z') return (Key)(KEY_A+c-'a');
    if(c>='A'&&c<='Z') return (Key)(KEY_A+c-'A');
    if(c>='0'&&c<='9') return (Key)(KEY_0+c-'0');
    return KEY_NONE;
#else  /* POSIX */
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
#endif
}

/* ── Utilities ── */
static int clamp(int x,int a,int b){return x<a?a:x>b?b:x;}
static int rng_bool(float p){return (float)rand()/RAND_MAX<p;}
static int rng_range(int lo,int hi){return lo+rand()%(hi-lo+1);}

/* Portable popcount — GCC/Clang has __builtin_popcount;
 * MSVC has __popcnt (intrin.h); we provide a fallback for all others. */
#if defined(_MSC_VER)
#  define popcount(x) ((int)__popcnt((unsigned)(x)))
#elif defined(__GNUC__)||defined(__clang__)
#  define popcount(x) __builtin_popcount((unsigned)(x))
#else
static int popcount(unsigned int x){
    int n=0; while(x){n+=(int)(x&1u);x>>=1;} return n;
}
#endif

static void make_bar(char *out,int v,int mx,int w){
    int f=(int)((float)w*clamp(v,0,mx)/(mx?mx:1)+0.5f),e=w-f,i;
    for(i=0;i<f;i++) out[i]='#';
    for(i=0;i<e;i++) out[f+i]='.';
    out[w]='\0';
}
static void ppad(const char *s,int w){
    int n=(int)strlen(s);
    if(n>=w) fwrite(s,1,w,stdout);
    else { fputs(s,stdout); for(int i=n;i<w;i++) putchar(' '); }
}
static void draw_box(int r1,int c1,int r2,int c2){
    at(r1,c1);fg(BLU);attr_bold();
    fputs(BOX_TL,stdout);for(int i=c1+1;i<c2;i++) fputs(BOX_H,stdout);fputs(BOX_TR,stdout);
    for(int r=r1+1;r<r2;r++){at(r,c1);fputs(BOX_V,stdout);at(r,c2);fputs(BOX_V,stdout);}
    at(r2,c1);fputs(BOX_BL,stdout);for(int i=c1+1;i<c2;i++) fputs(BOX_H,stdout);fputs(BOX_BR,stdout);
    attr_rst();fflush(stdout);
}
static void draw_hline_mid(int r,int c1,int c2){
    at(r,c1);fg(BLU);attr_bold();fputs(BOX_LM,stdout);
    for(int i=c1+1;i<c2;i++){fputs(BOX_H,stdout);} fputs(BOX_RM,stdout); attr_rst();
}

/* ================================================================
   DATA TABLES
   ================================================================ */

/* ── Tasks (7 total) ── */
typedef enum {
    TASK_STANDBY=0,TASK_PATROL,TASK_RAID,TASK_REPAIR,TASK_REST,
    TASK_FORAGE,TASK_SCAVENGE,
    TASK_COUNT
} Task;
typedef struct {
    const char *name; int color;
    int fat_delta,mor_delta,ammo_cost,tools_gain,food_gain,ammo_gain;
    const char *desc;
} TaskDef;
static const TaskDef TASK_DEFS[TASK_COUNT]={
    {"STANDBY", WHT,  -5, 0, 1,0,0,0, "Hold position. Conserves fatigue."},
    {"PATROL",  CYN, +10,+3, 3,0,0,0, "Scout no-man's-land. Morale up, costs ammo."},
    {"RAID",    RED, +20,+4, 6,0,0,0, "Strike enemy lines. High risk, high reward."},
    {"REPAIR",  YEL,  +5, 0, 1,2,0,0, "Fix works. Generates tools each turn."},
    {"REST",    GRN, -15,+5, 1,0,0,0, "Stand down. Restores fatigue and morale."},
    {"FORAGE",  MAG, +12,-2, 0,0,6,0, "Scour billets and farms. Find food; low morale."},
    {"SCAVENGE",YEL, +15,-3, 0,1,0,3, "Strip no-man's-land for materiel. Find ammo+tools."},
};
static const Task ORDER_OPTS[TASK_COUNT]={
    TASK_STANDBY,TASK_PATROL,TASK_RAID,TASK_REPAIR,TASK_REST,TASK_FORAGE,TASK_SCAVENGE
};

/* ── Ration Policy ── */
typedef enum {RATION_EMERGENCY=0,RATION_QUARTER,RATION_HALF,RATION_FULL,RATION_COUNT} RationLevel;
typedef struct {
    const char *name; int color;
    float food_mul;    /* multiplier on food consumed */
    int   mor_per_turn;/* morale adjustment per squad per turn */
    const char *desc;
} RationDef;
static const RationDef RATION_DEFS[RATION_COUNT]={
    {"EMERGENCY",RED,   0.30f,-8, "Bare minimum. Severe morale drain. Last resort."},
    {"QUARTER",  RED,   0.55f,-4, "Quarter rations. Significant morale impact."},
    {"HALF",     YEL,   0.75f,-1, "Half rations. Minor morale cost. Stretches supply."},
    {"FULL",     GRN,   1.00f, 0, "Full issue. No morale penalty. Standard rate."},
};

/* ── Ammo Policy ── */
typedef enum {AMMO_CONSERVE=0,AMMO_NORMAL,AMMO_LIBERAL,AMMO_COUNT} AmmoPolicy;
typedef struct {
    const char *name; int color;
    float ammo_mul;        /* multiplier on ammo consumed */
    float patrol_mor_mul;  /* multiplier on patrol morale bonus */
    float raid_resist_add; /* added to raid resistance */
    const char *desc;
} AmmoPolicyDef;
static const AmmoPolicyDef AMMO_DEFS[AMMO_COUNT]={
    {"CONSERVE",GRN,0.60f,0.60f,-0.10f,"Save ammo. Patrols less effective. Harder to hold raids."},
    {"NORMAL",  WHT,1.00f,1.00f, 0.00f,"Standard issue. Balanced effectiveness."},
    {"LIBERAL", RED,1.50f,1.40f, 0.15f,"Spend freely. Maximum patrol/raid effectiveness."},
};

/* ── Barter exchange rates ── */
typedef enum {RES_FOOD=0,RES_AMMO,RES_MEDS,RES_TOOLS,RES_COUNT} ResId;
typedef struct {
    ResId from,to; int give,get; const char *desc;
} BarterRate;
static const BarterRate BARTER_RATES[]={
    /* food trades */
    {RES_FOOD,RES_AMMO, 20, 8, "Trade rations for rounds (20 food → 8 ammo)"},
    {RES_FOOD,RES_MEDS, 15, 5, "Barter food for field dressings (15 food → 5 meds)"},
    {RES_FOOD,RES_TOOLS,18, 4, "Exchange rations for tools (18 food → 4 tools)"},
    /* ammo trades */
    {RES_AMMO,RES_FOOD,  8,18, "Sell rounds for food (8 ammo → 18 food)"},
    {RES_AMMO,RES_MEDS,  10, 4, "Swap bullets for bandages (10 ammo → 4 meds)"},
    {RES_AMMO,RES_TOOLS,  8, 4, "Trade shells for equipment (8 ammo → 4 tools)"},
    /* meds trades */
    {RES_MEDS,RES_FOOD,  5,14, "Sell dressings for rations (5 meds → 14 food)"},
    {RES_MEDS,RES_AMMO,  4, 9, "Exchange morphia for rounds (4 meds → 9 ammo)"},
    {RES_MEDS,RES_TOOLS, 3, 4, "Trade supplies for tools (3 meds → 4 tools)"},
    /* tools trades */
    {RES_TOOLS,RES_FOOD, 3,14, "Sell tools for food (3 tools → 14 food)"},
    {RES_TOOLS,RES_AMMO, 3, 7, "Trade equipment for ammo (3 tools → 7 ammo)"},
    {RES_TOOLS,RES_MEDS, 4, 3, "Exchange tools for field kit (4 tools → 3 meds)"},
};
#define BARTER_COUNT (int)(sizeof(BARTER_RATES)/sizeof(BARTER_RATES[0]))

static const char *RES_NAMES[RES_COUNT]={"Food","Ammo","Meds","Tools"};
static const int   RES_COLORS[RES_COUNT]={GRN,YEL,CYN,MAG};

/* ── Personalities ── */
typedef enum{PERS_STEADFAST=0,PERS_BRAVE,PERS_COWARDLY,PERS_DRUNKARD,PERS_COUNT}Personality;
typedef struct{const char *name;float mul;const char *effect_str;}PersDef;
static const PersDef PERS_DEFS[PERS_COUNT]={
    {"Steadfast",1.00f,"Consistent and reliable."},
    {"Brave",    1.20f,"+20% to fatigue/morale changes."},
    {"Cowardly", 0.70f,"-30% effectiveness under fire."},
    {"Drunkard", 0.85f,"-15% effectiveness; unpredictable."},
};

/* ── Notable soldier traits ── */
typedef enum {
    TRAIT_SHARPSHOOTER=0,TRAIT_COOK,TRAIT_MEDIC,TRAIT_SCROUNGER,
    TRAIT_MUSICIAN,TRAIT_RUNNER,TRAIT_COUNT
} SoldierTrait;
typedef struct {
    const char *name; int color;
    const char *effect;
    int res_id;  /* which resource they passively help with (-1=none) */
    int res_amt; /* amount saved/found per turn */
} TraitDef;
static const TraitDef TRAIT_DEFS[TRAIT_COUNT]={
    {"Sharpshooter",YEL,"Raid resist +10%. Sniper chance halved.",-1,0},
    {"Cook",        GRN,"Saves 1 food per turn passively.",RES_FOOD,1},
    {"Medic",       CYN,"Treats 1 wound per turn automatically.",RES_MEDS,0},
    {"Scrounger",   MAG,"Finds 1 ammo or tools per turn randomly.",RES_AMMO,1},
    {"Musician",    WHT,"Morale +2 per turn passively.",-1,0},
    {"Runner",      BLU,"Convoy ETA reduced by 1 turn.",-1,0},
};

/* ── Difficulty ── */
typedef enum{DIFF_EASY=0,DIFF_NORMAL,DIFF_HARD,DIFF_IRONMAN,DIFF_COUNT}Difficulty;
typedef struct{
    const char *name,*subtitle; int color;
    float food_mul,event_mul,morale_mul;
    float score_mul_x10; int save_allowed;
}DiffDef;
static const DiffDef DIFF_DEFS[DIFF_COUNT]={
    {"GREEN FIELDS", "Resources plentiful. Events rare.     For newcomers.",
     GRN,0.6f,0.6f,0.7f, 8,1},
    {"INTO THE MUD", "Balanced. The intended experience.    Recommended.",
     YEL,1.0f,1.0f,1.0f,10,1},
    {"NO MAN'S LAND","Scarce supplies. Brutal events.       For veterans.",
     RED,1.4f,1.5f,1.3f,14,1},
    {"GOD HELP US",  "One life. No loading. Maximum stakes. True iron.",
     MAG,1.6f,1.8f,1.5f,20,0},
};

/* ── Weather ── */
typedef enum{WEATHER_CLEAR=0,WEATHER_RAIN,WEATHER_FOG,WEATHER_SNOW,WEATHER_STORM,WEATHER_COUNT}Weather;
typedef struct{const char *label;int color;}WeatherDef;
static const WeatherDef WEATHER_DEFS[WEATHER_COUNT]={
    {"Clear  ",CYN},{"Rainy  ",BLU},{"Foggy  ",WHT},{"Snowing",WHT},{"Storm  ",MAG},
};
typedef struct{int fat_per_turn,food_extra,agg_drift;float raid_mul;const char *note;}WeatherEffect;
static const WeatherEffect WEATHER_FX[WEATHER_COUNT]={
    { 0,0, 0,1.00f,"No effect on operations."},
    { 3,1, 0,1.00f,"Mud slows everything. +fatigue, +food."},
    { 1,0, 0,1.50f,"Low visibility. Raids far more likely."},
    { 4,2,+2,0.75f,"Bitter cold. Heavy fatigue and food drain."},
    { 6,1,+4,1.20f,"Storm. Severe fatigue. Enemy emboldened."},
};
static Weather weather_next(Weather w){
    static const Weather L[10]={0,0,0,0,WEATHER_RAIN,WEATHER_RAIN,WEATHER_FOG,WEATHER_CLEAR,WEATHER_STORM,WEATHER_CLEAR};
    Weather r[10]; memcpy(r,L,sizeof(r));
    r[0]=r[1]=r[2]=r[3]=w; return r[rand()%10];
}

/* ── Morale levels ── */
typedef struct{int thr;const char *label;int color;}MorLevel;
static const MorLevel MOR_LEVELS[]={
    {80,"EXCELLENT",GRN},{65,"GOOD",GRN},{45,"FAIR",YEL},{25,"POOR",RED},{0,"CRITICAL",RED},
};
#define MOR_LEVEL_COUNT 5
static const char *mor_label(int m){for(int i=0;i<MOR_LEVEL_COUNT;i++) if(m>=MOR_LEVELS[i].thr) return MOR_LEVELS[i].label;return"CRITICAL";}
static int mor_color(int m){return m>=65?GRN:m>=45?YEL:RED;}

/* ── Raid resistance per task ── */
static const float RAID_RESIST[TASK_COUNT]={0.30f,0.55f,0.80f,0.40f,0.10f,0.20f,0.35f};

/* ── Random events ── */
typedef struct{float base_prob,agg_divisor;}RandEventProb;
typedef enum{
    REVT_ARTILLERY=0,REVT_ENEMY_RAID,REVT_GAS,REVT_MAIL,REVT_RATS,
    REVT_INFLUENZA,REVT_SGT_BREAKDOWN,REVT_SUPPLY_CONVOY,REVT_REINFORCE,
    REVT_SNIPER,REVT_FRATERNIZE,REVT_HERO,REVT_FRIENDLY_FIRE,REVT_CACHE,
    REVT_FOOD_SPOIL,REVT_AMMO_DAMP,REVT_WOUND_HEAL,REVT_SECTOR_ASSAULT,
    REVT_COUNT
} RandEventId;
static const RandEventProb RAND_PROBS[REVT_COUNT]={
    {0,350.f},{0,500.f},{0.035f,0},{0.10f,0},{0.07f,0},
    {0.04f,0},{0.03f,0},{0.08f,0},{0.04f,0},
    {0,600.f},{0.012f,0},{0.05f,0},{0.008f,0},{0,700.f},
    {0.06f,0}, /* FOOD_SPOIL    */
    {0.04f,0}, /* AMMO_DAMP     */
    {0.12f,0}, /* WOUND_HEAL    */
    {0,450.f}, /* SECTOR_ASSAULT */
};

/* ── Upgrades (12 total) ── */
typedef enum{
    UPG_DUCKBOARDS=0,UPG_SANDBAG,UPG_DUGOUT,UPG_PERISCOPE,
    UPG_LEWIS_NEST,UPG_SUMP,UPG_SIGNAL_WIRE,UPG_RUM_STORE,
    UPG_FIELD_HOSP,UPG_OBS_POST,UPG_MUNITIONS,UPG_FOOD_CACHE,
    UPG_COUNT
} UpgradeId;
typedef struct{const char *name;int tools_cost;const char *desc,*passive;}UpgradeDef;
static const UpgradeDef UPG_DEFS[UPG_COUNT]={
    {"Duckboards",       8,"Raised walkways over the mud.",
                           "Rain/storm fatigue halved."},
    {"Sandbag Revetments",12,"Reinforced firing bay.",
                           "Artillery casualties -1."},
    {"Officers' Dugout", 15,"Reinforced command shelter.",
                           "+1 morale/turn all squads passively."},
    {"Periscope",        10,"Mirror periscope for safe obs.",
                           "PATROL +2 extra morale/turn."},
    {"Lewis Gun Nest",   20,"Sandbagged MG emplacement.",
                           "+15% raid resistance all squads."},
    {"Trench Sump",       6,"Drainage channel under fire-step.",
                           "Disease/influenza chance halved."},
    {"Signal Wire",       8,"Buried telephone line to support.",
                           "+1 CP per AM turn."},
    {"Rum Store",        10,"Locked medicinal rum crate.",
                           "Rum Ration costs 0 CP."},
    {"Field Hospital",   18,"Expanded aid post with stretchers.",
                           "Wounds heal 1 extra/turn. Meds halved on triage."},
    {"Observation Post", 14,"Forward observation point.",
                           "Sniper/raid events -25% chance. Sector threat +1 awareness."},
    {"Munitions Store",  12,"Reinforced ammo crypt.",
                           "Ammo max +25. Ammo Damp events eliminated."},
    {"Food Cache",       10,"Hidden emergency food reserve.",
                           "Food max +25. Spoilage events eliminated."},
};

/* ── Command Actions (10 total) ── */
typedef enum{
    CMD_RUM=0,CMD_LETTERS,CMD_MEDICAL,CMD_REPRIMAND,
    CMD_SPEECH,CMD_RATIONS_EXTRA,CMD_CEREMONY,CMD_LEAVE,
    CMD_TREAT_WOUNDED,CMD_SUPPLY_REQUEST,
    CMD_COUNT
}CmdActionId;
typedef struct{const char *name;int cp_cost,food_cost,meds_cost;const char *desc,*effect;}CmdActionDef;
static const CmdActionDef CMD_DEFS[CMD_COUNT]={
    {"Rum Ration",       1,5,0, "Issue rum to selected squad.","+15 morale selected squad. -5 food."},
    {"Write Letters",    1,0,0, "Help men write letters home.","+10 morale selected squad."},
    {"Medical Triage",   1,0,5, "Field dressing rotation.","-25 fatigue selected squad. -5 meds."},
    {"Inspect/Reprimand",0,0,0, "Snap inspection.","-8 fatigue, -5 morale. No CP cost."},
    {"Officer's Speech", 2,0,0, "Address entire company.","+8 morale all squads."},
    {"Emergency Rations",2,20,0,"Break emergency food.","-15 fatigue all squads. -20 food."},
    {"Medal Ceremony",   3,0,0, "Formal commendation at HQ.","+20 morale selected squad. +1 medal."},
    {"Comp. Leave",      2,0,0, "Send one man to rear.","-1 man but +15 morale. Rare mercy."},
    {"Treat Wounded",    1,0,3, "Emergency wound treatment.","Heal up to 2 wounds in selected squad. -3 meds."},
    {"Supply Request",   2,0,0, "Petition HQ for supplies.","HQ rep-dependent convoy in 5-10 turns."},
};

/* ── HQ Dispatches (5) ── */
typedef struct{
    int turn;const char *title;const char *body[6];
    const char *comply_label,*defy_label,*comply_result,*defy_result;
    int cy_agg,cy_all_mor,cy_ammo,cy_food,cy_force_raid,cy_all_standby,cy_lose_men,cy_medals;
    int df_agg,df_all_mor; int cy_rep_delta,df_rep_delta;
}HqDispatch;
static const HqDispatch HQ_DISPATCHES[]={
{5,"BRIGADE ORDER — MANDATORY NIGHT RAID",
 {"Brigade HQ requires one section to advance at 0300 and destroy",
  "the enemy wire-cutting position at grid ref C-4.",
  "Capture of prisoners would be desirable but not required.",
  "Failure to execute this order will be viewed most seriously.",
  "Acknowledge receipt by runner. Captain Thorne to confirm personally.",NULL},
 "COMPLY — Assign a squad to raid","DEFY  — Decline the order",
 "Raid launched. Intel noted by Brigade. Aggression falls.",
 "Brigade marks you 'obstructive'. Enemy grows bolder.",
 -8,0,-10,0,1,0,0,0, +14,-5, +10,-15},
{16,"HQ ORDER — RATION REDUCTION EFFECTIVE IMMEDIATELY",
 {"Supply lines between the railhead and the forward position",
  "are under sustained enemy interdiction fire.",
  "All forward units are to reduce daily ration draw by one quarter,",
  "effective at 0600 tomorrow.",
  "The men are expected to bear this with good grace.",NULL},
 "COMPLY — Reduce rations","DEFY  — Maintain full rations",
 "Rations cut. Food reserves fall sharply. Men are hungry but obedient.",
 "HQ notes your refusal. Next convoy will be delayed.",
 0,-3,-5,-25,0,0,0,0, +8,0, +5,-20},
{28,"ORDER — SECONDMENT TO 3RD PIONEER BATTALION",
 {"The Pioneer Corps requires experienced infantry for deep tunneling",
  "operations in the Messines sector. Two men from your company",
  "are to report to 3rd Pioneer Bn HQ by 0600 hours tomorrow.",
  "Selection is at the Company Commander's discretion.",
  "This is not a request. Acknowledge by 2200 tonight.",NULL},
 "COMPLY — Send two men","DEFY  — Refuse the transfer",
 "Two men depart. Pioneers reciprocate with ammunition.",
 "You protect your men. Brigade is furious. Enemy senses weakness.",
 0,-4,+8,0,0,0,2,0, +12,-6, +8,-18},
{44,"INTELLIGENCE DISPATCH — GERMAN OFFENSIVE EXPECTED",
 {"Brigade Intelligence reports German forces massing along a",
  "3-kilometre front including your sector.",
  "All units are to assume defensive posture immediately.",
  "Ammunition is being pre-positioned at forward dumps.",
  "All sections to STANDBY. Conserve all resources. Acknowledge.",NULL},
 "COMPLY — Stand all squads by","DEFY  — Maintain current posture",
 "All squads to STANDBY. Ammo delivered. Enemy can't find an opening.",
 "Men prefer their tasks. Offensive hits a disorganised sector.",
 +5,0,+20,0,0,1,0,0, +10,+5, +12,-8},
{60,"COMMENDATION — VICTORIA CROSS NOMINATION REQUESTED",
 {"Brigade Commander writes personally: the conduct of the men",
  "of this sector has been noted with admiration at Corps level.",
  "One man is to be forwarded for formal commendation.",
  "Submit name by runner at first light.",
  "This is an honour reflecting upon the whole company.",NULL},
 "COMPLY — Submit a nomination","DEFY  — Decline the honour",
 "Ceremony held at Brigade. The men are enormously proud.",
 "Word spreads the Captain blocked the honour. Morale suffers.",
 0,+10,0,0,0,0,0,2, 0,-5, +15,-12},
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

/* ── Notable soldier name pools (per squad, 2 men each) ── */
typedef struct{const char *forename,*surname;SoldierTrait trait;}NotableDef;
static const NotableDef NOTABLE_INIT[SQUAD_INIT_COUNT][2]={
    { {"Pte. Jack","Whitmore",TRAIT_SHARPSHOOTER},{"Pte. Tom","Beecroft",TRAIT_COOK} },
    { {"Pte. Alf","Morley",   TRAIT_MEDIC},        {"Pte. Bill","Darton", TRAIT_MUSICIAN} },
    { {"Pte. Dan","Pollard",  TRAIT_SCROUNGER},    {"Pte. Fred","Stubbs", TRAIT_RUNNER} },
    { {"Pte. Sam","Colby",    TRAIT_COOK},          {"Pte. Ned","Finch",  TRAIT_SHARPSHOOTER} },
};

static const char *INIT_MSGS[]={
    "Welcome, Captain Thorne. God help us.",
    "ORDERS: Hold the line for 6 weeks.",
    "Supply convoy expected in ~3 turns.",
    "Intelligence: Expect shelling tonight.",
};
#define INIT_MSG_COUNT 4

/* ─── Historical events ─── */
typedef struct{int turn;const char *text;int agg_delta,all_mor_delta,all_fat_delta,set_weather;}HistEvent;
static const HistEvent HIST_EVENTS[]={
    { 2,"HQ runner: 'Hold sector at all costs. No retreat. Acknowledge.'",+5, 0, 0,-1},
    { 7,"End of first week. The rain has arrived. The mud is inescapable.",+3,-3,+5,WEATHER_RAIN},
    {14,"A chaplain visits the line. He is nineteen, fresh from seminary.", 0,+4, 0,-1},
    {21,"Haig communique: 'continued pressure'. The men find this darkly amusing.",0,0,0,-1},
    {28,"A German deserter is escorted through your sector. He is sixteen.",-5,+2, 0,-1},
    {35,"Six days of rain. The mud has become the primary tactical obstacle.",0,-3,+6,WEATHER_RAIN},
    {42,"Half the campaign. A relief column passes — they are not for you.",+5,-5, 0,-1},
    {49,"Word: a major offensive has failed eight miles north.",+8,-4, 0,-1},
    {56,"Rumour: peace negotiations. No one believes it. Everyone wants to.",0,+5, 0,-1},
    {63,"The men have stopped flinching at close shell-bursts. A bad sign.",+3, 0, 0,-1},
    {70,"Final week. The men are lean, hollow-eyed, and unbreakable.", 0,+6,-5,-1},
    {77,"Relief units spotted 3 miles behind the line. Almost.",0,+8,-8,-1},
};
#define HIST_EVENT_COUNT (int)(sizeof(HIST_EVENTS)/sizeof(HIST_EVENTS[0]))

/* ─── Layout constants (80×23) ─── */
#define TW  80
#define DIV 44
#define LW  42
#define RW  35

/* ================================================================
   CODEX — 15 entries
   ================================================================ */

typedef struct{const char *title;int title_color;const char *lines[12];}CodexEntry;
static const CodexEntry CODEX[]={
{"THE WESTERN FRONT",YEL,{"The Western Front stretches 700 km from the North Sea to Switzerland.",
  "Since the failure of the Schlieffen Plan in 1914, both sides entrenched.",
  "Progress is measured in metres and paid for in thousands of lives.",
  "You command a sector near Passchendaele, Flanders, Belgium.",
  "The mud here is legendary. Men have drowned in shell craters.",
  "Your orders arrive from Brigade HQ twelve miles behind the line.",
  "They have not personally visited the front in three months.",
  "The Somme claimed 57,000 British casualties on its first day alone.",
  "The Third Ypres offensive (Passchendaele) began July 31st, 1917.",
  "Haig believes attrition will break Germany. The men are not consulted.",NULL}},
{"TRENCH WARFARE",CYN,{"The front-line trench: a ditch six feet deep and two feet wide.",
  "Duckboards line the floor. Walls shored with corrugated iron.",
  "A fire-step lets men peer over the parapet at stand-to.",
  "Behind: support trenches, reserve trenches, communication lines.",
  "The smell: mud, cordite, decay, latrine, wet wool. Indescribable.",
  "Trench foot afflicts men standing in water without relief.",
  "Rats as large as cats devour rations and gnaw at the sleeping.",
  "Average life expectancy of a second lieutenant: six weeks.",
  "Between attacks: fatigues, carrying parties, digging, sandbagging.",
  "A man can go mad from the waiting as easily as from the shells.",NULL}},
{"GAS WARFARE",GRN,{"April 1915: the Germans first used chlorine gas at Ypres.",
  "The yellow-green cloud drifted over Allied trenches at dusk.",
  "Men described it as lungs full of fire — drowning from inside.",
  "Phosgene followed: colourless, smelling faintly of cut hay. Deadlier.",
  "Mustard gas, introduced July 1917, blisters skin and blinds eyes.",
  "Mustard gas has no immediate smell. Men often don't know until too late.",
  "A well-drilled unit with Small Box Respirators can survive most attacks.",
  "Without medical supplies a gas attack leaves lasting casualties.",
  "Gas masks fog in cold air and suffocate under exertion.",
  "The sound of the gas alarm — phosgene rattles — haunts veterans for life.",NULL}},
{"CAPTAIN ALISTAIR THORNE",YEL,{"Captain Alistair Thorne, age 29. 11th Battalion, East Lancashire Regiment.",
  "Born in Preston. Joined Kitchener's New Army voluntarily in August 1914.",
  "Commissioned after the Somme. Three of his original platoon survived.",
  "He does not speak of the Somme. He does not need to.",
  "He writes to his mother every Sunday. Half the letters are returned.",
  "He was mentioned in dispatches once, at Beaumont Wood.",
  "He keeps a photograph of his sister Agnes inside his breast pocket.",
  "His wristwatch stopped during a barrage in April. He has not replaced it.",
  "He still believes in what he is doing. He is no longer certain why.",
  "He is, in all practical terms, the last man standing in his draft.",NULL}},
{"SGT. HARRIS — BRAVE",RED,{"Sergeant Thomas Harris, age 34. Former coal miner from Wigan.",
  "Volunteered August 1914, driven by genuine and uncomplicated patriotism.",
  "Was at the Gallipoli landings. Does not discuss what he saw there.",
  "His bravery borders on recklessness — volunteers for every night raid.",
  "The men of Alpha Section follow him without hesitation.",
  "He would die for any one of them. They all know it.",
  "A recommendation for the Military Medal is pending at Brigade.",
  "He sharpens his bayonet each evening. He reads no books.",
  "His hands tremble slightly at breakfast. He pours his tea very carefully.",
  "He does not mention the trembling. Neither do the men.",NULL}},
{"SGT. MOORE — STEADFAST",GRN,{"Sergeant William Moore, age 41. Regular Army since 1897.",
  "Fought in the Second Boer War, the Northwest Frontier, France since 1914.",
  "The most experienced soldier in the company by a considerable margin.",
  "He is the calm eye at the centre of every storm.",
  "Even under the heaviest barrage he moves deliberately, checking men.",
  "He has four daughters at home in Dorset. He writes each weekly.",
  "He has never been wounded. The men consider this extraordinary luck.",
  "His vice: strong tea, guarded jealously from all appropriation.",
  "He has seen enough officers come and go that he does not form attachments.",
  "He respects Captain Thorne. This is rare and means a great deal.",NULL}},
{"SGT. LEWIS — DRUNKARD",YEL,{"Sergeant Owen Lewis, age 38. Former schoolteacher from Cardiff, Wales.",
  "Joined 1915, commissioned briefly as a second lieutenant.",
  "Demoted following an incident involving a rum store and a court of inquiry.",
  "The incident does not appear in any accessible official record.",
  "He was a fine soldier and a finer man once. The rum ration found him.",
  "He hides bottles inside the sandbag walls. The men pretend not to notice.",
  "On good mornings he recites Keats and Wilfred Owen to the section.",
  "The men listen. They do not always understand. They listen anyway.",
  "He is terrified of dying and more terrified that he deserves to.",
  "He has not had a full night's sleep since the winter of 1915.",NULL}},
{"SGT. BELL — COWARDLY",MAG,{"Sergeant Arthur Bell, age 26. Bank clerk from Lambeth, London.",
  "Conscripted under the Military Service Act in January 1916.",
  "He did not want to come. He stated this clearly to his tribunal.",
  "His sergeant's stripes came after two better men died in one week.",
  "He freezes under concentrated fire. His men have begun to notice.",
  "He is not a villain. He is a man violently miscast by history.",
  "Privately he writes detailed letters cataloguing his fear.",
  "He sends them to no one. He keeps them in a tin under his bedroll.",
  "He prays every night. He is no longer certain anyone is listening.",
  "He would be good at his former job. He was very good at his former job.",NULL}},
{"THE ENEMY — IMPERIAL GERMAN ARMY",RED,{"The German Imperial Army: professional, adaptive, formidably supplied.",
  "Their Sturmtruppen (stormtrooper) tactics, developed in 1917, are new.",
  "Infiltration: small groups bypass strongpoints to strike the rear.",
  "German artillery is heavy, accurate, and seemingly inexhaustible.",
  "You rarely see them. They are voices in darkness and metal in daylight.",
  "Some prisoners are boys of sixteen. The war does not discriminate.",
  "German soldiers call no-man's-land 'Niemandsland'. The word is the same.",
  "The German soldier opposite has the same rations, same rats,",
  "same fear. His officers told him the same lies. He holds his line.",
  "This does not make him your enemy any less. It makes it worse.",NULL}},
{"MEDICAL CARE IN THE FIELD",CYN,{"Regimental Aid Post: first stop for casualties. One Medical Officer.",
  "Stretcher-bearers carry wounded through open ground under fire.",
  "Casualty Clearing Stations are five miles back — hours away.",
  "Shell shock (neurasthenia) is poorly understood. Most are sent back.",
  "Morphia is scarce. Many receive rum, a pad, and a quiet word.",
  "Your medical supplies: dressings, anti-gas equipment, morphia.",
  "Run out and gas attacks become catastrophic. Disease spreads.",
  "Trench foot: preventable with dry socks and foot inspections.",
  "Wounds left untreated become infected. Men die in days.",
  "Every med spent is a bet that the man is worth saving. He is.",NULL}},
{"WEAPONS & MATERIEL",YEL,{"Lee-Enfield Mk III: 15 rounds per minute in trained hands.",
  "Pattern 1907 bayonet: 17 inches of steel. The last resort. Used often.",
  "Mills bomb No. 5: essential for raids and close defence.",
  "Lewis gun: light machine gun, 47-round drum. Section's backbone.",
  "Stokes mortar: short range, high arc. Extraordinarily effective.",
  "18-pounder field gun: workhorse of British artillery support.",
  "Ammo represents rounds, bombs, and mortar shells combined.",
  "Running low costs patrols their effectiveness. Men feel naked.",
  "LIBERAL ammo policy maximises combat power. Budget carefully.",
  "CONSERVE policy stretches supply but weakens every engagement.",NULL}},
{"RATIONS & SUPPLY",GRN,{"A British soldier required 3,000 calories per day in the field.",
  "Bully beef (canned corned beef) and hard tack biscuit were standard.",
  "Tea was not optional. It was as important as ammunition.",
  "Food on HALF rations produces quietly corrosive morale damage.",
  "EMERGENCY rations produce open resentment within 48 hours.",
  "The rum ration (SRD — Services Rum Diluted) was issued at stand-to.",
  "Issued at the captain's discretion. It was always deeply appreciated.",
  "Rats attacked food stores nightly. Eternal vigilance was required.",
  "A good cook (Pte. Whitmore notwithstanding) was worth two riflemen.",
  "The men could endure almost anything if they were fed.",NULL}},
{"COMMAND & THE OFFICER'S BURDEN",CYN,{"The company commander stands between men and a distant, abstract war.",
  "His orders come from men who have not seen the front in months.",
  "His decisions — who raids, who rests, who is sacrificed — are real.",
  "Command Points represent the finite reserve of personal authority.",
  "A captain can inspire, cajole, comfort, threaten — but not endlessly.",
  "Issue rum. Write letters for the illiterate. Hold a medal ceremony.",
  "Every act of personal leadership costs something from the commander.",
  "The men do not need a general. They need a captain who knows their names.",
  "Thorne knows all of them. He knows their wives' names. Their children's.",
  "He will carry that weight whether they survive or not.",NULL}},
{"TRENCH ENGINEERING",MAG,{"The British soldier spent far more time digging than fighting.",
  "Duckboards, drainage sumps, fire-steps: each built by hand and bayonet.",
  "Sandbag revetments absorb shell fragments and reduce casualties.",
  "A well-built dugout provides shelter and raises spirits.",
  "The Lewis gun nest: a sandbagged emplacement for the section weapon.",
  "Signal wire buried in the floor allowed communication under fire.",
  "Trench periscopes — mirrors on a stick — let men observe safely.",
  "The Field Hospital upgrade doubles the value of every medical supply.",
  "The Observation Post reveals threats before they strike.",
  "Build early. Build well. The mud will take everything else.",NULL}},
{"NOTABLE MEN — THE RANK AND FILE",WHT,{"Behind every statistic is a man with a name.",
  "Pte. Whitmore is a crack shot. He volunteers for sniping duty.",
  "Beecroft keeps a battered recipe book and makes rations edible.",
  "Pte. Morley learned first aid from his mother. He uses it every night.",
  "Darton plays mouth organ after stand-to. The men are grateful.",
  "Pollard can find a use for anything. His pouches are always full.",
  "Stubbs runs messages faster than anyone alive. He is also terrified.",
  "Colby barters cigarettes for rations with the supply wagoners.",
  "Finch shot a German officer at 400 yards in the fog. He does not boast.",
  "These men did not choose this. They chose each other. That is enough.",NULL}},
};
#define CODEX_COUNT (int)(sizeof(CODEX)/sizeof(CODEX[0]))

/* ================================================================
   GAME STRUCTURES
   ================================================================ */

#define MAX_SQUADS   8
#define MAX_MSGS     7
#define MAX_EVQ     16
#define MAX_DIARY   192
#define MSG_LEN      72
#define RES_HIST_LEN  10  /* resource history depth (turns) */
#define MAX_NOTABLES  2   /* notable men per squad */

typedef struct{char name[32];Personality pers;int ok;}Sgt;

typedef struct{
    char name[32];  /* "Pte. Jack Whitmore" */
    SoldierTrait trait;
    int alive;
}Notable;

typedef struct{
    char name[16];
    int  men,maxm,mor,fat,sick;
    Task task;
    int  has_sgt; Sgt sgt;
    int  lore_idx;
    int  wounds;          /* walking wounded count */
    Notable notables[MAX_NOTABLES]; int notable_count;
    /* stats */
    int  raids_repelled,men_lost,turns_alive;
}Squad;

typedef enum{EV_NONE=0,EV_SUPPLY,EV_REINFORCE}EvType;
typedef struct{int at;EvType type;int food,ammo,meds,tools,men;}SchedEv;
typedef struct{int turn,half_am;char text[MSG_LEN];}DiaryEntry;
typedef enum{OVER_NONE=0,OVER_WIN,OVER_LOSE,OVER_MUTINY}OverState;

typedef struct{
    /* time */
    int turn,maxt,half_am;
    /* environment */
    Weather weather; int agg;
    /* resources */
    int food,ammo,meds,tools;
    int food_max,ammo_max;       /* caps (modified by upgrades) */
    /* resource policies */
    RationLevel ration_level;
    AmmoPolicy  ammo_policy;
    /* sector threats A-D (0-100) */
    int sector_threat[4];
    /* squads */
    Squad squads[MAX_SQUADS]; int squad_count;
    /* messages & diary */
    char msgs[MAX_MSGS][MSG_LEN]; int msg_count;
    DiaryEntry diary[MAX_DIARY];  int diary_count;
    /* event queue */
    SchedEv evq[MAX_EVQ]; int ev_count;
    /* historical */
    int hist_fired;
    /* HQ */
    int dispatch_pending;
    int dispatch_done,dispatch_comply;
    int convoy_delayed;
    int hq_rep;             /* 0-100 — affects convoy frequency and quality */
    int supply_req_pending; /* turns until response, -1=none */
    /* resource history [RES_HIST_LEN][4] */
    int res_hist[RES_HIST_LEN][RES_COUNT];
    int res_hist_count;
    /* UI */
    int sel,orders_mode,osel;
    /* command & upgrades */
    int cmd_points; int upgrades;
    /* campaign */
    Difficulty difficulty;
    int score,medals;
    int forced_standby_turns;
    OverState over;
}GameState;

static GameState G;

/* ================================================================
   SAVE SYSTEM — 3 slots, v5
   ================================================================ */

#define SAVE_MAGIC   0xB0C1917
#define SAVE_VERSION 5
#define SAVE_SLOTS   3
static const char *SAVE_NAMES[SAVE_SLOTS]={"boc_s0.bin","boc_s1.bin","boc_s2.bin"};

typedef struct{int magic,version,used,turn,week,men_alive,overall_morale;
               Difficulty diff;OverState over;char slot_label[64];}SaveMeta;
typedef struct{SaveMeta meta;GameState gs;}SaveFile;

static int save_meta_read(int slot,SaveMeta *m){
    FILE *f=fopen(SAVE_NAMES[slot],"rb"); if(!f) return 0;
    int ok=(fread(m,sizeof(*m),1,f)==1&&m->magic==SAVE_MAGIC&&m->version==SAVE_VERSION);
    fclose(f); return ok&&m->used;
}
static int save_slot(int slot){
    static const char *mns[]={"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
    static const int md[]={31,28,31,30,31,30,31,31,30,31,30,31};
    int days=(G.turn-1)/2,mo=0,d=days;
    while(mo<11&&d>=md[mo]){d-=md[mo];mo++;}
    char dstr[24]; snprintf(dstr,sizeof(dstr),"%2d %.3s 1917",d+1,mns[mo]);
    SaveFile sf; memset(&sf,0,sizeof(sf));
    sf.meta.magic=SAVE_MAGIC; sf.meta.version=SAVE_VERSION; sf.meta.used=1;
    sf.meta.turn=G.turn; sf.meta.week=1+(G.turn-1)/14; sf.meta.diff=G.difficulty; sf.meta.over=G.over;
    sf.meta.men_alive=0; for(int i=0;i<G.squad_count;i++) sf.meta.men_alive+=G.squads[i].men;
    sf.meta.overall_morale=0; for(int i=0;i<G.squad_count;i++) sf.meta.overall_morale+=G.squads[i].mor;
    if(G.squad_count) sf.meta.overall_morale/=G.squad_count;
    snprintf(sf.meta.slot_label,sizeof(sf.meta.slot_label),
             "Wk %d  |  %s  |  %d men  |  %s",
             sf.meta.week,dstr,sf.meta.men_alive,DIFF_DEFS[G.difficulty].name);
    sf.gs=G;
    FILE *f=fopen(SAVE_NAMES[slot],"wb"); if(!f) return 0;
    int ok=(fwrite(&sf,sizeof(sf),1,f)==1); fclose(f); return ok;
}
static int load_slot(int slot){
    SaveFile sf; FILE *f=fopen(SAVE_NAMES[slot],"rb"); if(!f) return 0;
    int ok=(fread(&sf,sizeof(sf),1,f)==1); fclose(f);
    if(!ok||sf.meta.magic!=SAVE_MAGIC||sf.meta.version!=SAVE_VERSION||!sf.meta.used) return 0;
    G=sf.gs; return 1;
}

/* ================================================================
   HELPERS
   ================================================================ */

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
static int total_wounds(void){
    int s=0; for(int i=0;i<G.squad_count;i++) s+=G.squads[i].wounds;
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
static int food_cap(void){return upg_has(UPG_FOOD_CACHE)?125:100;}
static int ammo_cap(void){return upg_has(UPG_MUNITIONS)?125:100;}
static int *res_ptr(ResId id){
    switch(id){case RES_FOOD:return&G.food;case RES_AMMO:return&G.ammo;
               case RES_MEDS:return&G.meds;default:return&G.tools;}
}
static int res_cap(ResId id){
    if(id==RES_FOOD) return food_cap();
    if(id==RES_AMMO) return ammo_cap();
    if(id==RES_MEDS) return 50;
    return 50;
}
/* find a notable by trait in a squad */
static Notable *find_notable(Squad *sq,SoldierTrait t){
    for(int i=0;i<sq->notable_count;i++)
        if(sq->notables[i].trait==t&&sq->notables[i].alive) return &sq->notables[i];
    return NULL;
}

static int calc_score(void){
    float mul=(float)DIFF_DEFS[G.difficulty].score_mul_x10/10.0f;
    int s=G.turn*5+total_men()*20+overall_mor()*3+G.medals*50+(G.hq_rep/10)*30;
    /* bonus for built upgrades */
    s+=popcount(G.upgrades)*25;
    if(G.over==OVER_WIN) s+=500;
    if(G.over==OVER_MUTINY) s-=200;
    return (int)(s*mul)>0?(int)(s*mul):0;
}
static const char *score_grade(int sc){
    if(sc>=1400) return "S+";
    if(sc>=1000) return "S ";
    if(sc>=700)  return "A ";
    if(sc>=500)  return "B ";
    if(sc>=300)  return "C ";
    if(sc>=120)  return "D ";
    return "F ";
}

/* ================================================================
   INITIALISATION
   ================================================================ */

static void new_game(Difficulty diff){
    memset(&G,0,sizeof(G));
    G.difficulty=diff;
    G.turn=1; G.maxt=84; G.half_am=1;
    G.weather=WEATHER_CLEAR; G.agg=40;
    G.food=80; G.ammo=60; G.meds=30; G.tools=40;
    G.ration_level=RATION_FULL; G.ammo_policy=AMMO_NORMAL;
    G.cmd_points=2; G.dispatch_pending=-1;
    G.hq_rep=50; G.supply_req_pending=-1;
    for(int i=0;i<4;i++) G.sector_threat[i]=rng_range(20,50);

    G.squad_count=SQUAD_INIT_COUNT;
    for(int i=0;i<SQUAD_INIT_COUNT;i++){
        const SquadInitDef *d=&SQUAD_INIT[i];
        Squad *s=&G.squads[i];
        strncpy(s->name,d->name,sizeof(s->name)-1);
        s->men=d->men; s->maxm=d->maxm; s->mor=d->mor; s->fat=d->fat;
        s->task=TASK_STANDBY; s->has_sgt=1;
        strncpy(s->sgt.name,d->sgt_name,sizeof(s->sgt.name)-1);
        s->sgt.pers=d->sgt_pers; s->sgt.ok=1; s->lore_idx=4+i;
        /* notable soldiers */
        s->notable_count=2;
        for(int j=0;j<2;j++){
            const NotableDef *nd=&NOTABLE_INIT[i][j];
            char fullname[24];
            snprintf(fullname,sizeof(fullname),"%s %s",nd->forename,nd->surname);
            strncpy(s->notables[j].name,fullname,31); s->notables[j].name[31]='\0';
            s->notables[j].trait=nd->trait; s->notables[j].alive=1;
        }
    }
    for(int i=0;i<INIT_MSG_COUNT;i++) log_msg(INIT_MSGS[i]);
    G.ev_count=1;
    G.evq[0]=(SchedEv){.at=3,.type=EV_SUPPLY,.food=25,.ammo=20,.meds=8,.tools=5};
}

/* ================================================================
   RENDER — primitives
   ================================================================ */

static void vbar(void){fg(BLU);attr_bold();fputs(BOX_V,stdout);attr_rst();}

static void hline(int r,const char *kind){
    at(r,1);fg(BLU);attr_bold();
    int sp=!strcmp(kind,"split"),jo=!strcmp(kind,"join"),cl=!strcmp(kind,"close-r");
    int hm=sp||jo||cl;
    const char *lc,*rc,*mc="";
    if(!strcmp(kind,"top")){lc=BOX_TL;rc=BOX_TR;}
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
    ResRow rr[]={{"Food:",&G.food,food_cap(),GRN,0},{"Ammo:",&G.ammo,ammo_cap(),YEL,1},
                 {"Meds:",&G.meds,50,CYN,2},{"Tools:",&G.tools,50,MAG,3}};
    for(int i=0;i<4;i++){
        char b[14]; make_bar(b,*rr[i].v,rr[i].mx,10);
        at(4+i,1);vbar();at(4+i,2);
        attr_bold();fg(WHT);printf(" %-6s",rr[i].lb);attr_rst();
        fg(rr[i].bc);printf("[%s]",b);attr_rst();
        printf(" %3d/%3d",*rr[i].v,rr[i].mx);
        /* trend arrow from history */
        if(G.res_hist_count>=2){
            int cur=G.res_hist[(G.res_hist_count-1)%RES_HIST_LEN][i];
            int prev=G.res_hist[(G.res_hist_count-2)%RES_HIST_LEN][i];
            if(cur>prev+2){fg(GRN);fputs(SYM_UP,stdout);}
            else if(cur<prev-2){fg(RED);fputs(SYM_DN,stdout);}
            else{fg(GRY);fputs(SYM_EQ,stdout);}
            attr_rst();
        } else { putchar(' '); }
        for(int j=26;j<LW;j++) putchar(' ');
        at(4+i,DIV);vbar();at(4+i,DIV+1);
        int mi=G.msg_count-1-rr[i].mi;
        if(mi>=0&&mi<G.msg_count&&G.msgs[mi][0]){
            fg(CYN);fputs(" \xe2\x80\xba ",stdout);attr_rst();
            fg(WHT);ppad(G.msgs[mi],RW-3);attr_rst();
        } else for(int j=0;j<RW;j++) putchar(' ');
        at(4+i,TW);vbar();
    }
    /* Row 8: morale + policy badges + msg 4 */
    int m=overall_mor(); char b[10]; make_bar(b,m,100,8);
    at(8,1);vbar();at(8,2);
    attr_bold();fg(WHT);fputs(" Moral",stdout);attr_rst();
    fg(mor_color(m));printf("[%s]%3d%%",b,m);attr_rst();
    putchar(' ');fg(RATION_DEFS[G.ration_level].color);
    printf("R:%s",RATION_DEFS[G.ration_level].name);attr_rst();
    putchar(' ');fg(AMMO_DEFS[G.ammo_policy].color);
    printf("A:%s",AMMO_DEFS[G.ammo_policy].name);attr_rst();
    /* wounds indicator */
    int tw=total_wounds();
    if(tw>0){putchar(' ');fg(RED);attr_bold();printf("%s%dW",SYM_WN,tw);attr_rst();}
    for(int i=39;i<LW;i++) putchar(' ');
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
    /* sector threat row */
    char sec_row[RW+2]={0};
    snprintf(sec_row,sizeof(sec_row)," A:%-3d B:%-3d C:%-3d D:%-3d",
             G.sector_threat[0],G.sector_threat[1],G.sector_threat[2],G.sector_threat[3]);
    /* upgrades row */
    char upln[RW+2]=" Upg:";
    static const char *ushort[UPG_COUNT]={"DB","SB","DG","PR","LG","SU","SW","RS","FH","OP","MN","FC"};
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
        sec_row,
        "    " BOX_TL BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_TR "                 ",
        "    " BOX_V "   H.Q.   " BOX_V "  ~~No Man's~~  ",
        "    " BOX_BL BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_BR "  ~~  Land   ~~  ",
        G.upgrades ? upln : "                                   ",
        aln0, aln1,
    };

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
            if(sel){attr_bold();}fg(sel?YEL:WHT);
            char nm[8]; snprintf(nm,8,"%-7s",sq->name); fputs(nm,stdout);attr_rst();
            fg(GRY);printf("%d/%d",sq->men,sq->maxm);attr_rst();
            /* wounds indicator */
            if(sq->wounds>0){fg(RED);printf("+%dW",sq->wounds);attr_rst();}
            else putchar(' ');
            putchar(' ');
            fg(mor_color(sq->mor));printf("[%s]",mb);attr_rst();putchar(' ');
            fg(TASK_DEFS[sq->task].color);attr_bold();
            char ts[8]; snprintf(ts,8,"%-7s",TASK_DEFS[sq->task].name); fputs(ts,stdout);attr_rst();
            for(int i=33;i<LW;i++) putchar(' ');
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
                attr_dim();fg(GRY);ppad(sl,30);attr_rst();
                fputs("Fat:",stdout);fg(fc);printf("%2d%%",sq->fat);attr_rst();
                /* notable alive count */
                int na=0; for(int j=0;j<sq->notable_count;j++) if(sq->notables[j].alive) na++;
                fg(GRY);printf(" N:%d",na);attr_rst();
                for(int i=41;i<LW;i++) putchar(' ');
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
    fg(GRY);fputs("  " BOX_VT " CP:",stdout);attr_rst();
    for(int i=0;i<cp_max();i++){
        if(i<G.cmd_points){fg(YEL);attr_bold();fputs(SYM_BL,stdout);attr_rst();}
        else{fg(GRY);attr_dim();fputs(SYM_CI,stdout);attr_rst();}
    }
    fg(GRY);printf(" Rep:%d",G.hq_rep);attr_rst();
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
        ?" [" SYM_LF SYM_RT "] Cycle  [ENTER] Confirm  [ESC] Cancel"
        :" [SPC] EndTurn  [O] Orders  [C] Cmd  [R] Resources  [I] Intel  [D] Dossier  [ESC] Menu";
    ppad(ctrl,TW-2);attr_rst();
    at(22,TW);vbar();fflush(stdout);
    hline(23,"bot");
    at(24,1);fflush(stdout);
}

/* ================================================================
   SCREEN: RESOURCE MANAGEMENT
   ================================================================ */

static void screen_resources(void){
    int tab=0; /* 0=overview, 1=barter, 2=policy */
    int bsel=0;
    for(;;){
        cls();
        draw_box(1,2,23,79);
        at(1,4);fg(YEL);attr_bold();
        char dstr[24]; dat_str(dstr);
        printf(" RESOURCE MANAGEMENT  " BOX_VT "  %s  " BOX_VT "  HQ Rep: %d",dstr,G.hq_rep);
        attr_rst();

        /* Tab bar */
        at(2,4);
        if(tab==0){fg(YEL);attr_bold();}else{fg(GRY);}
        fputs(" [1] Overview ",stdout);attr_rst();
        if(tab==1){fg(YEL);attr_bold();}else{fg(GRY);}
        fputs(" [2] Barter ",stdout);attr_rst();
        if(tab==2){fg(YEL);attr_bold();}else{fg(GRY);}
        fputs(" [3] Policies ",stdout);attr_rst();
        draw_hline_mid(3,2,79);

        if(tab==0){
            /* ── OVERVIEW tab ── */
            /* Stock bars */
            at(4,4);fg(WHT);attr_bold();fputs(" CURRENT STOCK",stdout);attr_rst();
            at(5,4);fg(GRY);for(int i=0;i<74;i++) putchar('-');attr_rst();

            static const ResId rids[RES_COUNT]={RES_FOOD,RES_AMMO,RES_MEDS,RES_TOOLS};
            for(int i=0;i<RES_COUNT;i++){
                ResId rid=rids[i];
                int v=*res_ptr(rid), mx=res_cap(rid);
                char bar[20]; make_bar(bar,v,mx,20);
                at(6+i,4);fg(RES_COLORS[rid]);attr_bold();printf(" %-7s",RES_NAMES[rid]);attr_rst();
                fg(RES_COLORS[rid]);printf("[%s]",bar);attr_rst();
                printf(" %3d/%-3d",v,mx);
                /* status */
                float pct=(float)v/mx;
                at(6+i,42);
                if(pct<0.15f){fg(RED);attr_bold();fputs(" CRITICAL",stdout);}
                else if(pct<0.30f){fg(RED);fputs(" LOW",stdout);}
                else if(pct<0.60f){fg(YEL);fputs(" FAIR",stdout);}
                else{fg(GRN);fputs(" GOOD",stdout);}
                attr_rst();
            }

            /* Resource history sparkline */
            at(11,4);fg(WHT);attr_bold();fputs(" TREND  (last 10 turns)",stdout);attr_rst();
            at(12,4);fg(GRY);for(int i=0;i<74;i++) putchar('-');attr_rst();
            static const char *spark_chars="▁▂▃▄▅▆▇█";
            static const int sc_len=8;
            for(int ri=0;ri<RES_COUNT;ri++){
                int mx=res_cap((ResId)ri);
                at(13+ri,4);fg(RES_COLORS[ri]);printf(" %-7s",RES_NAMES[ri]);attr_rst();
                int n=G.res_hist_count<RES_HIST_LEN?G.res_hist_count:RES_HIST_LEN;
                for(int j=0;j<RES_HIST_LEN;j++){
                    int hi=(G.res_hist_count-RES_HIST_LEN+j);
                    if(hi<0||j>=n){putchar(' ');continue;}
                    int v=G.res_hist[hi%RES_HIST_LEN][ri];
                    int idx=(int)((float)v/mx*(sc_len-1));
                    idx=clamp(idx,0,sc_len-1);
                    /* print 3-byte UTF-8 spark char */
                    const char *sc=spark_chars+idx*3;
                    putchar(sc[0]);putchar(sc[1]);putchar(sc[2]);
                }
                /* current arrow trend */
                if(n>=2){
                    int cur=G.res_hist[(G.res_hist_count-1)%RES_HIST_LEN][ri];
                    int prv=G.res_hist[(G.res_hist_count-2)%RES_HIST_LEN][ri];
                    if(cur>prv+2){fg(GRN);fputs(" " SYM_UP,stdout);}
                    else if(cur<prv-2){fg(RED);fputs(" " SYM_DN,stdout);}
                    else{fg(GRY);fputs(" " SYM_EQ,stdout);}
                    attr_rst();
                }
            }

            /* Wound summary */
            at(18,4);fg(WHT);attr_bold();fputs(" WOUNDED PERSONNEL",stdout);attr_rst();
            at(19,4);fg(GRY);for(int i=0;i<74;i++) putchar('-');attr_rst();
            at(20,4);
            int any_wounds=0;
            for(int i=0;i<G.squad_count;i++){
                if(G.squads[i].wounds>0){
                    fg(RED);printf(" %s:%dW",G.squads[i].name,G.squads[i].wounds);attr_rst();
                    any_wounds=1;
                }
            }
            if(!any_wounds){fg(GRN);fputs(" No walking wounded.",stdout);attr_rst();}

            /* Sector threat row */
            at(21,4);fg(WHT);attr_bold();fputs(" SECTOR THREATS",stdout);attr_rst();
            at(22,4);
            const char *snames[]={"A","B","C","D"};
            for(int i=0;i<4;i++){
                int t=G.sector_threat[i];
                int col=t>=70?RED:t>=40?YEL:GRN;
                fg(col);printf("  [%s] %3d%%",snames[i],t);attr_rst();
            }

        } else if(tab==1){
            /* ── BARTER tab ── */
            at(4,4);fg(WHT);attr_bold();fputs(" RESOURCE EXCHANGE  (barter with field traders and salvage teams)",stdout);attr_rst();
            at(5,4);fg(GRY);for(int i=0;i<74;i++) putchar('-');attr_rst();
            for(int i=0;i<BARTER_COUNT;i++){
                const BarterRate *br=&BARTER_RATES[i];
                int can=(*res_ptr(br->from)>=br->give);
                int row=6+i;
                if(row>21) break;
                at(row,4);
                if(i==bsel&&can){fg(YEL);attr_bold();fputs("\xe2\x96\xb6 ",stdout);}
                else if(i==bsel){fg(GRY);fputs("\xe2\x96\xb6 ",stdout);}
                else fputs("  ",stdout);
                if(!can){attr_dim();fg(GRY);}
                else fg(RES_COLORS[br->from]);
                printf("%-6s",RES_NAMES[br->from]);attr_rst();
                fg(GRY);printf(" -%2d ",br->give);attr_rst();
                fputs(SYM_RT,stdout);
                fg(RES_COLORS[br->to]);printf(" %-6s",RES_NAMES[br->to]);attr_rst();
                fg(GRY);printf(" +%2d  ",br->get);attr_rst();
                if(i==bsel){fg(WHT);attr_dim();ppad(br->desc,40);attr_rst();}
            }
        } else {
            /* ── POLICY tab ── */
            at(4,4);fg(WHT);attr_bold();fputs(" RATION POLICY",stdout);attr_rst();
            at(5,4);fg(GRY);for(int i=0;i<74;i++) putchar('-');attr_rst();
            for(int i=0;i<RATION_COUNT;i++){
                int sel=(G.ration_level==(RationLevel)i);
                at(6+i,4);
                if(sel){fg(RATION_DEFS[i].color);attr_bold();fputs("\xe2\x96\xb6 ",stdout);}
                else {fg(GRY);fputs("  ",stdout);}
                fg(RATION_DEFS[i].color);printf("%-12s",RATION_DEFS[i].name);attr_rst();
                fg(sel?WHT:GRY);printf(" Food×%.2f  MorDelta: %+d/turn  ",RATION_DEFS[i].food_mul,RATION_DEFS[i].mor_per_turn);
                attr_dim();fputs(RATION_DEFS[i].desc,stdout);
                attr_rst();
            }
            at(11,4);fg(WHT);attr_bold();fputs(" AMMO POLICY",stdout);attr_rst();
            at(12,4);fg(GRY);for(int i=0;i<74;i++) putchar('-');attr_rst();
            for(int i=0;i<AMMO_COUNT;i++){
                int sel=(G.ammo_policy==(AmmoPolicy)i);
                at(13+i,4);
                if(sel){fg(AMMO_DEFS[i].color);attr_bold();fputs("\xe2\x96\xb6 ",stdout);}
                else {fg(GRY);fputs("  ",stdout);}
                fg(AMMO_DEFS[i].color);printf("%-10s",AMMO_DEFS[i].name);attr_rst();
                fg(sel?WHT:GRY);printf(" Ammo×%.2f  Patrol×%.2f  Raid+%.0f%%  ",
                    AMMO_DEFS[i].ammo_mul,AMMO_DEFS[i].patrol_mor_mul,AMMO_DEFS[i].raid_resist_add*100);
                attr_dim();ppad(AMMO_DEFS[i].desc,28);attr_rst();
            }
            at(17,4);fg(WHT);attr_bold();fputs(" WOUND TREATMENT PROTOCOL",stdout);attr_rst();
            at(18,4);fg(GRY);for(int i=0;i<74;i++) putchar('-');attr_rst();
            at(19,4);fg(WHT);fputs(" Walking wounded consume 1 med/turn passively.",stdout);attr_rst();
            at(20,4);fg(WHT);fputs(" Untreated wounds after 5 turns become fatal (-1 man).",stdout);attr_rst();
            at(21,4);fg(GRN);if(upg_has(UPG_FIELD_HOSP))fputs(" Field Hospital: heals 1 extra wound/turn automatically.",stdout);
            else{fg(GRY);fputs(" Field Hospital not built (use Trench Upgrades to build).",stdout);}
            attr_rst();
        }

        draw_hline_mid(22,2,79);
        if(tab==1){
            at(22,5);fg(YEL);fputs("[" SYM_UP SYM_DN "] Select  [ENTER] Execute  [1/2/3] Tabs  [ESC] Back",stdout);attr_rst();
        } else if(tab==2){
            at(22,5);fg(YEL);fputs("[" SYM_UP SYM_DN "] Change ration/ammo policy  [1/2/3] Tabs  [ESC] Back",stdout);attr_rst();
        } else {
            at(22,5);fg(YEL);fputs("[1/2/3] Tabs  [ESC] Back",stdout);attr_rst();
        }
        fflush(stdout);

        Key k=getch();
        if(k==KEY_ESC||k==KEY_Q) break;
        if(k==KEY_1) tab=0;
        if(k==KEY_2) tab=1;
        if(k==KEY_3) tab=2;
        if(tab==1){
            if(k==KEY_UP)   bsel=(bsel+BARTER_COUNT-1)%BARTER_COUNT;
            if(k==KEY_DOWN) bsel=(bsel+1)%BARTER_COUNT;
            if(k==KEY_ENTER){
                const BarterRate *br=&BARTER_RATES[bsel];
                int *fv=res_ptr(br->from), *tv=res_ptr(br->to);
                if(*fv>=br->give){
                    *fv-=br->give;
                    *tv=clamp(*tv+br->get,0,res_cap(br->to));
                    char msg[MSG_LEN];
                    snprintf(msg,MSG_LEN,"Barter: -%d %s +%d %s.",br->give,RES_NAMES[br->from],br->get,RES_NAMES[br->to]);
                    log_msg(msg);
                }
            }
        } else if(tab==2){
            if(k==KEY_UP)  G.ration_level=(RationLevel)clamp((int)G.ration_level+1,0,RATION_COUNT-1);
            if(k==KEY_DOWN)G.ration_level=(RationLevel)clamp((int)G.ration_level-1,0,RATION_COUNT-1);
            if(k==KEY_LEFT) G.ammo_policy=(AmmoPolicy)clamp((int)G.ammo_policy-1,0,AMMO_COUNT-1);
            if(k==KEY_RIGHT)G.ammo_policy=(AmmoPolicy)clamp((int)G.ammo_policy+1,0,AMMO_COUNT-1);
        }
    }
}

/* ================================================================
   SCREEN: COMMAND ACTIONS
   ================================================================ */

static void screen_command(void){
    int sel=0;
    for(;;){
        cls(); draw_box(2,5,22,75);
        at(2,7);fg(YEL);attr_bold();
        printf(" COMMAND ACTIONS  " BOX_VT "  Capt. Thorne  " BOX_VT "  CP: %d/%d  " BOX_VT "  Rep: %d",G.cmd_points,cp_max(),G.hq_rep);
        attr_rst(); draw_hline_mid(3,5,75);
        for(int i=0;i<CMD_COUNT;i++){
            const CmdActionDef *cd=&CMD_DEFS[i];
            int rum_free=(i==CMD_RUM&&upg_has(UPG_RUM_STORE));
            int can=(G.cmd_points>=(rum_free?0:cd->cp_cost)&&G.food>=cd->food_cost&&G.meds>=cd->meds_cost);
            /* special: treat wounded needs wounds */
            if(i==CMD_TREAT_WOUNDED){
                int w=G.sel<G.squad_count?G.squads[G.sel].wounds:0;
                can=can&&w>0;
            }
            /* field hospital halves meds cost on CMD_MEDICAL */
            int mc=cd->meds_cost;
            if(i==CMD_MEDICAL&&upg_has(UPG_FIELD_HOSP)) mc=(mc+1)/2;
            can=(G.cmd_points>=(rum_free?0:cd->cp_cost)&&G.food>=cd->food_cost&&G.meds>=mc);
            if(i==CMD_TREAT_WOUNDED&&(G.sel>=G.squad_count||G.squads[G.sel].wounds==0)) can=0;
            int row=4+i*2; int s=(i==sel);
            at(row,8);
            if(!can){attr_dim();fg(GRY);}else if(s){fg(YEL);attr_bold();}else fg(WHT);
            printf("%s %-22s",s?"\xe2\x96\xb6 ":"  ",cd->name);attr_rst();
            if(cd->cp_cost>0&&!rum_free){fg(YEL);printf("[%dCP]",cd->cp_cost);attr_rst();}
            if(cd->food_cost>0){fg(GRN);printf("[-%dF]",cd->food_cost);attr_rst();}
            if(mc>0){fg(CYN);printf("[-%dM]",mc);attr_rst();}
            if(s){at(row+1,10);attr_dim();fg(GRY);fputs(cd->effect,stdout);attr_rst();}
        }
        draw_hline_mid(4+CMD_COUNT*2-1,5,75);
        Squad *sq=G.sel<G.squad_count?&G.squads[G.sel]:NULL;
        at(4+CMD_COUNT*2,8);fg(YEL);
        printf("[" SYM_UP SYM_DN "] Select  [ENTER] Execute on %s  [ESC] Back",
               sq?sq->name:"???");attr_rst();fflush(stdout);

        Key k=getch();
        if(k==KEY_ESC||k==KEY_Q) break;
        if(k==KEY_UP)   sel=(sel+CMD_COUNT-1)%CMD_COUNT;
        if(k==KEY_DOWN) sel=(sel+1)%CMD_COUNT;
        if(k==KEY_ENTER&&sq){
            const CmdActionDef *cd=&CMD_DEFS[sel];
            int rum_free=(sel==CMD_RUM&&upg_has(UPG_RUM_STORE));
            int mc=cd->meds_cost; if(sel==CMD_MEDICAL&&upg_has(UPG_FIELD_HOSP)) mc=(mc+1)/2;
            int can=(G.cmd_points>=(rum_free?0:cd->cp_cost)&&G.food>=cd->food_cost&&G.meds>=mc);
            if(sel==CMD_TREAT_WOUNDED&&(!sq||sq->wounds==0)) can=0;
            if(!can) continue;
            if(!rum_free) G.cmd_points=clamp(G.cmd_points-cd->cp_cost,0,cp_max());
            G.food-=cd->food_cost; G.meds-=mc;
            char msg[MSG_LEN];
            switch(sel){
            case CMD_RUM:            sq->mor=clamp(sq->mor+15,0,100); snprintf(msg,MSG_LEN,"Rum ration: %s Sq +15 morale.",sq->name); break;
            case CMD_LETTERS:        sq->mor=clamp(sq->mor+10,0,100); snprintf(msg,MSG_LEN,"Letters home: %s Sq +10 morale.",sq->name); break;
            case CMD_MEDICAL:        sq->fat=clamp(sq->fat-25,0,100); snprintf(msg,MSG_LEN,"Triage: %s Sq fatigue reduced.",sq->name); break;
            case CMD_REPRIMAND:      sq->fat=clamp(sq->fat-8,0,100);sq->mor=clamp(sq->mor-5,0,100); snprintf(msg,MSG_LEN,"Inspection: %s Sq disciplined.",sq->name); break;
            case CMD_SPEECH:         for(int i=0;i<G.squad_count;i++) G.squads[i].mor=clamp(G.squads[i].mor+8,0,100); snprintf(msg,MSG_LEN,"Officer's speech: company morale rises."); break;
            case CMD_RATIONS_EXTRA:  for(int i=0;i<G.squad_count;i++) G.squads[i].fat=clamp(G.squads[i].fat-15,0,100); snprintf(msg,MSG_LEN,"Emergency rations: all fatigue reduced."); break;
            case CMD_CEREMONY:       sq->mor=clamp(sq->mor+20,0,100);G.medals++; snprintf(msg,MSG_LEN,"Medal ceremony: %s Sq. +1 commendation.",sq->name); break;
            case CMD_LEAVE:          if(sq->men>1){sq->men--;sq->men_lost++;} sq->mor=clamp(sq->mor+15,0,100); snprintf(msg,MSG_LEN,"Comp. leave: %s Sq -1 man +15 morale.",sq->name); break;
            case CMD_TREAT_WOUNDED:  {int h=clamp(sq->wounds,0,2);sq->wounds=clamp(sq->wounds-h,0,sq->men); snprintf(msg,MSG_LEN,"Treatment: %d wound(s) treated in %s Sq.",h,sq->name);} break;
            case CMD_SUPPLY_REQUEST:
                if(G.supply_req_pending<0){
                    int eta=G.turn+rng_range(5,10);
                    int qual=(G.hq_rep>=70)?1:0;
                    SchedEv ev={.at=eta,.type=EV_SUPPLY,
                        .food=rng_range(qual?20:10,qual?35:20),
                        .ammo=rng_range(qual?15:8,qual?25:15),
                        .meds=rng_range(qual?6:3,qual?12:8),
                        .tools=rng_range(qual?4:2,qual?8:5)};
                    if(G.ev_count<MAX_EVQ) G.evq[G.ev_count++]=ev;
                    G.supply_req_pending=eta;
                    snprintf(msg,MSG_LEN,"Supply request filed. ETA %d turns. Rep affects quality.",eta-G.turn);
                } else { snprintf(msg,MSG_LEN,"Supply request already pending."); }
                break;
            default: snprintf(msg,MSG_LEN,"Action complete."); break;
            }
            log_msg(msg);
        }
    }
}

/* ================================================================
   SCREEN: TRENCH UPGRADES
   ================================================================ */

static void screen_upgrades(void){
    int sel=0;
    for(;;){
        cls(); draw_box(2,4,22,77);
        at(2,6);fg(MAG);attr_bold();
        printf(" TRENCH ENGINEERING  " BOX_VT "  Tools: %d",G.tools);attr_rst();
        draw_hline_mid(3,4,77);
        for(int i=0;i<UPG_COUNT;i++){
            const UpgradeDef *ud=&UPG_DEFS[i];
            int owned=upg_has((UpgradeId)i),can=(!owned&&G.tools>=ud->tools_cost);
            int row=4+i*2;
            if(row>20) break;
            at(row,7);
            if(owned){fg(GRN);attr_bold();printf("  [" SYM_CK "] %-22s",ud->name);}
            else if(i==sel&&can){fg(YEL);attr_bold();printf("\xe2\x96\xb6 [%2d" SYM_HT "] %-22s",ud->tools_cost,ud->name);}
            else if(i==sel){fg(RED);printf("\xe2\x96\xb6 [%2d" SYM_HT "] %-22s",ud->tools_cost,ud->name);}
            else{fg(GRY);attr_dim();printf("  [%2d" SYM_HT "] %-22s",ud->tools_cost,ud->name);}
            attr_rst();
            if(i==sel||owned){fg(i==sel?WHT:GRN);attr_dim();fputs(ud->passive,stdout);attr_rst();}
        }
        draw_hline_mid(20,4,77);
        at(21,7);fg(YEL);fputs("[" SYM_UP SYM_DN "] Select  [ENTER] Build  [ESC] Back",stdout);attr_rst();fflush(stdout);
        Key k=getch(); if(k==KEY_ESC||k==KEY_Q) break;
        if(k==KEY_UP) sel=(sel+UPG_COUNT-1)%UPG_COUNT;
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

/* ================================================================
   SCREEN: HQ DISPATCH MODAL
   ================================================================ */

static int screen_hq_dispatch(int idx){
    const HqDispatch *d=&HQ_DISPATCHES[idx]; int sel=0;
    for(;;){
        cls(); draw_box(2,4,22,77);
        at(2,6);fg(RED);attr_bold();printf(" " SYM_WN "  %s  " SYM_WN " ",d->title);attr_rst();
        draw_hline_mid(3,4,77);
        int r=5; for(int i=0;d->body[i]&&r<=13;i++,r++){at(r,7);fg(WHT);fputs(d->body[i],stdout);attr_rst();}
        draw_hline_mid(14,4,77);
        at(15,7);if(sel==0){fg(GRN);attr_bold();printf("  \xe2\x96\xb6 %s",d->comply_label);}else{fg(GRY);printf("    %s",d->comply_label);}attr_rst();
        at(16,9);attr_dim();fg(GRY);fputs(d->comply_result,stdout);attr_rst();
        at(17,7);if(sel==1){fg(RED);attr_bold();printf("  \xe2\x96\xb6 %s",d->defy_label);}else{fg(GRY);printf("    %s",d->defy_label);}attr_rst();
        at(18,9);attr_dim();fg(GRY);fputs(d->defy_result,stdout);attr_rst();
        at(19,7);fg(YEL);
        printf("Rep effect: Comply %+d  /  Defy %+d",d->cy_rep_delta,d->df_rep_delta);attr_rst();
        draw_hline_mid(20,4,77);
        at(21,7);fg(YEL);fputs("[" SYM_UP SYM_DN "] Choose  [ENTER] Execute",stdout);attr_rst();fflush(stdout);
        Key k=getch();
        if(k==KEY_UP||k==KEY_DOWN) sel=1-sel;
        if(k==KEY_ENTER) return 1-sel;
    }
}

/* ================================================================
   SCREEN: INTEL
   ================================================================ */

static void screen_intel(void){
    cls(); draw_box(1,2,24,79);
    char dstr[24]; dat_str(dstr);
    at(1,4);fg(CYN);attr_bold();printf(" INTEL REPORT  " BOX_VT "  %s  " BOX_VT "  T:%d  " BOX_VT "  Rep:%d",dstr,G.turn,G.hq_rep);attr_rst();
    for(int r=2;r<=23;r++){at(r,40);fg(BLU);attr_bold();fputs(BOX_V,stdout);attr_rst();}
    /* left panel dividers */
    at(2,3);draw_hline_mid(2,2,79);
    at(3,3);fg(WHT);attr_bold();fputs(" QUEUE & DISPATCHES",stdout);attr_rst();
    at(4,3);fg(GRY);for(int i=0;i<37;i++) putchar('-');attr_rst();
    int lr=5;
    for(int i=0;i<G.ev_count&&lr<=10;i++){
        SchedEv *e=&G.evq[i]; int eta=e->at-G.turn;
        at(lr++,4);
        if(e->type==EV_SUPPLY){fg(GRN);printf(" Supply: T%-3d  (in %d turn%s)",e->at,eta,eta==1?"":"s");}
        else if(e->type==EV_REINFORCE){fg(CYN);printf(" Reinforce: T%-3d  (in %d turn%s)",e->at,eta,eta==1?"":"s");}
        attr_rst();
    }
    if(G.supply_req_pending>0){
        at(lr++,4);fg(YEL);printf(" HQ request: T%-3d",G.supply_req_pending);attr_rst();}
    at(11,3);fg(YEL);attr_bold();fputs(" UPCOMING HQ ORDERS",stdout);attr_rst();
    at(12,3);fg(GRY);for(int i=0;i<37;i++) putchar('-');attr_rst();
    int dr=13;
    for(int i=0;i<HQ_DISPATCH_COUNT&&dr<=17;i++){
        if(G.dispatch_done&(1<<i)) continue;
        int eta=HQ_DISPATCHES[i].turn-G.turn; if(eta<0) continue;
        at(dr++,4);fg(YEL);printf(" T%-3d: %.32s",HQ_DISPATCHES[i].turn,HQ_DISPATCHES[i].title);attr_rst();
    }
    at(18,3);fg(MAG);attr_bold();fputs(" FIELD NOTES",stdout);attr_rst();
    at(19,3);fg(GRY);for(int i=0;i<37;i++) putchar('-');attr_rst();
    int hr=20;
    for(int i=0;i<HIST_EVENT_COUNT&&hr<=22;i++){
        if(G.hist_fired&(1<<i)) continue;
        int eta=HIST_EVENTS[i].turn-G.turn; if(eta<0||eta>10) continue;
        at(hr++,4);fg(GRY);attr_dim();printf(" T%-3d: %.34s",HIST_EVENTS[i].turn,HIST_EVENTS[i].text);attr_rst();
    }
    /* right panel */
    at(3,42);fg(WHT);attr_bold();fputs(" ENEMY & SECTOR STATUS",stdout);attr_rst();
    at(4,42);fg(GRY);for(int i=0;i<37;i++) putchar('-');attr_rst();
    char abar[14]; make_bar(abar,G.agg,100,14);
    at(5,42);fg(WHT);printf(" Aggression: ");fg(G.agg>70?RED:G.agg>40?YEL:GRN);
    printf("%3d%% [%s]",G.agg,abar);attr_rst();
    at(6,42);fg(WHT);printf(" Weather  : ");fg(WEATHER_DEFS[G.weather].color);fputs(WEATHER_DEFS[G.weather].label,stdout);attr_rst();
    at(7,42);fg(GRY);attr_dim();printf("  %s",WEATHER_FX[G.weather].note);attr_rst();
    at(8,42);fg(WHT);attr_bold();fputs(" Sector threats:",stdout);attr_rst();
    const char *snames[]={"A","B","C","D"};
    for(int i=0;i<4;i++){
        int t=G.sector_threat[i];
        int col=t>=70?RED:t>=40?YEL:GRN;
        char tb[8]; make_bar(tb,t,100,7);
        at(9+i,43);fg(col);printf(" [%s] %3d%% [%s]",snames[i],t,tb);attr_rst();
    }
    /* resource forecast */
    at(14,42);fg(WHT);attr_bold();fputs(" RESOURCE FORECAST",stdout);attr_rst();
    at(15,42);fg(GRY);for(int i=0;i<37;i++) putchar('-');attr_rst();
    float fm=(float)DIFF_DEFS[G.difficulty].food_mul*(float)RATION_DEFS[G.ration_level].food_mul;
    int fc=(int)(clamp(total_men()/6,1,99)*fm+0.5f)+WEATHER_FX[G.weather].food_extra;
    int ac=0; for(int i=0;i<G.squad_count;i++) ac+=(int)(TASK_DEFS[G.squads[i].task].ammo_cost*AMMO_DEFS[G.ammo_policy].ammo_mul+0.5f);
    int tg=0; for(int i=0;i<G.squad_count;i++) tg+=TASK_DEFS[G.squads[i].task].tools_gain;
    int fg_gain=0; for(int i=0;i<G.squad_count;i++) fg_gain+=TASK_DEFS[G.squads[i].task].food_gain;
    int ag_gain=0; for(int i=0;i<G.squad_count;i++) ag_gain+=TASK_DEFS[G.squads[i].task].ammo_gain;
    at(16,43);fg(GRN);printf(" Food  %3d → ~%3d (%+d)",G.food,clamp(G.food-fc+fg_gain,0,food_cap()),fg_gain-fc);attr_rst();
    at(17,43);fg(YEL);printf(" Ammo  %3d → ~%3d (%+d)",G.ammo,clamp(G.ammo-ac+ag_gain,0,ammo_cap()),ag_gain-ac);attr_rst();
    at(18,43);fg(CYN);printf(" Meds  %3d → ~%3d (%+d)",G.meds,clamp(G.meds-total_wounds(),0,50),-total_wounds());attr_rst();
    at(19,43);fg(MAG);printf(" Tools %3d → ~%3d (%+d)",G.tools,clamp(G.tools+tg,0,50),tg);attr_rst();
    /* upgrade status */
    at(21,42);fg(WHT);attr_bold();fputs(" TRENCH STATUS",stdout);attr_rst();
    at(22,42);
    int uc=0;
    for(int i=0;i<UPG_COUNT;i++){
        if(upg_has((UpgradeId)i)){fg(GRN);printf(" %s",UPG_DEFS[i].name);attr_rst();uc++;}
    }
    if(!uc){fg(GRY);fputs(" No upgrades built.",stdout);attr_rst();}
    at(24,4);fg(YEL);fputs("[ESC] Back to command",stdout);attr_rst();fflush(stdout);
    getch();
}

/* ================================================================
   SCREEN: SQUAD DOSSIER
   ================================================================ */

static void screen_dossier(int idx){
    if(idx<0||idx>=G.squad_count) return;
    Squad *sq=&G.squads[idx];
    for(;;){
        cls(); draw_box(1,2,23,79);
        at(1,4);fg(YEL);attr_bold();
        printf(" DOSSIER: %s Section  " BOX_VT "  %s  " BOX_VT "  Sgt: %s",
               sq->name,PERS_DEFS[sq->sgt.pers].name,sq->has_sgt?sq->sgt.name:"None");attr_rst();
        for(int r=2;r<=22;r++){at(r,40);fg(BLU);attr_bold();fputs(BOX_V,stdout);attr_rst();}
        draw_hline_mid(2,2,79);
        at(3,3);fg(WHT);attr_bold();fputs(" SQUAD STATUS",stdout);attr_rst();
        at(4,3);fg(GRY);for(int i=0;i<37;i++) putchar('-');attr_rst();
        char lb[14];
        make_bar(lb,sq->men,sq->maxm,12);
        at(5,3);fg(WHT);printf(" Strength : %d/%d [%s]",sq->men,sq->maxm,lb);attr_rst();
        make_bar(lb,sq->mor,100,12);
        at(6,3);fg(WHT);printf(" Morale   : %3d%% [%s] ",sq->mor,lb);fg(mor_color(sq->mor));fputs(mor_label(sq->mor),stdout);attr_rst();
        make_bar(lb,sq->fat,100,12);
        at(7,3);fg(WHT);printf(" Fatigue  : %3d%% [%s]",sq->fat,lb);attr_rst();
        at(8,3);fg(WHT);printf(" Wounds   : %d walking wounded",sq->wounds);if(sq->wounds>0){fg(RED);fputs(" (!)",stdout);}attr_rst();
        at(9,3);fg(WHT);printf(" Task     : ");fg(TASK_DEFS[sq->task].color);attr_bold();fputs(TASK_DEFS[sq->task].name,stdout);attr_rst();
        attr_dim();fg(GRY);printf("  %s",TASK_DEFS[sq->task].desc);attr_rst();
        at(10,3);fg(GRY);for(int i=0;i<37;i++) putchar('-');attr_rst();
        at(11,3);fg(WHT);attr_bold();fputs(" NOTABLE MEN",stdout);attr_rst();
        for(int j=0;j<sq->notable_count;j++){
            Notable *n=&sq->notables[j];
            at(12+j,3);
            if(!n->alive){fg(GRY);attr_dim();printf(" %s  [KIA]",n->name);attr_rst();}
            else{fg(TRAIT_DEFS[n->trait].color);printf(" %s",n->name);attr_rst();
                fg(GRY);attr_dim();printf("  [%s] %s",TRAIT_DEFS[n->trait].name,TRAIT_DEFS[n->trait].effect);attr_rst();}
        }
        at(14,3);fg(GRY);for(int i=0;i<37;i++) putchar('-');attr_rst();
        at(15,3);fg(WHT);attr_bold();fputs(" COMBAT RECORD",stdout);attr_rst();
        at(16,3);fg(GRY);printf(" Raids repelled: ");fg(GRN);printf("%d",sq->raids_repelled);attr_rst();
        at(17,3);fg(GRY);printf(" Men lost      : ");fg(RED);printf("%d",sq->men_lost);attr_rst();
        at(18,3);fg(GRY);printf(" Turns in line : ");fg(CYN);printf("%d",sq->turns_alive);attr_rst();
        at(19,3);fg(WHT);printf(" Sgt status: ");
        if(!sq->has_sgt){fg(RED);fputs("No sergeant.",stdout);}
        else if(!sq->sgt.ok){fg(RED);fputs("Shell shock — unfit.",stdout);}
        else{fg(GRN);printf("Active — %s",PERS_DEFS[sq->sgt.pers].effect_str);}attr_rst();
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
        draw_hline_mid(22,2,79);
        at(22,5);fg(YEL);printf("[1-4] Switch  [" SYM_LF SYM_RT "] Prev/Next  [ESC] Back");attr_rst();fflush(stdout);
        Key k=getch(); if(k==KEY_ESC||k==KEY_Q) break;
        if(k==KEY_LEFT&&idx>0) idx--;
        else if(k==KEY_RIGHT&&idx<G.squad_count-1) idx++;
        else if(k==KEY_1&&G.squad_count>=1) idx=0;
        else if(k==KEY_2&&G.squad_count>=2) idx=1;
        else if(k==KEY_3&&G.squad_count>=3) idx=2;
        else if(k==KEY_4&&G.squad_count>=4) idx=3;
        sq=&G.squads[idx];
    }
}

/* ================================================================
   SCREEN: HOW TO PLAY  — 10 paged sections, data-driven
   ================================================================ */

typedef struct {
    const char *title;
    int         title_color;
    const char *lines[20]; /* NULL-terminated; '~' = divider, '|' = table row */
} HtpPage;

static const HtpPage HTP_PAGES[] = {
/* 0 */{ "THE BASICS — OBJECTIVE & TURNS", YEL, {
  "You are Captain Alistair Thorne, 11th East Lancashire Regiment, 1917.",
  "Your company holds a trench sector near Passchendaele, Flanders.",
  "Brigade HQ has ordered you to hold the line for six weeks. Survive.",
  "~",
  "~ TURNS",
  "The game runs for 84 turns — 6 weeks x 7 days x 2 half-days (AM/PM).",
  "Press SPACE to end your turn. Events and decay fire between turns.",
  "One command point regenerates each AM turn automatically.",
  "~",
  "~ END STATES",
  "| VICTORY  | Survive all 84 turns — your unit is relieved from the line. |",
  "| DEFEAT   | Every man in every squad has been killed.                   |",
  "| MUTINY   | All squads simultaneously fall below 5 morale.             |",
  "~",
  "There is no 'winning' in the usual sense. You endure. That is enough.",
  NULL
}},
/* 1 */{ "SQUADS & SERGEANTS", CYN, {
  "You command four sections, each led by a sergeant with a personality.",
  "Select a squad with the arrow keys. Press D to view the full Dossier.",
  "~",
  "| ALPHA   | Sgt. Harris | Brave     | 7/8 men | Good morale  |",
  "| BRAVO   | Sgt. Moore  | Steadfast | 8/8 men | Good morale  |",
  "| CHARLIE | Sgt. Lewis  | Drunkard  | 5/8 men | Poor morale  |",
  "| DELTA   | Sgt. Bell   | Cowardly  | 6/8 men | Fair morale  |",
  "~",
  "~ PERSONALITY  multiplies all fatigue and morale changes",
  "| Steadfast | x1.00 | Reliable. No penalty or bonus.             |",
  "| Brave     | x1.20 | Pushes hard. Burns fast. Worth keeping busy.|",
  "| Cowardly  | x0.70 | Low effectiveness. Poor under sustained fire.|",
  "| Drunkard  | x0.85 | Slightly below standard. Unpredictable.    |",
  "~",
  "A squad at 0 morale may mutiny. A squad at 0 men is gone for good.",
  NULL
}},
/* 2 */{ "SQUAD ORDERS", MAG, {
  "Press O then LEFT/RIGHT to cycle orders, ENTER to confirm.",
  "Orders persist until you change them.",
  "~",
  "| STANDBY  | -5fat  0mor  | Low ammo  | Safe. Rest in place.            |",
  "| PATROL   | +10fat +3mor | Med ammo  | Scout sector. Best morale gain. |",
  "| RAID     | +20fat +4mor | High ammo | Strike enemy. High risk/reward. |",
  "| REPAIR   | +5fat  0mor  | Min ammo  | Generate tools. Fix the trench. |",
  "| REST     | -15fat +5mor | Min ammo  | Fastest fatigue and morale cure.|",
  "| FORAGE   | +12fat -2mor | No ammo   | Find food. Costs morale.        |",
  "| SCAVENGE | +15fat -3mor | No ammo   | Find ammo+tools. Low defence.   |",
  "~",
  "~ KEY RULES",
  "Squads above 80% fatigue lose 4 extra morale per turn.",
  "PATROL bonus is multiplied by your current Ammo Policy (see page 5).",
  "Below 10% morale a squad may desert one man per turn.",
  "FORAGE and SCAVENGE leave squads exposed — watch sector threats.",
  NULL
}},
/* 3 */{ "RESOURCES — FOOD, AMMO, MEDS, TOOLS", GRN, {
  "Four resources must be balanced across 84 turns.",
  "~",
  "| FOOD  | Cap 100 (125*) | Consumed by men alive each turn.        |",
  "| AMMO  | Cap 100 (125*) | Spent by squads per their task.         |",
  "| MEDS  | Cap  50        | Spent treating wounds and gas attacks.  |",
  "| TOOLS | Cap  50        | Spent on upgrades; earned by REPAIR.    |",
  "  * Cap raised by Food Cache / Munitions Store upgrades.",
  "~",
  "~ CRITICAL THRESHOLDS",
  "| Food < 15 | All squads lose 5 morale per turn immediately.     |",
  "| Ammo < 10 | PATROL morale bonus is cancelled entirely.         |",
  "| Meds = 0  | Each wound has a 15% chance per turn of killing.   |",
  "~",
  "Press R for the Resource Management screen.",
  "There you can view sparkline trends, barter resources, set policies.",
  "Convoys arrive automatically. You can also request one (2 CP).",
  NULL
}},
/* 4 */{ "RATION POLICY, AMMO POLICY & BARTER", YEL, {
  "Set in Resources (R) -> Policies tab with arrow keys.",
  "~",
  "~ RATION POLICY  food multiplier + morale effect per squad per turn",
  "| FULL      | x1.00 |  0 mor | Standard. No penalty.                |",
  "| HALF      | x0.75 | -1 mor | Manageable short-term.               |",
  "| QUARTER   | x0.55 | -4 mor | Heavy damage within a week.          |",
  "| EMERGENCY | x0.30 | -8 mor | Last resort. Men will not endure long.|",
  "~",
  "~ AMMO POLICY  affects consumption, patrol bonus, and raid resistance",
  "| CONSERVE | x0.60 ammo | x0.60 patrol bonus | -10% raid resist |",
  "| NORMAL   | x1.00 ammo | x1.00 patrol bonus |   0% raid resist |",
  "| LIBERAL  | x1.50 ammo | x1.40 patrol bonus | +15% raid resist |",
  "~",
  "~ BARTER  (Resources -> Barter tab)",
  "Trade any resource for another at unfavourable frontline rates.",
  "Example: 20 food -> 8 ammo. Use only when a resource turns critical.",
  NULL
}},
/* 5 */{ "COMMAND POINTS & COMMAND ACTIONS", CYN, {
  "CP represent your personal leadership reserve (shown as dots in header).",
  "Max 3 per day (4 with Signal Wire upgrade). 1 regenerates each AM turn.",
  "Press C to open Command Actions and spend them.",
  "~",
  "| Rum Ration          | 1CP -5food | +15 morale to selected squad       |",
  "| Write Letters       | 1CP        | +10 morale to selected squad       |",
  "| Medical Triage      | 1CP -5meds | -25 fatigue on selected squad      |",
  "| Inspect/Reprimand   | FREE       | -8 fatigue, -5 morale  (no CP)     |",
  "| Officer's Speech    | 2CP        | +8 morale on ALL squads            |",
  "| Emergency Rations   | 2CP -20food| -15 fatigue on ALL squads          |",
  "| Medal Ceremony      | 3CP        | +20 morale selected; +1 medal      |",
  "| Compassionate Leave | 2CP        | -1 man but +15 morale              |",
  "| Treat Wounded       | 1CP -3meds | Heal 2 wounds in selected squad    |",
  "| Supply Request      | 2CP        | Petition HQ (quality scales w/rep) |",
  "~",
  "Rum Store upgrade makes Rum Ration cost 0 CP (food cost remains).",
  NULL
}},
/* 6 */{ "WOUNDS & MEDICAL CARE", RED, {
  "Artillery, raids, gas, snipers, and friendly fire create wounded men.",
  "Wounds appear as '+2W' next to squad strength in the main view.",
  "~",
  "~ HOW WOUNDS WORK",
  "Each wound drains 1 med per turn passively — automatically.",
  "If meds reach 0, every wound has a 15% chance per turn of killing.",
  "Wounded men fight normally. Wounds are an attrition drain, not penalty.",
  "~",
  "~ HOW TO TREAT WOUNDS",
  "| Treat Wounded (C menu) | 1CP -3meds  | Heals up to 2 wounds instantly  |",
  "| Field Hospital upgrade |             | Auto-heals 1 wound per turn     |",
  "| Medic notable soldier  |             | Auto-heals 1 wound per turn     |",
  "| Natural recovery event |             | Rare random event               |",
  "~",
  "~ PRIORITY GUIDANCE",
  "Build the Field Hospital early if raids are frequent.",
  "If meds are critically low and wounds are high: Treat Wounded first.",
  "A man who dies of an untreated wound is a permanent loss.",
  NULL
}},
/* 7 */{ "TRENCH UPGRADES & NOTABLE SOLDIERS", MAG, {
  "~ TRENCH UPGRADES  (Pause menu -> Trench Upgrades, spend tools)",
  "| Duckboards       |  8t | Rain/storm fatigue halved.                |",
  "| Sandbag Revetmts | 12t | Artillery casualties reduced by 1.        |",
  "| Officers' Dugout | 15t | +1 morale per turn for all squads.        |",
  "| Lewis Gun Nest   | 20t | +15% raid resistance for all squads.      |",
  "| Signal Wire      |  8t | +1 CP per AM turn (4 CP max).             |",
  "| Field Hospital   | 18t | Auto-heals 1 wound per turn.              |",
  "| Observation Post | 14t | Sniper/raid events reduced by 25%.        |",
  "| Munitions Store  | 12t | Ammo cap +25; ammo damp events stopped.   |",
  "| Food Cache       | 10t | Food cap +25; rat/spoilage events stopped.|",
  "~",
  "~ NOTABLE SOLDIERS  (2 per squad, visible in Dossier)",
  "| Sharpshooter | +10% raid resist; can intercept snipers          |",
  "| Cook         | Saves 1 food per turn passively                  |",
  "| Medic        | Auto-treats 1 wound per turn (costs 1 med)       |",
  "| Scrounger    | Randomly finds 1 ammo or 1 tool per turn         |",
  "| Musician     | +2 morale per turn passively                     |",
  "| Runner       | Supply convoy ETA reduced by 1 turn              |",
  "Notable soldiers can be killed. Their deaths are logged by name.",
  NULL
}},
/* 8 */{ "HQ REPUTATION, DISPATCHES & SECTOR THREATS", YEL, {
  "~ HQ REPUTATION  (0-100, header shows Rep:N)",
  "High rep (>70) means bigger supply convoys and better request outcomes.",
  "Modified by how you respond to the five scripted HQ Dispatches.",
  "~",
  "~ HQ DISPATCHES  five orders arrive at fixed turns — comply or defy",
  "| T5  | Night Raid    | Comply: -agg, -ammo      / Defy: +agg, -morale |",
  "| T16 | Ration Cut    | Comply: -food, +rep      / Defy: convoy delayed  |",
  "| T28 | Pioneer Loan  | Comply: -2men, +ammo     / Defy: +agg, -morale  |",
  "| T44 | Def. Posture  | Comply: STANDBY, +ammo   / Defy: sector gets hit|",
  "| T60 | VC Nomination | Comply: +morale, +medal  / Defy: -morale, -rep  |",
  "~",
  "~ SECTOR THREATS  (sectors A-D, each 0-100)",
  "Shown in the map panel and Intel screen (I).",
  "Threat RISES with aggression and Sector Assault random events.",
  "Threat FALLS when the corresponding squad is on PATROL or RAID.",
  "High threat means more frequent raids and artillery in that sector.",
  "Observation Post upgrade improves threat visibility.",
  NULL
}},
/* 9 */{ "DIFFICULTY, SCORING & KEY CONTROLS", GRN, {
  "~ DIFFICULTY  chosen at campaign start, cannot be changed mid-game",
  "| Green Fields  | x0.6 events | x0.7 morale | x0.8 score | Saves: YES |",
  "| Into the Mud  | x1.0 events | x1.0 morale | x1.0 score | Saves: YES |",
  "| No Man's Land | x1.5 events | x1.3 morale | x1.4 score | Saves: YES |",
  "| God Help Us   | x1.8 events | x1.5 morale | x2.0 score | Saves: NO  |",
  "~",
  "~ SCORING",
  "Turns x5 + Men x20 + Morale x3 + Medals x50 + Rep/10 x30 + Upgrades x25",
  "+500 (victory) - 200 (mutiny)  x difficulty multiplier",
  "Grades: S+(1400+) S(1000+) A(700+) B(500+) C(300+) D(120+) F(<120)",
  "~",
  "~ KEY CONTROLS",
  "| SPACE | End turn    | O | Orders        | C | Command actions |",
  "| R     | Resources   | I | Intel report  | D | Squad dossier   |",
  "| ESC   | Pause menu  | Q | Quit to menu  |   |                 |",
  "~",
  "Use the Codex (in the pause menu) for historical lore on the war.",
  "Good luck, Captain Thorne. God help us.",
  NULL
}},
};
#define HTP_PAGE_COUNT (int)(sizeof(HTP_PAGES)/sizeof(HTP_PAGES[0]))

static void screen_how_to_play(void){
    int page=0;
    for(;;){
        cls();
        const HtpPage *p=&HTP_PAGES[page];

        /* outer box */
        at(1,1);fg(BLU);attr_bold();
        fputs(BOX_TL,stdout);for(int i=2;i<TW;i++) fputs(BOX_H,stdout);fputs(BOX_TR,stdout);attr_rst();

        /* title */
        at(1,3);fg(p->title_color);attr_bold();
        printf(" HOW TO PLAY  "BOX_VT"  %-40s  "BOX_VT"  pg %d/%d",p->title,page+1,HTP_PAGE_COUNT);attr_rst();

        /* divider */
        at(2,1);fg(BLU);attr_bold();fputs(BOX_LM,stdout);
        for(int i=2;i<TW;i++){fputs(BOX_H,stdout);} fputs(BOX_RM,stdout); attr_rst();

        /* side bars */
        for(int r=3;r<=21;r++){
            at(r,1);fg(BLU);attr_bold();fputs(BOX_V,stdout);attr_rst();
            at(r,TW);fg(BLU);attr_bold();fputs(BOX_V,stdout);attr_rst();
        }

        /* content */
        int row=3;
        for(int i=0;p->lines[i]&&row<=21;i++){
            const char *ln=p->lines[i];
            at(row++,3);
            if(ln[0]=='~'){
                if(ln[1]==' '){fg(p->title_color);attr_bold();printf("  %s",ln+2);attr_rst();}
                else{fg(GRY);attr_dim();for(int j=0;j<TW-4;j++) putchar('-');attr_rst();}
            } else if(ln[0]=='|'){
                /* table row */
                fg(GRY);fputs("  ",stdout);attr_rst();
                const char *c=ln+1; int ci=0;
                while(*c){
                    if(*c=='|'){putchar(' ');fg(GRY);putchar('|');attr_rst();putchar(' ');c++;ci++;}
                    else{fg(ci%2==0?WHT:GRY);while(*c&&*c!='|') putchar(*c++);attr_rst();}
                }
            } else {
                fg(WHT);fputs("  ",stdout);fputs(ln,stdout);attr_rst();
            }
        }

        /* nav bar */
        at(22,1);fg(BLU);attr_bold();fputs(BOX_LM,stdout);
        for(int i=2;i<TW;i++){fputs(BOX_H,stdout);} fputs(BOX_RM,stdout); attr_rst();

        /* page dots */
        at(22,3);
        for(int i=0;i<HTP_PAGE_COUNT;i++){
            if(i==page){fg(p->title_color);attr_bold();fputs(SYM_BL,stdout);}
            else{fg(GRY);attr_dim();fputs(SYM_CI,stdout);}
            attr_rst();
        }
        at(22,TW-46);fg(YEL);
        fputs("["SYM_LF"] Prev  ["SYM_RT"] Next  [1-9][0] Jump  [ESC] Back",stdout);attr_rst();

        /* bottom border */
        at(23,1);fg(BLU);attr_bold();fputs(BOX_BL,stdout);
        for(int i=2;i<TW;i++){fputs(BOX_H,stdout);} fputs(BOX_BR,stdout); attr_rst();

        fflush(stdout);

        Key k=getch();
        if(k==KEY_ESC||k==KEY_Q) break;
        if(k==KEY_LEFT||k==KEY_UP)    page=(page+HTP_PAGE_COUNT-1)%HTP_PAGE_COUNT;
        if(k==KEY_RIGHT||k==KEY_DOWN) page=(page+1)%HTP_PAGE_COUNT;
        if(k>=KEY_1&&k<=KEY_9) page=k-KEY_1;
        if(k==KEY_0) page=9;
    }
}

/* ================================================================
   SCREEN: CODEX, DIARY, SAVE/LOAD — (reused from v3)
   ================================================================ */

static void screen_codex(int start){
    int idx=clamp(start,0,CODEX_COUNT-1);
    for(;;){
        const CodexEntry *ce=&CODEX[idx]; cls(); draw_box(1,2,23,79);
        at(1,4);fg(YEL);attr_bold();printf(" CODEX  " BOX_VT "  %d/%d  " BOX_VT "  %-38s",idx+1,CODEX_COUNT,ce->title);attr_rst();
        draw_hline_mid(2,2,79);
        at(4,5);fg(ce->title_color);attr_bold();fputs(ce->title,stdout);attr_rst();
        at(5,5);fg(GRY);for(int i=0;i<72;i++) putchar('-');attr_rst();
        int r=7; for(int i=0;ce->lines[i]&&r<=20;i++,r++){at(r,5);fg(WHT);fputs(ce->lines[i],stdout);attr_rst();}
        draw_hline_mid(21,2,79);
        at(22,5);fg(YEL);fputs("[" SYM_LF "] Prev  [" SYM_RT "] Next  [1-9] Jump  [ESC] Back",stdout);attr_rst();fflush(stdout);
        Key k=getch(); if(k==KEY_ESC||k==KEY_Q) break;
        if(k==KEY_LEFT||k==KEY_UP) idx=(idx+CODEX_COUNT-1)%CODEX_COUNT;
        if(k==KEY_RIGHT||k==KEY_DOWN) idx=(idx+1)%CODEX_COUNT;
        if(k>=KEY_1&&k<=KEY_9){int ji=k-KEY_1;if(ji<CODEX_COUNT) idx=ji;}
    }
}

static void screen_diary(void){
    int scroll=0;
    static const char *mns[]={"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
    static const int md[]={31,28,31,30,31,30,31,31,30,31,30,31};
    for(;;){
        cls(); draw_box(1,2,23,79);
        at(1,4);fg(YEL);attr_bold();printf(" FIELD DIARY  " BOX_VT "  %d entries",G.diary_count);attr_rst();
        draw_hline_mid(2,2,79);
        int vis=16,total=G.diary_count;
        for(int i=0;i<vis;i++){
            int ei=total-1-(scroll+i);
            at(3+i,3);at(3+i,2);fg(BLU);attr_bold();fputs(BOX_V,stdout);attr_rst();
            at(3+i,79);fg(BLU);attr_bold();fputs(BOX_V,stdout);attr_rst();
            if(ei>=0&&ei<total){
                at(3+i,3);
                DiaryEntry *e=&G.diary[ei];
                int days=(e->turn-1)/2,mo=0,d=days;
                while(mo<11&&d>=md[mo]){d-=md[mo];mo++;}
                fg(GRY);printf(" T%-3d %s %2d %-3s  ",e->turn,e->half_am?"AM":"PM",d+1,mns[mo]);attr_rst();
                fg(WHT);ppad(e->text,TW-22);attr_rst();
            }
        }
        draw_hline_mid(20,2,79);
        at(21,4);fg(GRY);printf(" %d-%d of %d",total-scroll,total>0?total-(scroll+vis-1)>0?total-(scroll+vis-1):1:0,total);attr_rst();
        at(22,4);fg(YEL);fputs("[" SYM_UP SYM_DN "] Scroll  [ESC] Back",stdout);attr_rst();fflush(stdout);
        Key k=getch(); if(k==KEY_ESC||k==KEY_Q) break;
        if(k==KEY_DOWN&&scroll+vis<total) scroll++;
        if(k==KEY_UP&&scroll>0) scroll--;
    }
}

static int screen_save_load(int is_save){
    SaveMeta meta[SAVE_SLOTS]; int valid[SAVE_SLOTS];
    for(int i=0;i<SAVE_SLOTS;i++) valid[i]=save_meta_read(i,&meta[i]);
    char texts[SAVE_SLOTS][72]; typedef struct{const char *text;int disabled;const char *hint;}MenuItem;
    MenuItem items[SAVE_SLOTS];
    for(int i=0;i<SAVE_SLOTS;i++){
        if(valid[i]) snprintf(texts[i],sizeof(texts[i]),"Slot %d  |  %s",i+1,meta[i].slot_label);
        else         snprintf(texts[i],sizeof(texts[i]),"Slot %d  |  (empty)",i+1);
        items[i].text=texts[i]; items[i].disabled=(!is_save&&!valid[i]); items[i].hint=NULL;
    }
    /* simple inline menu */
    int sel=0;
    for(;;){
        cls(); draw_box(6,12,6+SAVE_SLOTS*2+3,68);
        at(6,14);fg(YEL);attr_bold();
        fputs(is_save?" SAVE GAME — Choose a slot":" LOAD GAME — Choose a slot",stdout);attr_rst();
        draw_hline_mid(7,12,68);
        for(int i=0;i<SAVE_SLOTS;i++){
            at(8+i*2,14);
            if(items[i].disabled){attr_dim();fg(GRY);}else if(i==sel){fg(YEL);attr_bold();}else fg(WHT);
            printf("%s %s",i==sel?"\xe2\x96\xb6 ":"  ",items[i].text);attr_rst();
        }
        at(8+SAVE_SLOTS*2,14);fg(GRY);fputs("[" SYM_UP SYM_DN "] Nav  [ENTER] Select  [ESC] Cancel",stdout);attr_rst();fflush(stdout);
        Key k=getch(); if(k==KEY_ESC) return -1;
        if(k==KEY_UP) {do{sel=(sel+SAVE_SLOTS-1)%SAVE_SLOTS;}while(items[sel].disabled);}
        if(k==KEY_DOWN){do{sel=(sel+1)%SAVE_SLOTS;}while(items[sel].disabled);}
        if(k==KEY_ENTER&&!items[sel].disabled) return sel;
    }
}

/* ================================================================
   MENUS
   ================================================================ */

static Difficulty screen_difficulty(void){
    int sel=1;
    for(;;){
        cls(); draw_box(3,10,12,70);
        at(3,12);fg(YEL);attr_bold();fputs(" SELECT DIFFICULTY",stdout);attr_rst();
        draw_hline_mid(4,10,70);
        for(int i=0;i<DIFF_COUNT;i++){
            at(5+i*2,13);int s=(i==sel);
            if(s){fg(DIFF_DEFS[i].color);attr_bold();printf("  \xe2\x96\xb6 %-16s",DIFF_DEFS[i].name);}
            else{fg(GRY);printf("    %-16s",DIFF_DEFS[i].name);}
            attr_rst();fg(s?WHT:GRY);attr_dim();fputs(DIFF_DEFS[i].subtitle,stdout);attr_rst();
        }
        at(13,13);fg(GRY);fputs("[" SYM_UP SYM_DN "] Nav  [ENTER] Confirm",stdout);attr_rst();fflush(stdout);
        Key k=getch(); if(k==KEY_ESC) return DIFF_NORMAL;
        if(k==KEY_UP) sel=(sel+DIFF_COUNT-1)%DIFF_COUNT;
        if(k==KEY_DOWN) sel=(sel+1)%DIFF_COUNT;
        if(k==KEY_ENTER) return (Difficulty)sel;
    }
}

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
        at(6,22);fg(GRY);fputs("11th East Lancashire Regt.  " BOX_VT "  Passchendaele  " BOX_VT "  1917",stdout);attr_rst();
        const char *opts[]={"New Campaign","Load Game","How to Play","Codex & Lore","Credits","Quit"};
        int disabled[]={0,!any_save,0,0,0,0},n=6;
        int bw=40,br=(TW-bw)/2+1;
        draw_box(8,br,8+n*2+2,br+bw-1);
        for(int i=0;i<n;i++){
            int row=9+i*2;
            at(row,br+2);
            if(disabled[i]){attr_dim();fg(GRY);printf("    %s",opts[i]);attr_rst();}
            else if(i==sel){fg(YEL);attr_bold();printf("  \xe2\x96\xb6 %s",opts[i]);attr_rst();}
            else{fg(WHT);printf("    %s",opts[i]);attr_rst();}
        }
        at(9+n*2,br+4);fg(GRY);fputs("[" SYM_UP SYM_DN "] Nav  [ENTER] Select",stdout);attr_rst();fflush(stdout);
        Key k=getch();
        if(k==KEY_UP) {do{sel=(sel+n-1)%n;}while(disabled[sel]);}
        if(k==KEY_DOWN){do{sel=(sel+1)%n;}while(disabled[sel]);}
        if(k==KEY_Q||k==KEY_ESC) return 4;
        if(k==KEY_ENTER&&!disabled[sel]) return sel;
    }
}

/* Returns 0=resume,1=save,2=load,3=codex,4=diary,5=dossier,6=upgrades,7=quit */
static int screen_pause_menu(void){
    int ironman=(G.difficulty==DIFF_IRONMAN);
    const char *opts[]={"Resume","How to Play","Save Game","Load Game","Codex & Lore","Field Diary","Squad Dossier","Trench Upgrades","Quit to Main Menu"};
    int dis[]={0,0,0,ironman,0,0,0,0,0},n=9,sel=0;
    for(;;){
        cls();int bw=52,br=(TW-bw)/2+1;
        draw_box(4,br,4+n*2+3,br+bw-1);
        at(4,br+2);fg(YEL);attr_bold();fputs(" PAUSED  " BOX_VT "  11th East Lancashire Regt.",stdout);attr_rst();
        draw_hline_mid(5,br,br+bw-1);
        for(int i=0;i<n;i++){
            int row=6+i*2;
            at(row,br+2);
            if(dis[i]){attr_dim();fg(GRY);printf("    %s",opts[i]);attr_rst();}
            else if(i==sel){fg(YEL);attr_bold();printf("  \xe2\x96\xb6 %s",opts[i]);attr_rst();}
            else{fg(WHT);printf("    %s",opts[i]);attr_rst();}
        }
        at(6+n*2,br+2);fg(GRY);fputs("[" SYM_UP SYM_DN "] Nav  [ENTER] Select  [ESC] Resume",stdout);attr_rst();fflush(stdout);
        Key k=getch(); if(k==KEY_ESC) return 0;
        if(k==KEY_UP) {do{sel=(sel+n-1)%n;}while(dis[sel]);}
        if(k==KEY_DOWN){do{sel=(sel+1)%n;}while(dis[sel]);}
        if(k==KEY_ENTER&&!dis[sel]) return sel;
    }
}

static void screen_credits(void){
    cls(); draw_box(2,8,22,73);
    typedef struct{int r,c,col,bold;const char *t;}SL;
    static const SL L[]={
        {3,22,YEL,1,"B U R D E N   O F   C O M M A N D"},
        {4,24,GRY,0,"A WWI Trench Management Tycoon — v4"},
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
        at(L[i].r,L[i].c);if(L[i].bold){attr_bold();}fg(L[i].col);fputs(L[i].t,stdout);attr_rst();
    }
    fflush(stdout);getch();
}

static void screen_end(void){
    cls(); int men=total_men(),maxm=0;
    for(int i=0;i<G.squad_count;i++) maxm+=G.squads[i].maxm;
    int pct=maxm?(int)(100.f*men/maxm):0;
    int score=calc_score(); const char *grade=score_grade(score);
    typedef struct{OverState s;int col;const char *banner,*line1;}EndDef;
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
    at(5,8);fputs(BOX_BL,stdout);for(int i=0;i<62;i++)fputs(BOX_H,stdout);fputs(BOX_BR,stdout);attr_rst();
    at(7,10);fg(ed->col);fputs(ed->line1,stdout);attr_rst();
    at(8,10);fg(WHT);
    if(G.over==OVER_WIN) printf("%d/%d men survived (%d%%).",men,maxm,pct);
    else if(G.over==OVER_LOSE) printf("Fell on Week %d, Turn %d of %d.",curr_week(),G.turn,G.maxt);
    else printf("%d/%d men turned against their officers.",men,maxm);
    attr_rst();
    at(10,10);fg(CYN);attr_bold();printf("FINAL SCORE: %d",score);attr_rst();
    at(10,30);fg(YEL);attr_bold();printf("GRADE: %s",grade);attr_rst();
    at(10,42);fg(DIFF_DEFS[G.difficulty].color);printf("[%s]",DIFF_DEFS[G.difficulty].name);attr_rst();
    at(10,58);fg(GRY);printf("Rep: %d",G.hq_rep);attr_rst();
    at(12,10);fg(WHT);attr_bold();fputs("CAMPAIGN REPORT",stdout);attr_rst();
    at(13,10);fg(GRY);for(int i=0;i<58;i++) putchar('-');attr_rst();
    at(14,10);fg(WHT);printf(" Turns survived  : %d / %d",G.turn,G.maxt);attr_rst();
    at(15,10);fg(WHT);printf(" Men remaining   : %d / %d  (%d%%)",men,maxm,pct);attr_rst();
    at(16,10);fg(WHT);printf(" Overall morale  : %d%%  %-9s",overall_mor(),mor_label(overall_mor()));attr_rst();
    at(17,10);fg(WHT);printf(" Medals earned   : %d",G.medals);attr_rst();
    at(18,10);fg(WHT);printf(" Upgrades built  : %d / %d",popcount(G.upgrades),UPG_COUNT);attr_rst();
    at(19,10);fg(WHT);printf(" HQ Reputation   : %d / 100",G.hq_rep);attr_rst();
    at(20,10);fg(WHT);printf(" Ration policy   : %s",RATION_DEFS[G.ration_level].name);attr_rst();
    at(21,10);fg(CYN);attr_bold();fputs("SQUAD SUMMARY",stdout);attr_rst();
    for(int i=0;i<G.squad_count&&i<4;i++){
        Squad *s=&G.squads[i];
        at(22+i,10);fg(WHT);
        printf(" %-8s  %d/%d men  Mor:%3d%%  Raids:%2d  Lost:%2d  Wounds:%d",
               s->name,s->men,s->maxm,s->mor,s->raids_repelled,s->men_lost,s->wounds);
        attr_rst();
    }
    at(24,10);fg(GRY);fputs("Press any key...",stdout);attr_rst();fflush(stdout);getch();
}

/* ================================================================
   GAME LOGIC
   ================================================================ */

static Squad *rand_squad(void){return &G.squads[rand()%G.squad_count];}
static Squad *weakest_sq(void){int wi=0;for(int i=1;i<G.squad_count;i++) if(G.squads[i].mor<G.squads[wi].mor) wi=i;return &G.squads[wi];}
static Squad *largest_sq(void){int li=0;for(int i=1;i<G.squad_count;i++) if(G.squads[i].men>G.squads[li].men) li=i;return &G.squads[li];}

static void record_history(void){
    int hi=G.res_hist_count%RES_HIST_LEN;
    G.res_hist[hi][RES_FOOD]=G.food;
    G.res_hist[hi][RES_AMMO]=G.ammo;
    G.res_hist[hi][RES_MEDS]=G.meds;
    G.res_hist[hi][RES_TOOLS]=G.tools;
    G.res_hist_count++;
}

static void process_evq(void){
    char msg[MSG_LEN]; int keep=0;
    for(int i=0;i<G.ev_count;i++){
        SchedEv *e=&G.evq[i];
        if(G.convoy_delayed>0&&e->type==EV_SUPPLY){e->at++;G.convoy_delayed--;G.evq[keep++]=*e;continue;}
        if(e->at!=G.turn){G.evq[keep++]=*e;continue;}
        switch(e->type){
        case EV_SUPPLY:
            G.food =clamp(G.food +e->food, 0,food_cap());
            G.ammo =clamp(G.ammo +e->ammo, 0,ammo_cap());
            G.meds =clamp(G.meds +e->meds, 0,50);
            G.tools=clamp(G.tools+e->tools,0,50);
            snprintf(msg,MSG_LEN,"Supply! +%df +%da +%dm +%dt",e->food,e->ammo,e->meds,e->tools);
            log_msg(msg);
            if(G.supply_req_pending==G.turn) G.supply_req_pending=-1;
            break;
        case EV_REINFORCE:
            if(G.squad_count>0){Squad *sq=&G.squads[0];int a=clamp(e->men,0,sq->maxm-sq->men);sq->men+=a;
                snprintf(msg,MSG_LEN,"%d reinforcements join %s!",a,sq->name);log_msg(msg);}
            break;
        default: break;
        }
    }
    G.ev_count=keep;
}

static void apply_dispatch(int idx,int comply){
    char msg[MSG_LEN]; const HqDispatch *d=&HQ_DISPATCHES[idx];
    G.dispatch_done|=(1<<idx);
    if(comply){
        G.dispatch_comply|=(1<<idx);
        G.agg=clamp(G.agg+d->cy_agg,5,95);
        for(int i=0;i<G.squad_count;i++) G.squads[i].mor=clamp(G.squads[i].mor+d->cy_all_mor,0,100);
        G.ammo=clamp(G.ammo+d->cy_ammo,0,ammo_cap());
        G.food=clamp(G.food+d->cy_food,0,food_cap());
        if(d->cy_force_raid){Squad *sq=weakest_sq();sq->task=TASK_RAID;}
        if(d->cy_all_standby){for(int i=0;i<G.squad_count;i++) G.squads[i].task=TASK_STANDBY;G.forced_standby_turns=1;}
        if(d->cy_lose_men){Squad *sq=largest_sq();sq->men=clamp(sq->men-d->cy_lose_men,1,sq->maxm);}
        G.medals+=d->cy_medals;
        G.hq_rep=clamp(G.hq_rep+d->cy_rep_delta,0,100);
        snprintf(msg,MSG_LEN,"COMPLY: %s",d->comply_result);
    } else {
        G.agg=clamp(G.agg+d->df_agg,5,95);
        for(int i=0;i<G.squad_count;i++) G.squads[i].mor=clamp(G.squads[i].mor+d->df_all_mor,0,100);
        G.hq_rep=clamp(G.hq_rep+d->df_rep_delta,0,100);
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
    for(int i=0;i<HQ_DISPATCH_COUNT;i++){
        if(G.dispatch_done&(1<<i)) continue;
        if(G.turn>=HQ_DISPATCHES[i].turn&&G.dispatch_pending<0) G.dispatch_pending=i;
    }
}

static void apply_weather_effects(void){
    const WeatherEffect *fx=&WEATHER_FX[G.weather];
    float dm=(float)DIFF_DEFS[G.difficulty].event_mul;
    int fat_add=(int)(fx->fat_per_turn*dm+0.5f);
    if(upg_has(UPG_DUCKBOARDS)&&(G.weather==WEATHER_RAIN||G.weather==WEATHER_STORM)) fat_add/=2;
    for(int i=0;i<G.squad_count;i++) G.squads[i].fat=clamp(G.squads[i].fat+fat_add,0,100);
    G.agg=clamp(G.agg+fx->agg_drift,5,95);
}

static void apply_notable_passives(void){
    /* Cook: -1 food cost; Medic: auto-heal 1 wound; Scrounger: +1 ammo or tools; Musician: +2 morale; Runner: no passive here */
    for(int i=0;i<G.squad_count;i++){
        Squad *sq=&G.squads[i];
        if(!sq->men) continue;
        Notable *cook=find_notable(sq,TRAIT_COOK);
        if(cook) G.food=clamp(G.food+1,0,food_cap());
        Notable *med=find_notable(sq,TRAIT_MEDIC);
        if(med&&sq->wounds>0){sq->wounds--;G.meds=clamp(G.meds-1,0,50);}
        Notable *scr=find_notable(sq,TRAIT_SCROUNGER);
        if(scr){
            if(rng_bool(0.5f)) G.ammo=clamp(G.ammo+1,0,ammo_cap());
            else G.tools=clamp(G.tools+1,0,50);
        }
        Notable *mus=find_notable(sq,TRAIT_MUSICIAN);
        if(mus) sq->mor=clamp(sq->mor+2,0,100);
    }
}

static void consume(void){
    float fm=(float)DIFF_DEFS[G.difficulty].food_mul*(float)RATION_DEFS[G.ration_level].food_mul;
    int fc=(int)(clamp(total_men()/6,1,99)*fm+0.5f)+WEATHER_FX[G.weather].food_extra;
    float am=(float)AMMO_DEFS[G.ammo_policy].ammo_mul;
    int ac=0; for(int i=0;i<G.squad_count;i++) ac+=(int)(TASK_DEFS[G.squads[i].task].ammo_cost*am+0.5f);
    /* meds consumed by wounds */
    int mc=total_wounds();
    G.food=clamp(G.food-fc,0,food_cap());
    G.ammo=clamp(G.ammo-ac,0,ammo_cap());
    G.meds=clamp(G.meds-mc,0,50);
    /* ration policy morale effect */
    int ration_mor=RATION_DEFS[G.ration_level].mor_per_turn;
    if(ration_mor!=0) for(int i=0;i<G.squad_count;i++) G.squads[i].mor=clamp(G.squads[i].mor+ration_mor,0,100);

    if(G.food<15){
        float mm=(float)DIFF_DEFS[G.difficulty].morale_mul;
        for(int i=0;i<G.squad_count;i++) G.squads[i].mor=clamp(G.squads[i].mor-(int)(5*mm),0,100);
        add_msg("CRITICAL: Food exhausted — morale falling!");
    }
    if(G.ammo<10) add_msg("WARNING: Ammunition nearly depleted!");
    if(G.meds<=0&&total_wounds()>0) add_msg("WARNING: No meds — wounds untreated!");
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
        int mor_d=(int)(td->mor_delta*pm*(sq->task==TASK_PATROL?(float)AMMO_DEFS[G.ammo_policy].patrol_mor_mul:1.0f));
        if(sq->task==TASK_PATROL&&upg_has(UPG_PERISCOPE)) mor_d+=2;
        if(upg_has(UPG_DUGOUT)) mor_d+=1;
        if(sq->task==TASK_PATROL&&G.ammo<20) mor_d-=(int)(4*mm);
        sq->mor=clamp(sq->mor+mor_d,0,100);
        /* resource gains from task */
        if(td->food_gain>0) G.food=clamp(G.food+td->food_gain,0,food_cap());
        if(td->ammo_gain>0) G.ammo=clamp(G.ammo+td->ammo_gain,0,ammo_cap());
        if(td->tools_gain>0) G.tools=clamp(G.tools+td->tools_gain,0,50);
        /* high fatigue drain */
        if(sq->fat>80) sq->mor=clamp(sq->mor-(int)(4*mm),0,100);
        /* forced standby */
        if(G.forced_standby_turns>0) sq->task=TASK_STANDBY;
        /* wounds: if no meds for 5+ turns, or old wound, kill 1 man */
        if(sq->wounds>0&&G.meds<=0&&rng_bool(0.15f)&&sq->men>1){
            sq->men--; sq->men_lost++; sq->wounds=clamp(sq->wounds-1,0,sq->men);
            char msg[MSG_LEN];
            snprintf(msg,MSG_LEN,"%s Sq: 1 wounded man dies of infection.",sq->name); log_msg(msg);
        }
        /* field hospital auto-heal */
        if(upg_has(UPG_FIELD_HOSP)&&sq->wounds>0&&G.meds>0){
            sq->wounds--; G.meds--;
        }
        /* desertion */
        if(sq->mor<10&&rng_bool(0.18f)&&sq->men>1){
            sq->men--; sq->men_lost++;
            char msg[MSG_LEN];
            snprintf(msg,MSG_LEN,"%s Sq: desertion. Mor %d.",sq->name,sq->mor);log_msg(msg);
        }
    }
    if(G.forced_standby_turns>0) G.forced_standby_turns--;
}

static int has_supply_queued(void){for(int i=0;i<G.ev_count;i++) if(G.evq[i].type==EV_SUPPLY) return 1;return 0;}
static void push_ev(SchedEv ev){if(G.ev_count<MAX_EVQ) G.evq[G.ev_count++]=ev;}

/* kill a notable in a squad and write diary */
static void kill_notable(Squad *sq, Notable *n){
    if(!n||!n->alive) return;
    n->alive=0;
    char msg[MSG_LEN];
    snprintf(msg,MSG_LEN,"%s of %s Sq. has been killed in action.",n->name,sq->name);
    log_msg(msg);
}

static void random_events(void){
    char msg[MSG_LEN];
    float em=(float)DIFF_DEFS[G.difficulty].event_mul;
    float mm=(float)DIFF_DEFS[G.difficulty].morale_mul;
    float probs[REVT_COUNT];
    for(int i=0;i<REVT_COUNT;i++){
        const RandEventProb *p=&RAND_PROBS[i];
        float base=p->agg_divisor>0?G.agg/p->agg_divisor:p->base_prob;
        float wm=(i==REVT_ENEMY_RAID||i==REVT_SECTOR_ASSAULT)?WEATHER_FX[G.weather].raid_mul:1.0f;
        probs[i]=base*em*wm;
    }
    /* OBS POST: reduce sniper/raid */
    float obs_mul=upg_has(UPG_OBS_POST)?0.75f:1.0f;
    probs[REVT_SNIPER]*=obs_mul; probs[REVT_ENEMY_RAID]*=obs_mul;

    /* ARTILLERY */
    if(rng_bool(probs[REVT_ARTILLERY])){
        Squad *sq=rand_squad(); int cas=rng_range(1,2);
        if(upg_has(UPG_SANDBAG)) cas=clamp(cas-1,0,cas);
        if(sq->men>cas){
            sq->men-=cas; sq->men_lost+=cas;
            sq->wounds+=cas; /* artillery produces wounds, not just deaths */
            sq->mor=clamp(sq->mor-(int)(cas*6*mm),0,100);
            snprintf(msg,MSG_LEN,"ARTILLERY! %s Sq: %d casualt%s, +%dW.",sq->name,cas,cas==1?"y":"ies",cas);
            log_msg(msg);
            /* notable might be hit */
            if(rng_bool(0.25f)&&sq->notable_count>0){
                for(int j=0;j<sq->notable_count;j++){
                    if(sq->notables[j].alive&&rng_bool(0.4f)){kill_notable(sq,&sq->notables[j]);break;}
                }
            }
        }
    }
    /* ENEMY RAID */
    if(rng_bool(probs[REVT_ENEMY_RAID])){
        Squad *sq=rand_squad();
        float res=RAID_RESIST[sq->task]+(float)AMMO_DEFS[G.ammo_policy].raid_resist_add;
        if(upg_has(UPG_LEWIS_NEST)) res+=0.15f;
        /* sharpshooter bonus */
        if(find_notable(sq,TRAIT_SHARPSHOOTER)) res+=0.10f;
        if(rng_bool(res)){
            sq->mor=clamp(sq->mor+6,0,100); sq->raids_repelled++;
            if(sq->raids_repelled==2) G.medals++;
            if(sq->raids_repelled==4) G.medals++;
            snprintf(msg,MSG_LEN,"%s Sq repelled enemy raid! Morale up.",sq->name);
        } else {
            if(sq->men>1){sq->men--;sq->men_lost++;sq->wounds++;}
            sq->mor=clamp(sq->mor-(int)(12*mm),0,100);
            snprintf(msg,MSG_LEN,"%s Sq: enemy raid broke through! 1 KIA.",sq->name);
        }
        log_msg(msg);
    }
    /* GAS */
    if(rng_bool(probs[REVT_GAS])){
        Squad *sq=rand_squad();
        if(G.meds>=5){G.meds-=5;snprintf(msg,MSG_LEN,"GAS ATTACK on %s. Meds used (-5).",sq->name);}
        else{int cas=rng_range(1,2);sq->men=clamp(sq->men-cas,0,sq->maxm);sq->men_lost+=cas;sq->wounds+=cas;
             snprintf(msg,MSG_LEN,"GAS ATTACK — no meds! %d man/men lost.",cas);}
        log_msg(msg);
    }
    /* MAIL */
    if(rng_bool(probs[REVT_MAIL])){
        Squad *sq=rand_squad();sq->mor=clamp(sq->mor+rng_range(4,10),0,100);
        snprintf(msg,MSG_LEN,"Mail from home cheers %s Squad.",sq->name);log_msg(msg);
    }
    /* RATS */
    if(rng_bool(probs[REVT_RATS])&&!upg_has(UPG_FOOD_CACHE)){
        int lost=rng_range(2,9);G.food=clamp(G.food-lost,0,food_cap());
        snprintf(msg,MSG_LEN,"Rats in the stores! %d rations lost.",lost);log_msg(msg);
    }
    /* INFLUENZA */
    if(rng_bool(probs[REVT_INFLUENZA]*(upg_has(UPG_SUMP)?0.5f:1.0f))){
        Squad *sq=rand_squad();
        if(sq->men>1){sq->men--;sq->men_lost++;sq->wounds++;}
        snprintf(msg,MSG_LEN,"Influenza: %s Sq loses 1 man.",sq->name);log_msg(msg);
    }
    /* SGT BREAKDOWN */
    if(rng_bool(probs[REVT_SGT_BREAKDOWN])){
        Squad *sq=rand_squad();
        if(sq->has_sgt&&sq->sgt.ok){
            sq->sgt.ok=0;sq->mor=clamp(sq->mor-(int)(8*mm),0,100);
            snprintf(msg,MSG_LEN,"%s has broken down. %s Sq morale falls.",sq->sgt.name,sq->name);log_msg(msg);
        }
    }
    /* SUPPLY CONVOY */
    if(rng_bool(probs[REVT_SUPPLY_CONVOY])&&!has_supply_queued()){
        /* HQ rep affects convoy quality */
        int q=(G.hq_rep>=70)?1:0;
        int eta=G.turn+rng_range(3,7);
        /* runner notable reduces ETA */
        for(int i=0;i<G.squad_count;i++){
            if(find_notable(&G.squads[i],TRAIT_RUNNER)){eta=clamp(eta-1,G.turn+1,G.turn+8);break;}
        }
        SchedEv ev={.at=eta,.type=EV_SUPPLY,
            .food=rng_range(q?15:10,q?30:20),.ammo=rng_range(q?12:8,q?22:15),
            .meds=rng_range(q?5:3,q?10:7),.tools=rng_range(q?4:2,q?8:5)};
        push_ev(ev);
        snprintf(msg,MSG_LEN,"HQ: Supply convoy en route. ETA %d turns.",eta-G.turn);log_msg(msg);
    }
    /* REINFORCE */
    if(rng_bool(probs[REVT_REINFORCE])){
        int eta=G.turn+rng_range(5,12);
        push_ev((SchedEv){.at=eta,.type=EV_REINFORCE,.men=rng_range(1,3)});
        snprintf(msg,MSG_LEN,"HQ: Reinforcements en route. ETA %d turns.",eta-G.turn);log_msg(msg);
    }
    /* SNIPER */
    if(rng_bool(probs[REVT_SNIPER])){
        Squad *sq=rand_squad();
        if(sq->men>1&&(sq->task==TASK_STANDBY||sq->task==TASK_PATROL||sq->task==TASK_FORAGE)){
            /* sharpshooter may save */
            Notable *ss=find_notable(sq,TRAIT_SHARPSHOOTER);
            if(ss&&rng_bool(0.5f)){
                snprintf(msg,MSG_LEN,"Sniper targeting %s Sq — %s spots him first!",sq->name,ss->name);
            } else {
                sq->men--;sq->men_lost++;sq->wounds++;
                sq->mor=clamp(sq->mor-(int)(10*mm),0,100);
                snprintf(msg,MSG_LEN,"Sniper! %s Sq: 1 man hit.",sq->name);
            }
            log_msg(msg);
        }
    }
    /* FRATERNIZATION */
    if(rng_bool(probs[REVT_FRATERNIZE])&&(G.weather==WEATHER_CLEAR||G.weather==WEATHER_FOG)){
        for(int i=0;i<G.squad_count;i++) G.squads[i].mor=clamp(G.squads[i].mor+6,0,100);
        G.agg=clamp(G.agg-8,5,95);
        log_msg("Brief fraternization across no-man's-land. Aggression falls.");
    }
    /* HERO MOMENT */
    if(rng_bool(probs[REVT_HERO])){
        Squad *sq=rand_squad();sq->mor=clamp(sq->mor+10,0,100);sq->raids_repelled++;
        snprintf(msg,MSG_LEN,"Heroism: a man in %s Sq distinguishes himself.",sq->name);log_msg(msg);
    }
    /* FRIENDLY FIRE */
    if(rng_bool(probs[REVT_FRIENDLY_FIRE])){
        Squad *sq=rand_squad();
        if(sq->men>1){sq->men--;sq->men_lost++;sq->wounds++;}
        sq->mor=clamp(sq->mor-(int)(15*mm),0,100);
        snprintf(msg,MSG_LEN,"Friendly fire! %s Sq: 1 man lost.",sq->name);log_msg(msg);
    }
    /* ENEMY CACHE */
    if(rng_bool(probs[REVT_CACHE])){
        int f=rng_range(5,15);G.ammo=clamp(G.ammo+f,0,ammo_cap());
        snprintf(msg,MSG_LEN,"Patrol finds enemy cache! +%d ammo.",f);log_msg(msg);
    }
    /* FOOD SPOILAGE */
    if(rng_bool(probs[REVT_FOOD_SPOIL])&&!upg_has(UPG_FOOD_CACHE)){
        int lost=rng_range(3,12);G.food=clamp(G.food-lost,0,food_cap());
        snprintf(msg,MSG_LEN,"Food spoilage in hot weather! %d rations lost.",lost);log_msg(msg);
    }
    /* AMMO DAMP */
    if(rng_bool(probs[REVT_AMMO_DAMP])&&!upg_has(UPG_MUNITIONS)&&
       (G.weather==WEATHER_RAIN||G.weather==WEATHER_STORM)){
        int lost=rng_range(3,10);G.ammo=clamp(G.ammo-lost,0,ammo_cap());
        snprintf(msg,MSG_LEN,"Damp in the ammo crypt! %d rounds lost.",lost);log_msg(msg);
    }
    /* WOUND HEAL */
    if(rng_bool(probs[REVT_WOUND_HEAL])){
        for(int i=0;i<G.squad_count;i++){
            if(G.squads[i].wounds>0&&G.meds>0){
                G.squads[i].wounds--;G.meds--;
                if(G.squads[i].wounds==0){
                    snprintf(msg,MSG_LEN,"%s Sq: all wounds treated.",G.squads[i].name);log_msg(msg);
                }
                break;
            }
        }
    }
    /* SECTOR ASSAULT — spikes a sector threat */
    if(rng_bool(probs[REVT_SECTOR_ASSAULT])){
        int sec=rand()%4;
        G.sector_threat[sec]=clamp(G.sector_threat[sec]+rng_range(15,30),0,100);
        const char *snames[]={"A","B","C","D"};
        snprintf(msg,MSG_LEN,"Enemy pressure increasing in Sector %s. Threat rises.",snames[sec]);log_msg(msg);
    }
    /* Sector threat drift */
    for(int i=0;i<4;i++){
        /* high aggression increases sector threats */
        int drift=(G.agg>60?+1:0)-(G.agg<30?1:0);
        /* patrols/raids reduce sector threats */
        if(G.squads[i].task==TASK_PATROL||G.squads[i].task==TASK_RAID) drift-=2;
        G.sector_threat[i]=clamp(G.sector_threat[i]+drift,0,100);
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
    if(G.half_am){
        int bonus=upg_has(UPG_SIGNAL_WIRE)?1:0;
        G.cmd_points=clamp(G.cmd_points+1+bonus,0,cp_max());
    }
    record_history();
    process_evq();
    process_historical();
    apply_weather_effects();
    apply_notable_passives();
    consume();
    update_squads();
    random_events();
    G.turn++; G.half_am=!G.half_am;
    G.score=calc_score();
    check_over();
}

/* ================================================================
   INPUT HANDLER
   ================================================================ */

static int handle(Key key){
    int ns=G.squad_count,no=TASK_COUNT;
    if(G.orders_mode){
        switch(key){
        case KEY_LEFT:case KEY_UP:   G.osel=(G.osel+no-1)%no; break;
        case KEY_RIGHT:case KEY_DOWN:G.osel=(G.osel+1)%no;    break;
        case KEY_ENTER:{Squad *sq=G.sel<ns?&G.squads[G.sel]:NULL;
            if(sq){Task t=ORDER_OPTS[G.osel];sq->task=t;
                char m[MSG_LEN];snprintf(m,MSG_LEN,"Orders: %s Sq — %s.",sq->name,TASK_DEFS[t].name);log_msg(m);}
            G.orders_mode=0;break;}
        case KEY_ESC:G.orders_mode=0;break;
        default:break;
        }
    } else {
        switch(key){
        case KEY_UP:case KEY_LEFT:  G.sel=(G.sel+ns-1)%ns; break;
        case KEY_DOWN:case KEY_RIGHT:G.sel=(G.sel+1)%ns;   break;
        case KEY_SPACE: end_turn(); break;
        case KEY_O: G.orders_mode=1;G.osel=0; break;
        case KEY_C: screen_command();  break;
        case KEY_R: screen_resources(); break;
        case KEY_I: screen_intel();    break;
        case KEY_D: screen_dossier(G.sel); break;
        case KEY_Q: return 2;
        case KEY_ESC: return 1;
        default:break;
        }
    }
    return 0;
}

/* ================================================================
   MAIN
   ================================================================ */

int main(void){
    srand((unsigned)time(NULL));
    raw_on(); cur_off();

main_menu:;
    int choice=screen_main_menu();
    if(choice==5) goto done;
    if(choice==2){screen_how_to_play(); goto main_menu;}
    if(choice==3){screen_codex(0);      goto main_menu;}
    if(choice==4){screen_credits();     goto main_menu;}
    if(choice==1){
        int slot=screen_save_load(0); if(slot<0) goto main_menu;
        if(!load_slot(slot)){add_msg("Failed to load.");goto main_menu;}
    } else {
        Difficulty d=screen_difficulty(); new_game(d);
    }

    while(!G.over){
        render();
        if(G.dispatch_pending>=0){
            int idx=G.dispatch_pending; G.dispatch_pending=-1;
            int comply=screen_hq_dispatch(idx);
            apply_dispatch(idx,comply);
        }
        Key k=getch(); int r=handle(k);
        if(r==2) goto main_menu;
        if(r==1){
            int pm=screen_pause_menu();
            switch(pm){
            case 0: break;
            case 1: screen_how_to_play();  break;
            case 2:{int s=screen_save_load(1);if(s>=0){if(save_slot(s))add_msg("Game saved.");else add_msg("Save failed.");}break;}
            case 3:{int s=screen_save_load(0);if(s>=0&&!load_slot(s)) add_msg("Load failed.");break;}
            case 4: screen_codex(0);       break;
            case 5: screen_diary();        break;
            case 6: screen_dossier(G.sel); break;
            case 7: screen_upgrades();     break;
            case 8: goto main_menu;
            }
        }
    }
    if(G.over) screen_end();
    goto main_menu;

done:
    cls();cur_on();raw_off();
    printf("\nBurden of Command — Thank you for playing.\n"
           "Auf Wiedersehen, Captain Thorne.\n\n");
    fflush(stdout);
    return 0;
}
