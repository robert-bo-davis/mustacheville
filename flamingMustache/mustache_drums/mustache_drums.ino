#include <avr/pgmspace.h>
/*
  so this controls 6 solenoid activated switches used in a
  propane fire effect.  there are currently 8 buttons and 1 
  switch used to manage the system.  6 buttons line up directly
  with each of the solenoids and are used to manually actuate 
  them when the system is in manual mode.  The 6 buttons can
  be any 6 buttons… a drum kit, a game controller or just some
  regular old momentary switches The other two buttons are
  used to control the replay function in manual mode.  One 
  clears out the history the other replays up to the last 50 things
  a user has pressed.  In program mode the system randomly
  selects a pre-programed "show" and performs it… currently
  all buttons are disabled while in program mode.  This may change.
*/

// Length of poofs in ms
const int biggest  = 150;
const int big      = 125;
const int small    = 100;
const int smallest = 75;

// to try to cut out interference between drum pad sensors... we pause for a bit
// after each hit.
const int drumDelay = 100;

const int poofLimit = 1000;
const int poofLimitMultiplier = 3;
unsigned long poofStart;

// These set up various bits
// of the random show.
// *remember unsigned ints have a max value of 65,535 on arduino
const unsigned int minPause = 10 * 1000; // 10 seconds
const unsigned int maxPause = 20 * 1000; // 20 seconds
const int maxLoop  = 4;                  // maximum number of times to loop a single program

// this sets the maximum pause/poof in history
const unsigned long maxHistoryPause  = 4 * 1000;   //5s
// automatically reset history if nobody toches buttons for a while
const unsigned long autoHistoryReset = 60 * 1000; //60s


// These are the pre-programed "shows".  They use a
// very basic 8 bit storage system.  The first 6 0/1s
// after the B tell the program which burners to turn
// on. 111111 means all 6 will be on,  100001 means
// only the two on the ends will fire, 001100 means
// only the middle two will fire… etc.  the last two
// digits tell the system how long to fire the burner.
// There are only 4 options: 00 is shortest, 10 is short,
// 01 is long and 11 is longest.  a 0 ends the program
// and it will restart from the beginning.
// The pause between "poofs" is currently set at the
// shortest time.  If you want to make a longer pause
// you could use B00000010 which will add about a half
// a second pause.
const int programCount     = 3;
const int maxProgramLength = 50;
PROGMEM prog_uint8_t programs[3][50] = { // these are stored in flash memory see http://www.arduino.cc/en/Reference/PROGMEM
   {
      B11111100,
      B00000010,
      B10000110,
      B01001010,
      B00110010,
      B01001010,
      B10000110,
      B00000010,
      0
   },
   {
      B10000000,
      B01000000,
      B00100000,
      B00010000,
      B00001000,
      B00000100,
      B00001000,
      B00010000,
      B00100000,
      B01000000,
      0
   },
   {
      B00110001,
      B01111110,
      B00000010,
      B11111100,
      B00000010,
      B01111010,
      B00110001,
      B01111010,
      B00000010,
      B11111100,
      B00000010,
      B01111010,
      0
   },
};

prog_uint16_t butHistory[1][2]; // used to store button press history for easter eggs/replay
unsigned int butHistoryIndex        = 0;
const unsigned int maxHistoryLength = 1;

// easter egg programs - stores easter egg programs
// if the required buttons are (**pressed for shorter than
// the defined time**not yet) then the program represented by the
// first item in the array - 1 will play for the int in
// the second items loops. 0 ends the pattern
const int eggCount      = 2;
const int maxEggsLength = 22;
const int minEggsLength = 5;
PROGMEM prog_uint8_t eggs[2][22] = { // these are stored in flash memory see http://www.arduino.cc/en/Reference/PROGMEM
   {
      1,
      4,
      B01000000,
      B00000001,
      B00100000,
      B00000001,
      B00010000,
      B00000001,
      B00001000,
      B00000001,
      B00000100,
      B00000001,
      B00001000,
      B00000001,
      B00010000,
      B00000001,
      B00100000,
      B00000001,
      B01000000,
      B00000001,
      B10000000,      
      0,
    },
    {
      1,
      2,
      B11111100,
      B00000001,
      B01111000,
      B00000001,
      B00110000,
      0
    }
};

unsigned long lastTime; // stores the last change time used to keep history
prog_uint8_t lastSol; // stores the last solenoid pattern

// switch that changes from pre-programmed shows
// to button based operation
const int progSwitch = 12;

// button to replay history
const int butPlayHistory = 11;

// button to reset history
const int butResetHistory = 10;

// button pins
const int but1 = A5;
const int but2 = A4;
const int but3 = A3;
const int but4 = A2;
const int but5 = A1;
const int but6 = A0;

