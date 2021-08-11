//Authors: twitter @gigstaggart && @see_ess
// built using the arduino IDE, instructions on how to flash new code: https://wiki.seeedstudio.com/Seeeduino-XIAO/#getting-started
//
//License:
//           DO WHAT THE FUCK YOU WANT TO PUBLIC LICENSE
//                   Version 2, December 2004
//
//Copyright (C) 2004 Sam Hocevar <sam@hocevar.net>
//
//Everyone is permitted to copy and distribute verbatim or modified
//copies of this license document, and changing it is allowed as long
//as the name is changed.
//
//           DO WHAT THE FUCK YOU WANT TO PUBLIC LICENSE
//  TERMS AND CONDITIONS FOR COPYING, DISTRIBUTION AND MODIFICATION
//
// 0. You just DO WHAT THE FUCK YOU WANT TO.
// ---------------------

//XIAO pinout https://files.seeedstudio.com/wiki/Seeeduino-XIAO/img/Seeeduino-XIAO-pinout.jpg
#define GSRPIN A0        //A0  - GSR sensor, analog in
#define VREGENABLE A1    //A1  - PWR_EN output pin to sink 3.3 vreg enable pin
#define HBPIN A2         //A2  - HB sensor analog in
#define HBLED A3         //A3  - Heartbeat sensor green LED output (sink 1=off 0=on)
#define SDA_PIN -1       //A4  - SDA (i2c) set to -1 to use HW i2c and not bitbang (either way the displays peak at 16khz)
#define SCL_PIN -1       //A5  - SCL (i2c) set to -1 to use HW i2c and not bitbang (either way the displays peak at 16khz)
#define BATTSENSE A6     //A6  - uart/rx //5v vreg input for batt sense
#define UPBUTTON A7      //A7  - button up
#define LEFTBUTTON A8    //A8  - button left
#define DOWNBUTTON A9    //A9  - button down
#define RIGHTBUTTON A10  //A10 - button right

//-----------------------------------------------------------------
#define CODE_VERSION 6
#include <ss_oled.h> //https://github.com/bitbank2/ss_oled
#include <Adafruit_SleepyDog.h> //https://github.com/adafruit/Adafruit_SleepyDog
#include <FlashAsEEPROM_SAMD.h> //https://github.com/khoih-prog/FlashStorage_SAMD
#define RESET_PIN -1 // Set this to -1 to disable the GPIO pin number connected to the reset line of your display
#define OLED_ADDR_LEFT 0x3c //0x3c/0x78 0011 1100 stock screen (left)
#define OLED_ADDR_RIGHT 0x3d //0x3d/0x7a 0011 1101 reistor swapped (right)
#define FLIP180 0 // don't rotate the display
#define INVERT 0  // don't invert the display
#define OLEDRESOLUTION OLED_132x64 //it is actually 128x64 but setting that causes everything to shift
#define HBSAMPLESPERSEC 61 //the number of HB samples we read / sec, this has to be accurate to calculate BPM correctly
#define HBHISTORICALREADINGS HBSAMPLESPERSEC*2 //the number of historical hb readings we are storing in the hbsamplebuffer[]
#define NUMHISTORICALHB 8 //the number of historical heartbeat peaks we store
#define THRESHOLDABOVEAVG 120 //so the minor peak isn't detected as its own peak during a HB
#define GSRREADINGSPERLOOP 12 //take this many GSR readings each main loop, helps reduce noise

SSOLED ssoled[2]; //2 copies of the SSOLED structure. Each structure is about 56 bytes
//buffer holds 8 rows of pixels per array index (LSB=top), starting at top left, moving right, and wrapping down 8 pixels to the block of pixels
static uint8_t zerobuffer[1024]; //128*64/8
static uint8_t onebuffer[1024];  //128*64/8
static uint8_t zerobackbuffer[1024]; //128*64/8 
static uint8_t onebackbuffer[1024];  //128*64/8

int battsensevalue; //current batt sense value, used to shut off batt vreg when on USB
short gsrvalue; //current gsr reading
short lastgsrvalue; //last gsr reading
short gsrdelta[30]; //used to calculate the difference between the current reading and 10 readings ago
byte gsrdeltaindex = 0; //the index of where to insert the next gsr reading into gsrdelta[]
short gsrmin = 0; //used to scale the graph
short gsrmax = 54; //used to scale the graph
short hbvalue;//current hb reading
byte lasthbvalue;//last hb reading (0-55)
byte loopval = 0; //used to track how many times we've been through the main loop.
boolean rightdebounce = 0; //0=if button was just pressed or not pressed, 1=if being held down
boolean leftdebounce = 0; //0=if button was just pressed or not pressed, 1=if being held down
boolean updebounce = 0; //0=if button was just pressed or not pressed, 1=if being held down
boolean downdebounce = 0; //0=if button was just pressed or not pressed, 1=if being held down
byte screenbrightness = 52; //brightness, 50=low 51=med 52=high
short runmode = 0; //10-99 different menu modes (see updateMode()), 100=lie detector, 101=fake pulse
short tempmode = runmode; //used when switching between modes
short totalreadingsbetweenhbs = 0; //used when adding up the distance between HB peaks detected
byte validhbsdetected = 0; //number of valid heartbeats we've detected in our buffer
byte bpm = 0; //heart beats per minute
byte lastbpm = 0; //previous loop's heart beats per min
unsigned short samplessincelastbeat = 0; //samples since the last hb, <2*HBSAMPLESPERSEC==valid, 2*HBSAMPLESPERSEC==clearBPM, 1+2*HBSAMPLESPERSEC==stop incrementing
short hbsamplearrayindex = 0; //index to insert new hbs raw samples into the hbsamplebuffer[] array
short hbsamplebuffer[HBHISTORICALREADINGS]; //buffer of raw ADC readings for the HB
int hbhistoricalavg = 0; //the average of the values in the array
boolean hbrising = true; //if the hb raw samples are still rising (peak not detected yet)
short sampleindex3prior = 0;
byte hbdetectedindex = 0; //index to insert the samples between each hb
byte hbdetected[NUMHISTORICALHB]; //after we detect a hb peak, store the distance between the current hb and the last hb we detected
byte bpmdelta[10]; //record the average bpm calculation in a historical buffer used for delta calculations
byte bpmdeltaindex = 0; //index to reference bpmdelta[]
char tempstring[5]; //used to convert values for oled display
boolean pulseledstatus = false; //the state of the pulse led/heart icon, used to limit the display changes
boolean enablefakepulseled = true; //in fake pulse mode, enable or disable the blinking leds
byte tmpindx; //used in the guts of the hb detection
byte scrollspeed = 1; //the speed of the scroll mode 0,1,2,4,8,16,32
byte konami = 0; //don't worry about this little guy
byte currentadvice = 0; //current 'bad defcon advice' so backing out and re-entering it resumes at the current value
const char *onezero = "0"; //used for 2 digit gsr output
const char *twozeros = "00"; //used for 1 digit gsr output
unsigned long pretime; //used for micros() and millis()
unsigned long posttime; //used for micros() and millis()
unsigned long boottime; //used for uptime
unsigned long uptime = 0; //used to calc uptime
boolean cheatsenabled = false; //used to enable / disable cheats
byte triggerfakelie = 0; //which sample we are in during a cheat to trigger a fake lie, 0 cheats off, 1-31 fake lie, 100 prevent lies
short cheatgsrbaseline = 0; //used in gsr cheat, gsr value when cheat was triggered
byte cheatgsrvalues[30]={1,2,3,3,5,7,7,9,12,13,16,18,20,22,23,25,26,28,29,29,29,30,30,30,31,32,31,32,32,32};//when cheat is used, modify the gsr value by this much.
char enteredname[17]; //for name scroll
byte currententrychar = 0;//what character of the name they are entering 0-15
char scrollchar = 65;//for namescroll, what char we are scrolling to, valid vals 32-126 (starts at ascii 0x20 space)
boolean namescrollupdatescreen1 = false;
boolean namescrollupdatescreen0 = false;
byte namescrollpixelposition = 0;//pixel position of the string we are inserting on the right side of the screen
boolean allcharspainted = false; //to determine if we've painted all the characters to the screen in namescroll mode, once we have we will just move pixels around and not read from the charmap
boolean entrylockout = true; //don't count the button press that is used to enter name scroll entry mode, needed because we're acting on button up not button down. (short press == new char, long press == finished entering all chars)
uint16_t storedAddress = 0; //used for fake eeprom (flash storage)
int signature; //used to see if flash has been written already
const int WRITTEN_SIGNATURE = 0xBEEFDEED; //flash/eeprom stuff

static uint8_t staticlogobuffer[1024]={0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,//36 whitespace
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,B00000001,B00001111,B11111111,B11100010,B00000110,B00000100,B00001100,B00001000,B00011000,B11110000,B11110000,B11110000,B11100000,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,//END ROW 1
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,//72 whitespace
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,B10000000,B10000000,B11000000,B11100000,B11100000,B01111000,B00111111,B00001111,0,0,0,0,B10000000,B00001111,B00111111,B11111111,B11111111,B11111110,B11111000,B11100000,B11000000,B11000000,B10000000,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,//72 whitespace
0,0,0,0,0,0,B10000000,B11000000,B11100000,B11110000,B01110000,B00111000,B00111100,B00011110,B00001110,B00000111,B00000111,B00000011,B00000001,B00000001,0,B10000000,B11000000,B01100000,B00011100,B00000011,0,0,B00111111,B11100000,0,0,B00000011,B00001111,B00111111,B11111111,B11111111,B11111111,B11111111,B11111111,B11111111,B11111110,B11111110,B11111100,B11111000,B11111000,B11110000,B11100000,B11000000,B10000000,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,//72 whitespace
0,B10000000,B11100000,B11111000,B00111100,B00011111,B00000111,B00000011,B00000001,0,0,B10000000,B11000000,B01100000,B00110000,B00011000,B00001000,B00000100,B00000110,B00000011,B00000001,B00000001,0,0,B11000000,B01110000,0,0,B11110000,0,B00000011,B00001100,B00111000,B11000000,0,B00000001,B00001111,B11111111,B11111111,B11111111,B11111111,B11111111,B11111111,B11111111,B11111111,B11111111,B11111111,B11111111,B11111111,B11111111,B11111111,B11111100,B11111000,B11100000,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,//72 whitespace
B11111100,B11111111,B00001111,B00000001,0,0,0,B11000000,B01111000,B00001110,B00000011,B00000001,0,0,0,B10000000,B11000000,B01100000,B00110000,B00011000,B00001100,B00000100,B00000110,B00000011,B00000001,0,0,0,B00000001,B00000110,B00011100,B11110000,B10000000,B00000001,B00111111,B11110000,0,0,B00011111,B11111111,B11111111,B11111111,B11111111,B11111111,B11111111,B11111111,B11111111,B11111111,B11111111,B11111111,B11111111,B11111111,B11111111,B11111111,B11111111,B11111000,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,//72 whitespace
B00111111,B11111111,B11110000,B10000000,0,0,0,B00111111,B11100000,0,0,0,0,B11111100,B00000111,B00000001,0,0,0,0,0,0,B11000000,B01110000,B00011000,B00000110,0,0,B00011111,B11110000,0,0,B10001111,B11111000,0,B10000011,B11111110,0,0,B11111111,B11111111,B11111111,B11111111,B11111111,B11111111,B11111111,B11111111,B11111111,B11111111,B11111111,B11111111,B11111111,B11111111,B11111111,B11111111,B00011111,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,//72 whitespace
0,B00000001,B00000111,B00011111,B00111110,B01111000,B11110000,B11000000,B10000001,B00000111,B00011100,B00110000,B11100000,B10001111,B00111100,B01100000,B11000000,B10000000,0,0,0,B00111110,B11100011,B10000000,0,0,0,0,0,B11111111,0,B11110000,B00011111,B10000000,B11111100,B00000111,B10000000,B11100000,B11111110,B11111111,B11111111,B11111111,B11111111,B11111111,B11111111,B11111111,B11111111,B11111111,B11111111,B11111111,B01111111,B00111111,B00011111,B00000111,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,//72 whitespace
0,0,0,0,0,0,0,B00000001,B00000011,B00000111,B00001110,B00011100,B00011100,B00111001,B00110011,B01110110,B01111100,B01111001,B11110011,B11100110,B11101100,B11111000,B11110000,B11100011,B11101110,B11111000,B11110000,B11110000,B11111111,B11111001,B11111100,B11111111,B11111110,B11111111,B11111111,B11111111,B11111111,B11111111,B01111111,B01111111,B01111111,B00111111,B00111111,B00011111,B00011111,B00001111,B00000111,B00000011,B00000011,B00000001,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};//36 white space

static char* advicearray[46][6] = {
  {"","   Bring your work"," laptop to DEFCON to"," test your corporate","      security","   infrastructure"}, //0
  {"","Tell everyone you are","  a hacker and you","  learned all your","skills from CSI-Cyber","    and Scorpion"}, //1
  {"","    Only the most"," paranoid people use","   a burner phone","",""}, //2
  {""," If you're at DEFCON"," on the company dime,","   corporate will","  reimburse you for","  hookers and drugs"}, //3
  {"","Paste things you find","on Twitter into your","  terminal, nothing","   bad will happen",""}, //4
  {"","  If you eat enough","  garlic you don't","   need to shower","",""}, //5
  {""," Need some sunscreen","in Vegas? Take a swim"," in the closest pool","",""}, //6
  {"","Always use your gmail","account when signing","   up for parties","",""}, //7
  {"",""," Hand sanitizer can","also be used as lube","",""}, //8
  {"","  That other hotel,","bar, or restaurant is","totally right next to","   DEFCON, you can","  easily walk to it"}, //9
  {"","Legally, if you hail"," a vegas cab on the","street they must pick","       you up",""}, //10
  {"","Book your room at the"," Stratosphere. It's"," walking distance to","   the conference",""}, //11
  {"",""," Invite strangers to"," your room to party","",""}, //12
  {""," The 3-2-1 rule is:"," 3 shots at the bar,","2 casino locks picked"," and 1 night in jail",""}, //13
  {"","","  Take pictures of","  everyone you meet","",""}, //14
  {"","  DEFCON is a very"," formal event, wear","   a suit and tie","",""}, //15
  {"","Keep your vaccination","card and credit card","visible at all times","   for full access",""}, //16
  {"",""," You can rely on one"," pair of flip-flops","",""}, //17
  {"","Carry a laser pointer"," for 'Spot the Fed'.","It's the way to spot","  them and everyone","      will clap"}, //18
  {"","Avoid drinking water,","  stay dehydrated.","Dehydration means no","  sweating and less","    deodorant use"}, //19
  {"","  Don't worry about","phone charging. There","will be a lot of free","USB in charging ports","      at DEFCON"}, //20
  {""," If police question"," you for your public","intoxication, remind","them that you're the","one that's in charge"}, //21
  {"Feds are required to","identify themselves,"," they get a special"," badge and are never"," allowed in villages"," or the contest area"}, //22
  {""," Be sure to use your","Wi-Fi Pineapple right","   out of the box","",""}, //23
  {"","","  Eat Sbarro at 3AM","","",""}, //24
  {"","Stare at your mobile","phone and stop in the","middle of the hallway"," with people walking","     behind you"}, //25
  {"","","Beer is mostly water,"," use it to hydrate","",""}, //26
  {"","  Wear all of your"," clothes at the same","time and save on bag","fees, it is not that","    hot in Vegas"}, //27
  {"","That ATM wasn't there","  yesterday, but it","   should be safe","",""}, //28
  {"","  When introducing"," yourself, use your","  full name. No one","uses handles anymore",""}, //29
  {""," Leave your devices"," unlocked, we're all"," trustworthy people!","",""}, //30
  {"","Only go to corporate"," parties to exchange","   business cards","",""}, //31
  {"","Ramble about how much"," more you know about"," the presentation's","   topic than the","   presenter does"}, //32
  {""," Post your physical","address in the DEFCON","forums so people can","  send you stickers",""}, //33
  {""," The tamper evident","contest will be held","    at your local","   police station",""}, //34
  {"","   They don't mark","  crosswalks on the","vegas strip, just hop","  the barricade and","    dodge traffic"}, //35
  {""," New DEFCON drinking","  game: take a shot"," every time you see"," a guy with a beard","      in a kilt"}, //36
  {""," Locking your laptop","  if you walk away","    is for chumps","",""}, //37
  {""," The goons will tell","  you if the Wi-Fi","      is secure","",""}, //38
  {"","Other attendees will","  appreciate wakeup"," calls and thumping"," dance tracks at 7AM","  on Sunday morning"}, //39
  {""," Park on the roof of"," the parking garage,","   the soda on the"," your car dashboard","    will be fine"}, //40
  {"","  If security tries","  to come into your","room at DEFCON, just","   loudly ask them","AM I BEING DETAINED?"}, //41
  {""," The best pool party","  is always at the","    rooftop pool","",""}, //42
  {""," Smoking kills, you","should inhale solder","    fumes instead","",""}, //43
  {"","","Putting on deodorant"," counts as showering","",""}, //44
  {"","If DEFCON is handing","  out paper badges,"," don't photocopy it","  for your friends",""}, //45
};//end advicearray[]

