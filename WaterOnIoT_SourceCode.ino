/*
 * Project - Water IoT (Hackster Blynk Contest)
 * ---------------------------------------------
 * 
 * - Author: Pedro Bertoleti
 * - Date: 06/2016
 * 
 * This project allows water consumption ans flow mettering over
 * Internet, using Blynk Board, Blynk App and water flow sensor.
 * Among the features of this project, the highlights are:
 * 
 * - Instant consumption and flow mettering over the Internet
 * - Total consumption is stored and compared to a limit defined
 * by user (limit of consumption / period of time). So, user can
 * check and use water in a clever and more economic way ($$ and 
 * enviroment)
 * - The metterings can be calibrated, so this project fits to
 * every place (which it's into sensor flow and pressure limit, of 
 * course).
 * 
 * In general way, it's possible to:
 * - Read flow and consumption mettering
 * - Reset comsumption mettering
 * - Get software version (software flashed into Blynk Board)
 * - Start & end calibration
 */

/*
 * Includes
 */
#define BLYNK_PRINT Serial    // Comment this out to disable prints and save space
#include <ESP8266WiFi.h>
#include <BlynkSimpleEsp8266.h>
#include <EEPROM.h>

/*
 * Global definitions
 */
#define SOFTWARE_VERSION      "V1.00"   
#define BREATHING_LIGHT       5  //LED Pin on SparkFun Blynk Board
#define YES                   1
#define NO                    0
#define MAX_BUFFER_SIZE       20
#define PIN_SENSOR            0

//Emulated EEPROM definitions
#define EEPROM_BYTES  28    //amount of bytes allocated in flash to
                            //simulate an EEPROM. It must be between
                            //4 bytes and 4096 bytes
#define EEPROM_KEY    "PedroBertoleti123"  //this is a key that will
                                           //be stored just before the 
                                           //relevant data on EEPROM. It
                                           //indicates if EEPROM has already been
                                           //initialized or not.

#define ADDRESS_EEPROM_KEY               0
#define ADDRESS_PULSES_PER_LITTER        18
#define ADDRESS_CALIBRATION_MODE         22
#define ADDRESS_LIMIT_CONSUMPTION_DEFINE 23
#define ADDRESS_LIMIT_CONSUMPTION        24

//Virtual pins definitions
//Getting commands from App:
#define VP_CONSUMPTION_RESET         V0
#define VP_SOFTWARE_VERSION          V1
#define VP_START_CALIBRATION         V2
#define VP_END_CALIBRATION           V3
#define VP_SET_CONSUMPTION_LIMIT     V4

//Pushing data to App:
#define VP_CONSUMPTION_READ          V5
#define VP_FLOW_READ                 V6
#define VP_SOFTWARE_VERSION_READ     V7
#define VP_WARNING_CONSUMPTION_LIMIT V8
#define VP_BREATHING_LIGHT_APP       V9
#define VP_CALIBRATION_IS_OK         V10

/*
 * Typedefs and structures
 */

/*
 * Global variables
 */

//Blynk Settings
char BlynkAuth[] = ""; //write here your Blynk Auth Key
char WiFiNetwork[] = "";  //write here the SSID of WiFi network Blynk Board will connect
char WiFiPassword[] = ""; //write here the password of WiFi network Blynk Board will connect

//general variables 
long SensorPulseCounter;
float CalculatedFlow;
float CalculatedConsumption;
long LimitOfConsumption;
char SoftwareVersion[] = SOFTWARE_VERSION; 
long PulsosPerLitter;   //contains how much pulses of sensor indicates 1l of consumption
char CalibrationMode;   //indicates if board it's in calibration mode
char LimitOfConsumptionDefined;  //indicates if a limit of water consumption is defined
unsigned long LastMillis;   //used to time-slice processing
char VarToggle;   //it controls breathing light toggle

char EEPROMKeyRead[17];

/*
 * Prototypes
 */
void CountSensorPulses(void);
void ResetCountSensorPulses(void);
void CalculateFlowAndConsumption(void);
void SendFlowAndConsumptionToApp(void);
char VerifyLimitOfConsumption(void);
void ShowDebugMessagesInfoCalculated(void);
void TurnOnSensorPulseCounting(void);
void TurnOffSensorPulseCounting(void);
void ControlBreathingLight(void);
void StartEEPROMAndReadValues(void);
long EEPROMReadlong(long address);
void EEPROMWritelong(int address, long value);

/*
 * Implementation / functions
 */

//Following is the virtual pin's management. These are like events,
//so they don't appear on prototype's declarations

