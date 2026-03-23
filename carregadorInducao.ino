#include <Wire.h>
#include <Adafruit_INA219.h>

// Barramento I2C no ESP32: SDA = GPIO 15, SCL = GPIO 16
#define PIN_SDA 15
#define PIN_SCL 16

// Cada modulo INA219 precisa de um endereco I2C unico (A0/A1 no breakout).
// Ajuste estes valores conforme os jumpers das suas 3 placas.
#define ADDR_INA_1 0x40
#define ADDR_INA_2 0x41
#define ADDR_INA_3 0x44

Adafruit_INA219 ina1(ADDR_INA_1);
Adafruit_INA219 ina2(ADDR_INA_2);
Adafruit_INA219 ina3(ADDR_INA_3);

bool sensorOk[3] = {false, false, false};

float energia_mWh[3] = {0, 0, 0};
unsigned long tempoAnterior = 0;

bool iniciarSensor(Adafruit_INA219& sensor, const char* nome, uint8_t addr) {
  if (sensor.begin()) {
    Serial.print(nome);
    Serial.print(" OK (0x");
    if (addr < 16) Serial.print('0');
    Serial.print(addr, HEX);
    Serial.println(')');
    return true;
  }
  Serial.print(nome);
  Serial.print(" FALHOU - verifique fiacao e endereco 0x");
  if (addr < 16) Serial.print('0');
  Serial.println(addr, HEX);
  return false;
}

void setup() {
  Serial.begin(115200);
  delay(200);

  Wire.begin(PIN_SDA, PIN_SCL);

  Serial.println("Inicializando INA219...");
  sensorOk[0] = iniciarSensor(ina1, "Placa 1", ADDR_INA_1);
  sensorOk[1] = iniciarSensor(ina2, "Placa 2", ADDR_INA_2);
  sensorOk[2] = iniciarSensor(ina3, "Placa 3", ADDR_INA_3);

  if (!sensorOk[0] && !sensorOk[1] && !sensorOk[2]) {
    Serial.println("Nenhum sensor respondeu. Parado.");
    while (1) {
      delay(10);
    }
  }

  tempoAnterior = millis();
}

static void imprimirLeituras(int indice, Adafruit_INA219& ina, const char* titulo,
                             float horas) {
  float tensao = ina.getBusVoltage_V();
  float corrente = ina.getCurrent_mA();
  float potencia = ina.getPower_mW();

  energia_mWh[indice] += potencia * horas;

  Serial.print("------ ");
  Serial.print(titulo);
  Serial.println(" ------");

  Serial.print("Tensao barramento: ");
  Serial.print(tensao, 3);
  Serial.println(" V");

  Serial.print("Corrente: ");
  Serial.print(corrente, 2);
  Serial.println(" mA");

  Serial.print("Potencia: ");
  Serial.print(potencia, 2);
  Serial.println(" mW");

  Serial.print("Energia acumulada: ");
  Serial.print(energia_mWh[indice], 4);
  Serial.println(" mWh");

  Serial.println();
}

void loop() {
  unsigned long tempoAtual = millis();
  float horas = (tempoAtual - tempoAnterior) / 3600000.0f;
  tempoAnterior = tempoAtual;

  // if (sensorOk[0]) {
  //   imprimirLeituras(0, ina1, "INA219 #1", horas);
  // }
  if (sensorOk[1]) {
    imprimirLeituras(1, ina2, "INA219 Indutor Primario", horas);
  }
  if (sensorOk[2]) {
    imprimirLeituras(2, ina3, "INA219 Bateria", horas);
  }

  Serial.println("--------------------\n");

  delay(2000);
}