const uint8_t ucBigFont[] = {//stolen from ss_oled, but I fixed some errors on symbol characters: https://github.com/bitbank2/ss_oled/pull/50 each char is 16 pixels/bytes wide, and 4 rows (of 8 pixels) high = 48 bytes per char. [0]=space [32]=@ etc
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0xfc,0xfc,0xff,0xff,0xff,0xff,0xfc,0xfc,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x3f,0x3f,0x3f,0x3f,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x0f,0x0f,0x0f,0x0f,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x0f,0x0f,0x3f,0x3f,0x00,0x00,0x00,0x00,0x3f,0x3f,0x0f,0x0f,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0xc0,0xc0,0xfc,0xfc,0xfc,0xfc,0xc0,0xc0,0xfc,0xfc,0xfc,0xfc,0xc0,0xc0,0x00,0x00,0xc0,0xc0,0xff,0xff,0xff,0xff,0xc0,0xc0,0xff,0xff,0xff,0xff,0xc0,0xc0,0x00,0x00,0x00,0x00,0x0f,0x0f,0x0f,0x0f,0x00,0x00,0x0f,0x0f,0x0f,0x0f,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0xf0,0xf0,0xf0,0xf0,0x00,0x00,0x00,0x00,0x00,0x00,0xfc,0xfc,0xff,0xff,0x03,0x03,0x03,0x03,0x03,0x03,0x0f,0x0f,0x3c,0x3c,0x00,0x00,0xf0,0xf0,0xc3,0xc3,0x03,0x03,0x03,0x03,0x03,0x03,0xff,0xff,0xfc,0xfc,0x00,0x00,0x00,0x00,0x03,0x03,0x03,0x03,0x3f,0x3f,0x3f,0x3f,0x03,0x03,0x00,0x00,0x00,0x00,
  0xf0,0xf0,0xf0,0xf0,0x00,0x00,0x00,0x00,0x00,0x00,0xc0,0xc0,0xf0,0xf0,0x00,0x00,0x00,0x00,0xc0,0xc0,0xf0,0xf0,0x3c,0x3c,0x0f,0x0f,0x03,0x03,0x00,0x00,0x00,0x00,0x0f,0x0f,0x03,0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x0f,0x0f,0x0f,0x0f,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x3c,0x3c,0xff,0xff,0xc3,0xc3,0xff,0xff,0x3c,0x3c,0x00,0x00,0x00,0x00,0xfc,0xfc,0xff,0xff,0x03,0x03,0x0f,0x0f,0xfc,0xfc,0xff,0xff,0x03,0x03,0x00,0x00,0x03,0x03,0x0f,0x0f,0x0c,0x0c,0x0c,0x0c,0x03,0x03,0x0f,0x0f,0x0c,0x0c,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0xc0,0xc0,0xc0,0xc0,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x30,0x30,0x3f,0x3f,0x0f,0x0f,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0xf0,0xf0,0xfc,0xfc,0x0f,0x0f,0x03,0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x03,0x03,0x0f,0x0f,0x0c,0x0c,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x03,0x03,0x0f,0x0f,0xfc,0xfc,0xf0,0xf0,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x0c,0x0c,0x0f,0x0f,0x03,0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0xc0,0xc0,0xc0,0xc0,0x00,0x00,0x00,0x00,0xc0,0xc0,0xc0,0xc0,0x00,0x00,0x0c,0x0c,0xcc,0xcc,0xff,0xff,0x3f,0x3f,0x3f,0x3f,0xff,0xff,0xcc,0xcc,0x0c,0x0c,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0xc0,0xc0,0xc0,0xc0,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x0c,0x0c,0x0c,0x0c,0xff,0xff,0xff,0xff,0x0c,0x0c,0x0c,0x0c,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xc0,0xc0,0xc0,0xc0,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x30,0x30,0x3f,0x3f,0x0f,0x0f,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x0c,0x0c,0x0c,0x0c,0x0c,0x0c,0x0c,0x0c,0x0c,0x0c,0x0c,0x0c,0x0c,0x0c,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x0f,0x0f,0x0f,0x0f,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xc0,0xc0,0xf0,0xf0,0x00,0x00,0x00,0x00,0xc0,0xc0,0xf0,0xf0,0x3c,0x3c,0x0f,0x0f,0x03,0x03,0x00,0x00,0x00,0x00,0x0f,0x0f,0x03,0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0xfc,0xfc,0xff,0xff,0x03,0x03,0x03,0x03,0xc3,0xc3,0xff,0xff,0xfc,0xfc,0x00,0x00,0xff,0xff,0xff,0xff,0x30,0x30,0x0f,0x0f,0x00,0x00,0xff,0xff,0xff,0xff,0x00,0x00,0x03,0x03,0x0f,0x0f,0x0c,0x0c,0x0c,0x0c,0x0c,0x0c,0x0f,0x0f,0x03,0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x30,0x30,0x3c,0x3c,0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x0c,0x0c,0x0c,0x0c,0x0f,0x0f,0x0f,0x0f,0x0c,0x0c,0x0c,0x0c,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x0c,0x0c,0x0f,0x0f,0x03,0x03,0x03,0x03,0xc3,0xc3,0xff,0xff,0x3c,0x3c,0x00,0x00,0xc0,0xc0,0xf0,0xf0,0x3c,0x3c,0x0f,0x0f,0x03,0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x0f,0x0f,0x0f,0x0f,0x0c,0x0c,0x0c,0x0c,0x0c,0x0c,0x0f,0x0f,0x0f,0x0f,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x0c,0x0c,0x0f,0x0f,0x03,0x03,0x03,0x03,0x03,0x03,0xff,0xff,0xfc,0xfc,0x00,0x00,0x00,0x00,0x00,0x00,0x03,0x03,0x03,0x03,0x03,0x03,0xff,0xff,0xfc,0xfc,0x00,0x00,0x03,0x03,0x0f,0x0f,0x0c,0x0c,0x0c,0x0c,0x0c,0x0c,0x0f,0x0f,0x03,0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0xc0,0xc0,0xf0,0xf0,0x3c,0x3c,0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,0x0f,0x0f,0x0f,0x0f,0x0c,0x0c,0x0c,0x0c,0xff,0xff,0xff,0xff,0x0c,0x0c,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x0c,0x0c,0x0f,0x0f,0x0f,0x0f,0x0c,0x0c,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0xff,0xff,0xff,0xff,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x00,0x00,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x0f,0x0f,0xff,0xff,0xfc,0xfc,0x00,0x00,0x03,0x03,0x0f,0x0f,0x0c,0x0c,0x0c,0x0c,0x0c,0x0c,0x0f,0x0f,0x03,0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0xf0,0xf0,0xfc,0xfc,0x0f,0x0f,0x03,0x03,0x03,0x03,0x00,0x00,0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0x03,0x03,0x03,0x03,0x03,0x03,0xff,0xff,0xfc,0xfc,0x00,0x00,0x03,0x03,0x0f,0x0f,0x0c,0x0c,0x0c,0x0c,0x0c,0x0c,0x0f,0x0f,0x03,0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x0f,0x0f,0x0f,0x0f,0x03,0x03,0x03,0x03,0x03,0x03,0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,0x00,0x00,0xf0,0xf0,0xfc,0xfc,0x0f,0x0f,0x03,0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x0f,0x0f,0x0f,0x0f,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0xfc,0xfc,0xff,0xff,0x03,0x03,0x03,0x03,0x03,0x03,0xff,0xff,0xfc,0xfc,0x00,0x00,0xfc,0xfc,0xff,0xff,0x03,0x03,0x03,0x03,0x03,0x03,0xff,0xff,0xfc,0xfc,0x00,0x00,0x03,0x03,0x0f,0x0f,0x0c,0x0c,0x0c,0x0c,0x0c,0x0c,0x0f,0x0f,0x03,0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0xfc,0xfc,0xff,0xff,0x03,0x03,0x03,0x03,0x03,0x03,0xff,0xff,0xfc,0xfc,0x00,0x00,0x00,0x00,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,0x0c,0x0c,0x0c,0x0c,0x0c,0x0c,0x0f,0x0f,0x03,0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0xf0,0xf0,0xf0,0xf0,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xc0,0xc0,0xc0,0xc0,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x03,0x03,0x03,0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0xf0,0xf0,0xf0,0xf0,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xc0,0xc0,0xc0,0xc0,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x0c,0x0c,0x0f,0x0f,0x03,0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0xc0,0xc0,0xf0,0xf0,0x3c,0x3c,0x0c,0x0c,0x00,0x00,0x00,0x00,0x0c,0x0c,0x3f,0x3f,0xf3,0xf3,0xc0,0xc0,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x03,0x03,0x0f,0x0f,0x0c,0x0c,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xc3,0xc3,0xc3,0xc3,0xc3,0xc3,0xc3,0xc3,0xc3,0xc3,0xc3,0xc3,0xc3,0xc3,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x0c,0x0c,0x3c,0x3c,0xf0,0xf0,0xc0,0xc0,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xc0,0xc0,0xf3,0xf3,0x3f,0x3f,0x0c,0x0c,0x00,0x00,0x00,0x00,0x0c,0x0c,0x0f,0x0f,0x03,0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x3c,0x3c,0x3f,0x3f,0x03,0x03,0x03,0x03,0xc3,0xc3,0xff,0xff,0x3c,0x3c,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x3f,0x3f,0x3f,0x3f,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x0f,0x0f,0x0f,0x0f,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0xf0,0xf0,0xfc,0xfc,0x0c,0x0c,0x0c,0x0c,0x0c,0x0c,0xfc,0xfc,0xf0,0xf0,0x00,0x00,0xff,0xff,0xff,0xff,0x00,0x00,0xff,0xff,0xff,0xff,0xff,0xff,0x3f,0x3f,0x00,0x00,0x03,0x03,0x0f,0x0f,0x0c,0x0c,0x0c,0x0c,0x0c,0x0c,0x0c,0x0c,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0xc0,0xc0,0xf0,0xf0,0x3c,0x3c,0x0f,0x0f,0x3c,0x3c,0xf0,0xf0,0xc0,0xc0,0x00,0x00,0xff,0xff,0xff,0xff,0x0c,0x0c,0x0c,0x0c,0x0c,0x0c,0xff,0xff,0xff,0xff,0x00,0x00,0x0f,0x0f,0x0f,0x0f,0x00,0x00,0x00,0x00,0x00,0x00,0x0f,0x0f,0x0f,0x0f,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x03,0x03,0xff,0xff,0xff,0xff,0x03,0x03,0x03,0x03,0xff,0xff,0xfc,0xfc,0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0x03,0x03,0x03,0x03,0xff,0xff,0xfc,0xfc,0x00,0x00,0x0c,0x0c,0x0f,0x0f,0x0f,0x0f,0x0c,0x0c,0x0c,0x0c,0x0f,0x0f,0x03,0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0xf0,0xf0,0xfc,0xfc,0x0f,0x0f,0x03,0x03,0x03,0x03,0x0f,0x0f,0x3c,0x3c,0x00,0x00,0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xc0,0xc0,0x00,0x00,0x00,0x00,0x03,0x03,0x0f,0x0f,0x0c,0x0c,0x0c,0x0c,0x0f,0x0f,0x03,0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x03,0x03,0xff,0xff,0xff,0xff,0x03,0x03,0x0f,0x0f,0xfc,0xfc,0xf0,0xf0,0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0x00,0x00,0x0c,0x0c,0x0f,0x0f,0x0f,0x0f,0x0c,0x0c,0x0f,0x0f,0x03,0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x03,0x03,0xff,0xff,0xff,0xff,0x03,0x03,0xc3,0xc3,0x0f,0x0f,0x3f,0x3f,0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0x03,0x03,0x0f,0x0f,0x00,0x00,0xc0,0xc0,0x00,0x00,0x0c,0x0c,0x0f,0x0f,0x0f,0x0f,0x0c,0x0c,0x0c,0x0c,0x0f,0x0f,0x0f,0x0f,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x03,0x03,0xff,0xff,0xff,0xff,0x03,0x03,0xc3,0xc3,0x0f,0x0f,0x3f,0x3f,0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0x03,0x03,0x0f,0x0f,0x00,0x00,0x00,0x00,0x00,0x00,0x0c,0x0c,0x0f,0x0f,0x0f,0x0f,0x0c,0x0c,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0xf0,0xf0,0xfc,0xfc,0x0f,0x0f,0x03,0x03,0x03,0x03,0x0f,0x0f,0x3c,0x3c,0x00,0x00,0xff,0xff,0xff,0xff,0x00,0x00,0x0c,0x0c,0x0c,0x0c,0xfc,0xfc,0xfc,0xfc,0x00,0x00,0x00,0x00,0x03,0x03,0x0f,0x0f,0x0c,0x0c,0x0c,0x0c,0x03,0x03,0x0f,0x0f,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0x00,0x00,0xff,0xff,0xff,0xff,0x03,0x03,0x03,0x03,0x03,0x03,0xff,0xff,0xff,0xff,0x00,0x00,0x0f,0x0f,0x0f,0x0f,0x00,0x00,0x00,0x00,0x00,0x00,0x0f,0x0f,0x0f,0x0f,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x03,0x03,0xff,0xff,0xff,0xff,0x03,0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x0c,0x0c,0x0f,0x0f,0x0f,0x0f,0x0c,0x0c,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x03,0x03,0xff,0xff,0xff,0xff,0x03,0x03,0x00,0x00,0xf0,0xf0,0xf0,0xf0,0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,0x03,0x03,0x0f,0x0f,0x0c,0x0c,0x0c,0x0c,0x0f,0x0f,0x03,0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x03,0x03,0xff,0xff,0xff,0xff,0x00,0x00,0xf0,0xf0,0xff,0xff,0x0f,0x0f,0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0x0f,0x0f,0x3f,0x3f,0xf0,0xf0,0xc0,0xc0,0x00,0x00,0x0c,0x0c,0x0f,0x0f,0x0f,0x0f,0x00,0x00,0x00,0x00,0x0f,0x0f,0x0f,0x0f,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x03,0x03,0xff,0xff,0xff,0xff,0x03,0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,0x00,0x00,0xc0,0xc0,0x00,0x00,0x0c,0x0c,0x0f,0x0f,0x0f,0x0f,0x0c,0x0c,0x0c,0x0c,0x0f,0x0f,0x0f,0x0f,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0xff,0xff,0xff,0xff,0xfc,0xfc,0xf0,0xf0,0xfc,0xfc,0xff,0xff,0xff,0xff,0x00,0x00,0xff,0xff,0xff,0xff,0x00,0x00,0x03,0x03,0x00,0x00,0xff,0xff,0xff,0xff,0x00,0x00,0x0f,0x0f,0x0f,0x0f,0x00,0x00,0x00,0x00,0x00,0x00,0x0f,0x0f,0x0f,0x0f,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0xff,0xff,0xff,0xff,0xfc,0xfc,0xf0,0xf0,0xc0,0xc0,0xff,0xff,0xff,0xff,0x00,0x00,0xff,0xff,0xff,0xff,0x00,0x00,0x03,0x03,0x0f,0x0f,0xff,0xff,0xff,0xff,0x00,0x00,0x0f,0x0f,0x0f,0x0f,0x00,0x00,0x00,0x00,0x00,0x00,0x0f,0x0f,0x0f,0x0f,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0xf0,0xf0,0xfc,0xfc,0x0f,0x0f,0x03,0x03,0x0f,0x0f,0xfc,0xfc,0xf0,0xf0,0x00,0x00,0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,0x03,0x03,0x0f,0x0f,0x0c,0x0c,0x0f,0x0f,0x03,0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x03,0x03,0xff,0xff,0xff,0xff,0x03,0x03,0x03,0x03,0xff,0xff,0xfc,0xfc,0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0x03,0x03,0x03,0x03,0x03,0x03,0x00,0x00,0x00,0x00,0x0c,0x0c,0x0f,0x0f,0x0f,0x0f,0x0c,0x0c,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0xfc,0xfc,0xff,0xff,0x03,0x03,0x03,0x03,0x03,0x03,0xff,0xff,0xfc,0xfc,0x00,0x00,0xff,0xff,0xff,0xff,0x00,0x00,0xc0,0xc0,0x00,0x00,0xff,0xff,0xff,0xff,0x00,0x00,0x03,0x03,0x0f,0x0f,0x0c,0x0c,0x0f,0x0f,0xff,0xff,0xff,0xff,0xc3,0xc3,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x03,0x03,0xff,0xff,0xff,0xff,0x03,0x03,0x03,0x03,0xff,0xff,0xfc,0xfc,0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0x03,0x03,0x0f,0x0f,0xff,0xff,0xf0,0xf0,0x00,0x00,0x0c,0x0c,0x0f,0x0f,0x0f,0x0f,0x00,0x00,0x00,0x00,0x0f,0x0f,0x0f,0x0f,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x3c,0x3c,0xff,0xff,0xc3,0xc3,0x03,0x03,0x03,0x03,0x3f,0x3f,0x3c,0x3c,0x00,0x00,0xc0,0xc0,0xc0,0xc0,0x03,0x03,0x03,0x03,0x0f,0x0f,0xfc,0xfc,0xf0,0xf0,0x00,0x00,0x03,0x03,0x0f,0x0f,0x0c,0x0c,0x0c,0x0c,0x0c,0x0c,0x0f,0x0f,0x03,0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x3f,0x3f,0x0f,0x0f,0xff,0xff,0xff,0xff,0x0f,0x0f,0x3f,0x3f,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x0c,0x0c,0x0f,0x0f,0x0f,0x0f,0x0c,0x0c,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0x00,0x00,0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0x00,0x00,0x03,0x03,0x0f,0x0f,0x0c,0x0c,0x0c,0x0c,0x0c,0x0c,0x0f,0x0f,0x03,0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0x00,0x00,0x3f,0x3f,0xff,0xff,0xc0,0xc0,0x00,0x00,0xc0,0xc0,0xff,0xff,0x3f,0x3f,0x00,0x00,0x00,0x00,0x00,0x00,0x03,0x03,0x0f,0x0f,0x03,0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0x00,0x00,0xff,0xff,0xff,0xff,0xc0,0xc0,0xfc,0xfc,0xc0,0xc0,0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,0x0f,0x0f,0x0f,0x0f,0x00,0x00,0x0f,0x0f,0x0f,0x0f,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x0f,0x0f,0xff,0xff,0xf0,0xf0,0x00,0x00,0xf0,0xf0,0xff,0xff,0x0f,0x0f,0x00,0x00,0x00,0x00,0xf0,0xf0,0xff,0xff,0x0f,0x0f,0xff,0xff,0xf0,0xf0,0x00,0x00,0x00,0x00,0x0f,0x0f,0x0f,0x0f,0x00,0x00,0x00,0x00,0x00,0x00,0x0f,0x0f,0x0f,0x0f,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,0x00,0x00,0x03,0x03,0xff,0xff,0xff,0xff,0x03,0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x0c,0x0c,0x0f,0x0f,0x0f,0x0f,0x0c,0x0c,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x3f,0x3f,0x0f,0x0f,0x03,0x03,0x03,0x03,0xc3,0xc3,0xff,0xff,0x3f,0x3f,0x00,0x00,0xc0,0xc0,0xf0,0xf0,0x3c,0x3c,0x0f,0x0f,0x03,0x03,0x00,0x00,0xc0,0xc0,0x00,0x00,0x0f,0x0f,0x0f,0x0f,0x0c,0x0c,0x0c,0x0c,0x0c,0x0c,0x0f,0x0f,0x0f,0x0f,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0x03,0x03,0x03,0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x0f,0x0f,0x0f,0x0f,0x0c,0x0c,0x0c,0x0c,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0xfc,0xfc,0xf0,0xf0,0xc0,0xc0,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x03,0x03,0x0f,0x0f,0x3f,0x3f,0xfc,0xfc,0xf0,0xf0,0xc0,0xc0,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x03,0x03,0x0f,0x0f,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x03,0x03,0x03,0x03,0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x0c,0x0c,0x0c,0x0c,0x0f,0x0f,0x0f,0x0f,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0xc0,0xc0,0xf0,0xf0,0xc0,0xc0,0x00,0x00,0x00,0x00,0x00,0x00,0x0c,0x0c,0x0f,0x0f,0x03,0x03,0x00,0x00,0x03,0x03,0x0f,0x0f,0x0c,0x0c,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xc0,0xc0,0xc0,0xc0,0xc0,0xc0,0xc0,0xc0,0xc0,0xc0,0xc0,0xc0,0xc0,0xc0,0xc0,0xc0,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0xf0,0xf0,0xf0,0xf0,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x03,0x03,0x03,0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0xc0,0xc0,0xc0,0xc0,0xc0,0xc0,0xc0,0xc0,0x00,0x00,0x00,0x00,0x00,0x00,0xf0,0xf0,0xfc,0xfc,0x0c,0x0c,0x0c,0x0c,0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,0x03,0x03,0x0f,0x0f,0x0c,0x0c,0x0c,0x0c,0x03,0x03,0x0f,0x0f,0x0c,0x0c,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x03,0x03,0xff,0xff,0xff,0xff,0xc0,0xc0,0xc0,0xc0,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0x00,0x00,0x03,0x03,0xff,0xff,0xfc,0xfc,0x00,0x00,0x0c,0x0c,0x0f,0x0f,0x03,0x03,0x0c,0x0c,0x0c,0x0c,0x0f,0x0f,0x03,0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0xc0,0xc0,0xc0,0xc0,0xc0,0xc0,0xc0,0xc0,0xc0,0xc0,0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,0x00,0x00,0x03,0x03,0x03,0x03,0x00,0x00,0x03,0x03,0x0f,0x0f,0x0c,0x0c,0x0c,0x0c,0x0c,0x0c,0x0f,0x0f,0x03,0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0xc0,0xc0,0xc3,0xc3,0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,0xfc,0xfc,0xff,0xff,0x03,0x03,0x00,0x00,0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,0x03,0x03,0x0f,0x0f,0x0c,0x0c,0x0c,0x0c,0x03,0x03,0x0f,0x0f,0x0c,0x0c,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0xc0,0xc0,0xc0,0xc0,0xc0,0xc0,0xc0,0xc0,0xc0,0xc0,0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0x0c,0x0c,0x0c,0x0c,0x0c,0x0c,0x0f,0x0f,0x0f,0x0f,0x00,0x00,0x03,0x03,0x0f,0x0f,0x0c,0x0c,0x0c,0x0c,0x0c,0x0c,0x0f,0x0f,0x03,0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0xfc,0xfc,0xff,0xff,0x03,0x03,0x0f,0x0f,0x3c,0x3c,0x00,0x00,0x00,0x00,0x03,0x03,0xff,0xff,0xff,0xff,0x03,0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x0c,0x0c,0x0f,0x0f,0x0f,0x0f,0x0c,0x0c,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0xc0,0xc0,0xc0,0xc0,0xc0,0xc0,0x00,0x00,0xc0,0xc0,0xc0,0xc0,0x00,0x00,0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,0xc3,0xc3,0xcf,0xcf,0x0c,0x0c,0x0c,0x0c,0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,0x00,0x00,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x00,0x00,0x00,0x00,0x00,0x00,
  0x03,0x03,0xff,0xff,0xff,0xff,0x00,0x00,0xc0,0xc0,0xc0,0xc0,0x00,0x00,0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0x03,0x03,0x00,0x00,0xff,0xff,0xff,0xff,0x00,0x00,0x0c,0x0c,0x0f,0x0f,0x0f,0x0f,0x00,0x00,0x00,0x00,0x0f,0x0f,0x0f,0x0f,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0xc0,0xc0,0xcf,0xcf,0xcf,0xcf,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x0c,0x0c,0x0f,0x0f,0x0f,0x0f,0x0c,0x0c,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xc0,0xc0,0xcf,0xcf,0xcf,0xcf,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,0xf0,0xf0,0xf0,0xf0,0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,0x00,0x00,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x00,0x00,0x00,0x00,
  0x03,0x03,0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,0xc0,0xc0,0xc0,0xc0,0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0x3c,0x3c,0xff,0xff,0xc3,0xc3,0x00,0x00,0x00,0x00,0x0c,0x0c,0x0f,0x0f,0x0f,0x0f,0x00,0x00,0x00,0x00,0x0f,0x0f,0x0f,0x0f,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x03,0x03,0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x0c,0x0c,0x0f,0x0f,0x0f,0x0f,0x0c,0x0c,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0xc0,0xc0,0xc0,0xc0,0xc0,0xc0,0x00,0x00,0xc0,0xc0,0xc0,0xc0,0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0x03,0x03,0xff,0xff,0x03,0x03,0xff,0xff,0xff,0xff,0x00,0x00,0x0f,0x0f,0x0f,0x0f,0x00,0x00,0x0f,0x0f,0x00,0x00,0x0f,0x0f,0x0f,0x0f,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0xc0,0xc0,0xc0,0xc0,0x00,0x00,0xc0,0xc0,0xc0,0xc0,0xc0,0xc0,0x00,0x00,0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,0x0f,0x0f,0x0f,0x0f,0x00,0x00,0x00,0x00,0x0f,0x0f,0x0f,0x0f,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0xc0,0xc0,0xc0,0xc0,0xc0,0xc0,0xc0,0xc0,0xc0,0xc0,0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0x00,0x00,0x03,0x03,0x0f,0x0f,0x0c,0x0c,0x0c,0x0c,0x0c,0x0c,0x0f,0x0f,0x03,0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0xc0,0xc0,0xc0,0xc0,0x00,0x00,0xc0,0xc0,0xc0,0xc0,0xc0,0xc0,0x00,0x00,0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0x0c,0x0c,0x0c,0x0c,0x0f,0x0f,0x03,0x03,0x00,0x00,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0xc0,0xc0,0xc0,0xc0,0xc0,0xc0,0x00,0x00,0xc0,0xc0,0xc0,0xc0,0x00,0x00,0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,0x03,0x03,0x0f,0x0f,0x0c,0x0c,0x0c,0x0c,0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x00,0x00,
  0xc0,0xc0,0xc0,0xc0,0x00,0x00,0xc0,0xc0,0xc0,0xc0,0xc0,0xc0,0x00,0x00,0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0x03,0x03,0x00,0x00,0x03,0x03,0x0f,0x0f,0x00,0x00,0x0c,0x0c,0x0f,0x0f,0x0f,0x0f,0x0c,0x0c,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0xc0,0xc0,0xc0,0xc0,0xc0,0xc0,0xc0,0xc0,0xc0,0xc0,0x00,0x00,0x00,0x00,0x03,0x03,0x0f,0x0f,0x3c,0x3c,0x30,0x30,0xf0,0xf0,0xc3,0xc3,0x03,0x03,0x00,0x00,0x03,0x03,0x0f,0x0f,0x0c,0x0c,0x0c,0x0c,0x0c,0x0c,0x0f,0x0f,0x03,0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0xc0,0xc0,0xc0,0xc0,0xfc,0xfc,0xff,0xff,0xc0,0xc0,0xc0,0xc0,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x03,0x03,0x0f,0x0f,0x0c,0x0c,0x0f,0x0f,0x03,0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0xc0,0xc0,0xc0,0xc0,0x00,0x00,0x00,0x00,0xc0,0xc0,0xc0,0xc0,0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,0x03,0x03,0x0f,0x0f,0x0c,0x0c,0x0c,0x0c,0x03,0x03,0x0f,0x0f,0x0c,0x0c,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0xc0,0xc0,0xc0,0xc0,0x00,0x00,0x00,0x00,0xc0,0xc0,0xc0,0xc0,0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,0x00,0x00,0x03,0x03,0x0f,0x0f,0x0f,0x0f,0x03,0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0xc0,0xc0,0xc0,0xc0,0x00,0x00,0x00,0x00,0x00,0x00,0xc0,0xc0,0xc0,0xc0,0x00,0x00,0xff,0xff,0xff,0xff,0x00,0x00,0xf0,0xf0,0x00,0x00,0xff,0xff,0xff,0xff,0x00,0x00,0x03,0x03,0x0f,0x0f,0x0f,0x0f,0x03,0x03,0x0f,0x0f,0x0f,0x0f,0x03,0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0xc0,0xc0,0xc0,0xc0,0x00,0x00,0x00,0x00,0x00,0x00,0xc0,0xc0,0xc0,0xc0,0x00,0x00,0x00,0x00,0x03,0x03,0xff,0xff,0xfc,0xfc,0xff,0xff,0x03,0x03,0x00,0x00,0x00,0x00,0x0c,0x0c,0x0f,0x0f,0x03,0x03,0x00,0x00,0x03,0x03,0x0f,0x0f,0x0c,0x0c,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0xc0,0xc0,0xc0,0xc0,0x00,0x00,0x00,0x00,0x00,0x00,0xc0,0xc0,0xc0,0xc0,0x00,0x00,0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0x00,0x00,0x03,0x03,0x0f,0x0f,0x0c,0x0c,0x0c,0x0c,0xcc,0xcc,0xff,0xff,0x3f,0x3f,0x00,0x00,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x00,0x00,0x00,0x00,0x00,0x00,
  0xc0,0xc0,0xc0,0xc0,0xc0,0xc0,0xc0,0xc0,0xc0,0xc0,0xc0,0xc0,0xc0,0xc0,0x00,0x00,0x03,0x03,0xc3,0xc3,0xf0,0xf0,0x3c,0x3c,0x0f,0x0f,0x03,0x03,0x00,0x00,0x00,0x00,0x0f,0x0f,0x0f,0x0f,0x0c,0x0c,0x0c,0x0c,0x0c,0x0c,0x0f,0x0f,0x0f,0x0f,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0xfc,0xfc,0xff,0xff,0x03,0x03,0x03,0x03,0x00,0x00,0x00,0x00,0x03,0x03,0x03,0x03,0xff,0xff,0xfc,0xfc,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x03,0x03,0x0f,0x0f,0x0c,0x0c,0x0c,0x0c,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x0f,0x0f,0x0f,0x0f,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x03,0x03,0x03,0x03,0xff,0xff,0xfc,0xfc,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xfc,0xfc,0xff,0xff,0x03,0x03,0x03,0x03,0x00,0x00,0x00,0x00,0x0c,0x0c,0x0c,0x0c,0x0f,0x0f,0x03,0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x0c,0x0c,0x0f,0x0f,0x03,0x03,0x0f,0x0f,0x0c,0x0c,0x0f,0x0f,0x03,0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0xc0,0xc0,0xf0,0xf0,0xc0,0xc0,0x00,0x00,0x00,0x00,0x00,0x00,0xfc,0xfc,0xff,0xff,0x03,0x03,0x00,0x00,0x03,0x03,0xff,0xff,0xfc,0xfc,0x00,0x00,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};

