#include <Adafruit_AHTX0.h>
#include <LiquidCrystal_I2C.h>
#include <Bonezegei_DS3231.h>
#include <GyverNTC.h>
#include <SPI.h>
#include <SD.h>
#include <EEPROM.h>

#define DEBUG_MODE true

// Pin definitions
#define NTC_PIN A1
#define KEYPAD_PIN A2
#define POT_PIN A3

#define RELAY_HEAT 2
#define RELAY_FAN 3
#define RELAY_LIGHT 4
#define RELAY_LIGHT2 5
#define SD_CHIPS_SELECT 10

// EEPROM addresses
#define ROM_DAYSTART_H 0
#define ROM_DAYSTART_M 1
#define ROM_DAYEND_H 2
#define ROM_DAYEND_M 3

#define ROM_DAYTEMP_MIN 4
#define ROM_DAYTEMP_MAX 5
#define ROM_NIGHTTEMP_MIN 6
#define ROM_NIGHTTEMP_MAX 7

#define ROM_VENT_FREQUENT 8
#define ROM_VENT_LENGTH 9

// pause after on/off relay (cannot pull in/release two relays at the same time)
#define RELAY_DELAY 2000


Adafruit_AHTX0 aht;
LiquidCrystal_I2C lcd(0x27, 20, 4);
Bonezegei_DS3231 rtc(0x68);
File myFile;
GyverNTC therm(NTC_PIN, 10000, 3950); // analóg pin, ellenállás, ntc típus

int err = 0; // error code on booting

void setup() {
  // EEPROM reset - CSAK EGYSZER VÉGREHAJTANI! / ONLY ONCE RUN!
  //for(int i = 0; i < 1023; i++) EEPROM.write(i, 0);
  
  // soros konzol bekapcsolása
  Serial.begin(9600);

  // hő és pára mérő
  if (!aht.begin(&Wire, 0, 0x38)) {
    if(DEBUG_MODE) Serial.println("Could not find AHT on 0x38? Check wiring");
    err = 1;
  }

  // relék, 2-5
  pinMode(RELAY_HEAT, OUTPUT);
  pinMode(RELAY_FAN, OUTPUT);
  pinMode(RELAY_LIGHT, OUTPUT);
  pinMode(RELAY_LIGHT2, OUTPUT);

  // relék lekapcsolása
  digitalWrite(RELAY_HEAT, HIGH);
  digitalWrite(RELAY_FAN, HIGH);
  digitalWrite(RELAY_LIGHT, HIGH);
  digitalWrite(RELAY_LIGHT2, HIGH);

  // lcd
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Start...");

  // potméter miatt kellhet
  analogReference(DEFAULT);

  // RTC
  rtc.begin();
  rtc.setFormat(24); 

  // SD kártya, 10-es pin a chip select
  if (!SD.begin(SD_CHIPS_SELECT)) {
    if(DEBUG_MODE) Serial.println("Sd kartya hiba!");
    err = 2;
  }

  if(err > 0) {
    lcd.setCursor(0, 0);
    lcd.print("Hiba: ");
    switch(err) {
      case 1: 
        lcd.print("homero");
         break;
      case 2: 
        lcd.print("SD kartya");
        break;
    }
  }

  delay(3000);
  lcd.clear();
}


// lenyomott gomb kódját visszaadja (0-4), ha nincs lenyomva, akkor -1
// -1 - nincs lenyomott gomb, 0 - bal, 1 - fel, 2 - jobbra, 3 - le, 4 - enter
#define KEY_NONE -1
#define KEY_LEFT 0
#define KEY_UP 1
#define KEY_RIGHT 2
#define KEY_DOWN 3
#define KEY_ENTER 4

int getKey() {
  int r = analogRead(KEYPAD_PIN); // A2
  if(r < 20) return KEY_LEFT;
  if(r < 50) return KEY_UP;
  if(r < 120) return KEY_DOWN;
  if(r < 200) return KEY_RIGHT;
  if(r < 400) return KEY_ENTER;
  return KEY_NONE;
}


// vár a gombok felengedésre
void relaseKey() {
  while(getKey() != KEY_NONE) delay(20);
}


// vár egy gombnyomásra és felengedésre, és visszaadja a kódját (0-4)
int readKey() {
  int k = KEY_NONE;
  while(k == KEY_NONE) {
    delay(20); // enélkül furcsa értékeket is visszaad
    k = getKey();
  }
  relaseKey(); // waiting for relase key
  return k;
}


// visszaadja a potméter értékét, konvertálva a megfelelő határok közzé
int getPot(int minV, int maxV) {
  int v;
  v = analogRead(POT_PIN); // A3
  return minV + v / (1024.0 / (maxV - minV +1.0));
}