// solenoid pins 
const int sol1 = 2;
const int sol2 = 3;
const int sol3 = 4;
const int sol4 = 5;
const int sol5 = 6;
const int sol6 = 7;

// more solenoids could be added?
#define SOL_COUNT 6

// ordered arrays of solenoid, button pins
// and poof lengths used for bin storage and 
// easy looping
const int mySols[]      = {sol1, sol2, sol3, sol4, sol5, sol6};
const int myButs[]      = {but1, but2, but3, but4, but5, but1};
// for 5 button set up:
// const int myButs[]      = {but1, but2, but3, but4, but5, but1};
const int poofLengths[] = {smallest, small, big, biggest};

int lastPoofs[] = {0,0,0,0,0,0};
int poofThreshold = 6;


/*
  poof() sends a signal to the relay to close
  it takes an array of solenoid pins and a length
  if length is 0 it assumes that the relay
  is going to be opened elsewhere otherwise it 
  leaves the relay closed for length milliseconds
  and then closes it.
*/
void poof(int* sols, int length){
  for (int i = 0; i < SOL_COUNT; i++) {
    if (!sols[i]){
      break;
    }
    digitalWrite(sols[i], LOW);
    //Serial.print("POOF : ");
    //Serial.println(sols[i]);
  }

  if (length){
    delay(length);
    unpoof(sols, 0);
  }
}

/*
   unpoof() sends a signal to the relay to open
   it takes an array of solenoid pins and a length
   it delays for length in milliseconds after
   opening the relay.
*/
void unpoof(int* sols, int length){
  for (int i = 0; i < SOL_COUNT; i++) {
    if (!sols[i]){
      break;
    }
    digitalWrite(sols[i], HIGH);
    //Serial.print("UNPOOOOOOFFFED : ");
    //Serial.println(sols[i]);
  }

  if (length){
    delay(length);
  }
}

/*
  getBin() takes an array of solenoid pins and
  returns an integer that is the binary representation
  of those pins used for storage;
*/
prog_uint8_t getBin(int* sols){
  prog_uint8_t bin = 0;
  int count = 4;
  for (int i = 5; i >= 0; i--){  // have to go backwards or else shit is mirrored
    for (int ii = 0; ii < SOL_COUNT; ii++){
      if (sols[ii] == 0){
        break; 
      }
      if (mySols[i] == sols[ii]){
        bin += count;
      }
    }
    count = count * 2;
  }
  
  if(bin){
    //Serial.println(bin);
  }
  return bin;
}

/*
  getDuration() returns the time since lastTime in ms
  as an int and resets lastTime to now()
*/
int getDuration(){
  unsigned long now = millis(); // millis returns unsigned longs
  unsigned long length = (now - lastTime);
  lastTime = now;
  /*
  Serial.print(length);
  Serial.print(">");
  Serial.println(autoHistoryReset);
  */
  if (length > autoHistoryReset){
     //Serial.println("resetting history because of timeout");
     resetHistory();
  }
  if (length > maxHistoryPause){
     length = maxHistoryPause;
  }
  return (int)length;
}

/*
  keepHistotry updates the butHistory array with button and timing 
  information if needed.
*/
void keepHistory(int* sols){
  prog_uint8_t bin = getBin(sols);
  if (lastSol != bin){
    //Serial.println("NEW SOL");
    butHistory[butHistoryIndex][0] = lastSol;
    butHistory[butHistoryIndex][1] = (int)getDuration();
    
    Serial.print("BS: ");
    Serial.println(butHistory[butHistoryIndex][0]);
    Serial.print("length: ");
    Serial.println(butHistory[butHistoryIndex][1]);
    
    lastSol = bin;
    butHistoryIndex++;
    // if we hit the end of our history we shift everything back one 
    // index and drop off the beginning.
    if (butHistoryIndex >= maxHistoryLength){
      //Serial.println("Shifting history");
      for(int i = 1; i < maxHistoryLength; i++){
        int newI = i - 1;
        butHistory[newI][0] = butHistory[i][0];
        butHistory[newI][1] = butHistory[i][1];
      }
      butHistoryIndex--;
    }
    //checkEasterEggs();// should be at end of keepHistory() so arrays are up to date and part of it so it only happens when needed
  }
}

/*
   getSols() takes the binary storage system for
   programs and returns an array of solenoid pins.
*/
int* getSols(prog_uint8_t bs){
  int count = 0;
  int sols[6];
  for (int i = 0; i < SOL_COUNT; i++) {
    if (bs & (B10000000 >> i)) {
      sols[count] = mySols[i];
      count++;
    }
  }

  if (count < 6){
    sols[count] = 0; // we assume that an empty pin is the end of a usable array everywhere.
  }

  return sols;
}

