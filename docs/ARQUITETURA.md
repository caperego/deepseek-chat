# Arquitetura

## Visão geral

```
┌────────────────────────────────────────────┐
│ deepseek-chat  (Qt 6 / QML / C++17)        │
│  ├─ ApiClient      REST síncrono (Bearer)  │
│  ├─ StreamClient   SSE do chat             │
│  ├─ AppController  view-model + agente     │
│  │    └─ AgentRun  uma execução por envio  │
│  ├─ MessageListModel  modelo de mensagens  │
│  └─ QML (Sidebar, ChatView, ProjectView,   │
│          SettingsDialog, terminal, painel) │
└──────────────┬─────────────────────────────┘
               │ HTTPS
┌──────────────▼─────────────────────────────┐
│ deepseek-api  (PHP 8, sem framework)       │
│  index.php → src/routes.php (dispatcher)   │
│  src/auth.php       login/tokens (bcrypt)  │
│  src/deepseek.php   proxy + SSE            │
│  src/db.php         PDO/MySQL              │
└──────────────┬─────────────────────────────┘
               │
      ┌────────┴─────────┐
      ▼                  ▼
  MariaDB           API DeepSeek (e outros
  deepseek_chat     provedores OpenAI-compat.)
```

## Fluxo de login

1. `POST /auth/login` → backend valida (bcrypt; migra PBKDF2 legado) e devolve um
   token de 32 bytes. Só o `sha256` é gravado em `api_tokens` (com validade).
2. O app guarda o token no `config.ini` com DPAPI no Windows; no Linux/macOS,
   usa base64 e permissão 600 no arquivo. Base64 não fornece criptografia.
   As chamadas autenticadas usam `Authorization: Bearer …`.
3. `tryAutoLogin()` valida o token em `GET /me` ao abrir o app.

## Fluxo de conversa (agente)

1. `AppController::send()` cria um **AgentRun** (uma execução independente) e monta o
   contexto: instruções globais + instruções do projeto + descrição do ambiente
   (pasta de trabalho, modo de permissão, ferramentas).
2. Envia para `POST /chat/stream` que repassa para a API DeepSeek e devolve **SSE**;
   o backend persiste a mensagem do usuário e, no fim, a resposta + tokens.
3. O `StreamClient` acumula texto, raciocínio e `tool_calls`.
4. Se o modelo pedir ferramentas, o `AppController`:
   - verifica permissões (allowlist do projeto → modos ask/auto/full → blocklist);
   - pede aprovação quando necessário (fila de aprovações, uma janela por vez);
   - executa a ferramenta (arquivos/comando/HTTP/MCP) e devolve o resultado como
     mensagem `tool` no próximo round;
   - repete até acabar (máx. 12 rounds) e finaliza persistindo a resposta.
5. Execuções são **paralelas por conversa**: cada uma tem seu stream, e o modelo de
   mensagens só recebe atualizações quando a conversa está visível.

## Permissões

| Modo | Leitura dentro da pasta | Gravar/baixar dentro | Ler fora | Gravar fora | Comandos/MCP | Internet |
|---|---|---|---|---|---|---|
| `ask` | automático | pede aprovação | pede | pede | pede | pede |
| `auto` | automático | automático | pede | pede | pede | pede |
| `full` | automático | automático | automático | automático | automático | automático |

- "Não perguntar nesta conversa" → sessão `autoApprove`.
- "Sempre permitir este tipo de ação neste projeto" → allowlist no `config.ini`
  (`[permissions] project_<id>=tool1,tool2`).
- Comandos destrutivos conhecidos pedem confirmação mesmo em `full` (blocklist).

## MCP (Model Context Protocol)

- Servidores configurados em `mcp.json` (`command`, `args`, `env`, `cwd`).
- Handshake JSON-RPC por stdio: `initialize` → `notifications/initialized` →
  `tools/list`, `resources/list`, `prompts/list`.
- Ferramentas viram `mcp__servidor__ferramenta`; resources/prompts viram
  `mcp__servidor__read_resource` e `mcp__servidor__get_prompt`.
- Tudo passa pela mesma fila de aprovação do agente.

## Banco de dados (MariaDB)

`users`, `projects` (com `permission_mode`), `project_memories`, `project_items`,
`commands`, `conversations` (com origem da ramificação), `messages` (conteúdo,
raciocínio, tokens, `attachments_json` e `sources_json`) e
`activities`. `api_tokens` guarda os hashes dos tokens do backend.
Fonte: `deepseek-chat/sql/setup.sql` e `deepseek-api/sql/api_tokens.sql`.

## Atualização automática (Windows)

1. `GET /version.txt` (backend) é comparado com a versão embutida no app.
2. Se diferente, baixa `/download/deepseek-chat-windows-x64.zip`.
3. Extrai em `%TEMP%\deepseek-chat-update`, gera um `.ps1` que:
   aguarda o processo encerrar → copia os arquivos sobre a pasta do executável →
   reabre o app → limpa temporários.
4. `config.ini`, `mcp.json` e demais arquivos fora do pacote são preservados.