// visszaadja a dátumot és az időt formázva
void getTime(char *t) {
  rtc.getTime();
  int y = rtc.getYear();
  if(y < 24 || y > 99) y = 24;
  sprintf(t, "20%02d-%02d-%02d %02d:%02d", y, rtc.getMonth(), rtc.getDate(), rtc.getHour(), rtc.getMinute());
}


// visszaadja az időt formázva
void getHourMin(char *t) {
  rtc.getTime();
  sprintf(t, "%02d:%02d", rtc.getHour(), rtc.getMinute());
}


// elmenti a dátumot és az időt
void saveTime(int y, int m, int d, int h, int mn) {
  /*if(y < 24 || y > 99) y = 24;
  if(m < 1 || m > 12) m = 1;
  if(d < 1 || d > 31) d = 1;
  if(h < 0 || h > 23) h = 1;
  if(mn < 0 || mn > 59) mn = 1;*/
  char t[32];
  sprintf(t, "%02d:%02d:00", h, mn);
  rtc.setTime(t);  

  sprintf(t,"%d/%d/%d", m, d, y);
  rtc.setDate(t);
}


// visszadja a t stringben pos pozíciónál végződő kétjegyű szám értékét
int getActVal(char *t, int pos) {
  return (t[pos - 1] - '0') * 10 + (t[pos] - '0');
}


// visszadja a megfelelő pozícióhoz tartozó minimum és maximum értékeket a dátum/idő beállításnál
void getMinMax(int pos, int *minV, int *maxV) {
  switch(pos) {
    case 3:
      *minV = 24;
      *maxV = 99;
      break;
    case 6:
      *minV = 1;
      *maxV = 12;
      break;
    case 9:
      *minV = 1;
      *maxV = 31;
      break;
    case 12:
      *minV = 0;
      *maxV = 23;
      break;
    case 15:
      *minV = 0;
      *maxV = 59;
      break;
  }
}


// idő (és dátum) beállító menüpont
void setTime() {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Ido beallitasa");
    char t[32];
    char actS[4];
    getTime(t);
    lcd.setCursor(0, 1);
    lcd.print(t);
    
    lcd.blink();
    bool q = false;
    bool pot = false;
    int k = -1;
    int pos = 3;
    int actVal, minV, maxV;
    actVal = getActVal(t, pos);
    getMinMax(pos, &minV, &maxV);
    int year, month, day, hour, minut;
    relaseKey(); // addig itt marad, amíg fel nem engedjük a gombot
    while(!q) {
      lcd.setCursor(pos, 1);
      if(!pot) k = readKey(); else {
        k = getKey();
        if(k != KEY_RIGHT && k != KEY_ENTER) k = KEY_NONE;
        if(k != -1) relaseKey();
      }
      //Serial.println(k);
      // ok gomb következőre lép vagy a végén ment
      if(k == KEY_ENTER) {
        pot = false;
        if(pos == 3) {
          year = actVal;
          //Serial.println("year: " + String(year));
          pos = 6;
        } else if(pos == 6) {
          month = actVal;
          pos = 9;
        } else if(pos == 9) {
          day = actVal;
          pos = 12;
        } else if(pos == 12) {
          hour = actVal;
          pos = 15;
        } else {
          minut = actVal;
          saveTime(year, month, day, hour, minut);
          q = true;
        }
        actVal = getActVal(t, pos);
        getMinMax(pos, &minV, &maxV);
      }
      // fel/le gombokkal növel/csökkent
      if(k == KEY_UP || k == KEY_DOWN) {
        if(k == KEY_UP) {
          actVal++;
          if(actVal > maxV) actVal = maxV;
        }
        if(k == KEY_DOWN) {
          actVal--;
          if(actVal < minV) actVal = minV;
        }
        lcd.setCursor(pos -1, 1);
        sprintf(actS, "%02d", actVal);
        lcd.print(actS);
      }

      if(k == KEY_LEFT) { // balra gomb kilépés mentés nélkül
        q = true;
        return;
      }
      
      if(pot) {
        actVal = getPot(minV, maxV);
        lcd.setCursor(pos -1, 1);
        sprintf(actS, "%02d", actVal);
        lcd.print(actS);
        lcd.setCursor(pos, 1);
        delay(100);
      }
      
      if(k == KEY_RIGHT) { // jobbra gomb a potmétert kapcsolja be/ki
        pot = !pot;
        relaseKey(); // addig itt marad, amíg fel nem engedjük a gombot
      }    
   }
}


