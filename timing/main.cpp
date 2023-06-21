#include <Arduino.h>
#include <U8g2lib.h>
#include <bitset>
#include <STM32FreeRTOS.h>
#include <algorithm>
#include <ES_CAN.h>
#include <iostream>
#include <string>
#include <math.h>
// TIMING FILE
// Define the max() macro
#define max(a, b) ((a) > (b) ? (a) : (b))
#define DISABLE_THREADS
#define DISABLE_ISRS // comment out isrs 


//Constants
  const uint32_t interval = 100; //Display update interval

//Pin definitions
  //Row select and enable
  const int RA0_PIN = D3;
  const int RA1_PIN = D6;
  const int RA2_PIN = D12;
  const int REN_PIN = A5;

  //Matrix input and output
  const int C0_PIN = A2;
  const int C1_PIN = D9;
  const int C2_PIN = A6;
  const int C3_PIN = D1;
  const int OUT_PIN = D11;

  //Audio analogue out
  const int OUTL_PIN = A4;
  const int OUTR_PIN = A3;

  //Joystick analogue in
  const int JOYY_PIN = A0;
  const int JOYX_PIN = A1;

  //Output multiplexer bits
  const int DEN_BIT = 3;
  const int DRST_BIT = 4;
  const int HKOW_BIT = 5;
  const int HKOE_BIT = 6;

//Display driver object
U8G2_SSD1305_128X32_NONAME_F_HW_I2C u8g2(U8G2_R0);

//Check current step size
volatile uint32_t currentStepSize;
volatile uint8_t keyArray[7];
const int NUM_ROWS = 7; // define a constant for the number of rows
std::string keyStrArray[7];
SemaphoreHandle_t keyArrayMutex;
SemaphoreHandle_t RXMutex;
SemaphoreHandle_t CAN_TX_Semaphore;
volatile int rotationVar = 0;
volatile int octaveVar = 0;
volatile int masVar = 0;
volatile int volVar = 5;
volatile int octVar = 0;
volatile int waveVar = 0;

QueueHandle_t msgInQ;
uint8_t RX_Message[8]={1};
QueueHandle_t msgOutQ;
std::string prevKeyArray[7] = {"1111", "1111", "1111", "1111", "1111", "1111", "1111"};
int OCTAVE = 4;
uint8_t GLOBAL_RX_Message[8]={1};
std::string keyStr = "0000";
volatile bool master = true;
std::string RX_keyStr = "111111111111";

// volatile uint32_t localCurrentStepSize;

const std::string keyValues[NUM_ROWS][4] = {
  {"0111", "1011", "1101", "1110"},
  {"0111", "1011", "1101", "1110"},
  {"0111", "1011", "1101", "1110"}
};
const std::string noteNames[12] = {
  "C", "C#", "D", "D#",
  "E", "F", "F#", "G",
  "G#", "A", "A#", "B"
};


//Function to set outputs using key matrix
void setOutMuxBit(const uint8_t bitIdx, const bool value) {
      digitalWrite(REN_PIN,LOW);
      digitalWrite(RA0_PIN, bitIdx & 0x01);
      digitalWrite(RA1_PIN, bitIdx & 0x02);
      digitalWrite(RA2_PIN, bitIdx & 0x04);
      digitalWrite(OUT_PIN,value);
      digitalWrite(REN_PIN,HIGH);
      delayMicroseconds(2);
      digitalWrite(REN_PIN,LOW);
}

  // Function to concatenate bits
  uint8_t concatenateBits(int c0, int c1, int c2, int c3){
    uint8_t result = 0;
    result |= (c0 << 3);
    result |= (c1 << 2);
    result |= (c2 << 1);
    result |= c3;
    return result;
  }

  //Function to read the inputs from the four columns of the switch matrix (C0,1,2,3) and return the four bits concatenated together as a single byte
  uint8_t readCols(){

  int c0state = digitalRead(C0_PIN);
  int c1state = digitalRead(C1_PIN);
  int c2state = digitalRead(C2_PIN);
  int c3state = digitalRead(C3_PIN);

  // Call the concatenateBits() function with the read states
  uint8_t cols = concatenateBits(c0state, c1state, c2state, c3state);
  return cols;
}

//Select a given row of the switch matrix by setting the value of each row select address pin 
void setRow(uint8_t rowIdx){
  digitalWrite(REN_PIN,LOW);
  digitalWrite(RA0_PIN, rowIdx & 0b001);
  digitalWrite(RA1_PIN, rowIdx & 0b010);
  digitalWrite(RA2_PIN, rowIdx & 0b100);
  digitalWrite(REN_PIN,HIGH);
}

