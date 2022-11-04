
// For Hackrf Portapack Rolljam capture.
// Arduino Mega, connect pin 52 to either of the two middle pins of a 433MHz Receiver Module,
// connect pin 53 to 5v on Digispark with DigisparkJammerTimer code,
// connect negative from Digispark and Reciever Module to GND on Mega.
// ( see   https://youtu.be/r8ci0oNYmOA   for demo )

void setup() {
  Serial.begin(9600);
  pinMode(51, INPUT);
  pinMode(53, OUTPUT);
}

void loop ()
{
  int Remote =digitalRead(51);
  if (Remote) {
    Serial.println("                              Remote Control detected , Timejam activated ....");
    digitalWrite(53, HIGH);
    delay(45000);
  } else {
    Serial.println("=== nothing detected ..");
    digitalWrite(53, LOW);
    
    
  }
delay(260);
}
