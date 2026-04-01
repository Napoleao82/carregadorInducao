#include <Wire.h>
#include <Adafruit_INA219.h>

// I2C: evitar GPIO 15 (strap / JTAG no ESP32). Par usual em DevKit: 21 = SDA, 22 = SCL.
#define PIN_SDA 21
#define PIN_SCL 22

// Cada modulo INA219 precisa de um endereco I2C unico (A0/A1 no breakout).
#define ADDR_INA_1 0x40
#define ADDR_INA_2 0x41
#define ADDR_INA_3 0x44

#define SAMPLE_MS 100
#define REPORT_MS 10000
#define N_SAMPLES (REPORT_MS / SAMPLE_MS)

Adafruit_INA219 ina1(ADDR_INA_1);
Adafruit_INA219 ina2(ADDR_INA_2);
Adafruit_INA219 ina3(ADDR_INA_3);

bool sensorOk[3] = {false, false, false};

float energia_mWh[3] = {0, 0, 0};

// Buffers por sensor: uma amostra a cada SAMPLE_MS ate completar N_SAMPLES.
static float bufVinMenos[3][N_SAMPLES];  // registrador "bus" = tensao no pino VIN-
static float bufQuedaShunt_V[3][N_SAMPLES];
static float bufVload[3][N_SAMPLES];     // VIN+ estimado = VIN- + queda no shunt
static float bufCorrente[3][N_SAMPLES];
static float bufPotencia[3][N_SAMPLES];
static float bufEnergia[3][N_SAMPLES];

static uint16_t idxAmostra = 0;
static unsigned long proximaAmostraMs = 0;

bool iniciarSensor(Adafruit_INA219& sensor, const char* nome, uint8_t addr) {
  if (sensor.begin()) {
    sensor.setCalibration_32V_1A();
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

/** Media sem uma amostra minima e uma maxima (indices distintos). */
static float mediaSemExtremos(const float* v, int n) {
  if (n < 3) {
    return 0.0f;
  }
  int imin = 0;
  int imax = 0;
  for (int i = 1; i < n; i++) {
    if (v[i] < v[imin]) {
      imin = i;
    }
    if (v[i] > v[imax]) {
      imax = i;
    }
  }
  if (imin == imax) {
    float s = 0;
    for (int i = 0; i < n; i++) {
      s += v[i];
    }
    return s / (float)n;
  }
  float soma = 0;
  for (int i = 0; i < n; i++) {
    if (i != imin && i != imax) {
      soma += v[i];
    }
  }
  return soma / (float)(n - 2);
}

static void amostrarSensor(uint8_t indice, Adafruit_INA219& ina) {
  float shunt_mV = ina.getShuntVoltage_mV();
  float bus_V = ina.getBusVoltage_V();
  float vCarga = bus_V + (shunt_mV / 1000.0f);
  float corrente = ina.getCurrent_mA();
  float potencia = ina.getPower_mW();

  const float dt_h = SAMPLE_MS / 3600000.0f;
  energia_mWh[indice] += potencia * dt_h;

  bufVinMenos[indice][idxAmostra] = bus_V;
  bufQuedaShunt_V[indice][idxAmostra] = shunt_mV / 1000.0f;
  bufVload[indice][idxAmostra] = vCarga;
  bufCorrente[indice][idxAmostra] = corrente;
  bufPotencia[indice][idxAmostra] = potencia;
  bufEnergia[indice][idxAmostra] = energia_mWh[indice];
}

static void imprimirMedias10s(const char* titulo, uint8_t indice) {
  if (!sensorOk[indice]) {
    return;
  }
  Serial.print("====== ");
  Serial.print(titulo);
  Serial.println(" (media 10 s, sem 1 min e 1 max) ======");

  float mVm = mediaSemExtremos(bufVinMenos[indice], N_SAMPLES);
  float mQs = mediaSemExtremos(bufQuedaShunt_V[indice], N_SAMPLES);
  float mVc = mediaSemExtremos(bufVload[indice], N_SAMPLES);

  Serial.print("VIN- vs GND modulo (reg. barramento) (V): ");
  Serial.println(mVm, 4);
  Serial.print("Queda no shunt V+ a V- (V): ");
  Serial.println(mQs, 6);
  Serial.print("Tensao carga estimada VIN+ = VIN- + queda shunt (V): ");
  Serial.println(mVc, 4);
  Serial.println("(Confira com multimetro: VIN+ e VIN- vs GND do breakout, mesmo referencial.)");

  Serial.print("Corrente (mA): ");
  Serial.println(mediaSemExtremos(bufCorrente[indice], N_SAMPLES), 3);

  Serial.print("Potencia (mW): ");
  Serial.println(mediaSemExtremos(bufPotencia[indice], N_SAMPLES), 3);

  Serial.print("Energia acumulada (mWh) [media das amostras na janela]: ");
  Serial.println(mediaSemExtremos(bufEnergia[indice], N_SAMPLES), 4);

  Serial.println();
}

/** Energia acumulada desde o arranque: fracao do canal 0 face ao canal 2 (referencia = primario / fonte). */
static void imprimirAproveitamentoEnergia() {
  constexpr uint8_t kIdxCarga = 0;
  constexpr uint8_t kIdxFonte = 2;
  if (!sensorOk[kIdxCarga] || !sensorOk[kIdxFonte]) {
    return;
  }
  const float eCarga = energia_mWh[kIdxCarga];
  const float eFonte = energia_mWh[kIdxFonte];
  Serial.println("====== Rendimento energetico (acumulado desde o arranque) ======");
  Serial.print("Energia acumulada INA219 0 (carga / secundario) (mWh): ");
  Serial.println(eCarga, 4);
  Serial.print("Energia acumulada INA219 2 (fonte / primario) (mWh): ");
  Serial.println(eFonte, 4);
  if (eFonte > 1e-9f) {
    const float pct = 100.0f * (eCarga / eFonte);
    Serial.print("Aproveitamento (E0 / E2): ");
    Serial.print(pct, 2);
    Serial.println(" %");
  } else {
    Serial.println("Aproveitamento: indefinido (energia na fonte ~0).");
  }
  Serial.println();
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

  Serial.print("Amostra a cada ");
  Serial.print(SAMPLE_MS);
  Serial.print(" ms; relatorio a cada ");
  Serial.print(REPORT_MS / 1000);
  Serial.println(" s.\n");

  proximaAmostraMs = millis();
}

void loop() {
  unsigned long agora = millis();
  if (agora - proximaAmostraMs < SAMPLE_MS) {
    return;
  }
  proximaAmostraMs += SAMPLE_MS;

  if (sensorOk[0]) {
    amostrarSensor(0, ina1);
  }
  if (sensorOk[1]) {
    amostrarSensor(1, ina2);
  }
  if (sensorOk[2]) {
    amostrarSensor(2, ina3);
  }

  idxAmostra++;
  if (idxAmostra >= N_SAMPLES) {
    Serial.println("--------------------");
    imprimirMedias10s("INA219 BMS -> Bateria", 0);
    // imprimirMedias10s("INA219 Indutor Primario", 1);
    imprimirMedias10s("INA219 Primario", 2);
    imprimirAproveitamentoEnergia();
    Serial.println("--------------------\n");
    idxAmostra = 0;
  }
}