void clip (int& knobRotation, int max, int min){
  if (knobRotation > max){
    knobRotation = max;
  }
  else if (knobRotation < min){
    knobRotation = min;
  }
}

// KNOB CLASS
class Knob {
    private:
      int knobId;
    public:
      std::string prevKnob = "00";
      int knobRotation = 0;
        Knob(int knob_id) {
          knobId = knob_id;
        }
        Knob(int knob_id,int knob_rotation_start_val) {
          knobRotation = knob_rotation_start_val;
          knobId = knob_id;
        }

        void printknobRotation() {
            Serial.println(knobRotation);
        }
      void decodeKnob(std::string currentKnob){
      int rotationVar = 0;
      if (prevKnob == "00" && currentKnob == "01"){
        rotationVar = -1;
      }
      else if (prevKnob == "01" && currentKnob == "00"){
        rotationVar = 1;
      }
      else if (prevKnob == "10" && currentKnob == "11"){
        rotationVar = 1;
      }
      else if (prevKnob == "11" && currentKnob == "10"){
        rotationVar = -1;
      }
      else{
        rotationVar = 0;
      }
      knobRotation += rotationVar;
      if (knobId == 2){    // VOLUME KNOB
        if (!master){
          knobRotation = 0;
        }
        else{
          clip(knobRotation, 8, 0);
        }
      }
      else if ((knobId == 0)){   // WAVE KNOB
        clip(knobRotation, 1, 0);
        // master = bool(knobRotation);
      }
      else{
        clip(knobRotation, 8, 0);
      }
      if (knobId == 3){       // MASTER KNOB
        if (knobRotation > 1){
            knobRotation = 1;
          }
          else if (knobRotation < 0){
            knobRotation = 0;
          }
          // Serial.println("IN DECODE CLASS");
          
          if (knobRotation == 1){
            master = true;
          }
          else{
            master = false;
  }
      }
      prevKnob = currentKnob;
    }
};

Knob knob3(3,0);   // MASTER KNOB
Knob knob2(2,5);   // VOLUME KNOB
Knob knob1(1);   // OCTAVE KNOB
Knob knob0(0);   // WAVE KNOB


const uint32_t stepSizes [] = {
      51076922, //C4
      54112683, //C#4
      57330004, //D4
      60740598, //D#4
      64352275, //E4 
      68178701, //F4
      72231588, //F#4
      76528508, //G4
      81077269, //G#4
      85899345, //A4
      91006452, //A#4
      96426316, //B4
};


const int LUT_SIZE = 128;
int32_t LUT[LUT_SIZE];

void sine_LUT() {
  const float step = 2.0 * PI / LUT_SIZE;
  for (int i = 0; i < LUT_SIZE; i++) {
    float angle = i * step;
    LUT[i] = (int32_t)(127.0f * sinf(angle)) + 128;
  }
}

//WAVES CLASS
class Waves {
  private:
  public:

  int32_t get_sine(uint32_t phaseAcc){
  const char* tempkeyVal = keyStr.c_str();
  const char* tempRXkeyVal = RX_keyStr.c_str() ;
  uint32_t Vout_zeroCount = 0;
  uint32_t index = 0;
  int32_t localOct = OCTAVE;

  for (int i = 0; i < 12; i++){
    if (tempkeyVal[i] == '0'){
      if (localOct < 4){
        index = ((((stepSizes[i] >> -(OCTAVE-4)))*phaseAcc) >> 22)% 360;
      }
      else {
        index = ((((stepSizes[i] << (OCTAVE-4)))*phaseAcc) >> 22)% 360;
      }
      if (index > 180){
        Vout_zeroCount += -LUT[(index - 180) >> 1];
      }
      else {
        Vout_zeroCount += LUT[(index) >> 1];
      }
    }
  }

  for (int i = 0; i < 12; i++){
    if (tempRXkeyVal[i] == '0'){
      if (localOct < 4){
        index = ((((stepSizes[i] >> -(OCTAVE+1-4)))*phaseAcc) >> 22)% 360;
      }
      else {
        index = ((((stepSizes[i] << (OCTAVE+1-4)))*phaseAcc) >> 22)% 360;
      }
      if (index > 180){
        Vout_zeroCount += -LUT[(index - 180) >> 1];
      }
      else {
        Vout_zeroCount += LUT[(index) >> 1];
      }
    }
  }

  uint32_t Vout = Vout_zeroCount >> (34-volVar);
  return Vout;
  }
  int32_t get_sawtooth(uint32_t phaseAcc){
    int32_t Vout = (phaseAcc >> 24) - 128;
    Vout = Vout >> (8 - volVar);
    Vout = Vout + 128;
    return Vout;
  }
};

