# Ferramentas do agente e permissões

## Ferramentas locais

| Ferramenta | Parâmetros | O que faz |
|---|---|---|
| `list_files` | `path?`, `recursive?` | lista pastas/arquivos (até 500 itens) |
| `read_file` | `path`, `start_line?`, `end_line?` | lê texto (até 512 KB; recusa binário) |
| `write_file` | `path`, `content` | cria/sobrescreve com gravação atômica e backup automático (até 2 MB) |
| `find_files` | `name_glob`, `path?` | procura arquivos por nome (glob) |
| `grep_files` | `pattern` (regex), `path?`, `ignore_case?`, `max_results?` | procura no conteúdo |
| `restore_file` | `path` | restaura a versão anterior (backup) |
| `run_command` | `command`, `cwd?`, `timeout_seconds?` | executa no `cmd.exe` (Windows) ou `/bin/sh` |
| `download_file` | `url`, `path` | baixa arquivo (até 256 MB) |
| `fetch_url` | `url` | baixa e converte página em texto |
| `search_web` | `query` | pesquisa resultados web via RSS do Bing; páginas devem ser abertas antes de citar |
| `browse_url` | `url` | página via navegador **headless** (Edge/Chrome; fallback `fetch_url`) |
| `mcp__servidor__*` | conforme o servidor MCP | ferramentas, resources e prompts MCP |

Limites: a saída de comandos é drenada continuamente, retendo 100 KiB de bytes
do início e do final, mais um marcador quando truncada. A representação em texto
ou JSON pode ter tamanho diferente após decodificação e escape. Resultados de
ferramenta são exibidos até 20 KB no cartão.
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
Essa confirmação também prevalece sobre permissões persistentes e da sessão.

Para verificar se um caminho pertence ao projeto, o aplicativo resolve links
simbólicos e o ancestral existente de arquivos novos. Essa verificação orienta
as aprovações: não é uma sandbox do sistema operacional e não impede mudanças
nos caminhos entre a verificação e a execução. A lista de comandos perigosos é
limitada a padrões conhecidos e não detecta todos os comandos destrutivos.

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

## Plugins MCP

Em **Configurações → Plugins MCP**, adicione um nome, executável e argumentos.
O aplicativo inicia o executável ao confirmar. É possível ativar/desativar
servidores e consultar estado e número de ferramentas. A configuração é gravada
em `mcp.json`, ao lado de `config.ini`; campos avançados `env` e `cwd` são
preservados ao ativar/desativar um servidor. Nomes novos usam letras, números e `_`.

Cada entrada aceita `disabled: true` e `timeoutMs` (padrão: 60.000 ms; entre
1.000 e 600.000 ms). A inicialização espera até 15 segundos. Falhas e encerramento
devolvem erro à chamada pendente, em vez de deixar a conversa esperando sem fim.
O cliente não recarrega servidores enquanto houver chamadas pendentes.

Plugins Cordis do Harness não são compatíveis diretamente com MCP stdio.
