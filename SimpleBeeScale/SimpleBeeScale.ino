#include <HX711.h>
#include <Wire.h> 
#include <LiquidCrystal_I2C.h>
#include <EEPROM.h>

#define ScaleRead 150 //период чтения  чтения данных с тензодатчиков, ms
#define DrawTime 1000 //период обновления экрана, ms
#define KeyReadTime 20 //период проверки нажатия клавиатуры, ms
#define BatteryTime 5000 //период обновления уровня заряда, ms

#define KeypadPin A2 //пин, принимающий сигнал с клавиатуры
#define BattaryPin A3 //пин, принимающий сигнал от батареи

#define CntAvScaleRead 5 //За сколько измерений усреднять значения веса
#define CoefficientScale 24960 //За сколько измерений усреднять значения веса
#define KeypadMaratory 300 //время маротория на считывание значений клавиатуры для исключения 
                          //повторного нажатия, ms
#define MaxBatteryVoltage 4.2 //Напряжение полностью заряженной батареи (значение на ацп)
#define MinBatteryVoltage 2.9 //Напряжение полностью разряженной батареи (значение на ацп)
#define CntMedianFilter 9 //сколько измерений делается для мединного фильтра
#define scalOffsetEEpromCell 0 // (0-3) адресс первой ячейки EEPROM, хранящей scaleOffset 

#define SELECT 1 //коды клавиш
#define UP 2 //коды клавиш
#define DOWN 3 //коды клавиш
#define LEFT 4 //коды клавиш
#define RIGHT 5 //коды клавиш

//19 ячейка EEPROM - хранит порядковый номер записанного веса

HX711 scale;  
LiquidCrystal_I2C lcd(0x3F,16,2);  // Устанавливаем дисплей

volatile int tyme = 0; //переменная прерываний
bool dt = false; //флаг обновления экрана
bool scR = false; //флаг чтения данных с тензодатчиков
bool kr = 0; //флаг периода проверки клавиатуры
bool bt = 1; //флаг периода проверки уровня батареи

//scale variable
float readDigits[CntMedianFilter];
byte cntReadDigits = 0;
float sumScaleValue = 0;
byte cntSumScale = 0;
float scaleValue = 0;
float scaleValueRTU = 0;
long currentScaleOffset = 0;

//keypad variable
byte curPresKey = 0; //код текущей нажатой кнопки
bool keyPressed = 0; //Флан мартория обработки нажатия для исключения повторного нажатия
int lastPressedTime = 0; //время последнего нажатия

//menu / archive variable and other
byte batteryLevel = 0; //уровень заряда батареи
bool flagArchive = 0; //флаг нахождения в режиме работы с архивом
byte adress = 0; //адресс текущей ячейки EEPROM для записи
uint8_t charge0[8] =  //символ заряда батареи
{
  B11111,
  B10000,
  B10111,
  B10111,
  B10111,
  B10111,
  B10000,
  B11111,
};
uint8_t discharge0[8] =  //символ заряда батареи
{
  B11111,
  B10000,
  B10000,
  B10000,
  B10000,
  B10000,
  B10000,
  B11111,
};
uint8_t charge1[8] =  //символ заряда батареи
{
  B11111,
  B00000,
  B11111,
  B11111,
  B11111,
  B11111,
  B00000,
  B11111,
};
uint8_t discharge1[8] =  //символ заряда батареи
{
  B11111,
  B00000,
  B00000,
  B00000,
  B00000,
  B00000,
  B00000,
  B11111,
};
uint8_t charge2[8] =  //символ заряда батареи
{
  B11111,
  B00001,
  B11101,
  B11101,
  B11101,
  B11101,
  B00001,
  B11111,
};
uint8_t discharge2[8] =  //символ заряда батареи
{
  B11111,
  B00001,
  B00001,
  B00001,
  B00001,
  B00001,
  B00001,
  B11111,
};
//---------------- ФУНКЦИИ ----------------
long EEPROM_long_read(int addr) {    
  byte raw[4];
  for(byte i = 0; i < 4; i++) raw[i] = EEPROM.read(addr+i);
  long &num = (long&)raw;
  return num;
}

void EEPROM_long_write(int addr, long num) {
  byte raw[4];
  (long&)raw = num;
  for(byte i = 0; i < 4; i++) EEPROM.write(addr+i, raw[i]);
}

