#include "FeedbackController.h"
#include <stdint.h>
//#include <Arduino.h>

FeedbackController::FeedbackController(bool inverselyProportional, bool pwmOutput, int controlPeriod)
: inputs(nullptr), outputDevices(nullptr), buzzer(nullptr), latestSensorData(nullptr), inputCount(0), outputCount(0),
	upperBound(), lowerBound(), setpoint(0.0), hysteresis(0.0),
  alarmUpperThreshold(), alarmLowerThreshold(), gracePeriodMillis(0), activeAlarm(false), inAlarmStateSince(0),
	currentControlState(false), inverselyProportional(inverselyProportional), pwmOutput(pwmOutput), controlPeriod(controlPeriod) {
    
}

void FeedbackController::setUpperBound(double value){
	this->upperBound = Bound(value);
	poll();
}

void FeedbackController::setLowerBound(double value){
	this->lowerBound = Bound(value);
	poll();
}
	
void FeedbackController::setSetpoint(double setpoint){
	this->setpoint = setpoint;
	poll();
}

void FeedbackController::setHysteresis(double hysteresis) {
  this->hysteresis = hysteresis;
  poll();
}

void FeedbackController::setAlarm(double lowLimit, double highLimit, unsigned long gracePeriodMillis) {
  alarmUpperThreshold = Bound(highLimit);
  alarmLowerThreshold = Bound(lowLimit);
  this->gracePeriodMillis = gracePeriodMillis;
}

void FeedbackController::removeAlarm() {
  alarmUpperThreshold = Bound();
  alarmLowerThreshold = Bound();
  this->gracePeriodMillis = 0;
}

void FeedbackController::testInput() {  
  Serial.println("testing");
  while(true){
    for (int i = 0; i < inputCount; i++) {
      latestSensorData[i] = inputs[i].get();
    Serial.print(latestSensorData[i]);
    Serial.print(" ");
    }
    Serial.println();
    delay(2000);
  }
}

void FeedbackController::testOutput() {
  while(true){
    for (int i = 0; i < outputCount; i++) {
      outputDevices[i]->controlOutput(true);
    }
    delay(2000);
    for (int i = 0; i < outputCount; i++) {
      outputDevices[i]->controlOutput(false);
    }
    delay(2000);
  }
}

void FeedbackController::defineInputs(Sensor** sensorArray, uint8_t sensorArrayCount, char parameterCode){
  defineInputs(sensorArray, nullptr, sensorArrayCount, parameterCode);
}

void FeedbackController::defineInputs(Sensor** sensorArray, double* calibrationOffsetArray, uint8_t sensorArrayCount, char parameterCode){
  if (inputs) {
    delete inputs;
  }
  
  if (latestSensorData) {
   delete latestSensorData;
  }

  inputs = new SenseInput [sensorArrayCount];
	inputCount = sensorArrayCount;
  latestSensorData = new double [sensorArrayCount];
  
	for (int i = 0; i < inputCount; i++) {
    if (calibrationOffsetArray != nullptr) {
      inputs[i] = SenseInput(sensorArray[i], calibrationOffsetArray[i], parameterCode);
    }
    else {
      inputs[i] = SenseInput(sensorArray[i], parameterCode);
    }
    latestSensorData[i] = 0.0;
	}
}

void FeedbackController::defineOutputs(DigitalOutputDevice** deviceArray, uint8_t deviceArrayCount){
  if (outputDevices) {
    delete outputDevices; 
  }
  outputDevices = new DigitalOutputDevice* [deviceArrayCount];
  outputCount = deviceArrayCount;
  
  for (int i = 0; i < outputCount; i++) {
    outputDevices[i] = deviceArray[i];
    outputDevices[i]->controlOutput(currentControlState);
  }
}

void FeedbackController::defineBuzzer(Buzzer* _buzzer) {
  buzzer = _buzzer;
}

