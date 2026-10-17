<?php
declare(strict_types=1);

function project_out(array $row): array
{
    return [
        'id' => (int)$row['id'],
        'user_id' => (int)$row['user_id'],
        'name' => $row['name'],
        'description' => $row['description'],
        'workdir' => $row['workdir'],
        'system_prompt' => $row['system_prompt'],
        'default_model' => $row['default_model'],
        'default_effort' => $row['default_effort'],
        'permission_mode' => $row['permission_mode'] ?? 'ask',
        'created_at' => $row['created_at'],
        'updated_at' => $row['updated_at'],
    ];
}

function parse_permission_mode($value, string $fallback = 'ask'): string
{
    $mode = (string)$value;
    if (!in_array($mode, ['ask', 'auto', 'full'], true)) {
        return $fallback;
    }
    return $mode;
}

function conversation_out(array $row): array
{
    return [
        'id' => (int)$row['id'],
        'user_id' => (int)$row['user_id'],
        'project_id' => $row['project_id'] !== null ? (int)$row['project_id'] : 0,
        'title' => $row['title'],
        'model' => $row['model'],
        'effort' => $row['effort'],
        'parent_id' => isset($row['parent_id']) ? (int)$row['parent_id'] : 0,
        'fork_message_id' => isset($row['fork_message_id']) ? (int)$row['fork_message_id'] : 0,
        'created_at' => $row['created_at'],
        'updated_at' => $row['updated_at'],
    ];
}

function message_out(array $row): array
{
    $attachments = [];
    $sources = [];
    if (!empty($row['attachments_json'])) {
        $decoded = json_decode((string)$row['attachments_json'], true);
        if (is_array($decoded)) {
            $attachments = $decoded;
        }
    }
    if (!empty($row['sources_json'])) {
        $decoded = json_decode((string)$row['sources_json'], true);
        if (is_array($decoded)) {
            $sources = $decoded;
        }
    }
    return [
        'id' => (int)$row['id'],
        'conversation_id' => (int)$row['conversation_id'],
        'role' => $row['role'],
        'content' => $row['content'],
        'reasoning' => $row['reasoning'],
        'model' => $row['model'],
        'attachments' => $attachments,
        'sources' => $sources,
        'prompt_tokens' => (int)$row['prompt_tokens'],
        'completion_tokens' => (int)$row['completion_tokens'],
        'cached_tokens' => (int)$row['cached_tokens'],
        'created_at' => $row['created_at'],
    ];
}

function project_owned(Db $db, int $id, int $userId): array
{
    $row = $db->one('SELECT * FROM projects WHERE id = ? AND user_id = ?', [$id, $userId]);
    if (!$row) {
        fail(404, 'Projeto não encontrado.');
    }
    return $row;
}

function conversation_owned(Db $db, int $id, int $userId): array
{
    $row = $db->one('SELECT * FROM conversations WHERE id = ? AND user_id = ?',
                    [$id, $userId]);
    if (!$row) {
        fail(404, 'Conversa não encontrada.');
    }
    return $row;
}

function command_owned(Db $db, int $id, int $userId): array
{
    $row = $db->one(
        'SELECT c.* FROM commands c'
        . ' LEFT JOIN projects p ON p.id = c.project_id'
        . ' WHERE c.id = ? AND (c.project_id IS NULL OR p.user_id = ?)',
        [$id, $userId]
    );
    if (!$row) {
        fail(404, 'Comando não encontrado.');
    }
    return $row;
}

