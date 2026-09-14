/*
 * STC8H1K17T drying-box heater/UI build.
 * IMPORTANT: this firmware repurposes the ORIGINAL FAN OUTPUT H2 as the
 * heater output. H3 is wired to U2 physical pin 6 (TCap), not a GPIO;
 * never connect the heater to H3 on this PCB revision.
 */
__sfr __at(0x89) TMOD; __sfr __at(0x8a) TL0; __sfr __at(0x8c) TH0;
__sfr __at(0x8e) AUXR; __sfr __at(0x90) P1; __sfr __at(0x91) P1M1;
__sfr __at(0x92) P1M0; __sfr __at(0xb0) P3; __sfr __at(0xb1) P3M1;
__sfr __at(0xb2) P3M0; __sfr __at(0xba) P_SW2;
__sfr __at(0xbc) ADC_CONTR; __sfr __at(0xbd) ADC_RES;
__sfr __at(0xbe) ADC_RESL; __sfr __at(0xde) ADCCFG;
__xdata __at(0xfea8) unsigned char ADCTIM;

/* PCB OLED pins are U2 physical 3/4 = P1.6/P1.7 on the fitted T variant. */
__sbit __at(0x96) SDA; __sbit __at(0x97) SCL;
__sbit __at(0x95) BUZZER;   /* U2 physical pin 2 = P1.5, drives Q3 */
__sbit __at(0x93) HEATER;   /* U2 physical pin 5 = P1.3, drives Q1/H2 */
__sbit __at(0xb2) KEY_MINUS; /* SW3 */
__sbit __at(0xb3) KEY_MODE;  /* SW1 */
__sbit __at(0xb4) KEY_PLUS;  /* SW2 */
__sbit __at(0xb5) LED;
__sbit __at(0x8c) TR0; __sbit __at(0xa9) ET0; __sbit __at(0xaf) EA;

static volatile unsigned char tick5, second_event;
static unsigned char address, mode, set_temp = 50, set_minutes = 30;
static unsigned char beep_ticks, alarm_edges, alarm_tick;
static unsigned int control_temp10 = 0xffff;
static unsigned char fault_latched;
static unsigned long remaining;
static unsigned int temp10 = 0xffff;
static unsigned char invalid_temp_count;
/* 5 s time-proportion heater output: 1000 slots of 5 ms. */
static volatile unsigned int heater_duty, heater_phase;
static volatile unsigned char heater_enabled;
static signed int pid_integral8, pid_slope4;
static unsigned int pid_last_temp;

/* ASCII 0x20..0x5a, 5x7 font. */
static const __code unsigned char font[] = {
0,0,0,0,0, 0,0,0x5f,0,0, 0,7,0,7,0, 0x14,0x7f,0x14,0x7f,0x14,
0x24,0x2a,0x7f,0x2a,0x12, 0x23,0x13,8,0x64,0x62, 0x36,0x49,0x55,0x22,0x50,
0,5,3,0,0, 0,0x1c,0x22,0x41,0, 0,0x41,0x22,0x1c,0,
0x14,8,0x3e,8,0x14, 8,8,0x3e,8,8, 0,0x50,0x30,0,0,
8,8,8,8,8, 0,0x60,0x60,0,0, 0x20,0x10,8,4,2,
0x3e,0x51,0x49,0x45,0x3e, 0,0x42,0x7f,0x40,0,
0x42,0x61,0x51,0x49,0x46, 0x21,0x41,0x45,0x4b,0x31,
0x18,0x14,0x12,0x7f,0x10, 0x27,0x45,0x45,0x45,0x39,
0x3c,0x4a,0x49,0x49,0x30, 1,0x71,9,5,3,
0x36,0x49,0x49,0x49,0x36, 6,0x49,0x49,0x29,0x1e,
0,0x36,0x36,0,0, 0,0x56,0x36,0,0, 8,0x14,0x22,0x41,0,
0x14,0x14,0x14,0x14,0x14, 0,0x41,0x22,0x14,8, 2,1,0x51,9,6,
0x32,0x49,0x79,0x41,0x3e, 0x7e,0x11,0x11,0x11,0x7e,
0x7f,0x49,0x49,0x49,0x36, 0x3e,0x41,0x41,0x41,0x22,
0x7f,0x41,0x41,0x22,0x1c, 0x7f,0x49,0x49,0x49,0x41,
0x7f,9,9,9,1, 0x3e,0x41,0x49,0x49,0x7a, 0x7f,8,8,8,0x7f,
0,0x41,0x7f,0x41,0, 0x20,0x40,0x41,0x3f,1,
0x7f,8,0x14,0x22,0x41, 0x7f,0x40,0x40,0x40,0x40,
0x7f,2,0x0c,2,0x7f, 0x7f,4,8,0x10,0x7f, 0x3e,0x41,0x41,0x41,0x3e,
0x7f,9,9,9,6, 0x3e,0x41,0x51,0x21,0x5e, 0x7f,9,0x19,0x29,0x46,
0x46,0x49,0x49,0x49,0x31, 1,1,0x7f,1,1, 0x3f,0x40,0x40,0x40,0x3f,
0x1f,0x20,0x40,0x20,0x1f, 0x3f,0x40,0x38,0x40,0x3f,
0x63,0x14,8,0x14,0x63, 7,8,0x70,8,7, 0x61,0x51,0x49,0x45,0x43
};