Waves wave;

void sampleISR() {
  static uint32_t phaseAcc = 0;
  int32_t Vout;
  if (waveVar==0){
    phaseAcc += currentStepSize;
    Vout = wave.get_sawtooth(phaseAcc);
  }
  else if (waveVar==1){
    uint32_t currentStepCounter = 1;
    Vout = wave.get_sine(phaseAcc);
    phaseAcc += currentStepCounter;
  }
  analogWrite(OUTR_PIN, Vout);
}

// Create chords by summing the currentstepsize of each key 
uint32_t chords(std::string keyStr, int OCTAVE){
  int zeroCount = 0;
  uint32_t sum = 0;
  uint32_t localCurrentStepSize = 0;
  for (int i = 0; i < 12; i++){
    if (keyStr[i] == '0'){
      zeroCount++;
      localCurrentStepSize = stepSizes[i];
      if (OCTAVE < 4){
        localCurrentStepSize = localCurrentStepSize >> -(OCTAVE - 4);
      }
      else{
        localCurrentStepSize = localCurrentStepSize << (OCTAVE - 4);
      }
      
      sum += localCurrentStepSize;
    }
  }
  return sum;
}

uint32_t countZero(std::string keyStr){
  int zeroCount = 0;
  for (int i = 0; i < 12; i++){
    if (keyStr[i] == '0'){
      zeroCount++;
    }
  }
  return zeroCount;
}

// Everything that's relevant to scanning the Keys
void scanKeysTask(void * pvParameters){
  knob3.decodeKnob(keyStrArray[3].substr(0, 2));  // MASTER KNOB
  const TickType_t xFrequency = 50/portTICK_PERIOD_MS;
  TickType_t xLastWakeTime = xTaskGetTickCount();
  uint8_t TX_Message[8] = {0};
    // const int NUM_ROWS = 3; // define a constant for the number of rows
    uint32_t localCurrentStepSizeT = 0;
    uint32_t localCurrentStepSizeR = 0;
    uint32_t localCurrentStepSize  = 0;
    for (int row = 0; row < NUM_ROWS; row++) {
      setRow(row);
      delayMicroseconds(3);
      uint8_t keys = readCols();
      std::bitset<4> keyBits(keys);
      std::string keyString = keyBits.to_string();
      keyStrArray[row] = keyString;
      keyArray[row] = keys;
    }
    keyStr = keyStrArray[0]+ keyStrArray[1] + keyStrArray[2] + keyStrArray[3];

    if (keyStrArray[5][3] == '1' ){ // left most or solo
      master = true;
      // Serial.println("MASTER");
    }
    else{
      master = false;
      // Serial.println("Slave");
    }

    if (!master){
      TX_Message[1] = OCTAVE ;
      for (int row = 0; row < NUM_ROWS-1 ; row++){
        for (int col = 0; col < 5; col++){
          if (keyStrArray[row][col] != prevKeyArray[row][col]){
            if (prevKeyArray[row][col] == '1'){
              TX_Message[0] = 80;
            }
            else if (prevKeyArray[row][col] == '0'){
              TX_Message[0] = 82;
            }
            TX_Message[2] = row*4 + col;
          }
        }
      }
      std::string lo_str = keyStr.substr(0, 6);  // bit 0 to bit 5
      std::string hi_str = keyStr.substr(6, 6);   // bit 6 to bit 11
      int lo_val = stoi(lo_str, nullptr, 2);
      int hi_val = stoi(hi_str, nullptr, 2);
      TX_Message[3] = lo_val;
      TX_Message[4] = hi_val;
    }
  
  if (master){
    uint32_t sumMaster = chords(keyStr,OCTAVE);
    uint32_t sumSlave = 0;
    if (keyStrArray[5][3] == '0' || keyStrArray[6][3] == '0' ){
      // Serial.println("43");
      xSemaphoreTake(RXMutex, portMAX_DELAY);
      // detect press messages
      if (RX_Message[0] == 80){
        localCurrentStepSizeR = stepSizes[RX_Message[2]];
        localCurrentStepSizeR = localCurrentStepSizeR << (RX_Message[1] - 4);
      }
      std::bitset<6> binaryHigh(RX_Message[3]);
      std::string binaryHighStr = binaryHigh.to_string();
      std::bitset<6> binaryLow(RX_Message[4]);
      std::string binaryLowStr = binaryLow.to_string();
      RX_keyStr = binaryHighStr + binaryLowStr;
      
      xSemaphoreGive(RXMutex);
      sumSlave = chords(RX_keyStr,RX_Message[1]);
    }

    if (localCurrentStepSize != 0) {
      localCurrentStepSize = (sumSlave +  sumMaster) / (countZero(RX_keyStr) + countZero(keyStr));
    }
    else{
      localCurrentStepSize = (sumSlave +  sumMaster);
    }

    __atomic_store_n(&currentStepSize, localCurrentStepSize, __ATOMIC_RELAXED);
  }

  std::copy(keyStrArray, keyStrArray + sizeof(keyStrArray)/sizeof(keyStrArray[0]), prevKeyArray);
  
  if (!master){
    xQueueSend( msgOutQ, TX_Message, portMAX_DELAY);
  }
 
  knob2.decodeKnob(keyStrArray[3].substr(2, 4)); // VOLUME KNOB
  volVar = knob2.knobRotation;
  knob0.decodeKnob(keyStrArray[4].substr(2, 4)); // WAVE KNOB
  waveVar = knob0.knobRotation;
  knob1.decodeKnob(keyStrArray[4].substr(0, 2)); // OCTAVE KNOB
  OCTAVE = knob1.knobRotation; 
  
}