/*
  getLength() takes the binary storage system for
  programs and returns an int in ms for the length
  of the poof.
*/
int getLength(prog_uint8_t bs){
  int length = smallest;
  int b = 1;
  int count = 1;
  for (int i = 6; i < 8; i++) {
    if (bs & (B10000000 >> i)) {
      b = b + count;
    }
    count++;
  }

  return poofLengths[b-1];
}

void checkEasterEggs(){
  boolean match = false;
  for (int i = 0; i < eggCount; i++){
    if ((butHistoryIndex - minEggsLength) < 0){
      Serial.println("not enough history to match pattern");
      break; // we don't have enough history to match this pattern
    }
    int myH = butHistoryIndex;
    for (int ii = 2; ii < 22; ii++) {
      byte e = pgm_read_byte(&(eggs[i][ii]));
      if ((myH == butHistoryIndex && butHistory[myH][0] == 0) || e == 0){
        break;
      }
      if (e == 1){// we use 0 to end a pattern so we use 2 B00000001 to represent a 0 in the history
        e = 0; 
      }
      Serial.print(butHistory[myH][0]);
      Serial.print(" == ");
      Serial.println(e);
      if (butHistory[myH][0] == e){
         match = true;
         Serial.println("TRUE");
      }
      else{
        match = false;
        Serial.println("NO MATCH breaking");
        break;
      }
      myH--;
    }
    if (match){
      Serial.println("Pattern matched!");
      int loops = pgm_read_byte(&(eggs[i][1]));
      int p = pgm_read_byte(&(eggs[i][0]));
      //Serial.print("loops:  ");
      //Serial.println(loops);
      //Serial.print("p:  ");
      //Serial.println(p);
      loopProgram(p, loops, false);
      break;
    }
    //Serial.println();
    //Serial.println();
  }
}

/*
  playHistory() plays back the history of the button pushes
*/
void playHistory(){
  int i;
  for (i = 0; i < maxHistoryLength; i++) {
     //Serial.print("BS: ");
     //Serial.println(butHistory[i][0]);
     //Serial.print("length: ");
     //Serial.println(butHistory[i][1]);
     if (i == 0 && !butHistory[i][0]){
       //next; 
     }
     else{
       poof(getSols(butHistory[i][0]), butHistory[i][1]);
       // break out of program if we hit all 0s or if the program switch is flipped
       if ((!butHistory[i][0] && !butHistory[i][1]) || checkProgSwitch()){
         break;
       }
     }
  }
  //Serial.print("history count: ");
  //Serial.println(i);
}

/*
  resetHistory() empties the butHistory array and resets the index
*/
void resetHistory(){
  //Serial.println("RESET HISTORY");
  for (int i = 0; i < maxHistoryLength; i++) {
    butHistory[i][0] = 0;
    butHistory[i][1] = 0;
  }
  butHistoryIndex = 0;
  lastTime = millis();
  lastSol = 0;
}

/*
  randomShow() attempts to randomly produce
  shows by looping different programs with
  different intervals between them
*/
void randomShow(){ 
 // system decides if it feels like pausing
 // between shows.
 int boolPause = random(1000);
 unsigned int pause;
 delay(10);
 if (boolPause){
   pause = random(minPause, maxPause);
 }
 else{
   pause = 0;
 }
 delay(10);
 int loopCount = random(1, maxLoop);
 delay(10);
 int p = random((programCount - 1));
 
 Serial.print("boolPause: ");
 Serial.println(boolPause);
 Serial.print("pause: ");
 Serial.println(pause);
 Serial.print("loopCount: ");
 Serial.println(loopCount);
 Serial.print("p: ");
 Serial.println(p);
 
 loopProgram(p, loopCount, true);
 if (!checkProgSwitch()){
   return; 
 }
 pauseAndBreakForSwitch(pause);
}

void pauseAndBreakForSwitch(int pause){
  for (int p = (pause / 10); p>=0;p--){
    if (!checkProgSwitch()){
       break;
    }
    delay(10);
  }
}

void loopProgram(int p, int loopCount, boolean break_for_switch){
   for (int c = 0; c<=loopCount; c++){
     // break out of loop if the program switch is flipped
     if (break_for_switch && !checkProgSwitch()){
       break; 
     }

     playProgram(p, break_for_switch);
   }
}

void playProgram(int p, boolean break_for_switch){
  for (int i = 0; i <= maxProgramLength; i++){
   prog_uint8_t cp = pgm_read_byte(&(programs[p][i]));
   // break out of program if we hit the end or if the program 
   // switch is flipped... this will cascade up into loopProgram
   if (cp == 0 || (break_for_switch &&!checkProgSwitch())){
     break;
   }
   //Serial.println(programs[p][i]);
   int* sols = getSols(cp);
   int length = getLength(cp);
   poof(sols, length);
 }
}