uint8_t logogap[240]; //the buffer used when scrolling the logo, between screens

const short fakepulsedata[49] = {568,696,835,956,1021,1023,992,912,817,637,556,481,413,327,299,295,310,386,439,486,527,553,545,523,492,430,401,382,369,364,370,380,393,418,429,441,451,461,467,469,469,468,464,460,458,455,456,461,488};

const short fakepulsewave[343] = {
0,0,0,248,7,3,28,224,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,//row 0
0,0,254,1,0,0,0,7,248,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,//row 1
0,254,1,0,0,0,0,0,15,240,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,//row 2
252,3,0,0,0,0,0,0,0,31,224,0,0,0,0,0,0,0,0,0,0,128,128,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,//row 3
1,0,0,0,0,0,0,0,0,0,3,124,128,0,0,0,0,0,224,24,6,1,1,6,56,192,0,0,0,0,0,0,0,0,128,64,64,32,32,16,16,32,32,32,32,32,32,32,30,//row 4
0,0,0,0,0,0,0,0,0,0,0,0,15,240,0,0,224,30,1,0,0,0,0,0,0,1,2,4,8,8,8,4,4,3,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,//row 5
128,128,128,128,128,128,128,128,128,128,128,128,128,128,129,129,128,128,128,128,128,128,128,128,128,128,128,128,128,128,128,128,128,128,128,128,128,128,128,128,128,128,128,128,128,128,128,128,128};//row 6