// std::string convertNote(std::string )
std::string convertNote(std::string keysStr){
  std::string ans = "";
  for (int i = 0; i < keysStr.length(); i++){
    if (keysStr[i] == '0'){
      ans += noteNames[i];
    }
  }
  return ans;
}

std::string ks;
std::string rs;

// Display it on the screen
void displayUpdateTask(void *  pvParameters){
  const TickType_t xFrequency = 200/portTICK_PERIOD_MS;
  TickType_t xLastWakeTime = xTaskGetTickCount();
  uint32_t ID = 0x123;

    ks = "Keys:";
    rs = "";
    for (int i = 0; i < keyStr.length(); i++){
      if (keyStr[i] == '0'){
        ks += noteNames[i];
      }
      else if (RX_keyStr[i] == '0'){
        rs += noteNames[i];
      }
    }

    u8g2.clearBuffer();         // clear the internal memory
    u8g2.setFont(u8g2_font_ncenB08_tr); // choose a suitable font

    if(master){
      u8g2.drawStr(2,10, ks.c_str());
      u8g2.drawStr(2,20, rs.c_str());
    }

    std::string wave = "Wav: " + std::to_string(waveVar);
    u8g2.drawStr(2,30, wave.c_str());
    std::string octave = "Oct: " + std::to_string(OCTAVE);
    u8g2.drawStr(40,30, octave.c_str());
    std::string vol = "Vol: " + std::to_string(volVar);
    u8g2.drawStr(70,30, vol.c_str());
    u8g2.drawStr(115,30, master ? "M": "S");
    u8g2.sendBuffer();
  
}


void decodeTask(void *  pvParameters){

  uint32_t ID = 0x123;
  uint32_t localCurrentStepSize;
  uint8_t Local_RX_Message[8];
    xSemaphoreTake(RXMutex, portMAX_DELAY);
    if (keyStrArray[5][3] == '0' || keyStrArray[6][3] == '0' ){
      // Serial.println("515");
      xQueueReceive(msgInQ, Local_RX_Message, portMAX_DELAY);
      memcpy(RX_Message, Local_RX_Message, sizeof(RX_Message));
    }
    xSemaphoreGive(RXMutex);
}

void CAN_RX_ISR (void) {
	uint8_t RX_Message_ISR[8];
	uint32_t ID = 0x123;
	CAN_RX(ID, RX_Message_ISR);
	xQueueSendFromISR(msgInQ, RX_Message_ISR, NULL);
}

void CAN_TX_Task (void * pvParameters) {
  Serial.println("CAN");
	uint8_t msgOut[8];
	   xQueueReceive(msgOutQ, msgOut, portMAX_DELAY);
	   xSemaphoreTake(CAN_TX_Semaphore, portMAX_DELAY);
	   CAN_TX(0x123, msgOut);
     xSemaphoreGive(CAN_TX_Semaphore); //added for timing
}

void CAN_TX_ISR (void) {
	xSemaphoreGiveFromISR(CAN_TX_Semaphore, NULL);
}


