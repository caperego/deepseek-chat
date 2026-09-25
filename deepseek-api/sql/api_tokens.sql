USE deepseek_chat;

CREATE TABLE IF NOT EXISTS api_tokens (
  id BIGINT AUTO_INCREMENT PRIMARY KEY,
  user_id BIGINT NOT NULL,
  token_hash CHAR(64) NOT NULL UNIQUE,
  created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
  expires_at DATETIME NOT NULL,
  last_used_at DATETIME NULL,
  CONSTRAINT fk_token_user FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE,
  INDEX idx_token_user (user_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE USER IF NOT EXISTS 'deepseek_api'@'localhost' IDENTIFIED BY 'TROQUE_ESTA_SENHA';
GRANT SELECT, INSERT, UPDATE, DELETE ON deepseek_chat.* TO 'deepseek_api'@'localhost';
FLUSH PRIVILEGES;