void setSclaeTare (float weight) {
  scale.set_offset(CoefficientScale*weight);
  EEPROM_long_write(scalOffsetEEpromCell, scale.get_offset()); 
}

byte getBatteryLevel() {
  byte batLevelPercent = analogRead(BattaryPin)/(MaxBatteryVoltage/5*1024)*100;
  if (batLevelPercent >= 75) return 4;
  if (batLevelPercent >= 50) return 3;
  if (batLevelPercent >= 25) return 2;
  if (batLevelPercent > 5)  return 1;
  if (batLevelPercent >= 0) return 0;
}

void writeToArchive (byte adress1, float value) {
  if (adress > 166) return;
  
  int adr2 = 20+adress1*6;
  int intValue = round(value*100);
  
  EEPROM.write(adr2, intValue);
  EEPROM.write(adr2+1, intValue>>8); 
//  EEPROM.write(adr2+2, (year<<2) | (month>>2));
//  EEPROM.write(adr2+3, ((month<<6) | date));  
//  EEPROM.write(adr2+4, hour);
//  EEPROM.write(adr2+5, minute); 
  EEPROM.write(19, adress1);
}
void readFromArchive(byte adress1) {
  int adr2 = 20+adress1*6;
  
  scaleValue = EEPROM.read(adr2) | (EEPROM.read(adr2+1)<<8);
//  year=EEPROM.read(adr2+2)>>2;
//  month=(EEPROM.read(adr2+3)>>6) | ((EEPROM.read(adr2+2)&0x3)<<2);
//  date=EEPROM.read(adr2+3) & 0x3F;
//  hour=EEPROM.read(adr2+4);
//  minute=EEPROM.read(adr2+5);
}

//декодирует аналоговый сигнал от клавиатуры в номер нажатой кнопки
byte key() {                       
    //1(SEL-741), 2(LEFT-0),3(DOWN-327),4(UP-143),5(RIGHT-503)
    int val = analogRead(KeypadPin);
//    Serial.println(val);
    if (val < 50) return LEFT; 
    if (val < 200) return UP;
    if (val < 400) return DOWN;
    if (val < 600) return RIGHT;
    if (val < 800) return SELECT;
    return 0;
}
void drawLevelCharge (byte level) {
  if (level > 4) return;

  if (level > 0) { lcd.write(0); } else { lcd.write(1); }
  if (level > 1) { lcd.write(2); } else { lcd.write(3); }
  if (level > 2) { lcd.write(2); } else { lcd.write(3); }
  if (level > 3) { lcd.write(4); } else { lcd.write(5); }
  
}

float GetMedian (float digits[CntMedianFilter]) {
  float temp = 0;
  
  for (int i = 0; i < CntMedianFilter; i++){
    for (int j = 0; j < CntMedianFilter - 1; j++){
      if (digits[j] > digits[j + 1]){
        temp = digits[j];
        digits[j] = digits[j + 1];
        digits[j + 1] = temp;
      }
    }
  }

  float sum = 0;
  for (int i = 0; i < CntMedianFilter; i++) {
//    Serial.print("   ");
//    Serial.println(digits[i]);

     sum += digits[i];
  }
  
  Serial.print("avrage = ");
  Serial.println(sum/15);
  Serial.print("middle = ");
  Serial.println(digits[CntMedianFilter/2]);
  Serial.println();
  
  return digits[CntMedianFilter/2];
}

void drawWeight() {
  lcd.setCursor(0, 1);
  lcd.print("           "); //очистим ранее выведенное
  lcd.setCursor(0, 1);
  lcd.print(scaleValue);
  lcd.print(" kg");  
}

void DrawMenu () {
  lcd.setCursor(13, 0);
  lcd.print("   "); //очистим ранее выведенное
  lcd.setCursor(12, 1);
  lcd.print("    "); //очистим ранее выведенное
  if (flagArchive) {
    lcd.setCursor(13, 0);
    if (adress<10)  lcd.print(0);
    if (adress<100) lcd.print(0);
    lcd.print(adress);
    
  } else {
    lcd.setCursor(12, 1);
    drawLevelCharge(batteryLevel);
  }

  drawWeight();
}

