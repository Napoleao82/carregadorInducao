# Carregamento por indução — monitorização com ESP32 e INA219

Trabalho académico do curso de **Engenharia Elétrica** da **Pontifícia Universidade Católica de Goiás (PUC Goiás)**: sistema para acompanhar tensão de carga, corrente, potência e energia em vários pontos de um circuito de carregamento indutivo, com firmware no **ESP32** e painel **web** opcional.

---

## Equipa do projeto

### Alunos

- Napoleao Menezes  
- Patrícia Nakamura Franca  
- Weisner Resende  
- Elves Alves Barbosa  
- Bruno Viana Pinheiro  
- Gabriela Moreira Fernandes  

### Docentes

- Felipe Correa Veloso dos Santos  
- Cassio Hideki Fujisawa  

---

## Visão geral

O microcontrolador lê **três módulos INA219** no barramento **I2C** (endereços **0x40**, **0x41** e **0x44**), associados a canais como indutor primário, secundário/BMS e BMS–bateria. Os valores instantâneos são amostrados periodicamente; calculam-se **médias** e **energia acumulada** por integração da potência no tempo.

O repositório inclui:

| Componente | Descrição |
|------------|-----------|
| `ServerEsp32/ServerEsp32.ino` | Sketch Arduino: Wi‑Fi, servidor HTTP na porta 80, API JSON e amostragem dos INA219 |
| `web/index.html` | Página única (HTML/CSS/JS) que consulta a API no browser e mostra cartões por canal, processo de teste e JSON bruto |

---

## Hardware

| Item | Notas |
|------|--------|
| **MCU** | ESP32 (modo estação Wi‑Fi) |
| **Sensores** | 3× INA219 (ex.: Adafruit ou compatível), shunt típico 0,1 Ω |
| **I2C** | No firmware atual: **SDA = GPIO 21**, **SCL = GPIO 22** (evitar GPIO de strap problemáticos, como o 15) |
| **Endereços** | Um endereço 7 bits por placa; o sketch usa `0x41`, `0x40`, `0x44` — ajustar jumpers A0/A1 se necessário |

**Tensão de carga (estimativa VIN+):** `tensão_no_barramento (VIN−) + getShuntVoltage_mV() / 1000`. O multímetro deve medir os mesmos nós (**VIN+**, **VIN−**, **GND do módulo**) para comparar com o indicado pelo chip.

---

## Firmware (`ServerEsp32.ino`)

### Dependências

- Core **ESP32** (Arduino IDE ou PlatformIO)  
- Bibliotecas: **WiFi**, **WebServer**, **Wire**, **Adafruit INA219** (e dependências Adafruit BusIO, etc.)

### Configuração (Wi‑Fi sem expor senha no Git)

O sketch inclui `secrets.h`, que **não deve ser commitado** (está no `.gitignore`).

1. Na pasta `ServerEsp32/`, copie o modelo e edite com a sua rede: `cp secrets.h.example secrets.h`
2. Abra `secrets.h` e altere `WIFI_SSID` e `WIFI_PASS` (macros `#define`).
3. Compilar e gravar o sketch; `secrets.h` não entra no Git (`.gitignore`).

**Nota:** o Arduino IDE **não** usa ficheiros `.env` como o Node. Para injetar credenciais por **variáveis de ambiente** no compile, quem usa **PlatformIO** pode exportar `WIFI_SSID` / `WIFI_PASS` e usar `build_flags` com `-D` (ver comentário em `secrets.h.example`).

Se o repositório já incluiu a senha no histórico do Git, convém **alterar a palavra-passe da rede Wi‑Fi**.

4. Abrir o **Monitor série** (115200 baud) para ver o **IP** e o diagnóstico (INA219, I2C, NTP).

### API REST (JSON)

Base: `http://<IP_DO_ESP32>`

| Método | Caminho | Função |
|--------|---------|--------|
| `GET` | `/api/status` | Estado do dispositivo, Wi‑Fi, cada INA219 (instantâneo, médias, energia), processo (início/fim, NTP) |
| `POST` | `/api/reset` | Zera médias e energia acumulada nos canais |
| `POST` | `/api/process/start` | Marca início do “processo” de teste (timestamp se NTP OK) |
| `POST` | `/api/process/stop` | Marca fim do processo |

O servidor envia cabeçalhos **CORS** para permitir chamadas a partir de `file://` ou de outro host (ex.: `localhost`) na mesma rede.

### Calibração INA219

O sketch usa `setCalibration_32V_2A()` nos canais que inicializam com sucesso. Para correntes mais baixas com melhor resolução, avaliar `setCalibration_32V_1A()` na biblioteca Adafruit (e respeitar limites de tensão no shunt).

---

## Interface web (`web/index.html`)

1. Abrir o ficheiro no navegador **ou** servir a pasta `web/` com um servidor HTTP simples.  
2. No campo **URL base do ESP32**, indicar `http://<IP_DO_ESP32>` (sem barra final).  
3. **Guardar URL** grava no `localStorage` do browser.  
4. A página faz *polling* a cada **5 s** para `GET /api/status` e atualiza os cartões.  
5. Botões: iniciar/terminar processo, reiniciar ciclo (POST reset).

A página inclui identificação da **PUC Goiás**, logótipo institucional e a lista de **alunos** e **docentes**.

---

## Estrutura do repositório

```
carregadorInducao/
├── README.md                 ← este ficheiro
├── ServerEsp32/
│   └── ServerEsp32.ino       ← firmware ESP32 + API
└── web/
    └── index.html            ← painel de monitorização
```

Ficheiros de *log* (`*.log`) podem existir localmente para ensaios; não são obrigatórios para compilar o firmware.

---

## Referências

- [Arduino](https://www.arduino.cc/) — documentação geral  
- [Arduino Help Center](https://support.arduino.cc/hc/en-us)  
- [PUC Goiás](https://www.pucgoias.edu.br/)  
- [INA219 — Adafruit](https://www.adafruit.com/product/904)  

---

*Última atualização do README alinhada ao firmware em `ServerEsp32` e à página em `web/index.html`.*