//Event: reset consumption
BLYNK_WRITE(VP_CONSUMPTION_RESET) 
{
    int StatusButton;

    StatusButton = param.asInt();

    //ensure it executes only when virtual push button is in on state
    if (StatusButton)
    {
        CalculatedConsumption = 0.0;
        Serial.println("[EVENT] Total water consumption was reseted");
    }
}

//Event: send software version
BLYNK_WRITE(VP_SOFTWARE_VERSION) 
{
    int StatusButton;
    char SoftwareVersion[]=SOFTWARE_VERSION;

    StatusButton = param.asInt();

    //ensure it executes only when virtual push button is in on state
    if (StatusButton)
    {
        Blynk.virtualWrite(VP_SOFTWARE_VERSION_READ, SoftwareVersion);    
        Serial.println("[EVENT] Software version was sent");
    }
}

//Event: start calibration
BLYNK_WRITE(VP_START_CALIBRATION) 
{
    int StatusButton;

    StatusButton = param.asInt();

    //ensure it executes only when virtual push button is in on state
    if (StatusButton)
    {
        CalibrationMode = YES;
        PulsosPerLitter = 0;
        ResetCountSensorPulses();
        Serial.println("[EVENT] Sensor calibration started");
    }
}

//Event: end calibration
BLYNK_WRITE(VP_END_CALIBRATION) 
{
    int StatusButton;

    StatusButton = param.asInt();

    //ensure it executes only when virtual push button is in on state
    if (StatusButton)
    {
        PulsosPerLitter = SensorPulseCounter;
        CalibrationMode = NO;    
        
        //write calibration result to eeprom
        EEPROMWritelong(ADDRESS_PULSES_PER_LITTER,PulsosPerLitter);
        EEPROMWritelong(ADDRESS_CALIBRATION_MODE,CalibrationMode);
        
        ResetCountSensorPulses();
        Serial.print("[EVENT] Sensor calibration ended. Calibration factor: ");
        Serial.print(PulsosPerLitter);
        Serial.println(" pulses / litter");
        Serial.println("[EEPROM] Calibration values stored in EEPROM.");
    }
}

//Event: limit of water consumption defined
BLYNK_WRITE(VP_SET_CONSUMPTION_LIMIT) 
{
    int LimitConsumption;

    Serial.print("[EVENT] Limit of water consumption is now defined in ");
    Serial.print(LimitOfConsumption);
    Serial.println(" l");

    LimitConsumption = param.asInt();    
    LimitOfConsumptionDefined = YES;    
    LimitOfConsumption = (long)(LimitConsumption);

    //write limit of consumption to eeprom
    EEPROMWritelong(ADDRESS_LIMIT_CONSUMPTION_DEFINE,LimitOfConsumptionDefined);
    EEPROMWritelong(ADDRESS_LIMIT_CONSUMPTION,LimitOfConsumption);
    Serial.println("[EEPROM] Limit os water consumption stored in EEPROM.");
}

//Function: counts pulses of water flow sensor
//Params: none
//Return: none 
void CountSensorPulses(void)
{
    SensorPulseCounter++;      
}

//Function: reset counter of water flow sensor pulses
//Params: none
//Return: none 
void ResetCountSensorPulses(void)
{  
    SensorPulseCounter = 0;
}

//Function: calculate water instant flow and consumption
//Params: none
//Return: none 
void CalculateFlowAndConsumption(void)
{
    //this function allows calculations only if sensor it's already calibrated
    if ((CalibrationMode == YES) || (PulsosPerLitter == 0))
        return;    

    //calculation begins!    
    CalculatedFlow = (float)((float)SensorPulseCounter / (float)PulsosPerLitter);  //Flow in l/s
 
    CalculatedConsumption = CalculatedConsumption + CalculatedFlow;   //sum consumption of 1 second
    CalculatedFlow = CalculatedFlow*3600.0;   //Flow in l/h       
    Blynk.run();  //refreshes blynk connection (keep-alive)
}

//Function: send calculated water flow and consumption to Blynk App. 
//          Also, sends the calibration status
//Params: none
//Return: none 
void SendFlowAndConsumptionToApp(void)
{
    WidgetLED CalibrationLED(VP_CALIBRATION_IS_OK);
    
    Blynk.virtualWrite(VP_FLOW_READ, CalculatedFlow); 
    Blynk.virtualWrite(VP_CONSUMPTION_READ, CalculatedConsumption);     
    
    if (PulsosPerLitter == 0)   
        CalibrationLED.off();
    else
        CalibrationLED.on();
                
    Blynk.run();  //refreshes blynk connection (keep-alive)
}

