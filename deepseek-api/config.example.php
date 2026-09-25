<?php
declare(strict_types=1);

return [
    'db' => [
        'host' => '127.0.0.1',
        'port' => 3306,
        'name' => 'deepseek_chat',
        'user' => 'deepseek_api',
        'password' => 'TROQUE_ESTA_SENHA',
    ],
    'deepseek' => [
        'api_key' => 'sk-...',
        'base_url' => 'https://api.deepseek.com',
    ],
    // Provedores adicionais (OpenAI-compatíveis). Os modelos aparecem no app
    // como "nome/modelo" e o pedido é roteado para o provedor correspondente.
    // 'effort' só é enviado quando true (DeepSeek); 'images' habilita imagem.
    'providers' => [
        // 'local' => [
        //     'base_url' => 'http://127.0.0.1:11434/v1',
        //     'api_key' => 'ollama',
        //     'effort' => false,
        //     'images' => false,
        //     'context_window' => 32000,
        // ],
        // 'openai' => [
        //     'base_url' => 'https://api.openai.com/v1',
        //     'api_key' => 'sk-...',
        //     'effort' => false,
        //     'images' => true,
        //     'context_window' => 128000,
        // ],
    ],
    'tokens' => [
        'ttl_days' => 30,
    ],
];