void setup(){
  pinMode(VREGENABLE, INPUT_PULLUP); //when we set VREGENABLE to an output, don't default in the LOW state
  pinMode(VREGENABLE, OUTPUT); //pin used to sink/turn off the battery vreg when USB +5v is connected
  digitalWrite(VREGENABLE, HIGH); //don't shut off batt vreg by default
  pinMode(BATTSENSE, INPUT); //used to detect if USB 5v is connected
  pinMode(GSRPIN, INPUT); //gsr pin
  pinMode(HBPIN, INPUT); //heartbeat pin
  pinMode(RIGHTBUTTON, INPUT_PULLUP);
  pinMode(LEFTBUTTON, INPUT_PULLUP);
  pinMode(UPBUTTON, INPUT_PULLUP);
  pinMode(DOWNBUTTON, INPUT_PULLUP);
  pinMode(HBLED, OUTPUT);
  digitalWrite(HBLED, HIGH); //HB led off
  pinMode(LED_BUILTIN, OUTPUT); //builtin orange led on the xiao
  digitalWrite(LED_BUILTIN, HIGH); // turn the LED off, used to signify if battery vreg is enabled (ledoff=on battery power, ledon=vreg disabled and on USB power)
  pinMode(PIN_LED2, OUTPUT); //builtin bottom blue led on the xiao
  digitalWrite(PIN_LED2, HIGH); //turn the LED off
  pinMode(PIN_LED3, OUTPUT); //builtin top blue led on the xiao
  digitalWrite(PIN_LED3, HIGH); //turn the LED off
  analogReadResolution(10); //10-bit adc should be the default
  analogReference(AR_DEFAULT); //should be the default
  Serial.begin(115200); //USB serial port bps 115200/8/n/1
  setupOledScreens();
  oledDumpBuffer(&ssoled[0], staticlogobuffer);//display startup screen logo on screen 0
  for(byte x=0;x<8;x++){//invert all pixels
    for(byte y=0;y<128;y++){
      zerobuffer[(x*128)+y] = ~zerobuffer[(x*128)+y];
    }
  }
  oledDumpBuffer(&ssoled[0], zerobuffer);//show inverted logo
  oledDumpBuffer(&ssoled[1], staticlogobuffer);//display startup screen logo on screen 1
  oledWriteString(&ssoled[1], 0, 101/*x*/, 7/*y*/, "FW", FONT_SMALL, 0/*inv color*/, 1);//code version to bottom right corner of right screen
  oledWriteString(&ssoled[1], 0, 119/*x*/, 7/*y*/, shorttostring(CODE_VERSION), FONT_SMALL, 0/*inv color*/, 1);//dump code version to bottom right corner of right screen
  boottime = millis();//used for uptime calc
  delay(2000);//show the startup logo for a bit
  EEPROM.setCommitASAP(false);//don't commit data to flash on put(), you must commit()
  updateMode(10); //move to main menu  
} /* setup() */

void updateMode(short newmode){
  //show menu
  //Start Lie Detector 10
  //  ->normal ops mode == 100
  //Options 11
  //  Screen Brightness (255=max) 20
  //    High 52
  //    Medium 51
  //    Low 50
  //  Sleep Mode 21
  //  Calibrate GSR 22
  //    Calibrate GSR (MENU) 80
  //  Cheat Mode 23
  //Bling 12
  //  Fake Pulse 30
  //    ->fake pulse mode == 101
  //  Logo Scroll 31
  //    ->logo scroll mode == 102
  //  #BadDefconAdvice 32
  //  Name Scroll 33
  //    ->name scroll entrymode == 103, actual scrolling == 104
  //    ->bad defcon advice mode = 150-195
  //Help 13
  //  code/man url 40
  digitalWrite(HBLED, HIGH); //HB led off
  digitalWrite(PIN_LED2, HIGH); //bottom blue led off
  if((runmode < 10 || runmode > 13) && (newmode >= 10 && newmode <= 13 || //if we weren't in a menu mode, and now we switched to one, repaint the whole menu
    newmode == 30 && runmode == 101 || newmode == 31 && runmode == 102 || newmode == 32 && runmode >= 150 || newmode == 33 && (runmode == 103 || runmode == 104))){ //if we were in fake peak, logoscroll, badadvice mode, or namescroll entry mode go back to the second level bling menu
    oledFill(&ssoled[0], 0, 1);//cls
    oledFill(&ssoled[1], 0, 1);//cls
    oledWriteString(&ssoled[0], 0, 29/*x*/, 0/*y*/, "Main Menu", FONT_NORMAL, 0/*inv color*/, 0);
    oledRectangle(&ssoled[0], 0/*x*/, 8/*y*/, 127/*x*/, 10/*y*/, 1/*color*/, 1/*fill*/);//draw underline
    oledWriteString(&ssoled[0], 0, 9/*x*/, 3/*y*/, "Start Lie Detector", FONT_SMALL, 0/*inv color*/, 0);
    oledWriteString(&ssoled[0], 0, 9/*x*/, 4/*y*/, "Options", FONT_SMALL, 0/*inv color*/, 0);
    oledWriteString(&ssoled[0], 0, 9/*x*/, 5/*y*/, "Bling", FONT_SMALL, 0/*inv color*/, 0);
    oledWriteString(&ssoled[0], 0, 9/*x*/, 6/*y*/, "Help", FONT_SMALL, 0/*inv color*/, 0);
    if(newmode >= 30 && newmode <= 33){ //back out from bling modes into second level menu (since everything is else-if's) this avoids an unnecessary screen repaint
      oledWriteString(&ssoled[1], 0, 44/*x*/, 0/*y*/, "Bling", FONT_NORMAL, 0/*inv color*/, 0);
      oledRectangle(&ssoled[1], 0/*x*/, 8/*y*/, 127/*x*/, 10/*y*/, 1/*color*/, 1/*fill*/);//draw underline
      oledWriteString(&ssoled[1], 0, 9/*x*/, 3/*y*/, "Fake Pulse", FONT_SMALL, 0/*inv color*/, 0);
      oledWriteString(&ssoled[1], 0, 9/*x*/, 4/*y*/, "Logo Scroll", FONT_SMALL, 0/*inv color*/, 0);
      oledWriteString(&ssoled[1], 0, 9/*x*/, 5/*y*/, "Bad Defcon Advice", FONT_SMALL, 0/*inv color*/, 0);
      oledWriteString(&ssoled[1], 0, 9/*x*/, 6/*y*/, "Name Scroll", FONT_SMALL, 0/*inv color*/, 0);
    }else{ //not in secondary menu, show the logo on the second screen
      oledDumpBuffer(&ssoled[1], staticlogobuffer);
    }
  }else if( ((runmode > 10 && runmode <= 13) || (runmode >= 30 && runmode < 100)) && (newmode >= 20 && newmode <= 23) ){ //if we were in a top level or third level option menu, and we are now in a 2nd level options sub menu
    oledFill(&ssoled[1], 0, 1);//cls
    oledWriteString(&ssoled[1], 0, 37/*x*/, 0/*y*/, "Options", FONT_NORMAL, 0/*inv color*/, 0);
    oledRectangle(&ssoled[1], 0/*x*/, 8/*y*/, 127/*x*/, 10/*y*/, 1/*color*/, 1/*fill*/);//draw underline
    oledWriteString(&ssoled[1], 0, 9/*x*/, 3/*y*/, "Screen Brightness", FONT_SMALL, 0/*inv color*/, 0);
    oledWriteString(&ssoled[1], 0, 9/*x*/, 4/*y*/, "Sleep Mode", FONT_SMALL, 0/*inv color*/, 0);
    oledWriteString(&ssoled[1], 0, 9/*x*/, 5/*y*/, "Calibrate GSR", FONT_SMALL, 0/*inv color*/, 0);
    oledWriteString(&ssoled[1], 0, 9/*x*/, 6/*y*/, "Cheat Modes:", FONT_SMALL, 0/*inv color*/, 0);
    if(cheatsenabled == true){
      oledWriteString(&ssoled[1], 0, 83/*x*/, 6/*y*/, "On ", FONT_SMALL, 0/*inv color*/, 0);
    }else{
      oledWriteString(&ssoled[1], 0, 83/*x*/, 6/*y*/, "Off", FONT_SMALL, 0/*inv color*/, 0);
    }
  }else if( (runmode > 10 && runmode <= 13) && (newmode >= 30 && newmode <= 32) ){ //if we were in a top level menu, and we are now in the bling sub menu
    oledFill(&ssoled[1], 0, 1);//cls
    oledWriteString(&ssoled[1], 0, 44/*x*/, 0/*y*/, "Bling", FONT_NORMAL, 0/*inv color*/, 0);
    oledRectangle(&ssoled[1], 0/*x*/, 8/*y*/, 127/*x*/, 10/*y*/, 1/*color*/, 1/*fill*/);//draw underline
    oledWriteString(&ssoled[1], 0, 9/*x*/, 3/*y*/, "Fake Pulse", FONT_SMALL, 0/*inv color*/, 0);
    oledWriteString(&ssoled[1], 0, 9/*x*/, 4/*y*/, "Logo Scroll", FONT_SMALL, 0/*inv color*/, 0);
    oledWriteString(&ssoled[1], 0, 9/*x*/, 5/*y*/, "Bad Defcon Advice", FONT_SMALL, 0/*inv color*/, 0);
    oledWriteString(&ssoled[1], 0, 9/*x*/, 6/*y*/, "Name Scroll", FONT_SMALL, 0/*inv color*/, 0);
  }else if(newmode == 40){ //help sub-menu
    oledFill(&ssoled[1], 0, 1);//cls
    oledWriteString(&ssoled[1], 0, 49/*x*/, 0/*y*/, "Help", FONT_NORMAL, 0/*inv color*/, 0);
    oledRectangle(&ssoled[1], 0/*x*/, 8/*y*/, 127/*x*/, 10/*y*/, 1/*color*/, 1/*fill*/);//draw underline
    oledWriteString(&ssoled[1], 0, 14/*x*/, 2/*y*/, "Code And Manual:", FONT_SMALL, 0/*inv color*/, 0);
    oledWriteString(&ssoled[1], 0, 11/*x*/, 3/*y*/, "github.com/seeess", FONT_SMALL, 0/*inv color*/, 0);
    oledWriteString(&ssoled[1], 0, 0/*x*/, 5/*y*/, "Twitter:", FONT_SMALL, 0/*inv color*/, 0);
    oledWriteString(&ssoled[1], 0, 51/*x*/, 5/*y*/, "@gigstaggart", FONT_SMALL, 0/*inv color*/, 0);
    oledWriteString(&ssoled[1], 0, 51/*x*/, 6/*y*/, "@see_ess", FONT_SMALL, 0/*inv color*/, 0);
    oledWriteString(&ssoled[1], 0, 0/*x*/, 7/*y*/, "Uptime:", FONT_SMALL, 0/*inv color*/, 1);
    oledWriteString(&ssoled[1], 0, 44/*x*/, 7/*y*/, "00000min", FONT_SMALL, 0/*inv color*/, 1);//dump uptime in min
    oledWriteString(&ssoled[1], 0, 100/*x*/, 7/*y*/, "FW:", FONT_SMALL, 0/*inv color*/, 1);//dump code version
    oledWriteString(&ssoled[1], 0, 119/*x*/, 7/*y*/, shorttostring(CODE_VERSION), FONT_SMALL, 0/*inv color*/, 1);//dump code version to bottom right corner of right screen
    konami = 0;//uhhh... dwai
  }else if(newmode >= 50 && newmode <= 52){ //in one of the sub-sub menus to set screen brightness
    loopval = 0; //reset loop, used for auto-timeout
    oledWriteString(&ssoled[1], 0, 9/*x*/, 4/*y*/, "          ", FONT_SMALL, 0/*inv color*/, 0); //clear sleep string
    oledWriteString(&ssoled[1], 0, 9/*x*/, 5/*y*/, "             ", FONT_SMALL, 0/*inv color*/, 0); //clear calibrate gsr string
    oledWriteString(&ssoled[1], 0, 9/*x*/, 6/*y*/, "                ", FONT_SMALL, 0/*inv color*/, 0);//clear cheat mode
    if(newmode >= 50 && newmode <= 52){ //setting granular screen brightness setting
      oledSetContrast(&ssoled[0], ((screenbrightness - 50) * 127) + 1); oledSetContrast(&ssoled[1], ((screenbrightness - 50) * 127) + 1); //set new screen brightness value
      if(newmode == 52){
        oledWriteString(&ssoled[1], 0, 49/*x*/, 5/*y*/, "High", FONT_SMALL, 0/*inv color*/, 0);
      }else if(newmode == 51){
        oledWriteString(&ssoled[1], 0, 43/*x*/, 5/*y*/, "Medium", FONT_SMALL, 0/*inv color*/, 0);
      }else if(newmode == 50){
        oledWriteString(&ssoled[1], 0, 53/*x*/, 5/*y*/, "Low", FONT_SMALL, 0/*inv color*/, 0);
      }
      oledDumpBuffer(&ssoled[1], onebuffer);
    }
  }else if (newmode == 80){ //calibrate GSR
    oledWriteString(&ssoled[1], 0, 12/*x*/, 0/*y*/, "Calibrate GSR", FONT_NORMAL, 0/*inv color*/, 0);
    oledWriteString(&ssoled[1], 0, 0/*x*/, 2/*y*/, "Insert fingers, turn", FONT_SMALL, 0/*inv color*/, 0);
    oledWriteString(&ssoled[1], 0, 0/*x*/, 3/*y*/, "pot until reading is", FONT_SMALL, 0/*inv color*/, 0);
    oledWriteString(&ssoled[1], 0, 0/*x*/, 4/*y*/, "around 512         ", FONT_SMALL, 0/*inv color*/, 0);
    oledWriteString(&ssoled[1], 0, 9/*x*/, 5/*y*/, "               ", FONT_SMALL, 0/*inv color*/, 0);
    oledWriteString(&ssoled[1], 0, 9/*x*/, 6/*y*/, "                ", FONT_SMALL, 0/*inv color*/, 0);//clear cheat mode
  }else if (newmode == 100 || newmode == 101){ //normal ops / fake pulse
    oledFill(&ssoled[0], 0, 1);//cls
    oledFill(&ssoled[1], 0, 1);//cls
    oledDrawLine(&ssoled[0], 0/*x*/, 55/*y*/, 127/*x*/, 55/*y*/, 0); //draw bottom divider line on gsr screen
    oledDrawLine(&ssoled[1], 0/*x*/, 55/*y*/, 127/*x*/, 55/*y*/, 0); //draw bottom divider line on hb screen
    oledWriteString(&ssoled[1], 0, 0/*x*/, 7/*y*/, "BPM:", FONT_SMALL, 0/*inv color*/, 0); //BPM display
    onebuffer[950] = B01100000; onebuffer[951] = B01011000; onebuffer[952] = B01000110; onebuffer[953] = B01000001; //draw delta symbol
    onebuffer[954] = B01000110; onebuffer[955] = B01011000; onebuffer[956] = B01100000;
    oledWriteString(&ssoled[1], 0, 62/*x*/, 7/*y*/, "BPM:", FONT_SMALL, 0/*inv color*/, 0); //delta BPM

    if(newmode == 100){ //normal ops, set values required for initial state
      oledWriteString(&ssoled[1], 0, 26/*x*/, 7/*y*/, "N/A", FONT_SMALL, 0/*inv color*/, 0); //print BPM reading
      oledWriteString(&ssoled[1], 0, 88/*x*/, 7/*y*/, "-", FONT_SMALL, 0/*inv color*/, 0); //delta BPM value
      oledWriteString(&ssoled[0], 0, 0/*x*/, 7/*y*/, "GSR:", FONT_SMALL, 0/*inv color*/, 0);
      zerobuffer[964] = B01100000; zerobuffer[965] = B01011000; zerobuffer[966] = B01000110; zerobuffer[967] = B01000001; //draw delta symbol
      zerobuffer[968] = B01000110; zerobuffer[969] = B01011000; zerobuffer[970] = B01100000;
      oledWriteString(&ssoled[0], 0, 76/*x*/, 7/*y*/, "GSR:", FONT_SMALL, 0/*inv color*/, 0); //delta gsr
      oledWriteString(&ssoled[0], 0, 102/*x*/, 7/*y*/, "0", FONT_SMALL, 0/*inv color*/, 0); //delta gsr val
      memset(hbdetected, 0, NUMHISTORICALHB);//clear the hbdetected array
      samplessincelastbeat = 0; //we haven't detected a hb, so clear this before we start
      digitalWrite(HBLED, LOW); //turn on led
      memset(gsrdelta, 0, sizeof(gsrdelta));//clear the gsrdelta array
      gsrdeltaindex = 0; //shouldn't be needed but whatever
      memset(bpmdelta, 0, sizeof(bpmdelta));//clear the bpmdelta array
      bpmdeltaindex = 0; //shouldn't be needed but whatever
      heartFill(false, onebuffer); //draw a hollow heart icon
      lastgsrvalue = 0; //clear it before +='ing
      for(byte z = 0; z < GSRREADINGSPERLOOP; z++){
        lastgsrvalue += analogRead(GSRPIN); //set last gsr value, so our graph doesn't start from 0
      }
      lastgsrvalue /= GSRREADINGSPERLOOP;
      while(lastgsrvalue + 6 > gsrmax){
        gsrmax += 16;  //our gsr maximum display scale is too low
        gsrmin += 16;
      }
      while (lastgsrvalue - 6 < gsrmin) {
        gsrmax -= 16;  //our gsr minimum display scale is too high
        gsrmin -= 16;
      }
      gsrvalue = 0; //reset value, since we use += when reading new values
    }else{ //newmode == 101 fake pulse mode, set values required for initial state
      oledWriteString(&ssoled[1], 0, 26/*x*/, 7/*y*/, "74", FONT_SMALL, 0/*inv color*/, 0); //print BPM reading
      onebuffer[950] = B01100000; onebuffer[951] = B01011000; onebuffer[952] = B01000110; onebuffer[953] = B01000001; //draw delta symbol
      onebuffer[954] = B01000110; onebuffer[955] = B01011000; onebuffer[956] = B01100000;
      oledWriteString(&ssoled[1], 0, 62/*x*/, 7/*y*/, "BPM:", FONT_SMALL, 0/*inv color*/, 0); //delta BPM
      oledWriteString(&ssoled[1], 0, 88/*x*/, 7/*y*/, "0", FONT_SMALL, 0/*inv color*/, 0); //delta BPM value
      heartFill(false, onebuffer);
      memcpy(onebackbuffer, onebuffer, 1024);//copy bottom key to onebackbuffer
      memcpy(zerobackbuffer, onebuffer, 1024);//copy bottom key to zerobackbuffer
      memset(hbdetected, 49, NUMHISTORICALHB);
      memset(bpmdelta, 0, sizeof(bpmdelta));//clear the bpmdelta array
      samplessincelastbeat = 48;//so the initial HB reading doesn't jump
      digitalWrite(HBLED, LOW); //turn on led
      digitalWrite(PIN_LED2, LOW); //turn on the led
    }
    oledDumpBuffer(&ssoled[0], zerobuffer);
    oledDumpBuffer(&ssoled[1], onebuffer);

    bpm = 0; //needed so fake mode clears the "N/A"
    for(short z = 0; z < HBHISTORICALREADINGS; z++){hbsamplebuffer[z] = 512;} //set a baseline value
    hbrising = true; //if the hb raw samples are still rising (peak not detected yet)
    pulseledstatus = false; //the peak led should be off
    validhbsdetected = 0;
    lasthbvalue = 32; //set last hb value to the middle display range, so our graph doesn't start from 0
    loopval = 0; //used for fake pulse mode, and to not calc GSR before looping 4 timesF
  }else if(newmode == 102){ //logo scroll mode
    memcpy(zerobuffer, staticlogobuffer, 1024); //start the logo on the left screen
    memset(onebuffer, 0, 1024); //clear right screen
    memset(logogap, 0, 240); //clear the logo gap buffer
    scrollspeed = 1; //reset in case it was changed
    oledDumpBuffer(&ssoled[0], zerobuffer);
    oledDumpBuffer(&ssoled[1], onebuffer);
  }else if(newmode >= 150 && newmode <= 195){ //bad defcon advice
    pretime = millis();//timestamp of entering the mode
    currentadvice = newmode - 150;
    memset(zerobuffer, 0, 1024); //clear left screen
    memset(onebuffer, 0, 1024); //clear right screen

    oledWriteString(&ssoled[0], 0, 0/*x*/, 0/*y*/, "#BadDefconAdvice", FONT_NORMAL, 0/*inv color*/, 0);
    oledRectangle(&ssoled[0], 0/*x*/, 8/*y*/, 127/*x*/, 10/*y*/, 1/*color*/, 1/*fill*/);//draw underline
    oledWriteString(&ssoled[0], 0, 42/*x*/, 3/*y*/, "Number", FONT_SMALL, 0/*inv color*/, 0);
    oledWriteString(&ssoled[0], 0, 81/*x*/, 3/*y*/, shorttostring(newmode - 150), FONT_SMALL, 0/*inv color*/, 0);
    oledWriteString(&ssoled[0], 0, 47/*x*/, 4/*y*/, "out of", FONT_SMALL, 0/*inv color*/, 0);
    oledWriteString(&ssoled[0], 0, 58/*x*/, 5/*y*/, "45", FONT_SMALL, 0/*inv color*/, 0);
    oledWriteString(&ssoled[1], 0, 0/*x*/, 0/*y*/, "#BadDefconAdvice", FONT_NORMAL, 0/*inv color*/, 0);
    oledRectangle(&ssoled[1], 0/*x*/, 8/*y*/, 127/*x*/, 10/*y*/, 1/*color*/, 1/*fill*/);//draw underline
    for(byte textrow = 0; textrow < 6; textrow++){ //pull the strings from the advicearray and dump them to screen
      oledWriteString(&ssoled[1], 0, 0/*x*/, textrow + 2/*y*/, advicearray[newmode - 150][textrow], FONT_SMALL, 0/*inv color*/, 0);
    }
    oledDumpBuffer(&ssoled[0], zerobuffer);//redraw the left screen
    oledDumpBuffer(&ssoled[1], onebuffer);//redraw the right screen
  }else if(newmode == 103){//name scroll entry mode
    memset(zerobuffer, 0, 1024); //clear left screen
    memset(onebuffer, 0, 1024); //clear right screen
    EEPROM.get(storedAddress, signature);
    if(signature == WRITTEN_SIGNATURE){//if the eeprom isn't empty
      EEPROM.get(storedAddress+sizeof(signature), enteredname);//get stored name string
    }else{//nothing written to flash yet, use "FEDCON"
      strcpy(enteredname, "FEDCON");//keynotes these days, how many terrorists has the TSA caught in 20 years?... zero
      //memset(enteredname, 0, 17); //clear the entered string
    }
    currententrychar = 0;
    for(int z=0;z<16;z++){//calculate how many characters we have in our array (could've loaded it from flash)
      if(enteredname[z] != '\0'){//if this character isn't null
        currententrychar++; //increment the char we're at
      }
    }
    scrollchar = 65; //start at "A"
    oledWriteString(&ssoled[0], 0, 25/*x*/, 0/*y*/, "Enter Name", FONT_NORMAL, 0/*inv color*/, 0);
    oledRectangle(&ssoled[0], 0/*x*/, 8/*y*/, 127/*x*/, 10/*y*/, 1/*color*/, 1/*fill*/);//draw underline
    //entered chars go on line 3
    for(byte z = 0;z<16;z++){
      oledDrawLine(&ssoled[0], (z*8)+1/*x*/, 34/*y*/, (z*8)+6, 34, 0);
    }
    oledWriteString(&ssoled[0], 0, 4/*x*/, 7/*y*/, "Hold Right When Done", FONT_SMALL, 0/*inv color*/, 0);
    oledWriteString(&ssoled[1], 0, 47/*x*/, 4/*y*/, ">", FONT_NORMAL, 0/*inv color*/, 0); //new cursor
    oledWriteString(&ssoled[1], 0, 72/*x*/, 4/*y*/, "<", FONT_NORMAL, 0/*inv color*/, 0); //new cursor
    namescrollupdatescreen0 = true;
    namescrollupdatescreen1 = true;
    tempstring[1] = '\0';//we're only use the first char
    entrylockout = true; //don't count the button press that is used to enter name scroll entry mode
  }else if(newmode == 104){//actually scroll the name we entered
    scrollspeed = 2; //init at a scroll speed
    namescrollpixelposition = 0;//init
    allcharspainted = false;
    memset(zerobuffer, 0, 1024); //clear the left screen
    memset(onebuffer, 0, 1024); //clear right screen
    memset(logogap, 0, 240); //clear the logo gap buffer
    oledDumpBuffer(&ssoled[0], zerobuffer);//redraw the left screen
    oledDumpBuffer(&ssoled[1], onebuffer);//redraw the right screen
  }//end runmodes
  drawMenuCursor(newmode);
  runmode = newmode;
}