function handle_routes(Db $db, array $config, array $user, string $method,
                      string $path, array $body): never
{
    if ($path === '/' || $path === '') {
        json_out([
            'name' => 'DeepSeek Chat API',
            'version' => '1.0.0',
            'endpoints' => [
                'POST /auth/register', 'POST /auth/login', 'POST /auth/logout',
                'GET /me', 'GET /models', 'GET /balance', 'POST /chat/stream',
                'GET|POST /projects', 'PUT|DELETE /projects/{id}',
                'GET /conversations', 'POST /conversations',
                'PUT|DELETE /conversations/{id}',
                'POST /conversations/{id}/truncate',
                'POST /conversations/{id}/branch',
                'GET|POST /project-memories', 'PUT|DELETE /project-memories/{id}',
                'GET /search',
                'GET /messages', 'POST /messages',
                'GET|POST /commands', 'PUT|DELETE /commands/{id}',
                'GET|POST /project-items', 'DELETE /project-items/{id}',
                'GET /activities', 'GET /usage',
            ],
        ]);
    }

    if ($path === '/me' && $method === 'GET') {
        json_out(['user' => $user]);
    }

    if ($path === '/me' && $method === 'PUT') {
        $name = trim((string)($body['display_name'] ?? ''));
        if ($name === '') {
            fail(422, 'Nome inválido.');
        }
        $db->q('UPDATE users SET display_name = ? WHERE id = ?', [$name, $user['id']]);
        $db->q('INSERT INTO activities (user_id, action, details) VALUES (?, ?, ?)',
               [$user['id'], 'profile_update', 'Nome: ' . $name]);
        json_out(['ok' => true, 'display_name' => $name]);
    }

    if ($path === '/me/password' && $method === 'PUT') {
        $oldPassword = (string)($body['old_password'] ?? '');
        $newPassword = (string)($body['new_password'] ?? '');
        if (strlen($newPassword) < 4) {
            fail(422, 'A nova senha precisa ter pelo menos 4 caracteres.');
        }
        $row = $db->one('SELECT password_hash, salt FROM users WHERE id = ?',
                        [$user['id']]);
        if (!$row || !auth_verify_password($row, $oldPassword)) {
            fail(403, 'Senha atual incorreta.');
        }
        $db->q('UPDATE users SET password_hash = ? WHERE id = ?', [
            password_hash($newPassword, PASSWORD_BCRYPT),
            $user['id'],
        ]);
        $db->q('INSERT INTO activities (user_id, action, details) VALUES (?, ?, ?)',
               [$user['id'], 'password_change', 'Senha alterada']);
        json_out(['ok' => true]);
    }

    if ($path === '/models' && $method === 'GET') {
        deepseek_models($db, $config, $user);
    }

    if ($path === '/balance' && $method === 'GET') {
        deepseek_balance($db, $config, $user);
    }

    if ($path === '/chat/stream' && $method === 'POST') {
        deepseek_chat_stream($db, $config, $user, $body);
    }

    if ($path === '/projects' && $method === 'GET') {
        $rows = $db->all(
            'SELECT * FROM projects WHERE user_id = ? ORDER BY updated_at DESC, id DESC',
            [$user['id']]
        );
        json_out(array_map('project_out', $rows));
    }

    if ($path === '/projects' && $method === 'POST') {
        $name = trim((string)($body['name'] ?? ''));
        if ($name === '') {
            fail(422, 'Informe um nome para o projeto.');
        }
        $db->q(
            'INSERT INTO projects (user_id, name, description, workdir, system_prompt,'
            . ' default_model, default_effort, permission_mode) VALUES (?, ?, ?, ?, ?, ?, ?, ?)',
            [
                $user['id'], $name,
                (string)($body['description'] ?? ''),
                (string)($body['workdir'] ?? ''),
                (string)($body['system_prompt'] ?? ''),
                (string)($body['default_model'] ?? 'deepseek-flash'),
                (string)($body['default_effort'] ?? 'high'),
                parse_permission_mode($body['permission_mode'] ?? 'ask'),
            ]
        );
        $id = $db->id();
        $db->q('INSERT INTO activities (user_id, action, details) VALUES (?, ?, ?)',
               [$user['id'], 'project_create', $name]);
        json_out(project_out(project_owned($db, $id, (int)$user['id'])), 201);
    }

    if (preg_match('#^/projects/(\d+)$#', $path, $m)) {
        $id = (int)$m[1];
        $project = project_owned($db, $id, (int)$user['id']);

        if ($method === 'PUT') {
            $name = trim((string)($body['name'] ?? $project['name']));
            if ($name === '') {
                fail(422, 'Informe um nome para o projeto.');
            }
            $db->q(
                'UPDATE projects SET name = ?, description = ?, workdir = ?,'
                . ' system_prompt = ?, default_model = ?, default_effort = ?,'
                . ' permission_mode = ? WHERE id = ?',
                [
                    $name,
                    (string)($body['description'] ?? $project['description']),
                    (string)($body['workdir'] ?? $project['workdir']),
                    (string)($body['system_prompt'] ?? $project['system_prompt']),
                    (string)($body['default_model'] ?? $project['default_model']),
                    (string)($body['default_effort'] ?? $project['default_effort']),
                    parse_permission_mode(
                        $body['permission_mode'] ?? ($project['permission_mode'] ?? 'ask')
                    ),
                    $id,
                ]
            );
            $db->q('INSERT INTO activities (user_id, action, details) VALUES (?, ?, ?)',
                   [$user['id'], 'project_update', $name]);
            json_out(project_out(project_owned($db, $id, (int)$user['id'])));
        }

        if ($method === 'DELETE') {
            $db->q('DELETE FROM projects WHERE id = ?', [$id]);
            $db->q('INSERT INTO activities (user_id, action, details) VALUES (?, ?, ?)',
                   [$user['id'], 'project_delete', (string)$project['name']]);
            json_out(['ok' => true]);
        }

        fail(405, 'Método não permitido.');
    }

    if ($path === '/conversations' && $method === 'GET') {
        $projectId = isset($_GET['project_id']) ? (int)$_GET['project_id'] : -1;
        if ($projectId === 0) {
            $rows = $db->all(
                'SELECT * FROM conversations WHERE user_id = ? AND project_id IS NULL'
                . ' ORDER BY updated_at DESC, id DESC',
                [$user['id']]
            );
        } elseif ($projectId > 0) {
            $rows = $db->all(
                'SELECT * FROM conversations WHERE user_id = ? AND project_id = ?'
                . ' ORDER BY updated_at DESC, id DESC',
                [$user['id'], $projectId]
            );
        } else {
            $rows = $db->all(
                'SELECT * FROM conversations WHERE user_id = ?'
                . ' ORDER BY updated_at DESC, id DESC',
                [$user['id']]
            );
        }
        json_out(array_map('conversation_out', $rows));
    }

    if ($path === '/conversations' && $method === 'POST') {
        $title = trim((string)($body['title'] ?? 'Nova conversa'));
        $projectId = (int)($body['project_id'] ?? 0);
        if ($projectId > 0) {
            project_owned($db, $projectId, (int)$user['id']);
        }
        $db->q(
            'INSERT INTO conversations (user_id, project_id, title, model, effort)'
            . ' VALUES (?, ?, ?, ?, ?)',
            [
                $user['id'], $projectId > 0 ? $projectId : null, $title,
                (string)($body['model'] ?? 'deepseek-flash'),
                (string)($body['effort'] ?? 'high'),
            ]
        );
        json_out(conversation_out(
            conversation_owned($db, $db->id(), (int)$user['id'])
        ), 201);
    }

    if (preg_match('#^/conversations/(\d+)/branch$#', $path, $m)) {
        if ($method !== 'POST') {
            fail(405, 'Método não permitido.');
        }
        $sourceId = (int)$m[1];
        $source = conversation_owned($db, $sourceId, (int)$user['id']);
        $throughId = (int)($body['through_message_id'] ?? 0);
        if ($throughId < -1) {
            fail(422, 'Mensagem da ramificação inválida.');
        }
        if ($throughId > 0) {
            $message = $db->one('SELECT id FROM messages WHERE id = ? AND conversation_id = ?',
                                [$throughId, $sourceId]);
            if (!$message) {
                fail(422, 'Mensagem da ramificação não encontrada.');
            }
        }
        $defaultTitle = mb_substr((string)$source['title'], 0, 238) . ' (ramificação)';
        $title = trim((string)($body['title'] ?? $defaultTitle));
        if ($title === '' || mb_strlen($title) > 255) {
            fail(422, 'Título inválido.');
        }
        $db->pdo()->beginTransaction();
        try {
            $db->q('INSERT INTO conversations (user_id, project_id, title, model, effort, parent_id, fork_message_id)'
                   . ' VALUES (?, ?, ?, ?, ?, ?, ?)',
                   [$user['id'], $source['project_id'], $title, $source['model'],
                    $source['effort'], $sourceId, $throughId > 0 ? $throughId : null]);
            $newId = $db->id();
            $db->q('INSERT INTO messages (conversation_id, role, content, reasoning, attachments_json,'
                   . ' sources_json, model, prompt_tokens, completion_tokens, cached_tokens, created_at)'
                   . ' SELECT ?, role, content, reasoning, attachments_json, sources_json, model,'
                   . ' 0, 0, 0, created_at FROM messages'
                   . ' WHERE conversation_id = ?'
                   . ($throughId > 0 ? ' AND id <= ?' : ($throughId === -1 ? ' AND 1 = 0' : ''))
                   . ' ORDER BY id ASC',
                   $throughId > 0 ? [$newId, $sourceId, $throughId] : [$newId, $sourceId]);
            $db->pdo()->commit();
        } catch (Throwable $e) {
            $db->pdo()->rollBack();
            throw $e;
        }
        json_out(conversation_out(conversation_owned($db, $newId, (int)$user['id'])), 201);
    }

    if ($path === '/project-memories') {
        $projectId = (int)(($method === 'GET' ? $_GET : $body)['project_id'] ?? 0);
        project_owned($db, $projectId, (int)$user['id']);
        if ($method === 'GET') {
            json_out($db->all('SELECT * FROM project_memories WHERE project_id = ? ORDER BY updated_at DESC, id DESC',
                              [$projectId]));
        }
        if ($method === 'POST') {
            $content = trim((string)($body['content'] ?? ''));
            if ($content === '' || mb_strlen($content) > 2000) {
                fail(422, 'Memória deve conter de 1 a 2000 caracteres.');
            }
            $db->q('INSERT INTO project_memories (project_id, content) VALUES (?, ?)',
                   [$projectId, $content]);
            json_out($db->one('SELECT * FROM project_memories WHERE id = ?', [$db->id()]), 201);
        }
        fail(405, 'Método não permitido.');
    }

    if (preg_match('#^/project-memories/(\d+)$#', $path, $m)) {
        $id = (int)$m[1];
        $memory = $db->one('SELECT pm.* FROM project_memories pm JOIN projects p ON p.id = pm.project_id'
                           . ' WHERE pm.id = ? AND p.user_id = ?', [$id, $user['id']]);
        if (!$memory) {
            fail(404, 'Memória não encontrada.');
        }
        if ($method === 'PUT') {
            $content = trim((string)($body['content'] ?? ''));
            if ($content === '' || mb_strlen($content) > 2000) {
                fail(422, 'Memória deve conter de 1 a 2000 caracteres.');
            }
            $db->q('UPDATE project_memories SET content = ? WHERE id = ?', [$content, $id]);
            json_out($db->one('SELECT * FROM project_memories WHERE id = ?', [$id]));
        }
        if ($method === 'DELETE') {
            $db->q('DELETE FROM project_memories WHERE id = ?', [$id]);
            json_out(['ok' => true]);
        }
        fail(405, 'Método não permitido.');
    }

    if (preg_match('#^/conversations/(\d+)$#', $path, $m)) {
        $id = (int)$m[1];
        $conversation = conversation_owned($db, $id, (int)$user['id']);

        if ($method === 'PUT') {
            $title = trim((string)($body['title'] ?? $conversation['title']));
            if ($title === '') {
                fail(422, 'Título inválido.');
            }
            $hasProject = array_key_exists('project_id', $body);
            $projectId = $hasProject ? max(0, (int)$body['project_id']) : 0;
            if ($hasProject && $projectId > 0) {
                project_owned($db, $projectId, (int)$user['id']);
            }
            $db->q(
                'UPDATE conversations SET title = ?, model = ?, effort = ?'
                . ($hasProject ? ', project_id = ?' : '') . ' WHERE id = ?',
                $hasProject
                    ? [
                        $title,
                        (string)($body['model'] ?? $conversation['model']),
                        (string)($body['effort'] ?? $conversation['effort']),
                        $projectId > 0 ? $projectId : null,
                        $id,
                    ]
                    : [
                        $title,
                        (string)($body['model'] ?? $conversation['model']),
                        (string)($body['effort'] ?? $conversation['effort']),
                        $id,
                    ]
            );
            if ($hasProject) {
                $db->q('INSERT INTO activities (user_id, action, details) VALUES (?, ?, ?)',
                       [$user['id'], 'conversation_move',
                        'Conversa: ' . $title . ' -> projeto #' . $projectId]);
            }
            json_out(conversation_out(
                conversation_owned($db, $id, (int)$user['id'])
            ));
        }

        if ($method === 'DELETE') {
            $db->q('UPDATE conversations SET parent_id = NULL WHERE parent_id = ?', [$id]);
            $db->q('DELETE FROM conversations WHERE id = ?', [$id]);
            $db->q('INSERT INTO activities (user_id, action, details) VALUES (?, ?, ?)',
                   [$user['id'], 'delete_conversation', (string)$conversation['title']]);
            json_out(['ok' => true]);
        }

        fail(405, 'Método não permitido.');
    }

    if (preg_match('#^/conversations/(\d+)/truncate$#', $path, $m)) {
        $id = (int)$m[1];
        conversation_owned($db, $id, (int)$user['id']);
        if ($method !== 'POST') {
            fail(405, 'Método não permitido.');
        }
        $fromId = (int)($body['from_message_id'] ?? 0);
        if ($fromId <= 0) {
            fail(422, 'from_message_id obrigatório.');
        }
        $db->q('DELETE FROM messages WHERE conversation_id = ? AND id >= ?',
               [$id, $fromId]);
        $db->q('INSERT INTO activities (user_id, action, details) VALUES (?, ?, ?)',
               [$user['id'], 'conversation_truncate',
                'Conversa #' . $id . ' a partir da mensagem #' . $fromId]);
        json_out(['ok' => true]);
    }

    if ($path === '/search' && $method === 'GET') {
        $query = trim((string)($_GET['q'] ?? ''));
        if (mb_strlen($query) < 2) {
            json_out([]);
        }
        $like = '%' . str_replace(['%', '_'], ['\%', '\_'], $query) . '%';
        $rows = $db->all(
            'SELECT m.id, m.conversation_id, m.role, m.content, m.created_at, c.title'
            . ' FROM messages m JOIN conversations c ON c.id = m.conversation_id'
            . ' WHERE c.user_id = ? AND m.content LIKE ?'
            . ' ORDER BY m.id DESC LIMIT 50',
            [$user['id'], $like]
        );
        $out = [];
        foreach ($rows as $row) {
            $content = (string)$row['content'];
            $pos = mb_stripos($content, $query);
            $start = $pos !== false ? max(0, $pos - 60) : 0;
            $snippet = mb_substr($content, $start, 200);
            if ($start > 0) {
                $snippet = '...' . $snippet;
            }
            $out[] = [
                'conversation_id' => (int)$row['conversation_id'],
                'title' => $row['title'],
                'role' => $row['role'],
                'snippet' => $snippet,
                'created_at' => $row['created_at'],
            ];
        }
        json_out($out);
    }

    if ($path === '/messages' && $method === 'GET') {
        $conversationId = (int)($_GET['conversation_id'] ?? 0);
        if ($conversationId <= 0) {
            fail(422, 'conversation_id obrigatório.');
        }
        conversation_owned($db, $conversationId, (int)$user['id']);
        $rows = $db->all(
            'SELECT * FROM messages WHERE conversation_id = ? ORDER BY id ASC',
            [$conversationId]
        );
        json_out(array_map('message_out', $rows));
    }

    if ($path === '/messages' && $method === 'POST') {
        $conversationId = (int)($body['conversation_id'] ?? 0);
        if ($conversationId <= 0) {
            fail(422, 'conversation_id obrigatório.');
        }
        conversation_owned($db, $conversationId, (int)$user['id']);
        $role = (string)($body['role'] ?? 'user');
        if (!in_array($role, ['user', 'assistant', 'system'], true)) {
            fail(422, 'Papel inválido.');
        }
        $attachments = $body['attachments'] ?? null;
        $db->q(
            'INSERT INTO messages (conversation_id, role, content, reasoning, model,'
            . ' attachments_json) VALUES (?, ?, ?, ?, ?, ?)',
            [
                $conversationId, $role,
                (string)($body['content'] ?? ''),
                (string)($body['reasoning'] ?? ''),
                (string)($body['model'] ?? ''),
                $attachments !== null
                    ? json_encode($attachments, JSON_UNESCAPED_UNICODE) : null,
            ]
        );
        json_out(message_out(
            $db->one('SELECT * FROM messages WHERE id = ?', [$db->id()])
        ), 201);
    }

    if ($path === '/commands' && $method === 'GET') {
        $projectId = (int)($_GET['project_id'] ?? 0);
        if ($projectId > 0) {
            project_owned($db, $projectId, (int)$user['id']);
            $rows = $db->all(
                'SELECT * FROM commands WHERE project_id = ? ORDER BY name ASC',
                [$projectId]
            );
        } else {
            $rows = $db->all(
                'SELECT * FROM commands WHERE project_id IS NULL ORDER BY name ASC'
            );
        }
        json_out(array_map(static fn(array $r): array => [
            'id' => (int)$r['id'],
            'project_id' => $r['project_id'] !== null ? (int)$r['project_id'] : 0,
            'name' => $r['name'],
            'prompt' => $r['prompt'],
        ], $rows));
    }

    if ($path === '/commands' && $method === 'POST') {
        $name = trim((string)($body['name'] ?? ''));
        $prompt = (string)($body['prompt'] ?? '');
        if ($name === '' || $prompt === '') {
            fail(422, 'Nome e prompt são obrigatórios.');
        }
        $projectId = (int)($body['project_id'] ?? 0);
        if ($projectId > 0) {
            project_owned($db, $projectId, (int)$user['id']);
        }
        $db->q(
            'INSERT INTO commands (project_id, name, prompt) VALUES (?, ?, ?)',
            [$projectId > 0 ? $projectId : null, $name, $prompt]
        );
        json_out(['id' => $db->id(), 'project_id' => $projectId,
                  'name' => $name, 'prompt' => $prompt], 201);
    }

    if (preg_match('#^/commands/(\d+)$#', $path, $m)) {
        $id = (int)$m[1];
        command_owned($db, $id, (int)$user['id']);
        if ($method === 'PUT') {
            $db->q('UPDATE commands SET name = ?, prompt = ? WHERE id = ?',
                   [(string)($body['name'] ?? ''), (string)($body['prompt'] ?? ''), $id]);
            json_out(['ok' => true]);
        }
        if ($method === 'DELETE') {
            $db->q('DELETE FROM commands WHERE id = ?', [$id]);
            json_out(['ok' => true]);
        }
        fail(405, 'Método não permitido.');
    }

    if ($path === '/project-items' && $method === 'GET') {
        $projectId = (int)($_GET['project_id'] ?? 0);
        if ($projectId <= 0) {
            fail(422, 'project_id obrigatório.');
        }
        project_owned($db, $projectId, (int)$user['id']);
        $rows = $db->all(
            'SELECT id, project_id, kind, name, path, mime, size, created_at'
            . ' FROM project_items WHERE project_id = ? ORDER BY id DESC',
            [$projectId]
        );
        json_out(array_map(static fn(array $r): array => [
            'id' => (int)$r['id'],
            'project_id' => (int)$r['project_id'],
            'kind' => $r['kind'],
            'name' => $r['name'],
            'path' => $r['path'],
            'mime' => $r['mime'],
            'size' => (int)$r['size'],
            'created_at' => $r['created_at'],
        ], $rows));
    }

    if ($path === '/project-items' && $method === 'POST') {
        $projectId = (int)($body['project_id'] ?? 0);
        if ($projectId <= 0) {
            fail(422, 'project_id obrigatório.');
        }
        project_owned($db, $projectId, (int)$user['id']);
        $db->q(
            'INSERT INTO project_items (project_id, kind, name, path, mime, size)'
            . ' VALUES (?, ?, ?, ?, ?, ?)',
            [
                $projectId,
                in_array($body['kind'] ?? 'file', ['file', 'image', 'note', 'folder'], true)
                    ? $body['kind'] : 'file',
                (string)($body['name'] ?? ''),
                (string)($body['path'] ?? ''),
                (string)($body['mime'] ?? ''),
                (int)($body['size'] ?? 0),
            ]
        );
        json_out(['id' => $db->id()], 201);
    }

    if (preg_match('#^/project-items/(\d+)$#', $path, $m) && $method === 'DELETE') {
        $id = (int)$m[1];
        $item = $db->one(
            'SELECT i.id FROM project_items i JOIN projects p ON p.id = i.project_id'
            . ' WHERE i.id = ? AND p.user_id = ?',
            [$id, $user['id']]
        );
        if (!$item) {
            fail(404, 'Item não encontrado.');
        }
        $db->q('DELETE FROM project_items WHERE id = ?', [$id]);
        json_out(['ok' => true]);
    }

    if ($path === '/activities' && $method === 'POST') {
        $action = trim((string)($body['action'] ?? ''));
        $allowed = ['file_write', 'attachment_add', 'project_attachment',
                    'command_use', 'dictation'];
        if (!in_array($action, $allowed, true)) {
            fail(422, 'Ação não permitida.');
        }
        $details = trim((string)($body['details'] ?? ''));
        $db->q('INSERT INTO activities (user_id, action, details) VALUES (?, ?, ?)',
               [$user['id'], $action, mb_substr($details, 0, 500)]);
        json_out(['ok' => true], 201);
    }

    if ($path === '/activities' && $method === 'GET') {
        $limit = min(500, max(1, (int)($_GET['limit'] ?? 300)));
        $rows = $db->all(
            'SELECT action, details, created_at FROM activities'
            . ' WHERE user_id = ? ORDER BY id DESC LIMIT ' . $limit,
            [$user['id']]
        );
        json_out($rows);
    }

    if ($path === '/usage' && $method === 'GET') {
        $conversationId = (int)($_GET['conversation_id'] ?? 0);
        $conversation = ['prompt' => 0, 'completion' => 0, 'cached' => 0, 'messages' => 0];
        if ($conversationId > 0) {
            conversation_owned($db, $conversationId, (int)$user['id']);
            $row = $db->one(
                'SELECT COALESCE(SUM(prompt_tokens),0) p, COALESCE(SUM(completion_tokens),0) c,'
                . ' COALESCE(SUM(cached_tokens),0) ca, COUNT(*) n'
                . ' FROM messages WHERE conversation_id = ?',
                [$conversationId]
            );
            $conversation = ['prompt' => (int)$row['p'], 'completion' => (int)$row['c'],
                             'cached' => (int)$row['ca'], 'messages' => (int)$row['n']];
        }
        $byModel = $db->all(
            'SELECT m.model, COALESCE(SUM(m.prompt_tokens),0) p,'
            . ' COALESCE(SUM(m.completion_tokens),0) c, COALESCE(SUM(m.cached_tokens),0) ca,'
            . ' COUNT(*) n FROM messages m JOIN conversations cv ON cv.id = m.conversation_id'
            . ' WHERE cv.user_id = ? GROUP BY m.model',
            [$user['id']]
        );
        json_out([
            'conversation' => $conversation,
            'by_model' => array_map(static fn(array $r): array => [
                'model' => $r['model'],
                'prompt' => (int)$r['p'],
                'completion' => (int)$r['c'],
                'cached' => (int)$r['ca'],
                'messages' => (int)$r['n'],
            ], $byModel),
        ]);
    }

    fail(404, 'Rota não encontrada: ' . $method . ' ' . $path);
}
