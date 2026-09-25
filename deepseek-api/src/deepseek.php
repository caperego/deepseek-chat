<?php
declare(strict_types=1);

function deepseek_provider(array $config, string $provider): array
{
    $default = [
        'name' => 'deepseek',
        'base_url' => (string)($config['deepseek']['base_url'] ?? 'https://api.deepseek.com'),
        'api_key' => (string)($config['deepseek']['api_key'] ?? ''),
        'effort' => true,
    ];
    $providers = $config['providers'] ?? null;
    if ($provider === '' || !is_array($providers) || !isset($providers[$provider])
        || !is_array($providers[$provider])) {
        return $default;
    }
    $entry = $providers[$provider];
    return [
        'name' => $provider,
        'base_url' => (string)($entry['base_url'] ?? $default['base_url']),
        'api_key' => (string)($entry['api_key'] ?? $default['api_key']),
        'effort' => (bool)($entry['effort'] ?? false),
        'images' => (bool)($entry['images'] ?? false),
        'context_window' => (int)($entry['context_window'] ?? 0),
    ];
}

function deepseek_split_model(string $model, array $config): array
{
    $providers = $config['providers'] ?? null;
    $pos = strpos($model, '/');
    if ($pos !== false && is_array($providers)) {
        $prefix = substr($model, 0, $pos);
        if (isset($providers[$prefix]) && is_array($providers[$prefix])) {
            return [$prefix, substr($model, $pos + 1)];
        }
    }
    return ['', $model];
}

function deepseek_request_to(array $provider, string $path, ?array $body = null,
                            bool $stream = false): array
{
    $url = rtrim((string)$provider['base_url'], '/') . $path;
    $headers = [
        'Authorization: Bearer ' . $provider['api_key'],
        'Content-Type: application/json',
    ];

    $ch = curl_init($url);
    $options = [
        CURLOPT_RETURNTRANSFER => true,
        CURLOPT_TIMEOUT => $stream ? 600 : 60,
        CURLOPT_HTTPHEADER => $headers,
    ];
    if ($body !== null) {
        $options[CURLOPT_POST] = true;
        $options[CURLOPT_POSTFIELDS] = json_encode($body, JSON_UNESCAPED_UNICODE);
    }
    curl_setopt_array($ch, $options);

    $response = curl_exec($ch);
    $status = (int)curl_getinfo($ch, CURLINFO_HTTP_CODE);
    $error = curl_error($ch);
    curl_close($ch);

    if ($response === false) {
        fail(502, 'Falha ao contatar a API ' . $provider['name'] . ': ' . $error);
    }
    return ['status' => $status, 'body' => (string)$response];
}

function deepseek_request(array $config, string $path, ?array $body = null,
                          bool $stream = false): array
{
    return deepseek_request_to(deepseek_provider($config, ''), $path, $body, $stream);
}

function deepseek_balance(Db $db, array $config, array $user): never
{
    $provider = deepseek_provider($config, '');
    $result = deepseek_request_to($provider, '/user/balance');
    http_response_code($result['status'] ?: 502);
    header('Content-Type: application/json; charset=utf-8');
    echo $result['body'];
    exit;
}

function deepseek_models(Db $db, array $config, array $user): never
{
    $providers = $config['providers'] ?? null;
    if (!is_array($providers) || $providers === []) {
        $result = deepseek_request($config, '/models');
        http_response_code($result['status'] ?: 502);
        header('Content-Type: application/json; charset=utf-8');
        echo $result['body'];
        exit;
    }

    $models = [];
    $default = deepseek_provider($config, '');
    $result = deepseek_request_to($default, '/models');
    if ($result['status'] === 200) {
        $decoded = json_decode($result['body'], true);
        foreach (($decoded['data'] ?? []) as $model) {
            if (is_array($model)) {
                $models[] = $model;
            }
        }
    }

    foreach ($providers as $name => $entry) {
        if (!is_array($entry)) {
            continue;
        }
        $provider = deepseek_provider($config, (string)$name);
        $result = deepseek_request_to($provider, '/models');
        if ($result['status'] !== 200) {
            continue;
        }
        $decoded = json_decode($result['body'], true);
        foreach (($decoded['data'] ?? []) as $model) {
            if (!is_array($model)) {
                continue;
            }
            $id = (string)($model['id'] ?? '');
            if ($id === '') {
                continue;
            }
            $models[] = [
                'id' => $name . '/' . $id,
                'name' => $name . ': ' . $id,
                'context_window' => $provider['context_window'] ?? 0,
                'input_modalities' => !empty($provider['images'])
                    ? ['text', 'image'] : ['text'],
                'effort' => ['supported_levels' => [], 'default_level' => ''],
            ];
        }
    }

    header('Content-Type: application/json; charset=utf-8');
    echo json_encode(['object' => 'list', 'data' => $models], JSON_UNESCAPED_UNICODE);
    exit;
}