void setup() {
  // put your setup code here, to run once:
  msgInQ = xQueueCreate(384,8);  //increased for timing
  msgOutQ = xQueueCreate(384,8); //increased for timing
  keyArrayMutex = xSemaphoreCreateMutex();
  RXMutex = xSemaphoreCreateMutex();  
  CAN_TX_Semaphore = xSemaphoreCreateCounting(3,3);
  uint8_t test_send[8]={0};
  //Set pin directions
  pinMode(RA0_PIN, OUTPUT);
  pinMode(RA1_PIN, OUTPUT);
  pinMode(RA2_PIN, OUTPUT);
  pinMode(REN_PIN, OUTPUT);
  pinMode(OUT_PIN, OUTPUT);
  pinMode(OUTL_PIN, OUTPUT);
  pinMode(OUTR_PIN, OUTPUT);
  pinMode(LED_BUILTIN, OUTPUT);

  pinMode(C0_PIN, INPUT);
  pinMode(C1_PIN, INPUT);
  pinMode(C2_PIN, INPUT);
  pinMode(C3_PIN, INPUT);
  pinMode(JOYX_PIN, INPUT);
  pinMode(JOYY_PIN, INPUT);

  //Initialise display
  setOutMuxBit(DRST_BIT, LOW);  //Assert display logic reset
  delayMicroseconds(2);
  setOutMuxBit(DRST_BIT, HIGH);  //Release display logic reset
  u8g2.begin();
  setOutMuxBit(DEN_BIT, HIGH);  //Enable display power supply

  CAN_Init(false);
  CAN_RegisterRX_ISR(CAN_RX_ISR);
  CAN_RegisterTX_ISR(CAN_TX_ISR);
  setCANFilter(0x123,0x7ff);
  CAN_Start();

  //Initialise UART
  Serial.begin(9600);
  sine_LUT();
   // for timing CAN_TX_TASK
  // for (int iter = 0; iter < 32; iter++) {
  //     xQueueSend( msgOutQ, test_send, portMAX_DELAY);
  //   }

  // CALCULATING THE AVERAGE TIME
  uint32_t startTime = micros();
  uint32_t avgTime = 0;
  for (int iter = 0; iter < 36; iter++) {
    scanKeysTask(NULL);  // FOR CAN you need to go click on  CAN_TX_TASK and comment line 111 in the library
  } 
  avgTime  = (micros()-startTime)/36;
  Serial.println(avgTime);

  TIM_TypeDef *Instance = TIM1;
  HardwareTimer *sampleTimer = new HardwareTimer(Instance);
  sampleTimer->setOverflow(22000, HERTZ_FORMAT);
  sampleTimer->attachInterrupt(sampleISR);
  sampleTimer->resume();

  TaskHandle_t scanKeysHandle = NULL;
  xTaskCreate(
    scanKeysTask,		/* Function that implements the task */
    "scanKeys",		/* Text name for the task */
    64,      		/* Stack size in words, not bytes */
    NULL,			/* Parameter passed into the task */
    4,			/* Task priority */
    &scanKeysHandle );  /* Pointer to store the task handle */
  
  TaskHandle_t displayUpdateHandle = NULL;
  xTaskCreate(
    displayUpdateTask,		/* Function that implements the task */
    "displayUpdate",		/* Text name for the task */
    256,      		/* Stack size in words, not bytes */
    NULL,			/* Parameter passed into the task */
    3,			/* Task priority */
    &displayUpdateHandle );  /* Pointer to store the task handle */

  TaskHandle_t decodeHandle = NULL;
  xTaskCreate(
    decodeTask,		/* Function that implements the task */
    "decode",		/* Text name for the task */
    32,      		/* Stack size in words, not bytes */
    NULL,			/* Parameter passed into the task */
    2,			/* Task priority */
    &decodeHandle );  /* Pointer to store the task handle */
  
  TaskHandle_t CAN_TXHandle = NULL;
  xTaskCreate(
    CAN_TX_Task,		/* Function that implements the task */
    "CAN_TX",		/* Text name for the task */
    32,      		/* Stack size in words, not bytes */
    NULL,			/* Parameter passed into the task */
    1,			/* Task priority */
    &CAN_TXHandle );  /* Pointer to store the task handle */

    #ifndef DISABLE_THREADS
	  xTaskCreate(scanKeysTask,"scanKeys",64,NULL,4,&scanKeysHandle);
    xTaskCreate(displayUpdateTask,"displayUpdate",256,NULL,3,&displayUpdateHandle);
    xTaskCreate(decodeTask,"decode",32,NULL,2,&decodeHandle);
    xTaskCreate(CAN_TX_Task,"CAN_TX",64,NULL,1,&CAN_TXHandle);
    #endif

    #ifndef DISABLE_ISRS
    #endif
  
  vTaskStartScheduler();
}

void loop() {

}