void loop(){
  if(runmode == 100 || runmode == 101){ //graph hb and gsr values
    if(runmode == 100){//normal ops
      hbvalue = analogRead(HBPIN);//read raw HB value
      for(byte z = 0; z < GSRREADINGSPERLOOP; z++){gsrvalue += analogRead(GSRPIN);} //read GSR pin, add to value to average it out
    }else{hbvalue = fakepulsedata[loopval];} //if in fake pulse mode, read from buffer instead of sensor

    if(loopval % 4 == 0 && runmode != 101){ //every 4th time through the loop, if not in fake pulse mode
      gsrvalue /= (4 * GSRREADINGSPERLOOP);//divide by the number of times we read the gsrvalue
      if(triggerfakelie > 0 && triggerfakelie <= 30){//modify the gsr value to make it look like a lie
        gsrvalue = cheatgsrbaseline - cheatgsrvalues[triggerfakelie-1];//drop gsr by cheatgsrvalues[] amount
        triggerfakelie++;
        cheatgsrvalues[30] = gsrvalue; //used to get back to the current gsr value
      }else if(triggerfakelie == 31){//cheat over, get back to live GSR reading
        if(lastgsrvalue > gsrvalue-3 && lastgsrvalue < gsrvalue+3){//back in range of actual GSR value, turn off cheat mode
          if(digitalRead(UPBUTTON) == 1){//not pressing the up button, get out of all cheat modes
            triggerfakelie = 0;//return to not-cheating, don't modify this gsr value  
          }else{//up button pressed, go right to other cheat mode (prevent lie mode)
            triggerfakelie = 100;//enter cheat mode: prevent lie
            memset(zerobuffer+1023, B10000000, 1); //set cheating pixel
          }
          memset(onebuffer+1023, 0, 1); //clear cheating pixel
        }else{//we need to get back to the real gsr value
          if(gsrvalue > lastgsrvalue){//should be the case, we need to increase the fake gsr value
            gsrvalue = lastgsrvalue + random(0,2);//crawl back to the current gsr value with a little randomness
          }else{//the real gsr value dropped faster than our fake gsr (they were actually lieing) 
            gsrvalue = lastgsrvalue - random(0,2);
          }
        }//not in range of actual gsr
      }else if(triggerfakelie == 100){//cheat by preventing lies
        if(lastgsrvalue > gsrvalue-3 && lastgsrvalue < gsrvalue+3 && digitalRead(UPBUTTON) == 1){//if we're in range of the actual gsr value, and not pressing the button, exit cheat mode
          triggerfakelie = 0;
          memset(zerobuffer+1023, 0, 1); //clear cheating pixel
        }
        if(gsrvalue > lastgsrvalue){//real gsr is higher, we need to inrease the fake gsr value
          gsrvalue = lastgsrvalue + random(0,2);//crawl back to the current gsr value with a little randomness, going up semi-quickly is fine
        }else{//the real gsr is lower, we need to decrease the fake gsr value
          if(lastgsrvalue - gsrdelta[gsrdeltaindex] < -8){//we're dropping too fast, it could look like a lie while we're trying to cheat
             gsrvalue = lastgsrvalue - random(-1,2);//stick around 0 until the delta drops more
          }else{//the delta gsr ins't too low/suspicious 
            gsrvalue = lastgsrvalue - random(0,2);//go down
          }
        }
      }//triggerfakelie
      gsrdelta[gsrdeltaindex] = gsrvalue;//store the current value in our history buffer
      gsrdeltaindex++; //increment to next index
      gsrdeltaindex %= 30; //roll over so we don't roll off the end of the array, now it currently points to a sample gsrdelta.length ago
      memset(zerobuffer + 999, 0, 23); //clear the 1-4 digits of deltagsr so we don't have stale digits, and a invalid negative sign
      if(gsrdelta[gsrdeltaindex] != 0){//if we've collected 30 samples, this reading is valid, we don't have space for displaying 5 chars
        if((gsrvalue - gsrdelta[gsrdeltaindex]) <= 999 && (gsrvalue - gsrdelta[gsrdeltaindex]) >= 0){//bounds check, should be over 999 and we don't have the char spacing room anyway
          oledWriteString(&ssoled[0], 0, 108/*x*/, 7/*y*/, shorttostring(gsrvalue - gsrdelta[gsrdeltaindex]), FONT_SMALL, 0/*inv color*/, 0); //valid value, write it to screen
        }else if(gsrvalue - gsrdelta[gsrdeltaindex] >= -999){//bounds check, write a negative number
          oledWriteString(&ssoled[0], 0, 102/*x*/, 7/*y*/, shorttostring(gsrvalue - gsrdelta[gsrdeltaindex]), FONT_SMALL, 0/*inv color*/, 0); //valid value, write it to screen
        }
      }else{//invalid value in gsrdelta[] display a 0 until the buffer fills up
        oledWriteString(&ssoled[0], 0, 108/*x*/, 7/*y*/, "0", FONT_SMALL, 0/*inv color*/, 0); //we have an invalid value, this should be cleared after next reading
      }//end if(gsrdelta[gsrdeltaindex] != 0)

      while(gsrvalue + 6 > gsrmax){ //our gsr maximum display scale is too low
        gsrmax += 16; gsrmin += 16;
        scrollScreenDown(zerobuffer);//scroll down (move the pixels 8 pixels down)
        oledDrawLine(&ssoled[0], 0/*x*/, 55/*y*/, 127/*x*/, 55/*y*/, 0); //(re)draw bottom divider line since we scrolled down
      }
      while(gsrvalue - 6 < gsrmin){ //our gsr minimum display scale is too high
        gsrmax -= 16; gsrmin -= 16;
        //clear pixel row 55 due to scrolling twice
        for(short col = 768; col < 896; col++){ //clear the MSB pixel of last row (where we draw our line)
          zerobuffer[col] = zerobuffer[col] & B01111111;//clear last pixel in this pixel row (row 55)
        }
        scrollScreenUp(zerobuffer);//scroll up (move the pixels 16 pixels up, aka 2 rows)
      }
      if(lastgsrvalue >= 1000 && gsrvalue < 1000){memset(zerobuffer + 935, 0, 11);} //if we have a 3 digit GSR reading and the last reading was 4 digits, clear it out
      if(gsrvalue >= 100){//3 or 4 digit gsr
        oledWriteString(&ssoled[0], 0, 26/*x*/, 7/*y*/, shorttostring(gsrvalue), FONT_SMALL, 0/*inv color*/, 0); //print GSR reading
      }else if(gsrvalue >=10){//2 digit gsr value
        char gsrstring[4];
        strcpy(gsrstring, onezero);
        strcat(gsrstring, shorttostring(gsrvalue));
        oledWriteString(&ssoled[0], 0, 26/*x*/, 7/*y*/, gsrstring, FONT_SMALL, 0/*inv color*/, 0); //print GSR reading
      }else{//1 digit gsr value
        char gsrstring[4];
        strcpy(gsrstring, twozeros);
        strcat(gsrstring, shorttostring(gsrvalue));
        oledWriteString(&ssoled[0], 0, 26/*x*/, 7/*y*/, gsrstring, FONT_SMALL, 0/*inv color*/, 0); //print GSR reading
      }
      
      if(gsrmax - lastgsrvalue > 54){lastgsrvalue = gsrmax - 54; //gsr just shot up, draw the line from the bottom of the graph (not from inside the status bar)
      }else if(lastgsrvalue - gsrmin > 54){lastgsrvalue = gsrmax;} //gsr just dropped drastically, draw the line from the top of the graph (not beyond it which wouldn't work)
      oledDrawLine(&ssoled[0], 126/*x*/, gsrmax - lastgsrvalue/*y*/, 127/*x*/, gsrmax - gsrvalue/*y*/, 0); //graph the new gsr line value (no adjustment needed)
      oledDumpBuffer(&ssoled[0], zerobuffer); //dump screen buffer to display
      lastgsrvalue = gsrvalue;
      scrollScreenLeft(zerobuffer); //scroll the top 7/8 rows of display 0 left
      gsrvalue = 0; //reset value, since we use += when reading new values
    }//end gsr section

    hbsamplebuffer[hbsamplearrayindex] = hbvalue; //put the hbvalue in the buffer
    sampleindex3prior = hbsamplearrayindex - 3; //the index of the sample taken 3 readings ago (used to detect rising vs falling values)
    if(sampleindex3prior < 0){sampleindex3prior += HBHISTORICALREADINGS;} //wrap around if necessary
    hbsamplearrayindex++; //move to next index to store the next hb value
    hbsamplearrayindex %= HBHISTORICALREADINGS; //roll the index over to 0 if necessary
    hbhistoricalavg = 0; //reset the avg, we're going to build this value up
    for(short i = 0; i < HBHISTORICALREADINGS; i++){ //sum up the last hb samples to calc an avg
      hbhistoricalavg += hbsamplebuffer[i]; //add everything in the buffer up
    }
    hbhistoricalavg /= HBHISTORICALREADINGS; //average the readings in the buffer
    if(hbhistoricalavg > 612){hbhistoricalavg = 612; //bound the avg to something useful
    }else if(hbhistoricalavg < 412){hbhistoricalavg = 412;} //bound the avg to something useful

    if(hbrising && (hbvalue > (hbhistoricalavg + THRESHOLDABOVEAVG)) //if we have not detected a peak yet, and the hbvalue is above the threshold
        && hbvalue < hbsamplebuffer[sampleindex3prior]){ //and the reading was just lower than the reading 3 samples prior == peak detected!
      hbdetected[hbdetectedindex] = samplessincelastbeat; //record the number of samples since the last HB peak was detected
      hbrising = false; //don't detect a new HB until we start falling from the next peak
      samplessincelastbeat = 0; //reset the counter of the number of samples since the last hb
      totalreadingsbetweenhbs = 0; //add up the number of samples between detected HB peaks
      validhbsdetected = 0; //how many valid HBs we detected
      for(byte j = NUMHISTORICALHB; j > 0; j--){ //walk backwards through the number of samples between each hb we recorded
        tmpindx = (j + hbdetectedindex) % NUMHISTORICALHB;
        if(hbdetected[tmpindx] > HBSAMPLESPERSEC / 3 && hbdetected[tmpindx] < 2 * HBSAMPLESPERSEC){ //if the samples since last HB were within the range of 30bpm - 180bpm it could be a valid beat
          totalreadingsbetweenhbs += hbdetected[tmpindx]; //add up everything in the array
          validhbsdetected++; //increment the number of valid HBs we detected
        }else{break;//found an invalid sample in our buffer, break out, so we don't detect very old valid hbs (must have 5 valid in a row)
        }
      }//for loop adding up samples between hbs
      hbdetectedindex++; //increment array index for when we find the next HB peak
      hbdetectedindex %= NUMHISTORICALHB; //wrap around to 0

      if(validhbsdetected >= 4){ //if we detected at least 4 heartbeats
        totalreadingsbetweenhbs /= validhbsdetected; //average the total
        lastbpm = bpm; //store the last bpm calculation in lastbpm
        bpm = (HBSAMPLESPERSEC * 60) / totalreadingsbetweenhbs; //actually calculate the bpm
        bpmdelta[bpmdeltaindex] = bpm; //store the average bpm in a historical buffer used to calcualte the delta
        bpmdeltaindex++; //move to next index
        bpmdeltaindex %= 10; //roll over to the oldest historical bpm avg value
        memset(onebuffer + 990, 0, 12); //the number of chars in this displayed value is changing, clear the screen so we don't have stale chars.
        if(bpmdelta[bpmdeltaindex] != 0 && (bpm - bpmdelta[bpmdeltaindex] >= -99 && bpm - bpmdelta[bpmdeltaindex] <= 99)){ //if we've collected 10 samples, and within bounds, this reading is valid, we don't have space for displaying 4 chars
          oledWriteString(&ssoled[1], 0, 88/*x*/, 7/*y*/, shorttostring(bpm - bpmdelta[bpmdeltaindex]), FONT_SMALL, 0/*inv color*/, 0); //valid value, write it to screen
        }else{
          oledWriteString(&ssoled[1], 0, 88/*x*/, 7/*y*/, "-", FONT_SMALL, 0/*inv color*/, 0); //we have an invalid value, clear delta bpm
        } //end if(bpmdelta[bpmdeltaindex] != 0)

        if(lastbpm == 0 || (lastbpm >= 100 && bpm < 100)){ //if the last bpm was 3 digits and now it is 2, or if it was 0/"-", clear extra characters
          memset(onebuffer + 935, 0, 5); //clear the third digit/char
        }
        oledWriteString(&ssoled[1], 0, 26/*x*/, 7/*y*/, shorttostring(bpm), FONT_SMALL, 0/*inv color*/, 0); //print BPM reading
      }else{ //we haven't detected 5 hbs yet, don't calc a BPM
        bpm = 0;
        memset(onebuffer + 990, 0, 12); //clear the delta bpm extra chars
        oledWriteString(&ssoled[1], 0, 88/*x*/, 7/*y*/, "-", FONT_SMALL, 0/*inv color*/, 0); //clear delta bpm
        memset(bpmdelta, 0, 10); //clear the buffer containing historical delta bpm (because we check if it == 0)
        oledWriteString(&ssoled[1], 0, 26/*x*/, 7/*y*/, "N/A", FONT_SMALL, 0/*inv color*/, 0); //clear the BPM reading
      }
    }else if(!hbrising && hbvalue < hbhistoricalavg){ //if falling back below the average after a previous peak
      hbrising = true; //now we can start looking for the next peak
    }else if(samplessincelastbeat == 2 * HBSAMPLESPERSEC){ //it has been 2 sec since last HB (bpm would be <30)
      hbrising = true; //don't detect a new HB until we start falling from the next peak
      bpm = 0;
      memset(onebuffer + 990, 0, 12); //clear the delta bpm extra chars
      oledWriteString(&ssoled[1], 0, 88/*x*/, 7/*y*/, "-", FONT_SMALL, 0/*inv color*/, 0); //clear delta bpm
      memset(bpmdelta, 0, 10); //clear the buffer containing historical delta bpm (because we check if it == 0)
      oledWriteString(&ssoled[1], 0, 26/*x*/, 7/*y*/, "N/A", FONT_SMALL, 0/*inv color*/, 0); //clear the BPM reading
    }//end if a hb is detected

    if(samplessincelastbeat <= (2 * HBSAMPLESPERSEC)){ //if we have detected a HB in the last 2 seconds
      samplessincelastbeat++;//increment the number of samples counter, the if-condition stops this crom incrementing higher than (2*HBSAMPLESPERSEC)+1
      if(!pulseledstatus && hbvalue > (hbhistoricalavg + THRESHOLDABOVEAVG) && validhbsdetected >= 4){ //blink onboard led during pulse peak
        if(runmode == 100){//normal ops
          heartFill(true, onebuffer); //draw a filled in heart icon and lines around it
          digitalWrite(PIN_LED2, LOW); //turn on led
        }else if(runmode == 101){//in fake pulse mode
          heartFill(true, onebackbuffer); //draw a filled in heart icon and lines around it
          if(enablefakepulseled == true){//leds are enabled
            digitalWrite(HBLED, LOW); //turn on HB sensor LED
            digitalWrite(PIN_LED2, LOW); //turn on the led
          }
        }
        pulseledstatus = true;
      }else if(pulseledstatus && hbvalue < (hbhistoricalavg + THRESHOLDABOVEAVG) || samplessincelastbeat == 2 * HBSAMPLESPERSEC + 1){ //if the pulse led/hearticon is on and it shouldn't be
        digitalWrite(PIN_LED2, HIGH); //turn off led
        if(runmode == 100){
          heartFill(false, onebuffer); //draw a hollow heart icon
        }else if(runmode == 101){//in fake pulse mode
          heartFill(false, onebackbuffer); //draw a hollow heart icon
          if(enablefakepulseled == true){//if leds are enabled
            digitalWrite(HBLED, HIGH);//turn off HB sensor LED 
          }
        }//HB led off
        pulseledstatus = false;
      }
    }

    if(runmode == 100){//this is junk data in fake pulse mode
      String serialstring = "Thresh:";//build up a string to output over serial (faster than multiple calls to Serial.print())
      serialstring.concat(hbhistoricalavg + THRESHOLDABOVEAVG);
      serialstring += " HB:";
      serialstring.concat(hbvalue);
      serialstring += " BPM:";
      serialstring.concat(bpm);
      serialstring += " GSR:";
      serialstring.concat(lastgsrvalue);
      serialstring += "\n";
      Serial.print(serialstring);
      //Serial.print(" HB_AVG:"); //for debugging
      //Serial.print(hbhistoricalavg);
    }
    hbvalue = (1023 - hbvalue) / 15; //invert the value for the display, use 0-54 output value, and use the bottom row of pixels for text
    if(hbvalue > 53){hbvalue = 53;} //set a lowerbound on what we will graph so we don't interfere with the bottom boarder or text. Leave a 1 pixel gap so it is easy to see low readings

    if(runmode == 100){
      oledDrawLine(&ssoled[1], 126/*x*/, lasthbvalue/*y*/, 127/*x*/, hbvalue/*y*/, 0); //graph hb value line
      oledDumpBuffer(&ssoled[1], onebuffer); //dump screen buffer to display
      scrollScreenLeft(onebuffer); //scroll the top 7/8 rows of the display
    }else{//101 fake pulse mode
      for(byte z=0;z<7;z++){//write the fake pulse wave to the right side column (but not the last row)
        memset(onebackbuffer+127+(z*128), fakepulsewave[loopval+(z*49)], 1);
      }
      pretime = micros();
      oledDumpBuffer(&ssoled[1], onebackbuffer); //dump screen buffer to display
      oledDumpBuffer(&ssoled[0], onebackbuffer);
      posttime = micros();
      if(posttime - pretime < 18000){//each refresh will take a different amount of time, make that static so it doesn't look jumpy
        delayMicroseconds(18000 - (posttime - pretime));
      }
      if(loopval == 48){
        loopval = 255; //if we've displayed all fake hb values, roll over to 0
      }
      scrollScreenLeft(onebackbuffer); //scroll the top 7/8 rows of the display
    }
    lasthbvalue = hbvalue; //store current hb value as last hb value for next time through the loop
    //end hb section
  }else if(runmode >= 10 && runmode < 100 || runmode >= 150 && runmode <= 195){ //end runmode==100, runmode in a menus or baddcadvice
    detectUSBPower(); //check if USB is plugged in, and disable batt vreg if it is
    delay(8);//slow down so button presses aren't double counted
    if(runmode == 80 && !(loopval % 32)){ //in GSR calibration mode
      oledWriteString(&ssoled[1], 0, 39/*x*/, 6/*y*/, "     ", FONT_STRETCHED, 0/*inv color*/, 0);
      gsrvalue = analogRead(GSRPIN);
      if(gsrvalue > 999){gsrvalue = 999;}//how did you accomplish this?
      if(gsrvalue >= 100){//3 digit
        oledWriteString(&ssoled[1], 0, 39/*x*/, 6/*y*/, shorttostring(gsrvalue), FONT_STRETCHED, 0/*inv color*/, 1);
      }else if(gsrvalue >= 10){//2 digit
        char gsrstring[4];
        strcpy(gsrstring, onezero);
        strcat(gsrstring, shorttostring(gsrvalue));
        oledWriteString(&ssoled[1], 0, 39/*x*/, 6/*y*/, gsrstring, FONT_STRETCHED, 0/*inv color*/, 1);
      }else{//1 digit
        char gsrstring[4];
        strcpy(gsrstring, twozeros);
        strcat(gsrstring, shorttostring(gsrvalue));
        oledWriteString(&ssoled[1], 0, 39/*x*/, 6/*y*/, gsrstring, FONT_STRETCHED, 0/*inv color*/, 1);
      }
    }else if(runmode >= 50 && runmode < 53){ //screen brightness modes
      if(loopval == 255){ updateMode(20);}//used for display timeouts, when loopval is previously set to 0, go back to options menu
    }else if(runmode == 40 && konami == 8){ //help sub menu
      do{//flashy
        oledFill(&ssoled[0], 0, 1);//cls
        oledFill(&ssoled[1], 0, 1);//cls
        digitalWrite(PIN_LED2, HIGH); //builtin bottom blue led on the xiao
        digitalWrite(PIN_LED3, LOW); //builtin top blue led on the xiao
        digitalWrite(HBLED, LOW); //green hb led
        delay(140);
        oledFill(&ssoled[0], 255, 1);//uncls
        oledFill(&ssoled[1], 255, 1);//uncls
        digitalWrite(PIN_LED2, LOW); //builtin bottom blue led on the xiao
        digitalWrite(PIN_LED3, HIGH); //builtin top blue led on the xiao
        digitalWrite(HBLED, HIGH); //green hb led
        delay(140);
      }while(digitalRead(LEFTBUTTON) == 1 && digitalRead(RIGHTBUTTON) == 1 && digitalRead(UPBUTTON) == 1 && digitalRead(DOWNBUTTON) == 1);//no button press
      oledWriteString(&ssoled[0], 0, 1/*x*/, 3/*y*/, "gigsatdc.com/torbadge", FONT_SMALL, 0/*inv color*/, 1);
      oledWriteString(&ssoled[1], 0, 1/*x*/, 3/*y*/, "gigsatdc.com/torbadge", FONT_SMALL, 0/*inv color*/, 1);
      delay(1000); //could you give it a second?!
      while(digitalRead(LEFTBUTTON) == 1 && digitalRead(RIGHTBUTTON) == 1 && digitalRead(UPBUTTON) == 1 && digitalRead(DOWNBUTTON) == 1){
        digitalWrite(PIN_LED2, HIGH); //builtin bottom blue led on the xiao
        digitalWrite(PIN_LED3, LOW); //builtin top blue led on the xiao
        digitalWrite(HBLED, LOW); //green hb led
        delay(200);
        digitalWrite(PIN_LED2, LOW); //builtin bottom blue led on the xiao
        digitalWrite(PIN_LED3, HIGH); //builtin top blue led on the xiao
        digitalWrite(HBLED, HIGH); //green hb led
        delay(200);
      }//wait for a button press
      digitalWrite(PIN_LED2, HIGH); //builtin bottom blue led on the xiao
      digitalWrite(PIN_LED3, HIGH); //builtin top blue led on the xiao
      digitalWrite(HBLED, HIGH); //green hb led
      updateMode(10);//back to menu
    }else if(runmode == 40){
      uptime = (millis() - boottime) / 60000;
      if(uptime < 10){oledWriteString(&ssoled[1], 0, 68/*x*/, 7/*y*/, shorttostring(uptime), FONT_SMALL, 0/*inv color*/, 1);//dump uptime in min, 1 digit
      }else if(uptime < 100){oledWriteString(&ssoled[1], 0, 62/*x*/, 7/*y*/, shorttostring(uptime), FONT_SMALL, 0/*inv color*/, 1);//dump uptime in min, 2 digits
      }else if(uptime < 1000){oledWriteString(&ssoled[1], 0, 56/*x*/, 7/*y*/, shorttostring(uptime), FONT_SMALL, 0/*inv color*/, 1);//dump uptime in min, 3 digits
      }else if(uptime < 10000){oledWriteString(&ssoled[1], 0, 50/*x*/, 7/*y*/, shorttostring(uptime), FONT_SMALL, 0/*inv color*/, 1);//dump uptime in min, 4 digits
      }else if(uptime < 65536){oledWriteString(&ssoled[1], 0, 44/*x*/, 7/*y*/, shorttostring(uptime), FONT_SMALL, 0/*inv color*/, 1);//dump uptime in min, 5 digits
      }
    }else if(runmode >= 150 && runmode <= 195){ //bad dc advice
      if(millis() - pretime > 30000){//we've been on this advice for a bit, move on
        runmode++;
        if(runmode == 196){runmode = 150;} //roll over
        updateMode(runmode);//step through the baddefconadvice modes
      }
    }
  }else if(runmode == 102){ //logobuffer scroll mode
    if(scrollspeed > 0){//if we're not frozen
      scrollLogoRight(zerobuffer, onebuffer); //scroll the logo across both screens (and account for the gap)
      if(loopval % scrollspeed == 0){ //affects how many pixels we're moving the image each screen refresh, 1,2,4,8,16,32
        oledDumpBuffer(&ssoled[0], zerobuffer); //dump screen buffer to display
        oledDumpBuffer(&ssoled[1], onebuffer); //dump screen buffer to display
      }
    }else{//scrollspeed == 0, they could've inverted the colors
      oledDumpBuffer(&ssoled[0], zerobuffer); //dump screen buffer to display
      oledDumpBuffer(&ssoled[1], onebuffer); //dump screen buffer to display
    }
  }else if(runmode == 103){//namescroll entry mode
    for(byte z=0;z<7;z++){//draw 7 chars vertically on the right screen, used for char input
      if(scrollchar+z-3 < 32){//can't draw an ascii char lower than 'space' time to wrap
        tempstring[0] = (char)(scrollchar+z-3+95);
      }else if(scrollchar+z-3 > 126){//can't draw an ascii char above ~ time to wrap
        tempstring[0] = (char)(scrollchar+z-3-95);
      }else{//in range, no wrapping
        tempstring[0] = (char)(scrollchar+z-3);
      }
      oledWriteString(&ssoled[1], 0, 59/*x*/, z+1/*y*/, tempstring, FONT_NORMAL, 0/*inv color*/, 0);//write the character in the correct row (1-7)
    }
    if(namescrollupdatescreen0 == true){//if there were changes to the left screen, update it
      oledWriteString(&ssoled[0], 0, 0/*x*/, 3/*y*/, enteredname, FONT_NORMAL, 0/*inv color*/, 0);
      if(currententrychar < 16){//draw arrow (this is only an issue when we load a 16 char string from flash)
        memset(zerobuffer+640, 0, 128);//clear all arrows, so we don't have to figure out if we're going left or right
        memset(zerobuffer+640+(currententrychar*8), B00011000, 1);memset(zerobuffer+641+(currententrychar*8), B00001100, 1);//up arrow pointing at current character
        memset(zerobuffer+642+(currententrychar*8), B00000110, 1);memset(zerobuffer+643+(currententrychar*8), B11111111, 2);
        //memset(zerobuffer+644+(currententrychar*8), B11111111, 1);
        memset(zerobuffer+645+(currententrychar*8), B00000110, 1);memset(zerobuffer+646+(currententrychar*8), B00001100, 1);memset(zerobuffer+647+(currententrychar*8), B00011000, 1);
      }
      oledDumpBuffer(&ssoled[0], zerobuffer);//dump screen buffer to display
      namescrollupdatescreen0 = false;//we have the latest changes on screen, don't need to update next loop
    }
    if(namescrollupdatescreen1 == true){//if there were changes to the right screen, update it
      oledDumpBuffer(&ssoled[1], onebuffer);//dump screen buffer to display
      namescrollupdatescreen1 = false;//we have the latest changes on screen, don't need to update next loop
    }
    delay(2);//need a delay for when a button is held down, especially when no screens are updated (loopval increments too fast otherwise)
  }else if(runmode == 104){//name scroll mode
    if(scrollspeed > 0){//if we're not frozen, add new pixels or scroll
      if(allcharspainted == false && (enteredname[namescrollpixelposition/16]) != '\0'){//if we haven't dumped all the characters, dump some more pixels to the screen
        short charval = (64*((enteredname[namescrollpixelposition/16])-32))+namescrollpixelposition%16;//guess how long this took to get right
        memcpy(onebuffer+383, ucBigFont+charval, 1);//copy the 4 rows from the charmap to the screen
        memcpy(onebuffer+511, ucBigFont+charval+16, 1);
        memcpy(onebuffer+639, ucBigFont+charval+32, 1);
        memcpy(onebuffer+767, ucBigFont+charval+48, 1);
        if(namescrollpixelposition == 255){allcharspainted = true;}//16 chars, all painted
        namescrollpixelposition++;//go to next pixel
      }else if(enteredname[namescrollpixelposition/16] == '\0'){//if we have less than 16 chars, and we hit a null char, the string is ended
        allcharspainted = true;
      }
  
      for (uint8_t z = 0; z < 8; z++) {//shift everything left one pixel and account for screen gap
        memcpy(logogap + ((z + 1) * 30) - 1, onebuffer + (z * 128), 1); //take the left most column of right buffer and move it to the right most column from logogap
        memmove(logogap + (z * 30), logogap + (z * 30) + 1, 29); //move logogap columns to the left
        memmove(onebuffer + (z * 128), onebuffer + 1 + (z * 128), 127); //move right screen to the left
        memcpy(onebuffer + ((z + 1) * 128) - 1, zerobuffer + (z * 128), 1); //take far left columns on left screen and move them to far right columns of right screen
        memmove(zerobuffer + (z * 128), zerobuffer + 1 + (z * 128), 127); //move left screen to the left
        memcpy(zerobuffer + ((z + 1) * 128) - 1, logogap + (z * 30), 1); //move left most column of gap buffer to the right most column of left screen 
      }
      if(loopval % scrollspeed == 0){ //affects how many pixels we're moving the image each screen refresh, 1,2,4,8,16,32
        oledDumpBuffer(&ssoled[0], zerobuffer); //dump screen buffer to display
        oledDumpBuffer(&ssoled[1], onebuffer); //dump screen buffer to display
      }
    }else{//scrollspeed == 0, they could've inverted the colors
      oledDumpBuffer(&ssoled[0], zerobuffer); //dump screen buffer to display
      oledDumpBuffer(&ssoled[1], onebuffer); //dump screen buffer to display
    }
  }//end runmode main loop
  checkButtons(); //check for button presses
  loopval++;
}//mainloop