//Function: verify if water consumption is below the defines limit
//Params: none
//Return: YES - water consumption is above limit
//        NO - water consumption is below limit
char VerifyLimitOfConsumption(void)
{
     Blynk.run();  //refreshes blynk connection (keep-alive)
     if (LimitOfConsumptionDefined == NO)
         return NO;

     if (CalculatedConsumption > (float)LimitOfConsumption)
         return YES;
     else
         return NO;      
}

//Function: show calculated water flow and consumption os serial monitor
//Params: none
//Return: none
void ShowDebugMessagesInfoCalculated(void)
{
    char i;
    
    Serial.println(" ");
    
    Serial.print("[FLOW] Calculated flow: ");
    Serial.print(CalculatedFlow);
    Serial.println("l/h");
    Serial.print("[CONSUMPTION] Calculated consumption: ");
    Serial.print(CalculatedConsumption);
    Serial.println("l");
    
    Serial.print("[SENSOR] Number of pulses: ");
    Serial.println(SensorPulseCounter);

    Serial.print("[CAL_STATUS] ");
    if (CalibrationMode == YES)
        Serial.println("YES");
    else
        Serial.println("NO");

    Serial.println("EEPROM KEY: ");

    for(i=0;i<17;i++)
        Serial.print(EEPROMKeyRead[i]);

    Serial.println(" ");
    Serial.print("Calibration factor: ");
    Serial.print(PulsosPerLitter);
    Serial.println(" ");
    
    Blynk.run();  //refreshes blynk connection (keep-alive)
}

//Function: turn on sensor counting pulses
//Params: none
//Return: none
void TurnOnSensorPulseCounting(void)
{
    //this function allows pulse counter reset only if sensor it's already calibrated
    if (CalibrationMode == NO)
        ResetCountSensorPulses();
    
    //attach sensor pin's interrup (rising edge)
    attachInterrupt(digitalPinToInterrupt(PIN_SENSOR), CountSensorPulses, RISING);
    Blynk.run();  //refreshes blynk connection (keep-alive)
}

//Function: turn on sensor counting pulses
//Params: none
//Return: none
void TurnOffSensorPulseCounting(void)
{
    detachInterrupt(digitalPinToInterrupt(PIN_SENSOR));
    Blynk.run();  //refreshes blynk connection (keep-alive)
}

//Function: controls breathing light
//Params: none
//Return: none
void ControlBreathingLight(void)
{
    WidgetLED BreathingLightApp(VP_BREATHING_LIGHT_APP);

    //if this board is under calibration, the breathing led don't blink
    if (CalibrationMode==YES)
    {
         digitalWrite(BREATHING_LIGHT,HIGH);    
         BreathingLightApp.on();
         Blynk.run();  //refreshes blynk connection (keep-alive)        
         return;
    }
    
    VarToggle = ~VarToggle;
    if (!VarToggle)
    {
        digitalWrite(BREATHING_LIGHT,LOW);
        BreathingLightApp.off();
    }
    else    
    {
        digitalWrite(BREATHING_LIGHT,HIGH);    
        BreathingLightApp.on();
    }
    
    Blynk.run();  //refreshes blynk connection (keep-alive)        
}

//Function: starts EEPROM and reads values. If there is no relevant data, default data is written in EEPROM.
//Params: none
//Return: none
void StartEEPROMAndReadValues(void)
{
    char EEPROMKey[] = EEPROM_KEY;
    char i;

    EEPROM.begin(EEPROM_BYTES); 

    //reads first 48 bytes (EEPROM key size) to check if data written on EEPROM is relevant. 
    //If yes, load it. If not, start EEPROM data with default values.
    for(i=0;i<17;i++)
        EEPROMKeyRead[i] = EEPROM.read(i);
 
    EEPROM.end();
    
    if (memcmp(EEPROMKey,EEPROMKeyRead,17) == 0)
    {
        PulsosPerLitter = EEPROMReadlong(ADDRESS_PULSES_PER_LITTER);
        CalibrationMode = EEPROM.read(ADDRESS_CALIBRATION_MODE);
        LimitOfConsumptionDefined = EEPROM.read(ADDRESS_LIMIT_CONSUMPTION_DEFINE);
        LimitOfConsumption = EEPROMReadlong(ADDRESS_LIMIT_CONSUMPTION);
        Serial.println("[EEPROM] The data on EEPROM are ok and loaded successfully");    
    }
    else
    {  
        for(i=0;i<17;i++)
            EEPROM.write(i, EEPROMKey[i]);

        CalibrationMode = NO;
        LimitOfConsumptionDefined = NO;
        LimitOfConsumption = NO;
        PulsosPerLitter = 0;

        EEPROMWritelong(ADDRESS_PULSES_PER_LITTER,PulsosPerLitter);
        EEPROM.write(ADDRESS_CALIBRATION_MODE, CalibrationMode);
        EEPROM.write(ADDRESS_LIMIT_CONSUMPTION_DEFINE, LimitOfConsumptionDefined);
        EEPROMWritelong(ADDRESS_LIMIT_CONSUMPTION,LimitOfConsumption);
        Serial.println("[EEPROM] There was no relevant data on EEPROM.");    
    }
}