/* 100k/B3950 NTC with 100k pull-up: ADC at 0,5,...105 C. */
static const __code unsigned int ntc[] = {
3156,2955,2738,2510,2278,2048,1825,1614,1419,1241,1081,
940,815,707,613,532,462,401,350,305,267,234};

unsigned char __sdcc_external_startup(void)
{
    P1 = 0xc7;                  /* safe latches, OLED released */
    P1M1 &= (unsigned char)~0xc0; P1M0 &= (unsigned char)~0xc0;
    P3 &= 0xdf; P3M1 &= 0xdf; P3M0 |= 0x20;
    return 0;
}

static void delay_i2c(void)
{ volatile unsigned char n; for (n = 0; n < 40; ++n) { } }

static void start(void)
{ SDA=1; SCL=1; delay_i2c(); SDA=0; delay_i2c(); SCL=0; }
static void stop(void)
{ SCL=0; SDA=0; delay_i2c(); SCL=1; delay_i2c(); SDA=1; delay_i2c(); }

static unsigned char send(unsigned char v)
{
    unsigned char i, ack;
    for (i=0; i<8; ++i) {
        SDA=((v&0x80)!=0); delay_i2c(); SCL=1; delay_i2c(); SCL=0; v<<=1;
    }
    SDA=1; delay_i2c(); SCL=1; delay_i2c(); ack=!SDA; SCL=0;
    return ack;
}

static unsigned char probe(unsigned char a)
{ unsigned char ok; start(); ok=send(a); stop(); return ok; }

static void command(unsigned char v)
{ start(); send(address); send(0); send(v); stop(); }

static void set_pos(unsigned char page, unsigned char col)
{
    unsigned char c=col+2;
    command(0xb0|page); command(c&15); command(0x10|(c>>4));
}

static void set_raw_pos(unsigned char page, unsigned char col)
{
    command(0xb0|page); command(col&15); command(0x10|(col>>4));
}

static void clear(void)
{
    unsigned char p,x;
    for (p=0;p<8;++p) {
        /* Clear all 132 controller columns, including the two left-edge
         * columns omitted by the usual SH1106 visible-area +2 offset. */
        set_raw_pos(p,0); start(); send(address); send(0x40);
        for (x=0;x<132;++x) send(0); stop();
    }
}

static void line(unsigned char page, const char *s)
{
    unsigned char x=0,i,ch; unsigned int k;
    set_pos(page,0); start(); send(address); send(0x40);
    while (*s && x<126) {
        ch=(unsigned char)*s++; if(ch<0x20||ch>0x5a)ch=' ';
        k=(unsigned int)(ch-0x20)*5;
        for(i=0;i<5&&x<128;++i,++x)send(font[k+i]);
        if(x<128){send(0);++x;}
    }
    while(x++<128)send(0); stop();
}

static void oled_init(void)
{
    static const __code unsigned char seq[]={
    0xae,0xd5,0x80,0xa8,0x3f,0xd3,0,0x40,0x20,2,0xa1,0xc8,0xda,0x12,
    0x81,0x7f,0xd9,0xf1,0xdb,0x40,0xa4,0xa6,0x8d,0x14,0xad,0x8b,0xaf};
    unsigned char i; for(i=0;i<sizeof(seq);++i)command(seq[i]);
    command(0xa4); clear();
}

static void timer_init(void)
{
    AUXR|=0x80; TMOD=(TMOD&0xf0)|1; TH0=0x28; TL0=0;
    ET0=1; EA=1; TR0=1;
}

void timer0_isr(void) __interrupt(1)
{
    static unsigned char n;
    TH0=0x28; TL0=0;
    if(++heater_phase>=1000u)heater_phase=0;
    HEATER=(heater_enabled && heater_phase<heater_duty);
    if(tick5<250)++tick5; /* Preserve elapsed ticks while OLED is drawing. */
    if(++n>=200){n=0;if(second_event<250)++second_event;}
}

static void adc_init(void)
{
    P1M1|=1; P1M0&=(unsigned char)~1;
    P_SW2|=0x80; ADCTIM=0x3f; P_SW2&=0x7f;
    ADCCFG=0x2f; ADC_CONTR=0x80;
}