void scrollScreenLeft(uint8_t screenbuffer[]){ //scroll the top 7/8 rowsleft , but not row 55 (used for separation line)
  //row by row of 8 pixels high, except the last row -funroll-loops!
  memmove(screenbuffer, screenbuffer + 1, 127); //move a column of 8 pixels to the left for the entire row
  memmove(screenbuffer + 128, screenbuffer + 129, 127); //move a column of 8 pixels to the left for the entire row
  memmove(screenbuffer + 256, screenbuffer + 257, 127); //move a column of 8 pixels to the left for the entire row
  memmove(screenbuffer + 384, screenbuffer + 385, 127); //move a column of 8 pixels to the left for the entire row
  memmove(screenbuffer + 512, screenbuffer + 513, 127); //move a column of 8 pixels to the left for the entire row
  memmove(screenbuffer + 640, screenbuffer + 641, 127); //move a column of 8 pixels to the left for the entire row
  memmove(screenbuffer + 768, screenbuffer + 769, 127); //move a column of 8 pixels to the left for the entire row

  //clear last column on row 0-5 -funroll-loops!
  memset(screenbuffer + 127, 0, 1);
  memset(screenbuffer + 255, 0, 1);
  memset(screenbuffer + 383, 0, 1);
  memset(screenbuffer + 511, 0, 1);
  memset(screenbuffer + 639, 0, 1);
  memset(screenbuffer + 767, 0, 1);
  memset(screenbuffer + 895, B10000000, 1); //clear the last column on row 6, except for the horizontal divider line, set one pixel so it will be moved over
}