//Function: read a long value stores on EEPROM. Reference: http://playground.arduino.cc/Code/EEPROMReadWriteLong
//Params: first address of long value
//Return: long value
long EEPROMReadlong(long address)
{
    EEPROM.begin(EEPROM_BYTES); 

    //Read the 4 bytes from the eeprom memory.
    long four = EEPROM.read(address);
    long three = EEPROM.read(address + 1);
    long two = EEPROM.read(address + 2);
    long one = EEPROM.read(address + 3);

    EEPROM.end();
    
    //Return the recomposed long by using bitshift.
    return ((four << 0) & 0xFF) + ((three << 8) & 0xFFFF) + ((two << 16) & 0xFFFFFF) + ((one << 24) & 0xFFFFFFFF);
}

//Function: write a long value stores on EEPROM. Reference: http://playground.arduino.cc/Code/EEPROMReadWriteLong
//Params: first address of long value and long value itself
//Return: none
void EEPROMWritelong(int address, long value)
{
    EEPROM.begin(EEPROM_BYTES); 
    
    //Decomposition from a long to 4 bytes by using bitshift.
    //One = Most significant -> Four = Least significant byte
    byte four = (value & 0xFF);
    byte three = ((value >> 8) & 0xFF);
    byte two = ((value >> 16) & 0xFF);
    byte one = ((value >> 24) & 0xFF);

    //Write the 4 bytes into the eeprom memory.
    EEPROM.write(address, four);
    EEPROM.write(address + 1, three);
    EEPROM.write(address + 2, two);
    EEPROM.write(address + 3, one);
    EEPROM.end();
}
//Setup function. Here the inits are made!
void setup() 
{
    Serial.begin(115200);

    //starts EEPROM and loads saved values
    StartEEPROMAndReadValues();
    
    //variables init
    ResetCountSensorPulses();
    CalculatedFlow = 0.0;
    CalculatedConsumption = 0.0;
    
    //configure sensor pin as input
    pinMode(PIN_SENSOR, INPUT);

    //configure breathing light pin
    pinMode(BREATHING_LIGHT,OUTPUT);

    //initialize Blynk
    Blynk.begin(BlynkAuth, WiFiNetwork, WiFiPassword);
    
    //start counting sensor's pulses 
    TurnOnSensorPulseCounting();
    VarToggle = 0;
    LastMillis = millis();
}

//main loop / main program
void loop() 
{   
    Blynk.run();    
    
    if ((millis() - LastMillis) >= 1000)
    {
        //to avoid calculation errors, the sensor pulse acquisition is disabled
        //during all calculation     
        TurnOffSensorPulseCounting();
        
        //calculate water and flow consumption
        CalculateFlowAndConsumption();
        Blynk.run();    
        
        //send calculated values to Blynk App
        SendFlowAndConsumptionToApp();       
        Blynk.run();    
        
        //debug messages
        ShowDebugMessagesInfoCalculated();
        Blynk.run();    
        
        //verifiy if water consumption is below limit defined
        if (VerifyLimitOfConsumption() == YES)
             Blynk.virtualWrite(VP_WARNING_CONSUMPTION_LIMIT, "Water consumption is too high!"); 
        else
             Blynk.virtualWrite(VP_WARNING_CONSUMPTION_LIMIT, "Water consumption is below limit."); 
        Blynk.run();    
        
         //reset time-slice processing
        LastMillis = millis();    
        Blynk.run();    
        
        //calculation and sending data is complete! Sensor pulse acquisition starts again        
        TurnOnSensorPulseCounting();
        Blynk.run();    
        
        //control project's breathing light
        ControlBreathingLight();   
        Blynk.run();    
    }
}
