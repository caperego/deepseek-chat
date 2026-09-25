# Ferramentas do agente e permissões

## Ferramentas locais

| Ferramenta | Parâmetros | O que faz |
|---|---|---|
| `list_files` | `path?`, `recursive?` | lista pastas/arquivos (até 500 itens) |
| `read_file` | `path`, `start_line?`, `end_line?` | lê texto (até 512 KB; recusa binário) |
| `write_file` | `path`, `content` | cria/sobrescreve (backup automático, até 2 MB) |
| `find_files` | `name_glob`, `path?` | procura arquivos por nome (glob) |
| `grep_files` | `pattern` (regex), `path?`, `ignore_case?`, `max_results?` | procura no conteúdo |
| `restore_file` | `path` | restaura a versão anterior (backup) |
| `run_command` | `command`, `cwd?`, `timeout_seconds?` | executa no `cmd.exe` (Windows) ou `/bin/sh` |
| `download_file` | `url`, `path` | baixa arquivo (até 256 MB) |
| `fetch_url` | `url` | baixa e converte página em texto |
| `browse_url` | `url` | página via navegador **headless** (Edge/Chrome; fallback `fetch_url`) |
| `mcp__servidor__*` | conforme o servidor MCP | ferramentas, resources e prompts MCP |

Limites: saída de comando 100 KB; resultados de ferramenta exibidos até 20 KB no cartão.
Gravações guardam até 5 backups por arquivo em `%APPDATA%/DeepSeekChat/backups`.

## Como o agente é instruído

No início de cada mensagem o app envia um bloco de contexto com:

- a **pasta de trabalho** do projeto (caminhos relativos são resolvidos a partir dela);
- o **modo de permissão** atual;
- orientações: usar ferramentas para arquivos/comandos; exibir imagens geradas com
  `![descrição](caminho)`; ler links citados com `fetch_url`/`browse_url`.

## Modos de permissão (por projeto; padrão global em Configurações)

| Ação | Solicitar aprovação | Aprovar por mim | Acesso completo |
|---|---|---|---|
| listar/ler dentro da pasta | automático | automático | automático |
| gravar/baixar dentro da pasta | **pede** | automático | automático |
| ler/listar fora da pasta | **pede** | **pede** | automático |
| gravar fora da pasta | **pede** | **pede** | automático |
| executar comando | **pede** | **pede** | automático* |
| internet (fetch/browse) | **pede** | **pede** | automático |
| ferramentas MCP | **pede** | **pede** | automático* |

\* comandos que casam com a blocklist (ex.: `rm -rf /`, `format c:`, `del /s`,
`mkfs`, `dd if=`, `diskpart`, `reg delete`, `shutdown`, fork bombs) **pedem confirmação
mesmo em Acesso completo**, com o título "Comando potencialmente PERIGOSO".

## Aprovações

- Caixa única com detalhes: comando + pasta, URL + destino, caminho do arquivo,
  e **diff antes/depois** para gravações.
- Opções: **Permitir** / **Negar**, "não perguntar novamente nesta conversa" e
  "sempre permitir este tipo de ação neste projeto".
- Várias execuções em paralelo entram numa fila; aparece uma aprovação por vez.

## Terminal embutido (manual)

Botão **Terminal** na barra lateral: shell interativo (ConPTY no Windows, `openpty`
no Linux), saída ao vivo, enviar comandos e parar. É ação do usuário — não passa
pela fila de aprovação.