static unsigned int adc_read(void)
{
    unsigned int timeout=60000;
    ADC_CONTR=0xc0;
    while(!(ADC_CONTR&0x20)&&--timeout){ }
    ADC_CONTR&=(unsigned char)~0x20;
    return ((unsigned int)ADC_RES<<8)|ADC_RESL;
}

static unsigned int read_temp(void)
{
    unsigned long sum=0;
    unsigned int a, sample, minimum=0xffff, maximum=0;
    unsigned char i;

    /* Discard one conversion after channel selection, then use a trimmed mean. */
    (void)adc_read();
    for(i=0;i<16;++i){
        sample=adc_read();
        sum+=sample;
        if(sample<minimum)minimum=sample;
        if(sample>maximum)maximum=sample;
    }
    a=(unsigned int)((sum-minimum-maximum+7ul)/14ul);
    if(a>3500||a<210)return 0xffff; /* NTC missing/open */
    if(a>=ntc[0])return 0;
    for(i=0;i<21;++i)if(a>=ntc[i+1])
        return (unsigned int)i*50u+(unsigned int)
        ((unsigned long)(ntc[i]-a)*50u/(ntc[i]-ntc[i+1]));
    return 1050;
}

static void update_temperature(void)
{
    unsigned int raw=read_temp();

    if(raw==0xffff){
        control_temp10=0xffff; /* Turn heater off on the first bad reading. */
        /* Do not blank the display for a single noisy/out-of-range sample. */
        if(invalid_temp_count<3)++invalid_temp_count;
        if(invalid_temp_count>=3)temp10=0xffff;
        return;
    }
    invalid_temp_count=0;

    /* Empirical board/sensor calibration: displayed temperature was +0.5 C. */
    raw=(raw>=5)?(raw-5):0;
    control_temp10=raw; /* Use unsmoothed data for the safety cutoff. */

    if(temp10==0xffff)temp10=raw;
    else {
        /* EMA alpha=1/8: suppress ADC noise and fast visual jumping. */
        temp10=(unsigned int)(((unsigned long)temp10*7ul+raw+4ul)/8ul);
    }
}

static void blank(char *s)
{ unsigned char i; for(i=0;i<21;++i)s[i]=' '; s[21]=0; }
static void text(char *s,unsigned char p,const char *v)
{ while(*v&&p<21)s[p++]=*v++; }

static void draw(void)
{
    char s[22]; unsigned int w,m,power; unsigned long shown; unsigned char sec;
    blank(s);text(s,0,"TEMP:");
    if(temp10==0xffff)text(s,6,"--.-C");
    else {w=temp10/10;s[6]=(w>=100)?'0'+(w/100)%10:' ';s[7]='0'+(w/10)%10;
          s[8]='0'+w%10;s[9]='.';s[10]='0'+temp10%10;s[11]='C';} line(0,s);
    blank(s);text(s,0,"SET :");s[6]='0'+set_temp/10;s[7]='0'+set_temp%10;s[8]='C';line(2,s);
    shown=(mode==3||mode==4)?remaining:(unsigned long)set_minutes*60ul;
    m=(unsigned int)(shown/60ul);sec=(unsigned char)(shown%60ul);
    blank(s);text(s,0,"TIME:");s[5]='0'+(m/100)%10;s[6]='0'+(m/10)%10;
    s[7]='0'+m%10;s[8]=':';s[9]='0'+sec/10;s[10]='0'+sec%10;line(4,s);
    blank(s);text(s,0,"STATE:");
    if(mode==0)text(s,6,"READY");else if(mode==1)text(s,6,"SET TEMP");
    else if(mode==2)text(s,6,"SET TIME");
    else if(mode==3){
        text(s,6,"HEAT ");
        EA=0;power=heater_duty;EA=1;
        power=(power+5u)/10u;
        s[11]='0'+(power/100u)%10u;
        s[12]='0'+(power/10u)%10u;
        s[13]='0'+power%10u;s[14]='%';
    }
    else if(mode==4)text(s,6,"DONE");
    else text(s,6,"FAULT"); line(6,s);
}

static void beep_short(void)
{
    alarm_edges=0; alarm_tick=0;
    BUZZER=1; beep_ticks=20; /* 100 ms */
}

static void alarm_three_beeps(void)
{
    beep_ticks=0; alarm_tick=0;
    BUZZER=1; alarm_edges=5; /* Three 200 ms tones. */
}

static void buzzer_service(void)
{
    if(beep_ticks){
        if(!--beep_ticks)BUZZER=0;
    } else if(alarm_edges && ++alarm_tick>=40){
        alarm_tick=0; BUZZER=!BUZZER;
        if(!--alarm_edges)BUZZER=0;
    }
}

