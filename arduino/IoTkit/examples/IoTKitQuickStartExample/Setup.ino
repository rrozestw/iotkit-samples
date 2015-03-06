#include <stdio.h>
#include <IoTkit.h>
#include <Ethernet.h>
#include <aJSON.h>
#include <rgb_lcd.h>
#include <Wire.h>

char const * networkSSID="CHANGE_ME";
char const * WIFI_identity="CHANGE_ME";
char const *  WIFI_password="CHANGE_ME";
char const * activationCode="CHANGE_ME";

IoTkit iotkit;
int inPinTemperatureSensor = A0;
int outPinLed = 3;
rgb_lcd lcd;

float TemperatureValue;
int B=3975;                  //B value of the thermistor
float resistance;

void setup() {
  Serial.begin(115200);
  iotkit.begin();
  delay(1000);
  if(network_not_set()){
     setNetwork();
  }
  if(device_not_activated()){
   activateAndRegisterComponents();
  }

   pinMode(outPinLed, OUTPUT);
  lcd.begin(16, 2);
}

void loop() {
 readAllSensors();
 turnOnOfLed();
 Display();
 iotkit.send("temperature", TemperatureValue);
 delay(1000);
}

void readAllSensors(){
  int temp =  analogRead(inPinTemperatureSensor);
  resistance=(float)(1023-temp)*10000/temp; //get the resistance of the sensor;
  TemperatureValue=1/(log(resistance/10000)/B+1/298.15)-273.15;//convert to temperature via datasheet ;
}

void turnOnOfLed(){
 if( TemperatureValue > 36.6){
    digitalWrite(outPinLed, HIGH);
 } else {
   digitalWrite(outPinLed, LOW);
 }
}

void lcdClearRow(int row){
  lcd.setCursor(0,row);
  lcd.print("                ");
}

float minTempBlue = 20;//0% value
float maxTempRed = 50;//100% value
void Display(){
  //from blue to red
  int tempProcentValue = (TemperatureValue - minTempBlue) * (100 / (maxTempRed-minTempBlue));
  lcd.setRGB(255*tempProcentValue/100,0,(255 * (100 - tempProcentValue))/100);

  lcdClearRow(0);
  lcd.setCursor(0,0);
  lcd.print("Temperature:");
  char* formated = new char[512];
  sprintf(formated, "%.2f C",TemperatureValue);
  lcd.setCursor(0,1);
  lcd.print(String(formated));

}

boolean device_not_activated(){
  FILE *scans  = popen("iotkit-admin -V" ,"r");
  char buffer[6];
  char *line_p = fgets(buffer, sizeof(buffer), scans);
  Serial.println("Agent version");
  Serial.println(buffer);
  if(buffer[0]=='1'){
    if(buffer[2]=='5'){

     FILE *grep  = popen( "grep 'deviceToken' /usr/share/iotkit-agent/certs/token.json | awk '/,/{gsub(/ /,\"\",$0);gsub(/\"deviceToken\":/,\"\",$0);print $0;}'","r");
     fgets(buffer, sizeof(buffer), grep);
     Serial.println(buffer);
     if(strcmp(buffer,"false")==0){
        Serial.println("Token not set");
       return true;
     }
    }
  }
  Serial.println("Token set");
  return false;
}
void activateAndRegisterComponents(){

  char* formated = new char[512];
  sprintf(formated, "iotkit-admin activate %s > /dev/ttyGS0 2>&1", activationCode);
  Serial.println(formated);
  system(formated);
  system("iotkit-admin register temperature temperature.v1.0 > /dev/ttyGS0 2>&1");
  system("systemctl start iotkit-agent");
  system("systemctl enable iotkit-agent");
}

boolean network_not_set(){
  FILE *scans  = popen("wpa_cli status | grep ^ssid | awk '{split($0,s,\"=\");print s[2]}' " ,"r");
  if(scans){
     char buffer[strlen(networkSSID)+1];
     char *line_p = fgets(buffer, sizeof(buffer), scans);

     if(strcmp(buffer,networkSSID) == 0){
       return false;
     }
  }
  Serial.println("Network not set");
  return true;
}

String scanNetwork(){
  system("systemctl stop hostapd && sleep 2 && systemctl start wpa_supplicant");

  for(int i=0;i<6;i++){
    system("wpa_cli scan");
    delay(1000);
  }
  char* formated = new char[512];
  sprintf(formated, "wpa_cli scan_results | grep %s | awk 'NR==1{print $4}'",networkSSID);
  Serial.println(formated);
  FILE *scans  = popen(formated ,"r");

  if (scans)
  {
    char buffer[1024];
    char *line_p = fgets(buffer, sizeof(buffer), scans);

    Serial.print(buffer);
    if(buffer[1]=='W' && buffer[2]=='E'){return "WEP";}
        if(buffer[1]=='O'){return "OPEN";}
         if(buffer[1]=='W' && buffer[2]=='P'){
           if(buffer[4]=='2'){
             if(buffer[6]=='E')
             return "EAP";
             return "PSK";
           }
           if(buffer[5]=='E')
           return "EAP";
           return "PSK";
         }
  }
  return "";
}

void setNetwork(){
    String networkType = scanNetwork();

    int len = 0;
    char * network_conf2 = new char[512];
    if(networkType!=""){

       if(networkType== "EAP"){
          len = sprintf(network_conf2,"network={\nssid=\"%s\"\n\nkey_mgmt=WPA-EAP\npairwise=CCMP TKIP\ngroup=CCMP TKIP WEP104 WEP40\neap=TTLS PEAP TLS\nidentity=\"%s\"\npassword=\"%s\"\nphase1=\"peaplabel=0\"\n}\n",networkSSID,WIFI_identity,WIFI_password);
       }
       if(networkType== "OPEN"){
       len = sprintf(network_conf2,"network={\nssid=\"%s\"\n\nkey_mgmt=NONE\n}\n",networkSSID);
       }
       if(networkType== "PSK"){
       len = sprintf(network_conf2,"network={\nssid=\"%s\"\n\nkey_mgmt=WPA-PSK\npairwise=CCMP TKIP\ngroup=CCMP TKIP WEP104 WEP40\neap=TTLS PEAP TLS\npsk=\"%s\"\n}\n",networkSSID,WIFI_password);
       }
       if(networkType== "WEP"){
       len = sprintf(network_conf2,"network={\nssid=\"%s\"\n\nkey_mgmt=NONE\ngroup=WEP104 WEP40\nwep_key0=\"%s\"\n}\n",networkSSID,WIFI_password);
       }


   FILE *file =  fopen("/etc/wpa_supplicant/wpa_supplicant.conf","a");


   fwrite(network_conf2,sizeof(char),len,file);

   fclose(file);
   Serial.print("Restarting network");
   system("systemctl stop hostapd");
   system("systemctl restart wpa_supplicant");
   system("systemctl enable wpa_supplicant");
   system("wpa_cli select_network 0");
   //we wait max 30s for
   for(int i=0;i<30;i++){
      FILE *scans  = popen("wpa_cli status | grep ^ip_address | awk '{split($0,s,\"=\");print s[2]}' " ,"r");
      if(scans){
         char buffer[1];
         fgets(buffer, sizeof(buffer), scans);
         if(String(buffer)!=""){
           break;
         }
      }
      delay(1000);
   }

    } else {
      Serial.print("Network type not detected!");
    }

}