//---------------- System ФУНКЦИИ ----------------
void KeyPad () {
 if (!keyPressed){ //мороторий на нажатие?
    //ветка "нет"
   byte keyCode = key(); 

   if (keyCode != 0) {
    keyPressed = 1;
    lastPressedTime = millis(); 
   }
   
   switch (keyCode) {
    case SELECT:
      //запись в архив
      Serial.println("Select");
      if (!flagArchive) {
        adress = EEPROM.read(19); //последняя записанная ячейка
        adress++;
        writeToArchive(adress, scaleValue);
        flagArchive=!flagArchive;
        Serial.println("Write");
      }
      
      break;
    case UP:
      Serial.println("Up");
      if (flagArchive) {
        adress++;
        if (adress == 167) {adress = 1;}
        readFromArchive(adress);
      }
      break;
    case DOWN:
      Serial.println("Down");
      if (flagArchive) {
        adress--; 
        if (adress == 0) {adress = 166;}
        readFromArchive(adress);
      }
      break;
    case LEFT:
      //сброс до тары
      if (flagArchive) return;
      
      Serial.println("setSclaeTare(scaleValue);");
      setSclaeTare(scaleValue);
      break;
    case RIGHT:
      //вход в архив
      flagArchive=!flagArchive;
      adress = EEPROM.read(19); //последняя записанная ячейка
      
      if (!flagArchive) {
        Serial.println("Archiv OFF");
      } else {
        readFromArchive(adress);
        Serial.println("Archiv IN");
      }
      
      break;
    default:
      keyPressed = 0;
      break;
   }
 } else {
  int currentTime = millis();
  if ((currentTime-lastPressedTime)>KeypadMaratory) {
    keyPressed = 0;
  }   
 }
}

void ReadScale () {
  readDigits[cntReadDigits] = scale.get_units();
//  Serial.print("weight = ");
//  Serial.println(readDigits[cntReadDigits]);
  cntReadDigits++;
  
  if (cntReadDigits >= CntMedianFilter) {
//    Serial.print("weight Median = ");
//    Serial.println(GetMedian (readDigits));
    sumScaleValue += GetMedian (readDigits);
    cntSumScale++;
    cntReadDigits = 0;

    if (cntSumScale >= CntAvScaleRead) {
      scaleValueRTU = sumScaleValue/cntSumScale;
//      Serial.print("weight RTU = ");
//      Serial.println(scaleValueRTU);
      cntSumScale = 0;
      sumScaleValue = 0;
    }
  }

  if (!flagArchive) {
   scaleValue = scaleValueRTU;
  }
}

void UpdateBatteryLevel() {
  batteryLevel = getBatteryLevel();
}


void setup() {
  OCR0A = 0xAF; //прерывание
  TIMSK0 |= _BV(OCIE0A); //прерывание

  // HX711.DOUT  - pin #A1
  // HX711.PD_SCK - pin #A0
  scale.begin(A1, A0);
  scale.set_scale(CoefficientScale);     // this value is obtained by calibrating the scale with known weights; see the README for details
  scale.set_offset(EEPROM_long_read(scalOffsetEEpromCell)); //установим весы относительно последнего сброса тары
  
  lcd.init(); // initialize the LCD
  lcd.createChar(0, charge0);
  lcd.createChar(1, discharge0);
  lcd.createChar(2, charge1);
  lcd.createChar(3, discharge1);
  lcd.createChar(4, charge2);
  lcd.createChar(5, discharge2);
  lcd.backlight(); // Включаем подсветку дисплея
  lcd.clear();
  
  pinMode(KeypadPin, INPUT);
  pinMode(BattaryPin, INPUT);
  Serial.begin(9600);
  Serial.println("Start!");
}

//прерывание, формирует мини операционную систему (system)
SIGNAL(TIMER0_COMPA_vect) { 
  tyme = tyme + 1;
  if (tyme % ScaleRead == 0) { scR = 1; };
  if (tyme % DrawTime == 0) { dt = 1; };
  if (tyme % KeyReadTime == 0) { kr = 1; };
  if (tyme % BatteryTime == 0) { bt = 1; };
}

void loop() {
  if (scR == 1) { scR = 0; ReadScale(); };  
  if (dt == 1) { dt = 0; DrawMenu(); };
//  if (kr == 1) { kr = 0; KeyPad(); };
  if (bt == 1) { bt = 0; UpdateBatteryLevel(); };
}