/*
  checkButtons() loops through the buttons
  checking for pressed buttons and poofs/unpoofs
  the appropriate solenoids.
*/
void checkButtons(){
  //checkResetHistory();
  //checkPlayHistory();
  int pSols[6];
  int pSolsIndex = 0;
  int uSols[6];
  int uSolsIndex = 0;
  //int length = 0;
  int ulength = 0;
  boolean punish = false;
  for (int i = 0; i < SOL_COUNT; i++) {
   int val = analogRead(myButs[i]);
   if (punish){
     uSols[uSolsIndex] = mySols[i];
     uSolsIndex++;
   }
   else if (i != 0 && i !=5 && lastPoofs[i] && lastPoofs[i] <= poofThreshold){
     if (lastPoofs[i] == poofThreshold){
        lastPoofs[i] = 0; 
     }
     else{ 
       lastPoofs[i]++;
     }
     Serial.print("lastpoofs: ");
     Serial.println(lastPoofs[i]);
   }
   else if(val < 400 && val > 0){
        Serial.print(mySols[i]);
        Serial.print(" value: ");
        Serial.println(val);
       //Serial.print("poof ");
       //Serial.println(mySols[i]);
       pSols[pSolsIndex] = mySols[i];
       // only do shit to bass pedal
       if (i == 0){
         if (poofStart){
           unsigned long length = (millis() - poofStart);
           Serial.print("length: ");
           Serial.println(length);
           if (length > (poofLimit + 50)){ // if you release and come back we don't want to punish you.
             poofStart = millis();
           }
           else if (length >= poofLimit){
             punish = true;
             uSols[uSolsIndex] = mySols[i];
             uSolsIndex++;
             poofStart = 0;
             ulength = poofLimit * poofLimitMultiplier;
           }
         }
         else{
           poofStart = millis();
           Serial.print("setting poofStart to:  ");
           Serial.println(poofStart);
         }
       }
       else{ // spplies to everything else
        /*
         if (val > 300){
           length = poofLengths[0];
         }
         else if(val > 200){
           length = poofLengths[1];
         }
         else if(val > 150){
           length = poofLengths[2];
         }
         else{
           length = poofLengths[3];
         }
        */
       }
       lastPoofs[i] = 1;
       pSolsIndex++;
    }
    else{
       if (i == 0 || i == 5){
         poofStart = 0; 
       }
       if (!digitalRead(mySols[i])){
         //Serial.print("UNPOOF ");
         //Serial.println(mySols[i]);
         uSols[uSolsIndex] = mySols[i];
         uSolsIndex++;
       }
    }
  }
  if(uSolsIndex < SOL_COUNT){
    uSols[uSolsIndex] = 0;
  }
  if(pSolsIndex < SOL_COUNT){
    pSols[pSolsIndex] = 0;
  }
  //keepHistory(pSols);
  unpoof(uSols, ulength);
  poof(pSols, 0);
  /*
  if (pSolsIndex){
    delay(drumDelay); 
  }
  */
}

/*
  checkHistoryReset() resets history if button is pushed
*/
void checkResetHistory(){
  if(digitalRead(butResetHistory) == LOW){
    resetHistory();
  }
}

/*
  checkPlayHistory() plays history if button is pushed
*/
void checkPlayHistory(){
  if(digitalRead(butPlayHistory) == LOW){
    playHistory();
  }
}

/*
  checProgSwitch() returns true if the progSwitch
  has been activated and false if it hasn't
*/
boolean checkProgSwitch(){
  if(digitalRead(progSwitch) == HIGH){
    return true;
  }
  return false;
}

void setup(){
   // try to use analog noise on analog pin 0
   // to generate enough entropy for something
   // like randomness.
   randomSeed(analogRead(0));
  
  Serial.begin(9600);
  // loop through and set up button pins
  for (int i = 0; i <= 5; i++) {
    pinMode(myButs[i], INPUT);
    digitalWrite(myButs[i], HIGH);
  }

  // set up program switch
  pinMode(progSwitch, INPUT);
  digitalWrite(progSwitch, HIGH);
  
  // set up resetHistory button
  pinMode(butResetHistory, INPUT);
  digitalWrite(butResetHistory, HIGH);
  
  // set up playHistory button
  pinMode(butPlayHistory, INPUT);
  digitalWrite(butPlayHistory, HIGH);
 
  // loop through and set up solenoid pins.
  for (int i = 0; i <= 5; i++) {
    pinMode(mySols[i], OUTPUT);
    digitalWrite(mySols[i], HIGH);
  }
}

void loop(){
  /*
  // for testing
   int* sols = getSols(programs[0][4]);
   poof(sols, smallest);
   delay(smallest);
  */

  if (0){//checkProgSwitch()){
    resetHistory();
    randomShow();
  }
  else{
    checkButtons();
  }
}