bool FeedbackController::existingAlarmState() {
  return activeAlarm;
}

void FeedbackController::updateAlarmState(bool valueOutOfBounds) {
	if (valueOutOfBounds) {
    bool inRollover = (millis() < inAlarmStateSince);
    if (inRollover) {
      inAlarmStateSince = millis();
    }
    
    if (existingAlarmState()) {
      if ((millis() - inAlarmStateSince) > gracePeriodMillis ||
          inRollover) {
            if (millis() / 1000UL % 2UL) { //results in a 50% duty-cycle trill
              buzzer->beep(); 
            }
          }
    }
    else {
      Serial.print("Registering Alarm\n");
      activeAlarm = true;
      inAlarmStateSince = millis();
    }
  }
  else {
    activeAlarm = false;
    inAlarmStateSince = 0;
  }
}

void FeedbackController::poll(){
  bool valueOutOfBounds = false;
  for (int i = 0; i < inputCount; i++) {
    latestSensorData[i] = inputs[i].get();
    if ((alarmLowerThreshold.isSet && alarmLowerThreshold > latestSensorData[i])  || 
        (alarmUpperThreshold.isSet && alarmUpperThreshold < latestSensorData[i])) {
      valueOutOfBounds = true;                
    }
    
    Serial.print(inputs[i].getName());
    Serial.print(": ");
    Serial.print(inputs[i].getParameterCode());
    Serial.print(": ");
    Serial.print(latestSensorData[i]);
    Serial.print("    ");
    Serial.print(valueOutOfBounds ? "Tentative alarm state\n" : "No alarm\n");
  }
	
  controlOutputs();
  updateAlarmState(valueOutOfBounds);
}

void FeedbackController::controlOutputs() {
  bool controlState = currentControlState;

    //prepare to drive output based on average reading vs setpoint
  if (getAvgValue() < (this->setpoint - this->hysteresis)) {
    controlState = this->inverselyProportional ? false : true;
  }
  else if (getAvgValue() > (this->setpoint + this->hysteresis)) {
    controlState = this->inverselyProportional ? true : false;
  }

    //apply upper/lower bounds, giving priority to upper bound
  if (this->lowerBound.isSet && getMinValue() < this->lowerBound.value) {
    controlState = this->inverselyProportional ? false : true;
  }
  else if (this->upperBound.isSet && getMaxValue() > this->upperBound.value) {
    controlState = this->inverselyProportional ? true : false;
  }
  
  if (controlState != currentControlState) {
    for (int i = 0; i < outputCount; i++) {
      Serial.print(outputDevices[i]->getName());
      Serial.print(controlState ? ": ON " : ": OFF ");   
    }
  }
  
  this->currentControlState = controlState;
  for (int i = 0; i < this->outputCount; i++) {
    outputDevices[i]->controlOutput(controlState);
  }
}

double FeedbackController::getMaxValue(){
  double maximum = 0.0;

	for (int i = 0; i < inputCount; i++) {
    if (i == 0) {
      maximum = this->latestSensorData[0];
    }
    else if (i == 1) {
      maximum = max(this->latestSensorData[0], this->latestSensorData[1]);
    }
    else {
      maximum = max(this->latestSensorData[i], maximum);
    }
  }
  
	return maximum;
}

double FeedbackController::getAvgValue(){
	double acc = 0.0;
  double average = 0.0;

  for (int i = 0; i < inputCount; i++) {
    acc += this->latestSensorData[i];
  }
  
  average = acc / double(inputCount);
  return average;
}

double FeedbackController::getMinValue(){
	double minimum = 0.0;

  for (int i = 0; i < inputCount; i++) {
    if (i == 0) {
      minimum = this->latestSensorData[0];
    }
    else if (i == 1) {
      minimum = min(this->latestSensorData[0], this->latestSensorData[1]);
    }
    else {
      minimum = min(this->latestSensorData[i], minimum);
    }
  }
  
  return minimum;
}
