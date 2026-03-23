# Carregamento por indução — projeto ESP32

Firmware para ESP32 que lê **três sensores de corrente/tensão INA219** no barramento I2C, exibe tensão de barramento, corrente, potência instantânea e **energia acumulada** (integração numérica da potência no tempo) pela Serial.

## Objetivo

Monitorar até três canais de medição (por exemplo, diferentes pontos do circuito de carregamento por indução) com o mesmo microcontrolador, tolerando falha de um ou dois sensores desde que pelo menos um responda no I2C.

## Hardware

| Item | Descrição |
|------|-----------|
| Microcontrolador | ESP32 |
| Sensores | Até 3× [Adafruit INA219](https://www.adafruit.com/product/904) (ou compatíveis), cada um com endereço I2C configurável (jumpers A0/A1 no breakout) |
| Barramento I2C | **SDA = GPIO 15**, **SCL = GPIO 16** (definidos no código; ajuste se sua placa usar outros pinos) |

Cada INA219 deve ter um **endereço I2C único**. Os valores padrão no sketch são `0x40`, `0x41` e `0x44` — altere as macros `ADDR_INA_1`, `ADDR_INA_2` e `ADDR_INA_3` para coincidir com os jumpers das suas placas.

## Dependências (Arduino)

- Biblioteca **Wire** (I2C), incluída no core do ESP32.
- Biblioteca **Adafruit INA219** (normalmente instalada junto com dependências Adafruit Unified Sensor / BusIO pelo Gerenciador de Bibliotecas do Arduino IDE ou PlatformIO).

## Visão geral do arquivo `carregadorInducao.ino`

### Constantes e pinos

- `PIN_SDA` / `PIN_SCL`: fixam os pinos I2C do ESP32 usados na chamada `Wire.begin(PIN_SDA, PIN_SCL)`.
- `ADDR_INA_1`, `ADDR_INA_2`, `ADDR_INA_3`: endereços de 7 bits dos três módulos INA219.

### Objetos globais

- Três instâncias `Adafruit_INA219` (`ina1`, `ina2`, `ina3`), uma por endereço.
- `sensorOk[3]`: indica se cada sensor inicializou com sucesso (`begin()` retornou verdadeiro).
- `energia_mWh[3]`: acumulador de energia por canal, em **mWh** (miliwatt-hora).
- `tempoAnterior`: instante anterior (em ms, via `millis()`) usado para calcular o intervalo \(\Delta t\) entre leituras.

### Função `iniciarSensor`

Tenta `sensor.begin()` no INA219 informado. Em caso de sucesso, imprime na Serial o nome lógico e o endereço em hexadecimal. Em falha, imprime mensagem orientando a checar fiação e endereço. Retorna `true`/`false` para preencher `sensorOk[]`.

### `setup`

1. Inicia Serial a **115200** baud e aguarda um curto `delay` para estabilizar.
2. Inicia o I2C nos pinos definidos.
3. Chama `iniciarSensor` para cada uma das três placas.
4. Se **nenhum** sensor responder, imprime aviso e entra em loop infinito (o sketch para de avançar — evita leituras inválidas).
5. Inicializa `tempoAnterior` com `millis()`.

### Função estática `imprimirLeituras`

Parâmetros: índice do canal (0–2), referência ao objeto INA219, título para o cabeçalho, e `horas` (fração de hora desde a última iteração).

Para o sensor indicado:

- Lê **tensão de barramento** (V), **corrente** (mA) e **potência** (mW) via API do Adafruit_INA219.
- Atualiza a energia acumulada: `energia_mWh[indice] += potencia * horas` (potência em mW × tempo em horas ⇒ mWh).
- Imprime bloco formatado com rótulo, valores e energia acumulada com 4 casas decimais.

### `loop`

1. Calcula `horas = (tempoAtual - tempoAnterior) / 3600000.0f` (conversão de milissegundos para horas).
2. Atualiza `tempoAnterior` para o próximo ciclo.
3. Para cada canal com `sensorOk[i] == true`, chama `imprimirLeituras` com o índice e o objeto correspondente.
4. Imprime separador e aguarda **2 segundos** (`delay(2000)`).

Assim, a energia integrada reflete a potência média no intervalo entre duas leituras bem-sucedidas, multiplicada pela duração desse intervalo em horas.

## Saída Serial (exemplo de formato)

Para cada sensor ativo, algo na linha de:

- `------ INA219 #N ------`
- Tensão de barramento em V
- Corrente em mA
- Potência em mW
- Energia acumulada em mWh

Entre ciclos, um separador e linha em branco.

## Comportamento em falha parcial

Se um ou dois INA219 falharem na inicialização, o programa **continua** e só imprime leituras dos sensores que passaram em `iniciarSensor`. Se os três falharem, o código **trava** no `while (1)` após a mensagem de erro.

## Observações práticas

- **Precisão da energia**: a integração é por retângulos (potência atual × \(\Delta t\)); com `delay(2000)` o passo é grosso. Para cargas que mudam rápido, intervalos menores melhoram a estimativa (a custo de mais tráfego Serial e CPU).
- **Overflow de `millis()`**: em execuções muito longas, o wrap-around de `unsigned long` pode afetar uma única amostra de \(\Delta t\); em uso típico de laboratório isso costuma ser aceitável.
- **Calibração**: o INA219 pode precisar de calibração fina (shunt, ganho) conforme o breakout; o sketch usa os padrões da biblioteca Adafruit.

---

# Carregamento por Inducao Projeto ESP32

Arduino — [site oficial](https://www.arduino.cc/)

Documentação de referência — [Arduino Help Center](https://support.arduino.cc/hc/en-us)