int getNum(int x, int old_act, int minV, int maxV) {
  int act = old_act;
  char actS[4];
  lcd.setCursor(x, 1);
  lcd.blink();
  sprintf(actS, "%02d", act);
  lcd.print(actS);
  relaseKey(); // addig itt marad, amíg fel nem engedjük a gombot
  bool q = false;
  bool pot = false;
  int k = KEY_NONE;
    
  while(!q) {
      lcd.setCursor(x + 1, 1);
      if(!pot) k = readKey(); else {
        k = getKey();
        if(k != KEY_RIGHT && k != KEY_ENTER && k != KEY_LEFT) k = KEY_NONE;
        if(k != KEY_NONE) relaseKey();
      }
      
      if(k == KEY_ENTER) { // enter
        return act;
      }

      if(k == KEY_UP || k == KEY_DOWN) { // fel/le 
        if(k == KEY_UP) {
          act++;
          if(act > maxV) act = maxV;
        }
        if(k == KEY_DOWN) {
          act--;
          if(act < minV) act = minV;
        }
        lcd.setCursor(x, 1);
        sprintf(actS, "%02d", act);
        lcd.print(actS);
      }
      
      if(k == KEY_LEFT) { // balra gomb kilépés mentés nélkül
        lcd.setCursor(x, 1);
        sprintf(actS, "%02d", old_act);
        lcd.print(actS);
        return old_act;
      }
      
      if(pot) { 
        act = getPot(minV, maxV);
        lcd.setCursor(x, 1);
        sprintf(actS, "%02d", act);
        lcd.print(actS);
        lcd.setCursor(x + 1, 1);
        delay(100);
      }
      
      if(k == KEY_RIGHT) { // potmeter on/off
        pot = !pot;
        relaseKey(); // addig itt marad, amíg fel nem engedjük a gombot
      }
  }
}


void saveData(int n, int val) {
  if(DEBUG_MODE) {
    Serial.print("saveData n: "); Serial.print(n); Serial.print(", val: "); Serial.println(val);
  }
  EEPROM.update(n, val & 0xff); // write helyett update, ha nem módosult, ne írjuk fölöslegesen
}


int readData(int n) {
  return EEPROM.read(n);
}


// időpont beállítása fűtéshez, világításhoz, stb...
void addTime(int n) {
  char act_t[6] = "12:34"; // ezt a nem felejtő tárból kell majd olvasni, a switchben
  
  lcd.clear();
  lcd.setCursor(0, 0);

  switch(n) {
    case 0:
      lcd.print("Nappal kezdete");
      sprintf(act_t, "%02d:%02d", readData(ROM_DAYSTART_H), readData(ROM_DAYSTART_M));
      break;
    case 1:
      lcd.print("Nappal vege");
      sprintf(act_t, "%02d:%02d", readData(ROM_DAYEND_H), readData(ROM_DAYEND_M));
      break;
  }
  
  int h, m;
  lcd.setCursor(1, 1);
  lcd.print(act_t);
  h = getNum(1, getActVal(act_t, 1), 0, 23);
  m = getNum(4, getActVal(act_t, 4), 0, 59);

  switch(n) { // értékek mentése az eepromba
    case 0:
      saveData(ROM_DAYSTART_H, h); // 0-1 címek - nappal kezddete
      saveData(ROM_DAYSTART_M, m);
      break;
    case 1:
      saveData(ROM_DAYEND_H, h); // 2-3 címek - nappal vége
      saveData(ROM_DAYEND_M, m);
      break;
  }
}


void addTemp(int n) {
  char act_t[3] = "00"; // ezt a nem felejtő tárból kell majd olvasni, a switchben
  char act_h[3] = "00"; // ezt a nem felejtő tárból kell majd olvasni, a switchben
  
  lcd.clear();
  lcd.setCursor(0, 0);

  switch(n) {
    case 0:
      lcd.print("Nappali futes");
      sprintf(act_t, "%02d", readData(ROM_DAYTEMP_MIN));
      sprintf(act_h, "%02d", readData(ROM_DAYTEMP_MAX));
      break;
    case 1:
      lcd.print("Esti futes");
      sprintf(act_t, "%02d", readData(ROM_NIGHTTEMP_MIN));
      sprintf(act_h, "%02d", readData(ROM_NIGHTTEMP_MAX));
      break;
  }

  int t, h;
  lcd.setCursor(2, 1);
  lcd.print(act_t);
  lcd.print((char)223);
  lcd.print("C - ");
  lcd.print(act_h);
  lcd.print((char)223);
  lcd.print("C");
  t = getNum(2, getActVal(act_t, 1), 0, 40);
  h = getNum(9, getActVal(act_h, 1), 0, 40);

  switch(n) { // értékek mentése az eepromba
    case 0:
      saveData(ROM_DAYTEMP_MIN, t); // 4-5 címek - nappali fűtés min-max
      saveData(ROM_DAYTEMP_MAX, h);
      break;
    case 1:
      saveData(ROM_NIGHTTEMP_MIN, t); // 6-7 címek - esti fűtés min-max
      saveData(ROM_NIGHTTEMP_MAX, h);
      break;
  }
}