static void heater_off(void)
{
    heater_enabled=0;HEATER=0;
}

/* Conservative fixed-point PID, updated once per second. Raw temperature is
 * used for control to avoid the display filter's ~8 s thermal lag. */
static void pid_update(void)
{
    signed int error, rise, output;
    unsigned int duty;

    if(control_temp10==0xffff)return;
    error=(signed int)((unsigned int)set_temp*10u)-(signed int)control_temp10;
    rise=(signed int)control_temp10-(signed int)pid_last_temp;
    pid_last_temp=control_temp10;
    if(rise>20)rise=20;else if(rise< -20)rise= -20;
    pid_slope4=(signed int)((pid_slope4*3+rise*4)/4);

    if(error>=50){ /* More than 5 C below target: warm up at full power. */
        pid_integral8=0;duty=1000;
    } else if(error<= -20){ /* More than 2 C above target: no heating. */
        pid_integral8=0;duty=0;
    } else {
        /* P: 14 permille/0.1 C; I: 1/8 permille/0.1 C/s;
         * D: 8 permille per quarter-degree/s slope unit. */
        output=250+error*14+pid_integral8/8-pid_slope4*8;
        if(error<=20 && output>0 && output<1000){
            pid_integral8+=error;
            if(pid_integral8>4000)pid_integral8=4000;
            if(pid_integral8< -4000)pid_integral8= -4000;
            output=250+error*14+pid_integral8/8-pid_slope4*8;
        }
        if(output<0)duty=0;
        else if(output>1000)duty=1000;
        else duty=(unsigned int)output;
    }
    EA=0;heater_duty=duty;EA=1;
}

static void heater_service(void)
{
    if(mode!=3 || fault_latched){heater_off();return;}
    if(control_temp10==0xffff || control_temp10>=850u){
        heater_off();fault_latched=1;mode=5;alarm_three_beeps();return;
    }

    if(!heater_enabled){
        pid_integral8=0;pid_slope4=0;pid_last_temp=control_temp10;
        pid_update();
        EA=0;heater_phase=0;heater_enabled=1;EA=1;
    }
}

static void adjust(signed char d)
{
    if(mode==1){if(d>0&&set_temp<80)++set_temp;if(d<0&&set_temp>30)--set_temp;}
    if(mode==2){if(d>0&&set_minutes<=175)set_minutes+=5;
                if(d<0&&set_minutes>5)set_minutes-=5;}
}

static unsigned char keys(void)
{
    static unsigned char cm,cp,cn,long_done; unsigned char changed=0;
    if(!KEY_MODE){if(cm<250)++cm;if(cm>=200&&!long_done){long_done=1;
        if(mode==3)mode=0;
        else if(!fault_latched && control_temp10!=0xffff){
            remaining=(unsigned long)set_minutes*60ul;mode=3;
        }
        beep_short();changed=1;}}
    else {if(cm>=4&&!long_done){if(mode==0||mode==4)mode=1;else if(mode==1)mode=2;
          else mode=0;changed=1;}cm=0;long_done=0;}
    if(!KEY_PLUS){if(cp<250)++cp;if(cp==4||(cp>100&&!(cp%20))){adjust(1);changed=1;}}
    else cp=0;
    if(!KEY_MINUS){if(cn<250)++cn;if(cn==4||(cn>100&&!(cn%20))){adjust(-1);changed=1;}}
    else cn=0;
    return changed;
}

void main(void)
{
    unsigned char redraw=1; volatile unsigned int d;
    P1M1|=0xc0;P1M0|=0xc0;P1|=0xc0;
    P1M1&=(unsigned char)~0x08;P1M0|=0x08;HEATER=0;
    P1M1&=(unsigned char)~0x20;P1M0|=0x20;BUZZER=0;
    P3M1|=0x1c;P3M0&=(unsigned char)~0x1c;P3|=0x1c;LED=0;
    for(d=0;d<60000;++d){ }
    if(probe(0x78))address=0x78;else if(probe(0x7a))address=0x7a;
    else for(;;){LED=!LED;for(d=0;d<60000;++d){ }}
    oled_init();adc_init();update_temperature();timer_init();LED=1;beep_short();
    for(;;){
        if(!tick5)continue;
        EA=0;--tick5;EA=1;
        buzzer_service();if(keys())redraw=1;
        heater_service(); /* A button press changes the output without delay. */
        if(second_event){
            EA=0;--second_event;EA=1;
            update_temperature();
            if(mode==3&&remaining){--remaining;
                if(!remaining){mode=4;heater_off();alarm_three_beeps();}
                else if(control_temp10!=0xffff && control_temp10<850u)
                    pid_update();}
            redraw=1;
        }
        heater_service(); /* Also react immediately to a new NTC reading. */
        if(redraw){redraw=0;draw();}
    }
}
