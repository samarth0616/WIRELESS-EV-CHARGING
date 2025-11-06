// Include the library for the servo motor
#include <Servo.h>
// Include the Wire library for I2C communication
#include <Wire.h>
// Include the LiquidCrystal_I2C library for the LCD
#include <LiquidCrystal_I2C.h>

// Define the pins used
const int IRSensorPin = 2;   
const int RedLEDPin = 5;     
const int GreenLEDPin = 4;  
const int ServoMotorPin = 9; 

// Create a Servo object
Servo myservo;

// Define servo positions (in degrees)
const int ClosePosition = 0;   
const int OpenPosition = 90;   

// Initialize the LCD object
// Arguments: (I2C address, columns, rows)
// The common I2C addresses are 0x27 or 0x3F. Change 0x27 if yours is different.
LiquidCrystal_I2C lcd(0x27, 16, 2); 

// Variable to store the IR sensor value
int carDetected = LOW;

void setup() {
  // Initialize the pins as output or input
  pinMode(RedLEDPin, OUTPUT);
  pinMode(GreenLEDPin, OUTPUT);
  pinMode(IRSensorPin, INPUT);

  // Attach the servo object to the pin
  myservo.attach(ServoMotorPin);

  // --- LCD Initialization and Welcome Message ---
  lcd.init();      // Initialize the LCD
  lcd.backlight(); // Turn on the backlight (if applicable)

  // Display the initial "Welcome" message
  lcd.clear();
  lcd.setCursor(4, 0); // Set cursor to column 4, row 0
  lcd.print("WELCOME");
  lcd.setCursor(0, 1); // Set cursor to column 0, row 1
  lcd.print("Gate Closed - RED");
  
  // Initial state: Red light ON and servo closed
  digitalWrite(RedLEDPin, HIGH);  
  digitalWrite(GreenLEDPin, LOW); 
  myservo.write(ClosePosition);   
}

void loop() {
  // Read the state of the IR sensor
  carDetected = digitalRead(IRSensorPin);

  // Check if a car is detected
  if (carDetected == LOW) {
    // 1. Open the servo barrier
    myservo.write(OpenPosition);

    // 2. Change the light to Green
    digitalWrite(RedLEDPin, LOW);   
    digitalWrite(GreenLEDPin, HIGH); 
    
    // 3. Update LCD
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Welcome");
    lcd.setCursor(0, 1);
    lcd.print("To My Hub");

    // Keep the barrier open for a moment (e.g., 5 seconds)
    delay(5000); 

    // After the delay, the system resets to the closed state
    // 1. Close the servo barrier
    myservo.write(ClosePosition);

    // 2. Change the light back to Red
    digitalWrite(GreenLEDPin, LOW); 
    digitalWrite(RedLEDPin, HIGH);   

    // 3. Update LCD back to initial message
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Low on charge?");
    lcd.setCursor(0, 1);
    lcd.print("please drive in");

    // Add a small delay after the barrier closes
    delay(1000);

  } else {
    // No car is detected, keep the "Gate Closed" status displayed
    // To prevent the screen from flashing, we rely on the message set at the end of the 'if' block.
  }
}