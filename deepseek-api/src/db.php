<?php
declare(strict_types=1);

final class Db
{
    private PDO $pdo;

    public function __construct(array $config)
    {
        $dsn = sprintf(
            'mysql:host=%s;port=%d;dbname=%s;charset=utf8mb4',
            $config['host'] ?? '127.0.0.1',
            (int)($config['port'] ?? 3306),
            $config['name'] ?? 'deepseek_chat'
        );
        $this->pdo = new PDO($dsn, $config['user'] ?? '', $config['password'] ?? '', [
            PDO::ATTR_ERRMODE => PDO::ERRMODE_EXCEPTION,
            PDO::ATTR_DEFAULT_FETCH_MODE => PDO::FETCH_ASSOC,
            PDO::ATTR_EMULATE_PREPARES => false,
        ]);
    }

    public function pdo(): PDO
    {
        return $this->pdo;
    }

    public function q(string $sql, array $params = []): PDOStatement
    {
        $stmt = $this->pdo->prepare($sql);
        $stmt->execute($params);
        return $stmt;
    }

    public function one(string $sql, array $params = []): ?array
    {
        $row = $this->q($sql, $params)->fetch();
        return $row === false ? null : $row;
    }

    public function all(string $sql, array $params = []): array
    {
        return $this->q($sql, $params)->fetchAll();
    }

    public function id(): int
    {
        return (int)$this->pdo->lastInsertId();
    }
}