function deepseek_chat_stream(Db $db, array $config, array $user, array $body): never
{
    set_time_limit(0);
    ignore_user_abort(true);

    $conversationId = (int)($body['conversation_id'] ?? 0);
    $projectId = (int)($body['project_id'] ?? 0);
    $requestedModel = trim((string)($body['model'] ?? 'deepseek-flash'));
    [$providerName, $model] = deepseek_split_model($requestedModel, $config);
    $provider = deepseek_provider($config, $providerName);
    $effort = trim((string)($body['effort'] ?? ''));
    $messages = $body['messages'] ?? [];
    $title = trim((string)($body['title'] ?? ''));
    $persist = !array_key_exists('persist', $body) || (bool)$body['persist'];
    $persistUser = !array_key_exists('persist_user', $body) || (bool)$body['persist_user'];

    if (!is_array($messages) || $messages === []) {
        fail(422, 'A lista de mensagens está vazia.');
    }

    if ($conversationId > 0) {
        $conversation = $db->one(
            'SELECT id FROM conversations WHERE id = ? AND user_id = ?',
            [$conversationId, $user['id']]
        );
        if (!$conversation) {
            fail(404, 'Conversa não encontrada.');
        }
        if ($persist) {
            if ($projectId > 0) {
                $db->q('UPDATE conversations SET project_id = ? WHERE id = ?',
                       [$projectId, $conversationId]);
            }
            $db->q('UPDATE conversations SET model = ?, effort = ? WHERE id = ?',
                   [$model, $effort, $conversationId]);
        }
    } else {
        if ($title === '') {
            $title = 'Nova conversa';
        }
        if ($persist) {
            $db->q(
                'INSERT INTO conversations (user_id, project_id, title, model, effort) VALUES (?, ?, ?, ?, ?)',
                [$user['id'], $projectId > 0 ? $projectId : null, $title, $model, $effort]
            );
            $conversationId = $db->id();
        }
    }

    $lastMessage = $messages[array_key_last($messages)] ?? null;
    if ($persist && $persistUser && is_array($lastMessage)
        && ($lastMessage['role'] ?? '') === 'user') {
        $content = $lastMessage['content'] ?? '';
        if (!is_string($content)) {
            $parts = [];
            foreach ((array)$content as $part) {
                if (!is_array($part)) {
                    continue;
                }
                $type = (string)($part['type'] ?? '');
                if ($type === 'text') {
                    $parts[] = (string)($part['text'] ?? '');
                } elseif ($type === 'image_url') {
                    $parts[] = '[imagem]';
                }
            }
            $content = trim(implode("\n\n", $parts));
        }
        if ($content !== '') {
            $attachments = $body['attachments'] ?? null;
            $db->q(
                'INSERT INTO messages (conversation_id, role, content, model, attachments_json)'
                . ' VALUES (?, "user", ?, ?, ?)',
                [
                    $conversationId, $content, $model,
                    is_array($attachments)
                        ? json_encode($attachments, JSON_UNESCAPED_UNICODE) : null,
                ]
            );
            $db->q(
                'INSERT INTO activities (user_id, action, details) VALUES (?, ?, ?)',
                [$user['id'], 'message', 'modelo=' . $requestedModel . ', ' . mb_strlen($content) . ' caracteres']
            );
        }
    }

    while (ob_get_level() > 0) {
        ob_end_clean();
    }
    header('Content-Type: text/event-stream; charset=utf-8');
    header('Cache-Control: no-cache');
    header('Connection: keep-alive');
    header('X-Accel-Buffering: no');
    echo 'event: meta' . "\n" . 'data: ' . json_encode(['conversation_id' => $conversationId]) . "\n\n";
    flush();

    $payload = [
        'model' => $model,
        'messages' => $messages,
        'stream' => true,
        'stream_options' => ['include_usage' => true],
    ];
    if ($effort !== '' && $provider['effort']) {
        $payload['effort'] = $effort;
    }
    if (isset($body['tools']) && is_array($body['tools']) && $body['tools'] !== []) {
        $payload['tools'] = $body['tools'];
        $payload['tool_choice'] = $body['tool_choice'] ?? 'auto';
    }

    $accumulated = '';
    $ch = curl_init(rtrim((string)$provider['base_url'], '/') . '/chat/completions');
    curl_setopt_array($ch, [
        CURLOPT_POST => true,
        CURLOPT_HTTPHEADER => [
            'Authorization: Bearer ' . $provider['api_key'],
            'Content-Type: application/json',
            'Accept: text/event-stream',
        ],
        CURLOPT_POSTFIELDS => json_encode($payload, JSON_UNESCAPED_UNICODE),
        CURLOPT_TIMEOUT => 600,
        CURLOPT_WRITEFUNCTION => function ($ch, string $chunk) use (&$accumulated): int {
            echo $chunk;
            flush();
            $accumulated .= $chunk;
            return strlen($chunk);
        },
    ]);

    $ok = curl_exec($ch);
    $error = curl_error($ch);
    curl_close($ch);

    if ($ok === false) {
        echo 'event: error' . "\n" . 'data: ' . json_encode(['error' => $error]) . "\n\n";
        flush();
        exit;
    }

    $content = '';
    $reasoning = '';
    $hasToolCalls = false;
    $promptTokens = 0;
    $completionTokens = 0;
    $cachedTokens = 0;

    foreach (explode("\n", $accumulated) as $line) {
        $line = trim($line);
        if (!str_starts_with($line, 'data:')) {
            continue;
        }
        $data = trim(substr($line, 5));
        if ($data === '' || $data === '[DONE]') {
            continue;
        }
        $obj = json_decode($data, true);
        if (!is_array($obj)) {
            continue;
        }
        if (isset($obj['usage']) && is_array($obj['usage'])) {
            $usage = $obj['usage'];
            $promptTokens = (int)($usage['prompt_tokens'] ?? 0);
            $completionTokens = (int)($usage['completion_tokens'] ?? 0);
            $cachedTokens = (int)($usage['prompt_tokens_details']['cached_tokens'] ?? 0);
            if ($cachedTokens === 0) {
                $cachedTokens = (int)($usage['prompt_cache_hit_tokens'] ?? 0);
            }
        }
        $delta = $obj['choices'][0]['delta'] ?? null;
        if (is_array($delta)) {
            if (isset($delta['content']) && is_string($delta['content'])) {
                $content .= $delta['content'];
            }
            if (isset($delta['reasoning_content']) && is_string($delta['reasoning_content'])) {
                $reasoning .= $delta['reasoning_content'];
            }
            if (isset($delta['tool_calls']) && is_array($delta['tool_calls'])
                && $delta['tool_calls'] !== []) {
                $hasToolCalls = true;
            }
        }
    }

    if ($persist && ($content !== '' || (!$hasToolCalls && $reasoning !== ''))) {
        $db->q(
            'INSERT INTO messages (conversation_id, role, content, reasoning, model,'
            . ' prompt_tokens, completion_tokens, cached_tokens)'
            . ' VALUES (?, "assistant", ?, ?, ?, ?, ?, ?)',
            [$conversationId, $content, $reasoning, $model,
             $promptTokens, $completionTokens, $cachedTokens]
        );
        $db->q('UPDATE conversations SET updated_at = NOW() WHERE id = ?', [$conversationId]);
        $db->q(
            'INSERT INTO activities (user_id, action, details) VALUES (?, ?, ?)',
            [$user['id'], 'response', 'modelo=' . $requestedModel . ', ' . mb_strlen($content) . ' caracteres']
        );
    }

    exit;
}