void scrollLogoRight(uint8_t left[], uint8_t right[]){ //scroll the tor logo right across 2 screens, and account for the gap between screens
  for (uint8_t z = 0; z < 8; z++) {
    memcpy(logogap + (z * 30), left + ((z + 1) * 128) - 1, 1); //move right most column of left screen to left most column of gap buffer
    memmove(logogap + (z * 30) + 1, logogap + (z * 30), 29); //move logogap columns to the right
    memmove(left + (z * 128) + 1, left + (z * 128), 127); //move the left screen columns right
    memcpy(left + (z * 128), right + ((z + 1) * 128) - 1, 1); //take far left columns of each row on right screen and move them to far left columns of left screen
    memmove(right + 1 + (z * 128), right + (z * 128), 127); //move right screen to the right
    memcpy(right + (z * 128), logogap + ((z + 1) * 30) - 2, 1); //take the second to right most column from logogap and move it to the left most column of right buffer (the furthest right column of logo gap we ignore)
  }
}

void scrollScreenUp(uint8_t screenbuffer[]){ //scoll screen up, move pixels up by 16 pixels (two rows)
  for (byte row = 0; row < 5; row++) {
    memmove(screenbuffer + (row * 128), screenbuffer + ((row + 2) * 128), 128); //move data from two rows down to this row, 128 columns wide (screen width)
  }
  memset(screenbuffer + 640, 0, 128); //clear row 5
  memset(screenbuffer + 768, B10000000, 128); //clear row 6, leave the divider line
}

void scrollScreenDown(uint8_t screenbuffer[]){ //scroll screen down, move pixels down by 16 pixels (two rows)
  for (byte row = 6; row > 1; row--) { //don't need to do row 0/1, it will be blank after scrolling
    memmove(screenbuffer + (row * 128), screenbuffer + ((row - 2) * 128), 128); //move data from two rows up to this row, 128 columns wide (screen width)
  }
  memset(screenbuffer, 0, 256);//clear first two rows
}

void heartFill(boolean infill, uint8_t *buf){ //draw the heart icon filled in when a peak is detected, otherwise make it hollow
  buf[1011] = B00001110; //left side of the heart, each bit is a row from the bottom up
  if (infill) { //filled in heart icon with side lines
    buf[1008] = B00011100; buf[1009] = B00100010; buf[1012] = B00011111; buf[1013] = B00111111; buf[1014] = B01111110;
    buf[1015] = B11111100; buf[1016] = B01111110; buf[1017] = B00111111; buf[1018] = B00011111; buf[1021] = B00100010; buf[1022] = B00011100;
  } else { //hollow heart icon, clear side lines too
    buf[1008] = 0; buf[1009] = 0; buf[1012] = B00010001; buf[1013] = B00100001; buf[1014] = B01000010;
    buf[1015] = B10000100; buf[1016] = B01000010; buf[1017] = B00100001; buf[1018] = B00010001; buf[1021] = 0; buf[1022] = 0;
  }
  buf[1019] = B00001110;//right side of the heart, each bit is a row from the bottom up
}

void checkButtons(){ //check for button presses
  if(digitalRead(RIGHTBUTTON) == 0){ //button being pressed
    if(rightdebounce == 0){ //button previously up (just pressed)
      rightdebounce = 1; //next loop we'll know it is being held down
      if(runmode == 100){//normal ops
        if(cheatsenabled == true && triggerfakelie == 0 && lastgsrvalue > 50){//lets fuck with people, fake the GSR data so it looks like a lie, only if we're not already cheating, and make sure we don't go negative
          triggerfakelie = 1;//move through the cheat mode
          cheatgsrbaseline = lastgsrvalue;//set the gsr value we're going to offset from
          memset(onebuffer+1023, B10000000, 1); //set cheating pixel
        }
      }else if(runmode == 10){updateMode(100); //selected 'Start Lie Detector' from main menu
      }else if(runmode == 11){updateMode(20); //selected 'Options' from main menu
      }else if(runmode == 12){
        updateMode(30); //selected 'Bling' from the main menu
      }else if(runmode == 13){updateMode(40); //selected 'Help" from main menu
      }else if(runmode == 20){updateMode(screenbrightness); //selected 'screen brightness' from options menu
      }else if(runmode == 21){goToSleep(); //selected 'GSR scroll speed' from options menu
      }else if(runmode == 22){updateMode(80); //selected 'Calibrate GSR' from options menu
      }else if(runmode == 23){
        cheatsenabled = !cheatsenabled; //selected 'Cheat Modes' from the options menu
        if(cheatsenabled == true){oledWriteString(&ssoled[1], 0, 83/*x*/, 6/*y*/, "On ", FONT_SMALL, 0/*inv color*/, 1);
        }else{oledWriteString(&ssoled[1], 0, 83/*x*/, 6/*y*/, "Off", FONT_SMALL, 0/*inv color*/, 1);}
      }else if(runmode == 30){updateMode(101); //selected 'Fake Pulse' from bling menu
      }else if(runmode == 31){updateMode(102); //selected 'logo scroll' from bling menu
      }else if(runmode == 32){ //selected 'bad defcon advice' from bling menu
        if(currentadvice > 45){currentadvice = 0;} //something's wrong, reset
        updateMode(150 + currentadvice); //show the last bad defcon advice string
      }else if(runmode == 33){updateMode(103);//name entry scroll
      }else if(runmode >= 50 && runmode <= 52){ //currently setting granular screen brightness
        screenbrightness++;//step through low->med->high
        if(screenbrightness == 53){screenbrightness = 50;} //roll over
        updateMode(screenbrightness);
      }else if(runmode == 80){updateMode(22);//go back to options->GSR scroll being selected (from granular calibration selection)
      }else if (runmode == 40){ //in detailed help menu
        if(konami == 5 || konami == 7){konami++;
        }else{konami = 0;} //reset
      }else if(runmode >= 150 && runmode <= 195){ //showing bad defcon advice
        loopval = 120;//set it +1 over the press and hold value, so initial button press requires a longer hold
        runmode++;
        if(runmode == 196){runmode = 150;} //roll over
        updateMode(runmode);//step through the baddefconadvice modes
      }else if(runmode == 102){invertScreenPixels();//tor logo mode, invert pixels
      }else if(runmode == 103){//namescroll entry mode
        loopval = 0; //used to determine how long the button was pushed
        entrylockout = false;
      }else if(runmode == 104 && allcharspainted == true){//namescroll mode, invert colors if we've laid down all the pixels
        invertScreenPixels();
      }
    }else{ //debounce == 1, button being held down
      if(runmode >= 150 && runmode <= 195 && loopval == 10){//if we're holding the button down in baddcadvice, move to the next string after a short delay
        loopval = 0;//reset so held buttons loop quickly
        runmode++;
        if(runmode == 196){runmode = 150;} //roll over
        updateMode(runmode);//step through the baddefconadvice modes
      }else if(runmode == 103 && currententrychar != 0 && entrylockout == false){//if we're in name scroll entry mode, enetered at least one char, and we are holding the right button down
        if(loopval == 255){
          enteredname[currententrychar] = '\0';//end it at the current char
          EEPROM.put(storedAddress, WRITTEN_SIGNATURE);
          EEPROM.put(storedAddress+sizeof(signature), enteredname);
          EEPROM.commit();
          updateMode(104);
        }//don't let it roll over, used for button-up logic
      }
    }
  }else{ //button not pressed
    if(runmode == 103 && rightdebounce == 1 && entrylockout == false){//name scroll entry mode, and the button just came up, and we've entered at least one char
      if(loopval < 255 && currententrychar < 16){//short button press, on button up enter character
        enteredname[currententrychar] = scrollchar; //store current selection at the current char
        currententrychar++; //move to next char
        namescrollupdatescreen0 = true;
        if(currententrychar == 16){
          oledWriteString(&ssoled[0], 0, 0/*x*/, 3/*y*/, enteredname, FONT_NORMAL, 0/*inv color*/, 0);
          oledDumpBuffer(&ssoled[0], zerobuffer);//dump screen buffer to display
          delay(250);//show the last character on screen for a bit before moving to scrolling
          EEPROM.put(storedAddress, WRITTEN_SIGNATURE);
          EEPROM.put(storedAddress+sizeof(signature), enteredname);
          EEPROM.commit();
          updateMode(104);
        }
      }else if(entrylockout == false && currententrychar == 16){//we loaded the 16char string from flash, and right button was pressed
        updateMode(104);
      }
    }//runmode == 103
    rightdebounce = 0; //reset debounce
  }//end right button
  if(digitalRead(LEFTBUTTON) == 0){ //button being pressed
    if(leftdebounce == 0){ //button previously up (just pressed)
      leftdebounce = 1; //next loop we'll know it is being held down
      if(runmode == 100){updateMode(10);//back out of lie detector to main menu
      }else if(runmode == 101){updateMode(30);//back out of fake peak mode to options menu
      }else if(runmode == 102){updateMode(31);//back out of logoscroll to options menu
      }else if(runmode == 103){//name scroll entry mode
        if(currententrychar == 0){//can't delete any more chars, back out
          updateMode(33);//back out of namescroll entry mode to options menu
        }else{
          currententrychar--;
          memset(zerobuffer+384+(currententrychar*8), 0, 8);//clear all current characters
          enteredname[currententrychar] = '\0';//delete the last char we entered
          namescrollupdatescreen0 = true;
        }
      }else if(runmode == 104){updateMode(33);//back out of namescroll to options menu
      }else if(runmode >= 150 && runmode <= 195){updateMode(32);//back out of baddefconadvice
      }else if(runmode >= 20 && runmode < 30){updateMode(11);//back out of options menu
      }else if(runmode >= 30 && runmode < 40){updateMode(12);//back out of bling menu
      }else if(runmode == 40){ //in detailed help menu
        if(konami == 4 || konami == 6){konami++;
        }else{konami = 0;updateMode(13);} //reset and back out
      }else if(runmode >= 50 && runmode < 53){updateMode(20);//go back to options->screenbrightness being selected (from granular screen brightness selection)
      }else if (runmode == 80){updateMode(22);//go back to options->GSR scroll being selected (from granular calibration selection)
      }
    }else{ //debounce == 1, button being held down
    }
  }else{ //button not pressed
    leftdebounce = 0; //reset debounce
  }//end left button
  if(digitalRead(UPBUTTON) == 0){ //button being pressed
    if(updebounce == 0){ //button previously up (just pressed)
      updebounce = 1; //next loop we'll know it is being held down
      if(runmode == 100){//normal ops
        if(cheatsenabled == true && triggerfakelie == 0){//lets fuck with people, fake the GSR data so it looks like you're not lieing, only if we're not already cheating
          triggerfakelie = 100;//move through the cheat mode
          memset(zerobuffer+1023, B10000000, 1); //set cheating pixel
        }
      }else if(runmode >= 10 && runmode < 40){ //in menu, or submenu, move cursor up
        tempmode = runmode - 1;
        if(tempmode == 9){tempmode = 13; //bounds check for main menu
        }else if(tempmode == 19){tempmode = 23; //bound check for options sub menu
        }else if(tempmode == 29){tempmode = 33; //bounds check for bling menu
        }
        updateMode(tempmode);
      }else if (runmode >= 50 && runmode <= 52){ //currently setting granular screen brightness
        screenbrightness++;//step through low->med->high
        if(screenbrightness == 53){screenbrightness = 50;} //roll over
        updateMode(screenbrightness);
      }else if(runmode == 80){updateMode(22);//go back to options->GSR scroll being selected (from granular calibration selection)
      }else if(runmode == 40){ //in detailed help menu
        if(konami <= 1){konami++;
        }else{konami = 0;} //reset
      }else if(runmode == 101){ //fake pulse mode, turn on/off leds
         enablefakepulseled = !enablefakepulseled; //enable/disable leds
         if(enablefakepulseled == true){//show status change
           oledWriteString(&ssoled[0], 0, 8/*x*/, 3/*y*/, "LEDs On", FONT_STRETCHED, 1/*inv color*/, 1);
           oledWriteString(&ssoled[1], 0, 8/*x*/, 3/*y*/, "LEDs On", FONT_STRETCHED, 1/*inv color*/, 1);
           delay(1000);
         }else{
           digitalWrite(HBLED, HIGH); //turn off HB sensor LED
           digitalWrite(PIN_LED2, HIGH); //turn off the led
           oledWriteString(&ssoled[0], 0, 0/*x*/, 3/*y*/, "LEDs Off", FONT_STRETCHED, 1/*inv color*/, 1);
           oledWriteString(&ssoled[1], 0, 0/*x*/, 3/*y*/, "LEDs Off", FONT_STRETCHED, 1/*inv color*/, 1);
           delay(1000);
         }
      }else if(runmode == 102){ //logo scroll speed adjust 0,1,2,4,8,16
        if(scrollspeed == 0){scrollspeed = 1;//can't *2 from 0
        }else if(scrollspeed != 32){scrollspeed *= 2;} //increase the scroll speed, unless we're at max (32)
      }else if(runmode >= 150 && runmode <= 195){
        loopval = 120;//set it over the press and hold value, so initial button press requires a longer hold
        runmode++;
        if(runmode == 196){runmode = 150;} //roll over
        updateMode(runmode);//step through the baddefconadvice modes
      }else if(runmode == 103){//name scroll entry  mode
        loopval = 55;//set it over the press and hold value, so initial button press requires a longer hold
        scrollchar--;
        if(scrollchar < 32){scrollchar = 126;}//wrap
        namescrollupdatescreen1 = true;
      }else if(runmode == 104){
        if(scrollspeed == 0){scrollspeed = 1;//can't *2 from 0
        }else if(scrollspeed != 32){scrollspeed *= 2;} //increase the scroll speed, unless we're at max (32)
      }
    }else{ //debounce == 1, button being held down
      if(runmode >= 150 && runmode <= 195 && loopval == 10){//if we're holding the button down in baddcadvice, move to the next string after a short delay
        loopval = 0;//reset so held button loops quickly
        runmode++;
        if(runmode == 196){runmode = 150;} //roll over
        updateMode(runmode);//step through the baddefconadvice modes
      }else if(runmode == 103 && loopval == 9){//name scroll entry mode, press and hold continues to scroll
        loopval = 0;//reset so held button loops quickly
        scrollchar--;
        if(scrollchar < 32){scrollchar = 126;}//wrap
        namescrollupdatescreen1 = true;
      }
    }
  }else{ //button not pressed
    updebounce = 0; //reset debounce
  }//end upbutton
  if(digitalRead(DOWNBUTTON) == 0){ //button being pressed
    if(downdebounce == 0){ //button previously up (just pressed)
      downdebounce = 1; //next loop we'll know it is being held down
      if(runmode >= 10 && runmode < 40){ //in menu, or submenu, move cursor down
        tempmode = runmode + 1;
        if(tempmode == 14){tempmode = 10; //bounds check main menu
        }else if(tempmode == 24){tempmode = 20; //bounds check options sub menu
        }else if(tempmode == 34){tempmode = 30;} //bounds check bling sub menu
        updateMode(tempmode);
      }else if(runmode >= 50 && runmode <= 52){ //currently setting granular screen brightness
        screenbrightness--;//step through low->med->high
        if(screenbrightness == 49){screenbrightness = 52;} //roll over
        updateMode(screenbrightness);
      }else if(runmode == 80){
        updateMode(22);//go back to options->GSR scroll being selected (from granular calibration selection)
      }else if(runmode == 40){ //in detailed help menu
        if(konami == 2 || konami == 3){konami++;
        }else{konami = 0; //reset
        }
      }else if(runmode == 101){ //fake pulse mode, turn on/off leds
         enablefakepulseled = !enablefakepulseled; //enable/disable leds
         if(enablefakepulseled == true){//show status change
           oledWriteString(&ssoled[0], 0, 8/*x*/, 3/*y*/, "LEDs On", FONT_STRETCHED, 1/*inv color*/, 1);
           oledWriteString(&ssoled[1], 0, 8/*x*/, 3/*y*/, "LEDs On", FONT_STRETCHED, 1/*inv color*/, 1);
           delay(1000);
         }else{
           digitalWrite(HBLED, HIGH); //turn off HB sensor LED
           digitalWrite(PIN_LED2, HIGH); //turn off the led
           oledWriteString(&ssoled[0], 0, 0/*x*/, 3/*y*/, "LEDs Off", FONT_STRETCHED, 1/*inv color*/, 1);
           oledWriteString(&ssoled[1], 0, 0/*x*/, 3/*y*/, "LEDs Off", FONT_STRETCHED, 1/*inv color*/, 1);
           delay(1000);
         }
      }else if(runmode == 102){
        scrollspeed /= 2; //slow down the scroll speed
      }else if(runmode >= 150 && runmode <= 195){
        loopval = 120;//set it over the press and hold value, so initial button press requires a longer hold
        runmode--;
        if(runmode == 149){runmode = 195;} //roll over
        updateMode(runmode);//step through the baddefconadvice modes
      }else if(runmode == 103){//name scroll entry mode
        loopval = 55;//set it over the press and hold value, so initial button press requires a longer hold
        scrollchar++;
        if(scrollchar > 126){scrollchar = 32;}//wrap
        namescrollupdatescreen1 = true;
      }else if(runmode == 104){
        scrollspeed /= 2;//slow down the scroll speed
      }
    }else{ //debounce == 1, button being held down
      if(runmode >= 150 && runmode <= 195 && loopval == 10){//if we're holding the button down in baddcadvice, move to the next string after a short delay
        loopval = 0;//reset so held butotn loops quickly
        runmode--;
        if(runmode == 149){runmode = 195;} //roll over
        updateMode(runmode);//step through the baddefconadvice modes
      }else if(runmode == 103 && loopval == 9){//name scroll entry mode, press and hold continues to scroll
        loopval = 0;//reset so held button loops quickly
        scrollchar++;
        if(scrollchar > 126){scrollchar = 32;}//wrap
        namescrollupdatescreen1 = true;
      }
    }
  }else{ //button not pressed
    downdebounce = 0; //reset debounce
  }//end downbutton
}//checkButtons()

