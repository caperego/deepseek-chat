# DeepSeek Chat — Instalação e uso

Aplicativo desktop (Qt 6 / QML) para a API DeepSeek, com login, conversas,
projetos, arquivos, comandos, streaming, ditado/voz opcionais e painel de custos.

A arquitetura é **cliente + backend**: o aplicativo conversa com a **API PHP**
(em `https://web.cap.im/deepseek`) e o backend guarda a API key do DeepSeek,
acessa o MariaDB localmente e registra histórico, tokens, custos e atividades.
O aplicativo nunca vê a chave nem as credenciais do banco — só um **token**.

Plataformas do app: **Linux**, **Windows** e **macOS**. (iOS não é suportado:
o sistema não permite executar o Whisper como processo externo.)

---

## 1. Backend (API PHP)

O projeto está em `deepseek-api/` e o deploy no ISPConfig está documentado em
`deepseek-api/DEPLOY-ISPConfig.md`.

Resumo:

- Site ISPConfig `web.cap.im` → arquivos em `/web/deepseek`.
- Banco `deepseek_chat` (usuário `deepseek_api`, local) e tabela `api_tokens`.
- `config.php` (no servidor) com as credenciais locais + a API key DeepSeek.
- URL final: `https://web.cap.im/deepseek` (teste: `GET /health`).

> **Atualização do agente de arquivos:** o projeto do banco precisa da coluna
> de permissões. No servidor, rode:
> `ALTER TABLE projects ADD COLUMN IF NOT EXISTS permission_mode VARCHAR(16) NOT NULL DEFAULT 'ask';`
> e republique os arquivos de `deepseek-api/` (o `/chat/stream` agora aceita
> `tools`/`tool_choice`). Depois recompile o app.

### 1.1 config.ini do aplicativo

Coloque o `config.ini` na **mesma pasta do executável** (`bin/config.ini`).
Há um modelo em `config.ini.example`. Principais campos:

```ini
[api]
base_url=https://web.cap.im/deepseek
token=                ; preenchido automaticamente após o login
user_id=0
username=
display_name=

[deepseek]
model=deepseek-flash

[ui]
theme=light            ; light | dark | system
font_size=14
enter_to_send=true
show_reasoning=true
permission_mode=ask    ; ask | auto | full (padrão para projetos novos)

[audio]
tts_enabled=false
tts_voice=
tts_rate=0
stt_enabled=false
stt_command=whisper-cli -m /caminho/para/modelo.bin -f {file} -l pt -nt

[pricing]
flash_input_hit=0.003
flash_input_miss=0.15
flash_output=0.60
pro_input_hit=0.022
pro_input_miss=0.66
pro_output=1.98
peak_multiplier=2.0
peak_enabled=true
```

> Tudo isso também pode ser alterado pela tela **Configurações → Conexão**.
> Após o primeiro login o `token` é salvo e o app entra automaticamente
> (há "Sair" na sidebar para revogar).

---

## 2. Linux (Arch/Manjaro)

### 2.1 Instalar dependências

```bash
sudo pacman -S --needed base-devel qt6-base qt6-declarative qt6-multimedia \
    qt6-speech qt6-svg mariadb-libs
```

Debian/Ubuntu equivalentes:

```bash
sudo apt install build-essential qt6-base-dev qt6-declarative-dev \
    qt6-multimedia-dev qt6-speech-dev libqt6svg6-dev libmariadb-dev qt6-tools-dev
```

### 2.2 Opcional (áudio)

```bash
# TTS (ler respostas em voz alta)
sudo pacman -S speech-dispatcher espeak-ng

# STT (ditado por voz) — baixe também um modelo ggml
sudo pacman -S whisper-cpp
```

### 2.3 Compilar e executar

```bash
./build.sh
./bin/deepseek-chat
```

---

## 3. Windows

### 3.1 Instalar o Qt 6

1. Baixe o **Qt Online Installer** (conta gratuita): https://www.qt.io/download-qt-installer
2. Marque os componentes:
   - **Qt 6.x → MSVC 2022 64-bit** (recomendado) **ou** **MinGW 64-bit**
   - **Qt Multimedia**
   - **Qt Speech** (TextToSpeech)
   - **Qt SVG**
   - **Qt Quick Controls / Declarative** (já vem no Qt 6)
3. Se usar MSVC, instale também o **Visual Studio Build Tools**
   (workload "Desktop development with C++").

### 3.2 Cliente MySQL/MariaDB (driver QMYSQL)

O plugin `qsqlmysql.dll` precisa de uma biblioteca cliente no PATH:

- **MariaDB Connector/C** (`libmariadb.dll`) ou **MySQL Connector/C** (`libmysql.dll`).
- Copie a DLL para a pasta do executável (`bin\`) ou adicione ao PATH.

Sem isso o app mostra: *"Driver QMYSQL não está disponível"*.

### 3.3 Compilar e distribuir

Abra o **"Qt 6.x for Desktop"** no menu Iniciar e execute:

```bat
build-windows.bat
windeployqt bin\deepseek-chat.exe
```

### 3.4 Áudio no Windows

- **TTS**: usa a voz nativa do Windows (SAPI). Nada a instalar.
- **STT**: baixe o `whisper-cli.exe` (projeto whisper.cpp) e um modelo
  (`ggml-small.bin`). Configure em Configurações → Áudio:

  `whisper-cli.exe -m C:\modelos\ggml-small.bin -f {file} -l pt -nt`

- **HTTPS**: o Qt no Windows usa Schannel — não precisa instalar OpenSSL.

---

## 4. macOS

### 4.1 Instalar dependências (Homebrew)

```bash
brew install qt mariadb-connector-c
# opcional:
brew install whisper-cpp     # STT
```

O Qt do Homebrew fica em `/opt/homebrew/opt/qt` (Apple Silicon) ou
`/usr/local/opt/qt` (Intel). Se o `qmake` não estiver no PATH, use:

```bash
export PATH="$(brew --prefix qt)/bin:$PATH"
```

### 4.2 Compilar e distribuir

```bash
./build.sh
macdeployqt bin/deepseek-chat.app -dmg   # gera um .dmg (opcional)
```

Observações de macOS:
- O `mac/Info.plist` já inclui **NSMicrophoneUsageDescription** (permissão do
  microfone para o ditado). Na primeira gravação o sistema pedirá autorização
  em *Ajustes do Sistema → Privacidade e Segurança → Microfone*.
- O driver QMYSQL precisa da biblioteca do `mariadb-connector-c`; o `macdeployqt`
  copia as dependências do Qt, mas **não** a do MySQL — copie/integre a
  `libmariadb.dylib` no bundle se for distribuir.
- **TTS**: usa a voz nativa do macOS (AVSpeechSynthesizer). Nada a instalar.
- **STT**: funciona por comando (whisper-cli do Homebrew). Ex.:

  `whisper-cli -m /Users/voce/modelos/ggml-small.bin -f {file} -l pt -nt`

---

## 5. Áudio (TTS/STT)

- **TTS (texto → fala)**: ao ativar "Ler as respostas em voz alta", o app fala
  cada resposta (limpa markdown/código antes). Voz e velocidade em Configurações.
- **STT (fala → texto)**: ative "Ditado por voz". O botão de microfone aparece
  no chat; clique para gravar e clique de novo para parar. O áudio é gravado com
  QtMultimedia (portátil) e transcrito pelo comando configurado usando `{file}`.
- Backends de voz: **Windows** = SAPI, **macOS** = AVSpeechSynthesizer,
  **Linux** = speech-dispatcher + espeak-ng.

---

## 6. Usando o aplicativo

1. Abra o app, aba **Criar conta** (ou **Entrar**).
2. **+ Nova conversa** para conversar; **+ Novo projeto** para agrupar conversas,
   arquivos, comandos e uma pasta de trabalho.
3. No projeto:
   - **Instruções**: viram *system prompt* de todas as conversas.
   - **Arquivos**: mostra os arquivos reais da pasta de trabalho (com **Anexar** e
     **Abrir**) e os itens salvos no projeto; a IA também acessa a pasta sozinha.
   - **Comandos**: prompts reutilizáveis (aparecem no botão `/` do chat).
   - **Configurações**: nome, pasta de trabalho, modelo/esforço padrão e
     **Permissões da IA**.
4. **Permissões da IA** (por projeto; padrão global em Configurações → Conexão):
   - **Solicitar aprovação** — pede permissão para executar comandos,
     gravar/baixar arquivos e acessar a internet.
   - **Aprovar por mim** — leitura/gravação automáticas dentro da pasta do
     projeto; pede permissão para comandos, fora dela e na internet.
   - **Acesso completo** — sem pedir permissão: executa comandos, qualquer
     arquivo do computador e qualquer URL.
5. Com a pasta definida, a IA usa ferramentas para **listar** (`list_files`),
   **ler** (`read_file`), **criar/editar** (`write_file`) arquivos, **baixar
   arquivos/instaladores** (`download_file`), **executar programas e comandos**
   (`run_command` — instalações, builds, scripts) e **baixar páginas**
   (`fetch_url`). Cada passo aparece no chat como um cartão; o diálogo de
   aprovação tem a opção "Não perguntar novamente nesta conversa".
6. No chat: **Anexar** (texto/imagem), microfone (se STT ligado),
   seletor de modelo/esforço e o botão de **custos** (Custo, Uso %, Tokens).
7. Quando o modelo sugerir um arquivo (bloco ```file:caminho```), aparece o cartão
   **Salvar no projeto** — as gravações também respeitam o modo de permissão.

---

## 7. Problemas comuns

| Sintoma | Solução |
|---|---|
| "Falha ao conectar na API" | Confirme `GET https://web.cap.im/deepseek/health` no navegador/curl |
| Login "Token inválido ou expirado" | Faça login novamente (o token dura 30 dias) |
| Sem voz no Linux | Instale `speech-dispatcher espeak-ng` |
| Microfone não grava no macOS | Autorize em Privacidade e Segurança → Microfone (o app usa o Info.plist) |
| Transcrição vazia | Verifique o caminho do `whisper-cli` e do modelo em Configurações → Áudio |
| IA "não vê" a pasta do projeto | Confirme a pasta em Projeto → Configurações e clique em **Atualizar** na aba Arquivos; a IA só age se o modelo da conversa suportar ferramentas |
| Erro "unknown field permission_mode" no backend | Falta rodar o `ALTER TABLE projects ... permission_mode` no banco do servidor |
| Backend sem conexão ao banco | Confira `config.php` no servidor (usuário `deepseek_api`) |

---

## 8. Estrutura do projeto

```
deepseek-chat.pro      projeto qmake (Qt 6) — aplicativo
build.sh               build no Linux/macOS
build-windows.bat      build no Windows
config.ini.example     modelo de configuração
mac/Info.plist         metadados do bundle macOS (permissão de microfone)
resources.qrc          recursos (logo oficial, ícones SVG, QML)
qml/                   interface QML
src/                   núcleo C++ (config, API HTTP, streaming, áudio, controlador)
../deepseek-api/       backend PHP (index.php, src/, sql/, config.php)
../deepseek-api/DEPLOY-ISPConfig.md
```