void addVent(int n) {
  char act_t[3] = "00";

  lcd.clear();
  lcd.setCursor(0, 0);
  
  switch(n) {
    case 0:
      lcd.print("Szellozes gyak.");
      sprintf(act_t, "%02d", readData(ROM_VENT_FREQUENT)); // 8 cím - szellőztetés gyakorisága
      break;
    case 1:
      lcd.print("Szellozes hossz");
      sprintf(act_t, "%02d", readData(ROM_VENT_LENGTH)); // 9 cím - szellőztetés hossza
      break;
  }

  lcd.setCursor(2, 1);
  lcd.print(act_t);
  
  int i;
  
  switch(n) {
    case 0:
      lcd.print(" orankent");
      i = getNum(2, getActVal(act_t, 1), 0, 12);
      saveData(ROM_VENT_FREQUENT, i); // 8 cím - szellőztetés gyakorisága
      break;
    case 1:
      lcd.print(" percig");
      i = getNum(2, getActVal(act_t, 1), 1, 59);
      saveData(ROM_VENT_LENGTH, i); // 9 cím - szellőztetés hossza
      break;
  }
}


void showAirTemp() {
  sensors_event_t humidity, temp;
  aht.getEvent(&humidity, &temp);

  int air_temper = int(trunc(temp.temperature)); // a levegő hőmérséklete
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Levego hom.");
  lcd.setCursor(2, 1);
  lcd.print(String(air_temper));
  lcd.print((char)223); // fokjel karakter
  lcd.print("C");

  while(getKey() != KEY_LEFT) delay(30); // itt marad a balra gomb lenyomásáig
  relaseKey(); // gomb felengedést is meg kell várni, mert több menüszintből is kiléphet véletlenül
}


// idő korábbi értéke (hogy csak akkor frissüljön a képernyő, ha változás van)
char old_t[6] = "--:--";
int old_temper = -1, old_hum = -1;
bool daytime = false;

void baseLoop() {
  sensors_event_t humidity, temp;
  aht.getEvent(&humidity, &temp);
  
  char t[6];
  getHourMin(t);
  int hm = getActVal(t, 1) * 60 + getActVal(t, 4); // a jelenlegi idő percben
  //Serial.println(hm); Serial.println();

  // ha változott az idő
  if(strncmp(t, old_t, 5) != 0) {
    strcpy(old_t, t);
    lcd.setCursor(1, 0);
    lcd.print(t);
  }

  int air_temper = int(trunc(temp.temperature)); // a levegő hőmérséklete
  int temper = int(trunc(therm.getTempAverage())); // hőmérsékletet az ntc-ből kérjük le
  int hum = int(trunc(humidity.relative_humidity));

  // ha változott a hőmérséklet
  if(temper != old_temper) {
    old_temper = temper;
    lcd.setCursor(7, 0);
    lcd.print(String(temper));
    lcd.print((char)223); // fokjel karakter
    lcd.print("C");
  }
  
  // ha változott a páratartalom
  if(hum != old_hum) {
    old_hum = hum;
    lcd.setCursor(12, 0);
    lcd.print(String(hum));
    lcd.print("%");
  }

  int dth = readData(ROM_DAYSTART_H) * 60 + readData(ROM_DAYSTART_M);  // nap kezdete percben
  int dteh = readData(ROM_DAYEND_H) * 60 + readData(ROM_DAYEND_M); // nap vége percben
  if(hm < dth || hm > dteh) daytime = false; else daytime = true;

  heatCheck(temper);
  
  lightCheck();
  
  ventCheck(getActVal(t, 1), getActVal(t, 4)); // órát, percet megkapja
  

  // delay(1000) helyett a jobb válaszidőért
  int i = 0;
  while(i < 50 && getKey() == KEY_NONE) {
    i++;
    delay(20);
  }
}


#define MAX_ITEM 7