void invertScreenPixels(){
  for(byte x=0;x<8;x++){//invert all pixels
    for(byte y=0;y<128;y++){
      onebuffer[(x*128)+y] = ~onebuffer[(x*128)+y];
      zerobuffer[(x*128)+y] = ~zerobuffer[(x*128)+y];
    }
    for(byte y=0;y<30;y++){
      logogap[(x*30)+y] = ~logogap[(x*30)+y];
    }
  }
}

void drawMenuCursor(short newmode){
  if(newmode >= 10 && newmode < 100){ //draw the cursor
    if(runmode >= 10 && runmode < 20){ //if we were already in a menu, clear the old cursor
      oledRectangle(&ssoled[0], 2/*x*/, ((runmode % 10) * 8) + 24/*y*/, 6/*x*/, ((runmode % 10) * 8) + 30/*y*/, 0/*color*/, 1/*fill*/); //clear the old cursor
    }else if(runmode >= 10 && runmode < 40){ //in a sub menu, clear the old cursor
      oledRectangle(&ssoled[1], 2/*x*/, ((runmode % 10) * 8) + 24/*y*/, 6/*x*/, ((runmode % 10) * 8) + 30/*y*/, 0/*color*/, 1/*fill*/); //clear the old cursor
    }
    if(newmode < 20){ //in main menu, paint cursor
      oledWriteString(&ssoled[0], 0, 0/*x*/, ((newmode % 10) + 3)/*y*/, ">", FONT_NORMAL, 0/*inv color*/, 0); //new cursor
      oledRectangle(&ssoled[0], 2/*x*/, ((newmode % 10) * 8) + 25/*y*/, 4/*x*/, ((newmode % 10) * 8) + 29/*y*/, 1/*color*/, 1/*fill*/); //fill in cursor
    }else if(newmode < 40){ //display cursor on second screen, but not in help mode (40)
      oledWriteString(&ssoled[1], 0, 0/*x*/, ((newmode % 10) + 3)/*y*/, ">", FONT_NORMAL, 0/*inv color*/, 0); //new cursor
      oledRectangle(&ssoled[1], 2/*x*/, ((newmode % 10) * 8) + 25/*y*/, 4/*x*/, ((newmode % 10) * 8) + 29/*y*/, 1/*color*/, 1/*fill*/); //fill in cursor
    }
    oledDumpBuffer(&ssoled[0], zerobuffer);
    oledDumpBuffer(&ssoled[1], onebuffer);
  }
}

void detectUSBPower(){ //detect if USB-C is connected, and disable the battery vreg if it is. This is only checked when in menus
  //5.0v (usb) = 956
  //3.3v = 543 (sometimes as high as 570 (vreg bucking?)
  //3.2v = 544-546 (stable)
  //3.1v = 547-548 (stable)
  //3.0v = 549-551 (stable)
  //2.9v = 553-556 (as high as 598-617 rarely)
  //2.8v = 560-618 (all over the place)
  //2.7v = 588-620 (all over the place)
  //2.6v = 600-620 (mostly stable at 620)
  //2.5v = 620 (solid)
  //2.4v = 620
  //2.3v = 620
  //2.2v = 620
  //2.1v = 620
  analogReference(AR_INTERNAL1V0); //only 1v0 2v23 1v65 work (seeing strange things with everything except 1v0)
  battsensevalue = 0; //clear
  for(byte adcloop = 0; adcloop < 100; adcloop++){
    if(adcloop >= 50){ //only use the last half of the readings
      battsensevalue += analogRead(BATTSENSE); //read GSR pin, add to value to average it out
    }
  }
  battsensevalue /= 50; //average the readings
  //debug
  //oledWriteString(&ssoled[0], 0, 0/*x*/, 7/*y*/, shorttostring(battsensevalue), FONT_SMALL, 0/*inv color*/, 1);
  //delay(500);
  //end debug

  analogReference(AR_DEFAULT); //change this back, needed for GSR/HB readings
  if(battsensevalue > 750){ //on usb power
    digitalWrite(VREGENABLE, LOW);//disable batt vreg (sink enable pin)
    digitalWrite(LED_BUILTIN, LOW);//builtin led on
  }else{ //on batt power
    digitalWrite(VREGENABLE, HIGH);//enable batt vreg, prob not needed or we wouldn't be executing this anyway
    digitalWrite(LED_BUILTIN, HIGH); //builtin led off
  }
}//detectUSBPower()

void setupOledScreens(){ //setup OLED screens, and detect any issues
  int rcleft = false;
  int rcright = false;
  byte oledinitattempts = 0; //oleds aren't init'd propertly on first attempt, ignore the first init attempt. Plus the first time we check left, right can't have been init'd //TODO test with one screen physically connected
  do{
    rcleft = oledInit(&ssoled[0], OLEDRESOLUTION, OLED_ADDR_LEFT, FLIP180, INVERT, 1, SDA_PIN, SCL_PIN, RESET_PIN, 1600000L); // use I2C bus at 1600Khz
    rcright = oledInit(&ssoled[1], OLEDRESOLUTION, OLED_ADDR_RIGHT, FLIP180, INVERT, 1, SDA_PIN, SCL_PIN, RESET_PIN, 1600000L); // use I2C bus at 1600Khz
    if(rcleft == OLED_SSD1306_3C){ //oled init'd correctly
      oledSetBackBuffer(&ssoled[0], zerobuffer); //set buffer
      oledSetContrast(&ssoled[0], 255); //max brightness
      oledFill(&ssoled[0], 0, 1);//cls
    }else if(oledinitattempts > 0){ //if the left display isn't detected, and we've tried to init screens more than once
      Serial.print("Codever: ");
      Serial.print(CODE_VERSION);
      Serial.print(" Left OLED @ i2c address: 0x");
      Serial.print(OLED_ADDR_LEFT, HEX);
      Serial.println(" not found, double check i2c addressing or unbreak the OLED");
      if(rcright == OLED_SSD1306_3D){ //if the right display is detected (and the left isn't), display the error on screen
        oledWriteString(&ssoled[1], 0, 0/*x*/, 0/*y*/, "Error: OLED Detection", FONT_SMALL, 0/*inv color*/, 1); //print GSR reading
        oledWriteString(&ssoled[1], 0, 0/*x*/, 2/*y*/, "See Serial Output", FONT_SMALL, 0/*inv color*/, 1); //print GSR reading
      }
      flashLEDs();
    }

    if(rcright  == OLED_SSD1306_3D){ //oled init'd correctly
      oledSetBackBuffer(&ssoled[1], onebuffer); //set buffer
      oledSetContrast(&ssoled[1], 255); //max brightness
      oledFill(&ssoled[1], 0, 1);//cls
    }else if (oledinitattempts > 0){ //if the right display isn't detected, and we've tried to init screens more than once
      Serial.print("Codever: ");
      Serial.print(CODE_VERSION);
      Serial.print(" Right OLED @ i2c address: 0x"); //if left oled isn't found
      Serial.print(OLED_ADDR_RIGHT, HEX);
      Serial.println(" not found, double check i2c addressing or unbreak the OLED");
      if(rcleft == OLED_SSD1306_3C){ //if the left display is detected (and the right isn't), display the error on screen
        oledWriteString(&ssoled[0], 0, 0/*x*/, 0/*y*/, "Error: OLED Detection", FONT_SMALL, 0/*inv color*/, 1); //print GSR reading
        oledWriteString(&ssoled[0], 0, 0/*x*/, 2/*y*/, "See Serial Output", FONT_SMALL, 0/*inv color*/, 1); //print GSR reading
      }
      flashLEDs();
    }
    if(oledinitattempts < 100){
      oledinitattempts++; //increment counter, don't let it roll over
    }
  }while(rcleft != OLED_SSD1306_3C && rcright != OLED_SSD1306_3D);
}//setupOledScreens()

void flashLEDs(){ //don't flash for too long, this is hit once after reflash of the code. But a constant error will execute this multiple times and cause continuous flashing which we want.
  for(int i = 0; i < 3; i++){
    delay(50);
    digitalWrite(PIN_LED3, LOW);// led on
    digitalWrite(HBLED, HIGH); //HB led off
    delay(50);
    digitalWrite(PIN_LED3, HIGH); // led off
    digitalWrite(HBLED, LOW);//HB led on
  }//end with LED3 on, and builtin led off
}

void goToSleep(){ //~4.6ma@3v batts, prob can do better but we have a power switch anyway
  oledDumpBuffer(&ssoled[0], staticlogobuffer);//display startup screen logo on screen 0
  oledDumpBuffer(&ssoled[1], staticlogobuffer);//display startup screen logo on screen 1
  digitalWrite(HBLED, HIGH); //HB led off
  digitalWrite(PIN_LED2, HIGH); //turn off led
  digitalWrite(PIN_LED3, HIGH); //turn off led
  oledWriteString(&ssoled[0], 0, 40/*x*/, 0/*y*/, "Sleeping", FONT_SMALL, 0/*inv color*/, 1);
  oledWriteString(&ssoled[1], 0, 40/*x*/, 0/*y*/, "Sleeping", FONT_SMALL, 0/*inv color*/, 1);
  oledWriteString(&ssoled[0], 0, 13/*x*/, 7/*y*/, "To Wake: Hold UP", FONT_SMALL, 0/*inv color*/, 1);
  oledWriteString(&ssoled[1], 0, 13/*x*/, 7/*y*/, "To Wake: Hold UP", FONT_SMALL, 0/*inv color*/, 1);
  for(byte contrastfade = 255; contrastfade > 0; contrastfade--){
    oledSetContrast(&ssoled[0], contrastfade - 1); //fade displays down //todo: do we care about if they're already dim?
    oledSetContrast(&ssoled[1], contrastfade - 1); //fade displays down
    delay(15);
  }
  delay(300);//stay at min brightness for a bit
  oledPower(&ssoled[0], 0);//turn off display 0
  oledPower(&ssoled[1], 0);//turn off display 1
  //attachInterrupt(digitalPinToInterrupt(UPBUTTON), wakeupISR, LOW); //using interrupts doesn't work the second time you come out of sleep :: void wakeupISR(){ detachInterrupt(digitalPinToInterrupt(UPBUTTON)); //so we can use the up button again
  do{
    Watchdog.sleep(3000);//sleep
  }while (digitalRead(UPBUTTON) == 1); //while up button isn't being pressed, keep going to sleep
  oledPower(&ssoled[0], 1);//turn on display 0
  oledPower(&ssoled[1], 1);//turn on display 1
  oledSetContrast(&ssoled[0], ((screenbrightness - 50) * 127) + 1); //set screen brightness value
  oledSetContrast(&ssoled[1], ((screenbrightness - 50) * 127) + 1); //set screen brightness value
  digitalWrite(HBLED, LOW);//HB led on
  runmode = 1; //so updateMode() will re-draw the screen
  updateMode(11); //move to main menu (11 because holding up while waking will move it to 10)
}

char* shorttostring(short input){ //used to convert shorts to char* for oled display
  itoa(input, tempstring, 10);//convert to string
  return tempstring;
}