void menuLine(int n) {
  lcd.setCursor(0, 1);
  switch(n) {
    case 0:
      lcd.print(" Ora beallitas");
      break;
    case 1:
      lcd.print(" Nappal kezdete");
      break;
    case 2:
      lcd.print(" Nappal vege");
      break;
    case 3:
      lcd.print(" Nappali futes");
      break;
    case 4:
      lcd.print(" Esti futes");
      break;
    case 5:
      lcd.print(" Szellozes gyak.");
      break;
    case 6:
      lcd.print(" Szellozes hossz");
      break;
    case 7:
      lcd.print(" Levego hom.");
      break;

      
  }
  lcd.print("        "); // ne kelljen törölni az egész képernyőt
}

void mainMenu() {
  lcd.setCursor(0, 0);
  lcd.print("Menu    < Vissza");
  bool q = false;
  int item = 0;
  menuLine(item);
  while(getKey() == KEY_ENTER) delay(30); // addig itt marad, amíg az entert fel nem engedjük (nehogy továbbmenjen az első menüpontra)
  while(!q) {
    if(getKey() == KEY_LEFT) q = true; // balra gombra kilép
    if(getKey() == KEY_ENTER) { // enterre belép az almenübe
      switch(item) {
        case 0:
          setTime();
          break;
        case 1:
          addTime(0);
          break;
        case 2:
          addTime(1);
          break;
        case 3:
          addTemp(0);
          break;
        case 4:
          addTemp(1);
          break;
        case 5:
          addVent(0);
          break;
        case 6:
          addVent(1);
          break;
        case 7:
          showAirTemp();
          break;
          
      }
      lcd.noBlink();
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Menu    < Vissza");
      menuLine(item);
      relaseKey(); // addig itt marad, amíg fel nem engedjük a gombot
    }

    if(getKey() == KEY_UP) { // fel - lépkedés a menüben
      item--;
      if(item < 0) item = MAX_ITEM;
      menuLine(item);
      relaseKey();
    }
    
    if(getKey() == KEY_DOWN) { // le - lépkedés a menüben
      item++;
      if(item > MAX_ITEM) item = 0;
      menuLine(item);
      relaseKey();
    }
    
    delay(30);
  }
}


void heatCheck(int t) {
  int min_t, max_t;

  bool heathing = digitalRead(RELAY_HEAT) == LOW;
  
  if(daytime) {
    min_t = readData(ROM_DAYTEMP_MIN); // nappali min-max hőmérséklet
    max_t = readData(ROM_DAYTEMP_MAX);
  } else {
    min_t = readData(ROM_NIGHTTEMP_MIN); // esti min-max hőmérséklet
    max_t = readData(ROM_NIGHTTEMP_MAX);    
  }

  if(t < min_t && !heathing) {
    digitalWrite(RELAY_HEAT, LOW);
    delay(RELAY_DELAY);
  }
  
  if(t >= max_t && heathing) {
    digitalWrite(RELAY_HEAT, HIGH); 
    delay(RELAY_DELAY);
  }

  lcd.setCursor(1, 1);
  if(heathing) {
    lcd.print("F");
  } else lcd.print(" ");
}


void lightCheck() { // világítás D4 (és majd D5)
  bool lighting = digitalRead(RELAY_LIGHT) == LOW;
  if(daytime) {
    if(!lighting) {
      digitalWrite(RELAY_LIGHT, LOW); 
      delay(RELAY_DELAY);
    }
  } else {
    if(lighting) {
      digitalWrite(RELAY_LIGHT, HIGH);
      delay(RELAY_DELAY);
    }
  }
  
  lcd.setCursor(3, 1);
  if(lighting) {
    lcd.print("V");
  } else lcd.print(" ");
}


bool ventTime(int h, int m) {
  int rep = readData(ROM_VENT_FREQUENT);
  int len = readData(ROM_VENT_LENGTH);

  if(rep == 0) return false;
  if(len > m) {
    if(rep == 1) return true;
    if(h % rep == 0) return true;
  }
  
  return false;
}


void ventCheck(int h, int m) { // szellőztetés D3
  bool venting = digitalRead(RELAY_FAN) == LOW;

  lcd.setCursor(5, 1);
  if(ventTime(h, m)) {
    if(!venting) {
      digitalWrite(RELAY_FAN, LOW);
      delay(RELAY_DELAY);
    }
    lcd.print("Sz");
  } else {
    if(venting) {
      digitalWrite(RELAY_FAN, HIGH);
      delay(RELAY_DELAY);
    }
    lcd.print("  ");
  }
}


void loop() {
  baseLoop();
  if(getKey() == 4) {
    mainMenu();
    lcd.clear();
    old_temper = -1;
    old_hum = -1;
    strcpy(old_t, "00:00");
  }
}
